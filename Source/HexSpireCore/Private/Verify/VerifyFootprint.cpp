// Copyright Hex Spire. All Rights Reserved.
//
// 体型占位验证 —— 对应 Godot 版 tools/verify_footprint.gd（R9）
//
// 核心手法：【独立参考实现穷举比对】。
//   参考实现用完全不同的写法（先转 offset 坐标、逐格独立检查、不复用任何
//   CanPlace 的中间量），然后与 CanPlace 在全部
//   49 锚点 × 6 朝向 × 3 体型 × 6 地形 = 5292 组输入上比对。
//
// 为什么必须这样做：R9 说"多格单位的边界情况极多"。手写几个用例只能覆盖
// 想到的情况，而 bug 恰恰藏在没想到的地方。穷举 + 双实现是唯一可靠的办法。

#include "Verify/HexVerify.h"
#include "HexSpireCore.h"
#include "Hex/HexCoord.h"
#include "Hex/HexFootprint.h"
#include "Hex/HexGrid.h"
#include "Hex/HexPathfinder.h"
#include "Content/HexLayouts.h"
#include "Core/HexSpireConstants.h"

namespace
{
	/**
	 * 独立参考实现 —— 故意用与 CanPlace 不同的写法：
	 *   · 先全部转成 offset 坐标再判断边界（而非用 InBounds）
	 *   · 用 switch 逐个列举地形（而非调 IsWalkable）
	 *   · 先收集全部格再统一检查（而非边算边短路）
	 *
	 * 两个实现若在 5292 组输入上完全一致，可以合理相信没有边界 bug。
	 */
	bool ReferenceCanPlace(
		const FHexGrid& Grid,
		const FIntVector& Anchor,
		const TArray<FIntVector>& Footprint,
		int32 Facing,
		int32 IgnoreUnitId,
		bool bCanCrushRubble)
	{
		// 第一步：全部展开
		TArray<FIntVector> AllCells;
		for (const FIntVector& O : Footprint)
		{
			AllCells.Add(Anchor + FHexCoord::Rotate(O, Facing));
		}

		// 第二步：边界检查 —— 用 offset 坐标直接比较，不调 InBounds
		for (const FIntVector& C : AllCells)
		{
			const FIntPoint P = FHexCoord::CubeToOffset(C);
			if (P.X < 1) return false;
			if (P.X > Grid.GetCols()) return false;
			if (P.Y < 1) return false;
			if (P.Y > Grid.GetRows()) return false;
		}

		// 第三步：地形检查 —— 用 switch 逐个列举，不调 IsWalkable
		for (const FIntVector& C : AllCells)
		{
			const EHexTerrain T = Grid.TerrainAt(C);
			bool bBlocked = false;
			switch (T)
			{
			case EHexTerrain::Wall:   bBlocked = true;  break;
			case EHexTerrain::Pit:    bBlocked = true;  break;
			case EHexTerrain::Floor:  bBlocked = false; break;
			case EHexTerrain::Rubble: bBlocked = false; break;
			case EHexTerrain::ExitGate: bBlocked = false; break;
			default: bBlocked = true; break;
			}
			if (bBlocked)
			{
				return false;
			}
		}

		// 第四步：占据检查
		for (const FIntVector& C : AllCells)
		{
			const int32 Occ = Grid.OccupantAt(C);
			if (Occ == -1)
			{
				continue;
			}
			if (Occ == IgnoreUnitId)
			{
				continue;
			}
			return false;
		}

		return true;
	}
}

bool FHexVerifySuites::VerifyFootprint(FHexVerifyContext& Ctx)
{
	const EHexSizeClass Sizes[] = { EHexSizeClass::S, EHexSizeClass::M, EHexSizeClass::L };
	const TCHAR* SizeNames[] = { TEXT("S"), TEXT("M"), TEXT("L") };

	// ─────────────────────────────── 1. 体型定义自检
	Ctx.Section(TEXT("体型定义（§8.2.1）"));
	{
		Ctx.CheckEqual(TEXT("S 占 1 格"),
			FHexFootprint::GetSizeDef(EHexSizeClass::S).CellCount(), 1);
		Ctx.CheckEqual(TEXT("M 占 3 格"),
			FHexFootprint::GetSizeDef(EHexSizeClass::M).CellCount(), 3);
		Ctx.CheckEqual(TEXT("L 占 6 格"),
			FHexFootprint::GetSizeDef(EHexSizeClass::L).CellCount(), 6);

		// 位移抗性随体型递增（§8.2.2 机制点 4）
		const int32 KS = FHexFootprint::GetSizeDef(EHexSizeClass::S).DefaultKnockbackResist;
		const int32 KM = FHexFootprint::GetSizeDef(EHexSizeClass::M).DefaultKnockbackResist;
		const int32 KL = FHexFootprint::GetSizeDef(EHexSizeClass::L).DefaultKnockbackResist;
		Ctx.Check(TEXT("位移抗性 S < M < L"), KS < KM && KM < KL,
			FString::Printf(TEXT("S=%d M=%d L=%d"), KS, KM, KL));
		Ctx.Check(TEXT("L 完全免疫击退（抗性 ≥ 999）"), KL >= 999);

		// L 可碾压、可压碎碎石
		Ctx.Check(TEXT("M/L 可碾压比自己小的单位"),
			FHexFootprint::GetSizeDef(EHexSizeClass::M).bCanTrampleBelow &&
			FHexFootprint::GetSizeDef(EHexSizeClass::L).bCanTrampleBelow);
		Ctx.Check(TEXT("S 不可碾压"),
			!FHexFootprint::GetSizeDef(EHexSizeClass::S).bCanTrampleBelow);
		Ctx.Check(TEXT("只有 L 可压碎碎石"),
			FHexFootprint::GetSizeDef(EHexSizeClass::L).bCanCrushRubble &&
			!FHexFootprint::GetSizeDef(EHexSizeClass::M).bCanCrushRubble);

		// 掉落散布半径随体型递增（§8.2.2 机制点 8）
		Ctx.Check(TEXT("掉落散布半径随体型递增"),
			FHexFootprint::GetSizeDef(EHexSizeClass::S).LootScatterRadius <
			FHexFootprint::GetSizeDef(EHexSizeClass::M).LootScatterRadius &&
			FHexFootprint::GetSizeDef(EHexSizeClass::M).LootScatterRadius <
			FHexFootprint::GetSizeDef(EHexSizeClass::L).LootScatterRadius);
	}

	// ─────────────────────────────── 2. footprint 在六朝向下不自重叠
	Ctx.Section(TEXT("footprint 旋转不自重叠"));
	{
		bool bAllOk = true;
		FString Bad;
		for (int32 SI = 0; SI < 3; ++SI)
		{
			const FHexSizeClassDef& Def = FHexFootprint::GetSizeDef(Sizes[SI]);
			for (int32 F = 0; F < 6; ++F)
			{
				TArray<FIntVector> Cells;
				FHexFootprint::Cells(FIntVector::ZeroValue, Def.Footprint, F, Cells);
				TSet<FIntVector> Unique(Cells);
				if (Unique.Num() != Cells.Num())
				{
					bAllOk = false;
					Bad = FString::Printf(TEXT("体型%s 朝向%d 出现重叠格"), SizeNames[SI], F);
					break;
				}
				if (Cells.Num() != Def.CellCount())
				{
					bAllOk = false;
					Bad = FString::Printf(TEXT("体型%s 朝向%d 格数不符"), SizeNames[SI], F);
					break;
				}
			}
		}
		Ctx.Check(TEXT("三体型 × 六朝向均不自重叠且格数守恒"), bAllOk, Bad);
	}

	// ─────────────────────────────── 3. ⭐ 穷举比对（R9 的核心）
	Ctx.Section(TEXT("CanPlace vs 独立参考实现（穷举比对）"));
	{
		int32 TotalCases = 0;
		int32 Mismatches = 0;
		FString FirstMismatch;

		for (const FName& LayoutId : FHexLayouts::AllLayoutIds())
		{
			FHexGrid Grid;
			FHexLayouts::Build(LayoutId, Grid);

			// 放一个假单位（id=7）在 (1,1)，用于测试 occupant 分支
			{
				TArray<FIntVector> Occ;
				Occ.Add(FHexCoord::OffsetToCube(1, 1));
				Grid.SetOccupancy(7, Occ);
			}

			for (int32 SI = 0; SI < 3; ++SI)
			{
				const FHexSizeClassDef& Def = FHexFootprint::GetSizeDef(Sizes[SI]);

				for (int32 Row = 1; Row <= HexK::BoardRows; ++Row)
				{
					for (int32 Col = 1; Col <= HexK::BoardCols; ++Col)
					{
						const FIntVector Anchor = FHexCoord::OffsetToCube(Col, Row);

						for (int32 F = 0; F < 6; ++F)
						{
							// 两组 IgnoreUnitId：-1（谁都不忽略）与 7（忽略假单位）
							const int32 IgnoreIds[] = { -1, 7 };
							for (int32 II = 0; II < 2; ++II)
							{
								++TotalCases;

								const bool bActual = FHexFootprint::CanPlace(
									Grid, Anchor, Def.Footprint, F,
									IgnoreIds[II], Def.bCanCrushRubble);

								const bool bExpected = ReferenceCanPlace(
									Grid, Anchor, Def.Footprint, F,
									IgnoreIds[II], Def.bCanCrushRubble);

								if (bActual != bExpected)
								{
									++Mismatches;
									if (FirstMismatch.IsEmpty())
									{
										FirstMismatch = FString::Printf(
											TEXT("地形=%s 体型=%s 锚点=(%d,%d) 朝向=%d 忽略=%d :: CanPlace=%d 参考=%d"),
											*LayoutId.ToString(), SizeNames[SI], Col, Row, F,
											IgnoreIds[II], bActual ? 1 : 0, bExpected ? 1 : 0);
									}
								}
							}
						}
					}
				}
			}
		}

		Ctx.Check(FString::Printf(TEXT("穷举 %d 组：CanPlace 与参考实现完全一致"), TotalCases),
			Mismatches == 0,
			FString::Printf(TEXT("不一致 %d 组，首例: %s"), Mismatches, *FirstMismatch));

		// 用例数必须达到预期规模，防止循环写错导致"零用例通过"
		const int32 Expected = FHexLayouts::AllLayoutIds().Num() * 3 * 49 * 6 * 2;
		Ctx.CheckEqual(TEXT("穷举用例数符合预期规模"), TotalCases, Expected);
	}

	// ─────────────────────────────── 4. IgnoreUnitId 语义：原地转向不能因自己失败
	Ctx.Section(TEXT("IgnoreUnitId：原地转向"));
	{
		FHexGrid Grid;
		FHexLayouts::BuildOpenHall(Grid);

		const FIntVector Anchor = FHexCoord::OffsetToCube(4, 4);
		const FHexSizeClassDef& MDef = FHexFootprint::GetSizeDef(EHexSizeClass::M);

		// 先让单位 3 占住 facing=0 的位置
		TArray<FIntVector> Own;
		FHexFootprint::Cells(Anchor, MDef.Footprint, 0, Own);
		Grid.SetOccupancy(3, Own);

		// 忽略自己 → 应该能转向
		bool bAnyRotateOk = false;
		for (int32 F = 0; F < 6; ++F)
		{
			if (FHexFootprint::CanPlace(Grid, Anchor, MDef.Footprint, F, 3, false))
			{
				bAnyRotateOk = true;
				break;
			}
		}
		Ctx.Check(TEXT("忽略自身 id 时 M 单位可原地转向"), bAnyRotateOk);

		// 不忽略自己 → 原朝向必然失败（因为自己占着）
		Ctx.Check(TEXT("不忽略自身 id 时原朝向被自己挡住"),
			!FHexFootprint::CanPlace(Grid, Anchor, MDef.Footprint, 0, -1, false));
	}

	// ─────────────────────────────── 5. 相邻格：大体型的相邻格更多（§8.2.2 机制点 3）
	Ctx.Section(TEXT("相邻格数量随体型递增"));
	{
		const FIntVector Center = FHexCoord::OffsetToCube(4, 4);
		int32 Counts[3] = { 0, 0, 0 };
		for (int32 SI = 0; SI < 3; ++SI)
		{
			TArray<FIntVector> Adj;
			FHexFootprint::AdjacentCells(Center,
				FHexFootprint::GetSizeDef(Sizes[SI]).Footprint, 0, Adj);
			Counts[SI] = Adj.Num();
		}
		Ctx.CheckEqual(TEXT("S 的相邻格恰为 6"), Counts[0], 6);
		Ctx.Check(TEXT("相邻格数 S < M < L（大体型 AOE 天然更大）"),
			Counts[0] < Counts[1] && Counts[1] < Counts[2],
			FString::Printf(TEXT("S=%d M=%d L=%d"), Counts[0], Counts[1], Counts[2]));

		// 相邻格不含自身占格
		bool bNoSelfOverlap = true;
		for (int32 SI = 0; SI < 3; ++SI)
		{
			TArray<FIntVector> Own, Adj;
			FHexFootprint::Cells(Center, FHexFootprint::GetSizeDef(Sizes[SI]).Footprint, 0, Own);
			FHexFootprint::AdjacentCells(Center, FHexFootprint::GetSizeDef(Sizes[SI]).Footprint, 0, Adj);
			for (const FIntVector& A : Adj)
			{
				if (Own.Contains(A))
				{
					bNoSelfOverlap = false;
					break;
				}
			}
		}
		Ctx.Check(TEXT("相邻格集合不含自身占格"), bNoSelfOverlap);

		// 相邻格无重复
		bool bNoDup = true;
		for (int32 SI = 0; SI < 3; ++SI)
		{
			TArray<FIntVector> Adj;
			FHexFootprint::AdjacentCells(Center, FHexFootprint::GetSizeDef(Sizes[SI]).Footprint, 0, Adj);
			TSet<FIntVector> Uniq(Adj);
			if (Uniq.Num() != Adj.Num())
			{
				bNoDup = false;
				break;
			}
		}
		Ctx.Check(TEXT("相邻格集合无重复"), bNoDup);
	}

	// ─────────────────────────────── 6. 有效射程：大体型射程更长（§8.2.2 机制点 3）
	Ctx.Section(TEXT("DistanceFrom：射程从最近占格起算"));
	{
		const FIntVector Anchor = FHexCoord::OffsetToCube(4, 2);
		const FIntVector Target = FHexCoord::OffsetToCube(4, 6);

		int32 Dists[3];
		for (int32 SI = 0; SI < 3; ++SI)
		{
			Dists[SI] = FHexFootprint::DistanceFrom(Anchor,
				FHexFootprint::GetSizeDef(Sizes[SI]).Footprint, 2, Target);
		}
		Ctx.Check(TEXT("同锚点下 L 到目标的距离 ≤ M ≤ S（有效射程更长）"),
			Dists[2] <= Dists[1] && Dists[1] <= Dists[0],
			FString::Printf(TEXT("S=%d M=%d L=%d"), Dists[0], Dists[1], Dists[2]));

		// S 的 DistanceFrom 必须等于纯坐标距离
		Ctx.CheckEqual(TEXT("S 的 DistanceFrom 等于坐标距离"),
			Dists[0], FHexCoord::Distance(Anchor, Target));
	}

	// ─────────────────────────────── 7. 边界描边（§13.2 硬性要求）
	Ctx.Section(TEXT("BoundaryEdges 描边"));
	{
		const FIntVector Center = FHexCoord::OffsetToCube(4, 4);
		// S：单格 → 6 条边界边
		{
			TArray<TPair<FIntVector, int32>> Edges;
			FHexFootprint::BoundaryEdges(Center,
				FHexFootprint::GetSizeDef(EHexSizeClass::S).Footprint, 0, Edges);
			Ctx.CheckEqual(TEXT("S 体型边界边数 = 6"), Edges.Num(), 6);
		}
		// M：3 格三角形 → 3*6 - 2*(内部相邻对数) 
		// 三角形 3 格两两相邻 = 3 对内部边，每对消掉 2 条 → 18 - 6 = 12
		{
			TArray<TPair<FIntVector, int32>> Edges;
			FHexFootprint::BoundaryEdges(Center,
				FHexFootprint::GetSizeDef(EHexSizeClass::M).Footprint, 0, Edges);
			Ctx.CheckEqual(TEXT("M 体型边界边数 = 12"), Edges.Num(), 12);
		}
		// L：6 格，边界边数应少于 6*6=36
		{
			TArray<TPair<FIntVector, int32>> Edges;
			FHexFootprint::BoundaryEdges(Center,
				FHexFootprint::GetSizeDef(EHexSizeClass::L).Footprint, 0, Edges);
			Ctx.Check(TEXT("L 体型边界边数在 (12, 36) 之间"),
				Edges.Num() > 12 && Edges.Num() < 36,
				FString::Printf(TEXT("实际=%d"), Edges.Num()));
		}
		// 描边输出必须确定（两次调用结果一致）
		{
			TArray<TPair<FIntVector, int32>> E1, E2;
			FHexFootprint::BoundaryEdges(Center,
				FHexFootprint::GetSizeDef(EHexSizeClass::L).Footprint, 3, E1);
			FHexFootprint::BoundaryEdges(Center,
				FHexFootprint::GetSizeDef(EHexSizeClass::L).Footprint, 3, E2);
			bool bSame = E1.Num() == E2.Num();
			if (bSame)
			{
				for (int32 I = 0; I < E1.Num(); ++I)
				{
					if (E1[I].Key != E2[I].Key || E1[I].Value != E2[I].Value)
					{
						bSame = false;
						break;
					}
				}
			}
			Ctx.Check(TEXT("BoundaryEdges 输出顺序确定（可缓存比对）"), bSame);
		}
	}

	// ─────────────────────────────── 8. ⭐ 出生区对三体型均合法（§8.2.4 硬性约束）
	//
	// 策划案：RoomLayoutData 必须保证出生区域始终可通行，否则 M/L 英雄无法出生。
	// "这是地形模板设计的硬性规则，必须加进模板校验器"。
	Ctx.Section(TEXT("全部地形模板的出生区对 S/M/L 均合法"));
	{
		const FIntVector SpawnAnchor = FHexCoord::OffsetToCube(
			HexK::HeroSpawnCol, HexK::HeroSpawnRow);
		const int32 SpawnFacing = HexK::HeroSpawnFacing;

		for (const FName& LayoutId : FHexLayouts::AllLayoutIds())
		{
			FHexGrid Grid;
			FHexLayouts::Build(LayoutId, Grid);

			for (int32 SI = 0; SI < 3; ++SI)
			{
				const FHexSizeClassDef& Def = FHexFootprint::GetSizeDef(Sizes[SI]);
				const bool bOk = FHexFootprint::CanPlace(
					Grid, SpawnAnchor, Def.Footprint, SpawnFacing, -1, Def.bCanCrushRubble);

				Ctx.Check(
					FString::Printf(TEXT("模板 %s 可容纳 %s 体型出生"),
						*LayoutId.ToString(), SizeNames[SI]),
					bOk,
					FHexFootprint::PlaceFailureReason(
						Grid, SpawnAnchor, Def.Footprint, SpawnFacing, -1, Def.bCanCrushRubble));
			}
		}
	}

	// ─────────────────────────────── 9. 出生区不得有危害（避免开局就掉血）
	Ctx.Section(TEXT("出生区无危害地面"));
	{
		const FIntVector SpawnAnchor = FHexCoord::OffsetToCube(
			HexK::HeroSpawnCol, HexK::HeroSpawnRow);

		for (const FName& LayoutId : FHexLayouts::AllLayoutIds())
		{
			FHexGrid Grid;
			FHexLayouts::Build(LayoutId, Grid);

			// 检查最大体型 L 的全部出生占格
			TArray<FIntVector> Cells;
			FHexFootprint::Cells(SpawnAnchor,
				FHexFootprint::GetSizeDef(EHexSizeClass::L).Footprint,
				HexK::HeroSpawnFacing, Cells);

			bool bClean = true;
			FString BadCell;
			for (const FIntVector& C : Cells)
			{
				if (Grid.HazardAt(C) != EHexHazard::None)
				{
					bClean = false;
					BadCell = FHexCoord::ToOffsetString(C);
					break;
				}
			}
			Ctx.Check(
				FString::Printf(TEXT("模板 %s 的 L 出生占格无危害"), *LayoutId.ToString()),
				bClean,
				FString::Printf(TEXT("危害格: %s"), *BadCell));
		}
	}

	return Ctx.NumFailed() == 0;
}
