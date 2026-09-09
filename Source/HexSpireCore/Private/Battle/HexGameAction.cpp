// Copyright Hex Spire. All Rights Reserved.

#include "Battle/HexGameAction.h"
#include "Battle/HexBattleState.h"
#include "Battle/HexActionResolver.h"
#include "Hex/HexCoord.h"

// ───────────────────────────────────────────────────────── FHexGameAction

void FHexGameAction::Serialize(FArchive& Ar)
{
	uint8 T = static_cast<uint8>(Type);
	Ar << T;
	if (Ar.IsLoading())
	{
		Type = static_cast<EHexActionType>(T);
	}
	Ar << SourceUnitId << TargetUnitId;
	Ar << IntA << IntB << IntC;
	Ar << FloatA;
	Ar << NameA;
	Ar << CoordA << CoordB;
	Ar << bFlagA << bFlagB;
	Ar << SourceTag;
}

FString FHexGameAction::ToDebugString() const
{
	const TCHAR* TypeNames[] = {
		TEXT("None"),
		TEXT("SpendEnergy"), TEXT("GainEnergy"), TEXT("SetEnergy"),
		TEXT("Damage"), TEXT("Heal"), TEXT("GainBlock"), TEXT("ClearBlock"), TEXT("Kill"),
		TEXT("MoveUnit"), TEXT("RotateUnit"), TEXT("Knockback"), TEXT("Trample"),
		TEXT("ApplyStatus"), TEXT("RemoveStatus"), TEXT("TickStatus"),
		TEXT("DrawCards"), TEXT("DiscardCard"), TEXT("ExhaustCard"), TEXT("ReshuffleDeck"),
		TEXT("ModifyTerrain"), TEXT("TriggerHazard"),
		TEXT("SetPhase"), TEXT("AdvanceRound"), TEXT("SpawnUnit"),
	};
	const int32 Idx = static_cast<int32>(Type);
	const TCHAR* Name = (Idx >= 0 && Idx < UE_ARRAY_COUNT(TypeNames)) ? TypeNames[Idx] : TEXT("?");

	return FString::Printf(
		TEXT("%s src=%d tgt=%d A=%d B=%d C=%d f=%.2f n=%s coord=%s tag=%s"),
		Name, SourceUnitId, TargetUnitId, IntA, IntB, IntC, FloatA,
		*NameA.ToString(), *FHexCoord::ToOffsetString(CoordA), *SourceTag);
}

// ───────────────────────────────────────────────────────── FHexActions

namespace
{
	FORCEINLINE FHexGameAction Make(EHexActionType T)
	{
		FHexGameAction A;
		A.Type = T;
		return A;
	}
}

FHexGameAction FHexActions::SpendEnergy(int32 Amount)
{
	FHexGameAction A = Make(EHexActionType::SpendEnergy);
	A.IntA = Amount;
	return A;
}

FHexGameAction FHexActions::GainEnergy(int32 Amount)
{
	FHexGameAction A = Make(EHexActionType::GainEnergy);
	A.IntA = Amount;
	return A;
}

FHexGameAction FHexActions::SetEnergy(int32 Value)
{
	FHexGameAction A = Make(EHexActionType::SetEnergy);
	A.IntA = Value;
	return A;
}

FHexGameAction FHexActions::Damage(
	int32 SourceId, int32 TargetId,
	int32 ToBarrier, int32 ToBlock, int32 ToHP,
	bool bCrit, bool bDodged)
{
	FHexGameAction A = Make(EHexActionType::Damage);
	A.SourceUnitId = SourceId;
	A.TargetUnitId = TargetId;
	A.IntA = ToBarrier;
	A.IntB = ToBlock;
	A.IntC = ToHP;
	A.bFlagA = bCrit;
	A.bFlagB = bDodged;
	return A;
}

FHexGameAction FHexActions::Heal(int32 TargetId, int32 Amount)
{
	FHexGameAction A = Make(EHexActionType::Heal);
	A.TargetUnitId = TargetId;
	A.IntA = Amount;
	return A;
}

FHexGameAction FHexActions::GainBlock(int32 TargetId, int32 Amount)
{
	FHexGameAction A = Make(EHexActionType::GainBlock);
	A.TargetUnitId = TargetId;
	A.IntA = Amount;
	return A;
}

FHexGameAction FHexActions::ClearBlock(int32 TargetId)
{
	FHexGameAction A = Make(EHexActionType::ClearBlock);
	A.TargetUnitId = TargetId;
	return A;
}

FHexGameAction FHexActions::Kill(int32 TargetId)
{
	FHexGameAction A = Make(EHexActionType::Kill);
	A.TargetUnitId = TargetId;
	return A;
}

FHexGameAction FHexActions::MoveUnit(int32 UnitId, const FIntVector& ToAnchor, int32 ToFacing)
{
	FHexGameAction A = Make(EHexActionType::MoveUnit);
	A.TargetUnitId = UnitId;
	A.CoordA = ToAnchor;
	A.IntA = ToFacing;
	return A;
}

FHexGameAction FHexActions::RotateUnit(int32 UnitId, int32 ToFacing)
{
	FHexGameAction A = Make(EHexActionType::RotateUnit);
	A.TargetUnitId = UnitId;
	A.IntA = ToFacing;
	return A;
}

FHexGameAction FHexActions::Knockback(int32 SourceId, int32 TargetId, int32 Distance)
{
	FHexGameAction A = Make(EHexActionType::Knockback);
	A.SourceUnitId = SourceId;
	A.TargetUnitId = TargetId;
	A.IntA = Distance;
	return A;
}

FHexGameAction FHexActions::Trample(int32 SourceId, int32 TargetId)
{
	FHexGameAction A = Make(EHexActionType::Trample);
	A.SourceUnitId = SourceId;
	A.TargetUnitId = TargetId;
	return A;
}

FHexGameAction FHexActions::ApplyStatus(int32 TargetId, FName StatusId, int32 Stacks)
{
	FHexGameAction A = Make(EHexActionType::ApplyStatus);
	A.TargetUnitId = TargetId;
	A.NameA = StatusId;
	A.IntA = Stacks;
	return A;
}

FHexGameAction FHexActions::RemoveStatus(int32 TargetId, FName StatusId)
{
	FHexGameAction A = Make(EHexActionType::RemoveStatus);
	A.TargetUnitId = TargetId;
	A.NameA = StatusId;
	return A;
}

FHexGameAction FHexActions::TickStatus(int32 TargetId, EHexStatusTick Timing)
{
	FHexGameAction A = Make(EHexActionType::TickStatus);
	A.TargetUnitId = TargetId;
	A.IntA = static_cast<int32>(Timing);
	return A;
}

FHexGameAction FHexActions::DrawCards(int32 Count)
{
	FHexGameAction A = Make(EHexActionType::DrawCards);
	A.IntA = Count;
	return A;
}

FHexGameAction FHexActions::DiscardCard(int32 CardUid)
{
	FHexGameAction A = Make(EHexActionType::DiscardCard);
	A.IntA = CardUid;
	return A;
}

FHexGameAction FHexActions::ExhaustCard(int32 CardUid)
{
	FHexGameAction A = Make(EHexActionType::ExhaustCard);
	A.IntA = CardUid;
	return A;
}

FHexGameAction FHexActions::ReshuffleDeck()
{
	return Make(EHexActionType::ReshuffleDeck);
}

FHexGameAction FHexActions::ModifyTerrain(const FIntVector& Cell, EHexTerrain Terrain)
{
	FHexGameAction A = Make(EHexActionType::ModifyTerrain);
	A.CoordA = Cell;
	A.IntA = static_cast<int32>(Terrain);
	return A;
}

FHexGameAction FHexActions::TriggerHazard(int32 UnitId)
{
	FHexGameAction A = Make(EHexActionType::TriggerHazard);
	A.TargetUnitId = UnitId;
	return A;
}

FHexGameAction FHexActions::SetPhase(EHexBattlePhase Phase)
{
	FHexGameAction A = Make(EHexActionType::SetPhase);
	A.IntA = static_cast<int32>(Phase);
	return A;
}

FHexGameAction FHexActions::AdvanceRound()
{
	return Make(EHexActionType::AdvanceRound);
}

// ───────────────────────────────────────────────────────── FHexActionQueue

void FHexActionQueue::PushBack(const FHexGameAction& Action)
{
	Actions.Add(Action);
}

void FHexActionQueue::PushNext(const FHexGameAction& Action)
{
	if (!bResolving || CursorIndex < 0)
	{
		// 不在结算过程中，等价于 PushBack
		Actions.Add(Action);
		return;
	}

	// ⚠️ 插到【当前动作之后】而非队尾。
	//    连续多次 PushNext 时，后插入的要排在先插入的之后 ——
	//    所以每次插入后 InsertOffset 递增，保证符文效果内部顺序也稳定。
	Actions.Insert(Action, CursorIndex + 1 + PendingInsertOffset);
	++PendingInsertOffset;
}

void FHexActionQueue::Reset()
{
	Actions.Reset();
	CursorIndex = -1;
	bResolving = false;
	PendingInsertOffset = 0;
}

int32 FHexActionQueue::ResolveAll(FHexBattleState& State)
{
	if (bResolving)
	{
		// 防重入：Resolver 内部若又调 ResolveAll 会导致游标错乱
		return 0;
	}

	bResolving = true;
	int32 Executed = 0;

	for (CursorIndex = 0; CursorIndex < Actions.Num(); ++CursorIndex)
	{
		// R7 安全闸：超限【不崩溃】，写违规记录并中止
		if (Executed >= HexK::MaxActionsPerResolve)
		{
			State.AddRuleViolation(
				TEXT("action_overflow"),
				FString::Printf(TEXT("单次结算动作数超过上限 %d，已中止"),
					HexK::MaxActionsPerResolve));
			break;
		}

		PendingInsertOffset = 0;
		FHexActionResolver::Execute(Actions[CursorIndex], State);
		++Executed;
	}

	bResolving = false;
	CursorIndex = -1;
	PendingInsertOffset = 0;
	Actions.Reset();

	return Executed;
}
