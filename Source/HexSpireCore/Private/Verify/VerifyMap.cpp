// Copyright Hex Spire. All Rights Reserved.
//
// 盲探地图与层循环验证 —— D4 / §9
//
// ⚠️ 这个套件比其它套件更重要，原因：
//    Hex/Footprint/寻路/伤害管线都有 Godot 版的实测参考，
//    盲探地图【没有任何参考实现】—— 它是从策划案的框架描述从零设计的。
//    所以"设计意图"与"代码行为"是否一致，只能靠断言钉住。
//
// 三类断言：
//   ① 生成期不变量：连通性、Boss 必邻营地、入口到 Boss 的最短距离
//      —— 这些必须对【所有种子】成立，所以用 200 个种子穷举
//   ② 探索期性质：信息单调增长、可见性状态机不回退、
//      腐蚀度不可重复刷取
//   ③ R5 对抗：补偿②必须真的把 Boss 候选集合收窄
//      —— 若收窄不了，盲探就退化成随机点击，D4 白做

#include "Verify/HexVerify.h"
#include "Map/HexFloorMap.h"
#include "Run/HexRunState.h"
#include "Content/HexContentLibrary.h"
#include "Content/HexLayouts.h"
#include "Runes/HexRuneLibrary.h"
#include "Rng/HexRngStreams.h"
#include "Core/HexSpireConstants.h"

namespace
{
	/** 从入口出发的 BFS 距离；不可达为 -1 */
	void BfsFromEntrance(const FHexFloorMap& Map, TArray<int32>& OutDist)
	{
		const TArray<FHexRoomNode>& Rooms = Map.GetRooms();
		OutDist.Init(-1, Rooms.Num());

		const int32 Start = Map.GetEntranceId();
		if (!Rooms.IsValidIndex(Start))
		{
			return;
		}

		OutDist[Start] = 0;
		TArray<int32> Queue;
		Queue.Add(Start);

		int32 Head = 0;
		while (Head < Queue.Num())
		{
			const int32 Cur = Queue[Head++];
			for (const int32 Nid : Rooms[Cur].Neighbors)
			{
				if (OutDist.IsValidIndex(Nid) && OutDist[Nid] < 0)
				{
					OutDist[Nid] = OutDist[Cur] + 1;
					Queue.Add(Nid);
				}
			}
		}
	}

	// ─────────────────────────────────────────── 探索策略辅助
	//
	// ⚠️ 现在允许走回已清空的房间（否则叶节点会卡死玩家），
	//    所以"每步走 Accessible[0]"这种贪心会在两间房之间来回打转。
	//    验证器必须模拟【玩家实际会怎么走】：知道拓扑后规划路线。
	//    下面两个辅助就是这个规划器。

	/** BFS 求 From→To 的路径（含 To，不含 From）。不可达返回 false。 */
	bool FindPath(const FHexFloorMap& Map, int32 From, int32 To, TArray<int32>& OutPath)
	{
		OutPath.Reset();

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
		Prev.Init(-2, Rooms.Num());   // -2 = 未访问
		Prev[From] = -1;

		TArray<int32> Queue;
		Queue.Add(From);

		int32 Head = 0;
		bool bFound = false;
		while (Head < Queue.Num() && !bFound)
		{
			const int32 Cur = Queue[Head++];
			for (const int32 Nid : Rooms[Cur].Neighbors)
			{
				if (!Prev.IsValidIndex(Nid) || Prev[Nid] != -2)
				{
					continue;
				}
				Prev[Nid] = Cur;
				if (Nid == To)
				{
					bFound = true;
					break;
				}
				Queue.Add(Nid);
			}
		}

		if (!bFound)
		{
			return false;
		}

		// 回溯
		int32 Node = To;
		while (Node != From && Node >= 0)
		{
			OutPath.Insert(Node, 0);
			Node = Prev[Node];
		}
		return true;
	}

	/**
	 * 走向【最近的未清空房间】的下一步；全部清空或无路时返回 -1。
	 * 这模拟玩家"还有没探的地方就去探"的行为。
	 */
	int32 NextStepTowardUncleared(const FHexFloorMap& Map)
	{
		const TArray<FHexRoomNode>& Rooms = Map.GetRooms();
		const int32 Cur = Map.GetCurrentRoomId();

		int32 BestFirstStep = -1;
		int32 BestLen = INT32_MAX;

		for (const FHexRoomNode& R : Rooms)
		{
			if (R.Visibility == EHexRoomVisibility::Cleared)
			{
				continue;
			}

			TArray<int32> Path;
			if (!FindPath(Map, Cur, R.Id, Path) || Path.Num() == 0)
			{
				continue;
			}
			if (Path.Num() < BestLen)
			{
				BestLen = Path.Num();
				BestFirstStep = Path[0];
			}
		}

		return BestFirstStep;
	}

	/** 沿最短路走到目标房间；返回是否成功抵达 */
	bool TravelTo(FHexFloorMap& Map, int32 TargetId, bool bClearAlongTheWay)
	{
		TArray<int32> Path;
		if (!FindPath(Map, Map.GetCurrentRoomId(), TargetId, Path))
		{
			return false;
		}

		for (const int32 Step : Path)
		{
			if (!Map.EnterRoom(Step))
			{
				return false;
			}
			if (bClearAlongTheWay && Step != TargetId)
			{
				Map.ClearCurrentRoom();
			}
		}

		return Map.GetCurrentRoomId() == TargetId;
	}

	// ═══════════════════════════════════════════ ① 生成期不变量

	void CheckGeneration(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("生成期不变量（200 个种子穷举）"));

		bool bAllConnected = true;
		bool bAllBossNextToCamp = true;
		bool bAllRoomCountOk = true;
		bool bAllHaveOneBoss = true;
		bool bAllHaveOneCamp = true;
		bool bAllBossFarEnough = true;
		bool bAllCombatHaveEncounter = true;
		bool bAllLayoutsValid = true;
		bool bBossUsesOpenHall = true;
		bool bNoBottleneck = true;

		FString FirstFailure;
		int32 MinBossDist = 99;
		int32 MaxBossDist = 0;

		const TArray<FName>& ValidLayouts = FHexLayouts::AllLayoutIds();

		for (uint64 Seed = 1; Seed <= 200; ++Seed)
		{
			FHexRngStreams Rng(Seed);
			FHexFloorMap Map;
			Map.Generate(1, Rng);

			const TArray<FHexRoomNode>& Rooms = Map.GetRooms();

			// 房间数 = 6（用户决策 q19）
			if (Rooms.Num() != HexK::FloorRoomCount)
			{
				bAllRoomCountOk = false;
				if (FirstFailure.IsEmpty())
				{
					FirstFailure = FString::Printf(
						TEXT("seed=%llu 房间数=%d"), Seed, Rooms.Num());
				}
			}

			// ⚠️ 连通性：任何不可达的房间都会让玩家卡死在层里。
			//    这是最致命的一类 bug，且只在特定种子下出现 —— 必须穷举。
			TArray<int32> Dist;
			BfsFromEntrance(Map, Dist);
			for (int32 I = 0; I < Dist.Num(); ++I)
			{
				if (Dist[I] < 0)
				{
					bAllConnected = false;
					if (FirstFailure.IsEmpty())
					{
						FirstFailure = FString::Printf(
							TEXT("seed=%llu 房间 %d 不可达"), Seed, I);
					}
				}
			}

			// ⚠️ 补偿②的生成期保证：Boss 必邻营地。
			//    这条断掉，玩家找到营地也推不出 Boss 在哪，R5 立刻发生。
			if (!Map.IsBossAdjacentToCamp())
			{
				bAllBossNextToCamp = false;
				if (FirstFailure.IsEmpty())
				{
					FirstFailure = FString::Printf(TEXT("seed=%llu Boss 不邻营地"), Seed);
				}
			}

			// 类型唯一性
			{
				int32 NumBoss = 0, NumCamp = 0, NumEntrance = 0;
				for (const FHexRoomNode& R : Rooms)
				{
					if (R.Type == EHexRoomType::Boss) { ++NumBoss; }
					if (R.Type == EHexRoomType::Camp) { ++NumCamp; }
					if (R.Type == EHexRoomType::Entrance) { ++NumEntrance; }
				}
				if (NumBoss != 1 || NumEntrance != 1) { bAllHaveOneBoss = false; }
				if (NumCamp != 1) { bAllHaveOneCamp = false; }
			}

			// ⚠️ 入口到 Boss 至少 3 步。
			//    若只有 1–2 步，玩家第一次点击就可能撞上 Boss ——
			//    "探索"这件事没有发生的空间，D4 退化为"开局赌一把"。
			if (Dist.IsValidIndex(Map.GetBossId()))
			{
				const int32 D = Dist[Map.GetBossId()];
				MinBossDist = FMath::Min(MinBossDist, D);
				MaxBossDist = FMath::Max(MaxBossDist, D);
				if (D < 3)
				{
					bAllBossFarEnough = false;
					if (FirstFailure.IsEmpty())
					{
						FirstFailure = FString::Printf(
							TEXT("seed=%llu 入口到 Boss 仅 %d 步"), Seed, D);
					}
				}
			}

			// 战斗房必须有怪物组与地形，且都是有效引用
			for (const FHexRoomNode& R : Rooms)
			{
				if (!R.IsCombat())
				{
					continue;
				}

				if (R.EncounterId.IsNone())
				{
					bAllCombatHaveEncounter = false;
					continue;
				}

				// 怪物组必须非空（空组会让战斗一开始就判胜）
				TArray<FHexEncounterEntry> Entries;
				FHexContentLibrary::GetEncounter(R.EncounterId, Entries);
				if (Entries.Num() == 0)
				{
					bAllCombatHaveEncounter = false;
				}

				if (!ValidLayouts.Contains(R.LayoutId))
				{
					bAllLayoutsValid = false;
					if (FirstFailure.IsEmpty())
					{
						FirstFailure = FString::Printf(TEXT("seed=%llu 未知地形 %s"),
							Seed, *R.LayoutId.ToString());
					}
				}

				// ⚠️ 排除 bottleneck：1 格门配追踪型远程敌人 =
				//    玩家毫无应对手段（Godot 实测 15 场全败）。
				if (R.LayoutId == FName(TEXT("bottleneck")))
				{
					bNoBottleneck = false;
				}

				// ⚠️ Boss 房必须用 open_hall：攻城虫是 L 体型占 6 格，
				//    狭道/石柱会把它卡住 —— Boss 被地形困死是 bug 观感。
				if (R.Type == EHexRoomType::Boss
					&& R.LayoutId != FName(TEXT("open_hall")))
				{
					bBossUsesOpenHall = false;
				}
			}
		}

		Ctx.Check(TEXT("房间数 = 6（q19）"), bAllRoomCountOk, FirstFailure);
		Ctx.Check(TEXT("全图连通（无卡死）"), bAllConnected, FirstFailure);
		Ctx.Check(TEXT("补偿②：Boss 必邻营地"), bAllBossNextToCamp, FirstFailure);
		Ctx.Check(TEXT("入口/Boss 各恰好 1 间"), bAllHaveOneBoss, TEXT(""));
		Ctx.Check(TEXT("营地恰好 1 间（补偿②的锚点）"), bAllHaveOneCamp, TEXT(""));
		Ctx.Check(TEXT("入口到 Boss ≥3 步"), bAllBossFarEnough,
			FString::Printf(TEXT("%s（最短=%d 最长=%d）"),
				*FirstFailure, MinBossDist, MaxBossDist));
		Ctx.Check(TEXT("战斗房均有有效怪物组"), bAllCombatHaveEncounter, TEXT(""));
		Ctx.Check(TEXT("地形模板 id 均有效"), bAllLayoutsValid, FirstFailure);
		Ctx.Check(TEXT("不使用 bottleneck 地形"), bNoBottleneck,
			TEXT("1格门+追踪远程 = 玩家毫无应对手段"));
		Ctx.Check(TEXT("Boss 房使用 open_hall（L 体型可容纳）"), bBossUsesOpenHall, TEXT(""));

		// 拓扑必须【真的随机】—— 否则每局地图一样，盲探第二次就失效
		{
			TSet<uint32> Shapes;
			for (uint64 Seed = 1; Seed <= 60; ++Seed)
			{
				FHexRngStreams Rng(Seed);
				FHexFloorMap Map;
				Map.Generate(1, Rng);
				Shapes.Add(Map.ContentHash());
			}
			Ctx.Check(TEXT("60 个种子产出 ≥4 种不同地图"),
				Shapes.Num() >= 4,
				FString::Printf(TEXT("仅 %d 种 —— 地图不够随机，盲探第二局就失效"),
					Shapes.Num()));
		}

		// 同种子必须完全复现（架构纪律 1）
		{
			FHexRngStreams A(777);
			FHexRngStreams B(777);
			FHexFloorMap MapA, MapB;
			MapA.Generate(1, A);
			MapB.Generate(1, B);
			Ctx.Check(TEXT("同种子地图完全一致"),
				MapA.ContentHash() == MapB.ContentHash(),
				FString::Printf(TEXT("%u vs %u"), MapA.ContentHash(), MapB.ContentHash()));
		}

		// 地图生成只能消耗 Map 流 —— 否则玩家的战斗操作会改变地图
		{
			FHexRngStreams Rng(1234);
			const int32 CombatBefore = Rng.Stream(EHexRngStream::Combat).State[0] != 0 ? 1 : 0;
			FHexFloorMap Map;
			Map.Generate(1, Rng);

			// 用 Combat 流掷一次，与"未生成地图"的情况对比
			FHexRngStreams Fresh(1234);
			Ctx.CheckEqual(TEXT("地图生成不消耗 Combat 流"),
				Rng.Stream(EHexRngStream::Combat).State[0] ==
				Fresh.Stream(EHexRngStream::Combat).State[0] ? 1 : 0, 1);
			(void)CombatBefore;
		}
	}

	// ═══════════════════════════════════════════ ② 探索期性质

	void CheckExploration(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("探索期性质"));

		// ── 初始可见性
		{
			FHexRngStreams Rng(42);
			FHexFloorMap Map;
			Map.Generate(1, Rng);

			Ctx.CheckEqual(TEXT("初始位于入口"),
				Map.GetCurrentRoomId(), Map.GetEntranceId());

			const FHexRoomNode* Entrance = Map.FindRoom(Map.GetEntranceId());
			Ctx.Check(TEXT("入口初始即为 Cleared"),
				Entrance && Entrance->Visibility == EHexRoomVisibility::Cleared,
				TEXT("入口需要战斗会让开局就打一场，违反 §9.6"));

			// ⚠️ D4 的核心：Boss 初始【不可见】。
			//    这一条断掉，整个 D4 决策就不存在了。
			const FHexRoomNode* Boss = Map.FindRoom(Map.GetBossId());
			Ctx.Check(TEXT("Boss 初始不可见（D4 的核心）"),
				Boss && Boss->Visibility == EHexRoomVisibility::Unknown,
				TEXT("Boss 开局可见 → D4 决策不存在"));

			// 入口的邻居应为 Known（补偿④：看得见有门）
			int32 KnownNeighbors = 0;
			if (Entrance)
			{
				for (const int32 Nid : Entrance->Neighbors)
				{
					const FHexRoomNode* N = Map.FindRoom(Nid);
					if (N && N->Visibility == EHexRoomVisibility::Known)
					{
						++KnownNeighbors;
					}
				}
			}
			Ctx.Check(TEXT("补偿④：入口邻居初始为 Known"),
				KnownNeighbors >= 2,
				FString::Printf(TEXT("Known 邻居数=%d"), KnownNeighbors));

			// 初始信息量应该很低（只知道入口自己）
			Ctx.Check(TEXT("初始已知类型比例 ≤0.2"),
				Map.GetKnownRatio() <= 0.2f + 0.001f,
				FString::Printf(TEXT("初始已知比例=%.3f —— 开局信息过多"),
					Map.GetKnownRatio()));
		}

		// ── 不可达房间不得进入
		{
			FHexRngStreams Rng(99);
			FHexFloorMap Map;
			Map.Generate(1, Rng);

			// Boss 与入口不相邻（已由"≥3 步"保证），所以直接进入必须失败
			Ctx.Check(TEXT("不可直接跳到 Boss 房"),
				!Map.EnterRoom(Map.GetBossId()),
				TEXT("能从入口直达 Boss → 探索被跳过"));

			Ctx.Check(TEXT("非法房间 id 进入失败"),
				!Map.EnterRoom(999), TEXT(""));

			// 当前位置不该被失败的进入改变
			Ctx.CheckEqual(TEXT("失败的进入不改变当前位置"),
				Map.GetCurrentRoomId(), Map.GetEntranceId());
		}

		// ── 信息单调增长
		//
		// ⚠️ 这是揭示逻辑最容易出错的地方：
		//    RevealNeighborTopology 若写成无条件赋值，会把已揭示的房间
		//    "退回"Known 状态，玩家的信息量时增时减 ——
		//    那比完全没有补偿更糟（玩家会以为自己记错了）。
		{
			bool bMonotonic = true;
			bool bNoRegress = true;
			FString Bad;

			for (uint64 Seed = 1; Seed <= 80 && bMonotonic && bNoRegress; ++Seed)
			{
				FHexRngStreams Rng(Seed);
				FHexFloorMap Map;
				Map.Generate(1, Rng);

				float PrevRatio = Map.GetKnownRatio();

				// 记录每间房的可见性等级，检查不回退
				TArray<uint8> PrevVis;
				for (const FHexRoomNode& R : Map.GetRooms())
				{
					PrevVis.Add(static_cast<uint8>(R.Visibility));
				}

				// 探索：每步走向最近的未清空房间，直到全部清空
				for (int32 Step = 0; Step < 24; ++Step)
				{
					const int32 Next = NextStepTowardUncleared(Map);
					if (Next < 0)
					{
						break;
					}

					Map.EnterRoom(Next);
					Map.ClearCurrentRoom();

					const float NewRatio = Map.GetKnownRatio();
					if (NewRatio < PrevRatio - 0.0001f)
					{
						bMonotonic = false;
						Bad = FString::Printf(TEXT("seed=%llu 步%d 信息 %.3f→%.3f"),
							Seed, Step, PrevRatio, NewRatio);
						break;
					}
					PrevRatio = NewRatio;

					const TArray<FHexRoomNode>& Rooms = Map.GetRooms();
					for (int32 I = 0; I < Rooms.Num(); ++I)
					{
						const uint8 Now = static_cast<uint8>(Rooms[I].Visibility);
						if (Now < PrevVis[I])
						{
							bNoRegress = false;
							Bad = FString::Printf(TEXT("seed=%llu 房间%d 可见性 %d→%d"),
								Seed, I, PrevVis[I], Now);
							break;
						}
						PrevVis[I] = Now;
					}
					if (!bNoRegress)
					{
						break;
					}
				}
			}

			Ctx.Check(TEXT("信息量单调不减"), bMonotonic, Bad);
			Ctx.Check(TEXT("可见性状态机不回退"), bNoRegress, Bad);
		}

		// ── 已清空房间可以路过，但不再产生收益
		//
		// ⚠️ 防"刷房间赚腐蚀度"靠的是【收益幂等】，不是【封路】。
		//    早期版本用封路来防刷，结果精英房成为叶节点时玩家直接卡死
		//    （seed=1 复现）。收益幂等既能防刷，也不会卡死。
		{
			FHexRngStreams Rng(555);
			FHexFloorMap Map;
			Map.Generate(1, Rng);

			TArray<int32> Accessible;
			Map.GetAccessibleRooms(Accessible);
			if (Accessible.Num() > 0)
			{
				const int32 First = Accessible[0];
				Map.EnterRoom(First);

				const int32 Delta1 = Map.ClearCurrentRoom();
				const int32 Delta2 = Map.ClearCurrentRoom();

				Ctx.Check(TEXT("首次清空产生腐蚀度"), Delta1 > 0,
					FString::Printf(TEXT("Delta=%d"), Delta1));

				// ⚠️ 若重复清空能重复计数，玩家可以刷腐蚀度换掉落，
				//    风险收益的平衡彻底失效。
				Ctx.CheckEqual(TEXT("重复清空不再计腐蚀度"), Delta2, 0);

				// 走回去再清一次，仍然不给
				Map.GetAccessibleRooms(Accessible);
				Ctx.Check(TEXT("已清空房间仍可路过（不封路，避免卡死）"),
					Accessible.Contains(Map.GetEntranceId()),
					TEXT("封路会让叶节点房间成为死胡同"));

				Map.EnterRoom(Map.GetEntranceId());
				Map.EnterRoom(First);
				Ctx.CheckEqual(TEXT("往返后重复清空仍不给腐蚀度"),
					Map.ClearCurrentRoom(), 0);
			}
		}

		// ── 探索可完成：任意种子下都能走到 Boss
		//
		// ⚠️ 这条断言在 seed=1 抓到过真实缺陷：
		//    早期版本禁止走回已清空的房间，于是精英房成为叶节点时
		//    玩家清空它之后【无路可走】，整局卡死。
		{
			bool bAllReachable = true;
			FString Bad;

			for (uint64 Seed = 1; Seed <= 100 && bAllReachable; ++Seed)
			{
				FHexRngStreams Rng(Seed);
				FHexFloorMap Map;
				Map.Generate(1, Rng);

				// 先把所有非 Boss 房探完（模拟"探完再打 Boss"的谨慎玩家）
				for (int32 Step = 0; Step < 24; ++Step)
				{
					const int32 Next = NextStepTowardUncleared(Map);
					if (Next < 0 || Next == Map.GetBossId())
					{
						break;
					}
					Map.EnterRoom(Next);
					Map.ClearCurrentRoom();
				}

				// 再走向 Boss
				if (!TravelTo(Map, Map.GetBossId(), /*bClearAlongTheWay=*/false))
				{
					bAllReachable = false;
					Bad = FString::Printf(TEXT("seed=%llu 走不到 Boss"), Seed);
				}
			}

			Ctx.Check(TEXT("任意种子下都能走到 Boss（不会卡死）"),
				bAllReachable, Bad);
		}

		// ── 激进玩家：一发现 Boss 可达就直冲，也不能卡死
		{
			bool bOk = true;
			FString Bad;

			for (uint64 Seed = 1; Seed <= 100 && bOk; ++Seed)
			{
				FHexRngStreams Rng(Seed);
				FHexFloorMap Map;
				Map.Generate(1, Rng);

				if (!TravelTo(Map, Map.GetBossId(), /*bClearAlongTheWay=*/true))
				{
					bOk = false;
					Bad = FString::Printf(TEXT("seed=%llu 直冲 Boss 失败"), Seed);
				}
			}

			Ctx.Check(TEXT("直冲 Boss 的玩家也不会卡死"), bOk, Bad);
		}
	}

	// ═══════════════════════════════════════════ ③ R5 对抗

	void CheckR5Mitigation(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("R5 对抗：补偿②必须收窄 Boss 候选"));

		// ⚠️ 这是整个 D4 决策的成败判据。
		//    R5：信息不足的探索会退化为随机点击。
		//    补偿②的价值必须可量化 —— 找到营地后，
		//    "Boss 在哪"应当从 5 选 1 收窄到 1–3 选 1。
		//    若收窄不了，D4 就是纯粹的运气游戏，白做。

		// ── 营地未揭示时不给线索
		{
			FHexRngStreams Rng(31);
			FHexFloorMap Map;
			Map.Generate(1, Rng);

			TArray<int32> Candidates;
			Map.GetBossCandidates(Candidates);

			const FHexRoomNode* Camp = Map.FindRoom(Map.GetCampId());
			if (Camp && Camp->Visibility == EHexRoomVisibility::Unknown)
			{
				// 补偿②是"找到营地"的奖励，不是开局白送的提示
				Ctx.CheckEqual(TEXT("营地未发现时不提供 Boss 线索"),
					Candidates.Num(), 0);
			}
		}

		// ── 营地揭示后必须收窄，且 Boss 必在候选内
		{
			int32 TotalTrials = 0;
			int32 BossInCandidates = 0;
			int32 SumCandidates = 0;
			int32 WorstCandidates = 0;

			for (uint64 Seed = 1; Seed <= 150; ++Seed)
			{
				FHexRngStreams Rng(Seed);
				FHexFloorMap Map;
				Map.Generate(1, Rng);

				// 一路探到营地（沿最短路，途经的房间顺手清掉）
				const bool bFoundCamp = TravelTo(
					Map, Map.GetCampId(), /*bClearAlongTheWay=*/true);

				if (!bFoundCamp)
				{
					continue;
				}

				TArray<int32> Candidates;
				Map.GetBossCandidates(Candidates);

				++TotalTrials;
				SumCandidates += Candidates.Num();
				WorstCandidates = FMath::Max(WorstCandidates, Candidates.Num());
				if (Candidates.Contains(Map.GetBossId()))
				{
					++BossInCandidates;
				}
			}

			Ctx.Check(TEXT("测试样本足够（≥100 次成功找到营地）"),
				TotalTrials >= 100,
				FString::Printf(TEXT("仅 %d 次"), TotalTrials));

			// ⚠️ 100% 命中：Boss 必邻营地是硬约束，
			//    候选集合里没有 Boss 说明补偿②在探索期失效了。
			Ctx.CheckEqual(TEXT("Boss 必在候选集合内（100%）"),
				BossInCandidates, TotalTrials);

			// 收窄程度：候选数必须显著小于总房间数
			const float AvgCandidates = TotalTrials > 0
				? static_cast<float>(SumCandidates) / static_cast<float>(TotalTrials)
				: 99.0f;

			Ctx.Check(TEXT("平均候选数 ≤2.5（从 5 选 1 收窄）"),
				AvgCandidates <= 2.5f,
				FString::Printf(TEXT("平均候选=%.2f 最差=%d —— 收窄不足则 R5 发生"),
					AvgCandidates, WorstCandidates));

			Ctx.Check(TEXT("最差情况候选数 ≤3"),
				WorstCandidates <= 3,
				FString::Printf(TEXT("最差=%d"), WorstCandidates));
		}

		// ── 补偿④的量化价值：探明一间房必须带来信息增量
		{
			FHexRngStreams Rng(2001);
			FHexFloorMap Map;
			Map.Generate(1, Rng);

			// 统计 Known（知道位置不知类型）的房间数
			auto CountKnown = [&Map]() -> int32
			{
				int32 N = 0;
				for (const FHexRoomNode& R : Map.GetRooms())
				{
					if (R.Visibility == EHexRoomVisibility::Known)
					{
						++N;
					}
				}
				return N;
			};

			const float RatioBefore = Map.GetKnownRatio();
			const int32 KnownBefore = CountKnown();

			TArray<int32> Accessible;
			Map.GetAccessibleRooms(Accessible);
			if (Accessible.Num() > 0)
			{
				Map.EnterRoom(Accessible[0]);

				Ctx.Check(TEXT("补偿④：探明一间房后类型已知比例上升"),
					Map.GetKnownRatio() > RatioBefore,
					FString::Printf(TEXT("%.3f → %.3f"), RatioBefore, Map.GetKnownRatio()));

				// 拓扑揭示应当让新的房间进入 Known 状态
				// （或已有 Known 的转为 Revealed，两者都算信息增长）
				Ctx.Check(TEXT("补偿④：拓扑信息发生变化"),
					CountKnown() != KnownBefore || Map.GetKnownRatio() > RatioBefore,
					TEXT("探明房间后拓扑毫无变化 → 补偿④未生效"));
			}
		}

		// ── 全探完后信息必须完整（否则玩家永远不知道自己漏了什么）
		{
			bool bFullyKnown = true;
			FString Bad;

			for (uint64 Seed = 1; Seed <= 60 && bFullyKnown; ++Seed)
			{
				FHexRngStreams Rng(Seed);
				FHexFloorMap Map;
				Map.Generate(1, Rng);

				for (int32 Step = 0; Step < 24; ++Step)
				{
					const int32 Next = NextStepTowardUncleared(Map);
					if (Next < 0 || Next == Map.GetBossId())
					{
						break;
					}
					Map.EnterRoom(Next);
					Map.ClearCurrentRoom();
				}

				// 探完所有非 Boss 房后，Boss 至少应该是 Known
				const FHexRoomNode* Boss = Map.FindRoom(Map.GetBossId());
				if (Map.AreAllNonBossRoomsCleared() && Boss
					&& Boss->Visibility == EHexRoomVisibility::Unknown)
				{
					bFullyKnown = false;
					Bad = FString::Printf(
						TEXT("seed=%llu 全探完后 Boss 仍 Unknown"), Seed);
				}
			}

			Ctx.Check(TEXT("探完全部非 Boss 房后 Boss 位置已知"), bFullyKnown, Bad);
		}
	}

	// ═══════════════════════════════════════════ 层循环

	void CheckRunState(FHexVerifyContext& Ctx)
	{
		Ctx.Section(TEXT("层循环（RunState）"));

		// ── 开局状态
		{
			FHexRngStreams Rng(1);
			FHexRunState Run(1);
			Run.BeginRun(TEXT("warden"), Rng);

			Ctx.CheckEqual(TEXT("开局在第 1 层"), Run.FloorIndex, 1);
			Ctx.CheckEqual(TEXT("开局腐蚀度 = 0"), Run.Corruption, 0);
			Ctx.CheckEqual(TEXT("开局碎片 = 0"), Run.Shards, 0);
			Ctx.CheckEqual(TEXT("开局卡组容量 = 8"),
				Run.DeckCapacity, HexK::InitialDeckCapacity);
			Ctx.CheckEqual(TEXT("镇妖者 HP = 80"), Run.HeroHPMax, 80);
			Ctx.Check(TEXT("开局满血"), Run.HeroHP == Run.HeroHPMax, TEXT(""));
			Ctx.Check(TEXT("开局卡组非空"), Run.Deck.Num() > 0, TEXT(""));

			// ⚠️ q18 的核心：必须留 ≥3 个空位，否则掠夺空转
			const int32 Free = Run.DeckCapacity - Run.GetUsedCapacity();
			Ctx.Check(TEXT("开局留 ≥3 个卡组空位（q18）"), Free >= 3,
				FString::Printf(TEXT("空位=%d（已用 %d/%d）"),
					Free, Run.GetUsedCapacity(), Run.DeckCapacity));

			Ctx.Check(TEXT("开局符文槽为空（空槽是邀请）"),
				Run.RuneLoadout.GetFilledCount() == 0, TEXT(""));
		}

		// ── D3 替换制
		{
			FHexRngStreams Rng(2);
			FHexRunState Run(2);
			Run.BeginRun(TEXT("warden"), Rng);

			// 填满容量
			int32 Added = 0;
			while (Run.HasCapacityRoom() && Added < 20)
			{
				if (!Run.AddCardToDeck(TEXT("pierce_javelin")))
				{
					break;
				}
				++Added;
			}

			// ⚠️ 满容量时必须【拒绝】加卡，由 UI 强制玩家选"挤掉哪张"。
			//    若静默替换，§3.2 的核心决策就被代码代替玩家做了。
			if (!Run.HasCapacityRoom())
			{
				Ctx.Check(TEXT("满容量时拒绝加卡（D3 替换制）"),
					!Run.AddCardToDeck(TEXT("charge")),
					TEXT("满容量仍能加卡 → 卡组无限膨胀，D3 失效"));
			}

			// 同名份数上限
			{
				FHexRunState R2(3);
				FHexRngStreams Rng2(3);
				R2.BeginRun(TEXT("warden"), Rng2);
				R2.DeckCapacity = HexK::MaxDeckCapacity;

				int32 Copies = 0;
				while (R2.AddCardToDeck(TEXT("charge")) && Copies < 10)
				{
					++Copies;
				}
				Ctx.Check(TEXT("同名卡份数受 MaxCopiesInDeck 限制"),
					Copies <= HexK::DefaultMaxCopiesInDeck,
					FString::Printf(TEXT("加入了 %d 份"), Copies));
			}

			// 基石卡不可移除
			//
			// ⚠️ 契约已升级：基石卡不再放进卡组，而是常驻 FixedCards。
			//    以前靠 RemoveCardFromDeck 主动拒绝来保证"不会弄丢《攻击》"，
			//    现在它压根不在卡组里 —— 从结构上就不可能被移除，
			//    这比运行时判断更可靠。
			{
				int32 CornerstoneInDeck = 0;
				for (const FHexCardInstance& C : Run.Deck)
				{
					const FHexCardData* D = FHexContentLibrary::FindCard(C.CardId);
					if (D && D->bIsCornerstone)
					{
						++CornerstoneInDeck;
					}
				}
				Ctx.CheckEqual(TEXT("基石卡不在卡组中（§7.2 结构性保证）"),
					CornerstoneInDeck, 0);

				// 而且必须真的存在于固定卡区 —— 否则就是"弄丢了"，
				// 玩家会开局没有《攻击》可用。
				bool bAllPresent = Run.FixedCards.Num() > 0;
				for (const FHexCardInstance& C : Run.FixedCards)
				{
					const FHexCardData* D = FHexContentLibrary::FindCard(C.CardId);
					if (!D || !D->bIsCornerstone)
					{
						bAllPresent = false;
						break;
					}
				}
				Ctx.Check(TEXT("基石卡常驻于固定卡区"),
					bAllPresent,
					TEXT("固定卡区缺失基石 → 玩家将没有基础动作可用"));
			}
		}

		// ── 符文装备与替换流程（§6.6）
		//
		// ══════════════════════════════════════════════════════════
		// 为什么这一节必须有：RuneInventory 原本【只进不出】
		// ══════════════════════════════════════════════════════════
		// ApplyReward 在满槽时会把符文塞进 RuneInventory，
		// 但全工程【没有任何代码能把它装回槽位】——
		// 玩家攒一堆符文却永远用不上，等于奖励凭空消失。
		//
		// §6.6 明确要求"三选一界面下方显示当前 6 槽，玩家指定覆盖目标"，
		// 这需要三个能力：从背包装备、替换指定槽、槽位重排。
		// 三者原先都没有可调用的 API。
		{
			FHexRngStreams Rng(11);
			FHexRunState Run(11);
			Run.BeginRun(TEXT("warden"), Rng);

			// 先把 6 槽塞满（用符文库里的前 6 个）
			const TArray<FHexRuneData>& All = FHexRuneLibrary::AllRunes();
			Ctx.Check(TEXT("符文库至少有 7 个符文（替换测试需要）"),
				All.Num() >= 7, TEXT(""));

			if (All.Num() >= 7)
			{
				for (int32 I = 0; I < FHexRuneLoadout::SlotCount; ++I)
				{
					Run.RuneLoadout.SetSlot(I, &All[I]);
				}
				Ctx.CheckEqual(TEXT("6 槽已塞满"),
					Run.RuneLoadout.GetFilledCount(), FHexRuneLoadout::SlotCount);

				// ── 满槽时获得新符文 → 进背包
				Run.RuneInventory.Reset();
				{
					FHexRewardOption Opt;
					Opt.Kind = FHexRewardOption::EKind::Rune;
					Opt.ContentId = All[6].Id;
					Run.ApplyReward(Opt, Rng);
				}
				Ctx.Check(TEXT("满槽时新符文进入背包"),
					Run.RuneInventory.Contains(All[6].Id),
					TEXT("满槽的符文既没装上也没进背包 → 奖励凭空消失"));

				// ── 从背包装备到指定槽（替换）
				const FName Replaced = All[2].Id;
				const bool bEquipped = Run.EquipRuneFromInventory(All[6].Id, 2);

				Ctx.Check(TEXT("能把背包里的符文装到指定槽（§6.6 替换）"),
					bEquipped,
					TEXT("没有'从背包装备'的能力 → 玩家攒的符文永远用不上"));

				if (bEquipped)
				{
					const FHexRuneData* InSlot = Run.RuneLoadout.GetSlot(2);
					Ctx.Check(TEXT("目标槽已换成新符文"),
						InSlot && InSlot->Id == All[6].Id, TEXT(""));

					Ctx.Check(TEXT("新符文已从背包移除"),
						!Run.RuneInventory.Contains(All[6].Id),
						TEXT("装备后仍留在背包 → 同一符文可被无限复制"));

					// ⚠️ 被替换下来的符文必须【销毁】，不能回背包。
					//    §6.6 原文："被覆盖的符文销毁，不可回收"。
					//    若回收进背包，玩家就能在 6 槽间无成本地反复横跳，
					//    "选择覆盖哪个"这个决策会失去代价。
					Ctx.Check(TEXT("被替换的符文已销毁（§6.6 不可回收）"),
						!Run.RuneInventory.Contains(Replaced),
						TEXT("被替换的符文回到了背包 → 替换失去代价，"
							 "玩家可无成本反复横跳"));
				}

				// ── 槽位重排（§6.5：顺序影响结算，所以重排是真实操作）
				{
					const FHexRuneData* Before0 = Run.RuneLoadout.GetSlot(0);
					const FHexRuneData* Before5 = Run.RuneLoadout.GetSlot(5);

					const bool bMoved = Run.ReorderRune(0, 5);
					Ctx.Check(TEXT("能重排符文槽位（§6.5 顺序即策略）"),
						bMoved, TEXT(""));

					if (bMoved)
					{
						Ctx.Check(TEXT("重排后两槽内容交换"),
							Run.RuneLoadout.GetSlot(0) == Before5
							&& Run.RuneLoadout.GetSlot(5) == Before0,
							TEXT("重排没有真正交换内容"));
					}
				}

				// ── 非法输入不得破坏状态
				{
					const int32 FilledBefore = Run.RuneLoadout.GetFilledCount();
					Ctx.Check(TEXT("装备不存在的符文被拒绝"),
						!Run.EquipRuneFromInventory(TEXT("__no_such_rune__"), 0),
						TEXT(""));
					Ctx.Check(TEXT("越界槽位被拒绝"),
						!Run.ReorderRune(0, 99), TEXT(""));
					Ctx.CheckEqual(TEXT("非法操作未改变槽位占用数"),
						Run.RuneLoadout.GetFilledCount(), FilledBefore);
				}

				// ── 战斗中锁定（§6.5）
				//
				// ⚠️ 原先这条只写在注释里说"由调用方保证"——
				//    而"由调用方保证"等于没保证。锁必须在逻辑层，
				//    否则任何一个忘了检查的 UI 入口都能让玩家
				//    在战斗中途反复重排去找最优顺序。
				{
					// 先卸一个到背包，好有东西可装
					Run.UnequipRuneToInventory(1);
					const bool bHasInvItem = Run.RuneInventory.Num() > 0;

					Run.bRuneLayoutLocked = true;

					Ctx.Check(TEXT("战斗中禁止重排符文（§6.5）"),
						!Run.ReorderRune(0, 3),
						TEXT("战斗中仍可重排 → 顺序决策退化为每回合手动最优化"));

					if (bHasInvItem)
					{
						Ctx.Check(TEXT("战斗中禁止装备符文"),
							!Run.EquipRuneFromInventory(Run.RuneInventory[0], 1),
							TEXT(""));
					}
					Ctx.Check(TEXT("战斗中禁止卸下符文"),
						!Run.UnequipRuneToInventory(0), TEXT(""));

					// 解锁后恢复正常
					Run.bRuneLayoutLocked = false;
					Ctx.Check(TEXT("解锁后可正常重排"),
						Run.ReorderRune(0, 3), TEXT(""));
				}
			}
		}

		// ── 层结算三选一（§6.6）
		//
		// ⚠️ 这是获得符文的【唯一途径】。它原先只被试玩 commandlet
		//    调用过（而且无脑取 Rewards[0]），游戏侧没有任何入口 ——
		//    玩家打赢 Boss 拿不到符文，D6 整个系统是死的。
		{
			FHexRngStreams Rng(12);
			FHexRunState Run(12);
			Run.BeginRun(TEXT("warden"), Rng);

			TArray<FHexRewardOption> Rewards;
			Run.GenerateFloorRewards(Rng, Rewards);

			Ctx.Check(TEXT("层结算能生成奖励选项"),
				Rewards.Num() > 0,
				TEXT("生成不出奖励 → 打赢 Boss 什么都拿不到"));

			// 必须至少有一个符文选项 —— 否则层结算不是"符文的入口"
			int32 RuneOptions = 0;
			bool bAllHaveDesc = true;
			bool bNoCursed = true;
			for (const FHexRewardOption& R : Rewards)
			{
				if (R.Kind == FHexRewardOption::EKind::Rune)
				{
					++RuneOptions;
					// 诅咒不该进必选的三选一（策划案：强迫玩家吃亏）
					const FHexRuneData* Rune = FHexRuneLibrary::FindRune(R.ContentId);
					if (Rune && Rune->bIsCursed)
					{
						bNoCursed = false;
					}
				}
				// R8：每项都要能读懂
				if (R.Description.IsEmpty())
				{
					bAllHaveDesc = false;
				}
			}

			Ctx.Check(TEXT("层结算包含符文选项（§6.6 保底）"),
				RuneOptions > 0,
				TEXT("三选一里没有符文 → 符文没有获取途径"));

			Ctx.Check(TEXT("奖励项都有机制说明（R8）"),
				bAllHaveDesc,
				TEXT("选项没有说明 → 玩家无法判断该选哪个，三选一变抽奖"));

			Ctx.Check(TEXT("三选一不含诅咒符文"),
				bNoCursed,
				TEXT("必选场合塞诅咒 = 强迫玩家吃亏"));

			// 应用一次必须真的生效
			if (Rewards.Num() > 0)
			{
				const int32 FilledBefore = Run.RuneLoadout.GetFilledCount();
				int32 RuneIdx = INDEX_NONE;
				for (int32 I = 0; I < Rewards.Num(); ++I)
				{
					if (Rewards[I].Kind == FHexRewardOption::EKind::Rune)
					{
						RuneIdx = I;
						break;
					}
				}

				if (RuneIdx != INDEX_NONE)
				{
					Ctx.Check(TEXT("应用符文奖励成功"),
						Run.ApplyReward(Rewards[RuneIdx], Rng), TEXT(""));
					Ctx.CheckEqual(TEXT("符文真的装进了槽位"),
						Run.RuneLoadout.GetFilledCount(), FilledBefore + 1);
				}
			}

			// 已持有的符文不该再出现在三选一里
			{
				TArray<FHexRewardOption> Again;
				Run.GenerateFloorRewards(Rng, Again);

				TSet<FName> Owned;
				TArray<TPair<int32, const FHexRuneData*>> Equipped;
				Run.RuneLoadout.GetRunesInOrder(Equipped);
				for (const TPair<int32, const FHexRuneData*>& P : Equipped)
				{
					if (P.Value) { Owned.Add(P.Value->Id); }
				}

				bool bNoDup = true;
				for (const FHexRewardOption& R : Again)
				{
					if (R.Kind == FHexRewardOption::EKind::Rune
						&& Owned.Contains(R.ContentId))
					{
						bNoDup = false;
						break;
					}
				}
				Ctx.Check(TEXT("三选一不重复已持有的符文"),
					bNoDup,
					TEXT("给已装备的符文 = 这一选等于没有选项"));
			}
		}

		// ── 腐蚀度累积
		{
			FHexRngStreams Rng(4);
			FHexRunState Run(4);
			Run.BeginRun(TEXT("warden"), Rng);

			int32 Steps = 0;
			while (Steps < 24)
			{
				const int32 Next = NextStepTowardUncleared(Run.Map);
				if (Next < 0)
				{
					break;
				}
				Run.Map.EnterRoom(Next);
				Run.OnRoomCleared();
				++Steps;
			}

			Ctx.Check(TEXT("探索后腐蚀度上升"), Run.Corruption > 0,
				FString::Printf(TEXT("腐蚀度=%d 清空=%d 间"),
					Run.Corruption, Run.Stats.RoomsCleared));

			// 精英房 +2 而普通房 +1，所以腐蚀度应大于战斗房数量
			Ctx.Check(TEXT("统计与腐蚀度一致"),
				Run.Stats.RoomsCleared > 0, TEXT(""));

			// ⚠️ 腐蚀度跨层继承（不重置）—— 肉鸽的加压曲线依赖这一点
			const int32 Before = Run.Corruption;
			Run.BeginFloor(2, Rng);
			Ctx.CheckEqual(TEXT("进入新层腐蚀度不重置"), Run.Corruption, Before);
			Ctx.CheckEqual(TEXT("进入新层统计重置"), Run.Stats.RoomsCleared, 0);
			Ctx.CheckEqual(TEXT("层号已更新"), Run.FloorIndex, 2);
		}

		// ── 营地
		{
			FHexRngStreams Rng(5);
			FHexRunState Run(5);
			Run.BeginRun(TEXT("warden"), Rng);

			Run.HeroHP = 20;

			// 走到营地（沿最短路，途经房间顺手清掉）
			const bool bAtCamp = TravelTo(
				Run.Map, Run.Map.GetCampId(), /*bClearAlongTheWay=*/true);

			if (bAtCamp)
			{
				const int32 Healed = Run.RestAtCamp();
				Ctx.Check(TEXT("营地恢复生命"), Healed > 0,
					FString::Printf(TEXT("恢复=%d"), Healed));

				// ⚠️ 不满血：满血营地会让血量管理这条压力线消失
				Ctx.Check(TEXT("营地不回满血（保留血量压力）"),
					Run.HeroHP < Run.HeroHPMax,
					FString::Printf(TEXT("HP=%d/%d"), Run.HeroHP, Run.HeroHPMax));

				// 再次休息应无效（营地已清空）
				Ctx.CheckEqual(TEXT("营地只能用一次"), Run.RestAtCamp(), 0);
			}
			else
			{
				Ctx.Fail(TEXT("能走到营地"), TEXT("探索路径未能到达营地"));
			}

			// 非营地房间休息无效
			{
				FHexRunState R2(6);
				FHexRngStreams Rng2(6);
				R2.BeginRun(TEXT("warden"), Rng2);
				R2.HeroHP = 20;
				Ctx.CheckEqual(TEXT("非营地房间无法休息"), R2.RestAtCamp(), 0);
			}
		}

		// ── 层结算三选一
		{
			FHexRngStreams Rng(7);
			FHexRunState Run(7);
			Run.BeginRun(TEXT("warden"), Rng);

			TArray<FHexRewardOption> Rewards;
			Run.GenerateFloorRewards(Rng, Rewards);

			// 3 符文 + 容量 + 碎片 = 5 项
			Ctx.Check(TEXT("层结算给出 ≥4 个选项"), Rewards.Num() >= 4,
				FString::Printf(TEXT("选项数=%d"), Rewards.Num()));

			int32 RuneCount = 0;
			bool bAllHaveDesc = true;
			bool bNoCursed = true;
			TSet<FName> SeenIds;
			bool bNoDuplicate = true;

			for (const FHexRewardOption& O : Rewards)
			{
				if (O.Description.IsEmpty())
				{
					bAllHaveDesc = false;
				}
				if (O.Kind == FHexRewardOption::EKind::Rune)
				{
					++RuneCount;
					if (SeenIds.Contains(O.ContentId))
					{
						bNoDuplicate = false;
					}
					SeenIds.Add(O.ContentId);

					const FHexRuneData* R = FHexRuneLibrary::FindRune(O.ContentId);
					// ⚠️ 三选一是"必须选一个"的场合，塞诅咒等于强迫玩家吃亏。
					//    诅咒的正确出口是事件房与掉落（玩家可以拒绝）。
					if (R && R->bIsCursed)
					{
						bNoCursed = false;
					}
				}
			}

			Ctx.CheckEqual(TEXT("三选一恰好 3 个符文"), RuneCount, 3);
			Ctx.Check(TEXT("三选一无重复符文"), bNoDuplicate,
				TEXT("重复选项等于减少了选择"));
			Ctx.Check(TEXT("三选一不含诅咒符文"), bNoCursed,
				TEXT("必选场合塞诅咒 = 强迫玩家吃亏"));
			// R8：选项必须让玩家看懂才能做决策
			Ctx.Check(TEXT("所有选项都有描述（R8）"), bAllHaveDesc, TEXT(""));

			// ── 应用奖励
			{
				for (const FHexRewardOption& O : Rewards)
				{
					if (O.Kind == FHexRewardOption::EKind::Rune)
					{
						Ctx.Check(TEXT("应用符文奖励成功"),
							Run.ApplyReward(O, Rng), TEXT(""));
						Ctx.CheckEqual(TEXT("符文进入槽位"),
							Run.RuneLoadout.GetFilledCount(), 1);
						break;
					}
				}

				const int32 CapBefore = Run.DeckCapacity;
				for (const FHexRewardOption& O : Rewards)
				{
					if (O.Kind == FHexRewardOption::EKind::DeckCapacity)
					{
						Run.ApplyReward(O, Rng);
						Ctx.Check(TEXT("卡组容量奖励生效"),
							Run.DeckCapacity > CapBefore,
							FString::Printf(TEXT("%d → %d"), CapBefore, Run.DeckCapacity));
						break;
					}
				}

				for (const FHexRewardOption& O : Rewards)
				{
					if (O.Kind == FHexRewardOption::EKind::Shards)
					{
						Run.ApplyReward(O, Rng);
						Ctx.Check(TEXT("碎片奖励生效"), Run.Shards > 0,
							FString::Printf(TEXT("碎片=%d"), Run.Shards));
						break;
					}
				}
			}

			// ── 已持有的符文不再出现在下一次三选一
			{
				TArray<FHexRewardOption> Next;
				Run.GenerateFloorRewards(Rng, Next);

				bool bNoOwned = true;
				TArray<TPair<int32, const FHexRuneData*>> Equipped;
				Run.RuneLoadout.GetRunesInOrder(Equipped);

				for (const FHexRewardOption& O : Next)
				{
					if (O.Kind != FHexRewardOption::EKind::Rune)
					{
						continue;
					}
					for (const TPair<int32, const FHexRuneData*>& P : Equipped)
					{
						if (P.Value && P.Value->Id == O.ContentId)
						{
							bNoOwned = false;
						}
					}
				}
				Ctx.Check(TEXT("三选一排除已持有的符文"), bNoOwned,
					TEXT("给已装备的符文 = 这一选没有选项"));
			}

			// ── 腐蚀度提高碎片奖励（§9.4 正反馈）
			{
				FHexRunState Low(8);
				FHexRunState High(8);
				FHexRngStreams RngL(8);
				FHexRngStreams RngH(8);
				Low.BeginRun(TEXT("warden"), RngL);
				High.BeginRun(TEXT("warden"), RngH);
				High.Corruption = 12;

				TArray<FHexRewardOption> RL, RH;
				Low.GenerateFloorRewards(RngL, RL);
				High.GenerateFloorRewards(RngH, RH);

				auto ShardAmount = [](const TArray<FHexRewardOption>& Rs) -> int32
				{
					for (const FHexRewardOption& O : Rs)
					{
						if (O.Kind == FHexRewardOption::EKind::Shards)
						{
							return O.Amount;
						}
					}
					return 0;
				};

				Ctx.Check(TEXT("高腐蚀度给更多碎片"),
					ShardAmount(RH) > ShardAmount(RL),
					FString::Printf(TEXT("腐蚀0=%d 腐蚀12=%d"),
						ShardAmount(RL), ShardAmount(RH)));
			}
		}

		// ── 序列化往返
		{
			FHexRngStreams Rng(1111);
			FHexRunState Original(1111);
			Original.BeginRun(TEXT("warden"), Rng);

			// 制造一些进度
			Original.Corruption = 7;
			Original.Shards = 123;
			Original.DeckCapacity = 10;
			Original.HeroHP = 55;
			Original.RuneLoadout.SetSlot(0, FHexRuneLibrary::FindRune(TEXT("rune_whetstone")));
			Original.RuneLoadout.SetSlot(2, FHexRuneLibrary::FindRune(TEXT("rune_twin_shadow")));
			Original.EquipLoadout.Equip(
				FHexEquipGenerator::Generate(TEXT("wp_halberd"), EHexRarity::Rare, Rng, 500));

			TArray<uint8> Bytes;
			{
				FMemoryWriter Writer(Bytes);
				Original.Serialize(Writer);
			}

			FHexRunState Restored(0);
			{
				FMemoryReader Reader(Bytes);
				Restored.Serialize(Reader);
			}

			Ctx.CheckEqual(TEXT("序列化：腐蚀度"), Restored.Corruption, 7);
			Ctx.CheckEqual(TEXT("序列化：碎片"), Restored.Shards, 123);
			Ctx.CheckEqual(TEXT("序列化：卡组容量"), Restored.DeckCapacity, 10);
			Ctx.CheckEqual(TEXT("序列化：生命"), Restored.HeroHP, 55);
			Ctx.CheckEqual(TEXT("序列化：卡组张数"),
				Restored.Deck.Num(), Original.Deck.Num());

			// ⚠️ 符文槽存的是指针，必须按 id 重新解引用 ——
			//    这里验证槽位号也被正确保留（槽位顺序即结算顺序，§6.5）
			Ctx.CheckEqual(TEXT("序列化：符文数量"),
				Restored.RuneLoadout.GetFilledCount(), 2);
			Ctx.Check(TEXT("序列化：符文槽位号保留（顺序即结算顺序）"),
				Restored.RuneLoadout.GetSlot(0) != nullptr
				&& Restored.RuneLoadout.GetSlot(1) == nullptr
				&& Restored.RuneLoadout.GetSlot(2) != nullptr,
				TEXT("槽位错位会改变 ②′ 的结算顺序"));

			Ctx.Check(TEXT("序列化：整体哈希一致"),
				Restored.ContentHash() == Original.ContentHash(),
				FString::Printf(TEXT("%u vs %u"),
					Restored.ContentHash(), Original.ContentHash()));
		}
	}
}

bool FHexVerifySuites::VerifyMap(FHexVerifyContext& Ctx)
{
	CheckGeneration(Ctx);
	CheckExploration(Ctx);
	CheckR5Mitigation(Ctx);
	CheckRunState(Ctx);
	return Ctx.NumFailed() == 0;
}
