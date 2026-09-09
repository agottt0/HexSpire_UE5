// Copyright Hex Spire. All Rights Reserved.
//
// 局内进度 —— 一层完整循环的状态持有者
//
// ══════════════════════════════════════════════════════════════════
// 边界：本类【只管一局】，不做存档
// ══════════════════════════════════════════════════════════════════
// 用户决策 q24：碎片仅本局内消耗，Demo 结束即清空，不做跨局元进阶。
// 所以这里没有"元进阶树""永久解锁"这类字段 ——
// 加它们需要配套的存档系统与解锁 UI，不在第一版范围内。
//
// ⚠️ 但结构上留好了扩展位：RunState 整体可序列化，
//    将来做存档时把它整个写盘即可，不需要重构。
//
// ══════════════════════════════════════════════════════════════════
// 腐蚀度（§9.4）—— D4 风险收益的另一半
// ══════════════════════════════════════════════════════════════════
// 玩家每清一间房，腐蚀度 +1（精英 +2）。腐蚀度同时做两件事：
//   · 敌人 HP +8%/点、ATK +6%/点（见 FHexContentLibrary::MakeEnemyUnit）
//   · 掉落稀有度上移（见 FHexEquipGenerator::RollRarity）
//
// 这个双向作用是 D4 成立的前提：
//   若只加强敌人，最优策略是"尽量少探，直奔 Boss" → 探索没有理由发生
//   若只改善掉落，最优策略是"探完所有房" → 决策也不存在
// 两者同时存在，"再探一间还是收手"才成为真问题。

#pragma once

#include "CoreMinimal.h"
#include "Core/HexSpireEnums.h"
#include "Core/HexSpireConstants.h"
#include "Map/HexFloorMap.h"
#include "Equip/HexEquipData.h"
#include "Runes/HexRuneData.h"
#include "Deck/HexPileManager.h"

class FHexRngStreams;

/** 层结算界面的一项奖励选项 */
struct HEXSPIRECORE_API FHexRewardOption
{
	enum class EKind : uint8
	{
		Rune = 0,
		Card = 1,
		Equip = 2,
		/** 卡组容量 +N */
		DeckCapacity = 3,
		/** 定向碎片 */
		Shards = 4,
	};

	EKind Kind = EKind::Rune;

	/** Rune / Card / Equip 的 id */
	FName ContentId;

	/** DeckCapacity / Shards 的数量 */
	int32 Amount = 0;

	FString DisplayName;
	FString Description;
};

/** 本层的统计数据（层结算界面展示，也是平衡模拟的数据源） */
struct HEXSPIRECORE_API FHexFloorStats
{
	int32 RoomsCleared = 0;
	int32 BattlesWon = 0;
	int32 TotalRounds = 0;
	int32 DamageDealt = 0;
	int32 DamageTaken = 0;
	int32 CardsPlayed = 0;
	int32 EnemiesKilled = 0;

	/** 战斗结束时的剩余生命百分比（判断"硬但公平"是否达标） */
	float EndHPRatio = 1.0f;

	void Reset() { *this = FHexFloorStats(); }
	void Serialize(FArchive& Ar);
};

/**
 * 一局游戏的进度状态。
 *
 * ⚠️ 与 FHexBattleState 的分工：
 *    BattleState = 一场战斗内的状态（单位、牌堆、地形），战斗结束即弃
 *    RunState    = 跨战斗的状态（卡组、符文、装备、腐蚀度、地图）
 *    这个分界让"战斗可以被独立模拟"（BattleSim 只需 BattleState），
 *    是全部验证器与平衡矩阵成立的基础。
 */
class HEXSPIRECORE_API FHexRunState
{
public:
	explicit FHexRunState(uint64 InMasterSeed = 0);

	// ───────────────────────────────────────────── 开局

	/**
	 * 开始一局。
	 * @param HeroId 英雄 id（第一版只有 warden）
	 */
	void BeginRun(FName HeroId, FHexRngStreams& Rng);

	/** 进入下一层（生成地图、重置层统计） */
	void BeginFloor(int32 InFloorIndex, FHexRngStreams& Rng);

	// ───────────────────────────────────────────── 核心状态

	FName HeroId;

	int32 FloorIndex = 1;

	/**
	 * 腐蚀度。
	 * ⚠️ 只增不减 —— 它是"这一层我探了多深"的账本。
	 *    允许减少会让玩家找到"刷房间但回撤腐蚀度"的漏洞。
	 */
	int32 Corruption = 0;

	/** 本局碎片（q24：仅本局内消耗，用于重塑装备与层结算的定向分配） */
	int32 Shards = 0;

	/** 卡组容量上限（层结算可 +1~2） */
	int32 DeckCapacity = HexK::InitialDeckCapacity;

	/** 完整卡组（含基石卡与装备注入的衍生卡） */
	TArray<FHexCardInstance> Deck;

	FHexRuneLoadout RuneLoadout;
	FHexEquipLoadout EquipLoadout;

	/** 玩家持有但未装备的符文/装备（层结算与营地里可换） */
	TArray<FName> RuneInventory;
	TArray<FHexEquipInstance> EquipInventory;

	FHexFloorMap Map;
	FHexFloorStats Stats;

	/** 英雄的持久生命（跨战斗保留 —— 这是肉鸽的核心压力来源） */
	int32 HeroHP = 80;
	int32 HeroHPMax = 80;

	// ───────────────────────────────────────────── 卡组容量（D3）

	/**
	 * 当前占用的卡组容量（基石卡与衍生卡不计，§7.6）。
	 */
	int32 GetUsedCapacity() const;

	/** 是否还有空位 */
	bool HasCapacityRoom() const { return GetUsedCapacity() < DeckCapacity; }

	/**
	 * 加一张卡进卡组。
	 *
 	 * ⚠️ D3 的核心是【替换制】而非无限膨胀：
	 *    容量满时必须先挤掉一张。所以这里在满容量时【返回 false】，
	 *    由 UI 强制玩家做"挤掉哪张"的选择（§3.2 的四大决策之一）。
	 *
	 * @return 是否成功
	 */
	bool AddCardToDeck(FName CardId);

	/** 从卡组移除一张（按实例 uid） */
	bool RemoveCardFromDeck(int32 Uid);

	/** 同名卡在卡组中的份数（用于 MaxCopiesInDeck 校验） */
	int32 CountCopiesInDeck(FName CardId) const;

	// ───────────────────────────────────────────── 房间推进

	/**
	 * 清空当前房间：累加腐蚀度、更新统计。
	 * @return 本次增加的腐蚀度
	 */
	int32 OnRoomCleared();

	/** 营地：恢复生命（§9.6） */
	int32 RestAtCamp();

	// ───────────────────────────────────────────── 层结算（§9.5）

	/**
	 * 生成层结算的三选一奖励。
	 *
	 * 用户决策 q17：层结算 = 三选一符文 + 卡组容量 +1~2 +
	 *                定向碎片分配 + 本层统计。
	 *
	 * ⚠️ 三选一里【不重复】已持有的符文 ——
	 *    给一个已经装着的符文等于"这一选没有选项"，
	 *    在只有 17 个符文的第一版里必须显式排除。
	 */
	void GenerateFloorRewards(FHexRngStreams& Rng, TArray<FHexRewardOption>& Out) const;

	/** 应用一个奖励选项 */
	bool ApplyReward(const FHexRewardOption& Option, FHexRngStreams& Rng);

	// ───────────────────────────────────────────── 序列化

	void Serialize(FArchive& Ar);

	uint32 ContentHash() const;

	uint64 GetMasterSeed() const { return MasterSeed; }

private:
	/** 卡实例 uid 分配器（0 保留为无效） */
	int32 NextCardUid = 1;

	uint64 MasterSeed = 0;
};
