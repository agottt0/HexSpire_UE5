// Copyright Hex Spire. All Rights Reserved.
//
// 六边形坐标系的【唯一转换出口】—— 策划案 §8.1
//
// 禁止在任何其他文件手写坐标转换。由 CheckDiscipline 白名单强制。
//
// 两套坐标：
//   对外（策划/存档/UI）：offset (col, row)，odd-r，**1-indexed，原点在左下角**
//   对内（算法/距离/旋转/footprint）：cube FIntVector(x,y,z)，恒满足 x+y+z == 0
//
// ⚠️ 陷阱 H1：`Dirs[facing]` 不是朝向向量！
//   Rotate() 把 Dirs[i] 映射到 Dirs[(i-1) mod 6]（索引递减），
//   所以 facing=2 的 footprint 朝上，但 Dirs[2] 却指向 row 减小的方向（朝下）。
//   取朝向向量必须用 FacingDir()。
//
// ⚠️ 陷阱 H2：MapYFlip 必须是偶数。见该常量注释。

#pragma once

#include "CoreMinimal.h"
#include "Core/HexSpireConstants.h"

/**
 * 立方坐标的哈希与比较。
 * UE 的 FIntVector 自带 operator== 与 GetTypeHash，可直接做 TMap 键。
 */
struct HEXSPIRECORE_API FHexCoord
{
	static constexpr int32 Cols = HexK::BoardCols;
	static constexpr int32 Rows = HexK::BoardRows;

	/**
	 * 业务 row 1 在【下】，而多数渲染坐标 y 向【上】或向【下】增长，需要翻转。
	 *
	 * ⚠️ 这个常数【必须是偶数】：
	 *   `K - row` 与 `row` 同奇偶 ⟺ K 为偶数。
	 *   若取奇数（例如 Rows=7），row 的奇偶性会翻转，odd-r 的"奇数行右移"
	 *   会渲染成"偶数行右移" —— 整个网格被剪切错位，且只有相邻关系错、
	 *   单看一格完全正常，极难发现。
	 */
	static constexpr int32 MapYFlip = 8;

	/** 6 个方向向量。索引【不是】朝向，取朝向请用 FacingDir()。 */
	static const FIntVector Dirs[6];

	// ───────────────────────────────────────────── offset ↔ cube

	/** odd-r 偏移 (1-indexed, 左下原点) → 立方坐标 */
	static FORCEINLINE FIntVector OffsetToCube(int32 Col, int32 Row)
	{
		const int32 C = Col - 1;
		const int32 R = Row - 1;
		// 被除数恒为偶数，故截断除法与 floor 除法等价，负数区间也安全
		const int32 X = C - (R - (R & 1)) / 2;
		const int32 Z = R;
		return FIntVector(X, -X - Z, Z);
	}

	static FORCEINLINE FIntVector OffsetToCube(const FIntPoint& Offset)
	{
		return OffsetToCube(Offset.X, Offset.Y);
	}

	static FORCEINLINE FIntPoint CubeToOffset(const FIntVector& Cube)
	{
		const int32 Col = Cube.X + (Cube.Z - (Cube.Z & 1)) / 2 + 1;
		const int32 Row = Cube.Z + 1;
		return FIntPoint(Col, Row);
	}

	// ───────────────────────────────────────────── 度量

	static FORCEINLINE int32 Distance(const FIntVector& A, const FIntVector& B)
	{
		return FMath::Max3(
			FMath::Abs(A.X - B.X),
			FMath::Abs(A.Y - B.Y),
			FMath::Abs(A.Z - B.Z));
	}

	static FORCEINLINE FIntVector Neighbor(const FIntVector& Cube, int32 DirIndex)
	{
		return Cube + Dirs[((DirIndex % 6) + 6) % 6];
	}

	static void Neighbors(const FIntVector& Cube, TArray<FIntVector>& Out);

	static FORCEINLINE bool InBounds(const FIntVector& Cube)
	{
		const FIntPoint O = CubeToOffset(Cube);
		return O.X >= 1 && O.X <= Cols && O.Y >= 1 && O.Y <= Rows;
	}

	// ───────────────────────────────────────────── 旋转与朝向

	/**
	 * 绕原点旋转 60° × Steps。
	 *
	 * 公式取自策划案 §8.1 原文，【不要改】—— 已用 §8.2.4 的出生表验证：
	 *   facing=2 时 M 体型占 (4,1)(3,2)(4,2)，row 递增 = 朝上 ✓
	 *   facing=2 时 L 体型占 (4,1)(3,2)(4,2)(3,3)(4,3)(5,3)，全在 7×7 内 ✓
	 * 曾考虑的"修正"公式 (-y,-z,-x) 会把出生点算到 row 0 / -1，直接出界。
	 *
	 * 注意：本公式使 Dirs 的索引【递减】。这不影响正确性（旋转自洽、
	 * footprint 不重叠），但意味着不能拿 facing 直接去索引 Dirs ——
	 * 这就是 FacingDir() 存在的原因。
	 */
	static FORCEINLINE FIntVector Rotate(const FIntVector& Cube, int32 Steps)
	{
		FIntVector V = Cube;
		const int32 N = ((Steps % 6) + 6) % 6;
		for (int32 I = 0; I < N; ++I)
		{
			V = FIntVector(-V.Z, -V.X, -V.Y);
		}
		return V;
	}

	/**
	 * 朝向 Facing (0-5) 对应的【单位方向向量】。
	 *
	 * ⚠️ 这是 facing → 方向 的唯一出口。禁止写 Dirs[Unit.Facing]。
	 *
	 * 之所以是 (-Facing) mod 6：Rotate() 让索引递减，而 footprint 是用
	 * Rotate(Offset, Facing) 展开的，所以朝向向量必须用同样的递减方向取。
	 * 已验证 6 个朝向下 FacingDir 均落在 footprint 的尖端侧。
	 */
	static FORCEINLINE FIntVector FacingDir(int32 Facing)
	{
		return Dirs[((-Facing % 6) + 6) % 6];
	}

	/**
	 * FacingDir 的逆运算：方向索引 → 朝向。
	 *
	 * ⚠️ 必须走这个函数，不要手写 `facing = dirIndex`（陷阱 H1 的变体）。
	 *    Rotate() 使 Dirs 索引【递减】，因此 facing 与 dirIndex 是
	 *    互为相反数的关系，而不是相等。直接赋值会让单位转到镜像方向，
	 *    背击判定跟着全错 —— 而且错得很隐蔽：6 个朝向里有 2 个恰好自洽。
	 *
	 *    恒等式（已由 VerifyHexCoord 断言）：
	 *      FacingDir(FacingFromDir(i)) == Dirs[i]
	 */
	static FORCEINLINE int32 FacingFromDir(int32 DirIndex)
	{
		return ((-DirIndex % 6) + 6) % 6;
	}

	/** 朝向 Facing 的【正后方】方向索引。实测对 6 个朝向均成立。 */
	static FORCEINLINE int32 RearDirIndex(int32 Facing)
	{
		return (((3 - Facing) % 6) + 6) % 6;
	}

	/**
	 * 背击后弧的方向向量集合。
	 * §8.2.3 只写了"背面 2 个方向"未指明是哪两个 —— 偏移量放在 HexK 里，
	 * 试玩后可一键调整（含改成 3 方向对称后弧）。
	 */
	static void BackstabDirs(int32 Facing, TArray<FIntVector>& Out);

	/** 判断 Attacker 所在方向是否落在 Target 的背击后弧内。 */
	static bool IsBackstab(const FIntVector& AttackerCell, const FIntVector& TargetCell, int32 TargetFacing);

	// ───────────────────────────────────────────── 插值（视线用）

	/**
	 * 立方坐标线性插值 + 四舍五入。
	 * 修正误差最大的那一维，保证 x+y+z==0。
	 */
	static FIntVector CubeLerpRound(const FIntVector& A, const FIntVector& B, float T);

	/**
	 * 任意两格之间的连线（含 A 与 B），按从 A 到 B 的顺序。
	 *
	 * ⚠️ 与 Grid::CellsInLine 的区别很重要，别混用：
	 *      CellsInLine(origin, dirIndex, len) —— 沿【六轴之一】走 len 格，
	 *                                            方向必须是 6 个正方向
	 *      Line(A, B)                        —— 连接【任意】两格，
	 *                                            方向可以是斜的
	 *    冲撞、穿刺这类"冲向我点的那一格、沿途都吃到"的效果必须用后者：
	 *    前者会把斜向目标近似成某个正方向，落点与玩家点击的格子不一致 ——
	 *    在战棋里"落点和我点的不一样"是不可接受的。
	 *
	 * 纯坐标运算，不看地形。地形过滤由调用方负责。
	 */
	static void Line(const FIntVector& A, const FIntVector& B, TArray<FIntVector>& Out);

	// ───────────────────────────────────────────── 世界坐标映射（表现层隔离）

	/**
	 * offset (col,row) → 世界平面坐标（厘米）。尖顶六边形 odd-r 布局。
	 *
	 * 尖顶六边形：水平间距 = TileWidth，垂直间距 = TileHeight * 3/4，
	 * 奇数行右移半个 TileWidth。
	 *
	 * ⚠️ 只允许表现层的 HexBoardView 调用（CheckDiscipline 白名单强制）。
	 *    core 内部一律用 cube 坐标。
	 */
	static FVector2D OffsetToWorld2D(int32 Col, int32 Row);

	static FORCEINLINE FVector2D CubeToWorld2D(const FIntVector& Cube)
	{
		const FIntPoint O = CubeToOffset(Cube);
		return OffsetToWorld2D(O.X, O.Y);
	}

	/** 世界平面坐标 → 最近的格（用于鼠标拾取）。返回 cube 坐标。 */
	static FIntVector World2DToCube(const FVector2D& World);

	// ───────────────────────────────────────────── 调试

	static FString ToOffsetString(const FIntVector& Cube);
};
