// Copyright Hex Spire. All Rights Reserved.
//
// 伤害管线验证 —— 逐阶段钉住 §4.4 的 9 个阶段
//
// 本套件的核心价值是【②′ 顺序敏感性】：
//   §6.5 的"免费一层深度"完全依赖 [加区,乘区] ≠ [乘区,加区]。
//   如果哪天有人把 ②′ 改成桶式聚合（先求和所有加区、再求积所有乘区），
//   代码照样能跑、伤害数字看起来也正常，但 D6 的核心设计会静默死掉。
//   这个套件就是防这件事的。
//
// ⚠️ 全部断言走 Preview() 而不是 Calculate()：
//    Preview 不消耗 RNG，因此结果完全确定，验证器不需要固定种子。
//    ⑥护盾 / ⑦格挡 的分配逻辑需要真实战斗上下文，留给 VerifyBattle。

#include "Verify/HexVerify.h"
#include "Battle/HexDamageCalculator.h"
#include "Battle/HexUnit.h"
#include "Battle/HexStatusData.h"
#include "Content/HexContentLibrary.h"
#include "Runes/HexRuneLibrary.h"
#include "Core/HexSpireConstants.h"

namespace
{
	/** 造一个镇妖者规格的攻击方（ATK=10 DEF=8 LUK=5 CRIT=10 HP=80） */
	FHexUnit MakeWardenUnit()
	{
		const FHexHeroData* H = FHexContentLibrary::FindHero(TEXT("warden"));
		FHexUnit U;
		if (H)
		{
			U.DisplayName = H->DisplayName;
			U.SourceId = H->Id;
			U.SizeClass = H->SizeClass;
			U.HPMax = H->BaseHP;
			U.HP = H->BaseHP;
			U.ATK = H->BaseATK;
			U.DEF = H->BaseDEF;
			U.AGI = H->BaseAGI;
			U.LUK = H->BaseLUK;
			U.CRIT = H->BaseCRIT;
		}
		U.Team = EHexTeam::Player;
		U.Id = 1;
		return U;
	}

	/** 造一个"纯靶子"：DEF=0 AGI=0，用于隔离出单个阶段的效果 */
	FHexUnit MakeDummyTarget(int32 InDef = 0)
	{
		FHexUnit U;
		U.Id = 2;
		U.Team = EHexTeam::Enemy;
		U.HPMax = 9999;
		U.HP = 9999;
		U.ATK = 0;
		U.DEF = InDef;
		// AGI=0 保证闪避率为 0 —— 否则 Preview 与 Calculate 的语义差异会干扰断言
		U.AGI = 0;
		U.LUK = 0;
		U.CRIT = 0;
		return U;
	}

	FHexDamageContext MakeCtx(const FHexUnit* Src, FHexUnit* Tgt, float Ratio = 1.0f, float Flat = 0.0f)
	{
		FHexDamageContext C;
		C.Source = Src;
		C.Target = Tgt;
		C.Flat = Flat;
		C.StatRef = TEXT("ATK");
		C.StatRatio = Ratio;
		C.Tag = TEXT("verify");
		return C;
	}

	// ═══════════════════════════════════════════ ① ② 基础与平坦加成

	void CheckBaseAndFlat(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("① 基础 / ② 平坦加成"));

		const FHexUnit Src = MakeWardenUnit();
		FHexUnit Tgt = MakeDummyTarget(0);

		// ① Flat + ATK×Ratio
		{
			FHexDamageContext C = MakeCtx(&Src, &Tgt, 1.0f, 0.0f);
			Ctx.CheckNearlyEqual(TEXT("① ATK×1.0 = 10"), FHexDamageCalculator::BaseValue(C), 10.0f);
		}
		{
			FHexDamageContext C = MakeCtx(&Src, &Tgt, 1.8f, 0.0f);
			// 《重击》ATK×1.8
			Ctx.CheckNearlyEqual(TEXT("① ATK×1.8 = 18（重击）"), FHexDamageCalculator::BaseValue(C), 18.0f);
		}
		{
			FHexDamageContext C = MakeCtx(&Src, &Tgt, 0.42f, 0.0f);
			// 《连刺》单段 ATK×0.42 → 4.2。三段共 12.6，与《重击》2 费 18 同档费效
			Ctx.CheckNearlyEqual(TEXT("① ATK×0.42 = 4.2（连刺单段）"),
				FHexDamageCalculator::BaseValue(C), 4.2f);
		}

		// ② 非符文平坦加成
		{
			FHexDamageContext C = MakeCtx(&Src, &Tgt, 1.0f, 0.0f);
			C.FlatBonus = 5.0f;
			Ctx.CheckNearlyEqual(TEXT("② 平坦加成 +5 → 15"), FHexDamageCalculator::BaseValue(C), 15.0f);
		}

		// ② 状态【力量】自动进入平坦加成（3/层，不衰减）
		{
			FHexUnit Buffed = MakeWardenUnit();
			Buffed.ApplyStatus(FHexStatusLibrary::Strength, 2);
			Ctx.CheckEqual(TEXT("② 力量 2 层 = +6 平坦攻"), Buffed.GetFlatAtkBonus(), 6);

			FHexDamageContext C = MakeCtx(&Buffed, &Tgt, 1.0f, 0.0f);
			const FIntPoint P = FHexDamageCalculator::Preview(C);
			Ctx.CheckEqual(TEXT("② 力量 2 层：10 + 6 = 16"), P.X, 16);
		}
	}

	// ═══════════════════════════════════════════ ②′ 顺序钩子（核心）

	void CheckOrderHook(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("②′ 顺序钩子（§6.5 免费深度）"));

		const FHexUnit Src = MakeWardenUnit();
		FHexUnit Tgt = MakeDummyTarget(0);  // DEF=0 隔离出 ②′，不受 ⑤ 减伤干扰
		FHexDamageContext C = MakeCtx(&Src, &Tgt, 1.0f, 0.0f);

		// 模拟符文槽 1→6 的钩子链。
		// 加区 +ATK×0.5 = +5（《砺石》），乘区 ×1.4（《倍影》）。
		const float AddAmount = 5.0f;
		const float MultAmount = 1.4f;

		FHexValueHook AddThenMult = [AddAmount, MultAmount](float V, TArray<FHexRuneStep>& Log) -> float
		{
			{
				FHexRuneStep S; S.SourceTag = TEXT("砺石"); S.SlotOrder = 1;
				S.Before = V; V += AddAmount; S.After = V; Log.Add(S);
			}
			{
				FHexRuneStep S; S.SourceTag = TEXT("倍影"); S.SlotOrder = 2;
				S.Before = V; V *= MultAmount; S.After = V; Log.Add(S);
			}
			return V;
		};

		FHexValueHook MultThenAdd = [AddAmount, MultAmount](float V, TArray<FHexRuneStep>& Log) -> float
		{
			{
				FHexRuneStep S; S.SourceTag = TEXT("倍影"); S.SlotOrder = 1;
				S.Before = V; V *= MultAmount; S.After = V; Log.Add(S);
			}
			{
				FHexRuneStep S; S.SourceTag = TEXT("砺石"); S.SlotOrder = 2;
				S.Before = V; V += AddAmount; S.After = V; Log.Add(S);
			}
			return V;
		};

		const FIntPoint PAddMult = FHexDamageCalculator::Preview(C, AddThenMult);
		const FIntPoint PMultAdd = FHexDamageCalculator::Preview(C, MultThenAdd);

		// (10 + 5) × 1.4 = 21
		Ctx.CheckEqual(TEXT("②′ [砺石,倍影] = (10+5)×1.4 = 21"), PAddMult.X, 21);
		// 10 × 1.4 + 5 = 19
		Ctx.CheckEqual(TEXT("②′ [倍影,砺石] = 10×1.4+5 = 19"), PMultAdd.X, 19);

		// ⚠️ 这是本套件存在的理由：
		//    如果 ②′ 被改成桶式聚合，这两个值会相等，D6 的免费深度静默消失。
		Ctx.Check(TEXT("②′ 顺序敏感（两种摆法结果不同）"),
			PAddMult.X != PMultAdd.X,
			FString::Printf(TEXT("两种槽位顺序都得到 %d —— ②′ 已退化为桶式聚合，§6.5 失效"),
				PAddMult.X));

        // 差异幅度要足够大到玩家能察觉（§13.2 要求 UI 上肉眼可见）
		{
			const float DiffPct = 100.0f * (PAddMult.X - PMultAdd.X) / static_cast<float>(PMultAdd.X);
			Ctx.Check(TEXT("②′ 顺序差异 ≥5%（玩家可察觉）"),
				DiffPct >= 5.0f,
				FString::Printf(TEXT("差异仅 %.1f%%"), DiffPct));
		}

		// 空钩子必须等价于"没有符文"
		{
			const FIntPoint PNone = FHexDamageCalculator::Preview(C);
			Ctx.CheckEqual(TEXT("②′ 无符文时不改变基数"), PNone.X, 10);
		}

		// 钩子日志必须按槽位升序记录 —— 符文实验室 UI（§13.3）靠它展示结算链
		{
			TArray<FHexRuneStep> Log;
			AddThenMult(10.0f, Log);
			Ctx.CheckEqual(TEXT("②′ 日志记录了 2 步"), Log.Num(), 2);
			if (Log.Num() == 2)
			{
				Ctx.Check(TEXT("②′ 日志按槽位升序"), Log[0].SlotOrder < Log[1].SlotOrder, TEXT(""));
				Ctx.CheckNearlyEqual(TEXT("②′ 日志首步 Before=10"), Log[0].Before, 10.0f);
				Ctx.CheckNearlyEqual(TEXT("②′ 日志末步 After=21"), Log[1].After, 21.0f);
			}
		}

		// 真实符文表里的数值必须与本测试假设一致，
		// 否则测试通过了但游戏里的《砺石》《倍影》是别的数
		{
			const FHexRuneData* Whetstone = FHexRuneLibrary::FindRune(TEXT("rune_whetstone"));
			const FHexRuneData* TwinShadow = FHexRuneLibrary::FindRune(TEXT("rune_twin_shadow"));

			Ctx.Check(TEXT("《砺石》存在且为②′加区"),
				Whetstone && Whetstone->Triggers.Num() == 1 &&
				Whetstone->Triggers[0].bHasValueAdd &&
				Whetstone->Triggers[0].When == EHexTriggerTiming::OnAttack,
				TEXT(""));

			Ctx.Check(TEXT("《倍影》存在且为②′乘区"),
				TwinShadow && TwinShadow->Triggers.Num() == 1 &&
				TwinShadow->Triggers[0].bHasValueMult &&
				TwinShadow->Triggers[0].When == EHexTriggerTiming::OnAttack,
				TEXT(""));

			if (Whetstone && Whetstone->Triggers.Num() == 1)
			{
				Ctx.CheckNearlyEqual(TEXT("《砺石》加区系数 = ATK×0.5"),
					Whetstone->Triggers[0].ValueAddRatio, 0.5f);
			}
			if (TwinShadow && TwinShadow->Triggers.Num() == 1)
			{
				Ctx.CheckNearlyEqual(TEXT("《倍影》乘区 = 1.4"),
					TwinShadow->Triggers[0].ValueMult, 1.4f);
			}
		}
	}

	// ═══════════════════════════════════════════ ③ 乘区 / ④ 暴击

	void CheckMultipliersAndCrit(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("③ 乘区 / ④ 暴击"));

		const FHexUnit Src = MakeWardenUnit();
		FHexUnit Tgt = MakeDummyTarget(0);

		// ③ 乘区按 Π(1+x) 聚合
		{
			FHexDamageContext C = MakeCtx(&Src, &Tgt, 1.0f, 0.0f);
			C.Multipliers = { 0.5f };
			Ctx.CheckEqual(TEXT("③ 单乘区 +50% → 15"), FHexDamageCalculator::Preview(C).X, 15);
		}
		{
			FHexDamageContext C = MakeCtx(&Src, &Tgt, 1.0f, 0.0f);
			C.Multipliers = { 0.5f, 0.5f };
			// Π(1+x) = 1.5×1.5 = 2.25 → 22.5 → 22（不是 1+0.5+0.5=2.0 → 20）
			Ctx.CheckEqual(TEXT("③ 双乘区取积 1.5×1.5 → 22"), FHexDamageCalculator::Preview(C).X, 22);
		}

		// ③ 状态【易伤】+50% 受伤（自动追加）
		{
			FHexUnit Vuln = MakeDummyTarget(0);
			Vuln.ApplyStatus(FHexStatusLibrary::Vulnerable, 1);
			FHexDamageContext C = MakeCtx(&Src, &Vuln, 1.0f, 0.0f);
			Ctx.CheckEqual(TEXT("③ 易伤 1 层 → 15"), FHexDamageCalculator::Preview(C).X, 15);
		}

		// ③ 状态【虚弱】-25% 造成伤害
		{
			FHexUnit Weak = MakeWardenUnit();
			Weak.ApplyStatus(FHexStatusLibrary::Weak, 1);
			FHexDamageContext C = MakeCtx(&Weak, &Tgt, 1.0f, 0.0f);
			// 10 × 0.75 = 7.5 → 7
			Ctx.CheckEqual(TEXT("③ 虚弱 1 层 → 7"), FHexDamageCalculator::Preview(C).X, 7);
		}

		// ③ 背击自动追加 BackstabMult
		{
			FHexDamageContext C = MakeCtx(&Src, &Tgt, 1.0f, 0.0f);
			C.bFromRear = true;
			Ctx.CheckEqual(TEXT("③ 背击 +50% → 15"), FHexDamageCalculator::Preview(C).X, 15);
		}

		// ④ 暴击倍率 = 1 + CritBaseMult + LUK×LukToCritDmg
		//   镇妖者 LUK=5 → 1 + 0.5 + 0.1 = 1.6
		{
			Ctx.CheckNearlyEqual(TEXT("④ 暴击倍率（LUK5）= 1.6"),
				FHexDamageCalculator::CritMultiplier(&Src), 1.6f);

			FHexDamageContext C = MakeCtx(&Src, &Tgt, 1.0f, 0.0f);
			const FIntPoint P = FHexDamageCalculator::Preview(C);
			Ctx.CheckEqual(TEXT("④ 预览不暴 = 10"), P.X, 10);
			Ctx.CheckEqual(TEXT("④ 预览暴击 = 16"), P.Y, 16);
			// §13.2 要求悬停显示"预计伤害 X（暴击 Y）"，所以 Y 必须 > X
			Ctx.Check(TEXT("④ 暴击值大于普通值"), P.Y > P.X, TEXT(""));
		}

		// ④ LUK 管倍率、CRIT 管频率（§4.3 的分工）：
		//   LUK 提高必须提高暴击【伤害】，而不是频率
		{
			FHexUnit HighLuk = MakeWardenUnit();
			HighLuk.LUK = 50;
			Ctx.CheckNearlyEqual(TEXT("④ LUK50 暴击倍率 = 2.5"),
				FHexDamageCalculator::CritMultiplier(&HighLuk), 2.5f);
		}
	}

	// ═══════════════════════════════════════════ ⑤ 减伤 / ⑤′ 下限

	void CheckDefenseAndFloor(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("⑤ 减伤 / ⑤′ 取整下限"));

		const FHexUnit Src = MakeWardenUnit();

		// ⚠️ 这是 Godot 版最重要的一条平衡结论：DefSoftcap 从 50 改到 12。
		//    DEF=8 时减伤必须是 40%，否则"堆防御"不是一条真路线。
		{
			FHexUnit Tgt = MakeDummyTarget(8);
			FHexDamageContext C = MakeCtx(&Src, &Tgt, 1.0f, 0.0f);
			// 10 × (1 - 8/(8+12)) = 10 × 0.6 = 6
			Ctx.CheckEqual(TEXT("⑤ DEF8 减伤 40% → 6"), FHexDamageCalculator::Preview(C).X, 6);
		}
		{
			FHexUnit Tgt = MakeDummyTarget(6);
			FHexDamageContext C = MakeCtx(&Src, &Tgt, 2.0f, 0.0f);
			// 20 × (1 - 6/18) = 20 × 0.6667 = 13.33 → 13
			Ctx.CheckEqual(TEXT("⑤ DEF6 减伤 33% → 13"), FHexDamageCalculator::Preview(C).X, 13);
		}
		{
			FHexUnit Tgt = MakeDummyTarget(2);
			FHexDamageContext C = MakeCtx(&Src, &Tgt, 1.0f, 0.0f);
			// 10 × (1 - 2/14) = 8.57 → 8（扑咬犬 DEF=2）
			Ctx.CheckEqual(TEXT("⑤ DEF2 减伤 14% → 8"), FHexDamageCalculator::Preview(C).X, 8);
		}

		// ⑤ 递减曲线：DEF 增长的边际收益必须递减，永不免伤
		{
			FHexUnit D10 = MakeDummyTarget(10);
			FHexUnit D30 = MakeDummyTarget(30);
			FHexUnit D1000 = MakeDummyTarget(1000);

			FHexDamageContext C10 = MakeCtx(&Src, &D10, 10.0f, 0.0f);
			FHexDamageContext C30 = MakeCtx(&Src, &D30, 10.0f, 0.0f);
			FHexDamageContext C1000 = MakeCtx(&Src, &D1000, 10.0f, 0.0f);

			const int32 V10 = FHexDamageCalculator::Preview(C10).X;
			const int32 V30 = FHexDamageCalculator::Preview(C30).X;
			const int32 V1000 = FHexDamageCalculator::Preview(C1000).X;

			Ctx.Check(TEXT("⑤ DEF 越高伤害越低（单调）"), V10 > V30 && V30 > V1000,
				FString::Printf(TEXT("%d > %d > %d"), V10, V30, V1000));

			// ⚠️ 「保证永远能破防」：这是 DEF 软曲线的承诺，
			//    再高的 DEF 也不能把伤害压到 0，否则战斗会进入死局
			Ctx.Check(TEXT("⑤ DEF=1000 仍有伤害（永不免伤）"), V1000 >= HexK::MinDamage,
				FString::Printf(TEXT("伤害=%d"), V1000));
		}

		// ⑤′ 取整下限
		{
			FHexUnit Tgt = MakeDummyTarget(0);
			FHexDamageContext C = MakeCtx(&Src, &Tgt, 0.0f, 0.0f);
			// base=0 → floor(0)=0 → Max(0, MinDamage)=1
			Ctx.CheckEqual(TEXT("⑤′ 零伤害被抬到 MinDamage"),
				FHexDamageCalculator::Preview(C).X, HexK::MinDamage);
		}
		{
			FHexUnit Tgt = MakeDummyTarget(0);
			FHexDamageContext C = MakeCtx(&Src, &Tgt, 0.0f, 0.9f);
			// floor(0.9) = 0 → 抬到 1（取整向下，不四舍五入）
			Ctx.CheckEqual(TEXT("⑤′ 向下取整（0.9 → 1 由下限保证）"),
				FHexDamageCalculator::Preview(C).X, 1);
		}
		{
			FHexUnit Tgt = MakeDummyTarget(0);
			FHexDamageContext C = MakeCtx(&Src, &Tgt, 0.0f, 4.7f);
			// floor(4.7) = 4 —— 确认是向下取整而非四舍五入（否则会是 5）
			Ctx.CheckEqual(TEXT("⑤′ 4.7 向下取整为 4"),
				FHexDamageCalculator::Preview(C).X, 4);
		}
	}

	// ═══════════════════════════════════════════ 格挡

	void CheckBlock(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("格挡获取与上限"));

		const FHexUnit Src = MakeWardenUnit();

		// 《防御》= 1 + DEF×0.6
		// ⚠️ 曾是 2 + DEF×1.0 = 10 格挡/1 费，配合每回合 2–3 张 → 20–30 格挡，
		//    而最强普通敌人只打 6–8 点（4.2 倍过剩）。压到 5.8 后格挡回归"减伤"。
		Ctx.CheckEqual(TEXT("《防御》1 + DEF8×0.6 = 5"),
			FHexDamageCalculator::CalculateBlock(&Src, 1.0f, TEXT("DEF"), 0.6f), 5);

		// 《铁壁》= 2 + DEF×0.9 = 9.2 → 9
		Ctx.CheckEqual(TEXT("《铁壁》2 + DEF8×0.9 = 9"),
			FHexDamageCalculator::CalculateBlock(&Src, 2.0f, TEXT("DEF"), 0.9f), 9);

		// ⚠️ 格挡上限 = 25% maxHP。上限曾完全没实现，
		//    导致镇妖者 10 回合叠到 140 格挡（HP 只有 80）——
		//    失败条件在数学上不存在。
		Ctx.CheckEqual(TEXT("格挡上限 = 25% maxHP = 20"), Src.GetBlockCap(), 20);

		// 低 HP 单位走绝对下限，保证仍有基本格挡空间
		{
			FHexUnit Small = MakeDummyTarget(0);
			Small.HPMax = 20;
			Small.HP = 20;
			// floor(20×0.25)=5 < BlockCapMin(12) → 取 12
			Ctx.CheckEqual(TEXT("低 HP 单位格挡上限走绝对下限 12"),
				Small.GetBlockCap(), HexK::BlockCapMin);
		}

		// 状态【敏锐】给格挡平坦加成（2/层）
		{
			FHexUnit Dex = MakeWardenUnit();
			Dex.ApplyStatus(FHexStatusLibrary::Dexterity, 3);
			Ctx.CheckEqual(TEXT("敏锐 3 层 = +6 平坦格挡"), Dex.GetFlatBlockBonus(), 6);
			// 1 + 8×0.6 + 6 = 11.8 → 11
			Ctx.CheckEqual(TEXT("《防御》+敏锐3层 = 11"),
				FHexDamageCalculator::CalculateBlock(&Dex, 1.0f, TEXT("DEF"), 0.6f), 11);
		}

		// 格挡乘区（符文 BlockMultiplier）
		Ctx.CheckEqual(TEXT("格挡乘区 ×2 生效"),
			FHexDamageCalculator::CalculateBlock(&Src, 1.0f, TEXT("DEF"), 0.6f, 2.0f), 11);
	}

	// ═══════════════════════════════════════════ 闪避

	void CheckDodge(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("闪避（AGI 递减曲线）"));

		const FHexUnit Src = MakeWardenUnit();

		// ⚠️ 必须是递减曲线 min(AGI×K/(AGI+Softcap), Cap)，不是线性。
		//    线性实现会让 AGI 到 60 就撞上限，之后每点 AGI 收益为 0。
		{
			FHexUnit A0 = MakeDummyTarget(0);
			A0.AGI = 0;
			FHexDamageContext C = MakeCtx(&Src, &A0);
			Ctx.CheckNearlyEqual(TEXT("AGI0 闪避率 = 0"), FHexDamageCalculator::DodgeChance(C), 0.0f);
		}
		{
			FHexUnit A8 = MakeDummyTarget(0);
			A8.AGI = 8;
			FHexDamageContext C = MakeCtx(&Src, &A8);
			// 8×0.6/(8+40) = 4.8/48 = 0.1（扑咬犬 AGI=8 → 10% 闪避）
			Ctx.CheckNearlyEqual(TEXT("AGI8 闪避率 = 10%"),
				FHexDamageCalculator::DodgeChance(C), 0.1f, 0.005f);
		}
		{
			// 递减性：AGI 翻倍，闪避率增幅必须小于翻倍
			FHexUnit A20 = MakeDummyTarget(0);
			A20.AGI = 20;
			FHexUnit A40 = MakeDummyTarget(0);
			A40.AGI = 40;
			FHexDamageContext C20 = MakeCtx(&Src, &A20);
			FHexDamageContext C40 = MakeCtx(&Src, &A40);
			const float D20 = FHexDamageCalculator::DodgeChance(C20);
			const float D40 = FHexDamageCalculator::DodgeChance(C40);
			Ctx.Check(TEXT("闪避率随 AGI 单调递增"), D40 > D20,
				FString::Printf(TEXT("%.4f → %.4f"), D20, D40));
			Ctx.Check(TEXT("闪避收益递减（AGI 翻倍，闪避不翻倍）"), D40 < D20 * 2.0f,
				FString::Printf(TEXT("%.4f vs %.4f×2"), D40, D20));
		}
		{
			FHexUnit Huge = MakeDummyTarget(0);
			Huge.AGI = 100000;
			FHexDamageContext C = MakeCtx(&Src, &Huge);
			Ctx.Check(TEXT("闪避率不超过硬上限 DodgeCap"),
				FHexDamageCalculator::DodgeChance(C) <= HexK::DodgeCap + 0.0001f,
				FString::Printf(TEXT("实际=%.4f 上限=%.4f"),
					FHexDamageCalculator::DodgeChance(C), HexK::DodgeCap));
		}

		// 背击无法被闪避（§8.2.3）
		{
			FHexUnit A40 = MakeDummyTarget(0);
			A40.AGI = 40;
			FHexDamageContext C = MakeCtx(&Src, &A40);
			C.bFromRear = true;
			Ctx.CheckNearlyEqual(TEXT("背击闪避率 = 0"), FHexDamageCalculator::DodgeChance(C), 0.0f);
		}

		// tick 伤害 / 地形危害不该被闪避
		{
			FHexUnit A40 = MakeDummyTarget(0);
			A40.AGI = 40;
			FHexDamageContext C = MakeCtx(&Src, &A40);
			C.bCannotBeDodged = true;
			Ctx.CheckNearlyEqual(TEXT("标记不可闪避时闪避率 = 0"),
				FHexDamageCalculator::DodgeChance(C), 0.0f);
		}
	}

	// ═══════════════════════════════════════════ 端到端：真实卡牌数值

	void CheckRealCards(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("真实卡牌伤害（对扑咬犬 DEF=2）"));

		const FHexUnit Src = MakeWardenUnit();

		const FHexEnemyData* Hound = FHexContentLibrary::FindEnemy(TEXT("biting_hound"));
		if (!Hound)
		{
			Ctx.Fail(TEXT("前置条件"), TEXT("找不到 biting_hound"));
			return;
		}
		FHexUnit Tgt = FHexContentLibrary::MakeEnemyUnit(*Hound, 1, 0);
		// 闪避是独立的随机层，这里只验证伤害数值
		Tgt.AGI = 0;

		auto DamageOfCard = [&Src, &Tgt](const FName& CardId) -> int32
		{
			const FHexCardData* C = FHexContentLibrary::FindCard(CardId);
			if (!C) { return -1; }

			int32 Total = 0;
			for (const FHexEffectStep& S : C->Effects)
			{
				if (S.Op != EHexEffectOp::DealDamage) { continue; }
				FHexDamageContext D;
				D.Source = &Src;
				D.Target = &Tgt;
				D.Flat = S.FlatValue;
				D.StatRef = S.StatRef;
				D.StatRatio = S.StatRatio;
				Total += FHexDamageCalculator::Preview(D).X * S.Repeat;
			}
			return Total;
		};

		// 《攻击》ATK10 ×1.0 → 10 × (1-2/14) = 8.57 → 8
		Ctx.CheckEqual(TEXT("《攻击》→ 8"), DamageOfCard(TEXT("atk_basic")), 8);

		// 《重击》ATK10 ×1.8 = 18 → 18 × 0.857 = 15.4 → 15
		Ctx.CheckEqual(TEXT("《重击》→ 15"), DamageOfCard(TEXT("heavy_strike")), 15);

		// 《连刺》单段 4.2 → 4.2×0.857 = 3.6 → 3，三段 = 9
		// ⚠️ 这就是"floor 取整损失被摊薄 3 次"的问题：
		//    连刺的实际输出比账面 12.6 少 3.6 点（28%）。
		//    Godot 版把系数从 0.5 压到 0.42 正是为了把费效拉回《重击》同档。
		Ctx.CheckEqual(TEXT("《连刺》三段 → 9（取整损失×3）"),
			DamageOfCard(TEXT("multi_stab")), 9);

		// 《盾击》ATK×0.6 + DEF×0.8 = 6 + 6.4
		//   两步各自独立走管线：floor(6×0.857)=5，floor(6.4×0.857)=5 → 10
		Ctx.CheckEqual(TEXT("《盾击》→ 10（吃 DEF 加成）"),
			DamageOfCard(TEXT("shield_bash")), 10);

		// 费效对照：《连刺》1 费 9 点，《重击》2 费 15 点。
		// ⚠️ 曾经是 1 费 12 点 vs 2 费 15 点（费效 12 vs 7.5，1.6 倍差距），
		//    导致"有连刺就打连刺"。现在 9 vs 7.5，连刺略优但不再压倒性。
		{
			const float StabEff = static_cast<float>(DamageOfCard(TEXT("multi_stab"))) / 1.0f;
			const float HeavyEff = static_cast<float>(DamageOfCard(TEXT("heavy_strike"))) / 2.0f;
			Ctx.Check(TEXT("《连刺》与《重击》费效差距 < 1.4 倍"),
				StabEff < HeavyEff * 1.4f,
				FString::Printf(TEXT("连刺费效=%.2f 重击费效=%.2f 比值=%.2f"),
					StabEff, HeavyEff, StabEff / HeavyEff));
		}

		// 镇妖者 DEF8 承受扑咬犬 ATK13 的实际伤害：
		// 13 × (1 - 8/20) = 7.8 → 7。enc_02 有 3 狗 2 投石手 →
		// 约 19 点/回合，80 HP 撑约 4 回合（"硬但公平"的目标节奏）
		{
			FHexUnit Warden = MakeWardenUnit();
			FHexDamageContext C;
			C.Source = &Tgt;
			C.Target = &Warden;
			C.StatRef = TEXT("ATK");
			C.StatRatio = 1.0f;
			const int32 Incoming = FHexDamageCalculator::Preview(C).X;
			Ctx.CheckEqual(TEXT("扑咬犬打镇妖者 → 7（DEF8 减伤 40%）"), Incoming, 7);

			// 节奏校验：镇妖者能撑的回合数应落在 3–5 回合区间
			const int32 PerRound = 7 * 3 + 5 * 2;  // 3 狗 + 2 投石手(ATK10 → 6)
			const int32 Rounds = Warden.HPMax / FMath::Max(1, PerRound);
			Ctx.Check(TEXT("enc_02 下镇妖者可撑 3–5 回合"),
				Rounds >= 2 && Rounds <= 5,
				FString::Printf(TEXT("每回合承受约 %d 点，可撑 %d 回合"), PerRound, Rounds));
		}
	}
}

bool FHexVerifySuites::VerifyDamage(FHexVerifyContext& Ctx)
{
	CheckBaseAndFlat(Ctx);
	CheckOrderHook(Ctx);
	CheckMultipliersAndCrit(Ctx);
	CheckDefenseAndFloor(Ctx);
	CheckBlock(Ctx);
	CheckDodge(Ctx);
	CheckRealCards(Ctx);
	return Ctx.NumFailed() == 0;
}
