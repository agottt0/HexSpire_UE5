// Copyright Hex Spire. All Rights Reserved.

#include "Data/HexEnemyTableRow.h"

// ⚠️ 无脑直译，不做任何推导。见 HexHeroTableRow.cpp 顶部的同一条说明。

FHexEnemyData FHexEnemyTableRow::ToEnemyData(FName InId) const
{
	FHexEnemyData Out;

	Out.Id = InId;
	Out.DisplayName = DisplayName;
	Out.SizeClass = SizeClass;

	Out.BaseHP = BaseHP;
	Out.BaseATK = BaseATK;
	Out.BaseDEF = BaseDEF;
	Out.BaseAGI = BaseAGI;
	Out.BaseLUK = BaseLUK;
	Out.BaseCRIT = BaseCRIT;

	Out.AIProfile = AIProfile;
	Out.IntentTargeting = IntentTargeting;
	Out.MoveBudget = MoveBudget;
	Out.PreferredDistance = PreferredDistance;
	Out.AttackRangeOverride = AttackRangeOverride;

	Out.SkillIds = SkillIds;

	Out.bIsElite = bIsElite;
	Out.bIsBoss = bIsBoss;
	Out.KnockbackResistOverride = KnockbackResistOverride;

	Out.CodexText = CodexText;

	return Out;
}

void FHexEnemyTableRow::FromEnemyData(const FHexEnemyData& In)
{
	DisplayName = In.DisplayName;
	SizeClass = In.SizeClass;

	BaseHP = In.BaseHP;
	BaseATK = In.BaseATK;
	BaseDEF = In.BaseDEF;
	BaseAGI = In.BaseAGI;
	BaseLUK = In.BaseLUK;
	BaseCRIT = In.BaseCRIT;

	AIProfile = In.AIProfile;
	IntentTargeting = In.IntentTargeting;
	MoveBudget = In.MoveBudget;
	PreferredDistance = In.PreferredDistance;
	AttackRangeOverride = In.AttackRangeOverride;

	SkillIds = In.SkillIds;

	bIsElite = In.bIsElite;
	bIsBoss = In.bIsBoss;
	KnockbackResistOverride = In.KnockbackResistOverride;

	CodexText = In.CodexText;
}
