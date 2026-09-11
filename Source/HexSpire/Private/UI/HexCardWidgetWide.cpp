// Copyright Hex Spire. All Rights Reserved.

#include "UI/HexCardWidgetWide.h"
#include "UI/HexCardArt.h"
#include "UI/HexCardLayout.h"
#include "HexSpire.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace L = HexCardLayout;

namespace
{
	void SetOverlayPad(UWidget* W, EHorizontalAlignment H, EVerticalAlignment V,
		const FMargin& Pad = FMargin(0.0f))
	{
		if (UOverlaySlot* S = Cast<UOverlaySlot>(W->Slot))
		{
			S->SetHorizontalAlignment(H);
			S->SetVerticalAlignment(V);
			S->SetPadding(Pad);
		}
	}
}

UHexCardWidgetWide::UHexCardWidgetWide(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UTexture2D* UHexCardWidgetWide::GetCardFrameTexture() const
{
	return HexCardArt::GetWideFrame(GetCardView().CardType);
}

void UHexCardWidgetWide::BuildDefaultTree()
{
	// ══════════════════════════════════════════════════════════════
	// 横版结构：图标在左，卡名 + 描述在右
	// ══════════════════════════════════════════════════════════════
	//   ┌─────────────────────────┐
	//   │(1)                      │  ← 左上：费用
	//   │  [图]  盾 击        [Q] │  ← 图标 | 卡名 | 快捷键
	//   │        造成18点伤害      │  ← 描述
	//   └─────────────────────────┘
	//
	// ⚠️ 控件【名字】必须与父类的 BindWidgetOptional 完全一致
	//    （Frame / TypeIcon / Cost / Name / Desc / Hotkey / Dim）。
	//    绑定是纯按名字匹配的，名字对不上那一项就永远收不到数据，
	//    而且编译期只有一条 Note、运行时完全静默。
	//
	// ⚠️ 横版【没有】Range 和 TypeLabel：
	//    地方太小放不下，而固定卡的射程与类型玩家早就记住了
	//    （就那三张）。因为父类用的是 Optional 绑定，
	//    少这两个控件不会崩，对应字段保持 nullptr、ApplyView 里会跳过。

	Root = WidgetTree->ConstructWidget<USizeBox>(
		USizeBox::StaticClass(), TEXT("CardRoot"));
	Root->SetWidthOverride(WideWidth);
	Root->SetHeightOverride(WideHeight);

	// ⚠️ 与竖版同样的坑：WidthOverride 存不进控件蓝图资产
	//    （bOverride_WidthOverride 那一位不被序列化），
	//    宽度会被内容撑开。Min/Max DesiredWidth 能正常存盘，
	//    两个都设成卡宽即把尺寸夹死。
	Root->SetMinDesiredWidth(WideWidth);
	Root->SetMaxDesiredWidth(WideWidth);
	Root->SetMinDesiredHeight(WideHeight);
	Root->SetMaxDesiredHeight(WideHeight);

	MainOverlay = WidgetTree->ConstructWidget<UOverlay>(
		UOverlay::StaticClass(), TEXT("CardOverlay"));
	Root->AddChild(MainOverlay);

	// ── ① 卡框（横版贴图，铺满）
	Frame = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), L::N::Frame);
	MainOverlay->AddChild(Frame);
	SetOverlayPad(Frame, HAlign_Fill, VAlign_Fill);

	// ── ② 内容行：图标 | 文字 | 快捷键
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(
			UHorizontalBox::StaticClass(), TEXT("ContentRow"));
		MainOverlay->AddChild(Row);
		// ⚠️ 内边距让内容落在贴图的【留白区】内。
		//    这几个比例是对贴图做亮度采样量出来的，见 HexCardLayout::R。
		SetOverlayPad(Row, HAlign_Fill, VAlign_Fill, L::R::ContentPad);

		// 类型图标
		TypeIcon = WidgetTree->ConstructWidget<UImage>(
			UImage::StaticClass(), L::N::TypeIcon);
		TypeIcon->SetDesiredSizeOverride(L::R::IconSize);
		Row->AddChild(TypeIcon);
		if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(TypeIcon->Slot))
		{
			S->SetVerticalAlignment(VAlign_Center);
			S->SetPadding(L::R::IconPad);
		}

		// 文字栏（卡名 + 描述）
		{
			UVerticalBox* TextCol = WidgetTree->ConstructWidget<UVerticalBox>(
				UVerticalBox::StaticClass(), TEXT("TextCol"));
			Row->AddChild(TextCol);
			if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(TextCol->Slot))
			{
				// 撑满剩余宽度 —— 否则描述换行会把快捷键挤出卡外
				S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				S->SetVerticalAlignment(VAlign_Center);
			}

			Name = WidgetTree->ConstructWidget<UTextBlock>(
				UTextBlock::StaticClass(), L::N::Name);
			Name->SetFont(L::GetCardFont(L::R::FontName, true));
			Name->SetJustification(ETextJustify::Left);
			TextCol->AddChild(Name);

			Desc = WidgetTree->ConstructWidget<UTextBlock>(
				UTextBlock::StaticClass(), L::N::Desc);
			Desc->SetFont(L::GetCardFont(L::R::FontDesc));
			Desc->SetJustification(ETextJustify::Left);
			Desc->SetAutoWrapText(true);
			TextCol->AddChild(Desc);
		}

		// 快捷键（右侧居中）
		Hotkey = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), L::N::Hotkey);
		Hotkey->SetFont(L::GetCardFont(L::R::FontKey, true));
		Row->AddChild(Hotkey);
		if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(Hotkey->Slot))
		{
			S->SetVerticalAlignment(VAlign_Center);
			S->SetPadding(L::R::HotkeyPad);
		}
	}

	// ── ③ 左上角费用
	{
		CostBadge = WidgetTree->ConstructWidget<UBorder>(
			UBorder::StaticClass(), L::N::CostBadge);
		CostBadge->SetPadding(L::R::CostInner);
		CostBadge->SetBrushColor(L::ColCostBadge);
		MainOverlay->AddChild(CostBadge);
		SetOverlayPad(CostBadge, HAlign_Left, VAlign_Top, L::R::CostSlotPad);

		Cost = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), L::N::Cost);
		Cost->SetFont(L::GetCardFont(L::R::FontCost, true));
		Cost->SetJustification(ETextJustify::Center);
		CostBadge->AddChild(Cost);
	}

	// ── ④ 不可用遮罩（浅纱，不是压暗 —— 卡面是浅色的）
	Dim = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), L::N::Dim);
	Dim->SetBrushColor(L::ColDimVeil);
	Dim->SetVisibility(ESlateVisibility::Hidden);
	MainOverlay->AddChild(Dim);
	SetOverlayPad(Dim, HAlign_Fill, VAlign_Fill);

	// ⚠️ 漏了这一行会让整棵树【建了但不显示】：
	//    Super::RebuildWidget() 返回 WidgetTree->RootWidget 的 Slate 表示，
	//    不赋值时它是 nullptr → SNullWidget，控件占着布局位置却什么都不画。
	//    而所有自检（控件对象、贴图加载）都会通过，给出虚假的安全感。
	WidgetTree->RootWidget = Root;
}
