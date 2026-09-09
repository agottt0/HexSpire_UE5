// Copyright Hex Spire. All Rights Reserved.
//
// 坐标系验证 —— 对应 Godot 版 tools/verify_hex.gd
//
// 验证的是【策划案 §8.1 / §8.2.4 的原文约定】，不是"代码自己跟自己一致"。
// 出生表交叉验证是关键：它用一个完全独立的数据来源（策划案表格）
// 来钉住 Rotate() 的正确性。

#include "Verify/HexVerify.h"
#include "HexSpireCore.h"
#include "Hex/HexCoord.h"
#include "Core/HexSpireConstants.h"

bool FHexVerifySuites::VerifyHex(FHexVerifyContext& Ctx)
{
	// ─────────────────────────────── 1. offset ↔ cube 全格往返
	Ctx.Section(TEXT("offset ↔ cube 往返（全 49 格）"));
	{
		bool bAllOk = true;
		FString FirstBad;
		for (int32 Row = 1; Row <= FHexCoord::Rows; ++Row)
		{
			for (int32 Col = 1; Col <= FHexCoord::Cols; ++Col)
			{
				const FIntVector Cube = FHexCoord::OffsetToCube(Col, Row);
				const FIntPoint Back = FHexCoord::CubeToOffset(Cube);
				if (Back.X != Col || Back.Y != Row)
				{
					bAllOk = false;
					if (FirstBad.IsEmpty())
					{
						FirstBad = FString::Printf(
							TEXT("(%d,%d) → cube(%d,%d,%d) → (%d,%d)"),
							Col, Row, Cube.X, Cube.Y, Cube.Z, Back.X, Back.Y);
					}
				}
			}
		}
		Ctx.Check(TEXT("全 49 格往返一致"), bAllOk, FirstBad);
	}

	// ─────────────────────────────── 2. 立方坐标不变式 x+y+z==0
	Ctx.Section(TEXT("立方坐标不变式"));
	{
		bool bAllOk = true;
		for (int32 Row = 1; Row <= FHexCoord::Rows; ++Row)
		{
			for (int32 Col = 1; Col <= FHexCoord::Cols; ++Col)
			{
				const FIntVector C = FHexCoord::OffsetToCube(Col, Row);
				if (C.X + C.Y + C.Z != 0)
				{
					bAllOk = false;
					break;
				}
			}
		}
		Ctx.Check(TEXT("所有格满足 x+y+z==0"), bAllOk);
	}

	// ─────────────────────────────── 3. 旋转闭合性：旋转 6 次回到原点
	Ctx.Section(TEXT("旋转闭合性"));
	{
		bool bAllOk = true;
		FString FirstBad;
		// 用若干个非零向量测试
		const FIntVector Samples[] = {
			FIntVector(1, -1, 0), FIntVector(2, -1, -1), FIntVector(0, 2, -2),
			FIntVector(-3, 1, 2), FIntVector(1, 0, -1),
		};
		for (const FIntVector& V : Samples)
		{
			const FIntVector R6 = FHexCoord::Rotate(V, 6);
			if (R6 != V)
			{
				bAllOk = false;
				FirstBad = FString::Printf(TEXT("(%d,%d,%d) 旋转6次得到 (%d,%d,%d)"),
					V.X, V.Y, V.Z, R6.X, R6.Y, R6.Z);
				break;
			}
			// 旋转保持不变式与长度
			for (int32 S = 0; S < 6; ++S)
			{
				const FIntVector RS = FHexCoord::Rotate(V, S);
				if (RS.X + RS.Y + RS.Z != 0)
				{
					bAllOk = false;
					FirstBad = TEXT("旋转后破坏 x+y+z==0");
					break;
				}
				if (FHexCoord::Distance(RS, FIntVector::ZeroValue)
					!= FHexCoord::Distance(V, FIntVector::ZeroValue))
				{
					bAllOk = false;
					FirstBad = TEXT("旋转后距离原点的距离改变");
					break;
				}
			}
		}
		Ctx.Check(TEXT("旋转 6 次闭合 + 保持不变式与长度"), bAllOk, FirstBad);
	}

	// ─────────────────────────────── 4. ⭐ 出生表交叉验证（策划案 §8.2.4）
	//
	// 这是最重要的一条：用策划案原文给出的独立数据钉住 Rotate() 的语义。
	// 若 Rotate 公式被"优化"错，这条会立刻失败。
	Ctx.Section(TEXT("出生表交叉验证（§8.2.4，facing=2 朝上）"));
	{
		const FIntVector Anchor = FHexCoord::OffsetToCube(
			HexK::HeroSpawnCol, HexK::HeroSpawnRow); // (4,1)
		const int32 Facing = HexK::HeroSpawnFacing;  // 2

		// M 体型 footprint（策划案 §8.2.1）
		const FIntVector MFoot[] = {
			FIntVector(0, 0, 0), FIntVector(1, -1, 0), FIntVector(1, 0, -1),
		};
		// 策划案 §8.2.4 期望：(4,1) + 2 格（朝上方向的三角形）
		TArray<FIntPoint> MActual;
		for (const FIntVector& Off : MFoot)
		{
			MActual.Add(FHexCoord::CubeToOffset(Anchor + FHexCoord::Rotate(Off, Facing)));
		}
		// 期望集合：{(4,1),(3,2),(4,2)}
		const bool bMOk =
			MActual.Contains(FIntPoint(4, 1)) &&
			MActual.Contains(FIntPoint(3, 2)) &&
			MActual.Contains(FIntPoint(4, 2)) &&
			MActual.Num() == 3;

		FString MDetail;
		for (const FIntPoint& P : MActual)
		{
			MDetail += FString::Printf(TEXT("(%d,%d) "), P.X, P.Y);
		}
		Ctx.Check(TEXT("M 体型在 (4,1) facing=2 占 {(4,1),(3,2),(4,2)}"), bMOk,
			FString::Printf(TEXT("实际占格: %s"), *MDetail));

		// L 体型
		const FIntVector LFoot[] = {
			FIntVector(0, 0, 0), FIntVector(1, -1, 0), FIntVector(1, 0, -1),
			FIntVector(2, -2, 0), FIntVector(2, -1, -1), FIntVector(2, 0, -2),
		};
		TArray<FIntPoint> LActual;
		bool bLAllInBounds = true;
		for (const FIntVector& Off : LFoot)
		{
			const FIntVector C = Anchor + FHexCoord::Rotate(Off, Facing);
			LActual.Add(FHexCoord::CubeToOffset(C));
			if (!FHexCoord::InBounds(C))
			{
				bLAllInBounds = false;
			}
		}
		// 策划案期望：{(4,1),(3,2),(4,2),(3,3),(4,3),(5,3)}
		const bool bLOk =
			LActual.Contains(FIntPoint(4, 1)) &&
			LActual.Contains(FIntPoint(3, 2)) &&
			LActual.Contains(FIntPoint(4, 2)) &&
			LActual.Contains(FIntPoint(3, 3)) &&
			LActual.Contains(FIntPoint(4, 3)) &&
			LActual.Contains(FIntPoint(5, 3)) &&
			LActual.Num() == 6;

		FString LDetail;
		for (const FIntPoint& P : LActual)
		{
			LDetail += FString::Printf(TEXT("(%d,%d) "), P.X, P.Y);
		}
		Ctx.Check(TEXT("L 体型在 (4,1) facing=2 占策划案指定 6 格"), bLOk,
			FString::Printf(TEXT("实际占格: %s"), *LDetail));
		Ctx.Check(TEXT("L 体型出生占格全部在 7×7 内"), bLAllInBounds);

		// footprint 不自重叠（旋转正确性的必要条件）
		bool bNoOverlap = true;
		for (int32 F = 0; F < 6; ++F)
		{
			TSet<FIntVector> Seen;
			for (const FIntVector& Off : LFoot)
			{
				const FIntVector C = FHexCoord::Rotate(Off, F);
				if (Seen.Contains(C))
				{
					bNoOverlap = false;
					break;
				}
				Seen.Add(C);
			}
		}
		Ctx.Check(TEXT("L footprint 在 6 个朝向下均不自重叠"), bNoOverlap);
	}

	// ─────────────────────────────── 5. ⚠️ 陷阱 H1：FacingDir 语义
	//
	// facing=2 应该"朝上"（row 递增）。若直接用 Dirs[2] 会得到朝下。
	Ctx.Section(TEXT("陷阱 H1：FacingDir 不等于 Dirs[facing]"));
	{
		const FIntVector Center = FHexCoord::OffsetToCube(4, 4);
		const FIntVector Ahead = Center + FHexCoord::FacingDir(2);
		const FIntPoint AheadO = FHexCoord::CubeToOffset(Ahead);
		Ctx.Check(TEXT("facing=2 的前方 row 递增（朝上）"),
			AheadO.Y > 4,
			FString::Printf(TEXT("从 (4,4) 朝 facing=2 走一步到 (%d,%d)"), AheadO.X, AheadO.Y));

		// 反向验证陷阱确实存在：Dirs[2] 应该朝下（row 递减）
		const FIntVector Naive = Center + FHexCoord::Dirs[2];
		const FIntPoint NaiveO = FHexCoord::CubeToOffset(Naive);
		Ctx.Check(TEXT("Dirs[2] 确实朝下（证明陷阱存在，不可直接索引）"),
			NaiveO.Y < 4,
			FString::Printf(TEXT("Dirs[2] 从 (4,4) 走到 (%d,%d)"), NaiveO.X, NaiveO.Y));

		// FacingDir 六个朝向全部是单位向量（距离 1）
		bool bAllUnit = true;
		for (int32 F = 0; F < 6; ++F)
		{
			if (FHexCoord::Distance(FHexCoord::FacingDir(F), FIntVector::ZeroValue) != 1)
			{
				bAllUnit = false;
				break;
			}
		}
		Ctx.Check(TEXT("FacingDir 六朝向均为单位向量"), bAllUnit);

		// 六个朝向的方向向量互不相同
		TSet<FIntVector> DirSet;
		for (int32 F = 0; F < 6; ++F)
		{
			DirSet.Add(FHexCoord::FacingDir(F));
		}
		Ctx.CheckEqual(TEXT("FacingDir 六朝向互不相同"), DirSet.Num(), 6);
	}

	// ─────────────────────────────── 6. ⚠️ 陷阱 H2：MapYFlip 奇偶性
	Ctx.Section(TEXT("陷阱 H2：MapYFlip 必须为偶数"));
	{
		Ctx.Check(TEXT("MapYFlip 是偶数"),
			(FHexCoord::MapYFlip % 2) == 0,
			FString::Printf(TEXT("MapYFlip=%d"), FHexCoord::MapYFlip));

		// 奇偶性保持：K - row 与 row 同奇偶
		bool bParityOk = true;
		for (int32 Row = 1; Row <= FHexCoord::Rows; ++Row)
		{
			const int32 Flipped = FHexCoord::MapYFlip - Row;
			if ((Flipped & 1) != (Row & 1))
			{
				bParityOk = false;
				break;
			}
		}
		Ctx.Check(TEXT("翻转后 row 奇偶性不变（odd-r 渲染正确的前提）"), bParityOk);
	}

	// ─────────────────────────────── 7. 距离度量
	Ctx.Section(TEXT("距离度量"));
	{
		const FIntVector A = FHexCoord::OffsetToCube(4, 1);
		Ctx.CheckEqual(TEXT("自身距离为 0"), FHexCoord::Distance(A, A), 0);

		// 相邻格距离恒为 1
		bool bNeighborOk = true;
		for (int32 I = 0; I < 6; ++I)
		{
			if (FHexCoord::Distance(A, A + FHexCoord::Dirs[I]) != 1)
			{
				bNeighborOk = false;
				break;
			}
		}
		Ctx.Check(TEXT("六个相邻格距离均为 1"), bNeighborOk);

		// 对称性
		const FIntVector B = FHexCoord::OffsetToCube(1, 7);
		Ctx.CheckEqual(TEXT("距离对称"),
			FHexCoord::Distance(A, B), FHexCoord::Distance(B, A));

		// 7×7 内的最大距离（对角）
		const int32 MaxD = FHexCoord::Distance(
			FHexCoord::OffsetToCube(1, 1), FHexCoord::OffsetToCube(7, 7));
		Ctx.Check(TEXT("(1,1)→(7,7) 距离在合理范围 [6,9]"),
			MaxD >= 6 && MaxD <= 9,
			FString::Printf(TEXT("实际=%d"), MaxD));
	}

	// ─────────────────────────────── 8. 背击后弧
	Ctx.Section(TEXT("背击后弧"));
	{
		// 目标在 (4,4) 朝上(facing=2)，攻击者在其正下方 → 应判定背击
		const FIntVector Target = FHexCoord::OffsetToCube(4, 4);
		const int32 TargetFacing = 2;

		const FIntVector RearDir = FHexCoord::Dirs[FHexCoord::RearDirIndex(TargetFacing)];
		const FIntVector BehindCell = Target + RearDir;
		Ctx.Check(TEXT("正后方格判定为背击"),
			FHexCoord::IsBackstab(BehindCell, Target, TargetFacing),
			FString::Printf(TEXT("攻击者 %s vs 目标 %s facing=%d"),
				*FHexCoord::ToOffsetString(BehindCell),
				*FHexCoord::ToOffsetString(Target), TargetFacing));

		// 正前方不应判定背击
		const FIntVector FrontCell = Target + FHexCoord::FacingDir(TargetFacing);
		Ctx.Check(TEXT("正前方格不判定为背击"),
			!FHexCoord::IsBackstab(FrontCell, Target, TargetFacing));

		// 后弧方向数量符合常量配置
		TArray<FIntVector> Rear;
		FHexCoord::BackstabDirs(TargetFacing, Rear);
		Ctx.CheckEqual(TEXT("后弧方向数量等于配置"),
			Rear.Num(), HexK::BackstabRearOffsetCount);

		// 六个朝向下，背击格数量一致（对称性）
		bool bSymmetric = true;
		int32 FirstCount = -1;
		for (int32 F = 0; F < 6; ++F)
		{
			int32 Count = 0;
			for (int32 I = 0; I < 6; ++I)
			{
				if (FHexCoord::IsBackstab(Target + FHexCoord::Dirs[I], Target, F))
				{
					++Count;
				}
			}
			if (FirstCount < 0)
			{
				FirstCount = Count;
			}
			else if (Count != FirstCount)
			{
				bSymmetric = false;
				break;
			}
		}
		Ctx.Check(TEXT("六朝向的背击格数量一致（旋转对称）"), bSymmetric,
			FString::Printf(TEXT("首个朝向背击格数=%d"), FirstCount));
	}

	// ─────────────────────────────── 9. 世界坐标往返（拾取正确性）
	Ctx.Section(TEXT("世界坐标往返（鼠标拾取）"));
	{
		bool bAllOk = true;
		FString FirstBad;
		for (int32 Row = 1; Row <= FHexCoord::Rows; ++Row)
		{
			for (int32 Col = 1; Col <= FHexCoord::Cols; ++Col)
			{
				const FIntVector Cube = FHexCoord::OffsetToCube(Col, Row);
				const FVector2D W = FHexCoord::CubeToWorld2D(Cube);
				const FIntVector Back = FHexCoord::World2DToCube(W);
				if (Back != Cube)
				{
					bAllOk = false;
					if (FirstBad.IsEmpty())
					{
						FirstBad = FString::Printf(
							TEXT("(%d,%d) → world(%.1f,%.1f) → %s"),
							Col, Row, W.X, W.Y, *FHexCoord::ToOffsetString(Back));
					}
				}
			}
		}
		Ctx.Check(TEXT("格心世界坐标能反解回同一格"), bAllOk, FirstBad);

		// 相邻格的世界距离应大致等于 tile 宽度量级（不能重叠也不能有缝）
		const FVector2D C44 = FHexCoord::CubeToWorld2D(FHexCoord::OffsetToCube(4, 4));
		const FVector2D C54 = FHexCoord::CubeToWorld2D(FHexCoord::OffsetToCube(5, 4));
		const float HDist = FVector2D::Distance(C44, C54);
		Ctx.CheckNearlyEqual(TEXT("同行相邻格水平间距 = TileWidth"),
			HDist, HexK::TileWidth, 0.5f);
	}

	// ─────────────────────────────── 10. 立方插值（视线取样）
	Ctx.Section(TEXT("立方插值"));
	{
		const FIntVector A = FHexCoord::OffsetToCube(1, 1);
		const FIntVector B = FHexCoord::OffsetToCube(7, 7);
		Ctx.CheckEqualCoord(TEXT("插值 t=0 得到起点"),
			FHexCoord::CubeLerpRound(A, B, 0.0f), A);
		Ctx.CheckEqualCoord(TEXT("插值 t=1 得到终点"),
			FHexCoord::CubeLerpRound(A, B, 1.0f), B);

		// 所有插值结果满足不变式
		bool bInvariantOk = true;
		for (int32 I = 0; I <= 10; ++I)
		{
			const FIntVector S = FHexCoord::CubeLerpRound(A, B, I / 10.0f);
			if (S.X + S.Y + S.Z != 0)
			{
				bInvariantOk = false;
				break;
			}
		}
		Ctx.Check(TEXT("插值结果全部满足 x+y+z==0"), bInvariantOk);
	}

	return Ctx.NumFailed() == 0;
}
