// Copyright Hex Spire. All Rights Reserved.
//
// 盲探地图（D4）—— 策划案 §9
//
// ══════════════════════════════════════════════════════════════════
// D4 是什么，以及它为什么危险
// ══════════════════════════════════════════════════════════════════
// D4 的决策内容：【Boss 房不可见】。
// 玩家在层内探索时，看不到 Boss 在哪，只能一间间探出来。
//
// 与《杀戮尖塔》的对比（D2 沿用了它的三堆牌循环，但地图是相反的）：
//   StS 的地图【完全可见】——玩家开局就能规划整条路线，
//   "走精英拿遗物还是走商店省血"是一个【信息完整】的决策。
//   D4 把信息拿走了，于是决策变成"要不要多探一间"。
//
// ⚠️ R5 是这个决策最大的风险，且它【一定会发生】除非专门对抗：
//   信息不足的探索会退化为【随机点击】。
//   玩家发现"反正看不出来哪间是 Boss，那我随便点"，
//   于是 D4 不但没有增加深度，反而把 StS 原有的路线规划深度也删掉了。
//
// 策划案 §9.3.2 给了六条信息补偿来对抗 R5。
// 用户决策 q20 选了其中两条（另外四条需要美术/音频资源）：
//   ② 营地锚点：Boss 房【必然】与营地相邻 —— 找到营地就锁定了 Boss 的邻域
//   ④ 拓扑揭示：探明一间房后，揭示它的全部邻边 —— 地图结构逐步展开
//
// 这两条为什么够用（第一版）：
//   6 间房的图里，②把"Boss 在哪"从 6 选 1 压到"营地的邻居"（1–3 选 1），
//   ④让玩家能推理出图的形状，从而判断"哪条路能绕开 Boss 去营地"。
//   两条叠加后，玩家的探索【有推理对象】——这正是 R5 的解药。
//
// ⚠️ 未实现的四条补偿（策划案 §9.3.2 的①③⑤⑥）：
//   ① 声音线索（Boss 房相邻时播放低频轰鸣）—— 需音频
//   ③ 环境提示（Boss 房相邻的房间有视觉痕迹）—— 需美术
//   ⑤ 侦查道具（消耗品，揭示一间房的类型）—— 需道具系统
//   ⑥ 地图残片（掉落物，揭示随机一间房）—— 需掉落表扩展
//   这四条都是"给更多线索"，不改变本文件的结构。加回来时只需
//   往 FHexRoomNode 上加字段 + 在 Reveal 时填充。

#pragma once

#include "CoreMinimal.h"
#include "Core/HexSpireEnums.h"
#include "Core/HexSpireConstants.h"

class FHexRngStreams;

/** 房间的可见性状态（D4 的核心状态机） */
enum class EHexRoomVisibility : uint8
{
	/** 完全未知：玩家不知道这里有没有房间 */
	Unknown = 0,
	/**
	 * 已知存在但类型未明。
	 * ⚠️ 这是 D4 得以成立的关键状态 —— 玩家知道"那边有一间房"，
	 *    但不知道是普通战斗还是 Boss。这个"知道位置不知道内容"的
	 *    中间态就是补偿④（拓扑揭示）的产物。
	 */
	Known = 1,
	/** 类型已揭示（玩家已看到它是什么房） */
	Revealed = 2,
	/** 已清空（战斗打完 / 营地用过） */
	Cleared = 3,
};

/**
 * 一个房间节点。
 *
 * ⚠️ 房间用【图】而非【网格】组织。
 *    网格（StS 的做法）会让"哪些房间相邻"由坐标决定，
 *    而 D4 的补偿②要求"Boss 必邻营地"这种拓扑约束 ——
 *    图结构能直接表达并校验这类约束，网格不能。
 */
struct HEXSPIRECORE_API FHexRoomNode
{
	int32 Id = -1;

	EHexRoomType Type = EHexRoomType::Combat;
	EHexRoomVisibility Visibility = EHexRoomVisibility::Unknown;

	/** 相邻房间 id，按升序（确定性遍历） */
	TArray<int32> Neighbors;

	/** 战斗房的怪物组 id；非战斗房为空 */
	FName EncounterId;

	/** 战场地形模板 id */
	FName LayoutId;

	/**
	 * 布局用的层级深度（入口=0，Boss 最深）。
	 * 仅供 UI 排版，【不参与任何逻辑判断】——
	 * 若逻辑依赖它，"Boss 必然在最深层"就成了免费线索，破坏 D4。
	 */
	int32 Depth = 0;

	bool IsCombat() const
	{
		return Type == EHexRoomType::Combat
			|| Type == EHexRoomType::Elite
			|| Type == EHexRoomType::Boss;
	}

	void Serialize(FArchive& Ar);
};

/**
 * 一层的房间图。
 *
 * 第一版 6 间房（用户决策 q19）：入口 + 2 普通 + 精英 + 营地 + Boss。
 * ⚠️ 这违反策划案 §9.1「第 1 层 3 间房」，是刻意的：
 *    用户要求"一层完整循环"，3 间房演示不完盲探
 *    （2 间探完就只剩 1 间，Boss 位置直接暴露，D4 形同不存在）。
 */
class HEXSPIRECORE_API FHexFloorMap
{
public:
	/**
	 * 生成一层。
	 *
	 * ⚠️ 必须走 EHexRngStream::Map 子流（架构纪律 1）。
	 *    用战斗流或掉落流会让"这一层长什么样"被玩家的操作影响，
	 *    种子分享与回放全部失效。
	 *
	 * @param FloorIndex 层数（1 起）
	 */
	void Generate(int32 FloorIndex, FHexRngStreams& Rng);

	// ───────────────────────────────────────────── 查询

	const TArray<FHexRoomNode>& GetRooms() const { return Rooms; }

	const FHexRoomNode* FindRoom(int32 RoomId) const;
	FHexRoomNode* FindRoomMutable(int32 RoomId);

	int32 GetEntranceId() const { return EntranceId; }
	int32 GetBossId() const { return BossId; }
	int32 GetCampId() const { return CampId; }

	/** 玩家当前所在房间 */
	int32 GetCurrentRoomId() const { return CurrentRoomId; }

	/**
	 * 玩家【现在能去】的房间 id。
	 *
	 * 规则：当前房间的邻居中，可见性不是 Cleared 的那些。
	 * ⚠️ 刻意允许进入 Known（类型未明）的房间 ——
	 *    那正是 D4 的赌注：你不知道推开的门后面是什么。
	 */
	void GetAccessibleRooms(TArray<int32>& Out) const;

	/** 是否已清空全部非 Boss 房（用于"要不要直接打 Boss"的提示） */
	bool AreAllNonBossRoomsCleared() const;

	int32 GetClearedCount() const;

	// ───────────────────────────────────────────── 推进

	/**
	 * 进入一间房。会应用补偿④（拓扑揭示）。
	 * @return 是否成功（房间不可达时失败）
	 */
	bool EnterRoom(int32 RoomId);

	/**
	 * 标记当前房间已清空。
	 * @return 该房间产生的腐蚀度增量（普通 +1 / 精英 +2 / 其它 0）
	 */
	int32 ClearCurrentRoom();

	// ───────────────────────────────────────────── D4 信息补偿

	/**
	 * 补偿②：Boss 房是否与营地相邻。
	 * 这是生成期的【不变量】，由 VerifyMap 校验。玩家找到营地即锁定 Boss 邻域。
	 */
	bool IsBossAdjacentToCamp() const;

	/**
	 * 玩家【已知】的 Boss 候选房间。
	 *
	 * 这是补偿②真正的产出：一旦营地被揭示，Boss 必在它的邻居里。
	 * @return 候选房间 id；若营地尚未揭示则为空（玩家还没拿到这条线索）
	 */
	void GetBossCandidates(TArray<int32>& Out) const;

	/**
	 * 玩家视角的"信息量"：已知类型的房间数 / 总房间数。
	 * 供 VerifyMap 校验探索过程中信息是【单调增长】的 ——
	 * 若某步之后信息反而变少，说明揭示逻辑有 bug。
	 */
	float GetKnownRatio() const;

	// ───────────────────────────────────────────── 序列化

	void Serialize(FArchive& Ar);

	uint32 ContentHash() const;

private:
	/** 建立双向邻接（保持升序） */
	void Link(int32 A, int32 B);

	/** 补偿④：揭示某房间的全部邻居为 Known（不揭示类型） */
	void RevealNeighborTopology(int32 RoomId);

	/** 揭示一间房的类型 */
	void RevealRoomType(int32 RoomId);

	TArray<FHexRoomNode> Rooms;

	int32 EntranceId = -1;
	int32 BossId = -1;
	int32 CampId = -1;
	int32 CurrentRoomId = -1;
};
