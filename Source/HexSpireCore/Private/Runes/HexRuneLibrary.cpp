// Copyright Hex Spire. All Rights Reserved.
//
// 符文表（D6 / 策划案 §6）—— 第一版 17 个（用户决策 q11：我设计 12–18 个，你审）
//
// ⚠️ 三条不可动摇的约束（来自 HexRuneData.h 文件头）：
//   ① 纯数值符文占比【必须为 0】。本表 17 个符文里，
//      没有任何一个是"ATK +x%"这种无代价数值 —— 每一个都要么改写规则、
//      要么挂在时机上、要么带条件、要么有明确代价。
//   ② 标签只用于掉落加权 / UI 筛选，不产生任何加成。
//   ③ 6 槽有序，同时机按槽位 1→6 结算。
//
// §6.4 类别占比（目标 → 实际，由 VerifyRunes 钉住）：
//   规则改写 35% → 6/17 = 35.3%
//   触发器   30% → 5/17 = 29.4%
//   条件增益 15% → 3/17 = 17.6%
//   乘区    ≤8% → 1/17 =  5.9%
//   诅咒     12% → 2/17 = 11.8%
//
// ══════════════════════════════════════════════════════════════════
// 三组【非平凡组合】—— 用户决策 q11 要求至少 3 组。
// 非平凡 = 组合收益不是各自收益的简单相加，且玩家需要自己推理出来。
//
// 组合①「小卡组防御引擎」
//   薄刃契（卡组容量 -3，每回合多抽 1）
// + 轮回护符（每次洗回卡组 → 获得 DEF×1.0 格挡）
// + 空匣（抽牌堆为空时，回合开始获得 DEF×1.5 格挡）
//   → 卡组越小、抽牌越多，洗回频率越高（§7.4.4：11 张卡约 2.2 回合一轮，
//     缩到 5 张 + 多抽 1 后约 1 回合一轮）。三张符文互相放大对方的触发频率，
//     配合镇妖者「格挡不清空」被动滚雪球。单独拿任何一张都平淡。
//
// 组合②「顺序即策略」
//   砺石（②′ 加区：+ATK×0.5） + 倍影（②′ 乘区：×1.4）
//   → 槽位 [砺石, 倍影]：(ATK + ATK×0.5) × 1.4 = ATK×2.1
//     槽位 [倍影, 砺石]：ATK×1.4 + ATK×0.5     = ATK×1.9
//   同样两张符文，仅仅交换槽位就差 10% 伤害。这是 §6.5「6 槽有序」
//   带来的免费一层深度，也是教玩家"顺序有意义"的最佳教学关卡。
//
// 组合③「状态-伤害联动链」
//   点燃/焚心（施加燃烧） + 寻疵（对带燃烧的目标，②′ 加 ATK×0.6）
//   → 燃烧本身每层每回合只有 2 点（HexStatusData），单独看很弱；
//     接上寻疵后，"先点燃再打"变成一套固定连招，
//     再接焚心（暴击→施加燃烧）后形成自持循环。

#include "Runes/HexRuneLibrary.h"
#include "Core/HexSpireConstants.h"

namespace
{
	// ⚠️ 名字带 Rune 前缀：UE 的 unity build 会把多个 .cpp 合并进同一个
	//    编译单元，匿名命名空间里的同名符号会直接撞车（已踩过一次）。
	const FName RuneStatAtk(TEXT("ATK"));
	const FName RuneStatDef(TEXT("DEF"));

	// ───────────────────────────────────────── 构造辅助

	FHexRuneData MakeRune(
		const FName& Id,
		const FString& Name,
		EHexRarity Rarity,
		EHexRuneCategory Category,
		TArray<FName>&& Tags,
		const FString& Mechanic,
		const FString& Flavor)
	{
		FHexRuneData R;
		R.Id = Id;
		R.DisplayName = Name;
		R.Rarity = Rarity;
		R.Category = Category;
		R.Tags = MoveTemp(Tags);
		// ⚠️ R8 是真实风险：符文效果玩家看不懂 → 组合无法推理 → D6 价值归零。
		//    所以 MechanicText 必须写清"何时触发、触发几次、与什么交互"。
		R.MechanicText = Mechanic;
		R.FlavorText = Flavor;
		return R;
	}

	/** 布尔型规则开关（覆盖语义，不可累加） */
	FHexRuleOverride RuleBool(EHexGameRule Rule, bool bValue, int32 Order = 0)
	{
		FHexRuleOverride O;
		O.Rule = Rule;
		O.bBoolValue = bValue;
		O.bIsDelta = false;
		O.ApplyOrder = Order;
		return O;
	}

	/** 整数增量型规则（可叠加，例：EnergyMax +2） */
	FHexRuleOverride RuleIntDelta(EHexGameRule Rule, int32 Delta, int32 Order = 0)
	{
		FHexRuleOverride O;
		O.Rule = Rule;
		O.IntValue = Delta;
		O.bIsDelta = true;
		O.ApplyOrder = Order;
		return O;
	}

	/** 浮点增量型规则（可叠加，例：DamageMultiplier +0.5） */
	FHexRuleOverride RuleFloatDelta(EHexGameRule Rule, float Delta, int32 Order = 0)
	{
		FHexRuleOverride O;
		O.Rule = Rule;
		O.FloatValue = Delta;
		O.bIsDelta = true;
		O.ApplyOrder = Order;
		return O;
	}

	/** 挂在某个时机上的效果触发器 */
	FHexRuneTrigger TriggerOn(
		EHexTriggerTiming When,
		TArray<FHexEffectStep>&& Effects,
		int32 MaxPerRound = -1)
	{
		FHexRuneTrigger T;
		T.When = When;
		T.Effects = MoveTemp(Effects);
		T.MaxPerRound = MaxPerRound;
		return T;
	}

	/** ②′ 加区钩子（仅 OnAttack 有效） */
	FHexRuneTrigger HookAdd(float Flat, const FName& Stat, float Ratio)
	{
		FHexRuneTrigger T;
		T.When = EHexTriggerTiming::OnAttack;
		T.bHasValueAdd = true;
		T.ValueAddFlat = Flat;
		T.ValueAddStatRef = Stat;
		T.ValueAddRatio = Ratio;
		return T;
	}

	/** ②′ 乘区钩子（仅 OnAttack 有效） */
	FHexRuneTrigger HookMult(float Mult)
	{
		FHexRuneTrigger T;
		T.When = EHexTriggerTiming::OnAttack;
		T.bHasValueMult = true;
		T.ValueMult = Mult;
		return T;
	}

	// ───────────────────────────────────────── 条件辅助

	FHexEffectCondition CondTargetHPBelow(float Percent)
	{
		FHexEffectCondition C;
		C.Kind = FHexEffectCondition::EKind::TargetHPBelowPercent;
		C.FloatParam = Percent;
		return C;
	}

	FHexEffectCondition CondTargetHasStatus(const FName& StatusId)
	{
		FHexEffectCondition C;
		C.Kind = FHexEffectCondition::EKind::TargetHasStatus;
		C.NameParam = StatusId;
		return C;
	}

	FHexEffectCondition CondDrawPileEmpty()
	{
		FHexEffectCondition C;
		C.Kind = FHexEffectCondition::EKind::DrawPileEmpty;
		return C;
	}

	FHexEffectCondition CondCardsPlayedAtLeast(int32 N)
	{
		FHexEffectCondition C;
		C.Kind = FHexEffectCondition::EKind::CardsPlayedAtLeast;
		C.IntParam = N;
		return C;
	}

	/** 指向自身的效果（自伤 / 自我强化） */
	FHexEffectStep SelfTargeted(FHexEffectStep S)
	{
		S.TargetFilter = EHexTargetFilter::Self;
		return S;
	}

	/** 指向最近敌人的效果 */
	FHexEffectStep NearestTargeted(FHexEffectStep S)
	{
		S.TargetFilter = EHexTargetFilter::Nearest;
		return S;
	}

	// ═════════════════════════════════════════════════════════════
	//                        符文表
	// ═════════════════════════════════════════════════════════════

	TArray<FHexRuneData> BuildAllRunes()
	{
		TArray<FHexRuneData> Out;
		Out.Reserve(17);

		// ─────────────────── 一、规则改写（6 个 / 37.5%）
		//
		// D6 的核心武器。每一个都改变"游戏怎么玩"而不是"数字多大"。

		// 1. 先手符
		{
			FHexRuneData R = MakeRune(
				TEXT("rune_first_free"), TEXT("先手符"),
				EHexRarity::Uncommon, EHexRuneCategory::RuleRewrite,
				{ TEXT("节奏"), TEXT("体力") },
				TEXT("每回合打出的第 1 张牌不消耗体力。每回合恰好生效 1 次，与体力上限类符文可叠加。"),
				TEXT("贴在袖口，抬手第一式总是快过念头。"));
			R.RuleOverrides = { RuleBool(EHexGameRule::FirstCardFree, true) };
			Out.Add(R);
		}

		// 2. 薄刃契 —— 组合①的引擎
		{
			FHexRuneData R = MakeRune(
				TEXT("rune_thin_blade"), TEXT("薄刃契"),
				EHexRarity::Rare, EHexRuneCategory::RuleRewrite,
				{ TEXT("卡组"), TEXT("抽牌") },
				TEXT("卡组容量 -3，每回合抽牌数 +1。立即生效，与其它容量/抽牌符文按增量叠加。"
					 "副作用：卡组变小 → 洗回卡组的频率显著提高（触发【洗回卡组】类符文更频繁）。"),
				TEXT("刀越薄，出鞘越快，也越容易断。"));
			R.RuleOverrides = {
				RuleIntDelta(EHexGameRule::DeckCapacity, -3),
				RuleIntDelta(EHexGameRule::CardsDrawnPerTurn, 1),
			};
			Out.Add(R);
		}

		// 3. 铁誓 —— 对镇妖者是"反定位"诱惑：放弃你最强的一环换爆发
		{
			FHexRuneData R = MakeRune(
				TEXT("rune_iron_vow"), TEXT("铁誓"),
				EHexRarity::Uncommon, EHexRuneCategory::RuleRewrite,
				{ TEXT("体力"), TEXT("激进") },
				TEXT("体力上限 +2，但你无法再获得格挡（所有获得格挡的效果变为无效，已有格挡不会消失）。"
					 "与镇妖者被动【格挡不清空】直接冲突——这是刻意的取舍，不是 bug。"),
				TEXT("既然立了誓不退，盾就是多余的了。"));
			R.RuleOverrides = {
				RuleIntDelta(EHexGameRule::EnergyMax, 2),
				RuleBool(EHexGameRule::NoBlockAllowed, true),
			};
			Out.Add(R);
		}

		// 4. 负重咒
		{
			FHexRuneData R = MakeRune(
				TEXT("rune_burden"), TEXT("负重咒"),
				EHexRarity::Common, EHexRuneCategory::RuleRewrite,
				{ TEXT("手牌"), TEXT("费用") },
				TEXT("所有卡牌费用 -1（最低 0），但手牌上限 -4。"
					 "手牌上限降低意味着抽牌溢出会被卡住（抽不出来的牌留在抽牌堆），"
					 "与【每回合抽牌数 +N】类符文冲突。"),
				TEXT("压在背上的东西越重，脚下反而越稳。"));
			R.RuleOverrides = {
				RuleIntDelta(EHexGameRule::CardCostDelta, -1),
				RuleIntDelta(EHexGameRule::HandLimit, -4),
			};
			Out.Add(R);
		}

		// 5. 镇山印
		{
			FHexRuneData R = MakeRune(
				TEXT("rune_immovable"), TEXT("镇山印"),
				EHexRarity::Common, EHexRuneCategory::RuleRewrite,
				{ TEXT("位移抗性"), TEXT("守备") },
				TEXT("免疫一切击退与拉拽（含撞墙额外伤害），但你的移动类卡牌费用 +1。"
					 "对付【冲撞】型敌人与深坑地形极强，在需要走位躲避的战斗里是负担。"),
				TEXT("印落地，山不动。"));
			R.RuleOverrides = {
				RuleBool(EHexGameRule::KnockbackImmune, true),
				RuleIntDelta(EHexGameRule::MoveCostDelta, 1),
			};
			Out.Add(R);
		}

		// 6. 一期一刃 —— 极端 build 的入口
		{
			FHexRuneData R = MakeRune(
				TEXT("rune_one_edge"), TEXT("一期一刃"),
				EHexRarity::Rare, EHexRuneCategory::RuleRewrite,
				{ TEXT("消耗"), TEXT("抽牌") },
				TEXT("每回合抽牌数 +2，但所有攻击牌打出后【消耗】（进消耗区，本场战斗不再出现）。"
					 "战斗越长越吃亏；配合快速斩杀或【击杀→抽牌】类符文才成立。"),
				TEXT("一把刀只斩一次，所以那一次必须斩断。"));
			R.RuleOverrides = {
				RuleIntDelta(EHexGameRule::CardsDrawnPerTurn, 2),
				RuleBool(EHexGameRule::ExhaustAllAttacks, true),
			};
			Out.Add(R);
		}

		// ─────────────────── 二、触发器（5 个 / 31.3%）
		//
		// 挂在 §6.3 共享时机表上。符文之间靠"我触发的事件是你的触发条件"咬合。

		// 7. 轮回护符 —— 组合①
		{
			FHexRuneData R = MakeRune(
				TEXT("rune_cycle_ward"), TEXT("轮回护符"),
				EHexRarity::Uncommon, EHexRuneCategory::Trigger,
				{ TEXT("守备"), TEXT("卡组") },
				TEXT("每次弃牌堆洗回抽牌堆时，获得【防御×1.0】点格挡。每回合最多触发 2 次。"
					 "卡组越小洗回越频繁——与【薄刃契】《空匣》联动。"),
				TEXT("转过一圈，又回到起点，只是手里多了一面盾。"));
			R.Triggers = {
				TriggerOn(EHexTriggerTiming::OnDeckReshuffled,
					{ FHexEffectStep::MakeBlock(0.0f, RuneStatDef, 1.0f) },
					/*MaxPerRound=*/2)
			};
			Out.Add(R);
		}

		// 8. 推山手 —— 让《盾击》《冲撞》从"位移"变成"输出"
		{
			FHexRuneData R = MakeRune(
				TEXT("rune_push_hand"), TEXT("推山手"),
				EHexRarity::Common, EHexRuneCategory::Trigger,
				{ TEXT("推拉"), TEXT("位移") },
				TEXT("每次你推动或拉拽一个敌人时，对它造成【攻击×0.4】点伤害。"
					 "每回合最多触发 3 次。可与撞墙伤害（8 点）叠加。"),
				TEXT("推的不是人，是山。"));
			R.Triggers = {
				TriggerOn(EHexTriggerTiming::OnMoveEnemy,
					{ FHexEffectStep::MakeDamage(0.0f, RuneStatAtk, 0.4f) },
					/*MaxPerRound=*/3)
			};
			Out.Add(R);
		}

		// 9. 食魂
		{
			FHexRuneData R = MakeRune(
				TEXT("rune_soul_eater"), TEXT("食魂"),
				EHexRarity::Common, EHexRuneCategory::Trigger,
				{ TEXT("抽牌"), TEXT("击杀") },
				TEXT("每次击杀一个敌人，抽 1 张牌。每回合最多触发 2 次。"
					 "与【一期一刃】（攻击牌消耗）互补：靠击杀补充手牌。"),
				TEXT("吞下的不是血肉，是它还没做完的打算。"));
			R.Triggers = {
				TriggerOn(EHexTriggerTiming::OnKill,
					{ FHexEffectStep::MakeDraw(1) },
					/*MaxPerRound=*/2)
			};
			Out.Add(R);
		}

		// 10. 焚心 —— 组合③的自持环
		{
			FHexRuneData R = MakeRune(
				TEXT("rune_heart_burn"), TEXT("焚心"),
				EHexRarity::Rare, EHexRuneCategory::Trigger,
				{ TEXT("火焰"), TEXT("状态") },
				TEXT("每次暴击时，对被击中的目标施加 2 层【燃烧】。每回合最多触发 3 次。"
					 "与《寻疵》（对带燃烧目标加伤）形成循环：暴击→燃烧→加伤→更易暴击收益。"),
				TEXT("心一烧起来，就再也压不住了。"));
			R.Triggers = {
				TriggerOn(EHexTriggerTiming::OnCrit,
					{ FHexEffectStep::MakeApplyStatus(TEXT("burn"), 2) },
					/*MaxPerRound=*/3)
			};
			Out.Add(R);
		}

		// 11. 砺石 —— 组合②的加区侧
		{
			FHexRuneData R = MakeRune(
				TEXT("rune_whetstone"), TEXT("砺石"),
				EHexRarity::Common, EHexRuneCategory::Trigger,
				{ TEXT("顺序"), TEXT("加区") },
				TEXT("你的每次攻击在【平坦加成阶段】增加【攻击×0.5】。"
					 "⚠️ 这是加区：它在槽位顺序里【先】生效则会被后面的乘区放大，"
					 "【后】生效则不会。摆放位置直接改变结果。"),
				TEXT("刃口在石上走一趟，锋利是磨出来的，不是天生的。"));
			R.Triggers = { HookAdd(0.0f, RuneStatAtk, 0.5f) };
			Out.Add(R);
		}

		// ─────────────────── 三、条件增益（3 个 / 18.8%）
		//
		// 有条件 = 玩家要主动创造条件，而不是被动吃数值。

		// 12. 断头咒
		{
			FHexRuneData R = MakeRune(
				TEXT("rune_beheading"), TEXT("断头咒"),
				EHexRarity::Uncommon, EHexRuneCategory::Conditional,
				{ TEXT("处决"), TEXT("加区") },
				TEXT("攻击【生命低于 30%】的目标时，伤害额外增加【攻击×1.0】。"
					 "在平坦加成阶段结算，因此会被后续乘区放大。"),
				TEXT("话说到一半就该停了。"));
			FHexRuneTrigger T = HookAdd(0.0f, RuneStatAtk, 1.0f);
			T.Condition = CondTargetHPBelow(0.30f);
			R.Triggers = { T };
			Out.Add(R);
		}

		// 13. 寻疵 —— 组合③
		{
			FHexRuneData R = MakeRune(
				TEXT("rune_flaw_seeker"), TEXT("寻疵"),
				EHexRarity::Uncommon, EHexRuneCategory::Conditional,
				{ TEXT("状态"), TEXT("加区") },
				TEXT("攻击带有【燃烧】的目标时，伤害额外增加【攻击×0.6】。"
					 "与《点燃》（1 费施加 3 层燃烧）《焚心》联动；"
					 "燃烧自身每层每回合只造成 2 点，这条符文是它真正的价值来源。"),
				TEXT("烧过的地方会裂，裂缝就是下一刀的位置。"));
			FHexRuneTrigger T = HookAdd(0.0f, RuneStatAtk, 0.6f);
			T.Condition = CondTargetHasStatus(TEXT("burn"));
			R.Triggers = { T };
			Out.Add(R);
		}

		// 14. 空匣 —— 组合①
		{
			FHexRuneData R = MakeRune(
				TEXT("rune_empty_case"), TEXT("空匣"),
				EHexRarity::Uncommon, EHexRuneCategory::Conditional,
				{ TEXT("守备"), TEXT("卡组") },
				TEXT("回合开始时，若抽牌堆为空，获得【防御×1.5】点格挡。每回合最多 1 次。"
					 "卡组越小越容易满足条件——与【薄刃契】《轮回护符》联动。"),
				TEXT("匣子空了，才装得下别的东西。"));
			FHexRuneTrigger T = TriggerOn(EHexTriggerTiming::OnRoundStart,
				{ FHexEffectStep::MakeBlock(0.0f, RuneStatDef, 1.5f) },
				/*MaxPerRound=*/1);
			T.Condition = CondDrawPileEmpty();
			R.Triggers = { T };
			Out.Add(R);
		}

		// ─────────────────── 四、乘区（1 个 / 6.3%，§6.4 上限 8%）
		//
		// ⚠️ 乘区必须严格限量。乘区多了会变成"叠乘区就赢"，
		//    组合空间退化成一条乘法链（§6.4 明确点名的失败模式）。
		//    唯一这一个也带了条件，不是无脑数值。

		// 15. 倍影 —— 组合②的乘区侧
		{
			FHexRuneData R = MakeRune(
				TEXT("rune_twin_shadow"), TEXT("倍影"),
				EHexRarity::Rare, EHexRuneCategory::Multiplier,
				{ TEXT("顺序"), TEXT("乘区") },
				TEXT("你本回合打出的第 2 张及之后的攻击牌，伤害 ×1.4。"
					 "⚠️ 这是乘区：它会放大【它之前的槽位】提供的所有加成，"
					 "但不会放大排在它后面的加成。与《砺石》交换槽位可差约 10% 伤害。"),
				TEXT("影子跟着刀走，慢了半拍，所以砍中的是同一处。"));
			FHexRuneTrigger T = HookMult(1.4f);
			T.Condition = CondCardsPlayedAtLeast(2);
			R.Triggers = { T };
			Out.Add(R);
		}

		// ─────────────────── 五、诅咒（2 个 / 12.5%）
		//
		// 诅咒符文的定位：强度明显超标，代价明确且持续。
		// 它们让"要不要捡"变成真问题，也是腐蚀度正反馈的出口。

		// 16. 贪骨
		{
			FHexRuneData R = MakeRune(
				TEXT("rune_greed_bone"), TEXT("贪骨"),
				EHexRarity::Cursed, EHexRuneCategory::Curse,
				{ TEXT("诅咒"), TEXT("乘区") },
				TEXT("你造成的伤害 ×1.5（全局乘区）。每回合结束时，你受到 4 点伤害"
					 "（无视格挡与护盾，可致死）。装上后不可主动卸下，只能被替换。"),
				TEXT("它替你把力气借来了，利息按回合算。"));
			R.bIsCursed = true;
			R.RuleOverrides = { RuleFloatDelta(EHexGameRule::DamageMultiplier, 0.5f) };
			// ⚠️ 自伤走 Self 过滤器。放在 OnRoundEnd 而非 OnRoundStart：
			//    让玩家有机会在当回合先打完再吃这 4 点，读起来更符合"利息"的语义。
			R.Triggers = {
				TriggerOn(EHexTriggerTiming::OnRoundEnd,
					{ SelfTargeted(FHexEffectStep::MakeDamage(4.0f, RuneStatAtk, 0.0f)) },
					/*MaxPerRound=*/1)
			};
			Out.Add(R);
		}

		// 17. 咒锁
		{
			FHexRuneData R = MakeRune(
				TEXT("rune_hex_chain"), TEXT("咒锁"),
				EHexRarity::Cursed, EHexRuneCategory::Curse,
				{ TEXT("诅咒"), TEXT("卡组") },
				TEXT("卡组容量 +4，但手牌上限 -3，且每回合开始时对最近的敌人施加 1 层【流血】"
					 "（它移动时会受伤）——包括你不想惊动的敌人。"),
				TEXT("锁住的从来不只是一头。"));
			R.bIsCursed = true;
			R.RuleOverrides = {
				RuleIntDelta(EHexGameRule::DeckCapacity, 4),
				RuleIntDelta(EHexGameRule::HandLimit, -3),
			};
			R.Triggers = {
				TriggerOn(EHexTriggerTiming::OnRoundStart,
					{ NearestTargeted(FHexEffectStep::MakeApplyStatus(TEXT("bleed"), 1)) },
					/*MaxPerRound=*/1)
			};
			Out.Add(R);
		}

		return Out;
	}
}

// ═════════════════════════════════════════════════════════════ 查询接口

const TArray<FHexRuneData>& FHexRuneLibrary::AllRunes()
{
	static const TArray<FHexRuneData> Runes = BuildAllRunes();
	return Runes;
}

const FHexRuneData* FHexRuneLibrary::FindRune(FName Id)
{
	for (const FHexRuneData& R : AllRunes())
	{
		if (R.Id == Id)
		{
			return &R;
		}
	}
	return nullptr;
}

void FHexRuneLibrary::GetIdsByRarity(EHexRarity Rarity, TArray<FName>& Out)
{
	Out.Reset();
	for (const FHexRuneData& R : AllRunes())
	{
		if (R.Rarity == Rarity)
		{
			Out.Add(R.Id);
		}
	}
}

int32 FHexRuneLibrary::CountByCategory(EHexRuneCategory Category)
{
	int32 N = 0;
	for (const FHexRuneData& R : AllRunes())
	{
		if (R.Category == Category)
		{
			++N;
		}
	}
	return N;
}
