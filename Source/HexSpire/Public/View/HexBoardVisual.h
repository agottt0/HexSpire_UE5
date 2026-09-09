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

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/HexSpireEnums.h"
#include "HexBoardVisual.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class FHexGrid;

/** 高亮类型。不同用途用不同颜色，§13.2 要求玩家一眼能分清。 */
UENUM()
enum class EHexHighlight : uint8
{
	/** 可移动到的落点（蓝） */
	MoveTarget = 0,
	/** 卡牌的合法目标（黄） */
	LegalTarget = 1,
	/** 卡牌的波及范围（橙） */
	AffectedArea = 2,
	/** 敌人意图：可躲（红实心）—— §13.2 要求与追踪型区分 */
	IntentDodgeable = 3,
	/** 敌人意图：追踪（紫）*/
	IntentTracking = 4,
	/** 鼠标悬停 */
	Hover = 5,
	/** 玩家单位的 footprint 描边（§13.2 硬需求 3） */
	SelfFootprint = 6,
};

UCLASS()
class HEXSPIRE_API AHexBoardVisual : public AActor
{
	GENERATED_BODY()

public:
	AHexBoardVisual();

	/**
	 * 按逻辑网格重建地面。
	 * 每场战斗开始时调用一次（地形不会在战斗中大量变化）。
	 */
	void BuildFromGrid(const FHexGrid& Grid);

	/** 清空全部高亮 */
	void ClearHighlights();

	/** 添加一组高亮格 */
	void AddHighlight(const TArray<FIntVector>& Cells, EHexHighlight Type);

	void AddHighlight(const FIntVector& Cell, EHexHighlight Type);

	/**
	 * 把高亮提交到渲染。
	 * ⚠️ 必须在一帧内 ClearHighlights → AddHighlight×N → CommitHighlights，
	 *    否则会看到闪烁（中间态被渲染出来）。
	 */
	void CommitHighlights();

	/** 世界坐标 → 格（鼠标拾取用）。棋盘位于本 Actor 的局部 XY 平面。 */
	FIntVector WorldToCell(const FVector& WorldLocation) const;

	/** 格 → 世界坐标（单位摆放用）。Z 为地面高度。 */
	FVector CellToWorld(const FIntVector& Cell, float ZOffset = 0.0f) const;

	/** 棋盘中心（相机对准用） */
	FVector GetBoardCenter() const;

	/**
	 * 用默认地形（空旷厅堂）生成一次预览。
	 *
	 * ⚠️ 这是为了【编辑器视口可见】而存在的。
	 *    棋盘原本只在运行时由 GameMode 生成，导致关卡在编辑器里
	 *    是一片空白 —— 没法对齐相机、没法调光照、没法判断棋盘多大。
	 *    现在把 Actor 放进关卡就能立刻看到它。
	 */
	UFUNCTION(CallInEditor, Category = "HexSpire")
	void BuildPreview();

	/**
	 * 编辑器里显示的地形布局。改这个值视口会立刻重建。
	 * 可选：open_hall / pillars / corridor / bridge / arena_pit
	 */
	UPROPERTY(EditAnywhere, Category = "HexSpire")
	FName PreviewLayoutId = TEXT("open_hall");

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	/** 生成一个六边形的顶点与三角形，追加到给定数组 */
	void AppendHexagon(
		const FVector2D& Center2D,
		float Radius,
		float ZHeight,
		TArray<FVector>& Verts,
		TArray<int32>& Tris,
		TArray<FVector>& Normals,
		TArray<FVector2D>& UVs) const;

	/**
	 * 把"颜色 → 格子集合"的分组重建为多个 mesh section。
	 *
	 * ⚠️ 一色一 section 是刻意的：UE 5.8 引擎内容里没有读顶点色的材质，
	 *    所以颜色只能靠"每个 section 一个动态材质实例"来表达。
	 */
	void RebuildSections(
		UProceduralMeshComponent* Mesh,
		TArray<UMaterialInstanceDynamic*>& MaterialCache,
		const TArray<TPair<FLinearColor, TArray<FIntVector>>>& Groups,
		TFunctionRef<float(const FIntVector&, const FLinearColor&)> ZFunc,
		float RadiusScale);

	/** 地形 → 颜色 */
	static FLinearColor ColorForTerrain(EHexTerrain Terrain, EHexHazard Hazard, EHexFeature Feature);

	/** 高亮类型 → 颜色 */
	static FLinearColor ColorForHighlight(EHexHighlight Type);

	/** 高亮类型 → 绘制高度（避免多层高亮 z-fighting） */
	static float ZForHighlight(EHexHighlight Type);

	UPROPERTY()
	UProceduralMeshComponent* GroundMesh = nullptr;

	UPROPERTY()
	UProceduralMeshComponent* HighlightMesh = nullptr;

	/** BasicShapeMaterial（引擎必有，带 Color 向量参数） */
	UPROPERTY()
	UMaterialInterface* BaseMaterial = nullptr;

	/** 每个 section 一个 MID，复用以避免每帧新建 */
	UPROPERTY()
	TArray<UMaterialInstanceDynamic*> GroundMaterials;

	UPROPERTY()
	TArray<UMaterialInstanceDynamic*> HighlightMaterials;

	/** 每格的地面高度（墙抬高 / 深坑下沉），高亮要跟着走 */
	TMap<FIntVector, float> CellZ;

	/** 每格当前最高优先级高亮的 Z 偏移 */
	TMap<FIntVector, float> HighlightZ;

	/** 待提交的高亮 */
	TArray<TPair<FIntVector, EHexHighlight>> PendingHighlights;
};
