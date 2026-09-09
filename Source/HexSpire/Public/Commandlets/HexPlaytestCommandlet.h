// Copyright Hex Spire. All Rights Reserved.
//
// 一层完整循环的自检 —— 不开窗口，验证"这个 demo 真的能玩通"
//
// ══════════════════════════════════════════════════════════════════
// 为什么需要它
// ══════════════════════════════════════════════════════════════════
// VerifyBattle 验证的是【单场战斗】能跑完。
// 但"能玩"要求的是整条链路：
//   开局 → 盲探选房 → 战斗 → 出牌 → 胜利 → 结算 → 腐蚀度上升
//   → 营地回血 → 找到 Boss → 打 Boss → 层结算
// 这条链路里每个衔接点都可能断（例如生命没回写、卡组没归还、
// 房间清空后走不下去），而这些都不会崩溃，只会让玩家卡住。
//
// 用法：
//   UnrealEditor-Cmd.exe <uproject> -run=HexPlaytest
//   UnrealEditor-Cmd.exe <uproject> -run=HexPlaytest -runs=50

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "HexPlaytestCommandlet.generated.h"

UCLASS()
class UHexPlaytestCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UHexPlaytestCommandlet();

	virtual int32 Main(const FString& Params) override;
};
