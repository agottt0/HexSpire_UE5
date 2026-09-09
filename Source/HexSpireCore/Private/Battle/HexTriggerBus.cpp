// Copyright Hex Spire. All Rights Reserved.

#include "Battle/HexTriggerBus.h"
#include "Battle/HexBattleState.h"
#include "Battle/HexGameAction.h"
#include "Battle/HexUnit.h"
#include "Runes/HexRuneData.h"
#include "Core/HexSpireConstants.h"

// ───────────────────────────────────────────────────────── 监听者构建

void FHexTriggerBus::RebuildListeners(const FHexBattleState& State)
{
	Listeners.Reset();

	// ── 符文槽 1..6（SlotOrder = 1..6）
	//    ⚠️ 这是 §6.5 结算顺序的唯一来源，必须严格按槽位下标。
	TArray<TPair<int32, const FHexRuneData*>> Runes;
	State.RuneLoadout.GetRunesInOrder(Runes);

	for (const TPair<int32, const FHexRuneData*>& Pair : Runes)
	{
		const int32 SlotIndex = Pair.Key;
		const FHexRuneData* Rune = Pair.Value;
		if (!Rune)
		{
			continue;
		}

		for (int32 TI = 0; TI < Rune->Triggers.Num(); ++TI)
		{
			FHexTriggerListener L;
			// uid 用「槽位 × 1000 + 触发器下标」保证同名符文装在不同槽位时不串号
			L.SourceUid = (SlotIndex + 1) * 1000 + TI;
			L.SourceTag = FString::Printf(TEXT("rune:slot%d:%s"),
				SlotIndex + 1, *Rune->Id.ToString());
			L.SlotOrder = SlotIndex + 1;   // 1..6
			L.Timing = Rune->Triggers[TI].When;
			L.Trigger = &Rune->Triggers[TI];
			Listeners.Add(L);
		}
	}

	// ── 装备槽（SlotOrder = 10/11/12：武器/盔甲/饰品）
	//    ⚠️ 装备【永远在符文之后】结算。这个先后不是随意定的：
	//       符文改写规则、装备提供数值，"先定规则再算数值"
	//       才符合玩家的心智模型，也让 ②′ 的顺序教学只需讲符文槽。
	{
		TArray<TPair<int32, const FHexRuneTrigger*>> EquipTriggers;
		State.EquipLoadout.GetTriggersInOrder(EquipTriggers);

		for (int32 EI = 0; EI < EquipTriggers.Num(); ++EI)
		{
			const int32 SlotOrder = EquipTriggers[EI].Key;
			const FHexRuneTrigger* Trigger = EquipTriggers[EI].Value;
			if (!Trigger)
			{
				continue;
			}

			FHexTriggerListener L;
			// uid 用「槽位号 × 1000 + 序号」，与符文同构。
			// 符文槽位是 1..6、装备是 10..12，两者的 uid 空间天然不重叠，
			// 所以 MaxPerRound 计数不会互相串号。
			L.SourceUid = SlotOrder * 1000 + EI;
			L.SourceTag = FString::Printf(TEXT("equip:slot%d"), SlotOrder);
			L.SlotOrder = SlotOrder;
			L.Timing = Trigger->When;
			L.Trigger = Trigger;
			Listeners.Add(L);
		}
	}

	// ⚠️ 唯一顺序来源：显式排序，同 SlotOrder 时用 SourceUid tiebreak。
	//    没有 tiebreak 的排序会让同槽位的多个触发器顺序不定 → 结果漂移。
	Listeners.Sort([](const FHexTriggerListener& A, const FHexTriggerListener& B)
	{
		if (A.SlotOrder != B.SlotOrder)
		{
			return A.SlotOrder < B.SlotOrder;
		}
		return A.SourceUid < B.SourceUid;
	});
}

void FHexTriggerBus::ResetRoundCounters()
{
	RoundCounts.Reset();
}

void FHexTriggerBus::ResetBattleCounters()
{
	BattleCounts.Reset();
	RoundCounts.Reset();
}

FString FHexTriggerBus::CountKey(const FHexTriggerListener& L)
{
	return FString::Printf(TEXT("%d:%d"), L.SourceUid, static_cast<int32>(L.Timing));
}

bool FHexTriggerBus::CanFire(const FHexTriggerListener& L) const
{
	if (!L.Trigger)
	{
		return false;
	}
	const FString Key = CountKey(L);

	if (L.Trigger->MaxPerRound >= 0)
	{
		const int32* N = RoundCounts.Find(Key);
		if (N && *N >= L.Trigger->MaxPerRound)
		{
			return false;
		}
	}
	if (L.Trigger->MaxPerBattle >= 0)
	{
		const int32* N = BattleCounts.Find(Key);
		if (N && *N >= L.Trigger->MaxPerBattle)
		{
			return false;
		}
	}
	return true;
}

void FHexTriggerBus::MarkFired(const FHexTriggerListener& L)
{
	const FString Key = CountKey(L);
	RoundCounts.FindOrAdd(Key)++;
	BattleCounts.FindOrAdd(Key)++;
}

// ───────────────────────────────────────────────────────── 过滤与条件

bool FHexTriggerBus::PassesFilter(const FHexRuneTrigger& Trigger, const FHexTriggerContext& Ctx)
{
	if (Trigger.bFilterByCardType && Trigger.FilterCardType != Ctx.CardType)
	{
		return false;
	}
	if (!Trigger.FilterTag.IsNone() && !Ctx.CardTags.Contains(Trigger.FilterTag))
	{
		return false;
	}
	if (Trigger.FilterCostMin >= 0 && Ctx.CardCost < Trigger.FilterCostMin)
	{
		return false;
	}
	if (Trigger.FilterCostMax >= 0 && Ctx.CardCost > Trigger.FilterCostMax)
	{
		return false;
	}
	return true;
}

bool FHexTriggerBus::PassesCondition(
	const FHexEffectCondition& Condition,
	const FHexBattleState& State,
	const FHexTriggerContext& Ctx)
{
	switch (Condition.Kind)
	{
	case FHexEffectCondition::EKind::None:
		return true;

	case FHexEffectCondition::EKind::TargetHPBelowPercent:
	{
		const FHexUnit* T = State.FindUnit(Ctx.TargetUnitId);
		if (!T || T->HPMax <= 0)
		{
			return false;
		}
		return (static_cast<float>(T->HP) / static_cast<float>(T->HPMax)) < Condition.FloatParam;
	}

	case FHexEffectCondition::EKind::SelfAtFullHP:
	{
		const FHexUnit* S = State.FindUnit(Ctx.SourceUnitId);
		return S && S->HP >= S->HPMax;
	}

	case FHexEffectCondition::EKind::DrawPileEmpty:
		return State.Piles.NumDraw() == 0;

	case FHexEffectCondition::EKind::TargetHasStatus:
	{
		const FHexUnit* T = State.FindUnit(Ctx.TargetUnitId);
		return T && T->HasStatus(Condition.NameParam);
	}

	case FHexEffectCondition::EKind::CardsPlayedAtLeast:
		return State.CardsPlayedThisRound >= Condition.IntParam;

	default:
		return true;
	}
}

// ───────────────────────────────────────────────────────── 分发

void FHexTriggerBus::Emit(
	EHexTriggerTiming Timing,
	const FHexTriggerContext& Ctx,
	FHexBattleState& State,
	FHexActionQueue& Queue)
{
	// 保证 3：递归深度闸。超限【绝不抛异常】，只记录 ——
	// 这让 BattleSim 能把死循环变成可统计数据而非崩溃。
	if (Depth >= HexK::MaxTriggerDepth)
	{
		State.AddRuleViolation(
			TEXT("trigger_depth"),
			FString::Printf(TEXT("时机 %d 递归深度达到上限 %d"),
				static_cast<int32>(Timing), HexK::MaxTriggerDepth));
		return;
	}

	// 保证 2：冻结监听者列表。
	// 遍历中若被 RebuildListeners 改动会造成未定义行为与不确定性。
	const TArray<FHexTriggerListener> Frozen = Listeners;

	++Depth;
	int32 Fired = 0;

	for (const FHexTriggerListener& L : Frozen)
	{
		if (L.Timing != Timing || !L.Trigger)
		{
			continue;
		}
		if (!CanFire(L))
		{
			continue;
		}
		if (!PassesFilter(*L.Trigger, Ctx))
		{
			continue;
		}
		if (!PassesCondition(L.Trigger->Condition, State, Ctx))
		{
			continue;
		}

		// 保证 5：宽度闸
		if (Fired >= HexK::MaxTriggersPerEmit)
		{
			State.AddRuleViolation(
				TEXT("trigger_width"),
				FString::Printf(TEXT("时机 %d 单次触发数达到上限 %d"),
					static_cast<int32>(Timing), HexK::MaxTriggersPerEmit));
			break;
		}

		MarkFired(L);
		++Fired;

		FHexBattleEvent E;
		E.Type = TEXT("rune_triggered");
		E.TextA = L.SourceTag;
		E.IntA = static_cast<int32>(Timing);
		E.IntB = L.SlotOrder;
		State.LogEvent(E);

		// 把符文效果转成动作。
		// ⚠️ 用 PushNext 而非 PushBack：子动作必须紧随触发者结算，
		//    否则槽 1 的子动作会跑到槽 6 的主动作后面（§6.5 顺序被打乱）。
		const FHexUnit* Source = State.FindUnit(Ctx.SourceUnitId);

		for (const FHexEffectStep& Step : L.Trigger->Effects)
		{
			FHexGameAction A;
			bool bValid = true;

			switch (Step.Op)
			{
			case EHexEffectOp::GainBlock:
				A = FHexActions::GainBlock(Ctx.SourceUnitId, Step.ValueFor(Source));
				break;

			case EHexEffectOp::Heal:
				A = FHexActions::Heal(Ctx.SourceUnitId, Step.ValueFor(Source));
				break;

			case EHexEffectOp::DrawCard:
				A = FHexActions::DrawCards(Step.Repeat);
				break;

			case EHexEffectOp::GainEnergy:
				A = FHexActions::GainEnergy(FMath::FloorToInt(Step.FlatValue));
				break;

			case EHexEffectOp::ApplyStatus:
				A = FHexActions::ApplyStatus(
					Step.TargetFilter == EHexTargetFilter::Self ? Ctx.SourceUnitId : Ctx.TargetUnitId,
					Step.StatusId, Step.StatusStacks);
				break;

			case EHexEffectOp::DealDamage:
			{
				FHexUnit* Target = State.FindUnit(Ctx.TargetUnitId);
				if (!Target)
				{
					bValid = false;
					break;
				}
				FHexDamageContext DC;
				DC.Source = Source;
				DC.Target = Target;
				DC.Flat = Step.FlatValue;
				DC.StatRef = Step.StatRef;
				DC.StatRatio = Step.StatRatio;
				// ⚠️ 符文产出的伤害【不再走 ②′ 钩子】—— 否则符文触发符文会无限套娃。
				const FHexDamageResult R = FHexDamageCalculator::Calculate(DC, &State.Rng);
				A = FHexActions::Damage(Ctx.SourceUnitId, Ctx.TargetUnitId,
					R.ToBarrier, R.ToBlock, R.ToHP, R.bIsCrit, R.bIsDodged);
				break;
			}

			case EHexEffectOp::ShuffleDiscardIntoDraw:
				A = FHexActions::ReshuffleDeck();
				break;

			default:
				bValid = false;
				break;
			}

			if (bValid)
			{
				A.SourceTag = L.SourceTag;
				Queue.PushNext(A);
			}
		}
	}

	--Depth;
}

// ───────────────────────────────────────────────────────── ②′ 数值钩子

FHexValueHook FHexTriggerBus::MakeValueHook(
	EHexTriggerTiming Timing,
	const FHexBattleState& State,
	const FHexTriggerContext& Ctx) const
{
	// 收集监听该时机且带数值钩子的符文，保持 Listeners 的排序（槽位 1→6）
	struct FHookEntry
	{
		FString Tag;
		int32 SlotOrder;
		bool bHasAdd;
		float AddValue;
		bool bHasMult;
		float MultValue;
	};
	TArray<FHookEntry> Chain;

	const FHexUnit* Source = State.FindUnit(Ctx.SourceUnitId);

	for (const FHexTriggerListener& L : Listeners)
	{
		if (L.Timing != Timing || !L.Trigger)
		{
			continue;
		}
		if (!L.Trigger->bHasValueAdd && !L.Trigger->bHasValueMult)
		{
			continue;
		}
		if (!CanFire(L))
		{
			continue;
		}
		if (!PassesFilter(*L.Trigger, Ctx))
		{
			continue;
		}
		if (!PassesCondition(L.Trigger->Condition, State, Ctx))
		{
			continue;
		}

		FHookEntry Entry;
		Entry.Tag = L.SourceTag;
		Entry.SlotOrder = L.SlotOrder;
		Entry.bHasAdd = L.Trigger->bHasValueAdd;
		Entry.bHasMult = L.Trigger->bHasValueMult;
		Entry.MultValue = L.Trigger->ValueMult;

		// 加区也走系数化（§7.5）：Flat + Stat×Ratio
		Entry.AddValue = 0.0f;
		if (Entry.bHasAdd)
		{
			FHexEffectStep Tmp;
			Tmp.FlatValue = L.Trigger->ValueAddFlat;
			Tmp.StatRef = L.Trigger->ValueAddStatRef;
			Tmp.StatRatio = L.Trigger->ValueAddRatio;
			Entry.AddValue = static_cast<float>(Tmp.ValueFor(Source));
		}

		Chain.Add(Entry);
	}

	if (Chain.Num() == 0)
	{
		return FHexValueHook();
	}

	// ⚠️ 按槽位顺序依次作用 —— 这是 §6.5 的核心。
	//    [加区, 乘区] → (base + add) × mult
	//    [乘区, 加区] → base × mult + add
	//    两者不同，这个差异就是"免费的一层深度"。
	return [Chain](float Running, TArray<FHexRuneStep>& Log) -> float
	{
		float Cur = Running;
		for (const FHookEntry& E : Chain)
		{
			const float Before = Cur;
			if (E.bHasAdd)
			{
				Cur += E.AddValue;
			}
			if (E.bHasMult)
			{
				Cur *= E.MultValue;
			}

			FHexRuneStep S;
			S.SourceTag = E.Tag;
			S.SlotOrder = E.SlotOrder;
			S.Before = Before;
			S.After = Cur;
			Log.Add(S);
		}
		return Cur;
	};
}

// ───────────────────────────────────────────────────────── 调试

int32 FHexTriggerBus::ListenerCountFor(EHexTriggerTiming Timing) const
{
	int32 N = 0;
	for (const FHexTriggerListener& L : Listeners)
	{
		if (L.Timing == Timing)
		{
			++N;
		}
	}
	return N;
}

void FHexTriggerBus::DescribeChain(EHexTriggerTiming Timing, TArray<FString>& Out) const
{
	Out.Reset();
	for (const FHexTriggerListener& L : Listeners)
	{
		if (L.Timing == Timing)
		{
			Out.Add(FString::Printf(TEXT("[槽%d] %s"), L.SlotOrder, *L.SourceTag));
		}
	}
}
