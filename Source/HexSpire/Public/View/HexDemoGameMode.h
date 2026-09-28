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

// ⚠️ 这两个必须【完整包含】而不能前置声明。
//    TUniquePtr<T> 的析构要求 T 是完整类型，而本类的析构函数是
//    编译器隐式生成的 —— 生成点就在这个头文件里。
//    只前置声明会得到 C4150「删除指向不完整类型的指针」，
//    在 /WX 下直接编译失败（而且报错位置在 UniquePtr.h 里，
//    看起来像引擎的问题，实际原因在这里）。
#include "View/HexVisualQueue.h"
#include "Fx/HexFxRuntime.h"

#include "HexDemoGameMode.generated.h"

class AHexBoardVisual;
class UHexFxLibraryAsset;
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

	/** 当前房间类型（HUD 的胜负横幅要区分 Boss 击破与普通胜利） */
	EHexRoomType GetCurrentRoomType() const { return CurrentRoomType; }

	/**
	 * 某符文槽触发闪烁的当前强度 0..1（1 = 刚触发，随时间衰减到 0）。
	 * 供符文卡做脉冲动画 —— 符文生效必须有可见反馈，
	 * 否则玩家无法把"效果发生了"与"哪个符文干的"对上（R8）。
	 */
	float GetRuneFlashStrength(int32 SlotIndex) const;

	/** 战斗胜利后：结算并回到地图 */
	void FinishBattleAndReturnToMap();

	// ═══════════════════════════════════════════ 层结算（§6.6 / §9.8）

	/**
	 * 是否正在等待玩家选择层结算奖励。
	 *
	 * ⚠️ 这个阶段必须【阻塞进入下一间房】：
	 *    否则玩家会直接点下一间房，奖励静默消失 ——
	 *    而这是获得符文的唯一途径。
	 */
	bool IsAwaitingRewardChoice() const { return bAwaitingRewardChoice; }

	/** 当前待选的奖励项 */
	const TArray<FHexRewardOption>& GetPendingRewards() const { return PendingRewards; }

	/**
	 * 选择一项奖励并应用。
	 * @param Index 奖励下标；越界视为"全部放弃"
	 */
	void ChooseReward(int32 Index);

	/** 放弃全部奖励（§6.6 允许） */
	void DeclineRewards();

	/** 营地休息 */
	void RestAtCamp();

	/**
	 * 交换两个符文槽位（§6.5 顺序即策略）。
	 *
	 * ⚠️ 表现层只发输入：锁定判定（战斗中禁止重排）在
	 *    FHexRunState::ReorderRune 里，这里不重复判 ——
	 *    两边各写一套判断迟早漂移（同 CanEndTurn 的教训）。
	 *
	 * @return 是否成功（战斗中 / 越界返回 false）
	 */
	bool RequestRuneReorder(int32 SlotA, int32 SlotB);

	/**
	 * 调试：直接发一个符文（跳过层结算）。
	 *
	 * ⚠️ 只给控制台命令用（HexRune，见 PlayerController）。
	 *    正式获取路径是层 Boss 的三选一 —— 这个后门存在的理由是
	 *    "验证符文 UI / 触发链不该要求先打通一整层"。
	 *
	 * @param RuneIdStr 符文 id；空串 = 自动挑第一个未持有的
	 */
	bool DebugGrantRune(const FString& RuneIdStr);

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

	/**
	 * 让英雄播出手动作。
	 *
	 * ⚠️ 受击动画不在这里 —— 那个由表现层从掉血差分自行触发，
	 *    逻辑层不需要知道表现层的存在（纪律 3）。
	 *    出手则必须显式通知：打空/纯 buff 卡时状态无变化，推不出来。
	 */
	void PlayHeroCardAnim(EHexCardType Type);

	/** 摆好锁死的相机 */
	void SetupCamera();

	UPROPERTY()
	AHexBoardVisual* Board = nullptr;

	UPROPERTY()
	TMap<int32, AHexUnitVisual*> UnitVisuals;

	// ── 表现回放（纯 C++，同样不参与 GC）
	//
	// ⚠️ 特效库资产本身要 UPROPERTY 持有（见下），
	//    否则 GC 会在战斗中途回收它 —— 表现为"打了几回合特效突然没了"。
	TUniquePtr<FHexFxRuntime> FxRuntime;
	TUniquePtr<FHexVisualQueue> VisualQueue;

	/** 持有特效库的强引用，防 GC */
	UPROPERTY()
	const UHexFxLibraryAsset* FxLibraryKeepAlive = nullptr;

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

	// ── 层结算
	/**
	 * 当前所在房间的类型。
	 * ⚠️ 必须在战斗【开始时】记下来：FinishBattleAndReturnToMap 里
	 *    房间已被标记为 Cleared，那时再查类型拿到的是清空后的状态，
	 *    判不出"刚打的是不是 Boss"。
	 */
	EHexRoomType CurrentRoomType = EHexRoomType::Combat;

	TArray<FHexRewardOption> PendingRewards;
	bool bAwaitingRewardChoice = false;

	/**
	 * Boss 已倒，奖励处理完（选定或放弃）后进下一层。
	 *
	 * ⚠️ 这个标志曾【不存在】：打赢 Boss、选完奖励后没有任何代码
	 *    推进层数 —— 玩家永远卡在打完的第一层，符文获取链在
	 *    "第二次层结算"处断掉。层推进挂在奖励处理完之后而不是
	 *    Boss 倒下瞬间，是因为 GenerateFloorRewards 读的是
	 *    【当前层】的腐蚀度与持有列表，先换层再选奖励会算错。
	 */
	bool bAdvanceFloorAfterRewards = false;

	/** 层推进的执行处（重置地图、层统计；腐蚀度刻意跨层继承） */
	void AdvanceToNextFloor();

	/**
	 * 符文触发的闪烁时间戳（World 秒），下标 = 槽位 0..5。
	 * 在 Tick 的事件 Drain 处记录（rune_triggered 事件的 IntB = 槽位号）。
	 */
	float RuneFlashTime[6] = {};

	/**
	 * 战斗分出胜负时更新状态提示。
	 *
	 * ⚠️ 必须在【每个】能改变战场的入口末尾调用（PlayCard / EndTurn）。
	 *    原先只有 EndTurn 检查 —— 而最常见的胜利方式恰恰是
	 *    打出卡牌当场杀掉最后一个敌人：那条路径上什么都不显示，
	 *    玩家清完场毫无反馈，以为游戏卡住了。
	 */
	void AnnounceBattleOutcomeIfOver();
};
