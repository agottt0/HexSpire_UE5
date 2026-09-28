// Copyright Hex Spire. All Rights Reserved.

#include "Data/HexRuneTableRow.h"

// ⚠️ 无脑直译，不做任何推导。见 HexHeroTableRow.cpp 顶部的同一条说明。

namespace
{
	// 条件枚举的双向显式映射。
	//
	// ⚠️ 刻意不用 static_cast 直转：两边枚举一旦有一边插了新值，
	//    直转会静默错位成别的条件 —— 症状是"符文在完全不相干的
	//    情况下触发"，比崩溃难查一个数量级。
	//    显式映射 + default 分支，漏加新值时至少落回 None 并可被测试抓到。
	FHexEffectCondition::EKind RuneCondToLogic(EHexRuneConditionKind K)
	{
		switch (K)
		{
		case EHexRuneConditionKind::TargetHPBelowPercent:
			return FHexEffectCondition::EKind::TargetHPBelowPercent;
		case EHexRuneConditionKind::SelfAtFullHP:
			return FHexEffectCondition::EKind::SelfAtFullHP;
		case EHexRuneConditionKind::SelfNotAtFullHP:
			return FHexEffectCondition::EKind::SelfNotAtFullHP;
		case EHexRuneConditionKind::DrawPileEmpty:
			return FHexEffectCondition::EKind::DrawPileEmpty;
		case EHexRuneConditionKind::TargetHasStatus:
			return FHexEffectCondition::EKind::TargetHasStatus;
		case EHexRuneConditionKind::CardsPlayedAtLeast:
			return FHexEffectCondition::EKind::CardsPlayedAtLeast;
		case EHexRuneConditionKind::None:
		default:
			return FHexEffectCondition::EKind::None;
		}
	}

	EHexRuneConditionKind RuneCondFromLogic(FHexEffectCondition::EKind K)
	{
		switch (K)
		{
		case FHexEffectCondition::EKind::TargetHPBelowPercent:
			return EHexRuneConditionKind::TargetHPBelowPercent;
		case FHexEffectCondition::EKind::SelfAtFullHP:
			return EHexRuneConditionKind::SelfAtFullHP;
		case FHexEffectCondition::EKind::SelfNotAtFullHP:
			return EHexRuneConditionKind::SelfNotAtFullHP;
		case FHexEffectCondition::EKind::DrawPileEmpty:
			return EHexRuneConditionKind::DrawPileEmpty;
		case FHexEffectCondition::EKind::TargetHasStatus:
			return EHexRuneConditionKind::TargetHasStatus;
		case FHexEffectCondition::EKind::CardsPlayedAtLeast:
			return EHexRuneConditionKind::CardsPlayedAtLeast;
		case FHexEffectCondition::EKind::None:
		default:
			return EHexRuneConditionKind::None;
		}
	}
}

FHexRuneTrigger FHexRuneTriggerRow::ToTrigger() const
{
	FHexRuneTrigger Out;

	Out.When = When;

	Out.bFilterByCardType = bFilterByCardType;
	Out.FilterCardType = FilterCardType;
	Out.FilterTag = FilterTag;
	Out.FilterCostMin = FilterCostMin;
	Out.FilterCostMax = FilterCostMax;

	Out.Condition.Kind = RuneCondToLogic(ConditionKind);
	Out.Condition.FloatParam = ConditionFloat;
	Out.Condition.IntParam = ConditionInt;
	Out.Condition.NameParam = ConditionName;

	Out.Effects.Reserve(Effects.Num());
	for (const FHexEffectStepRow& S : Effects)
	{
		Out.Effects.Add(S.ToEffectStep());
	}

	Out.MaxPerRound = MaxPerRound;
	Out.MaxPerBattle = MaxPerBattle;
	Out.CounterThreshold = CounterThreshold;

	Out.bHasValueAdd = bHasValueAdd;
	Out.ValueAddFlat = ValueAddFlat;
	Out.ValueAddStatRef = ValueAddStatRef;
	Out.ValueAddRatio = ValueAddRatio;
	Out.bHasValueMult = bHasValueMult;
	Out.ValueMult = ValueMult;

	return Out;
}

void FHexRuneTriggerRow::FromTrigger(const FHexRuneTrigger& In)
{
	When = In.When;

	bFilterByCardType = In.bFilterByCardType;
	FilterCardType = In.FilterCardType;
	FilterTag = In.FilterTag;
	FilterCostMin = In.FilterCostMin;
	FilterCostMax = In.FilterCostMax;

	ConditionKind = RuneCondFromLogic(In.Condition.Kind);
	ConditionFloat = In.Condition.FloatParam;
	ConditionInt = In.Condition.IntParam;
	ConditionName = In.Condition.NameParam;

	Effects.Reset(In.Effects.Num());
	for (const FHexEffectStep& S : In.Effects)
	{
		FHexEffectStepRow Row;
		Row.Op = S.Op;
		Row.FlatValue = S.FlatValue;
		Row.StatRef = S.StatRef;
		Row.StatRatio = S.StatRatio;
		Row.Repeat = S.Repeat;
		Row.Distance = S.Distance;
		Row.StatusId = S.StatusId;
		Row.StatusStacks = S.StatusStacks;
		Row.TargetFilter = S.TargetFilter;
		Row.VfxId = S.VfxId;
		Row.SfxId = S.SfxId;
		Effects.Add(Row);
	}

	MaxPerRound = In.MaxPerRound;
	MaxPerBattle = In.MaxPerBattle;
	CounterThreshold = In.CounterThreshold;

	bHasValueAdd = In.bHasValueAdd;
	ValueAddFlat = In.ValueAddFlat;
	ValueAddStatRef = In.ValueAddStatRef;
	ValueAddRatio = In.ValueAddRatio;
	bHasValueMult = In.bHasValueMult;
	ValueMult = In.ValueMult;
}

FHexRuneData FHexRuneTableRow::ToRuneData(FName InId) const
{
	FHexRuneData Out;

	Out.Id = InId;
	Out.DisplayName = DisplayName;
	Out.Rarity = Rarity;
	Out.Category = Category;
	Out.Tags = Tags;

	Out.Triggers.Reserve(Triggers.Num());
	for (const FHexRuneTriggerRow& T : Triggers)
	{
		Out.Triggers.Add(T.ToTrigger());
	}

	Out.RuleOverrides.Reserve(RuleOverrides.Num());
	for (const FHexRuleOverrideRow& R : RuleOverrides)
	{
		Out.RuleOverrides.Add(R.ToRuleOverride());
	}

	Out.InjectedCardIds = InjectedCardIds;
	Out.bIsCursed = bIsCursed;
	Out.FlavorText = FlavorText;
	Out.MechanicText = MechanicText;

	return Out;
}

void FHexRuneTableRow::FromRuneData(const FHexRuneData& In)
{
	DisplayName = In.DisplayName;
	Rarity = In.Rarity;
	Category = In.Category;
	Tags = In.Tags;

	Triggers.Reset(In.Triggers.Num());
	for (const FHexRuneTrigger& T : In.Triggers)
	{
		FHexRuneTriggerRow Row;
		Row.FromTrigger(T);
		Triggers.Add(Row);
	}

	RuleOverrides.Reset(In.RuleOverrides.Num());
	for (const FHexRuleOverride& R : In.RuleOverrides)
	{
		FHexRuleOverrideRow Row;
		Row.Rule = R.Rule;
		Row.IntValue = R.IntValue;
		Row.FloatValue = R.FloatValue;
		Row.bBoolValue = R.bBoolValue;
		Row.ApplyOrder = R.ApplyOrder;
		Row.bIsDelta = R.bIsDelta;
		RuleOverrides.Add(Row);
	}

	InjectedCardIds = In.InjectedCardIds;
	bIsCursed = In.bIsCursed;
	FlavorText = In.FlavorText;
	MechanicText = In.MechanicText;
}
