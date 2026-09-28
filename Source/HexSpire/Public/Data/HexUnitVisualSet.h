// Copyright Hex Spire. All Rights Reserved.
//
// 单位外观资产 —— 一个单位一个 DataAsset
//
// ══════════════════════════════════════════════════════════════════
// 为什么资产引用走 DataAsset 而数值走 DataTable
// ══════════════════════════════════════════════════════════════════
// 分界线是"这个字段是数字还是资产引用"，不是"英雄还是怪物"：
//
//   数值（六属性/射程/冷却）  → DT_Heroes / DT_Enemies
//     能横向对比：四只怪的 HP 并排看才能发现某只跳了一截。
//     能脚本批改：CSV 往返，批量调平衡不用逐个点开资产。
//
//   资产（mesh/anim/Niagara） → 本资产
//     必须有类型检查：CSV 里路径是手打字符串，打错了静默变 null
//     → 单位 T-pose，不报任何错。DataAsset 的槽位拖错了拖不进去。
//     从不横向对比：外观是一个单位一个单位配的。
//
// 两边靠 FName Id 对应：UnitId == DT 里的 RowName。
//
// ══════════════════════════════════════════════════════════════════
// 为什么动画用 TMap 而不是 IdlePath/WalkPath 那样的命名字段
// ══════════════════════════════════════════════════════════════════
// 旧的 FHexUnitAppearance 是 8 个命名字段 + 一个 switch（PathFor）。
// 加一个动画状态要改三处：结构字段、switch 分支、赋值处。
// 漏改 switch 的症状是该状态静默回退到 Idle —— 看起来"动画没做"。
//
// 用 TMap<EHexUnitAnim, ...> 之后加状态只需要加枚举值，
// 数据侧自动就能配，代码一行不用改。
//
// ══════════════════════════════════════════════════════════════════
// 为什么用 TSoftObjectPtr 而不是硬引用
// ══════════════════════════════════════════════════════════════════
// 硬引用会让资产一加载就把所有 mesh/anim 全拉进内存。
// 单位扩到几十个时那是几个 GB。软引用按需加载，
// 且 GetVisualSet 只在战斗开始时各解析一次（单位数 ≤ 10，卡顿不可感知）。

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Core/HexSpireEnums.h"
#include "HexUnitVisualSet.generated.h"

class USkeletalMesh;
class UAnimSequence;
class UStaticMesh;

/**
 * 挂在骨骼 socket 上的一件装备（武器 / 盾 / 饰品）。
 *
 * ⚠️ SocketName 必须是【骨架里真实存在】的 socket 名。
 *    写错了不报错，组件会挂到原点 —— 症状是"武器掉在角色脚下"。
 *    可在骨架编辑器里右键骨骼 → Add Socket 查看已有 socket。
 */
USTRUCT(BlueprintType)
struct HEXSPIRE_API FHexUnitAttachment
{
	GENERATED_BODY()

	/** 这件装备的标识（用于日志与将来的换装查找）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "装备")
	FName SlotId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "装备")
	TSoftObjectPtr<UStaticMesh> Mesh;

	/** 骨架上的 socket 名。留空则挂到 actor 原点。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "装备")
	FName SocketName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "装备|变换")
	FVector RelativeLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "装备|变换")
	FRotator RelativeRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "装备|变换")
	FVector RelativeScale = FVector::OneVector;
};

/**
 * 一个单位的全部外观资产。
 *
 * 命名约定：DA_UnitVisual_<UnitId>，放 /Game/HexSpire/Data/Visuals/。
 *
 * ⚠️ UnitId 必须等于 DT_Heroes / DT_Enemies 里的 RowName。
 *    不一致会让这个资产【永远匹配不上任何单位】——
 *    症状是"配了模型但游戏里还是占位胶囊"，而资产本身怎么看都是对的。
 *    HexUnitVisualLoader 会校验并报错。
 */
UCLASS(BlueprintType)
class HEXSPIRE_API UHexUnitVisualSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 对应 DT 里的 RowName（如 warden / biting_hound）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "标识")
	FName UnitId;

	// ═════════════════════════════════════ 模型

	/** 留空则回退到灰盒占位（不是错误 —— 灰盒期的正常状态）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "模型")
	TSoftObjectPtr<USkeletalMesh> SkeletalMesh;

	/**
	 * 模型缩放。
	 *
	 * ⚠️ 这是【美术层面】的缩放，与体型 S/M/L 的占格数无关。
	 *    体型由 DT 里的 SizeClass 决定 footprint，这里只调视觉大小。
	 *    两者不匹配的症状是"模型比它占的格子小一圈"，不影响规则。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "模型")
	FVector MeshScale = FVector::OneVector;

	/** 模型朝向修正。导出朝向与引擎 +X 不一致时用它拧正。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "模型")
	FRotator MeshRotation = FRotator::ZeroRotator;

	/** 模型相对单位原点的偏移（脚底没落在格子平面上时用它抬/沉）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "模型")
	FVector MeshOffset = FVector::ZeroVector;

	// ═════════════════════════════════════ 动画

	/**
	 * 各状态对应的动画。
	 *
	 * ⚠️ 缺某个状态不是错误 —— 调用方回退到 Idle
	 *    （见 FHexUnitAppearance::PathFor 的同一条约定）。
	 *    灰盒期大部分单位只配 Idle + Attack 是正常的。
	 *
	 * ⚠️ 全部动画必须共用【同一套骨架】，否则加载成功但播放时姿态错乱。
	 *    模板动画都基于 S_Mannequin（108 骨骼），
	 *    Warden 模型已绑到同一骨架，可直接互换（probe_template_assets.py 实测）。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "动画")
	TMap<EHexUnitAnim, TSoftObjectPtr<UAnimSequence>> Anims;

	// ═════════════════════════════════════ 武器与挂件

	/**
	 * 挂在骨骼上的装备模型。
	 *
	 * ⚠️ 这里【只有外观】。武器的数值加成在装备表里（FHexEquipData），
	 *    两者靠 SlotId 无关 —— 挂件纯装饰，换武器模型不会改数值。
	 *    这是刻意的：让美术能先把模型挂上，不必等装备系统落地。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "装备")
	TArray<FHexUnitAttachment> Attachments;

	// ═════════════════════════════════════ 单位专属特效

	/**
	 * 这个单位专属的 VfxId → 覆写。
	 *
	 * ⚠️ 共用特效应当放 DA_FxLibrary，不要在每个单位里各配一份 ——
	 *    那样改一个通用打击特效要改 N 个资产，必然漏。
	 *    只有"这只怪的火球和别人的不一样"才配在这里。
	 *
	 * 查找顺序：本表 → DA_FxLibrary 的 Vfx → 按事件兜底 → 不播。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "特效")
	TMap<FName, FName> VfxOverrides;

	// ═════════════════════════════════════ 脚下光圈

	/**
	 * 光圈颜色覆写。Alpha = 0 表示不覆写，按等级取默认色。
	 *
	 * ⚠️ 光圈不是装饰品而是【必需品】：在拿到专属怪物模型之前，
	 *    敌人之间只能靠体型缩放和光圈颜色区分。
	 *    见 HexUnitAppearance.h 里 HexAppearance::For 的注释。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "光圈")
	FLinearColor RingColorOverride = FLinearColor(0, 0, 0, 0);

	// ═════════════════════════════════════ 解析接口

	/** 同步加载骨骼网格体。未配置或加载失败返回 nullptr（调用方回退灰盒）。 */
	USkeletalMesh* LoadMesh() const;

	/**
	 * 同步加载某状态的动画。
	 *
	 * 未配置该状态时回退到 Idle；Idle 也没配则返回 nullptr。
	 */
	UAnimSequence* LoadAnim(EHexUnitAnim Anim) const;

	/** 是否配了任何动画（用于判断该不该回退到旧的路径式外观）。 */
	bool HasAnyAnim() const { return Anims.Num() > 0; }
};
