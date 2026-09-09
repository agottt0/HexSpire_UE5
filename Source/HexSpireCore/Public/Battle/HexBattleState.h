// Copyright Hex Spire. All Rights Reserved.
//
// 战斗全状态 —— 策划案 §14.1
//
// 纯逻辑，零 Actor 依赖（纪律 3）。可序列化（纪律 2/5）。
//
// ══ 事件日志是逻辑层与表现层的唯一接口 ══
// 逻辑【瞬时算完】，把纯数据事件追加到 EventLog；
// 表现层每帧 Drain 取走并播动画。这带来三个红利：
//   · 战斗能在无渲染下跑完 → BattleSim / 平衡矩阵 / 全部验证器成立
//   · "一键跳过动画" = 把播放间隔设成 0
//   · 确定性可验证：同 seed 两次运行的 ActionLog 哈希逐位相同

#pragma once

#include "CoreMinimal.h"
#include "Core/HexSpireEnums.h"
#include "Hex/HexGrid.h"
#include "Battle/HexUnit.h"
#include "Battle/HexGameAction.h"
#include "Deck/HexPileManager.h"
#include "Rng/HexRngStreams.h"
#include "Runes/HexRuneData.h"
#include "Equip/HexEquipData.h"

/** 给表现层回放的一条事件（纯数据，可序列化） */
struct HEXSPIRECORE_API FHexBattleEvent
{
	FName Type;

	int32 SourceUnitId = -1;
	int32 TargetUnitId = -1;
	int32 IntA = 0;
	int32 IntB = 0;
	int32 IntC = 0;
	FName NameA;
	FIntVector CoordA = FIntVector::ZeroValue;
	FIntVector CoordB = FIntVector::ZeroValue;
	bool bFlagA = false;
	FString TextA;

	void Serialize(FArchive& Ar);
};

/** 规则违规记录（R7 安全闸的产物）。绝不抛异常，只记录。 */
struct HEXSPIRECORE_API FHexRuleViolation
{
	FName Kind;
	FString Detail;
	int32 Round = 0;
};

/**
 * 战斗全状态。
 *
 * ⚠️ 唯一允许修改本类的地方是 FHexActionResolver（纪律 2）。
 *    其他任何地方只读。
 */
class HEXSPIRECORE_API FHexBattleState
{
public:
	explicit FHexBattleState(uint64 MasterSeed = 0);

	// ───────────────────────────────────────────── 核心状态

	FHexGrid Grid;
	FHexPileManager Piles;
	FHexRngStreams Rng;

	EHexBattlePhase Phase = EHexBattlePhase::BattleStart;
	int32 RoundNumber = 0;

	int32 Energy = 0;
	int32 CardsPlayedThisRound = 0;

	/** 玩家英雄的单位 id（便捷索引；仍允许多个玩家方单位 —— 纪律 4） */
	int32 HeroUnitId = -1;

	// ── 英雄基线（RuleBook 的 fallback 值来源）
	int32 HeroEnergyMaxBase = 5;
	int32 HeroDrawBase = 5;
	int32 DeckCapacityBase = 8;

	// ── 英雄被动天赋（§4.2）
	//
	// ⚠️ 被动复用符文的规则改写结构，但【不占符文槽】——
	//    它是英雄的身份，不是玩家的选择。
	//
	//    这个字段曾经缺失：HeroData.PassiveRules 有定义，
	//    RebuildRuleAggregate 却只聚合符文与装备，于是镇妖者的
	//    核心被动「格挡不清空」在实战中完全没生效 ——
	//    不报错、不崩溃，只是这个角色的定位悄悄消失了。
	//    由 VerifyBattle 的"回合结束后格挡未被清空"断言钉住。
	TArray<FHexRuleOverride> HeroPassiveRules;

	// ── 符文（D6）
	FHexRuneLoadout RuneLoadout;

	/**
	 * 装备（§5）。三槽：武器 / 盔甲 / 饰品。
	 *
	 * ⚠️ 与符文共用规则聚合与触发分发，但结算【永远在符文之后】
	 *    （TriggerBus 的 SlotOrder 10-12）。
	 *    这个先后顺序不是随意定的：符文改写规则、装备提供数值，
	 *    "先定规则再算数值"才符合玩家的心智模型。
	 */
	FHexEquipLoadout EquipLoadout;

	/**
	 * 规则预聚合缓存。
	 * 由 RebuildRuleAggregate() 在 BattleStart 与任何 loadout 变更时一次性算好。
	 * ⚠️ RuleBook 只读这个缓存，不直接读 RuneLoadout ——
	 *    否则每次读规则都要遍历 6 个槽，在 10 万场模拟里是可观开销。
	 */
	TMap<EHexGameRule, FHexRuneLoadout::FAggregated> RuleAggregate;

	// ── 局内进度（腐蚀度等，影响敌人缩放）
	int32 FloorIndex = 1;
	int32 Corruption = 0;

	// ───────────────────────────────────────────── 单位管理

	/** 添加单位，返回分配的 id */
	int32 AddUnit(const FHexUnit& Unit);

	FHexUnit* FindUnit(int32 UnitId);
	const FHexUnit* FindUnit(int32 UnitId) const;

	FHexUnit* GetHero();
	const FHexUnit* GetHero() const;

	/**
	 * 全部单位，按 id 升序（确定性遍历的唯一入口）。
	 * ⚠️ 纪律 5：禁止依赖容器内部顺序做逻辑判断。
	 */
	const TArray<FHexUnit>& GetUnits() const { return Units; }
	TArray<FHexUnit>& GetUnitsMutable() { return Units; }

	/** 存活的敌方单位 id，按 id 升序 */
	void GetAliveEnemyIds(TArray<int32>& Out) const;

	/** 存活的玩家方单位 id，按 id 升序 */
	void GetAlivePlayerIds(TArray<int32>& Out) const;

	/**
	 * 敌方行动顺序：按 AGI 降序，同值按 id 升序（确定性 tiebreak，§12.1 纪律 5）。
	 */
	void GetEnemyActionOrder(TArray<int32>& Out) const;

	/** 占据某格的单位；无则 nullptr */
	FHexUnit* FindUnitAtCell(const FIntVector& Cell);
	const FHexUnit* FindUnitAtCell(const FIntVector& Cell) const;

	/** 重建网格占据信息（单位增删/移动后调用） */
	void RebuildOccupancy();

	// ───────────────────────────────────────────── 规则聚合

	/** 重算 RuleAggregate。loadout / 装备变更时必须调用。 */
	void RebuildRuleAggregate();

	// ───────────────────────────────────────────── 事件日志

	void LogEvent(const FHexBattleEvent& Event);

	/** 便捷版：只填类型与两个单位 id */
	void LogEvent(FName Type, int32 SourceId = -1, int32 TargetId = -1);

	/** 取走全部事件（表现层每帧调用） */
	void DrainEvents(TArray<FHexBattleEvent>& Out);

	int32 NumPendingEvents() const { return EventLog.Num(); }

	// ───────────────────────────────────────────── 动作日志（确定性验证 / 战报 / 回放）

	void LogAction(const FHexGameAction& Action);
	const TArray<FHexGameAction>& GetActionLog() const { return ActionLog; }

	/** 动作日志哈希 —— 同 seed 两次运行必须逐位相同 */
	uint32 ActionLogHash() const;

	// ───────────────────────────────────────────── 违规记录（R7）

	void AddRuleViolation(FName Kind, const FString& Detail);
	const TArray<FHexRuleViolation>& GetRuleViolations() const { return RuleViolations; }
	bool HasViolations() const { return RuleViolations.Num() > 0; }

	// ───────────────────────────────────────────── 胜负

	/** 敌方全灭 */
	bool IsPlayerVictorious() const;

	/** 玩家方全灭 */
	bool IsPlayerDefeated() const;

	// ───────────────────────────────────────────── 快照（Undo）

	/**
	 * 深拷贝快照。
	 * ⚠️ Undo 用快照而非逆动作 —— 逆动作在有状态/位移/牌堆的系统里必然写错
	 *    （架构文档 §4.7）。回合制下几 KB 的深拷贝零压力。
	 */
	TSharedPtr<FHexBattleState> Snapshot() const;

	void RestoreFrom(const FHexBattleState& Other);

	// ───────────────────────────────────────────── 序列化

	void Serialize(FArchive& Ar);

	/** 整体内容哈希 —— 确定性验证用 */
	uint32 ContentHash() const;

private:
	/** 按 id 升序维护 */
	TArray<FHexUnit> Units;
	int32 NextUnitId = 1;

	TArray<FHexBattleEvent> EventLog;
	TArray<FHexGameAction> ActionLog;
	TArray<FHexRuleViolation> RuleViolations;
};
