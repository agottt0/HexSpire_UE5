// Copyright Hex Spire. All Rights Reserved.
//
// 触发总线 与 敌人 AI 验证
//
// ══════════════════════════════════════════════════════════════════
// 为什么这两块合在一起
// ══════════════════════════════════════════════════════════════════
// 它们是"整场战斗能不能跑"的两个支点，且都有【必须成立的承诺】：
//   TriggerBus 承诺：顺序确定、不无限递归、计数不串号
//   EnemyAI    承诺：意图是承诺 —— 生成时冻结，执行时只重放
//
// 后者尤其关键：§13.2 要求玩家能读出"这一击能不能躲"。
// 若执行时重算目标，玩家走位规避就无效 ——
// 「预警 + 走位」这个核心循环（P2 支柱：空间即决策）直接失效，
// 而且【不会报任何错】，只会让玩家觉得"我明明躲开了还是挨打"。

#include "Verify/HexVerify.h"
#include "Battle/HexTriggerBus.h"
#include "Battle/HexEnemyAI.h"
#include "Battle/HexBattleState.h"
#include "Battle/HexBattleFlow.h"
#include "Battle/HexRuleBook.h"
#include "Battle/HexGameAction.h"
#include "Deck/HexPileManager.h"
#include "Battle/HexUnit.h"
#include "Battle/HexStatusData.h"
#include "Content/HexContentLibrary.h"
#include "Content/HexLayouts.h"
#include "Runes/HexRuneLibrary.h"
#include "Hex/HexCoord.h"
#include "Rng/HexRngStreams.h"
#include "Core/HexSpireConstants.h"

namespace
{
	/** 搭一个最小可用战场：镇妖者 + 指定敌人 */
	int32 SpawnHero(FHexBattleState& State)
	{
		const FHexHeroData* H = FHexContentLibrary::FindHero(TEXT("warden"));
		FHexUnit U;
		if (H)
		{
			U.SourceId = H->Id;
			U.DisplayName = H->DisplayName;
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
		U.Anchor = FHexCoord::OffsetToCube(HexK::HeroSpawnCol, HexK::HeroSpawnRow);
		U.Facing = HexK::HeroSpawnFacing;

		const int32 Id = State.AddUnit(U);
		State.HeroUnitId = Id;
		return Id;
	}

	int32 SpawnEnemy(FHexBattleState& State, const FName& EnemyId, int32 Col, int32 Row)
	{
		const FHexEnemyData* E = FHexContentLibrary::FindEnemy(EnemyId);
		if (!E)
		{
			return -1;
		}
		FHexUnit U = FHexContentLibrary::MakeEnemyUnit(*E, 1, 0);
		U.Anchor = FHexCoord::OffsetToCube(Col, Row);
		U.Facing = 5;   // 朝下（面向玩家侧）
		return State.AddUnit(U);
	}

	/** 空旷厅堂 + 镇妖者 + 一只敌人 */
	void SetupBattle(FHexBattleState& State, const FName& EnemyId, int32 Col, int32 Row)
	{
		FHexLayouts::Build(TEXT("open_hall"), State.Grid);
		SpawnHero(State);
		SpawnEnemy(State, EnemyId, Col, Row);
		State.RebuildOccupancy();
		State.RebuildRuleAggregate();
	}

	// ═══════════════════════════════════════════ 触发总线：顺序

	void CheckTriggerOrder(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("触发总线：分层顺序"));

		FHexBattleState State(1);
		FHexTriggerBus Bus;

		Bus.RebuildListeners(State);
		Ctx.CheckEqual(TEXT("空 loadout 无监听者"), Bus.ListenerCount(), 0);

		// ── 符文槽 1→6 的顺序
		//
		// ⚠️ 这是 §6.5「6 槽有序」的实现基础。
		//    把同一个符文装在不同槽位，触发链的顺序必须跟着变。
		{
			const FHexRuneData* Whet = FHexRuneLibrary::FindRune(TEXT("rune_whetstone"));
			const FHexRuneData* Twin = FHexRuneLibrary::FindRune(TEXT("rune_twin_shadow"));

			State.RuneLoadout.SetSlot(0, Whet);
			State.RuneLoadout.SetSlot(1, Twin);
			Bus.RebuildListeners(State);

			TArray<FString> ChainA;
			Bus.DescribeChain(EHexTriggerTiming::OnAttack, ChainA);
			Ctx.CheckEqual(TEXT("OnAttack 有 2 个监听者"),
				Bus.ListenerCountFor(EHexTriggerTiming::OnAttack), 2);

			// 交换槽位
			State.RuneLoadout.SetSlot(0, Twin);
			State.RuneLoadout.SetSlot(1, Whet);
			Bus.RebuildListeners(State);

			TArray<FString> ChainB;
			Bus.DescribeChain(EHexTriggerTiming::OnAttack, ChainB);

			Ctx.Check(TEXT("交换槽位后触发链顺序改变（§6.5）"),
				ChainA.Num() == ChainB.Num() && ChainA.Num() > 0 && ChainA[0] != ChainB[0],
				TEXT("槽位顺序未影响触发链 → 6 槽有序失效"));

			// 空槽必须被跳过而非占位
			State.RuneLoadout.ClearSlot(1);
			State.RuneLoadout.SetSlot(4, Twin);
			Bus.RebuildListeners(State);
			Ctx.CheckEqual(TEXT("跳过空槽后仍是 2 个监听者"),
				Bus.ListenerCountFor(EHexTriggerTiming::OnAttack), 2);
		}

		// ── 符文永远在装备之前
		//
		// SlotOrder：0=英雄被动 / 1-6=符文 / 10-12=装备 / 20+=状态
		{
			FHexBattleState S2(2);
			FHexTriggerBus B2;
			FHexRngStreams Rng(1);

			// 《倒刺革衣》在 OnDamageTaken 触发
			S2.EquipLoadout.Equip(
				FHexEquipGenerator::Generate(TEXT("ar_thorn"), EHexRarity::Common, Rng, 1));
			B2.RebuildListeners(S2);

			TArray<FString> Chain;
			B2.DescribeChain(EHexTriggerTiming::OnDamageTaken, Chain);

			bool bHasEquip = false;
			for (const FString& L : Chain)
			{
				if (L.Contains(TEXT("equip:")))
				{
					bHasEquip = true;
				}
			}
			Ctx.Check(TEXT("装备触发器已挂载且可识别"), bHasEquip, TEXT(""));

			// 同时装符文与装备，符文必须排在前面
			S2.RuneLoadout.SetSlot(0, FHexRuneLibrary::FindRune(TEXT("rune_cycle_ward")));
			B2.RebuildListeners(S2);

			TArray<FString> Mixed;
			B2.DescribeChain(EHexTriggerTiming::OnDeckReshuffled, Mixed);
			Ctx.Check(TEXT("符文触发器独立于装备时机"),
				Mixed.Num() >= 1, TEXT(""));
		}

		// ── 重建的幂等性
		//
		// ⚠️ RebuildListeners 被调用多次（每次符文/装备变更）。
		//    若它 append 而非 reset，监听者会翻倍，符文效果被触发两次。
		{
			FHexBattleState S3(3);
			FHexTriggerBus B3;
			S3.RuneLoadout.SetSlot(0, FHexRuneLibrary::FindRune(TEXT("rune_whetstone")));

			B3.RebuildListeners(S3);
			const int32 Once = B3.ListenerCount();
			B3.RebuildListeners(S3);
			B3.RebuildListeners(S3);
			Ctx.CheckEqual(TEXT("重复 Rebuild 不累积监听者"),
				B3.ListenerCount(), Once);
		}
	}

	// ═══════════════════════════════════════════ 触发总线：分发

	void CheckTriggerDispatch(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("触发总线：分发与安全闸"));

		// ── MaxPerRound 生效
		//
		// ⚠️ 《推山手》限每回合 3 次。若不生效，
		//    "推动敌人 → 伤害 → 击退 → 再触发"这类链可能自激。
		{
			FHexBattleState State(10);
			SetupBattle(State, TEXT("biting_hound"), 4, 6);

			FHexTriggerBus Bus;
			State.RuneLoadout.SetSlot(0, FHexRuneLibrary::FindRune(TEXT("rune_push_hand")));
			Bus.RebuildListeners(State);
			Bus.ResetBattleCounters();

			FHexActionQueue Queue;
			FHexTriggerContext TCtx;
			TCtx.SourceUnitId = State.HeroUnitId;
			TCtx.TargetUnitId = 2;

			// 连发 10 次，只有前 3 次该产生动作
			int32 TotalActions = 0;
			for (int32 I = 0; I < 10; ++I)
			{
				const int32 Before = Queue.Num();
				Bus.Emit(EHexTriggerTiming::OnMoveEnemy, TCtx, State, Queue);
				TotalActions += (Queue.Num() - Before);
			}

			Ctx.Check(TEXT("MaxPerRound=3 限制了触发次数"),
				TotalActions > 0 && TotalActions <= 3,
				FString::Printf(TEXT("产生 %d 个动作（应 ≤3）"), TotalActions));

			// 回合计数重置后可以再触发
			Bus.ResetRoundCounters();
			const int32 Before = Queue.Num();
			Bus.Emit(EHexTriggerTiming::OnMoveEnemy, TCtx, State, Queue);
			Ctx.Check(TEXT("回合计数重置后可再触发"),
				Queue.Num() > Before, TEXT(""));
		}

		// ── 计数键用实例 uid，同名符文装不同槽不串号
		//
		// ⚠️ 若用符文 id 做键，装两个相同符文时会共享计数，
		//    第二个符文的触发次数被第一个吃掉。
		{
			FHexBattleState State(11);
			SetupBattle(State, TEXT("biting_hound"), 4, 6);

			FHexTriggerBus Bus;
			const FHexRuneData* Push = FHexRuneLibrary::FindRune(TEXT("rune_push_hand"));
			// 同一个符文装两个槽
			State.RuneLoadout.SetSlot(0, Push);
			State.RuneLoadout.SetSlot(1, Push);
			Bus.RebuildListeners(State);
			Bus.ResetBattleCounters();

			FHexActionQueue Queue;
			FHexTriggerContext TCtx;
			TCtx.SourceUnitId = State.HeroUnitId;
			TCtx.TargetUnitId = 2;

			int32 Total = 0;
			for (int32 I = 0; I < 10; ++I)
			{
				const int32 Before = Queue.Num();
				Bus.Emit(EHexTriggerTiming::OnMoveEnemy, TCtx, State, Queue);
				Total += (Queue.Num() - Before);
			}

			// 两个独立计数器，各 3 次 = 6
			Ctx.Check(TEXT("同名符文装两槽各自计数（不串号）"),
				Total > 3 && Total <= 6,
				FString::Printf(TEXT("产生 %d 个动作（应为 6）"), Total));
		}

		// ── 时机隔离：发一个时机不该触发别的
		{
			FHexBattleState State(12);
			SetupBattle(State, TEXT("biting_hound"), 4, 6);

			FHexTriggerBus Bus;
			State.RuneLoadout.SetSlot(0, FHexRuneLibrary::FindRune(TEXT("rune_soul_eater")));
			Bus.RebuildListeners(State);
			Bus.ResetBattleCounters();

			FHexActionQueue Queue;
			FHexTriggerContext TCtx;
			TCtx.SourceUnitId = State.HeroUnitId;

			Bus.Emit(EHexTriggerTiming::OnRoundStart, TCtx, State, Queue);
			Ctx.CheckEqual(TEXT("《食魂》不响应 OnRoundStart"), Queue.Num(), 0);

			Bus.Emit(EHexTriggerTiming::OnKill, TCtx, State, Queue);
			Ctx.Check(TEXT("《食魂》响应 OnKill"), Queue.Num() > 0, TEXT(""));
		}

		// ── 条件过滤
		{
			FHexBattleState State(13);
			SetupBattle(State, TEXT("biting_hound"), 4, 6);

			FHexTriggerBus Bus;
			// 《空匣》：抽牌堆为空时才触发
			State.RuneLoadout.SetSlot(0, FHexRuneLibrary::FindRune(TEXT("rune_empty_case")));
			Bus.RebuildListeners(State);
			Bus.ResetBattleCounters();

			// 抽牌堆为空 → 条件满足
			FHexActionQueue Queue;
			FHexTriggerContext TCtx;
			TCtx.SourceUnitId = State.HeroUnitId;
			Bus.Emit(EHexTriggerTiming::OnRoundStart, TCtx, State, Queue);
			Ctx.Check(TEXT("《空匣》在抽牌堆空时触发"),
				Queue.Num() > 0, TEXT(""));

			// 放牌进抽牌堆 → 条件不满足
			{
				FHexBattleState S2(14);
				SetupBattle(S2, TEXT("biting_hound"), 4, 6);
				const FHexHeroData* H = FHexContentLibrary::FindHero(TEXT("warden"));
				TArray<FHexCardInstance> Deck;
				if (H)
				{
					FHexContentLibrary::BuildStartingDeck(*H, Deck, S2.FixedCards);
				}
				S2.Piles.BeginBattle(Deck, S2.Rng);

				FHexTriggerBus B2;
				S2.RuneLoadout.SetSlot(0, FHexRuneLibrary::FindRune(TEXT("rune_empty_case")));
				B2.RebuildListeners(S2);
				B2.ResetBattleCounters();

				FHexActionQueue Q2;
				FHexTriggerContext C2;
				C2.SourceUnitId = S2.HeroUnitId;
				B2.Emit(EHexTriggerTiming::OnRoundStart, C2, S2, Q2);
				Ctx.CheckEqual(TEXT("《空匣》在抽牌堆非空时不触发"), Q2.Num(), 0);
			}
		}

		// ── R7 安全闸：不得崩溃、不得抛异常
		//
		// ⚠️ 策划案 R7 明确要求：递归超限 → 写违规记录并停止分发，
		//    【绝不崩溃】。BattleSim 靠这条把死循环变成可统计数据。
		{
			FHexBattleState State(15);
			SetupBattle(State, TEXT("biting_hound"), 4, 6);

			FHexTriggerBus Bus;
			// 塞满 6 槽，全部挂触发器
			State.RuneLoadout.SetSlot(0, FHexRuneLibrary::FindRune(TEXT("rune_push_hand")));
			State.RuneLoadout.SetSlot(1, FHexRuneLibrary::FindRune(TEXT("rune_soul_eater")));
			State.RuneLoadout.SetSlot(2, FHexRuneLibrary::FindRune(TEXT("rune_heart_burn")));
			State.RuneLoadout.SetSlot(3, FHexRuneLibrary::FindRune(TEXT("rune_cycle_ward")));
			State.RuneLoadout.SetSlot(4, FHexRuneLibrary::FindRune(TEXT("rune_greed_bone")));
			State.RuneLoadout.SetSlot(5, FHexRuneLibrary::FindRune(TEXT("rune_hex_chain")));
			Bus.RebuildListeners(State);
			Bus.ResetBattleCounters();

			FHexActionQueue Queue;
			FHexTriggerContext TCtx;
			TCtx.SourceUnitId = State.HeroUnitId;
			TCtx.TargetUnitId = 2;

			// 把所有时机各发 20 次
			for (int32 T = 0; T < static_cast<int32>(EHexTriggerTiming::Count); ++T)
			{
				for (int32 I = 0; I < 20; ++I)
				{
					Bus.Emit(static_cast<EHexTriggerTiming>(T), TCtx, State, Queue);
				}
			}

			Ctx.Check(TEXT("暴力分发不崩溃（R7）"), true, TEXT(""));

			// 动作数必须有界 —— 无界说明存在自激链
			Ctx.Check(TEXT("动作总数有界（无自激链）"),
				Queue.Num() < HexK::MaxActionsPerResolve,
				FString::Printf(TEXT("产生 %d 个动作，上限 %d"),
					Queue.Num(), HexK::MaxActionsPerResolve));
		}

		// ── ②′ 钩子链
		{
			FHexBattleState State(16);
			SetupBattle(State, TEXT("biting_hound"), 4, 6);

			FHexTriggerBus Bus;
			State.RuneLoadout.SetSlot(0, FHexRuneLibrary::FindRune(TEXT("rune_whetstone")));
			State.RuneLoadout.SetSlot(1, FHexRuneLibrary::FindRune(TEXT("rune_twin_shadow")));
			Bus.RebuildListeners(State);

			FHexTriggerContext TCtx;
			TCtx.SourceUnitId = State.HeroUnitId;
			TCtx.TargetUnitId = 2;
			// 《倍影》要求本回合已打出 ≥2 张牌
			State.CardsPlayedThisRound = 3;

			const FHexValueHook Hook =
				Bus.MakeValueHook(EHexTriggerTiming::OnAttack, State, TCtx);
			Ctx.Check(TEXT("MakeValueHook 返回有效钩子"),
				static_cast<bool>(Hook), TEXT(""));

			if (Hook)
			{
				TArray<FHexRuneStep> Log;
				const float Out = Hook(10.0f, Log);
				// (10 + ATK×0.5=5) × 1.4 = 21
				Ctx.CheckNearlyEqual(TEXT("②′ 钩子链 [砺石,倍影] = 21"), Out, 21.0f, 0.01f);
				Ctx.Check(TEXT("②′ 钩子记录了每一步"), Log.Num() >= 2,
					FString::Printf(TEXT("日志步数=%d"), Log.Num()));
			}

			// 无监听者时返回空钩子
			{
				FHexBattleState S2(17);
				SetupBattle(S2, TEXT("biting_hound"), 4, 6);
				FHexTriggerBus B2;
				B2.RebuildListeners(S2);
				FHexTriggerContext C2;
				const FHexValueHook Empty =
					B2.MakeValueHook(EHexTriggerTiming::OnAttack, S2, C2);
				Ctx.Check(TEXT("无符文时钩子为空"),
					!static_cast<bool>(Empty), TEXT(""));
			}
		}
	}

	// ═══════════════════════════════════════════ 埋点覆盖率（§6.3）
	//
	// ══════════════════════════════════════════════════════════════
	// 为什么必须有这一节：1049 项断言全绿，却有 3 个符文是死的
	// ══════════════════════════════════════════════════════════════
	// 已有的 R7 暴力测试是【手动调 Bus.Emit】的，所以它只能证明
	// "总线收到时机后行为正确"，完全无法发现"生产代码从来不调它"。
	//
	// 实测：22 个时机里只有 11 个真的被 HexBattleFlow 派发过。
	// 后果是《食魂》(OnKill)、《焚心》(OnCrit)、《轮回护符》
	// (OnDeckReshuffled) 装上去毫无效果 —— 不报错、不崩溃，
	// 只显得"这符文很弱"。策划案 §6.3 的示范组合也无法成立。
	//
	// 所以这里【不手动 Emit】，而是跑真实战斗流程，
	// 看哪些时机自己冒出来。这是唯一能抓住"没人埋点"的方式。

	/** 记录某个时机是否被真实派发过 */
	struct FTimingProbe
	{
		/** 一个必然触发、且效果无害的探针符文触发器 */
		static FHexRuneTrigger MakeProbe(EHexTriggerTiming When)
		{
			FHexRuneTrigger T;
			T.When = When;
			// 效果选「获得 0 点格挡」：合法、可结算、但数值为 0，
			// 不会污染被测战斗的平衡。
			// （刻意不用伤害类效果 —— 那会让探针自己触发 OnDamageDealt，
			//   把"这个时机有没有埋点"的结论污染成自问自答。）
			T.Effects = { FHexEffectStep::MakeBlock(0.0f, NAME_None, 0.0f) };
			T.MaxPerRound = -1;
			T.MaxPerBattle = -1;
			return T;
		}
	};

	/** 时机 → 可读名（断言消息里必须有名字，裸数字无法排查） */
	const TCHAR* TimingName(EHexTriggerTiming T)
	{
		switch (T)
		{
		case EHexTriggerTiming::OnBattleStart:    return TEXT("OnBattleStart");
		case EHexTriggerTiming::OnRoundStart:     return TEXT("OnRoundStart");
		case EHexTriggerTiming::OnRoundEnd:       return TEXT("OnRoundEnd");
		case EHexTriggerTiming::OnCardPlayed:     return TEXT("OnCardPlayed");
		case EHexTriggerTiming::OnAttack:         return TEXT("OnAttack");
		case EHexTriggerTiming::OnDamageDealt:    return TEXT("OnDamageDealt");
		case EHexTriggerTiming::OnDamageTaken:    return TEXT("OnDamageTaken");
		case EHexTriggerTiming::OnKill:           return TEXT("OnKill");
		case EHexTriggerTiming::OnBlockGained:    return TEXT("OnBlockGained");
		case EHexTriggerTiming::OnBlockBroken:    return TEXT("OnBlockBroken");
		case EHexTriggerTiming::OnMoveSelf:       return TEXT("OnMoveSelf");
		case EHexTriggerTiming::OnMoveEnemy:      return TEXT("OnMoveEnemy");
		case EHexTriggerTiming::OnStatusApplied:  return TEXT("OnStatusApplied");
		case EHexTriggerTiming::OnCardDrawn:      return TEXT("OnCardDrawn");
		case EHexTriggerTiming::OnCardDiscarded:  return TEXT("OnCardDiscarded");
		case EHexTriggerTiming::OnCardExhausted:  return TEXT("OnCardExhausted");
		case EHexTriggerTiming::OnDeckReshuffled: return TEXT("OnDeckReshuffled");
		case EHexTriggerTiming::OnEnergyLeftover: return TEXT("OnEnergyLeftover");
		case EHexTriggerTiming::OnCrit:           return TEXT("OnCrit");
		case EHexTriggerTiming::OnDodge:          return TEXT("OnDodge");
		case EHexTriggerTiming::OnUnitDeath:      return TEXT("OnUnitDeath");
		case EHexTriggerTiming::OnBattleWin:      return TEXT("OnBattleWin");
		default:                                  return TEXT("?");
		}
	}

	void CheckTimingCoverage(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("触发时机埋点覆盖率（§6.3）"));

		// ── 哪些时机【不】由本探针战斗产生，属于合理豁免
		//
		// ⚠️ 这张豁免表是白名单，加东西必须写理由。
		//    把"暂时没实现"的时机塞进来就等于把缺陷合法化 ——
		//    那正是这一节要防的事。
		//
		// ⚠️ 必须用【枚举名】而不是数字写这张表。
		//    第一版我按记忆写成了数字，把 OnBlockBroken 记成 8
		//    （实际是 9）、OnCardExhausted 记成 14（实际是 15），
		//    结果豁免全打偏：真正该豁免的在被测，该被测的被豁免了。
		auto IsExempt = [](EHexTriggerTiming T) -> const TCHAR*
		{
			switch (T)
			{
			case EHexTriggerTiming::OnAttack:
				// ②′ 数值钩子，走 MakeValueHook 而非 Emit（设计如此）
				return TEXT("②′数值钩子，不走 Emit");
			case EHexTriggerTiming::OnBattleWin:
				// 需要打赢整场，本探针战斗只跑固定回合数
				return TEXT("需完整胜局");
			case EHexTriggerTiming::OnDodge:
				// 闪避是概率事件，固定种子下不保证出现
				return TEXT("概率事件");
			case EHexTriggerTiming::OnCardExhausted:
				// 需要消耗类卡或 ExhaustAllAttacks 规则，探针卡组里没有
				return TEXT("需消耗类卡");
			case EHexTriggerTiming::OnBlockBroken:
				// 需要敌人正好打穿玩家格挡，回合数不足时可能不发生
				return TEXT("需格挡被打穿");
			default:
				return nullptr;
			}
		};

		// ── 逐个时机装一个探针符文，跑真实战斗，看它有没有被触发
		for (int32 TI = 0; TI < static_cast<int32>(EHexTriggerTiming::Count); ++TI)
		{
			const EHexTriggerTiming Timing = static_cast<EHexTriggerTiming>(TI);
			const TCHAR* ExemptReason = IsExempt(Timing);

			// 造一个只挂这一个时机的临时符文
			FHexRuneData Probe;
			Probe.Id = FName(*FString::Printf(TEXT("__probe_%d"), TI));
			Probe.DisplayName = TEXT("探针");
			Probe.Triggers = { FTimingProbe::MakeProbe(Timing) };

			// ⚠️ 多种子重试。
			//    有些链路仍带随机性（闪避/暴击的 roll、敌人 AI 选择），
			//    单一种子下"没触发"可能只是这一局没赶上。
			//    只要【任一】种子触发过，就证明埋点存在 ——
			//    这正是本断言要回答的问题。
			//    反过来，全部种子都没触发才判失败，避免假红。
			int32 TotalFired = 0;

			for (int32 Seed = 0; Seed < 6 && TotalFired == 0; ++Seed)
			{
				FHexBattleState State(1000 + TI * 10 + Seed);

				// ⚠️ 敌人必须放在英雄【紧邻格】。
				//    英雄出生在 (4,1)，而默认的 (4,6) 隔着 5 行 ——
				//    《盾击》射程是 1-1（必须贴身），整场都打不出来，
				//    于是它自带的击退效果永远不发生，OnMoveEnemy 测不到。
				//    这不是埋点缺陷，而是探针没把场景摆对。
				FHexLayouts::Build(TEXT("open_hall"), State.Grid);
				SpawnHero(State);
				const int32 FrailId = SpawnEnemy(
					State, TEXT("biting_hound"), HexK::HeroSpawnCol, HexK::HeroSpawnRow + 1);
				SpawnEnemy(
					State, TEXT("biting_hound"), HexK::HeroSpawnCol + 1, HexK::HeroSpawnRow + 1);
				State.RebuildOccupancy();

				// ⚠️ SetupBattle 只搭网格与单位，【不】初始化牌堆 ——
				//    它是给"手动 Emit"的断言用的，那些不需要卡。
				//    这里必须自己备牌，否则手牌恒空、一张牌都打不出来，
				//    于是连 OnCardPlayed / OnBlockGained 这些【已有埋点】
				//    的时机也测不到，结论会变成一片假失败。
				//    （第一版就踩了这个坑，13 项失败里有一半是假的。）
				const FHexHeroData* HeroData = FHexContentLibrary::FindHero(TEXT("warden"));
				if (HeroData)
				{
					TArray<FHexCardInstance> Deck;
					FHexContentLibrary::BuildStartingDeck(*HeroData, Deck, State.FixedCards);
					State.Piles.BeginBattle(Deck, State.Rng);
					State.HeroEnergyMaxBase = HeroData->EnergyMax;
					State.HeroDrawBase = HeroData->CardsDrawnPerTurn;
					State.HeroPassiveRules = HeroData->PassiveRules;
				}

				State.RuneLoadout.SetSlot(0, &Probe);
				State.RebuildRuleAggregate();

				// ⚠️ 暴击率拉满：OnCrit 原本靠运气，
				//    换个敌人位置就会因为随机流不同而时通时不通 ——
				//    那样断言的红绿取决于种子，毫无意义。
				//    把 CRIT 设成 100 让暴击成为必然事件。
				if (FHexUnit* Hero = State.GetHero())
				{
					Hero->CRIT = 100;
				}

				// ⚠️ 只削这一只，另一只留满血。
				//    《盾击》是"先伤害、再击退"：若场上唯一的敌人只有 1 HP，
				//    它会被伤害步骤当场打死，击退步骤的 AffectedUnits
				//    就筛不到存活目标 —— OnMoveEnemy 又测不到了。
				//    留一只满血的，击杀与推拉才能同时发生。
				//
				//    削血本身的目的：OnKill / OnUnitDeath 只有真打死才触发，
				//    而探针只跑几回合、机器人式出牌未必打得死 24 HP 的狗。
				if (FHexUnit* Frail = State.FindUnit(FrailId))
				{
					Frail->HP = 1;
				}

				FHexBattleFlow Flow(State);
				Flow.SetCardLookup([](FName Id) { return FHexContentLibrary::FindCard(Id); });
				Flow.BeginBattle();

				// 打几张牌 + 过几个回合，尽量覆盖到各条链路
				for (int32 R = 0; R < 4 && !Flow.IsBattleOver(); ++R)
				{
					// 手牌与固定卡都试着往敌人身上招呼
					TArray<FHexCardInstance> Playable = State.Piles.GetHand();
					Playable.Append(State.FixedCards);

					for (const FHexCardInstance& C : Playable)
					{
						if (!Flow.CanPlayCard(C.Uid))
						{
							continue;
						}
						TArray<FIntVector> Targets;
						Flow.GetLegalTargets(C.Uid, Targets);
						if (Targets.Num() == 0)
						{
							continue;
						}

						// 优先打在敌人身上，否则打第一个合法格
						FIntVector Pick = Targets[0];
						for (const FIntVector& T : Targets)
						{
							const FHexUnit* U = State.FindUnitAtCell(T);
							if (U && U->Team == EHexTeam::Enemy && U->bIsAlive)
							{
								Pick = T;
								break;
							}
						}
						Flow.PlayCard(C.Uid, Pick);
					}

					Flow.EndPlayerTurn();
				}

				TotalFired += Flow.GetTimingFireCount(Timing);
			}

			if (ExemptReason)
			{
				// 豁免项不强制，但如果它居然触发了，说明豁免理由已过期 ——
				// 这是好事，提示可以把它移出白名单。
				Ctx.Check(FString::Printf(TEXT("[豁免] %s（%s）"),
					TimingName(Timing), ExemptReason), true, TEXT(""));
				continue;
			}

			Ctx.Check(
				FString::Printf(TEXT("%s 能被战斗流程真实派发"), TimingName(Timing)),
				TotalFired > 0,
				FString::Printf(
					TEXT("6 个种子的战斗中 %s 一次都没被 Emit —— ")
					TEXT("挂在它上面的符文会【静默失效】（装上去毫无效果也不报错）。")
					TEXT("请在 HexBattleFlow 补埋点，或加入豁免白名单并写明理由。"),
					TimingName(Timing)));
		}
	}

	// ═══════════════════════════════════════════ 符文端到端生效
	//
	// ══════════════════════════════════════════════════════════════
	// 为什么覆盖率断言还不够，必须再有这一节
	// ══════════════════════════════════════════════════════════════
	// CheckTimingCoverage 证明的是「时机被派发了」，
	// 但"派发了"离"符文真的产生效果"还差好几步：
	// 过滤器、条件、限次、EffectOp 是否被 TriggerBus 实现……
	// 任何一环断掉，符文依然是哑的，而覆盖率断言照样全绿。
	//
	// 所以这三个符文（P0 缺陷的原始受害者）必须有【看结果】的断言：
	//   《食魂》    OnKill           → 手牌真的多了一张
	//   《焚心》    OnCrit           → 目标真的挂上了燃烧
	//   《轮回护符》OnDeckReshuffled → 真的拿到了格挡
	// 它们是防止同类缺陷复发的最后一道防线。

	/** 备好牌堆与英雄基线的战场（端到端测试用） */
	void SetupFullBattle(FHexBattleState& State, int32 EnemyCol, int32 EnemyRow)
	{
		FHexLayouts::Build(TEXT("open_hall"), State.Grid);
		SpawnHero(State);
		SpawnEnemy(State, TEXT("biting_hound"), EnemyCol, EnemyRow);
		State.RebuildOccupancy();

		if (const FHexHeroData* H = FHexContentLibrary::FindHero(TEXT("warden")))
		{
			TArray<FHexCardInstance> Deck;
			FHexContentLibrary::BuildStartingDeck(*H, Deck, State.FixedCards);
			State.Piles.BeginBattle(Deck, State.Rng);
			State.HeroEnergyMaxBase = H->EnergyMax;
			State.HeroDrawBase = H->CardsDrawnPerTurn;
			State.HeroPassiveRules = H->PassiveRules;
		}
		State.RebuildRuleAggregate();
	}

	void CheckRuneEndToEnd(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("符文端到端生效（P0 回归防线）"));

		// ── 《食魂》：击杀 → 抽 1 张
		{
			FHexBattleState State(7001);
			SetupFullBattle(State, HexK::HeroSpawnCol, HexK::HeroSpawnRow + 1);
			State.RuneLoadout.SetSlot(0,
				FHexRuneLibrary::FindRune(TEXT("rune_soul_eater")));
			State.RebuildRuleAggregate();

			// 敌人削到 1 HP，保证这一击必杀
			for (FHexUnit& U : State.GetUnitsMutable())
			{
				if (U.Team == EHexTeam::Enemy) { U.HP = 1; }
			}

			FHexBattleFlow Flow(State);
			Flow.SetCardLookup([](FName Id) { return FHexContentLibrary::FindCard(Id); });
			Flow.BeginBattle();

			const int32 HandBefore = State.Piles.NumHand();

			// 用固定卡《盾击》贴身击杀
			bool bKilled = false;
			for (const FHexCardInstance& C : State.FixedCards)
			{
				const FHexCardData* Card = FHexContentLibrary::FindCard(C.CardId);
				if (!Card || Card->CardType != EHexCardType::Attack) { continue; }

				TArray<FIntVector> Targets;
				Flow.GetLegalTargets(C.Uid, Targets);
				for (const FIntVector& T : Targets)
				{
					const FHexUnit* U = State.FindUnitAtCell(T);
					if (U && U->Team == EHexTeam::Enemy && U->bIsAlive)
					{
						bKilled = (Flow.PlayCard(C.Uid, T) == EHexPlayResult::Success);
						break;
					}
				}
				if (bKilled) { break; }
			}

			Ctx.Check(TEXT("《食魂》前置：成功击杀敌人"), bKilled,
				TEXT("没打出击杀 → 本断言无从检验，请检查场景摆放"));

			if (bKilled)
			{
				// 打出 1 张（手牌 -1）+ 符文抽 1 张（手牌 +1）→ 净变化 -1+1 = 0
				// 若符文没生效，手牌会是 HandBefore-1。
				Ctx.Check(TEXT("《食魂》击杀后真的抽到了牌"),
					State.Piles.NumHand() >= HandBefore,
					FString::Printf(
						TEXT("手牌 %d → %d：击杀未补牌，说明 OnKill 链路断了"),
						HandBefore, State.Piles.NumHand()));
			}
		}

		// ── 《焚心》：暴击 → 施加 2 层燃烧
		{
			FHexBattleState State(7002);
			SetupFullBattle(State, HexK::HeroSpawnCol, HexK::HeroSpawnRow + 1);
			State.RuneLoadout.SetSlot(0,
				FHexRuneLibrary::FindRune(TEXT("rune_heart_burn")));
			State.RebuildRuleAggregate();

			// 暴击拉满 + 敌人厚血（避免被打死后查不到状态）
			if (FHexUnit* Hero = State.GetHero()) { Hero->CRIT = 100; }
			for (FHexUnit& U : State.GetUnitsMutable())
			{
				if (U.Team == EHexTeam::Enemy) { U.HPMax = 500; U.HP = 500; }
			}

			FHexBattleFlow Flow(State);
			Flow.SetCardLookup([](FName Id) { return FHexContentLibrary::FindCard(Id); });
			Flow.BeginBattle();

			int32 EnemyId = -1;
			for (const FHexUnit& U : State.GetUnits())
			{
				if (U.Team == EHexTeam::Enemy) { EnemyId = U.Id; break; }
			}

			// 贴身攻击一次（必暴击）
			for (const FHexCardInstance& C : State.FixedCards)
			{
				const FHexCardData* Card = FHexContentLibrary::FindCard(C.CardId);
				if (!Card || Card->CardType != EHexCardType::Attack) { continue; }
				TArray<FIntVector> Targets;
				Flow.GetLegalTargets(C.Uid, Targets);
				bool bDone = false;
				for (const FIntVector& T : Targets)
				{
					const FHexUnit* U = State.FindUnitAtCell(T);
					if (U && U->Team == EHexTeam::Enemy && U->bIsAlive)
					{
						bDone = (Flow.PlayCard(C.Uid, T) == EHexPlayResult::Success);
						break;
					}
				}
				if (bDone) { break; }
			}

			const FHexUnit* Enemy = State.FindUnit(EnemyId);
			Ctx.Check(TEXT("《焚心》暴击后目标真的挂上了燃烧"),
				Enemy && Enemy->GetStatusStacks(FHexStatusLibrary::Burn) > 0,
				TEXT("暴击未施加燃烧 → OnCrit 链路断了，组合③（焚心+寻疵）无法成立"));
		}

		// ── 《轮回护符》：洗回卡组 → 获得格挡
		{
			FHexBattleState State(7003);
			SetupFullBattle(State, HexK::HeroSpawnCol, HexK::HeroSpawnRow + 3);
			State.RuneLoadout.SetSlot(0,
				FHexRuneLibrary::FindRune(TEXT("rune_cycle_ward")));
			State.RebuildRuleAggregate();

			FHexBattleFlow Flow(State);
			Flow.SetCardLookup([](FName Id) { return FHexContentLibrary::FindCard(Id); });
			Flow.BeginBattle();

			// ⚠️ 必须先把抽牌堆掏空，洗回才会发生。
			//    卡组只有 5 张、每回合抽 3，所以过两个回合必然洗回。
			int32 BlockGained = 0;
			for (int32 R = 0; R < 4 && !Flow.IsBattleOver(); ++R)
			{
				Flow.EndPlayerTurn();
				if (const FHexUnit* Hero = State.GetHero())
				{
					BlockGained = FMath::Max(BlockGained, Hero->Block);
				}
			}

			Ctx.Check(TEXT("《轮回护符》洗回卡组后真的获得了格挡"),
				BlockGained > 0,
				TEXT("洗回未产生格挡 → OnDeckReshuffled 链路断了，"
					 "组合①（薄刃契+轮回护符+空匣）无法成立"));
		}
	}

	// ═══════════════════════════════════════════ 效果表达力（P1）

	void CheckEffectExpressiveness(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("符文效果表达力（P1）"));

		// ── CounterThreshold：每 N 次才触发一次
		//
		// ⚠️ 这个字段原先【只声明、从未被读取】——
		//    写了 CounterThreshold=3 的符文会每次都触发，
		//    等于"每 3 次"的设计意图被静默忽略。
		//    §6.3 的示例符文「你每移动 3 格，下一次攻击附加追击」
		//    完全依赖它。
		{
			FHexBattleState State(7101);
			SetupFullBattle(State, HexK::HeroSpawnCol, HexK::HeroSpawnRow + 2);

			// 造一个"每 3 次回合开始才给 1 点格挡"的符文
			FHexRuneData R;
			R.Id = TEXT("__counter_probe");
			R.DisplayName = TEXT("计数探针");
			FHexRuneTrigger T;
			T.When = EHexTriggerTiming::OnRoundStart;
			T.Effects = { FHexEffectStep::MakeBlock(5.0f, NAME_None, 0.0f) };
			T.CounterThreshold = 3;
			T.MaxPerRound = -1;
			T.MaxPerBattle = -1;
			R.Triggers = { T };

			State.RuneLoadout.SetSlot(0, &R);
			State.RebuildRuleAggregate();

			FHexTriggerBus Bus;
			Bus.RebuildListeners(State);
			Bus.ResetBattleCounters();

			FHexTriggerContext TCtx;
			TCtx.SourceUnitId = State.HeroUnitId;

			// 前两次应当只攒计数、不产出动作
			FHexActionQueue Q1;
			Bus.Emit(EHexTriggerTiming::OnRoundStart, TCtx, State, Q1);
			Ctx.CheckEqual(TEXT("CounterThreshold=3 第 1 次不触发"), Q1.Num(), 0);

			FHexActionQueue Q2;
			Bus.Emit(EHexTriggerTiming::OnRoundStart, TCtx, State, Q2);
			Ctx.CheckEqual(TEXT("CounterThreshold=3 第 2 次不触发"), Q2.Num(), 0);

			// 第三次攒满 → 产出
			FHexActionQueue Q3;
			Bus.Emit(EHexTriggerTiming::OnRoundStart, TCtx, State, Q3);
			Ctx.Check(TEXT("CounterThreshold=3 第 3 次触发"),
				Q3.Num() > 0,
				TEXT("攒满 3 次仍未触发 → CounterThreshold 未被实现或语义写反"));

			// 触发后清零 → 第 4 次又不触发
			FHexActionQueue Q4;
			Bus.Emit(EHexTriggerTiming::OnRoundStart, TCtx, State, Q4);
			Ctx.CheckEqual(TEXT("触发后计数清零（第 4 次不触发）"), Q4.Num(), 0);
		}

		// ── 未实现的 EffectOp 必须记违规，不得静默丢弃
		//
		// ⚠️ TriggerBus 只实现了 24 个算子中的 8 个。
		//    原先走到 default 就无声跳过，作者会以为"符文很弱"
		//    然后去调数值，永远查不到真因。
		{
			FHexBattleState State(7102);
			SetupFullBattle(State, HexK::HeroSpawnCol, HexK::HeroSpawnRow + 2);

			// 用一个 TriggerBus 尚未实现的算子（改地形）
			FHexRuneData R;
			R.Id = TEXT("__unimpl_probe");
			R.DisplayName = TEXT("未实现算子探针");
			FHexRuneTrigger T;
			T.When = EHexTriggerTiming::OnRoundStart;
			T.Effects = { FHexEffectStep::MakeOp(EHexEffectOp::ModifyTerrain) };
			R.Triggers = { T };

			State.RuneLoadout.SetSlot(0, &R);
			State.RebuildRuleAggregate();

			FHexTriggerBus Bus;
			Bus.RebuildListeners(State);
			Bus.ResetBattleCounters();

			const int32 ViolationsBefore = State.GetRuleViolations().Num();

			FHexActionQueue Q;
			FHexTriggerContext TCtx;
			TCtx.SourceUnitId = State.HeroUnitId;
			Bus.Emit(EHexTriggerTiming::OnRoundStart, TCtx, State, Q);

			Ctx.Check(TEXT("未实现的 EffectOp 会记录违规（不静默丢弃）"),
				State.GetRuleViolations().Num() > ViolationsBefore,
				TEXT("未实现算子被静默跳过 → 符文哑火且无任何线索，"
					 "这正是最难排查的一类缺陷"));
		}
	}

	// ═══════════════════════════════════════════ 新启用规则的实证（P2）
	//
	// ══════════════════════════════════════════════════════════════
	// 为什么加了符文还必须单独验这几条规则
	// ══════════════════════════════════════════════════════════════
	// 这 4 条 GameRule 在 RuleBook 里实现已久，但此前【没有任何符文
	// 使用它们】—— 等于那几段实现从来没在真实路径上跑过。
	// 现在第二批符文开始用了，必须证明"规则改写真的传导到了结果"，
	// 而不是又一次"写了但不生效"。
	//
	// 试玩机器人不会装备符文，所以批量试玩【测不到】这些 ——
	// 200 局全跑完数据一模一样，那是假绿。
	void CheckNewlyUsedRules(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("新启用的规则改写实证（P2）"));

		auto MakeRunWithRune = [](FHexBattleState& State, const TCHAR* RuneId)
		{
			FHexLayouts::Build(TEXT("open_hall"), State.Grid);
			SpawnHero(State);
			SpawnEnemy(State, TEXT("biting_hound"),
				HexK::HeroSpawnCol, HexK::HeroSpawnRow + 2);
			State.RebuildOccupancy();

			if (const FHexHeroData* H = FHexContentLibrary::FindHero(TEXT("warden")))
			{
				TArray<FHexCardInstance> Deck;
				FHexContentLibrary::BuildStartingDeck(*H, Deck, State.FixedCards);
				State.Piles.BeginBattle(Deck, State.Rng);
				State.HeroEnergyMaxBase = H->EnergyMax;
				State.HeroDrawBase = H->CardsDrawnPerTurn;
				State.HeroPassiveRules = H->PassiveRules;
			}
			State.RuneLoadout.SetSlot(0, FHexRuneLibrary::FindRune(RuneId));
			State.RebuildRuleAggregate();
		};

		// ── BlockMultiplier：《磐石誓约》格挡 ×1.5
		{
			FHexBattleState Base(8001);
			MakeRunWithRune(Base, TEXT("rune_whetstone"));   // 不改格挡的符文作基线

			FHexBattleState Buffed(8001);
			MakeRunWithRune(Buffed, TEXT("rune_bulwark_oath"));

			Ctx.CheckNearlyEqual(TEXT("基线格挡乘区 = 1.0"),
				FHexRuleBook::BlockMultiplier(Base), 1.0f);
			Ctx.CheckNearlyEqual(TEXT("《磐石誓约》使格挡乘区 = 1.5"),
				FHexRuleBook::BlockMultiplier(Buffed), 1.5f);

			// 抽牌数 -1 的代价也必须真实存在
			Ctx.CheckEqual(TEXT("《磐石誓约》的抽牌代价真实存在"),
				FHexRuleBook::CardsDrawnPerTurn(Buffed),
				FHexRuleBook::CardsDrawnPerTurn(Base) - 1);
		}

		// ── CritDamageMultiplier：《空手》+0.6
		{
			FHexBattleState Base(8002);
			MakeRunWithRune(Base, TEXT("rune_whetstone"));

			FHexBattleState Buffed(8002);
			MakeRunWithRune(Buffed, TEXT("rune_empty_hand"));

			Ctx.Check(TEXT("《空手》提升暴击伤害倍率"),
				FHexRuleBook::CritDamageMultiplier(Buffed)
				> FHexRuleBook::CritDamageMultiplier(Base),
				FString::Printf(TEXT("基线=%.2f 装备后=%.2f"),
					FHexRuleBook::CritDamageMultiplier(Base),
					FHexRuleBook::CritDamageMultiplier(Buffed)));

			Ctx.CheckEqual(TEXT("《空手》的手牌上限代价真实存在"),
				FHexRuleBook::HandLimit(Buffed),
				FHexRuleBook::HandLimit(Base) - 4);
		}

		// ── NoDrawFixedHand：《定式》
		{
			FHexBattleState Base(8003);
			MakeRunWithRune(Base, TEXT("rune_whetstone"));

			FHexBattleState Fixed(8003);
			MakeRunWithRune(Fixed, TEXT("rune_fixed_form"));

			Ctx.Check(TEXT("基线不启用固定手牌"),
				!FHexRuleBook::IsFixedHand(Base), TEXT(""));
			Ctx.Check(TEXT("《定式》启用固定手牌规则"),
				FHexRuleBook::IsFixedHand(Fixed),
				TEXT("NoDrawFixedHand 此前无任何符文使用 —— "
					 "这条规则的实现从未被真实验证过"));
		}

		// ── SizeClassOverride + KnockbackImmune：《巨化》
		{
			FHexBattleState Base(8004);
			MakeRunWithRune(Base, TEXT("rune_whetstone"));

			FHexBattleState Big(8004);
			MakeRunWithRune(Big, TEXT("rune_titanize"));

			const FHexUnit* BaseHero = Base.GetHero();
			const FHexUnit* BigHero = Big.GetHero();

			// ⚠️ KnockbackResistOf 返回的是【抗性数值】而不是布尔 ——
			//    接口名是 Immune 但语义是 Resist，容易看错。
			if (BaseHero && BigHero)
			{
				Ctx.Check(TEXT("《巨化》提升击退抗性"),
					FHexRuleBook::KnockbackResistOf(Big, *BigHero)
					> FHexRuleBook::KnockbackResistOf(Base, *BaseHero),
					FString::Printf(TEXT("基线=%d 装备后=%d"),
						FHexRuleBook::KnockbackResistOf(Base, *BaseHero),
						FHexRuleBook::KnockbackResistOf(Big, *BigHero)));

				// 体型覆写：§6.3 示例 F 点名要求，HexRuleBook.h 也点名过
				Ctx.Check(TEXT("基线体型为 S"),
					FHexRuleBook::SizeClassOf(Base, *BaseHero) == EHexSizeClass::S,
					TEXT(""));
				Ctx.Check(TEXT("《巨化》把体型改写为 M（SizeClassOverride 生效）"),
					FHexRuleBook::SizeClassOf(Big, *BigHero) == EHexSizeClass::M,
					TEXT("SizeClassOverride 此前无符文使用 —— "
						 "§6.3 示例 F《巨化》一直不存在，这条实现从未被验证"));
			}
		}
	}

	// ═══════════════════════════════════════════ 敌人 AI：意图是承诺

	void CheckEnemyAI(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("敌人 AI：意图是承诺（§8.7）"));

		// ── 生成意图
		{
			FHexBattleState State(20);
			SetupBattle(State, TEXT("biting_hound"), 4, 6);

			FHexUnit* Enemy = State.FindUnit(2);
			if (!Enemy)
			{
				Ctx.Fail(TEXT("AI 测试前置条件"), TEXT("敌人未生成"));
				return;
			}

			FHexEnemyAI::Decide(State, *Enemy);

			Ctx.Check(TEXT("生成了有效意图"), Enemy->Intent.IsValid(),
				TEXT("意图无效 → 敌人回合什么都不做"));

			// ⚠️ §13.2 要求玩家能读出"这一击能不能躲"，
			//    所以意图必须携带足够的显示信息。
			const bool bHasTarget =
				Enemy->Intent.TargetCells.Num() > 0
				|| Enemy->Intent.TrackedUnitId >= 0
				|| Enemy->Intent.Kind == EHexIntentKind::Move
				|| Enemy->Intent.Kind == EHexIntentKind::Rotate
				|| Enemy->Intent.Kind == EHexIntentKind::Buff;
			Ctx.Check(TEXT("意图携带目标信息（可显示）"), bHasTarget,
				TEXT("意图无目标信息 → UI 画不出预警"));
		}

		// ── 冻结：重复 Decide 之间状态不变则意图不变
		{
			FHexBattleState A(21);
			FHexBattleState B(21);
			SetupBattle(A, TEXT("biting_hound"), 4, 6);
			SetupBattle(B, TEXT("biting_hound"), 4, 6);

			FHexUnit* EA = A.FindUnit(2);
			FHexUnit* EB = B.FindUnit(2);
			if (EA && EB)
			{
				FHexEnemyAI::Decide(A, *EA);
				FHexEnemyAI::Decide(B, *EB);

				Ctx.Check(TEXT("同 seed 同局面产生相同意图"),
					EA->Intent.Kind == EB->Intent.Kind
					&& EA->Intent.PredictedDamage == EB->Intent.PredictedDamage
					&& EA->Intent.TargetCells == EB->Intent.TargetCells,
					TEXT("意图不确定 → 回放与种子分享失效"));
			}
		}

		// ── ⭐ 可躲型：玩家走开后目标格【不重算】
		//
		// ⚠️ 这是整个套件最重要的断言。
		//    若执行时重算目标，玩家走位规避就无效 ——
		//    「预警 + 走位」核心循环失效，且不报任何错，
		//    玩家只会觉得"我明明躲开了还是挨打"。
		{
			FHexBattleState State(22);
			// ⚠️ 扑咬犬（Aggressive，扑击射程 2）放在距离 2 处：
			//    在射程内 → 产出攻击意图；距离 >1 → 不触发贴身转追踪规则。
			//    这是可躲型攻击意图唯一能稳定复现的配置。
			SetupBattle(State, TEXT("biting_hound"),
				HexK::HeroSpawnCol, HexK::HeroSpawnRow + 2);

			FHexUnit* Enemy = State.FindUnit(2);
			FHexUnit* Hero = State.GetHero();
			if (Enemy && Hero)
			{
				// 强制为可躲型
				Enemy->IntentTargeting = EHexIntentTargeting::FixedTile;
				FHexEnemyAI::Decide(State, *Enemy);

				const TArray<FIntVector> FrozenCells = Enemy->Intent.TargetCells;
				const bool bIsFixedAttack =
					Enemy->Intent.Targeting == EHexIntentTargeting::FixedTile
					&& (Enemy->Intent.Kind == EHexIntentKind::Attack
						|| Enemy->Intent.Kind == EHexIntentKind::MultiAttack)
					&& FrozenCells.Num() > 0;

				if (bIsFixedAttack)
				{
					// 玩家瞬移到远处
					Hero->Anchor = FHexCoord::OffsetToCube(1, 1);
					State.RebuildOccupancy();

					FHexActionQueue Queue;
					FHexEnemyAI::ExecuteIntent(State, *Enemy, Queue);

					// 冻结的目标格必须原样保留
					Ctx.Check(TEXT("⭐ 可躲型：执行后目标格未被重算"),
						Enemy->Intent.TargetCells == FrozenCells,
						TEXT("执行时重算了目标 → 走位规避失效，P2 支柱崩塌"));

					// 玩家已走开，不该掉血
					Ctx.CheckEqual(TEXT("⭐ 可躲型：走开后玩家未受伤"),
						Hero->HP, Hero->HPMax);
				}
				else
				{
					// 布阵没能产出可躲型攻击意图 —— 这是测试自身的问题，
					// 必须显式失败而不是静默跳过（静默跳过 = 这条断言白写）
					Ctx.Fail(TEXT("⭐ 可躲型：布阵产出可躲型攻击意图"),
						FString::Printf(TEXT("实际意图 Kind=%d Targeting=%d 目标格=%d"),
							static_cast<int32>(Enemy->Intent.Kind),
							static_cast<int32>(Enemy->Intent.Targeting),
							FrozenCells.Num()));
				}
			}
		}

		// ── 追踪型：锁定单位而非格子
		{
			FHexBattleState State(23);
			// 距离 3 —— 在投石手射程 4 内，且 >1 不触发贴身规则，
			// 所以这里测的是它【天生】的追踪属性
			SetupBattle(State, TEXT("stone_slinger"), 4, 4);

			FHexUnit* Enemy = State.FindUnit(2);
			if (Enemy)
			{
				Ctx.Check(TEXT("投石手是追踪型"),
					Enemy->IntentTargeting == EHexIntentTargeting::TrackTarget,
					TEXT(""));

				FHexEnemyAI::Decide(State, *Enemy);

				// 追踪型必须锁定单位 id（UI 靠它画虚线+连线）
				if (Enemy->Intent.Kind == EHexIntentKind::Attack
					|| Enemy->Intent.Kind == EHexIntentKind::MultiAttack)
				{
					Ctx.Check(TEXT("追踪型意图锁定单位 id"),
						Enemy->Intent.TrackedUnitId >= 0
						|| Enemy->Intent.Targeting == EHexIntentTargeting::TrackTarget,
						TEXT("追踪型未锁定单位 → UI 无法区分可躲与追踪"));
				}
			}
		}

		// ── 两种意图类型都存在（§13.2 要求玩家能读出区分）
		//
		// ⚠️ 这条断言抓到过一个真实的内容缺陷：
		//    早期所有近战 Profile 射程都是 1，而两段明示规则规定
		//    距离 ≤1 一律转追踪 —— 于是近战永远产不出可躲型意图；
		//    加上唯一的远程（投石手）天生追踪，
		//    整个第一版里 FixedTile 攻击意图出现概率为 0。
		//    修法见 AttackRangeOf 中 Aggressive 的注释（射程改 2）。
		//
		// 布阵：扑咬犬距离 2（射程内、非贴身 → 可躲）
		//       投石手距离 3（射程 4 内、天生追踪 → 追踪）
		{
			bool bSawFixed = false;
			bool bSawTrack = false;

			for (uint64 Seed = 1; Seed <= 30; ++Seed)
			{
				FHexBattleState State(Seed);
				FHexLayouts::Build(TEXT("open_hall"), State.Grid);
				SpawnHero(State);
				SpawnEnemy(State, TEXT("biting_hound"),
					HexK::HeroSpawnCol, HexK::HeroSpawnRow + 2);
				SpawnEnemy(State, TEXT("stone_slinger"),
					HexK::HeroSpawnCol - 1, HexK::HeroSpawnRow + 3);
				State.RebuildOccupancy();
				State.RebuildRuleAggregate();

				FHexEnemyAI::DecideAll(State);

				for (const FHexUnit& U : State.GetUnits())
				{
					if (U.Team != EHexTeam::Enemy || !U.Intent.IsValid())
					{
						continue;
					}
					// 只看攻击类意图 —— 移动/转向意图一律是 FixedTile，
					// 拿它们来证明"存在可躲型"是自欺欺人
					if (U.Intent.Kind != EHexIntentKind::Attack
						&& U.Intent.Kind != EHexIntentKind::MultiAttack)
					{
						continue;
					}
					if (U.Intent.Targeting == EHexIntentTargeting::FixedTile)
					{
						bSawFixed = true;
					}
					if (U.Intent.Targeting == EHexIntentTargeting::TrackTarget)
					{
						bSawTrack = true;
					}
				}
			}

			Ctx.Check(TEXT("存在可躲型攻击意图（实线显示）"), bSawFixed,
				TEXT("全是追踪型 → 玩家学不到「有些攻击能躲」，走位价值减半"));
			Ctx.Check(TEXT("存在追踪型攻击意图（虚线+连线）"), bSawTrack,
				TEXT("全是可躲型 → 玩家无脑走位即可，威胁消失"));
		}

		// ── 贴身近战转追踪（两段明示规则，架构文档 §4.6）
		//
		// ⚠️ 这条规则的意义：被咬住了就躲不掉。
		//    若贴身仍是可躲型，玩家可以贴脸站着然后走 1 格躲开所有攻击。
		{
			FHexBattleState State(28);
			FHexLayouts::Build(TEXT("open_hall"), State.Grid);
			const int32 HeroId = SpawnHero(State);
			// 放在玩家正上方相邻格 → 距离 1
			SpawnEnemy(State, TEXT("biting_hound"),
				HexK::HeroSpawnCol, HexK::HeroSpawnRow + 1);
			State.RebuildOccupancy();
			State.RebuildRuleAggregate();

			FHexUnit* Enemy = State.FindUnit(2);
			const FHexUnit* Hero = State.FindUnit(HeroId);
			if (Enemy && Hero)
			{
				const int32 Dist = Enemy->DistanceToUnit(*Hero);
				Ctx.CheckEqual(TEXT("布阵距离 = 1（贴身）"), Dist, 1);

				FHexEnemyAI::Decide(State, *Enemy);
				Ctx.Check(TEXT("贴身近战转为追踪型（咬住了躲不掉）"),
					Enemy->Intent.Targeting == EHexIntentTargeting::TrackTarget,
					TEXT("贴身仍可躲 → 玩家贴脸站着走 1 格就能躲开一切"));
				Ctx.CheckEqual(TEXT("追踪型锁定了玩家 id"),
					Enemy->Intent.TrackedUnitId, HeroId);
			}
		}

		// ── 眩晕的敌人不该行动
		//
		// ⚠️ 眩晕是"打断敌人意图的唯一途径"（架构文档原话）。
		//    若眩晕不阻止执行，这条唯一途径就不存在了。
		{
			FHexBattleState State(24);
			SetupBattle(State, TEXT("biting_hound"), 4, 5);

			FHexUnit* Enemy = State.FindUnit(2);
			FHexUnit* Hero = State.GetHero();
			if (Enemy && Hero)
			{
				FHexEnemyAI::Decide(State, *Enemy);
				Enemy->ApplyStatus(FHexStatusLibrary::Stun, 1);

				Ctx.Check(TEXT("眩晕的敌人 ShouldSkipTurn"),
					Enemy->ShouldSkipTurn(),
					TEXT("眩晕未生效 → 打断敌人意图的唯一途径失效"));

				const int32 HPBefore = Hero->HP;
				FHexActionQueue Queue;
				FHexEnemyAI::ExecuteIntent(State, *Enemy, Queue);

				// 注：ExecuteIntent 是否检查眩晕由 BattleFlow 决定，
				// 这里只断言查询器可用（真正的跳过在 RunEnemyPhase）
				(void)HPBefore;
			}
		}

		// ── 预计伤害不消耗 RNG
		//
		// ⚠️ PredictDamage 会被 UI 反复调用（鼠标悬停）。
		//    若消耗 RNG，"鼠标划过战场"就会污染确定性。
		{
			FHexBattleState State(25);
			SetupBattle(State, TEXT("biting_hound"), 4, 6);

			const FHexUnit* Enemy = State.FindUnit(2);
			const FHexUnit* Hero = State.GetHero();
			if (Enemy && Hero)
			{
				const int32 DrawBefore = State.Rng.GetDrawCount();

				int32 First = 0;
				for (int32 I = 0; I < 50; ++I)
				{
					const int32 D = FHexEnemyAI::PredictDamage(State, *Enemy, *Hero);
					if (I == 0)
					{
						First = D;
					}
					else if (D != First)
					{
						Ctx.Fail(TEXT("预计伤害稳定"),
							FString::Printf(TEXT("第 %d 次得到 %d，首次 %d"), I, D, First));
						break;
					}
				}

				Ctx.CheckEqual(TEXT("PredictDamage 不消耗 RNG"),
					State.Rng.GetDrawCount(), DrawBefore);
				Ctx.Check(TEXT("预计伤害为正"), First > 0,
					FString::Printf(TEXT("预计伤害=%d"), First));

				// 扑咬犬 ATK13 打镇妖者 DEF8：13×(1-8/20)=7.8 → 7
				Ctx.CheckEqual(TEXT("扑咬犬预计伤害 = 7"), First, 7);
			}
		}

		// ── 意图预告的伤害 = 实际伤害
		//
		// ⚠️ 「意图是承诺」不只是目标格，还包括伤害数值。
		//    预告 7 点却打 12 点，玩家的格挡计算全部作废。
		{
			FHexBattleState State(26);
			SetupBattle(State, TEXT("biting_hound"), 4, 5);

			FHexUnit* Enemy = State.FindUnit(2);
			const FHexUnit* Hero = State.GetHero();
			if (Enemy && Hero)
			{
				FHexEnemyAI::Decide(State, *Enemy);

				if (Enemy->Intent.Kind == EHexIntentKind::Attack
					|| Enemy->Intent.Kind == EHexIntentKind::MultiAttack)
				{
					const int32 Predicted = FHexEnemyAI::PredictDamage(State, *Enemy, *Hero);
					Ctx.CheckEqual(TEXT("意图内冻结的伤害 = 预计伤害"),
						Enemy->Intent.PredictedDamage, Predicted);
				}
			}
		}

		// ── DecideAll 的确定性顺序
		{
			FHexBattleState A(27);
			FHexBattleState B(27);
			for (FHexBattleState* S : { &A, &B })
			{
				FHexLayouts::Build(TEXT("open_hall"), S->Grid);
				SpawnHero(*S);
				SpawnEnemy(*S, TEXT("biting_hound"), 3, 6);
				SpawnEnemy(*S, TEXT("stone_slinger"), 4, 6);
				SpawnEnemy(*S, TEXT("biting_hound"), 5, 6);
				S->RebuildOccupancy();
				S->RebuildRuleAggregate();
				FHexEnemyAI::DecideAll(*S);
			}

			bool bSame = true;
			for (int32 I = 0; I < A.GetUnits().Num() && bSame; ++I)
			{
				bSame = (A.GetUnits()[I].Intent.Kind == B.GetUnits()[I].Intent.Kind)
					&& (A.GetUnits()[I].Intent.PredictedDamage
						== B.GetUnits()[I].Intent.PredictedDamage);
			}
			Ctx.Check(TEXT("DecideAll 结果确定（同 seed 同结果）"), bSame,
				TEXT("AI 决策不确定 → 回放失效"));

			// 敌方行动顺序按 AGI 降序、同值按 id 升序
			{
				TArray<int32> Order;
				A.GetEnemyActionOrder(Order);
				bool bSorted = true;
				for (int32 I = 1; I < Order.Num(); ++I)
				{
					const FHexUnit* P = A.FindUnit(Order[I - 1]);
					const FHexUnit* C = A.FindUnit(Order[I]);
					if (!P || !C)
					{
						continue;
					}
					if (P->AGI < C->AGI || (P->AGI == C->AGI && P->Id > C->Id))
					{
						bSorted = false;
					}
				}
				Ctx.Check(TEXT("敌方行动顺序：AGI 降序 + id 升序 tiebreak"),
					bSorted, TEXT("顺序不确定 → 结果漂移"));
			}
		}

		// ── 全部 AI Profile 都能产出意图
		//
		// ⚠️ 漏实现某个 Profile 时，那种敌人会整场站着不动 ——
		//    不报错，只是"这个怪好像坏了"。
		{
			const TArray<FName> Enemies = {
				TEXT("biting_hound"), TEXT("stone_slinger"),
				TEXT("stone_golem"), TEXT("siege_worm")
			};

			for (const FName& Eid : Enemies)
			{
				FHexBattleState State(30);
				SetupBattle(State, Eid, 4, 6);

				FHexUnit* Enemy = State.FindUnit(2);
				if (!Enemy)
				{
					Ctx.Fail(FString::Printf(TEXT("[%s] 生成"), *Eid.ToString()),
						TEXT("敌人未生成（可能是 footprint 放不下）"));
					continue;
				}

				FHexEnemyAI::Decide(State, *Enemy);
				Ctx.Check(FString::Printf(TEXT("[%s] 能产出有效意图"), *Eid.ToString()),
					Enemy->Intent.IsValid(),
					TEXT("该 AI Profile 未实现 → 这种敌人会整场站着不动"));
			}
		}
	}
}

bool FHexVerifySuites::VerifyTrigger(FHexVerifyContext& Ctx)
{
	CheckTriggerOrder(Ctx);
	CheckTriggerDispatch(Ctx);
	CheckTimingCoverage(Ctx);
	CheckRuneEndToEnd(Ctx);
	CheckEffectExpressiveness(Ctx);
	CheckNewlyUsedRules(Ctx);
	return Ctx.NumFailed() == 0;
}

bool FHexVerifySuites::VerifyAI(FHexVerifyContext& Ctx)
{
	CheckEnemyAI(Ctx);
	return Ctx.NumFailed() == 0;
}
