// Copyright Hex Spire. All Rights Reserved.
//
// 地形模板库 —— 策划案 §9.7
//
// 模板必须手工设计并与怪物组配对。纯随机地形会产出大量"无聊"或"不可能"的战场
// （《Into the Breach》与《火焰纹章》的共同结论）。
//
// ⚠️ 每个模板都要能通过 LayoutValidator 的三条校验：
//    ① 出生区对 S/M/L 三种体型均合法（否则 M/L 英雄无法出生）
//    ② 全图连通（不存在无法到达的区域）
//    ③ 能容纳其配对怪物组中所有敌人的 footprint

#pragma once

#include "CoreMinimal.h"
#include "Core/HexSpireEnums.h"

class FHexGrid;

/** 地形模板的元信息 */
struct HEXSPIRECORE_API FHexLayoutInfo
{
	FName Id;
	FString DisplayName;
	/** 本模板能容纳的最大敌人体型（由校验器填写） */
	EHexSizeClass MaxEnemySize = EHexSizeClass::L;
	/** 哪些体型能在此出生（由校验器填写） */
	bool bValidForS = true;
	bool bValidForM = true;
	bool bValidForL = true;
	/**
	 * ⚠️ 配对警告：门宽 < 英雄体型时，怪物组不得含追踪型远程单位。
	 *    Godot 版实测：巨人(M) + 1格门 + 含投石手的怪物组 = 15 场全败，
	 *    巨人过不了门被隔墙风筝到死，玩家毫无应对手段。
	 *    这是体型闸门正常生效的结果，但作为关卡配置不公平。
	 */
	bool bForbidTrackingRangedEnemies = false;
};

struct HEXSPIRECORE_API FHexLayouts
{
	/** 全部模板 id，顺序固定 */
	static const TArray<FName>& AllLayoutIds();

	/** 按 id 构建一个网格；未知 id 返回空旷厅堂 */
	static void Build(FName LayoutId, FHexGrid& OutGrid);

	static FHexLayoutInfo GetInfo(FName LayoutId);

	// ── 具体模板

	/** 空旷厅堂：全 FLOOR，出口在 (4,7)。基准战场，对 L 型 Boss 友好 */
	static void BuildOpenHall(FHexGrid& G);

	/** 狭道·2格门：M 能过且能堵死，L 过不去（已用寻路验证） */
	static void BuildNarrowPass(FHexGrid& G);

	/** 狭道·1格门：只有 S 能过（真正的体型闸门） */
	static void BuildBottleneck(FHexGrid& G);

	/**
	 * 尖刺牢房。
	 * ⚠️ 尖刺位置是算过的：L 在 (4,1) facing=2 占
	 *   {(4,1)(3,2)(4,2)(3,3)(4,3)(5,3)}，与 6 个尖刺格无交集 ——
	 *   避免"L 英雄一出生就踩多格 hazard 各结算一次"（§8.2.2 机制点 7）
	 */
	static void BuildSpikeCell(FHexGrid& G);

	/** 石柱大厅：4 根柱子对称分布。断视线；限制 M/L 通行 */
	static void BuildPillarHall(FHexGrid& G);

	/** 断桥：中间一排 PIT，分割上下战场。推入深坑；限制通行 */
	static void BuildBrokenBridge(FHexGrid& G);
};
