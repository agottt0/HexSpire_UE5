// Copyright Hex Spire. All Rights Reserved.

#include "Battle/HexBattleState.h"
#include "Hex/HexFootprint.h"

// ───────────────────────────────────────────────────────── FHexBattleEvent

void FHexBattleEvent::Serialize(FArchive& Ar)
{
	Ar << Type;
	Ar << SourceUnitId << TargetUnitId;
	Ar << IntA << IntB << IntC;
	Ar << NameA;
	Ar << CoordA << CoordB;
	Ar << bFlagA;
	Ar << TextA;
}

// ───────────────────────────────────────────────────────── 构造

FHexBattleState::FHexBattleState(uint64 MasterSeed)
	: Rng(MasterSeed)
{
}

// ───────────────────────────────────────────────────────── 单位管理

int32 FHexBattleState::AddUnit(const FHexUnit& Unit)
{
	FHexUnit Copy = Unit;
	Copy.Id = NextUnitId++;
	Units.Add(Copy);

	// 保持 id 升序（确定性遍历的前提）
	Units.Sort([](const FHexUnit& A, const FHexUnit& B) { return A.Id < B.Id; });

	// 登记占位
	TArray<FIntVector> Cells;
	Copy.GetCells(Cells);
	Grid.SetOccupancy(Copy.Id, Cells);

	return Copy.Id;
}

FHexUnit* FHexBattleState::FindUnit(int32 UnitId)
{
	for (FHexUnit& U : Units)
	{
		if (U.Id == UnitId)
		{
			return &U;
		}
	}
	return nullptr;
}

const FHexUnit* FHexBattleState::FindUnit(int32 UnitId) const
{
	for (const FHexUnit& U : Units)
	{
		if (U.Id == UnitId)
		{
			return &U;
		}
	}
	return nullptr;
}

FHexUnit* FHexBattleState::GetHero()
{
	return FindUnit(HeroUnitId);
}

const FHexUnit* FHexBattleState::GetHero() const
{
	return FindUnit(HeroUnitId);
}

void FHexBattleState::GetAliveEnemyIds(TArray<int32>& Out) const
{
	Out.Reset();
	for (const FHexUnit& U : Units)
	{
		if (U.bIsAlive && U.Team == EHexTeam::Enemy)
		{
			Out.Add(U.Id);
		}
	}
	// Units 已按 id 升序，Out 天然升序
}

void FHexBattleState::GetAlivePlayerIds(TArray<int32>& Out) const
{
	Out.Reset();
	for (const FHexUnit& U : Units)
	{
		if (U.bIsAlive && U.Team == EHexTeam::Player)
		{
			Out.Add(U.Id);
		}
	}
}

void FHexBattleState::GetEnemyActionOrder(TArray<int32>& Out) const
{
	GetAliveEnemyIds(Out);

	// ⚠️ 按 AGI 降序，同值按 id 升序。
	//    id 作为 tiebreak 是纪律 5 的明确要求 ——
	//    没有它，两个同 AGI 敌人的行动顺序会随容器实现漂移，
	//    回放与种子复现全部失效。
	const TArray<FHexUnit>& UnitsRef = Units;
	Out.Sort([&UnitsRef](int32 A, int32 B)
	{
		const FHexUnit* UA = nullptr;
		const FHexUnit* UB = nullptr;
		for (const FHexUnit& U : UnitsRef)
		{
			if (U.Id == A) { UA = &U; }
			if (U.Id == B) { UB = &U; }
		}
		if (!UA || !UB)
		{
			return A < B;
		}
		if (UA->AGI != UB->AGI)
		{
			return UA->AGI > UB->AGI;
		}
		return UA->Id < UB->Id;
	});
}

FHexUnit* FHexBattleState::FindUnitAtCell(const FIntVector& Cell)
{
	const int32 Occ = Grid.OccupantAt(Cell);
	return Occ >= 0 ? FindUnit(Occ) : nullptr;
}

const FHexUnit* FHexBattleState::FindUnitAtCell(const FIntVector& Cell) const
{
	const int32 Occ = Grid.OccupantAt(Cell);
	return Occ >= 0 ? FindUnit(Occ) : nullptr;
}

void FHexBattleState::RebuildOccupancy()
{
	// 先全清
	for (const FHexUnit& U : Units)
	{
		Grid.ClearOccupancy(U.Id);
	}
	// 再按 id 升序重登记（确定性：重叠时后者覆盖，但正常情况下不该重叠）
	TArray<FIntVector> Cells;
	for (const FHexUnit& U : Units)
	{
		if (!U.bIsAlive)
		{
			continue; // 尸体不占格（§8.3）
		}
		U.GetCells(Cells);
		Grid.SetOccupancy(U.Id, Cells);
	}
}

// ───────────────────────────────────────────────────────── 规则聚合

void FHexBattleState::RebuildRuleAggregate()
{
	RuleAggregate.Reset();

	// ⚠️ 必须把符文、装备、英雄被动【一起】聚合。
	//    早期版本只聚合符文，导致装备的规则词条与英雄被动装上后毫无反应 ——
	//    这正是 R8 的典型症状：不报错、不崩溃，
	//    玩家只觉得"我的装备/被动没生效"。
	TSet<EHexGameRule> Touched;

	for (const FHexRuleOverride& O : HeroPassiveRules)
	{
		Touched.Add(O.Rule);
	}

	TArray<EHexGameRule> RuneRules;
	RuneLoadout.GetOverriddenRules(RuneRules);
	for (const EHexGameRule R : RuneRules)
	{
		Touched.Add(R);
	}

	TArray<EHexGameRule> EquipRules;
	EquipLoadout.GetOverriddenRules(EquipRules);
	for (const EHexGameRule R : EquipRules)
	{
		Touched.Add(R);
	}

	// 按枚举值升序处理 —— TSet 遍历顺序不稳定，直接用会让构建顺序漂移
	TArray<EHexGameRule> Ordered = Touched.Array();
	Ordered.Sort([](const EHexGameRule& A, const EHexGameRule& B)
	{
		return static_cast<uint8>(A) < static_cast<uint8>(B);
	});

	for (const EHexGameRule R : Ordered)
	{
		const FHexRuneLoadout::FAggregated FromRunes = RuneLoadout.AggregateRule(R);
		const FHexRuneLoadout::FAggregated FromEquip = EquipLoadout.AggregateRule(R);

		// 英雄被动 = 虚拟槽 0，最先生效（与 TriggerBus 的分层顺序一致）
		FHexRuneLoadout::FAggregated FromPassive;
		for (const FHexRuleOverride& O : HeroPassiveRules)
		{
			if (O.Rule != R)
			{
				continue;
			}
			FromPassive.bFound = true;
			if (O.bIsDelta)
			{
				FromPassive.IntDelta += O.IntValue;
				FromPassive.FloatDelta += O.FloatValue;
			}
			else
			{
				FromPassive.bHasOverride = true;
				FromPassive.IntOverride = O.IntValue;
				FromPassive.FloatOverride = O.FloatValue;
				FromPassive.bBoolOverride = O.bBoolValue;
			}
		}

		FHexRuneLoadout::FAggregated Merged;
		Merged.bFound = FromPassive.bFound || FromRunes.bFound || FromEquip.bFound;

		// 增量型：三个来源直接相加
		Merged.IntDelta = FromPassive.IntDelta + FromRunes.IntDelta + FromEquip.IntDelta;
		Merged.FloatDelta = FromPassive.FloatDelta + FromRunes.FloatDelta + FromEquip.FloatDelta;

		// 覆盖型：按结算顺序【后者胜出】——
		// 英雄被动(0) → 符文(1-6) → 装备(10-12)，与 TriggerBus 分层一致。
		// 这个顺序的含义：玩家的选择（符文/装备）可以推翻英雄的天生特性。
		if (FromEquip.bHasOverride)
		{
			Merged.bHasOverride = true;
			Merged.IntOverride = FromEquip.IntOverride;
			Merged.FloatOverride = FromEquip.FloatOverride;
			Merged.bBoolOverride = FromEquip.bBoolOverride;
		}
		else if (FromRunes.bHasOverride)
		{
			Merged.bHasOverride = true;
			Merged.IntOverride = FromRunes.IntOverride;
			Merged.FloatOverride = FromRunes.FloatOverride;
			Merged.bBoolOverride = FromRunes.bBoolOverride;
		}
		else if (FromPassive.bHasOverride)
		{
			Merged.bHasOverride = true;
			Merged.IntOverride = FromPassive.IntOverride;
			Merged.FloatOverride = FromPassive.FloatOverride;
			Merged.bBoolOverride = FromPassive.bBoolOverride;
		}

		RuleAggregate.Add(R, Merged);
	}
}

// ───────────────────────────────────────────────────────── 事件日志

void FHexBattleState::LogEvent(const FHexBattleEvent& Event)
{
	EventLog.Add(Event);
}

void FHexBattleState::LogEvent(FName Type, int32 SourceId, int32 TargetId)
{
	FHexBattleEvent E;
	E.Type = Type;
	E.SourceUnitId = SourceId;
	E.TargetUnitId = TargetId;
	EventLog.Add(E);
}

void FHexBattleState::DrainEvents(TArray<FHexBattleEvent>& Out)
{
	Out = MoveTemp(EventLog);
	EventLog.Reset();
}

TArrayView<const FHexBattleEvent> FHexBattleState::GetPendingEventsFrom(int32 StartIndex) const
{
	// 越界一律返回空视图而不是断言：调用方是触发翻译器，
	// 极端情况下（事件被提前 Drain）它应该"什么都不触发"，
	// 而不是把整场战斗弄崩。
	if (StartIndex < 0 || StartIndex >= EventLog.Num())
	{
		return TArrayView<const FHexBattleEvent>();
	}
	return TArrayView<const FHexBattleEvent>(
		EventLog.GetData() + StartIndex, EventLog.Num() - StartIndex);
}

// ───────────────────────────────────────────────────────── 动作日志

void FHexBattleState::LogAction(const FHexGameAction& Action)
{
	ActionLog.Add(Action);
}

uint32 FHexBattleState::ActionLogHash() const
{
	uint32 H = 0;
	for (const FHexGameAction& A : ActionLog)
	{
		H = HashCombine(H, GetTypeHash(static_cast<uint8>(A.Type)));
		H = HashCombine(H, GetTypeHash(A.SourceUnitId));
		H = HashCombine(H, GetTypeHash(A.TargetUnitId));
		H = HashCombine(H, GetTypeHash(A.IntA));
		H = HashCombine(H, GetTypeHash(A.IntB));
		H = HashCombine(H, GetTypeHash(A.IntC));
		H = HashCombine(H, GetTypeHash(A.CoordA));
	}
	return H;
}

// ───────────────────────────────────────────────────────── 违规记录

void FHexBattleState::AddRuleViolation(FName Kind, const FString& Detail)
{
	FHexRuleViolation V;
	V.Kind = Kind;
	V.Detail = Detail;
	V.Round = RoundNumber;
	RuleViolations.Add(V);
}

// ───────────────────────────────────────────────────────── 胜负

bool FHexBattleState::IsPlayerVictorious() const
{
	for (const FHexUnit& U : Units)
	{
		if (U.bIsAlive && U.Team == EHexTeam::Enemy)
		{
			return false;
		}
	}
	return true;
}

bool FHexBattleState::IsPlayerDefeated() const
{
	for (const FHexUnit& U : Units)
	{
		if (U.bIsAlive && U.Team == EHexTeam::Player)
		{
			return false;
		}
	}
	return true;
}

const FHexCardInstance* FHexBattleState::FindFixedCard(int32 Uid) const
{
	// uid 为 0 是"无效"约定值，不能匹配任何卡
	if (Uid == 0)
	{
		return nullptr;
	}
	for (const FHexCardInstance& C : FixedCards)
	{
		if (C.Uid == Uid)
		{
			return &C;
		}
	}
	return nullptr;
}

// ───────────────────────────────────────────────────────── 快照

TSharedPtr<FHexBattleState> FHexBattleState::Snapshot() const
{
	TSharedPtr<FHexBattleState> Copy = MakeShared<FHexBattleState>(Rng.GetMasterSeed());
	Copy->RestoreFrom(*this);
	return Copy;
}

void FHexBattleState::RestoreFrom(const FHexBattleState& Other)
{
	Grid = Other.Grid;
	Piles = Other.Piles;
	FixedCards = Other.FixedCards;
	Rng = Other.Rng;
	Phase = Other.Phase;
	RoundNumber = Other.RoundNumber;
	Energy = Other.Energy;
	CardsPlayedThisRound = Other.CardsPlayedThisRound;
	HeroUnitId = Other.HeroUnitId;
	HeroEnergyMaxBase = Other.HeroEnergyMaxBase;
	HeroDrawBase = Other.HeroDrawBase;
	DeckCapacityBase = Other.DeckCapacityBase;
	RuneLoadout = Other.RuneLoadout;

	// ⚠️ 这两行是补漏，不是新功能。
	//    原先只复制 RuneLoadout 与 RuleAggregate，漏了装备与英雄被动。
	//    平时看不出问题（RuleAggregate 已是算好的缓存），
	//    但只要在快照之后有任何一处调用 RebuildRuleAggregate()，
	//    装备加成与镇妖者被动就会【凭空消失】——
	//    表现为"读档后角色变弱了"，且不报任何错。
	EquipLoadout = Other.EquipLoadout;
	HeroPassiveRules = Other.HeroPassiveRules;

	RuleAggregate = Other.RuleAggregate;
	FloorIndex = Other.FloorIndex;
	Corruption = Other.Corruption;
	Units = Other.Units;
	NextUnitId = Other.NextUnitId;
	EventLog = Other.EventLog;
	ActionLog = Other.ActionLog;
	RuleViolations = Other.RuleViolations;
}

// ───────────────────────────────────────────────────────── 序列化

void FHexBattleState::Serialize(FArchive& Ar)
{
	Grid.Serialize(Ar);
	Piles.Serialize(Ar);
	Rng.Serialize(Ar);

	// 固定卡：不进牌堆，但要存档 —— 否则读档后基础动作全没了
	int32 FixedCount = FixedCards.Num();
	Ar << FixedCount;
	if (Ar.IsLoading())
	{
		FixedCards.SetNum(FixedCount);
	}
	for (FHexCardInstance& C : FixedCards)
	{
		C.Serialize(Ar);
	}

	uint8 P = static_cast<uint8>(Phase);
	Ar << P;
	if (Ar.IsLoading())
	{
		Phase = static_cast<EHexBattlePhase>(P);
	}

	Ar << RoundNumber << Energy << CardsPlayedThisRound;
	Ar << HeroUnitId;
	Ar << HeroEnergyMaxBase << HeroDrawBase << DeckCapacityBase;
	Ar << FloorIndex << Corruption;
	Ar << NextUnitId;

	int32 UnitCount = Units.Num();
	Ar << UnitCount;
	if (Ar.IsLoading())
	{
		Units.SetNum(UnitCount);
	}
	for (FHexUnit& U : Units)
	{
		U.Serialize(Ar);
	}

	int32 ActionCount = ActionLog.Num();
	Ar << ActionCount;
	if (Ar.IsLoading())
	{
		ActionLog.SetNum(ActionCount);
	}
	for (FHexGameAction& A : ActionLog)
	{
		A.Serialize(Ar);
	}
}

uint32 FHexBattleState::ContentHash() const
{
	uint32 H = Grid.ContentHash();
	H = HashCombine(H, Piles.ContentHash());
	H = HashCombine(H, GetTypeHash(RoundNumber));
	H = HashCombine(H, GetTypeHash(Energy));
	H = HashCombine(H, GetTypeHash(static_cast<uint8>(Phase)));
	for (const FHexUnit& U : Units)
	{
		H = HashCombine(H, U.ContentHash());
	}
	return H;
}
