// Copyright Hex Spire. All Rights Reserved.

#include "Hex/HexFootprint.h"
#include "Hex/HexCoord.h"
#include "Hex/HexGrid.h"

namespace
{
	/**
	 * 三档体型定义表。构造一次，全局共享。
	 *
	 * footprint 数值取自策划案 §8.2.1 原文：
	 *   S: [(0,0,0)]
	 *   M: [(0,0,0), (1,-1,0), (1,0,-1)]                              边长2三角形
	 *   L: [(0,0,0), (1,-1,0), (1,0,-1), (2,-2,0), (2,-1,-1), (2,0,-2)] 边长3三角形
	 *
	 * ⚠️ 已用 §8.2.4 的出生表验证：在 (4,1) facing=2 时
	 *   M 占 (4,1)(3,2)(4,2)，L 占 (4,1)(3,2)(4,2)(3,3)(4,3)(5,3)，
	 *   全部落在 7×7 内。
	 */
	struct FSizeDefTable
	{
		FHexSizeClassDef Defs[3];

		FSizeDefTable()
		{
			// ── S：1 格
			{
				FHexSizeClassDef& D = Defs[0];
				D.SizeClass = EHexSizeClass::S;
				D.Footprint = { FIntVector(0, 0, 0) };
				D.DefaultKnockbackResist = 0;
				D.DefaultRotateCost = 0;
				D.bCanTrampleBelow = false;
				D.bCanCrushRubble = false;
				D.LootScatterRadius = 0;
			}
			// ── M：3 格（边长2三角形）
			{
				FHexSizeClassDef& D = Defs[1];
				D.SizeClass = EHexSizeClass::M;
				D.Footprint = {
					FIntVector(0, 0, 0),
					FIntVector(1, -1, 0),
					FIntVector(1, 0, -1),
				};
				D.DefaultKnockbackResist = 1;
				// 多格单位"转身慢"，绕后打它们更有价值（§8.2.2 机制点 5）
				D.DefaultRotateCost = 0;
				D.bCanTrampleBelow = true;
				D.bCanCrushRubble = false;
				D.LootScatterRadius = 1;
			}
			// ── L：6 格（边长3三角形）
			{
				FHexSizeClassDef& D = Defs[2];
				D.SizeClass = EHexSizeClass::L;
				D.Footprint = {
					FIntVector(0, 0, 0),
					FIntVector(1, -1, 0),
					FIntVector(1, 0, -1),
					FIntVector(2, -2, 0),
					FIntVector(2, -1, -1),
					FIntVector(2, 0, -2),
				};
				// 999 = 完全免疫击退（§8.2.2 机制点 4）
				D.DefaultKnockbackResist = 999;
				D.DefaultRotateCost = 1;
				D.bCanTrampleBelow = true;
				D.bCanCrushRubble = true;
				D.LootScatterRadius = 2;
			}
		}
	};

	const FSizeDefTable& GetTable()
	{
		static const FSizeDefTable Table;
		return Table;
	}
}

const FHexSizeClassDef& FHexFootprint::GetSizeDef(EHexSizeClass SizeClass)
{
	const int32 Idx = FMath::Clamp(static_cast<int32>(SizeClass), 0, 2);
	return GetTable().Defs[Idx];
}

void FHexFootprint::Cells(
	const FIntVector& Anchor,
	const TArray<FIntVector>& Footprint,
	int32 Facing,
	TArray<FIntVector>& Out)
{
	Out.Reset(Footprint.Num());
	for (const FIntVector& Off : Footprint)
	{
		Out.Add(Anchor + FHexCoord::Rotate(Off, Facing));
	}
}

void FHexFootprint::Cells(
	const FIntVector& Anchor,
	EHexSizeClass SizeClass,
	int32 Facing,
	TArray<FIntVector>& Out)
{
	Cells(Anchor, GetSizeDef(SizeClass).Footprint, Facing, Out);
}

bool FHexFootprint::CanPlace(
	const FHexGrid& Grid,
	const FIntVector& Anchor,
	const TArray<FIntVector>& Footprint,
	int32 Facing,
	int32 IgnoreUnitId,
	bool bCanCrushRubble)
{
	// ⚠️ 不做"S 体型走快路径"的优化 —— 见文件头 R9 说明。
	for (const FIntVector& Off : Footprint)
	{
		const FIntVector C = Anchor + FHexCoord::Rotate(Off, Facing);

		if (!Grid.InBounds(C))
		{
			return false;
		}
		if (!Grid.IsWalkable(C, bCanCrushRubble))
		{
			return false;
		}
		const int32 Occ = Grid.OccupantAt(C);
		if (Occ != -1 && Occ != IgnoreUnitId)
		{
			return false;
		}
	}
	return true;
}

FString FHexFootprint::PlaceFailureReason(
	const FHexGrid& Grid,
	const FIntVector& Anchor,
	const TArray<FIntVector>& Footprint,
	int32 Facing,
	int32 IgnoreUnitId,
	bool bCanCrushRubble)
{
	for (const FIntVector& Off : Footprint)
	{
		const FIntVector C = Anchor + FHexCoord::Rotate(Off, Facing);
		const FString Pos = FHexCoord::ToOffsetString(C);

		if (!Grid.InBounds(C))
		{
			return FString::Printf(TEXT("格 %s 超出战场"), *Pos);
		}
		if (!Grid.IsWalkable(C, bCanCrushRubble))
		{
			return FString::Printf(TEXT("格 %s 不可通行"), *Pos);
		}
		const int32 Occ = Grid.OccupantAt(C);
		if (Occ != -1 && Occ != IgnoreUnitId)
		{
			return FString::Printf(TEXT("格 %s 已被单位 #%d 占据"), *Pos, Occ);
		}
	}
	return FString();
}

void FHexFootprint::AdjacentCells(
	const FIntVector& Anchor,
	const TArray<FIntVector>& Footprint,
	int32 Facing,
	TArray<FIntVector>& Out)
{
	TArray<FIntVector> Own;
	Cells(Anchor, Footprint, Facing, Own);

	Out.Reset();
	for (const FIntVector& C : Own)
	{
		for (int32 I = 0; I < 6; ++I)
		{
			const FIntVector N = C + FHexCoord::Dirs[I];
			if (Own.Contains(N))
			{
				continue;
			}
			Out.AddUnique(N);
		}
	}
}

int32 FHexFootprint::DistanceFrom(
	const FIntVector& Anchor,
	const TArray<FIntVector>& Footprint,
	int32 Facing,
	const FIntVector& Target)
{
	int32 Best = TNumericLimits<int32>::Max();
	for (const FIntVector& Off : Footprint)
	{
		const FIntVector C = Anchor + FHexCoord::Rotate(Off, Facing);
		Best = FMath::Min(Best, FHexCoord::Distance(C, Target));
	}
	return Best;
}

void FHexFootprint::BoundaryEdges(
	const FIntVector& Anchor,
	const TArray<FIntVector>& Footprint,
	int32 Facing,
	TArray<TPair<FIntVector, int32>>& Out)
{
	TArray<FIntVector> Own;
	Cells(Anchor, Footprint, Facing, Own);

	// 确定性顺序：按 (z, x) 排序后逐格、逐方向
	Own.Sort([](const FIntVector& A, const FIntVector& B)
	{
		if (A.Z != B.Z)
		{
			return A.Z < B.Z;
		}
		return A.X < B.X;
	});

	Out.Reset();
	for (const FIntVector& C : Own)
	{
		for (int32 DI = 0; DI < 6; ++DI)
		{
			const FIntVector N = C + FHexCoord::Dirs[DI];
			if (!Own.Contains(N))
			{
				Out.Add(TPair<FIntVector, int32>(C, DI));
			}
		}
	}
}
