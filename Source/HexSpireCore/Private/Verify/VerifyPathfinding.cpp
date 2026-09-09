// Copyright Hex Spire. All Rights Reserved.
//
// 寻路验证 —— 对应 Godot 版 tools/verify_pathfinding.gd
//
// 最重要的一条是【三级门宽闸门】：它是 D8 体型系统真正产生玩法差异的证据。
// 如果三种体型在狭道上的通过性没有差别，那 D8 就只是美术需求（策划案 §8.2.2 原话）。

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
	/** 构造一个指定体型的寻路查询 */
	FHexPathQuery MakeQuery(
		EHexSizeClass Size,
		const FIntVector& Anchor,
		int32 Facing,
		int32 Budget)
	{
		const FHexSizeClassDef& Def = FHexFootprint::GetSizeDef(Size);
		FHexPathQuery Q;
		Q.StartAnchor = Anchor;
		Q.StartFacing = Facing;
		Q.Footprint = Def.Footprint;
		Q.Budget = Budget;
		Q.SelfUnitId = 1;
		Q.RotateCost = Def.DefaultRotateCost;
		Q.bCanCrushRubble = Def.bCanCrushRubble;
		Q.MoveCostDelta = 0;
		return Q;
	}

	/** 在网格里登记单位 1 的初始占位，模拟真实战场状态 */
	void OccupySelf(FHexGrid& Grid, EHexSizeClass Size, const FIntVector& Anchor, int32 Facing)
	{
		TArray<FIntVector> Cells;
		FHexFootprint::Cells(Anchor, FHexFootprint::GetSizeDef(Size).Footprint, Facing, Cells);
		Grid.SetOccupancy(1, Cells);
	}

	/** 建一个自定义门宽的横墙地形：row 4 开一个宽 GateWidth 的口，从 col 4 起向右 */
	void BuildGate(FHexGrid& G, int32 GateWidth)
	{
		G = FHexGrid(HexK::BoardCols, HexK::BoardRows);
		const int32 GateStart = 4;
		for (int32 Col = 1; Col <= HexK::BoardCols; ++Col)
		{
			if (Col >= GateStart && Col < GateStart + GateWidth)
			{
				continue;
			}
			G.SetTerrain(FHexCoord::OffsetToCube(Col, 4), EHexTerrain::Wall);
		}
	}
}

bool FHexVerifySuites::VerifyPathfinding(FHexVerifyContext& Ctx)
{
	const EHexSizeClass Sizes[] = { EHexSizeClass::S, EHexSizeClass::M, EHexSizeClass::L };
	const TCHAR* SizeNames[] = { TEXT("S"), TEXT("M"), TEXT("L") };

	// ─────────────────────────────── 1. 状态编码往返
	Ctx.Section(TEXT("状态编码 (anchor, facing) ↔ StateId"));
	{
		bool bAllOk = true;
		FString Bad;
		for (int32 Row = 1; Row <= HexK::BoardRows && bAllOk; ++Row)
		{
			for (int32 Col = 1; Col <= HexK::BoardCols && bAllOk; ++Col)
			{
				for (int32 F = 0; F < 6; ++F)
				{
					const FIntVector A = FHexCoord::OffsetToCube(Col, Row);
					const int32 Sid = FHexPathfinder::StateId(A, F);

					FIntVector BackA;
					int32 BackF;
					FHexPathfinder::DecodeState(Sid, BackA, BackF);

					if (BackA != A || BackF != F)
					{
						bAllOk = false;
						Bad = FString::Printf(TEXT("(%d,%d) f=%d → sid=%d → %s f=%d"),
							Col, Row, F, Sid, *FHexCoord::ToOffsetString(BackA), BackF);
						break;
					}
				}
			}
		}
		Ctx.Check(TEXT("294 个状态编码往返一致"), bAllOk, Bad);

		// StateId 必须唯一
		TSet<int32> Ids;
		for (int32 Row = 1; Row <= HexK::BoardRows; ++Row)
		{
			for (int32 Col = 1; Col <= HexK::BoardCols; ++Col)
			{
				for (int32 F = 0; F < 6; ++F)
				{
					Ids.Add(FHexPathfinder::StateId(FHexCoord::OffsetToCube(Col, Row), F));
				}
			}
		}
		Ctx.CheckEqual(TEXT("StateId 无碰撞（49×6=294）"), Ids.Num(), 294);
	}

	// ─────────────────────────────── 2. 空旷地图上 S 的可达范围 = 六边形圆
	Ctx.Section(TEXT("空旷地图可达范围"));
	{
		FHexGrid Grid;
		FHexLayouts::BuildOpenHall(Grid);

		const FIntVector Start = FHexCoord::OffsetToCube(4, 4);
		OccupySelf(Grid, EHexSizeClass::S, Start, 0);

		const int32 Budget = 2;
		FHexPathQuery Q = MakeQuery(EHexSizeClass::S, Start, 0, Budget);

		TMap<FIntVector, TPair<int32, int32>> Anchors;
		FHexPathfinder::ReachableAnchors(Grid, Q, Anchors);

		// 所有可达锚点的距离必须 ≤ Budget
		bool bAllWithin = true;
		for (const TPair<FIntVector, TPair<int32, int32>>& P : Anchors)
		{
			if (FHexCoord::Distance(Start, P.Key) > Budget)
			{
				bAllWithin = false;
				break;
			}
		}
		Ctx.Check(TEXT("可达锚点全部在预算距离内"), bAllWithin);

		// 距离 1 的六个邻格必须都可达（空旷地图）
		int32 NeighborsReached = 0;
		for (int32 I = 0; I < 6; ++I)
		{
			if (Anchors.Contains(Start + FHexCoord::Dirs[I]))
			{
				++NeighborsReached;
			}
		}
		Ctx.CheckEqual(TEXT("空旷地图上六个邻格全部可达"), NeighborsReached, 6);

		// 起点自身也在结果里（cost 0）
		Ctx.Check(TEXT("起点在可达集合中且代价为 0"),
			Anchors.Contains(Start) && Anchors[Start].Key == 0);
	}

	// ─────────────────────────────── 3. ⭐ 三级门宽闸门（D8 的核心证据）
	Ctx.Section(TEXT("三级门宽闸门（体型系统的玩法证据）"));
	{
		// 期望矩阵（架构文档 §6 陷阱 3 的实测结论）：
		//   门宽 1：S 可过，M 过不去，L 过不去
		//   门宽 2：S 可过，M 可过，  L 过不去
		//   门宽 3：S 可过，M 可过，  L 可过
		const bool Expected[3][3] = {
			// S      M      L
			{ true,  false, false },  // 门宽 1
			{ true,  true,  false },  // 门宽 2
			{ true,  true,  true  },  // 门宽 3
		};

		for (int32 GateWidth = 1; GateWidth <= 3; ++GateWidth)
		{
			for (int32 SI = 0; SI < 3; ++SI)
			{
				FHexGrid Grid;
				BuildGate(Grid, GateWidth);

				const FIntVector Start = FHexCoord::OffsetToCube(
					HexK::HeroSpawnCol, HexK::HeroSpawnRow);
				const int32 Facing = HexK::HeroSpawnFacing;

				// 起点必须先合法，否则测的不是通过性
				const FHexSizeClassDef& Def = FHexFootprint::GetSizeDef(Sizes[SI]);
				if (!FHexFootprint::CanPlace(Grid, Start, Def.Footprint, Facing, -1, Def.bCanCrushRubble))
				{
					Ctx.Fail(
						FString::Printf(TEXT("门宽%d 体型%s 的起点应合法"), GateWidth, SizeNames[SI]),
						FHexFootprint::PlaceFailureReason(Grid, Start, Def.Footprint, Facing, -1, Def.bCanCrushRubble));
					continue;
				}

				OccupySelf(Grid, Sizes[SI], Start, Facing);

				// 预算给足，纯测拓扑通过性
				FHexPathQuery Q = MakeQuery(Sizes[SI], Start, Facing, 99);

				// 目标：墙的另一侧中央 (4,7)
				const FIntVector Goal = FHexCoord::OffsetToCube(4, 7);
				const bool bActual = FHexPathfinder::CanReach(Grid, Q, Goal);
				const bool bExpect = Expected[GateWidth - 1][SI];

				Ctx.Check(
					FString::Printf(TEXT("门宽 %d 格：体型 %s %s"),
						GateWidth, SizeNames[SI], bExpect ? TEXT("可过") : TEXT("过不去")),
					bActual == bExpect,
					FString::Printf(TEXT("实际=%s 期望=%s"),
						bActual ? TEXT("可过") : TEXT("过不去"),
						bExpect ? TEXT("可过") : TEXT("过不去")));
			}
		}

		// 额外断言：闸门必须真的产生差异，否则 D8 只是美术需求
		{
			FHexGrid Grid;
			BuildGate(Grid, 1);
			const FIntVector Start = FHexCoord::OffsetToCube(HexK::HeroSpawnCol, HexK::HeroSpawnRow);
			const FIntVector Goal = FHexCoord::OffsetToCube(4, 7);

			FHexGrid GridS = Grid;
			OccupySelf(GridS, EHexSizeClass::S, Start, HexK::HeroSpawnFacing);
			const bool bS = FHexPathfinder::CanReach(GridS,
				MakeQuery(EHexSizeClass::S, Start, HexK::HeroSpawnFacing, 99), Goal);

			FHexGrid GridM = Grid;
			OccupySelf(GridM, EHexSizeClass::M, Start, HexK::HeroSpawnFacing);
			const bool bM = FHexPathfinder::CanReach(GridM,
				MakeQuery(EHexSizeClass::M, Start, HexK::HeroSpawnFacing, 99), Goal);

			Ctx.Check(TEXT("⭐ 1格门确实区分 S 与 M（D8 产生真实玩法差异）"),
				bS && !bM,
				FString::Printf(TEXT("S可过=%d M可过=%d"), bS ? 1 : 0, bM ? 1 : 0));
		}
	}

	// ─────────────────────────────── 4. 路径正确性：不含虚假绕路
	Ctx.Section(TEXT("路径不含原地旋转造成的虚假绕路"));
	{
		FHexGrid Grid;
		FHexLayouts::BuildOpenHall(Grid);

		const FIntVector Start = FHexCoord::OffsetToCube(4, 1);
		OccupySelf(Grid, EHexSizeClass::S, Start, 2);

		// 目标：正上方 3 格
		const FIntVector Goal = FHexCoord::OffsetToCube(4, 4);
		const int32 CoordDist = FHexCoord::Distance(Start, Goal);

		FHexPathQuery Q = MakeQuery(EHexSizeClass::S, Start, 2, 99);
		TArray<FIntVector> Path;
		const bool bFound = FHexPathfinder::PathTo(Grid, Q, Goal, -1, Path);

		Ctx.Check(TEXT("空旷地图上能找到路径"), bFound);
		if (bFound)
		{
			// ⚠️ 这条断言就是为了防住 Godot 版踩过的
			//    "距离 6 却走 8 步"（把原地旋转也算进路径步）
			Ctx.CheckEqual(TEXT("路径步数等于坐标距离（S 无虚假绕路）"),
				Path.Num(), CoordDist);

			// 路径终点必须是目标
			if (Path.Num() > 0)
			{
				Ctx.CheckEqualCoord(TEXT("路径终点是目标锚点"), Path.Last(), Goal);
			}

			// 路径每一步都必须是相邻格（不能跳格）
			bool bContiguous = true;
			FIntVector Prev = Start;
			for (const FIntVector& Step : Path)
			{
				if (FHexCoord::Distance(Prev, Step) != 1)
				{
					bContiguous = false;
					break;
				}
				Prev = Step;
			}
			Ctx.Check(TEXT("路径每一步都是相邻格"), bContiguous);
		}
	}

	// ─────────────────────────────── 5. 确定性：同输入多次运行结果逐位一致
	Ctx.Section(TEXT("确定性（纪律 5）"));
	{
		FHexGrid Grid;
		FHexLayouts::BuildPillarHall(Grid);

		const FIntVector Start = FHexCoord::OffsetToCube(4, 1);
		OccupySelf(Grid, EHexSizeClass::M, Start, 2);

		const FIntVector Goal = FHexCoord::OffsetToCube(4, 6);
		FHexPathQuery Q = MakeQuery(EHexSizeClass::M, Start, 2, 99);

		TArray<FIntVector> P1, P2, P3;
		FHexPathfinder::PathTo(Grid, Q, Goal, -1, P1);
		FHexPathfinder::PathTo(Grid, Q, Goal, -1, P2);
		FHexPathfinder::PathTo(Grid, Q, Goal, -1, P3);

		bool bSame = (P1.Num() == P2.Num()) && (P2.Num() == P3.Num());
		if (bSame)
		{
			for (int32 I = 0; I < P1.Num(); ++I)
			{
				if (P1[I] != P2[I] || P2[I] != P3[I])
				{
					bSame = false;
					break;
				}
			}
		}
		Ctx.Check(TEXT("同输入三次寻路结果逐位一致"), bSame,
			FString::Printf(TEXT("步数 %d/%d/%d"), P1.Num(), P2.Num(), P3.Num()));
	}

	// ─────────────────────────────── 6. 预算约束：Budget 必须真的限制范围
	Ctx.Section(TEXT("移动预算约束"));
	{
		FHexGrid Grid;
		FHexLayouts::BuildOpenHall(Grid);
		const FIntVector Start = FHexCoord::OffsetToCube(4, 4);
		OccupySelf(Grid, EHexSizeClass::S, Start, 0);

		int32 PrevCount = 0;
		bool bMonotonic = true;
		for (int32 Budget = 1; Budget <= 4; ++Budget)
		{
			FHexPathQuery Q = MakeQuery(EHexSizeClass::S, Start, 0, Budget);
			TMap<FIntVector, TPair<int32, int32>> Anchors;
			FHexPathfinder::ReachableAnchors(Grid, Q, Anchors);

			if (Anchors.Num() <= PrevCount)
			{
				bMonotonic = false;
				break;
			}
			PrevCount = Anchors.Num();
		}
		Ctx.Check(TEXT("可达锚点数随预算单调增加"), bMonotonic);

		// 预算 0 时只有起点
		{
			FHexPathQuery Q = MakeQuery(EHexSizeClass::S, Start, 0, 0);
			TMap<FIntVector, TPair<int32, int32>> Anchors;
			FHexPathfinder::ReachableAnchors(Grid, Q, Anchors);
			Ctx.CheckEqual(TEXT("预算 0 时只有起点可达"), Anchors.Num(), 1);
		}
	}

	// ─────────────────────────────── 7. 碎石移动成本
	Ctx.Section(TEXT("碎石地形成本 +1，L 可压碎"));
	{
		FHexGrid Grid;
		FHexLayouts::BuildOpenHall(Grid);

		// 在 (4,2) 放碎石，(4,1) 出发
		const FIntVector Rubble = FHexCoord::OffsetToCube(4, 2);
		Grid.SetTerrain(Rubble, EHexTerrain::Rubble);

		Ctx.CheckEqual(TEXT("碎石格移动成本为 2（不可压碎时）"),
			Grid.MoveCostAt(Rubble, false), 2);
		Ctx.CheckEqual(TEXT("碎石格移动成本为 1（可压碎时）"),
			Grid.MoveCostAt(Rubble, true), 1);
		Ctx.Check(TEXT("碎石可通行"), Grid.IsWalkable(Rubble, false));
	}

	// ─────────────────────────────── 8. 断桥：PIT 不可通行
	Ctx.Section(TEXT("断桥地形"));
	{
		FHexGrid Grid;
		FHexLayouts::BuildBrokenBridge(Grid);

		const FIntVector Pit = FHexCoord::OffsetToCube(1, 5);
		Ctx.Check(TEXT("PIT 不可通行"), !Grid.IsWalkable(Pit, false));
		Ctx.Check(TEXT("PIT 不阻断视线（可穿射）"),
			Grid.HasLineOfSight(
				FHexCoord::OffsetToCube(1, 4),
				FHexCoord::OffsetToCube(1, 6)));

		// 桥面 col 3-5 仍可通行，S 应能穿过
		const FIntVector Start = FHexCoord::OffsetToCube(4, 1);
		OccupySelf(Grid, EHexSizeClass::S, Start, 2);
		FHexPathQuery Q = MakeQuery(EHexSizeClass::S, Start, 2, 99);
		Ctx.Check(TEXT("S 能通过断桥的桥面到达 (4,7)"),
			FHexPathfinder::CanReach(Grid, Q, FHexCoord::OffsetToCube(4, 7)));
	}

	// ─────────────────────────────── 9. 视线：WALL 阻断、PILLAR 阻断
	Ctx.Section(TEXT("视线阻断"));
	{
		FHexGrid Grid;
		FHexLayouts::BuildOpenHall(Grid);

		const FIntVector A = FHexCoord::OffsetToCube(4, 2);
		const FIntVector B = FHexCoord::OffsetToCube(4, 6);
		Ctx.Check(TEXT("空旷地图有视线"), Grid.HasLineOfSight(A, B));

		// 在中间放墙
		Grid.SetTerrain(FHexCoord::OffsetToCube(4, 4), EHexTerrain::Wall);
		Ctx.Check(TEXT("中间有 WALL 时视线被阻断"), !Grid.HasLineOfSight(A, B));

		// 自身到自身恒有视线
		Ctx.Check(TEXT("自身到自身有视线"), Grid.HasLineOfSight(A, A));

		// 相邻格恒有视线（即使中间"理论上"有东西）
		Ctx.Check(TEXT("相邻格恒有视线"),
			Grid.HasLineOfSight(A, A + FHexCoord::Dirs[0]));
	}

	// ─────────────────────────────── 10. M/L 在石柱大厅的通行性差异
	Ctx.Section(TEXT("石柱大厅的体型通行差异"));
	{
		const FIntVector Start = FHexCoord::OffsetToCube(4, 1);
		const FIntVector Goal = FHexCoord::OffsetToCube(4, 7);

		for (int32 SI = 0; SI < 3; ++SI)
		{
			FHexGrid Grid;
			FHexLayouts::BuildPillarHall(Grid);
			OccupySelf(Grid, Sizes[SI], Start, HexK::HeroSpawnFacing);

			FHexPathQuery Q = MakeQuery(Sizes[SI], Start, HexK::HeroSpawnFacing, 99);
			const bool bReach = FHexPathfinder::CanReach(Grid, Q, Goal);

			// 石柱大厅中央通道宽，三种体型都应能过；
			// 这条断言保证模板不会意外把 L 锁死（那样 Boss 战会无法进行）
			Ctx.Check(
				FString::Printf(TEXT("石柱大厅：体型 %s 能到达出口"), SizeNames[SI]),
				bReach);
		}
	}

	return Ctx.NumFailed() == 0;
}
