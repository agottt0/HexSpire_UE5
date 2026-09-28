// Copyright Hex Spire. All Rights Reserved.

#include "View/HexVisualQueue.h"
#include "View/HexBoardVisual.h"
#include "View/HexUnitVisual.h"
#include "Fx/HexFxRuntime.h"
#include "Battle/HexBattleEventNames.h"
#include "HexSpire.h"

void FHexVisualQueue::Init(
	AHexBoardVisual* InBoard,
	FHexFxRuntime* InFx,
	const TMap<int32, AHexUnitVisual*>* InUnitVisuals)
{
	Board = InBoard;
	Fx = InFx;
	UnitVisuals = InUnitVisuals;
}

void FHexVisualQueue::Enqueue(const TArray<FHexBattleEvent>& Events)
{
	// ⚠️ 保持原顺序 append。事件顺序就是逻辑的结算顺序，
	//    重排会让「先扣格挡再扣血」看起来像「先扣血再扣格挡」。
	Pending.Append(Events);
}

bool FHexVisualQueue::Tick(float DeltaSeconds)
{
	if (Pending.IsEmpty())
	{
		return false;
	}

	Timer -= DeltaSeconds;

	// while 而不是 if：StepInterval 为 0（无动画模式）时
	// 本帧就要把队列清空，否则"跳过动画"会变成"每帧播一条"，
	// 在长连击下依然要等好几秒。
	while (!Pending.IsEmpty() && Timer <= 0.0f)
	{
		PlayOne(Pending[0]);
		Pending.RemoveAt(0);
		Timer += StepInterval;

		if (StepInterval <= 0.0f)
		{
			// 防死循环：间隔为 0 时 Timer 永远不会变正
			Timer = 0.0f;
			if (Pending.IsEmpty())
			{
				break;
			}
		}
	}

	return !Pending.IsEmpty();
}

void FHexVisualQueue::SkipAll()
{
	// 全部播出而非丢弃 —— 丢弃会让状态变化失去视觉痕迹，
	// 玩家看到血条掉了一截却不知道是谁打的。
	for (const FHexBattleEvent& E : Pending)
	{
		PlayOne(E);
	}
	Pending.Reset();
	Timer = 0.0f;
}

AHexUnitVisual* FHexVisualQueue::FindVisual(int32 UnitId) const
{
	if (!UnitVisuals || UnitId < 0)
	{
		return nullptr;
	}
	AHexUnitVisual* const* Found = UnitVisuals->Find(UnitId);
	return Found ? *Found : nullptr;
}

FVector FHexVisualQueue::ResolveWorldLocation(const FHexBattleEvent& Event) const
{
	// 优先受击者 —— 绝大多数特效（命中、流血、状态）应该出现在挨打的那个身上。
	if (const AHexUnitVisual* V = FindVisual(Event.TargetUnitId))
	{
		return V->GetActorLocation();
	}

	// 受击者已被销毁（死亡后清理）时退到施法者。
	// ⚠️ 不能直接返回零向量：那会让特效出现在世界原点，
	//    而世界原点通常在棋盘角落外，表现为"特效不见了"。
	if (const AHexUnitVisual* V = FindVisual(Event.SourceUnitId))
	{
		return V->GetActorLocation();
	}

	// 事件带了坐标就用它（地形类事件没有单位）
	if (Board && Event.CoordA != FIntVector::ZeroValue)
	{
		return Board->CellToWorld(Event.CoordA);
	}

	return Board ? Board->GetBoardCenter() : FVector::ZeroVector;
}

EHexUnitAnim FHexVisualQueue::DefaultAnimFor(FName EventType)
{
	// ══════════════════════════════════════════════════════════════
	// 这里【故意什么都不兜底】—— 受击与死亡已经有更好的实现
	// ══════════════════════════════════════════════════════════════
	// AHexUnitVisual::SyncFromUnit 用【状态差分】驱动受击与死亡：
	// 表现层自己记住上一次的 HP，掉血就播 GetHit，bIsAlive 变 false 就 SetDead。
	//
	// 那个做法比从事件推更强，原因是它【不会漏】：
	// 掉血的来源有伤害动作、状态 tick、地形危害、反伤……
	// 每条路径各发不同的事件，在这里逐个列举必然漏掉几条，
	// 而"血掉了但没有受击反馈"看起来就像卡帧。
	//
	// 所以本队列只负责【状态差分推不出来的那部分】：
	// 出手动作（打空/被闪避/纯 buff 卡时没有任何状态变化，
	// 表现层无从得知玩家做了什么）、特效、音效。
	//
	// ⚠️ 不要在这里给 damage_dealt 加默认攻击动作。
	//    伤害事件的 SourceUnitId 可能是地形或状态施加者，
	//    让它做一个攻击动作会让玩家以为"它又打了我一下"。
	return EHexUnitAnim::None;
}

void FHexVisualQueue::PlayOne(const FHexBattleEvent& Event)
{
	UWorld* World = Board ? Board->GetWorld() : nullptr;
	const FVector Loc = ResolveWorldLocation(Event);

	// ── 动画：内容声明优先，其次按事件类型兜底
	const EHexUnitAnim Anim =
		(Event.CastAnim != EHexUnitAnim::None)
			? Event.CastAnim
			: DefaultAnimFor(Event.Type);

	if (Anim != EHexUnitAnim::None)
	{
		// 出手动作播在【施法者】身上。
		//
		// ⚠️ 用 PlayOneShot 而不是 PlayAnim(bLooping=false)：
		//    单节点模式下动画播到末尾会停在最后一帧，没有"播放结束"
		//    事件可听。PlayOneShot 内部用 Timer 回 Idle，
		//    直接调 PlayAnim 会让角色永远僵在挥刀姿势。
		if (AHexUnitVisual* V = FindVisual(Event.SourceUnitId))
		{
			V->PlayOneShot(Anim);
		}
	}

	// ── 特效与音效
	if (Fx && World)
	{
		Fx->PlayVfx(
			World, Event.VfxId, Event.Type, Loc,
			FindVisual(Event.SourceUnitId), FindVisual(Event.TargetUnitId));

		Fx->PlaySfx(
			World, Event.SfxId, Event.Type, Loc,
			FindVisual(Event.SourceUnitId));
	}
}
