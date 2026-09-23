// Copyright Hex Spire. All Rights Reserved.

#include "Battle/HexEnemyAI.h"
#include "Battle/HexBattleState.h"
#include "Battle/HexUnit.h"
#include "Battle/HexGameAction.h"
#include "Battle/HexDamageCalculator.h"
#include "Hex/HexCoord.h"
#include "Hex/HexFootprint.h"
#include "Hex/HexPathfinder.h"
#include "Core/HexSpireConstants.h"

namespace
{
	/** 各 AI Profile 的攻击射程 */
	int32 AttackRangeOf(const FHexUnit& Enemy)
	{
		switch (Enemy.AIProfile)
		{
		case EHexAIProfile::Aggressive:
			// ⚠️ 扑击型射程 2，不是 1。这不是"让近战变强"，是为了让
			//    【可躲型意图真的存在】——
			//
			//    射程 1 时，攻击意图只可能在距离 1 处生成，而两段明示规则
			//    （架构文档 §4.6）规定距离 ≤1 一律转 TrackTarget。
			//    两者叠加的结果是：近战敌人【永远】产不出可躲型攻击意图。
			//    加上唯一的远程单位（投石手）天生就是追踪型，
			//    整个第一版内容里 FixedTile 攻击意图出现概率为 0 ——
			//    §13.2 要求玩家区分的"实线 vs 虚线"里，实线永不出现，
			//    玩家学不到"有些攻击能躲"这件事，走位的一半价值消失。
			//
			//    射程 2 让扑咬犬在距离 2 处锁定格子扑击（可躲，走开就落空），
			//    贴到距离 1 才咬住（追踪，躲不掉）。这正是它图鉴文本写的
			//    "走开就能躲——但它的攻击有范围，得挪够 2 格"。
			//
			//    由 VerifyAI 的"存在可躲型攻击意图"断言钉住。
			return 2;
		case EHexAIProfile::RangedKiter:
			return 4;
		case EHexAIProfile::Support:
			return 3;
		case EHexAIProfile::Summoner:
			return 3;
		case EHexAIProfile::BossPhased:
			// Boss 阶段越高射程越远（阶段 0/1/2 → 1/2/3）
			return 1 + FMath::Clamp(Enemy.BossPhase, 0, 2);
		default:
			return 1;
		}
	}

	/** 风筝型的理想距离（太近会后退） */
	int32 PreferredDistanceOf(const FHexUnit& Enemy)
	{
		if (Enemy.AIProfile == EHexAIProfile::RangedKiter)
		{
			return 3;
		}
		return 1;
	}

	/** 找最近的玩家方单位 */
	const FHexUnit* FindNearestPlayerUnit(const FHexBattleState& State, const FHexUnit& Enemy)
	{
		TArray<int32> PlayerIds;
		State.GetAlivePlayerIds(PlayerIds);

		const FHexUnit* Best = nullptr;
		int32 BestDist = TNumericLimits<int32>::Max();

		// PlayerIds 已按 id 升序 → 同距离时取 id 小者（确定性）
		for (const int32 Id : PlayerIds)
		{
			const FHexUnit* U = State.FindUnit(Id);
			if (!U)
			{
				continue;
			}
			const int32 D = Enemy.DistanceToUnit(*U);
			if (D < BestDist)
			{
				BestDist = D;
				Best = U;
			}
		}
		return Best;
	}

	/**
	 * 计算敌人朝目标移动的落点。
	 * 风筝型会保持距离；其他型贴近。
	 */
	bool ComputeMoveTarget(
		const FHexBattleState& State,
		const FHexUnit& Enemy,
		const FHexUnit& Target,
		FIntVector& OutAnchor,
		int32& OutFacing)
	{
		const int32 Budget = FMath::Max(1, 2 - Enemy.GetMovePenalty());
		const int32 Preferred = PreferredDistanceOf(Enemy);
		const int32 Range = AttackRangeOf(Enemy);

		FHexPathQuery Q;
		Q.StartAnchor = Enemy.Anchor;
		Q.StartFacing = Enemy.Facing;
		Q.Footprint = Enemy.GetFootprint();
		Q.Budget = Budget;
		Q.SelfUnitId = Enemy.Id;
		Q.RotateCost = Enemy.GetRotateCost();
		Q.bCanCrushRubble = Enemy.CanCrushRubble();
		Q.MoveCostDelta = 0;

		TMap<FIntVector, TPair<int32, int32>> Anchors;
		FHexPathfinder::ReachableAnchors(State.Grid, Q, Anchors);

		// 收集候选并排序（确定性）
		TArray<FIntVector> Candidates;
		Anchors.GetKeys(Candidates);
		Candidates.Sort([](const FIntVector& A, const FIntVector& B)
		{
			const FIntPoint OA = FHexCoord::CubeToOffset(A);
			const FIntPoint OB = FHexCoord::CubeToOffset(B);
			if (OA.Y != OB.Y)
			{
				return OA.Y < OB.Y;
			}
			return OA.X < OB.X;
		});

		FIntVector BestAnchor = Enemy.Anchor;
		int32 BestFacing = Enemy.Facing;
		int32 BestScore = TNumericLimits<int32>::Max();

		TArray<FIntVector> TargetCells;
		Target.GetCells(TargetCells);

		for (const FIntVector& Cand : Candidates)
		{
			const int32 CandFacing = Anchors[Cand].Value;

			// 从候选位置到目标的距离
			int32 Dist = TNumericLimits<int32>::Max();
			for (const FIntVector& TC : TargetCells)
			{
				Dist = FMath::Min(Dist,
					FHexFootprint::DistanceFrom(Cand, Enemy.GetFootprint(), CandFacing, TC));
			}

			// 评分：与理想距离的偏差；能进入攻击射程优先
			int32 Score = FMath::Abs(Dist - Preferred);
			if (Dist <= Range)
			{
				// 进入射程给一个大的加分（减分）
				Score -= 10;
			}

			if (Score < BestScore)
			{
				BestScore = Score;
				BestAnchor = Cand;
				BestFacing = CandFacing;
			}
		}

		if (BestAnchor == Enemy.Anchor && BestFacing == Enemy.Facing)
		{
			return false;
		}

		OutAnchor = BestAnchor;
		OutFacing = BestFacing;
		return true;
	}

	/** 求朝向目标的 facing */
	int32 FacingToward(const FHexUnit& From, const FIntVector& TargetCell)
	{
		// FacingDir(F) 应尽量指向目标
		int32 BestFacing = From.Facing;
		int32 BestDist = TNumericLimits<int32>::Max();
		const int32 D = FMath::Max(1, FHexCoord::Distance(From.Anchor, TargetCell));

		for (int32 F = 0; F < 6; ++F)
		{
			const FIntVector Probe = From.Anchor + FHexCoord::FacingDir(F) * D;
			const int32 Dist = FHexCoord::Distance(Probe, TargetCell);
			if (Dist < BestDist)
			{
				BestDist = Dist;
				BestFacing = F;
			}
		}
		return BestFacing;
	}
}

// ───────────────────────────────────────────────────────── 预计伤害

int32 FHexEnemyAI::PredictDamage(
	const FHexBattleState& State, const FHexUnit& Enemy, const FHexUnit& Target)
{
	FHexDamageContext DC;
	DC.Source = &Enemy;
	// Preview 不修改 Target，但签名需要非常量指针 —— 这里安全地去常量。
	DC.Target = const_cast<FHexUnit*>(&Target);
	DC.Flat = 0.0f;
	DC.StatRef = TEXT("ATK");
	DC.StatRatio = 1.0f;

	// ⚠️ 用 Preview（不消耗 RNG）—— 它会被 UI 每帧调用。
	const FIntPoint Range = FHexDamageCalculator::Preview(DC);
	return Range.X; // 取不暴击值作为预告（暴击是意外之喜，不该预告）
}

// ───────────────────────────────────────────────────────── 决策

void FHexEnemyAI::Decide(FHexBattleState& State, FHexUnit& Enemy)
{
	Enemy.Intent = FHexIntent();

	if (!Enemy.bIsAlive)
	{
		return;
	}

	// 眩晕 → 休眠意图（玩家能看到"这只被打断了"）
	if (Enemy.ShouldSkipTurn())
	{
		Enemy.Intent.Kind = EHexIntentKind::Sleep;
		return;
	}

	// Boss 阶段更新（按 HP 百分比切三阶段）
	if (Enemy.bIsBoss && Enemy.HPMax > 0)
	{
		const float Ratio = static_cast<float>(Enemy.HP) / static_cast<float>(Enemy.HPMax);
		Enemy.BossPhase = (Ratio > 0.66f) ? 0 : ((Ratio > 0.33f) ? 1 : 2);
	}

	const FHexUnit* Target = FindNearestPlayerUnit(State, Enemy);
	if (!Target)
	{
		Enemy.Intent.Kind = EHexIntentKind::Sleep;
		return;
	}

	const int32 Dist = Enemy.DistanceToUnit(*Target);
	const int32 Range = AttackRangeOf(Enemy);

	// ── 在射程内 → 攻击意图
	if (Dist <= Range)
	{
		Enemy.Intent.Kind = EHexIntentKind::Attack;
		Enemy.Intent.PredictedDamage = PredictDamage(State, Enemy, *Target);
		Enemy.Intent.HitCount = 1;

		// Boss 第三阶段多段攻击
		if (Enemy.bIsBoss && Enemy.BossPhase >= 2)
		{
			Enemy.Intent.Kind = EHexIntentKind::MultiAttack;
			Enemy.Intent.HitCount = 2;
		}

		// ⚠️ 两段明示规则（架构文档 §4.6）
		if (Enemy.IntentTargeting == EHexIntentTargeting::TrackTarget || Dist <= 1)
		{
			// 贴身近战 or 天生追踪型 → 咬住了，躲不掉
			Enemy.Intent.Targeting = EHexIntentTargeting::TrackTarget;
			Enemy.Intent.TrackedUnitId = Target->Id;
			// 追踪型也要填目标格（UI 显示用），但执行时会重取单位位置
			Target->GetCells(Enemy.Intent.TargetCells);
		}
		else
		{
			// 远处扑击 → 锁定格子，可躲
			Enemy.Intent.Targeting = EHexIntentTargeting::FixedTile;
			Enemy.Intent.TrackedUnitId = -1;

			// 冻结目标格：玩家当前占格 + 朝向侧若干方向
			// （给一定容错，否则走 1 格就躲掉，"可躲"变成"必躲"）
			TArray<FIntVector> Cells;
			Target->GetCells(Cells);
			Enemy.Intent.TargetCells = Cells;

			// 近战扑击额外覆盖玩家朝向侧 1 格
			if (Enemy.AIProfile == EHexAIProfile::Aggressive)
			{
				const FIntVector Ahead = Target->Anchor + Target->GetForwardDir();
				if (State.Grid.InBounds(Ahead))
				{
					Enemy.Intent.TargetCells.AddUnique(Ahead);
				}
			}
		}

		Enemy.Intent.ResultFacing = FacingToward(Enemy, Target->Anchor);
		return;
	}

	// ── 射程外 → 移动意图
	FIntVector MoveAnchor;
	int32 MoveFacing;
	if (ComputeMoveTarget(State, Enemy, *Target, MoveAnchor, MoveFacing))
	{
		Enemy.Intent.Kind = EHexIntentKind::Move;
		Enemy.Intent.Targeting = EHexIntentTargeting::FixedTile;
		Enemy.Intent.MoveToAnchor = MoveAnchor;
		Enemy.Intent.MoveToFacing = MoveFacing;
		Enemy.Intent.ResultFacing = MoveFacing;
		return;
	}

	// ── 走不动（被堵住）→ 转向意图
	const int32 NewFacing = FacingToward(Enemy, Target->Anchor);
	if (NewFacing != Enemy.Facing)
	{
		Enemy.Intent.Kind = EHexIntentKind::Rotate;
		Enemy.Intent.ResultFacing = NewFacing;
		return;
	}

	Enemy.Intent.Kind = EHexIntentKind::Sleep;
}

void FHexEnemyAI::DecideAll(FHexBattleState& State)
{
	// 按行动顺序决策（AGI 降序 + id tiebreak），保证确定性
	TArray<int32> Order;
	State.GetEnemyActionOrder(Order);

	for (const int32 Id : Order)
	{
		FHexUnit* Enemy = State.FindUnit(Id);
		if (Enemy)
		{
			Decide(State, *Enemy);
		}
	}
}

// ───────────────────────────────────────────────────────── 执行

void FHexEnemyAI::ExecuteIntent(FHexBattleState& State, FHexUnit& Enemy, FHexActionQueue& Queue)
{
	if (!Enemy.bIsAlive)
	{
		return;
	}

	// 眩晕：跳过行动（意图已在 Decide 时置为 Sleep，这里再兜一层）
	if (Enemy.ShouldSkipTurn())
	{
		State.LogEvent(TEXT("enemy_stunned"), -1, Enemy.Id);
		return;
	}

	const FHexIntent& Intent = Enemy.Intent;

	switch (Intent.Kind)
	{
	case EHexIntentKind::Attack:
	case EHexIntentKind::MultiAttack:
	{
		// ⚠️ 关键分支：意图是承诺。
		TArray<FIntVector> HitCells;

		if (Intent.Targeting == EHexIntentTargeting::TrackTarget)
		{
			// 追踪型：重取被锁定单位的【当前】位置（躲不掉）
			const FHexUnit* Tracked = State.FindUnit(Intent.TrackedUnitId);
			if (!Tracked || !Tracked->bIsAlive)
			{
				State.LogEvent(TEXT("intent_whiffed"), Enemy.Id, -1);
				break;
			}
			Tracked->GetCells(HitCells);
		}
		else
		{
			// 打空型：用【冻结的】目标格，不重算 —— 玩家走开就打空
			HitCells = Intent.TargetCells;
		}

		// 找出实际站在打击格上的玩家方单位（去重）
		TArray<int32> HitUnitIds;
		for (const FIntVector& C : HitCells)
		{
			const FHexUnit* U = State.FindUnitAtCell(C);
			if (U && U->bIsAlive && Enemy.IsHostileTo(*U))
			{
				HitUnitIds.AddUnique(U->Id);
			}
		}
		HitUnitIds.Sort();

		if (HitUnitIds.Num() == 0)
		{
			// 打空 —— 这是"可躲"意图的正常结果，是玩家走位成功的奖励
			State.LogEvent(TEXT("intent_whiffed"), Enemy.Id, -1);
			break;
		}

		const int32 Hits = FMath::Max(1, Intent.HitCount);
		for (int32 H = 0; H < Hits; ++H)
		{
			for (const int32 TargetId : HitUnitIds)
			{
				FHexUnit* Target = State.FindUnit(TargetId);
				if (!Target || !Target->bIsAlive)
				{
					continue;
				}

				// 背击判定：敌人是否位于目标后弧
				const bool bRear = Target->IsAttackedFromRear(Enemy.Anchor);

				FHexDamageContext DC;
				DC.Source = &Enemy;
				DC.Target = Target;
				DC.Flat = 0.0f;
				DC.StatRef = TEXT("ATK");
				DC.StatRatio = 1.0f;
				DC.bFromRear = bRear;

				const FHexDamageResult R = FHexDamageCalculator::Calculate(DC, &State.Rng);

				FHexGameAction A = FHexActions::Damage(
					Enemy.Id, TargetId, R.ToBarrier, R.ToBlock, R.ToHP,
					R.bIsCrit, R.bIsDodged);
				A.SourceTag = FString::Printf(TEXT("enemy:%s"), *Enemy.SourceId.ToString());
				Queue.PushBack(A);
			}
		}

		// 行动后转向（意图中已预告）
		if (Intent.ResultFacing != Enemy.Facing)
		{
			Queue.PushBack(FHexActions::RotateUnit(Enemy.Id, Intent.ResultFacing));
		}
		break;
	}

	case EHexIntentKind::Move:
	{
		// 定身：不可移动
		if (Enemy.IsMovementBlocked())
		{
			State.LogEvent(TEXT("enemy_rooted"), -1, Enemy.Id);
			break;
		}
		Queue.PushBack(FHexActions::MoveUnit(
			Enemy.Id, Intent.MoveToAnchor, Intent.MoveToFacing));
		break;
	}

	case EHexIntentKind::Rotate:
		Queue.PushBack(FHexActions::RotateUnit(Enemy.Id, Intent.ResultFacing));
		break;

	case EHexIntentKind::Debuff:
		if (!Intent.StatusId.IsNone())
		{
			const FHexUnit* Tracked = State.FindUnit(Intent.TrackedUnitId);
			if (Tracked)
			{
				// ⚠️ 末参数传 Enemy.Id：归属必须对称。
				//    玩家的毒算玩家击杀，敌人的毒就得算敌人击杀 ——
				//    否则敌人毒死玩家时会被判成"环境击杀"，
				//    将来做"复仇/受击反制"类符文会全部失准。
				Queue.PushBack(FHexActions::ApplyStatus(
					Tracked->Id, Intent.StatusId, Intent.StatusStacks, Enemy.Id));
			}
		}
		break;

	case EHexIntentKind::Buff:
		if (!Intent.StatusId.IsNone())
		{
			Queue.PushBack(FHexActions::ApplyStatus(
				Enemy.Id, Intent.StatusId, Intent.StatusStacks, Enemy.Id));
		}
		break;

	case EHexIntentKind::Sleep:
	default:
		break;
	}
}
