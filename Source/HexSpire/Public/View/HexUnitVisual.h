// Copyright Hex Spire. All Rights Reserved.
//
// 单位的灰盒可视化 —— 表现层
//
// 用户决策 q14：怪物用胶囊/几何体灰盒
//   "先保证体型/朝向/footprint 一看就懂，不被占位美术干扰"
//
// 所以这里不加载任何模型资产，全部用引擎自带的基础几何体
// （/Engine/BasicShapes/*），运行时按体型缩放。
// 镇妖者的 3D 模型接入后，只需把 BodyMesh 换成 SkeletalMesh，
// 其余逻辑（朝向、footprint、HP 条）完全不用改。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/HexSpireEnums.h"
#include "HexUnitVisual.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class FHexUnit;
class AHexBoardVisual;

UCLASS()
class HEXSPIRE_API AHexUnitVisual : public AActor
{
	GENERATED_BODY()

public:
	AHexUnitVisual();

	/**
	 * 按逻辑单位刷新外观与位置。
	 * ⚠️ 每帧调用是安全的（内部只在变化时重建几何）。
	 */
	void SyncFromUnit(const FHexUnit& Unit, const AHexBoardVisual& Board);

	int32 GetUnitId() const { return UnitId; }

	/** 死亡表现：变暗压扁（灰盒期不做死亡动画） */
	void SetDead();

private:
	/** 体型 → 身体尺寸（半径 / 高度） */
	static void SizeForClass(EHexSizeClass SizeClass, float& OutRadius, float& OutHeight);

	/** 队伍 → 基础颜色 */
	static FLinearColor ColorForTeam(EHexTeam Team, bool bIsElite, bool bIsBoss);

	UPROPERTY()
	UStaticMeshComponent* BodyMesh = nullptr;

	/**
	 * 朝向指示器。
	 * ⚠️ 这不是装饰：§8.2.3 的背击与 §8.2.2 的转向成本都依赖朝向，
	 *    玩家看不到朝向就无法计算"绕后能不能背击"。
	 *    灰盒期用一个前伸的小方块表示，比任何图标都直观。
	 */
	UPROPERTY()
	UStaticMeshComponent* FacingMesh = nullptr;

	/** 生命条（世界空间的一个扁方块，按比例缩放） */
	UPROPERTY()
	UStaticMeshComponent* HealthBarMesh = nullptr;

	UPROPERTY()
	UStaticMeshComponent* HealthBarBackMesh = nullptr;

	UPROPERTY()
	UMaterialInstanceDynamic* BodyMaterial = nullptr;

	UPROPERTY()
	UMaterialInstanceDynamic* HealthMaterial = nullptr;

	UPROPERTY()
	UMaterialInstanceDynamic* HealthBackMaterial = nullptr;

	UPROPERTY()
	UMaterialInstanceDynamic* FacingMaterial = nullptr;

	int32 UnitId = -1;

	/** 上次同步的体型，变化时才重建缩放 */
	EHexSizeClass CachedSize = EHexSizeClass::S;
	bool bInitialized = false;
};
