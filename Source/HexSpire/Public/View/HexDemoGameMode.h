// Copyright Hex Spire. All Rights Reserved.
//
// Demo 的游戏模式 —— 表现层与逻辑层的唯一桥梁
//
// ══════════════════════════════════════════════════════════════════
// 它持有什么
// ══════════════════════════════════════════════════════════════════
//   FHexRunState  —— 局内进度（卡组、符文、装备、腐蚀度、地图）
//   FHexBattleState + FHexBattleFlow —— 当前这场战斗
//
// ⚠️ 纪律 3：逻辑【瞬时算完】，表现层按事件回放。
//    所以这里的每个 API 都是"调用 → 逻辑立刻算完 → 刷新可视化"，
//    不存在"等动画播完再继续"的逻辑分支。
//    这让整场战斗可以在无渲染下跑完（全部验证器成立的前提）。
//
// ⚠️ 相机完全锁死（美术文档 §5.3 明确禁止战斗中旋转）。
//    这不只是美术要求 —— 2D 背景板 + 3D 棋盘的方案要求
//    透视关系一次性标定后不能变。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Core/HexSpireEnums.h"

// ⚠️ 这四个必须是完整类型而非前置声明：
//    TUniquePtr 的析构函数需要看到完整定义才能生成 delete，
//    否则 MSVC 报 C4150「删除指向不完整类型的指针；没有调用析构函数」——
//    那会导致【析构不执行】，是静默的内存泄漏。
#include "Run/HexRunState.h"
#include "Battle/HexBattleState.h"
#include "Battle/HexBattleFlow.h"
#include "Rng/HexRngStreams.h"

#include "HexDemoGameMode.generated.h"

class AHexBoardVisual;
class AHexUnitVisual;
struct FHexCardData;

/** 房间选择项（给 HUD 显示） */
USTRUCT()
struct FHexRoomChoice
{
	GENERATED_BODY()

	int32 RoomId = -1;
	FString Label;
	/** 类型是否已知（D4：Known 状态下玩家不知道是什么房） */
	bool bTypeKnown = false;
};

UCLASS()
class HEXSPIRE_API AHexDemoGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AHexDemoGameMode();
	virtual ~AHexDemoGameMode() override;

	virtual void StartPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// ═══════════════════════════════════════════ 局流程

	/** 开新的一局（可指定种子；0 = 随机） */
	void StartNewRun(uint64 Seed = 0);

	/** 进入一间房 */
	bool EnterRoom(int32 RoomId);

	/** 当前是否在战斗中 */
	bool IsInBattle() const { return bInBattle; }

	/** 战斗是否已结束（胜/负/拾取阶段） */
	bool IsBattleOver() const;

	bool IsPlayerDefeated() const;

	/** 战斗胜利后：结算并回到地图 */
	void FinishBattleAndReturnToMap();

	/** 营地休息 */
	void RestAtCamp();

	// ═══════════════════════════════════════════ 战斗操作

	/** 打出手牌。TargetCell 对 SELF 类卡可任意。 */
	bool PlayCard(int32 CardUid, const FIntVector& TargetCell);

	void EndTurn();

	/** 选中一张手牌（用于高亮合法目标） */
	void SelectCard(int32 CardUid);

	void ClearSelection();

	int32 GetSelectedCardUid() const { return SelectedCardUid; }

	/** 鼠标悬停的格 */
	void SetHoverCell(const FIntVector& Cell);

	// ═══════════════════════════════════════════ 查询（HUD 用）

	const FHexRunState* GetRunState() const { return RunState.Get(); }
	const FHexBattleState* GetBattleState() const { return BattleState.Get(); }
	FHexBattleFlow* GetBattleFlow() const { return BattleFlow.Get(); }

	/** 当前可去的房间 */
	void GetRoomChoices(TArray<FHexRoomChoice>& Out) const;

	/** 当前选中卡的合法目标 */
	const TArray<FIntVector>& GetLegalTargets() const { return CachedLegalTargets; }

	/** 上一次操作的提示信息（HUD 顶部显示） */
	const FString& GetStatusMessage() const { return StatusMessage; }

	void SetStatusMessage(const FString& Msg) { StatusMessage = Msg; }

	AHexBoardVisual* GetBoard() const { return Board; }

private:
	/** 按当前逻辑状态重建全部可视化 */
	void RefreshVisuals();

	/** 重算高亮（选中卡的目标、敌人意图、自身 footprint） */
	void RefreshHighlights();

	/** 战斗开始：布阵、洗牌、生成意图 */
	void BeginBattleForRoom(int32 RoomId);

	/** 销毁全部单位 Actor */
	void ClearUnitVisuals();

	/** 摆好锁死的相机 */
	void SetupCamera();

	UPROPERTY()
	AHexBoardVisual* Board = nullptr;

	UPROPERTY()
	TMap<int32, AHexUnitVisual*> UnitVisuals;

	// ── 逻辑层（TUniquePtr：core 是纯 C++ 类，不参与 UObject GC）
	TUniquePtr<FHexRngStreams> Rng;
	TUniquePtr<FHexRunState> RunState;
	TUniquePtr<FHexBattleState> BattleState;
	TUniquePtr<FHexBattleFlow> BattleFlow;

	bool bInBattle = false;

	int32 SelectedCardUid = 0;
	FIntVector HoverCell = FIntVector::ZeroValue;
	bool bHasHover = false;

	TArray<FIntVector> CachedLegalTargets;
	FString StatusMessage;

	/** 卡实例 uid 分配器（战斗内注入的衍生卡用） */
	int32 NextRuntimeUid = 100000;
};
