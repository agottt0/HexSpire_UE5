// Copyright Hex Spire. All Rights Reserved.

#include "UI/HexRuneSlotWidget.h"
#include "Runes/HexRuneData.h"
#include "HexSpire.h"

#include "Blueprint/WidgetTree.h"
#include "Components/SizeBox.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Image.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"

namespace
{
	// ── 卡面尺寸。贴图约 470x485（近方形），按 ~0.97 宽高比缩放。
	//    6 张 + 间距 ≈ 600px 宽，1280 分辨率下顶部正中放得下。
	constexpr float RuneCardW = 92.0f;
	constexpr float RuneCardH = 95.0f;

	// ── 稀有度 → 符文名颜色（卡面本身是美术贴图，稀有度改用文字色表达）
	FLinearColor RuneRarityColor(EHexRarity R)
	{
		switch (R)
		{
		case EHexRarity::Uncommon:  return FLinearColor(0.55f, 0.95f, 0.55f);
		case EHexRarity::Rare:      return FLinearColor(0.50f, 0.75f, 1.00f);
		case EHexRarity::Epic:      return FLinearColor(0.80f, 0.55f, 1.00f);
		case EHexRarity::Legendary: return FLinearColor(1.00f, 0.80f, 0.35f);
		case EHexRarity::Cursed:    return FLinearColor(1.00f, 0.40f, 0.42f);
		case EHexRarity::Common:
		default:                    return FLinearColor(0.92f, 0.92f, 0.90f);
		}
	}

	const TCHAR* RuneCategoryShort(EHexRuneCategory C)
	{
		switch (C)
		{
		case EHexRuneCategory::RuleRewrite: return TEXT("规则改写");
		case EHexRuneCategory::Trigger:     return TEXT("触发器");
		case EHexRuneCategory::Conditional: return TEXT("条件增益");
		case EHexRuneCategory::Multiplier:  return TEXT("乘区");
		case EHexRuneCategory::Curse:       return TEXT("诅咒");
		default:                            return TEXT("?");
		}
	}

	/** 文字压在美术贴图上必须带影，否则亮色卡面上直接看不见 */
	void ApplyShadow(UTextBlock* T)
	{
		T->SetShadowOffset(FVector2D(1.0f, 1.0f));
		T->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f));
	}
}

UHexRuneSlotWidget::UHexRuneSlotWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

TSharedRef<SWidget> UHexRuneSlotWidget::RebuildWidget()
{
	// WBP 设计器已提供树时绝不能顶掉（同 UHexCardWidget::RebuildWidget
	// 的长注释 —— "编辑器对、游戏错"最难查）。
	const bool bDesignerProvidedTree =
		(WidgetTree && WidgetTree->RootWidget != nullptr);

	if (!bDesignerProvidedTree && !CardArt)
	{
		USizeBox* Root = WidgetTree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(), TEXT("RuneSlotRoot"));
		Root->SetWidthOverride(RuneCardW);
		Root->SetHeightOverride(RuneCardH);

		UOverlay* Ov = WidgetTree->ConstructWidget<UOverlay>(
			UOverlay::StaticClass(), TEXT("RuneOverlay"));
		Root->AddChild(Ov);

		// ── ① 卡面（铺满）
		CardArt = WidgetTree->ConstructWidget<UImage>(
			UImage::StaticClass(), TEXT("CardArt"));
		Ov->AddChildToOverlay(CardArt);
		if (UOverlaySlot* S = Cast<UOverlaySlot>(CardArt->Slot))
		{
			S->SetHorizontalAlignment(HAlign_Fill);
			S->SetVerticalAlignment(VAlign_Fill);
		}

		// ── ② 选中蒙层（平时全透明；在文字【之下】，不压暗文字）
		SlotBorder = WidgetTree->ConstructWidget<UBorder>(
			UBorder::StaticClass(), TEXT("SlotBorder"));
		SlotBorder->SetBrushColor(FLinearColor(0, 0, 0, 0));
		Ov->AddChildToOverlay(SlotBorder);
		if (UOverlaySlot* S = Cast<UOverlaySlot>(SlotBorder->Slot))
		{
			S->SetHorizontalAlignment(HAlign_Fill);
			S->SetVerticalAlignment(VAlign_Fill);
		}

		// ── ③ 槽位号（左上角小字）
		SlotNum = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), TEXT("SlotNum"));
		{
			FSlateFontInfo Font = SlotNum->GetFont();
			Font.Size = 10;
			SlotNum->SetFont(Font);
			SlotNum->SetColorAndOpacity(
				FSlateColor(FLinearColor(0.95f, 0.92f, 0.80f)));
			ApplyShadow(SlotNum);
		}
		Ov->AddChildToOverlay(SlotNum);
		if (UOverlaySlot* S = Cast<UOverlaySlot>(SlotNum->Slot))
		{
			S->SetHorizontalAlignment(HAlign_Left);
			S->SetVerticalAlignment(VAlign_Top);
			S->SetPadding(FMargin(7.0f, 4.0f, 0.0f, 0.0f));
		}

		// ── ④ 符文名 + 类别（下部居中，压在卡面上）
		UVerticalBox* TextCol = WidgetTree->ConstructWidget<UVerticalBox>(
			UVerticalBox::StaticClass(), TEXT("RuneTextCol"));
		Ov->AddChildToOverlay(TextCol);
		if (UOverlaySlot* S = Cast<UOverlaySlot>(TextCol->Slot))
		{
			S->SetHorizontalAlignment(HAlign_Center);
			S->SetVerticalAlignment(VAlign_Bottom);
			S->SetPadding(FMargin(2.0f, 0.0f, 2.0f, 7.0f));
		}

		RuneName = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), TEXT("RuneName"));
		{
			FSlateFontInfo Font = RuneName->GetFont();
			Font.Size = 13;
			RuneName->SetFont(Font);
			RuneName->SetJustification(ETextJustify::Center);
			ApplyShadow(RuneName);
		}
		TextCol->AddChildToVerticalBox(RuneName);

		RuneCategory = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), TEXT("RuneCategory"));
		{
			FSlateFontInfo Font = RuneCategory->GetFont();
			Font.Size = 9;
			RuneCategory->SetFont(Font);
			RuneCategory->SetJustification(ETextJustify::Center);
			RuneCategory->SetColorAndOpacity(
				FSlateColor(FLinearColor(0.85f, 0.85f, 0.82f)));
			ApplyShadow(RuneCategory);
		}
		UVerticalBoxSlot* CatSlot = TextCol->AddChildToVerticalBox(RuneCategory);
		CatSlot->SetPadding(FMargin(0, 1, 0, 0));
		CatSlot->SetHorizontalAlignment(HAlign_Center);
	}

	return Super::RebuildWidget();
}

void UHexRuneSlotWidget::SetRune(
	int32 InSlotIndex, const FHexRuneData* Rune,
	bool bLocked, bool bSelected, float FlashStrength)
{
	SlotIndex = InSlotIndex;
	LastFlash = FlashStrength;

	// ── 卡面贴图：TCard_<槽位号>。只在槽位变化时加载一次。
	if (CardArt && CachedArtIndex != InSlotIndex)
	{
		const FString Path = FString::Printf(
			TEXT("/Game/ArtResource/Card/TCard_%d.TCard_%d"),
			InSlotIndex + 1, InSlotIndex + 1);
		CachedArt = LoadObject<UTexture2D>(nullptr, *Path);
		CachedArtIndex = InSlotIndex;

		if (CachedArt)
		{
			CardArt->SetBrushFromTexture(CachedArt, false);
		}
		else
		{
			// 贴图缺失必须报：否则表现是"第 N 张卡是白块"，
			// 而资产改名/挪目录不会有任何编译期信号。
			UE_LOG(LogHexSpire, Warning,
				TEXT("符文卡面贴图缺失：%s（槽 %d 显示为纯色块）"),
				*Path, InSlotIndex + 1);
		}
	}

	if (SlotNum)
	{
		SlotNum->SetText(FText::AsNumber(InSlotIndex + 1));
	}

	if (RuneName)
	{
		if (Rune)
		{
			RuneName->SetText(FText::FromString(Rune->DisplayName));
			RuneName->SetColorAndOpacity(FSlateColor(RuneRarityColor(Rune->Rarity)));
		}
		else
		{
			RuneName->SetText(FText::FromString(TEXT("空")));
			RuneName->SetColorAndOpacity(
				FSlateColor(FLinearColor(0.75f, 0.75f, 0.75f)));
		}
	}

	if (RuneCategory)
	{
		RuneCategory->SetText(Rune
			? FText::FromString(RuneCategoryShort(Rune->Category))
			: FText::GetEmpty());
	}

	// ── 卡面明暗：空槽压暗（像背面朝上），锁定再暗一档
	if (CardArt)
	{
		FLinearColor Tint = Rune
			? FLinearColor::White
			: FLinearColor(0.40f, 0.40f, 0.45f, 0.85f);
		if (bLocked)
		{
			Tint *= 0.7f;
			Tint.A = Rune ? 1.0f : 0.85f;
		}
		CardArt->SetColorAndOpacity(Tint);
	}

	// ── 选中蒙层 / 触发金闪（共用同一层：取两者较强的）
	if (SlotBorder)
	{
		const float SelA = bSelected ? 0.35f : 0.0f;
		const float FlashA = FlashStrength * 0.45f;
		const float A = FMath::Max(SelA, FlashA);
		SlotBorder->SetBrushColor(A > 0.0f
			? FLinearColor(0.95f, 0.80f, 0.25f, A)
			: FLinearColor(0, 0, 0, 0));
	}

	// 机制文本做成 tooltip —— R8 的要求：玩家必须能读到
	// "何时触发、几次、与什么交互"，否则组合无法推理。
	if (Rune)
	{
		FString Tip = Rune->DisplayName;
		if (!Rune->MechanicText.IsEmpty())
		{
			Tip += TEXT("\n\n") + Rune->MechanicText;
		}
		if (!Rune->FlavorText.IsEmpty())
		{
			Tip += TEXT("\n\n") + Rune->FlavorText;
		}
		SetToolTipText(FText::FromString(Tip));
	}
	else
	{
		SetToolTipText(FText::FromString(
			TEXT("空符文槽。三选一奖励里选中的符文会装进第一个空槽。")));
	}
}

void UHexRuneSlotWidget::NativeOnMouseEnter(
	const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
	bHovered = true;
}

void UHexRuneSlotWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	bHovered = false;
}

void UHexRuneSlotWidget::NativeTick(
	const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 悬停放大让玩家凑近看（tooltip 里是机制全文）；
	// 触发脉冲叠加一小段缩放，让"哪个符文生效了"可被余光捕捉。
	const float Target = 1.0f
		+ (bHovered ? 0.22f : 0.0f)
		+ LastFlash * 0.08f;

	if (!FMath::IsNearlyEqual(CurrentScale, Target, 0.001f))
	{
		CurrentScale = FMath::FInterpTo(CurrentScale, Target, InDeltaTime, 12.0f);

		// 锚点在顶边中点：顶部一排卡，向下放大不会顶出屏幕
		SetRenderTransformPivot(FVector2D(0.5f, 0.0f));
		SetRenderScale(FVector2D(CurrentScale, CurrentScale));
	}
}

FReply UHexRuneSlotWidget::NativeOnMouseButtonDown(
	const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	// ⚠️ 无论面板怎么处理，本控件都要【消费】点击 ——
	//    穿透到棋盘会被解释成"点了槽位背后那一格"（同卡牌控件的教训）。
	if (OnSlotClicked.IsBound())
	{
		OnSlotClicked.Execute(SlotIndex);
	}

	return FReply::Handled();
}
