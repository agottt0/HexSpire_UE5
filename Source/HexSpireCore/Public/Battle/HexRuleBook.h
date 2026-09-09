// Copyright Hex Spire. All Rights Reserved.
//
// 每条 GameRule 的【唯一消费点】—— 策划案 §15.6 的硬要求
//
// 策划案原文：「每条 GameRule 在代码中只有一个消费点」。
// 这个要求靠自觉是守不住的（加符文时会散落十几处偷读），
// 所以用 注册表 + 源码扫描 机械强制：
//   · Consumers 把每条规则映射到唯一函数名
//   · CheckDiscipline 扫描源码，除 HexSpireEnums.h / HexRuleBook.* /
//     HexRuneData.* 与数据声明文件外，出现 EHexGameRule::XXX 即报错
//
// 用法：所有需要读取"当前有效规则值"的地方都调这里的函数，
//       禁止直接读 State.RuleAggregate，更禁止读 RuneLoadout。
//
// ⚠️ 绕过 RuleBook 的后果是【静默的】：符文改写不生效，
//    玩家会觉得"我装了符文但没用"，而代码不会报任何错。这正是 R8 的典型症状。

#pragma once

#include "CoreMinimal.h"
#include "Core/HexSpireEnums.h"

class FHexBattleState;
class FHexUnit;

struct HEXSPIRECORE_API FHexRuleBook
{
	// ───────────────────────────────────────────── 体力与抽牌

	/** EnergyMax */
	static int32 EnergyMax(const FHexBattleState& State);

	/** CardsDrawnPerTurn */
	static int32 CardsDrawnPerTurn(const FHexBattleState& State);

	/** HandLimit */
	static int32 HandLimit(const FHexBattleState& State);

	/** DeckCapacity */
	static int32 DeckCapacity(const FHexBattleState& State);

	/** NoDrawFixedHand */
	static bool IsFixedHand(const FHexBattleState& State);

	/** FirstCardFree */
	static bool IsFirstCardFree(const FHexBattleState& State);

	// ───────────────────────────────────────────── 卡牌费用

	/**
	 * 实际费用 = 卡面 Cost + CardCostDelta，最低 0。
	 * 若本回合尚未打牌且 FirstCardFree 生效 → 0。
	 *
	 * @param CardsPlayedThisRound < 0 时从 State 取
	 */
	static int32 CardCost(const FHexBattleState& State, int32 BaseCost, int32 CardsPlayedThisRound = -1);

	/** ExhaustAllAttacks */
	static bool AttacksExhaust(const FHexBattleState& State);

	// ───────────────────────────────────────────── 体型与位移

	/** SizeClassOverride —— 体型可被符文改写（《巨化》） */
	static EHexSizeClass SizeClassOf(const FHexBattleState& State, const FHexUnit& Unit);

	/** KnockbackImmune */
	static int32 KnockbackResistOf(const FHexBattleState& State, const FHexUnit& Unit);

	/** MoveCostDelta */
	static int32 MoveCostDelta(const FHexBattleState& State);

	// ───────────────────────────────────────────── 格挡

	/** BlockPersists —— 镇妖者被动：格挡不在回合结束清空 */
	static bool BlockPersists(const FHexBattleState& State);

	/** NoBlockAllowed —— 《巨化》的代价之一 */
	static bool CanGainBlock(const FHexBattleState& State);

	/** BlockMultiplier */
	static float BlockMultiplier(const FHexBattleState& State);

	// ───────────────────────────────────────────── 伤害乘区

	/** DamageMultiplier */
	static float DamageMultiplier(const FHexBattleState& State);

	/** CritDamageMultiplier */
	static float CritDamageMultiplier(const FHexBattleState& State);

	// ───────────────────────────────────────────── 自检

	/**
	 * 校验每条 GameRule 都有且只有一个消费函数。
	 * 由 VerifyRules 调用 —— 新增 GameRule 却忘记加消费点时会失败。
	 */
	static bool VerifyAllRulesHaveConsumer(TArray<EHexGameRule>& OutMissing);

	/** 规则 → 消费函数名（自检与文档用） */
	static const TMap<EHexGameRule, FString>& Consumers();
};
