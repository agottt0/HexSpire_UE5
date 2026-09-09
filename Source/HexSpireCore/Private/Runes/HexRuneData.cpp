// Copyright Hex Spire. All Rights Reserved.

#include "Runes/HexRuneData.h"

FHexRuneLoadout::FHexRuneLoadout()
{
	for (int32 I = 0; I < SlotCount; ++I)
	{
		Slots[I] = nullptr;
	}
}

const FHexRuneData* FHexRuneLoadout::GetSlot(int32 SlotIndex) const
{
	if (SlotIndex < 0 || SlotIndex >= SlotCount)
	{
		return nullptr;
	}
	return Slots[SlotIndex];
}

void FHexRuneLoadout::SetSlot(int32 SlotIndex, const FHexRuneData* Rune)
{
	if (SlotIndex < 0 || SlotIndex >= SlotCount)
	{
		return;
	}
	Slots[SlotIndex] = Rune;
}

void FHexRuneLoadout::ClearSlot(int32 SlotIndex)
{
	SetSlot(SlotIndex, nullptr);
}

void FHexRuneLoadout::SwapSlots(int32 A, int32 B)
{
	if (A < 0 || A >= SlotCount || B < 0 || B >= SlotCount)
	{
		return;
	}
	const FHexRuneData* Tmp = Slots[A];
	Slots[A] = Slots[B];
	Slots[B] = Tmp;
}

int32 FHexRuneLoadout::FindFirstEmptySlot() const
{
	for (int32 I = 0; I < SlotCount; ++I)
	{
		if (Slots[I] == nullptr)
		{
			return I;
		}
	}
	return INDEX_NONE;
}

int32 FHexRuneLoadout::GetFilledCount() const
{
	int32 N = 0;
	for (int32 I = 0; I < SlotCount; ++I)
	{
		if (Slots[I] != nullptr)
		{
			++N;
		}
	}
	return N;
}

void FHexRuneLoadout::GetRunesInOrder(TArray<TPair<int32, const FHexRuneData*>>& Out) const
{
	Out.Reset();
	// ⚠️ 严格按槽位 1→6（下标 0→5）。这是 §6.5 结算顺序的唯一来源。
	for (int32 I = 0; I < SlotCount; ++I)
	{
		if (Slots[I] != nullptr)
		{
			Out.Add(TPair<int32, const FHexRuneData*>(I, Slots[I]));
		}
	}
}

FHexRuneLoadout::FAggregated FHexRuneLoadout::AggregateRule(EHexGameRule Rule) const
{
	FAggregated Result;

	// 收集所有针对该规则的改写，附带槽位号用于 tiebreak
	struct FEntry
	{
		const FHexRuleOverride* Override;
		int32 SlotIndex;
	};
	TArray<FEntry> Entries;

	for (int32 I = 0; I < SlotCount; ++I)
	{
		if (!Slots[I])
		{
			continue;
		}
		for (const FHexRuleOverride& Ov : Slots[I]->RuleOverrides)
		{
			if (Ov.Rule == Rule)
			{
				Entries.Add({ &Ov, I });
			}
		}
	}

	if (Entries.Num() == 0)
	{
		return Result;
	}

	// ⚠️ 确定性排序：ApplyOrder 升序，同序按槽位升序。
	//    没有 tiebreak 的排序会让同 ApplyOrder 的两个符文顺序不定 → 结果漂移。
	Entries.Sort([](const FEntry& A, const FEntry& B)
	{
		if (A.Override->ApplyOrder != B.Override->ApplyOrder)
		{
			return A.Override->ApplyOrder < B.Override->ApplyOrder;
		}
		return A.SlotIndex < B.SlotIndex;
	});

	Result.bFound = true;

	for (const FEntry& E : Entries)
	{
		const FHexRuleOverride& Ov = *E.Override;
		if (Ov.bIsDelta)
		{
			// 增量型：累加（两个 EnergyMax +1 应得 +2，而非后者吃掉前者）
			Result.IntDelta += Ov.IntValue;
			Result.FloatDelta += Ov.FloatValue;
		}
		else
		{
			// 覆盖型：按排序后的顺序，最后一个生效
			Result.bHasOverride = true;
			Result.IntOverride = Ov.IntValue;
			Result.FloatOverride = Ov.FloatValue;
			Result.bBoolOverride = Ov.bBoolValue;
		}
	}

	return Result;
}

void FHexRuneLoadout::GetOverriddenRules(TArray<EHexGameRule>& Out) const
{
	Out.Reset();
	for (int32 I = 0; I < SlotCount; ++I)
	{
		if (!Slots[I])
		{
			continue;
		}
		for (const FHexRuleOverride& Ov : Slots[I]->RuleOverrides)
		{
			Out.AddUnique(Ov.Rule);
		}
	}
	// 确定性顺序
	Out.Sort([](EHexGameRule A, EHexGameRule B)
	{
		return static_cast<uint8>(A) < static_cast<uint8>(B);
	});
}
