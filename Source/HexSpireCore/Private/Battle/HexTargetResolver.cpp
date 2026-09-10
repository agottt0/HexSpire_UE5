// Copyright Hex Spire. All Rights Reserved.

#include "Battle/HexTargetResolver.h"
#include "Battle/HexBattleState.h"
#include "Battle/HexUnit.h"
#include "Hex/HexCoord.h"
#include "Hex/HexFootprint.h"
#include "Hex/HexGrid.h"

namespace
{
	/** 队伍过滤 */
	bool TeamAllowed(const FHexTargetSpec& Spec, EHexTeam Team)
	{
		if (Spec.ValidTeams.Num() == 0)
		{
			return true;
		}
		return Spec.ValidTeams.Contains(Team);
	}

	bool SizeAllowed(const FHexTargetSpec& Spec, EHexSizeClass Size)
	{
		if (Spec.ValidSizeClasses.Num() == 0)
		{
			return true;
		}
		return Spec.ValidSizeClasses.Contains(Size);
	}

	/**
	 * 从施法者到某格的距离 —— 取最近的己方 footprint 格（§8.2.2 机制点 3）。
	 * 这是大体型有效射程更长的来源。
	 */
	int32 RangeFrom(const FHexUnit& Caster, const FIntVector& Cell)
	{
		return FHexFootprint::DistanceFrom(
			Caster.Anchor, Caster.GetFootprint(), Caster.Facing, Cell);
	}

	/**
	 * 视线判定：多格单位【任意一对 footprint 格之间有视线即算有视线】（§8.5）。
	 * 若按锚点单点判定，大体型会出现"我明明有半个身子能看到"却打不了的反直觉情况。
	 */
	bool HasLoS(const FHexGrid& Grid, const FHexUnit& Caster, const FIntVector& Cell)
	{
		TArray<FIntVector> OwnCells;
		Caster.GetCells(OwnCells);
		for (const FIntVector& From : OwnCells)
		{
			if (Grid.HasLineOfSight(From, Cell))
			{
				return true;
			}
		}
		return false;
	}

	/** 结果按 (row, col) 升序，保证确定性 */
	void SortCellsDeterministic(TArray<FIntVector>& Cells)
	{
		Cells.Sort([](const FIntVector& A, const FIntVector& B)
		{
			const FIntPoint OA = FHexCoord::CubeToOffset(A);
			const FIntPoint OB = FHexCoord::CubeToOffset(B);
			if (OA.Y != OB.Y)
			{
				return OA.Y < OB.Y;
			}
			return OA.X < OB.X;
		});
	}

	/**
	 * 冲撞路径是否被地形堵死。
	 *
	 * ⚠️ 只看地形，【故意不看单位】—— 冲撞的核心就是穿透。
	 *    若把单位算作阻挡，敌人往身前一站，冲撞就退化成短距离移动，
	 *    而它是镇妖者唯一的接敌手段，废掉它等于对风筝型敌人永久僵持。
	 *
	 * 起点与终点不参与判定：起点是自己，终点的可站立性由 CanPlace 单独查。
	 */
	bool DashPathBlocked(
		const FHexGrid& Grid, const FIntVector& From, const FIntVector& To,
		bool bCanCrushRubble)
	{
		TArray<FIntVector> Path;
		FHexCoord::Line(From, To, Path);

		for (const FIntVector& C : Path)
		{
			if (C == From || C == To)
			{
				continue;
			}
			// 墙、深坑：撞上去停下，不能穿过
			if (!Grid.IsWalkable(C, bCanCrushRubble))
			{
				return true;
			}
			// 石柱是齐胸的实体障碍，同样挡冲撞
			if (Grid.FeatureAt(C) == EHexFeature::Pillar)
			{
				return true;
			}
		}
		return false;
	}
}

// ───────────────────────────────────────────────────────── 合法目标格

void FHexTargetResolver::LegalCells(
	const FHexBattleState& State,
	const FHexUnit& Caster,
	const FHexTargetSpec& Spec,
	TArray<FIntVector>& Out)
{
	Out.Reset();

	// SELF：自身全部占格
	if (Spec.Shape == EHexTargetShape::SelfShape)
	{
		Caster.GetCells(Out);
		SortCellsDeterministic(Out);
		return;
	}

	// ADJACENT_ALL：相邻格全部（多格单位的相邻格更多 → 大体型 AOE 天然更大）
	if (Spec.Shape == EHexTargetShape::AdjacentAll)
	{
		TArray<FIntVector> Adj;
		Caster.GetAdjacentCells(Adj);
		for (const FIntVector& C : Adj)
		{
			if (State.Grid.InBounds(C))
			{
				Out.Add(C);
			}
		}
		SortCellsDeterministic(Out);
		return;
	}

	// RING：以自身为中心的环（不含中心）
	if (Spec.Shape == EHexTargetShape::Ring)
	{
		for (const FIntVector& C : State.Grid.AllCells())
		{
			const int32 D = RangeFrom(Caster, C);
			if (D == FMath::Max(1, Spec.AreaSize))
			{
				Out.Add(C);
			}
		}
		SortCellsDeterministic(Out);
		return;
	}

	// 其余形状：遍历全图，按射程 + 视线 + 内容筛选
	for (const FIntVector& C : State.Grid.AllCells())
	{
		const int32 D = RangeFrom(Caster, C);
		if (D < Spec.RangeMin || D > Spec.RangeMax)
		{
			continue;
		}

		// 不能选自己占的格（TILE / DASH 类除外，位移可能原地）
		if (D == 0
			&& Spec.Shape != EHexTargetShape::Tile
			&& Spec.Shape != EHexTargetShape::DashPath)
		{
			continue;
		}

		if (Spec.bRequiresLineOfSight && !HasLoS(State.Grid, Caster, C))
		{
			continue;
		}

		const FHexUnit* Occupant = State.FindUnitAtCell(C);

		if (Spec.bCanTargetEmptyCell)
		{
			// TILE / DASH：目标必须是可通行的空地（用于位移/召唤/改地形）
			if (Spec.Shape == EHexTargetShape::Tile
				|| Spec.Shape == EHexTargetShape::DashPath)
			{
				if (!State.Grid.IsWalkable(C, Caster.CanCrushRubble()))
				{
					continue;
				}
				// 移动类：落点必须能放下整个 footprint
				if (!FHexFootprint::CanPlace(
					State.Grid, C, Caster.GetFootprint(), Caster.Facing,
					Caster.Id, Caster.CanCrushRubble()))
				{
					continue;
				}
				// 冲撞额外要求：路径不能被墙/石柱堵死（但允许穿人）
				if (Spec.Shape == EHexTargetShape::DashPath
					&& DashPathBlocked(
						State.Grid, Caster.Anchor, C, Caster.CanCrushRubble()))
				{
					continue;
				}
			}
		}
		else
		{
			// 需要单位作为目标
			if (!Occupant || !Occupant->bIsAlive)
			{
				continue;
			}
			if (!TeamAllowed(Spec, Occupant->Team))
			{
				continue;
			}
			if (!SizeAllowed(Spec, Occupant->SizeClass))
			{
				continue;
			}
		}

		Out.Add(C);
	}

	SortCellsDeterministic(Out);
}

// ───────────────────────────────────────────────────────── 波及格

void FHexTargetResolver::AffectedCells(
	const FHexBattleState& State,
	const FHexUnit& Caster,
	const FHexTargetSpec& Spec,
	const FIntVector& TargetCell,
	TArray<FIntVector>& Out)
{
	Out.Reset();

	switch (Spec.Shape)
	{
	case EHexTargetShape::SelfShape:
		Caster.GetCells(Out);
		break;

	case EHexTargetShape::Single:
	case EHexTargetShape::Tile:
		Out.Add(TargetCell);
		break;

	case EHexTargetShape::DashPath:
	{
		// 整条冲撞路径 = 沿途 + 落点。
		//
		// ⚠️ 必须【排除起点】。起点是施法者自己占的格，
		//    留着它会让 AllInArea 过滤器把自己也算成受害者，
		//    出现"冲撞把自己打一顿"的荒唐结果。
		TArray<FIntVector> Path;
		FHexCoord::Line(Caster.Anchor, TargetCell, Path);

		TArray<FIntVector> OwnCells;
		Caster.GetCells(OwnCells);

		for (const FIntVector& C : Path)
		{
			if (OwnCells.Contains(C))
			{
				continue;
			}
			Out.Add(C);
		}
		break;
	}

	case EHexTargetShape::Line:
	{
		const int32 Dir = DirectionTo(Caster, TargetCell);
		// 从施法者最近的占格出发
		State.Grid.CellsInLine(Caster.Anchor, Dir,
			FMath::Max(1, Spec.AreaSize), Out);
		break;
	}

	case EHexTargetShape::Cone:
	{
		const int32 Dir = DirectionTo(Caster, TargetCell);
		State.Grid.CellsInCone(Caster.Anchor, Dir,
			FMath::Max(1, Spec.AreaSize), Out);
		break;
	}

	case EHexTargetShape::Burst:
		State.Grid.CellsInRange(TargetCell, FMath::Max(0, Spec.AreaSize), Out);
		break;

	case EHexTargetShape::Ring:
	{
		TArray<FIntVector> Own;
		Caster.GetCells(Own);
		for (const FIntVector& C : State.Grid.AllCells())
		{
			if (RangeFrom(Caster, C) == FMath::Max(1, Spec.AreaSize))
			{
				Out.Add(C);
			}
		}
		break;
	}

	case EHexTargetShape::AdjacentAll:
	{
		TArray<FIntVector> Adj;
		Caster.GetAdjacentCells(Adj);
		for (const FIntVector& C : Adj)
		{
			if (State.Grid.InBounds(C))
			{
				Out.Add(C);
			}
		}
		break;
	}

	default:
		Out.Add(TargetCell);
		break;
	}

	SortCellsDeterministic(Out);
}

// ───────────────────────────────────────────────────────── 波及单位

void FHexTargetResolver::AffectedUnits(
	const FHexBattleState& State,
	const FHexUnit& Caster,
	const FHexTargetSpec& Spec,
	const FIntVector& TargetCell,
	EHexTargetFilter Filter,
	TArray<int32>& Out)
{
	Out.Reset();

	TArray<FIntVector> Cells;
	AffectedCells(State, Caster, Spec, TargetCell, Cells);

	// ⚠️ 去重：多格单位占多格，但每次攻击只结算 1 次伤害（§8.2.2 机制点 2）。
	//    没有这一步，L 型 Boss 会被一发 AOE 打 6 次 —— 大体型从"弱点"变成"必死"。
	for (const FIntVector& C : Cells)
	{
		const FHexUnit* U = State.FindUnitAtCell(C);
		if (!U || !U->bIsAlive)
		{
			continue;
		}

		// 队伍过滤
		switch (Filter)
		{
		case EHexTargetFilter::Enemy:
			if (!Caster.IsHostileTo(*U))
			{
				continue;
			}
			break;
		case EHexTargetFilter::Ally:
			if (Caster.IsHostileTo(*U) || U->Id == Caster.Id)
			{
				continue;
			}
			break;
		case EHexTargetFilter::Self:
			if (U->Id != Caster.Id)
			{
				continue;
			}
			break;
		case EHexTargetFilter::AllInArea:
			break;
		default:
			break;
		}

		Out.AddUnique(U->Id);
	}

	// 按 id 升序（确定性）
	Out.Sort();
}

// ───────────────────────────────────────────────────────── 辅助

bool FHexTargetResolver::IsLegalTarget(
	const FHexBattleState& State,
	const FHexUnit& Caster,
	const FHexTargetSpec& Spec,
	const FIntVector& TargetCell)
{
	TArray<FIntVector> Legal;
	LegalCells(State, Caster, Spec, Legal);
	return Legal.Contains(TargetCell);
}

int32 FHexTargetResolver::DirectionTo(const FHexUnit& Caster, const FIntVector& TargetCell)
{
	// 取六个方向中"沿该方向走 D 格后最接近目标"的那个
	const int32 D = FMath::Max(1, FHexCoord::Distance(Caster.Anchor, TargetCell));

	int32 BestDir = 0;
	int32 BestDist = TNumericLimits<int32>::Max();
	for (int32 I = 0; I < 6; ++I)
	{
		const FIntVector Probe = Caster.Anchor + FHexCoord::Dirs[I] * D;
		const int32 Dist = FHexCoord::Distance(Probe, TargetCell);
		if (Dist < BestDist)
		{
			BestDist = Dist;
			BestDir = I;
		}
	}
	return BestDir;
}
