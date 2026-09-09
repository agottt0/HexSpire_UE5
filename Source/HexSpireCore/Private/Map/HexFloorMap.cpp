// Copyright Hex Spire. All Rights Reserved.

#include "Map/HexFloorMap.h"
#include "Rng/HexRngStreams.h"
#include "Content/HexLayouts.h"

// ══════════════════════════════════════════════════════════ 节点

void FHexRoomNode::Serialize(FArchive& Ar)
{
	Ar << Id;

	uint8 T = static_cast<uint8>(Type);
	uint8 V = static_cast<uint8>(Visibility);
	Ar << T;
	Ar << V;
	if (Ar.IsLoading())
	{
		Type = static_cast<EHexRoomType>(T);
		Visibility = static_cast<EHexRoomVisibility>(V);
	}

	Ar << Neighbors;
	Ar << EncounterId;
	Ar << LayoutId;
	Ar << Depth;
}

// ══════════════════════════════════════════════════════════ 生成

void FHexFloorMap::Link(int32 A, int32 B)
{
	if (A == B || !Rooms.IsValidIndex(A) || !Rooms.IsValidIndex(B))
	{
		return;
	}

	// 保持升序 + 去重 —— 邻居顺序参与确定性遍历（纪律 5）
	Rooms[A].Neighbors.AddUnique(B);
	Rooms[B].Neighbors.AddUnique(A);
	Rooms[A].Neighbors.Sort();
	Rooms[B].Neighbors.Sort();
}

void FHexFloorMap::Generate(int32 FloorIndex, FHexRngStreams& Rng)
{
	Rooms.Reset();
	EntranceId = -1;
	BossId = -1;
	CampId = -1;
	CurrentRoomId = -1;

	// ══════════════════════════════════════════════════════
	// 6 间房的固定构成（用户决策 q19）
	// ══════════════════════════════════════════════════════
	//   0 入口 / 1 普通 / 2 普通 / 3 精英 / 4 营地 / 5 Boss
	//
	// ⚠️ 房间【类型构成】固定而【拓扑结构】随机。
	//    为什么不让类型也随机：6 间房太少，随机构成会产出
	//    "3 间普通 + 0 营地"这种没有营地锚点的图，
	//    补偿②直接失效，R5 立刻发生。
	//    到第 2 层（房间更多）时才有随机构成的余量。

	auto AddRoom = [this](EHexRoomType Type, int32 Depth) -> int32
	{
		FHexRoomNode N;
		N.Id = Rooms.Num();
		N.Type = Type;
		N.Depth = Depth;
		N.Visibility = EHexRoomVisibility::Unknown;
		Rooms.Add(N);
		return N.Id;
	};

	EntranceId = AddRoom(EHexRoomType::Entrance, 0);
	const int32 Combat1 = AddRoom(EHexRoomType::Combat, 1);
	const int32 Combat2 = AddRoom(EHexRoomType::Combat, 1);
	const int32 Elite = AddRoom(EHexRoomType::Elite, 2);
	CampId = AddRoom(EHexRoomType::Camp, 2);
	BossId = AddRoom(EHexRoomType::Boss, 3);

	// ══════════════════════════════════════════════════════
	// 拓扑：菱形骨架 + 随机捷径
	// ══════════════════════════════════════════════════════
	//
	//        入口(0)
	//        /     \
	//   普通(1)   普通(2)
	//        \     /
	//     精英(3) 营地(4)
	//           |
	//        Boss(5)
	//
	// 骨架保证三条性质（VerifyMap 全部校验）：
	//   · 从入口可达每一间房（否则玩家会卡死）
	//   · Boss 必邻营地（补偿②）
	//   · 从入口到 Boss 至少 3 步（否则第一次点击就可能撞上 Boss，
	//     "探索"没有发生的空间）

	Link(EntranceId, Combat1);
	Link(EntranceId, Combat2);

	// 两条中路各自接向精英或营地，且【随机交叉】。
	// 这是玩家需要推理的部分：走左边还是右边通向营地？
	const bool bSwap = Rng.Chance(EHexRngStream::Map, 0.5f);
	if (bSwap)
	{
		Link(Combat1, CampId);
		Link(Combat2, Elite);
	}
	else
	{
		Link(Combat1, Elite);
		Link(Combat2, CampId);
	}

	// ⚠️ 补偿②的实现处：Boss 只连营地。
	//    这条边是硬约束，不参与随机 —— 它是玩家唯一可靠的推理支点。
	Link(CampId, BossId);

	// 随机捷径：50% 概率把精英也连到营地。
	// 作用是让"精英房要不要打"变成真选择 ——
	// 有捷径时可以绕过精英直达营地，没有时精英是通往营地的必经之路之一。
	if (Rng.Chance(EHexRngStream::Map, 0.5f))
	{
		Link(Elite, CampId);
	}

	// ══════════════════════════════════════════════════════
	// 战斗房配怪物组与地形
	// ══════════════════════════════════════════════════════
	//
	// ⚠️ 怪物组按"教学 → 标准 → 精英 → Boss"四档递进，
	//    与 HexContentLibrary 的 enc_01..enc_04 一一对应。
	//    第一间普通房必须是 enc_01（教学局），否则玩家开局就撞上
	//    5 只怪的标准局 —— Godot 实测过这个问题。

	// 地形模板池：不含 bottleneck。
	// ⚠️ 1 格门配追踪型远程敌人 = 玩家毫无应对手段（Godot 实测 15 场全败）。
	//    第一版英雄是 S 体型能过 1 格门，但敌人体型混杂，
	//    先整体排除，等 LayoutValidator 的配对校验做完再放回来。
	const TArray<FName> SafeLayouts = {
		TEXT("open_hall"), TEXT("narrow_pass"), TEXT("pillar_hall"), TEXT("spike_cell")
	};

	auto PickLayout = [&Rng, &SafeLayouts]() -> FName
	{
		const int32 I = Rng.RandRange(EHexRngStream::Map, 0, SafeLayouts.Num() - 1);
		return SafeLayouts[I];
	};

	Rooms[Combat1].EncounterId = TEXT("enc_01");
	Rooms[Combat1].LayoutId = PickLayout();

	Rooms[Combat2].EncounterId = TEXT("enc_02");
	Rooms[Combat2].LayoutId = PickLayout();

	Rooms[Elite].EncounterId = TEXT("enc_03");
	Rooms[Elite].LayoutId = PickLayout();

	// ⚠️ Boss 房固定用 open_hall。
	//    攻城虫是 L 体型（占 6 格），狭道/石柱地形会把它卡住 ——
	//    Boss 被地形困死不是"战术胜利"，是 bug 观感。
	Rooms[BossId].EncounterId = TEXT("enc_04");
	Rooms[BossId].LayoutId = TEXT("open_hall");

	// ══════════════════════════════════════════════════════
	// 初始可见性
	// ══════════════════════════════════════════════════════
	//
	// 入口已揭示（玩家站在这里），其邻居为 Known（看得见有门，不知门后是什么）。
	// 其余全部 Unknown —— 这就是"盲探"。
	CurrentRoomId = EntranceId;
	RevealRoomType(EntranceId);
	Rooms[EntranceId].Visibility = EHexRoomVisibility::Cleared;  // 入口无需战斗
	RevealNeighborTopology(EntranceId);

	// FloorIndex 目前只影响敌人缩放（见 MakeEnemyUnit），不改变拓扑。
	// 第 2 层起会增加房间数，届时在此分支。
	(void)FloorIndex;
}

// ══════════════════════════════════════════════════════════ 查询

const FHexRoomNode* FHexFloorMap::FindRoom(int32 RoomId) const
{
	return Rooms.IsValidIndex(RoomId) ? &Rooms[RoomId] : nullptr;
}

FHexRoomNode* FHexFloorMap::FindRoomMutable(int32 RoomId)
{
	return Rooms.IsValidIndex(RoomId) ? &Rooms[RoomId] : nullptr;
}

void FHexFloorMap::GetAccessibleRooms(TArray<int32>& Out) const
{
	Out.Reset();

	const FHexRoomNode* Cur = FindRoom(CurrentRoomId);
	if (!Cur)
	{
		return;
	}

	// ⚠️ 【允许】走回已清空的房间。
	//
	//    这里曾经过滤掉 Cleared，想法是"防止玩家来回刷已清空的房间赚腐蚀度"。
	//    那个做法有两个错误：
	//
	//    ① 它不必要：腐蚀度的防重复由 ClearCurrentRoom 的幂等性保证
	//       （已 Cleared 的房间再清一次返回 0）。用封路来防刷是多余的。
	//
	//    ② 它会让玩家【卡死】。菱形骨架里精英房可能只连一个普通房
	//       （随机捷径未生成时），玩家走进精英房、清空后就无路可走 ——
	//       整局直接卡住。VerifyMap 的 200 种子穷举在 seed=1 抓到了这个。
	//
	//    正确的心智模型是：打完的房间当然可以路过，只是不再有收益。
	for (const int32 Nid : Cur->Neighbors)
	{
		if (FindRoom(Nid))
		{
			Out.Add(Nid);
		}
	}

	// 邻居已按升序维护，这里无需再排序
}

bool FHexFloorMap::AreAllNonBossRoomsCleared() const
{
	for (const FHexRoomNode& R : Rooms)
	{
		if (R.Type == EHexRoomType::Boss)
		{
			continue;
		}
		if (R.Visibility != EHexRoomVisibility::Cleared)
		{
			return false;
		}
	}
	return true;
}

int32 FHexFloorMap::GetClearedCount() const
{
	int32 N = 0;
	for (const FHexRoomNode& R : Rooms)
	{
		if (R.Visibility == EHexRoomVisibility::Cleared)
		{
			++N;
		}
	}
	return N;
}

// ══════════════════════════════════════════════════════════ 推进

void FHexFloorMap::RevealRoomType(int32 RoomId)
{
	FHexRoomNode* R = FindRoomMutable(RoomId);
	if (!R)
	{
		return;
	}

	// ⚠️ 单向状态机：Cleared 不得退回 Revealed。
	//    否则"已清空的房间"会重新可进入，腐蚀度可以被反复刷取。
	if (R->Visibility == EHexRoomVisibility::Cleared)
	{
		return;
	}
	R->Visibility = EHexRoomVisibility::Revealed;
}

void FHexFloorMap::RevealNeighborTopology(int32 RoomId)
{
	// ══ 补偿④（拓扑揭示）的实现处 ══
	//
	// 探明一间房后，它的全部邻居变为 Known ——
	// 玩家知道"那边有房间"，但不知道是什么房。
	//
	// ⚠️ 只提升 Unknown → Known，绝不覆盖更高的可见性。
	//    若写成无条件赋值，会把已揭示的房间"退回"未明状态，
	//    玩家的信息量会时增时减 —— 那比完全没有补偿更糟糕
	//    （VerifyMap 有一条断言专门盯这个：信息必须单调增长）。
	const FHexRoomNode* R = FindRoom(RoomId);
	if (!R)
	{
		return;
	}

	for (const int32 Nid : R->Neighbors)
	{
		FHexRoomNode* N = FindRoomMutable(Nid);
		if (N && N->Visibility == EHexRoomVisibility::Unknown)
		{
			N->Visibility = EHexRoomVisibility::Known;
		}
	}
}

bool FHexFloorMap::EnterRoom(int32 RoomId)
{
	TArray<int32> Accessible;
	GetAccessibleRooms(Accessible);
	if (!Accessible.Contains(RoomId))
	{
		return false;
	}

	CurrentRoomId = RoomId;

	// 进入即知道它是什么房（这是"探"的结果）
	RevealRoomType(RoomId);

	// 补偿④：同时展开这间房的邻边拓扑
	RevealNeighborTopology(RoomId);

	return true;
}

int32 FHexFloorMap::ClearCurrentRoom()
{
	FHexRoomNode* R = FindRoomMutable(CurrentRoomId);
	if (!R)
	{
		return 0;
	}

	// 重复调用不得重复计腐蚀度
	if (R->Visibility == EHexRoomVisibility::Cleared)
	{
		return 0;
	}

	R->Visibility = EHexRoomVisibility::Cleared;

	// ⚠️ 腐蚀度是 §9.4 的加压机制，也是 D4 风险收益的一半：
	//    多探一间房 → 腐蚀度上升 → 后续敌人更强，但掉落更好
	//    （见 FHexEquipGenerator::RollRarity）。
	//    Boss 房与营地不加腐蚀度：Boss 是终点，营地是补给。
	switch (R->Type)
	{
	case EHexRoomType::Combat:
		return HexK::CorruptionPerRoom;
	case EHexRoomType::Elite:
		return HexK::CorruptionPerEliteRoom;
	default:
		return 0;
	}
}

// ══════════════════════════════════════════════════════════ D4 信息补偿

bool FHexFloorMap::IsBossAdjacentToCamp() const
{
	const FHexRoomNode* Boss = FindRoom(BossId);
	if (!Boss)
	{
		return false;
	}
	return Boss->Neighbors.Contains(CampId);
}

void FHexFloorMap::GetBossCandidates(TArray<int32>& Out) const
{
	Out.Reset();

	const FHexRoomNode* Camp = FindRoom(CampId);
	if (!Camp)
	{
		return;
	}

	// ⚠️ 线索只在【营地已被玩家看到】之后才生效。
	//    若营地还是 Unknown，玩家根本不知道营地在哪，
	//    这条补偿此时提供 0 信息 —— 这是刻意的：
	//    补偿②是"找到营地"的奖励，不是开局白送的提示。
	if (Camp->Visibility == EHexRoomVisibility::Unknown)
	{
		return;
	}

	for (const int32 Nid : Camp->Neighbors)
	{
		const FHexRoomNode* N = FindRoom(Nid);
		if (!N)
		{
			continue;
		}
		// 已清空的不可能是未打的 Boss
		if (N->Visibility == EHexRoomVisibility::Cleared)
		{
			continue;
		}
		Out.Add(Nid);
	}
}

float FHexFloorMap::GetKnownRatio() const
{
	if (Rooms.Num() == 0)
	{
		return 0.0f;
	}

	int32 KnownTypes = 0;
	for (const FHexRoomNode& R : Rooms)
	{
		// Revealed 与 Cleared 都算"类型已知"
		if (R.Visibility == EHexRoomVisibility::Revealed
			|| R.Visibility == EHexRoomVisibility::Cleared)
		{
			++KnownTypes;
		}
	}

	return static_cast<float>(KnownTypes) / static_cast<float>(Rooms.Num());
}

// ══════════════════════════════════════════════════════════ 序列化

void FHexFloorMap::Serialize(FArchive& Ar)
{
	int32 Count = Rooms.Num();
	Ar << Count;

	if (Ar.IsLoading())
	{
		Rooms.SetNum(Count);
	}
	for (int32 I = 0; I < Count; ++I)
	{
		Rooms[I].Serialize(Ar);
	}

	Ar << EntranceId;
	Ar << BossId;
	Ar << CampId;
	Ar << CurrentRoomId;
}

uint32 FHexFloorMap::ContentHash() const
{
	uint32 H = 0x4D415021u;  // "MAP!"

	for (const FHexRoomNode& R : Rooms)
	{
		H = HashCombine(H, GetTypeHash(R.Id));
		H = HashCombine(H, GetTypeHash(static_cast<uint8>(R.Type)));
		H = HashCombine(H, GetTypeHash(static_cast<uint8>(R.Visibility)));
		H = HashCombine(H, GetTypeHash(R.EncounterId));
		H = HashCombine(H, GetTypeHash(R.LayoutId));
		for (const int32 Nid : R.Neighbors)
		{
			H = HashCombine(H, GetTypeHash(Nid));
		}
	}

	H = HashCombine(H, GetTypeHash(CurrentRoomId));
	return H;
}
