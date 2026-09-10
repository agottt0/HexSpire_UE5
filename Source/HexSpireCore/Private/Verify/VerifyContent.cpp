// Copyright Hex Spire. All Rights Reserved.
//
// 内容库验证 —— 把"内容纪律"变成机械断言
//
// 为什么这个套件优先级最高：
//   内容是唯一会持续增长的部分（卡池目标 190–230 张、符文 90–120 个）。
//   §7.5 系数化、§6.4 类别占比、§7.6 基石不占容量、§7.8 标签零加成
//   这些纪律都是"人写着写着就会忘"的类型 —— 一旦写歪，
//   后果不是崩溃而是【平衡静默失真】，这是最难查的一类问题。
//
// 本套件的断言全部零依赖（不需要战场、不需要 RNG），
// 所以可以在每次改内容后无条件跑一遍。

#include "Verify/HexVerify.h"
#include "Content/HexContentLibrary.h"
#include "Runes/HexRuneLibrary.h"
#include "Battle/HexUnit.h"
#include "Battle/HexStatusData.h"
#include "Deck/HexPileManager.h"
#include "Core/HexSpireConstants.h"

namespace
{
	// ─────────────────────────────────────────── 卡牌

	void CheckCards(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("卡牌"));

		const TArray<FHexCardData>& Cards = FHexContentLibrary::AllCards();
		Ctx.Check(TEXT("卡池非空"), Cards.Num() > 0,
			FString::Printf(TEXT("卡池数量=%d"), Cards.Num()));

		// id 唯一性：重复 id 会让 FindCard 永远返回第一个，
		// 后写的那张卡静默失效（不报错、不崩溃，只是永远打不出来）
		{
			TSet<FName> Seen;
			bool bUnique = true;
			FString Dup;
			for (const FHexCardData& C : Cards)
			{
				if (Seen.Contains(C.Id))
				{
					bUnique = false;
					Dup = C.Id.ToString();
					break;
				}
				Seen.Add(C.Id);
			}
			Ctx.Check(TEXT("卡牌 id 唯一"), bUnique,
				FString::Printf(TEXT("重复 id=%s"), *Dup));
		}

		for (const FHexCardData& C : Cards)
		{
			const FString N = C.Id.ToString();

			Ctx.Check(FString::Printf(TEXT("[%s] 有显示名"), *N),
				!C.DisplayName.IsEmpty(), TEXT("DisplayName 为空"));

			Ctx.Check(FString::Printf(TEXT("[%s] 有效果步骤"), *N),
				C.Effects.Num() > 0, TEXT("Effects 为空 —— 这张卡打出来什么都不会发生"));

			Ctx.Check(FString::Printf(TEXT("[%s] 费用非负"), *N),
				C.EnergyCost >= 0, FString::Printf(TEXT("EnergyCost=%d"), C.EnergyCost));

			// ⚠️ §7.5【不可妥协】：伤害与格挡必须系数化。
			//    硬编码 Damage=12 会让 ATK 从 10 涨到 200 时这张卡原地不动，
			//    整个"强数值养成"与"卡牌策略"的共存基础崩掉。
			for (const FHexEffectStep& S : C.Effects)
			{
				if (S.Op == EHexEffectOp::DealDamage || S.Op == EHexEffectOp::GainBlock)
				{
					Ctx.Check(
						FString::Printf(TEXT("[%s] §7.5 系数化(%s)"), *N,
							S.Op == EHexEffectOp::DealDamage ? TEXT("伤害") : TEXT("格挡")),
						S.StatRatio > 0.0f,
						TEXT("StatRatio=0 → 这是硬编码数值，违反 §7.5"));
				}

				// 离散量的合理性：位移 0 格的位移效果是笔误
				if (S.Op == EHexEffectOp::MoveSelf || S.Op == EHexEffectOp::Dash ||
					S.Op == EHexEffectOp::Knockback || S.Op == EHexEffectOp::Pull)
				{
					Ctx.Check(FString::Printf(TEXT("[%s] 位移格数 > 0"), *N),
						S.Distance > 0, FString::Printf(TEXT("Distance=%d"), S.Distance));
				}

				// 施加状态必须引用一个真实存在的状态
				if (S.Op == EHexEffectOp::ApplyStatus)
				{
					Ctx.Check(
						FString::Printf(TEXT("[%s] 状态 id 存在(%s)"), *N, *S.StatusId.ToString()),
						FHexStatusLibrary::Exists(S.StatusId),
						TEXT("引用了未定义的状态 —— 效果会静默变成 no-op"));

					Ctx.Check(FString::Printf(TEXT("[%s] 状态层数 > 0"), *N),
						S.StatusStacks > 0,
						FString::Printf(TEXT("StatusStacks=%d"), S.StatusStacks));
				}
			}

			// §7.6：基石卡不占卡组容量
			if (C.bIsCornerstone)
			{
				Ctx.Check(FString::Printf(TEXT("[%s] 基石不占容量"), *N),
					!C.bCountsTowardCapacity,
					TEXT("基石卡占了容量 → 起始卡组会白白吃掉 3 格空间（违反 §7.6）"));
			}

			// 描述模板：占位符必须能被填充。
			// 写了 {dmg} 却没有伤害步骤 → 卡面会显示 "造成 0 点伤害"
			if (C.DescriptionTemplate.Contains(TEXT("{dmg}")))
			{
				bool bHasDamage = false;
				for (const FHexEffectStep& S : C.Effects)
				{
					if (S.Op == EHexEffectOp::DealDamage) { bHasDamage = true; break; }
				}
				Ctx.Check(FString::Printf(TEXT("[%s] {dmg} 有对应步骤"), *N),
					bHasDamage, TEXT("描述里有 {dmg} 但没有伤害效果"));
			}
			if (C.DescriptionTemplate.Contains(TEXT("{block}")))
			{
				bool bHasBlock = false;
				for (const FHexEffectStep& S : C.Effects)
				{
					if (S.Op == EHexEffectOp::GainBlock) { bHasBlock = true; break; }
				}
				Ctx.Check(FString::Printf(TEXT("[%s] {block} 有对应步骤"), *N),
					bHasBlock, TEXT("描述里有 {block} 但没有格挡效果"));
			}
		}
	}

	// ─────────────────────────────────────────── 英雄与起始卡组

	void CheckHeroesAndDecks(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("英雄与起始卡组"));

		const TArray<FHexHeroData>& Heroes = FHexContentLibrary::AllHeroes();
		Ctx.Check(TEXT("英雄表非空"), Heroes.Num() > 0, TEXT(""));

		// 用户决策 q7：第一版只做镇妖者
		Ctx.Check(TEXT("镇妖者存在"),
			FHexContentLibrary::FindHero(TEXT("warden")) != nullptr,
			TEXT("找不到 id=warden 的英雄"));

		for (const FHexHeroData& H : Heroes)
		{
			const FString N = H.Id.ToString();

			Ctx.CheckEqual(FString::Printf(TEXT("[%s] 基石卡 3 张"), *N),
				H.CornerstoneCardIds.Num(), 3);

			for (const FName& Cid : H.CornerstoneCardIds)
			{
				const FHexCardData* C = FHexContentLibrary::FindCard(Cid);
				Ctx.Check(FString::Printf(TEXT("[%s] 基石 %s 存在于卡池"), *N, *Cid.ToString()),
					C != nullptr, TEXT("基石 id 打错 → 起始卡组会少一张，平衡数据全部失真"));

				if (C)
				{
					Ctx.Check(FString::Printf(TEXT("[%s] 基石 %s 已标记 bIsCornerstone"), *N, *Cid.ToString()),
						C->bIsCornerstone,
						TEXT("英雄把它当基石，但卡本身没标记 → 它会占用卡组容量"));
				}
			}

			Ctx.Check(FString::Printf(TEXT("[%s] 六属性为正"), *N),
				H.BaseHP > 0 && H.BaseATK > 0 && H.BaseDEF >= 0 &&
				H.BaseAGI >= 0 && H.BaseLUK >= 0 && H.BaseCRIT >= 0,
				TEXT("属性出现非法值"));

			Ctx.Check(FString::Printf(TEXT("[%s] 体力与抽牌数为正"), *N),
				H.EnergyMax > 0 && H.CardsDrawnPerTurn > 0, TEXT(""));

			// ── 起始卡组 + 固定卡
			TArray<FHexCardInstance> Deck;
			TArray<FHexCardInstance> Fixed;
			FHexContentLibrary::BuildStartingDeck(H, Deck, Fixed);

			Ctx.Check(FString::Printf(TEXT("[%s] 起始卡组非空"), *N),
				Deck.Num() > 0, TEXT(""));

			// uid 唯一：重复 uid 会让"打出手牌第 2 张攻击"定位到错误的卡
			//
			// ⚠️ 必须把【卡组与固定卡放在一起】查重。
			//    两者由不同的分配器发号（卡组从 1、固定卡从 10000），
			//    各自内部唯一不代表合起来唯一，而 PlayCard 只收一个 uid，
			//    撞号就会"点 A 卡结算成 B 卡"。
			{
				TSet<int32> Uids;
				bool bUnique = true;
				for (const FHexCardInstance& I : Deck)
				{
					if (I.Uid == 0 || Uids.Contains(I.Uid)) { bUnique = false; break; }
					Uids.Add(I.Uid);
				}
				for (const FHexCardInstance& I : Fixed)
				{
					if (I.Uid == 0 || Uids.Contains(I.Uid)) { bUnique = false; break; }
					Uids.Add(I.Uid);
				}
				Ctx.Check(FString::Printf(TEXT("[%s] 卡实例 uid 全局唯一且非 0（含固定卡）"), *N),
					bUnique, TEXT("uid 重复或为 0 —— 0 被保留为无效 uid"));
			}

			// 容量核算（用户决策 q18 的核心：必须留出空位）
			int32 CountsTowardCap = 0;
			int32 CornerstoneInDeck = 0;
			for (const FHexCardInstance& I : Deck)
			{
				const FHexCardData* C = FHexContentLibrary::FindCard(I.CardId);
				if (!C) { continue; }
				if (C->bIsCornerstone) { ++CornerstoneInDeck; }
				if (C->bCountsTowardCapacity) { ++CountsTowardCap; }
			}

			Ctx.Check(FString::Printf(TEXT("[%s] 占容量卡数不超上限"), *N),
				CountsTowardCap <= HexK::InitialDeckCapacity,
				FString::Printf(TEXT("占容量=%d 上限=%d"), CountsTowardCap, HexK::InitialDeckCapacity));

			// ⚠️ 这条是 q18 决策的机械化：卡组必须留 >= 3 个空位，
			//    否则 §3.2 的核心决策「收哪张卡 / 挤掉哪张卡」在第一层触发不了，
			//    掠夺阶段变成空转。这是内容量问题，不是数值问题。
			const int32 FreeSlots = HexK::InitialDeckCapacity - CountsTowardCap;
			Ctx.Check(FString::Printf(TEXT("[%s] 至少留 3 个空位(q18)"), *N),
				FreeSlots >= 3,
				FString::Printf(TEXT("空位=%d（占容量=%d/%d）—— 掠夺会空转"),
					FreeSlots, CountsTowardCap, HexK::InitialDeckCapacity));

			// ── 固定卡契约
			//
			// ⚠️ 基石卡必须【全部】离开抽牌堆。留一张在卡组里，
			//    玩家就会在手牌里偶尔摸到重复的《移动》，
			//    而左侧固定卡区同时也有一张 —— 同一张卡两个入口，
			//    是最容易让玩家困惑的那类 bug。
			Ctx.CheckEqual(FString::Printf(TEXT("[%s] 起始卡组【不含】基石卡"), *N),
				CornerstoneInDeck, 0);

			Ctx.CheckEqual(FString::Printf(TEXT("[%s] 固定卡 3 张"), *N),
				Fixed.Num(), 3);

			{
				bool bAllCornerstone = Fixed.Num() > 0;
				bool bAllInFixedRange = true;
				for (const FHexCardInstance& I : Fixed)
				{
					const FHexCardData* C = FHexContentLibrary::FindCard(I.CardId);
					if (!C || !C->bIsCornerstone) { bAllCornerstone = false; }
					if (I.Uid < HexK::FixedCardUidBase) { bAllInFixedRange = false; }
				}
				Ctx.Check(FString::Printf(TEXT("[%s] 固定卡全部是基石卡"), *N),
					bAllCornerstone, TEXT(""));
				Ctx.Check(FString::Printf(TEXT("[%s] 固定卡 uid 落在专属号段"), *N),
					bAllInFixedRange,
					TEXT("固定卡 uid 必须 >= FixedCardUidBase，否则可能与卡组撞号"));
			}

			// ── 掠夺池
			TArray<FName> Loot;
			FHexContentLibrary::GetLootableCardIds(H, Loot);

			Ctx.Check(FString::Printf(TEXT("[%s] 掠夺池非空"), *N),
				Loot.Num() > 0, TEXT("掠夺池为空 → 战斗后拾取界面什么都给不出来"));

			// 掠夺池必须能填满空位，否则玩家看着空位却没东西可捡
			Ctx.Check(FString::Printf(TEXT("[%s] 掠夺池够填空位"), *N),
				Loot.Num() >= FreeSlots,
				FString::Printf(TEXT("掠夺池=%d 空位=%d"), Loot.Num(), FreeSlots));

			// 掠夺池不得与起始卡组重叠（否则"掉落"给的是已有的牌，感受不到成长）
			{
				TSet<FName> InDeck;
				for (const FHexCardInstance& I : Deck) { InDeck.Add(I.CardId); }

				bool bNoOverlap = true;
				FString Bad;
				for (const FName& L : Loot)
				{
					if (InDeck.Contains(L)) { bNoOverlap = false; Bad = L.ToString(); break; }
				}
				Ctx.Check(FString::Printf(TEXT("[%s] 掠夺池与起始卡组无交集"), *N),
					bNoOverlap, FString::Printf(TEXT("重复卡=%s"), *Bad));
			}

			// 掠夺池不得含基石卡（基石是英雄自带的，不该作为掉落）
			{
				bool bNoCornerstone = true;
				FString Bad;
				for (const FName& L : Loot)
				{
					const FHexCardData* C = FHexContentLibrary::FindCard(L);
					if (C && C->bIsCornerstone) { bNoCornerstone = false; Bad = L.ToString(); break; }
				}
				Ctx.Check(FString::Printf(TEXT("[%s] 掠夺池不含基石"), *N),
					bNoCornerstone, FString::Printf(TEXT("基石进了掠夺池=%s"), *Bad));
			}

			// 被动天赋若引用规则，必须是有效枚举值
			for (const FHexRuleOverride& O : H.PassiveRules)
			{
				Ctx.Check(FString::Printf(TEXT("[%s] 被动规则枚举有效"), *N),
					static_cast<uint8>(O.Rule) < static_cast<uint8>(EHexGameRule::Count),
					TEXT("Rule 越界"));
			}
		}
	}

	// ─────────────────────────────────────────── 敌人与怪物组

	void CheckEnemiesAndEncounters(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("敌人与怪物组"));

		const TArray<FHexEnemyData>& Enemies = FHexContentLibrary::AllEnemies();
		Ctx.Check(TEXT("敌人表非空"), Enemies.Num() > 0, TEXT(""));

		{
			TSet<FName> Seen;
			bool bUnique = true;
			for (const FHexEnemyData& E : Enemies)
			{
				if (Seen.Contains(E.Id)) { bUnique = false; break; }
				Seen.Add(E.Id);
			}
			Ctx.Check(TEXT("敌人 id 唯一"), bUnique, TEXT(""));
		}

		// D8 覆盖：三种体型都要有敌人，否则体型系统无法被实际验证。
		// 用户决策 q7 只做镇妖者(S)，所以 M/L 的验证【完全依赖敌人侧】。
		{
			bool bHasS = false, bHasM = false, bHasL = false;
			for (const FHexEnemyData& E : Enemies)
			{
				if (E.SizeClass == EHexSizeClass::S) { bHasS = true; }
				if (E.SizeClass == EHexSizeClass::M) { bHasM = true; }
				if (E.SizeClass == EHexSizeClass::L) { bHasL = true; }
			}
			Ctx.Check(TEXT("敌人覆盖 S/M/L 三种体型(D8)"), bHasS && bHasM && bHasL,
				FString::Printf(TEXT("S=%d M=%d L=%d"), bHasS, bHasM, bHasL));
		}

		// 意图可躲 vs 追踪：两种都必须存在。
		// §13.2 要求玩家能读出这个区分，只有一种的话这条 UX 需求无从体现。
		{
			bool bHasFixed = false, bHasTrack = false;
			for (const FHexEnemyData& E : Enemies)
			{
				if (E.IntentTargeting == EHexIntentTargeting::FixedTile) { bHasFixed = true; }
				if (E.IntentTargeting == EHexIntentTargeting::TrackTarget) { bHasTrack = true; }
			}
			Ctx.Check(TEXT("同时存在可躲与追踪型敌人"), bHasFixed && bHasTrack, TEXT(""));
		}

		for (const FHexEnemyData& E : Enemies)
		{
			const FString N = E.Id.ToString();
			Ctx.Check(FString::Printf(TEXT("[%s] HP/ATK 为正"), *N),
				E.BaseHP > 0 && E.BaseATK > 0, TEXT(""));

			// 敌人不暴击：让玩家受到的伤害完全可预测，
			// 这是"意图是承诺"（§8.7）的一部分 —— 意图上写 13 点就必须是 13 点。
			Ctx.CheckEqual(FString::Printf(TEXT("[%s] 敌人不暴击(CRIT=0)"), *N), E.BaseCRIT, 0);

			Ctx.Check(FString::Printf(TEXT("[%s] 有图鉴文本"), *N),
				!E.CodexText.IsEmpty(), TEXT("空图鉴 → 玩家无从学习它的行为模式"));
		}

		// ── 怪物组
		const TArray<FName>& EncIds = FHexContentLibrary::AllEncounterIds();
		Ctx.Check(TEXT("怪物组表非空"), EncIds.Num() > 0, TEXT(""));

		for (const FName& Eid : EncIds)
		{
			TArray<FHexEncounterEntry> Entries;
			FHexContentLibrary::GetEncounter(Eid, Entries);

			// ⚠️ 空怪物组会让战斗一开始就判胜利 —— 最难查的一类 bug
			Ctx.Check(FString::Printf(TEXT("[%s] 怪物组非空"), *Eid.ToString()),
				Entries.Num() > 0, TEXT("空怪物组 → 战斗立刻判胜"));

			int32 TotalCount = 0;
			for (const FHexEncounterEntry& En : Entries)
			{
				Ctx.Check(
					FString::Printf(TEXT("[%s] 引用敌人 %s 存在"), *Eid.ToString(), *En.EnemyId.ToString()),
					FHexContentLibrary::FindEnemy(En.EnemyId) != nullptr, TEXT(""));

				Ctx.Check(FString::Printf(TEXT("[%s] 数量 > 0"), *Eid.ToString()),
					En.Count > 0, FString::Printf(TEXT("Count=%d"), En.Count));

				TotalCount += En.Count;
			}

			// ⚠️ 规模曾太小：enc_01 只有 1 只狗，玩家一回合清完、掉血 0% ——
			//    战斗还没开始就结束了。至少 2 只才谈得上博弈。
			Ctx.Check(FString::Printf(TEXT("[%s] 敌人总数 >= 2"), *Eid.ToString()),
				TotalCount >= 2, FString::Printf(TEXT("总数=%d"), TotalCount));
		}

		// 未知 id 必须有兜底，绝不返回空组
		{
			TArray<FHexEncounterEntry> Fallback;
			FHexContentLibrary::GetEncounter(TEXT("__no_such_encounter__"), Fallback);
			Ctx.Check(TEXT("未知怪物组有兜底"), Fallback.Num() > 0, TEXT(""));
		}
	}

	// ─────────────────────────────────────────── 腐蚀度缩放

	void CheckEnemyScaling(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("敌人缩放（层数 / 腐蚀度）"));

		const FHexEnemyData* Hound = FHexContentLibrary::FindEnemy(TEXT("biting_hound"));
		if (!Hound)
		{
			Ctx.Fail(TEXT("缩放测试前置条件"), TEXT("找不到 biting_hound"));
			return;
		}

		// ⚠️ 最关键的一条回归：第 1 层、腐蚀度 0 时【必须】与 Godot 实测数值完全一致。
		//    如果层缩放污染了基线，所有移植过来的平衡结论（敌人 ATK×1.6、
		//    enc_02 打 19 点/回合、镇妖者撑 4 回合）就全部作废。
		{
			const FHexUnit U = FHexContentLibrary::MakeEnemyUnit(*Hound, 1, 0);
			Ctx.CheckEqual(TEXT("第1层腐蚀0：HP 等于基线"), U.HPMax, Hound->BaseHP);
			Ctx.CheckEqual(TEXT("第1层腐蚀0：ATK 等于基线"), U.ATK, Hound->BaseATK);
			Ctx.CheckEqual(TEXT("第1层腐蚀0：HP 满血"), U.HP, U.HPMax);
			Ctx.CheckEqual(TEXT("DEF 不被缩放"), U.DEF, Hound->BaseDEF);
			Ctx.CheckEqual(TEXT("AGI 不被缩放"), U.AGI, Hound->BaseAGI);
			Ctx.Check(TEXT("队伍为敌方"), U.Team == EHexTeam::Enemy, TEXT(""));
			Ctx.Check(TEXT("SourceId 正确回填"), U.SourceId == Hound->Id, TEXT(""));
		}

		// 腐蚀度单调递增（§9.4 的正反馈必须真的变强）
		{
			const FHexUnit A = FHexContentLibrary::MakeEnemyUnit(*Hound, 1, 0);
			const FHexUnit B = FHexContentLibrary::MakeEnemyUnit(*Hound, 1, 5);
			const FHexUnit C = FHexContentLibrary::MakeEnemyUnit(*Hound, 1, 10);

			Ctx.Check(TEXT("腐蚀度提升 HP 单调递增"),
				A.HPMax < B.HPMax && B.HPMax < C.HPMax,
				FString::Printf(TEXT("%d → %d → %d"), A.HPMax, B.HPMax, C.HPMax));

			Ctx.Check(TEXT("腐蚀度提升 ATK 单调递增"),
				A.ATK < B.ATK && B.ATK < C.ATK,
				FString::Printf(TEXT("%d → %d → %d"), A.ATK, B.ATK, C.ATK));

			// HP 成长快于 ATK（0.08 vs 0.06）：让后期战斗变长而不是变致死，
			// 玩家仍有反应空间
			const float HpGrowth = static_cast<float>(C.HPMax) / static_cast<float>(A.HPMax);
			const float AtkGrowth = static_cast<float>(C.ATK) / static_cast<float>(A.ATK);
			Ctx.Check(TEXT("HP 成长率 > ATK 成长率"), HpGrowth > AtkGrowth,
				FString::Printf(TEXT("HP×%.3f ATK×%.3f"), HpGrowth, AtkGrowth));
		}

		// 层数折算为等效腐蚀度：第 2 层 ≡ 腐蚀度 +3
		{
			const FHexUnit Floor2 = FHexContentLibrary::MakeEnemyUnit(*Hound, 2, 0);
			const FHexUnit Corr3 = FHexContentLibrary::MakeEnemyUnit(*Hound, 1, HexK::CorruptionPerDifficultyTier);
			Ctx.CheckEqual(TEXT("第2层 ≡ 腐蚀度+3（HP）"), Floor2.HPMax, Corr3.HPMax);
			Ctx.CheckEqual(TEXT("第2层 ≡ 腐蚀度+3（ATK）"), Floor2.ATK, Corr3.ATK);
		}

		// 负数入参不得产生更弱的敌人（防御式编程）
		{
			const FHexUnit Neg = FHexContentLibrary::MakeEnemyUnit(*Hound, -5, -99);
			Ctx.CheckEqual(TEXT("负数入参被夹到基线（HP）"), Neg.HPMax, Hound->BaseHP);
			Ctx.CheckEqual(TEXT("负数入参被夹到基线（ATK）"), Neg.ATK, Hound->BaseATK);
		}

		// Boss 标记必须带 BossPhase=1，否则多阶段 AI 不会启动
		{
			const FHexEnemyData* Worm = FHexContentLibrary::FindEnemy(TEXT("siege_worm"));
			if (Worm)
			{
				const FHexUnit U = FHexContentLibrary::MakeEnemyUnit(*Worm, 1, 0);
				Ctx.Check(TEXT("Boss 的 BossPhase 初始为 1"), U.BossPhase == 1,
					FString::Printf(TEXT("BossPhase=%d"), U.BossPhase));
				Ctx.Check(TEXT("Boss 免疫击退（override 生效）"),
					U.GetKnockbackResist() >= 999,
					FString::Printf(TEXT("抗性=%d"), U.GetKnockbackResist()));
			}
		}
	}

	// ─────────────────────────────────────────── 符文（§6.4）

	void CheckRunes(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("符文"));

		const TArray<FHexRuneData>& Runes = FHexRuneLibrary::AllRunes();

		// 用户决策 q11：第一版 12–18 个
		Ctx.Check(TEXT("符文数量在 12–18 之间(q11)"),
			Runes.Num() >= 12 && Runes.Num() <= 18,
			FString::Printf(TEXT("数量=%d"), Runes.Num()));

		{
			TSet<FName> Seen;
			bool bUnique = true;
			for (const FHexRuneData& R : Runes)
			{
				if (Seen.Contains(R.Id)) { bUnique = false; break; }
				Seen.Add(R.Id);
			}
			Ctx.Check(TEXT("符文 id 唯一"), bUnique, TEXT(""));
		}

		for (const FHexRuneData& R : Runes)
		{
			const FString N = R.Id.ToString();

			Ctx.Check(FString::Printf(TEXT("[%s] 有显示名"), *N),
				!R.DisplayName.IsEmpty(), TEXT(""));

			// ⚠️ 每个符文必须【真的做一件事】。
			//    既无触发器又无规则改写又无注入卡 = 装上去毫无变化，
			//    玩家会认为"我的符文没生效"（R8 的典型症状）。
			Ctx.Check(FString::Printf(TEXT("[%s] 至少有一种实际效果"), *N),
				R.Triggers.Num() > 0 || R.RuleOverrides.Num() > 0 || R.InjectedCardIds.Num() > 0,
				TEXT("空符文 —— 装上去什么都不会发生"));

			// ⚠️ R8 是真实风险：描述含糊的符文等于不存在。
			//    这条断言强制"何时触发、触发几次、与什么交互"写清楚。
			//    30 字是经验阈值：低于这个长度不可能把三件事说完。
			Ctx.Check(FString::Printf(TEXT("[%s] 机制描述足够精确(R8)"), *N),
				R.MechanicText.Len() >= 30,
				FString::Printf(TEXT("MechanicText 长度=%d，说不清"), R.MechanicText.Len()));

			// 诅咒类必须打 bIsCursed 标记：UI 靠它给红框警告，
			// 掉落逻辑靠它决定是否需要玩家二次确认
			if (R.Category == EHexRuneCategory::Curse)
			{
				Ctx.Check(FString::Printf(TEXT("[%s] 诅咒类已标记 bIsCursed"), *N),
					R.bIsCursed, TEXT("诅咒符文没标记 → UI 不会警告玩家"));
			}
			if (R.bIsCursed)
			{
				Ctx.Check(FString::Printf(TEXT("[%s] bIsCursed 与稀有度一致"), *N),
					R.Rarity == EHexRarity::Cursed, TEXT(""));
			}

			// 触发器自检
			for (const FHexRuneTrigger& T : R.Triggers)
			{
				Ctx.Check(FString::Printf(TEXT("[%s] 触发时机枚举有效"), *N),
					static_cast<uint8>(T.When) < static_cast<uint8>(EHexTriggerTiming::Count),
					TEXT("Timing 越界"));

				// ②′ 数值钩子【只在 OnAttack 有效】。
				// 挂在别的时机上会静默失效 —— 伤害管线只在 ② 与 ③ 之间调用钩子链。
				if (T.bHasValueAdd || T.bHasValueMult)
				{
					Ctx.Check(FString::Printf(TEXT("[%s] ②′钩子挂在 OnAttack"), *N),
						T.When == EHexTriggerTiming::OnAttack,
						TEXT("ValueAdd/ValueMult 挂在非 OnAttack 时机 → 永远不会生效"));
				}

				// 乘区为 1.0 等于没有乘区，是典型的忘填
				if (T.bHasValueMult)
				{
					Ctx.Check(FString::Printf(TEXT("[%s] 乘区不等于 1.0"), *N),
						!FMath::IsNearlyEqual(T.ValueMult, 1.0f),
						FString::Printf(TEXT("ValueMult=%.3f"), T.ValueMult));
				}

				// 触发器要么有效果、要么有数值钩子
				Ctx.Check(FString::Printf(TEXT("[%s] 触发器非空转"), *N),
					T.Effects.Num() > 0 || T.bHasValueAdd || T.bHasValueMult,
					TEXT("触发器既无效果也无数值钩子"));

				// 引用的状态必须存在
				for (const FHexEffectStep& S : T.Effects)
				{
					if (S.Op == EHexEffectOp::ApplyStatus)
					{
						Ctx.Check(
							FString::Printf(TEXT("[%s] 状态 id 存在(%s)"), *N, *S.StatusId.ToString()),
							FHexStatusLibrary::Exists(S.StatusId),
							TEXT("符文引用了未定义的状态 → 效果静默变 no-op"));
					}
				}
			}

			// 规则改写自检
			for (const FHexRuleOverride& O : R.RuleOverrides)
			{
				Ctx.Check(FString::Printf(TEXT("[%s] 规则枚举有效"), *N),
					static_cast<uint8>(O.Rule) < static_cast<uint8>(EHexGameRule::Count),
					TEXT("Rule 越界"));

				// 布尔型规则必须是覆盖语义。累加 bool 无意义，
				// 且会让两个改同一开关的符文互相吃掉（R8 症状）
				const bool bIsBoolRule =
					O.Rule == EHexGameRule::FirstCardFree ||
					O.Rule == EHexGameRule::NoDrawFixedHand ||
					O.Rule == EHexGameRule::KnockbackImmune ||
					O.Rule == EHexGameRule::BlockPersists ||
					O.Rule == EHexGameRule::NoBlockAllowed ||
					O.Rule == EHexGameRule::ExhaustAllAttacks;
				if (bIsBoolRule)
				{
					Ctx.Check(FString::Printf(TEXT("[%s] 布尔规则用覆盖语义"), *N),
						!O.bIsDelta, TEXT("布尔开关标成了 Delta → 无法叠加也无法覆盖"));
				}
			}

			// 注入的衍生卡必须存在
			for (const FName& Cid : R.InjectedCardIds)
			{
				Ctx.Check(FString::Printf(TEXT("[%s] 注入卡 %s 存在"), *N, *Cid.ToString()),
					FHexContentLibrary::FindCard(Cid) != nullptr, TEXT(""));
			}
		}

		// ── §6.4 类别占比
		//
		// ⚠️ 这不是形式主义。占比失衡的后果很具体：
		//    乘区多了 → "叠乘区就赢"，组合空间退化成一条乘法链；
		//    规则改写少了 → 符文变成数值词条，D6 的价值归零。
		//    人天然倾向写乘区（好写、好懂、好平衡），所以必须机械钉住。
		{
			const int32 Total = Runes.Num();
			const int32 NRule = FHexRuneLibrary::CountByCategory(EHexRuneCategory::RuleRewrite);
			const int32 NTrig = FHexRuneLibrary::CountByCategory(EHexRuneCategory::Trigger);
			const int32 NCond = FHexRuneLibrary::CountByCategory(EHexRuneCategory::Conditional);
			const int32 NMult = FHexRuneLibrary::CountByCategory(EHexRuneCategory::Multiplier);
			const int32 NCurse = FHexRuneLibrary::CountByCategory(EHexRuneCategory::Curse);

			Ctx.CheckEqual(TEXT("类别计数之和 = 符文总数"),
				NRule + NTrig + NCond + NMult + NCurse, Total);

			auto Pct = [Total](int32 N) { return Total > 0 ? 100.0f * N / Total : 0.0f; };

			// 目标 35%，允许 ±10 个百分点（小样本下不可能精确命中）
			Ctx.Check(TEXT("规则改写占比 ≈35%"),
				FMath::Abs(Pct(NRule) - 35.0f) <= 10.0f,
				FString::Printf(TEXT("实际 %.1f%% (%d/%d)"), Pct(NRule), NRule, Total));

			Ctx.Check(TEXT("触发器占比 ≈30%"),
				FMath::Abs(Pct(NTrig) - 30.0f) <= 10.0f,
				FString::Printf(TEXT("实际 %.1f%% (%d/%d)"), Pct(NTrig), NTrig, Total));

			Ctx.Check(TEXT("条件增益占比 ≈15%"),
				FMath::Abs(Pct(NCond) - 15.0f) <= 10.0f,
				FString::Printf(TEXT("实际 %.1f%% (%d/%d)"), Pct(NCond), NCond, Total));

			// 乘区是【硬上限】，不给容差
			Ctx.Check(TEXT("乘区占比 ≤8%（硬上限）"),
				Pct(NMult) <= 8.0f,
				FString::Printf(TEXT("实际 %.1f%% (%d/%d) —— 乘区超标会让组合退化成乘法链"),
					Pct(NMult), NMult, Total));

			Ctx.Check(TEXT("诅咒占比 ≈12%"),
				FMath::Abs(Pct(NCurse) - 12.0f) <= 10.0f,
				FString::Printf(TEXT("实际 %.1f%% (%d/%d)"), Pct(NCurse), NCurse, Total));
		}

		// ── ②′ 顺序组合的存在性
		//
		// §6.5 的"免费一层深度"要求至少存在一对【加区 + 乘区】符文，
		// 否则"槽位有序"这件事在第一版里根本无法被玩家察觉。
		{
			bool bHasAddHook = false;
			bool bHasMultHook = false;
			for (const FHexRuneData& R : Runes)
			{
				for (const FHexRuneTrigger& T : R.Triggers)
				{
					if (T.When != EHexTriggerTiming::OnAttack) { continue; }
					if (T.bHasValueAdd) { bHasAddHook = true; }
					if (T.bHasValueMult) { bHasMultHook = true; }
				}
			}
			Ctx.Check(TEXT("存在②′加区符文"), bHasAddHook, TEXT(""));
			Ctx.Check(TEXT("存在②′乘区符文"), bHasMultHook, TEXT(""));
			Ctx.Check(TEXT("加区+乘区可组成顺序敏感对(§6.5)"),
				bHasAddHook && bHasMultHook,
				TEXT("没有这一对，「6槽有序」在第一版无法被玩家察觉"));
		}

		// 稀有度查询可用（三选一抽取依赖它）
		{
			TArray<FName> Commons;
			FHexRuneLibrary::GetIdsByRarity(EHexRarity::Common, Commons);
			Ctx.Check(TEXT("存在普通稀有度符文（保证三选一抽得到）"),
				Commons.Num() >= 3,
				FString::Printf(TEXT("普通符文数=%d"), Commons.Num()));
		}

		// 内容库的转发必须与符文库一致（防止两处不同步）
		{
			Ctx.CheckEqual(TEXT("内容库转发数量一致"),
				FHexContentLibrary::AllRunes().Num(), Runes.Num());

			if (Runes.Num() > 0)
			{
				Ctx.Check(TEXT("内容库 FindRune 转发正确"),
					FHexContentLibrary::FindRune(Runes[0].Id) != nullptr, TEXT(""));
			}
		}
	}
}

bool FHexVerifySuites::VerifyContent(FHexVerifyContext& Ctx)
{
	CheckCards(Ctx);
	CheckHeroesAndDecks(Ctx);
	CheckEnemiesAndEncounters(Ctx);
	CheckEnemyScaling(Ctx);
	CheckRunes(Ctx);
	return Ctx.NumFailed() == 0;
}
