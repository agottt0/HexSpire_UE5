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

/**
 * 敌人定义（§15.8）。
 *
 * ⚠️ 六属性是【第 1 层、腐蚀度 0】的基线值。MakeEnemyUnit 会按层数与
 *    腐蚀度缩放 HP/ATK（DEF/AGI 刻意不缩放，见那边注释）。
 *    配表时填的是基线，不要预先把难度加成算进去 —— 那会被缩放二次放大。
 */
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

	/**
	 * 技能列表（按 id 引用 FHexEnemySkillLibrary）。
	 *
	 * ⚠️ 留空 = 走 profile 默认攻击（AttackRangeOf + 1.0×ATK），
	 *    行为与「技能系统存在之前」完全一致。
	 *    这是刻意的默认值：现有四只敌人的数值是实测调过的，
	 *    不该因为多了一套技能框架而被动改变。
	 *
	 * ⚠️ 引用了不存在的技能 id 时会记一条警告并【跳过该 id】，
	 *    而不是让敌人整场站着不动。由 VerifyContent 的引用完整性断言钉住。
	 */
	TArray<FName> SkillIds;

	/**
	 * 移动力（每回合可走的格数预算）。
	 *
	 * ⚠️ 曾经硬编码为 2（见 ComputeMoveTarget 的 Budget）。
	 *    提出来配表之后，「慢速重装」与「高速游走」才能靠数据区分 ——
	 *    而这正是 AGI 之外唯一能表达"这只怪逼得多紧"的旋钮。
	 */
	int32 MoveBudget = 2;

	/**
	 * 风筝型的理想距离（离目标多远就不再靠近）。
	 *
	 * 只对 RangedKiter 有意义；其他 profile 一律贴近到 1。
	 * ⚠️ 填得比攻击射程还大会让敌人永远进不了射程 → 整场只后退。
	 *    由 VerifyContent 断言 PreferredDistance <= 攻击射程。
	 */
	int32 PreferredDistance = 3;

	/**
	 * 攻击射程覆写。-1 = 用 AIProfile 的默认射程。
	 *
	 * ⚠️ 改这个值会直接影响「可躲型意图是否存在」。
	 *    填 1 会让该敌人永远产不出可躲型攻击意图 ——
	 *    两段明示规则规定距离 ≤1 一律转追踪（见 HexEnemyAI.cpp
	 *    AttackRangeOf 里 Aggressive 射程为何是 2 的长注释）。
	 *    §13.2 要求玩家能区分"能躲 / 不能躲"，全表都填 1 会让这条 UX 消失。
	 */
	int32 AttackRangeOverride = -1;

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

	/**
	 * 用外部数据（DataTable）覆写/追加一个英雄。
	 *
	 * 语义与纪律与 OverrideCard 完全一致，见那边的长注释：
	 * core 自己永远不调用，只有表现层的配表加载器会调；
	 * 同 Id 则【整条替换】，新 Id 则追加。
	 *
	 * ⚠️ 必须在 StartNewRun 之前调用。RunState 会拷贝英雄数值，
	 *    之后再覆写只会改到库里那份，本局纹丝不动 ——
	 *    症状是"改表没生效，重开一局才对"。
	 *
	 * @return true = 覆写了已有英雄；false = 追加了新英雄
	 */
	static bool OverrideHero(const FHexHeroData& Hero);

	/** 撤销全部英雄覆写，回到纯代码内建状态 */
	static void ResetHeroOverrides();

	// ═══════════════════════════════════════ 卡牌

	static const FHexCardData* FindCard(FName Id);
	static const TArray<FHexCardData>& AllCards();

	/**
	 * 用外部数据（DataTable）覆写/追加一张卡。
	 *
	 * ⚠️ 这是【表现层配表】的唯一入口，core 自己永远不调用它 ——
	 *    所以 headless 验证与批量模拟拿到的始终是代码内建的那套
	 *    实测数值，不受配表状态影响。这是 core 只依赖
	 *    Core/CoreUObject 的前提，也是验证体系能秒级重跑的前提。
	 *
	 * 语义：同 Id 则【整条替换】，新 Id 则追加。
	 *   整条替换而非字段级合并，是因为 CSV 的空单元格会填成类型默认值，
	 *   "没填"与"填了 0"在 DataTable 里无法区分 ——
	 *   做字段级合并会让"把费用改成 0"被当成"没填"而静默失效。
	 *
	 * ⚠️ 只应在游戏启动早期（任何战斗开始之前）调用。
	 *    战斗中途替换卡定义会让已在手的卡实例与定义脱节。
	 *
	 * @return true = 覆写了已有卡；false = 追加了新卡
	 */
	static bool OverrideCard(const FHexCardData& Card);

	/**
	 * 撤销全部覆写，回到纯代码内建状态。
	 * 验证器用它保证每个用例从同一基线开始。
	 */
	static void ResetCardOverrides();

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
	 * 用外部数据（DataTable）覆写/追加一个敌人。
	 *
	 * 语义与纪律同 OverrideCard。同 Id 整条替换，新 Id 追加。
	 *
	 * ⚠️ 覆写会让【已持有的 FHexEnemyData* 失效】（TArray 追加时重分配）。
	 *    BeginBattleForRoom 在遭遇构造期间持有这些指针，
	 *    所以只能在启动早期调用，不能在战斗中途。
	 *
	 * @return true = 覆写了已有敌人；false = 追加了新敌人
	 */
	static bool OverrideEnemy(const FHexEnemyData& Enemy);

	/** 撤销全部敌人覆写，回到纯代码内建状态 */
	static void ResetEnemyOverrides();

	/**
	 * 由敌人定义构造战场单位，含层数与腐蚀度缩放。
	 * @param FloorIndex 层数（1 起）
	 * @param Corruption 腐蚀度（§9.4：每 +1 敌人 HP/ATK 提升一档）
	 */
	static FHexUnit MakeEnemyUnit(const FHexEnemyData& Data, int32 FloorIndex, int32 Corruption);

	/**
	 * 取敌人已配置的技能（跳过引用不到的 id，按优先级降序 + id 字典序）。
	 *
	 * ⚠️ 返回排好序的指针数组而非让调用方自己排 ——
	 *    AI 的选技能逻辑依赖这个顺序的确定性，散落在调用方会漂移。
	 */
	static void GetEnemySkills(
		const FHexEnemyData& Data, TArray<const struct FHexEnemySkillData*>& Out);

	// ═══════════════════════════════════════ 敌人技能（转发到 HexEnemySkillLibrary）

	static const struct FHexEnemySkillData* FindEnemySkill(FName Id);
	static const TArray<struct FHexEnemySkillData>& AllEnemySkills();

	// ═══════════════════════════════════════ 怪物组

	static void GetEncounter(FName EncounterId, TArray<FHexEncounterEntry>& Out);
	static const TArray<FName>& AllEncounterIds();

	// ═══════════════════════════════════════ 符文（D6）

	static const FHexRuneData* FindRune(FName Id);
	static const TArray<FHexRuneData>& AllRunes();

	/** 按稀有度筛选（三选一抽取用） */
	static void GetRuneIdsByRarity(EHexRarity Rarity, TArray<FName>& Out);
};
