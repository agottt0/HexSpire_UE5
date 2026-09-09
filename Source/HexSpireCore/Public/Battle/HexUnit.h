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

	/** 施加状态（按 StackMode 处理叠加），返回实际生效后的层数 */
	int32 ApplyStatus(FName StatusId, int32 Stacks);

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
