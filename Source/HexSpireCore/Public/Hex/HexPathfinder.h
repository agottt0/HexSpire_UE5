// Copyright Hex Spire. All Rights Reserved.
//
// 多格单位寻路 —— 策划案 §14.1
//
// ⚠️ 状态是 (Anchor, Facing) 而【不是】Anchor ——
//    因为 M/L 转向会改变占位（§8.2.2 机制点 5），
//    "能不能走到那里"可能取决于"到那里时朝哪边"。
//    状态数 49×6 = 294，Dijkstra 秒开。
//
// ⚠️ 这里也是"体型能否通过狭道"的【正确判定处】。
//    footprint 层只能判单点合法性，通过性必须搜索 ——
//    Godot 版曾连续用 4 种静态几何判据全部失败。
//
//    实测的三级门宽闸门（架构文档 §6 陷阱 3）：
//      门宽 1 格：S 可过，M 过不去，L 过不去
//      门宽 2 格：S 可过，M 可过，  L 过不去
//      门宽 3 格：S 可过，M 可过，  L 可过
//
// 确定性（纪律 5）：
//   · 优先队列比较键 = (Cost, StateId)，StateId = AnchorIndex*6 + Facing
//   · 邻居枚举顺序固定为 [平移 0..5, 旋转 +1, 旋转 -1]
//   禁止用无 tiebreak 的堆 —— 同代价路径的选择必须可复现。

#pragma once

#include "CoreMinimal.h"
#include "Core/HexSpireEnums.h"

class FHexGrid;

/** 寻路查询的输入参数。显式传参而非传 Unit，便于验证器穷举各种体型组合。 */
struct HEXSPIRECORE_API FHexPathQuery
{
	FIntVector StartAnchor = FIntVector::ZeroValue;
	int32 StartFacing = 0;

	/** facing=0 时的相对偏移数组 */
	TArray<FIntVector> Footprint;

	/** 移动预算（格数）。旋转也可能消耗预算。 */
	int32 Budget = 1;

	/** 自身单位 id —— 校验占位时必须忽略自己当前占的格，否则原地转向永远失败 */
	int32 SelfUnitId = -1;

	int32 RotateCost = 0;
	bool bCanCrushRubble = false;

	/** 来自 RuleBook 的移动成本增减（符文可改写 MoveCostDelta） */
	int32 MoveCostDelta = 0;
};

/** 单个可达状态 */
struct HEXSPIRECORE_API FHexPathNode
{
	FIntVector Anchor = FIntVector::ZeroValue;
	int32 Facing = 0;
	int32 Cost = 0;
	/** 前驱 StateId，-1 表示起点 */
	int32 Prev = -1;
};

struct HEXSPIRECORE_API FHexPathfinder
{
	/** 把 (Anchor, Facing) 编码成整数，用于确定性排序与查表 */
	static int32 StateId(const FIntVector& Anchor, int32 Facing);

	static void DecodeState(int32 Sid, FIntVector& OutAnchor, int32& OutFacing);

	/**
	 * 计算全部可达状态。
	 * @return StateId → FHexPathNode 的映射
	 */
	static void Reachable(
		const FHexGrid& Grid,
		const FHexPathQuery& Query,
		TMap<int32, FHexPathNode>& Out);

	/**
	 * 可达的【锚点】集合（UI 高亮用）。同一锚点取最小 Cost。
	 * @return Anchor → (Cost, Facing) 
	 */
	static void ReachableAnchors(
		const FHexGrid& Grid,
		const FHexPathQuery& Query,
		TMap<FIntVector, TPair<int32, int32>>& Out);

	/**
	 * 求到目标锚点的路径。返回途经锚点的立方坐标数组（不含起点）。
	 *
	 * @param TargetFacing < 0 表示任意朝向（取代价最小者）
	 *
	 * ⚠️ 只保留【锚点发生变化】的节点 —— 状态图里"原地旋转"也是一条边，
	 *    若把它也算进路径，S 单位（RotateCost=0）会出现
	 *    "距离 6 却走 8 步"这种虚假绕路（Godot 版实测踩过）。
	 *    朝向变化通过移动动作的 Facing 参数一次性传达，不占路径步。
	 */
	static bool PathTo(
		const FHexGrid& Grid,
		const FHexPathQuery& Query,
		const FIntVector& TargetAnchor,
		int32 TargetFacing,
		TArray<FIntVector>& OutPath);

	/** 到达目标锚点时的最优朝向；不可达返回 -1 */
	static int32 BestFacingAt(
		const FHexGrid& Grid,
		const FHexPathQuery& Query,
		const FIntVector& TargetAnchor);

	/**
	 * ⭐ 通过性判定：能否从起点走到 ToAnchor（任意朝向）。
	 * 这是"体型能否通过狭道"的正确答案 —— 见文件头注释。
	 */
	static bool CanReach(
		const FHexGrid& Grid,
		const FHexPathQuery& Query,
		const FIntVector& ToAnchor);
};
