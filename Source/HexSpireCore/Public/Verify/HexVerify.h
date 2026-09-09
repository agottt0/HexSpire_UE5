// Copyright Hex Spire. All Rights Reserved.
//
// 自动化验证器框架 —— 对应 Godot 版 tools/verify_*.gd
//
// 设计目标：全部 headless、退出码即结论。
// 这是我（AI）能自行 debug 的基础设施：改任何东西后跑一遍，
// 看的是"通过/失败"而不是"肉眼审代码"。
//
// 用法（命令行）：
//   UnrealEditor-Cmd.exe <uproject> -run=HexSpireVerify -suite=all
//   UnrealEditor-Cmd.exe <uproject> -run=HexSpireVerify -suite=hex
//
// ⚠️ 验证器属于 core 的一部分（纯逻辑、无渲染），
//    因此放在 HexSpireCore 而非编辑器模块 —— 这样打包后也能跑。

#pragma once

#include "CoreMinimal.h"

/** 单条断言的结果 */
struct HEXSPIRECORE_API FHexVerifyResult
{
	FString Name;
	bool bPassed = false;
	FString Detail;
};

/**
 * 验证上下文。收集断言结果，最终汇总。
 *
 * 刻意不用 UE 的 AutomationTest 框架：
 *   ① 那套依赖编辑器模块，打包后跑不了
 *   ② 我们需要"跑 10 万场模拟统计分布"这种非布尔断言
 *   ③ 需要精确控制输出格式以便我解析
 */
class HEXSPIRECORE_API FHexVerifyContext
{
public:
	explicit FHexVerifyContext(const FString& InSuiteName);

	/** 布尔断言 */
	void Check(const FString& Name, bool bCondition, const FString& Detail = FString());

	/** 相等断言（整数） */
	void CheckEqual(const FString& Name, int32 Actual, int32 Expected);

	/** 相等断言（浮点，带容差） */
	void CheckNearlyEqual(const FString& Name, float Actual, float Expected, float Tolerance = 0.001f);

	/** 相等断言（立方坐标，失败时打印 offset 形式便于人读） */
	void CheckEqualCoord(const FString& Name, const FIntVector& Actual, const FIntVector& Expected);

	/** 直接记一条失败 */
	void Fail(const FString& Name, const FString& Detail);

	/** 分节标题，仅用于输出可读性 */
	void Section(const FString& Title);

	/** 打印汇总并返回是否全部通过 */
	bool Summarize() const;

	int32 NumPassed() const;
	int32 NumFailed() const;

	const TArray<FHexVerifyResult>& GetResults() const { return Results; }

private:
	FString SuiteName;
	TArray<FHexVerifyResult> Results;
};

/** 各验证套件的入口。每个返回是否全部通过。 */
struct HEXSPIRECORE_API FHexVerifySuites
{
	/** 坐标往返、旋转闭合、facing 语义、出生表交叉验证 */
	static bool VerifyHex(FHexVerifyContext& Ctx);

	/** CanPlace 与独立参考实现的穷举比对（R9） */
	static bool VerifyFootprint(FHexVerifyContext& Ctx);

	/** 最短路、三级门宽闸门、确定性 tiebreak */
	static bool VerifyPathfinding(FHexVerifyContext& Ctx);

	/** RNG 均匀性、无偏性、可复现性、分流独立性 */
	static bool VerifyRng(FHexVerifyContext& Ctx);

	/**
	 * 内容纪律：§7.5 系数化、§7.6 基石不占容量、§6.4 符文类别占比、
	 * q18 卡组空位、怪物组引用完整性、腐蚀度缩放单调性。
	 */
	static bool VerifyContent(FHexVerifyContext& Ctx);

	/**
	 * 伤害管线 9 阶段。
	 * 核心是 ②′ 顺序敏感性 —— 它一旦退化为桶式聚合，D6 的免费深度会静默消失。
	 */
	static bool VerifyDamage(FHexVerifyContext& Ctx);

	/**
	 * 装备系统：词条纪律、稀有度门槛、槽位限制、重塑、
	 * 武器对《攻击》的结构覆写、以及接入 RuleBook / TriggerBus 的正确性。
	 */
	static bool VerifyEquip(FHexVerifyContext& Ctx);

	/**
	 * 盲探地图（D4）与层循环。
	 *
	 * ⚠️ 这是唯一【没有 Godot 参考实现】的系统，全部行为从策划案框架从零设计。
	 *    所以断言最严：生成期不变量用 200 个种子穷举，
	 *    并量化 R5 对抗效果（补偿②必须把 Boss 候选从 5 选 1 收窄到 ≤2.5）。
	 */
	static bool VerifyMap(FHexVerifyContext& Ctx);

	/**
	 * 状态效果 11 种：叠加语义、衰减、数值后果。
	 * ⚠️ 数值是自拟的（§8.9 只给字段框架），所以断言还要证明
	 *    注释里的设计推导自洽（如"虚弱 4 层不会变免伤"）。
	 */
	static bool VerifyStatus(FHexVerifyContext& Ctx);

	/** 牌堆四区：不变量、洗回、手牌上限、置顶、序列化 */
	static bool VerifyDeck(FHexVerifyContext& Ctx);

	/** 规则书：每条 GameRule 有唯一消费点、增量叠加、边界夹取 */
	static bool VerifyRules(FHexVerifyContext& Ctx);

	/** 触发总线：分层顺序、计数不串号、R7 安全闸、②′ 钩子链 */
	static bool VerifyTrigger(FHexVerifyContext& Ctx);

	/**
	 * 敌人 AI。
	 * ⚠️ 核心断言是「意图是承诺」：可躲型意图在执行时【不得重算】目标。
	 *    重算不会报错，只会让玩家觉得"我明明躲开了还是挨打"。
	 */
	static bool VerifyAI(FHexVerifyContext& Ctx);

	/**
	 * 整场战斗流程（集成测试）。
	 *
	 * 抓的是"单块都对但合起来错"的问题：阶段卡住、战斗不终止、
	 * 长流程下确定性漂移、多回合后牌堆不变量被破坏。
	 * ⚠️ 最重要的一条是【战斗必须终止】—— 死循环在回合制里
	 *    表现为"点了结束回合没反应"，玩家只能强杀进程。
	 */
	static bool VerifyBattle(FHexVerifyContext& Ctx);

	/** 跑全部套件 */
	static bool RunAll();

	/** 按名字跑指定套件；SuiteName 为 "all" 时等价 RunAll */
	static bool RunSuite(const FString& SuiteName);
};
