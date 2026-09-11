// Copyright Hex Spire. All Rights Reserved.

#include "View/HexDemoHUD.h"
#include "View/HexDemoGameMode.h"
#include "View/HexDemoPlayerController.h"
#include "View/HexBoardVisual.h"
#include "UI/HexHandPanelWidget.h"
#include "HexSpire.h"

#include "Blueprint/UserWidget.h"

#include "Run/HexRunState.h"
#include "Battle/HexBattleState.h"
#include "Battle/HexBattleFlow.h"
#include "Battle/HexRuleBook.h"
#include "Battle/HexUnit.h"
#include "Battle/HexStatusData.h"
#include "Content/HexContentLibrary.h"
#include "Map/HexFloorMap.h"
#include "Runes/HexRuneData.h"
#include "Deck/HexPileManager.h"
#include "Hex/HexCoord.h"
#include "Core/HexSpireConstants.h"

#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	const FLinearColor ColText(0.92f, 0.92f, 0.90f);
	const FLinearColor ColDim(0.62f, 0.62f, 0.60f);
	const FLinearColor ColGood(0.35f, 0.90f, 0.45f);
	const FLinearColor ColWarn(0.98f, 0.78f, 0.20f);
	const FLinearColor ColBad(0.95f, 0.30f, 0.25f);
	const FLinearColor ColEnergy(0.35f, 0.75f, 1.00f);
	const FLinearColor ColPanel(0.04f, 0.05f, 0.07f);
	const FLinearColor ColTrack(0.72f, 0.20f, 0.88f);

	constexpr float LineH = 20.0f;
}

AHexDemoHUD::AHexDemoHUD()
{
	// 引擎自带字体，无需任何资产
	HudFont = GEngine ? GEngine->GetMediumFont() : nullptr;
}

// ══════════════════════════════════════════════════════════ 绘制辅助

void AHexDemoHUD::DrawPanel(float X, float Y, float W, float H,
	const FLinearColor& Fill, float Alpha)
{
	FLinearColor C = Fill;
	C.A = Alpha;
	DrawRect(C, X, Y, W, H);
}

void AHexDemoHUD::DrawTextShadowed(const FString& Text, float X, float Y,
	const FLinearColor& Color, float Scale)
{
	// ⚠️ 阴影不是装饰：战场底色是深灰，而伤害数字要显示在
	//    亮色高亮上；没有阴影时浅色文字在浅色高亮上会看不见。
	DrawText(Text, FLinearColor(0.0f, 0.0f, 0.0f, 0.85f),
		X + 1.0f, Y + 1.0f, HudFont, Scale);
	DrawText(Text, Color, X, Y, HudFont, Scale);
}

void AHexDemoHUD::DrawSolidBox(float X, float Y, float W, float H,
	const FLinearColor& Color, float Thickness)
{
	DrawLine(X, Y, X + W, Y, Color, Thickness);
	DrawLine(X + W, Y, X + W, Y + H, Color, Thickness);
	DrawLine(X + W, Y + H, X, Y + H, Color, Thickness);
	DrawLine(X, Y + H, X, Y, Color, Thickness);
}

void AHexDemoHUD::DrawDashedBox(float X, float Y, float W, float H,
	const FLinearColor& Color, float DashLen, float Thickness)
{
	// ⚠️ 实线 vs 虚线是 §13.2 点名的区分方式：
	//    实线 = 锁定格子（可躲），虚线 = 锁定单位（追踪）。
	//    用形状而非仅用颜色区分，是为了色弱玩家也能分辨。
	auto DashH = [&](float Y0)
	{
		for (float I = 0; I < W; I += DashLen * 2.0f)
		{
			const float Len = FMath::Min(DashLen, W - I);
			DrawLine(X + I, Y0, X + I + Len, Y0, Color, Thickness);
		}
	};
	auto DashV = [&](float X0)
	{
		for (float I = 0; I < H; I += DashLen * 2.0f)
		{
			const float Len = FMath::Min(DashLen, H - I);
			DrawLine(X0, Y + I, X0, Y + I + Len, Color, Thickness);
		}
	};

	DashH(Y);
	DashH(Y + H);
	DashV(X);
	DashV(X + W);
}

bool AHexDemoHUD::WorldToScreen(const FVector& World, FVector2D& OutScreen) const
{
	APlayerController* PC = GetOwningPlayerController();
	if (!PC)
	{
		return false;
	}
	return PC->ProjectWorldLocationToScreen(World, OutScreen);
}

// ══════════════════════════════════════════════════════════ 主入口

void AHexDemoHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	AHexDemoGameMode* Mode = Cast<AHexDemoGameMode>(
		UGameplayStatics::GetGameMode(GetWorld()));
	if (!Mode)
	{
		return;
	}

	// Tab 切换牌堆浏览器
	if (APlayerController* PC = GetOwningPlayerController())
	{
		static bool bTabWasDown = false;
		const bool bTabDown = PC->IsInputKeyDown(EKeys::Tab);
		if (bTabDown && !bTabWasDown)
		{
			bShowPiles = !bShowPiles;
		}
		bTabWasDown = bTabDown;
	}

	DrawTopBar(Mode);

	// 手牌与固定卡是 UMG 控件，每帧只需同步内容（不参与 Canvas 绘制）。
	// ⚠️ 放在 IsInBattle 判断【外面】：不在战斗时也要调用，
	//    否则从战斗回到地图时卡牌会留在屏幕上。
	//    RefreshFromGameMode 内部会在非战斗状态下折叠自己。
	EnsureHandPanel(Mode);

	if (Mode->IsInBattle())
	{
		DrawIntentLines(Mode);
		DrawUnitOverlays(Mode);
		DrawDamagePreview(Mode);
		if (bShowPiles)
		{
			DrawPileBrowser(Mode);
		}
	}
	else
	{
		DrawMapPanel(Mode);
	}

	DrawLegend();
	DrawHelp(Mode);
}

// ══════════════════════════════════════════════════════════ 顶栏

void AHexDemoHUD::DrawTopBar(AHexDemoGameMode* Mode)
{
	const float W = Canvas->SizeX;
	DrawPanel(0, 0, W, 74.0f, ColPanel, 0.85f);

	const FHexRunState* Run = Mode->GetRunState();
	if (!Run)
	{
		return;
	}

	float X = 14.0f;
	const float Y0 = 8.0f;
	const float Y1 = 30.0f;
	const float Y2 = 51.0f;

	// ── 生命
	{
		const float Ratio = Run->HeroHPMax > 0
			? static_cast<float>(Run->HeroHP) / Run->HeroHPMax : 0.0f;
		DrawTextShadowed(FString::Printf(TEXT("镇妖者  HP %d/%d"),
			Run->HeroHP, Run->HeroHPMax), X, Y0,
			Ratio > 0.5f ? ColGood : (Ratio > 0.25f ? ColWarn : ColBad));

		// 血条
		const float BarW = 200.0f;
		DrawRect(FLinearColor(0.15f, 0.05f, 0.05f, 0.9f), X, Y1, BarW, 10.0f);
		DrawRect(FLinearColor(0.25f, 0.85f, 0.35f, 0.95f), X, Y1, BarW * Ratio, 10.0f);
	}

	X += 230.0f;

	// ── §13.2 硬需求 5：腐蚀度与 Boss 强度【明示】
	//
	// ⚠️ 不能只显示"腐蚀度 5"这个裸数字 —— 玩家不知道它意味着什么。
	//    必须把它换算成可读的后果（敌人强度百分比），
	//    否则 D4 的"要不要多探一间"决策缺少判断依据。
	{
		const float HpBoost = Run->Corruption * HexK::CorruptionEnemyHpStep * 100.0f;
		const float AtkBoost = Run->Corruption * HexK::CorruptionEnemyAtkStep * 100.0f;

		DrawTextShadowed(FString::Printf(TEXT("腐蚀度 %d"), Run->Corruption),
			X, Y0, Run->Corruption >= 6 ? ColBad : ColWarn);
		DrawTextShadowed(FString::Printf(
			TEXT("敌人 HP +%.0f%%  ATK +%.0f%%  掉落品质↑"), HpBoost, AtkBoost),
			X, Y1 - 2.0f, ColDim);
		DrawTextShadowed(FString::Printf(
			TEXT("第 %d 层 · 碎片 %d"), Run->FloorIndex, Run->Shards),
			X, Y2, ColDim);
	}

	X += 300.0f;

	// ── 卡组与符文
	{
		DrawTextShadowed(FString::Printf(TEXT("卡组 %d/%d"),
			Run->GetUsedCapacity(), Run->DeckCapacity), X, Y0, ColText);

		FString Runes;
		for (int32 I = 0; I < FHexRuneLoadout::SlotCount; ++I)
		{
			const FHexRuneData* R = Run->RuneLoadout.GetSlot(I);
			Runes += R ? FString::Printf(TEXT("[%s]"), *R->DisplayName) : TEXT("[空]");
		}
		DrawTextShadowed(TEXT("符文 ") + Runes, X, Y1 - 2.0f, ColDim);
	}

	// ── 战斗中：回合与体力
	if (Mode->IsInBattle())
	{
		if (const FHexBattleState* BS = Mode->GetBattleState())
		{
			const float RX = W - 260.0f;
			DrawTextShadowed(FString::Printf(TEXT("回合 %d"), BS->RoundNumber),
				RX, Y0, ColText);

			const int32 EnergyMax = FHexRuleBook::EnergyMax(*BS);
			FString Pips;
			for (int32 I = 0; I < EnergyMax; ++I)
			{
				Pips += (I < BS->Energy) ? TEXT("◆") : TEXT("◇");
			}
			DrawTextShadowed(FString::Printf(TEXT("体力 %s  %d/%d"),
				*Pips, BS->Energy, EnergyMax), RX, Y1 - 2.0f, ColEnergy);

			DrawTextShadowed(FString::Printf(TEXT("抽 %d · 弃 %d · 消耗 %d"),
				BS->Piles.NumDraw(), BS->Piles.NumDiscard(), BS->Piles.NumExhaust()),
				RX, Y2, ColDim);
		}
	}

	// ── 状态提示
	DrawTextShadowed(Mode->GetStatusMessage(), 14.0f, 80.0f, ColWarn);
}

// ══════════════════════════════════════════════════════════ 地图面板

void AHexDemoHUD::DrawMapPanel(AHexDemoGameMode* Mode)
{
	const FHexRunState* Run = Mode->GetRunState();
	if (!Run)
	{
		return;
	}

	TArray<FHexRoomChoice> Choices;
	Mode->GetRoomChoices(Choices);

	const float PanelW = 520.0f;
	const float PanelH = 90.0f + Choices.Num() * 30.0f;
	const float X = (Canvas->SizeX - PanelW) * 0.5f;
	const float Y = Canvas->SizeY * 0.30f;

	DrawPanel(X, Y, PanelW, PanelH, ColPanel, 0.90f);
	DrawSolidBox(X, Y, PanelW, PanelH, FLinearColor(0.35f, 0.45f, 0.55f), 2.0f);

	DrawTextShadowed(TEXT("── 盲探：选择下一间房 ──"), X + 16.0f, Y + 12.0f, ColText, 1.1f);

	// ⚠️ D4 的信息补偿必须在 UI 上【说出来】，否则玩家不知道有这条规则。
	//    R5（盲探退化为随机点击）的一半原因是玩家不知道自己拥有什么线索。
	DrawTextShadowed(TEXT("线索：Boss 房必然与营地相邻 · 探明一间房会揭示它的邻接"),
		X + 16.0f, Y + 36.0f, ColDim);

	float RowY = Y + 62.0f;
	for (int32 I = 0; I < Choices.Num(); ++I)
	{
		const FHexRoomChoice& C = Choices[I];

		FLinearColor Col = ColText;
		if (C.Label.Contains(TEXT("BOSS")))
		{
			Col = ColBad;
		}
		else if (C.Label.Contains(TEXT("营地")))
		{
			Col = ColGood;
		}
		else if (C.Label.Contains(TEXT("精英")))
		{
			Col = ColWarn;
		}
		else if (!C.bTypeKnown)
		{
			Col = FLinearColor(0.55f, 0.60f, 0.75f);
		}
		else if (C.Label.Contains(TEXT("已清空")))
		{
			Col = ColDim;
		}

		DrawTextShadowed(FString::Printf(TEXT("[%d]  %s"), I + 1, *C.Label),
			X + 28.0f, RowY, Col);
		RowY += 30.0f;
	}

	// 死亡提示
	if (Run->HeroHP <= 0)
	{
		DrawTextShadowed(TEXT("※ 你已阵亡 —— 按 R 重开一局"),
			X + 16.0f, Y + PanelH - 26.0f, ColBad, 1.1f);
	}
}

// ══════════════════════════════════════════════════════════ 手牌

void AHexDemoHUD::EnsureHandPanel(AHexDemoGameMode* Mode)
{
	// ⚠️ 控件在首次 DrawHUD 时创建而不是在构造函数里：
	//    构造 HUD 时 PlayerController 还没准备好，
	//    CreateWidget 需要一个有效的 OwningPlayer 才能正确接收输入。
	if (!HandPanel)
	{
		APlayerController* PC = GetOwningPlayerController();
		if (!PC)
		{
			return;
		}

		// ⚠️ 走 ResolvePanelClass 而不是写死 StaticClass()：
		//    否则 WB_HandPanel 蓝图【永远不会被使用】——
		//    美术在里面调好的手牌排列（间距/锚点/固定卡位置）不生效，
		//    而且没有任何报错，只是看起来"改了没反应"。
		HandPanel = CreateWidget<UHexHandPanelWidget>(
			PC, UHexHandPanelWidget::ResolvePanelClass());
		if (!HandPanel)
		{
			UE_LOG(LogHexSpire, Error, TEXT("手牌控件创建失败"));
			return;
		}

		// ZOrder 0：卡牌在最底层。
		// ⚠️ HUD 的 Canvas 绘制【永远画在所有 UMG 之上】，
		//    所以顶栏/图例/伤害预览不会被卡牌挡住，不需要调 ZOrder。
		HandPanel->AddToViewport(0);
	}

	HandPanel->RefreshFromGameMode(Mode);
}


// ══════════════════════════════════════════════════════════ 单位浮层

void AHexDemoHUD::DrawUnitOverlays(AHexDemoGameMode* Mode)
{
	const FHexBattleState* BS = Mode->GetBattleState();
	const AHexBoardVisual* Board = Mode->GetBoard();
	if (!BS || !Board)
	{
		return;
	}

	for (const FHexUnit& U : BS->GetUnits())
	{
		if (!U.bIsAlive)
		{
			continue;
		}

		FVector2D Screen;
		if (!WorldToScreen(Board->CellToWorld(U.Anchor, 240.0f), Screen))
		{
			continue;
		}

		float Y = Screen.Y;

		// 名字 + 体型
		const TCHAR* SizeTag =
			(U.SizeClass == EHexSizeClass::L) ? TEXT("L") :
			(U.SizeClass == EHexSizeClass::M) ? TEXT("M") : TEXT("S");

		DrawTextShadowed(FString::Printf(TEXT("%s(%s)"), *U.DisplayName, SizeTag),
			Screen.X - 34.0f, Y,
			U.Team == EHexTeam::Player ? ColGood : ColText, 0.9f);
		Y += 15.0f;

		// HP / 格挡
		FString Vitals = FString::Printf(TEXT("%d/%d"), U.HP, U.HPMax);
		if (U.Block > 0)
		{
			Vitals += FString::Printf(TEXT("  盾%d"), U.Block);
		}
		DrawTextShadowed(Vitals, Screen.X - 34.0f, Y,
			U.Block > 0 ? ColEnergy : ColDim, 0.85f);
		Y += 15.0f;

		// ── 状态图标（灰盒期用文字缩写）
		//
		// ⚠️ 状态必须可见，否则玩家算不出伤害。
		//    §13.2 的"伤害可预览"依赖玩家知道场上有哪些 buff/debuff。
		//
		// ⚠️ 变量名不能叫 Tags —— AActor 已有 Tags 成员，会被 /WX 判为
		//    C4458「声明隐藏了类成员」并升级为错误。
		if (U.Statuses.Num() > 0)
		{
			FString StatusTags;
			for (const FHexStatusInstance& S : U.Statuses)
			{
				if (S.Stacks <= 0)
				{
					continue;
				}
				const FHexStatusDef& Def = FHexStatusLibrary::Get(S.Id);
				StatusTags += FString::Printf(TEXT("%s%d "), *Def.DisplayName, S.Stacks);
			}
			if (!StatusTags.IsEmpty())
			{
				DrawTextShadowed(StatusTags, Screen.X - 34.0f, Y, ColWarn, 0.82f);
				Y += 15.0f;
			}
		}

		// ── 敌人意图文字（§13.2 硬需求 2 的文字部分）
		if (U.Team == EHexTeam::Enemy && U.Intent.IsValid())
		{
			FString IntentText;
			FLinearColor IntentCol = ColBad;

			if (U.ShouldSkipTurn())
			{
				IntentText = TEXT("眩晕·不行动");
				IntentCol = ColWarn;
			}
			else
			{
				switch (U.Intent.Kind)
				{
				case EHexIntentKind::Attack:
					IntentText = FString::Printf(TEXT("攻击 %d"), U.Intent.PredictedDamage);
					break;
				case EHexIntentKind::MultiAttack:
					IntentText = FString::Printf(TEXT("连击 %d×%d"),
						U.Intent.PredictedDamage, U.Intent.HitCount);
					break;
				case EHexIntentKind::Move:
					IntentText = TEXT("移动");
					IntentCol = ColDim;
					break;
				case EHexIntentKind::Rotate:
					IntentText = TEXT("转向");
					IntentCol = ColDim;
					break;
				case EHexIntentKind::Buff:
					IntentText = TEXT("强化");
					IntentCol = ColWarn;
					break;
				case EHexIntentKind::Debuff:
					IntentText = TEXT("削弱");
					IntentCol = ColWarn;
					break;
				case EHexIntentKind::Sleep:
					IntentText = TEXT("休眠");
					IntentCol = ColDim;
					break;
				default:
					IntentText = TEXT("特殊");
					break;
				}

				// ⚠️ 这是 §13.2 明确点名的区分：
				//    「可躲」→ 玩家应该走开；「追踪」→ 走也没用，得格挡/打断。
				//    两者的应对方式完全相反，标错会直接害死玩家。
				if (U.Intent.Kind == EHexIntentKind::Attack
					|| U.Intent.Kind == EHexIntentKind::MultiAttack)
				{
					if (U.Intent.Targeting == EHexIntentTargeting::TrackTarget)
					{
						IntentText += TEXT(" ⟨追踪·躲不掉⟩");
						IntentCol = ColTrack;
					}
					else
					{
						IntentText += TEXT(" ⟨可躲⟩");
					}
				}
			}

			DrawTextShadowed(IntentText, Screen.X - 34.0f, Y, IntentCol, 0.88f);
		}
	}
}

// ══════════════════════════════════════════════════════════ 意图连线

void AHexDemoHUD::DrawIntentLines(AHexDemoGameMode* Mode)
{
	const FHexBattleState* BS = Mode->GetBattleState();
	const AHexBoardVisual* Board = Mode->GetBoard();
	if (!BS || !Board)
	{
		return;
	}

	for (const FHexUnit& U : BS->GetUnits())
	{
		if (U.Team != EHexTeam::Enemy || !U.bIsAlive || !U.Intent.IsValid())
		{
			continue;
		}
		if (U.ShouldSkipTurn())
		{
			continue;
		}
		if (U.Intent.Kind != EHexIntentKind::Attack
			&& U.Intent.Kind != EHexIntentKind::MultiAttack)
		{
			continue;
		}

		FVector2D From;
		if (!WorldToScreen(Board->CellToWorld(U.Anchor, 90.0f), From))
		{
			continue;
		}

		const bool bTracking =
			(U.Intent.Targeting == EHexIntentTargeting::TrackTarget);

		// ── 追踪型：画连线到被锁定的单位（§13.2 明确要求"虚线+连线"）
		if (bTracking)
		{
			const FHexUnit* Tracked = BS->FindUnit(U.Intent.TrackedUnitId);
			if (!Tracked)
			{
				continue;
			}

			FVector2D To;
			if (!WorldToScreen(Board->CellToWorld(Tracked->Anchor, 90.0f), To))
			{
				continue;
			}

			// 虚线连线 —— 一眼就知道"这一击跟着我走"
			const FVector2D Dir = (To - From).GetSafeNormal();
			const float Len = FVector2D::Distance(From, To);
			const float Dash = 12.0f;
			for (float I = 0; I < Len; I += Dash * 2.0f)
			{
				const FVector2D A = From + Dir * I;
				const FVector2D B = From + Dir * FMath::Min(I + Dash, Len);
				DrawLine(A.X, A.Y, B.X, B.Y, ColTrack, 2.5f);
			}

			// 目标处画虚线框
			DrawDashedBox(To.X - 26.0f, To.Y - 26.0f, 52.0f, 52.0f, ColTrack);
		}
		else
		{
			// ── 可躲型：在冻结的目标格画实线框，不画连线
			//    （不画连线是刻意的：连线暗示"跟着你"，与"可躲"矛盾）
			for (const FIntVector& Cell : U.Intent.TargetCells)
			{
				FVector2D S;
				if (WorldToScreen(Board->CellToWorld(Cell, 20.0f), S))
				{
					DrawSolidBox(S.X - 22.0f, S.Y - 16.0f, 44.0f, 32.0f, ColBad, 2.0f);
				}
			}
		}
	}
}

// ══════════════════════════════════════════════════════════ 伤害预览

void AHexDemoHUD::DrawDamagePreview(AHexDemoGameMode* Mode)
{
	// ⚠️ §13.2 硬需求 1：悬停必须显示"预计伤害 X（暴击 Y）"。
	//    这是"硬但公平"的前提 —— 玩家必须能在出牌前算清后果。
	//    PreviewDamage 保证不消耗 RNG，所以每帧调用是安全的。
	const int32 Uid = Mode->GetSelectedCardUid();
	if (Uid == 0)
	{
		return;
	}

	FHexBattleFlow* Flow = Mode->GetBattleFlow();
	const FHexBattleState* BS = Mode->GetBattleState();
	const AHexBoardVisual* Board = Mode->GetBoard();
	if (!Flow || !BS || !Board)
	{
		return;
	}

	AHexDemoPlayerController* PC =
		Cast<AHexDemoPlayerController>(GetOwningPlayerController());
	if (!PC)
	{
		return;
	}

	bool bValid = false;
	const FIntVector Cell = PC->GetHoveredCell(bValid);
	if (!bValid || !Mode->GetLegalTargets().Contains(Cell))
	{
		return;
	}

	const FIntPoint Dmg = Flow->PreviewDamage(Uid, Cell);
	if (Dmg.X <= 0 && Dmg.Y <= 0)
	{
		return;
	}

	FVector2D Screen;
	if (!WorldToScreen(Board->CellToWorld(Cell, 160.0f), Screen))
	{
		return;
	}

	const float W = 156.0f;
	const float H = 46.0f;
	const float X = Screen.X - W * 0.5f;
	const float Y = Screen.Y - H;

	DrawPanel(X, Y, W, H, FLinearColor(0.06f, 0.02f, 0.02f), 0.92f);
	DrawSolidBox(X, Y, W, H, ColBad, 1.5f);

	DrawTextShadowed(FString::Printf(TEXT("预计 %d"), Dmg.X),
		X + 8.0f, Y + 4.0f, ColBad, 1.15f);
	DrawTextShadowed(FString::Printf(TEXT("暴击 %d"), Dmg.Y),
		X + 8.0f, Y + 24.0f, ColWarn, 0.92f);

	// 目标当前血量与格挡 —— 让玩家算"这一下能不能杀"
	if (const FHexUnit* Target = BS->FindUnitAtCell(Cell))
	{
		FString Info = FString::Printf(TEXT("HP %d"), Target->HP);
		if (Target->Block > 0)
		{
			Info += FString::Printf(TEXT(" +盾%d"), Target->Block);
		}
		DrawTextShadowed(Info, X + 84.0f, Y + 24.0f, ColDim, 0.85f);

		// 能否斩杀
		if (Dmg.X >= Target->HP + Target->Block)
		{
			DrawTextShadowed(TEXT("必杀"), X + 84.0f, Y + 4.0f, ColGood, 1.0f);
		}
	}
}

// ══════════════════════════════════════════════════════════ 牌堆浏览器

void AHexDemoHUD::DrawPileBrowser(AHexDemoGameMode* Mode)
{
	// ⚠️ §13.2 硬需求 4：抽/弃牌堆必须可浏览。
	//    D2 的核心红利是【概率可推算】—— 玩家能算"我的《重击》还有几回合回来"。
	//    不给这个 UI 等于放弃 D2 的价值。
	const FHexBattleState* BS = Mode->GetBattleState();
	if (!BS)
	{
		return;
	}

	const float W = 300.0f;
	const float H = Canvas->SizeY * 0.62f;
	const float X = Canvas->SizeX - W - 12.0f;
	const float Y = 120.0f;

	DrawPanel(X, Y, W, H, ColPanel, 0.93f);
	DrawSolidBox(X, Y, W, H, FLinearColor(0.40f, 0.45f, 0.50f), 2.0f);

	float TY = Y + 10.0f;

	// ── 弃牌堆：完全可见【且有序】（§7.4 明确要求）
	DrawTextShadowed(FString::Printf(TEXT("弃牌堆（%d）· 有序"),
		BS->Piles.NumDiscard()), X + 10.0f, TY, ColText, 1.05f);
	TY += LineH + 2.0f;

	for (const FHexCardInstance& C : BS->Piles.GetDiscardPile())
	{
		if (TY > Y + H * 0.5f)
		{
			DrawTextShadowed(TEXT("…"), X + 20.0f, TY, ColDim, 0.85f);
			TY += LineH;
			break;
		}
		if (const FHexCardData* Card = FHexContentLibrary::FindCard(C.CardId))
		{
			DrawTextShadowed(Card->DisplayName, X + 20.0f, TY, ColDim, 0.9f);
			TY += LineH;
		}
	}

	TY += 10.0f;

	// ── 抽牌堆：内容可见，【顺序不可见】（§7.4：否则就是作弊）
	DrawTextShadowed(FString::Printf(TEXT("抽牌堆（%d）· 乱序显示"),
		BS->Piles.NumDraw()), X + 10.0f, TY, ColText, 1.05f);
	TY += LineH + 2.0f;

	{
		// 按卡名聚合并排序 —— 既隐藏了真实顺序，又让玩家能数张数
		TMap<FString, int32> Counts;
		for (const FHexCardInstance& C : BS->Piles.GetDrawPile())
		{
			if (const FHexCardData* Card = FHexContentLibrary::FindCard(C.CardId))
			{
				Counts.FindOrAdd(Card->DisplayName)++;
			}
		}

		TArray<FString> Names;
		Counts.GetKeys(Names);
		Names.Sort();

		for (const FString& N : Names)
		{
			if (TY > Y + H - 40.0f)
			{
				break;
			}
			DrawTextShadowed(FString::Printf(TEXT("%s ×%d"), *N, Counts[N]),
				X + 20.0f, TY, ColDim, 0.9f);
			TY += LineH;
		}
	}

	// ── 消耗区
	if (BS->Piles.NumExhaust() > 0)
	{
		TY += 8.0f;
		DrawTextShadowed(FString::Printf(TEXT("消耗区（%d）本场不再出现"),
			BS->Piles.NumExhaust()), X + 10.0f, TY, ColBad, 0.95f);
	}

	DrawTextShadowed(TEXT("Tab 关闭"), X + 10.0f, Y + H - 22.0f, ColDim, 0.85f);
}

// ══════════════════════════════════════════════════════════ 图例与帮助

void AHexDemoHUD::DrawLegend()
{
	// 灰盒期必须有图例：颜色的含义无法从画面本身推断
	const float H = 148.0f;
	const float W = 196.0f;

	// ⚠️ 图例放【右侧】。
	//    它原本在左侧垂直居中，与后加的固定卡区完全重叠 ——
	//    实测截图里《防御》被图例整个盖住，玩家看不到也点不准。
	//    让图例让位而不是挪固定卡：固定卡要点击、要肌肉记忆，
	//    位置必须稳定；图例只是静态参考，放哪都行。
	const float X = Canvas->SizeX - W - 12.0f;
	const float Y = Canvas->SizeY * 0.5f - H * 0.5f;

	DrawPanel(X, Y, W, H, ColPanel, 0.72f);

	struct FEntry { FLinearColor Color; const TCHAR* Text; };
	const FEntry Entries[] = {
		{ FLinearColor(0.20f, 0.90f, 0.55f), TEXT("我占据的格") },
		{ FLinearColor(0.95f, 0.85f, 0.20f), TEXT("可点击的目标") },
		{ FLinearColor(0.95f, 0.50f, 0.10f), TEXT("会波及的范围") },
		{ FLinearColor(0.95f, 0.15f, 0.15f), TEXT("敌人攻击·可躲") },
		{ ColTrack,                          TEXT("敌人攻击·追踪") },
		{ FLinearColor(0.15f, 0.45f, 0.95f), TEXT("敌人将移动到") },
	};

	float TY = Y + 8.0f;
	for (const FEntry& E : Entries)
	{
		DrawRect(E.Color, X + 10.0f, TY + 3.0f, 14.0f, 12.0f);
		DrawTextShadowed(E.Text, X + 32.0f, TY, ColText, 0.88f);
		TY += 23.0f;
	}
}

void AHexDemoHUD::DrawHelp(AHexDemoGameMode* Mode)
{
	const FString Help = Mode->IsInBattle()
		? TEXT("数字键选手牌 · QWE(或点击)选固定卡 · 左键点黄格出牌 · 右键取消 · 空格结束回合 · Tab 看牌堆 · R 重开")
		: TEXT("数字键选择房间 · Enter 确认 · R 重开一局");

	// ⚠️ 提示行原本贴在手牌上方(SizeY-158)。固定卡区在左侧，
	//    两者不重叠，位置不用动。
	DrawTextShadowed(Help, 12.0f, Canvas->SizeY - 158.0f, ColDim, 0.88f);
}
