// Copyright Hex Spire. All Rights Reserved.
//
// GameAction —— 架构纪律 2：所有游戏状态变更必须经过 ActionQueue
//
// ══ 为什么用「扁平数据 + 处理器注册表」而不是子类多态 ══
//
// 代价：字段没有静态类型 → 用 HexActions 的命名构造器补偿。
// 收益：Serialize 两行 → 动作日志天然可持久化，于是
//       战报、Undo、bug 复现、（未来的）联机广播全部免费。
//
// ⚠️ 禁止在任何地方写 Unit.HP -= X。必须 Queue.PushBack(Actions::Damage(...))。
//    由 CheckDiscipline 扫描强制。
//
// ══ PushBack vs PushNext ══
// PushNext 是必须的：符文触发的子动作要紧随触发者结算，
// 否则 §6.5 的"槽位 1→6 顺序"会被打乱 —— 槽 1 的子动作会跑到槽 6 的主动作后面。

#pragma once

#include "CoreMinimal.h"
#include "Core/HexSpireEnums.h"
#include "Core/HexSpireConstants.h"

class FHexBattleState;
struct FHexBattleEvent;
struct FHexGameAction;
class FHexActionQueue;

/**
 * 结算观察者：每个动作执行完毕后被调用一次。
 *
 * ══════════════════════════════════════════════════════════════════
 * 为什么触发时机要走这里，而不是在业务代码里一处处手写 Emit
 * ══════════════════════════════════════════════════════════════════
 * 手写埋点的失败方式是【静默】的：漏一个时机，挂在它上面的符文
 * 就永远不响应 —— 不报错、不崩溃，只显得"这个符文很弱"。
 *
 * 这件事已经真实发生过：22 个时机里 8 个没有埋点，
 * 《食魂》(OnKill)、《焚心》(OnCrit)、《轮回护符》(OnDeckReshuffled)
 * 三个符文装上去完全没有效果，而 1049 项断言全部通过。
 *
 * 动作是状态变更的唯一通道（纪律 2），所以"动作执行完"是唯一
 * 不会漏的埋点位置。集中到一处之后，新增动作类型最坏情况是
 * "翻译表里少一条"，而那是能被埋点覆盖率断言抓住的。
 *
 * @param Action    刚执行完的动作
 * @param NewEvents 本动作产生的事件。block_broken / unit_died 这类
 *                  只有 Resolver 内部能判断的事实全在这里。
 * @param State     当前状态（只读用途；要改状态必须再推动作）
 * @param Queue     可用 PushNext 追加子动作，保持槽位顺序语义
 */
using FHexActionObserver = TFunction<void(
	const FHexGameAction& Action,
	TArrayView<const FHexBattleEvent> NewEvents,
	FHexBattleState& State,
	FHexActionQueue& Queue)>;

/**
 * 动作类型。
 * ⚠️ 每加一种必须同步在 HexActionResolver::RegisterHandlers 里注册处理器，
 *    否则会记 no_handler 违规（而不是静默失败）。
 *
 * 刻意【不】标 UENUM：动作是 core 内部机制，表现层通过事件日志读取结果，
 * 不直接消费 action。少一个 generated.h 依赖，core 更干净。
 */
enum class EHexActionType : uint8
{
	None = 0,

	// ── 资源
	SpendEnergy,
	GainEnergy,
	SetEnergy,

	// ── 伤害与生命
	Damage,
	Heal,
	GainBlock,
	ClearBlock,
	Kill,

	// ── 位移
	MoveUnit,
	RotateUnit,
	Knockback,
	Trample,

	// ── 状态
	ApplyStatus,
	RemoveStatus,
	TickStatus,

	// ── 牌堆
	DrawCards,
	DiscardCard,
	ExhaustCard,
	ReshuffleDeck,

	// ── 地形
	ModifyTerrain,
	TriggerHazard,

	// ── 流程
	SetPhase,
	AdvanceRound,
	SpawnUnit,

	Count
};

/**
 * 一个动作 = 类型 + 扁平数据。
 *
 * 字段命名刻意通用（IntA/IntB…），由命名构造器赋予语义。
 * 这样 Serialize 是固定的两行，不随动作种类增长。
 */
struct HEXSPIRECORE_API FHexGameAction
{
	EHexActionType Type = EHexActionType::None;

	/** 常用槽位 */
	int32 SourceUnitId = -1;
	int32 TargetUnitId = -1;
	int32 IntA = 0;
	int32 IntB = 0;
	int32 IntC = 0;
	float FloatA = 0.0f;
	FName NameA;
	FIntVector CoordA = FIntVector::ZeroValue;
	FIntVector CoordB = FIntVector::ZeroValue;
	bool bFlagA = false;
	bool bFlagB = false;

	/** 来源标记（"rune:slot3" / "card:heavy_strike" / "hazard"），写进日志 */
	FString SourceTag;

	void Serialize(FArchive& Ar);

	FString ToDebugString() const;
};

/** 命名构造器 —— 补偿扁平结构缺失的静态类型 */
struct HEXSPIRECORE_API FHexActions
{
	static FHexGameAction SpendEnergy(int32 Amount);
	static FHexGameAction GainEnergy(int32 Amount);
	static FHexGameAction SetEnergy(int32 Value);

	/**
	 * 伤害动作。伤害值已由 DamageCalculator 算完并冻结在此。
	 * ⚠️ 这是有意的：Resolver 不重算伤害，只应用结果。
	 *    否则同一次攻击在 preview 与 resolve 时可能得到不同值。
	 */
	static FHexGameAction Damage(
		int32 SourceId, int32 TargetId,
		int32 ToBarrier, int32 ToBlock, int32 ToHP,
		bool bCrit, bool bDodged);

	static FHexGameAction Heal(int32 TargetId, int32 Amount);
	static FHexGameAction GainBlock(int32 TargetId, int32 Amount);
	static FHexGameAction ClearBlock(int32 TargetId);
	static FHexGameAction Kill(int32 TargetId);

	static FHexGameAction MoveUnit(int32 UnitId, const FIntVector& ToAnchor, int32 ToFacing);
	static FHexGameAction RotateUnit(int32 UnitId, int32 ToFacing);
	static FHexGameAction Knockback(int32 SourceId, int32 TargetId, int32 Distance);
	static FHexGameAction Trample(int32 SourceId, int32 TargetId);

	/**
	 * @param SourceId 施加者。-1 = 环境。
	 *        ⚠️ 玩家/敌人施加的 debuff 必须传 id：
	 *        它决定燃烧、中毒致死时算谁的击杀（影响 OnKill 类符文）。
	 */
	static FHexGameAction ApplyStatus(
		int32 TargetId, FName StatusId, int32 Stacks, int32 SourceId = -1);
	static FHexGameAction RemoveStatus(int32 TargetId, FName StatusId);
	static FHexGameAction TickStatus(int32 TargetId, EHexStatusTick Timing);

	static FHexGameAction DrawCards(int32 Count);
	static FHexGameAction DiscardCard(int32 CardUid);
	static FHexGameAction ExhaustCard(int32 CardUid);
	static FHexGameAction ReshuffleDeck();

	static FHexGameAction ModifyTerrain(const FIntVector& Cell, EHexTerrain Terrain);
	static FHexGameAction TriggerHazard(int32 UnitId);

	static FHexGameAction SetPhase(EHexBattlePhase Phase);
	static FHexGameAction AdvanceRound();
};

/**
 * 动作队列。
 *
 * ⚠️ R7 安全闸：单次 ResolveAll 的动作总数上限 MaxActionsPerResolve。
 *    超限时【写违规记录并中止，绝不抛异常】——
 *    这让 BattleSim 能把死循环变成可统计数据而非崩溃。
 */
class HEXSPIRECORE_API FHexActionQueue
{
public:
	/** 排到队尾 */
	void PushBack(const FHexGameAction& Action);

	/**
	 * 插到【当前动作之后】。
	 * 符文触发的子动作必须用这个，否则槽位顺序语义被打乱。
	 */
	void PushNext(const FHexGameAction& Action);

	bool IsEmpty() const { return Actions.Num() == 0; }
	int32 Num() const { return Actions.Num(); }

	void Reset();

	/**
	 * 顺序执行全部动作直到队列空。
	 *
	 * @param Observer 可选。每个动作执行后被调用，用于把动作翻译成
	 *                 触发时机（见 FHexActionObserver）。
	 *                 传空则退化为纯结算，行为与从前逐位相同。
	 * @return 实际执行的动作数
	 */
	int32 ResolveAll(FHexBattleState& State, const FHexActionObserver& Observer = nullptr);

	const TArray<FHexGameAction>& GetActions() const { return Actions; }

private:
	TArray<FHexGameAction> Actions;

	/** ResolveAll 期间的当前下标，PushNext 据此插入 */
	int32 CursorIndex = -1;
	bool bResolving = false;

	/**
	 * 本次动作内已 PushNext 的条数。
	 * ⚠️ 必须有：连续多次 PushNext 时，后插入的要排在先插入的之后。
	 *    没有这个偏移的话，符文槽 1 产出的 3 个子动作会被倒序执行。
	 */
	int32 PendingInsertOffset = 0;
};
