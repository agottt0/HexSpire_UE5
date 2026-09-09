// Copyright Hex Spire. All Rights Reserved.

#include "Battle/HexRuleBook.h"
#include "Battle/HexBattleState.h"
#include "Battle/HexUnit.h"
#include "Core/HexSpireConstants.h"

namespace
{
	/** 取聚合结果；无改写时返回 nullptr */
	const FHexRuneLoadout::FAggregated* Agg(const FHexBattleState& State, EHexGameRule Rule)
	{
		return State.RuleAggregate.Find(Rule);
	}

	/** 整数型规则：Fallback + Delta，或 Override */
	int32 AggInt(const FHexBattleState& State, EHexGameRule Rule, int32 Fallback)
	{
		const FHexRuneLoadout::FAggregated* A = Agg(State, Rule);
		if (!A || !A->bFound)
		{
			return Fallback;
		}
		if (A->bHasOverride)
		{
			return A->IntOverride;
		}
		return Fallback + A->IntDelta;
	}

	/** 浮点型规则（乘区）：Fallback × (1 + Delta)，或 Override */
	float AggFloatMult(const FHexBattleState& State, EHexGameRule Rule, float Fallback)
	{
		const FHexRuneLoadout::FAggregated* A = Agg(State, Rule);
		if (!A || !A->bFound)
		{
			return Fallback;
		}
		if (A->bHasOverride)
		{
			return A->FloatOverride;
		}
		// 乘区叠加：多个 +0.4 的符文应得 ×1.8 而非 ×1.4
		return Fallback + A->FloatDelta;
	}

	/** 布尔型规则：任一符文置真即为真 */
	bool AggBool(const FHexBattleState& State, EHexGameRule Rule, bool Fallback)
	{
		const FHexRuneLoadout::FAggregated* A = Agg(State, Rule);
		if (!A || !A->bFound)
		{
			return Fallback;
		}
		if (A->bHasOverride)
		{
			return A->bBoolOverride;
		}
		// Delta 型布尔：IntDelta > 0 视为真
		return A->IntDelta > 0 ? true : Fallback;
	}
}

// ───────────────────────────────────────────────────────── 体力与抽牌

int32 FHexRuleBook::EnergyMax(const FHexBattleState& State)
{
	return FMath::Max(0, AggInt(State, EHexGameRule::EnergyMax, State.HeroEnergyMaxBase));
}

int32 FHexRuleBook::CardsDrawnPerTurn(const FHexBattleState& State)
{
	return FMath::Max(0, AggInt(State, EHexGameRule::CardsDrawnPerTurn, State.HeroDrawBase));
}

int32 FHexRuleBook::HandLimit(const FHexBattleState& State)
{
	return FMath::Max(1, AggInt(State, EHexGameRule::HandLimit, HexK::HandLimit));
}

int32 FHexRuleBook::DeckCapacity(const FHexBattleState& State)
{
	return FMath::Clamp(
		AggInt(State, EHexGameRule::DeckCapacity, State.DeckCapacityBase),
		1, HexK::MaxDeckCapacity);
}

bool FHexRuleBook::IsFixedHand(const FHexBattleState& State)
{
	return AggBool(State, EHexGameRule::NoDrawFixedHand, false);
}

bool FHexRuleBook::IsFirstCardFree(const FHexBattleState& State)
{
	return AggBool(State, EHexGameRule::FirstCardFree, false);
}

// ───────────────────────────────────────────────────────── 卡牌费用

int32 FHexRuleBook::CardCost(const FHexBattleState& State, int32 BaseCost, int32 CardsPlayedThisRound)
{
	const int32 Played = (CardsPlayedThisRound < 0)
		? State.CardsPlayedThisRound
		: CardsPlayedThisRound;

	if (IsFirstCardFree(State) && Played == 0)
	{
		return 0;
	}

	const FHexRuneLoadout::FAggregated* A = Agg(State, EHexGameRule::CardCostDelta);
	const int32 Delta = (A && A->bFound) ? A->IntDelta : 0;

	return FMath::Max(0, BaseCost + Delta);
}

bool FHexRuleBook::AttacksExhaust(const FHexBattleState& State)
{
	return AggBool(State, EHexGameRule::ExhaustAllAttacks, false);
}

// ───────────────────────────────────────────────────────── 体型与位移

EHexSizeClass FHexRuleBook::SizeClassOf(const FHexBattleState& State, const FHexUnit& Unit)
{
	// 符文装在玩家身上，只对玩家方生效
	if (Unit.Team == EHexTeam::Player)
	{
		const FHexRuneLoadout::FAggregated* A = Agg(State, EHexGameRule::SizeClassOverride);
		if (A && A->bFound && A->bHasOverride)
		{
			return static_cast<EHexSizeClass>(FMath::Clamp(A->IntOverride, 0, 2));
		}
	}
	return Unit.SizeClass;
}

int32 FHexRuleBook::KnockbackResistOf(const FHexBattleState& State, const FHexUnit& Unit)
{
	if (Unit.Team == EHexTeam::Player)
	{
		if (AggBool(State, EHexGameRule::KnockbackImmune, false))
		{
			return 999;
		}
	}
	return Unit.GetKnockbackResist();
}

int32 FHexRuleBook::MoveCostDelta(const FHexBattleState& State)
{
	const FHexRuneLoadout::FAggregated* A = Agg(State, EHexGameRule::MoveCostDelta);
	return (A && A->bFound) ? A->IntDelta : 0;
}

// ───────────────────────────────────────────────────────── 格挡

bool FHexRuleBook::BlockPersists(const FHexBattleState& State)
{
	return AggBool(State, EHexGameRule::BlockPersists, false);
}

bool FHexRuleBook::CanGainBlock(const FHexBattleState& State)
{
	return !AggBool(State, EHexGameRule::NoBlockAllowed, false);
}

float FHexRuleBook::BlockMultiplier(const FHexBattleState& State)
{
	return FMath::Max(0.0f, AggFloatMult(State, EHexGameRule::BlockMultiplier, 1.0f));
}

// ───────────────────────────────────────────────────────── 伤害乘区

float FHexRuleBook::DamageMultiplier(const FHexBattleState& State)
{
	return FMath::Max(0.0f, AggFloatMult(State, EHexGameRule::DamageMultiplier, 1.0f));
}

float FHexRuleBook::CritDamageMultiplier(const FHexBattleState& State)
{
	return FMath::Max(0.0f, AggFloatMult(State, EHexGameRule::CritDamageMultiplier, 1.0f));
}

// ───────────────────────────────────────────────────────── 自检

const TMap<EHexGameRule, FString>& FHexRuleBook::Consumers()
{
	static const TMap<EHexGameRule, FString> Map = {
		{ EHexGameRule::EnergyMax,            TEXT("EnergyMax") },
		{ EHexGameRule::CardsDrawnPerTurn,    TEXT("CardsDrawnPerTurn") },
		{ EHexGameRule::HandLimit,            TEXT("HandLimit") },
		{ EHexGameRule::DeckCapacity,         TEXT("DeckCapacity") },
		{ EHexGameRule::CardCostDelta,        TEXT("CardCost") },
		{ EHexGameRule::FirstCardFree,        TEXT("IsFirstCardFree") },
		{ EHexGameRule::NoDrawFixedHand,      TEXT("IsFixedHand") },
		{ EHexGameRule::SizeClassOverride,    TEXT("SizeClassOf") },
		{ EHexGameRule::KnockbackImmune,      TEXT("KnockbackResistOf") },
		{ EHexGameRule::BlockPersists,        TEXT("BlockPersists") },
		{ EHexGameRule::DamageMultiplier,     TEXT("DamageMultiplier") },
		{ EHexGameRule::BlockMultiplier,      TEXT("BlockMultiplier") },
		{ EHexGameRule::CritDamageMultiplier, TEXT("CritDamageMultiplier") },
		{ EHexGameRule::NoBlockAllowed,       TEXT("CanGainBlock") },
		{ EHexGameRule::MoveCostDelta,        TEXT("MoveCostDelta") },
		{ EHexGameRule::ExhaustAllAttacks,    TEXT("AttacksExhaust") },
	};
	return Map;
}

bool FHexRuleBook::VerifyAllRulesHaveConsumer(TArray<EHexGameRule>& OutMissing)
{
	OutMissing.Reset();
	const TMap<EHexGameRule, FString>& Map = Consumers();

	const int32 RuleCount = static_cast<int32>(EHexGameRule::Count);
	for (int32 I = 0; I < RuleCount; ++I)
	{
		const EHexGameRule R = static_cast<EHexGameRule>(I);
		if (!Map.Contains(R))
		{
			OutMissing.Add(R);
		}
	}
	return OutMissing.Num() == 0;
}
