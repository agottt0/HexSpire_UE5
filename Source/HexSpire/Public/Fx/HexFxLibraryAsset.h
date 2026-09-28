// Copyright Hex Spire. All Rights Reserved.
//
// 特效 / 音效库 —— 表现层 DataAsset
//
// ══════════════════════════════════════════════════════════════════
// 为什么是「一张 Map 资产」而不是「一个 id 一个资产」
// ══════════════════════════════════════════════════════════════════
// 特效 id 在配表时是手打字符串（VfxId = "vfx_slash"）。
// 若做成一资产一 id，拼错时要翻遍整个 Content 目录才能确认
// 「到底有没有这个资产」—— 而拼错的症状只是"没看见特效"，
// 你会先怀疑代码。
//
// 集中成一张 Map 之后：
//   · 加载时能一次性把全部 key 打进日志（拼错时对照着看就行）
//   · 能让验证器断言「卡表/技能表引用的每个 VfxId 都在库里」
//     —— 这条断言才是「只需配置资产即可」能成立的保障
//   · 美术在一个窗口里就能看到全部特效，不用在资产树里翻
//
// ══════════════════════════════════════════════════════════════════
// 为什么特效不塞进单位外观资产
// ══════════════════════════════════════════════════════════════════
// 特效跨单位复用 —— 「斩击」会被十几个技能共用。
// 塞进单位资产等于每只怪各存一份引用，改一次要改十几个地方，
// 而且必然漏掉几个，表现为「有的怪的斩击换了、有的没换」。
//
// ══════════════════════════════════════════════════════════════════
// 为什么全部用 TSoftObjectPtr
// ══════════════════════════════════════════════════════════════════
// 硬引用会让这张库资产一加载就把【全部】特效与音效拉进内存。
// 特效表扩到上百条时那是几百 MB，而一场战斗只用到其中几条。
// 软引用按需加载；代价是第一次播放时有一次同步加载 ——
// 战斗开始时可以预热（见 FHexFxRuntime::Preload）。

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HexFxLibraryAsset.generated.h"

class UNiagaraSystem;
class USoundBase;

/**
 * 特效挂点方式。
 *
 * ⚠️ 挂点选错不会报错，只会让特效出现在错误的位置 ——
 *    最常见的是本该在目标身上的打击特效被挂到施法者脚下，
 *    看起来像"特效没生效"。配的时候对照技能语义选。
 */
UENUM(BlueprintType)
enum class EHexFxAttach : uint8
{
	/** 在目标格的世界坐标处播放，不跟随任何单位 */
	TargetCell = 0	UMETA(DisplayName = "目标格（世界坐标）"),

	/** 跟随施法者。适合起手光效、施法法阵 */
	Source = 1		UMETA(DisplayName = "跟随施法者"),

	/** 跟随受击者。适合命中闪光、流血 */
	Target = 2		UMETA(DisplayName = "跟随受击者"),

	/**
	 * 跟随施法者的指定 socket（武器尖端等）。
	 * ⚠️ socket 名写错时会静默回退到 actor 原点 ——
	 *    表现为"特效从脚下冒出来"，不报错。
	 */
	SourceSocket = 3	UMETA(DisplayName = "施法者 socket"),
};

/** 一条特效配置 */
USTRUCT(BlueprintType)
struct HEXSPIRE_API FHexVfxEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "特效")
	TSoftObjectPtr<UNiagaraSystem> System;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "特效")
	EHexFxAttach Attach = EHexFxAttach::Target;

	/** 仅 Attach = SourceSocket 时有意义 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "特效")
	FName SocketName;

	/** 相对挂点的偏移。大体型单位通常要抬高 Z。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "特效")
	FVector Offset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "特效",
		meta = (ClampMin = "0.01"))
	float Scale = 1.0f;

	/**
	 * 强制回收时间。0 = 由特效自己决定（推荐）。
	 *
	 * ⚠️ 只在特效【不会自己结束】时才填非 0。
	 *    给一个本来会自然结束的特效填 LifeSeconds 会把它切断，
	 *    表现为"特效播一半就没了"。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "特效",
		meta = (ClampMin = "0.0"))
	float LifeSeconds = 0.0f;
};

/** 一条音效配置 */
USTRUCT(BlueprintType)
struct HEXSPIRE_API FHexSfxEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "音效")
	TSoftObjectPtr<USoundBase> Sound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "音效",
		meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float VolumeMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "音效",
		meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float PitchMultiplier = 1.0f;

	/**
	 * 是否跟随施法者移动。
	 *
	 * false = 在坐标处一次性播放（2D 化处理）。
	 * ⚠️ 回合制战棋的相机基本不动，绝大多数音效填 false 就够 ——
	 *    填 true 会让音效随单位移动产生多普勒感，在俯视战棋里听起来很怪。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "音效")
	bool bAttachToSource = false;
};

/**
 * 特效与音效的集中配置库。
 *
 * 路径固定在 FHexFxRuntime::DefaultLibraryPath()。
 * ⚠️ 挪目录或改名会让加载【静默失败】—— 不报错，只是全程没有特效。
 *    要改请连带改那个函数。
 */
UCLASS(BlueprintType)
class HEXSPIRE_API UHexFxLibraryAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/**
	 * key = 逻辑层 FHexEffectStep::VfxId。
	 *
	 * ⚠️ key 必须与卡表 / 敌人技能表里填的 VfxId 逐字一致。
	 *    不一致时该技能【没有任何特效】且不报错 ——
	 *    这正是 VerifyContent 要断言引用完整性的原因。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "特效")
	TMap<FName, FHexVfxEntry> Vfx;

	/** key = 逻辑层 FHexEffectStep::SfxId */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "音效")
	TMap<FName, FHexSfxEntry> Sfx;

	/**
	 * 事件名 → 兜底特效。
	 *
	 * 用途：内容没配 VfxId 时，按事件类型给一个通用表现
	 * （damage_dealt → 通用命中闪光，unit_died → 通用消散）。
	 *
	 * ⚠️ 这是【兜底】不是【默认设计】。灰盒期靠它让战斗看起来不是静止的；
	 *    正式内容应该显式配 VfxId，否则所有技能长得一模一样，
	 *    §13.2 要求的"从表现区分技能"就没了。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "兜底")
	TMap<FName, FHexVfxEntry> FallbackByEvent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "兜底")
	TMap<FName, FHexSfxEntry> FallbackSfxByEvent;
};
