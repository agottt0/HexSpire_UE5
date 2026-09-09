// Copyright Hex Spire. All Rights Reserved.

#include "Battle/HexUnit.h"
#include "Hex/HexCoord.h"
#include "Hex/HexFootprint.h"
#include "Core/HexSpireConstants.h"

// ───────────────────────────────────────────────────────── FHexIntent

void FHexIntent::Serialize(FArchive& Ar)
{
	uint8 K = static_cast<uint8>(Kind);
	uint8 T = static_cast<uint8>(Targeting);
	Ar << K << T;
	if (Ar.IsLoading())
	{
		Kind = static_cast<EHexIntentKind>(K);
		Targeting = static_cast<EHexIntentTargeting>(T);
	}
	Ar << TargetCells;
	Ar << TrackedUnitId;
	Ar << PredictedDamage;
	Ar << HitCount;
	Ar << MoveToAnchor;
	Ar << MoveToFacing;
	Ar << ResultFacing;
	Ar << StatusId;
	Ar << StatusStacks;
}

// ───────────────────────────────────────────────────────── 几何

void FHexUnit::GetCells(TArray<FIntVector>& Out) const
{
	FHexFootprint::Cells(Anchor, GetFootprint(), Facing, Out);
}

const TArray<FIntVector>& FHexUnit::GetFootprint() const
{
	return FHexFootprint::GetSizeDef(SizeClass).Footprint;
}

int32 FHexUnit::GetCellCount() const
{
	return FHexFootprint::GetSizeDef(SizeClass).CellCount();
}

void FHexUnit::GetAdjacentCells(TArray<FIntVector>& Out) const
{
	FHexFootprint::AdjacentCells(Anchor, GetFootprint(), Facing, Out);
}

int32 FHexUnit::DistanceToCell(const FIntVector& C) const
{
	return FHexFootprint::DistanceFrom(Anchor, GetFootprint(), Facing, C);
}

int32 FHexUnit::DistanceToUnit(const FHexUnit& Other) const
{
	TArray<FIntVector> OtherCells;
	Other.GetCells(OtherCells);

	int32 Best = TNumericLimits<int32>::Max();
	for (const FIntVector& C : OtherCells)
	{
		Best = FMath::Min(Best, DistanceToCell(C));
	}
	return Best;
}

int32 FHexUnit::GetKnockbackResist() const
{
	if (KnockbackResistOverride >= 0)
	{
		return KnockbackResistOverride;
	}
	return FHexFootprint::GetSizeDef(SizeClass).DefaultKnockbackResist;
}

int32 FHexUnit::GetRotateCost() const
{
	return FHexFootprint::GetSizeDef(SizeClass).DefaultRotateCost;
}

bool FHexUnit::CanTrampleBelow() const
{
	return FHexFootprint::GetSizeDef(SizeClass).bCanTrampleBelow;
}

bool FHexUnit::CanCrushRubble() const
{
	return FHexFootprint::GetSizeDef(SizeClass).bCanCrushRubble;
}

int32 FHexUnit::GetLootScatterRadius() const
{
	return FHexFootprint::GetSizeDef(SizeClass).LootScatterRadius;
}

FIntVector FHexUnit::GetForwardDir() const
{
	return FHexCoord::FacingDir(Facing);
}

bool FHexUnit::IsAttackedFromRear(const FIntVector& AttackerCell) const
{
	TArray<FIntVector> RearDirs;
	FHexCoord::BackstabDirs(Facing, RearDirs);

	TArray<FIntVector> OwnCells;
	GetCells(OwnCells);

	// 多格单位：任意一格的后弧方向上有攻击者即算背击
	for (const FIntVector& D : RearDirs)
	{
		for (const FIntVector& Own : OwnCells)
		{
			if (Own + D == AttackerCell)
			{
				return true;
			}
		}
	}
	return false;
}

bool FHexUnit::IsHostileTo(const FHexUnit& Other) const
{
	if (Team == EHexTeam::Neutral || Other.Team == EHexTeam::Neutral)
	{
		return false;
	}
	return Team != Other.Team;
}

// ───────────────────────────────────────────────────────── 状态效果

int32 FHexUnit::GetStatusStacks(FName StatusId) const
{
	for (const FHexStatusInstance& S : Statuses)
	{
		if (S.Id == StatusId)
		{
			return S.Stacks;
		}
	}
	return 0;
}

bool FHexUnit::HasStatus(FName StatusId) const
{
	return GetStatusStacks(StatusId) > 0;
}

int32 FHexUnit::ApplyStatus(FName StatusId, int32 Stacks)
{
	if (Stacks <= 0 || !FHexStatusLibrary::Exists(StatusId))
	{
		return 0;
	}

	const FHexStatusDef& Def = FHexStatusLibrary::Get(StatusId);

	// 找现有实例
	for (FHexStatusInstance& S : Statuses)
	{
		if (S.Id != StatusId)
		{
			continue;
		}

		switch (Def.StackMode)
		{
		case EHexStackMode::StackIntensity:
			S.Stacks = FMath::Min(S.Stacks + Stacks, Def.MaxStack);
			break;

		case EHexStackMode::StackDuration:
			S.Duration = FMath::Min(S.Duration + Stacks, Def.MaxStack);
			S.Stacks = FMath::Max(1, S.Stacks);
			break;

		case EHexStackMode::RefreshDuration:
			// ⚠️ 眩晕/定身走这一支：只刷新时长，不叠层。
			//    见 HexStatusData.cpp 中【眩晕】的注释：
			//    单玩家单位的游戏里可叠层的眩晕等于单方面处刑。
			S.Stacks = FMath::Min(FMath::Max(S.Stacks, Stacks), Def.MaxStack);
			S.Duration = FMath::Max(S.Duration, Stacks);
			break;
		}

		if (Def.bIsAbsorbShield)
		{
			S.AbsorbLeft += Stacks;
		}
		return S.Stacks;
	}

	// 新建实例
	FHexStatusInstance New;
	New.Id = StatusId;
	New.Stacks = FMath::Min(Stacks, Def.MaxStack);
	New.Duration = New.Stacks;
	New.AbsorbLeft = Def.bIsAbsorbShield ? Stacks : 0;
	Statuses.Add(New);

	// ⚠️ 确定性（纪律 5）：状态数组必须保持确定顺序，
	//    否则 tick 伤害的结算顺序会随插入顺序漂移。
	//    按 FName 的字符串比较排序（而非 FName 的内部 index，那个不稳定）。
	Statuses.Sort([](const FHexStatusInstance& A, const FHexStatusInstance& B)
	{
		return A.Id.ToString() < B.Id.ToString();
	});

	return New.Stacks;
}

void FHexUnit::RemoveStatus(FName StatusId)
{
	Statuses.RemoveAll([StatusId](const FHexStatusInstance& S)
	{
		return S.Id == StatusId;
	});
}

void FHexUnit::RemoveAllDebuffs()
{
	Statuses.RemoveAll([](const FHexStatusInstance& S)
	{
		return FHexStatusLibrary::Get(S.Id).bIsDebuff;
	});
}

void FHexUnit::DecayStatuses(TArray<FName>& OutExpired)
{
	OutExpired.Reset();

	for (FHexStatusInstance& S : Statuses)
	{
		const FHexStatusDef& Def = FHexStatusLibrary::Get(S.Id);
		if (Def.DecayPerRound == 0)
		{
			continue; // 力量/敏锐/护盾不衰减
		}

		S.Stacks += Def.DecayPerRound;
		S.Duration += Def.DecayPerRound;

		if (S.Stacks <= 0)
		{
			OutExpired.Add(S.Id);
		}
	}

	Statuses.RemoveAll([](const FHexStatusInstance& S)
	{
		return S.Stacks <= 0;
	});
}

float FHexUnit::GetDamageDealtMultiplier() const
{
	// ⚠️ 不按层数线性叠加 —— 见 HexStatusData.cpp【虚弱】注释：
	//    按层叠加会让 4 层虚弱变成免伤，破坏"永不免伤"原则（§4.4 ⑤）。
	//    层数只决定持续回合数。
	float Sum = 0.0f;
	for (const FHexStatusInstance& S : Statuses)
	{
		if (S.Stacks > 0)
		{
			Sum += FHexStatusLibrary::Get(S.Id).DamageDealtMult;
		}
	}
	return Sum;
}

float FHexUnit::GetDamageTakenMultiplier() const
{
	float Sum = 0.0f;
	for (const FHexStatusInstance& S : Statuses)
	{
		if (S.Stacks > 0)
		{
			Sum += FHexStatusLibrary::Get(S.Id).DamageTakenMult;
		}
	}
	return Sum;
}

int32 FHexUnit::GetFlatAtkBonus() const
{
	int32 Sum = 0;
	for (const FHexStatusInstance& S : Statuses)
	{
		Sum += FHexStatusLibrary::Get(S.Id).FlatAtkPerStack * S.Stacks;
	}
	return Sum;
}

int32 FHexUnit::GetFlatBlockBonus() const
{
	int32 Sum = 0;
	for (const FHexStatusInstance& S : Statuses)
	{
		Sum += FHexStatusLibrary::Get(S.Id).FlatBlockPerStack * S.Stacks;
	}
	return Sum;
}

bool FHexUnit::ShouldSkipTurn() const
{
	for (const FHexStatusInstance& S : Statuses)
	{
		if (S.Stacks > 0 && FHexStatusLibrary::Get(S.Id).bSkipTurn)
		{
			return true;
		}
	}
	return false;
}

bool FHexUnit::IsMovementBlocked() const
{
	for (const FHexStatusInstance& S : Statuses)
	{
		if (S.Stacks > 0 && FHexStatusLibrary::Get(S.Id).bCannotMove)
		{
			return true;
		}
	}
	return false;
}

int32 FHexUnit::GetMovePenalty() const
{
	// 缓迟：每层 -1 移动力
	return GetStatusStacks(FHexStatusLibrary::Chill);
}

int32 FHexUnit::GetBarrierAmount() const
{
	int32 Sum = 0;
	for (const FHexStatusInstance& S : Statuses)
	{
		if (FHexStatusLibrary::Get(S.Id).bIsAbsorbShield)
		{
			Sum += S.AbsorbLeft;
		}
	}
	return Sum;
}

int32 FHexUnit::ConsumeBarrier(int32 Amount)
{
	int32 Remaining = Amount;
	int32 Consumed = 0;

	for (FHexStatusInstance& S : Statuses)
	{
		if (Remaining <= 0)
		{
			break;
		}
		if (!FHexStatusLibrary::Get(S.Id).bIsAbsorbShield)
		{
			continue;
		}
		const int32 Take = FMath::Min(S.AbsorbLeft, Remaining);
		S.AbsorbLeft -= Take;
		Remaining -= Take;
		Consumed += Take;

		// 护盾用尽即移除
		if (S.AbsorbLeft <= 0)
		{
			S.Stacks = 0;
		}
	}

	Statuses.RemoveAll([](const FHexStatusInstance& S)
	{
		return S.Stacks <= 0;
	});

	return Consumed;
}

// ───────────────────────────────────────────────────────── 格挡

int32 FHexUnit::GetBlockCap() const
{
	const int32 ByRatio = FMath::FloorToInt(HPMax * HexK::BlockCapRatio);
	return FMath::Max(ByRatio, HexK::BlockCapMin);
}

// ───────────────────────────────────────────────────────── 序列化

void FHexUnit::Serialize(FArchive& Ar)
{
	Ar << Id;
	Ar << DisplayName;

	uint8 T = static_cast<uint8>(Team);
	uint8 SC = static_cast<uint8>(SizeClass);
	uint8 AI = static_cast<uint8>(AIProfile);
	uint8 IT = static_cast<uint8>(IntentTargeting);
	Ar << T << SC << AI << IT;
	if (Ar.IsLoading())
	{
		Team = static_cast<EHexTeam>(T);
		SizeClass = static_cast<EHexSizeClass>(SC);
		AIProfile = static_cast<EHexAIProfile>(AI);
		IntentTargeting = static_cast<EHexIntentTargeting>(IT);
	}

	Ar << SourceId;
	Ar << Anchor;
	Ar << Facing;
	Ar << HP << HPMax << ATK << DEF << AGI << LUK << CRIT;
	Ar << Block;
	Ar << bIsAlive;
	Ar << bIsElite << bIsBoss << BossPhase;
	Ar << KnockbackResistOverride;

	Intent.Serialize(Ar);

	int32 StatusCount = Statuses.Num();
	Ar << StatusCount;
	if (Ar.IsLoading())
	{
		Statuses.SetNum(StatusCount);
	}
	for (FHexStatusInstance& S : Statuses)
	{
		S.Serialize(Ar);
	}
}

uint32 FHexUnit::ContentHash() const
{
	uint32 H = GetTypeHash(Id);
	H = HashCombine(H, GetTypeHash(Anchor));
	H = HashCombine(H, GetTypeHash(Facing));
	H = HashCombine(H, GetTypeHash(HP));
	H = HashCombine(H, GetTypeHash(Block));
	H = HashCombine(H, GetTypeHash(static_cast<uint8>(bIsAlive)));
	// 状态按已排序顺序参与哈希（确定性）
	for (const FHexStatusInstance& S : Statuses)
	{
		H = HashCombine(H, GetTypeHash(S.Id));
		H = HashCombine(H, GetTypeHash(S.Stacks));
	}
	return H;
}
