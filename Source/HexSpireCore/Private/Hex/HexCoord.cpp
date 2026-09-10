// Copyright Hex Spire. All Rights Reserved.

#include "Hex/HexCoord.h"

const FIntVector FHexCoord::Dirs[6] = {
	FIntVector(1, -1, 0),
	FIntVector(1, 0, -1),
	FIntVector(0, 1, -1),
	FIntVector(-1, 1, 0),
	FIntVector(-1, 0, 1),
	FIntVector(0, -1, 1),
};

void FHexCoord::Neighbors(const FIntVector& Cube, TArray<FIntVector>& Out)
{
	Out.Reset(6);
	for (int32 I = 0; I < 6; ++I)
	{
		Out.Add(Cube + Dirs[I]);
	}
}

void FHexCoord::BackstabDirs(int32 Facing, TArray<FIntVector>& Out)
{
	const int32 Base = RearDirIndex(Facing);
	Out.Reset(HexK::BackstabRearOffsetCount);
	for (int32 I = 0; I < HexK::BackstabRearOffsetCount; ++I)
	{
		const int32 Idx = (((Base + HexK::BackstabRearOffsets[I]) % 6) + 6) % 6;
		Out.Add(Dirs[Idx]);
	}
}

bool FHexCoord::IsBackstab(const FIntVector& AttackerCell, const FIntVector& TargetCell, int32 TargetFacing)
{
	// 判据：攻击者位于目标的后弧方向上。
	// 用"目标 + 后弧方向"是否与"攻击者相对目标的方向"同向来判定 ——
	// 对非相邻的远程攻击也成立（取归一化的主方向）。
	const FIntVector Delta = AttackerCell - TargetCell;
	if (Delta == FIntVector::ZeroValue)
	{
		return false;
	}

	TArray<FIntVector> Rear;
	BackstabDirs(TargetFacing, Rear);

	// 把 Delta 投影到六个方向上，取点积最大者作为"主方向"。
	// 立方坐标下用 -(x*dx + y*dy + z*dz) 的变体：直接比较距离更稳。
	int32 BestDir = -1;
	int32 BestDist = TNumericLimits<int32>::Max();
	const int32 Dist = Distance(AttackerCell, TargetCell);
	for (int32 I = 0; I < 6; ++I)
	{
		// 沿方向 I 走 Dist 格后与攻击者的距离，越小说明越贴近该方向
		const FIntVector Probe = TargetCell + Dirs[I] * Dist;
		const int32 D = Distance(Probe, AttackerCell);
		if (D < BestDist)
		{
			BestDist = D;
			BestDir = I;
		}
	}

	if (BestDir < 0)
	{
		return false;
	}
	return Rear.Contains(Dirs[BestDir]);
}

FIntVector FHexCoord::CubeLerpRound(const FIntVector& A, const FIntVector& B, float T)
{
	const float X = FMath::Lerp(static_cast<float>(A.X), static_cast<float>(B.X), T);
	const float Y = FMath::Lerp(static_cast<float>(A.Y), static_cast<float>(B.Y), T);
	const float Z = FMath::Lerp(static_cast<float>(A.Z), static_cast<float>(B.Z), T);

	float RX = FMath::RoundToFloat(X);
	float RY = FMath::RoundToFloat(Y);
	float RZ = FMath::RoundToFloat(Z);

	const float DX = FMath::Abs(RX - X);
	const float DY = FMath::Abs(RY - Y);
	const float DZ = FMath::Abs(RZ - Z);

	// 修正误差最大的那一维，保证 x+y+z == 0
	if (DX > DY && DX > DZ)
	{
		RX = -RY - RZ;
	}
	else if (DY > DZ)
	{
		RY = -RX - RZ;
	}
	else
	{
		RZ = -RX - RY;
	}

	return FIntVector(
		static_cast<int32>(RX),
		static_cast<int32>(RY),
		static_cast<int32>(RZ));
}

void FHexCoord::Line(const FIntVector& A, const FIntVector& B, TArray<FIntVector>& Out)
{
	Out.Reset();

	const int32 N = Distance(A, B);
	if (N == 0)
	{
		Out.Add(A);
		return;
	}

	// ⚠️ 用 AddUnique 而不是 Add。
	//    正好穿过两格【交界】的连线（例如恰好 45° 斜穿）会让相邻两步
	//    round 到同一格，产生重复。重复格会让路径伤害对同一个敌人
	//    结算两次 —— 而 AffectedUnits 的去重是按【单位】去的，
	//    挡不住同一格重复出现导致的多次命中。
	for (int32 I = 0; I <= N; ++I)
	{
		const float T = static_cast<float>(I) / static_cast<float>(N);
		Out.AddUnique(CubeLerpRound(A, B, T));
	}
}

FVector2D FHexCoord::OffsetToWorld2D(int32 Col, int32 Row)
{
	// 尖顶(pointy-top) odd-r：
	//   水平间距 = TileWidth，奇数行右移 TileWidth/2
	//   垂直间距 = TileHeight * 3/4
	//
	// ⚠️ 这里用 (Row & 1) 判断奇偶。业务 row 是 1-indexed，
	//    row=1 为奇数行 → 右移。与 OffsetToCube 的 (r & 1)（0-indexed）
	//    错开一位是【有意的】：OffsetToCube 内部 r = Row-1，
	//    所以 row=1 → r=0 → 不移；这里 row=1 → 移。
	//    两者描述的是同一套几何，只是原点行的选择不同，
	//    由 VerifyHex 的往返测试保证一致性。
	const float XOffset = (Row & 1) ? (HexK::TileWidth * 0.5f) : 0.0f;
	const float WX = (Col - 1) * HexK::TileWidth + XOffset;
	const float WY = (Row - 1) * HexK::TileHeight * 0.75f;
	return FVector2D(WX, WY);
}

FIntVector FHexCoord::World2DToCube(const FVector2D& World)
{
	// 反解：先估算行，再估算列，然后在 3×3 邻域内取最近者。
	// 直接解析反解在六边形边界处会出错，邻域搜索是标准做法。
	const float ApproxRowF = World.Y / (HexK::TileHeight * 0.75f) + 1.0f;
	const int32 ApproxRow = FMath::RoundToInt(ApproxRowF);

	FIntVector Best = OffsetToCube(1, 1);
	float BestDistSq = TNumericLimits<float>::Max();

	for (int32 R = ApproxRow - 1; R <= ApproxRow + 1; ++R)
	{
		const float XOffset = (R & 1) ? (HexK::TileWidth * 0.5f) : 0.0f;
		const float ApproxColF = (World.X - XOffset) / HexK::TileWidth + 1.0f;
		const int32 ApproxCol = FMath::RoundToInt(ApproxColF);

		for (int32 C = ApproxCol - 1; C <= ApproxCol + 1; ++C)
		{
			const FVector2D Center = OffsetToWorld2D(C, R);
			const float DistSq = FVector2D::DistSquared(Center, World);
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				Best = OffsetToCube(C, R);
			}
		}
	}

	return Best;
}

FString FHexCoord::ToOffsetString(const FIntVector& Cube)
{
	const FIntPoint O = CubeToOffset(Cube);
	return FString::Printf(TEXT("(%d,%d)"), O.X, O.Y);
}
