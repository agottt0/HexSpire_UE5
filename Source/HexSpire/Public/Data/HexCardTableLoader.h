// Copyright Hex Spire. All Rights Reserved.
//
// DataTable → 逻辑层 的合并入口
//
// ══════════════════════════════════════════════════════════════════
// 合并语义（刻意选的，不是偷懒）
// ══════════════════════════════════════════════════════════════════
//   代码内建 12 张卡（HexContentLibrary）  = 基线，永远存在
//   DataTable 同 Id 的行                   = 覆写该卡的字段
//   DataTable 新 Id 的行                   = 追加为新卡
//
// 为什么不让 DataTable 成为唯一数据源：
//   验证器与批量模拟（10 万场）跑在 headless 下，不加载任何资产。
//   若卡池只存在于 .uasset 里，那套验证体系就得先启动完整引擎 ——
//   跑一轮从秒级变成分钟级，而它的价值恰恰在于"改完立刻能跑"。
//
//   保留代码内建当基线还有一个好处：表配错/表丢了，游戏依然可玩，
//   只是回到实测过的那套数值。这比"启动就崩"或"卡池为空"强得多。
//
// ⚠️ 覆写是【整行替换】而非字段级 diff。
//    CSV 导入时空单元格会填成类型默认值（0 / 空字符串），
//    做字段级 diff 就得区分"没填"和"填了 0"，而 DataTable
//    根本不保留这个信息 —— 于是"把费用改成 0"会被当成"没填"而失效。
//    整行替换语义简单且可预测：表里那行就是最终结果。

#pragma once

#include "CoreMinimal.h"
#include "Data/HexCardTableRow.h"

class UDataTable;
struct FHexCardData;

/** 卡牌配表的加载与合并 */
struct HEXSPIRE_API FHexCardTableLoader
{
	/**
	 * 默认表路径。
	 *
	 * ⚠️ 表不存在【不是错误】—— 那只是"还没开始配表"，
	 *    此时全部走代码内建。所以这里只在找到表时才记日志。
	 */
	static const TCHAR* DefaultTablePath();

	/**
	 * 加载默认表并把覆写合并进逻辑层卡池。
	 *
	 * 调用时机：GameMode::StartPlay 的最前面（在 StartNewRun 之前）。
	 * 之后 HexContentLibrary::FindCard 返回的就是合并后的数据。
	 *
	 * @return 实际生效的行数（0 = 没有表或表为空，全部走代码内建）
	 */
	static int32 ApplyDefaultTable();

	/**
	 * 把指定表合并进卡池。
	 * @return 实际生效的行数
	 */
	static int32 Apply(const UDataTable* Table);

	/**
	 * 取某张卡的美术配置。
	 *
	 * 表里没有该卡（或没有表）时返回 nullptr —— 调用方须回退到
	 * 按 CardType / Rarity 取默认资产。
	 *
	 * ⚠️ 返回裸指针指向表内部的行。DataTable 在编辑器里被重新导入时
	 *    行内存会失效，所以【不要缓存这个指针】，每次要用时重新查。
	 *    查表是 TMap 查找，开销可忽略。
	 */
	static const FHexCardVisualRow* FindVisual(FName CardId);

	/** 已加载的表（可能为 nullptr） */
	static const UDataTable* GetLoadedTable();
};
