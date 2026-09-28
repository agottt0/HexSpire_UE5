// Copyright Hex Spire. All Rights Reserved.
//
// 敌人技能定义 —— 策划案 §8.7 / §15.8
//
// ══════════════════════════════════════════════════════════════════
// 为什么复用卡牌的 FHexEffectStep / FHexTargetSpec 而不新造一套
// ══════════════════════════════════════════════════════════════════
// 敌人技能与卡牌要做的事高度重合：算伤害、施加状态、击退、获得格挡。
// 若为敌人另立一套效果结构，同一个「造成伤害」概念就有两份实现，
// 而伤害管线（§4.4）只允许有一个出口 —— 两份结构迟早会在
// StatRatio / Repeat / TargetFilter 的语义上漂移，且不会报错，
// 只会表现为「同样配 1.0×ATK，卡牌打 13 敌人打 11」。
//
// 复用之后，策划在卡表与敌人技能表里看到的字段完全一致，
// §7.5 的系数化规范也自动适用于敌人。
//
// ══════════════════════════════════════════════════════════════════
// 技能与「意图是承诺」的关系（⚠️ 最容易配错的地方）
// ══════════════════════════════════════════════════════════════════
// 技能【不是】在执行时才决定的。Decide() 阶段就要：
//   ① 选出本回合要用的技能
//   ② 用它的 TargetSpec 算出波及格并【冻结】进 Intent.TargetCells
//   ③ 把预计伤害冻结进 Intent.PredictedDamage
// 执行阶段只重放。这样玩家在预警里看到的范围与数字才是承诺。
//
// 因此技能的 TargetSpec 会直接决定 UI 上画出的预警范围 ——
// 配一个 Burst(AreaSize=2) 的技能，玩家回合就会看到一片高亮。
//
// ══════════════════════════════════════════════════════════════════
// ⚠️ 未配技能的敌人走【旧的硬编码路径】，行为与配表前完全一致
// ══════════════════════════════════════════════════════════════════
// SkillIds 为空时，AI 回到 AttackRangeOf() 的 profile 默认攻击。
// 这是刻意的：四只已有敌人的数值是 Godot 版实测调过的，
// 给它们塞一条「等价」的技能会引入 profile 射程（Boss 随阶段变化）
// 无法在表里表达的细节，静默改变平衡。
// 想让某只敌人走技能路径，显式给它配 SkillIds 即可。

#pragma once

#include "CoreMinimal.h"
#include "Core/HexSpireEnums.h"
#include "Battle/HexCardData.h"

/**
 * 一条敌人技能。
 *
 * 可用性由四个门槛共同决定（全部满足才可用）：
 *   · 冷却已走完
 *   · 回合数 >= FirstUsableRound
 *   * Boss 阶段 >= MinBossPhase
 *   · 自身 HP 比例 <= UseBelowSelfHPRatio（0 = 不设此门槛）
 * 再加一条硬条件：必须存在合法目标格。
 */
struct HEXSPIRECORE_API FHexEnemySkillData
{
	FName Id;
	FString DisplayName;

	/**
	 * 意图种类 —— 决定 UI 画什么图标与文案。
	 *
	 * ⚠️ 这只是【显示分类】，实际做什么由 Effects 决定。
	 *    配成 Buff 但 Effects 里写 DealDamage，玩家会看到"强化"
	 *    却挨一刀 —— §8.7 的可读性直接作废。配表时务必对应。
	 */
	EHexIntentKind IntentKind = EHexIntentKind::Attack;

	/**
	 * 是否用本技能自己的可躲性覆盖敌人的默认 IntentTargeting。
	 *
	 * 留 false 时沿用敌人自身设定 + 两段明示规则（贴身转追踪）。
	 * 置 true 可以做出「这只近战敌人平时可躲，但大招锁人」这种设计。
	 */
	bool bOverrideTargeting = false;
	EHexIntentTargeting Targeting = EHexIntentTargeting::FixedTile;

	/**
	 * 射程与形状。
	 *
	 * ⚠️ RangeMax 从【最近的己方 footprint 格】起算（§8.5），
	 *    所以 M/L 体型敌人的有效射程天然更长。按"格数"填即可。
	 */
	FHexTargetSpec TargetSpec;

	/**
	 * 按顺序执行的效果步骤。顺序有意义（§6.5）。
	 *
	 * 目前敌人侧支持的 op：
	 *   DealDamage / ApplyStatus / GainBlock / Heal / Knockback / Pull
	 * 其余 op 会记 unimplemented_enemy_op 违规而【不是】静默失效。
	 */
	TArray<FHexEffectStep> Effects;

	/** 多段攻击的段数（整组 Effects 重复 HitCount 次） */
	int32 HitCount = 1;

	/** 冷却回合数。0 = 每回合可用。在【执行成功后】开始计时。 */
	int32 CooldownRounds = 0;

	/** 最早可用的回合（1 起）。用来做"开场三回合内不放大招"。 */
	int32 FirstUsableRound = 1;

	/** Boss 阶段门槛（0 = 不限制）。配合 BossPhased profile 做阶段技。 */
	int32 MinBossPhase = 0;

	/**
	 * 自身 HP 比例门槛：HP/HPMax <= 此值时才可用。0 = 不设门槛。
	 * 用来做"残血时治疗自己 / 狂暴"。
	 */
	float UseBelowSelfHPRatio = 0.0f;

	/**
	 * 优先级。同时可用时取【大者】；相同则按 Id 字典序（确定性 tiebreak）。
	 *
	 * ⚠️ 刻意不做随机选取。敌人行为随机会让「预警 + 走位」的学习
	 *    过程失效 —— 玩家无法总结"它血低了会治疗"这类规律，
	 *    而可学习性正是 §13.2 要求的。
	 */
	int32 Priority = 0;

	/**
	 * 释放本技能时该播的动作。
	 *
	 * ⚠️ 这条与 IntentKind 是【两件事】，别指望其中一个推出另一个：
	 *    IntentKind 决定预警 UI 画什么图标（玩家回合看到的承诺），
	 *    CastAnim 决定敌人执行时身体怎么动。
	 *    一个 Debuff 意图完全可以是近战动作（贴上来抓一把），
	 *    按 IntentKind 推会把它播成施法，玩家学不会"它要贴身"。
	 *
	 * None = 没声明，表现层取保守默认。
	 */
	EHexUnitAnim CastAnim = EHexUnitAnim::None;

	/** 图鉴/预警文本，逻辑层不消费 */
	FString TelegraphText;

	/** 本技能的效果里是否含伤害步骤（决定要不要算预计伤害） */
	bool HasDamageStep() const;
};
