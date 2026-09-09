// Copyright Hex Spire. All Rights Reserved.
//
// D6：统一触发时机分发 —— 策划案 §6.3 / §6.5
//
// ══ 五条保证机制（R7 / §6.5）══
// 1. 监听者数组是【唯一顺序来源】，禁止哈希容器遍历：
//      [英雄被动(虚拟槽0)] → [符文槽1..6 跳空] → [装备 武器,盔甲,饰品]
//      → [状态：按 (UnitId, StatusId, 实例下标) 显式排序]
// 2. Emit 入口【冻结】监听者列表 —— 遍历中被改会造成未定义行为与不确定性
// 3. 递归深度超限 → 停止分发 + 写违规记录，【绝不抛异常】
// 4. MaxPerRound / MaxPerBattle 计数键用实例 uid（非 id，避免同名符文串号）
// 5. 单次 Emit 触发总数上限 —— 兜住"A 触发 B、B 触发 A"这种
//    深度=2 但宽度爆炸的组合（策划案 R7 只提了深度，这条是补的）
//
// ⚠️ 为什么埋点必须一次埋满：
//    事后补钩子必然漏 —— OnBlockBroken / OnDeckReshuffled 这类冷门时机
//    最容易忘。埋点时监听者为空几乎零成本，回填却要重读整个战斗流程
//    （架构文档 §8 原话）。

#pragma once

#include "CoreMinimal.h"
#include "Core/HexSpireEnums.h"
#include "Battle/HexCardData.h"
#include "Battle/HexDamageCalculator.h"

class FHexBattleState;
class FHexActionQueue;
struct FHexRuneData;
struct FHexRuneTrigger;

/** 触发时的上下文（纯数据） */
struct HEXSPIRECORE_API FHexTriggerContext
{
	int32 SourceUnitId = -1;
	int32 TargetUnitId = -1;

	/** 触发源卡牌（OnCardPlayed 等时机用于过滤） */
	FName CardId;
	EHexCardType CardType = EHexCardType::Attack;
	int32 CardCost = 0;
	TArray<FName> CardTags;

	/** 数值上下文（伤害量、格挡量等） */
	int32 IntA = 0;
};

/**
 * 单个监听者条目。
 *
 * SlotOrder 是排序键，语义固定：
 *   0      = 英雄被动（虚拟槽）
 *   1..6   = 符文槽 1..6
 *   10..12 = 装备（武器/盔甲/饰品）
 *   20+    = 状态效果
 * 这个分层保证「符文永远在装备之前结算、装备永远在状态之前」，
 * 是 §6.5 顺序语义的基础。
 */
struct HEXSPIRECORE_API FHexTriggerListener
{
	/** 实例唯一 id —— 计数键用它，避免同名符文互相干扰 */
	int32 SourceUid = 0;
	FString SourceTag;
	int32 SlotOrder = 0;

	EHexTriggerTiming Timing = EHexTriggerTiming::OnRoundStart;

	/** 指向符文触发器定义（生命周期由 RuneData 保证） */
	const FHexRuneTrigger* Trigger = nullptr;
};

class HEXSPIRECORE_API FHexTriggerBus
{
public:
	/**
	 * 重建监听者缓存。符文/装备/状态变化时必须调用。
	 * 会自动按 SlotOrder 升序、同序按 SourceUid 升序排序（确定性）。
	 */
	void RebuildListeners(const FHexBattleState& State);

	void ResetRoundCounters();
	void ResetBattleCounters();

	/**
	 * 分发一个时机。
	 * 符文效果会转成动作并用 PushNext 插入，保持槽位顺序语义。
	 */
	void Emit(
		EHexTriggerTiming Timing,
		const FHexTriggerContext& Ctx,
		FHexBattleState& State,
		FHexActionQueue& Queue);

	/**
	 * ②′ 的数值钩子链：按槽位顺序依次作用于 running value。
	 *
	 * ⚠️ 这就是 §6.5「[锐化,倍化]=21 而 [倍化,锐化]=19」的实现处。
	 * @return 无监听者时返回空钩子
	 */
	FHexValueHook MakeValueHook(
		EHexTriggerTiming Timing,
		const FHexBattleState& State,
		const FHexTriggerContext& Ctx) const;

	int32 ListenerCount() const { return Listeners.Num(); }

	int32 ListenerCountFor(EHexTriggerTiming Timing) const;

	/**
	 * 调试：完整触发链。
	 * 服务 §13.3 的「符文实验室」UI —— 这是 R8（符文看不懂）的解药。
	 */
	void DescribeChain(EHexTriggerTiming Timing, TArray<FString>& Out) const;

private:
	/** 过滤器判定：卡类型 / 标签 / 费用区间 */
	static bool PassesFilter(const FHexRuneTrigger& Trigger, const FHexTriggerContext& Ctx);

	/** 条件判定 */
	static bool PassesCondition(
		const FHexEffectCondition& Condition,
		const FHexBattleState& State,
		const FHexTriggerContext& Ctx);

	bool CanFire(const FHexTriggerListener& L) const;
	void MarkFired(const FHexTriggerListener& L);

	static FString CountKey(const FHexTriggerListener& L);

	TArray<FHexTriggerListener> Listeners;
	TMap<FString, int32> RoundCounts;
	TMap<FString, int32> BattleCounts;
	int32 Depth = 0;
};
