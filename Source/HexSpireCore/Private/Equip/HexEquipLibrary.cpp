// Copyright Hex Spire. All Rights Reserved.
//
// 装备内容表 —— 词条 22 条 + 装备 12 件（每槽 4 件）
//
// ══════════════════════════════════════════════════════════════════
// 词条设计的三条自律（与符文的约束【相反】，理由见 HexEquipData.h 文件头）
// ══════════════════════════════════════════════════════════════════
//   ① 装备【应该】给纯数值。§3.1 的"强数值养成"这一半就靠它。
//      平坦属性走伤害管线 ②，会被 §7.5 的系数化自动放大到每一张卡。
//   ② 但【禁止】给"某张卡 +5 伤害"。那会破坏卡牌间的相对优劣不变性。
//   ③ 数值量级参照镇妖者基线（HP80 ATK10 DEF8 AGI6 LUK5 CRIT10）：
//      单条平坦词条约为基线的 30%–80%，三槽满配约让属性翻倍。
//      翻倍是刻意的上限 —— 再高会让第一层的敌人数值（Godot 实测调过的）
//      全部失效，而那套数值是我们唯一的平衡锚点。

#include "Equip/HexEquipData.h"
#include "Battle/HexStatusData.h"

namespace
{
	const FName EquipStatAtk(TEXT("ATK"));
	const FName EquipStatDef(TEXT("DEF"));

	// ─────────────────────────────────────────── 词条构造辅助

	FHexAffixDef AffixFlat(
		const FName& Id, const FString& Name,
		const FName& Family,
		EHexStat Stat, int32 Value,
		EHexRarity MinRarity, int32 Weight,
		TArray<EHexEquipSlot>&& Slots,
		const FString& Text)
	{
		FHexAffixDef A;
		A.Id = Id;
		A.DisplayName = Name;
		A.Family = Family;
		A.Kind = EHexAffixKind::StatFlat;
		A.Stat = Stat;
		A.FlatValue = Value;
		A.MinRarity = MinRarity;
		A.Weight = Weight;
		A.AllowedSlots = MoveTemp(Slots);
		A.Text = Text;
		return A;
	}

	FHexAffixDef AffixPercent(
		const FName& Id, const FString& Name,
		const FName& Family,
		EHexStat Stat, float Percent,
		EHexRarity MinRarity, int32 Weight,
		TArray<EHexEquipSlot>&& Slots,
		const FString& Text)
	{
		FHexAffixDef A;
		A.Id = Id;
		A.DisplayName = Name;
		A.Family = Family;
		A.Kind = EHexAffixKind::StatPercent;
		A.Stat = Stat;
		A.PercentValue = Percent;
		A.MinRarity = MinRarity;
		A.Weight = Weight;
		A.AllowedSlots = MoveTemp(Slots);
		A.Text = Text;
		return A;
	}

	FHexAffixDef AffixRule(
		const FName& Id, const FString& Name,
		EHexGameRule Rule, int32 IntDelta, bool bBoolValue, bool bIsDelta,
		EHexRarity MinRarity, int32 Weight,
		TArray<EHexEquipSlot>&& Slots,
		const FString& Text)
	{
		FHexAffixDef A;
		A.Id = Id;
		A.DisplayName = Name;
		A.Kind = EHexAffixKind::RuleOverride;
		A.Rule.Rule = Rule;
		A.Rule.IntValue = IntDelta;
		A.Rule.bBoolValue = bBoolValue;
		A.Rule.bIsDelta = bIsDelta;
		A.MinRarity = MinRarity;
		A.Weight = Weight;
		A.AllowedSlots = MoveTemp(Slots);
		A.Text = Text;
		return A;
	}

	FHexAffixDef AffixTrigger(
		const FName& Id, const FString& Name,
		EHexTriggerTiming When,
		TArray<FHexEffectStep>&& Effects,
		int32 MaxPerRound,
		EHexRarity MinRarity, int32 Weight,
		TArray<EHexEquipSlot>&& Slots,
		const FString& Text)
	{
		FHexAffixDef A;
		A.Id = Id;
		A.DisplayName = Name;
		A.Kind = EHexAffixKind::Trigger;
		A.Trigger.When = When;
		A.Trigger.Effects = MoveTemp(Effects);
		A.Trigger.MaxPerRound = MaxPerRound;
		A.MinRarity = MinRarity;
		A.Weight = Weight;
		A.AllowedSlots = MoveTemp(Slots);
		A.Text = Text;
		return A;
	}

	FHexEffectStep SelfStep(FHexEffectStep S)
	{
		S.TargetFilter = EHexTargetFilter::Self;
		return S;
	}

	/**
	 * 治疗步骤。
	 * ⚠️ 治疗【刻意不走 §7.5 系数化】：固定 4 点。
	 *    若写成 ATK×0.4，后期 ATK 上到 200 时单次回血 80 点，
	 *    回复量会彻底压过敌人输出，"硬但公平"的节奏崩塌。
	 *    治疗与状态层数同属"离散锚点"（见 HexCardData.h 文件头的例外清单）。
	 */
	FHexEffectStep HealStep(int32 Amount)
	{
		FHexEffectStep S = FHexEffectStep::MakeOp(EHexEffectOp::Heal);
		S.FlatValue = static_cast<float>(Amount);
		S.StatRatio = 0.0f;
		S.TargetFilter = EHexTargetFilter::Self;
		return S;
	}

	// ═════════════════════════════════════════════════════════
	//                        词条表
	// ═════════════════════════════════════════════════════════

	TArray<FHexAffixDef> BuildAllAffixes()
	{
		TArray<FHexAffixDef> Out;
		Out.Reserve(24);

		// ───────── 平坦属性（10 条）—— 装备的主力
		//
		// ⚠️ 两条硬约束（都由 VerifyEquip 钉住）：
		//
		// ① 同族互斥：ATK 的三个档位共用族名 fam_atk，
		//    一件装备最多命中一条。没有这条，史诗装备能同时 roll 到
		//    +3/+5/+8 = +16，三槽满配 +37（基线 10 的 3.7 倍）。
		//
		// ② 槽位分工：攻击只来自武器、防御只来自盔甲。
		//    这既符合玩家直觉（刀加攻、甲加防），也是数值闸门 ——
		//    ATK 上限被锁在"一件武器的一条词条"，即 +10。
		//    三槽满配后 ATK 10→20、DEF 8→16、HP 80→约 115，
		//    属性大致翻倍，Godot 那套实测敌人数值仍然成立。

		Out.Add(AffixFlat(TEXT("af_atk_s"), TEXT("锋利"), TEXT("fam_atk"),
			EHexStat::ATK, 3,
			EHexRarity::Common, 120, { EHexEquipSlot::Weapon },
			TEXT("攻击力 +3")));

		Out.Add(AffixFlat(TEXT("af_atk_m"), TEXT("锐利"), TEXT("fam_atk"),
			EHexStat::ATK, 6,
			EHexRarity::Uncommon, 80, { EHexEquipSlot::Weapon },
			TEXT("攻击力 +6")));

		Out.Add(AffixFlat(TEXT("af_atk_l"), TEXT("凶戾"), TEXT("fam_atk"),
			EHexStat::ATK, 10,
			EHexRarity::Rare, 40, { EHexEquipSlot::Weapon },
			TEXT("攻击力 +10")));

		Out.Add(AffixFlat(TEXT("af_def_s"), TEXT("坚固"), TEXT("fam_def"),
			EHexStat::DEF, 3,
			EHexRarity::Common, 120, { EHexEquipSlot::Armor },
			TEXT("防御值 +3")));

		// ⚠️ DEF 对镇妖者是【超线性】收益：
		//    DEF 8→13 时减伤从 40% 升到 52%，同时《盾击》《防御》《铁壁》
		//    三张卡的数值一起涨（它们都吃 DEF 系数）。
		//    这是刻意的 —— 它让"堆防御"成为一条能被装备强化的真路线（§4.2）。
		Out.Add(AffixFlat(TEXT("af_def_m"), TEXT("厚重"), TEXT("fam_def"),
			EHexStat::DEF, 5,
			EHexRarity::Uncommon, 80, { EHexEquipSlot::Armor },
			TEXT("防御值 +5")));

		Out.Add(AffixFlat(TEXT("af_def_l"), TEXT("金刚"), TEXT("fam_def"),
			EHexStat::DEF, 8,
			EHexRarity::Rare, 40, { EHexEquipSlot::Armor },
			TEXT("防御值 +8")));

		Out.Add(AffixFlat(TEXT("af_agi_s"), TEXT("轻捷"), TEXT("fam_agi"),
			EHexStat::AGI, 4,
			EHexRarity::Common, 100,
			{ EHexEquipSlot::Armor, EHexEquipSlot::Trinket },
			TEXT("敏捷值 +4（提升闪避与移动力）")));

		Out.Add(AffixFlat(TEXT("af_agi_m"), TEXT("疾影"), TEXT("fam_agi"),
			EHexStat::AGI, 8,
			EHexRarity::Uncommon, 60,
			{ EHexEquipSlot::Armor, EHexEquipSlot::Trinket },
			TEXT("敏捷值 +8（每 8 点敏捷让移动卡多走 1 格）")));

		// LUK 允许武器与饰品：《引魂幡》这类"暴击倍率"路线的武器需要它
		Out.Add(AffixFlat(TEXT("af_luk_m"), TEXT("福缘"), TEXT("fam_luk"),
			EHexStat::LUK, 10,
			EHexRarity::Uncommon, 70,
			{ EHexEquipSlot::Weapon, EHexEquipSlot::Trinket },
			TEXT("幸运值 +10（提升暴击伤害倍率）")));

		Out.Add(AffixFlat(TEXT("af_crit_m"), TEXT("觅隙"), TEXT("fam_crit"),
			EHexStat::CRIT, 12,
			EHexRarity::Uncommon, 70,
			{ EHexEquipSlot::Weapon, EHexEquipSlot::Trinket },
			TEXT("暴击率 +12%")));

		// ───────── 百分比属性（3 条）
		//
		// ⚠️ 只给 HP 百分比。ATK/DEF 的百分比会与平坦词条叠加成
		//    "先加后乘"的复利，玩家算不明白（§13.2 要求数值可推算）。
		//    HP 是唯一例外：它不参与伤害管线，纯粹是容错量。
		//
		// 三条共用族名 fam_hp_pct，且只允许盔甲 ——
		// 上限锁在一件盔甲的一条，即 +32%（80 → 105）。

		Out.Add(AffixPercent(TEXT("af_hp_p_s"), TEXT("坚韧"), TEXT("fam_hp_pct"),
			EHexStat::HP, 0.12f,
			EHexRarity::Common, 110,
			{ EHexEquipSlot::Armor, EHexEquipSlot::Trinket },
			TEXT("最大生命 +12%")));

		Out.Add(AffixPercent(TEXT("af_hp_p_m"), TEXT("磐石"), TEXT("fam_hp_pct"),
			EHexStat::HP, 0.20f,
			EHexRarity::Uncommon, 70,
			{ EHexEquipSlot::Armor },
			TEXT("最大生命 +20%")));

		Out.Add(AffixPercent(TEXT("af_hp_p_l"), TEXT("不朽"), TEXT("fam_hp_pct"),
			EHexStat::HP, 0.32f,
			EHexRarity::Epic, 25,
			{ EHexEquipSlot::Armor },
			TEXT("最大生命 +32%")));

		// ───────── 规则改写（4 条）—— 与符文共用 GameRule
		//
		// ⚠️ 装备的规则改写必须【克制】。规则改写是符文的领地（§6.4 占 35%），
		//    装备抢这块会让两个系统的定位模糊。
		//    所以只放 4 条，且都挂高稀有度门槛。

		Out.Add(AffixRule(TEXT("af_energy"), TEXT("充盈"),
			EHexGameRule::EnergyMax, 1, false, /*bIsDelta=*/true,
			EHexRarity::Rare, 30,
			{ EHexEquipSlot::Trinket },
			TEXT("体力上限 +1")));

		Out.Add(AffixRule(TEXT("af_draw"), TEXT("洞察"),
			EHexGameRule::CardsDrawnPerTurn, 1, false, /*bIsDelta=*/true,
			EHexRarity::Rare, 30,
			{ EHexEquipSlot::Trinket },
			TEXT("每回合抽牌数 +1")));

		Out.Add(AffixRule(TEXT("af_capacity"), TEXT("囊纳"),
			EHexGameRule::DeckCapacity, 2, false, /*bIsDelta=*/true,
			EHexRarity::Uncommon, 50,
			{ EHexEquipSlot::Trinket, EHexEquipSlot::Armor },
			TEXT("卡组容量 +2")));

		Out.Add(AffixRule(TEXT("af_kb_immune"), TEXT("稳如"),
			EHexGameRule::KnockbackImmune, 0, true, /*bIsDelta=*/false,
			EHexRarity::Rare, 25,
			{ EHexEquipSlot::Armor },
			TEXT("免疫击退与拉拽")));

		// ───────── 触发效果（5 条）
		//
		// 挂在 §6.3 共享时机表上，与符文走同一条分发路径（SlotOrder 10-12，
		// 永远在符文之后结算）。

		Out.Add(AffixTrigger(TEXT("af_on_start_block"), TEXT("先备"),
			EHexTriggerTiming::OnBattleStart,
			{ FHexEffectStep::MakeBlock(0.0f, EquipStatDef, 1.0f) },
			/*MaxPerRound=*/-1,
			EHexRarity::Uncommon, 60,
			{ EHexEquipSlot::Armor },
			TEXT("战斗开始时，获得【防御×1.0】点格挡")));

		Out.Add(AffixTrigger(TEXT("af_on_kill_heal"), TEXT("饮血"),
			EHexTriggerTiming::OnKill,
			{ HealStep(4) },
			/*MaxPerRound=*/2,
			EHexRarity::Rare, 35,
			{ EHexEquipSlot::Weapon },
			TEXT("每次击杀敌人，回复 4 点生命。每回合最多 2 次")));

		Out.Add(AffixTrigger(TEXT("af_on_block_thorn"), TEXT("倒刺"),
			EHexTriggerTiming::OnDamageTaken,
			{ FHexEffectStep::MakeDamage(3.0f, EquipStatAtk, 0.0f) },
			/*MaxPerRound=*/3,
			EHexRarity::Uncommon, 50,
			{ EHexEquipSlot::Armor },
			TEXT("每次受到伤害，对攻击者造成 3 点伤害。每回合最多 3 次")));

		Out.Add(AffixTrigger(TEXT("af_on_move_dex"), TEXT("流转"),
			EHexTriggerTiming::OnMoveSelf,
			{ SelfStep(FHexEffectStep::MakeApplyStatus(FHexStatusLibrary::Dexterity, 1)) },
			/*MaxPerRound=*/2,
			EHexRarity::Uncommon, 45,
			{ EHexEquipSlot::Trinket },
			TEXT("每次自身移动后，获得 1 层【敏锐】。每回合最多 2 次")));

		Out.Add(AffixTrigger(TEXT("af_on_crit_burn"), TEXT("燎原"),
			EHexTriggerTiming::OnCrit,
			{ FHexEffectStep::MakeApplyStatus(FHexStatusLibrary::Burn, 2) },
			/*MaxPerRound=*/3,
			EHexRarity::Rare, 30,
			{ EHexEquipSlot::Weapon },
			TEXT("每次暴击时，对目标施加 2 层【燃烧】。每回合最多 3 次")));

		return Out;
	}

	// ═════════════════════════════════════════════════════════
	//                        装备表
	// ═════════════════════════════════════════════════════════

	FHexEquipData MakeEquip(
		const FName& Id, const FString& Name, EHexEquipSlot Slot,
		EHexRarity BaseRarity,
		TArray<FName>&& Inherent,
		const FString& Flavor)
	{
		FHexEquipData E;
		E.Id = Id;
		E.DisplayName = Name;
		E.Slot = Slot;
		E.BaseRarity = BaseRarity;
		E.InherentAffixIds = MoveTemp(Inherent);
		E.FlavorText = Flavor;
		return E;
	}

	TArray<FHexEquipData> BuildAllEquips()
	{
		TArray<FHexEquipData> Out;
		Out.Reserve(12);

		// ───────────────────── 武器（4 件）
		//
		// ⚠️ 武器的存在感【不来自数值】，来自对基石《攻击》的结构覆写（§5.2）。
		//    换一把武器 → 《攻击》的射程与形状变了 → 整个走位逻辑跟着变。
		//    这是装备系统里唯一能改变"怎么打"而不只是"打多少"的部分。

		// 《镇魂锏》：不覆写形状，纯数值武器。
		// 作用是给玩家一个"我不想改打法"的选项 —— 没有这个基准，
		// 其它三把武器的结构变化就无法被对比出来。
		{
			FHexEquipData E = MakeEquip(
				TEXT("wp_mace"), TEXT("镇魂锏"), EHexEquipSlot::Weapon,
				EHexRarity::Common, { TEXT("af_atk_s") },
				TEXT("锏身刻满经文，敲在骨头上会有回声。"));
			// 无 AttackOverride：《攻击》保持相邻单体
			Out.Add(E);
		}

		// 《长柄戟》：《攻击》→ 直线穿刺 2 格。
		// 让镇妖者能"站在原地打到第二排"，与它「不移动」的定位契合。
		{
			FHexEquipData E = MakeEquip(
				TEXT("wp_halberd"), TEXT("长柄戟"), EHexEquipSlot::Weapon,
				EHexRarity::Uncommon, { TEXT("af_atk_m") },
				TEXT("一丈二尺，杀的是不肯靠近的东西。"));
			E.AttackOverride.bActive = true;
			E.AttackOverride.Shape = EHexTargetShape::Line;
			E.AttackOverride.RangeMin = 1;
			E.AttackOverride.RangeMax = 2;
			E.AttackOverride.AreaSize = 2;
			E.AttackOverride.bRequiresLineOfSight = true;
			Out.Add(E);
		}

		// 《环首刀》：《攻击》→ 全部相邻。
		// 对 M/L 体型敌人是巨大加成（大体型的相邻格更多，§8.2.2 机制点 3），
		// 也让"被包围"从纯劣势变成一个可利用的局面。
		{
			FHexEquipData E = MakeEquip(
				TEXT("wp_ring_saber"), TEXT("环首刀"), EHexEquipSlot::Weapon,
				EHexRarity::Rare, { TEXT("af_atk_m"), TEXT("af_crit_m") },
				TEXT("一圈挥完，谁站得近谁倒。"));
			E.AttackOverride.bActive = true;
			E.AttackOverride.Shape = EHexTargetShape::AdjacentAll;
			E.AttackOverride.RangeMin = 0;
			E.AttackOverride.RangeMax = 1;
			E.AttackOverride.bRequiresLineOfSight = false;
			Out.Add(E);
		}

		// 《引魂幡》：《攻击》→ 3 格远程单体，并注入一张衍生卡。
		// 这是"武器改变卡组"的示范：装上它，卡组里多出一张不占容量的
		// 《招魂》，卸下即消失（§5.4）。
		{
			FHexEquipData E = MakeEquip(
				TEXT("wp_soul_banner"), TEXT("引魂幡"), EHexEquipSlot::Weapon,
				EHexRarity::Rare, { TEXT("af_atk_s"), TEXT("af_luk_m") },
				TEXT("幡一摇，该来的自己会来。"));
			E.AttackOverride.bActive = true;
			E.AttackOverride.Shape = EHexTargetShape::Single;
			E.AttackOverride.RangeMin = 1;
			E.AttackOverride.RangeMax = 3;
			E.AttackOverride.bRequiresLineOfSight = true;
			E.InjectedCardIds = { TEXT("eq_soul_call") };
			Out.Add(E);
		}

		// ───────────────────── 盔甲（4 件）

		Out.Add(MakeEquip(
			TEXT("ar_cloth"), TEXT("粗麻衣"), EHexEquipSlot::Armor,
			EHexRarity::Common, { TEXT("af_hp_p_s") },
			TEXT("洗得发白，但挡得住第一口。")));

		Out.Add(MakeEquip(
			TEXT("ar_scale"), TEXT("鱼鳞甲"), EHexEquipSlot::Armor,
			EHexRarity::Uncommon, { TEXT("af_def_m") },
			TEXT("一片压一片，像还活着。")));

		// 《镇邪重铠》：给镇妖者的"极端坦克"路线 ——
		// 免疫击退 + 战斗开始白给格挡，配合被动「格挡不清空」滚雪球
		Out.Add(MakeEquip(
			TEXT("ar_heavy"), TEXT("镇邪重铠"), EHexEquipSlot::Armor,
			EHexRarity::Rare, { TEXT("af_def_l"), TEXT("af_kb_immune") },
			TEXT("穿上就别想着躲了。")));

		Out.Add(MakeEquip(
			TEXT("ar_thorn"), TEXT("倒刺革衣"), EHexEquipSlot::Armor,
			EHexRarity::Uncommon, { TEXT("af_on_block_thorn") },
			TEXT("咬它的东西，牙会留在上面。")));

		// ───────────────────── 饰品（4 件）
		//
		// 饰品是"改玩法"的槽位：规则改写词条基本都限定在这里。

		Out.Add(MakeEquip(
			TEXT("tr_bell"), TEXT("摄魂铃"), EHexEquipSlot::Trinket,
			EHexRarity::Common, { TEXT("af_luk_m") },
			TEXT("摇一下，脚步声就乱了。")));

		Out.Add(MakeEquip(
			TEXT("tr_talisman"), TEXT("敕令符"), EHexEquipSlot::Trinket,
			EHexRarity::Rare, { TEXT("af_energy") },
			TEXT("纸是黄的，字是新写的。")));

		Out.Add(MakeEquip(
			TEXT("tr_compass"), TEXT("寻踪罗盘"), EHexEquipSlot::Trinket,
			EHexRarity::Rare, { TEXT("af_draw") },
			TEXT("指针从不指北，只指该去的地方。")));

		Out.Add(MakeEquip(
			TEXT("tr_pouch"), TEXT("百宝囊"), EHexEquipSlot::Trinket,
			EHexRarity::Uncommon, { TEXT("af_capacity") },
			TEXT("看着不大，掏什么都有。")));

		return Out;
	}
}

// ══════════════════════════════════════════════════════════ 查询接口

const TArray<FHexAffixDef>& FHexEquipLibrary::AllAffixes()
{
	static const TArray<FHexAffixDef> Affixes = BuildAllAffixes();
	return Affixes;
}

const FHexAffixDef* FHexEquipLibrary::FindAffix(FName Id)
{
	for (const FHexAffixDef& A : AllAffixes())
	{
		if (A.Id == Id)
		{
			return &A;
		}
	}
	return nullptr;
}

const TArray<FHexEquipData>& FHexEquipLibrary::AllEquips()
{
	static const TArray<FHexEquipData> Equips = BuildAllEquips();
	return Equips;
}

const FHexEquipData* FHexEquipLibrary::FindEquip(FName Id)
{
	for (const FHexEquipData& E : AllEquips())
	{
		if (E.Id == Id)
		{
			return &E;
		}
	}
	return nullptr;
}

void FHexEquipLibrary::GetEquipIdsForSlot(EHexEquipSlot Slot, TArray<FName>& Out)
{
	Out.Reset();
	for (const FHexEquipData& E : AllEquips())
	{
		if (E.Slot == Slot)
		{
			Out.Add(E.Id);
		}
	}
}
