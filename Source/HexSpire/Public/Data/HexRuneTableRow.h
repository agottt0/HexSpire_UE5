// Copyright Hex Spire. All Rights Reserved.
//
// 符文 DataTable 行结构 —— 给策划配符文用
//
// 分层与 FHexCardTableRow 完全一致（理由见那个文件顶部）：
//   HexSpireCore  FHexRuneData        逻辑层权威结构，纯 C++
//   HexSpire      FHexRuneTableRow    配表结构，UPROPERTY 全暴露
//                 ↓ ToRuneData()
//
// ══════════════════════════════════════════════════════════════════
// 符文为什么值得单独一张表（而不是塞进卡表）
// ══════════════════════════════════════════════════════════════════
// 符文与卡牌结构不同：没有费用/目标规格/卡组约束，
// 多了触发器（时机+过滤+条件+限次）与规则改写。
// 硬套卡表会让两边都长出一堆"对方用不到"的列 ——
// 表格视图的横向对比价值（哪个时机挂了几个符文）也没了。
//
// ⚠️ 代码内建仍是【权威基线】（HexRuneLibrary.cpp）。
//    表的语义是覆写：同 RowName 整条替换，新 RowName 追加。
//    目标 90–120 个符文（§6.1），后期扩量主要走这张表。
//
// ⚠️ 三条不可动摇的设计约束（§6.4 / §6.7），配表时同样生效：
//   ① 纯数值符文占比必须为 0（VerifyRunes 会拦占比）
//   ② Tags 只用于掉落加权/UI筛选/图鉴，不产生任何加成
//   ③ MechanicText 必须写清"何时触发、几次、与什么交互"（R8）

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Core/HexSpireEnums.h"
#include "Runes/HexRuneData.h"
#include "Data/HexCardTableRow.h"
#include "Data/HexHeroTableRow.h"
#include "HexRuneTableRow.generated.h"

/**
 * 条件类型的配表形式。
 *
 * ⚠️ 必须单独一个 UENUM：逻辑层的 FHexEffectCondition::EKind 是
 *    嵌套 enum，UHT 不接受嵌套枚举做 UPROPERTY。
 *    两边的枚举项必须一一对应，ToCondition() 里做显式映射 ——
 *    刻意不用 static_cast 直转：那样两边顺序一旦漂移就是静默错值。
 */
UENUM(BlueprintType)
enum class EHexRuneConditionKind : uint8
{
	None = 0				UMETA(DisplayName = "无条件"),
	TargetHPBelowPercent	UMETA(DisplayName = "目标生命低于百分比"),
	SelfAtFullHP			UMETA(DisplayName = "自身满血"),
	SelfNotAtFullHP			UMETA(DisplayName = "自身未满血"),
	DrawPileEmpty			UMETA(DisplayName = "抽牌堆为空"),
	TargetHasStatus			UMETA(DisplayName = "目标有指定状态"),
	CardsPlayedAtLeast		UMETA(DisplayName = "本回合已出牌数≥N"),
};

/**
 * 符文触发器的配表形式。对应逻辑层 FHexRuneTrigger。
 *
 * ⚠️ ValueAdd / ValueMult 仅在 When = OnAttack 时有效（②′ 数值钩子）。
 *    挂在别的时机上不报错也不生效 —— 这是最容易配错的一处。
 */
USTRUCT(BlueprintType)
struct HEXSPIRE_API FHexRuneTriggerRow
{
	GENERATED_BODY()

	/** 挂在哪个时机上（§6.3 统一时机表） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "触发")
	EHexTriggerTiming When = EHexTriggerTiming::OnRoundStart;

	// ═════════════════════════════════════ 过滤器（只对卡牌类时机有意义）

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "过滤")
	bool bFilterByCardType = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "过滤",
		meta = (EditCondition = "bFilterByCardType"))
	EHexCardType FilterCardType = EHexCardType::Attack;

	/** 只认带此标签的卡（空 = 不过滤）。OnStatusExpired 时是状态 id。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "过滤")
	FName FilterTag;

	/** 费用区间过滤，-1 = 不限 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "过滤",
		meta = (ClampMin = "-1"))
	int32 FilterCostMin = -1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "过滤",
		meta = (ClampMin = "-1"))
	int32 FilterCostMax = -1;

	// ═════════════════════════════════════ 条件

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "条件")
	EHexRuneConditionKind ConditionKind = EHexRuneConditionKind::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "条件")
	float ConditionFloat = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "条件")
	int32 ConditionInt = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "条件")
	FName ConditionName;

	// ═════════════════════════════════════ 效果

	/** 触发时执行的效果步骤（复用卡牌的步骤结构） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "效果")
	TArray<FHexEffectStepRow> Effects;

	// ═════════════════════════════════════ 限次（R7 防自激）

	/** 每回合最多触发次数，-1 = 无限 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "限次",
		meta = (ClampMin = "-1"))
	int32 MaxPerRound = -1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "限次",
		meta = (ClampMin = "-1"))
	int32 MaxPerBattle = -1;

	/** "每 N 次才触发一次"的累积阈值。0/1 = 每次都触发。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "限次",
		meta = (ClampMin = "0"))
	int32 CounterThreshold = 0;

	// ═════════════════════════════════════ ②′ 数值钩子（仅 OnAttack）

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "数值钩子")
	bool bHasValueAdd = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "数值钩子",
		meta = (EditCondition = "bHasValueAdd"))
	float ValueAddFlat = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "数值钩子",
		meta = (EditCondition = "bHasValueAdd"))
	FName ValueAddStatRef = TEXT("ATK");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "数值钩子",
		meta = (EditCondition = "bHasValueAdd"))
	float ValueAddRatio = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "数值钩子")
	bool bHasValueMult = false;

	/** 乘区（1.4 = ×1.4）。§6.4：乘区符文必须稀有（≤8%）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "数值钩子",
		meta = (EditCondition = "bHasValueMult"))
	float ValueMult = 1.0f;

	FHexRuneTrigger ToTrigger() const;
	void FromTrigger(const FHexRuneTrigger& In);
};

/**
 * 符文配表行。
 *
 * ⚠️ RowName 必须等于符文 Id（如 rune_echo）。
 *    不一致会让覆写静默落空。Loader 会校验并报错。
 */
USTRUCT(BlueprintType)
struct HEXSPIRE_API FHexRuneTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "基础")
	FString DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "基础")
	EHexRarity Rarity = EHexRarity::Common;

	/** §6.4 类别。占比由 VerifyRunes 钉住，配表扩量时同样受约束。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "基础")
	EHexRuneCategory Category = EHexRuneCategory::Trigger;

	/** ⚠️ 仅用于掉落加权/UI筛选/图鉴，不产生任何加成（§6.7） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "基础")
	TArray<FName> Tags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "触发")
	TArray<FHexRuneTriggerRow> Triggers;

	/** 规则改写（复用英雄被动的配表结构 —— 同一套逻辑层实现） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "规则改写")
	TArray<FHexRuleOverrideRow> RuleOverrides;

	/** 注入卡组的衍生卡 id（随符文移除而消失，不占容量） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "衍生卡")
	TArray<FName> InjectedCardIds;

	/** 诅咒符文不进层结算三选一（只出现在事件房/高腐蚀掉落） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "基础")
	bool bIsCursed = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "文本",
		meta = (MultiLine = "true"))
	FString FlavorText;

	/**
	 * ⚠️ 必须精确到"何时触发、触发几次、与什么交互"（R8）。
	 *    VerifyRunes 强制 ≥30 字。描述含糊的符文等于不存在。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "文本",
		meta = (MultiLine = "true"))
	FString MechanicText;

	/**
	 * 转成逻辑层的符文定义。
	 * @param InId 符文 Id（取自 RowName）
	 */
	FHexRuneData ToRuneData(FName InId) const;

	/** 由逻辑层结构填充本行（导出 CSV 用） */
	void FromRuneData(const FHexRuneData& In);
};
