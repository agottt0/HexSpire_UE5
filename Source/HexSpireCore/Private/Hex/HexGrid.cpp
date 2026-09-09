// Copyright Hex Spire. All Rights Reserved.

#include "Hex/HexGrid.h"

FHexGrid::FHexGrid(int32 InCols, int32 InRows)
	: Cols(InCols)
	, Rows(InRows)
{
	Cells.SetNum(Cols * Rows);
	CellOrder.Reserve(Cols * Rows);

	// ⚠️ 按 row 再 col 的固定顺序生成 —— 纪律 5：禁止依赖哈希容器遍历顺序，
	//    需要顺序时用显式排序的数组。
	for (int32 Row = 1; Row <= Rows; ++Row)
	{
		for (int32 Col = 1; Col <= Cols; ++Col)
		{
			CellOrder.Add(FHexCoord::OffsetToCube(Col, Row));
		}
	}
}

bool FHexGrid::InBounds(const FIntVector& C) const
{
	const FIntPoint O = FHexCoord::CubeToOffset(C);
	return O.X >= 1 && O.X <= Cols && O.Y >= 1 && O.Y <= Rows;
}

// ───────────────────────────────────────────────────────── 地形

EHexTerrain FHexGrid::TerrainAt(const FIntVector& C) const
{
	const int32 Idx = IndexOf(C);
	// 界外视为墙 —— 这样射线/范围计算天然在边界停下，无需到处写边界判断
	return Idx == INDEX_NONE ? EHexTerrain::Wall : Cells[Idx].Terrain;
}

void FHexGrid::SetTerrain(const FIntVector& C, EHexTerrain T)
{
	const int32 Idx = IndexOf(C);
	if (Idx != INDEX_NONE)
	{
		Cells[Idx].Terrain = T;
	}
}

bool FHexGrid::IsWalkable(const FIntVector& C, bool /*bCanCrushRubble*/) const
{
	const int32 Idx = IndexOf(C);
	if (Idx == INDEX_NONE)
	{
		return false;
	}
	switch (Cells[Idx].Terrain)
	{
	case EHexTerrain::Wall:
		return false;
	case EHexTerrain::Pit:
		return false;
	case EHexTerrain::Rubble:
		// 可进入，成本更高；bCanCrushRubble 影响的是成本不是可否
		return true;
	default:
		return true;
	}
}

int32 FHexGrid::MoveCostAt(const FIntVector& C, bool bCanCrushRubble) const
{
	if (TerrainAt(C) == EHexTerrain::Rubble && !bCanCrushRubble)
	{
		return 2;
	}
	return 1;
}

// ───────────────────────────────────────────────────────── 危害

EHexHazard FHexGrid::HazardAt(const FIntVector& C) const
{
	const int32 Idx = IndexOf(C);
	return Idx == INDEX_NONE ? EHexHazard::None : Cells[Idx].Hazard;
}

void FHexGrid::SetHazard(const FIntVector& C, EHexHazard H, int32 TurnsLeft)
{
	const int32 Idx = IndexOf(C);
	if (Idx != INDEX_NONE)
	{
		Cells[Idx].Hazard = H;
		Cells[Idx].HazardTurnsLeft = TurnsLeft;
	}
}

int32 FHexGrid::HazardTurnsLeftAt(const FIntVector& C) const
{
	const int32 Idx = IndexOf(C);
	return Idx == INDEX_NONE ? 0 : Cells[Idx].HazardTurnsLeft;
}

void FHexGrid::GetHazardCells(TArray<FIntVector>& Out) const
{
	Out.Reset();
	for (const FIntVector& C : CellOrder)
	{
		if (HazardAt(C) != EHexHazard::None)
		{
			Out.Add(C);
		}
	}
}

void FHexGrid::TickHazards(TArray<FIntVector>& OutExpired)
{
	OutExpired.Reset();
	for (const FIntVector& C : CellOrder)
	{
		const int32 Idx = IndexOf(C);
		if (Idx == INDEX_NONE)
		{
			continue;
		}
		FHexCell& Cell = Cells[Idx];
		if (Cell.Hazard == EHexHazard::None || Cell.HazardTurnsLeft < 0)
		{
			continue; // 无危害，或永久危害
		}
		--Cell.HazardTurnsLeft;
		if (Cell.HazardTurnsLeft <= 0)
		{
			Cell.Hazard = EHexHazard::None;
			Cell.HazardTurnsLeft = -1;
			OutExpired.Add(C);
		}
	}
}

// ───────────────────────────────────────────────────────── 地物

EHexFeature FHexGrid::FeatureAt(const FIntVector& C) const
{
	const int32 Idx = IndexOf(C);
	return Idx == INDEX_NONE ? EHexFeature::None : Cells[Idx].Feature;
}

void FHexGrid::SetFeature(const FIntVector& C, EHexFeature F)
{
	const int32 Idx = IndexOf(C);
	if (Idx != INDEX_NONE)
	{
		Cells[Idx].Feature = F;
	}
}

bool FHexGrid::FindFeature(EHexFeature F, FIntVector& Out) const
{
	for (const FIntVector& C : CellOrder)
	{
		if (FeatureAt(C) == F)
		{
			Out = C;
			return true;
		}
	}
	return false;
}

// ───────────────────────────────────────────────────────── 占据

int32 FHexGrid::OccupantAt(const FIntVector& C) const
{
	const int32 Idx = IndexOf(C);
	return Idx == INDEX_NONE ? -1 : Cells[Idx].OccupantId;
}

void FHexGrid::SetOccupancy(int32 UnitId, const TArray<FIntVector>& InCells)
{
	for (const FIntVector& C : InCells)
	{
		const int32 Idx = IndexOf(C);
		if (Idx != INDEX_NONE)
		{
			Cells[Idx].OccupantId = UnitId;
		}
	}
}

void FHexGrid::ClearOccupancy(int32 UnitId)
{
	for (FHexCell& Cell : Cells)
	{
		if (Cell.OccupantId == UnitId)
		{
			Cell.OccupantId = -1;
		}
	}
}

void FHexGrid::GetCellsOfUnit(int32 UnitId, TArray<FIntVector>& Out) const
{
	Out.Reset();
	for (const FIntVector& C : CellOrder)
	{
		if (OccupantAt(C) == UnitId)
		{
			Out.Add(C);
		}
	}
}

bool FHexGrid::IsEmpty(const FIntVector& C) const
{
	return InBounds(C) && IsWalkable(C) && OccupantAt(C) == -1;
}

// ───────────────────────────────────────────────────────── 范围与视线

void FHexGrid::CellsInRange(const FIntVector& Center, int32 R, TArray<FIntVector>& Out) const
{
	Out.Reset();
	for (const FIntVector& C : CellOrder)
	{
		if (FHexCoord::Distance(Center, C) <= R)
		{
			Out.Add(C);
		}
	}
}

void FHexGrid::CellsInRing(const FIntVector& Center, int32 R, TArray<FIntVector>& Out) const
{
	Out.Reset();
	for (const FIntVector& C : CellOrder)
	{
		if (FHexCoord::Distance(Center, C) == R)
		{
			Out.Add(C);
		}
	}
}

void FHexGrid::CellsInLine(const FIntVector& Origin, int32 DirIndex, int32 Length, TArray<FIntVector>& Out) const
{
	Out.Reset();
	const FIntVector D = FHexCoord::Dirs[((DirIndex % 6) + 6) % 6];
	FIntVector Cur = Origin;
	for (int32 I = 0; I < Length; ++I)
	{
		Cur = Cur + D;
		if (!InBounds(Cur))
		{
			break;
		}
		Out.Add(Cur);
		if (TerrainAt(Cur) == EHexTerrain::Wall)
		{
			break; // 墙格本身included（表现上"打在墙上"），但不再延伸
		}
	}
}

void FHexGrid::CellsInCone(const FIntVector& Origin, int32 DirIndex, int32 Length, TArray<FIntVector>& Out) const
{
	// 60° 锥：以主方向与其顺时针相邻方向张成的楔形区域。
	// 立方坐标下，方向 A 与方向 B 的非负整系数组合即为该楔形。
	Out.Reset();
	const int32 DA = ((DirIndex % 6) + 6) % 6;
	const int32 DB = ((DA + 1) % 6);
	const FIntVector VA = FHexCoord::Dirs[DA];
	const FIntVector VB = FHexCoord::Dirs[DB];

	// 按 (总距离, a) 升序生成，保证确定性
	for (int32 Total = 1; Total <= Length; ++Total)
	{
		for (int32 A = Total; A >= 0; --A)
		{
			const int32 B = Total - A;
			const FIntVector C = Origin + VA * A + VB * B;
			if (!InBounds(C))
			{
				continue;
			}
			Out.AddUnique(C);
		}
	}
}

bool FHexGrid::HasLineOfSight(const FIntVector& From, const FIntVector& To) const
{
	if (From == To)
	{
		return true;
	}
	const int32 N = FHexCoord::Distance(From, To);
	for (int32 I = 1; I < N; ++I)
	{
		const float T = static_cast<float>(I) / static_cast<float>(N);
		const FIntVector S = FHexCoord::CubeLerpRound(From, To, T);
		if (S == From || S == To)
		{
			continue;
		}
		if (TerrainAt(S) == EHexTerrain::Wall)
		{
			return false;
		}
		if (FeatureAt(S) == EHexFeature::Pillar)
		{
			return false;
		}
	}
	return true;
}

// ───────────────────────────────────────────────────────── 序列化

void FHexGrid::Serialize(FArchive& Ar)
{
	Ar << Cols;
	Ar << Rows;

	if (Ar.IsLoading())
	{
		Cells.SetNum(Cols * Rows);
		CellOrder.Reset(Cols * Rows);
		for (int32 Row = 1; Row <= Rows; ++Row)
		{
			for (int32 Col = 1; Col <= Cols; ++Col)
			{
				CellOrder.Add(FHexCoord::OffsetToCube(Col, Row));
			}
		}
	}

	for (FHexCell& Cell : Cells)
	{
		uint8 T = static_cast<uint8>(Cell.Terrain);
		uint8 H = static_cast<uint8>(Cell.Hazard);
		uint8 F = static_cast<uint8>(Cell.Feature);
		Ar << T << H << F << Cell.OccupantId << Cell.HazardTurnsLeft;
		if (Ar.IsLoading())
		{
			Cell.Terrain = static_cast<EHexTerrain>(T);
			Cell.Hazard = static_cast<EHexHazard>(H);
			Cell.Feature = static_cast<EHexFeature>(F);
		}
	}
}

uint32 FHexGrid::ContentHash() const
{
	uint32 Hash = HashCombine(GetTypeHash(Cols), GetTypeHash(Rows));
	for (const FHexCell& Cell : Cells)
	{
		Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Cell.Terrain)));
		Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Cell.Hazard)));
		Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Cell.Feature)));
		Hash = HashCombine(Hash, GetTypeHash(Cell.OccupantId));
	}
	return Hash;
}
