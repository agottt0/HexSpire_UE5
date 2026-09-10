// Copyright Hex Spire. All Rights Reserved.

#include "Commandlets/HexPlaytestCommandlet.h"
#include "HexSpire.h"

#include "Run/HexRunState.h"
#include "Map/HexFloorMap.h"
#include "Battle/HexBattleState.h"
#include "Battle/HexBattleFlow.h"
#include "Battle/HexUnit.h"
#include "Battle/HexRuleBook.h"
#include "Content/HexContentLibrary.h"
#include "Content/HexLayouts.h"
#include "Hex/HexCoord.h"
#include "Hex/HexFootprint.h"
#include "Rng/HexRngStreams.h"
#include "Core/HexSpireConstants.h"

UHexPlaytestCommandlet::UHexPlaytestCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

namespace
{
	/** 一局的结果 */
	struct FRunResult
	{
		bool bReachedBoss = false;
		bool bBeatBoss = false;
		bool bDied = false;
		bool bStuck = false;         // 走不下去（最严重的问题）
		int32 RoomsCleared = 0;
		int32 BattlesFought = 0;
		int32 FinalCorruption = 0;
		int32 FinalHP = 0;
		int32 TotalRounds = 0;
		FString StuckReason;
	};

	/** 到最近存活敌人的距离 */
	int32 DistToNearestEnemy(const FHexBattleState& S, const FIntVector& Cell)
	{
		int32 Best = -1;
		for (const FHexUnit& U : S.GetUnits())
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
	 * 本回合【预计会吃到】的总伤害。
	 *
	 * ⚠️ 这是"意图是承诺"的直接红利：敌人意图里冻结了 PredictedDamage，
	 *    所以玩家（和这个机器人）能在行动前精确算出威胁。
	 *    可躲型意图只在它真的会命中玩家占格时才计入 ——
	 *    这正是走位规避的价值来源。
	 */
	int32 IncomingDamageThisRound(const FHexBattleState& S)
	{
		const FHexUnit* Hero = S.GetHero();
		if (!Hero)
		{
			return 0;
		}

		TArray<FIntVector> HeroCells;
		Hero->GetCells(HeroCells);

		int32 Total = 0;
		for (const FHexUnit& U : S.GetUnits())
		{
			if (U.Team != EHexTeam::Enemy || !U.bIsAlive || !U.Intent.IsValid())
			{
				continue;
			}
			// 眩晕的敌人不会行动
			if (U.ShouldSkipTurn())
			{
				continue;
			}
			if (U.Intent.Kind != EHexIntentKind::Attack
				&& U.Intent.Kind != EHexIntentKind::MultiAttack)
			{
				continue;
			}

			const int32 Hit = U.Intent.PredictedDamage
				* FMath::Max(1, U.Intent.HitCount);

			if (U.Intent.Targeting == EHexIntentTargeting::TrackTarget)
			{
				// 追踪型：躲不掉，必然吃到
				Total += Hit;
			}
			else
			{
				// 可躲型：只有目标格与自己重叠时才会吃到
				for (const FIntVector& C : U.Intent.TargetCells)
				{
					if (HeroCells.Contains(C))
					{
						Total += Hit;
						break;
					}
				}
			}
		}

		return Total;
	}

	/** 一张卡是否是防御卡（会产生格挡） */
	bool IsGuardCard(const FHexCardData& Card)
	{
		for (const FHexEffectStep& S : Card.Effects)
		{
			if (S.Op == EHexEffectOp::GainBlock)
			{
				return true;
			}
		}
		return false;
	}

	/** 某组格子里，站上去会吃到多少【可躲型】伤害 */
	int32 DodgeableDamageAtCells(const FHexBattleState& S, const TArray<FIntVector>& Cells)
	{
		int32 Total = 0;
		for (const FHexUnit& U : S.GetUnits())
		{
			if (U.Team != EHexTeam::Enemy || !U.bIsAlive || !U.Intent.IsValid())
			{
				continue;
			}
			if (U.ShouldSkipTurn())
			{
				continue;
			}
			if (U.Intent.Targeting != EHexIntentTargeting::FixedTile)
			{
				continue;   // 追踪型躲不掉，不参与走位决策
			}
			if (U.Intent.Kind != EHexIntentKind::Attack
				&& U.Intent.Kind != EHexIntentKind::MultiAttack)
			{
				continue;
			}

			for (const FIntVector& C : U.Intent.TargetCells)
			{
				if (Cells.Contains(C))
				{
					Total += U.Intent.PredictedDamage
						* FMath::Max(1, U.Intent.HitCount);
					break;
				}
			}
		}
		return Total;
	}

	/**
	 * 简单机器人出一张牌。
	 *
	 * 优先级：
	 *   ① 站在可躲型攻击的落点上 → 走开（走位规避）
	 *   ② 剩余威胁 > 当前格挡 → 买格挡
	 *   ③ 能直接命中敌人的攻击卡
	 *   ④ 靠近敌人（对付风筝型）
	 *   ⑤ 其它任意卡
	 *
	 * ⚠️ ① 和 ② 的顺序不能反：先躲再挡。
	 *    躲开的伤害是【免费】的（只花移动卡的费用），
	 *    而格挡要花体力去买。反过来会浪费格挡在本可躲开的攻击上。
	 *
	 * ⚠️ 这两条都是自检过程中被数据逼出来的：
	 *    第一版机器人既不挡也不躲，40 局阵亡率 100%。
	 *    而镇妖者的定位是格挡坦克（§4.2），游戏的核心机制之一是
	 *    「可躲 vs 追踪」（§13.2）—— 两者都不用，等于放弃了
	 *    这个角色和这个战斗系统的一半设计。
	 *    机器人不代表玩家上限，但它至少要会用核心机制，
	 *    否则测出来的难度数据完全失真。
	 */
	bool BotPlayOne(FHexBattleFlow& Flow, FHexBattleState& S)
	{
		// ⚠️ 可选牌 = 手牌 + 固定卡（基石卡已移出牌堆循环）。
		//    漏掉固定卡的话，机器人不会移动也不会防御，
		//    跑出来的难度数据会严重偏难，且看不出原因。
		TArray<FHexCardInstance> Hand = S.Piles.GetHand();
		Hand.Append(S.FixedCards);

		const FHexUnit* Hero = S.GetHero();

		// ── ① 走位躲避
		if (Hero)
		{
			TArray<FIntVector> HeroCells;
			Hero->GetCells(HeroCells);
			const int32 DangerHere = DodgeableDamageAtCells(S, HeroCells);

			if (DangerHere > 0)
			{
				int32 BestUid = 0;
				FIntVector BestCell = FIntVector::ZeroValue;
				int32 BestDanger = DangerHere;

				for (const FHexCardInstance& C : Hand)
				{
					const FHexCardData* Card = FHexContentLibrary::FindCard(C.CardId);
					if (!Card || !Card->TargetSpec.TargetsCell()
						|| !Flow.CanPlayCard(C.Uid))
					{
						continue;
					}

					TArray<FIntVector> Targets;
					Flow.GetLegalTargets(C.Uid, Targets);
					for (const FIntVector& T : Targets)
					{
						// 落点的 footprint（S 体型就是那一格）
						TArray<FIntVector> DestCells;
						FHexFootprint::Cells(T, Hero->SizeClass, Hero->Facing, DestCells);

						const int32 Danger = DodgeableDamageAtCells(S, DestCells);
						if (Danger < BestDanger)
						{
							BestDanger = Danger;
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

		// ── ② 挡不住就先买格挡
		//
		// ══════════════════════════════════════════════════════════
		// ⚠️ 这里的两个闸门是【常驻固定卡引入后必须加的】
		// ══════════════════════════════════════════════════════════
		// 《防御》从手牌卡变成常驻固定卡后，它【永远可打】。
		// 而原判据只有 "Shield < Incoming"：
		// 当来袭伤害超过格挡上限（镇妖者 20 点）时这个条件恒成立，
		// 机器人于是把 5 点体力全买成格挡、一次攻击都不出，
		// 被慢慢耗死 —— 实测抵达 Boss 率从 ≥80% 掉到 71%。
		//
		// 这正是本文件开头注释记录过的老陷阱（"早期版本把体力
		// 全花在《防御》《铁壁》上"）的复发：当时靠"手牌里防御卡
		// 数量有限"兜住，常驻化之后那个天然限制没有了。
		if (Hero)
		{
			const int32 Incoming = IncomingDamageThisRound(S);
			const int32 Shield = Hero->Block;

			// 闸门一：格挡已经顶到上限，再买是纯浪费
			const bool bCapReached = Shield >= Hero->GetBlockCap();

			// 闸门二：至少留一半体力用于输出。
			//        不留的话，"打不死敌人 → 敌人一直打我 → 继续买格挡"
			//        会形成自锁，战斗拖到回合上限也分不出胜负。
			const int32 EnergyFloor = FHexRuleBook::EnergyMax(S) / 2;
			const bool bEnergyReserved = S.Energy <= EnergyFloor;

			if (Incoming > 0 && Shield < Incoming && !bCapReached && !bEnergyReserved)
			{
				for (const FHexCardInstance& C : Hand)
				{
					const FHexCardData* Card = FHexContentLibrary::FindCard(C.CardId);
					if (!Card || !IsGuardCard(*Card) || !Flow.CanPlayCard(C.Uid))
					{
						continue;
					}
					TArray<FIntVector> Targets;
					Flow.GetLegalTargets(C.Uid, Targets);
					if (Targets.Num() > 0
						&& Flow.PlayCard(C.Uid, Targets[0]) == EHexPlayResult::Success)
					{
						return true;
					}
				}
			}
		}

		// ── ③ 命中敌人的攻击卡
		for (const FHexCardInstance& C : Hand)
		{
			const FHexCardData* Card = FHexContentLibrary::FindCard(C.CardId);
			if (!Card || Card->CardType != EHexCardType::Attack
				|| !Flow.CanPlayCard(C.Uid))
			{
				continue;
			}
			TArray<FIntVector> Targets;
			Flow.GetLegalTargets(C.Uid, Targets);
			for (const FIntVector& T : Targets)
			{
				const FHexUnit* O = S.FindUnitAtCell(T);
				if (O && O->Team == EHexTeam::Enemy && O->bIsAlive)
				{
					if (Flow.PlayCard(C.Uid, T) == EHexPlayResult::Success)
					{
						return true;
					}
				}
			}
		}

		// ── ④ 靠近敌人（对付风筝型）
		if (Hero)
		{
			const int32 Cur = DistToNearestEnemy(S, Hero->Anchor);
			if (Cur > 1)
			{
				int32 BestUid = 0;
				FIntVector BestCell = FIntVector::ZeroValue;
				int32 BestDist = Cur;

				for (const FHexCardInstance& C : Hand)
				{
					const FHexCardData* Card = FHexContentLibrary::FindCard(C.CardId);
					if (!Card || !Card->TargetSpec.TargetsCell()
						|| !Flow.CanPlayCard(C.Uid))
					{
						continue;
					}
					TArray<FIntVector> Targets;
					Flow.GetLegalTargets(C.Uid, Targets);
					for (const FIntVector& T : Targets)
					{
						const int32 D = DistToNearestEnemy(S, T);
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

		// ── ⑤ 任何能打的牌
		for (const FHexCardInstance& C : Hand)
		{
			if (!Flow.CanPlayCard(C.Uid))
			{
				continue;
			}
			TArray<FIntVector> Targets;
			Flow.GetLegalTargets(C.Uid, Targets);
			if (Targets.Num() > 0
				&& Flow.PlayCard(C.Uid, Targets[0]) == EHexPlayResult::Success)
			{
				return true;
			}
		}

		return false;
	}

	/**
	 * 打一场战斗。返回是否胜利。
	 * 这里复刻 AHexDemoGameMode 的布阵逻辑 ——
	 * 若两边不一致，自检就失去意义了。
	 */
	bool FightRoom(FHexRunState& Run, const FHexRoomNode& Room, int32& OutRounds)
	{
		FHexBattleState S(Run.GetMasterSeed() + static_cast<uint64>(Room.Id) * 7919);
		FHexLayouts::Build(Room.LayoutId, S.Grid);

		const FHexHeroData* Hero = FHexContentLibrary::FindHero(Run.HeroId);
		if (!Hero)
		{
			return false;
		}

		// ── 英雄（生命跨战斗继承）
		{
			FHexUnit U;
			U.SourceId = Hero->Id;
			U.DisplayName = Hero->DisplayName;
			U.Team = EHexTeam::Player;
			U.SizeClass = Hero->SizeClass;
			U.HPMax = Run.HeroHPMax;
			U.HP = Run.HeroHP;
			U.ATK = Hero->BaseATK + Run.EquipLoadout.GetStatBonus(EHexStat::ATK, Hero->BaseATK);
			U.DEF = Hero->BaseDEF + Run.EquipLoadout.GetStatBonus(EHexStat::DEF, Hero->BaseDEF);
			U.AGI = Hero->BaseAGI + Run.EquipLoadout.GetStatBonus(EHexStat::AGI, Hero->BaseAGI);
			U.LUK = Hero->BaseLUK + Run.EquipLoadout.GetStatBonus(EHexStat::LUK, Hero->BaseLUK);
			U.CRIT = Hero->BaseCRIT + Run.EquipLoadout.GetStatBonus(EHexStat::CRIT, Hero->BaseCRIT);
			U.Anchor = FHexCoord::OffsetToCube(HexK::HeroSpawnCol, HexK::HeroSpawnRow);
			U.Facing = HexK::HeroSpawnFacing;
			S.HeroUnitId = S.AddUnit(U);
		}

		S.HeroEnergyMaxBase = Hero->EnergyMax;
		S.HeroDrawBase = Hero->CardsDrawnPerTurn;
		S.DeckCapacityBase = Run.DeckCapacity;
		S.RuneLoadout = Run.RuneLoadout;
		S.EquipLoadout = Run.EquipLoadout;
		S.HeroPassiveRules = Hero->PassiveRules;

		// ── 敌人
		{
			TArray<FHexEncounterEntry> Entries;
			FHexContentLibrary::GetEncounter(Room.EncounterId, Entries);

			int32 Col = 2;
			int32 Row = HexK::EnemySpawnRowMax;

			for (const FHexEncounterEntry& E : Entries)
			{
				const FHexEnemyData* Data = FHexContentLibrary::FindEnemy(E.EnemyId);
				if (!Data)
				{
					continue;
				}
				for (int32 I = 0; I < E.Count; ++I)
				{
					FHexUnit U = FHexContentLibrary::MakeEnemyUnit(
						*Data, Run.FloorIndex, Run.Corruption);
					U.Facing = 5;

					bool bPlaced = false;
					for (int32 A = 0; A < 60 && !bPlaced; ++A)
					{
						const FIntVector Anchor = FHexCoord::OffsetToCube(Col, Row);
						U.Anchor = Anchor;
						const FHexSizeClassDef& Def = FHexFootprint::GetSizeDef(U.SizeClass);
						if (FHexFootprint::CanPlace(S.Grid, Anchor, Def.Footprint,
								U.Facing, -1, Def.bCanCrushRubble))
						{
							S.AddUnit(U);
							bPlaced = true;
						}
						Col += 2;
						if (Col > HexK::BoardCols)
						{
							Col = 2;
							--Row;
							if (Row < HexK::EnemySpawnRowMin)
							{
								Row = HexK::EnemySpawnRowMax;
								Col = 3;
							}
						}
					}
				}
			}
		}

		S.RebuildOccupancy();

		// ── 卡组（含装备注入）
		{
			TArray<FHexCardInstance> Deck = Run.Deck;
			TArray<FName> Injected;
			Run.EquipLoadout.GetInjectedCardIds(Injected);
			int32 Uid = 900000;
			for (const FName& Cid : Injected)
			{
				FHexCardInstance Inst;
				Inst.Uid = Uid++;
				Inst.CardId = Cid;
				Deck.Add(Inst);
			}
			S.Piles.BeginBattle(Deck, S.Rng);
		}

		// 固定卡常驻，不入牌堆
		S.FixedCards = Run.FixedCards;

		FHexBattleFlow Flow(S);
		Flow.SetCardLookup([](FName Id) { return FHexContentLibrary::FindCard(Id); });
		Flow.BeginBattle();

		int32 Turns = 0;
		while (!Flow.IsBattleOver() && Turns < HexK::MaxRoundsPerBattle)
		{
			++Turns;
			int32 Guard = 0;
			while (Guard < 40 && !Flow.IsBattleOver())
			{
				++Guard;
				if (!BotPlayOne(Flow, S))
				{
					break;
				}
			}
			if (Flow.IsBattleOver())
			{
				break;
			}
			Flow.EndPlayerTurn();
		}

		OutRounds = S.RoundNumber;

		const bool bWon = (S.Phase != EHexBattlePhase::BattleLose)
			&& Flow.IsBattleOver();

		// ── 生命回写 + 卡组归还（这是最容易忘的衔接点）
		if (const FHexUnit* H = S.GetHero())
		{
			Run.HeroHP = FMath::Max(0, H->HP);
		}
		{
			TArray<FHexCardInstance> Full;
			S.Piles.EndBattle(Full);

			TArray<FName> Injected;
			Run.EquipLoadout.GetInjectedCardIds(Injected);
			Run.Deck.Reset();
			for (const FHexCardInstance& C : Full)
			{
				if (!Injected.Contains(C.CardId))
				{
					Run.Deck.Add(C);
				}
			}
		}

		return bWon;
	}

	/** BFS 找路径（含目标，不含起点） */
	bool FindPath(const FHexFloorMap& Map, int32 From, int32 To, TArray<int32>& Out)
	{
		Out.Reset();
		const TArray<FHexRoomNode>& Rooms = Map.GetRooms();
		if (!Rooms.IsValidIndex(From) || !Rooms.IsValidIndex(To))
		{
			return false;
		}
		if (From == To)
		{
			return true;
		}

		TArray<int32> Prev;
		Prev.Init(-2, Rooms.Num());
		Prev[From] = -1;

		TArray<int32> Q;
		Q.Add(From);
		int32 Head = 0;
		bool bFound = false;

		while (Head < Q.Num() && !bFound)
		{
			const int32 Cur = Q[Head++];
			for (const int32 N : Rooms[Cur].Neighbors)
			{
				if (!Prev.IsValidIndex(N) || Prev[N] != -2)
				{
					continue;
				}
				Prev[N] = Cur;
				if (N == To)
				{
					bFound = true;
					break;
				}
				Q.Add(N);
			}
		}

		if (!bFound)
		{
			return false;
		}

		int32 Node = To;
		while (Node != From && Node >= 0)
		{
			Out.Insert(Node, 0);
			Node = Prev[Node];
		}
		return true;
	}

	/**
	 * 跑一局完整循环。
	 *
	 * 模拟一个【谨慎玩家】：先探完所有非 Boss 房（拿满腐蚀度与奖励），
	 * 路过营地时休息，最后才打 Boss。这是最长的路径，
	 * 也最容易暴露衔接点问题。
	 */
	FRunResult PlayOneRun(uint64 Seed)
	{
		FRunResult R;

		FHexRngStreams Rng(Seed);
		FHexRunState Run(Seed);
		Run.BeginRun(TEXT("warden"), Rng);

		const int32 BossId = Run.Map.GetBossId();

		// ── 先探完所有非 Boss 房
		for (int32 Step = 0; Step < 30; ++Step)
		{
			// 找最近的未清空、非 Boss 房
			int32 TargetRoom = -1;
			int32 BestLen = INT32_MAX;

			for (const FHexRoomNode& Node : Run.Map.GetRooms())
			{
				if (Node.Visibility == EHexRoomVisibility::Cleared
					|| Node.Id == BossId)
				{
					continue;
				}
				TArray<int32> Path;
				if (FindPath(Run.Map, Run.Map.GetCurrentRoomId(), Node.Id, Path)
					&& Path.Num() > 0 && Path.Num() < BestLen)
				{
					BestLen = Path.Num();
					TargetRoom = Node.Id;
				}
			}

			if (TargetRoom < 0)
			{
				break;   // 非 Boss 房全清完了
			}

			TArray<int32> Path;
			if (!FindPath(Run.Map, Run.Map.GetCurrentRoomId(), TargetRoom, Path))
			{
				R.bStuck = true;
				R.StuckReason = TEXT("找不到通往未清空房间的路");
				break;
			}

			bool bMoved = false;
			for (const int32 Next : Path)
			{
				if (!Run.Map.EnterRoom(Next))
				{
					R.bStuck = true;
					R.StuckReason = FString::Printf(
						TEXT("无法进入房间 %d（路径上的一步失败）"), Next);
					break;
				}
				bMoved = true;

				const FHexRoomNode* Node = Run.Map.FindRoom(Next);
				if (!Node || Node->Visibility == EHexRoomVisibility::Cleared)
				{
					continue;   // 路过已清空的房间
				}

				if (Node->Type == EHexRoomType::Camp)
				{
					Run.RestAtCamp();
					continue;
				}

				if (Node->IsCombat())
				{
					int32 Rounds = 0;
					const bool bWon = FightRoom(Run, *Node, Rounds);
					++R.BattlesFought;
					R.TotalRounds += Rounds;

					if (!bWon || Run.HeroHP <= 0)
					{
						R.bDied = true;
						break;
					}
					Run.OnRoomCleared();
				}
				else
				{
					Run.OnRoomCleared();
				}
			}

			if (R.bDied || R.bStuck || !bMoved)
			{
				break;
			}
		}

		if (!R.bDied && !R.bStuck)
		{
			// ── 打 Boss
			TArray<int32> Path;
			if (!FindPath(Run.Map, Run.Map.GetCurrentRoomId(), BossId, Path))
			{
				R.bStuck = true;
				R.StuckReason = TEXT("探完后找不到通往 Boss 的路");
			}
			else
			{
				for (const int32 Next : Path)
				{
					if (!Run.Map.EnterRoom(Next))
					{
						R.bStuck = true;
						R.StuckReason = FString::Printf(
							TEXT("无法进入 Boss 路径上的房间 %d"), Next);
						break;
					}
				}

				if (!R.bStuck && Run.Map.GetCurrentRoomId() == BossId)
				{
					R.bReachedBoss = true;
					const FHexRoomNode* Boss = Run.Map.FindRoom(BossId);
					if (Boss)
					{
						int32 Rounds = 0;
						R.bBeatBoss = FightRoom(Run, *Boss, Rounds);
						++R.BattlesFought;
						R.TotalRounds += Rounds;
						if (!R.bBeatBoss || Run.HeroHP <= 0)
						{
							R.bDied = true;
						}
						else
						{
							Run.OnRoomCleared();
						}
					}
				}
			}
		}

		// ── 层结算
		if (R.bBeatBoss)
		{
			TArray<FHexRewardOption> Rewards;
			Run.GenerateFloorRewards(Rng, Rewards);
			if (Rewards.Num() > 0)
			{
				Run.ApplyReward(Rewards[0], Rng);
			}
		}

		R.RoomsCleared = Run.Stats.RoomsCleared;
		R.FinalCorruption = Run.Corruption;
		R.FinalHP = Run.HeroHP;
		return R;
	}
}

int32 UHexPlaytestCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamsMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamsMap);

	int32 NumRuns = 30;
	if (const FString* Found = ParamsMap.Find(TEXT("runs")))
	{
		NumRuns = FMath::Clamp(FCString::Atoi(**Found), 1, 2000);
	}

	UE_LOG(LogHexSpire, Display, TEXT(""));
	UE_LOG(LogHexSpire, Display, TEXT("╔══════════════════════════════════════════════╗"));
	UE_LOG(LogHexSpire, Display, TEXT("║   Hex Spire 一层完整循环自检（%4d 局）      ║"), NumRuns);
	UE_LOG(LogHexSpire, Display, TEXT("╚══════════════════════════════════════════════╝"));

	int32 Stuck = 0, Died = 0, ReachedBoss = 0, BeatBoss = 0;
	int32 SumRooms = 0, SumBattles = 0, SumCorruption = 0, SumRounds = 0;
	TArray<FString> StuckReasons;

	const double Start = FPlatformTime::Seconds();

	for (int32 I = 0; I < NumRuns; ++I)
	{
		const uint64 Seed = static_cast<uint64>(I) * 104729 + 12345;
		const FRunResult R = PlayOneRun(Seed);

		if (R.bStuck)
		{
			++Stuck;
			StuckReasons.AddUnique(FString::Printf(
				TEXT("seed=%llu: %s"), Seed, *R.StuckReason));
		}
		if (R.bDied)      { ++Died; }
		if (R.bReachedBoss) { ++ReachedBoss; }
		if (R.bBeatBoss)  { ++BeatBoss; }

		SumRooms += R.RoomsCleared;
		SumBattles += R.BattlesFought;
		SumCorruption += R.FinalCorruption;
		SumRounds += R.TotalRounds;
	}

	const double Elapsed = FPlatformTime::Seconds() - Start;
	const float N = static_cast<float>(NumRuns);

	UE_LOG(LogHexSpire, Display, TEXT(""));
	UE_LOG(LogHexSpire, Display, TEXT("── 结果 ──"));
	UE_LOG(LogHexSpire, Display, TEXT("  抵达 Boss   : %d / %d  (%.0f%%)"),
		ReachedBoss, NumRuns, 100.0f * ReachedBoss / N);
	UE_LOG(LogHexSpire, Display, TEXT("  击败 Boss   : %d / %d  (%.0f%%)"),
		BeatBoss, NumRuns, 100.0f * BeatBoss / N);
	UE_LOG(LogHexSpire, Display, TEXT("  途中阵亡     : %d / %d  (%.0f%%)"),
		Died, NumRuns, 100.0f * Died / N);
	UE_LOG(LogHexSpire, Display, TEXT("  ★ 卡死       : %d / %d"), Stuck, NumRuns);
	UE_LOG(LogHexSpire, Display, TEXT(""));
	UE_LOG(LogHexSpire, Display, TEXT("  平均清空房间 : %.1f"), SumRooms / N);
	UE_LOG(LogHexSpire, Display, TEXT("  平均战斗场数 : %.1f"), SumBattles / N);
	UE_LOG(LogHexSpire, Display, TEXT("  平均总回合数 : %.1f"), SumRounds / N);
	UE_LOG(LogHexSpire, Display, TEXT("  平均终局腐蚀 : %.1f"), SumCorruption / N);
	UE_LOG(LogHexSpire, Display, TEXT("  耗时         : %.2f 秒"), Elapsed);

	for (const FString& S : StuckReasons)
	{
		UE_LOG(LogHexSpire, Error, TEXT("  卡死原因: %s"), *S);
	}

	// ══ 判定 ══
	//
	// ⚠️ 只有两条是【硬性失败】：
	//    ① 卡死 —— 玩家走不下去，这是绝对不可接受的
	//    ② 抵达 Boss 率过低 —— 说明路径逻辑有问题（不是难度问题）
	//
	//    胜率本身【不】作为失败条件：Boss 打不过是难度问题，
	//    要靠平衡调整解决，而"能不能玩通"是结构问题。
	//    把两者混在一起会让自检失去意义。
	bool bOk = true;

	if (Stuck > 0)
	{
		UE_LOG(LogHexSpire, Error,
			TEXT("✗ 有 %d 局卡死 —— 玩家走不下去"), Stuck);
		bOk = false;
	}

	const float ReachRate = 100.0f * ReachedBoss / N;
	if (ReachRate < 80.0f)
	{
		UE_LOG(LogHexSpire, Error,
			TEXT("✗ 抵达 Boss 率仅 %.0f%%（应 ≥80%%）—— 路径或死亡率异常"),
			ReachRate);
		bOk = false;
	}

	if (bOk)
	{
		UE_LOG(LogHexSpire, Display, TEXT(""));
		UE_LOG(LogHexSpire, Display, TEXT("★ 一层完整循环可玩通"));
		UE_LOG(LogHexSpire, Display, TEXT("HEXSPIRE_PLAYTEST_RESULT: PASS"));
		return 0;
	}

	UE_LOG(LogHexSpire, Error, TEXT("HEXSPIRE_PLAYTEST_RESULT: FAIL"));
	return 1;
}
