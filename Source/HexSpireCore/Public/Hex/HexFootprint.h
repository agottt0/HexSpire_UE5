// Copyright Hex Spire. All Rights Reserved.
//
// 体型定义与占位校验的【唯一出口】—— D8 / 策划案 §14.2、§15.2
//
// ⚠️ R9（策划案 §16.1）：多格单位的边界情况极多（转向被卡、推拉部分重叠、
//    寻路、召唤位置），且"必须在 M0 就做，后期加体型系统的重构成本极高"。
//
// 因此：所有位移 / 生成 / 转向【必须】走 CanPlace()，
// 即使当前单位是 S 体型（footprint 长度恒为 1）。
// 不要写"如果是 S 就跳过检查"的快路径 —— 那正是 R9 说的重构陷阱。

#pragma once

#include "CoreMinimal.h"
#include "Core/HexSpireEnums.h"

class FHexGrid;

/**
 * 体型档位定义（§15.2 SizeClassData）。
 *
 * 三种基准体型均为【六边形三角形】，因此旋转规则统一、视觉一致：
 *   S = 1 格，  M = 边长2三角形(3格)，  L = 边长3三角形(6格)
 *
 * 选三角形而非直线/菱形的理由（§8.2.1）：
 *   ① 三角形在六边形网格上是最紧凑的多格形状，视觉上像"一坨"而非"一串"
 *   ② 边长2与边长3是同一族形状，旋转/碰撞代码完全共用
 *   ③ 三角形有明确的"尖端"，天然指示朝向
 */
struct HEXSPIRECORE_API FHexSizeClassDef
{
	EHexSizeClass SizeClass = EHexSizeClass::S;

	/** facing=0 时相对锚点的立方偏移。实际占格 = Anchor + Rotate(Offset, Facing) */
	TArray<FIntVector> Footprint;

	/** 位移抗性。S=0, M=1, L=999(免疫)。§8.2.2 机制点 4 */
	int32 DefaultKnockbackResist = 0;

	/** 转向成本。多格单位"转身慢"，绕后打它们更有价值。§8.2.2 机制点 5 */
	int32 DefaultRotateCost = 0;

	/** 能否碾压穿过比自己小的单位（M/L）。§8.2.2 机制点 6 */
	bool bCanTrampleBelow = false;

	/** 能否压碎碎石（L）。§8.2.2 机制点 7 */
	bool bCanCrushRubble = false;

	/** 掉落散布半径 —— 体型越大死亡后"爆一堆"。§8.2.2 机制点 8 */
	int32 LootScatterRadius = 0;

	FORCEINLINE int32 CellCount() const { return Footprint.Num(); }
};

/**
 * 体型占位的静态工具集。
 * 全部为静态函数，无状态，可在任意线程调用（BattleSim 会并行跑）。
 */
struct HEXSPIRECORE_API FHexFootprint
{
	/** 取某体型档位的定义。三档均为编译期常量表，返回引用不会失效。 */
	static const FHexSizeClassDef& GetSizeDef(EHexSizeClass SizeClass);

	/**
	 * 把 footprint 按 facing 旋转后展开成绝对立方坐标集合。
	 *
	 * ⚠️ 用 FHexCoord::Rotate（策划案 §8.1 原文公式），已验证与 §8.2.4
	 *    的出生表一致：facing=2 时 M 占 (4,1)(3,2)(4,2)，朝上。
	 */
	static void Cells(
		const FIntVector& Anchor,
		const TArray<FIntVector>& Footprint,
		int32 Facing,
		TArray<FIntVector>& Out);

	/** 便捷重载：直接按体型档位展开 */
	static void Cells(
		const FIntVector& Anchor,
		EHexSizeClass SizeClass,
		int32 Facing,
		TArray<FIntVector>& Out);

	/**
	 * 校验一个单位能否放在 (Anchor, Facing)。
	 *
	 * 依次校验（§15.2 原文顺序）：
	 *   1. InBounds   —— 所有格必须在 7×7 内
	 *   2. IsWalkable —— 非 WALL / 非 PIT
	 *   3. OccupantAt —— 未被其他单位占据
	 *
	 * @param IgnoreUnitId 移动自己时要忽略自己当前占的格，
	 *                     否则原地转向永远失败。
	 */
	static bool CanPlace(
		const FHexGrid& Grid,
		const FIntVector& Anchor,
		const TArray<FIntVector>& Footprint,
		int32 Facing,
		int32 IgnoreUnitId = -1,
		bool bCanCrushRubble = false);

	/** 校验失败的原因（用于 UI 提示与调试：告诉玩家"为什么放不下"） */
	static FString PlaceFailureReason(
		const FHexGrid& Grid,
		const FIntVector& Anchor,
		const TArray<FIntVector>& Footprint,
		int32 Facing,
		int32 IgnoreUnitId = -1,
		bool bCanCrushRubble = false);

	/**
	 * 单位占据格的相邻格集合（不含自身占格）。
	 * 用于 AdjacentAll 目标形状、背击判定、"相邻敌人"类效果。
	 *
	 * ⚠️ 大体型的相邻格显著更多 —— 这是 §8.2.2 机制点 3 的直接后果，
	 *    也是"大体型的 AOE 天然更大"的来源。
	 */
	static void AdjacentCells(
		const FIntVector& Anchor,
		const TArray<FIntVector>& Footprint,
		int32 Facing,
		TArray<FIntVector>& Out);

	/**
	 * 从多格单位到目标格的距离 —— 取【最近的己方 footprint 格】起算。
	 * §8.2.2 机制点 3：大体型的"有效射程"更长。
	 */
	static int32 DistanceFrom(
		const FIntVector& Anchor,
		const TArray<FIntVector>& Footprint,
		int32 Facing,
		const FIntVector& Target);

	/**
	 * footprint 的边界边集合，用于描边渲染（§13.2 硬性要求）。
	 *
	 * 算法：每格的 6 条边中，邻格不属于自己的那条就是边界边。
	 * 返回值为 (格, 方向索引) 对，表现层据此画线。
	 *
	 * 输出顺序确定（按 z 再 x 排序），保证描边网格可缓存比对。
	 */
	static void BoundaryEdges(
		const FIntVector& Anchor,
		const TArray<FIntVector>& Footprint,
		int32 Facing,
		TArray<TPair<FIntVector, int32>>& Out);
};
