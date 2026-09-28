// Copyright Hex Spire. All Rights Reserved.

#include "Commandlets/HexExportRunesCommandlet.h"
#include "Data/HexRuneTableRow.h"
#include "Runes/HexRuneLibrary.h"
#include "HexSpire.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	/** 枚举标识名（不是 DisplayName —— 中文导入会静默填成第 0 项） */
	template <typename TEnum>
	FString RuneEnumName(TEnum Value)
	{
		const UEnum* E = StaticEnum<TEnum>();
		if (!E)
		{
			return FString::FromInt(static_cast<int32>(Value));
		}
		return E->GetNameStringByValue(static_cast<int64>(Value));
	}

	/** CSV 字段转义（同卡牌导出器） */
	FString RuneCsv(const FString& In)
	{
		if (!In.Contains(TEXT(",")) && !In.Contains(TEXT("\"")) && !In.Contains(TEXT("\n")))
		{
			return In;
		}
		FString Out = In.Replace(TEXT("\""), TEXT("\"\""));
		return FString::Printf(TEXT("\"%s\""), *Out);
	}

	/** FName 数组 → ("a","b")。空数组 → 空串。（数组必须用括号字面量！） */
	FString RuneNames(const TArray<FName>& In)
	{
		if (In.Num() == 0)
		{
			return FString();
		}
		TArray<FString> Parts;
		Parts.Reserve(In.Num());
		for (const FName& N : In)
		{
			Parts.Add(FString::Printf(TEXT("\"%s\""), *N.ToString()));
		}
		return FString::Printf(TEXT("(%s)"), *FString::Join(Parts, TEXT(",")));
	}

	FString RuneBl(bool b)
	{
		return b ? TEXT("True") : TEXT("False");
	}

	/**
	 * 结构字面量里的字符串值。
	 *
	 * ⚠️ 与 CSV 转义是两层不同的转义：
	 *    这里是 UE ImportText 的规则（反斜杠转义内部引号），
	 *    外面还要再过一遍 RuneCsv（双引号翻倍）。
	 *    只做一层的话，含引号的文本会让整格解析错位。
	 */
	FString RuneStr(const FString& In)
	{
		FString Out = In;
		Out.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Out.ReplaceInline(TEXT("\""), TEXT("\\\""));
		return FString::Printf(TEXT("\"%s\""), *Out);
	}

	FString RuneName(const FName& N)
	{
		// 空 FName 导出成 ""，不能是 "None"（会变成名字叫 None 的 FName）
		return N.IsNone() ? TEXT("\"\"") : FString::Printf(TEXT("\"%s\""), *N.ToString());
	}

	/** 效果步骤数组 → UE 结构字面量（键名与 FHexEffectStepRow 逐字一致） */
	FString RuneEffects(const TArray<FHexEffectStep>& Steps)
	{
		TArray<FString> Items;
		Items.Reserve(Steps.Num());
		for (const FHexEffectStep& S : Steps)
		{
			TArray<FString> F;
			F.Add(FString::Printf(TEXT("Op=%s"), *RuneEnumName(S.Op)));
			F.Add(FString::Printf(TEXT("FlatValue=%f"), S.FlatValue));
			F.Add(FString::Printf(TEXT("StatRef=%s"), *RuneName(S.StatRef)));
			F.Add(FString::Printf(TEXT("StatRatio=%f"), S.StatRatio));
			F.Add(FString::Printf(TEXT("Repeat=%d"), S.Repeat));
			F.Add(FString::Printf(TEXT("Distance=%d"), S.Distance));
			F.Add(FString::Printf(TEXT("StatusId=%s"), *RuneName(S.StatusId)));
			F.Add(FString::Printf(TEXT("StatusStacks=%d"), S.StatusStacks));
			F.Add(FString::Printf(TEXT("TargetFilter=%s"), *RuneEnumName(S.TargetFilter)));
			F.Add(FString::Printf(TEXT("VfxId=%s"), *RuneName(S.VfxId)));
			F.Add(FString::Printf(TEXT("SfxId=%s"), *RuneName(S.SfxId)));
			Items.Add(FString::Printf(TEXT("(%s)"), *FString::Join(F, TEXT(","))));
		}
		return FString::Printf(TEXT("(%s)"), *FString::Join(Items, TEXT(",")));
	}

	/**
	 * 触发器数组 → UE 结构字面量。
	 *
	 * ⚠️ 键名必须与 FHexRuneTriggerRow 的 UPROPERTY 逐字一致，
	 *    写错的键被静默忽略。条件枚举导出的是【配表侧】的
	 *    EHexRuneConditionKind 标识名（两边映射见 HexRuneTableRow.cpp）。
	 */
	FString RuneTriggers(const TArray<FHexRuneTrigger>& Triggers)
	{
		// 逻辑层条件 → 配表侧枚举名（借 Row 的映射，不重写一份）
		auto CondName = [](const FHexEffectCondition& C) -> FString
		{
			FHexRuneTriggerRow Tmp;
			FHexRuneTrigger T;
			T.Condition = C;
			Tmp.FromTrigger(T);
			return RuneEnumName(Tmp.ConditionKind);
		};

		TArray<FString> Items;
		Items.Reserve(Triggers.Num());
		for (const FHexRuneTrigger& T : Triggers)
		{
			TArray<FString> F;
			F.Add(FString::Printf(TEXT("When=%s"), *RuneEnumName(T.When)));
			F.Add(FString::Printf(TEXT("bFilterByCardType=%s"), *RuneBl(T.bFilterByCardType)));
			F.Add(FString::Printf(TEXT("FilterCardType=%s"), *RuneEnumName(T.FilterCardType)));
			F.Add(FString::Printf(TEXT("FilterTag=%s"), *RuneName(T.FilterTag)));
			F.Add(FString::Printf(TEXT("FilterCostMin=%d"), T.FilterCostMin));
			F.Add(FString::Printf(TEXT("FilterCostMax=%d"), T.FilterCostMax));
			F.Add(FString::Printf(TEXT("ConditionKind=%s"), *CondName(T.Condition)));
			F.Add(FString::Printf(TEXT("ConditionFloat=%f"), T.Condition.FloatParam));
			F.Add(FString::Printf(TEXT("ConditionInt=%d"), T.Condition.IntParam));
			F.Add(FString::Printf(TEXT("ConditionName=%s"), *RuneName(T.Condition.NameParam)));
			F.Add(FString::Printf(TEXT("Effects=%s"), *RuneEffects(T.Effects)));
			F.Add(FString::Printf(TEXT("MaxPerRound=%d"), T.MaxPerRound));
			F.Add(FString::Printf(TEXT("MaxPerBattle=%d"), T.MaxPerBattle));
			F.Add(FString::Printf(TEXT("CounterThreshold=%d"), T.CounterThreshold));
			F.Add(FString::Printf(TEXT("bHasValueAdd=%s"), *RuneBl(T.bHasValueAdd)));
			F.Add(FString::Printf(TEXT("ValueAddFlat=%f"), T.ValueAddFlat));
			F.Add(FString::Printf(TEXT("ValueAddStatRef=%s"), *RuneName(T.ValueAddStatRef)));
			F.Add(FString::Printf(TEXT("ValueAddRatio=%f"), T.ValueAddRatio));
			F.Add(FString::Printf(TEXT("bHasValueMult=%s"), *RuneBl(T.bHasValueMult)));
			F.Add(FString::Printf(TEXT("ValueMult=%f"), T.ValueMult));
			Items.Add(FString::Printf(TEXT("(%s)"), *FString::Join(F, TEXT(","))));
		}
		return FString::Printf(TEXT("(%s)"), *FString::Join(Items, TEXT(",")));
	}

	/** 规则改写数组 → 字面量（键名与 FHexRuleOverrideRow 一致） */
	FString RuneRules(const TArray<FHexRuleOverride>& Rules)
	{
		TArray<FString> Items;
		Items.Reserve(Rules.Num());
		for (const FHexRuleOverride& R : Rules)
		{
			TArray<FString> F;
			F.Add(FString::Printf(TEXT("Rule=%s"), *RuneEnumName(R.Rule)));
			F.Add(FString::Printf(TEXT("IntValue=%d"), R.IntValue));
			F.Add(FString::Printf(TEXT("FloatValue=%f"), R.FloatValue));
			F.Add(FString::Printf(TEXT("bBoolValue=%s"), *RuneBl(R.bBoolValue)));
			F.Add(FString::Printf(TEXT("ApplyOrder=%d"), R.ApplyOrder));
			F.Add(FString::Printf(TEXT("bIsDelta=%s"), *RuneBl(R.bIsDelta)));
			Items.Add(FString::Printf(TEXT("(%s)"), *FString::Join(F, TEXT(","))));
		}
		return FString::Printf(TEXT("(%s)"), *FString::Join(Items, TEXT(",")));
	}
}

UHexExportRunesCommandlet::UHexExportRunesCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UHexExportRunesCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamsMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamsMap);

	FString OutPath = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("Export"), TEXT("Runes.csv"));
	if (const FString* Found = ParamsMap.Find(TEXT("out")))
	{
		OutPath = *Found;
	}

	// ⚠️ 第一列必须叫 Name（RowName）。其余列名与 FHexRuneTableRow
	//    的 UPROPERTY 逐字同名，【新增字段必须同时加到这里】——
	//    漏加的后果见卡牌导出器的 CastAnim 事故记录。
	TArray<FString> Lines;
	Lines.Add(TEXT("Name,DisplayName,Rarity,Category,Tags,")
		TEXT("Triggers,RuleOverrides,InjectedCardIds,bIsCursed,")
		TEXT("FlavorText,MechanicText"));

	const TArray<FHexRuneData>& Runes = FHexRuneLibrary::AllRunes();

	for (const FHexRuneData& R : Runes)
	{
		TArray<FString> C;
		C.Add(R.Id.ToString());
		C.Add(RuneCsv(R.DisplayName));
		C.Add(RuneEnumName(R.Rarity));
		C.Add(RuneEnumName(R.Category));
		C.Add(RuneCsv(RuneNames(R.Tags)));
		C.Add(RuneCsv(RuneTriggers(R.Triggers)));
		C.Add(RuneCsv(RuneRules(R.RuleOverrides)));
		C.Add(RuneCsv(RuneNames(R.InjectedCardIds)));
		C.Add(RuneBl(R.bIsCursed));
		C.Add(RuneCsv(R.FlavorText));
		C.Add(RuneCsv(R.MechanicText));
		Lines.Add(FString::Join(C, TEXT(",")));
	}

	const FString Content = FString::Join(Lines, TEXT("\n")) + TEXT("\n");

	// ⚠️ 必须带 BOM（Excel 中文乱码问题，同卡牌导出器）
	if (!FFileHelper::SaveStringToFile(
		Content, *OutPath, FFileHelper::EEncodingOptions::ForceUTF8))
	{
		UE_LOG(LogHexSpire, Error, TEXT("写入失败：%s"), *OutPath);
		return 1;
	}

	UE_LOG(LogHexSpire, Display, TEXT(""));
	UE_LOG(LogHexSpire, Display, TEXT("已导出 %d 个符文 → %s"), Runes.Num(), *OutPath);
	UE_LOG(LogHexSpire, Display, TEXT(""));
	UE_LOG(LogHexSpire, Display, TEXT("导入步骤："));
	UE_LOG(LogHexSpire, Display, TEXT("  1. Import Runes.csv → 行结构 FHexRuneTableRow → /Game/HexSpire/Data/DT_Runes"));
	UE_LOG(LogHexSpire, Display, TEXT("  2. 或跑 Tools/make_rune_datatable.py 首次建表"));
	UE_LOG(LogHexSpire, Display, TEXT(""));

	return 0;
}
