// Copyright Hex Spire. All Rights Reserved.
//
// 效果步骤 / 目标规格 / 卡牌定义 —— 策划案 §15.3 / §15.4 / §15.5
//
// ⚠️ §7.5 强制规范：所有卡牌数值必须写成系数形式
//      Value = FlatValue + Stats[StatRef] × StatRatio
//    禁止硬编码 Damage = 12。
//
//    理由：ATK 从 10 涨到 200 时，所有卡牌一起翻倍，卡牌之间的相对优劣不变，
//    策略层不被数值冲垮。这是"强数值养成"能与"卡牌策略"共存的唯一办法，
//    也是策划案明确标注【不可妥协】的一条。
//
//    例外（离散量，不随属性缩放，它们是策略层的锚点）：
//      状态层数、位移格数、抽牌数、体力数

#pragma once

#include "CoreMinimal.h"
#include "Core/HexSpireEnums.h"

class FHexUnit;

/** 效果的条件判定（§15.4 EffectCondition） */
struct HEXSPIRECORE_API FHexEffectCondition
{
	enum class EKind : uint8
	{
		None,
		/** 目标生命低于百分比 */
		TargetHPBelowPercent,
		/** 自身生命满 */
		SelfAtFullHP,
		/**
		 * 自身生命未满。
		 * ⚠️ 不是 SelfAtFullHP 的简单取反用法问题 ——
		 *    条件系统没有"否"运算符，"低血反打"类符文必须有独立枚举。
		 */
		SelfNotAtFullHP,
		/** 抽牌堆为空 */
		DrawPileEmpty,
		/** 目标有指定状态 */
		TargetHasStatus,
		/** 本回合已打出 N 张牌 */
		CardsPlayedAtLeast,
	};

	EKind Kind = EKind::None;
	float FloatParam = 0.0f;
	int32 IntParam = 0;
	FName NameParam;

	bool IsNone() const { return Kind == EKind::None; }
};

/**
 * 单个效果步骤。卡牌 / 符文 / 状态共用。
 *
 * 这套结构能表达绝大多数卡牌与符文，无需写代码 ——
 * 新增一张卡 = 新增一条数据。这是内容量能上去的前提（§15.4）。
 */
struct HEXSPIRECORE_API FHexEffectStep
{
	EHexEffectOp Op = EHexEffectOp::DealDamage;

	// ── §7.5 系数化三件套
	float FlatValue = 0.0f;
	FName StatRef = TEXT("ATK");
	float StatRatio = 0.0f;

	/** 连击次数 */
	int32 Repeat = 1;

	// ── 离散量（不随属性缩放）
	/** 位移格数 / 击退格数 */
	int32 Distance = 0;
	FName StatusId;
	int32 StatusStacks = 0;

	EHexTargetFilter TargetFilter = EHexTargetFilter::Enemy;

	FHexEffectCondition Condition;

	/** 视觉/音效标记，逻辑层不消费，只写进事件日志给表现层用 */
	FName VfxId;
	FName SfxId;

	/** 计算该步骤的数值（§7.5 系数化） */
	int32 ValueFor(const FHexUnit* Source) const;

	// ── 便捷构造

	static FHexEffectStep MakeDamage(float Flat, FName Stat, float Ratio, int32 InRepeat = 1);
	static FHexEffectStep MakeBlock(float Flat, FName Stat, float Ratio);
	static FHexEffectStep MakeMove(int32 InDistance);
	static FHexEffectStep MakeDash(int32 InDistance);
	static FHexEffectStep MakeKnockback(int32 InDistance);
	static FHexEffectStep MakeApplyStatus(FName InStatusId, int32 InStacks);
	static FHexEffectStep MakeDraw(int32 Count);
	static FHexEffectStep MakeGainEnergy(int32 Amount);
	static FHexEffectStep MakeOp(EHexEffectOp InOp);
};

/**
 * 目标规格（§15.5）。数据驱动，不写死。
 *
 * ⚠️ RangeMax 从【最近的己方 footprint 格】起算（§8.5）——
 *    这是大体型"有效射程更长"的来源。
 */
struct HEXSPIRECORE_API FHexTargetSpec
{
	EHexTargetShape Shape = EHexTargetShape::Single;
	int32 RangeMin = 0;
	int32 RangeMax = 1;
	/** LINE 长度 / BURST 半径 / CONE 长度 / RING 半径 */
	int32 AreaSize = 0;
	bool bRequiresLineOfSight = true;
	bool bCanTargetEmptyCell = false;
	/** 空 = 全部队伍合法 */
	TArray<EHexTeam> ValidTeams;
	/** 空 = 全部体型合法；可做"只能对小型单位使用"的卡 */
	TArray<EHexSizeClass> ValidSizeClasses;

	static FHexTargetSpec MakeSelf();
	static FHexTargetSpec Make(
		EHexTargetShape InShape,
		int32 InRangeMin,
		int32 InRangeMax,
		int32 InAreaSize = 0,
		bool bLoS = true);
	static FHexTargetSpec MakeTile(int32 InRangeMin, int32 InRangeMax);

	/**
	 * 冲撞：落点可站立、路径不被墙堵死、允许穿过单位。
	 * 波及格 = 整条路径（沿途敌人都会吃到后续的伤害步骤）。
	 */
	static FHexTargetSpec MakeDashPath(int32 InRangeMin, int32 InRangeMax);

	/**
	 * 是否以【格子】为目标（而非以单位为目标）。
	 *
	 * ⚠️ 判断"这是不是一张位移卡"请一律用这个方法，
	 *    不要在业务代码里手写 `Shape == Tile`。
	 *    加入 DashPath 时，散落各处的手写判断漏了三处：
	 *      · 验证套件的接敌逻辑   → 机器人不再冲锋，对风筝敌人永久僵持
	 *      · 试玩机器人的走位逻辑 → 不再用冲撞躲避
	 *      · 试玩机器人的接敌逻辑 → 同上
	 *    这些都不会报错，只会让 AI 悄悄变笨，极难察觉。
	 */
	bool TargetsCell() const
	{
		return Shape == EHexTargetShape::Tile
			|| Shape == EHexTargetShape::DashPath;
	}
};

/** 卡牌定义（§15.3） */
struct HEXSPIRECORE_API FHexCardData
{
	FName Id;
	FString DisplayName;
	EHexCardType CardType = EHexCardType::Attack;
	EHexRarity Rarity = EHexRarity::Common;
	int32 EnergyCost = 1;

	/** 仅用于检索/加权/符文过滤，【不产生任何加成】（§7.8） */
	TArray<FName> Tags;

	FHexTargetSpec TargetSpec;
	TArray<FHexEffectStep> Effects;

	bool bIsCornerstone = false;
	/** 【消耗】→ 打出后进消耗区，本场战斗不再出现 */
	bool bIsExhaust = false;
	/** 基石卡与衍生卡不占卡组容量（§7.6） */
	bool bCountsTowardCapacity = true;
	int32 MaxCopiesInDeck = 3;

	/** 描述模板，运行时填入实算值。例："造成 {dmg} 伤害并击退 {kb} 格" */
	FString DescriptionTemplate;

	/** 渲染描述（把 {dmg} 等占位符替换为按当前属性算出的实值） */
	FString RenderDescription(const FHexUnit* Source) const;
};
