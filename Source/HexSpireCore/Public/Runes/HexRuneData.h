// Copyright Hex Spire. All Rights Reserved.
//
// 符文系统 —— D6 / 策划案 §6、§15.6
//
// 符文【不是】属性词条集合，【不是】羁绊套装。
// 符文是《英雄联盟》海克斯强化 + 《小丑牌》小丑卡式的【独立加成牌】：
// 每张自己做一件明确的怪事，玩家把它们摆在一起，
// 组合是玩家自己研究出来的，游戏不提供"该凑什么"的提示表。
//
// ⚠️ 三条不可动摇的设计约束：
//   ① 纯数值符文（「ATK +10%」）占比必须为 0 ——
//      那种符文只会让玩家"拿数值最高的那个"，选择是假的（§6.4）。
//   ② 标签只用于掉落加权 / UI 筛选 / 图鉴分类，【不产生任何游戏内加成】（§6.7）。
//   ③ 6 槽【有序】，同时机按槽位 1→6 结算 —— 这是免费的一层深度（§6.5）。

#pragma once

#include "CoreMinimal.h"
#include "Core/HexSpireEnums.h"
#include "Core/HexSpireConstants.h"
#include "Battle/HexCardData.h"

/** 规则改写（§15.6 RuleOverride）—— D6 的核心武器 */
struct HEXSPIRECORE_API FHexRuleOverride
{
	EHexGameRule Rule = EHexGameRule::EnergyMax;

	int32 IntValue = 0;
	float FloatValue = 0.0f;
	bool bBoolValue = false;

	/**
	 * 多个 override 冲突时的应用顺序。
	 * 同 ApplyOrder 时按槽位升序 —— 保证确定性（纪律 5）。
	 */
	int32 ApplyOrder = 0;

	/**
	 * 是否为"增量"而非"覆盖"。
	 * 增量型（如 EnergyMax +2）可叠加；覆盖型（如 SizeClassOverride = L）取最后一个。
	 *
	 * ⚠️ 这个区分是必须的：两个符文都写 EnergyMax，
	 *    若都当覆盖则后者吃掉前者，玩家会觉得"我的符文没生效"（R8 的典型症状）。
	 */
	bool bIsDelta = true;
};

/** 符文触发器（§15.6 RuneTrigger）—— 挂在 §6.3 的共享时机表上 */
struct HEXSPIRECORE_API FHexRuneTrigger
{
	EHexTriggerTiming When = EHexTriggerTiming::OnRoundStart;

	/** 过滤器：只在特定卡类型 / 标签 / 费用时触发 */
	bool bFilterByCardType = false;
	EHexCardType FilterCardType = EHexCardType::Attack;
	FName FilterTag;
	int32 FilterCostMin = -1;
	int32 FilterCostMax = -1;

	FHexEffectCondition Condition;

	TArray<FHexEffectStep> Effects;

	/** 防无限循环的硬上限（R7）。-1 = 无限 */
	int32 MaxPerRound = -1;
	int32 MaxPerBattle = -1;

	/** 用于"每 N 次…"类效果的内部计数阈值 */
	int32 CounterThreshold = 0;

	// ── ②′ 数值钩子（仅 OnAttack 时机有效）
	//
	// 这两个字段是 §6.5「顺序影响结算」得以成立的载体：
	//   ValueAdd 在 running value 上做 +=
	//   ValueMult 在 running value 上做 *=
	// 同一时机的多个符文按槽位 1→6 依次作用，因此
	//   [加区, 乘区] ≠ [乘区, 加区]

	/** 平坦加值（可系数化：Flat + Stat×Ratio） */
	bool bHasValueAdd = false;
	float ValueAddFlat = 0.0f;
	FName ValueAddStatRef = TEXT("ATK");
	float ValueAddRatio = 0.0f;

	/** 乘区（1.4 表示 ×1.4） */
	bool bHasValueMult = false;
	float ValueMult = 1.0f;
};

/** 符文定义（§15.6 RuneData） */
struct HEXSPIRECORE_API FHexRuneData
{
	FName Id;
	FString DisplayName;
	EHexRarity Rarity = EHexRarity::Common;
	EHexRuneCategory Category = EHexRuneCategory::Trigger;

	/** ⚠️ 仅用于掉落加权/UI筛选/图鉴，不产生任何加成（§6.7） */
	TArray<FName> Tags;

	TArray<FHexRuneTrigger> Triggers;
	TArray<FHexRuleOverride> RuleOverrides;

	/** 注入卡组的衍生卡 id（随符文移除而移除） */
	TArray<FName> InjectedCardIds;

	bool bIsCursed = false;

	FString FlavorText;

	/**
	 * ⚠️ 必须精确到"何时触发、触发几次、与什么交互"（§13.2 / R8）。
	 *
	 * R8 是真实风险：符文效果玩家看不懂 → 组合无法推理 → D6 的价值归零。
	 * 描述含糊的符文等于不存在。
	 */
	FString MechanicText;
};

/**
 * 6 槽有序容器（§15.6 RuneLoadout）。
 *
 * 槽位固定 6 个，开局全开（§6.2）。
 * 「空槽是邀请，不是缺失」——《小丑牌》开局就是 5 个空槽，
 * 玩家反而立刻开始规划。
 */
class HEXSPIRECORE_API FHexRuneLoadout
{
public:
	FHexRuneLoadout();

	static constexpr int32 SlotCount = HexK::RuneSlotCount;

	/** 槽位内容，nullptr 表示空槽 */
	const FHexRuneData* GetSlot(int32 SlotIndex) const;

	void SetSlot(int32 SlotIndex, const FHexRuneData* Rune);

	void ClearSlot(int32 SlotIndex);

	/** 战斗外自由重排（§6.5）。战斗中应锁定，由调用方保证。 */
	void SwapSlots(int32 A, int32 B);

	/** 第一个空槽下标；满槽返回 INDEX_NONE */
	int32 FindFirstEmptySlot() const;

	int32 GetFilledCount() const;

	/**
	 * 按槽位 1→6 顺序返回非空符文（附带槽位号）。
	 * ⚠️ 这是 TriggerBus 的【唯一顺序来源】。
	 */
	void GetRunesInOrder(TArray<TPair<int32, const FHexRuneData*>>& Out) const;

	/**
	 * 聚合某条规则的最终值。
	 *
	 * 聚合规则：
	 *   · 先按 ApplyOrder 升序，同序按槽位升序（确定性）
	 *   · bIsDelta=true 的累加；bIsDelta=false 的覆盖
	 *
	 * @param OutFound 是否有任何符文改写了这条规则
	 * @return 增量总和（Delta 型）或最终覆盖值（Override 型）
	 */
	struct FAggregated
	{
		bool bFound = false;
		/** Delta 型的累加结果 */
		int32 IntDelta = 0;
		float FloatDelta = 0.0f;
		/** Override 型的最终值（bHasOverride=true 时有效） */
		bool bHasOverride = false;
		int32 IntOverride = 0;
		float FloatOverride = 0.0f;
		bool bBoolOverride = false;
	};

	FAggregated AggregateRule(EHexGameRule Rule) const;

	/** 全部被改写的规则（供 BattleState 预聚合缓存） */
	void GetOverriddenRules(TArray<EHexGameRule>& Out) const;

private:
	const FHexRuneData* Slots[SlotCount];
};
