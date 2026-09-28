// Copyright Hex Spire. All Rights Reserved.

#include "Data/HexRuneTableLoader.h"
#include "Data/HexRuneTableRow.h"
#include "Runes/HexRuneLibrary.h"
#include "HexSpire.h"

#include "Engine/DataTable.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	// ⚠️ TStrongObjectPtr 持有，防 GC 回收（HexUnitTableLoader 顶部
	//    有这个坑的完整记录：裸指针在第 853 帧崩）。
	TStrongObjectPtr<const UDataTable> GLoadedRuneTable;
}

const TCHAR* FHexRuneTableLoader::DefaultTablePath()
{
	return TEXT("/Game/HexSpire/Data/DT_Runes.DT_Runes");
}

int32 FHexRuneTableLoader::Apply(const UDataTable* Table)
{
	if (!Table)
	{
		return 0;
	}

	if (Table->GetRowStruct() != FHexRuneTableRow::StaticStruct())
	{
		UE_LOG(LogHexSpire, Error,
			TEXT("符文配表 %s 的行结构不是 FHexRuneTableRow（实际 %s），已忽略整张表"),
			*Table->GetPathName(),
			Table->GetRowStruct() ? *Table->GetRowStruct()->GetName() : TEXT("null"));
		return 0;
	}

	GLoadedRuneTable.Reset(Table);

	int32 Overridden = 0;
	int32 Added = 0;

	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		const FName RowName = Pair.Key;
		const FHexRuneTableRow* Row =
			reinterpret_cast<const FHexRuneTableRow*>(Pair.Value);
		if (!Row)
		{
			continue;
		}

		if (RowName.IsNone())
		{
			UE_LOG(LogHexSpire, Warning, TEXT("符文配表里有空 RowName 的行，已跳过"));
			continue;
		}

		// 占位行检测：判据用 DisplayName（同单位表，见那边的说明）
		if (Row->DisplayName.IsEmpty())
		{
			UE_LOG(LogHexSpire, Verbose,
				TEXT("符文 %s 的 DisplayName 为空（占位行），跳过覆写"),
				*RowName.ToString());
			continue;
		}

		// ── 配表期就能抓到的静默 bug：只报告，不修正
		//
		// 数值钩子挂错时机不报错也不生效（②′ 仅 OnAttack），
		// 这是符文配表最容易踩的一处。
		for (const FHexRuneTriggerRow& T : Row->Triggers)
		{
			if ((T.bHasValueAdd || T.bHasValueMult)
				&& T.When != EHexTriggerTiming::OnAttack)
			{
				UE_LOG(LogHexSpire, Warning,
					TEXT("符文 %s：数值钩子挂在 %d 时机上不会生效（仅 OnAttack 有效）"),
					*RowName.ToString(), static_cast<int32>(T.When));
			}
		}

		if (FHexRuneLibrary::OverrideRune(Row->ToRuneData(RowName)))
		{
			++Overridden;
		}
		else
		{
			++Added;
			UE_LOG(LogHexSpire, Display,
				TEXT("配表新增符文：%s（%s）"), *RowName.ToString(), *Row->DisplayName);
		}
	}

	UE_LOG(LogHexSpire, Display,
		TEXT("符文配表已应用：覆写 %d，新增 %d（表 %s）"),
		Overridden, Added, *Table->GetName());

	return Overridden + Added;
}

int32 FHexRuneTableLoader::ApplyDefaults()
{
	if (const UDataTable* Table =
		LoadObject<UDataTable>(nullptr, DefaultTablePath()))
	{
		return Apply(Table);
	}

	UE_LOG(LogHexSpire, Verbose,
		TEXT("符文配表不存在（%s），全部使用代码内建"), DefaultTablePath());
	return 0;
}
