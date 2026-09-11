// Copyright Hex Spire. All Rights Reserved.

#include "Data/HexCardTableLoader.h"
#include "Content/HexContentLibrary.h"
#include "Battle/HexCardData.h"
#include "HexSpire.h"

#include "Engine/DataTable.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	// ══════════════════════════════════════════════════════════════
	// 必须用 TStrongObjectPtr 持有，不能用裸指针
	// ══════════════════════════════════════════════════════════════
	// ⚠️ 这里【曾经】是 `const UDataTable*` 裸指针，注释还写着
	//    "表由本模块持有生命周期" —— 那是错的，而且是会崩的错。
	//
	//    命名空间作用域的裸指针对 GC 【完全不可见】：
	//    LoadObject 把表加载进来后，没有任何 UPROPERTY 或 root 引用它，
	//    于是第一次 GC 就把它回收，GLoadedCardTable 变成悬垂指针。
	//    下一次 FindVisual 解引用它 → EXCEPTION_ACCESS_VIOLATION。
	//
	//    崩溃时机是这个 bug 最阴的地方：加载后一分钟左右（首次 GC）
	//    才炸，而且崩在 FindVisual 里，看起来像"卡牌美术配置有问题"，
	//    实际根因在生命周期管理。实测在第 853 帧崩溃。
	//
	//    TStrongObjectPtr 会把对象加进 root 集合，GC 不再回收它。
	TStrongObjectPtr<const UDataTable> GLoadedCardTable;
}

const TCHAR* FHexCardTableLoader::DefaultTablePath()
{
	return TEXT("/Game/HexSpire/Data/DT_Cards.DT_Cards");
}

const UDataTable* FHexCardTableLoader::GetLoadedTable()
{
	return GLoadedCardTable.Get();
}

int32 FHexCardTableLoader::ApplyDefaultTable()
{
	// ⚠️ 表不存在【不是错误】，只是"还没开始配表"。
	//    所以这里用 LoadObject 而非 ensure/check，且失败时不报 Warning ——
	//    灰盒期每次启动都刷一条警告会淹没真正需要看的日志。
	const UDataTable* Table = LoadObject<UDataTable>(nullptr, DefaultTablePath());

	if (!Table)
	{
		UE_LOG(LogHexSpire, Verbose,
			TEXT("卡牌配表不存在（%s），全部使用代码内建卡池"),
			DefaultTablePath());
		return 0;
	}

	return Apply(Table);
}

int32 FHexCardTableLoader::Apply(const UDataTable* Table)
{
	if (!Table)
	{
		return 0;
	}

	// ── 行结构必须匹配，否则 FindRow 会拿到错位的内存
	//
	// ⚠️ 这个检查不能省。DataTable 的 RowStruct 是导入时选的，
	//    选错（比如选了 FHexEquipTableRow）时 FindRow<FHexCardTableRow>
	//    仍然会返回一个指针 —— 指向按错误布局解读的字节，
	//    于是费用/伤害变成垃圾值，而且不崩溃、不报错。
	if (Table->GetRowStruct() != FHexCardTableRow::StaticStruct())
	{
		UE_LOG(LogHexSpire, Error,
			TEXT("卡牌配表 %s 的行结构不是 FHexCardTableRow（实际 %s），已忽略整张表"),
			*Table->GetPathName(),
			Table->GetRowStruct() ? *Table->GetRowStruct()->GetName() : TEXT("null"));
		return 0;
	}

	GLoadedCardTable.Reset(Table);

	int32 Overridden = 0;
	int32 Added = 0;

	// GetRowMap 的 key 就是 RowName
	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		const FName RowName = Pair.Key;
		const FHexCardTableRow* Row =
			reinterpret_cast<const FHexCardTableRow*>(Pair.Value);
		if (!Row)
		{
			continue;
		}

		// ⚠️ RowName 即卡牌 Id。
		//    策划若把 RowName 写成"攻击"而 Id 实际是 atk_basic，
		//    覆写会静默落空 —— 表里改了数值，游戏里纹丝不动。
		//    这种"改了没反应"最难查，所以这里主动提示。
		if (RowName.IsNone())
		{
			UE_LOG(LogHexSpire, Warning,
				TEXT("卡牌配表里有空 RowName 的行，已跳过"));
			continue;
		}

		const bool bWasExisting = FHexContentLibrary::FindCard(RowName) != nullptr;

		FHexCardData Card = Row->ToCardData(RowName);

		// 显示名为空会让 HUD 画出空白卡 —— 补一个可辨认的占位
		if (Card.DisplayName.IsEmpty())
		{
			Card.DisplayName = RowName.ToString();
			UE_LOG(LogHexSpire, Warning,
				TEXT("卡牌 %s 的 DisplayName 为空，暂用 Id 显示"), *RowName.ToString());
		}

		FHexContentLibrary::OverrideCard(Card);

		if (bWasExisting)
		{
			++Overridden;
		}
		else
		{
			++Added;
			UE_LOG(LogHexSpire, Display,
				TEXT("配表新增卡牌：%s（%s）"), *RowName.ToString(), *Card.DisplayName);
		}
	}

	UE_LOG(LogHexSpire, Display,
		TEXT("卡牌配表已应用：覆写 %d 张，新增 %d 张（表 %s）"),
		Overridden, Added, *Table->GetName());

	return Overridden + Added;
}

const FHexCardVisualRow* FHexCardTableLoader::FindVisual(FName CardId)
{
	const UDataTable* Table = GLoadedCardTable.Get();
	if (!Table)
	{
		return nullptr;
	}

	// ⚠️ 不缓存结果。表在编辑器里被重新导入时行内存会整体重建，
	//    缓存下来的指针会指向已释放的内存 —— 而且通常不会立刻崩，
	//    只是读到垃圾贴图指针。查表本身是 TMap 查找，不值得缓存。
	const FHexCardTableRow* Row =
		Table->FindRow<FHexCardTableRow>(CardId, TEXT("FindVisual"), false);

	return Row ? &Row->Visual : nullptr;
}
