// Copyright Hex Spire. All Rights Reserved.

#include "Battle/HexActionResolver.h"
#include "Battle/HexBattleState.h"
#include "Battle/HexRuleBook.h"
#include "Battle/HexDamageCalculator.h"
#include "Hex/HexCoord.h"
#include "Hex/HexFootprint.h"
#include "Core/HexSpireConstants.h"

namespace
{
	// 事件类型名常量 —— 表现层按这些名字订阅
	const FName EV_EnergyChanged   = TEXT("energy_changed");
	const FName EV_DamageDealt     = TEXT("damage_dealt");
	const FName EV_Dodged          = TEXT("dodged");
	const FName EV_Healed          = TEXT("healed");
	const FName EV_BlockGained     = TEXT("block_gained");
	const FName EV_BlockCleared    = TEXT("block_cleared");
	const FName EV_UnitDied        = TEXT("unit_died");
	const FName EV_UnitMoved       = TEXT("unit_moved");
	const FName EV_UnitRotated     = TEXT("unit_rotated");
	const FName EV_Knockback       = TEXT("knockback");
	const FName EV_WallSlam        = TEXT("wall_slam");
	const FName EV_Trampled        = TEXT("trampled");
	const FName EV_StatusApplied   = TEXT("status_applied");
	const FName EV_StatusRemoved   = TEXT("status_removed");
	const FName EV_StatusTicked    = TEXT("status_ticked");
	const FName EV_CardsDrawn      = TEXT("cards_drawn");
	const FName EV_CardDiscarded   = TEXT("card_discarded");
	const FName EV_CardExhausted   = TEXT("card_exhausted");
	const FName EV_DeckReshuffled  = TEXT("deck_reshuffled");
	const FName EV_TerrainChanged  = TEXT("terrain_changed");
	const FName EV_HazardTriggered = TEXT("hazard_triggered");
	const FName EV_PhaseChanged    = TEXT("phase_changed");
	const FName EV_RoundAdvanced   = TEXT("round_advanced");

	/**
	 * 把伤害应用到单位上，并处理死亡。
	 * ⚠️ 这是【唯一】扣血的地方。任何其他"我在这里直接减 HP"的代码都是 bug。
	 */
	void ApplyDamageToUnit(
		FHexBattleState& State, FHexUnit& Target,
		int32 ToBarrier, int32 ToBlock, int32 ToHP,
		int32 SourceId, bool bCrit)
	{
		if (ToBarrier > 0)
		{
			Target.ConsumeBarrier(ToBarrier);
		}

		bool bBlockBroken = false;
		if (ToBlock > 0)
		{
			const int32 Before = Target.Block;
			Target.Block = FMath::Max(0, Target.Block - ToBlock);
			if (Before > 0 && Target.Block == 0)
			{
				bBlockBroken = true;
			}
		}

		if (ToHP > 0)
		{
			Target.HP = FMath::Max(0, Target.HP - ToHP);
		}

		FHexBattleEvent E;
		E.Type = EV_DamageDealt;
		E.SourceUnitId = SourceId;
		E.TargetUnitId = Target.Id;
		E.IntA = ToBarrier;
		E.IntB = ToBlock;
		E.IntC = ToHP;
		E.bFlagA = bCrit;
		State.LogEvent(E);

		if (bBlockBroken)
		{
			State.LogEvent(TEXT("block_broken"), SourceId, Target.Id);
		}

		// 死亡判定
		if (Target.HP <= 0 && Target.bIsAlive)
		{
			Target.bIsAlive = false;
			// 尸体不占格（§8.3）
			State.Grid.ClearOccupancy(Target.Id);
			State.LogEvent(EV_UnitDied, SourceId, Target.Id);
		}
	}

	/** 把单位移到新位置，处理占位登记 */
	bool MoveUnitTo(FHexBattleState& State, FHexUnit& Unit, const FIntVector& NewAnchor, int32 NewFacing)
	{
		// ⚠️ 必须走 CanPlace 校验（D8 单一出口）。
		//    即使是 S 体型也不能跳过 —— 见 HexFootprint.h 的 R9 说明。
		if (!FHexFootprint::CanPlace(
			State.Grid, NewAnchor, Unit.GetFootprint(), NewFacing,
			Unit.Id, Unit.CanCrushRubble()))
		{
			return false;
		}

		const FIntVector OldAnchor = Unit.Anchor;
		const int32 OldFacing = Unit.Facing;

		State.Grid.ClearOccupancy(Unit.Id);
		Unit.Anchor = NewAnchor;
		Unit.Facing = ((NewFacing % 6) + 6) % 6;

		TArray<FIntVector> Cells;
		Unit.GetCells(Cells);
		State.Grid.SetOccupancy(Unit.Id, Cells);

		FHexBattleEvent E;
		E.Type = EV_UnitMoved;
		E.TargetUnitId = Unit.Id;
		E.CoordA = OldAnchor;
		E.CoordB = NewAnchor;
		E.IntA = OldFacing;
		E.IntB = Unit.Facing;
		State.LogEvent(E);

		return true;
	}

	/**
	 * 结算单位所处格的地形危害。
	 * ⚠️ §8.2.2 机制点 7：大体型同时站 N 个危害格 → 【每个各结算一次】。
	 *    这是大体型的真实代价，不能只算一次。
	 */
	void ResolveHazardsFor(FHexBattleState& State, FHexUnit& Unit)
	{
		if (!Unit.bIsAlive)
		{
			return;
		}

		TArray<FIntVector> Cells;
		Unit.GetCells(Cells);

		int32 TotalDamage = 0;
		int32 HazardCellCount = 0;

		for (const FIntVector& C : Cells)
		{
			if (State.Grid.HazardAt(C) != EHexHazard::None)
			{
				TotalDamage += HexK::HazardDamage;
				++HazardCellCount;
			}
		}

		if (TotalDamage <= 0)
		{
			return;
		}

		FHexBattleEvent E;
		E.Type = EV_HazardTriggered;
		E.TargetUnitId = Unit.Id;
		E.IntA = TotalDamage;
		E.IntB = HazardCellCount;
		State.LogEvent(E);

		// 危害伤害不可闪避、不被格挡吸收（它是环境伤害）
		ApplyDamageToUnit(State, Unit, 0, 0, TotalDamage, -1, false);
	}
}

// ───────────────────────────────────────────────────────── 执行

void FHexActionResolver::Execute(const FHexGameAction& Action, FHexBattleState& State)
{
	State.LogAction(Action);

	switch (Action.Type)
	{
	// ═══════════════════════════════════════════ 资源
	case EHexActionType::SpendEnergy:
	{
		State.Energy = FMath::Max(0, State.Energy - Action.IntA);
		FHexBattleEvent E;
		E.Type = EV_EnergyChanged;
		E.IntA = State.Energy;
		E.IntB = -Action.IntA;
		State.LogEvent(E);
		break;
	}

	case EHexActionType::GainEnergy:
	{
		State.Energy += Action.IntA;
		FHexBattleEvent E;
		E.Type = EV_EnergyChanged;
		E.IntA = State.Energy;
		E.IntB = Action.IntA;
		State.LogEvent(E);
		break;
	}

	case EHexActionType::SetEnergy:
	{
		State.Energy = FMath::Max(0, Action.IntA);
		FHexBattleEvent E;
		E.Type = EV_EnergyChanged;
		E.IntA = State.Energy;
		State.LogEvent(E);
		break;
	}

	// ═══════════════════════════════════════════ 伤害与生命
	case EHexActionType::Damage:
	{
		FHexUnit* Target = State.FindUnit(Action.TargetUnitId);
		if (!Target || !Target->bIsAlive)
		{
			break;
		}

		if (Action.bFlagB) // 闪避
		{
			State.LogEvent(EV_Dodged, Action.SourceUnitId, Action.TargetUnitId);
			break;
		}

		ApplyDamageToUnit(State, *Target,
			Action.IntA, Action.IntB, Action.IntC,
			Action.SourceUnitId, Action.bFlagA);
		break;
	}

	case EHexActionType::Heal:
	{
		FHexUnit* Target = State.FindUnit(Action.TargetUnitId);
		if (!Target || !Target->bIsAlive)
		{
			break;
		}
		const int32 Before = Target->HP;
		Target->HP = FMath::Min(Target->HPMax, Target->HP + Action.IntA);

		FHexBattleEvent E;
		E.Type = EV_Healed;
		E.TargetUnitId = Target->Id;
		E.IntA = Target->HP - Before;
		State.LogEvent(E);
		break;
	}

	case EHexActionType::GainBlock:
	{
		FHexUnit* Target = State.FindUnit(Action.TargetUnitId);
		if (!Target || !Target->bIsAlive)
		{
			break;
		}

		// 《巨化》类符文的代价：无法获得格挡
		if (Target->Team == EHexTeam::Player && !FHexRuleBook::CanGainBlock(State))
		{
			break;
		}

		// ⚠️ 格挡上限（§4.2）。上限缺失会让失败条件在数学上不存在 ——
		//    Godot 版实测镇妖者 10 回合叠到 140 格挡而 HP 只有 80。
		const int32 Cap = Target->GetBlockCap();
		const int32 Before = Target->Block;
		Target->Block = FMath::Min(Cap, Target->Block + Action.IntA);

		FHexBattleEvent E;
		E.Type = EV_BlockGained;
		E.TargetUnitId = Target->Id;
		E.IntA = Target->Block - Before;
		E.IntB = Target->Block;
		E.IntC = Cap;
		State.LogEvent(E);
		break;
	}

	case EHexActionType::ClearBlock:
	{
		FHexUnit* Target = State.FindUnit(Action.TargetUnitId);
		if (!Target)
		{
			break;
		}
		// 镇妖者被动：格挡不在回合结束清空（§4.2）
		if (Target->Team == EHexTeam::Player && FHexRuleBook::BlockPersists(State))
		{
			break;
		}
		Target->Block = 0;
		State.LogEvent(EV_BlockCleared, -1, Target->Id);
		break;
	}

	case EHexActionType::Kill:
	{
		FHexUnit* Target = State.FindUnit(Action.TargetUnitId);
		if (!Target || !Target->bIsAlive)
		{
			break;
		}
		Target->HP = 0;
		Target->bIsAlive = false;
		State.Grid.ClearOccupancy(Target->Id);
		State.LogEvent(EV_UnitDied, Action.SourceUnitId, Target->Id);
		break;
	}

	// ═══════════════════════════════════════════ 位移
	case EHexActionType::MoveUnit:
	{
		FHexUnit* Unit = State.FindUnit(Action.TargetUnitId);
		if (!Unit || !Unit->bIsAlive)
		{
			break;
		}

		// 定身状态：不可移动
		if (Unit->IsMovementBlocked())
		{
			break;
		}

		if (MoveUnitTo(State, *Unit, Action.CoordA, Action.IntA))
		{
			// 流血：移动时结算（§8.6 位移即伤害的延伸）
			const int32 BleedStacks = Unit->GetStatusStacks(FHexStatusLibrary::Bleed);
			if (BleedStacks > 0)
			{
				const int32 Dmg = FHexStatusLibrary::Get(FHexStatusLibrary::Bleed).DamageOnMovePerStack
					* BleedStacks;
				ApplyDamageToUnit(State, *Unit, 0, 0, Dmg, -1, false);
			}

			ResolveHazardsFor(State, *Unit);
		}
		break;
	}

	case EHexActionType::RotateUnit:
	{
		FHexUnit* Unit = State.FindUnit(Action.TargetUnitId);
		if (!Unit || !Unit->bIsAlive)
		{
			break;
		}
		const int32 NewFacing = ((Action.IntA % 6) + 6) % 6;

		// ⚠️ 多格单位转向会改变占位，必须校验（§8.2.2 机制点 5）
		if (!FHexFootprint::CanPlace(
			State.Grid, Unit->Anchor, Unit->GetFootprint(), NewFacing,
			Unit->Id, Unit->CanCrushRubble()))
		{
			break;
		}

		const int32 OldFacing = Unit->Facing;
		State.Grid.ClearOccupancy(Unit->Id);
		Unit->Facing = NewFacing;
		TArray<FIntVector> Cells;
		Unit->GetCells(Cells);
		State.Grid.SetOccupancy(Unit->Id, Cells);

		FHexBattleEvent E;
		E.Type = EV_UnitRotated;
		E.TargetUnitId = Unit->Id;
		E.IntA = OldFacing;
		E.IntB = NewFacing;
		State.LogEvent(E);
		break;
	}

	case EHexActionType::Knockback:
	{
		FHexUnit* Target = State.FindUnit(Action.TargetUnitId);
		const FHexUnit* Source = State.FindUnit(Action.SourceUnitId);
		if (!Target || !Target->bIsAlive || !Source)
		{
			break;
		}

		// 位移抗性（§8.2.2 机制点 4）：L 完全免疫
		const int32 Resist = FHexRuleBook::KnockbackResistOf(State, *Target);
		const int32 Distance = Action.IntA - Resist;
		if (Distance <= 0)
		{
			break;
		}

		// 方向：攻击者 → 目标
		const FIntVector Delta = Target->Anchor - Source->Anchor;
		int32 BestDir = 0;
		int32 BestDist = TNumericLimits<int32>::Max();
		for (int32 I = 0; I < 6; ++I)
		{
			const FIntVector Probe = Source->Anchor + FHexCoord::Dirs[I]
				* FMath::Max(1, FHexCoord::Distance(Source->Anchor, Target->Anchor));
			const int32 D = FHexCoord::Distance(Probe, Target->Anchor);
			if (D < BestDist)
			{
				BestDist = D;
				BestDir = I;
			}
		}
		const FIntVector Dir = FHexCoord::Dirs[BestDir];

		// 逐格推动，撞墙/撞单位则停下并结算撞击伤害
		int32 Moved = 0;
		bool bSlammed = false;
		for (int32 Step = 0; Step < Distance; ++Step)
		{
			const FIntVector Next = Target->Anchor + Dir;
			if (!FHexFootprint::CanPlace(
				State.Grid, Next, Target->GetFootprint(), Target->Facing,
				Target->Id, Target->CanCrushRubble()))
			{
				bSlammed = true;
				break;
			}
			MoveUnitTo(State, *Target, Next, Target->Facing);
			++Moved;
		}

		FHexBattleEvent E;
		E.Type = EV_Knockback;
		E.SourceUnitId = Action.SourceUnitId;
		E.TargetUnitId = Target->Id;
		E.IntA = Moved;
		E.bFlagA = bSlammed;
		State.LogEvent(E);

		// ⚠️ 撞墙伤害是「位移即伤害」的一半（§8.6 / Into the Breach 的核心课）。
		//    没有它，位移卡只有"跑位"价值，太单薄。
		if (bSlammed)
		{
			State.LogEvent(EV_WallSlam, Action.SourceUnitId, Target->Id);
			ApplyDamageToUnit(State, *Target, 0, 0, HexK::WallSlamDamage,
				Action.SourceUnitId, false);
		}

		// 被推动后可能落在危害格上
		if (Moved > 0)
		{
			ResolveHazardsFor(State, *Target);
		}
		break;
	}

	case EHexActionType::Trample:
	{
		FHexUnit* Source = State.FindUnit(Action.SourceUnitId);
		FHexUnit* Target = State.FindUnit(Action.TargetUnitId);
		if (!Source || !Target || !Target->bIsAlive)
		{
			break;
		}
		// 只能碾压比自己小的单位（§8.2.2 机制点 6）
		if (!Source->CanTrampleBelow() ||
			static_cast<uint8>(Target->SizeClass) >= static_cast<uint8>(Source->SizeClass))
		{
			break;
		}

		State.LogEvent(EV_Trampled, Source->Id, Target->Id);

		// 被穿过者受伤并被挤到相邻空格；无空格则额外受伤留在原地
		bool bPushed = false;
		for (int32 I = 0; I < 6; ++I)
		{
			const FIntVector Cand = Target->Anchor + FHexCoord::Dirs[I];
			if (FHexFootprint::CanPlace(
				State.Grid, Cand, Target->GetFootprint(), Target->Facing,
				Target->Id, Target->CanCrushRubble()))
			{
				MoveUnitTo(State, *Target, Cand, Target->Facing);
				bPushed = true;
				break;
			}
		}

		const int32 Dmg = bPushed ? HexK::WallSlamDamage : HexK::WallSlamDamage * 2;
		ApplyDamageToUnit(State, *Target, 0, 0, Dmg, Source->Id, false);
		break;
	}

	// ═══════════════════════════════════════════ 状态
	case EHexActionType::ApplyStatus:
	{
		FHexUnit* Target = State.FindUnit(Action.TargetUnitId);
		if (!Target || !Target->bIsAlive)
		{
			break;
		}
		const int32 NewStacks = Target->ApplyStatus(Action.NameA, Action.IntA);
		if (NewStacks > 0)
		{
			FHexBattleEvent E;
			E.Type = EV_StatusApplied;
			E.SourceUnitId = Action.SourceUnitId;
			E.TargetUnitId = Target->Id;
			E.NameA = Action.NameA;
			E.IntA = Action.IntA;
			E.IntB = NewStacks;
			State.LogEvent(E);
		}
		break;
	}

	case EHexActionType::RemoveStatus:
	{
		FHexUnit* Target = State.FindUnit(Action.TargetUnitId);
		if (!Target)
		{
			break;
		}
		Target->RemoveStatus(Action.NameA);
		FHexBattleEvent E;
		E.Type = EV_StatusRemoved;
		E.TargetUnitId = Target->Id;
		E.NameA = Action.NameA;
		State.LogEvent(E);
		break;
	}

	case EHexActionType::TickStatus:
	{
		FHexUnit* Target = State.FindUnit(Action.TargetUnitId);
		if (!Target || !Target->bIsAlive)
		{
			break;
		}
		const EHexStatusTick Timing = static_cast<EHexStatusTick>(Action.IntA);

		// ⚠️ 按状态数组的已排序顺序结算（FHexUnit::ApplyStatus 保证有序），
		//    否则燃烧与中毒的先后会漂移，影响"是否恰好打死"的结果。
		int32 TotalNormal = 0;
		int32 TotalIgnoreBlock = 0;

		for (const FHexStatusInstance& S : Target->Statuses)
		{
			const FHexStatusDef& Def = FHexStatusLibrary::Get(S.Id);
			if (Def.TickTiming != Timing || Def.TickDamagePerStack <= 0)
			{
				continue;
			}
			const int32 Dmg = Def.TickDamagePerStack * S.Stacks;
			if (Def.bTickIgnoresBlock)
			{
				TotalIgnoreBlock += Dmg;
			}
			else
			{
				TotalNormal += Dmg;
			}
		}

		if (TotalNormal > 0 || TotalIgnoreBlock > 0)
		{
			FHexBattleEvent E;
			E.Type = EV_StatusTicked;
			E.TargetUnitId = Target->Id;
			E.IntA = TotalNormal;
			E.IntB = TotalIgnoreBlock;
			State.LogEvent(E);
		}

		// 中毒无视格挡（这是它与燃烧的唯一区别，也是它存在的理由）
		if (TotalIgnoreBlock > 0)
		{
			ApplyDamageToUnit(State, *Target, 0, 0, TotalIgnoreBlock, -1, false);
		}
		// 燃烧可被格挡吸收
		if (TotalNormal > 0 && Target->bIsAlive)
		{
			const int32 ToBlock = FMath::Min(TotalNormal, Target->Block);
			ApplyDamageToUnit(State, *Target, 0, ToBlock, TotalNormal - ToBlock, -1, false);
		}
		break;
	}

	// ═══════════════════════════════════════════ 牌堆
	case EHexActionType::DrawCards:
	{
		TArray<FHexCardInstance> Drawn;
		bool bReshuffled = false;
		const int32 Limit = FHexRuleBook::HandLimit(State);
		const int32 N = State.Piles.Draw(Action.IntA, Limit, State.Rng, Drawn, bReshuffled);

		if (bReshuffled)
		{
			// D2 带来的新时机，是很好的组合钩子（§6.3）
			State.LogEvent(EV_DeckReshuffled);
		}

		FHexBattleEvent E;
		E.Type = EV_CardsDrawn;
		E.IntA = N;
		E.IntB = State.Piles.NumDraw();
		E.IntC = State.Piles.NumDiscard();
		State.LogEvent(E);
		break;
	}

	case EHexActionType::DiscardCard:
	{
		if (State.Piles.DiscardFromHand(Action.IntA))
		{
			FHexBattleEvent E;
			E.Type = EV_CardDiscarded;
			E.IntA = Action.IntA;
			State.LogEvent(E);
		}
		break;
	}

	case EHexActionType::ExhaustCard:
	{
		if (State.Piles.ResolvePlayedCard(Action.IntA, true))
		{
			FHexBattleEvent E;
			E.Type = EV_CardExhausted;
			E.IntA = Action.IntA;
			State.LogEvent(E);
		}
		break;
	}

	case EHexActionType::ReshuffleDeck:
	{
		if (State.Piles.ReshuffleDiscardIntoDraw(State.Rng))
		{
			State.LogEvent(EV_DeckReshuffled);
		}
		break;
	}

	// ═══════════════════════════════════════════ 地形
	case EHexActionType::ModifyTerrain:
	{
		State.Grid.SetTerrain(Action.CoordA, static_cast<EHexTerrain>(Action.IntA));
		FHexBattleEvent E;
		E.Type = EV_TerrainChanged;
		E.CoordA = Action.CoordA;
		E.IntA = Action.IntA;
		State.LogEvent(E);
		break;
	}

	case EHexActionType::TriggerHazard:
	{
		FHexUnit* Unit = State.FindUnit(Action.TargetUnitId);
		if (Unit)
		{
			ResolveHazardsFor(State, *Unit);
		}
		break;
	}

	// ═══════════════════════════════════════════ 流程
	case EHexActionType::SetPhase:
	{
		const EHexBattlePhase Old = State.Phase;
		State.Phase = static_cast<EHexBattlePhase>(Action.IntA);
		FHexBattleEvent E;
		E.Type = EV_PhaseChanged;
		E.IntA = static_cast<int32>(Old);
		E.IntB = Action.IntA;
		State.LogEvent(E);
		break;
	}

	case EHexActionType::AdvanceRound:
	{
		++State.RoundNumber;
		State.CardsPlayedThisRound = 0;
		FHexBattleEvent E;
		E.Type = EV_RoundAdvanced;
		E.IntA = State.RoundNumber;
		State.LogEvent(E);
		break;
	}

	case EHexActionType::SpawnUnit:
		// 召唤在 P2 实现；此处占位以保证 VerifyAllHandlersRegistered 通过
		break;

	case EHexActionType::None:
		break;

	default:
		// ⚠️ 未注册的动作类型 —— 记违规而非静默失败。
		//    "加了新动作却忘了写处理器"只能靠这条抓住。
		State.AddRuleViolation(
			TEXT("no_handler"),
			FString::Printf(TEXT("动作类型 %d 没有处理器"), static_cast<int32>(Action.Type)));
		break;
	}
}

bool FHexActionResolver::VerifyAllHandlersRegistered(TArray<EHexActionType>& OutMissing)
{
	OutMissing.Reset();

	// 用一个临时状态跑一遍所有动作类型，看是否产生 no_handler 违规
	for (int32 I = 1; I < static_cast<int32>(EHexActionType::Count); ++I)
	{
		FHexBattleState Probe(1);
		FHexGameAction A;
		A.Type = static_cast<EHexActionType>(I);
		Execute(A, Probe);

		for (const FHexRuleViolation& V : Probe.GetRuleViolations())
		{
			if (V.Kind == TEXT("no_handler"))
			{
				OutMissing.Add(static_cast<EHexActionType>(I));
				break;
			}
		}
	}
	return OutMissing.Num() == 0;
}
