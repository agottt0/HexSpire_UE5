// Copyright Hex Spire. All Rights Reserved.
//
// 六边形棋盘的可视化 —— 表现层
//
// ══════════════════════════════════════════════════════════════════
// 纪律 3（策划案 §12.1）：表现层只读逻辑、只发输入，绝不驱动逻辑
// ══════════════════════════════════════════════════════════════════
// 本 Actor 做三件事：
//   ① 按 FHexGrid 生成六边形地面（一次性）
//   ② 按地形类型着色（地面/墙/深坑/碎石/出口/危害）
//   ③ 画高亮（合法目标 / 波及范围 / 敌人意图 / 移动落点）
// 它【不】修改任何逻辑状态。
//
// ⚠️ 为什么用 ProceduralMesh 而不是 StaticMesh 资产：
//    tile 尺寸（HexK::TileWidth / TileHeight）还在调，且六边形的朝向
//    （尖顶 vs 平顶）与坐标系强绑定。做成 .uasset 后每次改都要
//    开编辑器重导，而且我（AI）无法编辑二进制资产。
//    程序化生成让"改常量 → 重编译 → 立刻看到"成为一步操作。
//
// ⚠️ 为什么按颜色分 section 而不是用顶点色：
//    UE 5.8 的引擎内容里【没有】读顶点色的现成材质
//    （VertexColorViewMode_* 只存在于旧版本/编辑器专用路径）。
//    第一版用顶点色，结果 CDO 构造时报
//    "Failed to find /Engine/EngineMaterials/VertexColorViewMode_ColorOnly"，
//    材质为空 → 棋盘用默认材质渲染成一片纯白，所有地形色全部丢失。
//    现在改成"每种颜色一个 mesh section + 一个动态材质实例"，
//    只依赖 BasicShapeMaterial（引擎必有），且颜色数量有限（<15），
//    section 开销可以忽略。

#include "View/HexBoardVisual.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Hex/HexGrid.h"
#include "Hex/HexCoord.h"
#include "Content/HexLayouts.h"
#include "Core/HexSpireConstants.h"
#include "Engine/World.h"

namespace
{
	/**
	 * 尖顶六边形的外接圆半径。
	 *
	 * 尖顶（pointy-top）六边形：宽 = √3 × R，高 = 2R。
	 * HexK::TileWidth 是"宽"，所以 R = Width / √3。
	 * 代入 TileWidth=200 → R ≈ 115.47，高 = 230.94 = HexK::TileHeight ✓
	 * 两者自洽，说明常量没写错。
	 */
	constexpr float HexRadius = HexK::TileWidth / 1.7320508f;

	/** 地面厚度（纯视觉，让棋盘有立体感而不是一张纸） */
	constexpr float GroundThickness = 12.0f;

	/** 格子之间留的缝隙比例 —— 没有缝隙时六边形边界看不清 */
	constexpr float GapScale = 0.94f;

	/** 引擎必有的基础材质，带 Color 向量参数 */
	const TCHAR* BasicMatPath =
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

	/** 一组同色的几何数据 */
	struct FColorBatch
	{
		FLinearColor Color = FLinearColor::White;
		TArray<FVector> Verts;
		TArray<int32> Tris;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<FLinearColor> VertexColors;
	};

	/** 颜色量化成键，用于分组（避免浮点误差导致同色分成两组） */
	uint32 ColorKey(const FLinearColor& C)
	{
		const uint8 R = static_cast<uint8>(FMath::Clamp(C.R, 0.0f, 1.0f) * 255.0f);
		const uint8 G = static_cast<uint8>(FMath::Clamp(C.G, 0.0f, 1.0f) * 255.0f);
		const uint8 B = static_cast<uint8>(FMath::Clamp(C.B, 0.0f, 1.0f) * 255.0f);
		return (static_cast<uint32>(R) << 16) | (static_cast<uint32>(G) << 8) | B;
	}
}

AHexBoardVisual::AHexBoardVisual()
{
	PrimaryActorTick.bCanEverTick = false;

	GroundMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GroundMesh"));
	RootComponent = GroundMesh;
	GroundMesh->bUseAsyncCooking = false;
	// 棋盘不需要碰撞：鼠标拾取走"射线与棋盘平面求交"，
	// 比逐格碰撞快且不会漏点（缝隙处不会拾取失败）
	GroundMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	HighlightMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("HighlightMesh"));
	HighlightMesh->SetupAttachment(RootComponent);
	HighlightMesh->bUseAsyncCooking = false;
	HighlightMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMat(BasicMatPath);
	if (BasicMat.Succeeded())
	{
		BaseMaterial = BasicMat.Object;
	}
}

void AHexBoardVisual::BeginPlay()
{
	Super::BeginPlay();
}

void AHexBoardVisual::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// ⚠️ 只在编辑器里预览。
	//    运行时由 GameMode 用【真实房间地形】调 BuildFromGrid()，
	//    若这里也建一次会白白重建一遍网格。
	if (GetWorld() && !GetWorld()->IsGameWorld())
	{
		BuildPreview();
	}
}

void AHexBoardVisual::BuildPreview()
{
	// 用一份临时网格生成预览 —— 不触碰任何逻辑状态（纪律 3）
	FHexGrid Preview;
	FHexLayouts::Build(PreviewLayoutId, Preview);
	BuildFromGrid(Preview);

	// 预览也画一圈高亮，这样能立刻看出：
	//   ① 高亮的尺寸与地面是否对齐
	//   ② 玩家/敌人出生区在哪（摆相机时要照顾到）
	ClearHighlights();
	{
		const FIntVector HeroCell =
			FHexCoord::OffsetToCube(HexK::HeroSpawnCol, HexK::HeroSpawnRow);
		AddHighlight(HeroCell, EHexHighlight::SelfFootprint);

		for (int32 Row = HexK::EnemySpawnRowMin; Row <= HexK::EnemySpawnRowMax; ++Row)
		{
			for (int32 Col = 1; Col <= HexK::BoardCols; ++Col)
			{
				AddHighlight(FHexCoord::OffsetToCube(Col, Row),
					EHexHighlight::IntentDodgeable);
			}
		}
	}
	CommitHighlights();
}

// ══════════════════════════════════════════════════════════ 几何生成

void AHexBoardVisual::AppendHexagon(
	const FVector2D& Center2D,
	float Radius,
	float ZHeight,
	TArray<FVector>& Verts,
	TArray<int32>& Tris,
	TArray<FVector>& Normals,
	TArray<FVector2D>& UVs) const
{
	const int32 Base = Verts.Num();

	// 中心点（扇形三角化的公共顶点）
	Verts.Add(FVector(Center2D.X, Center2D.Y, ZHeight));
	Normals.Add(FVector::UpVector);
	UVs.Add(FVector2D(0.5f, 0.5f));

	// ⚠️ 尖顶六边形：第一个顶点在正上方（角度 90°），然后每 60° 一个。
	//    若从 0° 开始就会得到平顶六边形，与 HexCoord 的 odd-r 布局
	//    不匹配（格子会明显错位重叠）。
	for (int32 I = 0; I < 6; ++I)
	{
		const float Angle = FMath::DegreesToRadians(90.0f + 60.0f * I);
		const float X = Center2D.X + Radius * FMath::Cos(Angle);
		const float Y = Center2D.Y + Radius * FMath::Sin(Angle);

		Verts.Add(FVector(X, Y, ZHeight));
		Normals.Add(FVector::UpVector);
		UVs.Add(FVector2D(
			0.5f + 0.5f * FMath::Cos(Angle),
			0.5f + 0.5f * FMath::Sin(Angle)));
	}

	// 扇形三角化：中心 + 相邻两个边缘点
	for (int32 I = 0; I < 6; ++I)
	{
		const int32 Next = (I + 1) % 6;
		Tris.Add(Base);
		Tris.Add(Base + 1 + Next);
		Tris.Add(Base + 1 + I);
	}
}

FLinearColor AHexBoardVisual::ColorForTerrain(
	EHexTerrain Terrain, EHexHazard Hazard, EHexFeature Feature)
{
	// ⚠️ 危害与地物优先于地形显示 —— 玩家最需要看到的是"这格会伤我"。
	//    §13.2 没有列这条，但灰盒期若地形色盖掉危害色，
	//    "把敌人推进尖刺"这个战术根本看不出来。
	if (Hazard != EHexHazard::None)
	{
		switch (Hazard)
		{
		case EHexHazard::Spikes: return FLinearColor(0.70f, 0.08f, 0.08f);
		case EHexHazard::Fire:   return FLinearColor(0.95f, 0.35f, 0.05f);
		case EHexHazard::Acid:   return FLinearColor(0.35f, 0.75f, 0.15f);
		case EHexHazard::Ash:    return FLinearColor(0.50f, 0.47f, 0.44f);
		default: break;
		}
	}

	if (Feature == EHexFeature::Pillar)
	{
		// 石柱阻断视线，必须显眼
		return FLinearColor(0.34f, 0.34f, 0.44f);
	}

	switch (Terrain)
	{
	case EHexTerrain::Wall:
		return FLinearColor(0.10f, 0.10f, 0.13f);
	case EHexTerrain::Pit:
		return FLinearColor(0.03f, 0.03f, 0.06f);
	case EHexTerrain::Rubble:
		return FLinearColor(0.46f, 0.38f, 0.28f);
	case EHexTerrain::ExitGate:
		return FLinearColor(0.15f, 0.80f, 0.90f);
	case EHexTerrain::Floor:
	default:
		// 地铁站台的水泥灰 —— 第一章黄泉线的基调（美术文档 §5）
		return FLinearColor(0.30f, 0.32f, 0.35f);
	}
}

void AHexBoardVisual::RebuildSections(
	UProceduralMeshComponent* Mesh,
	TArray<UMaterialInstanceDynamic*>& MaterialCache,
	const TArray<TPair<FLinearColor, TArray<FIntVector>>>& Groups,
	TFunctionRef<float(const FIntVector&, const FLinearColor&)> ZFunc,
	float RadiusScale)
{
	if (!Mesh)
	{
		return;
	}

	Mesh->ClearAllMeshSections();

	// 复用材质实例：每帧新建 MID 会让 GC 压力肉眼可见
	while (MaterialCache.Num() < Groups.Num())
	{
		UMaterialInstanceDynamic* MID = BaseMaterial
			? UMaterialInstanceDynamic::Create(BaseMaterial, this)
			: nullptr;
		MaterialCache.Add(MID);
	}

	for (int32 GI = 0; GI < Groups.Num(); ++GI)
	{
		const FLinearColor& Color = Groups[GI].Key;
		const TArray<FIntVector>& Cells = Groups[GI].Value;
		if (Cells.Num() == 0)
		{
			continue;
		}

		TArray<FVector> Verts;
		TArray<int32> Tris;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<FLinearColor> VColors;
		TArray<FProcMeshTangent> Tangents;

		Verts.Reserve(Cells.Num() * 7);
		Tris.Reserve(Cells.Num() * 18);

		for (const FIntVector& Cell : Cells)
		{
			const FVector2D Center = FHexCoord::CubeToWorld2D(Cell);
			AppendHexagon(Center, HexRadius * GapScale * RadiusScale,
				ZFunc(Cell, Color), Verts, Tris, Normals, UVs);
		}

		Mesh->CreateMeshSection_LinearColor(
			GI, Verts, Tris, Normals, UVs, VColors, Tangents,
			/*bCreateCollision=*/false);

		if (MaterialCache.IsValidIndex(GI) && MaterialCache[GI])
		{
			// BasicShapeMaterial 的向量参数名是 "Color"
			MaterialCache[GI]->SetVectorParameterValue(TEXT("Color"), Color);
			Mesh->SetMaterial(GI, MaterialCache[GI]);
		}
	}
}

void AHexBoardVisual::BuildFromGrid(const FHexGrid& Grid)
{
	// ── 按颜色分组
	TMap<uint32, int32> KeyToIndex;
	TArray<TPair<FLinearColor, TArray<FIntVector>>> Groups;

	// 记下每格的抬升/下沉，供 ZFunc 使用
	CellZ.Reset();

	for (const FIntVector& Cell : Grid.AllCells())
	{
		const EHexTerrain T = Grid.TerrainAt(Cell);
		const EHexFeature F = Grid.FeatureAt(Cell);
		const FLinearColor Color = ColorForTerrain(T, Grid.HazardAt(Cell), F);

		// 墙与石柱抬高，让它们在视觉上真的"挡住"；深坑下沉
		float Z = GroundThickness;
		if (T == EHexTerrain::Wall || F == EHexFeature::Pillar)
		{
			Z = GroundThickness + 95.0f;
		}
		else if (T == EHexTerrain::Pit)
		{
			Z = -65.0f;
		}
		CellZ.Add(Cell, Z);

		const uint32 Key = ColorKey(Color);
		int32* Found = KeyToIndex.Find(Key);
		if (!Found)
		{
			const int32 NewIndex = Groups.Num();
			Groups.Add(TPair<FLinearColor, TArray<FIntVector>>(
				Color, TArray<FIntVector>()));
			KeyToIndex.Add(Key, NewIndex);
			Found = KeyToIndex.Find(Key);
		}
		Groups[*Found].Value.Add(Cell);
	}

	RebuildSections(GroundMesh, GroundMaterials, Groups,
		[this](const FIntVector& Cell, const FLinearColor&) -> float
		{
			const float* Z = CellZ.Find(Cell);
			return Z ? *Z : GroundThickness;
		},
		/*RadiusScale=*/1.0f);
}

// ══════════════════════════════════════════════════════════ 高亮

FLinearColor AHexBoardVisual::ColorForHighlight(EHexHighlight Type)
{
	switch (Type)
	{
	case EHexHighlight::MoveTarget:
		return FLinearColor(0.15f, 0.45f, 0.95f);   // 蓝：敌人将移动到
	case EHexHighlight::LegalTarget:
		return FLinearColor(0.95f, 0.85f, 0.20f);   // 黄：我能点哪
	case EHexHighlight::AffectedArea:
		return FLinearColor(0.95f, 0.50f, 0.10f);   // 橙：会打到哪
	case EHexHighlight::IntentDodgeable:
		// ⚠️ §13.2 硬需求：可躲 vs 追踪必须一眼分清。
		//    亮红 = 可躲（走开就没事）
		return FLinearColor(0.95f, 0.15f, 0.15f);
	case EHexHighlight::IntentTracking:
		// 紫 = 追踪（躲不掉，得格挡或打断）
		return FLinearColor(0.72f, 0.15f, 0.88f);
	case EHexHighlight::Hover:
		return FLinearColor(1.0f, 1.0f, 1.0f);
	case EHexHighlight::SelfFootprint:
		return FLinearColor(0.20f, 0.90f, 0.55f);   // 绿：我占哪几格
	default:
		return FLinearColor::White;
	}
}

float AHexBoardVisual::ZForHighlight(EHexHighlight Type)
{
	// ⚠️ 不同层给不同高度，避免 z-fighting（闪烁）。
	//    顺序即优先级：越靠后画在越上面。
	switch (Type)
	{
	case EHexHighlight::SelfFootprint:   return GroundThickness + 2.0f;
	case EHexHighlight::MoveTarget:      return GroundThickness + 4.0f;
	case EHexHighlight::LegalTarget:     return GroundThickness + 6.0f;
	case EHexHighlight::AffectedArea:    return GroundThickness + 8.0f;
	case EHexHighlight::IntentDodgeable: return GroundThickness + 10.0f;
	case EHexHighlight::IntentTracking:  return GroundThickness + 10.0f;
	case EHexHighlight::Hover:           return GroundThickness + 12.0f;
	default:                             return GroundThickness + 2.0f;
	}
}

void AHexBoardVisual::ClearHighlights()
{
	PendingHighlights.Reset();
}

void AHexBoardVisual::AddHighlight(const TArray<FIntVector>& Cells, EHexHighlight Type)
{
	for (const FIntVector& C : Cells)
	{
		PendingHighlights.Add(TPair<FIntVector, EHexHighlight>(C, Type));
	}
}

void AHexBoardVisual::AddHighlight(const FIntVector& Cell, EHexHighlight Type)
{
	PendingHighlights.Add(TPair<FIntVector, EHexHighlight>(Cell, Type));
}

void AHexBoardVisual::CommitHighlights()
{
	if (!HighlightMesh)
	{
		return;
	}

	if (PendingHighlights.Num() == 0)
	{
		HighlightMesh->ClearAllMeshSections();
		return;
	}

	// 按高亮类型分组（类型数量固定且很少，直接按枚举顺序建组）
	TMap<uint8, TArray<FIntVector>> ByType;
	for (const TPair<FIntVector, EHexHighlight>& H : PendingHighlights)
	{
		ByType.FindOrAdd(static_cast<uint8>(H.Value)).AddUnique(H.Key);
	}

	// 按 Z 升序排列组，保证优先级高的画在上面
	TArray<uint8> Types;
	ByType.GetKeys(Types);
	Types.Sort([](const uint8& A, const uint8& B)
	{
		return ZForHighlight(static_cast<EHexHighlight>(A))
			< ZForHighlight(static_cast<EHexHighlight>(B));
	});

	TArray<TPair<FLinearColor, TArray<FIntVector>>> Groups;
	HighlightZ.Reset();

	for (const uint8 T : Types)
	{
		const EHexHighlight Type = static_cast<EHexHighlight>(T);
		const FLinearColor Color = ColorForHighlight(Type);
		const float Z = ZForHighlight(Type);

		for (const FIntVector& C : ByType[T])
		{
			// 同一格可能有多种高亮，取最高的 Z（优先级最高的那层）
			float* Existing = HighlightZ.Find(C);
			if (!Existing || Z > *Existing)
			{
				HighlightZ.Add(C, Z);
			}
		}

		Groups.Add(TPair<FLinearColor, TArray<FIntVector>>(Color, ByType[T]));
	}

	// 高亮比地面略小，这样能看到下面的地形色
	RebuildSections(HighlightMesh, HighlightMaterials, Groups,
		[this](const FIntVector& Cell, const FLinearColor&) -> float
		{
			// 站在抬高的格（墙）上的高亮也要抬高，否则会埋进墙里
			const float* GroundZ = CellZ.Find(Cell);
			const float Base = GroundZ ? *GroundZ : GroundThickness;
			const float* HZ = HighlightZ.Find(Cell);
			const float Offset = HZ ? (*HZ - GroundThickness) : 2.0f;
			return Base + Offset;
		},
		/*RadiusScale=*/0.80f);
}

// ══════════════════════════════════════════════════════════ 坐标转换

FIntVector AHexBoardVisual::WorldToCell(const FVector& WorldLocation) const
{
	// 转到本 Actor 的局部空间再交给 HexCoord（唯一转换出口）
	const FVector Local = GetActorTransform().InverseTransformPosition(WorldLocation);
	return FHexCoord::World2DToCube(FVector2D(Local.X, Local.Y));
}

FVector AHexBoardVisual::CellToWorld(const FIntVector& Cell, float ZOffset) const
{
	const FVector2D Local2D = FHexCoord::CubeToWorld2D(Cell);
	const FVector Local(Local2D.X, Local2D.Y, GroundThickness + ZOffset);
	return GetActorTransform().TransformPosition(Local);
}

FVector AHexBoardVisual::GetBoardCenter() const
{
	const FIntVector Center = FHexCoord::OffsetToCube(
		(HexK::BoardCols + 1) / 2, (HexK::BoardRows + 1) / 2);
	return CellToWorld(Center);
}
