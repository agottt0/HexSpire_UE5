// Copyright Hex Spire. All Rights Reserved.

#include "Battle/HexEnemySkillData.h"

bool FHexEnemySkillData::HasDamageStep() const
{
	for (const FHexEffectStep& S : Effects)
	{
		if (S.Op == EHexEffectOp::DealDamage)
		{
			return true;
		}
	}
	return false;
}
