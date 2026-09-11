// Copyright Hex Spire. All Rights Reserved.
//
// 卡牌 DataTable 行结构 —— 给策划与美术配表用
//
// ══════════════════════════════════════════════════════════════════
// 为什么放在 HexSpire（表现层）而不是 HexSpireCore（逻辑层）
// ══════════════════════════════════════════════════════════════════
// HexSpireCore 刻意只依赖 Core / CoreUObject（见 HexSpireCore.Build.cs），
// 这是「战斗能在 headless 下跑完」的技术前提 —— 全部验证器与批量模拟
// 都建立在此之上。
//
// FTableRowBase / UDataTable 属于 Engine 模块。把它塞进 Core
// 会让 core 依赖 Engine，于是 HexVerify commandlet 与 battle_sim
// 都需要完整引擎启动才能跑 —— 那会直接废掉现有的验证体系。
//
// 所以分层是：
//   HexSpireCore  FHexCardData        逻辑层的权威结构，纯 C++
//   HexSpire      FHexCardTableRow    配表结构，UPROPERTY 全暴露
//                 ↓ ToCardData()
//                 转换成 FHexCardData 交给逻辑层
//
// ══════════════════════════════════════════════════════════════════
// 代码内建仍然是【权威数据源】
// ══════════════════════════════════════════════════════════════════
// HexContentLibrary.cpp 里那 12 张卡的数值是 Godot 版实测调过的，
// 每条都带「为什么是这个数」的注释。DataTable 不取代它，而是【覆写】它：
//
//   代码内建 12 张卡  →  DataTable 有同 Id 的行则覆写其字段
//                     →  DataTable 有新 Id 则追加为新卡
//
// 这样做的理由：
//   · 验证器/模拟不加载任何资产也能拿到一套完整可玩的卡池（回退保障）
//   · 策划改数值不需要编译，改表即可
//   · 美术资产字段【只存在于表里】，逻辑层完全不知道它们，纪律 3 不破
//
// ⚠️ §7.5 强制：数值必须写成 FlatValue + Stats[StatRef] × StatRatio。
//    配表时不要把 Damage 直接填 12 —— ATK 从 10 涨到 200 时
//    硬编码的卡会被数值冲垮，策略层失效。
//    例外（离散量，不随属性缩放）：状态层数、位移格数、抽牌数、体力数。

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Core/HexSpireEnums.h"
#include "Battle/HexCardData.h"
#include "HexCardTableRow.generated.h"

class UTexture2D;

/**
 * 单个效果步骤的配表形式。
 *
 * 对应逻辑层的 FHexEffectStep（Battle/HexCardData.h）。
 * 字段刻意一一对应，方便对照，也让 ToEffectStep() 保持无脑直译 ——
 * 转换函数里一旦出现"聪明"的推导逻辑，配表结果就会与表面读到的不一致。
 */
USTRUCT(BlueprintType)
struct HEXSPIRE_API FHexEffectStepRow
{
	GENERATED_BODY()

	/** 操作码。新增值必须同步在 BattleFlow::ExecuteStep 里加分支。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "效果")
	EHexEffectOp Op = EHexEffectOp::DealDamage;

	// ── §7.5 系数化三件套

	/** 固定值部分 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "效果|数值")
	float FlatValue = 0.0f;

	/** 吃哪项属性的加成（ATK / DEF / AGI / LUK / CRIT / HP / HP_MAX） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "效果|数值")
	FName StatRef = TEXT("ATK");

	/** 属性系数。最终值 = FlatValue + Stats[StatRef] × StatRatio */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "效果|数值")
	float StatRatio = 0.0f;

	/** 连击次数 / 抽牌张数 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "效果|数值",
		meta = (ClampMin = "1"))
	int32 Repeat = 1;

	// ── 离散量（策略层锚点，不随属性缩放）

	/** 位移格数 / 击退格数 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "效果|离散量")
	int32 Distance = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "效果|离散量")
	FName StatusId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "效果|离散量")
	int32 StatusStacks = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "效果")
	EHexTargetFilter TargetFilter = EHexTargetFilter::Enemy;

	/** 视觉/音效标记。逻辑层不消费，只写进事件日志给表现层用。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "效果|表现")
	FName VfxId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "效果|表现")
	FName SfxId;

	/** 转成逻辑层结构 */
	FHexEffectStep ToEffectStep() const;
};

/**
 * 目标规格的配表形式。对应 FHexTargetSpec。
 *
 * ⚠️ RangeMax 从【最近的己方 footprint 格】起算（§8.5），
 *    所以大体型单位的有效射程天然更长。配表时按"格数"填即可。
 */
USTRUCT(BlueprintType)
struct HEXSPIRE_API FHexTargetSpecRow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "目标")
	EHexTargetShape Shape = EHexTargetShape::Single;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "目标",
		meta = (ClampMin = "0"))
	int32 RangeMin = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "目标",
		meta = (ClampMin = "0"))
	int32 RangeMax = 1;

	/** LINE 长度 / BURST 半径 / CONE 长度 / RING 半径 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "目标",
		meta = (ClampMin = "0"))
	int32 AreaSize = 0;

	/**
	 * 是否需要视线。
	 *
	 * ⚠️ 冲撞（DashPath）必须填 false。
	 *    视线判定会因石柱直接否决目标，而冲撞需要的是"墙挡、人不挡"，
	 *    那条规则由 LegalCells 的 DashPath 分支自己实现，不能借用视线。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "目标")
	bool bRequiresLineOfSight = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "目标")
	bool bCanTargetEmptyCell = false;

	FHexTargetSpec ToTargetSpec() const;
};

/**
 * 卡牌的美术配置。
 *
 * ⚠️ 这一组【只有表现层读】。逻辑层的 FHexCardData 里没有对应字段 ——
 *    纪律 3 要求逻辑层不知道渲染的存在。
 *
 * ⚠️ 用 TSoftObjectPtr 而不是硬引用（UTexture2D*）：
 *    硬引用会让整张 DataTable 一加载就把所有卡的贴图全拉进内存。
 *    卡池扩到 100+ 张时那是几百 MB。软引用按需加载。
 */
USTRUCT(BlueprintType)
struct HEXSPIRE_API FHexCardVisualRow
{
	GENERATED_BODY()

	/** 卡框。留空则按稀有度取默认框。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "美术")
	TSoftObjectPtr<UTexture2D> CardFrame;

	/** 类型图标（攻击/守备/移动…）。留空则按 CardType 取默认图标。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "美术")
	TSoftObjectPtr<UTexture2D> TypeIcon;

	/** 卡面插画 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "美术")
	TSoftObjectPtr<UTexture2D> Artwork;

	/**
	 * 卡框染色。
	 *
	 * 白色 = 不染色。用它可以让同一张灰度卡框复用出多种配色，
	 * 美术不必为每个稀有度各画一张。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "美术")
	FLinearColor FrameTint = FLinearColor::White;

	/** 稀有度光晕强度。0 = 不发光。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "美术",
		meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float RarityGlow = 0.0f;
};

/**
 * 卡牌配表行。
 *
 * 用法（策划）：
 *   1. 跑 `-run=HexExportCards` 导出当前 12 张卡为 CSV
 *   2. 编辑器里 Import CSV → 选 FHexCardTableRow 作为行结构
 *   3. 把生成的 DataTable 放到 /Game/HexSpire/Data/DT_Cards
 *   4. 改数值 → 重启（或热重载表）→ 生效，不需要编译 C++
 *
 * ⚠️ RowName 必须等于卡牌 Id（如 atk_basic）。
 *    Id 是逻辑层认卡的唯一依据；RowName 与 Id 不一致会让覆写
 *    静默落空 —— 表里改了数值，游戏里纹丝不动。
 *    HexCardTableLoader 会校验并报错。
 */
USTRUCT(BlueprintType)
struct HEXSPIRE_API FHexCardTableRow : public FTableRowBase
{
	GENERATED_BODY()

	// ═════════════════════════════════════ 基础

	/** 显示名（中文） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "基础")
	FString DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "基础")
	EHexCardType CardType = EHexCardType::Attack;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "基础")
	EHexRarity Rarity = EHexRarity::Common;

	/** 体力费用 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "基础",
		meta = (ClampMin = "0"))
	int32 EnergyCost = 1;

	/**
	 * 检索/加权/符文过滤用的标签。
	 *
	 * ⚠️ 标签【不产生任何数值加成】（§7.8）。
	 *    想让"近战牌 +2 伤害"请写成符文的规则改写，不要指望标签自己生效。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "基础")
	TArray<FName> Tags;

	// ═════════════════════════════════════ 目标与效果

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "目标")
	FHexTargetSpecRow TargetSpec;

	/** 按顺序执行。顺序有意义（§6.5：[锐化,倍化] ≠ [倍化,锐化]）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "效果")
	TArray<FHexEffectStepRow> Effects;

	// ═════════════════════════════════════ 卡组规则

	/** 基石卡：常驻可用、不进抽牌堆、不占卡组容量 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "卡组")
	bool bIsCornerstone = false;

	/** 消耗：打出后进消耗区，本场战斗不再出现 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "卡组")
	bool bIsExhaust = false;

	/** 是否占卡组容量。基石卡与衍生卡填 false（§7.6）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "卡组")
	bool bCountsTowardCapacity = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "卡组",
		meta = (ClampMin = "1"))
	int32 MaxCopiesInDeck = 3;

	// ═════════════════════════════════════ 文本

	/**
	 * 描述模板。运行时把占位符替换成按当前属性实算的值。
	 *
	 * 可用占位符：
	 *   {dmg}    总伤害      {block}  总格挡
	 *   {kb}     击退格数    {move}   位移格数
	 *   {stacks} 状态层数    {draw}   抽牌数
	 *   {hits}   连击次数
	 *
	 * 例：造成 {dmg} 点伤害并击退 {kb} 格。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "文本",
		meta = (MultiLine = "true"))
	FString DescriptionTemplate;

	// ═════════════════════════════════════ 美术（仅表现层读）

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "美术")
	FHexCardVisualRow Visual;

	/**
	 * 转成逻辑层的卡牌定义。
	 * @param InId 卡牌 Id（取自 RowName）
	 */
	FHexCardData ToCardData(FName InId) const;

	/** 由逻辑层结构填充本行（导出 CSV 用） */
	void FromCardData(const FHexCardData& In);
};
