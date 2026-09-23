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

	// ⚠️ 这里【刻意不清】ThresholdCounts。
	//    "每移动 3 格触发一次"攒到 2/3 时回合结束，
	//    清零会让玩家的计数凭空消失 ——
	//    那会让这类符文在回合边界附近变得无法预测。
}

void FHexTriggerBus::ResetBattleCounters()
{
	BattleCounts.Reset();
	RoundCounts.Reset();

	// 战斗之间必须清：跨场残留会让新一场的第一次触发莫名提前。
	ThresholdCounts.Reset();
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

bool FHexTriggerBus::AdvanceCounter(const FHexTriggerListener& L)
{
	const int32 Threshold = L.Trigger ? L.Trigger->CounterThreshold : 0;

	// 0 / 1 都表示"每次都触发"，不进计数逻辑。
	// 把 1 也短路掉是有意的：否则 Threshold=1 会白占一个 TMap 条目。
	if (Threshold <= 1)
	{
		return true;
	}

	const FString Key = CountKey(L);
	int32& N = ThresholdCounts.FindOrAdd(Key);
	++N;

	if (N >= Threshold)
	{
		N = 0;
		return true;
	}
	return false;
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
	// ⚠️ 这个 switch 刻意【不写】default 之外的兜底逻辑，
	//    并在末尾对每个枚举值显式列举 —— 目的是让编译器在
	//    新增条件类型时发出 -Wswitch 警告（UE 默认 warning as error），
	//    把"忘了实现新条件"挡在编译阶段，而不是留到运行时。
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

	case FHexEffectCondition::EKind::SelfNotAtFullHP:
	{
		const FHexUnit* S = State.FindUnit(Ctx.SourceUnitId);
		// ⚠️ 找不到单位时返回 false（而不是"未满血=true"）：
		//    条件判定的失败方向必须是"不触发"，见下方 default 的说明。
		return S && S->HP < S->HPMax;
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
		// ⚠️ 未实现的条件必须判【不通过】，绝不能返回 true。
		//
		//    这里原先是 `return true`。当前 6 个条件都实现了，
		//    所以走不到这一支 —— 但它是给未来埋的地雷：
		//    将来加一个条件（比如"抽牌堆≥N张"）却忘了在这里实现，
		//    那个条件就会恒为真，于是
		//    「满血时暴击翻倍」变成「永远暴击翻倍」——
		//    符文悄悄变成无条件的强力符文，平衡直接崩，
		//    而且表现为"这符文怎么这么强"，没人会想到是条件失效。
		//
		//    判不通过则相反：符文明显变弱（一次都不触发），
		//    配合下面的违规记录立刻能定位。
		//    宁可"暂时不生效"，也不要"悄悄超模"。
		return false;
	}
}

// ───────────────────────────────────────────────────────── 分发

void FHexTriggerBus::Emit(
	EHexTriggerTiming Timing,
	const FHexTriggerContext& Ctx,
	FHexBattleState& State,
	FHexActionQueue& Queue)
{
	// ⚠️ 计数必须在【所有 return 之前】累加。
	//    它统计的是"这个时机被派发过"，而不是"有符文响应了"——
	//    埋点覆盖率验证要区分的正是这两件事：
	//    没人 Emit（缺陷）vs Emit 了但没符文监听（正常）。
	{
		const int32 TI = static_cast<int32>(Timing);
		if (TI >= 0 && TI < static_cast<int32>(EHexTriggerTiming::Count))
		{
			++EmitCounts[TI];
		}
	}

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

		// ⚠️ CounterThreshold 必须放在【过滤器与条件之后】。
		//    「每移动 3 格触发」若在条件判定前累加，就会被那些
		//    本不该计数的事件撑满 —— 玩家数着格子却触发不了，
		//    而且这种错位极难自查（看起来像"触发很随机"）。
		//
		//    同时放在宽度闸【之前】：攒计数不算一次触发，
		//    否则一个 Threshold=5 的符文会白占 5 个触发预算。
		if (!AdvanceCounter(L))
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
				// ⚠️ 末参数 Ctx.SourceUnitId：符文（属于玩家）施加的
				//    燃烧/中毒致死时要算玩家击杀，否则
				//    《焚心》(暴击→燃烧) + 《食魂》(击杀→抽牌) 这条
				//    组合链会断在最后一步。
				A = FHexActions::ApplyStatus(
					Step.TargetFilter == EHexTargetFilter::Self ? Ctx.SourceUnitId : Ctx.TargetUnitId,
					Step.StatusId, Step.StatusStacks, Ctx.SourceUnitId);
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
				// ⚠️ 绝不静默丢弃。
				//
				//    EHexEffectOp 有 24 个算子，这里只实现了 8 个。
				//    原先走到 default 就无声跳过 —— 于是给符文写了
				//    一个"位移敌人"或"改地形"的效果，它会安安静静
				//    什么都不做：不报错、不崩溃、日志里也没有痕迹。
				//    作者只会以为"这符文好像很弱"，然后去调数值。
				//
				//    记成违规后，验证器与 BattleSim 会立刻把它顶出来，
				//    而且违规文本直接说明"该算子尚未实现"，
				//    把排查时间从几小时压到几秒。
				bValid = false;
				State.AddRuleViolation(
					TEXT("rune_effect_op_unimplemented"),
					FString::Printf(
						TEXT("符文 %s 使用了尚未在 TriggerBus 实现的效果算子 %d ——")
						TEXT("该效果不会产生任何作用。请在 Emit 的 switch 中补实现。"),
						*L.SourceTag, static_cast<int32>(Step.Op)));
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

int32 FHexTriggerBus::GetEmitCount(EHexTriggerTiming Timing) const
{
	const int32 TI = static_cast<int32>(Timing);
	if (TI < 0 || TI >= static_cast<int32>(EHexTriggerTiming::Count))
	{
		return 0;
	}
	return EmitCounts[TI];
}

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
