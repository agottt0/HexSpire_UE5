// Copyright Hex Spire. All Rights Reserved.
//
// 战场单位 —— 策划案 §8.3 / §14.1
//
// 纯逻辑，零 Actor 依赖（纪律 3）。可序列化（纪律 2/5）。
// ⚠️ 纪律 4：不假设我方只有 1 个单位。召唤物、被魅惑的敌人都是 Unit。

#pragma once

#include "CoreMinimal.h"
#include "Core/HexSpireEnums.h"
#include "Battle/HexStatusData.h"

/** 敌人意图（§8.7）。生成时【冻结】全部结算参数，执行时只重放。 */
struct HEXSPIRECORE_API FHexIntent
{
	EHexIntentKind Kind = EHexIntentKind::Sleep;

	/** 是否可躲（§13.2 要求玩家能读出这个区分） */
	EHexIntentTargeting Targeting = EHexIntentTargeting::FixedTile;

	/**
	 * 冻结的目标格集合。
	 * ⚠️ FixedTile 型意图执行时【不重算】—— 玩家走开就打空。
	 *    这是"意图必须是承诺"的技术实现（架构文档 §4.6）。
	 */
	TArray<FIntVector> TargetCells;

	/** 追踪型意图锁定的单位 id */
	int32 TrackedUnitId = -1;

	/**
	 * 本回合要施放的技能 id（空 = 走 profile 默认攻击）。
	 *
	 * ⚠️ 技能在 Decide() 阶段就【选定并冻结】，执行阶段只按这个 id 重放。
	 *    若执行时重选，同一个预警可能放出另一个技能 ——
	 *    玩家看到的范围与实际打到的范围不一致，「意图是承诺」直接作废。
	 */
	FName SkillId;

	/** 冻结的预计伤害（显示用，也是执行时的实际值） */
	int32 PredictedDamage = 0;

	/** 多段攻击的段数 */
	int32 HitCount = 1;

	/** 移动意图的落点与朝向 */
	FIntVector MoveToAnchor = FIntVector::ZeroValue;
	int32 MoveToFacing = 0;

	/** 行动后的朝向（意图中预告，§8.2.3） */
	int32 ResultFacing = 0;

	/** 要施加的状态 */
	FName StatusId;
	int32 StatusStacks = 0;

	bool IsValid() const { return Kind != EHexIntentKind::Sleep; }

	void Serialize(FArchive& Ar);
};

/**
 * 战场单位。
 *
 * ⚠️ 关键数值（HP、伤害）用 int32 —— 纪律 5：浮点只在中间计算用，
 *    避免跨平台浮点差异破坏确定性。
 */
class HEXSPIRECORE_API FHexUnit
{
public:
	int32 Id = -1;
	FString DisplayName;
	EHexTeam Team = EHexTeam::Enemy;

	/** 数据来源 id（HeroData/EnemyData），用于查图鉴与复现 */
	FName SourceId;

	// ── 位置与朝向
	FIntVector Anchor = FIntVector::ZeroValue;
	int32 Facing = 0;

	// ── 体型（D8）
	EHexSizeClass SizeClass = EHexSizeClass::S;

	// ── 六属性（§4.3）
	int32 HP = 1;
	int32 HPMax = 1;
	int32 ATK = 0;
	int32 DEF = 0;
	int32 AGI = 0;
	int32 LUK = 0;
	int32 CRIT = 0;

	// ── 战斗内临时状态
	int32 Block = 0;
	TArray<FHexStatusInstance> Statuses;
	bool bIsAlive = true;

	// ── 敌人专属
	EHexAIProfile AIProfile = EHexAIProfile::Aggressive;
	EHexIntentTargeting IntentTargeting = EHexIntentTargeting::FixedTile;
	FHexIntent Intent;
	bool bIsElite = false;
	bool bIsBoss = false;

	/** Boss 阶段（按 HP 百分比切换），0 = 非 Boss */
	int32 BossPhase = 0;

	/** -1 表示用体型默认值 */
	int32 KnockbackResistOverride = -1;

	// ── 敌人可配置项（由 FHexEnemyData 拷入，见 MakeEnemyUnit）
	//
	// ⚠️ 为什么拷到单位上而不是让 AI 去查 FHexContentLibrary：
	//    core 刻意不让战斗逻辑依赖具体内容 —— BattleFlow 查卡牌走的是
	//    注入的 CardLookup 回调，就是这个原因。AI 直接 include 内容库会
	//    把"战斗"与"第一版的四只怪"焊死，验证器再也无法构造一只
	//    "射程 5 的测试怪"来单测射程逻辑。
	//    拷贝的另一个好处：这些值进了 Serialize，存档/回放自带它们。

	/** 每回合移动格数预算 */
	int32 MoveBudget = 2;

	/** 风筝型的理想距离（只对 RangedKiter 有意义） */
	int32 PreferredDistance = 3;

	/** 攻击射程覆写；-1 = 用 AIProfile 默认射程 */
	int32 AttackRangeOverride = -1;

	/** 可用技能 id 列表，已按优先级降序排好（顺序即选取顺序） */
	TArray<FName> SkillIds;

	/**
	 * 技能冷却剩余回合数，与 SkillIds 一一对应（同下标）。
	 *
	 * ⚠️ 用平行数组而不是 TMap<FName,int32>：
	 *    TMap 的遍历顺序不保证，而"选第一条可用技能"必须是确定性的。
	 *    纪律 5 明确禁止依赖容器内部顺序做逻辑判断。
	 */
	TArray<int32> SkillCooldowns;

	// ───────────────────────────────────────────── 技能

	/** 某技能的剩余冷却；未配置该技能则返回 0 */
	int32 GetSkillCooldown(FName InSkillId) const;

	/** 置某技能的冷却（施放成功后调用） */
	void SetSkillCooldown(FName InSkillId, int32 Rounds);

	/** 全部技能冷却 -1（回合总结束时调用） */
	void TickSkillCooldowns();

	// ───────────────────────────────────────────── 几何

	/** 当前占据的全部格。⚠️ 唯一来源是 FHexFootprint（D8 单一出口） */
	void GetCells(TArray<FIntVector>& Out) const;

	const TArray<FIntVector>& GetFootprint() const;

	int32 GetCellCount() const;

	/** 相邻格（大体型显著更多 —— §8.2.2 机制点 3） */
	void GetAdjacentCells(TArray<FIntVector>& Out) const;

	/** 到某格的距离，从【最近的己方 footprint 格】起算（§8.2.2 机制点 3） */
	int32 DistanceToCell(const FIntVector& C) const;

	int32 DistanceToUnit(const FHexUnit& Other) const;

	/** 位移抗性：override 优先，否则取体型默认（§8.2.2 机制点 4） */
	int32 GetKnockbackResist() const;

	int32 GetRotateCost() const;
	bool CanTrampleBelow() const;
	bool CanCrushRubble() const;
	int32 GetLootScatterRadius() const;

	/** 前方方向向量。⚠️ 必须走 FacingDir（陷阱 H1），不能写 Dirs[Facing] */
	FIntVector GetForwardDir() const;

	/** 背击判定：攻击者格是否位于本单位的后弧（§8.2.3） */
	bool IsAttackedFromRear(const FIntVector& AttackerCell) const;

	bool IsHostileTo(const FHexUnit& Other) const;

	// ───────────────────────────────────────────── 状态效果

	/** 取某状态的层数，无则 0 */
	int32 GetStatusStacks(FName StatusId) const;

	bool HasStatus(FName StatusId) const;

	/**
	 * 施加状态（按 StackMode 处理叠加），返回实际生效后的层数。
	 *
	 * @param SourceUnitId 施加者。默认 -1 = 环境/未知。
	 *        ⚠️ 攻击方施加的 debuff【务必传入攻击者 id】，
	 *        否则燃烧/中毒致死时会被判为环境击杀，
	 *        OnKill 类符文（如《食魂》）在烧流下静默失效。
	 *        参数有默认值只是为了不惊动既有的几十处调用，
	 *        不代表可以省略。
	 */
	int32 ApplyStatus(FName StatusId, int32 Stacks, int32 SourceUnitId = -1);

	/** 移除状态 */
	void RemoveStatus(FName StatusId);

	/** 移除全部减益（"净化"类效果） */
	void RemoveAllDebuffs();

	/** 状态每回合衰减；输出本回合过期的状态 id */
	void DecayStatuses(TArray<FName>& OutExpired);

	/** 造成伤害的乘区总和（力量走平坦加攻，不在此） */
	float GetDamageDealtMultiplier() const;

	/** 受到伤害的乘区总和 */
	float GetDamageTakenMultiplier() const;

	/** 来自状态的平坦攻击加成 */
	int32 GetFlatAtkBonus() const;

	/** 来自状态的平坦格挡加成 */
	int32 GetFlatBlockBonus() const;

	/** 是否跳过本次行动（眩晕） */
	bool ShouldSkipTurn() const;

	/** 是否不可移动（定身） */
	bool IsMovementBlocked() const;

	/** 移动力减免（缓迟） */
	int32 GetMovePenalty() const;

	/** 护盾剩余吸收量 */
	int32 GetBarrierAmount() const;

	/** 消耗护盾，返回实际吸收量 */
	int32 ConsumeBarrier(int32 Amount);

	// ───────────────────────────────────────────── 格挡

	/**
	 * 格挡上限（§4.2 镇妖者被动「可叠加至上限」）。
	 * ⚠️ 上限缺失会让失败条件在数学上不存在 —— 见 HexK::BlockCapRatio 注释。
	 */
	int32 GetBlockCap() const;

	// ───────────────────────────────────────────── 序列化

	void Serialize(FArchive& Ar);

	uint32 ContentHash() const;
};
