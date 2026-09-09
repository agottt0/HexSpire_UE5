// Copyright Hex Spire. All Rights Reserved.
//
// 敌人 AI 与意图 —— 策划案 §8.7
//
// ══ 意图必须是承诺 ══
// Decide() 在生成意图时【冻结全部结算参数】（目标格集合 / 伤害值），
// ExecuteIntent() 只重放，不重算。
//
// 这是"打空型 vs 追踪型"能区分的前提，而 §13.2 明确要求玩家能读出
// 「意图是否可躲」——这是核心决策信息：
//   · FixedTile   → 锁定格子，玩家走开就打空（可躲，实线显示）
//   · TrackTarget → 锁定单位，跟着玩家走（躲不掉，虚线+连线显示）
//
// 两段明示规则（架构文档 §4.6）：
//   · 贴身（距离 ≤ 1）近战 → 转 TrackTarget（咬住了，躲不掉）
//   · 远处扑击 → FixedTile，覆盖玩家占格 + 朝向侧若干方向
//
// ⚠️ 若执行时重算，玩家就无法通过走位规避 —— 这会让整个"预警 + 走位"
//    的核心循环（P2 支柱：空间即决策）失效。

#pragma once

#include "CoreMinimal.h"
#include "Core/HexSpireEnums.h"

class FHexBattleState;
class FHexUnit;
class FHexActionQueue;

struct HEXSPIRECORE_API FHexEnemyAI
{
	/**
	 * 为一个敌人决策下回合意图，并【冻结】结算参数写入 Unit.Intent。
	 * ⚠️ 会消耗 RNG（AI 的随机选择走 Combat 流）。
	 */
	static void Decide(FHexBattleState& State, FHexUnit& Enemy);

	/**
	 * 执行已冻结的意图。只重放，不重算。
	 * 产生的动作推入队列。
	 */
	static void ExecuteIntent(FHexBattleState& State, FHexUnit& Enemy, FHexActionQueue& Queue);

	/** 为全部存活敌人生成意图（按行动顺序，确定性） */
	static void DecideAll(FHexBattleState& State);

	/**
	 * 预计伤害（意图显示用）。
	 * ⚠️ 不消耗 RNG —— 它会被 UI 反复调用。
	 */
	static int32 PredictDamage(const FHexBattleState& State, const FHexUnit& Enemy, const FHexUnit& Target);
};
