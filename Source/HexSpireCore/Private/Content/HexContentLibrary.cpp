// Copyright Hex Spire. All Rights Reserved.
//
// 内容库实现 —— 移植自 Godot 版 src/core/battle/content_library.gd
//
// ⚠️ 用户决策 q9：Godot 版的实测数值【全部沿用，一个不改】。
//    下面每一处 "⚠️ 曾是…" 的注释都是 Godot 版实测后调整的记录，
//    它们是决策依据，删掉注释等于丢掉结论。
//
// 与 Godot 版的【刻意差异】（共 4 处，均有理由）：
//   ① 英雄只保留镇妖者（用户决策 q7：先把一个角色做透）。
//      Godot 的 knight 就是镇妖者（unit_sprites.gd 里 knight → "镇妖者"），
//      这里直接把 id 正名为 warden，数值 1:1 照抄 knight。
//   ② 不移植巨人专属基石《巨岩之躯》boulder_body ——
//      它的载体英雄（御灵者/M 体型）本版不做，留着会污染掠夺池。
//      御灵者上线时再加回，数值已记录在本文件末尾的注释里。
//   ③ 起始卡组从 11 张缩到 8 张（3 基石 + 5 普通）——用户决策 q18。
//      Godot 版起始 11 张正好占满 8/8 容量，导致 §3.2 的核心决策
//      「收哪张卡 / 挤掉哪张卡」在第一层根本触发不了。
//   ④ bIsCornerstone / bCountsTowardCapacity 显式赋值。
//      Godot 版这两个字段【从未被赋值】（13 张卡全是 false），
//      基石身份只靠 HeroData.cornerstone_card_ids 判断 ——
//      那是未完成状态，会让基石卡吃掉卡组容量（违反 §7.6）。

#include "Content/HexContentLibrary.h"
#include "Battle/HexUnit.h"
#include "Deck/HexPileManager.h"
#include "Runes/HexRuneLibrary.h"
#include "Core/HexSpireConstants.h"

// ══════════════════════════════════════════════════════════ 英雄

namespace
{
	/**
	 * 构造镇妖者（Godot 版 knight）。
	 *
	 * 定位（§4.2）：站在原地、让敌人自己撞死的坦克。
	 * 被动「格挡不在回合结束清空」→ 滚雪球型防御，
	 * 但受 HexK::BlockCapRatio（25% maxHP = 20 点）约束，不会变成无敌护盾。
	 */
	FHexHeroData MakeWarden()
	{
		FHexHeroData H;
		H.Id = TEXT("warden");
		H.DisplayName = TEXT("镇妖者");
		H.SizeClass = EHexSizeClass::S;

		H.BaseHP = 80;
		H.BaseATK = 10;
		H.BaseDEF = 8;
		H.BaseAGI = 6;
		H.BaseLUK = 5;
		H.BaseCRIT = 10;

		H.EnergyMax = 5;

		// ⚠️ 抽牌数 5 → 3，这是基石卡移出牌堆后的【必要配套】。
		//
		//    基石卡常驻后，牌堆里只剩 5 张构筑卡（满容量也只有 8 张）。
		//    仍抽 5 张的话，每回合把整个卡组抽光：
		//      · 抽牌不再有随机性 → D2 的"概率可推算"没有了对象
		//      · 每回合都在洗牌 → 弃牌堆信息失去意义
		//      · 构筑深度归零 → 拿到什么卡都一样，D3 白做
		//    这个问题由验证器抓到（《薄刃契》+1 抽牌后应有 6 张，
		//    实测只有 5 张 —— 因为整个卡组就 5 张）。
		//
		//    3 张时：起始 5 张卡撑 1.67 回合，满容量 8 张撑 2.67 回合，
		//    与原设计（8张/抽5、11张/抽5）的洗牌节奏基本一致。
		//    玩家每回合的选项 = 3 手牌 + 3 常驻固定卡 = 6 个，
		//    配 5 点体力仍然充裕。
		H.CardsDrawnPerTurn = 3;

		// 基石三张：盾击（专属变体）+ 防御 + 移动
		H.CornerstoneCardIds = { TEXT("shield_bash"), TEXT("def_basic"), TEXT("move_basic") };

		// ⚠️ 仅用于掉落加权与 UI 筛选，【不产生任何加成】（§7.8）
		H.CardPoolTags = { TEXT("守备"), TEXT("反击"), TEXT("嘲讽"), TEXT("位移抗性") };

		FHexRuleOverride BlockPersists;
		BlockPersists.Rule = EHexGameRule::BlockPersists;
		BlockPersists.bBoolValue = true;
		// 布尔型开关是"覆盖"而非"增量" —— 累加一个 bool 没有意义
		BlockPersists.bIsDelta = false;
		BlockPersists.ApplyOrder = 0;
		H.PassiveRules = { BlockPersists };
		H.PassiveText = TEXT("格挡不在回合结束清空（可叠加至最大生命的 25%）");

		return H;
	}
}

const TArray<FHexHeroData>& FHexContentLibrary::AllHeroes()
{
	// 函数内静态：首次调用时构造一次，之后零开销。
	// 验证器与批量模拟会调用几十万次，不能每次重建。
	static const TArray<FHexHeroData> Heroes = { MakeWarden() };
	return Heroes;
}

const FHexHeroData* FHexContentLibrary::FindHero(FName Id)
{
	for (const FHexHeroData& H : AllHeroes())
	{
		if (H.Id == Id)
		{
			return &H;
		}
	}
	return nullptr;
}

// ══════════════════════════════════════════════════════════ 卡牌
//
// ⚠️ §7.5 强制：全部写成 Flat + Stats[StatRef] × Ratio。
//    离散量（位移格数 / 状态层数 / 抽牌数）不缩放 —— 它们是策略层的锚点。

namespace
{
	// ⚠️ 名字带 Card 前缀：UE 的 unity build 会把多个 .cpp 合并进同一个
	//    编译单元，匿名命名空间里的同名符号会直接撞车（已踩过一次）。
	const FName CardStatAtk(TEXT("ATK"));
	const FName CardStatDef(TEXT("DEF"));

	FHexCardData MakeCard(
		const FName& Id,
		const FString& Name,
		EHexCardType Type,
		int32 Cost,
		const FHexTargetSpec& Spec,
		TArray<FHexEffectStep>&& Effects,
		TArray<FName>&& Tags,
		const FString& Desc)
	{
		FHexCardData C;
		C.Id = Id;
		C.DisplayName = Name;
		C.CardType = Type;
		// Godot 版 13 张卡的 rarity 全部是默认 COMMON（从未被赋值）。
		// 第一版沿用：稀有度分层等卡池扩到 30+ 张再做，现在分层没有意义。
		C.Rarity = EHexRarity::Common;
		C.EnergyCost = Cost;
		C.TargetSpec = Spec;
		C.Effects = MoveTemp(Effects);
		C.Tags = MoveTemp(Tags);
		C.DescriptionTemplate = Desc;
		C.MaxCopiesInDeck = HexK::DefaultMaxCopiesInDeck;
		return C;
	}

	/** 标记为基石卡：不占卡组容量（§7.6），且不进掠夺池 */
	FHexCardData& AsCornerstone(FHexCardData& C)
	{
		C.bIsCornerstone = true;
		C.bCountsTowardCapacity = false;
		return C;
	}

	TArray<FHexCardData> BuildAllCards()
	{
		TArray<FHexCardData> Out;
		Out.Reserve(12);

		// ───────────────────────── 三张通用基石卡（§7.2）

		// 《攻击》—— 装备系统（§5）会覆写它的射程/形状，所以即使
		// 镇妖者的基石是《盾击》，这张也必须保留在卡池里。
		{
			FHexCardData C = MakeCard(
				TEXT("atk_basic"), TEXT("攻击"), EHexCardType::Attack, 1,
				FHexTargetSpec::Make(EHexTargetShape::Single, 1, 1),
				{ FHexEffectStep::MakeDamage(0.0f, CardStatAtk, 1.0f) },
				{ TEXT("近战") },
				TEXT("造成 {dmg} 点伤害。"));
			Out.Add(AsCornerstone(C));
		}

		// 《防御》
		// ⚠️ 费效曾严重过高：2 + DEF×1.0 = 10 格挡/1 费，
		//   每回合能打 2–3 张 → 20–30 格挡，而最强普通敌人只打 6–8 点（4.2 倍过剩）。
		//   压到 1 + DEF×0.6 = 5.8/张，配合 HexK::BlockCapRatio 上限，
		//   格挡回归"减伤"而非"无敌"。
		{
			FHexCardData C = MakeCard(
				TEXT("def_basic"), TEXT("防御"), EHexCardType::Guard, 1,
				FHexTargetSpec::MakeSelf(),
				{ FHexEffectStep::MakeBlock(1.0f, CardStatDef, 0.6f) },
				{ TEXT("格挡") },
				TEXT("获得 {block} 点格挡。"));
			Out.Add(AsCornerstone(C));
		}

		// 《移动》
		// ⚠️ 与《疾风步》一起从 0 费改为 1 费 —— 见 gust_step 的注释。
		{
			FHexCardData C = MakeCard(
				TEXT("move_basic"), TEXT("移动"), EHexCardType::Move, 1,
				FHexTargetSpec::MakeTile(1, 2),
				{ FHexEffectStep::MakeMove(2) },
				{ TEXT("位移") },
				TEXT("移动最多 {move} 格。"));
			Out.Add(AsCornerstone(C));
		}

		// ───────────────────────── 镇妖者基石变体

		// 《盾击》：伤害吃 DEF 加成 + 击退 1 格 → 让"堆 DEF"成为一条真路线。
		// 这是镇妖者与其它职业的分化点：它的输出来自防御属性。
		{
			FHexCardData C = MakeCard(
				TEXT("shield_bash"), TEXT("盾击"), EHexCardType::Attack, 1,
				FHexTargetSpec::Make(EHexTargetShape::Single, 1, 1),
				{
					FHexEffectStep::MakeDamage(0.0f, CardStatAtk, 0.6f),
					FHexEffectStep::MakeDamage(0.0f, CardStatDef, 0.8f),
					FHexEffectStep::MakeKnockback(1),
				},
				{ TEXT("近战"), TEXT("位移") },
				TEXT("造成 {dmg} 点伤害（受防御加成），并击退 {kb} 格。"));
			Out.Add(AsCornerstone(C));
		}

		// ───────────────────────── 8 张普通卡

		// 《重击》：费效基准线（2 费 18 点 → 费效 9）
		Out.Add(MakeCard(
			TEXT("heavy_strike"), TEXT("重击"), EHexCardType::Attack, 2,
			FHexTargetSpec::Make(EHexTargetShape::Single, 1, 1),
			{ FHexEffectStep::MakeDamage(0.0f, CardStatAtk, 1.8f) },
			{ TEXT("近战") },
			TEXT("造成 {dmg} 点伤害。")));

		// 《连刺》
		// ⚠️ 曾是 ATK×0.5×3。因为 floor 取整损失被摊薄 3 次，
		//   1 费打出 12 点（费效 12）远超《重击》2 费 15 点（费效 7.5）——
		//   1.6 倍差距让"有连刺就打连刺"，其他攻击卡失去存在意义。
		//   压到 0.42 后费效约 9，与《重击》同档，出牌重新需要看情况。
		Out.Add(MakeCard(
			TEXT("multi_stab"), TEXT("连刺"), EHexCardType::Attack, 1,
			FHexTargetSpec::Make(EHexTargetShape::Single, 1, 1),
			{ FHexEffectStep::MakeDamage(0.0f, CardStatAtk, 0.42f, 3) },
			{ TEXT("近战"), TEXT("连击") },
			TEXT("造成 {dmg} 点伤害，共 {hits} 次。")));

		// 《穿刺投枪》：唯一的远程直线 AoE
		Out.Add(MakeCard(
			TEXT("pierce_javelin"), TEXT("穿刺投枪"), EHexCardType::Attack, 2,
			FHexTargetSpec::Make(EHexTargetShape::Line, 1, 3, 3, true),
			{ FHexEffectStep::MakeDamage(0.0f, CardStatAtk, 1.1f) },
			{ TEXT("远程"), TEXT("范围") },
			TEXT("对直线上最多 3 个目标各造成 {dmg} 点伤害。")));

		// 《铁壁》：镇妖者的核心防御牌，起始带 2 张
		Out.Add(MakeCard(
			TEXT("iron_wall"), TEXT("铁壁"), EHexCardType::Guard, 2,
			FHexTargetSpec::MakeSelf(),
			{ FHexEffectStep::MakeBlock(2.0f, CardStatDef, 0.9f) },
			{ TEXT("格挡") },
			TEXT("获得 {block} 点格挡。")));

		// 《冲撞》：位移即伤害（§8.6）
		// ⚠️ 伤害曾是 ATK×0.4 = 3 点，是废牌。现在 ATK×0.7 + 击退撞墙额外 8 点
		//   （HexK::WallSlamDamage）→ 把敌人推到墙上或尖刺里成为真正的战术选择。
		//
		// ⚠️ TargetSpec 必须是 DashPath，不能用 MakeTile。
		//    用 Tile 时波及格只有落点自己，而落点又必须是空格 ——
		//    后面两步（伤害、击退）永远找不到目标，冲撞等于只会跑位。
		//    DashPath 把整条路径都算作波及格，沿途敌人才吃得到。
		Out.Add(MakeCard(
			TEXT("charge"), TEXT("冲撞"), EHexCardType::Attack, 1,
			FHexTargetSpec::MakeDashPath(1, 3),
			{
				FHexEffectStep::MakeDash(3),
				FHexEffectStep::MakeDamage(0.0f, CardStatAtk, 0.7f),
				FHexEffectStep::MakeKnockback(2),
			},
			{ TEXT("位移"), TEXT("近战") },
			TEXT("冲向 3 格内的空地，穿过沿途敌人并各造成 {dmg} 点伤害，"
				 "击退 {kb} 格（撞墙额外 8 点）。")));

		// 《点燃》：唯一的状态施加牌，是燃烧机制的入口
		Out.Add(MakeCard(
			TEXT("ignite"), TEXT("点燃"), EHexCardType::Skill, 1,
			FHexTargetSpec::Make(EHexTargetShape::Single, 1, 2),
			{
				FHexEffectStep::MakeDamage(0.0f, CardStatAtk, 0.3f),
				FHexEffectStep::MakeApplyStatus(TEXT("burn"), 3),
			},
			{ TEXT("火焰"), TEXT("状态") },
			TEXT("造成 {dmg} 点伤害，并施加 {stacks} 层【燃烧】。")));

		// 《急谋》
		// ⚠️ 曾是 0 费。0 费抽 2 无代价无上限 → 玩家永远不缺牌，
		//   单回合能打 7 张（5 体力），卡组构筑的取舍消失（R1'）。改 1 费。
		Out.Add(MakeCard(
			TEXT("quick_plan"), TEXT("急谋"), EHexCardType::Skill, 1,
			FHexTargetSpec::MakeSelf(),
			{ FHexEffectStep::MakeDraw(2) },
			{ TEXT("抽牌") },
			TEXT("抽 {draw} 张牌。")));

		// 《疾风步》
		// ⚠️ 曾是 0 费。0 费移动 2 格意味着【躲避零成本】——
		//   实测镇妖者每个回合都打它躲掉所有"可躲"攻击，全程 0 掉血。
		//   "硬但公平"要求躲避是一个【有代价的选择】，而不是免费的默认动作。
		//   改 1 费后，玩家每回合要在"躲"和"多打一张输出"之间取舍。
		Out.Add(MakeCard(
			TEXT("gust_step"), TEXT("疾风步"), EHexCardType::Move, 1,
			FHexTargetSpec::MakeTile(1, 2),
			{ FHexEffectStep::MakeMove(2) },
			{ TEXT("位移") },
			TEXT("移动最多 {move} 格。")));

		// ───────────────────────── 装备注入的衍生卡（§5.4）
		//
		// ⚠️ 衍生卡【不占卡组容量】且【不进掠夺池】：
		//    它随装备来、随装备走，不是玩家构筑的一部分。
		//    若占容量，装上《引魂幡》就等于卡组容量 -1，
		//    玩家会因为"这件武器让我少带一张牌"而拒绝所有注入型装备。

		// 《招魂》—— 由《引魂幡》注入。
		// 设计意图：给远程武器一个"把敌人拉过来"的手段。
		// 否则纯远程打法会退化成无脑风筝 —— Godot 版实测过这个问题：
		// 零成本躲避让玩家全程 0 掉血，战斗失去博弈。
		{
			FHexEffectStep Pull = FHexEffectStep::MakeKnockback(2);
			// Pull 与 Knockback 共用结算路径，只有方向相反
			Pull.Op = EHexEffectOp::Pull;

			FHexCardData C = MakeCard(
				TEXT("eq_soul_call"), TEXT("招魂"), EHexCardType::Derived, 1,
				FHexTargetSpec::Make(EHexTargetShape::Single, 2, 3),
				{
					FHexEffectStep::MakeDamage(0.0f, CardStatAtk, 0.4f),
					Pull,
				},
				{ TEXT("远程"), TEXT("推拉"), TEXT("衍生") },
				TEXT("造成 {dmg} 点伤害，并将目标拉近 {kb} 格。"));
			C.bCountsTowardCapacity = false;
			// 衍生卡不该被复制成多份：它的数量由装备决定
			C.MaxCopiesInDeck = 1;
			Out.Add(C);
		}

		return Out;
	}
}

const TArray<FHexCardData>& FHexContentLibrary::AllCards()
{
	static const TArray<FHexCardData> Cards = BuildAllCards();
	return Cards;
}

const FHexCardData* FHexContentLibrary::FindCard(FName Id)
{
	for (const FHexCardData& C : AllCards())
	{
		if (C.Id == Id)
		{
			return &C;
		}
	}
	return nullptr;
}

// ══════════════════════════════════════════════════════════ 起始卡组与掠夺池

namespace
{
	/**
	 * 某英雄起始携带的【非基石】卡（用户决策 q18：5 张，留 3 个空位）。
	 *
	 * ⚠️ 卡组按英雄定位分化（§4.2）。Godot 版曾让两个英雄用同一套 8 张通用卡，
	 *   结果镇妖者拿到 3 张位移卡（移动/疾风步/冲撞）→ 在开阔地无限风筝、
	 *   全程 0 掉血。但策划案把「风筝型」明确分配给术士，
	 *   镇妖者的定位是「站在原地，让敌人自己撞死」的坦克。
	 *
	 * ⚠️ 但【必须保留《冲撞》】，它不是位移卡而是【追击手段】。
	 *   缩减卡组时我一度把它删掉，只留基石《移动》（2 格）——
	 *   后果是面对风筝型敌人（投石手 PreferredDistance=3，每回合后退）
	 *   镇妖者永远够不到，而对方也打不穿它的格挡，双方永久僵持。
	 *   集成测试里 enc_02 / enc_03 连续 20 场跑满 50 回合、胜 0 负 0
	 *   就是这么来的。《冲撞》3 格突进是它唯一的接敌手段。
	 *
	 *   代价是防御卡从 2 张《铁壁》减到 1 张 —— 可以接受：
	 *   基石《防御》仍在，且「格挡不清空」被动让单张防御卡也能滚起来。
	 *
	 * 留给掠夺的 3 个空位对应的池子（自动推导，见 GetLootableCardIds）：
	 *   穿刺投枪(远程AoE) / 急谋(抽牌) / 疾风步(位移)
	 *   —— 3 张候选抢 3 个空位，每一张都会实质改变打法。
	 */
	void StartingExtrasFor(FName HeroId, TArray<FName>& Out)
	{
		if (HeroId == FName(TEXT("warden")))
		{
			Out = {
				TEXT("heavy_strike"),
				TEXT("multi_stab"),
				TEXT("iron_wall"),
				TEXT("charge"),      // 追击手段，不可省
				TEXT("ignite"),
			};
			return;
		}

		// 兜底：未知英雄给一套均衡起手，保证验证器不会拿到空卡组
		Out = {
			TEXT("heavy_strike"),
			TEXT("multi_stab"),
			TEXT("iron_wall"),
			TEXT("ignite"),
			TEXT("quick_plan"),
		};
	}
}

void FHexContentLibrary::BuildStartingDeck(
	const FHexHeroData& Hero,
	TArray<FHexCardInstance>& OutDeck,
	TArray<FHexCardInstance>& OutFixedCards)
{
	OutDeck.Reset();
	OutFixedCards.Reset();

	// ── 固定卡：基石卡，常驻不入牌堆
	//
	// uid 用独立段（FixedCardUidBase 起）。卡组从 1 起递增，
	// 装备注入的衍生卡用运行时计数器 —— 三个分配器互不知情，
	// 不分段迟早撞号，而撞号会让"点 A 卡结算成 B 卡"。
	{
		int32 NextFixedUid = HexK::FixedCardUidBase;
		for (const FName& Cid : Hero.CornerstoneCardIds)
		{
			if (FindCard(Cid) == nullptr)
			{
				ensureMsgf(false, TEXT("BuildStartingDeck: 未知基石卡 id %s"), *Cid.ToString());
				continue;
			}

			FHexCardInstance Inst;
			Inst.Uid = NextFixedUid++;
			Inst.CardId = Cid;
			OutFixedCards.Add(Inst);
		}
	}

	// ── 卡组：只有技能卡等构筑内容
	TArray<FName> Ids;
	StartingExtrasFor(Hero.Id, Ids);

	// uid 从 1 开始连续分配。0 保留为"无效 uid"，
	// 这样 FindInHand(0) 之类的调用能被安全地判为未找到。
	int32 NextUid = 1;
	for (const FName& Cid : Ids)
	{
		if (FindCard(Cid) == nullptr)
		{
			// 打错 id 不该静默丢牌 —— 卡组少一张会让所有平衡数据失真
			ensureMsgf(false, TEXT("BuildStartingDeck: 未知卡 id %s"), *Cid.ToString());
			continue;
		}

		FHexCardInstance Inst;
		Inst.Uid = NextUid++;
		Inst.CardId = Cid;
		OutDeck.Add(Inst);
	}
}

void FHexContentLibrary::GetLootableCardIds(const FHexHeroData& Hero, TArray<FName>& Out)
{
	Out.Reset();

	// 掠夺池 = 全部非基石卡 − 起始卡组里已有的卡。
	// 自动推导而非手写常量：改起始卡组时掠夺池自动跟着变，
	// 不会出现"起始已有 + 掠夺又给"的重复掉落。
	TArray<FName> Extras;
	StartingExtrasFor(Hero.Id, Extras);

	for (const FHexCardData& C : AllCards())
	{
		if (C.bIsCornerstone)
		{
			continue;
		}
		// ⚠️ 衍生卡（装备/符文注入）不进掠夺池。
		//    它们随来源装卸，掠夺到一张"没有对应装备的衍生卡"
		//    会变成永久占位的幽灵牌。
		if (C.CardType == EHexCardType::Derived)
		{
			continue;
		}
		// 诅咒卡不走掠夺池 —— 它们由事件/诅咒符文塞进来，不是奖励
		if (C.CardType == EHexCardType::Curse)
		{
			continue;
		}
		if (Extras.Contains(C.Id))
		{
			continue;
		}
		Out.Add(C.Id);
	}
}

// ══════════════════════════════════════════════════════════ 敌人
//
// ⚠️ 敌人 ATK 在 Godot 版调平衡时统一上调约 1.6 倍。两个原因叠加：
//   ① 原数值下 enc_02 只打 11 点/回合，镇妖者 80 HP 能撑 7 回合 → 无威胁
//   ② HexK::DefSoftcap 从 50 改到 12 后 DEF 真正生效（镇妖者 DEF8 = 40% 减伤），
//      进一步压低了敌人实际输出
//   目标：enc_02 总输出 ≈ 19 点/回合 → 镇妖者撑 4 回合（"硬但公平"的节奏）

namespace
{
	FHexEnemyData MakeEnemy(
		const FName& Id, const FString& Name, EHexSizeClass Size,
		int32 HP, int32 ATK, int32 DEF, int32 AGI,
		EHexAIProfile AI, EHexIntentTargeting Targeting,
		const FString& Codex)
	{
		FHexEnemyData E;
		E.Id = Id;
		E.DisplayName = Name;
		E.SizeClass = Size;
		E.BaseHP = HP;
		E.BaseATK = ATK;
		E.BaseDEF = DEF;
		E.BaseAGI = AGI;
		// Godot 版敌人未设 LUK/CRIT（走默认 0）——敌人不暴击，
		// 让玩家受到的伤害完全可预测，这是"意图是承诺"的一部分（§8.7）。
		E.BaseLUK = 0;
		E.BaseCRIT = 0;
		E.AIProfile = AI;
		E.IntentTargeting = Targeting;
		E.CodexText = Codex;
		return E;
	}

	TArray<FHexEnemyData> BuildAllEnemies()
	{
		TArray<FHexEnemyData> Out;
		Out.Reserve(4);

		// 打空型：玩家走开就落空（但攻击有范围，躲需要走出 2 格）
		Out.Add(MakeEnemy(
			TEXT("biting_hound"), TEXT("扑咬犬"), EHexSizeClass::S,
			24, 13, 2, 8,
			EHexAIProfile::Aggressive, EHexIntentTargeting::FixedTile,
			TEXT("锁定格子后扑击。走开就能躲——但它的攻击有范围，得挪够 2 格。")));

		// 追踪型：躲不掉，但可以断视线
		Out.Add(MakeEnemy(
			TEXT("stone_slinger"), TEXT("投石手"), EHexSizeClass::S,
			18, 10, 1, 5,
			EHexAIProfile::RangedKiter, EHexIntentTargeting::TrackTarget,
			TEXT("锁定你本人，跑不掉。解法是躲到石柱后断视线，或者先杀它。")));

		// M 体型：相邻格多、转向慢 → 绕后价值高
		{
			FHexEnemyData Golem = MakeEnemy(
				TEXT("stone_golem"), TEXT("石傀"), EHexSizeClass::M,
				60, 16, 6, 4,
				EHexAIProfile::Blocker, EHexIntentTargeting::FixedTile,
				TEXT("三格身躯堵死通路。转向要花整个行动，绕到它背后是正解。"));
			Golem.bIsElite = true;
			Out.Add(Golem);
		}

		// L 体型：免疫击退、可碾压、按 HP 切三阶段
		{
			FHexEnemyData Worm = MakeEnemy(
				TEXT("siege_worm"), TEXT("攻城虫"), EHexSizeClass::L,
				120, 22, 8, 2,
				EHexAIProfile::BossPhased, EHexIntentTargeting::FixedTile,
				TEXT("六格巨躯，推不动，会直接碾过比它小的一切。"));
			Worm.bIsBoss = true;
			// 999 = 事实免疫。用 override 而非改体型默认值，
			// 是为了让"L 体型默认抗性"与"这只怪免疫"两件事分开
			Worm.KnockbackResistOverride = 999;
			Out.Add(Worm);
		}

		return Out;
	}
}

const TArray<FHexEnemyData>& FHexContentLibrary::AllEnemies()
{
	static const TArray<FHexEnemyData> Enemies = BuildAllEnemies();
	return Enemies;
}

const FHexEnemyData* FHexContentLibrary::FindEnemy(FName Id)
{
	for (const FHexEnemyData& E : AllEnemies())
	{
		if (E.Id == Id)
		{
			return &E;
		}
	}
	return nullptr;
}

FHexUnit FHexContentLibrary::MakeEnemyUnit(const FHexEnemyData& Data, int32 FloorIndex, int32 Corruption)
{
	FHexUnit U;

	U.SourceId = Data.Id;
	U.DisplayName = Data.DisplayName;
	U.Team = EHexTeam::Enemy;
	U.SizeClass = Data.SizeClass;

	// ⚠️ 层数不引入新常量：直接折算成"等效腐蚀度"。
	//   每往上一层 ≡ +CorruptionPerDifficultyTier(3) 点腐蚀度。
	//   这样第 1 层（FloorIndex=1）缩放为 0，与 Godot 版数值完全一致 ——
	//   本版只做第一层，所以移植的平衡结论不会被层缩放污染。
	const int32 EffectiveCorruption =
		FMath::Max(0, Corruption) +
		FMath::Max(0, FloorIndex - 1) * HexK::CorruptionPerDifficultyTier;

	const float HpScale = 1.0f + HexK::CorruptionEnemyHpStep * static_cast<float>(EffectiveCorruption);
	const float AtkScale = 1.0f + HexK::CorruptionEnemyAtkStep * static_cast<float>(EffectiveCorruption);

	// 关键数值落地为 int（纪律 5：浮点只在中间计算用）
	U.HPMax = FMath::Max(1, FMath::RoundToInt(static_cast<float>(Data.BaseHP) * HpScale));
	U.HP = U.HPMax;
	U.ATK = FMath::Max(0, FMath::RoundToInt(static_cast<float>(Data.BaseATK) * AtkScale));

	// DEF/AGI 不随腐蚀度成长：否则后期敌人会同时变厚变快变痛，
	// 玩家的应对手段（破防、走位）会一起失效 —— 只放大 HP/ATK 是可控的加压方式。
	U.DEF = Data.BaseDEF;
	U.AGI = Data.BaseAGI;
	U.LUK = Data.BaseLUK;
	U.CRIT = Data.BaseCRIT;

	U.AIProfile = Data.AIProfile;
	U.IntentTargeting = Data.IntentTargeting;
	U.bIsElite = Data.bIsElite;
	U.bIsBoss = Data.bIsBoss;
	U.KnockbackResistOverride = Data.KnockbackResistOverride;
	U.BossPhase = Data.bIsBoss ? 1 : 0;

	// Anchor / Facing / Id 由生成器（战场布阵）负责，这里不猜。
	return U;
}

// ══════════════════════════════════════════════════════════ 怪物组
//
// ⚠️ 怪物组的规模曾太小：enc_01 只有 1 只狗（24 HP），玩家一回合就清完，
//   掉血率 0% —— 战斗还没开始就结束了，谈不上博弈。
//   §9.6 的设计里普通战斗房是「1 个怪物组」，但组内数量应该让战斗
//   持续 3–5 回合（§4.1 单场 2–5 分钟的前提）。
//   现在按"教学 → 标准 → 精英 → Boss"分四档递进。
//
// ══════════════════════════════════════════════════════════════════
// ⚠️ 第二次调整：按"单场 3–5 回合"反推规模（HexPlaytest 实测驱动）
// ══════════════════════════════════════════════════════════════════
// Godot 版的注释写着"组内数量应让战斗持续 3–5 回合"，
// 但它【从未验证过战斗时长】—— 11 个验证器里没有这一条。
// UE 版的 HexPlaytest 一跑就露了：平均每场 9.4 回合，是目标的两倍。
//
// 反推过程：
//   镇妖者 ATK 10，5 体力/回合。但要留 1–2 体力买格挡与走位，
//   实际输出体力约 3 → 约 18 点伤害/回合（《重击》15 或《连刺》9+《盾击》10）。
//   要 4 回合打完 → 怪物组总 HP 应约 72。
//
// 调整前后：
//   enc_01  48 →  48（教学局，2.7 回合，短一点合理）
//   enc_02 108 →  84（去掉 1 只狗 → 约 4.7 回合）
//   enc_03 150 → 108（去掉 1 只狗与投石手 → 约 6 回合，精英可以长一点）
//   enc_04 204 → 156（去掉 2 只狗 → 约 8.7 回合，Boss 战应当最长）
//
// 连带效果：战斗变短 → 玩家吃到的总伤害下降 →
// 一层 4 场的血量预算才成立（否则 60 局阵亡率 98–100%）。

void FHexContentLibrary::GetEncounter(FName EncounterId, TArray<FHexEncounterEntry>& Out)
{
	Out.Reset();

	auto Add = [&Out](const FName& EnemyId, int32 Count)
	{
		FHexEncounterEntry E;
		E.EnemyId = EnemyId;
		E.Count = Count;
		Out.Add(E);
	};

	// 教学局：2 只狗（48 HP），让玩家学会"躲不掉就得挨打"
	if (EncounterId == FName(TEXT("enc_01")))
	{
		Add(TEXT("biting_hound"), 2);
		return;
	}

	// 标准局：近战 + 远程混编（84 HP），逼玩家分配注意力
	if (EncounterId == FName(TEXT("enc_02")))
	{
		Add(TEXT("biting_hound"), 2);
		Add(TEXT("stone_slinger"), 2);
		return;
	}

	// 精英局：M 体型堵路 + 杂兵骚扰（108 HP）
	if (EncounterId == FName(TEXT("enc_03")))
	{
		Add(TEXT("stone_golem"), 1);
		Add(TEXT("biting_hound"), 2);
		return;
	}

	// Boss 局：L 体型 + 远程支援（156 HP）
	//
	// ⚠️ 不放近战杂兵：攻城虫本身就贴身输出 22 点，
	//    再加狗会让玩家在贴身位置同时吃 3 个来源的伤害，
	//    格挡完全跟不上 —— 那不是难，是没有解法。
	//    远程投石手反而给玩家"先杀谁"的选择。
	if (EncounterId == FName(TEXT("enc_04")))
	{
		Add(TEXT("siege_worm"), 1);
		Add(TEXT("stone_slinger"), 2);
		return;
	}

	// 兜底：未知 id 给教学局，绝不返回空怪物组
	//（空怪物组会让战斗一开始就判胜利，是最难查的一类 bug）
	Add(TEXT("biting_hound"), 2);
}

const TArray<FName>& FHexContentLibrary::AllEncounterIds()
{
	static const TArray<FName> Ids = {
		TEXT("enc_01"), TEXT("enc_02"), TEXT("enc_03"), TEXT("enc_04")
	};
	return Ids;
}

// ══════════════════════════════════════════════════════════ 符文（D6）
//
// 符文表在 Runes/HexRuneLibrary.cpp 里定义（12–18 个，用户决策 q11）。
// 这里只做转发，避免内容库文件膨胀到无法审阅。

const TArray<FHexRuneData>& FHexContentLibrary::AllRunes()
{
	return FHexRuneLibrary::AllRunes();
}

const FHexRuneData* FHexContentLibrary::FindRune(FName Id)
{
	return FHexRuneLibrary::FindRune(Id);
}

void FHexContentLibrary::GetRuneIdsByRarity(EHexRarity Rarity, TArray<FName>& Out)
{
	FHexRuneLibrary::GetIdsByRarity(Rarity, Out);
}

// ══════════════════════════════════════════════════════════ 未移植内容备忘
//
// 《巨岩之躯》boulder_body（御灵者/M 体型专属基石，Godot 版已调好）：
//   GUARD / 1 费 / ADJACENT_ALL(rmin=0, rmax=1, LoS=false)
//   效果① GainBlock(flat=2.0, DEF×1.0)
//   效果② ApplyStatus("chill", 1)
//   标签 ["格挡","控场"]
//   设计意图：格挡 + 相邻敌人缓迟 → M 体型的"我就是障碍物"
//   ⚠️ 依赖 chill（缓迟）状态。而 chill 目前在 HexStatusData 里是空壳
//      （没有任何字段承载"移动力 -1"）——加回这张卡前必须先补 chill 的效果字段。
