// Copyright Hex Spire. All Rights Reserved.
//
// 状态效果 / 牌堆 / 规则书 验证
//
// 这三块合在一个套件里，因为它们都是"数据表 + 一组不变量"的形态，
// 且都没有 Godot 版的完整参考（状态在 Godot 里 burn 是 no-op）。
//
// ⚠️ 状态效果的数值是我自拟的（策划案 §8.9 只给了字段框架与 10 个状态名）。
//    所以这里的断言不只检查"代码是否按注释执行"，
//    还要检查"注释里的设计推导是否自洽"——
//    例如【虚弱】的注释声称"不按层数线性叠加，否则 4 层变免伤"，
//    那就必须有一条断言证明 4 层虚弱确实不免伤。

#include "Verify/HexVerify.h"
#include "Battle/HexStatusData.h"
#include "Battle/HexUnit.h"
#include "Battle/HexRuleBook.h"
#include "Battle/HexBattleState.h"
#include "Battle/HexDamageCalculator.h"
#include "Deck/HexPileManager.h"
#include "Content/HexContentLibrary.h"
#include "Runes/HexRuneLibrary.h"
#include "Rng/HexRngStreams.h"
#include "Core/HexSpireConstants.h"

namespace
{
	FHexUnit MakeTestUnit(int32 InHP = 80, int32 InAtk = 10, int32 InDef = 0)
	{
		FHexUnit U;
		U.Id = 1;
		U.Team = EHexTeam::Player;
		U.HPMax = InHP;
		U.HP = InHP;
		U.ATK = InAtk;
		U.DEF = InDef;
		U.AGI = 0;   // 排除闪避干扰
		U.LUK = 0;
		U.CRIT = 0;
		return U;
	}

	// ═══════════════════════════════════════════ 状态：表结构

	void CheckStatusTable(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("状态表结构"));

		const TArray<FName>& Ids = FHexStatusLibrary::AllIds();

		// 策划案 §8.9 点名的 10 种必备状态，一个都不能少
		const TArray<FName> Required = {
			FHexStatusLibrary::Strength, FHexStatusLibrary::Dexterity,
			FHexStatusLibrary::Weak, FHexStatusLibrary::Vulnerable,
			FHexStatusLibrary::Burn, FHexStatusLibrary::Poison,
			FHexStatusLibrary::Bleed, FHexStatusLibrary::Stun,
			FHexStatusLibrary::Root, FHexStatusLibrary::Barrier,
		};

		for (const FName& R : Required)
		{
			Ctx.Check(FString::Printf(TEXT("必备状态 %s 存在"), *R.ToString()),
				FHexStatusLibrary::Exists(R), TEXT("§8.9 点名的状态缺失"));
		}

		Ctx.Check(TEXT("未知状态 id 返回无害空定义"),
			!FHexStatusLibrary::Exists(TEXT("__no_such_status__")),
			TEXT(""));

		// ⚠️ AllIds 的顺序参与确定性遍历（tick 伤害的结算顺序）。
		//    顺序不稳定会让同 seed 的两次运行产出不同结果。
		{
			const TArray<FName>& Again = FHexStatusLibrary::AllIds();
			bool bSameOrder = Ids.Num() == Again.Num();
			for (int32 I = 0; bSameOrder && I < Ids.Num(); ++I)
			{
				bSameOrder = (Ids[I] == Again[I]);
			}
			Ctx.Check(TEXT("AllIds 顺序稳定"), bSameOrder, TEXT(""));
		}

		// 每个状态都必须真的做一件事
		for (const FName& Id : Ids)
		{
			const FHexStatusDef& D = FHexStatusLibrary::Get(Id);
			const FString N = Id.ToString();

			Ctx.Check(FString::Printf(TEXT("[%s] 有显示名"), *N),
				!D.DisplayName.IsEmpty(), TEXT(""));

			Ctx.Check(FString::Printf(TEXT("[%s] MaxStack > 0"), *N),
				D.MaxStack > 0, FString::Printf(TEXT("MaxStack=%d"), D.MaxStack));

			const bool bDoesSomething =
				D.FlatAtkPerStack != 0 || D.FlatBlockPerStack != 0 ||
				!FMath::IsNearlyZero(D.DamageDealtMult) ||
				!FMath::IsNearlyZero(D.DamageTakenMult) ||
				D.TickDamagePerStack != 0 || D.bSkipTurn || D.bCannotMove ||
				D.DamageOnMovePerStack != 0 || D.bIsAbsorbShield;

			// ⚠️ 【缓迟】是已知的例外：它的注释声称"移动力 -1"，
			//    但 FHexStatusDef 上【没有任何字段承载这个效果】。
			//    Godot 版也是同样状态（chill 是纯占位）。
			//    这里显式记录为已知缺口而非让断言失败 ——
			//    它的载体卡《巨岩之躯》属于御灵者，本版不做（见内容库末尾备忘）。
			if (Id == FHexStatusLibrary::Chill)
			{
				Ctx.Check(TEXT("[chill] 已知缺口：无字段承载移动惩罚"),
					!bDoesSomething,
					TEXT("chill 变成有效果了 —— 请同步更新此断言与文档"));
				continue;
			}

			Ctx.Check(FString::Printf(TEXT("[%s] 至少有一种实际效果"), *N),
				bDoesSomething, TEXT("空状态 —— 施加后毫无变化"));
		}
	}

	// ═══════════════════════════════════════════ 状态：叠加语义

	void CheckStatusStacking(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("状态叠加语义"));

		// ── StackIntensity：层数累加
		{
			FHexUnit U = MakeTestUnit();
			Ctx.CheckEqual(TEXT("首次施加力量 2 层"),
				U.ApplyStatus(FHexStatusLibrary::Strength, 2), 2);
			Ctx.CheckEqual(TEXT("再施加 3 层 → 5 层"),
				U.ApplyStatus(FHexStatusLibrary::Strength, 3), 5);
			Ctx.CheckEqual(TEXT("查询层数 = 5"),
				U.GetStatusStacks(FHexStatusLibrary::Strength), 5);
		}

		// ── MaxStack 上限
		{
			FHexUnit U = MakeTestUnit();
			U.ApplyStatus(FHexStatusLibrary::Weak, 99);
			// ⚠️ 虚弱上限 10 层 —— 防"永久虚弱锁"
			Ctx.CheckEqual(TEXT("虚弱层数被夹到上限 10"),
				U.GetStatusStacks(FHexStatusLibrary::Weak), 10);
		}

		// ── RefreshDuration：眩晕不叠层
		//
		// ⚠️ 这是全表最重要的一条设计约束。
		//    单玩家单位的游戏里，可叠层的眩晕 = 单方面处刑：
		//    敌人眩晕玩家 3 回合，玩家什么都做不了，纯挫败。
		{
			FHexUnit U = MakeTestUnit();
			U.ApplyStatus(FHexStatusLibrary::Stun, 1);
			U.ApplyStatus(FHexStatusLibrary::Stun, 1);
			U.ApplyStatus(FHexStatusLibrary::Stun, 1);
			Ctx.CheckEqual(TEXT("眩晕三次施加仍为 1 层（不可叠层）"),
				U.GetStatusStacks(FHexStatusLibrary::Stun), 1);

			// 一次施加大量层数也不行
			FHexUnit U2 = MakeTestUnit();
			U2.ApplyStatus(FHexStatusLibrary::Stun, 5);
			Ctx.CheckEqual(TEXT("眩晕单次施加 5 层被夹到 1"),
				U2.GetStatusStacks(FHexStatusLibrary::Stun), 1);

			Ctx.Check(TEXT("眩晕使单位跳过行动"), U.ShouldSkipTurn(), TEXT(""));
		}

		// ── 定身：不可移动但仍可行动
		{
			FHexUnit U = MakeTestUnit();
			U.ApplyStatus(FHexStatusLibrary::Root, 1);
			Ctx.Check(TEXT("定身使单位不可移动"), U.IsMovementBlocked(), TEXT(""));
			// ⚠️ 定身比眩晕弱，所以【不】剥夺行动 —— 否则两者没有区别
			Ctx.Check(TEXT("定身不剥夺行动（比眩晕弱）"),
				!U.ShouldSkipTurn(),
				TEXT("定身剥夺了行动 → 与眩晕重复，失去设计价值"));
			Ctx.CheckEqual(TEXT("定身上限 2 层"),
				FHexStatusLibrary::Get(FHexStatusLibrary::Root).MaxStack, 2);
		}

		// ── 非法输入
		{
			FHexUnit U = MakeTestUnit();
			Ctx.CheckEqual(TEXT("施加 0 层无效"),
				U.ApplyStatus(FHexStatusLibrary::Burn, 0), 0);
			Ctx.CheckEqual(TEXT("施加负层无效"),
				U.ApplyStatus(FHexStatusLibrary::Burn, -5), 0);
			Ctx.CheckEqual(TEXT("施加未知状态无效"),
				U.ApplyStatus(TEXT("__nope__"), 3), 0);
			Ctx.Check(TEXT("非法施加不产生状态实例"),
				U.Statuses.Num() == 0, TEXT(""));
		}

		// ── 衰减
		{
			FHexUnit U = MakeTestUnit();
			U.ApplyStatus(FHexStatusLibrary::Burn, 3);
			U.ApplyStatus(FHexStatusLibrary::Strength, 2);

			TArray<FName> Expired;
			U.DecayStatuses(Expired);
			Ctx.CheckEqual(TEXT("燃烧衰减 3 → 2"),
				U.GetStatusStacks(FHexStatusLibrary::Burn), 2);
			// ⚠️ 力量不衰减 —— 它是"滚雪球"型增益
			Ctx.CheckEqual(TEXT("力量不衰减（仍 2 层）"),
				U.GetStatusStacks(FHexStatusLibrary::Strength), 2);

			U.DecayStatuses(Expired);
			U.DecayStatuses(Expired);
			Ctx.Check(TEXT("燃烧 3 层经 3 回合过期"),
				!U.HasStatus(FHexStatusLibrary::Burn), TEXT(""));
			Ctx.Check(TEXT("过期状态被报告"),
				Expired.Contains(FHexStatusLibrary::Burn), TEXT(""));
			Ctx.Check(TEXT("力量仍在"),
				U.HasStatus(FHexStatusLibrary::Strength), TEXT(""));
		}

		// ── 净化只移除减益
		{
			FHexUnit U = MakeTestUnit();
			U.ApplyStatus(FHexStatusLibrary::Strength, 2);
			U.ApplyStatus(FHexStatusLibrary::Dexterity, 2);
			U.ApplyStatus(FHexStatusLibrary::Weak, 3);
			U.ApplyStatus(FHexStatusLibrary::Burn, 3);

			U.RemoveAllDebuffs();

			Ctx.Check(TEXT("净化保留力量"), U.HasStatus(FHexStatusLibrary::Strength), TEXT(""));
			Ctx.Check(TEXT("净化保留敏锐"), U.HasStatus(FHexStatusLibrary::Dexterity), TEXT(""));
			Ctx.Check(TEXT("净化移除虚弱"), !U.HasStatus(FHexStatusLibrary::Weak), TEXT(""));
			Ctx.Check(TEXT("净化移除燃烧"), !U.HasStatus(FHexStatusLibrary::Burn), TEXT(""));
		}

		// ── 状态数组顺序确定
		//
		// ⚠️ tick 伤害按数组顺序结算，顺序漂移会让同 seed 结果不同。
		{
			FHexUnit A = MakeTestUnit();
			A.ApplyStatus(FHexStatusLibrary::Burn, 1);
			A.ApplyStatus(FHexStatusLibrary::Poison, 1);
			A.ApplyStatus(FHexStatusLibrary::Bleed, 1);

			FHexUnit B = MakeTestUnit();
			// 反序施加
			B.ApplyStatus(FHexStatusLibrary::Bleed, 1);
			B.ApplyStatus(FHexStatusLibrary::Poison, 1);
			B.ApplyStatus(FHexStatusLibrary::Burn, 1);

			bool bSame = A.Statuses.Num() == B.Statuses.Num();
			for (int32 I = 0; bSame && I < A.Statuses.Num(); ++I)
			{
				bSame = (A.Statuses[I].Id == B.Statuses[I].Id);
			}
			Ctx.Check(TEXT("状态数组顺序与施加顺序无关（确定性）"), bSame,
				TEXT("顺序依赖插入顺序 → tick 结算顺序漂移"));
		}
	}

	// ═══════════════════════════════════════════ 状态：数值后果

	void CheckStatusEffects(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("状态数值后果"));

		// ── 力量：平坦加攻 3/层
		{
			FHexUnit U = MakeTestUnit();
			U.ApplyStatus(FHexStatusLibrary::Strength, 3);
			Ctx.CheckEqual(TEXT("力量 3 层 = +9 平坦攻"), U.GetFlatAtkBonus(), 9);

			// 注释声称"3 层力量 ≈ +90% 输出"（ATK 10 → 19）
			FHexUnit Target = MakeTestUnit(9999, 0, 0);
			FHexDamageContext C;
			C.Source = &U;
			C.Target = &Target;
			C.StatRef = TEXT("ATK");
			C.StatRatio = 1.0f;
			Ctx.CheckEqual(TEXT("力量 3 层《攻击》10 → 19（+90%）"),
				FHexDamageCalculator::Preview(C).X, 19);
		}

		// ── 敏锐：平坦加格挡 2/层
		{
			FHexUnit U = MakeTestUnit(80, 10, 8);
			U.ApplyStatus(FHexStatusLibrary::Dexterity, 4);
			Ctx.CheckEqual(TEXT("敏锐 4 层 = +8 平坦格挡"), U.GetFlatBlockBonus(), 8);

			// ⚠️ 注释声称"每层 +2 而非 +3，否则 7 层就撞 25% maxHP 上限"。
			//    验证这个推导：镇妖者上限 20 点，敏锐 10 层 = +20 —— 正好撞线。
			Ctx.CheckEqual(TEXT("格挡上限 20（25% of 80）"), U.GetBlockCap(), 20);
			FHexUnit Dex10 = MakeTestUnit(80, 10, 8);
			Dex10.ApplyStatus(FHexStatusLibrary::Dexterity, 10);
			Ctx.Check(TEXT("敏锐 10 层的平坦加成才追平格挡上限"),
				Dex10.GetFlatBlockBonus() >= Dex10.GetBlockCap(),
				FString::Printf(TEXT("加成=%d 上限=%d"),
					Dex10.GetFlatBlockBonus(), Dex10.GetBlockCap()));
		}

		// ── 虚弱：不按层数线性叠加
		//
		// ⚠️ 这是 HexStatusData.cpp 注释里的核心声明：
		//    "若按层线性叠加，4 层就变成免伤，破坏永不免伤原则（§4.4 ⑤）"。
		//    必须有断言证明这个声明成立，否则注释只是一句愿望。
		{
			FHexUnit W1 = MakeTestUnit();
			W1.ApplyStatus(FHexStatusLibrary::Weak, 1);
			FHexUnit W4 = MakeTestUnit();
			W4.ApplyStatus(FHexStatusLibrary::Weak, 4);
			FHexUnit W10 = MakeTestUnit();
			W10.ApplyStatus(FHexStatusLibrary::Weak, 10);

			Ctx.CheckNearlyEqual(TEXT("虚弱 1 层乘区 = -0.25"),
				W1.GetDamageDealtMultiplier(), -0.25f);
			// 关键：4 层与 1 层的乘区【相同】
			Ctx.CheckNearlyEqual(TEXT("虚弱 4 层乘区仍 = -0.25（不叠加）"),
				W4.GetDamageDealtMultiplier(), -0.25f);
			Ctx.CheckNearlyEqual(TEXT("虚弱 10 层乘区仍 = -0.25"),
				W10.GetDamageDealtMultiplier(), -0.25f);

			// 端到端：10 层虚弱仍能造成伤害（永不免伤）
			FHexUnit Target = MakeTestUnit(9999, 0, 0);
			FHexDamageContext C;
			C.Source = &W10;
			C.Target = &Target;
			C.StatRef = TEXT("ATK");
			C.StatRatio = 1.0f;
			const int32 Dmg = FHexDamageCalculator::Preview(C).X;
			Ctx.Check(TEXT("虚弱 10 层仍造成伤害（永不免伤）"),
				Dmg >= HexK::MinDamage,
				FString::Printf(TEXT("伤害=%d"), Dmg));
			Ctx.CheckEqual(TEXT("虚弱 10 层伤害 = 7（10×0.75）"), Dmg, 7);
		}

		// ── 易伤：50% > 虚弱 25%
		{
			FHexUnit V = MakeTestUnit();
			V.ApplyStatus(FHexStatusLibrary::Vulnerable, 1);
			Ctx.CheckNearlyEqual(TEXT("易伤 1 层乘区 = +0.50"),
				V.GetDamageTakenMultiplier(), 0.5f);

			// ⚠️ 注释声称"进攻性 debuff 应比防御性更有回报，
			//    这样先上易伤再爆发成为真实连招思路"。验证这个不等式。
			FHexUnit W = MakeTestUnit();
			W.ApplyStatus(FHexStatusLibrary::Weak, 1);
			Ctx.Check(TEXT("易伤强度 > 虚弱强度（进攻性回报更高）"),
				FMath::Abs(V.GetDamageTakenMultiplier())
				> FMath::Abs(W.GetDamageDealtMultiplier()),
				TEXT(""));
		}

		// ── 护盾：独立吸收池
		{
			FHexUnit U = MakeTestUnit();
			U.ApplyStatus(FHexStatusLibrary::Barrier, 10);
			Ctx.CheckEqual(TEXT("护盾吸收量 = 10"), U.GetBarrierAmount(), 10);

			Ctx.CheckEqual(TEXT("消耗 4 点护盾"), U.ConsumeBarrier(4), 4);
			Ctx.CheckEqual(TEXT("护盾剩余 6"), U.GetBarrierAmount(), 6);

			// 超额消耗只返回实际吸收量
			Ctx.CheckEqual(TEXT("超额消耗只吸收剩余 6"), U.ConsumeBarrier(99), 6);
			Ctx.CheckEqual(TEXT("护盾耗尽"), U.GetBarrierAmount(), 0);

			// ⚠️ 护盾不随回合衰减 —— 它是"存下来的资源"，
			//    与每回合清空的 block 语义不同。
			FHexUnit B = MakeTestUnit();
			B.ApplyStatus(FHexStatusLibrary::Barrier, 5);
			TArray<FName> Expired;
			B.DecayStatuses(Expired);
			B.DecayStatuses(Expired);
			Ctx.CheckEqual(TEXT("护盾经 2 回合不衰减"), B.GetBarrierAmount(), 5);
		}

		// ── 燃烧 vs 中毒：格挡差异是中毒存在的唯一理由
		{
			const FHexStatusDef& Burn = FHexStatusLibrary::Get(FHexStatusLibrary::Burn);
			const FHexStatusDef& Poison = FHexStatusLibrary::Get(FHexStatusLibrary::Poison);

			Ctx.CheckEqual(TEXT("燃烧 tick 2/层"), Burn.TickDamagePerStack, 2);
			Ctx.CheckEqual(TEXT("中毒 tick 1/层"), Poison.TickDamagePerStack, 1);

			Ctx.Check(TEXT("燃烧可被格挡"), !Burn.bTickIgnoresBlock, TEXT(""));
			// ⚠️ 若中毒也能被格挡，它就是燃烧的弱化版，没有设计价值
			Ctx.Check(TEXT("中毒无视格挡（它存在的唯一理由）"),
				Poison.bTickIgnoresBlock,
				TEXT("中毒可被格挡 → 它变成燃烧的弱化版，应删除或重设计"));

			Ctx.Check(TEXT("两者都在回合结束 tick"),
				Burn.TickTiming == EHexStatusTick::RoundEnd
				&& Poison.TickTiming == EHexStatusTick::RoundEnd, TEXT(""));

			// 《点燃》3 层燃烧的总伤害推导：3+2+1 = 6 层·回合 × 2 = 12 点
			{
				FHexUnit U = MakeTestUnit();
				U.ApplyStatus(FHexStatusLibrary::Burn, 3);
				int32 Total = 0;
				TArray<FName> Expired;
				for (int32 R = 0; R < 5; ++R)
				{
					Total += U.GetStatusStacks(FHexStatusLibrary::Burn)
						* Burn.TickDamagePerStack;
					U.DecayStatuses(Expired);
				}
				Ctx.CheckEqual(TEXT("《点燃》3 层燃烧总伤害 = 12"), Total, 12);
			}
		}

		// ── 流血：移动时受伤
		{
			const FHexStatusDef& Bleed = FHexStatusLibrary::Get(FHexStatusLibrary::Bleed);
			Ctx.CheckEqual(TEXT("流血移动伤害 2/层"), Bleed.DamageOnMovePerStack, 2);
			// ⚠️ 流血【不按回合 tick】—— 它只在移动时结算，
			//    否则它就变成另一个燃烧
			Ctx.Check(TEXT("流血不按回合 tick"),
				Bleed.TickTiming == EHexStatusTick::None,
				TEXT("流血按回合 tick → 与燃烧重复"));
		}
	}

	// ═══════════════════════════════════════════ 牌堆四区

	void CheckPiles(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("牌堆四区"));

		const FHexHeroData* Hero = FHexContentLibrary::FindHero(TEXT("warden"));
		if (!Hero)
		{
			Ctx.Fail(TEXT("牌堆测试前置条件"), TEXT("找不到镇妖者"));
			return;
		}

		TArray<FHexCardInstance> Deck;
		TArray<FHexCardInstance> Fixed;
		FHexContentLibrary::BuildStartingDeck(*Hero, Deck, Fixed);

		// ── 不变量：四区之和 = 全集
		//
		// ⚠️ 这条不变量能抓住绝大多数牌堆 bug（丢牌、复制牌、幽灵牌），
		//    所以在每一步操作后都要查一遍。
		auto CheckInv = [&Ctx](FHexPileManager& P, const FString& When)
		{
			FString Err;
			Ctx.Check(FString::Printf(TEXT("不变量：%s"), *When),
				P.CheckInvariants(Err), Err);
		};

		{
			FHexRngStreams Rng(1);
			FHexPileManager P;
			P.BeginBattle(Deck, Rng);

			CheckInv(P, TEXT("战斗开始"));
			Ctx.CheckEqual(TEXT("开局全部在抽牌堆"), P.NumDraw(), Deck.Num());
			Ctx.CheckEqual(TEXT("开局手牌为空"), P.NumHand(), 0);
			Ctx.CheckEqual(TEXT("开局弃牌堆为空"), P.NumDiscard(), 0);
			Ctx.CheckEqual(TEXT("开局消耗区为空"), P.NumExhaust(), 0);

			// ── 抽牌
			TArray<FHexCardInstance> Drawn;
			bool bReshuffled = false;
			const int32 N = P.Draw(5, HexK::HandLimit, Rng, Drawn, bReshuffled);

			Ctx.CheckEqual(TEXT("抽 5 张成功"), N, 5);
			Ctx.CheckEqual(TEXT("手牌 5 张"), P.NumHand(), 5);
			Ctx.CheckEqual(TEXT("抽牌堆减少 5"), P.NumDraw(), Deck.Num() - 5);
			Ctx.Check(TEXT("首次抽牌不触发洗回"), !bReshuffled, TEXT(""));
			CheckInv(P, TEXT("抽牌后"));

			// ── 打出一张 → 弃牌堆
			const int32 Uid = P.GetHand()[0].Uid;
			Ctx.Check(TEXT("手牌包含该 uid"), P.HandContains(Uid), TEXT(""));
			Ctx.Check(TEXT("打出成功"), P.ResolvePlayedCard(Uid, false), TEXT(""));
			Ctx.CheckEqual(TEXT("手牌 -1"), P.NumHand(), 4);
			Ctx.CheckEqual(TEXT("弃牌堆 +1"), P.NumDiscard(), 1);
			Ctx.Check(TEXT("已打出的卡不在手牌"), !P.HandContains(Uid), TEXT(""));
			CheckInv(P, TEXT("打出后"));

			// ── 重复打出同一张必须失败
			Ctx.Check(TEXT("重复打出失败"), !P.ResolvePlayedCard(Uid, false),
				TEXT("重复打出成功 → 会产生复制牌"));

			// ── 消耗 → 消耗区
			const int32 ExhaustUid = P.GetHand()[0].Uid;
			P.ResolvePlayedCard(ExhaustUid, true);
			Ctx.CheckEqual(TEXT("消耗区 +1"), P.NumExhaust(), 1);
			Ctx.CheckEqual(TEXT("弃牌堆不变"), P.NumDiscard(), 1);
			CheckInv(P, TEXT("消耗后"));

			// ── 回合结束弃手牌
			TArray<FHexCardInstance> Discarded;
			const int32 HandBefore = P.NumHand();
			P.DiscardHand(Discarded);
			Ctx.CheckEqual(TEXT("弃手牌数量正确"), Discarded.Num(), HandBefore);
			Ctx.CheckEqual(TEXT("手牌清空"), P.NumHand(), 0);
			CheckInv(P, TEXT("弃手牌后"));

			// ── 战斗结束归还全集
			TArray<FHexCardInstance> Returned;
			P.EndBattle(Returned);
			// ⚠️ 消耗区的卡也要归还 —— 【消耗】只在本场战斗生效，
			//    不该永久移出卡组（那是"移除"，是另一个机制）
			Ctx.CheckEqual(TEXT("战斗结束归还全部卡（含消耗区）"),
				Returned.Num(), Deck.Num());
		}

		// ── 抽牌堆耗尽 → 洗回
		{
			FHexRngStreams Rng(2);
			FHexPileManager P;
			P.BeginBattle(Deck, Rng);

			// 把所有卡抽出来再全部弃掉
			TArray<FHexCardInstance> Drawn;
			bool bReshuffled = false;
			P.Draw(Deck.Num(), 99, Rng, Drawn, bReshuffled);
			TArray<FHexCardInstance> Discarded;
			P.DiscardHand(Discarded);

			Ctx.CheckEqual(TEXT("抽牌堆已空"), P.NumDraw(), 0);
			Ctx.CheckEqual(TEXT("弃牌堆有全部卡"), P.NumDiscard(), Deck.Num());

			// 再抽 → 必须洗回
			bReshuffled = false;
			const int32 N = P.Draw(3, HexK::HandLimit, Rng, Drawn, bReshuffled);
			Ctx.CheckEqual(TEXT("洗回后抽到 3 张"), N, 3);
			// ⚠️ OnDeckReshuffled 是高频符文钩子（§7.4.4：小卡组约 2 回合一轮），
			//    调用方靠这个返回值决定是否 emit。漏报会让符文静默失效。
			Ctx.Check(TEXT("洗回被正确报告"), bReshuffled,
				TEXT("洗回未报告 → OnDeckReshuffled 类符文静默失效"));
			CheckInv(P, TEXT("洗回后"));
		}

		// ── 两堆皆空：停止抽牌但不报错
		{
			FHexRngStreams Rng(3);
			FHexPileManager P;
			P.BeginBattle(Deck, Rng);

			TArray<FHexCardInstance> Drawn;
			bool bReshuffled = false;
			P.Draw(Deck.Num(), 99, Rng, Drawn, bReshuffled);

			// 手牌持有全部卡，抽牌堆与弃牌堆都空
			const int32 N = P.Draw(5, 99, Rng, Drawn, bReshuffled);
			Ctx.CheckEqual(TEXT("无牌可抽时返回 0（不报错）"), N, 0);
			CheckInv(P, TEXT("无牌可抽后"));
		}

		// ── 手牌上限
		{
			FHexRngStreams Rng(4);
			FHexPileManager P;
			P.BeginBattle(Deck, Rng);

			TArray<FHexCardInstance> Drawn;
			bool bReshuffled = false;
			const int32 N = P.Draw(8, 3, Rng, Drawn, bReshuffled);

			Ctx.CheckEqual(TEXT("手牌上限 3 时只抽到 3 张"), N, 3);
			Ctx.CheckEqual(TEXT("手牌 = 3"), P.NumHand(), 3);
			// ⚠️ 多余的牌【留在抽牌堆】而非被弃掉 ——
			//    被弃掉会让玩家的卡组循环凭空加速（§7.4.2）
			Ctx.CheckEqual(TEXT("多余的牌留在抽牌堆"),
				P.NumDraw(), Deck.Num() - 3);
			Ctx.CheckEqual(TEXT("未产生弃牌"), P.NumDiscard(), 0);
			CheckInv(P, TEXT("撞手牌上限后"));
		}

		// ── 洗牌确定性与随机性
		{
			FHexRngStreams A(777);
			FHexRngStreams B(777);
			FHexPileManager PA, PB;
			PA.BeginBattle(Deck, A);
			PB.BeginBattle(Deck, B);
			Ctx.Check(TEXT("同 seed 洗牌结果一致"),
				PA.ContentHash() == PB.ContentHash(),
				FString::Printf(TEXT("%u vs %u"), PA.ContentHash(), PB.ContentHash()));

			TSet<uint32> Hashes;
			for (uint64 Seed = 1; Seed <= 30; ++Seed)
			{
				FHexRngStreams Rng(Seed);
				FHexPileManager P;
				P.BeginBattle(Deck, Rng);
				Hashes.Add(P.ContentHash());
			}
			Ctx.Check(TEXT("30 个 seed 产出 ≥20 种洗牌结果"),
				Hashes.Num() >= 20,
				FString::Printf(TEXT("仅 %d 种 —— 洗牌可能有偏"), Hashes.Num()));
		}

		// ── 置于牌堆顶
		{
			FHexRngStreams Rng(5);
			FHexPileManager P;
			P.BeginBattle(Deck, Rng);

			FHexCardInstance Special;
			Special.Uid = 9999;
			Special.CardId = TEXT("heavy_strike");
			P.PutOnTopOfDraw(Special);

			TArray<FHexCardInstance> Drawn;
			bool bReshuffled = false;
			P.Draw(1, 99, Rng, Drawn, bReshuffled);

			Ctx.Check(TEXT("置顶的卡最先被抽到"),
				Drawn.Num() == 1 && Drawn[0].Uid == 9999,
				TEXT("置顶无效 → PutCardOnTop 类效果失效"));
		}

		// ── 序列化往返
		{
			FHexRngStreams Rng(6);
			FHexPileManager P;
			P.BeginBattle(Deck, Rng);
			TArray<FHexCardInstance> Drawn;
			bool bReshuffled = false;
			P.Draw(5, 99, Rng, Drawn, bReshuffled);
			P.ResolvePlayedCard(P.GetHand()[0].Uid, false);

			TArray<uint8> Bytes;
			{
				FMemoryWriter W(Bytes);
				P.Serialize(W);
			}
			FHexPileManager R;
			{
				FMemoryReader Rd(Bytes);
				R.Serialize(Rd);
			}

			Ctx.CheckEqual(TEXT("序列化：抽牌堆"), R.NumDraw(), P.NumDraw());
			Ctx.CheckEqual(TEXT("序列化：手牌"), R.NumHand(), P.NumHand());
			Ctx.CheckEqual(TEXT("序列化：弃牌堆"), R.NumDiscard(), P.NumDiscard());
			Ctx.Check(TEXT("序列化：哈希一致"),
				R.ContentHash() == P.ContentHash(), TEXT(""));
			CheckInv(R, TEXT("序列化还原后"));
		}
	}

	// ═══════════════════════════════════════════ 规则书

	void CheckRuleBook(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("规则书（唯一消费点）"));

		// ⚠️ 策划案 §15.6：每条 GameRule 只能有一个消费点。
		//    新增 GameRule 却忘记加消费函数时，符文改写会静默失效 ——
		//    这正是 R8 的典型症状。
		{
			TArray<EHexGameRule> Missing;
			const bool bOk = FHexRuleBook::VerifyAllRulesHaveConsumer(Missing);

			FString MissingNames;
			for (const EHexGameRule R : Missing)
			{
				MissingNames += FString::Printf(TEXT("%d "), static_cast<int32>(R));
			}
			Ctx.Check(TEXT("每条 GameRule 都有消费点（§15.6）"), bOk,
				FString::Printf(TEXT("缺失: %s"), *MissingNames));

			Ctx.CheckEqual(TEXT("消费点数量 = 规则总数"),
				FHexRuleBook::Consumers().Num(),
				static_cast<int32>(EHexGameRule::Count));
		}

		// ── 无改写时返回基线
		{
			FHexBattleState S(1);
			S.HeroEnergyMaxBase = 5;
			S.HeroDrawBase = 5;
			S.DeckCapacityBase = 8;
			S.RebuildRuleAggregate();

			Ctx.CheckEqual(TEXT("体力上限 = 基线 5"), FHexRuleBook::EnergyMax(S), 5);
			Ctx.CheckEqual(TEXT("抽牌数 = 基线 5"), FHexRuleBook::CardsDrawnPerTurn(S), 5);
			Ctx.CheckEqual(TEXT("手牌上限 = 10"), FHexRuleBook::HandLimit(S), HexK::HandLimit);
			Ctx.CheckEqual(TEXT("卡组容量 = 8"), FHexRuleBook::DeckCapacity(S), 8);
			Ctx.CheckNearlyEqual(TEXT("伤害乘区 = 1.0"), FHexRuleBook::DamageMultiplier(S), 1.0f);
			Ctx.Check(TEXT("默认可获得格挡"), FHexRuleBook::CanGainBlock(S), TEXT(""));
			Ctx.Check(TEXT("默认格挡回合清空"), !FHexRuleBook::BlockPersists(S), TEXT(""));
			Ctx.Check(TEXT("默认首卡不免费"), !FHexRuleBook::IsFirstCardFree(S), TEXT(""));
		}

		// ── 符文改写生效
		{
			FHexBattleState S(2);
			S.HeroEnergyMaxBase = 5;
			S.HeroDrawBase = 5;
			S.DeckCapacityBase = 8;

			// 《薄刃契》：容量 -3、抽牌 +1
			S.RuneLoadout.SetSlot(0, FHexRuneLibrary::FindRune(TEXT("rune_thin_blade")));
			S.RebuildRuleAggregate();

			Ctx.CheckEqual(TEXT("《薄刃契》抽牌 5 → 6"),
				FHexRuleBook::CardsDrawnPerTurn(S), 6);
			Ctx.CheckEqual(TEXT("《薄刃契》容量 8 → 5"),
				FHexRuleBook::DeckCapacity(S), 5);

			// 《铁誓》：体力 +2、无法获得格挡
			S.RuneLoadout.SetSlot(1, FHexRuneLibrary::FindRune(TEXT("rune_iron_vow")));
			S.RebuildRuleAggregate();

			Ctx.CheckEqual(TEXT("《铁誓》体力 5 → 7"), FHexRuleBook::EnergyMax(S), 7);
			Ctx.Check(TEXT("《铁誓》禁止获得格挡"), !FHexRuleBook::CanGainBlock(S), TEXT(""));

			// 《负重咒》：费用 -1、手牌上限 -4
			S.RuneLoadout.SetSlot(2, FHexRuneLibrary::FindRune(TEXT("rune_burden")));
			S.RebuildRuleAggregate();

			Ctx.CheckEqual(TEXT("《负重咒》2 费卡变 1 费"),
				FHexRuleBook::CardCost(S, 2, 1), 1);
			Ctx.CheckEqual(TEXT("《负重咒》手牌上限 10 → 6"),
				FHexRuleBook::HandLimit(S), 6);
			// 费用最低 0，不得为负
			Ctx.CheckEqual(TEXT("1 费卡不会变成负费用"),
				FHexRuleBook::CardCost(S, 1, 1), 0);
		}

		// ── 增量叠加
		//
		// ⚠️ 两个符文都改 EnergyMax 时必须【相加】而非后者吃掉前者。
		//    后者吃掉前者是 R8 的典型症状："我装了两个符文但只生效一个"。
		{
			FHexBattleState S(3);
			S.HeroEnergyMaxBase = 5;

			// 《铁誓》体力 +2、《敕令符》（装备）体力 +1
			S.RuneLoadout.SetSlot(0, FHexRuneLibrary::FindRune(TEXT("rune_iron_vow")));
			FHexRngStreams Rng(1);
			S.EquipLoadout.Equip(
				FHexEquipGenerator::Generate(TEXT("tr_talisman"), EHexRarity::Rare, Rng, 1));
			S.RebuildRuleAggregate();

			Ctx.CheckEqual(TEXT("符文+2 与装备+1 相加 → 8"),
				FHexRuleBook::EnergyMax(S), 8);
		}

		// ── 首卡免费
		{
			FHexBattleState S(4);
			S.RuneLoadout.SetSlot(0, FHexRuneLibrary::FindRune(TEXT("rune_first_free")));
			S.RebuildRuleAggregate();

			Ctx.Check(TEXT("《先手符》启用首卡免费"),
				FHexRuleBook::IsFirstCardFree(S), TEXT(""));
			Ctx.CheckEqual(TEXT("本回合第 1 张牌免费"),
				FHexRuleBook::CardCost(S, 2, /*CardsPlayed=*/0), 0);
			// ⚠️ 只对第 1 张生效，否则等于全免费
			Ctx.CheckEqual(TEXT("第 2 张牌恢复原价"),
				FHexRuleBook::CardCost(S, 2, /*CardsPlayed=*/1), 2);
		}

		// ── 镇妖者被动
		{
			FHexBattleState S(5);
			const FHexHeroData* Hero = FHexContentLibrary::FindHero(TEXT("warden"));
			if (Hero)
			{
				// 被动天赋复用符文的规则改写结构，挂在虚拟槽 0
				// 这里直接把聚合结果塞进去模拟
				for (const FHexRuleOverride& O : Hero->PassiveRules)
				{
					FHexRuneLoadout::FAggregated A;
					A.bFound = true;
					A.bHasOverride = !O.bIsDelta;
					A.bBoolOverride = O.bBoolValue;
					A.IntOverride = O.IntValue;
					S.RuleAggregate.Add(O.Rule, A);
				}

				Ctx.Check(TEXT("镇妖者被动：格挡不清空"),
					FHexRuleBook::BlockPersists(S),
					TEXT("被动未生效 → 镇妖者的核心定位失效"));
			}
		}

		// ── 边界夹取
		{
			FHexBattleState S(6);
			S.DeckCapacityBase = 8;
			// 容量 -3 × 两次（若能装两个《薄刃契》）会到 2，仍需 ≥1
			S.HeroEnergyMaxBase = 0;
			S.RebuildRuleAggregate();

			Ctx.Check(TEXT("体力上限不为负"), FHexRuleBook::EnergyMax(S) >= 0, TEXT(""));
			Ctx.Check(TEXT("手牌上限至少 1"), FHexRuleBook::HandLimit(S) >= 1, TEXT(""));
			Ctx.Check(TEXT("卡组容量至少 1"), FHexRuleBook::DeckCapacity(S) >= 1, TEXT(""));
			Ctx.Check(TEXT("卡组容量不超硬上限"),
				FHexRuleBook::DeckCapacity(S) <= HexK::MaxDeckCapacity, TEXT(""));
		}

		// ── 卸下符文后必须回到基线
		{
			FHexBattleState S(7);
			S.HeroEnergyMaxBase = 5;
			S.RuneLoadout.SetSlot(0, FHexRuneLibrary::FindRune(TEXT("rune_iron_vow")));
			S.RebuildRuleAggregate();
			Ctx.CheckEqual(TEXT("装上《铁誓》体力 = 7"), FHexRuleBook::EnergyMax(S), 7);

			S.RuneLoadout.ClearSlot(0);
			S.RebuildRuleAggregate();
			Ctx.CheckEqual(TEXT("卸下后体力回到 5"), FHexRuleBook::EnergyMax(S), 5);
			Ctx.Check(TEXT("卸下后恢复可获得格挡"), FHexRuleBook::CanGainBlock(S), TEXT(""));
		}
	}
}

bool FHexVerifySuites::VerifyStatus(FHexVerifyContext& Ctx)
{
	CheckStatusTable(Ctx);
	CheckStatusStacking(Ctx);
	CheckStatusEffects(Ctx);
	return Ctx.NumFailed() == 0;
}

bool FHexVerifySuites::VerifyDeck(FHexVerifyContext& Ctx)
{
	CheckPiles(Ctx);
	return Ctx.NumFailed() == 0;
}

bool FHexVerifySuites::VerifyRules(FHexVerifyContext& Ctx)
{
	CheckRuleBook(Ctx);
	return Ctx.NumFailed() == 0;
}
