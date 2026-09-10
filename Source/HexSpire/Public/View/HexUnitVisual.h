// Copyright Hex Spire. All Rights Reserved.
//
// 单位的可视化 —— 表现层
//
// 结构与设计理由见 HexUnitVisual.cpp 文件头。
// 一句话版本：模型与动画来自模板资产（共用 S_Mannequin 骨架），
// 但【颜色语言全部走脚下光圈】，与美术资产解耦。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/HexSpireEnums.h"
#include "View/HexUnitAppearance.h"
#include "HexUnitVisual.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class USkeletalMeshComponent;
class UMaterialInstanceDynamic;
class UAnimSequence;
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

	/** 死亡表现 */
	void SetDead();

	/**
	 * 播一次性动作（攻击/施法等）。
	 *
	 * 供 GameMode 在结算出手时调用 —— 受击可以从掉血差分推出来，
	 * 但"谁在攻击"无法从状态推断（打空、被闪避时血量不变），
	 * 所以这个必须由流程层显式触发。
	 */
	void PlayOneShot(EHexUnitAnim Anim);

	virtual void Tick(float DeltaSeconds) override;

private:
	/** 首次同步时装配模型与动画（需要先知道队伍/等级才能选外观） */
	void EnsureAppearance(const FHexUnit& Unit);

	/** 播放动画，返回该动画的时长（秒）。缺失时回退 Idle。 */
	float PlayAnim(EHexUnitAnim Anim, bool bLooping = true);

	/** 一次性动作播完后的回调 */
	void ReturnToIdle();

	/** 体型 → 身体尺寸（半径 / 高度） */
	static void SizeForClass(EHexSizeClass SizeClass, float& OutRadius, float& OutHeight);

	/** 队伍 → 基础颜色 */
	static FLinearColor ColorForTeam(EHexTeam Team, bool bIsElite, bool bIsBoss);

	UPROPERTY()
	USceneComponent* Root = nullptr;

	/** 骨骼模型（模板资产）。加载失败时不可见，由 BodyMesh 顶上。 */
	UPROPERTY()
	USkeletalMeshComponent* BodySkel = nullptr;

	/**
	 * 灰盒回退体。
	 * ⚠️ 不要因为"已经有模型了"就删掉它 ——
	 *    模板目录将来会被整个移除，届时这是唯一还能显示单位的东西。
	 */
	UPROPERTY()
	UStaticMeshComponent* BodyMesh = nullptr;

	/**
	 * 脚下光圈：队伍 / 威胁等级 / debuff 的唯一颜色载体。
	 * 详见 .cpp 文件头 —— 简言之，美术材质不保证有 Color 参数，
	 * 给模型染色会静默失败。
	 */
	UPROPERTY()
	UStaticMeshComponent* GroundRing = nullptr;

	/**
	 * 朝向指示器。
	 * ⚠️ 这不是装饰：§8.2.3 的背击与 §8.2.2 的转向成本都依赖朝向。
	 *    有了人形模型后仍然保留 —— 55° 俯视下人形朝向不够果断。
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
	UMaterialInstanceDynamic* RingMaterial = nullptr;

	UPROPERTY()
	UMaterialInstanceDynamic* HealthMaterial = nullptr;

	UPROPERTY()
	UMaterialInstanceDynamic* HealthBackMaterial = nullptr;

	UPROPERTY()
	UMaterialInstanceDynamic* FacingMaterial = nullptr;

	// ── 动画（预载，避免出手时才同步加载造成卡顿）
	UPROPERTY()
	UAnimSequence* AnimIdle = nullptr;

	UPROPERTY()
	UAnimSequence* AnimWalk = nullptr;

	UPROPERTY()
	UAnimSequence* AnimAttack = nullptr;

	UPROPERTY()
	UAnimSequence* AnimGetHit = nullptr;

	UPROPERTY()
	UAnimSequence* AnimDie = nullptr;

	/** 指向静态外观表，不持有所有权 */
	const FHexUnitAppearance* Look = nullptr;

	FTimerHandle AnimResetTimer;

	int32 UnitId = -1;

	/** 上次同步的体型，变化时才重建缩放 */
	EHexSizeClass CachedSize = EHexSizeClass::S;

	/** 上次同步的 HP —— 用来差分出"受击"事件（见 .cpp） */
	int32 CachedHP = 0;
	bool bHasCachedHP = false;

	/** 移动插值目标 */
	FVector TargetLocation = FVector::ZeroVector;
	bool bHasTargetLoc = false;

	/** 朝向插值目标（Yaw，度） */
	float TargetYaw = 0.0f;
	bool bHasTargetYaw = false;

	EHexUnitAnim CurrentAnim = EHexUnitAnim::Idle;

	bool bInitialized = false;
	bool bAppearanceReady = false;
	bool bUsingSkeletalMesh = false;
	bool bIsDeadVisual = false;
};
