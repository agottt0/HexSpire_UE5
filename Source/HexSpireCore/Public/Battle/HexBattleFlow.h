// Copyright Hex Spire. All Rights Reserved.
//
// 回合状态机与出牌流程 —— 策划案 §8.4 / §7.4.2
//
// ══ 完整回合流程（§8.4）══
//   BattleStart
//     ├─ 生成地形 & 敌人（含体型合法性检查）
//     ├─ 玩家放置于 (4,1) facing=2，按体型展开 footprint
//     ├─ 卡组洗混 → 抽牌堆
//     ├─ Emit(OnBattleStart)
//     ├─ 抽 5 张
//     └─ 生成敌方首个意图并显示
//        ↓
//   ┌──► RoundStart
//   │     ├─ 回合数 +1
//   │     ├─ Emit(OnRoundStart)（符文按槽位 1→6 结算）
//   │     ├─ 状态 tick（按各自 TickTiming）
//   │     ├─ 抽 D 张（抽牌堆空 → 洗回 → Emit(OnDeckReshuffled)）
//   │     └─ 体力 = 体力上限
//   │        ↓
//   │    PlayerPhase（玩家自由操作，无时限）
//   │        ↓
//   │    RoundEndPlayer
//   │     ├─ Emit(OnRoundEnd)
//   │     ├─ 弃掉全部手牌（每张 Emit(OnCardDiscarded)）
//   │     ├─ Emit(OnEnergyLeftover) → 体力清零
//   │     └─ 格挡清空（镇妖者被动除外）
//   │        ↓
//   │    EnemyPhase（按 AGI 降序依次行动）
//   │     ├─ 每个敌人：执行已预告的意图
//   │     └─ 生成下回合意图并立即显示
//   │        ↓
//   │    RoundEndAll
//   │     ├─ 危害地面 tick & 衰减
//   │     ├─ 状态持续时间 -1 / 过期移除
//   │     └─ 检查胜负
//   │        ↓
//   └──── (未结束) 回到 RoundStart
//
// ⚠️ 逻辑【瞬时算完】，不等待动画（纪律 3）。
//    一次 PlayCard() 返回时战斗状态已经全部算完，动画只是事后回放。

#pragma once

#include "CoreMinimal.h"
#include "Core/HexSpireEnums.h"
#include "Battle/HexGameAction.h"
#include "Battle/HexTriggerBus.h"

class FHexBattleState;
struct FHexCardData;
struct FHexBattleEvent;

/** 出牌尝试的结果 */
enum class EHexPlayResult : uint8
{
	Success,
	NotPlayerPhase,
	CardNotInHand,
	NotEnoughEnergy,
	IllegalTarget,
	CardNotFound,
};

/**
 * 战斗流程控制器。
 *
 * 持有 State 的引用而非拷贝 —— 它是无状态的流程逻辑，
 * 全部状态都在 BattleState 里（这样快照 Undo 只需拷 State）。
 */
class HEXSPIRECORE_API FHexBattleFlow
{
public:
	explicit FHexBattleFlow(FHexBattleState& InState);

	/** 卡牌定义查询回调 —— 由内容库注入，避免 core 依赖具体内容 */
	using FCardLookup = TFunction<const FHexCardData*(FName /*CardId*/)>;
	void SetCardLookup(FCardLookup InLookup) { CardLookup = MoveTemp(InLookup); }

	// ───────────────────────────────────────────── 流程

	/**
	 * 战斗开始：洗牌、抽牌、生成意图。
	 * ⚠️ 内部会调用 BeginRound —— 返回时已在【第 1 回合的玩家阶段】，
	 *    调用方不要再手动开回合。
	 */
	void BeginBattle();

	/**
	 * 玩家主动结束回合 → 走完 RoundEndPlayer / EnemyPhase / RoundEndAll，
	 * 并自动进入下一回合。
	 * ⚠️ 返回时已在【下一回合的玩家阶段】（除非战斗已结束）。
	 */
	void EndPlayerTurn();

	/**
	 * 打出一张手牌。
	 * @param CardUid    手牌实例 uid
	 * @param TargetCell 目标格（SELF 类卡传任意值）
	 */
	EHexPlayResult PlayCard(int32 CardUid, const FIntVector& TargetCell);

	/** 战斗是否已结束 */
	bool IsBattleOver() const;

	/**
	 * 某触发时机在本场战斗中被派发了多少次。
	 *
	 * ⚠️ 存在的唯一目的是让验证器能断言「每个时机都真的有埋点」。
	 *    漏埋点不会报错，只会让挂在该时机上的符文静默失效 ——
	 *    详见 VerifyTrigger 的 CheckTimingCoverage。
	 */
	int32 GetTimingFireCount(EHexTriggerTiming Timing) const
	{
		return TriggerBus.GetEmitCount(Timing);
	}

	// ───────────────────────────────────────────── 查询（UI 用）

	/**
	 * 卡牌的实际费用（经 RuleBook 的规则改写）。
	 */
	int32 GetCardCost(int32 CardUid) const;

	/** 卡牌能否打出（体力够 + 有合法目标） */
	bool CanPlayCard(int32 CardUid) const;

	/**
	 * 合法目标格（UI 高亮"我能点哪"）。
	 */
	void GetLegalTargets(int32 CardUid, TArray<FIntVector>& Out) const;

	/**
	 * 波及格（UI 高亮"会打到哪"）。
	 */
	void GetAffectedCells(int32 CardUid, const FIntVector& TargetCell, TArray<FIntVector>& Out) const;

	/**
	 * 伤害预览：返回 (不暴击, 暴击)。
	 * ⚠️ 不消耗 RNG（§13.2 要求悬停显示预计伤害；若消耗 RNG 会污染确定性）。
	 */
	FIntPoint PreviewDamage(int32 CardUid, const FIntVector& TargetCell) const;

	/** 玩家可移动到的锚点（含代价），移动卡的 UI 高亮用 */
	void GetReachableAnchors(int32 MoveBudget, TMap<FIntVector, TPair<int32, int32>>& Out) const;

	FHexTriggerBus& GetTriggerBus() { return TriggerBus; }
	const FHexTriggerBus& GetTriggerBus() const { return TriggerBus; }

private:
	/**
	 * 回合开始。
	 *
	 * ⚠️ 私有：它【只能】由 BeginBattle 与 EndPlayerTurn 调用。
	 *    外部再调一次会让回合数翻倍、手牌被重复抽取、
	 *    战斗永不终止 —— 集成测试第一次跑时就踩了这个坑。
	 */
	void BeginRound();

	/** 执行一个效果步骤，产出动作 */
	void ExecuteStep(
		const FHexEffectStep& Step,
		const FHexCardData& Card,
		const FIntVector& TargetCell);

	/** 状态 tick（按时机） */
	void TickStatuses(EHexStatusTick Timing);

	/** 敌方阶段 */
	void RunEnemyPhase();

	/** 回合总结束：危害 tick、状态衰减、胜负判定 */
	void RunRoundEndAll();

	/** 结算胜负；返回是否已结束 */
	bool CheckBattleEnd();

	/** 进入胜利后的探索拾取阶段（§7.3 无限体力） */
	void EnterExplorePhase();

	const FHexCardData* LookupCard(FName CardId) const;

	/**
	 * 由 uid 找到卡实例 —— 手牌与固定卡的统一入口。
	 * 任何"按 uid 查卡"都必须经过它，否则固定卡会被漏掉。
	 */
	const struct FHexCardInstance* InstFromUid(int32 CardUid) const;

	/** 由 uid 找到卡牌定义（手牌或固定卡） */
	const FHexCardData* CardFromUid(int32 CardUid) const;

	// ─────────────────────────────────────────── 触发派发（§6.3）

	/**
	 * 结算动作队列 —— 【本类唯一允许的结算入口】。
	 *
	 * ⚠️ 不要再直接写 Queue.ResolveAll(State)：
	 *    那样会绕过下面的触发翻译，符文重新变成静默失效。
	 */
	int32 ResolveQueue();

	/**
	 * 把「刚执行完的动作 + 它产生的事件」翻译成触发时机并派发。
	 * 这是 §6.3 时机表中动作型时机的【唯一】埋点处。
	 * 分工表见 .cpp 实现处的注释。
	 */
	void DispatchTriggersForAction(
		const FHexGameAction& Action,
		TArrayView<const FHexBattleEvent> NewEvents,
		FHexActionQueue& InQueue);

	/** 带连锁预算地 Emit 一个时机 */
	void EmitWithBudget(
		EHexTriggerTiming Timing,
		const FHexTriggerContext& Ctx,
		FHexActionQueue& InQueue);

	/** 该单位是否属于玩家方（决定"我造成"还是"我承受"） */
	bool IsPlayerSide(int32 UnitId) const;

	FHexBattleState& State;
	FHexActionQueue Queue;
	FHexTriggerBus TriggerBus;
	FCardLookup CardLookup;

	/**
	 * 本次 ResolveQueue 内，翻译器还能 Emit 多少次。
	 * 见 HexK::MaxObserverEmitsPerResolve —— 它堵的是
	 * 递归深度闸盖不住的「符文自激」。
	 */
	int32 ObserverEmitBudget = 0;
};
