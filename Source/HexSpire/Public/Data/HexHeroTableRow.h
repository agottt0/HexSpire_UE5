// Copyright Hex Spire. All Rights Reserved.
//
// 英雄 DataTable 行结构 —— 给策划配数值用
//
// ══════════════════════════════════════════════════════════════════
// 分层与 FHexCardTableRow 完全一致，理由见那个文件顶部的长注释
// ══════════════════════════════════════════════════════════════════
//   HexSpireCore  FHexHeroData        逻辑层的权威结构，纯 C++
//   HexSpire      FHexHeroTableRow    配表结构，UPROPERTY 全暴露
//                 ↓ ToHeroData()
//
// FTableRowBase 属于 Engine 模块，塞进 Core 会让 HexVerify commandlet
// 与 battle_sim 都需要完整引擎启动 —— 直接废掉现有验证体系。
//
// ══════════════════════════════════════════════════════════════════
// 这张表【只放数值】，资产引用一律在 DA_UnitVisual_* 里
// ══════════════════════════════════════════════════════════════════
// 分界线是"这个字段是数字还是资产引用"：
//   · 六属性 / 体力 / 抽牌数 / 基石卡 id  → 这张表（能横向对比、能脚本批改）
//   · mesh / anim / 武器模型 / Niagara    → UHexUnitVisualSet（编辑器里拖）
//
// 为什么资产不进表：CSV 里资产路径是手打字符串，打错了静默变 null
// → 角色 T-pose，没有任何报错。DataAsset 的槽位有类型检查，拖错了拖不进去。
//
// 两边靠 FName Id 对应：本行的 RowName == UHexUnitVisualSet::UnitId。
//
// ⚠️ 代码内建仍是【权威基线】（HexContentLibrary.cpp 的 MakeWarden）。
//    表的语义是覆写：同 RowName 整行替换，新 RowName 追加。
//    这样 headless 验证不加载任何资产也能拿到一套完整可玩的英雄。

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Core/HexSpireEnums.h"
#include "Content/HexContentLibrary.h"
#include "HexHeroTableRow.generated.h"

/**
 * 规则改写的配表形式。对应逻辑层的 FHexRuleOverride（Runes/HexRuneData.h）。
 *
 * 英雄的被动天赋复用这个结构 —— 与符文同一套实现，
 * 所以"格挡不清空"这类被动不需要第二份代码路径。
 *
 * ⚠️ 三个值字段（Int/Float/Bool）按 Rule 的类型取用其中一个，
 *    填错那个不会报错，只是静默取 0/false。
 *    配表时对着 EHexGameRule 的注释确认该规则吃哪种值。
 */
USTRUCT(BlueprintType)
struct HEXSPIRE_API FHexRuleOverrideRow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "规则")
	EHexGameRule Rule = EHexGameRule::EnergyMax;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "规则|值")
	int32 IntValue = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "规则|值")
	float FloatValue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "规则|值")
	bool bBoolValue = false;

	/** 多个改写冲突时的应用顺序。同序按槽位升序（纪律 5：确定性）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "规则")
	int32 ApplyOrder = 0;

	/**
	 * 增量还是覆盖。
	 *
	 * ⚠️ 布尔型开关（BlockPersists 等）必须填 false ——
	 *    累加一个 bool 没有意义，填 true 的行为未定义。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "规则")
	bool bIsDelta = true;

	FHexRuleOverride ToRuleOverride() const;
};

/**
 * 英雄配表行。
 *
 * ⚠️ RowName 必须等于英雄 Id（如 warden）。
 *    Id 是逻辑层认人的唯一依据；不一致会让覆写静默落空 ——
 *    表里改了数值，游戏里纹丝不动。Loader 会校验并报错。
 */
USTRUCT(BlueprintType)
struct HEXSPIRE_API FHexHeroTableRow : public FTableRowBase
{
	GENERATED_BODY()

	// ═════════════════════════════════════ 基础

	/** 显示名（中文） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "基础")
	FString DisplayName;

	/** 体型。影响 footprint 占格数与射程起算点（§8.5）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "基础")
	EHexSizeClass SizeClass = EHexSizeClass::S;

	// ═════════════════════════════════════ 六属性

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "属性",
		meta = (ClampMin = "1"))
	int32 BaseHP = 80;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "属性",
		meta = (ClampMin = "0"))
	int32 BaseATK = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "属性",
		meta = (ClampMin = "0"))
	int32 BaseDEF = 8;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "属性",
		meta = (ClampMin = "0"))
	int32 BaseAGI = 6;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "属性",
		meta = (ClampMin = "0"))
	int32 BaseLUK = 5;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "属性",
		meta = (ClampMin = "0"))
	int32 BaseCRIT = 10;

	// ═════════════════════════════════════ 资源与手牌

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "资源",
		meta = (ClampMin = "1"))
	int32 EnergyMax = 5;

	/**
	 * 每回合抽牌数。
	 *
	 * ⚠️ 这个值与基石卡机制强耦合，不能单独调。
	 *    基石卡常驻后牌堆里只剩 5 张构筑卡（满容量 8 张），
	 *    抽 5 张会每回合把卡组抽光 → 随机性消失、构筑深度归零。
	 *    镇妖者填 3 的完整推导见 HexContentLibrary.cpp 的 MakeWarden。
	 *    改大之前先想清楚牌堆还剩几张。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "资源",
		meta = (ClampMin = "1"))
	int32 CardsDrawnPerTurn = 3;

	// ═════════════════════════════════════ 卡牌

	/**
	 * 基石卡 id（常驻可用、不进抽牌堆、不占卡组容量）。
	 *
	 * 约定 3 张：攻击 + 防御 + 移动，其中攻击位放专属变体。
	 *
	 * ⚠️ 填了不存在的卡 id 会让该英雄缺一个基础动作 ——
	 *    症状是"这个角色走不了路"。由 VerifyContent 的引用完整性断言钉住。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "卡牌")
	TArray<FName> CornerstoneCardIds;

	/**
	 * 掉落加权标签。
	 *
	 * ⚠️ 标签【不产生任何数值加成】（§7.8），只影响掉落权重与 UI 筛选。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "卡牌")
	TArray<FName> CardPoolTags;

	// ═════════════════════════════════════ 被动天赋

	/** 复用符文的规则改写结构 —— 与符文同一套实现，不走第二条代码路径。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "被动")
	TArray<FHexRuleOverrideRow> PassiveRules;

	/** 被动的玩家可读描述。留空则 UI 不显示被动条目。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "被动",
		meta = (MultiLine = "true"))
	FString PassiveText;

	/**
	 * 转成逻辑层的英雄定义。
	 * @param InId 英雄 Id（取自 RowName）
	 */
	FHexHeroData ToHeroData(FName InId) const;

	/** 由逻辑层结构填充本行（导出 CSV 用） */
	void FromHeroData(const FHexHeroData& In);
};
