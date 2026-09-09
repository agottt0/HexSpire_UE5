// Copyright Hex Spire. All Rights Reserved.
//
// 动作执行器 —— 【唯一】能修改 FHexBattleState 的地方（纪律 2）
//
// 每种动作一个处理器。未注册的动作类型会记 no_handler 违规，
// 而不是静默失败 —— 这是"加了新动作却忘了写处理器"的唯一防线。

#pragma once

#include "CoreMinimal.h"
#include "Battle/HexGameAction.h"

class FHexBattleState;

struct HEXSPIRECORE_API FHexActionResolver
{
	/** 执行单个动作。会写事件日志与动作日志。 */
	static void Execute(const FHexGameAction& Action, FHexBattleState& State);

	/** 自检：全部动作类型都有处理器 */
	static bool VerifyAllHandlersRegistered(TArray<EHexActionType>& OutMissing);
};
