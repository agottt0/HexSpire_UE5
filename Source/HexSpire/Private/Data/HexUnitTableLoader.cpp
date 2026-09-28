// Copyright Hex Spire. All Rights Reserved.

#include "Data/HexUnitTableLoader.h"
#include "Data/HexHeroTableRow.h"
#include "Data/HexEnemyTableRow.h"
#include "Data/HexUnitVisualSet.h"
#include "Content/HexContentLibrary.h"
#include "HexSpire.h"

#include "Engine/DataTable.h"
#include "UObject/StrongObjectPtr.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/ARFilter.h"

namespace
{
	// ══════════════════════════════════════════════════════════════
	// 必须用 TStrongObjectPtr 持有 —— 裸指针对 GC 不可见
	// ══════════════════════════════════════════════════════════════
	// ⚠️ 这个坑在 HexCardTableLoader 上已经踩过一次并留了长注释：
	//    命名空间作用域的裸 UObject* 没有任何 UPROPERTY / root 引用，
	//    第一次 GC 就回收，之后解引用 → EXCEPTION_ACCESS_VIOLATION。
	//    崩溃发生在加载后一分钟左右（首次 GC），看起来像"配置有问题"，
	//    实际根因是生命周期。实测在第 853 帧崩。
	//
	//    外观资产尤其危险：它们只被这个 map 引用，
	//    而战斗开始时才解引用 —— 中间隔了整个主菜单的时间，GC 必然跑过。
	TStrongObjectPtr<const UDataTable> GLoadedHeroTable;
	TStrongObjectPtr<const UDataTable> GLoadedEnemyTable;

	TMap<FName, TStrongObjectPtr<const UHexUnitVisualSet>> GVisualSets;

	/**
	 * 校验行结构。
	 *
	 * ⚠️ 这个检查不能省。RowStruct 选错时 FindRow 仍会返回指针 ——
	 *    指向按错误布局解读的字节，于是 HP/ATK 变成垃圾值，
	 *    不崩溃、不报错。整张表拒绝掉比用垃圾数值跑下去好。
	 */
	bool CheckRowStruct(const UDataTable* Table, const UScriptStruct* Want,
		const TCHAR* Label)
	{
		if (Table->GetRowStruct() == Want)
		{
			return true;
		}

		UE_LOG(LogHexSpire, Error,
			TEXT("%s配表 %s 的行结构不是 %s（实际 %s），已忽略整张表"),
			Label, *Table->GetPathName(), *Want->GetName(),
			Table->GetRowStruct() ? *Table->GetRowStruct()->GetName() : TEXT("null"));
		return false;
	}
}

const TCHAR* FHexUnitTableLoader::DefaultHeroTablePath()
{
	return TEXT("/Game/HexSpire/Data/DT_Heroes.DT_Heroes");
}

const TCHAR* FHexUnitTableLoader::DefaultEnemyTablePath()
{
	return TEXT("/Game/HexSpire/Data/DT_Enemies.DT_Enemies");
}

const TCHAR* FHexUnitTableLoader::DefaultVisualDir()
{
	return TEXT("/Game/HexSpire/Data/Visuals");
}

// ══════════════════════════════════════════════════════════ 英雄表

int32 FHexUnitTableLoader::ApplyHeroTable(const UDataTable* Table)
{
	if (!Table)
	{
		return 0;
	}

	if (!CheckRowStruct(Table, FHexHeroTableRow::StaticStruct(), TEXT("英雄")))
	{
		return 0;
	}

	GLoadedHeroTable.Reset(Table);

	int32 Overridden = 0;
	int32 Added = 0;

	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		const FName RowName = Pair.Key;
		const FHexHeroTableRow* Row =
			reinterpret_cast<const FHexHeroTableRow*>(Pair.Value);
		if (!Row)
		{
			continue;
		}

		if (RowName.IsNone())
		{
			UE_LOG(LogHexSpire, Warning, TEXT("英雄配表里有空 RowName 的行，已跳过"));
			continue;
		}

		// ── 占位行检测
		//
		// ⚠️ 只搭框架时会先建几行空的（DisplayName 空、属性全默认）。
		//    那些行【不应当覆写内建英雄】—— 否则调好的镇妖者数值
		//    会被一行空白冲成默认值，而表面上"表里有这个英雄"。
		//    判据用 DisplayName 而不是属性：属性的默认值与真实值可能重合，
		//    但没人会把英雄的显示名故意留空。
		if (Row->DisplayName.IsEmpty())
		{
			UE_LOG(LogHexSpire, Verbose,
				TEXT("英雄 %s 的 DisplayName 为空（占位行），跳过覆写"),
				*RowName.ToString());
			continue;
		}

		const bool bWasExisting = FHexContentLibrary::FindHero(RowName) != nullptr;

		FHexContentLibrary::OverrideHero(Row->ToHeroData(RowName));

		if (bWasExisting)
		{
			++Overridden;
		}
		else
		{
			++Added;
			UE_LOG(LogHexSpire, Display,
				TEXT("配表新增英雄：%s（%s）"), *RowName.ToString(), *Row->DisplayName);
		}
	}

	UE_LOG(LogHexSpire, Display,
		TEXT("英雄配表已应用：覆写 %d，新增 %d（表 %s）"),
		Overridden, Added, *Table->GetName());

	return Overridden + Added;
}

// ══════════════════════════════════════════════════════════ 敌人表

int32 FHexUnitTableLoader::ApplyEnemyTable(const UDataTable* Table)
{
	if (!Table)
	{
		return 0;
	}

	if (!CheckRowStruct(Table, FHexEnemyTableRow::StaticStruct(), TEXT("敌人")))
	{
		return 0;
	}

	GLoadedEnemyTable.Reset(Table);

	int32 Overridden = 0;
	int32 Added = 0;

	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		const FName RowName = Pair.Key;
		const FHexEnemyTableRow* Row =
			reinterpret_cast<const FHexEnemyTableRow*>(Pair.Value);
		if (!Row)
		{
			continue;
		}

		if (RowName.IsNone())
		{
			UE_LOG(LogHexSpire, Warning, TEXT("敌人配表里有空 RowName 的行，已跳过"));
			continue;
		}

		if (Row->DisplayName.IsEmpty())
		{
			UE_LOG(LogHexSpire, Verbose,
				TEXT("敌人 %s 的 DisplayName 为空（占位行），跳过覆写"),
				*RowName.ToString());
			continue;
		}

		const bool bWasExisting = FHexContentLibrary::FindEnemy(RowName) != nullptr;

		FHexEnemyData Enemy = Row->ToEnemyData(RowName);

		// ── 配表期就能抓到的两个静默 bug
		//
		// ⚠️ 这两条【不修正数据，只报告】。自动修正会让表里写的值
		//    与实际跑的值不一致 —— 那比报错难查得多。

		// 风筝型的理想距离超过攻击射程 → 整场只后退，不报任何错。
		// -1 表示用 profile 默认射程，此时无法在这里判定，交给 VerifyContent。
		if (Enemy.AIProfile == EHexAIProfile::RangedKiter
			&& Enemy.AttackRangeOverride > 0
			&& Enemy.PreferredDistance > Enemy.AttackRangeOverride)
		{
			UE_LOG(LogHexSpire, Warning,
				TEXT("敌人 %s：理想距离 %d > 攻击射程 %d，它将永远进不了射程（整场只后退）"),
				*RowName.ToString(), Enemy.PreferredDistance, Enemy.AttackRangeOverride);
		}

		// 射程填 1 会让该敌人永远产不出可躲型意图（§13.2 的可预判性消失）
		if (Enemy.AttackRangeOverride == 1
			&& Enemy.IntentTargeting == EHexIntentTargeting::FixedTile)
		{
			UE_LOG(LogHexSpire, Warning,
				TEXT("敌人 %s：射程 1 + 锁定格子 —— 距离≤1 一律转追踪，"
					 "这只怪不会有可躲意图"),
				*RowName.ToString());
		}

		FHexContentLibrary::OverrideEnemy(Enemy);

		if (bWasExisting)
		{
			++Overridden;
		}
		else
		{
			++Added;
			UE_LOG(LogHexSpire, Display,
				TEXT("配表新增敌人：%s（%s）"), *RowName.ToString(), *Row->DisplayName);
		}
	}

	UE_LOG(LogHexSpire, Display,
		TEXT("敌人配表已应用：覆写 %d，新增 %d（表 %s）"),
		Overridden, Added, *Table->GetName());

	return Overridden + Added;
}

// ══════════════════════════════════════════════════════════ 外观资产

int32 FHexUnitTableLoader::LoadVisualSets()
{
	GVisualSets.Reset();

	// ⚠️ 用 AssetRegistry 扫目录而不是猜文件名。
	//    按 "DA_UnitVisual_" + UnitId 拼路径去 LoadObject 的做法有个坑：
	//    资产名与 UnitId 不一致时（改名了但没同步）会静默找不到，
	//    而扫目录能把资产读出来并用它【自己声明的 UnitId】匹配 ——
	//    于是"资产名写错"退化成"能用"，"UnitId 写错"才报错。
	//    后者是真正需要报的那个。
	const FAssetRegistryModule& Registry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	FARFilter Filter;
	Filter.bRecursivePaths = true;
	Filter.bRecursiveClasses = true;
	Filter.PackagePaths.Add(FName(DefaultVisualDir()));
	Filter.ClassPaths.Add(UHexUnitVisualSet::StaticClass()->GetClassPathName());

	TArray<FAssetData> Found;
	Registry.Get().GetAssets(Filter, Found);

	if (Found.Num() == 0)
	{
		// 不是错误 —— 还没开始配外观时全部走旧的 HexAppearance 路径式外观。
		UE_LOG(LogHexSpire, Verbose,
			TEXT("外观资产目录为空（%s），全部使用内建路径式外观"),
			DefaultVisualDir());
		return 0;
	}

	int32 Loaded = 0;

	for (const FAssetData& Data : Found)
	{
		const UHexUnitVisualSet* Set = Cast<UHexUnitVisualSet>(Data.GetAsset());
		if (!Set)
		{
			continue;
		}

		// ⚠️ UnitId 为空的资产【匹配不上任何单位】，症状是
		//    "配了模型但游戏里还是占位胶囊"，而资产本身怎么看都是对的。
		//    这个必须报 Warning，否则查不出来。
		if (Set->UnitId.IsNone())
		{
			UE_LOG(LogHexSpire, Warning,
				TEXT("外观资产 %s 的 UnitId 为空，它不会匹配到任何单位"),
				*Data.AssetName.ToString());
			continue;
		}

		if (GVisualSets.Contains(Set->UnitId))
		{
			UE_LOG(LogHexSpire, Warning,
				TEXT("外观资产 %s 的 UnitId（%s）与另一个资产重复，后者被忽略"),
				*Data.AssetName.ToString(), *Set->UnitId.ToString());
			continue;
		}

		GVisualSets.Add(Set->UnitId, TStrongObjectPtr<const UHexUnitVisualSet>(Set));
		++Loaded;
	}

	UE_LOG(LogHexSpire, Display, TEXT("外观资产已加载：%d 个"), Loaded);
	return Loaded;
}

const UHexUnitVisualSet* FHexUnitTableLoader::FindVisualSet(FName UnitId)
{
	const TStrongObjectPtr<const UHexUnitVisualSet>* Found = GVisualSets.Find(UnitId);
	return Found ? Found->Get() : nullptr;
}

void FHexUnitTableLoader::ResetVisualSets()
{
	GVisualSets.Reset();
}

// ══════════════════════════════════════════════════════════ 入口

int32 FHexUnitTableLoader::ApplyDefaults()
{
	int32 Total = 0;

	// ⚠️ 表不存在不是错误（还没开始配表），所以用 LoadObject 而非 check，
	//    且失败时只记 Verbose —— 灰盒期每次启动刷警告会淹没真正的日志。
	if (const UDataTable* Heroes =
		LoadObject<UDataTable>(nullptr, DefaultHeroTablePath()))
	{
		Total += ApplyHeroTable(Heroes);
	}
	else
	{
		UE_LOG(LogHexSpire, Verbose,
			TEXT("英雄配表不存在（%s），全部使用代码内建"), DefaultHeroTablePath());
	}

	if (const UDataTable* Enemies =
		LoadObject<UDataTable>(nullptr, DefaultEnemyTablePath()))
	{
		Total += ApplyEnemyTable(Enemies);
	}
	else
	{
		UE_LOG(LogHexSpire, Verbose,
			TEXT("敌人配表不存在（%s），全部使用代码内建"), DefaultEnemyTablePath());
	}

	Total += LoadVisualSets();

	return Total;
}
