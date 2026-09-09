// Copyright Hex Spire. All Rights Reserved.
//
// 7×7 六边形战场的格子容器 —— 策划案 §8.3
//
// 纯逻辑，零 Actor 依赖（纪律 3）。可序列化（纪律 2/5）。
// 格子属性：terrain / hazard / feature / occupant

#pragma once

#include "CoreMinimal.h"
#include "Core/HexSpireEnums.h"
#include "Core/HexSpireConstants.h"
#include "Hex/HexCoord.h"

/**
 * 单格的全部数据。
 * 用稠密数组存（49 格），而非稀疏 TMap ——
 * 数量固定且极小，数组的缓存局部性与确定性遍历都更好。
 */
struct HEXSPIRECORE_API FHexCell
{
	EHexTerrain Terrain = EHexTerrain::Floor;
	EHexHazard Hazard = EHexHazard::None;
	EHexFeature Feature = EHexFeature::None;

	/** 占据该格的单位 id，-1 表示空。尸体不占格。 */
	int32 OccupantId = -1;

	/** 危害剩余回合数，-1 表示永久（如固定尖刺） */
	int32 HazardTurnsLeft = -1;
};

/**
 * 战场网格。
 *
 * ⚠️ 确定性（纪律 5）：所有遍历都走 AllCells()，它按 (row, col) 升序返回。
 *    禁止依赖 TMap 的遍历顺序做逻辑判断。
 */
class HEXSPIRECORE_API FHexGrid
{
public:
	FHexGrid(int32 InCols = HexK::BoardCols, int32 InRows = HexK::BoardRows);

	int32 GetCols() const { return Cols; }
	int32 GetRows() const { return Rows; }

	/** 全部界内格，按 (row, col) 升序。确定性遍历的唯一入口。 */
	const TArray<FIntVector>& AllCells() const { return CellOrder; }

	bool InBounds(const FIntVector& C) const;

	// ───────────────────────────────────────────── 地形

	EHexTerrain TerrainAt(const FIntVector& C) const;
	void SetTerrain(const FIntVector& C, EHexTerrain T);

	/**
	 * 可通行判定（§8.3）：
	 *   WALL 永不可通行；PIT 不可通行（不做飞行单位）；
	 *   RUBBLE 可通行但移动成本 +1，L 体型可压碎。
	 */
	bool IsWalkable(const FIntVector& C, bool bCanCrushRubble = false) const;

	/** 移动成本。RUBBLE 为 2（L 体型压碎后为 1） */
	int32 MoveCostAt(const FIntVector& C, bool bCanCrushRubble = false) const;

	// ───────────────────────────────────────────── 危害

	EHexHazard HazardAt(const FIntVector& C) const;
	void SetHazard(const FIntVector& C, EHexHazard H, int32 TurnsLeft = -1);
	int32 HazardTurnsLeftAt(const FIntVector& C) const;
	void GetHazardCells(TArray<FIntVector>& Out) const;

	/** 危害每回合衰减；返回本回合过期的格 */
	void TickHazards(TArray<FIntVector>& OutExpired);

	// ───────────────────────────────────────────── 地物

	EHexFeature FeatureAt(const FIntVector& C) const;
	void SetFeature(const FIntVector& C, EHexFeature F);
	/** 找出第一个指定地物的格；找不到返回 false */
	bool FindFeature(EHexFeature F, FIntVector& Out) const;

	// ───────────────────────────────────────────── 占据

	/** 返回占据该格的单位 id，无则 -1 */
	int32 OccupantAt(const FIntVector& C) const;

	/** 登记单位占据的全部格。调用前应已通过 FHexFootprint::CanPlace。 */
	void SetOccupancy(int32 UnitId, const TArray<FIntVector>& Cells);

	/** 清除某单位的全部占格 */
	void ClearOccupancy(int32 UnitId);

	void GetCellsOfUnit(int32 UnitId, TArray<FIntVector>& Out) const;

	bool IsEmpty(const FIntVector& C) const;

	// ───────────────────────────────────────────── 范围与视线

	/** 以 Center 为中心、半径 R 的全部界内格（含中心） */
	void CellsInRange(const FIntVector& Center, int32 R, TArray<FIntVector>& Out) const;

	/** 环形：距离恰为 R */
	void CellsInRing(const FIntVector& Center, int32 R, TArray<FIntVector>& Out) const;

	/** 直线：从 Origin 朝 DirIndex 走 Length 格（遇 WALL 停止，含该墙格） */
	void CellsInLine(const FIntVector& Origin, int32 DirIndex, int32 Length, TArray<FIntVector>& Out) const;

	/** 扇形：朝 DirIndex 的 60° 锥，长度 Length */
	void CellsInCone(const FIntVector& Origin, int32 DirIndex, int32 Length, TArray<FIntVector>& Out) const;

	/** 视线判定：WALL 与 PILLAR 阻断。立方坐标线性插值取样。 */
	bool HasLineOfSight(const FIntVector& From, const FIntVector& To) const;

	// ───────────────────────────────────────────── 序列化（纪律 2/5）

	/** 紧凑序列化：只写非默认值，便于存档与哈希比对 */
	void Serialize(FArchive& Ar);

	/** 用于确定性验证的内容哈希 */
	uint32 ContentHash() const;

private:
	int32 Cols;
	int32 Rows;

	/** 稠密存储，索引 = (row-1) * Cols + (col-1) */
	TArray<FHexCell> Cells;

	/** 按 (row, col) 升序的立方坐标，与 Cells 一一对应 */
	TArray<FIntVector> CellOrder;

	FORCEINLINE int32 IndexOf(const FIntVector& C) const
	{
		const FIntPoint O = FHexCoord::CubeToOffset(C);
		if (O.X < 1 || O.X > Cols || O.Y < 1 || O.Y > Rows)
		{
			return INDEX_NONE;
		}
		return (O.Y - 1) * Cols + (O.X - 1);
	}
};
