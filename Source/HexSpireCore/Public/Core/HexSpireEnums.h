// Copyright Hex Spire. All Rights Reserved.
//
// 所有共享枚举的【唯一定义处】—— 对应 Godot 版 src/core/enums.gd
//
// 全部标记 BlueprintType，让表现层蓝图与 DataAsset 能直接用下拉框选择，
// 这是策划案 §15.4「新增一张卡 = 新增一个数据资产」得以成立的前提。
//
// ⚠️ 枚举值的【数字顺序】是存档与确定性哈希的一部分。
//    只允许在末尾追加，禁止插入或重排。

#pragma once

#include "CoreMinimal.h"
#include "HexSpireEnums.generated.h"

// ────────────────────────────────────────────────────────── 体型（D8）

/** 体型。S=1格 / M=3格 / L=6格，均为六边形三角形（策划案 §8.2.1） */
UENUM(BlueprintType)
enum class EHexSizeClass : uint8
{
	S = 0	UMETA(DisplayName = "小型 S (1格)"),
	M = 1	UMETA(DisplayName = "中型 M (3格)"),
	L = 2	UMETA(DisplayName = "大型 L (6格)"),
};

// ────────────────────────────────────────────────────────── 卡牌

UENUM(BlueprintType)
enum class EHexCardType : uint8
{
	Attack = 0	UMETA(DisplayName = "攻击"),
	Guard = 1	UMETA(DisplayName = "守备"),
	Move = 2	UMETA(DisplayName = "移动"),
	Skill = 3	UMETA(DisplayName = "技能"),
	Stance = 4	UMETA(DisplayName = "姿态"),
	Derived = 5	UMETA(DisplayName = "衍生"),
	Curse = 6	UMETA(DisplayName = "诅咒"),
};

UENUM(BlueprintType)
enum class EHexRarity : uint8
{
	Common = 0		UMETA(DisplayName = "普通"),
	Uncommon = 1	UMETA(DisplayName = "精良"),
	Rare = 2		UMETA(DisplayName = "稀有"),
	Epic = 3		UMETA(DisplayName = "史诗"),
	Legendary = 4	UMETA(DisplayName = "传说"),
	Cursed = 5		UMETA(DisplayName = "诅咒"),
};

// ────────────────────────────────────────────────────────── 效果（§15.4）

/**
 * 效果步骤的操作码。卡牌 / 符文 / 状态共用同一套。
 *
 * ⚠️ 新增一项必须同步在 BattleFlow::ExecuteStep 里加分支，
 *    否则会静默记 unimplemented_op（Godot 版踩过这个坑）。
 */
UENUM(BlueprintType)
enum class EHexEffectOp : uint8
{
	DealDamage = 0			UMETA(DisplayName = "造成伤害"),
	GainBlock = 1			UMETA(DisplayName = "获得格挡"),
	Heal = 2				UMETA(DisplayName = "治疗"),
	MoveSelf = 3			UMETA(DisplayName = "自身位移"),
	Dash = 4				UMETA(DisplayName = "突进"),
	Blink = 5				UMETA(DisplayName = "闪现"),
	Rotate = 6				UMETA(DisplayName = "转向"),
	Knockback = 7			UMETA(DisplayName = "击退"),
	Pull = 8				UMETA(DisplayName = "拉拽"),
	Swap = 9				UMETA(DisplayName = "换位"),
	Trample = 10			UMETA(DisplayName = "碾压"),
	ApplyStatus = 11		UMETA(DisplayName = "施加状态"),
	RemoveStatus = 12		UMETA(DisplayName = "移除状态"),
	DrawCard = 13			UMETA(DisplayName = "抽牌"),
	DiscardCard = 14		UMETA(DisplayName = "弃牌"),
	ExhaustCard = 15		UMETA(DisplayName = "消耗牌"),
	ShuffleDiscardIntoDraw = 16	UMETA(DisplayName = "洗回弃牌堆"),
	PutCardOnTop = 17		UMETA(DisplayName = "置于牌堆顶"),
	GainEnergy = 18			UMETA(DisplayName = "获得体力"),
	ModifyStat = 19			UMETA(DisplayName = "修改属性"),
	Summon = 20				UMETA(DisplayName = "召唤"),
	ModifyTerrain = 21		UMETA(DisplayName = "改变地形"),
	TriggerAnotherCard = 22	UMETA(DisplayName = "触发另一张卡"),
	Conditional = 23		UMETA(DisplayName = "条件分支"),
};

UENUM(BlueprintType)
enum class EHexTargetFilter : uint8
{
	Enemy = 0		UMETA(DisplayName = "敌方"),
	Ally = 1		UMETA(DisplayName = "友方"),
	Self = 2		UMETA(DisplayName = "自身"),
	AllInArea = 3	UMETA(DisplayName = "范围内全部"),
	RandomN = 4		UMETA(DisplayName = "随机N个"),
	Largest = 5		UMETA(DisplayName = "体型最大"),
	Nearest = 6		UMETA(DisplayName = "最近"),
};

// ────────────────────────────────────────────────────────── 目标（§15.5）

UENUM(BlueprintType)
enum class EHexTargetShape : uint8
{
	SelfShape = 0	UMETA(DisplayName = "自身"),
	Single = 1		UMETA(DisplayName = "单体"),
	Line = 2		UMETA(DisplayName = "直线"),
	Cone = 3		UMETA(DisplayName = "扇形"),
	Burst = 4		UMETA(DisplayName = "范围圆"),
	Ring = 5		UMETA(DisplayName = "环形"),
	AdjacentAll = 6	UMETA(DisplayName = "全部相邻"),
	Tile = 7		UMETA(DisplayName = "指定格"),
	/**
	 * 冲撞路径：落点 + 沿途全部格子（§8.6 位移即伤害）。
	 *
	 * ⚠️ 为什么不能用 Tile 表达冲撞：
	 *    Tile 的波及格【只有落点本身】，而落点必须是空格（要站得下人），
	 *    于是"对沿途敌人造成伤害"这一步永远找不到目标 —— 伤害恒为 0。
	 *    DashPath 的波及格是整条连线，沿途敌人才吃得到伤害。
	 *
	 *    另一个差别：Tile 的合法格要求"可站立"，
	 *    DashPath 额外要求"路径不被墙/石柱堵死"，但【允许穿过单位】——
	 *    冲撞的定位就是穿透，被人挡住就退化成普通移动了。
	 */
	DashPath = 8	UMETA(DisplayName = "冲撞路径"),
};

// ────────────────────────────────────────────────────────── 符文（D6 / §15.6）

UENUM(BlueprintType)
enum class EHexRuneCategory : uint8
{
	RuleRewrite = 0	UMETA(DisplayName = "规则改写"),
	Trigger = 1		UMETA(DisplayName = "触发器"),
	Conditional = 2	UMETA(DisplayName = "条件增益"),
	Multiplier = 3	UMETA(DisplayName = "乘区"),
	Curse = 4		UMETA(DisplayName = "诅咒"),
};

/**
 * 统一触发时机表（策划案 §6.3）—— 符文互相咬合的地基。
 *
 * ⚠️ 新增时机必须同步在流程里埋 Emit 点。
 *    埋点时监听者为空几乎零成本，事后回填极痛（架构文档 §8 原话）。
 */
UENUM(BlueprintType)
enum class EHexTriggerTiming : uint8
{
	OnBattleStart = 0		UMETA(DisplayName = "战斗开始"),
	OnRoundStart = 1		UMETA(DisplayName = "回合开始"),
	OnRoundEnd = 2			UMETA(DisplayName = "回合结束"),
	OnCardPlayed = 3		UMETA(DisplayName = "打出卡牌"),
	/** ⭐ 伤害管线 ②′ 的顺序钩子（§6.5 的核心：[锐化,倍化] ≠ [倍化,锐化]） */
	OnAttack = 4			UMETA(DisplayName = "攻击时"),
	OnDamageDealt = 5		UMETA(DisplayName = "造成伤害后"),
	OnDamageTaken = 6		UMETA(DisplayName = "受到伤害后"),
	OnKill = 7				UMETA(DisplayName = "击杀"),
	OnBlockGained = 8		UMETA(DisplayName = "获得格挡"),
	OnBlockBroken = 9		UMETA(DisplayName = "格挡被击破"),
	OnMoveSelf = 10			UMETA(DisplayName = "自身移动"),
	OnMoveEnemy = 11		UMETA(DisplayName = "推动敌人"),
	OnStatusApplied = 12	UMETA(DisplayName = "施加状态"),
	OnCardDrawn = 13		UMETA(DisplayName = "抽到卡牌"),
	OnCardDiscarded = 14	UMETA(DisplayName = "弃掉卡牌"),
	OnCardExhausted = 15	UMETA(DisplayName = "消耗卡牌"),
	/** D2 带来的新时机；小卡组下每 2–4 回合触发一次，是高频组合钩子 */
	OnDeckReshuffled = 16	UMETA(DisplayName = "洗回卡组"),
	OnEnergyLeftover = 17	UMETA(DisplayName = "回合剩余体力"),
	OnCrit = 18				UMETA(DisplayName = "暴击"),
	OnDodge = 19			UMETA(DisplayName = "闪避"),
	OnUnitDeath = 20		UMETA(DisplayName = "单位死亡"),
	OnBattleWin = 21		UMETA(DisplayName = "战斗胜利"),

	Count = 22				UMETA(Hidden),
};

/**
 * 可被符文/装备改写的规则（§15.6）。
 *
 * ⚠️ 每一条【必须且只能】在 RuleBook 里有一个消费点。
 *    由 CheckDiscipline 源码扫描强制。绕过 RuleBook 直接读值 = 符文改写静默失效。
 */
UENUM(BlueprintType)
enum class EHexGameRule : uint8
{
	EnergyMax = 0			UMETA(DisplayName = "体力上限"),
	CardsDrawnPerTurn = 1	UMETA(DisplayName = "每回合抽牌数"),
	HandLimit = 2			UMETA(DisplayName = "手牌上限"),
	DeckCapacity = 3		UMETA(DisplayName = "卡组容量"),
	CardCostDelta = 4		UMETA(DisplayName = "卡牌费用增减"),
	FirstCardFree = 5		UMETA(DisplayName = "首张卡免费"),
	NoDrawFixedHand = 6		UMETA(DisplayName = "不抽牌固定手牌"),
	SizeClassOverride = 7	UMETA(DisplayName = "体型覆写"),
	KnockbackImmune = 8		UMETA(DisplayName = "免疫击退"),
	BlockPersists = 9		UMETA(DisplayName = "格挡不清空"),
	DamageMultiplier = 10	UMETA(DisplayName = "伤害乘区"),
	BlockMultiplier = 11	UMETA(DisplayName = "格挡乘区"),
	CritDamageMultiplier = 12	UMETA(DisplayName = "暴击伤害乘区"),
	NoBlockAllowed = 13		UMETA(DisplayName = "无法获得格挡"),
	MoveCostDelta = 14		UMETA(DisplayName = "移动成本增减"),
	ExhaustAllAttacks = 15	UMETA(DisplayName = "攻击牌全部消耗"),

	Count = 16				UMETA(Hidden),
};

// ────────────────────────────────────────────────────────── 装备（§15.7）

UENUM(BlueprintType)
enum class EHexEquipSlot : uint8
{
	Weapon = 0	UMETA(DisplayName = "武器"),
	Armor = 1	UMETA(DisplayName = "盔甲"),
	Trinket = 2	UMETA(DisplayName = "饰品"),

	Count = 3	UMETA(Hidden),
};

// ────────────────────────────────────────────────────────── 敌人（§15.8）

UENUM(BlueprintType)
enum class EHexAIProfile : uint8
{
	Aggressive = 0	UMETA(DisplayName = "近战突进"),
	RangedKiter = 1	UMETA(DisplayName = "远程风筝"),
	Support = 2		UMETA(DisplayName = "辅助"),
	Summoner = 3	UMETA(DisplayName = "召唤"),
	Tank = 4		UMETA(DisplayName = "坦克"),
	Blocker = 5		UMETA(DisplayName = "堵路"),
	BossPhased = 6	UMETA(DisplayName = "多阶段Boss"),
};

/** 意图是否可躲 —— §13.2 点名要求玩家能读出这个区分（实线 vs 虚线+连线） */
UENUM(BlueprintType)
enum class EHexIntentTargeting : uint8
{
	/** 锁定格子：玩家走开则打空（可躲） */
	FixedTile = 0	UMETA(DisplayName = "锁定格子(可躲)"),
	/** 锁定单位：跟着玩家走（躲不掉，需打断/格挡/断视线） */
	TrackTarget = 1	UMETA(DisplayName = "锁定单位(追踪)"),
};

UENUM(BlueprintType)
enum class EHexIntentKind : uint8
{
	Attack = 0		UMETA(DisplayName = "攻击"),
	MultiAttack = 1	UMETA(DisplayName = "多段攻击"),
	Buff = 2		UMETA(DisplayName = "强化"),
	Debuff = 3		UMETA(DisplayName = "削弱"),
	Summon = 4		UMETA(DisplayName = "召唤"),
	Move = 5		UMETA(DisplayName = "移动"),
	Rotate = 6		UMETA(DisplayName = "转向"),
	Special = 7		UMETA(DisplayName = "特殊"),
	Sleep = 8		UMETA(DisplayName = "休眠"),
};

// ────────────────────────────────────────────────────────── 地形（§8.3）

UENUM(BlueprintType)
enum class EHexTerrain : uint8
{
	Floor = 0		UMETA(DisplayName = "地面"),
	Wall = 1		UMETA(DisplayName = "墙(不可通行不可穿射)"),
	Pit = 2			UMETA(DisplayName = "深坑(不可通行可穿射)"),
	Rubble = 3		UMETA(DisplayName = "碎石(可通行,成本+1)"),
	ExitGate = 4	UMETA(DisplayName = "出口"),
};

UENUM(BlueprintType)
enum class EHexHazard : uint8
{
	None = 0	UMETA(DisplayName = "无"),
	Spikes = 1	UMETA(DisplayName = "尖刺"),
	Fire = 2	UMETA(DisplayName = "火焰"),
	Acid = 3	UMETA(DisplayName = "腐蚀"),
	Ash = 4		UMETA(DisplayName = "香灰"),
};

UENUM(BlueprintType)
enum class EHexFeature : uint8
{
	None = 0		UMETA(DisplayName = "无"),
	Pillar = 1		UMETA(DisplayName = "石柱(阻断视线)"),
	Crate = 2		UMETA(DisplayName = "箱子"),
	Turnstile = 3	UMETA(DisplayName = "闸机"),
	Altar = 4		UMETA(DisplayName = "祭坛"),
};

// ────────────────────────────────────────────────────────── 战斗流程（§8.4）

UENUM(BlueprintType)
enum class EHexBattlePhase : uint8
{
	BattleStart = 0		UMETA(DisplayName = "战斗开始"),
	RoundStart = 1		UMETA(DisplayName = "回合开始"),
	PlayerPhase = 2		UMETA(DisplayName = "玩家阶段"),
	RoundEndPlayer = 3	UMETA(DisplayName = "玩家回合结束"),
	EnemyPhase = 4		UMETA(DisplayName = "敌方阶段"),
	RoundEndAll = 5		UMETA(DisplayName = "回合总结束"),
	/** 胜利后的拾取阶段：无限体力，走到 ExitGate 结束（§7.3） */
	Explore = 6			UMETA(DisplayName = "探索拾取"),
	BattleWin = 7		UMETA(DisplayName = "战斗胜利"),
	BattleLose = 8		UMETA(DisplayName = "战斗失败"),
};

UENUM(BlueprintType)
enum class EHexTeam : uint8
{
	Player = 0	UMETA(DisplayName = "玩家方"),
	Enemy = 1	UMETA(DisplayName = "敌方"),
	Neutral = 2	UMETA(DisplayName = "中立"),
};

// ────────────────────────────────────────────────────────── 状态（§8.9）

UENUM(BlueprintType)
enum class EHexStackMode : uint8
{
	StackIntensity = 0	UMETA(DisplayName = "层数叠加"),
	StackDuration = 1	UMETA(DisplayName = "时长叠加"),
	RefreshDuration = 2	UMETA(DisplayName = "刷新时长"),
};

UENUM(BlueprintType)
enum class EHexStatusTick : uint8
{
	None = 0		UMETA(DisplayName = "不结算"),
	RoundStart = 1	UMETA(DisplayName = "回合开始"),
	RoundEnd = 2	UMETA(DisplayName = "回合结束"),
	OnHit = 3		UMETA(DisplayName = "受击时"),
};

UENUM(BlueprintType)
enum class EHexWinCondition : uint8
{
	KillAll = 0		UMETA(DisplayName = "全灭敌人"),
	SurviveN = 1	UMETA(DisplayName = "存活N回合"),
	ReachTile = 2	UMETA(DisplayName = "到达指定格"),
	Protect = 3		UMETA(DisplayName = "保护目标"),
};

// ────────────────────────────────────────────────────────── 房间（§9.6）

UENUM(BlueprintType)
enum class EHexRoomType : uint8
{
	Entrance = 0	UMETA(DisplayName = "入口"),
	Combat = 1		UMETA(DisplayName = "普通战斗"),
	Elite = 2		UMETA(DisplayName = "精英战斗"),
	Event = 3		UMETA(DisplayName = "事件"),
	Shop = 4		UMETA(DisplayName = "商店"),
	Treasure = 5	UMETA(DisplayName = "宝藏"),
	Camp = 6		UMETA(DisplayName = "营地"),
	Boss = 7		UMETA(DisplayName = "Boss"),
};

// ────────────────────────────────────────────────────────── 属性

/**
 * 六属性（§4.3）。用枚举而非字符串做索引 —— 字符串键在
 * 10 万场模拟里是可观的开销，且拼写错误只能在运行时发现。
 *
 * CRIT 管频率、LUK 管倍率，这个分工是策划案明确的（§4.3）。
 */
UENUM(BlueprintType)
enum class EHexStat : uint8
{
	HP = 0		UMETA(DisplayName = "生命值"),
	ATK = 1		UMETA(DisplayName = "攻击力"),
	DEF = 2		UMETA(DisplayName = "防御值"),
	AGI = 3		UMETA(DisplayName = "敏捷值"),
	LUK = 4		UMETA(DisplayName = "幸运值"),
	CRIT = 5	UMETA(DisplayName = "暴击率"),

	Count = 6	UMETA(Hidden),
};

/** RNG 子流（架构纪律 1）。单一主 seed 派生五条独立流。 */
UENUM(BlueprintType)
enum class EHexRngStream : uint8
{
	Map = 0		UMETA(DisplayName = "地图"),
	Loot = 1	UMETA(DisplayName = "掉落"),
	Combat = 2	UMETA(DisplayName = "战斗"),
	Deck = 3	UMETA(DisplayName = "洗牌"),
	Event = 4	UMETA(DisplayName = "事件"),

	Count = 5	UMETA(Hidden),
};
