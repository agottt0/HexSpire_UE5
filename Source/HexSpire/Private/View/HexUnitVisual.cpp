// Copyright Hex Spire. All Rights Reserved.

#include "View/HexUnitVisual.h"
#include "View/HexBoardVisual.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "Battle/HexUnit.h"
#include "Hex/HexCoord.h"
#include "Core/HexSpireConstants.h"

namespace
{
	/** 引擎自带的圆柱：默认 100×100×100，中心在原点 */
	const TCHAR* CylinderPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const TCHAR* CubePath = TEXT("/Engine/BasicShapes/Cube.Cube");

	/**
	 * 引擎自带的基础材质。
	 * ⚠️ 用 BasicShapeMaterial 而不是自建材质 —— 它支持 Color 参数，
	 *    可以做成动态材质实例改颜色，且【不需要创建任何 .uasset】。
	 */
	const TCHAR* BasicMatPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
}

AHexUnitVisual::AHexUnitVisual()
{
	PrimaryActorTick.bCanEverTick = false;

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	RootComponent = BodyMesh;
	// 鼠标拾取走"射线 × 地面平面"，单位不需要碰撞
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	FacingMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Facing"));
	FacingMesh->SetupAttachment(RootComponent);
	FacingMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	HealthBarBackMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HealthBack"));
	HealthBarBackMesh->SetupAttachment(RootComponent);
	HealthBarBackMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	HealthBarMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HealthBar"));
	HealthBarMesh->SetupAttachment(RootComponent);
	HealthBarMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(CylinderPath);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(CubePath);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMat(BasicMatPath);

	if (Cylinder.Succeeded())
	{
		BodyMesh->SetStaticMesh(Cylinder.Object);
	}
	if (Cube.Succeeded())
	{
		FacingMesh->SetStaticMesh(Cube.Object);
		HealthBarMesh->SetStaticMesh(Cube.Object);
		HealthBarBackMesh->SetStaticMesh(Cube.Object);
	}

	if (BasicMat.Succeeded())
	{
		BodyMaterial = UMaterialInstanceDynamic::Create(BasicMat.Object, this);
		FacingMaterial = UMaterialInstanceDynamic::Create(BasicMat.Object, this);
		HealthMaterial = UMaterialInstanceDynamic::Create(BasicMat.Object, this);
		HealthBackMaterial = UMaterialInstanceDynamic::Create(BasicMat.Object, this);

		BodyMesh->SetMaterial(0, BodyMaterial);
		FacingMesh->SetMaterial(0, FacingMaterial);
		HealthBarMesh->SetMaterial(0, HealthMaterial);
		HealthBarBackMesh->SetMaterial(0, HealthBackMaterial);

		if (FacingMaterial)
		{
			FacingMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor::White);
		}
		if (HealthMaterial)
		{
			HealthMaterial->SetVectorParameterValue(
				TEXT("Color"), FLinearColor(0.15f, 0.85f, 0.25f));
		}
		if (HealthBackMaterial)
		{
			HealthBackMaterial->SetVectorParameterValue(
				TEXT("Color"), FLinearColor(0.08f, 0.02f, 0.02f));
		}
	}
}

void AHexUnitVisual::SizeForClass(EHexSizeClass SizeClass, float& OutRadius, float& OutHeight)
{
	// ⚠️ 体型必须【一眼可辨】（D8 的全部机制都建立在体型差异上）。
	//    半径按 footprint 覆盖范围给：
	//      S 占 1 格 → 略小于一格
	//      M 占 3 格（三角形）→ 约 1.6 格宽
	//      L 占 6 格 → 约 2.4 格宽
	//    高度差比半径差更夸张，因为俯视视角下高度差比宽度差更显眼。
	switch (SizeClass)
	{
	case EHexSizeClass::M:
		OutRadius = HexK::TileWidth * 0.80f;
		OutHeight = 210.0f;
		break;
	case EHexSizeClass::L:
		OutRadius = HexK::TileWidth * 1.20f;
		OutHeight = 300.0f;
		break;
	case EHexSizeClass::S:
	default:
		OutRadius = HexK::TileWidth * 0.36f;
		OutHeight = 140.0f;
		break;
	}
}

FLinearColor AHexUnitVisual::ColorForTeam(EHexTeam Team, bool bIsElite, bool bIsBoss)
{
	if (Team == EHexTeam::Player)
	{
		// 镇妖者：青灰道袍（美术文档的东方怪谈基调）
		return FLinearColor(0.35f, 0.62f, 0.72f);
	}

	// 敌人按威胁等级分色 —— 玩家进房时要能立刻判断"这间房难不难"
	if (bIsBoss)
	{
		return FLinearColor(0.75f, 0.10f, 0.35f);   // 深红紫：Boss
	}
	if (bIsElite)
	{
		return FLinearColor(0.85f, 0.45f, 0.10f);   // 橙：精英
	}
	return FLinearColor(0.60f, 0.25f, 0.25f);       // 暗红：杂兵
}

void AHexUnitVisual::SyncFromUnit(const FHexUnit& Unit, const AHexBoardVisual& Board)
{
	UnitId = Unit.Id;

	float Radius = 0.0f;
	float Height = 0.0f;
	SizeForClass(Unit.SizeClass, Radius, Height);

	// ── 位置：footprint 的几何中心
	//
	// ⚠️ 不能直接用 Anchor —— M/L 体型的 anchor 是 footprint 的一个角，
	//    把模型放在 anchor 上会让它明显偏出自己占的格子。
	//    §8.2.1 的 footprint 是三角形，几何中心才是视觉正确的位置。
	TArray<FIntVector> Cells;
	Unit.GetCells(Cells);

	FVector Sum = FVector::ZeroVector;
	for (const FIntVector& C : Cells)
	{
		Sum += Board.CellToWorld(C);
	}
	const FVector Center = Cells.Num() > 0
		? Sum / static_cast<float>(Cells.Num())
		: Board.CellToWorld(Unit.Anchor);

	SetActorLocation(Center + FVector(0, 0, Height * 0.5f));

	// ── 身体缩放
	//    引擎 Cylinder 默认是 100×100×100（直径 100、高 100）
	if (!bInitialized || CachedSize != Unit.SizeClass)
	{
		BodyMesh->SetRelativeScale3D(FVector(
			Radius * 2.0f / 100.0f,
			Radius * 2.0f / 100.0f,
			Height / 100.0f));
		CachedSize = Unit.SizeClass;
		bInitialized = true;
	}

	// ── 颜色
	if (BodyMaterial)
	{
		FLinearColor C = ColorForTeam(Unit.Team, Unit.bIsElite, Unit.bIsBoss);

		// ⚠️ 受到 debuff 时偏色 —— 灰盒期没有状态图标，
		//    但"这个敌人身上有 debuff"必须能看出来，否则玩家算不清伤害。
		if (Unit.GetDamageTakenMultiplier() > 0.01f)
		{
			// 易伤：偏紫
			C = FMath::Lerp(C, FLinearColor(0.85f, 0.20f, 0.85f), 0.35f);
		}
		if (Unit.GetDamageDealtMultiplier() < -0.01f)
		{
			// 虚弱：偏灰
			C = FMath::Lerp(C, FLinearColor(0.35f, 0.35f, 0.35f), 0.35f);
		}
		if (Unit.ShouldSkipTurn())
		{
			// 眩晕：偏黄（这是玩家最需要看到的状态 ——
			// 它意味着这个敌人本回合不会行动，威胁评估完全不同）
			C = FMath::Lerp(C, FLinearColor(0.95f, 0.90f, 0.20f), 0.55f);
		}

		BodyMaterial->SetVectorParameterValue(TEXT("Color"), C);
	}

	// ── 朝向指示器：从中心朝 FacingDir 前伸
	//
	// ⚠️ 必须走 FHexCoord::FacingDir()，不能用 Dirs[Facing]（陷阱 H1）。
	{
		const FIntVector Dir = FHexCoord::FacingDir(Unit.Facing);
		// 用"anchor 的邻格方向"换算世界方向
		const FVector2D From = FHexCoord::CubeToWorld2D(Unit.Anchor);
		const FVector2D To = FHexCoord::CubeToWorld2D(Unit.Anchor + Dir);
		FVector2D D2 = To - From;
		if (!D2.IsNearlyZero())
		{
			D2.Normalize();
		}

		const float Reach = Radius * 1.05f;
		FacingMesh->SetRelativeLocation(FVector(
			D2.X * Reach, D2.Y * Reach, Height * 0.30f));
		// 细长的小方块，像一个"鼻子"
		FacingMesh->SetRelativeScale3D(FVector(0.22f, 0.22f, 0.14f));
	}

	// ── 生命条
	{
		const float Ratio = Unit.HPMax > 0
			? FMath::Clamp(static_cast<float>(Unit.HP) / Unit.HPMax, 0.0f, 1.0f)
			: 0.0f;

		const float BarWidth = FMath::Max(Radius * 2.0f, 80.0f);
		const float BarZ = Height * 0.5f + 40.0f;

		HealthBarBackMesh->SetRelativeLocation(FVector(0, 0, BarZ));
		HealthBarBackMesh->SetRelativeScale3D(FVector(BarWidth / 100.0f, 0.14f, 0.06f));

		// 前景条按比例缩短，并左移保持左对齐
		HealthBarMesh->SetRelativeScale3D(
			FVector(BarWidth * Ratio / 100.0f, 0.16f, 0.08f));
		HealthBarMesh->SetRelativeLocation(FVector(
			-BarWidth * 0.5f * (1.0f - Ratio), 0, BarZ + 1.0f));

		if (HealthMaterial)
		{
			// 血量越低越红 —— 这是"还能撑几回合"的最快读数
			const FLinearColor Full(0.15f, 0.85f, 0.25f);
			const FLinearColor Low(0.90f, 0.15f, 0.10f);
			HealthMaterial->SetVectorParameterValue(
				TEXT("Color"), FMath::Lerp(Low, Full, Ratio));
		}

		// ── 格挡显示
		//
		// ⚠️ 格挡必须与生命分开显示（§4.4 ⑦：格挡先扣、溢出打 HP）。
		//    混在一条里玩家算不出"这一击会不会破防"。
		//    灰盒期用生命条背景变蓝表示有格挡。
		if (HealthBackMaterial)
		{
			const FLinearColor NoBlock(0.08f, 0.02f, 0.02f);
			const FLinearColor HasBlock(0.20f, 0.45f, 0.85f);
			const float BlockRatio = Unit.HPMax > 0
				? FMath::Clamp(static_cast<float>(Unit.Block) / Unit.HPMax, 0.0f, 1.0f)
				: 0.0f;
			HealthBackMaterial->SetVectorParameterValue(
				TEXT("Color"), FMath::Lerp(NoBlock, HasBlock, FMath::Min(1.0f, BlockRatio * 3.0f)));
		}
	}

	if (!Unit.bIsAlive)
	{
		SetDead();
	}
}

void AHexUnitVisual::SetDead()
{
	// 压扁 + 变暗。灰盒期不做死亡动画，但"这个单位已经死了"必须明确。
	const FVector S = BodyMesh->GetRelativeScale3D();
	BodyMesh->SetRelativeScale3D(FVector(S.X, S.Y, 0.08f));

	if (BodyMaterial)
	{
		BodyMaterial->SetVectorParameterValue(
			TEXT("Color"), FLinearColor(0.10f, 0.08f, 0.08f));
	}

	FacingMesh->SetVisibility(false);
	HealthBarMesh->SetVisibility(false);
	HealthBarBackMesh->SetVisibility(false);
}
