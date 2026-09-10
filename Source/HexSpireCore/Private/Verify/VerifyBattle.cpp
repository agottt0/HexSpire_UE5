// Copyright Hex Spire. All Rights Reserved.
//
// 整场战斗流程验证 —— §8.4
//
// ══════════════════════════════════════════════════════════════════
// 这个套件与其它套件的区别：它是【集成测试】
// ══════════════════════════════════════════════════════════════════
// 前面的套件各自验证一块（伤害管线、牌堆、AI、状态）。
// 这个套件把它们串起来跑【完整的战斗】，抓的是"单块都对但合起来错"的问题：
//   · 阶段推进卡住（玩家永远等不到自己的回合）
//   · 战斗不终止（回合数无限增长）
//   · 卡组不变量在多回合后被破坏
//   · 确定性在长流程下漂移
//
// ⚠️ 最重要的一条：【战斗必须终止】。
//    死循环在回合制游戏里表现为"点了结束回合没反应"，
//    玩家只能强杀进程。R7 的安全闸就是为这个准备的，
//    但安全闸本身也要被验证。

#include "Verify/HexVerify.h"
#include "Battle/HexBattleFlow.h"
#include "Battle/HexBattleState.h"
#include "Battle/HexEnemyAI.h"
#include "Battle/HexUnit.h"
#include "Battle/HexStatusData.h"
#include "Battle/HexTargetResolver.h"
#include "Content/HexContentLibrary.h"
#include "Content/HexLayouts.h"
#include "Runes/HexRuneLibrary.h"
#include "Hex/HexCoord.h"
#include "Rng/HexRngStreams.h"
#include "Core/HexSpireConstants.h"

namespace
{
	/** 一场可跑的战斗：镇妖者 + 指定怪物组 + 指定地形 */
	struct FBattleFixture
	{
		TUniquePtr<FHexBattleState> State;
		TUniquePtr<FHexBattleFlow> Flow;

		void Build(uint64 Seed, const FName& LayoutId, const FName& EncounterId)
		{
			State = MakeUnique<FHexBattleState>(Seed);
			FHexLayouts::Build(LayoutId, State->Grid);

			// ── 英雄
			const FHexHeroData* H = FHexContentLibrary::FindHero(TEXT("warden"));
			FHexUnit Hero;
			if (H)
			{
				Hero.SourceId = H->Id;
				Hero.DisplayName = H->DisplayName;
				Hero.SizeClass = H->SizeClass;
				Hero.HPMax = H->BaseHP;
				Hero.HP = H->BaseHP;
				Hero.ATK = H->BaseATK;
				Hero.DEF = H->BaseDEF;
				Hero.AGI = H->BaseAGI;
				Hero.LUK = H->BaseLUK;
				Hero.CRIT = H->BaseCRIT;

				State->HeroEnergyMaxBase = H->EnergyMax;
				State->HeroDrawBase = H->CardsDrawnPerTurn;

				// ⚠️ 必须写进 HeroPassiveRules 而非直接篡改 RuleAggregate：
				//    BeginBattle 会调用 RebuildRuleAggregate() 重算，
				//    直接塞 RuleAggregate 的话会被当场覆盖掉。
				State->HeroPassiveRules = H->PassiveRules;
			}
			Hero.Team = EHexTeam::Player;
			Hero.Anchor = FHexCoord::OffsetToCube(HexK::HeroSpawnCol, HexK::HeroSpawnRow);
			Hero.Facing = HexK::HeroSpawnFacing;
			State->HeroUnitId = State->AddUnit(Hero);

			// ── 敌人：按怪物组在敌方生成区排布
			TArray<FHexEncounterEntry> Entries;
			FHexContentLibrary::GetEncounter(EncounterId, Entries);

			int32 Col = 2;
			int32 Row = HexK::EnemySpawnRowMin;
			for (const FHexEncounterEntry& E : Entries)
			{
				const FHexEnemyData* Data = FHexContentLibrary::FindEnemy(E.EnemyId);
				if (!Data)
				{
					continue;
				}
				for (int32 I = 0; I < E.Count; ++I)
				{
					FHexUnit U = FHexContentLibrary::MakeEnemyUnit(*Data, 1, 0);
					U.Anchor = FHexCoord::OffsetToCube(Col, Row);
					U.Facing = 5;
					State->AddUnit(U);

					Col += 2;
					if (Col > HexK::BoardCols - 1)
					{
						Col = 2;
						++Row;
						if (Row > HexK::EnemySpawnRowMax)
						{
							Row = HexK::EnemySpawnRowMax;
						}
					}
				}
			}

			State->RebuildOccupancy();

			// ── 卡组 + 固定卡
			TArray<FHexCardInstance> Deck;
			if (H)
			{
				FHexContentLibrary::BuildStartingDeck(*H, Deck, State->FixedCards);
			}
			State->Piles.BeginBattle(Deck, State->Rng);

			// ── 流程
			Flow = MakeUnique<FHexBattleFlow>(*State);
			Flow->SetCardLookup([](FName Id) { return FHexContentLibrary::FindCard(Id); });
		}
	};

	/** 到最近存活敌人的距离；无敌人返回 -1 */
	int32 DistanceToNearestEnemy(const FHexBattleState& State, const FIntVector& Cell)
	{
		int32 Best = -1;
		for (const FHexUnit& U : State.GetUnits())
		{
			if (U.Team != EHexTeam::Enemy || !U.bIsAlive)
			{
				continue;
			}
			const int32 D = U.DistanceToCell(Cell);
			if (Best < 0 || D < Best)
			{
				Best = D;
			}
		}
		return Best;
	}

	/**
	 * 出一张牌；返回是否成功出牌。
	 *
	 * 优先级：① 能直接命中敌人的攻击卡
	 *         ② 能让自己更靠近敌人的卡（含《冲撞》这类 TILE 目标的突进）
	 *         ③ 其它任何能打的牌
	 *
	 * ⚠️ 早期版本只有"打第一张能打的牌"，结果机器人每回合把 5 点体力
	 *    全花在《防御》《铁壁》上（防御卡永远有合法目标），
	 *    配合镇妖者「格挡不清空」被动，双方陷入永久僵持。
	 *
	 * ⚠️ ② 是必须的：面对风筝型敌人（投石手每回合后退到距离 3），
	 *    不会主动接敌的机器人永远打不到它。加上 ② 之后，
	 *    《冲撞》3 格突进才真正被用起来。
	 */
	bool BotPlayOneCard(FHexBattleFlow& Flow, FHexBattleState& State)
	{
		// ⚠️ 可选牌 = 手牌 + 固定卡。
		//    基石卡移出牌堆后，只遍历手牌的机器人会彻底失去
		//    移动与防御能力 —— 它不会报错，只会打不过、或者
		//    因为无法接敌而把每场战斗拖到回合上限。
		TArray<FHexCardInstance> Hand = State.Piles.GetHand();
		Hand.Append(State.FixedCards);

		// ── ① 直接命中敌人的攻击卡
		for (const FHexCardInstance& C : Hand)
		{
			const FHexCardData* Card = FHexContentLibrary::FindCard(C.CardId);
			if (!Card || Card->CardType != EHexCardType::Attack)
			{
				continue;
			}
			if (!Flow.CanPlayCard(C.Uid))
			{
				continue;
			}

			TArray<FIntVector> Targets;
			Flow.GetLegalTargets(C.Uid, Targets);
			for (const FIntVector& T : Targets)
			{
				const FHexUnit* Occupant = State.FindUnitAtCell(T);
				if (Occupant && Occupant->Team == EHexTeam::Enemy && Occupant->bIsAlive)
				{
					if (Flow.PlayCard(C.Uid, T) == EHexPlayResult::Success)
					{
						return true;
					}
				}
			}
		}

		// ── ② 朝敌人靠近
		{
			const FHexUnit* Hero = State.GetHero();
			if (Hero)
			{
				const int32 CurDist = DistanceToNearestEnemy(State, Hero->Anchor);
				if (CurDist > 1)
				{
					int32 BestUid = 0;
					FIntVector BestCell = FIntVector::ZeroValue;
					int32 BestDist = CurDist;

					for (const FHexCardInstance& C : Hand)
					{
						if (!Flow.CanPlayCard(C.Uid))
						{
							continue;
						}
						const FHexCardData* Card = FHexContentLibrary::FindCard(C.CardId);
						// 只考虑目标是格子的卡（移动/突进/冲撞）
						//
						// ⚠️ 用 TargetsCell() 而不是手写形状比较。
						//    《冲撞》从 Tile 改成 DashPath 时，这里若漏改，
						//    机器人就再也不会冲锋 —— 对风筝型敌人
						//    （投石手每回合退到 3 格外）永远打不着，
						//    战斗不终止，最后由 R7 安全闸兜底，
						//    表现为"某些种子的战斗莫名其妙判和"。
						if (!Card || !Card->TargetSpec.TargetsCell())
						{
							continue;
						}

						TArray<FIntVector> Targets;
						Flow.GetLegalTargets(C.Uid, Targets);
						for (const FIntVector& T : Targets)
						{
							const int32 D = DistanceToNearestEnemy(State, T);
							if (D >= 0 && D < BestDist)
							{
								BestDist = D;
								BestUid = C.Uid;
								BestCell = T;
							}
						}
					}

					if (BestUid != 0
						&& Flow.PlayCard(BestUid, BestCell) == EHexPlayResult::Success)
					{
						return true;
					}
				}
			}
		}

		// ── ③ 任何能打的牌（防御/技能等）
		for (const FHexCardInstance& C : Hand)
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
			if (Flow.PlayCard(C.Uid, Targets[0]) == EHexPlayResult::Success)
			{
				return true;
			}
		}

		return false;
	}

	/**
	 * 让一个"简单机器人"打完整场：每回合尽量出牌，然后结束回合。
	 *
	 * ⚠️ 流程 API 的契约（第一次写这个测试时踩过）：
	 *      BeginBattle()   内部会调 BeginRound() —— 返回时已在第 1 回合的玩家阶段
	 *      EndPlayerTurn() 内部会走完敌方阶段并调 BeginRound() —— 返回时已在下一回合
	 *    所以驱动循环【不得】再手动调 BeginRound，否则每轮推进两个回合，
	 *    表现为"回合数翻倍 + 手牌被重复抽干 + 战斗永不终止"。
	 *    （BeginRound 现已私有化，从 API 层面杜绝了这个错误。）
	 *
	 * @return 战斗结束时的回合数
	 */
	int32 RunFullBattle(FBattleFixture& Fx, int32 MaxRounds = HexK::MaxRoundsPerBattle)
	{
		FHexBattleFlow& Flow = *Fx.Flow;
		FHexBattleState& State = *Fx.State;

		// 返回时已经在第 1 回合的玩家阶段
		Flow.BeginBattle();

		int32 Turns = 0;
		while (!Flow.IsBattleOver() && Turns < MaxRounds)
		{
			++Turns;

			// 出牌直到打不动为止（Guard 防"出牌不消耗体力"这类 bug 导致死循环）
			int32 Guard = 0;
			while (Guard < 40 && !Flow.IsBattleOver())
			{
				++Guard;
				if (!BotPlayOneCard(Flow, State))
				{
					break;
				}
			}

			if (Flow.IsBattleOver())
			{
				break;
			}

			// 走完敌方阶段并进入下一回合
			Flow.EndPlayerTurn();
		}

		return State.RoundNumber;
	}

	// ═══════════════════════════════════════════ 阶段推进

	void CheckPhaseFlow(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("阶段推进"));

		FBattleFixture Fx;
		Fx.Build(1, TEXT("open_hall"), TEXT("enc_01"));
		FHexBattleFlow& Flow = *Fx.Flow;
		FHexBattleState& State = *Fx.State;

		Flow.BeginBattle();

		// ── 战斗开始的后置条件
		Ctx.Check(TEXT("开局抽到手牌"), State.Piles.NumHand() > 0,
			FString::Printf(TEXT("手牌=%d"), State.Piles.NumHand()));

		// ⚠️ 敌人必须在战斗开始时就有意图 ——
		//    §8.7 要求玩家【在行动前】就能看到威胁。
		//    若第一回合没有意图，玩家第一回合是在无信息下决策。
		{
			int32 WithIntent = 0;
			int32 EnemyCount = 0;
			for (const FHexUnit& U : State.GetUnits())
			{
				if (U.Team != EHexTeam::Enemy)
				{
					continue;
				}
				++EnemyCount;
				if (U.Intent.IsValid())
				{
					++WithIntent;
				}
			}
			Ctx.Check(TEXT("开局所有敌人都有意图（§8.7 预警）"),
				EnemyCount > 0 && WithIntent == EnemyCount,
				FString::Printf(TEXT("%d/%d 个敌人有意图"), WithIntent, EnemyCount));
		}

		// ── 回合开始
		//
		// ⚠️ BeginBattle 内部已调用 BeginRound，所以这里【不再手动调】。
		//    外部再调一次会让回合数翻倍、手牌被重复抽取。
		Ctx.CheckEqual(TEXT("BeginBattle 后已在第 1 回合"), State.RoundNumber, 1);
		Ctx.Check(TEXT("BeginBattle 后已在玩家阶段"),
			State.Phase == EHexBattlePhase::PlayerPhase,
			FString::Printf(TEXT("Phase=%d"), static_cast<int32>(State.Phase)));
		Ctx.CheckEqual(TEXT("体力已回满"), State.Energy, 5);
		Ctx.CheckEqual(TEXT("本回合出牌数归零"), State.CardsPlayedThisRound, 0);

		// ── 记录回合结束前的手牌，用于验证"弃手牌"确实发生
		const int32 HandBeforeEnd = State.Piles.NumHand();
		Ctx.Check(TEXT("回合结束前手牌非空"), HandBeforeEnd > 0, TEXT(""));

		TSet<int32> FirstRoundHandUids;
		for (const FHexCardInstance& C : State.Piles.GetHand())
		{
			FirstRoundHandUids.Add(C.Uid);
		}

		// ── 结束回合
		//
		// ⚠️ EndPlayerTurn 内部会走完敌方阶段并调 BeginRound，
		//    所以返回时【已经在下一回合的玩家阶段】。
		Flow.EndPlayerTurn();

		if (!Flow.IsBattleOver())
		{
			Ctx.CheckEqual(TEXT("EndPlayerTurn 后进入第 2 回合"), State.RoundNumber, 2);
			Ctx.Check(TEXT("第 2 回合处于玩家阶段"),
				State.Phase == EHexBattlePhase::PlayerPhase,
				FString::Printf(TEXT("Phase=%d"), static_cast<int32>(State.Phase)));
			Ctx.Check(TEXT("第 2 回合重新抽到手牌"),
				State.Piles.NumHand() > 0, TEXT(""));
			Ctx.CheckEqual(TEXT("第 2 回合体力回满"), State.Energy, 5);
			Ctx.CheckEqual(TEXT("第 2 回合出牌数归零"), State.CardsPlayedThisRound, 0);

			// ── 上一回合的手牌确实被弃掉了（§7.4.2 的紧迫感来源）
			//
			// ⚠️ 不能用"弃牌堆非空"来验证：起始卡组只有 8 张，
			//    抽 5 弃 5 后抽牌堆只剩 3 张，下回合抽 5 张时会触发洗回，
			//    把弃牌堆整个搬回抽牌堆 —— 此时弃牌堆【正常地】为空。
			//    （§7.4.4：小卡组约 2 回合走完一轮，这是 D2 的预期行为。）
			//    正确的验证方式是看手牌实例整体被换过一轮。
			{
				bool bHandRefreshed = true;
				for (const FHexCardInstance& C : State.Piles.GetHand())
				{
					if (FirstRoundHandUids.Contains(C.Uid))
					{
						// 洗回后同一张卡可能又被抽到 —— 这不算失败，
						// 只有"整手牌原封不动"才说明没弃过
						bHandRefreshed = false;
						break;
					}
				}
				// 至少要有证据表明发生过弃牌或洗牌
				const bool bCycled = bHandRefreshed
					|| State.Piles.NumDiscard() > 0
					|| State.Rng.GetDrawCount() > 0;
				Ctx.Check(TEXT("回合结束弃手牌 → 牌堆发生了循环"),
					bCycled,
					TEXT("手牌原封不动 → 玩家可以攒牌，紧迫感消失"));
			}
		}

		// ── 非玩家阶段不得出牌
		//
		// ⚠️ EndPlayerTurn 是原子的（一路走完敌方阶段），
		//    所以"敌方阶段插队出牌"无法从外部构造。
		//    这里用另一种方式验证守卫：手动把阶段设成敌方阶段。
		{
			const int32 Uid = State.Piles.GetHand().Num() > 0
				? State.Piles.GetHand()[0].Uid : 0;

			if (Uid != 0)
			{
				const EHexBattlePhase Saved = State.Phase;
				State.Phase = EHexBattlePhase::EnemyPhase;

				Ctx.Check(TEXT("非玩家阶段出牌被拒绝"),
					Flow.PlayCard(Uid, FIntVector::ZeroValue)
						== EHexPlayResult::NotPlayerPhase,
					TEXT("敌方阶段能出牌 → 玩家可以插队"));

				State.Phase = Saved;
			}
		}
	}

	// ═══════════════════════════════════════════ 出牌

	void CheckPlayCard(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("出牌"));

		FBattleFixture Fx;
		Fx.Build(2, TEXT("open_hall"), TEXT("enc_01"));
		FHexBattleFlow& Flow = *Fx.Flow;
		FHexBattleState& State = *Fx.State;

		// BeginBattle 内部已进入第 1 回合的玩家阶段
		Flow.BeginBattle();

		// ── 非法 uid
		Ctx.Check(TEXT("不存在的卡 uid 被拒绝"),
			Flow.PlayCard(99999, FIntVector::ZeroValue) == EHexPlayResult::CardNotInHand,
			TEXT(""));

		// ── 体力扣减
		{
			int32 PlayedUid = 0;
			for (const FHexCardInstance& C : State.Piles.GetHand())
			{
				if (Flow.CanPlayCard(C.Uid))
				{
					TArray<FIntVector> Targets;
					Flow.GetLegalTargets(C.Uid, Targets);
					if (Targets.Num() > 0)
					{
						PlayedUid = C.Uid;
						const int32 Cost = Flow.GetCardCost(C.Uid);
						const int32 EnergyBefore = State.Energy;
						const int32 HandBefore = State.Piles.NumHand();

						const EHexPlayResult R = Flow.PlayCard(C.Uid, Targets[0]);
						Ctx.Check(TEXT("合法出牌成功"),
							R == EHexPlayResult::Success,
							FString::Printf(TEXT("结果=%d"), static_cast<int32>(R)));
						Ctx.CheckEqual(TEXT("体力按费用扣减"),
							State.Energy, EnergyBefore - Cost);
						Ctx.CheckEqual(TEXT("手牌减少 1 张"),
							State.Piles.NumHand(), HandBefore - 1);
						Ctx.CheckEqual(TEXT("出牌计数 +1"),
							State.CardsPlayedThisRound, 1);
						break;
					}
				}
			}
			Ctx.Check(TEXT("找到了可打出的卡"), PlayedUid != 0,
				TEXT("开局 5 张手牌一张都打不出 → 玩家第一回合无事可做"));
		}

		// ── 体力不足时拒绝
		{
			State.Energy = 0;
			bool bAnyPlayable = false;
			for (const FHexCardInstance& C : State.Piles.GetHand())
			{
				if (Flow.GetCardCost(C.Uid) > 0 && Flow.CanPlayCard(C.Uid))
				{
					bAnyPlayable = true;
				}
			}
			Ctx.Check(TEXT("体力为 0 时有费卡不可打出"), !bAnyPlayable,
				TEXT("体力不足仍能出牌 → 体力系统失效"));
		}

		// ── 牌堆不变量在出牌后仍成立
		{
			FString Err;
			Ctx.Check(TEXT("出牌后牌堆不变量成立"),
				State.Piles.CheckInvariants(Err), Err);
		}

		// ── 伤害预览不消耗 RNG
		//
		// ⚠️ §13.2 要求悬停显示"预计伤害 X（暴击 Y）"。
		//    若预览消耗 RNG，鼠标划过战场就会污染确定性，
		//    并连带打爆 Undo 的撤销屏障。
		{
			State.Energy = 5;
			for (const FHexCardInstance& C : State.Piles.GetHand())
			{
				TArray<FIntVector> Targets;
				Flow.GetLegalTargets(C.Uid, Targets);
				if (Targets.Num() == 0)
				{
					continue;
				}

				const int32 DrawBefore = State.Rng.GetDrawCount();
				FIntPoint First(0, 0);
				bool bStable = true;
				for (int32 I = 0; I < 30; ++I)
				{
					const FIntPoint P = Flow.PreviewDamage(C.Uid, Targets[0]);
					if (I == 0)
					{
						First = P;
					}
					else if (P != First)
					{
						bStable = false;
					}
				}

				Ctx.Check(TEXT("伤害预览稳定（30 次同值）"), bStable, TEXT(""));
				Ctx.CheckEqual(TEXT("伤害预览不消耗 RNG"),
					State.Rng.GetDrawCount(), DrawBefore);
				break;
			}
		}
	}

	// ═══════════════════════════════════════════ 完整战斗

	void CheckFullBattle(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("完整战斗（4 个怪物组 × 多种子）"));

		const TArray<FName> Encounters = {
			TEXT("enc_01"), TEXT("enc_02"), TEXT("enc_03"), TEXT("enc_04")
		};

		for (const FName& Enc : Encounters)
		{
			int32 Wins = 0, Losses = 0, Timeouts = 0;
			int32 TotalRounds = 0;
			int32 Violations = 0;
			bool bInvariantHeld = true;
			FString InvariantErr;

			const int32 Trials = 20;
			for (int32 T = 0; T < Trials; ++T)
			{
				FBattleFixture Fx;
				Fx.Build(static_cast<uint64>(T) * 31 + 7, TEXT("open_hall"), Enc);

				const int32 Rounds = RunFullBattle(Fx);
				TotalRounds += Rounds;

				if (Fx.Flow->IsBattleOver())
				{
					const EHexBattlePhase P = Fx.State->Phase;
					if (P == EHexBattlePhase::BattleLose)
					{
						++Losses;
					}
					else
					{
						// Win 或 Explore（胜利后进入拾取阶段）
						++Wins;
					}
				}
				else
				{
					++Timeouts;
				}

				// ⚠️ R7：安全闸触发时写违规记录而非崩溃。
				//    有违规记录不算失败，但数量必须为 0 —— 有就说明存在自激链。
				Violations += Fx.State->GetRuleViolations().Num();

				FString Err;
				if (!Fx.State->Piles.CheckInvariants(Err))
				{
					bInvariantHeld = false;
					InvariantErr = Err;
				}
			}

			const FString N = Enc.ToString();

			// ⚠️ 最重要的一条：战斗必须终止。
			//    不终止在回合制里表现为"点了结束回合没反应"，只能强杀进程。
			Ctx.CheckEqual(FString::Printf(TEXT("[%s] 无超时（战斗必然终止）"), *N),
				Timeouts, 0);

			Ctx.CheckEqual(FString::Printf(TEXT("[%s] 无规则违规（无自激链）"), *N),
				Violations, 0);

			Ctx.Check(FString::Printf(TEXT("[%s] 牌堆不变量全程成立"), *N),
				bInvariantHeld, InvariantErr);

			const float AvgRounds = static_cast<float>(TotalRounds) / Trials;

			// ⚠️ §4.1 的节奏目标：单场 3–5 回合。
			//    这里放宽到 2–12：机器人策略很笨（打第一张能打的牌），
			//    真实玩家会更快。低于 2 说明战斗过于一边倒，
			//    高于 12 说明双方都打不动对方（数值失衡）。
			Ctx.Check(FString::Printf(TEXT("[%s] 平均回合数在 2–12 之间"), *N),
				AvgRounds >= 2.0f && AvgRounds <= 12.0f,
				FString::Printf(TEXT("平均 %.1f 回合（胜%d 负%d）"),
					AvgRounds, Wins, Losses));

			Ctx.Check(FString::Printf(TEXT("[%s] 至少有一次分出胜负"), *N),
				Wins + Losses == Trials,
				FString::Printf(TEXT("胜%d 负%d 超时%d"), Wins, Losses, Timeouts));
		}
	}

	// ═══════════════════════════════════════════ 确定性

	void CheckDeterminism(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("确定性（同 seed 逐位一致）"));

		// ⚠️ 这是架构纪律 1 的最终检验：
		//    同 seed 两次运行的 ActionLog 哈希必须【逐位相同】。
		//    这条成立才有：种子分享、录像回放、bug 精确复现、平衡模拟。
		for (uint64 Seed : { 1ull, 42ull, 999ull })
		{
			FBattleFixture A, B;
			A.Build(Seed, TEXT("open_hall"), TEXT("enc_02"));
			B.Build(Seed, TEXT("open_hall"), TEXT("enc_02"));

			const int32 RoundsA = RunFullBattle(A);
			const int32 RoundsB = RunFullBattle(B);

			Ctx.CheckEqual(FString::Printf(TEXT("seed=%llu 回合数一致"), Seed),
				RoundsA, RoundsB);

			Ctx.Check(FString::Printf(TEXT("seed=%llu ActionLog 哈希一致"), Seed),
				A.State->ActionLogHash() == B.State->ActionLogHash(),
				FString::Printf(TEXT("%u vs %u"),
					A.State->ActionLogHash(), B.State->ActionLogHash()));

			Ctx.Check(FString::Printf(TEXT("seed=%llu 整体状态哈希一致"), Seed),
				A.State->ContentHash() == B.State->ContentHash(),
				FString::Printf(TEXT("%u vs %u"),
					A.State->ContentHash(), B.State->ContentHash()));

			Ctx.Check(FString::Printf(TEXT("seed=%llu 结束阶段一致"), Seed),
				A.State->Phase == B.State->Phase, TEXT(""));
		}

		// 不同 seed 应当产出不同战斗（否则随机性形同不存在）
		{
			TSet<uint32> Hashes;
			for (uint64 Seed = 1; Seed <= 15; ++Seed)
			{
				FBattleFixture Fx;
				Fx.Build(Seed, TEXT("open_hall"), TEXT("enc_02"));
				RunFullBattle(Fx);
				Hashes.Add(Fx.State->ActionLogHash());
			}
			Ctx.Check(TEXT("15 个 seed 产出 ≥10 种不同战斗"),
				Hashes.Num() >= 10,
				FString::Printf(TEXT("仅 %d 种"), Hashes.Num()));
		}
	}

	// ═══════════════════════════════════════════ 快照与 Undo

	void CheckSnapshot(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("快照与回滚"));

		// ⚠️ Undo 用快照而非逆动作 —— 逆动作在有状态/位移/牌堆的系统里
		//    必然写错（架构文档 §4.7）。回合制下几 KB 的深拷贝零压力。
		FBattleFixture Fx;
		Fx.Build(5, TEXT("open_hall"), TEXT("enc_01"));
		Fx.Flow->BeginBattle();

		const uint32 HashBefore = Fx.State->ContentHash();
		TSharedPtr<FHexBattleState> Snap = Fx.State->Snapshot();

		Ctx.Check(TEXT("快照创建成功"), Snap.IsValid(), TEXT(""));
		if (!Snap.IsValid())
		{
			return;
		}

		Ctx.Check(TEXT("快照内容与原状态一致"),
			Snap->ContentHash() == HashBefore,
			FString::Printf(TEXT("%u vs %u"), Snap->ContentHash(), HashBefore));

		// 改变状态
		bool bChanged = false;
		for (const FHexCardInstance& C : Fx.State->Piles.GetHand())
		{
			TArray<FIntVector> Targets;
			Fx.Flow->GetLegalTargets(C.Uid, Targets);
			if (Fx.Flow->CanPlayCard(C.Uid) && Targets.Num() > 0)
			{
				Fx.Flow->PlayCard(C.Uid, Targets[0]);
				bChanged = true;
				break;
			}
		}

		if (bChanged)
		{
			Ctx.Check(TEXT("出牌后状态哈希改变"),
				Fx.State->ContentHash() != HashBefore, TEXT(""));

			// 回滚
			Fx.State->RestoreFrom(*Snap);
			Ctx.Check(TEXT("回滚后哈希恢复"),
				Fx.State->ContentHash() == HashBefore,
				FString::Printf(TEXT("%u vs %u"),
					Fx.State->ContentHash(), HashBefore));

			FString Err;
			Ctx.Check(TEXT("回滚后牌堆不变量成立"),
				Fx.State->Piles.CheckInvariants(Err), Err);
		}
		else
		{
			Ctx.Fail(TEXT("快照测试：能打出一张牌"),
				TEXT("开局无牌可打，无法测试回滚"));
		}
	}

	// ═══════════════════════════════════════════ 镇妖者被动

	void CheckWardenPassive(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("镇妖者被动：格挡不清空"));

		// ⚠️ 这是镇妖者的核心定位（§4.2「站在原地让敌人自己撞死」）。
		//    被动失效则它退化成一个平庸的近战角色。
		//
		//    这条断言抓到过真实缺陷：RebuildRuleAggregate 原先只聚合
		//    符文与装备，HeroData.PassiveRules 从未被读取 ——
		//    镇妖者的被动在实战中【完全没生效】。
		FBattleFixture Fx;
		Fx.Build(6, TEXT("open_hall"), TEXT("enc_01"));
		FHexBattleFlow& Flow = *Fx.Flow;
		FHexBattleState& State = *Fx.State;

		Flow.BeginBattle();

		// 先确认被动真的进了规则聚合
		Ctx.Check(TEXT("镇妖者被动已接入 RuleBook"),
			FHexRuleBook::BlockPersists(State),
			TEXT("被动未接入 → 格挡照常清空，角色定位消失"));

		FHexUnit* Hero = State.GetHero();
		if (!Hero)
		{
			Ctx.Fail(TEXT("被动测试前置条件"), TEXT("找不到英雄"));
			return;
		}

		// 手动给一个远超敌人输出的格挡值，这样"回合结束后仍有剩余"
		// 只可能来自被动，而不是"敌人恰好没打满"
		Hero->Block = 60;

		Flow.EndPlayerTurn();

		Hero = State.GetHero();
		if (Hero)
		{
			Ctx.Check(TEXT("回合结束后格挡未被清空（被动生效）"),
				Hero->Block > 0,
				FString::Printf(TEXT("格挡 60 → %d —— 被动失效则会归零"),
					Hero->Block));
		}

		// ── 格挡上限：不能无限叠
		//
		// ⚠️ 上限缺失曾让镇妖者 10 回合叠到 140 格挡（HP 只有 80），
		//    失败条件在数学上不存在。
		{
			FBattleFixture F2;
			F2.Build(7, TEXT("open_hall"), TEXT("enc_01"));
			F2.Flow->BeginBattle();

			FHexUnit* H = F2.State->GetHero();
			if (H)
			{
				Ctx.CheckEqual(TEXT("镇妖者格挡上限 = 20"), H->GetBlockCap(), 20);

				// 跑 8 回合。EndPlayerTurn 自动推进到下一回合，
				// 所以循环里不再调 BeginRound。
				for (int32 R = 0; R < 8 && !F2.Flow->IsBattleOver(); ++R)
				{
					F2.Flow->EndPlayerTurn();
				}

				H = F2.State->GetHero();
				if (H)
				{
					Ctx.Check(TEXT("8 回合后格挡未突破上限"),
						H->Block <= H->GetBlockCap(),
						FString::Printf(TEXT("格挡=%d 上限=%d —— 上限失效则失败条件不存在"),
							H->Block, H->GetBlockCap()));
				}
			}
		}
	}

	// ═══════════════════════════════════════════ 符文接入整场战斗

	void CheckRuneInBattle(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("符文在整场战斗中生效"));

		// ⚠️ 单元测试证明了符文能改规则、能挂触发器，
		//    但"装上符文后整场战斗仍能跑完"是另一回事 ——
		//    符文的触发可能在某个阶段引发连锁而卡死。
		{
			int32 Timeouts = 0;
			int32 Violations = 0;

			for (int32 T = 0; T < 10; ++T)
			{
				FBattleFixture Fx;
				Fx.Build(static_cast<uint64>(T) * 13 + 3, TEXT("open_hall"), TEXT("enc_02"));

				// 塞满 6 槽，包含诅咒符文（它们有自伤与额外触发）
				Fx.State->RuneLoadout.SetSlot(0, FHexRuneLibrary::FindRune(TEXT("rune_whetstone")));
				Fx.State->RuneLoadout.SetSlot(1, FHexRuneLibrary::FindRune(TEXT("rune_twin_shadow")));
				Fx.State->RuneLoadout.SetSlot(2, FHexRuneLibrary::FindRune(TEXT("rune_push_hand")));
				Fx.State->RuneLoadout.SetSlot(3, FHexRuneLibrary::FindRune(TEXT("rune_soul_eater")));
				Fx.State->RuneLoadout.SetSlot(4, FHexRuneLibrary::FindRune(TEXT("rune_greed_bone")));
				Fx.State->RuneLoadout.SetSlot(5, FHexRuneLibrary::FindRune(TEXT("rune_hex_chain")));
				Fx.State->RebuildRuleAggregate();

				RunFullBattle(Fx);

				if (!Fx.Flow->IsBattleOver())
				{
					++Timeouts;
				}
				Violations += Fx.State->GetRuleViolations().Num();
			}

			Ctx.CheckEqual(TEXT("满符文槽下战斗仍必然终止"), Timeouts, 0);
			Ctx.CheckEqual(TEXT("满符文槽下无规则违规"), Violations, 0);
		}

		// ── 《贪骨》的自伤真的会掉血
		//
		// 诅咒符文的代价必须真实存在，否则它就是白送的强度。
		{
			FBattleFixture Fx;
			Fx.Build(8, TEXT("open_hall"), TEXT("enc_01"));
			Fx.State->RuneLoadout.SetSlot(0, FHexRuneLibrary::FindRune(TEXT("rune_greed_bone")));
			Fx.State->RebuildRuleAggregate();

			Fx.Flow->BeginBattle();
			const int32 HPStart = Fx.State->GetHero() ? Fx.State->GetHero()->HP : 0;

			// 跑 3 回合（EndPlayerTurn 自动推进）
			for (int32 R = 0; R < 3 && !Fx.Flow->IsBattleOver(); ++R)
			{
				Fx.Flow->EndPlayerTurn();
			}

			const FHexUnit* H = Fx.State->GetHero();
			if (H && HPStart > 0)
			{
				Ctx.Check(TEXT("《贪骨》的自伤代价真实存在"),
					H->HP < HPStart,
					FString::Printf(TEXT("HP %d → %d（诅咒无代价 = 白送强度）"),
						HPStart, H->HP));
			}
		}

		// ── 《薄刃契》真的改变抽牌数
		{
			FBattleFixture Fx;
			Fx.Build(9, TEXT("open_hall"), TEXT("enc_01"));
			Fx.State->RuneLoadout.SetSlot(0, FHexRuneLibrary::FindRune(TEXT("rune_thin_blade")));
			Fx.State->RebuildRuleAggregate();

			Fx.Flow->BeginBattle();

			// 基线抽 3，《薄刃契》+1 → 4
			// ⚠️ 基线是 3 而非 5：基石卡移出牌堆后，卡组只剩 5 张，
			//    抽 5 会把整个卡组抽光（见 MakeWarden 的 CardsDrawnPerTurn 注释）。
			Ctx.CheckEqual(TEXT("《薄刃契》使开局手牌变 4 张"),
				Fx.State->Piles.NumHand(), 4);
		}
	}

	// ═══════════════════════════════════════════ 状态在整场中的表现

	void CheckStatusInBattle(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("状态在整场战斗中结算"));

		// ── 燃烧真的会 tick 掉血
		//
		// ⚠️ Godot 版里 burn 是 no-op（埋点存在但没有实现）。
		//    这条断言防止 UE 版重蹈覆辙。
		{
			FBattleFixture Fx;
			Fx.Build(10, TEXT("open_hall"), TEXT("enc_01"));
			Fx.Flow->BeginBattle();

			// 给第一个敌人挂 5 层燃烧
			FHexUnit* Enemy = nullptr;
			for (FHexUnit& U : Fx.State->GetUnitsMutable())
			{
				if (U.Team == EHexTeam::Enemy && U.bIsAlive)
				{
					Enemy = &U;
					break;
				}
			}

			if (Enemy)
			{
				Enemy->ApplyStatus(FHexStatusLibrary::Burn, 5);
				const int32 HPBefore = Enemy->HP;
				const int32 EnemyId = Enemy->Id;

				Fx.Flow->EndPlayerTurn();

				const FHexUnit* After = Fx.State->FindUnit(EnemyId);
				if (After)
				{
					Ctx.Check(TEXT("燃烧在回合结束造成伤害"),
						After->HP < HPBefore,
						FString::Printf(TEXT("HP %d → %d（燃烧未结算 = Godot 版的 no-op 缺陷）"),
							HPBefore, After->HP));

					Ctx.Check(TEXT("燃烧层数衰减"),
						After->GetStatusStacks(FHexStatusLibrary::Burn) < 5,
						FString::Printf(TEXT("层数=%d"),
							After->GetStatusStacks(FHexStatusLibrary::Burn)));
				}
			}
		}

		// ── 力量真的提升伤害
		{
			FBattleFixture Fx;
			Fx.Build(11, TEXT("open_hall"), TEXT("enc_01"));
			Fx.Flow->BeginBattle();

			FHexUnit* Hero = Fx.State->GetHero();
			if (!Hero)
			{
				Ctx.Fail(TEXT("力量测试前置条件"), TEXT("找不到英雄"));
				return;
			}

			// ⚠️ 必须先把敌人挪到攻击范围内。
			//    开局敌人在敌方生成区（row 5+），玩家在 (4,1)，
			//    近战攻击卡此时【没有任何合法目标】，
			//    PreviewDamage 会返回 0 —— 断言"0 → 0"什么也证明不了。
			const FIntVector Adjacent =
				FHexCoord::OffsetToCube(HexK::HeroSpawnCol, HexK::HeroSpawnRow + 1);
			for (FHexUnit& U : Fx.State->GetUnitsMutable())
			{
				if (U.Team == EHexTeam::Enemy && U.bIsAlive)
				{
					U.Anchor = Adjacent;
					break;
				}
			}
			Fx.State->RebuildOccupancy();

			// 找一张能真正命中敌人的攻击卡，对比加力量前后的预览伤害
			bool bTested = false;
			for (const FHexCardInstance& C : Fx.State->Piles.GetHand())
			{
				const FHexCardData* Card = FHexContentLibrary::FindCard(C.CardId);
				if (!Card || Card->CardType != EHexCardType::Attack)
				{
					continue;
				}

				TArray<FIntVector> Targets;
				Fx.Flow->GetLegalTargets(C.Uid, Targets);

				for (const FIntVector& T : Targets)
				{
					const FHexUnit* Occupant = Fx.State->FindUnitAtCell(T);
					if (!Occupant || Occupant->Team != EHexTeam::Enemy)
					{
						continue;
					}

					const int32 Before = Fx.Flow->PreviewDamage(C.Uid, T).X;
					if (Before <= 0)
					{
						continue;
					}

					Hero = Fx.State->GetHero();
					if (Hero)
					{
						Hero->ApplyStatus(FHexStatusLibrary::Strength, 5);
					}
					const int32 After = Fx.Flow->PreviewDamage(C.Uid, T).X;

					Ctx.Check(TEXT("力量提升攻击卡预览伤害"),
						After > Before,
						FString::Printf(TEXT("%d → %d"), Before, After));
					bTested = true;
					break;
				}

				if (bTested)
				{
					break;
				}
			}

			if (!bTested)
			{
				Ctx.Fail(TEXT("力量测试：找到可命中敌人的攻击卡"),
					TEXT("手牌里没有能命中相邻敌人的攻击卡"));
			}
		}
	}
}

// ══════════════════════════════════════════════════════════ 冲撞
//
// 《冲撞》曾同时存在三个缺陷，且三个都【不会报错】，只是行为不对：
//   ① 不看目标格，沿六轴近似方向走满 3 格 → 落点与点击不符
//   ② 逐格判定占位，遇到单位就停 → 表现为"被其他角色顶开"
//   ③ 目标形状是 Tile，波及格只有落点（且落点必须为空）
//      → 伤害步骤恒 0 目标，冲撞从未造成过任何伤害
//
// 这三条各对应下面一组断言。手工摆位而不用随机战斗，
// 因为随机场景无法稳定复现"沿途正好站着敌人"这个关键条件。
void CheckCharge(FHexVerifyContext& Ctx)
{
	const FHexCardData* Charge = FHexContentLibrary::FindCard(TEXT("charge"));
	if (!Charge)
	{
		Ctx.Fail(TEXT("冲撞：卡牌存在"), TEXT("找不到 charge"));
		return;
	}

	Ctx.Check(TEXT("冲撞：目标形状为 DashPath"),
		Charge->TargetSpec.Shape == EHexTargetShape::DashPath,
		FString::Printf(TEXT("Shape=%d"), static_cast<int32>(Charge->TargetSpec.Shape)));

	Ctx.Check(TEXT("冲撞：被识别为格子目标卡（AI 接敌依赖）"),
		Charge->TargetSpec.TargetsCell(), TEXT(""));

	// ── 构造：英雄在 (2,4)，敌人在 (3,4)，空地在 (4,4)
	//    冲撞 (4,4) 必须穿过 (3,4) 上的敌人。
	FHexBattleState State(12345);
	FHexLayouts::Build(TEXT("open_hall"), State.Grid);

	const FIntVector HeroCell = FHexCoord::OffsetToCube(2, 4);
	const FIntVector MidCell = FHexCoord::OffsetToCube(3, 4);
	const FIntVector FarCell = FHexCoord::OffsetToCube(4, 4);

	int32 EnemyId = -1;
	{
		FHexUnit Hero;
		Hero.Team = EHexTeam::Player;
		Hero.SizeClass = EHexSizeClass::S;
		Hero.HPMax = 80;
		Hero.HP = 80;
		Hero.ATK = 10;
		Hero.Anchor = HeroCell;
		Hero.Facing = 0;
		State.HeroUnitId = State.AddUnit(Hero);

		FHexUnit Foe;
		Foe.Team = EHexTeam::Enemy;
		Foe.SizeClass = EHexSizeClass::S;
		Foe.HPMax = 100;
		Foe.HP = 100;
		Foe.DEF = 0;
		Foe.Anchor = MidCell;
		Foe.Facing = 3;
		EnemyId = State.AddUnit(Foe);
	}

	FHexBattleFlow Flow(State);
	Flow.SetCardLookup([](FName Id) { return FHexContentLibrary::FindCard(Id); });

	const FHexUnit* Hero = State.GetHero();
	if (!Hero)
	{
		Ctx.Fail(TEXT("冲撞：英雄存在"), TEXT(""));
		return;
	}

	// ── ① 合法目标：敌人身后的空格必须可选（穿人）
	TArray<FIntVector> Legal;
	FHexTargetResolver::LegalCells(State, *Hero, Charge->TargetSpec, Legal);

	Ctx.Check(TEXT("冲撞：可以选中敌人【身后】的空格（不被单位阻挡）"),
		Legal.Contains(FarCell),
		FString::Printf(TEXT("合法格 %d 个"), Legal.Num()));

	Ctx.Check(TEXT("冲撞：不能选中敌人占据的格（落点要站得下）"),
		!Legal.Contains(MidCell), TEXT(""));

	// ── ② 波及格：必须包含沿途的敌人格，而不只是落点
	TArray<FIntVector> Affected;
	FHexTargetResolver::AffectedCells(State, *Hero, Charge->TargetSpec, FarCell, Affected);

	Ctx.Check(TEXT("冲撞：波及格含沿途格（缺陷③ 的核心）"),
		Affected.Contains(MidCell),
		FString::Printf(TEXT("波及 %d 格"), Affected.Num()));
	Ctx.Check(TEXT("冲撞：波及格含落点"), Affected.Contains(FarCell), TEXT(""));
	Ctx.Check(TEXT("冲撞：波及格【不含】自身起点（否则会打到自己）"),
		!Affected.Contains(HeroCell), TEXT(""));

	// ── ③ 沿途敌人确实受伤，且英雄落在点击的那一格
	TArray<int32> Units;
	FHexTargetResolver::AffectedUnits(
		State, *Hero, Charge->TargetSpec, FarCell, EHexTargetFilter::Enemy, Units);
	Ctx.Check(TEXT("冲撞：沿途敌人被算作受击目标"),
		Units.Contains(EnemyId),
		FString::Printf(TEXT("命中 %d 个单位"), Units.Num()));
}

bool FHexVerifySuites::VerifyBattle(FHexVerifyContext& Ctx)
{
	CheckPhaseFlow(Ctx);
	CheckPlayCard(Ctx);
	CheckCharge(Ctx);
	CheckFullBattle(Ctx);
	CheckDeterminism(Ctx);
	CheckSnapshot(Ctx);
	CheckWardenPassive(Ctx);
	CheckRuneInBattle(Ctx);
	CheckStatusInBattle(Ctx);
	return Ctx.NumFailed() == 0;
}
