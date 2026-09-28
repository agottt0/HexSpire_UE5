// Copyright Hex Spire. All Rights Reserved.

#include "Commandlets/HexExportUnitsCommandlet.h"
#include "Data/HexHeroTableRow.h"
#include "Data/HexEnemyTableRow.h"
#include "Content/HexContentLibrary.h"
#include "Runes/HexRuneData.h"
#include "HexSpire.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	/**
	 * 取枚举值的【标识名】（如 "Aggressive"），不是 DisplayName（"近战突进"）。
	 *
	 * ⚠️ 必须用标识名。DataTable 导入 CSV 时按标识名匹配，
	 *    写中文 DisplayName 会导入失败并静默填成枚举第 0 项 ——
	 *    于是所有敌人都变成 Aggressive，而且不报错。
	 */
	template <typename TEnum>
	FString EnumName(TEnum Value)
	{
		const UEnum* E = StaticEnum<TEnum>();
		if (!E)
		{
			return FString::FromInt(static_cast<int32>(Value));
		}
		return E->GetNameStringByValue(static_cast<int64>(Value));
	}

	/**
	 * CSV 字段转义。
	 *
	 * ⚠️ 图鉴文本与被动描述里有中文逗号也有半角逗号，
	 *    不转义会让那一行的列数错位 —— 后面所有字段整体左移，
	 *    而导入时只报一条模糊的行解析警告。
	 */
	FString Csv(const FString& In)
	{
		if (!In.Contains(TEXT(",")) && !In.Contains(TEXT("\"")) && !In.Contains(TEXT("\n")))
		{
			return In;
		}
		FString Out = In.Replace(TEXT("\""), TEXT("\"\""));
		return FString::Printf(TEXT("\"%s\""), *Out);
	}

	/**
	 * FName 数组 → UE 数组字面量：("a","b","c")，空数组 → 空串。
	 *
	 * ⚠️ 不能用 "a|b|c" 这类自造分隔符。FArrayProperty::ImportText
	 *    要求单元格以 '(' 开头（空串除外，空串视为空数组），否则整格
	 *    解析失败 —— 该列静默变成空数组，只在导入日志里留一条含糊的
	 *    Problem 警告。CornerstoneCardIds 丢了的症状是"角色走不了路"。
	 *    含逗号，所以整格必须再过一遍 Csv()。
	 */
	FString NamesLiteral(const TArray<FName>& In)
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

	FString Bl(bool b)
	{
		return b ? TEXT("True") : TEXT("False");
	}

	/**
	 * 把被动规则数组序列化成 DataTable 能吃的内联结构体数组文本。
	 *
	 * 格式：((Rule=BlockPersists,IntValue=0,...),(...))
	 *
	 * ⚠️ 这是 UE 的 struct 字面量语法，不是 JSON。
	 *    键名必须与 UPROPERTY 的 C++ 名字逐字一致（大小写敏感），
	 *    写错的键被静默忽略 —— 那一项取默认值。
	 */
	FString RulesLiteral(const TArray<FHexRuleOverride>& Rules)
	{
		TArray<FString> Items;
		Items.Reserve(Rules.Num());

		for (const FHexRuleOverride& R : Rules)
		{
			TArray<FString> F;
			F.Add(FString::Printf(TEXT("Rule=%s"), *EnumName(R.Rule)));
			F.Add(FString::Printf(TEXT("IntValue=%d"), R.IntValue));
			F.Add(FString::Printf(TEXT("FloatValue=%f"), R.FloatValue));
			F.Add(FString::Printf(TEXT("bBoolValue=%s"), *Bl(R.bBoolValue)));
			F.Add(FString::Printf(TEXT("ApplyOrder=%d"), R.ApplyOrder));
			F.Add(FString::Printf(TEXT("bIsDelta=%s"), *Bl(R.bIsDelta)));
			Items.Add(FString::Printf(TEXT("(%s)"), *FString::Join(F, TEXT(","))));
		}

		return FString::Printf(TEXT("(%s)"), *FString::Join(Items, TEXT(",")));
	}

	// ⚠️ 必须带 BOM（ForceUTF8 是带 BOM 的那个）。
	//    Excel 打开无 BOM 的 UTF-8 CSV 会把中文显示成乱码，
	//    而第一反应会是"导出坏了"。
	bool Save(const TArray<FString>& Lines, const FString& Path)
	{
		const FString Content = FString::Join(Lines, TEXT("\n")) + TEXT("\n");
		if (!FFileHelper::SaveStringToFile(
			Content, *Path, FFileHelper::EEncodingOptions::ForceUTF8))
		{
			UE_LOG(LogHexSpire, Error, TEXT("写入失败：%s"), *Path);
			return false;
		}
		return true;
	}
}

UHexExportUnitsCommandlet::UHexExportUnitsCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UHexExportUnitsCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamsMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamsMap);

	FString OutDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Export"));
	if (const FString* Found = ParamsMap.Find(TEXT("outdir")))
	{
		OutDir = *Found;
	}

	// ══════════════════════════════════════════════════ 英雄
	//
	// ⚠️ 第一列必须叫 Name —— 导入时用它做 RowName。
	//    其余列名必须与 FHexHeroTableRow 的 UPROPERTY 逐字同名，
	//    且【新增字段必须同时加到这里】。漏加的后果是导出的 CSV
	//    缺这一列 → 导回来时该列取类型默认值 → 覆写（整行替换）
	//    把代码内建的值冲掉。也就是"漏改导出器"会表现为
	//    "内建数据凭空丢失"，而查定义时怎么看都是对的。
	{
		TArray<FString> Lines;
		Lines.Add(TEXT("Name,DisplayName,SizeClass,")
			TEXT("BaseHP,BaseATK,BaseDEF,BaseAGI,BaseLUK,BaseCRIT,")
			TEXT("EnergyMax,CardsDrawnPerTurn,")
			TEXT("CornerstoneCardIds,CardPoolTags,")
			TEXT("PassiveRules,PassiveText"));

		for (const FHexHeroData& H : FHexContentLibrary::AllHeroes())
		{
			TArray<FString> C;
			C.Add(H.Id.ToString());
			C.Add(Csv(H.DisplayName));
			C.Add(EnumName(H.SizeClass));
			C.Add(FString::FromInt(H.BaseHP));
			C.Add(FString::FromInt(H.BaseATK));
			C.Add(FString::FromInt(H.BaseDEF));
			C.Add(FString::FromInt(H.BaseAGI));
			C.Add(FString::FromInt(H.BaseLUK));
			C.Add(FString::FromInt(H.BaseCRIT));
			C.Add(FString::FromInt(H.EnergyMax));
			C.Add(FString::FromInt(H.CardsDrawnPerTurn));
			C.Add(Csv(NamesLiteral(H.CornerstoneCardIds)));
			C.Add(Csv(NamesLiteral(H.CardPoolTags)));
			C.Add(Csv(RulesLiteral(H.PassiveRules)));
			C.Add(Csv(H.PassiveText));
			Lines.Add(FString::Join(C, TEXT(",")));
		}

		const FString Path = FPaths::Combine(OutDir, TEXT("Heroes.csv"));
		if (!Save(Lines, Path))
		{
			return 1;
		}
		UE_LOG(LogHexSpire, Display, TEXT("已导出 %d 个英雄 → %s"),
			FHexContentLibrary::AllHeroes().Num(), *Path);
	}

	// ══════════════════════════════════════════════════ 敌人
	{
		TArray<FString> Lines;
		Lines.Add(TEXT("Name,DisplayName,SizeClass,")
			TEXT("BaseHP,BaseATK,BaseDEF,BaseAGI,BaseLUK,BaseCRIT,")
			TEXT("AIProfile,IntentTargeting,")
			TEXT("MoveBudget,PreferredDistance,AttackRangeOverride,")
			TEXT("SkillIds,")
			TEXT("bIsElite,bIsBoss,KnockbackResistOverride,")
			TEXT("CodexText"));

		for (const FHexEnemyData& E : FHexContentLibrary::AllEnemies())
		{
			TArray<FString> C;
			C.Add(E.Id.ToString());
			C.Add(Csv(E.DisplayName));
			C.Add(EnumName(E.SizeClass));
			C.Add(FString::FromInt(E.BaseHP));
			C.Add(FString::FromInt(E.BaseATK));
			C.Add(FString::FromInt(E.BaseDEF));
			C.Add(FString::FromInt(E.BaseAGI));
			C.Add(FString::FromInt(E.BaseLUK));
			C.Add(FString::FromInt(E.BaseCRIT));
			C.Add(EnumName(E.AIProfile));
			C.Add(EnumName(E.IntentTargeting));
			C.Add(FString::FromInt(E.MoveBudget));
			C.Add(FString::FromInt(E.PreferredDistance));
			C.Add(FString::FromInt(E.AttackRangeOverride));
			C.Add(Csv(NamesLiteral(E.SkillIds)));
			C.Add(Bl(E.bIsElite));
			C.Add(Bl(E.bIsBoss));
			C.Add(FString::FromInt(E.KnockbackResistOverride));
			C.Add(Csv(E.CodexText));
			Lines.Add(FString::Join(C, TEXT(",")));
		}

		const FString Path = FPaths::Combine(OutDir, TEXT("Enemies.csv"));
		if (!Save(Lines, Path))
		{
			return 1;
		}
		UE_LOG(LogHexSpire, Display, TEXT("已导出 %d 个敌人 → %s"),
			FHexContentLibrary::AllEnemies().Num(), *Path);
	}

	UE_LOG(LogHexSpire, Display, TEXT(""));
	UE_LOG(LogHexSpire, Display, TEXT("导入步骤："));
	UE_LOG(LogHexSpire, Display, TEXT("  1. Import Heroes.csv  → 行结构 FHexHeroTableRow  → /Game/HexSpire/Data/DT_Heroes"));
	UE_LOG(LogHexSpire, Display, TEXT("  2. Import Enemies.csv → 行结构 FHexEnemyTableRow → /Game/HexSpire/Data/DT_Enemies"));
	UE_LOG(LogHexSpire, Display, TEXT("  3. 资产引用在 DA_UnitVisual_<UnitId> 里配，不在表里"));
	UE_LOG(LogHexSpire, Display, TEXT(""));

	return 0;
}
