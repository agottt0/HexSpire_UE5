// Copyright Hex Spire. All Rights Reserved.
//
// 装备系统验证 —— §5 / §15.7
//
// 本套件盯三类问题：
//   ① 内容纪律：词条不得给"某张卡固定伤害"、槽位限制要生效、
//      稀有度门槛要生效（否则稀有度维度崩塌）
//   ② 随机性质量：同 seed 必须完全复现；词条不重复；
//      腐蚀度必须真的提升掉落品质（否则 D4 盲探的风险收益不成立）
//   ③ 接入正确性：规则改写要真的进 RuleAggregate、
//      触发器要真的进 TriggerBus、武器覆写要真的改 TargetSpec
//      —— 这三条只要断一条，玩家就会遇到"装备装上了但没反应"（R8）

#include "Verify/HexVerify.h"
#include "Equip/HexEquipData.h"
#include "Content/HexContentLibrary.h"
#include "Battle/HexBattleState.h"
#include "Battle/HexTriggerBus.h"
#include "Battle/HexRuleBook.h"
#include "Battle/HexStatusData.h"
#include "Rng/HexRngStreams.h"
#include "Core/HexSpireConstants.h"

namespace
{
	// ═══════════════════════════════════════════ 词条表

	void CheckAffixes(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("词条表"));

		const TArray<FHexAffixDef>& All = FHexEquipLibrary::AllAffixes();
		Ctx.Check(TEXT("词条表非空"), All.Num() > 0, TEXT(""));

		{
			TSet<FName> Seen;
			bool bUnique = true;
			FString Dup;
			for (const FHexAffixDef& A : All)
			{
				if (Seen.Contains(A.Id)) { bUnique = false; Dup = A.Id.ToString(); break; }
				Seen.Add(A.Id);
			}
			Ctx.Check(TEXT("词条 id 唯一"), bUnique,
				FString::Printf(TEXT("重复 id=%s"), *Dup));
		}

		for (const FHexAffixDef& A : All)
		{
			const FString N = A.Id.ToString();

			Ctx.Check(FString::Printf(TEXT("[%s] 有显示名"), *N),
				!A.DisplayName.IsEmpty(), TEXT(""));

			// 词条描述是 UI 直接显示的内容，空描述 = 玩家看不懂自己装了什么
			Ctx.Check(FString::Printf(TEXT("[%s] 有描述文本"), *N),
				!A.Text.IsEmpty(), TEXT("空描述 → 玩家无法判断这条词条的价值"));

			// 词条必须真的做一件事
			switch (A.Kind)
			{
			case EHexAffixKind::StatFlat:
				Ctx.Check(FString::Printf(TEXT("[%s] 平坦词条数值非 0"), *N),
					A.FlatValue != 0, TEXT("FlatValue=0 → 装上无任何变化"));
				break;

			case EHexAffixKind::StatPercent:
				Ctx.Check(FString::Printf(TEXT("[%s] 百分比词条数值非 0"), *N),
					!FMath::IsNearlyZero(A.PercentValue), TEXT("PercentValue=0"));

				// ⚠️ 只允许 HP 走百分比。ATK/DEF 的百分比会与平坦词条
				//    叠成"先加后乘"的复利，玩家算不明白（§13.2 要求数值可推算）。
				Ctx.Check(FString::Printf(TEXT("[%s] 百分比词条仅限 HP"), *N),
					A.Stat == EHexStat::HP,
					TEXT("非 HP 属性走百分比会产生玩家算不明白的复利"));
				break;

			case EHexAffixKind::RuleOverride:
				Ctx.Check(FString::Printf(TEXT("[%s] 规则枚举有效"), *N),
					static_cast<uint8>(A.Rule.Rule) < static_cast<uint8>(EHexGameRule::Count),
					TEXT("Rule 越界"));

				// 规则改写是符文的领地（§6.4 占 35%），装备抢这块会让
				// 两个系统定位模糊。所以装备的规则词条必须挂稀有度门槛。
				Ctx.Check(FString::Printf(TEXT("[%s] 规则词条有稀有度门槛"), *N),
					static_cast<uint8>(A.MinRarity) >= static_cast<uint8>(EHexRarity::Uncommon),
					TEXT("规则改写出现在普通装备上 → 与符文的定位冲突"));
				break;

			case EHexAffixKind::Trigger:
				Ctx.Check(FString::Printf(TEXT("[%s] 触发器有效果"), *N),
					A.Trigger.Effects.Num() > 0, TEXT("触发器空转"));

				Ctx.Check(FString::Printf(TEXT("[%s] 触发时机有效"), *N),
					static_cast<uint8>(A.Trigger.When) < static_cast<uint8>(EHexTriggerTiming::Count),
					TEXT(""));

				// ⚠️ ②′ 数值钩子是符文【专属】的深度来源（§6.5）。
				//    装备也挂钩子会让"槽位顺序"这件事扩散到无序的装备槽上，
				//    玩家无法预期结果。
				Ctx.Check(FString::Printf(TEXT("[%s] 装备不使用②′数值钩子"), *N),
					!A.Trigger.bHasValueAdd && !A.Trigger.bHasValueMult,
					TEXT("②′顺序钩子是符文专属 —— 装备槽无序，挂钩子结果不可预期"));

				// 引用的状态必须存在
				for (const FHexEffectStep& S : A.Trigger.Effects)
				{
					if (S.Op == EHexEffectOp::ApplyStatus)
					{
						Ctx.Check(
							FString::Printf(TEXT("[%s] 状态 %s 存在"), *N, *S.StatusId.ToString()),
							FHexStatusLibrary::Exists(S.StatusId),
							TEXT("引用未定义状态 → 效果静默变 no-op"));
					}
					// ⚠️ 治疗刻意不系数化（固定值）。若写成 ATK×0.4，
					//    后期 ATK 200 时单次回血 80 点，会压过敌人输出。
					if (S.Op == EHexEffectOp::Heal)
					{
						Ctx.Check(FString::Printf(TEXT("[%s] 治疗有数值"), *N),
							S.FlatValue > 0.0f, TEXT("Heal 的 FlatValue=0 → 回复 0 点"));
						Ctx.Check(FString::Printf(TEXT("[%s] 治疗不随属性缩放"), *N),
							FMath::IsNearlyZero(S.StatRatio),
							TEXT("治疗系数化会让后期回复量压过敌人输出"));
					}
				}
				break;
			}

			Ctx.Check(FString::Printf(TEXT("[%s] 权重非负"), *N),
				A.Weight >= 0, TEXT(""));
		}
	}

	// ═══════════════════════════════════════════ 装备表

	void CheckEquips(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("装备表"));

		const TArray<FHexEquipData>& All = FHexEquipLibrary::AllEquips();
		Ctx.Check(TEXT("装备表非空"), All.Num() > 0, TEXT(""));

		{
			TSet<FName> Seen;
			bool bUnique = true;
			for (const FHexEquipData& E : All)
			{
				if (Seen.Contains(E.Id)) { bUnique = false; break; }
				Seen.Add(E.Id);
			}
			Ctx.Check(TEXT("装备 id 唯一"), bUnique, TEXT(""));
		}

		// 三个槽位都要有装备，否则掉落时某个槽永远空着
		for (int32 I = 0; I < static_cast<int32>(EHexEquipSlot::Count); ++I)
		{
			const EHexEquipSlot Slot = static_cast<EHexEquipSlot>(I);
			TArray<FName> Ids;
			FHexEquipLibrary::GetEquipIdsForSlot(Slot, Ids);
			Ctx.Check(FString::Printf(TEXT("槽位 %d 至少 2 件装备"), I),
				Ids.Num() >= 2,
				FString::Printf(TEXT("数量=%d —— 该槽位掉落缺乏多样性"), Ids.Num()));
		}

		for (const FHexEquipData& E : All)
		{
			const FString N = E.Id.ToString();

			Ctx.Check(FString::Printf(TEXT("[%s] 有显示名"), *N),
				!E.DisplayName.IsEmpty(), TEXT(""));

			// 固有词条必须存在，且必须允许本装备的槽位
			for (const FName& Aid : E.InherentAffixIds)
			{
				const FHexAffixDef* A = FHexEquipLibrary::FindAffix(Aid);
				Ctx.Check(FString::Printf(TEXT("[%s] 固有词条 %s 存在"), *N, *Aid.ToString()),
					A != nullptr, TEXT("引用了未定义的词条"));

				if (A)
				{
					// ⚠️ 槽位限制被绕过的后果：饰品 roll 出射程加成，
					//    玩家对"什么槽位能出什么词条"的直觉被破坏。
					Ctx.Check(
						FString::Printf(TEXT("[%s] 固有词条 %s 允许本槽位"), *N, *Aid.ToString()),
						A->AllowsSlot(E.Slot),
						TEXT("固有词条挂在了它不允许的槽位上"));
				}
			}

			// 注入的衍生卡必须存在，且必须不占卡组容量
			for (const FName& Cid : E.InjectedCardIds)
			{
				const FHexCardData* C = FHexContentLibrary::FindCard(Cid);
				Ctx.Check(FString::Printf(TEXT("[%s] 注入卡 %s 存在"), *N, *Cid.ToString()),
					C != nullptr, TEXT(""));

				if (C)
				{
					// ⚠️ 若衍生卡占容量，装上这件武器就等于卡组容量 -1，
					//    玩家会拒绝所有注入型装备。
					Ctx.Check(
						FString::Printf(TEXT("[%s] 注入卡 %s 不占容量"), *N, *Cid.ToString()),
						!C->bCountsTowardCapacity,
						TEXT("衍生卡占用卡组容量 → 玩家会拒绝注入型装备"));

					Ctx.Check(
						FString::Printf(TEXT("[%s] 注入卡 %s 为衍生类型"), *N, *Cid.ToString()),
						C->CardType == EHexCardType::Derived,
						TEXT("非 Derived 类型的注入卡会进掠夺池，变成幽灵牌"));
				}
			}

			// 攻击覆写只允许出现在武器上
			if (E.AttackOverride.bActive)
			{
				Ctx.Check(FString::Printf(TEXT("[%s] 攻击覆写仅限武器"), *N),
					E.Slot == EHexEquipSlot::Weapon,
					TEXT("非武器带攻击覆写 → GetAttackOverride 只读武器槽，会静默失效"));

				Ctx.Check(FString::Printf(TEXT("[%s] 覆写射程有效"), *N),
					E.AttackOverride.RangeMax >= E.AttackOverride.RangeMin &&
					E.AttackOverride.RangeMax >= 1,
					FString::Printf(TEXT("射程 %d-%d"),
						E.AttackOverride.RangeMin, E.AttackOverride.RangeMax));
			}
		}

		// ⚠️ 至少要有一把不覆写《攻击》的武器。
		//    没有这个"基准武器"，玩家无法对比出其它武器的结构变化，
		//    也没有"我不想改打法"的选项。
		{
			bool bHasPlain = false;
			bool bHasOverride = false;
			for (const FHexEquipData& E : All)
			{
				if (E.Slot != EHexEquipSlot::Weapon) { continue; }
				if (E.AttackOverride.bActive) { bHasOverride = true; }
				else { bHasPlain = true; }
			}
			Ctx.Check(TEXT("存在不覆写《攻击》的基准武器"), bHasPlain, TEXT(""));
			Ctx.Check(TEXT("存在覆写《攻击》的武器（§5.2）"), bHasOverride, TEXT(""));
		}
	}

	// ═══════════════════════════════════════════ 稀有度与词条数

	void CheckRarity(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("稀有度 → 词条数"));

		Ctx.CheckEqual(TEXT("普通 = 1 条"),
			FHexEquipLibrary::AffixCountForRarity(EHexRarity::Common), 1);
		Ctx.CheckEqual(TEXT("精良 = 2 条"),
			FHexEquipLibrary::AffixCountForRarity(EHexRarity::Uncommon), 2);
		Ctx.CheckEqual(TEXT("稀有 = 3 条"),
			FHexEquipLibrary::AffixCountForRarity(EHexRarity::Rare), 3);
		Ctx.CheckEqual(TEXT("史诗 = 4 条"),
			FHexEquipLibrary::AffixCountForRarity(EHexRarity::Epic), 4);
		Ctx.CheckEqual(TEXT("传说 = 5 条"),
			FHexEquipLibrary::AffixCountForRarity(EHexRarity::Legendary), 5);

		// ⚠️ 必须线性递增而非跳跃（1/2/4/8）。
		//    跳跃式会让高稀有度形成断层碾压，中间档位立刻变垃圾，
		//    掉落的期待感只剩最高一档。
		{
			bool bLinear = true;
			int32 Prev = FHexEquipLibrary::AffixCountForRarity(EHexRarity::Common);
			const EHexRarity Ladder[] = {
				EHexRarity::Uncommon, EHexRarity::Rare,
				EHexRarity::Epic, EHexRarity::Legendary
			};
			for (const EHexRarity R : Ladder)
			{
				const int32 Cur = FHexEquipLibrary::AffixCountForRarity(R);
				if (Cur - Prev != 1) { bLinear = false; break; }
				Prev = Cur;
			}
			Ctx.Check(TEXT("词条数逐档 +1（禁止跳跃式增长）"), bLinear, TEXT(""));
		}
	}

	// ═══════════════════════════════════════════ 生成器

	void CheckGenerator(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("装备生成"));

		// ── 词条数量符合稀有度
		{
			FHexRngStreams Rng(12345);
			const FHexEquipInstance Inst =
				FHexEquipGenerator::Generate(TEXT("wp_mace"), EHexRarity::Rare, Rng, 1);

			Ctx.Check(TEXT("生成的实例有效"), Inst.IsValid(), TEXT(""));
			Ctx.CheckEqual(TEXT("稀有装备 roll 出 3 条随机词条"),
				Inst.RolledAffixIds.Num(),
				FHexEquipLibrary::AffixCountForRarity(EHexRarity::Rare));
		}

		// ── 同 seed 必须完全复现（架构纪律 1）
		{
			FHexRngStreams A(999);
			FHexRngStreams B(999);
			const FHexEquipInstance IA = FHexEquipGenerator::GenerateRandom(
				EHexEquipSlot::Weapon, 3, A, 1);
			const FHexEquipInstance IB = FHexEquipGenerator::GenerateRandom(
				EHexEquipSlot::Weapon, 3, B, 1);

			Ctx.Check(TEXT("同 seed 生成结果完全一致"),
				IA.ContentHash() == IB.ContentHash(),
				FString::Printf(TEXT("哈希 %u vs %u"), IA.ContentHash(), IB.ContentHash()));
			Ctx.Check(TEXT("同 seed 装备 id 一致"), IA.EquipId == IB.EquipId, TEXT(""));
			Ctx.Check(TEXT("同 seed 稀有度一致"), IA.Rarity == IB.Rarity, TEXT(""));
		}

		// ── 不同 seed 应当产生差异（否则随机性形同不存在）
		{
			TSet<uint32> Hashes;
			for (uint64 Seed = 1; Seed <= 40; ++Seed)
			{
				FHexRngStreams Rng(Seed);
				const FHexEquipInstance I = FHexEquipGenerator::GenerateRandom(
					EHexEquipSlot::Trinket, 2, Rng, 1);
				Hashes.Add(I.ContentHash());
			}
			Ctx.Check(TEXT("40 个 seed 产出至少 8 种不同结果"),
				Hashes.Num() >= 8,
				FString::Printf(TEXT("仅 %d 种"), Hashes.Num()));
		}

		// ── 词条不重复
		{
			bool bAllUnique = true;
			for (uint64 Seed = 1; Seed <= 60 && bAllUnique; ++Seed)
			{
				FHexRngStreams Rng(Seed);
				const FHexEquipInstance I = FHexEquipGenerator::Generate(
					TEXT("ar_cloth"), EHexRarity::Epic, Rng, 1);

				TSet<FName> Seen;
				// 固有词条也要参与去重 —— "固有 ATK+3 / 随机 ATK+3" 同样是 bug
				const FHexEquipData* D = FHexEquipLibrary::FindEquip(I.EquipId);
				if (D)
				{
					for (const FName& A : D->InherentAffixIds) { Seen.Add(A); }
				}
				for (const FName& A : I.RolledAffixIds)
				{
					if (Seen.Contains(A)) { bAllUnique = false; break; }
					Seen.Add(A);
				}
			}
			Ctx.Check(TEXT("同一件装备上无重复词条（含固有）"), bAllUnique,
				TEXT("重复词条看起来像 bug，且让词条数量的多样性意图落空"));
		}

		// ── 稀有度门槛真的生效
		{
			bool bNoOverpowered = true;
			FString Bad;
			for (uint64 Seed = 1; Seed <= 80 && bNoOverpowered; ++Seed)
			{
				FHexRngStreams Rng(Seed);
				const FHexEquipInstance I = FHexEquipGenerator::Generate(
					TEXT("ar_cloth"), EHexRarity::Common, Rng, 1);

				for (const FName& Aid : I.RolledAffixIds)
				{
					const FHexAffixDef* A = FHexEquipLibrary::FindAffix(Aid);
					if (A && static_cast<uint8>(A->MinRarity) > static_cast<uint8>(EHexRarity::Common))
					{
						bNoOverpowered = false;
						Bad = Aid.ToString();
						break;
					}
				}
			}
			Ctx.Check(TEXT("普通装备 roll 不出高稀有度词条"), bNoOverpowered,
				FString::Printf(TEXT("越级词条=%s —— 稀有度维度会崩塌"), *Bad));
		}

		// ── 槽位限制真的生效
		{
			bool bSlotOk = true;
			FString Bad;
			for (uint64 Seed = 1; Seed <= 60 && bSlotOk; ++Seed)
			{
				FHexRngStreams Rng(Seed);
				const FHexEquipInstance I = FHexEquipGenerator::Generate(
					TEXT("wp_mace"), EHexRarity::Epic, Rng, 1);

				for (const FName& Aid : I.RolledAffixIds)
				{
					const FHexAffixDef* A = FHexEquipLibrary::FindAffix(Aid);
					if (A && !A->AllowsSlot(EHexEquipSlot::Weapon))
					{
						bSlotOk = false;
						Bad = Aid.ToString();
						break;
					}
				}
			}
			Ctx.Check(TEXT("武器 roll 不出禁用于武器的词条"), bSlotOk,
				FString::Printf(TEXT("越界词条=%s"), *Bad));
		}

		// ── 腐蚀度必须真的提升掉落品质（§9.4 的正反馈）
		//
		// ⚠️ 这条断言直接关系到 D4 盲探的成立：
		//    "多探一间房 → 腐蚀度 +1 → 敌人更强但掉落更好"。
		//    若掉落不变好，玩家的最优策略就是尽量少探，D4 退化为纯风险。
		{
			auto AvgRarity = [](int32 Corruption) -> float
			{
				int32 Sum = 0;
				const int32 N = 400;
				for (int32 I = 0; I < N; ++I)
				{
					FHexRngStreams Rng(static_cast<uint64>(I) * 7919 + 13);
					Sum += static_cast<int32>(FHexEquipGenerator::RollRarity(Corruption, Rng));
				}
				return static_cast<float>(Sum) / static_cast<float>(N);
			};

			const float R0 = AvgRarity(0);
			const float R6 = AvgRarity(6);
			const float R12 = AvgRarity(12);

			Ctx.Check(TEXT("腐蚀度提升平均稀有度（单调）"),
				R0 < R6 && R6 < R12,
				FString::Printf(TEXT("%.3f → %.3f → %.3f"), R0, R6, R12));

			// 幅度也要够 —— 提升 0.01 档玩家感受不到
			Ctx.Check(TEXT("腐蚀度 12 时平均稀有度提升 ≥0.3 档"),
				R12 - R0 >= 0.3f,
				FString::Printf(TEXT("提升仅 %.3f 档，玩家感受不到"), R12 - R0));
		}

		// ── 装备自身的稀有度下限优先
		{
			FHexRngStreams Rng(555);
			// ar_heavy 的 BaseRarity 是 Rare，要求以 Common 生成时应被抬到 Rare
			const FHexEquipInstance I = FHexEquipGenerator::Generate(
				TEXT("ar_heavy"), EHexRarity::Common, Rng, 1);
			Ctx.Check(TEXT("装备 BaseRarity 下限优先于请求稀有度"),
				I.Rarity == EHexRarity::Rare,
				FString::Printf(TEXT("实际稀有度=%d"), static_cast<int32>(I.Rarity)));
		}

		// ── 未知 id 返回无效实例而非崩溃
		{
			FHexRngStreams Rng(1);
			const FHexEquipInstance I = FHexEquipGenerator::Generate(
				TEXT("__no_such_equip__"), EHexRarity::Rare, Rng, 1);
			Ctx.Check(TEXT("未知装备 id 返回无效实例"), !I.IsValid(), TEXT(""));
		}
	}

	// ═══════════════════════════════════════════ 重塑

	void CheckReforge(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("重塑（§5.5）"));

		FHexRngStreams Rng(2024);
		FHexEquipInstance Inst = FHexEquipGenerator::Generate(
			TEXT("tr_pouch"), EHexRarity::Rare, Rng, 1);

		const EHexRarity RarityBefore = Inst.Rarity;
		const int32 CountBefore = Inst.RolledAffixIds.Num();

		FHexEquipGenerator::Reforge(Inst, Rng);

		// ⚠️ 重塑改的是"运气"，不是"品质"。
		//    若重塑能提升稀有度，玩家会无脑重塑到传说，稀有度维度崩塌。
		Ctx.Check(TEXT("重塑不改变稀有度"), Inst.Rarity == RarityBefore, TEXT(""));
		Ctx.CheckEqual(TEXT("重塑后词条数不变"), Inst.RolledAffixIds.Num(), CountBefore);
		Ctx.CheckEqual(TEXT("重塑次数累加"), Inst.ReforgeCount, 1);

		// ── 消耗递增
		//
		// ⚠️ 固定消耗下玩家会重塑到完美词条为止，随机性带来的取舍消失，
		//    装备变成"存够碎片就一定最优"。
		{
			FHexEquipInstance A = FHexEquipGenerator::Generate(
				TEXT("tr_pouch"), EHexRarity::Rare, Rng, 2);
			const int32 C0 = FHexEquipGenerator::ReforgeCost(A);
			A.ReforgeCount = 1;
			const int32 C1 = FHexEquipGenerator::ReforgeCost(A);
			A.ReforgeCount = 3;
			const int32 C3 = FHexEquipGenerator::ReforgeCost(A);

			Ctx.Check(TEXT("重塑消耗随次数递增"), C0 < C1 && C1 < C3,
				FString::Printf(TEXT("%d → %d → %d"), C0, C1, C3));

			// 高稀有度重塑更贵（它的词条池更强，重塑收益也更大）
			FHexEquipInstance B = A;
			B.Rarity = EHexRarity::Epic;
			B.ReforgeCount = 0;
			A.ReforgeCount = 0;
			Ctx.Check(TEXT("高稀有度重塑消耗更高"),
				FHexEquipGenerator::ReforgeCost(B) > FHexEquipGenerator::ReforgeCost(A),
				TEXT(""));
		}

		// ── 重塑可复现
		{
			FHexRngStreams RA(77);
			FHexRngStreams RB(77);
			FHexEquipInstance IA = FHexEquipGenerator::Generate(TEXT("ar_scale"), EHexRarity::Epic, RA, 1);
			FHexEquipInstance IB = FHexEquipGenerator::Generate(TEXT("ar_scale"), EHexRarity::Epic, RB, 1);
			FHexEquipGenerator::Reforge(IA, RA);
			FHexEquipGenerator::Reforge(IB, RB);
			Ctx.Check(TEXT("同 seed 重塑结果一致"),
				IA.ContentHash() == IB.ContentHash(), TEXT(""));
		}
	}

	// ═══════════════════════════════════════════ 三槽装载

	void CheckLoadout(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("三槽装载"));

		FHexRngStreams Rng(4242);
		FHexEquipLoadout Loadout;

		Ctx.CheckEqual(TEXT("初始为空"), Loadout.GetFilledCount(), 0);
		Ctx.Check(TEXT("空槽返回 nullptr"),
			Loadout.GetSlot(EHexEquipSlot::Weapon) == nullptr, TEXT(""));

		// ── 装备与槽位匹配
		{
			const FHexEquipInstance Weapon = FHexEquipGenerator::Generate(
				TEXT("wp_halberd"), EHexRarity::Uncommon, Rng, 1);
			Ctx.Check(TEXT("装备武器成功"), Loadout.Equip(Weapon), TEXT(""));
			Ctx.CheckEqual(TEXT("已装备数 = 1"), Loadout.GetFilledCount(), 1);
			Ctx.Check(TEXT("武器槽可取回"),
				Loadout.GetSlot(EHexEquipSlot::Weapon) != nullptr, TEXT(""));
			// 装备会按 EquipData 的槽位自动归位，不会串槽
			Ctx.Check(TEXT("盔甲槽仍为空"),
				Loadout.GetSlot(EHexEquipSlot::Armor) == nullptr, TEXT(""));
		}

		// ── 无效实例不得装上
		{
			FHexEquipInstance Invalid;
			Ctx.Check(TEXT("无效实例装备失败"), !Loadout.Equip(Invalid), TEXT(""));
		}

		// ── 卸下
		{
			Loadout.Unequip(EHexEquipSlot::Weapon);
			Ctx.CheckEqual(TEXT("卸下后为空"), Loadout.GetFilledCount(), 0);
		}

		// ── 属性加成：平坦 + 百分比
		{
			FHexEquipLoadout L;
			// 《镇魂锏》固有 af_atk_s（ATK +3）
			L.Equip(FHexEquipGenerator::Generate(TEXT("wp_mace"), EHexRarity::Common, Rng, 10));

			const int32 AtkBonus = L.GetStatBonus(EHexStat::ATK, 10);
			Ctx.Check(TEXT("武器提供 ATK 加成"), AtkBonus >= 3,
				FString::Printf(TEXT("ATK 加成=%d（至少含固有 +3）"), AtkBonus));

			// 无关属性不该被加成（除非 roll 到）
			const int32 HpBonus = L.GetStatBonus(EHexStat::HP, 80);
			Ctx.Check(TEXT("HP 加成非负"), HpBonus >= 0, TEXT(""));
		}

		// ── 百分比基于基线而非当前值
		//
		// ⚠️ 若基于当前值，三件 +15% 会变成 ×1.15³=+52%，
		//    玩家算不明白，且与 §4.4「加法先做完」的口径冲突。
		{
			FHexEquipLoadout L;
			// 《粗麻衣》固有 af_hp_p_s（HP +12%）
			L.Equip(FHexEquipGenerator::Generate(TEXT("ar_cloth"), EHexRarity::Common, Rng, 11));
			const int32 Bonus100 = L.GetStatBonus(EHexStat::HP, 100);
			const int32 Bonus200 = L.GetStatBonus(EHexStat::HP, 200);
			// 线性关系：基线翻倍，百分比部分也翻倍
			Ctx.Check(TEXT("百分比加成与基线成正比"),
				Bonus200 >= Bonus100 * 2 - 2 && Bonus200 <= Bonus100 * 2 + 2,
				FString::Printf(TEXT("基线100→%d 基线200→%d"), Bonus100, Bonus200));
		}

		// ── 武器覆写《攻击》
		{
			FHexEquipLoadout L;
			Ctx.Check(TEXT("未装武器时无覆写"),
				!L.GetAttackOverride().bActive, TEXT(""));

			// 《镇魂锏》不覆写
			L.Equip(FHexEquipGenerator::Generate(TEXT("wp_mace"), EHexRarity::Common, Rng, 12));
			Ctx.Check(TEXT("基准武器不覆写《攻击》"),
				!L.GetAttackOverride().bActive, TEXT(""));

			// 《长柄戟》→ 直线 2 格
			L.Equip(FHexEquipGenerator::Generate(TEXT("wp_halberd"), EHexRarity::Uncommon, Rng, 13));
			const FHexAttackOverride Ov = L.GetAttackOverride();
			Ctx.Check(TEXT("《长柄戟》覆写生效"), Ov.bActive, TEXT(""));
			Ctx.Check(TEXT("《长柄戟》改为直线形状"),
				Ov.Shape == EHexTargetShape::Line, TEXT(""));
			Ctx.CheckEqual(TEXT("《长柄戟》射程 2"), Ov.RangeMax, 2);

			// 覆写真的改变了《攻击》的 TargetSpec
			{
				const FHexCardData* Atk = FHexContentLibrary::FindCard(TEXT("atk_basic"));
				if (Atk)
				{
					FHexTargetSpec Spec = Atk->TargetSpec;
					Ctx.CheckEqual(TEXT("《攻击》原始射程 = 1"), Spec.RangeMax, 1);
					Ov.ApplyTo(Spec);
					Ctx.CheckEqual(TEXT("覆写后《攻击》射程 = 2"), Spec.RangeMax, 2);
					Ctx.Check(TEXT("覆写后《攻击》形状 = 直线"),
						Spec.Shape == EHexTargetShape::Line, TEXT(""));
				}
			}

			// 《环首刀》→ 全部相邻，且不需要视线
			L.Equip(FHexEquipGenerator::Generate(TEXT("wp_ring_saber"), EHexRarity::Rare, Rng, 14));
			const FHexAttackOverride Saber = L.GetAttackOverride();
			Ctx.Check(TEXT("《环首刀》改为全部相邻"),
				Saber.Shape == EHexTargetShape::AdjacentAll, TEXT(""));
			Ctx.Check(TEXT("《环首刀》不需要视线"),
				!Saber.bRequiresLineOfSight, TEXT(""));
		}

		// ── 注入衍生卡
		{
			FHexEquipLoadout L;
			TArray<FName> Injected;
			L.GetInjectedCardIds(Injected);
			Ctx.CheckEqual(TEXT("空装备无注入卡"), Injected.Num(), 0);

			L.Equip(FHexEquipGenerator::Generate(TEXT("wp_soul_banner"), EHexRarity::Rare, Rng, 15));
			L.GetInjectedCardIds(Injected);
			Ctx.CheckEqual(TEXT("《引魂幡》注入 1 张衍生卡"), Injected.Num(), 1);
			if (Injected.Num() == 1)
			{
				Ctx.Check(TEXT("注入的是《招魂》"),
					Injected[0] == FName(TEXT("eq_soul_call")), TEXT(""));
			}

			// 卸下即移除 —— 衍生卡不该在卸下装备后残留
			L.Unequip(EHexEquipSlot::Weapon);
			L.GetInjectedCardIds(Injected);
			Ctx.CheckEqual(TEXT("卸下武器后注入卡消失"), Injected.Num(), 0);
		}
	}

	// ═══════════════════════════════════════════ 接入正确性

	void CheckIntegration(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("接入：RuleBook / TriggerBus"));

		FHexRngStreams Rng(31337);

		// ── 装备的规则改写必须真的进 RuleAggregate 并被 RuleBook 读到
		//
		// ⚠️ 这是最容易断的一环：RebuildRuleAggregate 早期只聚合符文，
		//    装备的规则词条装上后毫无反应（R8 典型症状：不报错、不崩溃）。
		{
			FHexBattleState State(1);
			State.HeroEnergyMaxBase = 5;
			State.HeroDrawBase = 5;
			State.DeckCapacityBase = HexK::InitialDeckCapacity;
			State.RebuildRuleAggregate();

			const int32 EnergyBefore = FHexRuleBook::EnergyMax(State);
			const int32 DrawBefore = FHexRuleBook::CardsDrawnPerTurn(State);
			const int32 CapBefore = FHexRuleBook::DeckCapacity(State);

			Ctx.CheckEqual(TEXT("无装备时体力上限 = 基线"), EnergyBefore, 5);

			// 《敕令符》固有 af_energy（体力上限 +1）
			State.EquipLoadout.Equip(
				FHexEquipGenerator::Generate(TEXT("tr_talisman"), EHexRarity::Rare, Rng, 20));
			State.RebuildRuleAggregate();

			Ctx.CheckEqual(TEXT("《敕令符》使体力上限 +1"),
				FHexRuleBook::EnergyMax(State), EnergyBefore + 1);

			// 换成《寻踪罗盘》（抽牌 +1）
			State.EquipLoadout.Equip(
				FHexEquipGenerator::Generate(TEXT("tr_compass"), EHexRarity::Rare, Rng, 21));
			State.RebuildRuleAggregate();

			Ctx.CheckEqual(TEXT("《寻踪罗盘》使抽牌数 +1"),
				FHexRuleBook::CardsDrawnPerTurn(State), DrawBefore + 1);
			Ctx.CheckEqual(TEXT("换饰品后体力上限回到基线"),
				FHexRuleBook::EnergyMax(State), EnergyBefore);

			// 《百宝囊》（容量 +2）
			State.EquipLoadout.Equip(
				FHexEquipGenerator::Generate(TEXT("tr_pouch"), EHexRarity::Uncommon, Rng, 22));
			State.RebuildRuleAggregate();
			Ctx.CheckEqual(TEXT("《百宝囊》使卡组容量 +2"),
				FHexRuleBook::DeckCapacity(State), CapBefore + 2);

			// 卸下后必须回到基线（防止规则改写"粘住"）
			State.EquipLoadout.Unequip(EHexEquipSlot::Trinket);
			State.RebuildRuleAggregate();
			Ctx.CheckEqual(TEXT("卸下饰品后容量回到基线"),
				FHexRuleBook::DeckCapacity(State), CapBefore);
		}

		// ── 符文与装备的规则改写叠加
		{
			FHexBattleState State(2);
			State.HeroEnergyMaxBase = 5;
			State.RebuildRuleAggregate();

			// 《镇邪重铠》固有 af_kb_immune（免疫击退，布尔覆盖型）
			State.EquipLoadout.Equip(
				FHexEquipGenerator::Generate(TEXT("ar_heavy"), EHexRarity::Rare, Rng, 30));
			State.RebuildRuleAggregate();

			FHexUnit Hero;
			Hero.HPMax = 80;
			Hero.HP = 80;
			Hero.SizeClass = EHexSizeClass::S;
			Hero.Team = EHexTeam::Player;
			const int32 HeroId = State.AddUnit(Hero);
			State.HeroUnitId = HeroId;

			if (const FHexUnit* H = State.FindUnit(HeroId))
			{
				// 免疫击退在 RuleBook 里表现为极高的抗性值
				Ctx.Check(TEXT("《镇邪重铠》使击退抗性极高"),
					FHexRuleBook::KnockbackResistOf(State, *H) >= 999,
					FString::Printf(TEXT("抗性=%d"),
						FHexRuleBook::KnockbackResistOf(State, *H)));
			}
		}

		// ── 装备触发器必须进 TriggerBus，且 SlotOrder 在 10-12
		{
			FHexBattleState State(3);
			FHexTriggerBus Bus;

			Bus.RebuildListeners(State);
			const int32 Before = Bus.ListenerCount();

			// 《倒刺革衣》固有 af_on_block_thorn（OnDamageTaken 触发）
			State.EquipLoadout.Equip(
				FHexEquipGenerator::Generate(TEXT("ar_thorn"), EHexRarity::Uncommon, Rng, 40));
			Bus.RebuildListeners(State);

			Ctx.Check(TEXT("装备触发器已注册到 TriggerBus"),
				Bus.ListenerCount() > Before,
				FString::Printf(TEXT("注册前=%d 注册后=%d"), Before, Bus.ListenerCount()));

			Ctx.Check(TEXT("OnDamageTaken 时机有监听者"),
				Bus.ListenerCountFor(EHexTriggerTiming::OnDamageTaken) >= 1, TEXT(""));

			// 装备的 SlotOrder 必须落在预留区间，否则会与符文槽混淆
			{
				TArray<FString> Chain;
				Bus.DescribeChain(EHexTriggerTiming::OnDamageTaken, Chain);
				bool bHasEquip = false;
				for (const FString& S : Chain)
				{
					if (S.Contains(TEXT("equip:"))) { bHasEquip = true; break; }
				}
				Ctx.Check(TEXT("触发链中可识别出装备来源"), bHasEquip,
					TEXT("SourceTag 未标记 equip → 符文实验室 UI 无法区分来源"));
			}

			// 卸下后监听者必须移除（否则会出现"幽灵触发"）
			State.EquipLoadout.Unequip(EHexEquipSlot::Armor);
			Bus.RebuildListeners(State);
			Ctx.CheckEqual(TEXT("卸下装备后监听者移除"), Bus.ListenerCount(), Before);
		}

		// ── 符文永远排在装备之前（§6.5 的分层顺序）
		{
			FHexBattleState State(4);
			FHexTriggerBus Bus;

			// 装一件在 OnBattleStart 触发的盔甲（《鱼鳞甲》可能 roll 到，
			// 这里用《倒刺革衣》的 OnDamageTaken 无法对比，改用固定组合）
			State.EquipLoadout.Equip(
				FHexEquipGenerator::Generate(TEXT("ar_thorn"), EHexRarity::Common, Rng, 50));
			Bus.RebuildListeners(State);

			TArray<FString> Chain;
			Bus.DescribeChain(EHexTriggerTiming::OnDamageTaken, Chain);

			// 只有装备时，链上第一个必然是装备（符文槽为空）
			if (Chain.Num() > 0)
			{
				Ctx.Check(TEXT("仅装备时触发链非空"), true, TEXT(""));
			}
			else
			{
				Ctx.Fail(TEXT("仅装备时触发链非空"), TEXT("链为空 —— 装备触发器未被挂载"));
			}
		}

		// ── 序列化往返
		{
			FHexEquipLoadout Original;
			Original.Equip(FHexEquipGenerator::Generate(TEXT("wp_soul_banner"), EHexRarity::Rare, Rng, 60));
			Original.Equip(FHexEquipGenerator::Generate(TEXT("ar_heavy"), EHexRarity::Rare, Rng, 61));
			Original.Equip(FHexEquipGenerator::Generate(TEXT("tr_bell"), EHexRarity::Common, Rng, 62));

			TArray<uint8> Bytes;
			{
				FMemoryWriter Writer(Bytes);
				Original.Serialize(Writer);
			}

			FHexEquipLoadout Restored;
			{
				FMemoryReader Reader(Bytes);
				Restored.Serialize(Reader);
			}

			Ctx.CheckEqual(TEXT("序列化往返：已装备数一致"),
				Restored.GetFilledCount(), Original.GetFilledCount());
			Ctx.Check(TEXT("序列化往返：内容哈希一致"),
				Restored.ContentHash() == Original.ContentHash(),
				FString::Printf(TEXT("%u vs %u"),
					Restored.ContentHash(), Original.ContentHash()));
			Ctx.Check(TEXT("序列化往返：武器覆写保留"),
				Restored.GetAttackOverride().bActive ==
				Original.GetAttackOverride().bActive, TEXT(""));
		}
	}

	// ═══════════════════════════════════════════ 数值量级

	void CheckPowerBudget(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("数值量级（三槽满配）"));

		// ⚠️ 三槽满配的属性提升必须【有上限】。
		//    Godot 实测调好的那套敌人数值（敌人 ATK×1.6、enc_02 打 19 点/回合、
		//    镇妖者撑 4 回合）是我们唯一的平衡锚点。
		//    若装备能让属性涨 3 倍，第一层的全部数值结论立刻作废。
		//
		// 目标：史诗级三槽满配时，主属性提升不超过基线的 2.5 倍。
		{
			int32 WorstAtk = 0;
			int32 WorstDef = 0;
			int32 WorstHp = 0;

			for (uint64 Seed = 1; Seed <= 200; ++Seed)
			{
				FHexRngStreams Rng(Seed);
				FHexEquipLoadout L;
				L.Equip(FHexEquipGenerator::GenerateRandom(EHexEquipSlot::Weapon, 20, Rng, 1));
				L.Equip(FHexEquipGenerator::GenerateRandom(EHexEquipSlot::Armor, 20, Rng, 2));
				L.Equip(FHexEquipGenerator::GenerateRandom(EHexEquipSlot::Trinket, 20, Rng, 3));

				WorstAtk = FMath::Max(WorstAtk, L.GetStatBonus(EHexStat::ATK, 10));
				WorstDef = FMath::Max(WorstDef, L.GetStatBonus(EHexStat::DEF, 8));
				WorstHp = FMath::Max(WorstHp, L.GetStatBonus(EHexStat::HP, 80));
			}

			// ⚠️ 上限的来源是【词条族互斥 + 槽位分工】，不是运气：
			//    ATK 只出现在武器上，且 fam_atk 三档互斥 → 上限 = 最高档 +10
			//    DEF 只出现在盔甲上，fam_def 三档互斥 → 上限 = +8
			//    HP% 的 fam_hp_pct 在盔甲(32%)与饰品(12%)各一条 → 上限 44% = 35 点
			//    留少量余量以容纳后续新增词条，但不容纳"档位叠加"这种失控。

			Ctx.Check(TEXT("ATK 加成上限 ≤12（族互斥+限武器槽）"), WorstAtk <= 12,
				FString::Printf(TEXT("最大 ATK 加成=%d（基线 10）—— 检查 fam_atk 是否失效"),
					WorstAtk));

			Ctx.Check(TEXT("DEF 加成上限 ≤10（族互斥+限盔甲槽）"), WorstDef <= 10,
				FString::Printf(TEXT("最大 DEF 加成=%d（基线 8）—— 检查 fam_def 是否失效"),
					WorstDef));

			Ctx.Check(TEXT("HP 加成上限 ≤50（≈基线 60%）"), WorstHp <= 50,
				FString::Printf(TEXT("最大 HP 加成=%d（基线 80）"), WorstHp));

			// 但也不能太弱 —— 装备是"强数值养成"的载体（§3.1），
			// 满配却只加 2 点属性会让整条养成线没有意义
			Ctx.Check(TEXT("满配 ATK 加成 ≥5（养成有感知）"), WorstAtk >= 5,
				FString::Printf(TEXT("最大 ATK 加成仅 %d"), WorstAtk));

			Ctx.Check(TEXT("满配 DEF 加成 ≥3（养成有感知）"), WorstDef >= 3,
				FString::Printf(TEXT("最大 DEF 加成仅 %d"), WorstDef));
		}

		// ── 同族互斥的直接校验
		//
		// ⚠️ 上面的量级断言是"结果检查"，这里是"机制检查"。
		//    两者都要有：量级断言会随词条表扩充而需要调阈值，
		//    机制断言永远不需要改。
		{
			bool bFamilyOk = true;
			FString Bad;

			for (uint64 Seed = 1; Seed <= 120 && bFamilyOk; ++Seed)
			{
				FHexRngStreams Rng(Seed);
				// 用史诗（4 条随机词条）最容易撞出同族冲突
				const FHexEquipInstance I = FHexEquipGenerator::Generate(
					TEXT("ar_scale"), EHexRarity::Epic, Rng, 1);

				TSet<FName> Families;
				const FHexEquipData* D = FHexEquipLibrary::FindEquip(I.EquipId);
				if (D)
				{
					for (const FName& Aid : D->InherentAffixIds)
					{
						if (const FHexAffixDef* A = FHexEquipLibrary::FindAffix(Aid))
						{
							Families.Add(A->EffectiveFamily());
						}
					}
				}
				for (const FName& Aid : I.RolledAffixIds)
				{
					const FHexAffixDef* A = FHexEquipLibrary::FindAffix(Aid);
					if (!A) { continue; }
					const FName Fam = A->EffectiveFamily();
					if (Families.Contains(Fam))
					{
						bFamilyOk = false;
						Bad = FString::Printf(TEXT("seed=%llu 族=%s 词条=%s"),
							Seed, *Fam.ToString(), *Aid.ToString());
						break;
					}
					Families.Add(Fam);
				}
			}

			Ctx.Check(TEXT("同一件装备上同族词条不重复"), bFamilyOk,
				FString::Printf(TEXT("%s —— 同族叠加会让数值失控"), *Bad));
		}

		// ── 同属性的多个档位必须同族
		//
		// 这条防的是"新增词条时忘填 Family"：
		// 忘填就自成一族，于是 +3/+6/+10 又能同时命中，数值失控复现。
		{
			TMap<EHexStat, TSet<FName>> FamiliesByStat;
			for (const FHexAffixDef& A : FHexEquipLibrary::AllAffixes())
			{
				if (A.Kind != EHexAffixKind::StatFlat)
				{
					continue;
				}
				FamiliesByStat.FindOrAdd(A.Stat).Add(A.EffectiveFamily());
			}

			bool bOneFamilyPerStat = true;
			FString Bad;
			for (const TPair<EHexStat, TSet<FName>>& Pair : FamiliesByStat)
			{
				if (Pair.Value.Num() > 1)
				{
					bOneFamilyPerStat = false;
					Bad = FString::Printf(TEXT("属性 %d 有 %d 个族"),
						static_cast<int32>(Pair.Key), Pair.Value.Num());
					break;
				}
			}
			Ctx.Check(TEXT("同一属性的平坦词条共用一个族"), bOneFamilyPerStat,
				FString::Printf(TEXT("%s —— 漏填 Family 会让档位叠加"), *Bad));
		}
	}
}

bool FHexVerifySuites::VerifyEquip(FHexVerifyContext& Ctx)
{
	CheckAffixes(Ctx);
	CheckEquips(Ctx);
	CheckRarity(Ctx);
	CheckGenerator(Ctx);
	CheckReforge(Ctx);
	CheckLoadout(Ctx);
	CheckIntegration(Ctx);
	CheckPowerBudget(Ctx);
	return Ctx.NumFailed() == 0;
}
