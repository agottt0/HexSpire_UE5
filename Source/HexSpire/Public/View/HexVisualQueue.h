// Copyright Hex Spire. All Rights Reserved.
//
// 事件回放队列 —— 表现层
//
// ══════════════════════════════════════════════════════════════════
// 为什么需要队列，而不是收到事件就立刻播
// ══════════════════════════════════════════════════════════════════
// 逻辑层【瞬时算完】（纪律 3）。一张连击卡会在同一帧产出 3 条
// damage_dealt，一次 AOE 会同时产出 5 条。
//
// 立刻播的结果是：三个特效叠在同一处、三声音效同时响 ——
// 玩家看不出打了三下，也看不出打中了几个目标。
// §13.2 要求玩家能看懂发生了什么，而"同时发生"等于"看不见"。
//
// 队列把它们按顺序铺开在时间轴上。这是纯表现层的事，
// 逻辑状态早就是最终值了。
//
// ══════════════════════════════════════════════════════════════════
// 铁律：队列只能延迟【表现】，绝不能回压【逻辑】
// ══════════════════════════════════════════════════════════════════
// 一旦表现层能让逻辑"等一下"，确定性立刻消失 ——
// 同一个 seed 在两台机器上会因帧率不同产生不同结果，
// 回放对不上，纪律 5 破，而这是联机预留的前提。
//
// 所以本类【只读】事件，从不调用任何逻辑层的变更接口。
//
// ══════════════════════════════════════════════════════════════════
// 为什么"跳过动画"是免费的
// ══════════════════════════════════════════════════════════════════
// 因为逻辑与表现本来就是分离的：SkipAll 只是把队列清空。
// 战斗状态不受任何影响 —— 它在玩家点下卡牌的那一帧就已经算完了。

#pragma once

#include "CoreMinimal.h"
#include "Battle/HexBattleState.h"

class AHexBoardVisual;
class AHexUnitVisual;
class FHexFxRuntime;

/**
 * 事件回放队列。
 *
 * 由 AHexDemoGameMode 持有，每帧 Drain 后 Enqueue，然后 Tick。
 */
class HEXSPIRE_API FHexVisualQueue
{
public:
	/**
	 * 依赖注入。
	 *
	 * ⚠️ 用注入而不是让本类自己去找 GameMode：
	 *    那样会把"回放"与"某个具体 GameMode"焊死，
	 *    而战斗表现将来要在其他场景（图鉴演示、回放播放器）复用。
	 */
	void Init(
		AHexBoardVisual* InBoard,
		FHexFxRuntime* InFx,
		const TMap<int32, AHexUnitVisual*>* InUnitVisuals);

	/** 把一批事件排进队列（保持原始顺序 —— 那是逻辑的结算顺序） */
	void Enqueue(const TArray<FHexBattleEvent>& Events);

	/**
	 * 推进回放。
	 * @return 是否还有未播完的事件（UI 可据此决定是否锁输入）
	 */
	bool Tick(float DeltaSeconds);

	/**
	 * 立刻播完剩余全部事件（快速模式 / 玩家点了跳过）。
	 *
	 * ⚠️ 是"全部播出"而不是"全部丢弃"：
	 *    丢弃会让状态变化没有任何视觉痕迹 ——
	 *    玩家看到血量突然掉了一截却不知道谁打的。
	 */
	void SkipAll();

	bool IsIdle() const { return Pending.IsEmpty(); }

	int32 NumPending() const { return Pending.Num(); }

	/**
	 * 每条事件之间的间隔。
	 *
	 * ⚠️ 设为 0 就是"无动画模式"，行为仍然正确 ——
	 *    这是逻辑与表现分离的直接红利，不需要任何特殊分支。
	 */
	void SetStepInterval(float Seconds) { StepInterval = FMath::Max(0.0f, Seconds); }

private:
	/** 播放一条事件（生成特效、播音效、驱动动画） */
	void PlayOne(const FHexBattleEvent& Event);

	/** 事件对应的世界坐标：优先受击者位置，退而用施法者，再退到棋盘中心 */
	FVector ResolveWorldLocation(const FHexBattleEvent& Event) const;

	AHexUnitVisual* FindVisual(int32 UnitId) const;

	/**
	 * 没有显式 CastAnim 时，按事件类型取一个保守默认。
	 *
	 * ⚠️ 这是【兜底】。正经内容应该在卡/技能上显式声明 CastAnim ——
	 *    §13.2 要求玩家能从动作预判意图，而按事件类型推
	 *    会让近战与远程播同一个动作。
	 */
	static EHexUnitAnim DefaultAnimFor(FName EventType);

	TArray<FHexBattleEvent> Pending;

	AHexBoardVisual* Board = nullptr;
	FHexFxRuntime* Fx = nullptr;
	const TMap<int32, AHexUnitVisual*>* UnitVisuals = nullptr;

	float StepInterval = 0.18f;
	float Timer = 0.0f;
};
