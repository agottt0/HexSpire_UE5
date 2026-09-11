// Copyright Hex Spire. All Rights Reserved.

#include "View/HexDemoPlayerController.h"
#include "View/HexDemoGameMode.h"
#include "View/HexBoardVisual.h"
#include "View/HexDemoHUD.h"
#include "Battle/HexBattleState.h"
#include "Battle/HexBattleFlow.h"
#include "Battle/HexUnit.h"
#include "Deck/HexPileManager.h"
#include "Run/HexRunState.h"
#include "Hex/HexCoord.h"
#include "Kismet/GameplayStatics.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"

AHexDemoPlayerController::AHexDemoPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
	PrimaryActorTick.bCanEverTick = true;
}

AHexDemoGameMode* AHexDemoPlayerController::GetDemoMode() const
{
	return Cast<AHexDemoGameMode>(UGameplayStatics::GetGameMode(GetWorld()));
}

void AHexDemoPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (!InputComponent)
	{
		return;
	}

	// ⚠️ 用 BindKey 直接绑物理按键，不走 Input Action 资产 ——
	//    Input Action 是 .uasset，而这个 demo 要求零资产依赖。
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed,
		this, &AHexDemoPlayerController::OnLeftClick);
	InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed,
		this, &AHexDemoPlayerController::OnRightClick);

	InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed,
		this, &AHexDemoPlayerController::OnEndTurn);
	InputComponent->BindKey(EKeys::R, IE_Pressed,
		this, &AHexDemoPlayerController::OnRestart);
	InputComponent->BindKey(EKeys::Enter, IE_Pressed,
		this, &AHexDemoPlayerController::OnConfirm);

	// 数字键 1..9
	const FKey NumKeys[9] = {
		EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
		EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine
	};
	for (int32 I = 0; I < 9; ++I)
	{
		// 用 lambda 捕获下标：BindKey 不支持带参数的成员函数
		FInputKeyBinding Binding(FInputChord(NumKeys[I]), IE_Pressed);
		Binding.KeyDelegate.GetDelegateForManualSet().BindLambda(
			[this, I]() { OnNumberKey(I); });
		InputComponent->KeyBindings.Add(Binding);
	}

	// ── 固定卡：Q / W / E
	//
	// ⚠️ 刻意【不】和手牌共用数字键。
	//    共用的话手牌张数一变，固定卡的键位就跟着漂移，
	//    而固定卡的全部价值就在于"永远在那儿、永远是同一个键"。
	const FKey FixedKeys[3] = { EKeys::Q, EKeys::W, EKeys::E };
	for (int32 I = 0; I < 3; ++I)
	{
		FInputKeyBinding Binding(FInputChord(FixedKeys[I]), IE_Pressed);
		Binding.KeyDelegate.GetDelegateForManualSet().BindLambda(
			[this, I]() { OnFixedCardKey(I); });
		InputComponent->KeyBindings.Add(Binding);
	}
}

void AHexDemoPlayerController::OnFixedCardKey(int32 Index)
{
	AHexDemoGameMode* Mode = GetDemoMode();
	if (!Mode || !Mode->IsInBattle() || Mode->IsBattleOver())
	{
		return;
	}

	const FHexBattleState* BS = Mode->GetBattleState();
	if (!BS || !BS->FixedCards.IsValidIndex(Index))
	{
		return;
	}

	Mode->SelectCard(BS->FixedCards[Index].Uid);
}

FIntVector AHexDemoPlayerController::GetHoveredCell(bool& bValid) const
{
	bValid = false;

	const AHexDemoGameMode* Mode = GetDemoMode();
	if (!Mode || !Mode->GetBoard())
	{
		return FIntVector::ZeroValue;
	}

	FVector WorldOrigin;
	FVector WorldDirection;
	if (!DeprojectMousePositionToWorld(WorldOrigin, WorldDirection))
	{
		return FIntVector::ZeroValue;
	}

	// ── 射线 × 棋盘平面求交
	//
	// 棋盘位于本 Actor 的局部 Z=GroundThickness 平面。
	// Demo 里棋盘放在原点无旋转，所以直接用世界 Z 即可。
	const AHexBoardVisual* Board = Mode->GetBoard();
	const float PlaneZ = Board->CellToWorld(FIntVector::ZeroValue).Z;

	// 射线朝下才可能与地面相交
	if (FMath::IsNearlyZero(WorldDirection.Z))
	{
		return FIntVector::ZeroValue;
	}

	const float T = (PlaneZ - WorldOrigin.Z) / WorldDirection.Z;
	if (T <= 0.0f)
	{
		// 交点在相机背后
		return FIntVector::ZeroValue;
	}

	const FVector Hit = WorldOrigin + WorldDirection * T;
	const FIntVector Cell = Board->WorldToCell(Hit);

	// 界外判定：World2DToCube 会返回最近的格，即使指到棋盘外
	if (!FHexCoord::InBounds(Cell))
	{
		return Cell;
	}

	bValid = true;
	return Cell;
}

void AHexDemoPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	AHexDemoGameMode* Mode = GetDemoMode();
	if (!Mode)
	{
		return;
	}

	bool bValid = false;
	const FIntVector Cell = GetHoveredCell(bValid);
	if (bValid)
	{
		Mode->SetHoverCell(Cell);
	}
}

// ══════════════════════════════════════════════════════════ 操作

void AHexDemoPlayerController::OnLeftClick()
{
	AHexDemoGameMode* Mode = GetDemoMode();
	if (!Mode || !Mode->IsInBattle())
	{
		return;
	}

	// ⚠️ 卡牌的命中测试【已交给 Slate】（UHexCardWidget）。
	//    这里不再手算矩形 —— 原本 HUD 画一套坐标、这里算一套坐标，
	//    改动其一就会出现"看到的卡和点到的卡错位"。
	//
	//    落在卡牌上的点击会被控件消费（返回 Handled），根本不会
	//    走到这个函数；能走到这里的就是真正落在棋盘上的点击。

	bool bValid = false;
	const FIntVector Cell = GetHoveredCell(bValid);
	if (!bValid)
	{
		return;
	}

	const int32 Selected = Mode->GetSelectedCardUid();
	if (Selected == 0)
	{
		Mode->SetStatusMessage(TEXT("先选一张卡（手牌数字键 / 固定卡 Q W E 或点击），再点目标格"));
		return;
	}

	// ⚠️ 只在合法目标上才出牌 —— 点错地方不该消耗体力。
	//    合法目标已经高亮成黄色，玩家不会误判。
	if (!Mode->GetLegalTargets().Contains(Cell))
	{
		Mode->SetStatusMessage(TEXT("那不是合法目标（黄色格才是）"));
		return;
	}

	Mode->PlayCard(Selected, Cell);
}

void AHexDemoPlayerController::OnRightClick()
{
	if (AHexDemoGameMode* Mode = GetDemoMode())
	{
		Mode->ClearSelection();
		Mode->SetStatusMessage(TEXT("已取消选择"));
	}
}

void AHexDemoPlayerController::OnEndTurn()
{
	AHexDemoGameMode* Mode = GetDemoMode();
	if (!Mode || !Mode->IsInBattle() || Mode->IsBattleOver())
	{
		return;
	}
	Mode->EndTurn();
}

void AHexDemoPlayerController::OnRestart()
{
	if (AHexDemoGameMode* Mode = GetDemoMode())
	{
		Mode->StartNewRun(0);
	}
}

void AHexDemoPlayerController::OnConfirm()
{
	AHexDemoGameMode* Mode = GetDemoMode();
	if (!Mode)
	{
		return;
	}

	// 战斗胜利后：结算并回地图
	if (Mode->IsInBattle() && Mode->IsBattleOver())
	{
		if (Mode->IsPlayerDefeated())
		{
			Mode->SetStatusMessage(TEXT("你已经死了 —— 按 R 重开"));
			return;
		}
		Mode->FinishBattleAndReturnToMap();
	}
}

void AHexDemoPlayerController::OnNumberKey(int32 Index)
{
	AHexDemoGameMode* Mode = GetDemoMode();
	if (!Mode)
	{
		return;
	}

	// ── 战斗中：选手牌
	if (Mode->IsInBattle() && !Mode->IsBattleOver())
	{
		const FHexBattleState* BS = Mode->GetBattleState();
		if (!BS)
		{
			return;
		}

		const TArray<FHexCardInstance>& Hand = BS->Piles.GetHand();
		if (!Hand.IsValidIndex(Index))
		{
			return;
		}

		Mode->SelectCard(Hand[Index].Uid);
		return;
	}

	// ── 地图上：选房间
	if (!Mode->IsInBattle())
	{
		TArray<FHexRoomChoice> Choices;
		Mode->GetRoomChoices(Choices);
		if (!Choices.IsValidIndex(Index))
		{
			return;
		}
		Mode->EnterRoom(Choices[Index].RoomId);
	}
}
