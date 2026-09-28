// Copyright Hex Spire. All Rights Reserved.

#include "Data/HexHeroTableRow.h"
#include "Runes/HexRuneData.h"

// ⚠️ 转换函数刻意保持【无脑直译】：字段一一对应，不做任何推导。
//    这里一旦出现"聪明"的逻辑（比如按体型自动调 HP），
//    配表结果就会与表面读到的值不一致 —— 策划填 80 却跑出 96，
//    而且查表怎么看都是对的。要做派生值请在逻辑层做，不要在转换层做。

FHexRuleOverride FHexRuleOverrideRow::ToRuleOverride() const
{
	FHexRuleOverride Out;
	Out.Rule = Rule;
	Out.IntValue = IntValue;
	Out.FloatValue = FloatValue;
	Out.bBoolValue = bBoolValue;
	Out.ApplyOrder = ApplyOrder;
	Out.bIsDelta = bIsDelta;
	return Out;
}

FHexHeroData FHexHeroTableRow::ToHeroData(FName InId) const
{
	FHexHeroData Out;

	Out.Id = InId;
	Out.DisplayName = DisplayName;
	Out.SizeClass = SizeClass;

	Out.BaseHP = BaseHP;
	Out.BaseATK = BaseATK;
	Out.BaseDEF = BaseDEF;
	Out.BaseAGI = BaseAGI;
	Out.BaseLUK = BaseLUK;
	Out.BaseCRIT = BaseCRIT;

	Out.EnergyMax = EnergyMax;
	Out.CardsDrawnPerTurn = CardsDrawnPerTurn;

	Out.CornerstoneCardIds = CornerstoneCardIds;
	Out.CardPoolTags = CardPoolTags;

	Out.PassiveRules.Reserve(PassiveRules.Num());
	for (const FHexRuleOverrideRow& R : PassiveRules)
	{
		Out.PassiveRules.Add(R.ToRuleOverride());
	}
	Out.PassiveText = PassiveText;

	return Out;
}

void FHexHeroTableRow::FromHeroData(const FHexHeroData& In)
{
	DisplayName = In.DisplayName;
	SizeClass = In.SizeClass;

	BaseHP = In.BaseHP;
	BaseATK = In.BaseATK;
	BaseDEF = In.BaseDEF;
	BaseAGI = In.BaseAGI;
	BaseLUK = In.BaseLUK;
	BaseCRIT = In.BaseCRIT;

	EnergyMax = In.EnergyMax;
	CardsDrawnPerTurn = In.CardsDrawnPerTurn;

	CornerstoneCardIds = In.CornerstoneCardIds;
	CardPoolTags = In.CardPoolTags;

	PassiveRules.Reset(In.PassiveRules.Num());
	for (const FHexRuleOverride& R : In.PassiveRules)
	{
		FHexRuleOverrideRow Row;
		Row.Rule = R.Rule;
		Row.IntValue = R.IntValue;
		Row.FloatValue = R.FloatValue;
		Row.bBoolValue = R.bBoolValue;
		Row.ApplyOrder = R.ApplyOrder;
		Row.bIsDelta = R.bIsDelta;
		PassiveRules.Add(Row);
	}
	PassiveText = In.PassiveText;
}
