// Copyright Hex Spire. All Rights Reserved.
//
// 状态效果定义表 —— 策划案 §8.9
//
// 统一数据驱动，不为每个状态写独立逻辑。
//
// ⚠️ 策划案只给出了字段框架与 10 个必备状态名，【没有任何数值】。
//    下面的数值由我拟定，设计依据写在每一条的注释里，
//    并由 VerifyStatus / VerifyBalance 机械校验其后果是否落在
//    策划案给定的节奏区间内（单场 3–5 回合、镇妖者能撑 4 回合）。

#pragma once

#include "CoreMinimal.h"
#include "Core/HexSpireEnums.h"

/**
 * 状态效果的静态定义。
 *
 * 修正字段的语义分三类，刻意分开而不是合并成一个通用 modifier：
 *   · DamageDealtMult  —— 影响"我打出去的伤害"（力量/虚弱）
 *   · DamageTakenMult  —— 影响"我受到的伤害"（易伤）
 *   · FlatAtkPerStack  —— 平坦加攻（走伤害管线 ②，可与乘区叠加）
 * 合并会导致"力量和易伤谁先算"这种问题无法回答。
 */
struct HEXSPIRECORE_API FHexStatusDef
{
	FName Id;
	FString DisplayName;

	EHexStackMode StackMode = EHexStackMode::StackIntensity;
	EHexStatusTick TickTiming = EHexStatusTick::None;

	/** 每回合层数变化（燃烧 -1，力量 0） */
	int32 DecayPerRound = 0;

	int32 MaxStack = 99;

	bool bIsDebuff = false;

	// ── 修正（每层的量）

	/** 每层给攻击者的平坦攻击加成（走伤害管线 ②） */
	int32 FlatAtkPerStack = 0;

	/** 每层给格挡的平坦加成 */
	int32 FlatBlockPerStack = 0;

	/** 造成伤害的乘区增量（虚弱为负） */
	float DamageDealtMult = 0.0f;

	/** 受到伤害的乘区增量（易伤为正） */
	float DamageTakenMult = 0.0f;

	// ── tick 伤害

	/** 每层每次 tick 造成的伤害 */
	int32 TickDamagePerStack = 0;

	/** tick 伤害是否无视格挡（中毒无视，燃烧不无视） */
	bool bTickIgnoresBlock = false;

	// ── 行动限制

	/** 跳过行动（眩晕） */
	bool bSkipTurn = false;

	/** 不可移动（定身） */
	bool bCannotMove = false;

	/** 移动时按层数结算伤害（流血） */
	int32 DamageOnMovePerStack = 0;

	/** 独立于 block 的吸收池（护盾），回合结束不清空 */
	bool bIsAbsorbShield = false;
};

struct HEXSPIRECORE_API FHexStatusLibrary
{
	/** 取状态定义；未知 id 返回一个无害的空定义 */
	static const FHexStatusDef& Get(FName Id);

	static bool Exists(FName Id);

	/** 全部状态 id，顺序固定（确定性遍历用） */
	static const TArray<FName>& AllIds();

	// 便捷常量，避免各处硬写字符串
	static const FName Strength;    // 力量
	static const FName Dexterity;   // 敏锐
	static const FName Weak;        // 虚弱
	static const FName Vulnerable;  // 易伤
	static const FName Burn;        // 燃烧
	static const FName Poison;      // 中毒
	static const FName Bleed;       // 流血
	static const FName Stun;        // 眩晕
	static const FName Root;        // 定身
	static const FName Barrier;     // 护盾
	static const FName Chill;       // 缓迟（巨岩之躯用，减移动）
};

/** 单位身上的一个状态实例 */
struct HEXSPIRECORE_API FHexStatusInstance
{
	FName Id;
	int32 Stacks = 0;
	/** 用于 StackDuration / RefreshDuration 模式 */
	int32 Duration = 0;
	/** 护盾类的剩余吸收量 */
	int32 AbsorbLeft = 0;

	/**
	 * 施加者的单位 id。-1 = 环境/未知。
	 *
	 * ══════════════════════════════════════════════════════════════
	 * 为什么状态必须记住"谁施加的"
	 * ══════════════════════════════════════════════════════════════
	 * 燃烧/中毒的 tick 伤害原先一律传 SourceId = -1，于是
	 * 「敌人被玩家点燃后烧死」时 OnKill 不会触发 ——
	 * 《食魂》(击杀→抽牌) 在烧流打法下完全失效，
	 * 而 OnUnitDeath 却正常触发，两者行为不一致且无任何报错。
	 *
	 * 后果不只是少抽一张牌：它让「上状态」与「靠击杀滚雪球」
	 * 这两条符文路线【永久互斥】，直接砍掉一大片 D6 组合空间。
	 *
	 * ⚠️ 叠加时保留【最后一次】施加者：符文组合里"最后补刀的人"
	 *    才是玩家心智中的击杀者。同时这也让玩家刷新状态后
	 *    必然拿回归属权，不会被之前的施加者抢走。
	 */
	int32 SourceUnitId = -1;

	void Serialize(FArchive& Ar);
};
