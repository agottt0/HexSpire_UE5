// Copyright Hex Spire. All Rights Reserved.

#include "Battle/HexBattleFlow.h"
#include "Battle/HexBattleState.h"
#include "Battle/HexUnit.h"
#include "Battle/HexRuleBook.h"
#include "Battle/HexDamageCalculator.h"
#include "Battle/HexTargetResolver.h"
#include "Battle/HexEnemyAI.h"
#include "Hex/HexCoord.h"
#include "Hex/HexFootprint.h"
#include "Hex/HexPathfinder.h"
#include "Core/HexSpireConstants.h"

FHexBattleFlow::FHexBattleFlow(FHexBattleState& InState)
	: State(InState)
{
}

const FHexCardData* FHexBattleFlow::LookupCard(FName CardId) const
{
	return CardLookup ? CardLookup(CardId) : nullptr;
}

const FHexCardInstance* FHexBattleFlow::InstFromUid(int32 CardUid) const
{
	// ⚠️ 所有"按 uid 找卡"的查询都必须走这里。
	//    CanPlayCard / GetLegalTargets / GetAffectedCells / CardCostOf
	//    全部依赖它 —— 漏掉固定卡的话，玩家会看到一张点不动、
	//    没有高亮、费用显示为 0 的卡，且没有任何报错。
	if (const FHexCardInstance* InHand = State.Piles.FindInHand(CardUid))
	{
		return InHand;
	}
	return State.FindFixedCard(CardUid);
}

const FHexCardData* FHexBattleFlow::CardFromUid(int32 CardUid) const
{
	const FHexCardInstance* Inst = InstFromUid(CardUid);
	return Inst ? LookupCard(Inst->CardId) : nullptr;
}

// ───────────────────────────────────────────────────────── 战斗开始

void FHexBattleFlow::BeginBattle()
{
	State.Phase = EHexBattlePhase::BattleStart;
	State.RoundNumber = 0;

	// 规则预聚合（符文改写在此生效）
	State.RebuildRuleAggregate();

	// 重建触发监听者（D6）
	TriggerBus.ResetBattleCounters();
	TriggerBus.RebuildListeners(State);

	State.LogEvent(TEXT("battle_start"));

	// Emit(OnBattleStart)
	{
		FHexTriggerContext Ctx;
		Ctx.SourceUnitId = State.HeroUnitId;
		TriggerBus.Emit(EHexTriggerTiming::OnBattleStart, Ctx, State, Queue);
		Queue.ResolveAll(State);
	}

	// 生成敌方首个意图并显示（§8.4）
	FHexEnemyAI::DecideAll(State);
	State.LogEvent(TEXT("intents_updated"));

	BeginRound();
}

// ───────────────────────────────────────────────────────── 回合开始

void FHexBattleFlow::BeginRound()
{
	if (IsBattleOver())
	{
		return;
	}

	State.Phase = EHexBattlePhase::RoundStart;
	Queue.PushBack(FHexActions::AdvanceRound());
	Queue.ResolveAll(State);

	TriggerBus.ResetRoundCounters();

	// Emit(OnRoundStart) —— 符文按槽位 1→6 结算
	{
		FHexTriggerContext Ctx;
		Ctx.SourceUnitId = State.HeroUnitId;
		TriggerBus.Emit(EHexTriggerTiming::OnRoundStart, Ctx, State, Queue);
		Queue.ResolveAll(State);
	}

	// 状态 tick（回合开始时机）
	TickStatuses(EHexStatusTick::RoundStart);

	if (CheckBattleEnd())
	{
		return;
	}

	// 抽牌（§7.4.2）
	if (!FHexRuleBook::IsFixedHand(State))
	{
		const int32 DrawCount = FHexRuleBook::CardsDrawnPerTurn(State);
		Queue.PushBack(FHexActions::DrawCards(DrawCount));
		Queue.ResolveAll(State);
	}

	// 体力 = 体力上限
	Queue.PushBack(FHexActions::SetEnergy(FHexRuleBook::EnergyMax(State)));
	Queue.ResolveAll(State);

	State.Phase = EHexBattlePhase::PlayerPhase;
	State.LogEvent(TEXT("player_phase_begin"));
}

// ───────────────────────────────────────────────────────── 出牌

EHexPlayResult FHexBattleFlow::PlayCard(int32 CardUid, const FIntVector& TargetCell)
{
	// EXPLORE 阶段允许自由使用移动卡（§7.3 无限体力）
	const bool bExplore = (State.Phase == EHexBattlePhase::Explore);

	if (State.Phase != EHexBattlePhase::PlayerPhase && !bExplore)
	{
		return EHexPlayResult::NotPlayerPhase;
	}

	// ── 定位卡实例：先查手牌，再查固定卡
	//
	// ⚠️ 顺序不能反。固定卡常驻、手牌会变，
	//    先查数量少且稳定的手牌能让常见路径更快返回；
	//    更重要的是语义：若将来出现同 uid（不应该，但防御性地），
	//    手牌优先才符合"玩家点的是手上那张"的直觉。
	const FHexCardInstance* Inst = State.Piles.FindInHand(CardUid);
	const bool bFixedCard = (Inst == nullptr) && State.IsFixedCard(CardUid);
	if (!Inst && bFixedCard)
	{
		Inst = State.FindFixedCard(CardUid);
	}
	if (!Inst)
	{
		return EHexPlayResult::CardNotInHand;
	}

	const FHexCardData* Card = LookupCard(Inst->CardId);
	if (!Card)
	{
		return EHexPlayResult::CardNotFound;
	}

	FHexUnit* Hero = State.GetHero();
	if (!Hero || !Hero->bIsAlive)
	{
		return EHexPlayResult::NotPlayerPhase;
	}

	// ── 体力检查
	//    ⚠️ EXPLORE 阶段直接放行，【不】用"体力=9999"实现 ——
	//    否则会污染所有依赖体力值的符文效果（§7.3 明确警告）。
	const int32 Cost = FHexRuleBook::CardCost(State, Card->EnergyCost + Inst->TempCostDelta);
	if (!bExplore && State.Energy < Cost)
	{
		return EHexPlayResult::NotEnoughEnergy;
	}

	// ── 目标合法性
	if (Card->TargetSpec.Shape != EHexTargetShape::SelfShape &&
		Card->TargetSpec.Shape != EHexTargetShape::AdjacentAll)
	{
		if (!FHexTargetResolver::IsLegalTarget(State, *Hero, Card->TargetSpec, TargetCell))
		{
			return EHexPlayResult::IllegalTarget;
		}
	}

	// ── 支付体力
	if (!bExplore && Cost > 0)
	{
		Queue.PushBack(FHexActions::SpendEnergy(Cost));
	}

	// ── 结算效果
	for (const FHexEffectStep& Step : Card->Effects)
	{
		ExecuteStep(Step, *Card, TargetCell);
	}

	// ── 卡牌归宿
	//
	// ⚠️ 固定卡【原地不动】：不进弃牌堆、不进消耗区、也不从任何地方移除。
	//    它们本来就不在牌堆四区里，若误调 ResolvePlayedCard，
	//    该函数会在手牌中找不到这个 uid 而返回 false（静默失败），
	//    但更糟的情况是把一张不属于牌堆的卡塞进弃牌堆 ——
	//    那会直接打破"四区之和 = 卡组全集"的不变量，
	//    下一次 CheckInvariants 才报错，届时已经很难追溯来源。
	if (!bFixedCard)
	{
		// 带【消耗】→ 消耗区；否则 → 弃牌堆
		const bool bExhaust = Card->bIsExhaust
			|| (Card->CardType == EHexCardType::Attack && FHexRuleBook::AttacksExhaust(State));
		State.Piles.ResolvePlayedCard(CardUid, bExhaust);
	}

	if (!bExplore)
	{
		++State.CardsPlayedThisRound;
	}

	{
		FHexBattleEvent E;
		E.Type = TEXT("card_played");
		E.SourceUnitId = Hero->Id;
		E.NameA = Card->Id;
		E.IntA = CardUid;
		E.IntB = Cost;
		State.LogEvent(E);
	}

	// ── Emit(OnCardPlayed)
	{
		FHexTriggerContext Ctx;
		Ctx.SourceUnitId = Hero->Id;
		Ctx.CardId = Card->Id;
		Ctx.CardType = Card->CardType;
		Ctx.CardCost = Cost;
		Ctx.CardTags = Card->Tags;
		TriggerBus.Emit(EHexTriggerTiming::OnCardPlayed, Ctx, State, Queue);
	}

	Queue.ResolveAll(State);

	CheckBattleEnd();

	return EHexPlayResult::Success;
}

// ───────────────────────────────────────────────────────── 效果执行

void FHexBattleFlow::ExecuteStep(
	const FHexEffectStep& Step,
	const FHexCardData& Card,
	const FIntVector& TargetCell)
{
	FHexUnit* Hero = State.GetHero();
	if (!Hero)
	{
		return;
	}

	const FString SrcTag = FString::Printf(TEXT("card:%s"), *Card.Id.ToString());

	switch (Step.Op)
	{
	// ═══════════════════════════════════════ 伤害
	case EHexEffectOp::DealDamage:
	{
		TArray<int32> TargetIds;
		FHexTargetResolver::AffectedUnits(
			State, *Hero, Card.TargetSpec, TargetCell, Step.TargetFilter, TargetIds);

		const int32 Repeat = FMath::Max(1, Step.Repeat);
		const float GlobalDmgMult = FHexRuleBook::DamageMultiplier(State);
		const float GlobalCritMult = FHexRuleBook::CritDamageMultiplier(State);

		for (int32 R = 0; R < Repeat; ++R)
		{
			for (const int32 TargetId : TargetIds)
			{
				FHexUnit* Target = State.FindUnit(TargetId);
				if (!Target || !Target->bIsAlive)
				{
					continue;
				}

				// 背击判定（§8.2.3）
				const bool bRear = Target->IsAttackedFromRear(Hero->Anchor);

				FHexDamageContext DC;
				DC.Source = Hero;
				DC.Target = Target;
				DC.Flat = Step.FlatValue;
				DC.StatRef = Step.StatRef;
				DC.StatRatio = Step.StatRatio;
				DC.bFromRear = bRear;
				DC.Tags = Card.Tags;
				DC.Tag = SrcTag;

				// ⭐ ②′ 顺序钩子：符文按槽位 1→6 依次作用（§6.5）
				FHexTriggerContext TCtx;
				TCtx.SourceUnitId = Hero->Id;
				TCtx.TargetUnitId = TargetId;
				TCtx.CardId = Card.Id;
				TCtx.CardType = Card.CardType;
				TCtx.CardCost = Card.EnergyCost;
				TCtx.CardTags = Card.Tags;

				const FHexValueHook Hook =
					TriggerBus.MakeValueHook(EHexTriggerTiming::OnAttack, State, TCtx);

				const FHexDamageResult Result = FHexDamageCalculator::Calculate(
					DC, &State.Rng, Hook, GlobalDmgMult, GlobalCritMult);

				FHexGameAction A = FHexActions::Damage(
					Hero->Id, TargetId,
					Result.ToBarrier, Result.ToBlock, Result.ToHP,
					Result.bIsCrit, Result.bIsDodged);
				A.SourceTag = SrcTag;
				Queue.PushBack(A);
			}
		}
		break;
	}

	// ═══════════════════════════════════════ 格挡
	case EHexEffectOp::GainBlock:
	{
		const int32 Amount = FHexDamageCalculator::CalculateBlock(
			Hero, Step.FlatValue, Step.StatRef, Step.StatRatio,
			FHexRuleBook::BlockMultiplier(State));

		FHexGameAction A = FHexActions::GainBlock(Hero->Id, Amount);
		A.SourceTag = SrcTag;
		Queue.PushBack(A);

		// Emit(OnBlockGained)
		FHexTriggerContext Ctx;
		Ctx.SourceUnitId = Hero->Id;
		Ctx.IntA = Amount;
		TriggerBus.Emit(EHexTriggerTiming::OnBlockGained, Ctx, State, Queue);
		break;
	}

	case EHexEffectOp::Heal:
	{
		FHexGameAction A = FHexActions::Heal(Hero->Id, Step.ValueFor(Hero));
		A.SourceTag = SrcTag;
		Queue.PushBack(A);
		break;
	}

	// ═══════════════════════════════════════ 位移
	case EHexEffectOp::MoveSelf:
	case EHexEffectOp::Blink:
	{
		// 移动额外距离：AGI 的次要作用之一（§4.3）
		const int32 Bonus = Hero->AGI / HexK::AgiPerStep;
		const int32 Budget = FMath::Max(1, Step.Distance + Bonus - Hero->GetMovePenalty());

		if (Step.Op == EHexEffectOp::Blink)
		{
			// 闪现：无视中间格，只校验终点（§8.6）
			FHexGameAction A = FHexActions::MoveUnit(Hero->Id, TargetCell, Hero->Facing);
			A.SourceTag = SrcTag;
			Queue.PushBack(A);
		}
		else
		{
			// 普通移动：走可通行格，需要寻路
			FHexPathQuery Q;
			Q.StartAnchor = Hero->Anchor;
			Q.StartFacing = Hero->Facing;
			Q.Footprint = Hero->GetFootprint();
			Q.Budget = Budget;
			Q.SelfUnitId = Hero->Id;
			Q.RotateCost = Hero->GetRotateCost();
			Q.bCanCrushRubble = Hero->CanCrushRubble();
			Q.MoveCostDelta = FHexRuleBook::MoveCostDelta(State);

			TArray<FIntVector> Path;
			if (FHexPathfinder::PathTo(State.Grid, Q, TargetCell, -1, Path))
			{
				const int32 BestFacing = FHexPathfinder::BestFacingAt(State.Grid, Q, TargetCell);
				FHexGameAction A = FHexActions::MoveUnit(
					Hero->Id, TargetCell, BestFacing >= 0 ? BestFacing : Hero->Facing);
				A.SourceTag = SrcTag;
				Queue.PushBack(A);
			}
		}

		// Emit(OnMoveSelf)
		FHexTriggerContext Ctx;
		Ctx.SourceUnitId = Hero->Id;
		TriggerBus.Emit(EHexTriggerTiming::OnMoveSelf, Ctx, State, Queue);
		break;
	}

	case EHexEffectOp::Dash:
	{
		// 突进：冲到【玩家点击的那一格】，穿过沿途单位（§8.6）。
		//
		// ══════════════════════════════════════════════════════════
		// 这里曾经有三个叠加的缺陷，改法记录在此，避免回退
		// ══════════════════════════════════════════════════════════
		// ① 旧实现用 DirectionTo() 把目标近似成六轴之一，再【走满】
		//    Step.Distance 格。后果：点近处会冲过头，点斜向会冲歪，
		//    落点和玩家点的格子对不上 —— 战棋里这是致命的。
		// ② 旧实现逐格 CanPlace，遇到任何单位就 break 停下。
		//    这正是"被其他角色顶开"的观感来源。
		//    冲撞是镇妖者唯一的接敌手段，被人挡住就废了。
		// ③ 配套的 TargetSpec 是 Tile，波及格只有落点自己，
		//    而落点又必须是空格 —— 于是后续的伤害步骤恒定 0 目标，
		//    冲撞【从来没有造成过伤害】。
		//
		// 现在：落点合法性已由 LegalCells(DashPath) 全权保证
		//       （可站立 + 路径不被墙堵死 + 允许穿人），
		//       这里直接移动过去即可，不再做二次阻挡判定。
		if (TargetCell != Hero->Anchor)
		{
			// 转向冲撞方向：冲过去之后背对目标是荒谬的，
			// 而且朝向直接决定下回合的背击关系（§8.2.3）。
			const int32 DashDir = FHexTargetResolver::DirectionTo(*Hero, TargetCell);
			const int32 NewFacing = FHexCoord::FacingFromDir(DashDir);

			FHexGameAction A = FHexActions::MoveUnit(
				Hero->Id, TargetCell,
				NewFacing >= 0 ? NewFacing : Hero->Facing);
			A.SourceTag = SrcTag;
			Queue.PushBack(A);
		}

		// Emit(OnMoveSelf)：冲撞也是位移，位移流符文必须能吃到
		FHexTriggerContext Ctx;
		Ctx.SourceUnitId = Hero->Id;
		TriggerBus.Emit(EHexTriggerTiming::OnMoveSelf, Ctx, State, Queue);
		break;
	}

	case EHexEffectOp::Rotate:
	{
		FHexGameAction A = FHexActions::RotateUnit(Hero->Id, Step.Distance);
		A.SourceTag = SrcTag;
		Queue.PushBack(A);
		break;
	}

	case EHexEffectOp::Knockback:
	case EHexEffectOp::Pull:
	{
		TArray<int32> TargetIds;
		FHexTargetResolver::AffectedUnits(
			State, *Hero, Card.TargetSpec, TargetCell, EHexTargetFilter::Enemy, TargetIds);

		for (const int32 TargetId : TargetIds)
		{
			const int32 Dist = (Step.Op == EHexEffectOp::Pull) ? -Step.Distance : Step.Distance;
			FHexGameAction A = FHexActions::Knockback(Hero->Id, TargetId, Dist);
			A.SourceTag = SrcTag;
			Queue.PushBack(A);
		}

		// Emit(OnMoveEnemy) —— 推拉流符文的钩子（§6.3 示例 E《碾压者》）
		if (TargetIds.Num() > 0)
		{
			FHexTriggerContext Ctx;
			Ctx.SourceUnitId = Hero->Id;
			Ctx.TargetUnitId = TargetIds[0];
			TriggerBus.Emit(EHexTriggerTiming::OnMoveEnemy, Ctx, State, Queue);
		}
		break;
	}

	case EHexEffectOp::Trample:
	{
		TArray<int32> TargetIds;
		FHexTargetResolver::AffectedUnits(
			State, *Hero, Card.TargetSpec, TargetCell, EHexTargetFilter::Enemy, TargetIds);
		for (const int32 TargetId : TargetIds)
		{
			FHexGameAction A = FHexActions::Trample(Hero->Id, TargetId);
			A.SourceTag = SrcTag;
			Queue.PushBack(A);
		}
		break;
	}

	// ═══════════════════════════════════════ 状态
	case EHexEffectOp::ApplyStatus:
	{
		if (Step.TargetFilter == EHexTargetFilter::Self)
		{
			FHexGameAction A = FHexActions::ApplyStatus(Hero->Id, Step.StatusId, Step.StatusStacks);
			A.SourceTag = SrcTag;
			Queue.PushBack(A);
		}
		else
		{
			TArray<int32> TargetIds;
			FHexTargetResolver::AffectedUnits(
				State, *Hero, Card.TargetSpec, TargetCell, Step.TargetFilter, TargetIds);
			for (const int32 TargetId : TargetIds)
			{
				FHexGameAction A = FHexActions::ApplyStatus(
					TargetId, Step.StatusId, Step.StatusStacks);
				A.SourceTag = SrcTag;
				Queue.PushBack(A);
			}
		}

		FHexTriggerContext Ctx;
		Ctx.SourceUnitId = Hero->Id;
		Ctx.CardTags = Card.Tags;
		TriggerBus.Emit(EHexTriggerTiming::OnStatusApplied, Ctx, State, Queue);
		break;
	}

	case EHexEffectOp::RemoveStatus:
	{
		FHexGameAction A = FHexActions::RemoveStatus(Hero->Id, Step.StatusId);
		A.SourceTag = SrcTag;
		Queue.PushBack(A);
		break;
	}

	// ═══════════════════════════════════════ 牌堆
	case EHexEffectOp::DrawCard:
	{
		FHexGameAction A = FHexActions::DrawCards(FMath::Max(1, Step.Repeat));
		A.SourceTag = SrcTag;
		Queue.PushBack(A);
		break;
	}

	case EHexEffectOp::ShuffleDiscardIntoDraw:
		Queue.PushBack(FHexActions::ReshuffleDeck());
		break;

	case EHexEffectOp::GainEnergy:
	{
		FHexGameAction A = FHexActions::GainEnergy(FMath::FloorToInt(Step.FlatValue));
		A.SourceTag = SrcTag;
		Queue.PushBack(A);
		break;
	}

	// ═══════════════════════════════════════ 地形
	case EHexEffectOp::ModifyTerrain:
	{
		FHexGameAction A = FHexActions::ModifyTerrain(
			TargetCell, static_cast<EHexTerrain>(Step.Distance));
		A.SourceTag = SrcTag;
		Queue.PushBack(A);
		break;
	}

	default:
		// ⚠️ 未实现的 op 记违规而非静默失败 —— 否则策划配了效果却不生效，
		//    只能靠肉眼测出来（Godot 版踩过这个坑，叫 unimplemented_op）。
		State.AddRuleViolation(
			TEXT("unimplemented_op"),
			FString::Printf(TEXT("卡牌 %s 的效果 op=%d 未实现"),
				*Card.Id.ToString(), static_cast<int32>(Step.Op)));
		break;
	}
}

// ───────────────────────────────────────────────────────── 回合结束

void FHexBattleFlow::EndPlayerTurn()
{
	if (State.Phase != EHexBattlePhase::PlayerPhase)
	{
		return;
	}

	State.Phase = EHexBattlePhase::RoundEndPlayer;

	// Emit(OnRoundEnd)
	{
		FHexTriggerContext Ctx;
		Ctx.SourceUnitId = State.HeroUnitId;
		TriggerBus.Emit(EHexTriggerTiming::OnRoundEnd, Ctx, State, Queue);
		Queue.ResolveAll(State);
	}

	// ── 剩余体力 → Emit(OnEnergyLeftover) → 清零
	//    这是符文《余烬》类效果的钩子（§6.3 示例 C）
	if (State.Energy > 0)
	{
		FHexTriggerContext Ctx;
		Ctx.SourceUnitId = State.HeroUnitId;
		Ctx.IntA = State.Energy;
		TriggerBus.Emit(EHexTriggerTiming::OnEnergyLeftover, Ctx, State, Queue);
		Queue.ResolveAll(State);
	}
	Queue.PushBack(FHexActions::SetEnergy(0));

	// ── 弃掉全部手牌（§7.4.2：制造"这回合必须用完"的紧迫感）
	{
		TArray<FHexCardInstance> Discarded;
		State.Piles.DiscardHand(Discarded);

		for (const FHexCardInstance& C : Discarded)
		{
			FHexBattleEvent E;
			E.Type = TEXT("card_discarded");
			E.IntA = C.Uid;
			E.NameA = C.CardId;
			State.LogEvent(E);

			FHexTriggerContext Ctx;
			Ctx.SourceUnitId = State.HeroUnitId;
			Ctx.CardId = C.CardId;
			TriggerBus.Emit(EHexTriggerTiming::OnCardDiscarded, Ctx, State, Queue);
		}
	}

	// ── 格挡清空（镇妖者被动除外，由 ClearBlock 的 handler 判断）
	{
		TArray<int32> PlayerIds;
		State.GetAlivePlayerIds(PlayerIds);
		for (const int32 Id : PlayerIds)
		{
			Queue.PushBack(FHexActions::ClearBlock(Id));
		}
	}

	// 状态 tick（回合结束时机：燃烧/中毒在此结算）
	Queue.ResolveAll(State);
	TickStatuses(EHexStatusTick::RoundEnd);

	if (CheckBattleEnd())
	{
		return;
	}

	RunEnemyPhase();

	if (CheckBattleEnd())
	{
		return;
	}

	RunRoundEndAll();

	if (CheckBattleEnd())
	{
		return;
	}

	BeginRound();
}

void FHexBattleFlow::TickStatuses(EHexStatusTick Timing)
{
	// 按 id 升序遍历（确定性）
	TArray<int32> AllIds;
	for (const FHexUnit& U : State.GetUnits())
	{
		if (U.bIsAlive)
		{
			AllIds.Add(U.Id);
		}
	}

	for (const int32 Id : AllIds)
	{
		Queue.PushBack(FHexActions::TickStatus(Id, Timing));
	}
	Queue.ResolveAll(State);
}

// ───────────────────────────────────────────────────────── 敌方阶段

void FHexBattleFlow::RunEnemyPhase()
{
	State.Phase = EHexBattlePhase::EnemyPhase;
	State.LogEvent(TEXT("enemy_phase_begin"));

	// 按 AGI 降序依次行动（同值按 id 升序 —— 确定性 tiebreak）
	TArray<int32> Order;
	State.GetEnemyActionOrder(Order);

	for (const int32 Id : Order)
	{
		FHexUnit* Enemy = State.FindUnit(Id);
		if (!Enemy || !Enemy->bIsAlive)
		{
			continue;
		}

		FHexEnemyAI::ExecuteIntent(State, *Enemy, Queue);
		Queue.ResolveAll(State);

		// 玩家死了就立刻停 —— 不让后续敌人"鞭尸"
		if (State.IsPlayerDefeated())
		{
			return;
		}
	}

	// 生成下回合意图并立即显示（§8.4：预警是可读性的生命线）
	FHexEnemyAI::DecideAll(State);
	State.LogEvent(TEXT("intents_updated"));
}

// ───────────────────────────────────────────────────────── 回合总结束

void FHexBattleFlow::RunRoundEndAll()
{
	State.Phase = EHexBattlePhase::RoundEndAll;

	// 危害地面 tick & 衰减
	{
		TArray<FIntVector> Expired;
		State.Grid.TickHazards(Expired);
		for (const FIntVector& C : Expired)
		{
			FHexBattleEvent E;
			E.Type = TEXT("hazard_expired");
			E.CoordA = C;
			State.LogEvent(E);
		}
	}

	// 状态持续时间 -1 / 过期移除
	{
		for (FHexUnit& U : State.GetUnitsMutable())
		{
			if (!U.bIsAlive)
			{
				continue;
			}
			TArray<FName> ExpiredStatuses;
			U.DecayStatuses(ExpiredStatuses);
			for (const FName& S : ExpiredStatuses)
			{
				FHexBattleEvent E;
				E.Type = TEXT("status_expired");
				E.TargetUnitId = U.Id;
				E.NameA = S;
				State.LogEvent(E);
			}
		}
	}
}

// ───────────────────────────────────────────────────────── 胜负

bool FHexBattleFlow::CheckBattleEnd()
{
	if (State.Phase == EHexBattlePhase::BattleWin ||
		State.Phase == EHexBattlePhase::BattleLose ||
		State.Phase == EHexBattlePhase::Explore)
	{
		return true;
	}

	if (State.IsPlayerDefeated())
	{
		State.Phase = EHexBattlePhase::BattleLose;
		State.LogEvent(TEXT("battle_lose"));
		return true;
	}

	if (State.IsPlayerVictorious())
	{
		// Emit(OnBattleWin)
		FHexTriggerContext Ctx;
		Ctx.SourceUnitId = State.HeroUnitId;
		TriggerBus.Emit(EHexTriggerTiming::OnBattleWin, Ctx, State, Queue);
		Queue.ResolveAll(State);

		State.LogEvent(TEXT("battle_win"));
		EnterExplorePhase();
		return true;
	}

	return false;
}

void FHexBattleFlow::EnterExplorePhase()
{
	// §7.3：战斗结束后进入无限体力状态，可自由走动拾取、走到出口离开。
	//
	// ⚠️ "无限体力"的实现是【阶段判定】而非"体力=9999"——
	//    后者会污染所有依赖体力值的符文效果（策划案明确警告）。
	//    见 PlayCard 里的 bExplore 分支。
	State.Phase = EHexBattlePhase::Explore;

	// 把弃牌堆洗回并补满手牌，让玩家有移动卡可用
	State.Piles.ReshuffleDiscardIntoDraw(State.Rng);
	Queue.PushBack(FHexActions::DrawCards(FHexRuleBook::HandLimit(State)));
	Queue.ResolveAll(State);

	State.LogEvent(TEXT("explore_phase_begin"));
}

bool FHexBattleFlow::IsBattleOver() const
{
	// ⚠️ Explore 必须算作"战斗已结束"。
	//
	//    Explore 是【胜利之后】的拾取阶段（§7.3 无限体力探索），
	//    此时敌人已全灭，回合循环不再推进。
	//    漏掉它的后果很隐蔽：调用方看到 IsBattleOver()==false 于是
	//    继续调 EndPlayerTurn()，而 EndPlayerTurn 因 Phase != PlayerPhase
	//    直接返回 —— 外层循环空转到超时上限。
	//    对 UI 而言就是"打赢了但界面卡住"。
	//    集成测试第一次跑时 20 场 enc_01 全部"超时"，实际上全是胜利。
	return State.Phase == EHexBattlePhase::BattleWin
		|| State.Phase == EHexBattlePhase::BattleLose
		|| State.Phase == EHexBattlePhase::Explore;
}

// ───────────────────────────────────────────────────────── 查询（UI）

int32 FHexBattleFlow::GetCardCost(int32 CardUid) const
{
	const FHexCardInstance* Inst = InstFromUid(CardUid);
	if (!Inst)
	{
		return 0;
	}
	const FHexCardData* Card = LookupCard(Inst->CardId);
	if (!Card)
	{
		return 0;
	}
	return FHexRuleBook::CardCost(State, Card->EnergyCost + Inst->TempCostDelta);
}

bool FHexBattleFlow::CanPlayCard(int32 CardUid) const
{
	const FHexCardData* Card = CardFromUid(CardUid);
	if (!Card)
	{
		return false;
	}

	if (State.Phase == EHexBattlePhase::Explore)
	{
		// 探索阶段只允许移动类卡
		return Card->CardType == EHexCardType::Move;
	}

	if (State.Phase != EHexBattlePhase::PlayerPhase)
	{
		return false;
	}

	if (State.Energy < GetCardCost(CardUid))
	{
		return false;
	}

	// 需要至少有一个合法目标
	const FHexUnit* Hero = State.GetHero();
	if (!Hero)
	{
		return false;
	}
	if (Card->TargetSpec.Shape == EHexTargetShape::SelfShape ||
		Card->TargetSpec.Shape == EHexTargetShape::AdjacentAll)
	{
		return true;
	}

	TArray<FIntVector> Legal;
	FHexTargetResolver::LegalCells(State, *Hero, Card->TargetSpec, Legal);
	return Legal.Num() > 0;
}

void FHexBattleFlow::GetLegalTargets(int32 CardUid, TArray<FIntVector>& Out) const
{
	Out.Reset();
	const FHexCardData* Card = CardFromUid(CardUid);
	const FHexUnit* Hero = State.GetHero();
	if (!Card || !Hero)
	{
		return;
	}
	FHexTargetResolver::LegalCells(State, *Hero, Card->TargetSpec, Out);
}

void FHexBattleFlow::GetAffectedCells(
	int32 CardUid, const FIntVector& TargetCell, TArray<FIntVector>& Out) const
{
	Out.Reset();
	const FHexCardData* Card = CardFromUid(CardUid);
	const FHexUnit* Hero = State.GetHero();
	if (!Card || !Hero)
	{
		return;
	}
	FHexTargetResolver::AffectedCells(State, *Hero, Card->TargetSpec, TargetCell, Out);
}

FIntPoint FHexBattleFlow::PreviewDamage(int32 CardUid, const FIntVector& TargetCell) const
{
	const FHexCardData* Card = CardFromUid(CardUid);
	const FHexUnit* Hero = State.GetHero();
	if (!Card || !Hero)
	{
		return FIntPoint(0, 0);
	}

	TArray<int32> TargetIds;
	FHexTargetResolver::AffectedUnits(
		State, *Hero, Card->TargetSpec, TargetCell, EHexTargetFilter::Enemy, TargetIds);

	if (TargetIds.Num() == 0)
	{
		return FIntPoint(0, 0);
	}

	const FHexUnit* Target = State.FindUnit(TargetIds[0]);
	if (!Target)
	{
		return FIntPoint(0, 0);
	}

	int32 TotalNormal = 0;
	int32 TotalCrit = 0;

	const float GlobalDmgMult = FHexRuleBook::DamageMultiplier(State);
	const float GlobalCritMult = FHexRuleBook::CritDamageMultiplier(State);

	for (const FHexEffectStep& Step : Card->Effects)
	{
		if (Step.Op != EHexEffectOp::DealDamage)
		{
			continue;
		}

		FHexDamageContext DC;
		DC.Source = Hero;
		DC.Target = const_cast<FHexUnit*>(Target);
		DC.Flat = Step.FlatValue;
		DC.StatRef = Step.StatRef;
		DC.StatRatio = Step.StatRatio;
		DC.bFromRear = Target->IsAttackedFromRear(Hero->Anchor);
		DC.Tags = Card->Tags;

		FHexTriggerContext TCtx;
		TCtx.SourceUnitId = Hero->Id;
		TCtx.TargetUnitId = Target->Id;
		TCtx.CardId = Card->Id;
		TCtx.CardType = Card->CardType;
		TCtx.CardCost = Card->EnergyCost;
		TCtx.CardTags = Card->Tags;

		// ⚠️ MakeValueHook 与 Preview 都不消耗 RNG，可安全用于每帧悬停
		const FHexValueHook Hook =
			TriggerBus.MakeValueHook(EHexTriggerTiming::OnAttack, State, TCtx);

		const FIntPoint R = FHexDamageCalculator::Preview(
			DC, Hook, GlobalDmgMult, GlobalCritMult);

		const int32 Repeat = FMath::Max(1, Step.Repeat);
		TotalNormal += R.X * Repeat;
		TotalCrit += R.Y * Repeat;
	}

	return FIntPoint(TotalNormal, TotalCrit);
}

void FHexBattleFlow::GetReachableAnchors(
	int32 MoveBudget, TMap<FIntVector, TPair<int32, int32>>& Out) const
{
	Out.Reset();
	const FHexUnit* Hero = State.GetHero();
	if (!Hero)
	{
		return;
	}

	FHexPathQuery Q;
	Q.StartAnchor = Hero->Anchor;
	Q.StartFacing = Hero->Facing;
	Q.Footprint = Hero->GetFootprint();
	Q.Budget = FMath::Max(1, MoveBudget - Hero->GetMovePenalty());
	Q.SelfUnitId = Hero->Id;
	Q.RotateCost = Hero->GetRotateCost();
	Q.bCanCrushRubble = Hero->CanCrushRubble();
	Q.MoveCostDelta = FHexRuleBook::MoveCostDelta(State);

	FHexPathfinder::ReachableAnchors(State.Grid, Q, Out);
}
