// Copyright Hex Spire. All Rights Reserved.
//
// 英雄/敌人配表与外观资产的加载与合并
//
// 与 FHexCardTableLoader 同一套形状（见那个文件）：
//   代码内建是基线 → 表里同 RowName 的行整行覆写 → 新 RowName 追加
//
// ⚠️ 调用时机：GameMode::StartPlay 的最前面，在 StartNewRun【之前】。
//    顺序反了的症状是"改表没生效，重开一局才对" ——
//    RunState 在开局时就把英雄数值拷走了。

#pragma once

#include "CoreMinimal.h"

class UDataTable;
class UHexUnitVisualSet;

/** 英雄与敌人配表的加载与合并 */
struct HEXSPIRE_API FHexUnitTableLoader
{
	// ═════════════════════════════════════ 路径

	/**
	 * 默认表/资产路径。
	 *
	 * ⚠️ 不存在【不是错误】—— 那只是"还没开始配表"，
	 *    此时全部走代码内建。所以只在找到时才记日志。
	 */
	static const TCHAR* DefaultHeroTablePath();
	static const TCHAR* DefaultEnemyTablePath();

	/** 外观资产所在目录（按 DA_UnitVisual_<UnitId> 命名约定扫描）。 */
	static const TCHAR* DefaultVisualDir();

	// ═════════════════════════════════════ 应用

	/**
	 * 加载并应用全部默认配置（英雄表 + 敌人表 + 外观资产）。
	 * @return 实际生效的条目总数
	 */
	static int32 ApplyDefaults();

	/** @return 实际生效的行数 */
	static int32 ApplyHeroTable(const UDataTable* Table);

	/** @return 实际生效的行数 */
	static int32 ApplyEnemyTable(const UDataTable* Table);

	// ═════════════════════════════════════ 外观查询

	/**
	 * 取某个单位的外观资产。
	 *
	 * 没有对应资产（或还没加载过）时返回 nullptr ——
	 * 调用方须回退到 HexAppearance::For 的按队伍/等级选择。
	 *
	 * ⚠️ 这里返回的指针由 loader 持有强引用防 GC，可以跨帧持有。
	 *    这与 FHexCardTableLoader::FindVisual 的约定【相反】
	 *    （那个返回表内部的行指针，重导入会失效）——
	 *    因为这里是独立 UObject，不随表重导入而失效。
	 */
	static const UHexUnitVisualSet* FindVisualSet(FName UnitId);

	/**
	 * 扫描目录加载全部外观资产。
	 *
	 * ⚠️ 只在编辑器/带 AssetRegistry 的环境里能扫目录。
	 *    打包后走的是显式路径列表 —— 见实现里的说明。
	 *
	 * @return 加载到的资产数
	 */
	static int32 LoadVisualSets();

	/** 清空已加载的外观（验证器用它保证基线一致） */
	static void ResetVisualSets();
};
