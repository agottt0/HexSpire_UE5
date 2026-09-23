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

// ══════════════════════════════════════════════════════════ 触发派发（§6.3）
//
// ══════════════════════════════════════════════════════════════════
// 这一段是 P0 缺陷的修复处，务必先读完再改
// ══════════════════════════════════════════════════════════════════
// 症状：22 个触发时机里有 8 个从来没被 Emit 过。挂在它们上面的符文
//       装备后【毫无效果、毫无报错】—— 实测《食魂》(OnKill)、
//       《焚心》(OnCrit)、《轮回护符》(OnDeckReshuffled) 三个符文
//       完全是死的，策划案 §6.3 的两组示范组合也无法成立。
//       而当时 1049 项断言全部通过。
//
// 讽刺的是 HexTriggerBus.h 开头就写着「埋点必须一次埋满，
// OnBlockBroken / OnDeckReshuffled 这类最容易忘」—— 然后就忘了。
// 结论：靠纪律提醒不可靠，必须靠【结构】保证。
//
// 修法：不再让业务代码逐处手写 Emit，而是统一在"动作执行完"这个
//       唯一通道上翻译。动作是状态变更的唯一途径（纪律 2），
//       所以这里天然不会漏；漏了也会被埋点覆盖率断言抓住
//       （VerifyTrigger::CheckTimingCoverage）。
//
// ── 分工表（必须互斥，否则会双重触发）──
//   本翻译器负责（原先完全没有埋点的 8 个）：
//     OnDamageDealt / OnDamageTaken / OnCrit
//     OnKill / OnUnitDeath
//     OnCardDrawn / OnDeckReshuffled
//     OnMoveEnemy（推拉造成的位移；卡牌主动推拉已有埋点，见下）
//
//   仍由业务代码手写 Emit（保持原状，本次【不动】）：
//     OnBattleStart / OnRoundStart / OnRoundEnd / OnBattleWin
//     OnCardPlayed / OnEnergyLeftover     ← 非动作型，没有对应的单一动作
//     OnBlockGained / OnMoveSelf / OnStatusApplied / OnCardDiscarded
//                                         ← 已有埋点，翻译器【不得】重复处理
//     OnAttack                            ← ②′ 数值钩子，不走 Emit
//
// ⚠️ 往分工表里加东西前，先确认另一侧没有同名埋点。
//    双重触发比不触发更难查：符文效果会莫名翻倍，且看起来像数值问题。

int32 FHexBattleFlow::ResolveQueue()
{
	// 预算是"本次结算"的概念，每次进来重置。
	ObserverEmitBudget = HexK::MaxObserverEmitsPerResolve;

	return Queue.ResolveAll(State,
		[this](const FHexGameAction& Action,
			TArrayView<const FHexBattleEvent> NewEvents,
			FHexBattleState& /*InState*/,
			FHexActionQueue& InQueue)
		{
			DispatchTriggersForAction(Action, NewEvents, InQueue);
		});
}

bool FHexBattleFlow::IsPlayerSide(int32 UnitId) const
{
	const FHexUnit* U = State.FindUnit(UnitId);
	return U && U->Team == EHexTeam::Player;
}

void FHexBattleFlow::EmitWithBudget(
	EHexTriggerTiming Timing,
	const FHexTriggerContext& Ctx,
	FHexActionQueue& InQueue)
{
	// ⚠️ 预算耗尽时记违规，而不是静默返回 ——
	//    静默返回会让"符文突然不生效"变成无头案，
	//    那正是我们刚刚花大力气消灭的那类问题。
	if (ObserverEmitBudget <= 0)
	{
		State.AddRuleViolation(
			TEXT("trigger_chain_budget"),
			FString::Printf(
				TEXT("单次结算的触发翻译次数达上限 %d（疑似符文自激），时机 %d"),
				HexK::MaxObserverEmitsPerResolve, static_cast<int32>(Timing)));
		return;
	}

	--ObserverEmitBudget;
	TriggerBus.Emit(Timing, Ctx, State, InQueue);
}

void FHexBattleFlow::DispatchTriggersForAction(
	const FHexGameAction& Action,
	TArrayView<const FHexBattleEvent> NewEvents,
	FHexActionQueue& InQueue)
{
	// ── 先处理"只能由动作本身表达"的时机
	//
	// 击退/踩踏是敌人被动位移。卡牌里主动推拉的分支已经在
	// ExecuteStep 里 Emit 过 OnMoveEnemy，所以这里【只补】
	// 那些不经由卡牌效果产生的位移（如碰撞连锁、地形推挤）。
	// 判据用 SourceTag 为空：卡牌产生的动作都会带 SourceTag。
	if ((Action.Type == EHexActionType::Knockback
			|| Action.Type == EHexActionType::Trample)
		&& Action.SourceTag.IsEmpty())
	{
		FHexTriggerContext Ctx;
		Ctx.SourceUnitId = Action.SourceUnitId;
		Ctx.TargetUnitId = Action.TargetUnitId;
		EmitWithBudget(EHexTriggerTiming::OnMoveEnemy, Ctx, InQueue);
	}

	// ── 其余全部以【事件】为准
	//
	// ⚠️ 不要照着动作参数重算。"格挡是否被打破"、"这一下是否致死"、
	//    "抽牌时是否触发了洗回"只有 Resolver 内部知道；
	//    在这里重算必然与真实结算产生偏差（而且是静默偏差）。
	for (const FHexBattleEvent& E : NewEvents)
	{
		// ── 伤害链
		if (E.Type == TEXT("damage_dealt"))
		{
			FHexTriggerContext Ctx;
			Ctx.SourceUnitId = E.SourceUnitId;
			Ctx.TargetUnitId = E.TargetUnitId;
			// IntA 护盾 / IntB 格挡 / IntC 真正打进 HP 的部分。
			// 符文关心"打进去多少"，所以取 HP 伤害。
			Ctx.IntA = E.IntC;

			// 语义：符文属于玩家，所以
			//   "我造成伤害" = 来源是玩家方
			//   "我受到伤害" = 承受方是玩家方
			// 敌人互相伤害（尖刺、踩踏连锁）不该触发玩家符文。
			if (IsPlayerSide(E.SourceUnitId))
			{
				EmitWithBudget(EHexTriggerTiming::OnDamageDealt, Ctx, InQueue);

				if (E.bFlagA)   // bFlagA = 本次是暴击
				{
					EmitWithBudget(EHexTriggerTiming::OnCrit, Ctx, InQueue);
				}
			}

			if (IsPlayerSide(E.TargetUnitId))
			{
				EmitWithBudget(EHexTriggerTiming::OnDamageTaken, Ctx, InQueue);
			}
			continue;
		}

		// ── 闪避
		if (E.Type == TEXT("dodged"))
		{
			if (IsPlayerSide(E.TargetUnitId))
			{
				FHexTriggerContext Ctx;
				Ctx.SourceUnitId = E.SourceUnitId;
				Ctx.TargetUnitId = E.TargetUnitId;
				EmitWithBudget(EHexTriggerTiming::OnDodge, Ctx, InQueue);
			}
			continue;
		}

		// ── 格挡被打破
		if (E.Type == TEXT("block_broken"))
		{
			if (IsPlayerSide(E.TargetUnitId))
			{
				FHexTriggerContext Ctx;
				Ctx.SourceUnitId = E.SourceUnitId;
				Ctx.TargetUnitId = E.TargetUnitId;
				EmitWithBudget(EHexTriggerTiming::OnBlockBroken, Ctx, InQueue);
			}
			continue;
		}

		// ── 死亡
		if (E.Type == TEXT("unit_died"))
		{
			FHexTriggerContext Ctx;
			Ctx.SourceUnitId = E.SourceUnitId;   // 击杀者（环境伤害时为 -1）
			Ctx.TargetUnitId = E.TargetUnitId;   // 死者

			// OnKill：玩家击杀了什么。《食魂》靠它抽牌。
			if (IsPlayerSide(E.SourceUnitId))
			{
				EmitWithBudget(EHexTriggerTiming::OnKill, Ctx, InQueue);
			}

			// OnUnitDeath：任何单位死亡，【不过滤】。
			// 与 OnKill 分开是有意的：有些符文关心"场上有人死了"
			// （尖刺致死、毒杀），那时击杀者是 -1，OnKill 不该触发。
			EmitWithBudget(EHexTriggerTiming::OnUnitDeath, Ctx, InQueue);
			continue;
		}

		// ── 牌堆链
		if (E.Type == TEXT("cards_drawn"))
		{
			FHexTriggerContext Ctx;
			Ctx.SourceUnitId = State.HeroUnitId;
			Ctx.IntA = E.IntA;   // 实际抽到的张数
			EmitWithBudget(EHexTriggerTiming::OnCardDrawn, Ctx, InQueue);
			continue;
		}

		if (E.Type == TEXT("card_exhausted"))
		{
			FHexTriggerContext Ctx;
			Ctx.SourceUnitId = State.HeroUnitId;
			Ctx.IntA = E.IntA;
			EmitWithBudget(EHexTriggerTiming::OnCardExhausted, Ctx, InQueue);
			continue;
		}

		if (E.Type == TEXT("deck_reshuffled"))
		{
			// §6.3 点名这是 D2 带来的好钩子：小卡组会频繁洗回。
			// 《薄刃契》(容量-3) + 《轮回护符》(洗回得格挡) 的组合
			// 完全建立在它上面。
			FHexTriggerContext Ctx;
			Ctx.SourceUnitId = State.HeroUnitId;
			EmitWithBudget(EHexTriggerTiming::OnDeckReshuffled, Ctx, InQueue);
			continue;
		}
	}
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
		ResolveQueue();
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
	ResolveQueue();

	TriggerBus.ResetRoundCounters();

	// Emit(OnRoundStart) —— 符文按槽位 1→6 结算
	{
		FHexTriggerContext Ctx;
		Ctx.SourceUnitId = State.HeroUnitId;
		TriggerBus.Emit(EHexTriggerTiming::OnRoundStart, Ctx, State, Queue);
		ResolveQueue();
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
		ResolveQueue();
	}

	// 体力 = 体力上限
	Queue.PushBack(FHexActions::SetEnergy(FHexRuleBook::EnergyMax(State)));
	ResolveQueue();

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

	ResolveQueue();

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
			FHexGameAction A = FHexActions::ApplyStatus(
				Hero->Id, Step.StatusId, Step.StatusStacks, Hero->Id);
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
				// ⚠️ 末参数传 Hero->Id：玩家点燃的敌人若被烧死，
				//    必须算【玩家击杀】，否则《食魂》在烧流下失效。
				FHexGameAction A = FHexActions::ApplyStatus(
					TargetId, Step.StatusId, Step.StatusStacks, Hero->Id);
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
		ResolveQueue();
	}

	// ── 剩余体力 → Emit(OnEnergyLeftover) → 清零
	//    这是符文《余烬》类效果的钩子（§6.3 示例 C）
	if (State.Energy > 0)
	{
		FHexTriggerContext Ctx;
		Ctx.SourceUnitId = State.HeroUnitId;
		Ctx.IntA = State.Energy;
		TriggerBus.Emit(EHexTriggerTiming::OnEnergyLeftover, Ctx, State, Queue);
		ResolveQueue();
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
	ResolveQueue();
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
	ResolveQueue();
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
		ResolveQueue();

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
		ResolveQueue();

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
	ResolveQueue();

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
