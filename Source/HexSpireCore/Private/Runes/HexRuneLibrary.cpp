// Copyright Hex Spire. All Rights Reserved.
//
// 符文表（D6 / 策划案 §6）—— 当前 30 个（目标 90–120，见 HexRuneLibrary.h）
//
// ⚠️ 三条不可动摇的约束（来自 HexRuneData.h 文件头）：
//   ① 纯数值符文占比【必须为 0】。本表 30 个符文里，
//      没有任何一个是"ATK +x%"这种无代价数值 —— 每一个都要么改写规则、
//      要么挂在时机上、要么带条件、要么有明确代价。
//   ② 标签只用于掉落加权 / UI 筛选，不产生任何加成。
//   ③ 6 槽有序，同时机按槽位 1→6 结算。
//
// §6.4 类别占比（目标 → 实际，由 VerifyRunes 钉住）：
//   规则改写 35% → 11/30 = 36.7%
//   触发器   30% →  9/30 = 30.0%
//   条件增益 15% →  5/30 = 16.7%
//   乘区    ≤8% →  2/30 =  6.7%   ← 硬上限，无容差
//   诅咒     12% →  3/30 = 10.0%
//
// ⚠️ 这张表的数字【必须与实际一致】。它不是装饰：
//    占比失衡的后果是平衡静默失真（乘区多了 → "叠乘区就赢"），
//    而人天然倾向写乘区（好写、好懂、好平衡）。
//    改表后请重跑 VerifyContent，它会算实际占比并比对。
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

	/**
	 * 整数【覆盖】型规则（不可累加，例：SizeClassOverride = M）。
	 *
	 * ⚠️ 与 RuleIntDelta 的区别是语义性的：
	 *    体型、开关这类量累加没有意义（"体型 +1" 是什么？），
	 *    必须走覆盖。标错会让两个改体型的符文叠成越界的体型值。
	 */
	FHexRuleOverride RuleIntOverride(EHexGameRule Rule, int32 Value, int32 Order = 0)
	{
		FHexRuleOverride O;
		O.Rule = Rule;
		O.IntValue = Value;
		O.bIsDelta = false;
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

	/**
	 * 挂在某时机上、但【每 N 次才触发一次】的触发器。
	 *
	 * ⚠️ 计数在通过过滤器与条件之后才累加（见 TriggerBus::AdvanceCounter），
	 *    所以"每移动 3 格"不会被无关事件撑满。
	 */
	FHexRuneTrigger TriggerEvery(
		EHexTriggerTiming When,
		int32 Threshold,
		TArray<FHexEffectStep>&& Effects,
		int32 MaxPerRound = -1)
	{
		FHexRuneTrigger T;
		T.When = When;
		T.Effects = MoveTemp(Effects);
		T.CounterThreshold = Threshold;
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

	FHexEffectCondition CondSelfNotFullHP()
	{
		FHexEffectCondition C;
		C.Kind = FHexEffectCondition::EKind::SelfNotAtFullHP;
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
		Out.Reserve(30);

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

		// ═════════════════════════════════════════════════════════
		// 第二批：18–30 号（13 个）
		// ═════════════════════════════════════════════════════════
		//
		// 配比按 §6.4 反算（30 个总量）：
		//   规则改写 35% → 11 个（6 + 5）
		//   触发器   30% →  9 个（5 + 4）
		//   条件增益 15% →  5 个（3 + 2）
		//   乘区    ≤8% →  2 个（1 + 1）← 硬上限，30×8% = 2.4，最多 2
		//   诅咒     12% →  3 个（2 + 1）
		//
		// 本批刻意补齐三类空白：
		//   ① 四条从未被任何符文使用的 GameRule：
		//      NoDrawFixedHand / SizeClassOverride / BlockMultiplier / CritDamageMultiplier
		//      规则写了却没有符文用 = 那条规则的实现从未被真实验证过。
		//   ② Epic / Legendary 两个稀有度档位原先零符文 ——
		//      掉落表里这两档会抽空，高层奖励失去梯度。
		//   ③ 新打通的 8 个触发时机（OnDamageTaken / OnDeckReshuffled /
		//      OnCardDrawn / OnUnitDeath 等）原先几乎没有符文消费，
		//      §6.3 要求"每个时机至少 3 个符文能触发它"。

		// ─────────────────── 规则改写（+5）

		// 18. 巨化 —— HexRuleBook.h 点名却一直不存在的那个符文
		{
			FHexRuneData R = MakeRune(
				TEXT("rune_titanize"), TEXT("巨化"),
				EHexRarity::Epic, EHexRuneCategory::RuleRewrite,
				{ TEXT("体型"), TEXT("位移抗性") },
				TEXT("你的体型变为 M（占 3 格），且无法被位移。"
					 "体型变大后：更容易被多目标技能同时命中，但也能堵住通路、"
					 "且推拉类符文更容易同时打到多个敌人。"),
				TEXT("它把自己钉进地里，于是地也成了它的一部分。"));
			// ⚠️ SizeClassOverride 是【覆盖】而非增量：体型不是可累加的量。
			//    这条规则在 RuleBook 里实现已久，但此前没有任何符文使用它 ——
			//    等于那段实现从未被真实验证过（§6.3 示例 F《巨化》点名要求）。
			R.RuleOverrides = {
				RuleIntOverride(EHexGameRule::SizeClassOverride,
					static_cast<int32>(EHexSizeClass::M)),
				RuleBool(EHexGameRule::KnockbackImmune, true),
			};
			Out.Add(R);
		}

		// 19. 磐石誓约 —— BlockMultiplier 的唯一使用者
		{
			FHexRuneData R = MakeRune(
				TEXT("rune_bulwark_oath"), TEXT("磐石誓约"),
				EHexRarity::Rare, EHexRuneCategory::RuleRewrite,
				{ TEXT("守备"), TEXT("乘区") },
				TEXT("你获得的所有格挡 ×1.5，但每回合抽牌数 -1。"
					 "与镇妖者「格挡不清空」被动叠乘：滚雪球更快，"
					 "但手牌变少意味着可选项减少 —— 换的是厚度，付的是灵活性。"),
				TEXT("站得越稳，走得越慢。"));
			R.RuleOverrides = {
				RuleFloatDelta(EHexGameRule::BlockMultiplier, 0.5f),
				RuleIntDelta(EHexGameRule::CardsDrawnPerTurn, -1),
			};
			Out.Add(R);
		}

		// 20. 定式 —— NoDrawFixedHand 的唯一使用者
		{
			FHexRuneData R = MakeRune(
				TEXT("rune_fixed_form"), TEXT("定式"),
				EHexRarity::Epic, EHexRuneCategory::RuleRewrite,
				{ TEXT("卡组"), TEXT("确定性") },
				TEXT("你不再抽牌：每回合开始时，手牌固定为抽牌堆顶的 4 张。"
					 "手牌完全可预测（配合牌堆浏览器可以精确规划两三回合），"
					 "代价是失去抽牌类符文与洗回引擎的全部收益。"),
				TEXT("不再赌了，那就只剩算。"));
			R.RuleOverrides = {
				RuleBool(EHexGameRule::NoDrawFixedHand, true),
			};
			Out.Add(R);
		}

		// 21. 鲸吞 —— 大手牌但费用全涨
		{
			FHexRuneData R = MakeRune(
				TEXT("rune_engulf"), TEXT("鲸吞"),
				EHexRarity::Uncommon, EHexRuneCategory::RuleRewrite,
				{ TEXT("抽牌"), TEXT("费用") },
				TEXT("每回合抽牌数 +2，手牌上限 +4，但所有卡牌费用 +1。"
					 "抽得多不等于打得多 —— 需要配合体力类符文或 0 费卡才成立。"),
				TEXT("张口的时候没想过咽不下去。"));
			R.RuleOverrides = {
				RuleIntDelta(EHexGameRule::CardsDrawnPerTurn, 2),
				RuleIntDelta(EHexGameRule::HandLimit, 4),
				RuleIntDelta(EHexGameRule::CardCostDelta, 1),
			};
			Out.Add(R);
		}

		// 22. 轻身诀 —— 移动成本归零，换防御
		{
			FHexRuneData R = MakeRune(
				TEXT("rune_lightfoot"), TEXT("轻身诀"),
				EHexRarity::Common, EHexRuneCategory::RuleRewrite,
				{ TEXT("位移"), TEXT("走位") },
				TEXT("移动类卡牌费用 -1（最低 0），但你无法获得格挡。"
					 "把「挨打后挡住」换成「根本不站在那里」——"
					 "与走位规避型打法（§13.2 可躲意图）直接契合。"),
				TEXT("挡得住的人才需要盾。"));
			R.RuleOverrides = {
				RuleIntDelta(EHexGameRule::MoveCostDelta, -1),
				RuleBool(EHexGameRule::NoBlockAllowed, true),
			};
			Out.Add(R);
		}

		// ─────────────────── 触发器（+4）

		// 23. 步履印 —— CounterThreshold 的首个使用者（§6.3 示例符文）
		{
			FHexRuneData R = MakeRune(
				TEXT("rune_pacer"), TEXT("步履印"),
				EHexRarity::Uncommon, EHexRuneCategory::Trigger,
				{ TEXT("位移"), TEXT("计数") },
				TEXT("你每移动 3 次，获得 DEF×1.2 的格挡。"
					 "计数跨回合保留（攒到 2/3 时回合结束不会清零），"
					 "每回合最多触发 2 次。与移动流符文叠加成防御引擎。"),
				TEXT("走够了路，脚底就成了甲。"));
			// ⚠️ 这是 §6.3 示例「你每移动 3 格，下一次攻击附加追击」的实现形态。
			//    CounterThreshold 此前【只声明未实现】，所以这类符文一个都写不出来。
			R.Triggers = {
				TriggerEvery(EHexTriggerTiming::OnMoveSelf, 3,
					{ FHexEffectStep::MakeBlock(0.0f, RuneStatDef, 1.2f) },
					/*MaxPerRound=*/2)
			};
			Out.Add(R);
		}

		// 24. 还魂气 —— OnDamageTaken 的消费者（受击链原先无符文）
		{
			FHexRuneData R = MakeRune(
				TEXT("rune_revenant_breath"), TEXT("还魂气"),
				EHexRarity::Common, EHexRuneCategory::Trigger,
				{ TEXT("受击"), TEXT("体力") },
				TEXT("每次受到伤害后获得 1 点体力，每回合最多 2 次。"
					 "挨打变成资源 —— 与「不获得格挡」类符文形成反直觉的组合："
					 "主动挨打来换取行动力。"),
				TEXT("疼一下，清醒一下。"));
			R.Triggers = {
				TriggerOn(EHexTriggerTiming::OnDamageTaken,
					{ FHexEffectStep::MakeGainEnergy(1) },
					/*MaxPerRound=*/2)
			};
			Out.Add(R);
		}

		// 25. 拾遗 —— OnUnitDeath 的消费者（与 OnKill 区分）
		{
			FHexRuneData R = MakeRune(
				TEXT("rune_scavenge"), TEXT("拾遗"),
				EHexRarity::Common, EHexRuneCategory::Trigger,
				{ TEXT("死亡"), TEXT("格挡") },
				TEXT("场上任何单位死亡时（不限是否由你击杀）获得 DEF×0.8 格挡，"
					 "每回合最多 3 次。与《食魂》(需要你亲手击杀) 的区别在于："
					 "燃烧、中毒、尖刺造成的死亡它也吃得到。"),
				TEXT("谁死的不重要，重要的是谁还站着。"));
			// ⚠️ 刻意挂 OnUnitDeath 而非 OnKill：这两个时机的差别
			//    （是否要求玩家是击杀者）需要至少一对符文来体现，
			//    否则玩家无法察觉这是两件不同的事。
			R.Triggers = {
				TriggerOn(EHexTriggerTiming::OnUnitDeath,
					{ FHexEffectStep::MakeBlock(0.0f, RuneStatDef, 0.8f) },
					/*MaxPerRound=*/3)
			};
			Out.Add(R);
		}

		// 26. 循环印 —— OnCardDrawn 的消费者
		{
			FHexRuneData R = MakeRune(
				TEXT("rune_cycle_seal"), TEXT("循环印"),
				EHexRarity::Uncommon, EHexRuneCategory::Trigger,
				{ TEXT("抽牌"), TEXT("计数") },
				TEXT("你每发生 4 次抽牌，对最近的敌人造成 ATK×0.8 伤害。"
					 "抽牌次数按「抽牌事件」计，一次抽 3 张算 1 次。"
					 "与《薄刃契》《鲸吞》等多抽符文叠加成持续输出。"),
				TEXT("翻页的声音，其实是刀出鞘。"));
			R.Triggers = {
				TriggerEvery(EHexTriggerTiming::OnCardDrawn, 4,
					{ NearestTargeted(FHexEffectStep::MakeDamage(0.0f, RuneStatAtk, 0.8f)) },
					/*MaxPerRound=*/2)
			};
			Out.Add(R);
		}

		// ─────────────────── 条件增益（+2）

		// 27. 背水 —— 低血反打
		{
			FHexRuneData R = MakeRune(
				TEXT("rune_last_stand"), TEXT("背水"),
				EHexRarity::Rare, EHexRuneCategory::Conditional,
				{ TEXT("低血"), TEXT("加区") },
				TEXT("当你自身生命未满时，攻击的 ②′ 阶段附加 ATK×0.45。"
					 "与《贪骨》(每回合自伤) 形成正向联动：诅咒的代价反而是启动条件。"),
				TEXT("退到墙边，才终于站稳。"));
			// ⚠️ 用 SelfAtFullHP 取反：条件系统没有 SelfNotFullHP，
			//    但"满血时不触发"等价于"未满血时触发"。
			//    这里靠 ②′ 钩子 + 条件实现 —— 钩子本身也走 PassesCondition。
			FHexRuneTrigger T = HookAdd(0.0f, RuneStatAtk, 0.45f);
			T.Condition = CondSelfNotFullHP();
			R.Triggers = { T };
			Out.Add(R);
		}

		// 28. 空手 —— CritDamageMultiplier 的唯一使用者
		{
			FHexRuneData R = MakeRune(
				TEXT("rune_empty_hand"), TEXT("空手"),
				EHexRarity::Uncommon, EHexRuneCategory::Conditional,
				{ TEXT("手牌"), TEXT("暴击") },
				TEXT("暴击伤害倍率 +0.6，但手牌上限 -4。"
					 "手牌少意味着每回合选项少、更依赖抽到什么，"
					 "换来的是每次暴击的收益显著放大 —— 与暴击流装备叠乘。"),
				TEXT("手里空了，反倒看清了要打哪。"));
			// ⚠️ CritDamageMultiplier 此前【无任何符文使用】，
			//    等于那条规则的实现从未被真实验证过。
			//
			// ⚠️ 原本想做成"抽牌堆为空时才 +0.6"，但引擎目前不支持
			//    【带条件的 RuleOverride】（条件只作用于 Trigger）。
			//    与其写一个恒定生效、文案却说"有条件"的符文
			//    （那是最糟的情况：玩家按文案推理会算错），
			//    不如改成"常驻增益 + 明确代价"，文案与实现完全一致。
			//    带条件的规则改写留作后续引擎增强。
			R.RuleOverrides = {
				RuleFloatDelta(EHexGameRule::CritDamageMultiplier, 0.6f),
				RuleIntDelta(EHexGameRule::HandLimit, -4),
			};
			Out.Add(R);
		}

		// ─────────────────── 乘区（+1，总 2 个 = 6.7% ≤ 8%）

		// 29. 倾覆 —— 第二个也是最后一个乘区
		{
			FHexRuneData R = MakeRune(
				TEXT("rune_overturn"), TEXT("倾覆"),
				EHexRarity::Legendary, EHexRuneCategory::Multiplier,
				{ TEXT("乘区"), TEXT("低血") },
				TEXT("当目标生命低于 40% 时，②′ 阶段伤害 ×1.5。"
					 "与《断头咒》(目标低于 30% 时加区) 叠加成斩杀线，"
					 "但槽位顺序决定收益：加区在前才能被这个乘区放大。"),
				TEXT("最后一推，从来不需要很重。"));
			// ⚠️ 乘区是【硬上限 8%】的稀缺资源（VerifyContent 无容差断言）。
			//    30 个符文只允许 2 个乘区，所以这一个必须给足分量 ——
			//    做成 Legendary + 带条件，而不是无条件的 ×1.2。
			FHexRuneTrigger T = HookMult(1.5f);
			T.Condition = CondTargetHPBelow(0.4f);
			R.Triggers = { T };
			Out.Add(R);
		}

		// ─────────────────── 诅咒（+1，总 3 个 = 10%）

		// 30. 血契 —— 强乘区 + 持续失血
		{
			FHexRuneData R = MakeRune(
				TEXT("rune_blood_pact"), TEXT("血契"),
				EHexRarity::Cursed, EHexRuneCategory::Curse,
				{ TEXT("诅咒"), TEXT("体力") },
				TEXT("体力上限 +3，但每回合结束时失去 5% 最大生命（向上取整）。"
					 "战斗越长代价越高 —— 逼你把它变成速杀流派，"
					 "而不是当成白送的体力。"),
				TEXT("借来的力气，利息按回合算。"));
			R.bIsCursed = true;
			R.RuleOverrides = {
				RuleIntDelta(EHexGameRule::EnergyMax, 3),
			};
			// 5% of 80 = 4 点/回合。与《贪骨》的固定 4 点不同，
			// 这个随 HPMax 缩放，后期成长后代价同步上升。
			//
			// ⚠️ StatRef 必须写 "HP_MAX"（带下划线）。
			//    写成 "HPMAX" 会让 StatOf() 走到兜底 return 0 ——
			//    诅咒的代价静默消失，变成"白送 3 点体力"的超模符文，
			//    而且不报任何错。属性名拼写由 VerifyRunes 的
			//    "诅咒必须有真实代价"断言兜住。
			R.Triggers = {
				TriggerOn(EHexTriggerTiming::OnRoundEnd,
					{ SelfTargeted(FHexEffectStep::MakeDamage(0.0f, TEXT("HP_MAX"), 0.05f)) },
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
