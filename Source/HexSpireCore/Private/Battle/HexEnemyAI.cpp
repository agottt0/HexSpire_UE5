// Copyright Hex Spire. All Rights Reserved.

#include "Battle/HexEnemyAI.h"
#include "Battle/HexBattleEventNames.h"
#include "Battle/HexEnemySkillData.h"
#include "Content/HexEnemySkillLibrary.h"
#include "Battle/HexTargetResolver.h"
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
	/**
	 * 各 AI Profile 的攻击射程。
	 *
	 * ⚠️ 配表的 AttackRangeOverride 优先，但【Boss 不吃覆写】——
	 *    BossPhased 的射程随阶段变化（1/2/3），是"阶段推进"这件事
	 *    在射程上的体现。让表里一个固定数字盖掉它，Boss 三阶段就
	 *    只剩伤害差别，而玩家读不到"它变强了"的空间信号。
	 *    想改 Boss 射程请改下面的 case，或给它配技能（技能有自己的射程）。
	 */
	int32 AttackRangeOf(const FHexUnit& Enemy)
	{
		if (Enemy.AttackRangeOverride > 0
			&& Enemy.AIProfile != EHexAIProfile::BossPhased)
		{
			return Enemy.AttackRangeOverride;
		}

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

	/**
	 * 风筝型的理想距离（太近会后退）。
	 *
	 * ⚠️ 只对 RangedKiter 生效。给近战也读这个值会让"贴身"变成可配置项，
	 *    而近战不贴身就永远打不到人 —— 配表一个手滑就能让整只怪变成
	 *    原地转圈的摆设，且不报错。
	 */
	int32 PreferredDistanceOf(const FHexUnit& Enemy)
	{
		if (Enemy.AIProfile == EHexAIProfile::RangedKiter)
		{
			return FMath::Max(1, Enemy.PreferredDistance);
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
		// 移动预算来自配表（缓迟等状态在此扣减）
		const int32 Budget = FMath::Max(1, Enemy.MoveBudget - Enemy.GetMovePenalty());
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

	// ══════════════════════════════════════════════════════════════
	// 技能选取
	// ══════════════════════════════════════════════════════════════
	//
	// ⚠️ 全程【零 RNG】。敌人选技能若带随机，玩家就无法总结
	//    "它血低了会硬化"这类规律，而可学习性正是 §13.2 的要求，
	//    也是"预警 + 走位"循环能被玩家掌握的前提。
	//    确定性还让同 seed 回放逐位一致（纪律 1）。

	/** 技能的非空间门槛（冷却 / 回合 / Boss 阶段 / 自身血线） */
	bool SkillGatesPass(const FHexUnit& Enemy, const FHexEnemySkillData& Skill, int32 RoundNumber)
	{
		if (Enemy.GetSkillCooldown(Skill.Id) > 0)
		{
			return false;
		}
		if (RoundNumber < FMath::Max(1, Skill.FirstUsableRound))
		{
			return false;
		}
		if (Skill.MinBossPhase > 0 && Enemy.BossPhase < Skill.MinBossPhase)
		{
			return false;
		}
		if (Skill.UseBelowSelfHPRatio > 0.0f)
		{
			// ⚠️ HPMax 为 0 时不能做除法。理论上 MakeEnemyUnit 保证 >= 1，
			//    但验证器与配表都可以手工构造单位，除零会直接产出 NaN
			//    并让这条门槛变成"随机通过"。
			if (Enemy.HPMax <= 0)
			{
				return false;
			}
			const float Ratio = static_cast<float>(Enemy.HP) / static_cast<float>(Enemy.HPMax);
			if (Ratio > Skill.UseBelowSelfHPRatio)
			{
				return false;
			}
		}
		return true;
	}

	/**
	 * 技能的预计伤害（把全部 DealDamage 步骤按 §7.5 系数化累加）。
	 *
	 * ⚠️ 与 PredictDamage 一样【不消耗 RNG】：它既要给 UI 显示，
	 *    也要被冻结进意图当作执行时的实际值。
	 *    这里用 Preview 而非 Calculate，两者的差别就是 RNG（暴击/闪避）。
	 */
	int32 PredictSkillDamage(
		const FHexUnit& Enemy, const FHexUnit& Target, const FHexEnemySkillData& Skill)
	{
		int32 Total = 0;

		for (const FHexEffectStep& Step : Skill.Effects)
		{
			if (Step.Op != EHexEffectOp::DealDamage)
			{
				continue;
			}

			FHexDamageContext DC;
			DC.Source = &Enemy;
			DC.Target = const_cast<FHexUnit*>(&Target);
			DC.Flat = Step.FlatValue;
			DC.StatRef = Step.StatRef;
			DC.StatRatio = Step.StatRatio;

			const FIntPoint R = FHexDamageCalculator::Preview(DC);
			Total += R.X * FMath::Max(1, Step.Repeat);
		}

		return Total;
	}

	/**
	 * 挑一条本回合可用的技能，并算出它的波及格。
	 *
	 * Enemy.SkillIds 已按优先级降序排好（见 GetEnemySkills），
	 * 所以这里"取第一条全部门槛通过的"即可 —— 无需再排序。
	 *
	 * @param OutCells 冻结用的波及格（已含多格单位的完整占位）
	 * @return 选中的技能；没有可用技能返回 nullptr（调用方回退到默认攻击）
	 */
	const FHexEnemySkillData* SelectSkill(
		const FHexBattleState& State,
		const FHexUnit& Enemy,
		const FHexUnit& Target,
		TArray<FIntVector>& OutCells)
	{
		OutCells.Reset();

		for (const FName& Sid : Enemy.SkillIds)
		{
			const FHexEnemySkillData* Skill = FHexEnemySkillLibrary::Find(Sid);
			if (!Skill)
			{
				continue;
			}
			if (!SkillGatesPass(Enemy, *Skill, State.RoundNumber))
			{
				continue;
			}

			// ── 自身向技能（强化/治疗/格挡）：目标就是自己，不需要空间判定
			if (Skill->TargetSpec.Shape == EHexTargetShape::SelfShape)
			{
				Enemy.GetCells(OutCells);
				return Skill;
			}

			// ── 其余技能必须真的打得到目标
			//
			// ⚠️ 这里用 TargetResolver 而不是简单的距离比较：
			//    技能可以是 Burst / Line / Cone，"距离够"不等于"打得到"，
			//    而且视线判定（bRequiresLineOfSight）也只有 Resolver 懂。
			//    自己写一套近似判定必然与实际波及格不一致 ——
			//    结果是预警画一片、实际打另一片。
			TArray<FIntVector> TargetCells;
			Target.GetCells(TargetCells);

			// 目标格取"目标占据的格"里第一个合法的（TargetCells 顺序确定）
			for (const FIntVector& TC : TargetCells)
			{
				if (!FHexTargetResolver::IsLegalTarget(State, Enemy, Skill->TargetSpec, TC))
				{
					continue;
				}

				FHexTargetResolver::AffectedCells(
					State, Enemy, Skill->TargetSpec, TC, OutCells);

				if (OutCells.Num() > 0)
				{
					return Skill;
				}
			}
		}

		return nullptr;
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

	// ══════════════════════════════════════════════════════════════
	// ── 技能优先（仅当该敌人配了技能）
	// ══════════════════════════════════════════════════════════════
	//
	// ⚠️ 未配技能的敌人【完全不进这个分支】，走下面的 profile 默认攻击，
	//    行为与技能系统存在之前逐位一致。现有四只敌人的数值是 Godot 版
	//    实测调过的，不该因为多了一层框架而被动改变。
	//
	// ⚠️ 这里就把技能、波及格、伤害全部【冻结】进意图。
	//    执行阶段只按 Intent.SkillId 重放，不重选、不重算 ——
	//    否则玩家看到的预警范围与实际打击范围会不一致（§8.7）。
	if (Enemy.SkillIds.Num() > 0)
	{
		TArray<FIntVector> SkillCells;
		if (const FHexEnemySkillData* Skill = SelectSkill(State, Enemy, *Target, SkillCells))
		{
			Enemy.Intent.Kind = Skill->IntentKind;
			Enemy.Intent.SkillId = Skill->Id;
			Enemy.Intent.HitCount = FMath::Max(1, Skill->HitCount);
			Enemy.Intent.TargetCells = SkillCells;
			Enemy.Intent.PredictedDamage =
				Skill->HasDamageStep()
					? PredictSkillDamage(Enemy, *Target, *Skill) * Enemy.Intent.HitCount
					: 0;

			// 自身向技能不需要可躲性 —— 它打的是自己，玩家走位无关
			if (Skill->TargetSpec.Shape == EHexTargetShape::SelfShape)
			{
				Enemy.Intent.Targeting = EHexIntentTargeting::FixedTile;
				Enemy.Intent.TrackedUnitId = -1;
			}
			else if (Skill->bOverrideTargeting)
			{
				// 技能显式指定可躲性：可以做出"平时可躲、大招锁人"的敌人
				Enemy.Intent.Targeting = Skill->Targeting;
				Enemy.Intent.TrackedUnitId =
					(Skill->Targeting == EHexIntentTargeting::TrackTarget) ? Target->Id : -1;
			}
			else if (Enemy.IntentTargeting == EHexIntentTargeting::TrackTarget || Dist <= 1)
			{
				// 沿用两段明示规则（架构文档 §4.6）：贴身近战一律转追踪
				Enemy.Intent.Targeting = EHexIntentTargeting::TrackTarget;
				Enemy.Intent.TrackedUnitId = Target->Id;
			}
			else
			{
				Enemy.Intent.Targeting = EHexIntentTargeting::FixedTile;
				Enemy.Intent.TrackedUnitId = -1;
			}

			Enemy.Intent.ResultFacing = FacingToward(Enemy, Target->Anchor);
			return;
		}

		// 没有可用技能（全在冷却 / 够不到）→ 落到下面的默认路径。
		// ⚠️ 刻意不在这里 return Sleep：那会让"技能冷却中"表现为
		//    敌人整回合站着不动，而玩家读不出原因，只会觉得 AI 坏了。
	}

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

// ───────────────────────────────────────────────────────── 技能执行

void FHexEnemyAI::ExecuteSkill(
	FHexBattleState& State,
	FHexUnit& Enemy,
	const FHexEnemySkillData& Skill,
	FHexActionQueue& Queue)
{
	const FHexIntent& Intent = Enemy.Intent;
	const FString SrcTag = FString::Printf(TEXT("enemy_skill:%s"), *Skill.Id.ToString());

	// ── 解析打击格
	//
	// ⚠️ 与默认攻击完全相同的两分支语义（§8.7）：
	//    追踪型重取被锁定单位的当前位置；可躲型用【冻结的】格子。
	//    这段逻辑与下面 ExecuteIntent 的 Attack 分支刻意保持镜像 ——
	//    两者若在"可躲性怎么解析"上产生分歧，就会出现
	//    "普通攻击能躲、技能躲不掉"这种玩家无法理解的不一致。
	TArray<FIntVector> HitCells;
	if (Intent.Targeting == EHexIntentTargeting::TrackTarget)
	{
		const FHexUnit* Tracked = State.FindUnit(Intent.TrackedUnitId);
		if (!Tracked || !Tracked->bIsAlive)
		{
			State.LogEvent(HexEv::IntentWhiffed, Enemy.Id, -1);
			return;
		}
		Tracked->GetCells(HitCells);
	}
	else
	{
		HitCells = Intent.TargetCells;
	}

	// ── 收集打击格上的敌对单位（去重 + 排序）
	//
	// ⚠️ 去重是 §8.2.2 机制点 2：多格单位只结算一次伤害，
	//    否则 L 体型（占 6 格）会被一个 Burst 打 6 次。
	TArray<int32> HostileIds;
	for (const FIntVector& C : HitCells)
	{
		const FHexUnit* U = State.FindUnitAtCell(C);
		if (U && U->bIsAlive && Enemy.IsHostileTo(*U))
		{
			HostileIds.AddUnique(U->Id);
		}
	}
	HostileIds.Sort();

	const int32 Hits = FMath::Max(1, Skill.HitCount);

	// 自身向技能没有敌对目标也要正常生效（强化/治疗/格挡）
	const bool bSelfOnly = Skill.TargetSpec.Shape == EHexTargetShape::SelfShape;

	if (!bSelfOnly && HostileIds.Num() == 0)
	{
		// 打空 —— 可躲型意图的正常结果，是玩家走位成功的奖励
		State.LogEvent(HexEv::IntentWhiffed, Enemy.Id, -1);
		return;
	}

	{
		FHexBattleEvent E;
		E.Type = HexEv::EnemySkillBegin;
		E.SourceUnitId = Enemy.Id;
		E.NameA = Skill.Id;
		E.IntA = Hits;
		State.LogEvent(E);
	}

	for (int32 H = 0; H < Hits; ++H)
	{
		for (const FHexEffectStep& Step : Skill.Effects)
		{
			// 本步骤产出的动作自动带上表现意图。
			// ⚠️ 必须在 switch 之前、循环【内部】构造：
			//    放到循环外的话，多段攻击的每一段都会共用第一个 step
			//    的特效；而放到某个 case 里又会漏掉其他 case。
			const FHexActionQueue::FVisualScope VisualScope(
				Queue, Step.VfxId, Step.SfxId, Skill.CastAnim);

			// 施加给自己的步骤：目标是敌人自身，与打击格无关
			const bool bOnSelf = Step.TargetFilter == EHexTargetFilter::Self || bSelfOnly;

			switch (Step.Op)
			{
			case EHexEffectOp::DealDamage:
			{
				const int32 Repeat = FMath::Max(1, Step.Repeat);
				for (int32 R = 0; R < Repeat; ++R)
				{
					for (const int32 TargetId : HostileIds)
					{
						FHexUnit* Target = State.FindUnit(TargetId);
						if (!Target || !Target->bIsAlive)
						{
							continue;
						}

						// 背击判定（§8.2.3）：敌人也吃这条规则
						const bool bRear = Target->IsAttackedFromRear(Enemy.Anchor);

						FHexDamageContext DC;
						DC.Source = &Enemy;
						DC.Target = Target;
						DC.Flat = Step.FlatValue;
						DC.StatRef = Step.StatRef;
						DC.StatRatio = Step.StatRatio;
						DC.bFromRear = bRear;
						DC.Tag = SrcTag;

						// ⚠️ 走同一个 DamageCalculator（§4.4 唯一实现）。
						//    敌人侧另算一套伤害必然与卡牌侧漂移。
						const FHexDamageResult Res =
							FHexDamageCalculator::Calculate(DC, &State.Rng);

						FHexGameAction A = FHexActions::Damage(
							Enemy.Id, TargetId, Res.ToBarrier, Res.ToBlock, Res.ToHP,
							Res.bIsCrit, Res.bIsDodged);
						A.SourceTag = SrcTag;
						Queue.PushBack(A);
					}
				}
				break;
			}

			case EHexEffectOp::ApplyStatus:
			{
				if (bOnSelf)
				{
					FHexGameAction A = FHexActions::ApplyStatus(
						Enemy.Id, Step.StatusId, Step.StatusStacks);
					A.SourceTag = SrcTag;
					Queue.PushBack(A);
				}
				else
				{
					for (const int32 TargetId : HostileIds)
					{
						FHexGameAction A = FHexActions::ApplyStatus(
							TargetId, Step.StatusId, Step.StatusStacks);
						A.SourceTag = SrcTag;
						Queue.PushBack(A);
					}
				}
				break;
			}

			case EHexEffectOp::GainBlock:
			{
				// ⚠️ 格挡恒定给【自己】。"给敌方格挡"没有任何设计意义，
				//    而配表手滑把 TargetFilter 填成 Enemy 时若照做，
				//    结果是敌人给玩家送格挡 —— 荒谬且极难察觉。
				const int32 Amount = FHexDamageCalculator::CalculateBlock(
					&Enemy, Step.FlatValue, Step.StatRef, Step.StatRatio, 1.0f);

				FHexGameAction A = FHexActions::GainBlock(Enemy.Id, Amount);
				A.SourceTag = SrcTag;
				Queue.PushBack(A);
				break;
			}

			case EHexEffectOp::Heal:
			{
				// 同上：治疗恒定给自己
				FHexGameAction A = FHexActions::Heal(Enemy.Id, Step.ValueFor(&Enemy));
				A.SourceTag = SrcTag;
				Queue.PushBack(A);
				break;
			}

			case EHexEffectOp::Knockback:
			case EHexEffectOp::Pull:
			{
				for (const int32 TargetId : HostileIds)
				{
					const int32 Dist =
						(Step.Op == EHexEffectOp::Pull) ? -Step.Distance : Step.Distance;
					FHexGameAction A = FHexActions::Knockback(Enemy.Id, TargetId, Dist);
					A.SourceTag = SrcTag;
					Queue.PushBack(A);
				}
				break;
			}

			default:
				// ⚠️ 记违规而非静默失效。策划配了个敌人侧还没实现的 op
				//    （如 Summon / ModifyTerrain）时，必须能从违规记录里看到，
				//    否则表现是"这个技能好像没效果"，只能靠肉眼测出来。
				State.AddRuleViolation(
					TEXT("unimplemented_enemy_op"),
					FString::Printf(TEXT("敌人技能 %s 的效果 op=%d 未实现"),
						*Skill.Id.ToString(), static_cast<int32>(Step.Op)));
				break;
			}
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
		State.LogEvent(HexEv::EnemyStunned, -1, Enemy.Id);
		return;
	}

	const FHexIntent& Intent = Enemy.Intent;

	// ══════════════════════════════════════════════════════════════
	// 技能重放（意图里冻结了 SkillId 时走这条路）
	// ══════════════════════════════════════════════════════════════
	//
	// ⚠️ 只按冻结的数据重放，不重选技能、不重算波及格。
	//    Decide 与 Execute 之间玩家已经行动过了（走位、打牌），
	//    在这里重算等于让敌人"看到玩家的应对之后再决定打哪"——
	//    走位规避彻底失效，而且不报任何错。
	if (!Intent.SkillId.IsNone())
	{
		if (const FHexEnemySkillData* Skill = FHexEnemySkillLibrary::Find(Intent.SkillId))
		{
			ExecuteSkill(State, Enemy, *Skill, Queue);

			// ⚠️ 冷却在【执行后】才置，而不是 Decide 里选中时置。
			//    在 Decide 里置的话，被眩晕打断的技能也会进冷却 ——
			//    玩家花一张眩晕卡换来的是"敌人白等一轮"，而不是
			//    "打断了它的大招"，眩晕的价值被悄悄削掉一半。
			if (Skill->CooldownRounds > 0)
			{
				// +1 补偿：本回合末 RoundEndAll 会统一 -1，
				// 不补的话 CooldownRounds=1 等于没有冷却。
				Enemy.SetSkillCooldown(Skill->Id, Skill->CooldownRounds + 1);
			}

			if (Intent.ResultFacing != Enemy.Facing)
			{
				Queue.PushBack(FHexActions::RotateUnit(Enemy.Id, Intent.ResultFacing));
			}
			return;
		}

		// 技能 id 查不到（表被改过 / 存档里的 id 已不存在）——
		// 记违规并落到默认分支，不让敌人整回合空转。
		State.AddRuleViolation(
			TEXT("missing_enemy_skill"),
			FString::Printf(TEXT("敌人 %s 的意图引用了不存在的技能 %s"),
				*Enemy.SourceId.ToString(), *Intent.SkillId.ToString()));
	}

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
				State.LogEvent(HexEv::IntentWhiffed, Enemy.Id, -1);
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
			State.LogEvent(HexEv::IntentWhiffed, Enemy.Id, -1);
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
			State.LogEvent(HexEv::EnemyRooted, -1, Enemy.Id);
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
