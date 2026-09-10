// Copyright Hex Spire. All Rights Reserved.
//
// 内容库 —— 英雄 / 卡牌 / 敌人 / 怪物组 / 符文
//
// ⚠️ 本文件的每一个数值都是 Godot 版【实测调过】的结果（用户决策 q9：全盘沿用）。
//    每条注释记录了"为什么是这个数"，改之前先读注释。
//
// 为什么用代码内建而不是 DataAsset：
//   · 验证器与批量模拟可以零依赖地构造内容（不需要加载资产、不需要引擎启动）
//   · 灰盒期改数值就是改这个文件，比在编辑器里点资产快
//   · 数值全部走 §7.5 系数化，所以"改成 DataAsset"随时可做，结构完全一致

#pragma once

#include "CoreMinimal.h"
#include "Core/HexSpireEnums.h"
#include "Battle/HexCardData.h"
#include "Runes/HexRuneData.h"

// ⚠️ FHexUnit 是 class（见 Battle/HexUnit.h）。前置声明必须用 class，
//    否则 MSVC 会报 C4099 并在 /WX 下直接编译失败。
class FHexUnit;
struct FHexCardInstance;

/** 英雄定义 */
struct HEXSPIRECORE_API FHexHeroData
{
	FName Id;
	FString DisplayName;
	EHexSizeClass SizeClass = EHexSizeClass::S;

	int32 BaseHP = 80;
	int32 BaseATK = 10;
	int32 BaseDEF = 8;
	int32 BaseAGI = 6;
	int32 BaseLUK = 5;
	int32 BaseCRIT = 10;

	int32 EnergyMax = 5;
	int32 CardsDrawnPerTurn = 5;

	/** 3 张基石卡（含专属变体） */
	TArray<FName> CornerstoneCardIds;

	/** 掉落加权标签 */
	TArray<FName> CardPoolTags;

	/** 被动天赋（复用规则改写结构） */
	TArray<FHexRuleOverride> PassiveRules;
	FString PassiveText;
};

/** 敌人定义 */
struct HEXSPIRECORE_API FHexEnemyData
{
	FName Id;
	FString DisplayName;
	EHexSizeClass SizeClass = EHexSizeClass::S;

	int32 BaseHP = 24;
	int32 BaseATK = 13;
	int32 BaseDEF = 2;
	int32 BaseAGI = 8;
	int32 BaseLUK = 0;
	int32 BaseCRIT = 0;

	EHexAIProfile AIProfile = EHexAIProfile::Aggressive;
	EHexIntentTargeting IntentTargeting = EHexIntentTargeting::FixedTile;

	bool bIsElite = false;
	bool bIsBoss = false;
	int32 KnockbackResistOverride = -1;

	FString CodexText;
};

/** 怪物组中的一项 */
struct HEXSPIRECORE_API FHexEncounterEntry
{
	FName EnemyId;
	int32 Count = 1;
};

struct HEXSPIRECORE_API FHexContentLibrary
{
	// ═══════════════════════════════════════ 英雄

	static const FHexHeroData* FindHero(FName Id);
	static const TArray<FHexHeroData>& AllHeroes();

	// ═══════════════════════════════════════ 卡牌

	static const FHexCardData* FindCard(FName Id);
	static const TArray<FHexCardData>& AllCards();

	/**
	 * 构造英雄的初始卡组。
	 *
	 * ⚠️ 用户决策 q18：起始卡组缩到 4–5 张普通卡（留 3–4 个空位）。
	 *    理由：卡池只有 11 张，若起始就占满 8/8 容量，
	 *    「收哪张卡 / 挤掉哪张卡」这个核心决策（§3.2）在第一层根本触发不了。
	 *
	 * @param OutDeck  输出卡实例（uid 从 1 开始连续分配）
	 */
	/**
	 * 构建起始卡组与固定卡。
	 *
	 * ⚠️ 基石卡（攻击/防御/移动）【不再进卡组】，而是走 OutFixedCards：
	 *    它们常驻可用、不进抽牌堆、打出后不进弃牌堆。
	 *    这样抽到的每一张都是玩家构筑出来的技能卡，
	 *    也不会出现"这回合没抽到移动所以走不了路"的随机挫败。
	 *
	 * ⚠️ 参数是两个而非一个，是【故意】的：
	 *    改成分离式时若保留单参数版本，忘记取固定卡的调用点会
	 *    静默得到"没有基础动作"的角色。多一个出参能让编译器
	 *    把所有调用点逼出来。
	 */
	static void BuildStartingDeck(
		const FHexHeroData& Hero,
		TArray<struct FHexCardInstance>& OutDeck,
		TArray<struct FHexCardInstance>& OutFixedCards);

	/** 掉落池：不在起始卡组里的卡（供战斗后掉落） */
	static void GetLootableCardIds(const FHexHeroData& Hero, TArray<FName>& Out);

	// ═══════════════════════════════════════ 敌人

	static const FHexEnemyData* FindEnemy(FName Id);
	static const TArray<FHexEnemyData>& AllEnemies();

	/**
	 * 由敌人定义构造战场单位，含层数与腐蚀度缩放。
	 * @param FloorIndex 层数（1 起）
	 * @param Corruption 腐蚀度（§9.4：每 +1 敌人 HP/ATK 提升一档）
	 */
	static FHexUnit MakeEnemyUnit(const FHexEnemyData& Data, int32 FloorIndex, int32 Corruption);

	// ═══════════════════════════════════════ 怪物组

	static void GetEncounter(FName EncounterId, TArray<FHexEncounterEntry>& Out);
	static const TArray<FName>& AllEncounterIds();

	// ═══════════════════════════════════════ 符文（D6）

	static const FHexRuneData* FindRune(FName Id);
	static const TArray<FHexRuneData>& AllRunes();

	/** 按稀有度筛选（三选一抽取用） */
	static void GetRuneIdsByRarity(EHexRarity Rarity, TArray<FName>& Out);
};
