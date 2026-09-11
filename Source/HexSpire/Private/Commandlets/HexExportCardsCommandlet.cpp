// Copyright Hex Spire. All Rights Reserved.

#include "Commandlets/HexExportCardsCommandlet.h"
#include "Data/HexCardTableRow.h"
#include "Content/HexContentLibrary.h"
#include "Battle/HexCardData.h"
#include "HexSpire.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/EnumProperty.h"

namespace
{
	/**
	 * 取枚举值的【标识名】（如 "Attack"），而不是 DisplayName（"攻击"）。
	 *
	 * ⚠️ 必须用标识名。DataTable 导入 CSV 时按枚举的标识名匹配，
	 *    写中文 DisplayName 会导入失败并静默填成枚举的第 0 项 ——
	 *    于是所有卡都变成"攻击"类型，而且不报错。
	 */
	template <typename TEnum>
	FString EnumName(TEnum Value)
	{
		const UEnum* E = StaticEnum<TEnum>();
		if (!E)
		{
			return FString::FromInt(static_cast<int32>(Value));
		}
		// GetNameStringByValue 返回不带作用域前缀的短名
		return E->GetNameStringByValue(static_cast<int64>(Value));
	}

	/**
	 * CSV 字段转义。
	 *
	 * ⚠️ 描述模板里有中文逗号也有半角逗号（"造成 {dmg} 点伤害，并击退 {kb} 格"），
	 *    不转义会让那一行的列数错位 —— 后面所有字段整体左移，
	 *    而 DataTable 导入时只会报一条模糊的行解析警告。
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

	/** FName 数组 → "a|b|c"（CSV 里不能用逗号分隔子项） */
	/**
	 * 空 FName 导出成空串而不是 "None"。
	 *
	 * ⚠️ FName().ToString() 返回的是字面量 "None"。直接写进 CSV 的话，
	 *    导入回来会得到一个【名字就叫 None】的 FName ——
	 *    于是 StatusId 非空，ApplyStatus 会去找一个叫 "None" 的状态，
	 *    查不到就静默什么也不做。卡牌看起来正常，效果凭空消失。
	 */
	FString NameCell(const FName& N)
	{
		return N.IsNone() ? FString() : N.ToString();
	}

	FString JoinNames(const TArray<FName>& In)
	{
		TArray<FString> Parts;
		Parts.Reserve(In.Num());
		for (const FName& N : In)
		{
			Parts.Add(N.ToString());
		}
		return FString::Join(Parts, TEXT("|"));
	}

	/**
	 * 把 Effects 数组序列化成 DataTable 能吃的内联结构体数组文本。
	 *
	 * 格式：((Op=DealDamage,FlatValue=0.000000,...),(...))
	 *
	 * ⚠️ 这是 UE 的 struct 字面量语法，不是 JSON。
	 *    键名必须与 UPROPERTY 的 C++ 名字【完全一致】（大小写敏感），
	 *    写错的键会被静默忽略 —— 那一项就取默认值。
	 */
	FString EffectsLiteral(const TArray<FHexEffectStep>& Steps)
	{
		TArray<FString> Items;
		Items.Reserve(Steps.Num());

		for (const FHexEffectStep& S : Steps)
		{
			TArray<FString> F;
			F.Add(FString::Printf(TEXT("Op=%s"), *EnumName(S.Op)));
			F.Add(FString::Printf(TEXT("FlatValue=%f"), S.FlatValue));
			F.Add(FString::Printf(TEXT("StatRef=\"%s\""), *NameCell(S.StatRef)));
			F.Add(FString::Printf(TEXT("StatRatio=%f"), S.StatRatio));
			F.Add(FString::Printf(TEXT("Repeat=%d"), S.Repeat));
			F.Add(FString::Printf(TEXT("Distance=%d"), S.Distance));
			F.Add(FString::Printf(TEXT("StatusId=\"%s\""), *NameCell(S.StatusId)));
			F.Add(FString::Printf(TEXT("StatusStacks=%d"), S.StatusStacks));
			F.Add(FString::Printf(TEXT("TargetFilter=%s"), *EnumName(S.TargetFilter)));
			F.Add(FString::Printf(TEXT("VfxId=\"%s\""), *NameCell(S.VfxId)));
			F.Add(FString::Printf(TEXT("SfxId=\"%s\""), *NameCell(S.SfxId)));

			Items.Add(FString::Printf(TEXT("(%s)"), *FString::Join(F, TEXT(","))));
		}

		return FString::Printf(TEXT("(%s)"), *FString::Join(Items, TEXT(",")));
	}

	FString TargetSpecLiteral(const FHexTargetSpec& T)
	{
		TArray<FString> F;
		F.Add(FString::Printf(TEXT("Shape=%s"), *EnumName(T.Shape)));
		F.Add(FString::Printf(TEXT("RangeMin=%d"), T.RangeMin));
		F.Add(FString::Printf(TEXT("RangeMax=%d"), T.RangeMax));
		F.Add(FString::Printf(TEXT("AreaSize=%d"), T.AreaSize));
		F.Add(FString::Printf(TEXT("bRequiresLineOfSight=%s"),
			T.bRequiresLineOfSight ? TEXT("True") : TEXT("False")));
		F.Add(FString::Printf(TEXT("bCanTargetEmptyCell=%s"),
			T.bCanTargetEmptyCell ? TEXT("True") : TEXT("False")));
		return FString::Printf(TEXT("(%s)"), *FString::Join(F, TEXT(",")));
	}
}

UHexExportCardsCommandlet::UHexExportCardsCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UHexExportCardsCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamsMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamsMap);

	FString OutPath = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("Export"), TEXT("Cards.csv"));
	if (const FString* Found = ParamsMap.Find(TEXT("out")))
	{
		OutPath = *Found;
	}

	const TArray<FHexCardData>& Cards = FHexContentLibrary::AllCards();

	// ── 表头
	//
	// ⚠️ 第一列必须叫 Name —— DataTable 导入时用它做 RowName。
	//    列名其余部分必须与 FHexCardTableRow 的 UPROPERTY 同名。
	TArray<FString> Lines;
	Lines.Add(TEXT("Name,DisplayName,CardType,Rarity,EnergyCost,Tags,")
		TEXT("TargetSpec,Effects,")
		TEXT("bIsCornerstone,bIsExhaust,bCountsTowardCapacity,MaxCopiesInDeck,")
		TEXT("DescriptionTemplate"));

	for (const FHexCardData& C : Cards)
	{
		TArray<FString> Cols;
		Cols.Add(C.Id.ToString());
		Cols.Add(Csv(C.DisplayName));
		Cols.Add(EnumName(C.CardType));
		Cols.Add(EnumName(C.Rarity));
		Cols.Add(FString::FromInt(C.EnergyCost));
		Cols.Add(Csv(JoinNames(C.Tags)));
		Cols.Add(Csv(TargetSpecLiteral(C.TargetSpec)));
		Cols.Add(Csv(EffectsLiteral(C.Effects)));
		Cols.Add(C.bIsCornerstone ? TEXT("True") : TEXT("False"));
		Cols.Add(C.bIsExhaust ? TEXT("True") : TEXT("False"));
		Cols.Add(C.bCountsTowardCapacity ? TEXT("True") : TEXT("False"));
		Cols.Add(FString::FromInt(C.MaxCopiesInDeck));
		Cols.Add(Csv(C.DescriptionTemplate));

		Lines.Add(FString::Join(Cols, TEXT(",")));
	}

	const FString Content = FString::Join(Lines, TEXT("\n")) + TEXT("\n");

	// ⚠️ 必须带 BOM（ForceUTF8 就是带 BOM 的那个，
	//    不带 BOM 的是 ForceUTF8WithoutBOM）。
	//    Excel 打开无 BOM 的 UTF-8 CSV 会把中文显示成乱码，
	//    而策划的第一反应会是"导出坏了"。
	if (!FFileHelper::SaveStringToFile(
		Content, *OutPath,
		FFileHelper::EEncodingOptions::ForceUTF8))
	{
		UE_LOG(LogHexSpire, Error, TEXT("写入失败：%s"), *OutPath);
		return 1;
	}

	UE_LOG(LogHexSpire, Display, TEXT(""));
	UE_LOG(LogHexSpire, Display, TEXT("已导出 %d 张卡 → %s"), Cards.Num(), *OutPath);
	UE_LOG(LogHexSpire, Display, TEXT(""));
	UE_LOG(LogHexSpire, Display, TEXT("导入步骤："));
	UE_LOG(LogHexSpire, Display, TEXT("  1. 编辑器内容浏览器 → Import → 选这个 CSV"));
	UE_LOG(LogHexSpire, Display, TEXT("  2. 行结构选 FHexCardTableRow"));
	UE_LOG(LogHexSpire, Display, TEXT("  3. 存到 /Game/HexSpire/Data/DT_Cards"));
	UE_LOG(LogHexSpire, Display, TEXT("  4. 美术字段（Visual）在编辑器里逐张挂贴图"));
	UE_LOG(LogHexSpire, Display, TEXT(""));

	return 0;
}
