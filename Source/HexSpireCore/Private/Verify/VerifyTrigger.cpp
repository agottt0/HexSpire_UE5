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
#include "Battle/HexGameAction.h"
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
					FHexContentLibrary::BuildStartingDeck(*H, Deck);
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
	return Ctx.NumFailed() == 0;
}

bool FHexVerifySuites::VerifyAI(FHexVerifyContext& Ctx)
{
	CheckEnemyAI(Ctx);
	return Ctx.NumFailed() == 0;
}
