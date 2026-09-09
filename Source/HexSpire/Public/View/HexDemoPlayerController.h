// Copyright Hex Spire. All Rights Reserved.
//
// Demo 的输入 —— 鼠标拾取格子 + 键盘操作
//
// ⚠️ 鼠标拾取【不用碰撞检测】，而是"屏幕射线 × 棋盘平面求交"。
//    理由：
//      ① 六边形之间有缝隙（GapScale=0.94），碰撞体会在缝隙处漏点
//      ② 单位站在格子上会挡住格子的碰撞体
//      ③ 平面求交是纯数学，零开销且永不失败
//    交点再交给 FHexCoord::World2DToCube 反算格坐标 —— 那是唯一出口。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "HexDemoPlayerController.generated.h"

class AHexDemoGameMode;

UCLASS()
class HEXSPIRE_API AHexDemoPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AHexDemoPlayerController();

	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

	/** 鼠标当前指向的格；bValid 为 false 表示指到棋盘外 */
	FIntVector GetHoveredCell(bool& bValid) const;

private:
	void OnLeftClick();
	void OnRightClick();
	void OnEndTurn();
	void OnRestart();
	void OnConfirm();

	/** 数字键 1..9：地图阶段选房间 / 战斗阶段选手牌 */
	void OnNumberKey(int32 Index);

	AHexDemoGameMode* GetDemoMode() const;
};
