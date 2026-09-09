// Copyright Hex Spire. All Rights Reserved.
//
// 装备系统 —— 策划案 §5 / §15.7（用户决策 q12：做完整版）
//
// ══════════════════════════════════════════════════════════════════
// 装备与符文的【分工】—— 这是整个系统最重要的一条设计约束
// ══════════════════════════════════════════════════════════════════
// 符文（D6）= 改写【规则】。§6.4 明令纯数值符文占比必须为 0，
//             因为"拿数值最高的那个"会让选择变成假的。
// 装备（§5）= 承载【数值养成】。它就该给属性，而且必须给得爽 ——
//             §3.1 的"强数值养成"这一半完全落在装备与碎片上。
//
// 两者不冲突，因为它们放大的东西不同：
//   装备给 ATK+8 → 所有卡牌因 §7.5 系数化【一起变强】，卡牌间相对优劣不变
//   符文给"每次洗牌得格挡" → 改变的是打法结构，与数值大小无关
//
// ⚠️ 所以装备词条【禁止】直接给"某张卡 +5 伤害"这种固定值。
//    那会破坏 §7.5 的相对优劣不变性，让某张卡在后期变成废牌或神卡。
//    唯一例外是武器对基石《攻击》的射程/形状覆写 —— 那是【结构】改写，
//    不是数值改写，且策划案 §5.2 明确要求。
//
// ══════════════════════════════════════════════════════════════════
// 复用而非新建：本文件刻意不发明新的触发器/规则结构
// ══════════════════════════════════════════════════════════════════
//   装备的规则改写 → 直接用 FHexRuleOverride（RuleAggregate 已能聚合）
//   装备的触发效果 → 直接用 FHexRuneTrigger（TriggerBus 已能分发）
// 收益：装备接入 TriggerBus 只需填 SlotOrder 10..12（那三个槽位号
//       在 HexTriggerBus.h 里早就预留好了），零新增分发逻辑。
// 代价：FHexRuneTrigger 的名字里带 "Rune"，读起来别扭 —— 接受，
//       因为再造一套等价结构会带来两条必须永久同步的分发路径。

#pragma once

#include "CoreMinimal.h"
#include "Core/HexSpireEnums.h"
#include "Core/HexSpireConstants.h"
#include "Battle/HexCardData.h"
#include "Runes/HexRuneData.h"

class FHexRngStreams;

// ══════════════════════════════════════════════════════════ 词条

/** 词条的作用方式 */
enum class EHexAffixKind : uint8
{
	/** 平坦属性加成（ATK +8）—— 装备的主力形态 */
	StatFlat = 0,
	/** 百分比属性加成（HP +15%），基于英雄基线属性计算 */
	StatPercent = 1,
	/** 规则改写（与符文共用同一套 GameRule） */
	RuleOverride = 2,
	/** 挂在时机上的触发效果 */
	Trigger = 3,
};

/**
 * 词条定义。
 *
 * ⚠️ 词条是【定义】，不是实例。一件装备 roll 到哪几条词条记在
 *    FHexEquipInstance::AffixIds 里，这样存档只需存 id 列表，
 *    且改词条数值时已有存档会自动跟着更新（灰盒期非常重要）。
 */
struct HEXSPIRECORE_API FHexAffixDef
{
	FName Id;
	FString DisplayName;

	/**
	 * 词条族。同一族在【一件装备上】只能出现一条。
	 *
	 * ⚠️ 没有这个字段会出现严重的数值失控：
	 *    "锋利+3 / 锐利+5 / 凶戾+8" 是同一属性的三个档位，
	 *    若能同时命中，一件史诗装备就能给 +16 攻击力，
	 *    三槽满配 +37（基线 10 的 3.7 倍）——
	 *    Godot 实测调好的那套敌人数值会全部作废。
	 *
	 * 空 = 自成一族（等价于用 Id 做族名）。
	 */
	FName Family;

	EHexAffixKind Kind = EHexAffixKind::StatFlat;

	// ── StatFlat / StatPercent
	EHexStat Stat = EHexStat::ATK;
	int32 FlatValue = 0;
	float PercentValue = 0.0f;

	// ── RuleOverride
	FHexRuleOverride Rule;

	// ── Trigger（复用符文触发器结构）
	FHexRuneTrigger Trigger;

	/**
	 * 限定可出现的装备槽。空 = 任意槽。
	 * 用途：把"射程 +1"限制在武器上，"格挡"限制在盔甲上 ——
	 * 否则饰品能 roll 出射程加成，玩家的直觉会被破坏。
	 */
	TArray<EHexEquipSlot> AllowedSlots;

	/**
	 * 最低出现稀有度。
	 * 强词条设为 Rare 以上，保证低稀有度装备不会 roll 出逆天效果 ——
	 * 否则稀有度这个维度就失去意义了。
	 */
	EHexRarity MinRarity = EHexRarity::Common;

	/** 掉落权重（越大越常见）。0 = 不参与随机，只能固定挂载。 */
	int32 Weight = 100;

	/** 词条描述（UI 直接显示，必须让玩家看懂） */
	FString Text;

	bool AllowsSlot(EHexEquipSlot Slot) const
	{
		return AllowedSlots.Num() == 0 || AllowedSlots.Contains(Slot);
	}

	/** 有效族名：Family 为空时自成一族 */
	FName EffectiveFamily() const
	{
		return Family.IsNone() ? Id : Family;
	}
};

// ══════════════════════════════════════════════════════════ 攻击覆写

/**
 * 武器对基石《攻击》的覆写（§5.2）。
 *
 * 这是装备系统里唯一改写【结构】而非数值的部分，也是最有存在感的一环：
 * 换一把长柄武器，《攻击》从"相邻单体"变成"直线穿刺 3 格"，
 * 整个走位逻辑跟着变。数值再高也做不到这件事。
 *
 * ⚠️ 只覆写基石《攻击》(atk_basic)，不影响其它攻击牌 ——
 *    否则《穿刺投枪》这类本身有射程设计的卡会被武器搞乱。
 */
struct HEXSPIRECORE_API FHexAttackOverride
{
	bool bActive = false;

	EHexTargetShape Shape = EHexTargetShape::Single;
	int32 RangeMin = 1;
	int32 RangeMax = 1;
	int32 AreaSize = 0;
	bool bRequiresLineOfSight = true;

	/** 应用到一个目标规格上 */
	void ApplyTo(FHexTargetSpec& Spec) const;
};

// ══════════════════════════════════════════════════════════ 装备定义

/** 装备定义（§15.7 EquipData） */
struct HEXSPIRECORE_API FHexEquipData
{
	FName Id;
	FString DisplayName;
	EHexEquipSlot Slot = EHexEquipSlot::Weapon;

	/** 基础稀有度下限（这件装备最低以什么稀有度出现） */
	EHexRarity BaseRarity = EHexRarity::Common;

	/**
	 * 固有词条：不随机、永远存在。
	 * 这是装备的"身份"——《长柄戟》必然带射程，否则它就不是长柄戟。
	 */
	TArray<FName> InherentAffixIds;

	/** 武器专属：对基石《攻击》的覆写 */
	FHexAttackOverride AttackOverride;

	/**
	 * 注入卡组的衍生卡（§5.4「注入衍生卡」）。
	 * 卸下装备时这些卡一并移除，且不占卡组容量。
	 */
	TArray<FName> InjectedCardIds;

	FString FlavorText;
};

// ══════════════════════════════════════════════════════════ 装备实例

/**
 * 装备实例 —— 一件【具体的】装备。
 *
 * 同一个 EquipData 可以产出无数实例，差别在稀有度与 roll 到的随机词条。
 * 这是"刷装备"这件事的载体（§5.3）。
 */
struct HEXSPIRECORE_API FHexEquipInstance
{
	/** 全局唯一实例 id。0 = 空/无效 */
	int32 Uid = 0;

	FName EquipId;
	EHexRarity Rarity = EHexRarity::Common;

	/** 随机 roll 出的词条（不含固有词条） */
	TArray<FName> RolledAffixIds;

	/** 已重塑次数（§5.5 重塑面板；用于递增消耗） */
	int32 ReforgeCount = 0;

	bool IsValid() const { return Uid != 0 && !EquipId.IsNone(); }

	void Serialize(FArchive& Ar);

	uint32 ContentHash() const;
};

// ══════════════════════════════════════════════════════════ 内容库

struct HEXSPIRECORE_API FHexEquipLibrary
{
	// ── 词条
	static const TArray<FHexAffixDef>& AllAffixes();
	static const FHexAffixDef* FindAffix(FName Id);

	// ── 装备
	static const TArray<FHexEquipData>& AllEquips();
	static const FHexEquipData* FindEquip(FName Id);

	/** 某个槽位的全部装备 id（掉落用） */
	static void GetEquipIdsForSlot(EHexEquipSlot Slot, TArray<FName>& Out);

	/**
	 * 稀有度对应的随机词条数量（§5.3）。
	 *
	 * 普通 1 / 精良 2 / 稀有 3 / 史诗 4 / 传说 5。
	 * 线性递增而非跳跃：跳跃式（1/2/4/8）会让高稀有度装备形成
	 * 断层式碾压，中间稀有度立刻变成垃圾，掉落的期待感只剩最高一档。
	 */
	static int32 AffixCountForRarity(EHexRarity Rarity);
};

// ══════════════════════════════════════════════════════════ 生成器

struct HEXSPIRECORE_API FHexEquipGenerator
{
	/**
	 * 生成一件装备实例。
	 *
	 * ⚠️ 必须走 EHexRngStream::Loot 子流（架构纪律 1）。
	 *    用战斗流会让"打了几张牌"影响掉落结果，破坏可复现性。
	 *
	 * @param NextUid 实例 id 分配器（调用方自增）
	 */
	static FHexEquipInstance Generate(
		FName EquipId, EHexRarity Rarity, FHexRngStreams& Rng, int32 Uid);

	/**
	 * 按腐蚀度加权随机生成（§9.4：腐蚀度越高掉落越好）。
	 * @param Corruption 当前腐蚀度
	 */
	static FHexEquipInstance GenerateRandom(
		EHexEquipSlot Slot, int32 Corruption, FHexRngStreams& Rng, int32 Uid);

	/**
	 * 重塑：重新 roll 全部随机词条（§5.5）。
	 * 固有词条与稀有度不变 —— 重塑改的是"运气"，不是"品质"。
	 */
	static void Reforge(FHexEquipInstance& Inst, FHexRngStreams& Rng);

	/** 重塑消耗的碎片数（随次数递增，防止无限重 roll 到完美词条） */
	static int32 ReforgeCost(const FHexEquipInstance& Inst);

	/** 按腐蚀度决定稀有度 */
	static EHexRarity RollRarity(int32 Corruption, FHexRngStreams& Rng);
};

// ══════════════════════════════════════════════════════════ 三槽装载

/**
 * 三槽装备（§5.1：武器 / 盔甲 / 饰品）。
 *
 * 与符文 6 槽的关键差异：装备槽【无序】。
 * 符文槽有序是因为 §6.5 要靠顺序制造深度；
 * 装备是数值载体，给它排序只会增加无意义的操作负担。
 * 但接入 TriggerBus 时仍需固定 SlotOrder（10/11/12），
 * 否则同时机的装备触发顺序会漂移 —— 那是确定性问题，不是设计问题。
 */
class HEXSPIRECORE_API FHexEquipLoadout
{
public:
	FHexEquipLoadout();

	static constexpr int32 SlotCount = static_cast<int32>(EHexEquipSlot::Count);

	// ── 装卸

	const FHexEquipInstance* GetSlot(EHexEquipSlot Slot) const;

	/**
	 * 装备。会自动校验 EquipData 的槽位是否匹配。
	 * @return 是否成功（槽位不匹配或 id 未知时失败）
	 */
	bool Equip(const FHexEquipInstance& Inst);

	void Unequip(EHexEquipSlot Slot);

	int GetFilledCount() const;

	// ── 属性加成

	/**
	 * 某项属性的加成。
	 *
	 * @param BaseValue 该属性的基线值（用于计算 StatPercent）
	 * @return 加成量（平坦 + 基线×百分比），已取整
	 *
	 * ⚠️ 百分比基于【基线】而非【当前值】，避免装备之间互相乘算
	 *    导致"三件 +15% 变成 +52%"这种玩家算不明白的复利。
	 */
	int32 GetStatBonus(EHexStat Stat, int32 BaseValue) const;

	// ── 规则改写（与符文共用聚合语义）

	FHexRuneLoadout::FAggregated AggregateRule(EHexGameRule Rule) const;

	void GetOverriddenRules(TArray<EHexGameRule>& Out) const;

	// ── 结构改写

	/** 武器对基石《攻击》的覆写；未装武器或武器无覆写时 bActive=false */
	FHexAttackOverride GetAttackOverride() const;

	/** 全部注入的衍生卡 id */
	void GetInjectedCardIds(TArray<FName>& Out) const;

	// ── 触发器（供 TriggerBus 按 SlotOrder 10..12 挂载）

	/** 输出 (SlotOrder, 触发器指针)，按槽位升序 */
	void GetTriggersInOrder(TArray<TPair<int32, const FHexRuneTrigger*>>& Out) const;

	// ── 词条查询（UI 与验证器用）

	/** 某槽位的全部生效词条（固有 + 随机），按"固有优先"排序 */
	void GetAffixesOfSlot(EHexEquipSlot Slot, TArray<const FHexAffixDef*>& Out) const;

	void Serialize(FArchive& Ar);

	uint32 ContentHash() const;

private:
	FHexEquipInstance Slots[SlotCount];

	/** 遍历全部已装备槽的全部词条（固有 + 随机），按槽位升序 */
	void ForEachAffix(
		TFunctionRef<void(EHexEquipSlot, const FHexAffixDef&)> Fn) const;
};
