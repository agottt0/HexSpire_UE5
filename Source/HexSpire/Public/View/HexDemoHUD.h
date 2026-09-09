// Copyright Hex Spire. All Rights Reserved.
//
// Demo 的界面 —— 用 DrawHUD 直接画，零 UMG 资产
//
// ══════════════════════════════════════════════════════════════════
// 为什么用 DrawHUD 而不是 UMG
// ══════════════════════════════════════════════════════════════════
// UMG 的每个控件都是 .uasset（WidgetBlueprint），是二进制资产，
// 而这个 demo 的目标是"纯代码交付，你点 Play 就能玩"。
// DrawHUD 能画文字、方框、线条、连线 —— 灰盒期完全够用，
// 且改一行代码就能看到效果，比在编辑器里拖控件快得多。
//
// 正式版做 UMG 时，本文件里的布局逻辑与数据读取可以整段搬过去。
//
// ══════════════════════════════════════════════════════════════════
// §13.2 的 5 条硬需求（用户决策 q16 选定），全部在这里落地
// ══════════════════════════════════════════════════════════════════
//   ① 伤害预览：悬停目标显示"预计 X（暴击 Y）"
//   ② 意图可躲 vs 追踪：实线框 vs 虚线框 + 连线
//   ③ footprint 描边 + 移动落点预览（棋盘高亮负责，这里画图例）
//   ④ 抽/弃牌堆浏览器：Tab 展开
//   ⑤ 腐蚀度与 Boss 强度明示

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "HexDemoHUD.generated.h"

class AHexDemoGameMode;
class FHexBattleState;

UCLASS()
class HEXSPIRE_API AHexDemoHUD : public AHUD
{
	GENERATED_BODY()

public:
	AHexDemoHUD();

	virtual void DrawHUD() override;

private:
	// ── 分区绘制
	void DrawTopBar(AHexDemoGameMode* Mode);
	void DrawMapPanel(AHexDemoGameMode* Mode);
	void DrawHandPanel(AHexDemoGameMode* Mode);
	void DrawUnitOverlays(AHexDemoGameMode* Mode);
	void DrawIntentLines(AHexDemoGameMode* Mode);
	void DrawDamagePreview(AHexDemoGameMode* Mode);
	void DrawPileBrowser(AHexDemoGameMode* Mode);
	void DrawLegend();
	void DrawHelp(AHexDemoGameMode* Mode);

	// ── 绘制辅助
	void DrawPanel(float X, float Y, float W, float H, const FLinearColor& Fill, float Alpha = 0.78f);
	void DrawTextShadowed(const FString& Text, float X, float Y,
		const FLinearColor& Color, float Scale = 1.0f);
	/** 虚线框（意图·追踪型用） */
	void DrawDashedBox(float X, float Y, float W, float H,
		const FLinearColor& Color, float DashLen = 8.0f, float Thickness = 2.0f);
	void DrawSolidBox(float X, float Y, float W, float H,
		const FLinearColor& Color, float Thickness = 2.0f);

	/** 世界坐标 → 屏幕坐标；返回是否在屏幕内 */
	bool WorldToScreen(const FVector& World, FVector2D& OutScreen) const;

	UPROPERTY()
	class UFont* HudFont = nullptr;

	/** Tab 展开的牌堆浏览器（§13.2 硬需求 4） */
	bool bShowPiles = false;
};
