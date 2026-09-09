// Copyright Hex Spire. All Rights Reserved.

#include "Content/HexLayouts.h"
#include "Hex/HexGrid.h"
#include "Hex/HexCoord.h"
#include "Core/HexSpireConstants.h"

namespace
{
	FORCEINLINE FIntVector Off(int32 Col, int32 Row)
	{
		return FHexCoord::OffsetToCube(Col, Row);
	}

	/** 所有模板统一在 (4,7) 放出口 —— 战斗胜利后走到这里离开房间（§8.4） */
	void PlaceExit(FHexGrid& G)
	{
		G.SetTerrain(Off(4, 7), EHexTerrain::ExitGate);
	}
}

const TArray<FName>& FHexLayouts::AllLayoutIds()
{
	static const TArray<FName> Ids = {
		TEXT("open_hall"),
		TEXT("narrow_pass"),
		TEXT("bottleneck"),
		TEXT("spike_cell"),
		TEXT("pillar_hall"),
		TEXT("broken_bridge"),
	};
	return Ids;
}

void FHexLayouts::Build(FName LayoutId, FHexGrid& OutGrid)
{
	OutGrid = FHexGrid(HexK::BoardCols, HexK::BoardRows);

	const FString S = LayoutId.ToString();
	if (S == TEXT("narrow_pass"))
	{
		BuildNarrowPass(OutGrid);
	}
	else if (S == TEXT("bottleneck"))
	{
		BuildBottleneck(OutGrid);
	}
	else if (S == TEXT("spike_cell"))
	{
		BuildSpikeCell(OutGrid);
	}
	else if (S == TEXT("pillar_hall"))
	{
		BuildPillarHall(OutGrid);
	}
	else if (S == TEXT("broken_bridge"))
	{
		BuildBrokenBridge(OutGrid);
	}
	else
	{
		BuildOpenHall(OutGrid);
	}
}

FHexLayoutInfo FHexLayouts::GetInfo(FName LayoutId)
{
	FHexLayoutInfo Info;
	Info.Id = LayoutId;

	const FString S = LayoutId.ToString();
	if (S == TEXT("narrow_pass"))
	{
		Info.DisplayName = TEXT("狭道·2格门");
		Info.MaxEnemySize = EHexSizeClass::M;
	}
	else if (S == TEXT("bottleneck"))
	{
		Info.DisplayName = TEXT("狭道·1格门");
		Info.MaxEnemySize = EHexSizeClass::S;
		// 见头文件注释：门宽 1 时不得配追踪型远程
		Info.bForbidTrackingRangedEnemies = true;
	}
	else if (S == TEXT("spike_cell"))
	{
		Info.DisplayName = TEXT("尖刺牢房");
		Info.MaxEnemySize = EHexSizeClass::M;
	}
	else if (S == TEXT("pillar_hall"))
	{
		Info.DisplayName = TEXT("石柱大厅");
		Info.MaxEnemySize = EHexSizeClass::M;
	}
	else if (S == TEXT("broken_bridge"))
	{
		Info.DisplayName = TEXT("断桥");
		Info.MaxEnemySize = EHexSizeClass::M;
	}
	else
	{
		Info.DisplayName = TEXT("空旷厅堂");
		Info.MaxEnemySize = EHexSizeClass::L;
	}
	return Info;
}

// ───────────────────────────────────────────────────────── 模板实现

void FHexLayouts::BuildOpenHall(FHexGrid& G)
{
	PlaceExit(G);
}

void FHexLayouts::BuildNarrowPass(FHexGrid& G)
{
	// row 4 拉一道墙，只在 col 3、4 留 2 格通道
	for (int32 Col = 1; Col <= HexK::BoardCols; ++Col)
	{
		if (Col == 3 || Col == 4)
		{
			continue;
		}
		G.SetTerrain(Off(Col, 4), EHexTerrain::Wall);
	}
	PlaceExit(G);
}

void FHexLayouts::BuildBottleneck(FHexGrid& G)
{
	// row 4 拉一道墙，只在 col 4 留 1 格通道 → 只有 S 能过
	for (int32 Col = 1; Col <= HexK::BoardCols; ++Col)
	{
		if (Col == 4)
		{
			continue;
		}
		G.SetTerrain(Off(Col, 4), EHexTerrain::Wall);
	}
	PlaceExit(G);
}

void FHexLayouts::BuildSpikeCell(FHexGrid& G)
{
	G.SetTerrain(Off(2, 4), EHexTerrain::Wall);
	G.SetTerrain(Off(6, 4), EHexTerrain::Wall);

	// 尖刺格：与 L 体型出生占格无交集（见头文件注释）
	const FIntPoint Spikes[] = {
		FIntPoint(2, 3), FIntPoint(6, 3), FIntPoint(4, 4),
		FIntPoint(3, 5), FIntPoint(5, 5), FIntPoint(4, 6),
	};
	for (const FIntPoint& P : Spikes)
	{
		// -1 = 永久危害
		G.SetHazard(Off(P.X, P.Y), EHexHazard::Spikes, -1);
	}
	PlaceExit(G);
}

void FHexLayouts::BuildPillarHall(FHexGrid& G)
{
	// 4 根柱子对称分布，断视线且限制大体型通行。
	// ⚠️ 刻意避开 row 1–3 的中央区域（col 3–5），保证 L 英雄能出生。
	const FIntPoint Pillars[] = {
		FIntPoint(2, 3), FIntPoint(6, 3),
		FIntPoint(2, 5), FIntPoint(6, 5),
	};
	for (const FIntPoint& P : Pillars)
	{
		G.SetTerrain(Off(P.X, P.Y), EHexTerrain::Wall);
		G.SetFeature(Off(P.X, P.Y), EHexFeature::Pillar);
	}
	PlaceExit(G);
}

void FHexLayouts::BuildBrokenBridge(FHexGrid& G)
{
	// row 5 一排深坑，只在 col 3、4、5 留桥面。
	// PIT 不可通行但可穿射 → 远程仍能对射，近战必须绕桥。
	for (int32 Col = 1; Col <= HexK::BoardCols; ++Col)
	{
		if (Col >= 3 && Col <= 5)
		{
			continue;
		}
		G.SetTerrain(Off(Col, 5), EHexTerrain::Pit);
	}
	PlaceExit(G);
}
