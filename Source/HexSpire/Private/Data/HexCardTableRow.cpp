// Copyright Hex Spire. All Rights Reserved.

#include "Data/HexCardTableRow.h"

// ═══════════════════════════════════════════════════ FHexEffectStepRow

FHexEffectStep FHexEffectStepRow::ToEffectStep() const
{
	// 无脑直译，刻意不做任何推导。
	// 配表的人应当能从表格字段一眼预测运行时行为。
	FHexEffectStep S;
	S.Op = Op;
	S.FlatValue = FlatValue;
	S.StatRef = StatRef;
	S.StatRatio = StatRatio;
	S.Repeat = FMath::Max(1, Repeat);
	S.Distance = Distance;
	S.StatusId = StatusId;
	S.StatusStacks = StatusStacks;
	S.TargetFilter = TargetFilter;
	S.VfxId = VfxId;
	S.SfxId = SfxId;
	return S;
}

// ═══════════════════════════════════════════════════ FHexTargetSpecRow

FHexTargetSpec FHexTargetSpecRow::ToTargetSpec() const
{
	FHexTargetSpec T;
	T.Shape = Shape;
	T.RangeMin = RangeMin;
	T.RangeMax = RangeMax;
	T.AreaSize = AreaSize;
	T.bRequiresLineOfSight = bRequiresLineOfSight;
	T.bCanTargetEmptyCell = bCanTargetEmptyCell;
	return T;
}

// ═══════════════════════════════════════════════════ FHexCardTableRow

FHexCardData FHexCardTableRow::ToCardData(FName InId) const
{
	FHexCardData C;
	C.Id = InId;
	C.DisplayName = DisplayName;
	C.CardType = CardType;
	C.Rarity = Rarity;
	C.EnergyCost = EnergyCost;
	C.Tags = Tags;
	C.TargetSpec = TargetSpec.ToTargetSpec();

	C.Effects.Reserve(Effects.Num());
	for (const FHexEffectStepRow& R : Effects)
	{
		C.Effects.Add(R.ToEffectStep());
	}

	C.bIsCornerstone = bIsCornerstone;
	C.bIsExhaust = bIsExhaust;
	C.bCountsTowardCapacity = bCountsTowardCapacity;
	C.MaxCopiesInDeck = FMath::Max(1, MaxCopiesInDeck);
	C.DescriptionTemplate = DescriptionTemplate;

	// ⚠️ Visual 刻意【不】拷进 FHexCardData ——
	//    逻辑层没有那些字段，也不该有（纪律 3）。
	//    表现层需要美术配置时直接查 DataTable。
	return C;
}

void FHexCardTableRow::FromCardData(const FHexCardData& In)
{
	DisplayName = In.DisplayName;
	CardType = In.CardType;
	Rarity = In.Rarity;
	EnergyCost = In.EnergyCost;
	Tags = In.Tags;

	TargetSpec.Shape = In.TargetSpec.Shape;
	TargetSpec.RangeMin = In.TargetSpec.RangeMin;
	TargetSpec.RangeMax = In.TargetSpec.RangeMax;
	TargetSpec.AreaSize = In.TargetSpec.AreaSize;
	TargetSpec.bRequiresLineOfSight = In.TargetSpec.bRequiresLineOfSight;
	TargetSpec.bCanTargetEmptyCell = In.TargetSpec.bCanTargetEmptyCell;

	Effects.Reset(In.Effects.Num());
	for (const FHexEffectStep& S : In.Effects)
	{
		FHexEffectStepRow R;
		R.Op = S.Op;
		R.FlatValue = S.FlatValue;
		R.StatRef = S.StatRef;
		R.StatRatio = S.StatRatio;
		R.Repeat = S.Repeat;
		R.Distance = S.Distance;
		R.StatusId = S.StatusId;
		R.StatusStacks = S.StatusStacks;
		R.TargetFilter = S.TargetFilter;
		R.VfxId = S.VfxId;
		R.SfxId = S.SfxId;
		Effects.Add(R);
	}

	bIsCornerstone = In.bIsCornerstone;
	bIsExhaust = In.bIsExhaust;
	bCountsTowardCapacity = In.bCountsTowardCapacity;
	MaxCopiesInDeck = In.MaxCopiesInDeck;
	DescriptionTemplate = In.DescriptionTemplate;
}
