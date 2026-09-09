// Copyright Hex Spire. All Rights Reserved.

#include "Hex/HexPathfinder.h"
#include "Hex/HexCoord.h"
#include "Hex/HexFootprint.h"
#include "Hex/HexGrid.h"

int32 FHexPathfinder::StateId(const FIntVector& Anchor, int32 Facing)
{
	const FIntPoint O = FHexCoord::CubeToOffset(Anchor);
	const int32 Idx = (O.Y - 1) * FHexCoord::Cols + (O.X - 1);
	return Idx * 6 + (((Facing % 6) + 6) % 6);
}

void FHexPathfinder::DecodeState(int32 Sid, FIntVector& OutAnchor, int32& OutFacing)
{
	OutFacing = Sid % 6;
	const int32 Idx = Sid / 6;
	const int32 Col = Idx % FHexCoord::Cols + 1;
	const int32 Row = Idx / FHexCoord::Cols + 1;
	OutAnchor = FHexCoord::OffsetToCube(Col, Row);
}

void FHexPathfinder::Reachable(
	const FHexGrid& Grid,
	const FHexPathQuery& Query,
	TMap<int32, FHexPathNode>& Out)
{
	Out.Reset();

	const int32 StartId = StateId(Query.StartAnchor, Query.StartFacing);
	{
		FHexPathNode Start;
		Start.Anchor = Query.StartAnchor;
		Start.Facing = ((Query.StartFacing % 6) + 6) % 6;
		Start.Cost = 0;
		Start.Prev = -1;
		Out.Add(StartId, Start);
	}

	// 状态总数只有 294，用"数组 + 每轮线性取最小"比堆更简单且完全确定。
	// 复杂度 O(N²)=~86k 次比较，对回合制游戏完全无感。
	TArray<int32> Frontier;
	Frontier.Add(StartId);

	// 已定型（确定最短代价）的状态，避免重复展开
	TSet<int32> Settled;

	while (Frontier.Num() > 0)
	{
		// 取 Cost 最小、Id 最小者（确定性 tiebreak）
		int32 BestIdx = 0;
		{
			const FHexPathNode& B0 = Out[Frontier[0]];
			int32 BestCost = B0.Cost;
			int32 BestSid = Frontier[0];
			for (int32 I = 1; I < Frontier.Num(); ++I)
			{
				const int32 Sid = Frontier[I];
				const int32 C = Out[Sid].Cost;
				if (C < BestCost || (C == BestCost && Sid < BestSid))
				{
					BestCost = C;
					BestSid = Sid;
					BestIdx = I;
				}
			}
		}

		const int32 CurId = Frontier[BestIdx];
		Frontier.RemoveAtSwap(BestIdx, EAllowShrinking::No);

		if (Settled.Contains(CurId))
		{
			continue;
		}
		Settled.Add(CurId);

		const FHexPathNode Cur = Out[CurId];
		if (Cur.Cost >= Query.Budget)
		{
			continue;
		}

		// ── 邻居 1：6 个平移（Facing 不变）。顺序固定 0..5
		for (int32 DI = 0; DI < 6; ++DI)
		{
			const FIntVector NextAnchor = Cur.Anchor + FHexCoord::Dirs[DI];

			if (!FHexFootprint::CanPlace(Grid, NextAnchor, Query.Footprint,
				Cur.Facing, Query.SelfUnitId, Query.bCanCrushRubble))
			{
				continue;
			}

			int32 Step = Grid.MoveCostAt(NextAnchor, Query.bCanCrushRubble) + Query.MoveCostDelta;
			Step = FMath::Max(1, Step);

			const int32 NewCost = Cur.Cost + Step;
			if (NewCost > Query.Budget)
			{
				continue;
			}

			const int32 Nid = StateId(NextAnchor, Cur.Facing);
			FHexPathNode* Existing = Out.Find(Nid);
			if (!Existing || NewCost < Existing->Cost)
			{
				FHexPathNode N;
				N.Anchor = NextAnchor;
				N.Facing = Cur.Facing;
				N.Cost = NewCost;
				N.Prev = CurId;
				Out.Add(Nid, N);
				Frontier.Add(Nid);
			}
		}

		// ── 邻居 2：原地旋转 ±1（顺序固定 +1 然后 -1）
		const int32 RotDeltas[2] = { 1, -1 };
		for (int32 K = 0; K < 2; ++K)
		{
			const int32 NF = (((Cur.Facing + RotDeltas[K]) % 6) + 6) % 6;

			// ⚠️ 多格单位转向会改变占位，必须校验（§8.2.2 机制点 5）
			if (!FHexFootprint::CanPlace(Grid, Cur.Anchor, Query.Footprint,
				NF, Query.SelfUnitId, Query.bCanCrushRubble))
			{
				continue;
			}

			const int32 NewCost = Cur.Cost + FMath::Max(0, Query.RotateCost);
			if (NewCost > Query.Budget)
			{
				continue;
			}

			const int32 Nid = StateId(Cur.Anchor, NF);
			FHexPathNode* Existing = Out.Find(Nid);
			if (!Existing || NewCost < Existing->Cost)
			{
				FHexPathNode N;
				N.Anchor = Cur.Anchor;
				N.Facing = NF;
				N.Cost = NewCost;
				N.Prev = CurId;
				Out.Add(Nid, N);
				Frontier.Add(Nid);
			}
		}
	}
}

void FHexPathfinder::ReachableAnchors(
	const FHexGrid& Grid,
	const FHexPathQuery& Query,
	TMap<FIntVector, TPair<int32, int32>>& Out)
{
	TMap<int32, FHexPathNode> Nodes;
	Reachable(Grid, Query, Nodes);

	Out.Reset();

	// 按 StateId 升序遍历，保证同 Cost 时结果确定（纪律 5）
	TArray<int32> Ids;
	Nodes.GetKeys(Ids);
	Ids.Sort();

	for (const int32 Sid : Ids)
	{
		const FHexPathNode& N = Nodes[Sid];
		TPair<int32, int32>* Existing = Out.Find(N.Anchor);
		if (!Existing || N.Cost < Existing->Key)
		{
			Out.Add(N.Anchor, TPair<int32, int32>(N.Cost, N.Facing));
		}
	}
}

bool FHexPathfinder::PathTo(
	const FHexGrid& Grid,
	const FHexPathQuery& Query,
	const FIntVector& TargetAnchor,
	int32 TargetFacing,
	TArray<FIntVector>& OutPath)
{
	OutPath.Reset();

	TMap<int32, FHexPathNode> Nodes;
	Reachable(Grid, Query, Nodes);

	TArray<int32> Ids;
	Nodes.GetKeys(Ids);
	Ids.Sort(); // 确定性

	int32 BestId = -1;
	int32 BestCost = TNumericLimits<int32>::Max();
	for (const int32 Sid : Ids)
	{
		const FHexPathNode& N = Nodes[Sid];
		if (N.Anchor != TargetAnchor)
		{
			continue;
		}
		if (TargetFacing >= 0 && N.Facing != TargetFacing)
		{
			continue;
		}
		if (N.Cost < BestCost)
		{
			BestCost = N.Cost;
			BestId = Sid;
		}
	}

	if (BestId < 0)
	{
		return false;
	}

	// 回溯。只保留锚点发生变化的节点 —— 见头文件注释。
	TArray<FIntVector> Chain;
	int32 Cur = BestId;
	FIntVector PrevAnchor(TNumericLimits<int32>::Max(), 0, 0);
	while (Cur != -1)
	{
		const FHexPathNode& N = Nodes[Cur];
		if (N.Anchor != PrevAnchor)
		{
			Chain.Add(N.Anchor);
			PrevAnchor = N.Anchor;
		}
		Cur = N.Prev;
	}
	Algo::Reverse(Chain);

	// 去掉起点，只留移动步
	if (Chain.Num() > 1)
	{
		Chain.RemoveAt(0);
	}
	else
	{
		Chain.Reset();
	}

	OutPath = MoveTemp(Chain);
	return true;
}

int32 FHexPathfinder::BestFacingAt(
	const FHexGrid& Grid,
	const FHexPathQuery& Query,
	const FIntVector& TargetAnchor)
{
	TMap<int32, FHexPathNode> Nodes;
	Reachable(Grid, Query, Nodes);

	TArray<int32> Ids;
	Nodes.GetKeys(Ids);
	Ids.Sort();

	int32 BestFacing = -1;
	int32 BestCost = TNumericLimits<int32>::Max();
	for (const int32 Sid : Ids)
	{
		const FHexPathNode& N = Nodes[Sid];
		if (N.Anchor == TargetAnchor && N.Cost < BestCost)
		{
			BestCost = N.Cost;
			BestFacing = N.Facing;
		}
	}
	return BestFacing;
}

bool FHexPathfinder::CanReach(
	const FHexGrid& Grid,
	const FHexPathQuery& Query,
	const FIntVector& ToAnchor)
{
	TMap<int32, FHexPathNode> Nodes;
	Reachable(Grid, Query, Nodes);
	for (const TPair<int32, FHexPathNode>& Pair : Nodes)
	{
		if (Pair.Value.Anchor == ToAnchor)
		{
			return true;
		}
	}
	return false;
}
