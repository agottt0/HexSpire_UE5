// Copyright Hex Spire. All Rights Reserved.

#include "UI/HexHandPanelWidget.h"
#include "UI/HexCardWidget.h"
#include "UI/HexCardWidgetWide.h"
#include "UI/HexCardArt.h"
#include "UI/HexCardLayout.h"
#include "UI/HexTopBarLayout.h"
#include "View/HexDemoGameMode.h"

#include "Battle/HexBattleState.h"
#include "Battle/HexBattleFlow.h"
#include "Battle/HexRuleBook.h"
#include "Battle/HexUnit.h"
#include "Content/HexContentLibrary.h"
#include "Core/HexSpireConstants.h"
#include "Deck/HexPileManager.h"
#include "Run/HexRunState.h"
#include "Runes/HexRuneData.h"
#include "HexSpire.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
// FParse::Param —— 自检里的"模拟点击"只在 -HexAutoRoom 下执行
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Styling/CoreStyle.h"
#include "Kismet/GameplayStatics.h"

namespace TB = HexTopBarLayout;


namespace
{
	/** 固定卡的按键标签。与 PlayerController 的 Q/W/E 绑定一致。 */
	const TCHAR* GFixedKeys[3] = { TEXT("Q"), TEXT("W"), TEXT("E") };

}

UHexHandPanelWidget::UHexHandPanelWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(false);
}

TSharedRef<SWidget> UHexHandPanelWidget::RebuildWidget()
{
	// ⚠️ WBP 子类在设计器里摆了控件时，WidgetTree->RootWidget 已非空，
	//    此时【不能】再建一棵默认树 —— 那会把设计器的根顶掉，
	//    美术摆好的版式在游戏里完全不出现（但编辑器预览是对的，
	//    因为预览不走这条路）。
	if (RootCanvas || (WidgetTree && WidgetTree->RootWidget))
	{
		return Super::RebuildWidget();
	}

	RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(), TEXT("HandRoot"));

	// ── 手牌：底部居中横排
	HandBox = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("HandBox"));
	RootCanvas->AddChild(HandBox);
	if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(HandBox->Slot))
	{
		// 锚在底边中点，控件自身以底边中点对齐 —— 分辨率变化时自动居中
		S->SetAnchors(FAnchors(0.5f, 1.0f, 0.5f, 1.0f));
		S->SetAlignment(FVector2D(0.5f, 1.0f));
		S->SetOffsets(FMargin(0.0f, 0.0f, 0.0f, 16.0f));
		S->SetAutoSize(true);
	}

	// ── 固定卡：左侧竖排
	{
		UVerticalBox* LeftCol = WidgetTree->ConstructWidget<UVerticalBox>(
			UVerticalBox::StaticClass(), TEXT("LeftCol"));
		RootCanvas->AddChild(LeftCol);
		if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(LeftCol->Slot))
		{
			// 锚在左侧垂直中点偏下 —— 避开顶栏(74px)与图例
			S->SetAnchors(FAnchors(0.0f, 0.52f, 0.0f, 0.52f));
			S->SetAlignment(FVector2D(0.0f, 0.5f));
			S->SetOffsets(FMargin(14.0f, 0.0f, 0.0f, 0.0f));
			S->SetAutoSize(true);
		}

		FixedTitle = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), TEXT("FixedTitle"));
		FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle("Regular", 10);
		if (GEngine && GEngine->GetMediumFont())
		{
			TitleFont.FontObject = GEngine->GetMediumFont();
			TitleFont.Size = 10;
		}
		FixedTitle->SetFont(TitleFont);
		// 让玩家明白这几张与手牌规则不同（常驻、不入牌堆）
		FixedTitle->SetText(FText::FromString(TEXT("固定卡 · 常驻")));
		FixedTitle->SetColorAndOpacity(FSlateColor(FLinearColor(0.62f, 0.62f, 0.60f)));
		LeftCol->AddChild(FixedTitle);

		FixedBox = WidgetTree->ConstructWidget<UVerticalBox>(
			UVerticalBox::StaticClass(), TEXT("FixedBox"));
		LeftCol->AddChild(FixedBox);
	}

	// ── 顶栏（左上角头像/字样 + 局内读数）
	BuildTopBarDefaultTree(RootCanvas);

	// ── 右下角操作区（体力火苗 + 结束回合按钮）
	BuildActionAreaDefaultTree(RootCanvas);

	WidgetTree->RootWidget = RootCanvas;

	return Super::RebuildWidget();
}

// ══════════════════════════════════════════════════════════ 顶栏默认树

void UHexHandPanelWidget::BuildTopBarDefaultTree(UCanvasPanel* Canvas)
{
	if (!Canvas)
	{
		return;
	}

	// 与 commandlet 生成的蓝图结构【必须一致】，否则"没建蓝图"和
	// "建了蓝图"两条路径的顶栏长得不一样，而这不会报错。
	// 数值全部读 HexTopBarLayout，不在这里写字面量。

	UBorder* Bar = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), FName(TB::N::TopBar));
	Bar->SetBrushColor(TB::ColBarBg);
	Bar->SetPadding(TB::BarPad);
	Canvas->AddChild(Bar);
	if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Bar->Slot))
	{
		// 锚顶边左右拉伸：分辨率变化时顶栏始终贴满上沿
		S->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 0.0f));
		S->SetOffsets(FMargin(0.0f, 0.0f, 0.0f, TB::BarHeight));
		S->SetAlignment(FVector2D(0.0f, 0.0f));
	}
	TopBar = Bar;

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), FName(TB::N::TopRow));
	Bar->AddChild(Row);

	// ── 字样（竖条，在头像【左边】）
	{
		UScaleBox* ArtBox = WidgetTree->ConstructWidget<UScaleBox>(
			UScaleBox::StaticClass(), FName(TB::N::NameArtBox));
		// ⚠️ ScaleToFit 而不是写死尺寸：四个角色的字样比例是
		//    0.36 / 0.41 / 0.52 / 0.34，写死宽高会让其中三个拉伸变形。
		ArtBox->SetStretch(EStretch::ScaleToFit);
		Row->AddChild(ArtBox);
		if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(ArtBox->Slot))
		{
			S->SetVerticalAlignment(VAlign_Fill);
			S->SetPadding(TB::NameArtPad);
		}

		// ⚠️ ScaleBox 只接【一个】子控件。四张字样要各自能被
		//    BindWidgetOptional 找到，所以中间垫一层 Overlay
		//    （Collapsed 的那三张不参与布局，不会挤偏可见那张）。
		UOverlay* Stack = WidgetTree->ConstructWidget<UOverlay>(
			UOverlay::StaticClass(), TEXT("NameArtStack"));
		ArtBox->AddChild(Stack);

		auto MakeNameArt = [&](const TCHAR* WidgetName) -> UImage*
		{
			UImage* Img = WidgetTree->ConstructWidget<UImage>(
				UImage::StaticClass(), FName(WidgetName));
			Stack->AddChild(Img);
			HexCardLayout::SetOverlaySlot(Img, HAlign_Center, VAlign_Center);
			Img->SetVisibility(ESlateVisibility::Collapsed);
			return Img;
		};

		NameArt_Warden    = MakeNameArt(TB::N::NameArtWarden);
		NameArt_Medium    = MakeNameArt(TB::N::NameArtMedium);
		NameArt_Scrivener = MakeNameArt(TB::N::NameArtScrivener);
		NameArt_Revenant  = MakeNameArt(TB::N::NameArtRevenant);
	}

	// ── 头像
	{
		USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(), TEXT("PortraitBox"));
		// ⚠️ 用 Min/MaxDesiredWidth 而不是 WidthOverride：
		//    WidthOverride 的 bOverride_ 那一位【存不进资产】
		//    （实测记录见 HexCardLayout.h），蓝图路径下尺寸会失控。
		//    这里虽然是 C++ 路径，但两条路径要一致，写法保持同一套。
		Box->SetMinDesiredWidth(TB::PortraitSize);
		Box->SetMaxDesiredWidth(TB::PortraitSize);
		Box->SetMinDesiredHeight(TB::PortraitSize);
		Box->SetMaxDesiredHeight(TB::PortraitSize);
		Row->AddChild(Box);
		if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(Box->Slot))
		{
			S->SetVerticalAlignment(VAlign_Center);
			S->SetPadding(TB::PortraitPad);
		}

		Portrait = WidgetTree->ConstructWidget<UImage>(
			UImage::StaticClass(), FName(TB::N::Portrait));
		Box->AddChild(Portrait);
	}

	// ── 读数列
	{
		UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>(
			UVerticalBox::StaticClass(), FName(TB::N::StatCol));
		Row->AddChild(Col);
		if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(Col->Slot))
		{
			S->SetVerticalAlignment(VAlign_Center);
		}

		auto MakeText = [&](const TCHAR* WidgetName, int32 FontSize,
			const FLinearColor& Color) -> UTextBlock*
		{
			UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(
				UTextBlock::StaticClass(), FName(WidgetName));
			T->SetFont(HexCardLayout::GetCardFont(FontSize));
			T->SetColorAndOpacity(FSlateColor(Color));
			Col->AddChild(T);
			if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(T->Slot))
			{
				S->SetPadding(TB::RowPad);
			}
			return T;
		};

		HeroHP = MakeText(TB::N::HeroHP, TB::FontHP, TB::ColGood);

		// ⚠️ 血条尺寸要靠外层 SizeBox 定，不能像 UImage 那样调
		//    SetDesiredSizeOverride —— UProgressBar 没有那个函数
		//    （它不是 UImage 的子类，笔刷尺寸不可直接覆写）。
		//    不套 SizeBox 的话血条会被 VerticalBox 拉成整列宽、高度塌成 0。
		USizeBox* BarBox = WidgetTree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(), TEXT("HPBarBox"));
		// 同前：用 Min/Max 而不是 WidthOverride（后者存不进资产）
		BarBox->SetMinDesiredWidth(TB::HPBarWidth);
		BarBox->SetMaxDesiredWidth(TB::HPBarWidth);
		BarBox->SetMinDesiredHeight(TB::HPBarHeight);
		BarBox->SetMaxDesiredHeight(TB::HPBarHeight);
		Col->AddChild(BarBox);
		if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(BarBox->Slot))
		{
			S->SetPadding(TB::RowPad);
			S->SetHorizontalAlignment(HAlign_Left);
		}

		HPBar = WidgetTree->ConstructWidget<UProgressBar>(
			UProgressBar::StaticClass(), FName(TB::N::HPBar));
		HPBar->SetFillColorAndOpacity(TB::ColHPFill);
		BarBox->AddChild(HPBar);

		Corruption       = MakeText(TB::N::Corruption, TB::FontStat, TB::ColWarn);
		CorruptionEffect = MakeText(TEXT("CorruptionEffect"), TB::FontSmall, TB::ColDim);
		FloorInfo        = MakeText(TEXT("FloorInfo"),        TB::FontSmall, TB::ColDim);
		DeckInfo         = MakeText(TB::N::DeckInfo,   TB::FontStat,  TB::ColText);
		RuneInfo         = MakeText(TB::N::RuneInfo,   TB::FontSmall, TB::ColDim);
		RoundNum         = MakeText(TB::N::RoundNum,   TB::FontStat,  TB::ColText);
		EnergyText       = MakeText(TB::N::EnergyText, TB::FontStat,  TB::ColEnergy);
		PileInfo         = MakeText(TB::N::PileInfo,   TB::FontSmall, TB::ColDim);
	}

	// ── 状态提示（顶栏下方，不在顶栏容器内 —— 它要能压在棋盘上）
	{
		StatusText = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), FName(TB::N::StatusText));
		StatusText->SetFont(HexCardLayout::GetCardFont(TB::FontStatus));
		StatusText->SetColorAndOpacity(FSlateColor(TB::ColWarn));
		Canvas->AddChild(StatusText);
		if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(StatusText->Slot))
		{
			S->SetAnchors(FAnchors(0.0f, 0.0f, 0.0f, 0.0f));
			S->SetAlignment(FVector2D(0.0f, 0.0f));
			S->SetOffsets(TB::StatusPad);
			S->SetAutoSize(true);
		}
	}
}

// ══════════════════════════════════════════════════ 右下角操作区默认树

void UHexHandPanelWidget::BuildActionAreaDefaultTree(UCanvasPanel* Canvas)
{
	if (!Canvas)
	{
		return;
	}

	// 与 commandlet 生成的蓝图结构【必须一致】，理由同顶栏：
	// 两条路径产出的外观不一样时【不会报错】，只能靠眼睛发现。
	//
	// 摆右下角的理由：手牌横排锚底边中点、固定卡锚左侧、
	// 图例（DrawLegend）在右侧垂直居中 —— 右下角是底部唯一空着的地方，
	// 而"结束回合"必须离手牌近：出完牌后的下一个动作就是它。

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), FName(TB::N::ActionCol));
	Canvas->AddChild(Col);
	if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Col->Slot))
	{
		// 锚右下角，自身以右下角对齐 —— 分辨率变化时始终贴着右下
		S->SetAnchors(FAnchors(1.0f, 1.0f, 1.0f, 1.0f));
		S->SetAlignment(FVector2D(1.0f, 1.0f));
		S->SetOffsets(TB::ActionPad);
		// ⚠️ AutoSize 必须开。不开的话 Slate 会拿 Offset 的
		//    Right/Bottom 当【尺寸】用（见 SConstraintCanvas::OnArrangeChildren），
		//    于是这里的 20/20 内缩会被解释成 20x20 的槽位，
		//    104px 的按钮被挤成 20px —— 表现是"按钮小得几乎看不见"。
		S->SetAutoSize(true);
	}
	ActionCol = Col;

	// ── 体力火苗（在按钮【上方】）
	//
	// ⚠️ 放按钮上方而不是左边：火苗数量会变（体力上限受符文影响），
	//    放左边的话整块会随体力数左右伸缩，按钮位置跟着漂 ——
	//    而按钮要肌肉记忆，位置必须钉死。放上方则只有火苗那一行变宽。
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(
			UHorizontalBox::StaticClass(), FName(TB::N::EnergyRow));
		Col->AddChild(Row);
		if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(Row->Slot))
		{
			S->SetPadding(TB::EnergyRowPad);
			// 右对齐 —— 让火苗这一行与按钮右边缘齐平，
			// 否则火苗变多时整块看起来在左右晃
			S->SetHorizontalAlignment(HAlign_Right);
		}
		EnergyRow = Row;

		EnergyCount = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), FName(TB::N::EnergyCount));
		EnergyCount->SetFont(HexCardLayout::GetCardFont(TB::FontStat, /*bBold*/true));
		EnergyCount->SetColorAndOpacity(FSlateColor(TB::ColEnergy));
		Row->AddChild(EnergyCount);
		if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(EnergyCount->Slot))
		{
			S->SetVerticalAlignment(VAlign_Center);
			S->SetPadding(TB::EnergyCountPad);
		}
	}

	// ── 结束回合按钮
	{
		USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(), TEXT("EndTurnBox"));
		// ⚠️ 用 Min/Max 而不是 WidthOverride —— 后者的 bOverride_ 位
		//    存不进资产（实测记录见 HexCardLayout.h），蓝图路径下
		//    按钮尺寸会失控。两条路径写法保持同一套。
		Box->SetMinDesiredWidth(TB::EndTurnSize);
		Box->SetMaxDesiredWidth(TB::EndTurnSize);
		Box->SetMinDesiredHeight(TB::EndTurnSize);
		Box->SetMaxDesiredHeight(TB::EndTurnSize);
		Col->AddChild(Box);
		if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(Box->Slot))
		{
			S->SetHorizontalAlignment(HAlign_Right);
		}

		EndTurnButton = WidgetTree->ConstructWidget<UButton>(
			UButton::StaticClass(), FName(TB::N::EndTurnButton));
		Box->AddChild(EndTurnButton);
		ApplyEndTurnStyle();
	}
}

void UHexHandPanelWidget::ApplyEndTurnStyle()
{
	if (!EndTurnButton)
	{
		return;
	}

	UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, TB::EndTurnPath);
	if (!Tex)
	{
		// ⚠️ 不 return —— 贴图缺失时保留引擎默认样式（灰方块）。
		//    按钮仍然能点，只是丑。悄悄不画的话玩家连"这里有个按钮"
		//    都不知道，而结束回合是每回合必用的操作。
		UE_LOG(LogHexSpire, Warning,
			TEXT("结束回合贴图加载失败：%s —— 按钮回退到引擎默认样式"),
			TB::EndTurnPath);
		return;
	}

	FButtonStyle Style = EndTurnButton->GetStyle();

	// ⚠️ 三态必须【全部】赋值。只设 Normal 的话，鼠标一移上去
	//    Hovered 会回退到引擎默认的灰色圆角方块 ——
	//    表现是"印章一碰就变成灰方块"，看起来像贴图丢了。
	auto MakeBrush = [Tex](const FLinearColor& Tint)
	{
		FSlateBrush B;
		B.SetResourceObject(Tex);
		B.SetImageSize(FVector2D(TB::EndTurnSize, TB::EndTurnSize));
		// ⚠️ DrawAs 必须是 Image 而不是默认的 Box。
		//    Box 走九宫格拉伸，而这张印章【不是九宫格资产】
		//    （边缘是造型的一部分），按 Box 画会把角上的尖端糊掉。
		B.DrawAs = ESlateBrushDrawType::Image;
		B.TintColor = FSlateColor(Tint);
		return B;
	};

	Style.SetNormal(MakeBrush(TB::ColEndTurnOn));
	Style.SetHovered(MakeBrush(TB::ColEndTurnHover));
	Style.SetPressed(MakeBrush(TB::ColEndTurnPress));
	Style.SetDisabled(MakeBrush(TB::ColEndTurnOff));

	// ⚠️ 内边距清零。引擎默认样式带 NormalPadding/PressedPadding，
	//    留着会让 104px 的槽位里只剩一小块画图，印章被缩小并偏移；
	//    而 PressedPadding 与 NormalPadding 不等还会让按下时整张图跳一下。
	Style.SetNormalPadding(FMargin(0.0f));
	Style.SetPressedPadding(FMargin(0.0f));

	EndTurnButton->SetStyle(Style);
}

UImage* UHexHandPanelWidget::GetOrCreateEnergyPip(int32 Index)
{
	if (EnergyPips.IsValidIndex(Index) && EnergyPips[Index])
	{
		return EnergyPips[Index];
	}

	if (!EnergyRow)
	{
		return nullptr;
	}

	UImage* Pip = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
	if (!Pip)
	{
		return nullptr;
	}

	if (UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, TB::EnergyPipPath))
	{
		Pip->SetBrushFromTexture(Tex, false);
		// ⚠️ 按贴图原比例给尺寸（1154x1000 → 34x30），不能写成正方形：
		//    火苗整图偏宽（左右各有一簇小火苗），压成方形会把它挤扁。
		Pip->SetDesiredSizeOverride(
			FVector2D(TB::EnergyPipWidth, TB::EnergyPipHeight));
	}
	else
	{
		UE_LOG(LogHexSpire, Warning,
			TEXT("体力火苗贴图加载失败：%s"), TB::EnergyPipPath);
	}

	// ⚠️ 必须插在 EnergyCount【之前】—— 火苗在左、读数在右。
	//    直接 AddChild 会把火苗追加到读数右边，于是变成 "2/5 🔥🔥"，
	//    而且每多一点体力读数就往左跳一格。
	const int32 InsertAt = EnergyCount
		? EnergyRow->GetChildIndex(EnergyCount) : EnergyRow->GetChildrenCount();
	EnergyRow->InsertChildAt(FMath::Max(0, InsertAt), Pip);

	if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(Pip->Slot))
	{
		S->SetVerticalAlignment(VAlign_Center);
		S->SetPadding(TB::EnergyPipPad);
	}

	EnergyPips.SetNum(FMath::Max(EnergyPips.Num(), Index + 1));
	EnergyPips[Index] = Pip;
	return Pip;
}

void UHexHandPanelWidget::NativePreConstruct()
{
	Super::NativePreConstruct();

	// ⚠️ 只在设计器里预览。运行时【必须】直接返回：
	//    否则这些占位卡会和真实手牌同时存在，
	//    而且它们没有 uid，点上去会被当成 uid=0 的卡处理。
	if (!IsDesignTime())
	{
		return;
	}

	// 顶栏在设计器里也要所见即所得：不摆头像/字样的话，
	// 美术调位置时四张图全是 Collapsed，只能盲摆。
	ApplyHeroArt();

	// 按钮样式也要在设计器里生效 —— 蓝图资产里存的是引擎默认的灰方块，
	// 不套一次样式的话美术看到的是灰方块而不是印章，会以为贴图没配上。
	ApplyEndTurnStyle();

	// 体力火苗同理：运行时才按体力上限建，设计器里 EnergyRow 是空的，
	// 在里面调间距/调火苗与按钮的相对位置全都看不到效果。
	if (EnergyRow)
	{
		// 每次改属性都会重跑 PreConstruct，先清掉上一批，否则越堆越多。
		//
		// ⚠️ 【不能】用 EnergyRow->ClearChildren()。EnergyCount 是蓝图里
		//    摆在这个容器内的控件（BindWidget 绑过来的），ClearChildren
		//    会把它一起删掉 —— 表现是"设计器里体力读数凭空消失"，
		//    而且一改预览数量就消失一次，看起来像绑定坏了。
		//    只移除【本函数上一轮自己建的】那些火苗。
		for (UImage* Old : EnergyPips)
		{
			if (Old)
			{
				EnergyRow->RemoveChild(Old);
			}
		}
		EnergyPips.Reset();

		for (int32 I = 0; I < DesignPreviewEnergy; ++I)
		{
			if (UImage* Pip = GetOrCreateEnergyPip(I))
			{
				// 预览里让后两个是"已用掉"的状态，好看出染色对比够不够
				Pip->SetColorAndOpacity(I < DesignPreviewEnergy - 2
					? TB::ColEnergyPipOn : TB::ColEnergyPipOff);
			}
		}
	}

	if (!HandBox)
	{
		return;
	}

	// 设计器每次改属性都会重跑 PreConstruct，先清掉上一批预览卡，
	// 否则会越堆越多。
	HandBox->ClearChildren();

	UClass* CardClass = ResolveCardClass();

	for (int32 I = 0; I < DesignPreviewCardCount; ++I)
	{
		UHexCardWidget* W = WidgetTree->ConstructWidget<UHexCardWidget>(CardClass);
		if (!W)
		{
			continue;
		}

		HandBox->AddChild(W);
		if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(W->Slot))
		{
			// 与运行时同一套间距/对齐，否则预览出来的排列不作数
			S->SetPadding(FMargin(4.0f, 0.0f));
			S->SetVerticalAlignment(VAlign_Bottom);
		}

		// 给点不一样的假数据，好看出"文字长度不同会不会把卡撑变形"
		FHexCardView V;
		V.DisplayName = FString::Printf(TEXT("预览卡%d"), I + 1);
		V.Description = (I % 2 == 0)
			? TEXT("造成 12 点伤害。")
			: TEXT("造成 18 点伤害（受防御加成），并击退 1 格。");
		V.Cost = I + 1;
		V.RangeMin = 1;
		V.RangeMax = 1 + (I % 2);
		V.HotkeyNumber = I + 1;
		V.CardType = (I % 2 == 0) ? EHexCardType::Attack : EHexCardType::Guard;
		V.bPlayable = true;
		V.bSelected = false;
		W->SetCardView(V);
	}
}

UClass* UHexHandPanelWidget::ResolvePanelClass()
{
	// ⚠️ 与 ResolveCardClass 同样【不扫资产注册表】：
	//    WidgetBlueprint 是编辑器专属资产类，-game 下注册表里一条都没有。
	//    这里加载的是【生成类】(_C)，那是运行时资产，两种环境都在。
	static const TCHAR* Candidates[] =
	{
		TEXT("/Game/HexSpire/UI/WB_HandPanel.WB_HandPanel_C"),
		TEXT("/Game/HexSpire/UI/WBP_HandPanel.WBP_HandPanel_C"),
	};

	for (const TCHAR* Path : Candidates)
	{
		if (UClass* Loaded = LoadClass<UHexHandPanelWidget>(nullptr, Path))
		{
			UE_LOG(LogHexSpire, Display,
				TEXT("手牌区控件类：已发现控件蓝图 %s"), Path);
			return Loaded;
		}
	}

	// 没建蓝图是常态，不报警
	return UHexHandPanelWidget::StaticClass();
}

UClass* UHexHandPanelWidget::ResolveFixedCardClass()
{
	if (ResolvedFixedCardClass)
	{
		return ResolvedFixedCardClass;
	}

	if (FixedCardWidgetClass)
	{
		ResolvedFixedCardClass = FixedCardWidgetClass.Get();
		UE_LOG(LogHexSpire, Display,
			TEXT("固定卡控件类：显式指定 %s"), *ResolvedFixedCardClass->GetName());
		return ResolvedFixedCardClass;
	}

	// ⚠️ 与竖版同理：加载【生成类】(_C) 而不是扫资产注册表 ——
	//    WidgetBlueprint 是编辑器专属资产类，-game 下注册表里一条都没有。
	static const TCHAR* Candidates[] =
	{
		TEXT("/Game/HexSpire/UI/WB_CardWide.WB_CardWide_C"),
		TEXT("/Game/HexSpire/UI/WBP_CardWide.WBP_CardWide_C"),
	};

	for (const TCHAR* Path : Candidates)
	{
		if (UClass* Loaded = LoadClass<UHexCardWidgetWide>(nullptr, Path))
		{
			ResolvedFixedCardClass = Loaded;
			UE_LOG(LogHexSpire, Display,
				TEXT("固定卡控件类：已发现控件蓝图 %s"), Path);
			return ResolvedFixedCardClass;
		}
	}

	ResolvedFixedCardClass = UHexCardWidgetWide::StaticClass();
	UE_LOG(LogHexSpire, Verbose,
		TEXT("固定卡控件类：未找到 WB_CardWide，使用 C++ 横版"));
	return ResolvedFixedCardClass;
}

UClass* UHexHandPanelWidget::ResolveCardClass()
{
	if (ResolvedCardClass)
	{
		return ResolvedCardClass;
	}

	// ① 显式指定优先（WBP 子类的默认值，或关卡里配的）
	if (CardWidgetClass)
	{
		ResolvedCardClass = CardWidgetClass.Get();
		UE_LOG(LogHexSpire, Display,
			TEXT("卡牌控件类：显式指定 %s"), *ResolvedCardClass->GetName());
		return ResolvedCardClass;
	}

	// ② 按约定路径直接加载生成类。
	//
	// ⚠️ 这里【曾经】是"扫资产注册表找继承本类的 WidgetBlueprint"。
	//    那个做法在编辑器里能用，但 -game 下恒失败：
	//    WidgetBlueprint 是【编辑器专属资产类】，独立运行时
	//    注册表里一条都没有（实测日志：「注册表里有 0 个 WidgetBlueprint」）。
	//    于是自动发现静默落空，回退到 C++ 版 —— 玩家看到的永远是
	//    C++ 布局，美术在蓝图里做的一切都不生效，且没有任何报错。
	//
	//    改成直接 LoadClass 生成类（_C）：生成类是【运行时资产】，
	//    编辑器和打包后都在，不依赖注册表。
	//    代价是路径要按约定固定下来，所以下面把候选名都列出来。
	//
	// ⚠️ 必须带 _C 后缀。/Game/.../WB_Card 是 WidgetBlueprint 资产本身
	//    （运行时不存在），ConstructWidget 要的是它生成的类
	//    ...WB_Card.WB_Card_C。漏了它恒返回 nullptr 然后静默回退。
	{
		// 允许两种命名，省得为了"WB_"还是"WBP_"前缀返工。
		// 顺序即优先级。
		static const TCHAR* Candidates[] =
		{
			TEXT("/Game/HexSpire/UI/WB_Card.WB_Card_C"),
			TEXT("/Game/HexSpire/UI/WBP_Card.WBP_Card_C"),
			TEXT("/Game/HexSpire/UI/WBP_HexCard.WBP_HexCard_C"),
		};

		for (const TCHAR* Path : Candidates)
		{
			// ⚠️ 用 LoadClass 而非 StaticFindObject：首次访问时资产还没加载，
			//    Find 只查内存里已有的对象，会漏。
			if (UClass* Loaded = LoadClass<UHexCardWidget>(nullptr, Path))
			{
				ResolvedCardClass = Loaded;
				UE_LOG(LogHexSpire, Display,
					TEXT("卡牌控件类：已发现控件蓝图 %s，改用它建卡"), Path);
				return ResolvedCardClass;
			}
		}
	}

	// ③ 回退到 C++ 版。
	//
	// ⚠️ 这【不是错误】，是灰盒期的常态（还没人建蓝图）。
	//    所以只记 Verbose —— 每次启动刷一条警告会淹掉真正要看的日志。
	ResolvedCardClass = UHexCardWidget::StaticClass();
	UE_LOG(LogHexSpire, Verbose,
		TEXT("卡牌控件类：未找到 /Game/HexSpire/UI/WB_Card 等候选，使用 C++ 版"));
	return ResolvedCardClass;
}

UHexCardWidget* UHexHandPanelWidget::GetOrCreateCard(
	UPanelWidget* Container, TArray<UHexCardWidget*>& Pool, int32 Index,
	UClass* CardClass)
{
	if (Pool.IsValidIndex(Index) && Pool[Index])
	{
		return Pool[Index];
	}

	UHexCardWidget* Card = WidgetTree->ConstructWidget<UHexCardWidget>(CardClass);
	if (!Card)
	{
		return nullptr;
	}

	Container->AddChild(Card);

	// 点击 → 选中该卡。
	// ⚠️ 走 GameMode::SelectCard 而不是直接改状态 —— 纪律 3：
	//    表现层只发送输入，不驱动逻辑。
	Card->OnCardClicked.BindLambda([this](int32 Uid)
	{
		if (AHexDemoGameMode* Mode = Cast<AHexDemoGameMode>(
			UGameplayStatics::GetGameMode(GetWorld())))
		{
			if (Mode->IsInBattle() && !Mode->IsBattleOver())
			{
				Mode->SelectCard(Uid);
			}
		}
	});

	Pool.SetNum(FMath::Max(Pool.Num(), Index + 1));
	Pool[Index] = Card;
	return Card;
}

void UHexHandPanelWidget::HideExtra(TArray<UHexCardWidget*>& Pool, int32 UsedCount)
{
	// ⚠️ 折叠而不销毁：手牌张数每回合都在变，
	//    反复销毁重建控件会产生大量 UObject 垃圾，
	//    而 Collapsed 的控件不参与布局也不渲染，开销可忽略。
	for (int32 I = UsedCount; I < Pool.Num(); ++I)
	{
		if (Pool[I])
		{
			Pool[I]->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

// ══════════════════════════════════════════════════════════ 顶栏取值
//
// ⚠️ 每个函数都必须能在 Mode / RunState / BattleState 为空时返回合法值。
//    属性绑定在【设计器预览】里也会被调用，而那时根本没有 GameMode ——
//    不判空会让美术一打开控件蓝图就崩编辑器。

AHexDemoGameMode* UHexHandPanelWidget::GetMode() const
{
	// ⚠️ 设计器里 GetWorld() 返回的是预览世界，没有我们的 GameMode，
	//    Cast 会得到 nullptr —— 这是正常路径，不要在这里报错。
	return Cast<AHexDemoGameMode>(UGameplayStatics::GetGameMode(GetWorld()));
}

FText UHexHandPanelWidget::GetHeroHPText() const
{
	if (AHexDemoGameMode* Mode = GetMode())
	{
		if (const FHexRunState* Run = Mode->GetRunState())
		{
			return FText::FromString(FString::Printf(
				TEXT("镇妖者  HP %d/%d"), Run->HeroHP, Run->HeroHPMax));
		}
	}
	// 设计器占位：空 TextBlock 高度为 0，会让美术以为这行不存在
	return FText::FromString(TEXT("镇妖者  HP 80/80"));
}

float UHexHandPanelWidget::GetHeroHPPercent() const
{
	if (AHexDemoGameMode* Mode = GetMode())
	{
		if (const FHexRunState* Run = Mode->GetRunState())
		{
			return Run->HeroHPMax > 0
				? static_cast<float>(Run->HeroHP) / Run->HeroHPMax : 0.0f;
		}
	}
	return 1.0f;
}

FLinearColor UHexHandPanelWidget::GetHeroHPColor() const
{
	const float R = GetHeroHPPercent();
	return R > 0.5f ? TB::ColGood : (R > 0.25f ? TB::ColWarn : TB::ColBad);
}

FSlateColor UHexHandPanelWidget::GetHeroHPSlateColor() const
{
	// 只是把 FLinearColor 包一层 —— 见头文件说明：
	// TextBlock 的 ColorAndOpacity 委托要 FSlateColor，签名必须严格一致。
	return FSlateColor(GetHeroHPColor());
}

FText UHexHandPanelWidget::GetCorruptionText() const
{
	if (AHexDemoGameMode* Mode = GetMode())
	{
		if (const FHexRunState* Run = Mode->GetRunState())
		{
			return FText::FromString(FString::Printf(
				TEXT("腐蚀度 %d"), Run->Corruption));
		}
	}
	return FText::FromString(TEXT("腐蚀度 0"));
}

FLinearColor UHexHandPanelWidget::GetCorruptionColor() const
{
	if (AHexDemoGameMode* Mode = GetMode())
	{
		if (const FHexRunState* Run = Mode->GetRunState())
		{
			return Run->Corruption >= 6 ? TB::ColBad : TB::ColWarn;
		}
	}
	return TB::ColWarn;
}

FSlateColor UHexHandPanelWidget::GetCorruptionSlateColor() const
{
	return FSlateColor(GetCorruptionColor());
}

FText UHexHandPanelWidget::GetCorruptionEffectText() const
{
	// ⚠️ §13.2 硬需求 5：不能只显示"腐蚀度 5"这个裸数字 ——
	//    玩家不知道它意味着什么。必须换算成可读的后果，
	//    否则 D4 的"要不要多探一间"决策缺少判断依据。
	int32 C = 0;
	if (AHexDemoGameMode* Mode = GetMode())
	{
		if (const FHexRunState* Run = Mode->GetRunState())
		{
			C = Run->Corruption;
		}
	}

	return FText::FromString(FString::Printf(
		TEXT("敌人 HP +%.0f%%  ATK +%.0f%%  掉落品质↑"),
		C * HexK::CorruptionEnemyHpStep * 100.0f,
		C * HexK::CorruptionEnemyAtkStep * 100.0f));
}

FText UHexHandPanelWidget::GetFloorText() const
{
	if (AHexDemoGameMode* Mode = GetMode())
	{
		if (const FHexRunState* Run = Mode->GetRunState())
		{
			return FText::FromString(FString::Printf(
				TEXT("第 %d 层 · 碎片 %d"), Run->FloorIndex, Run->Shards));
		}
	}
	return FText::FromString(TEXT("第 1 层 · 碎片 0"));
}

FText UHexHandPanelWidget::GetDeckText() const
{
	if (AHexDemoGameMode* Mode = GetMode())
	{
		if (const FHexRunState* Run = Mode->GetRunState())
		{
			return FText::FromString(FString::Printf(
				TEXT("卡组 %d/%d"), Run->GetUsedCapacity(), Run->DeckCapacity));
		}
	}
	return FText::FromString(TEXT("卡组 0/12"));
}

FText UHexHandPanelWidget::GetRuneText() const
{
	if (AHexDemoGameMode* Mode = GetMode())
	{
		if (const FHexRunState* Run = Mode->GetRunState())
		{
			FString Runes;
			for (int32 I = 0; I < FHexRuneLoadout::SlotCount; ++I)
			{
				const FHexRuneData* R = Run->RuneLoadout.GetSlot(I);
				Runes += R
					? FString::Printf(TEXT("[%s]"), *R->DisplayName)
					: TEXT("[空]");
			}
			return FText::FromString(TEXT("符文 ") + Runes);
		}
	}
	return FText::FromString(TEXT("符文 [空][空][空]"));
}

FText UHexHandPanelWidget::GetRoundText() const
{
	if (AHexDemoGameMode* Mode = GetMode())
	{
		if (Mode->IsInBattle())
		{
			if (const FHexBattleState* BS = Mode->GetBattleState())
			{
				return FText::FromString(FString::Printf(
					TEXT("回合 %d"), BS->RoundNumber));
			}
		}
		// 战斗外没有回合概念 —— 返回空，蓝图可绑显隐把这行收掉
		return FText::GetEmpty();
	}
	return FText::FromString(TEXT("回合 1"));
}

FText UHexHandPanelWidget::GetEnergyText() const
{
	if (AHexDemoGameMode* Mode = GetMode())
	{
		if (Mode->IsInBattle())
		{
			if (const FHexBattleState* BS = Mode->GetBattleState())
			{
				const int32 Max = FHexRuleBook::EnergyMax(*BS);

				// ⚠️ 用 ◆/◇ 而不是纯数字：体力是每回合都要数的量，
				//    §13.2 要求关键资源可以"一眼扫到"而不是读数字。
				FString Pips;
				for (int32 I = 0; I < Max; ++I)
				{
					Pips += (I < BS->Energy) ? TEXT("◆") : TEXT("◇");
				}
				return FText::FromString(FString::Printf(
					TEXT("体力 %s  %d/%d"), *Pips, BS->Energy, Max));
			}
		}
		return FText::GetEmpty();
	}
	return FText::FromString(TEXT("体力 ◆◆◆◇◇  3/5"));
}

FText UHexHandPanelWidget::GetPileText() const
{
	if (AHexDemoGameMode* Mode = GetMode())
	{
		if (Mode->IsInBattle())
		{
			if (const FHexBattleState* BS = Mode->GetBattleState())
			{
				return FText::FromString(FString::Printf(
					TEXT("抽 %d · 弃 %d · 消耗 %d"),
					BS->Piles.NumDraw(), BS->Piles.NumDiscard(),
					BS->Piles.NumExhaust()));
			}
		}
		return FText::GetEmpty();
	}
	return FText::FromString(TEXT("抽 5 · 弃 0 · 消耗 0"));
}

FText UHexHandPanelWidget::GetStatusText() const
{
	if (AHexDemoGameMode* Mode = GetMode())
	{
		return FText::FromString(Mode->GetStatusMessage());
	}
	return FText::GetEmpty();
}

FText UHexHandPanelWidget::GetEnergyCountText() const
{
	if (AHexDemoGameMode* Mode = GetMode())
	{
		if (Mode->IsInBattle())
		{
			if (const FHexBattleState* BS = Mode->GetBattleState())
			{
				// 火苗已经把"有几点"画出来了，这里只补数字 ——
				// 上限一多（符文加体力）时数火苗会数错，数字是兜底。
				return FText::FromString(FString::Printf(TEXT("%d/%d"),
					BS->Energy, FHexRuleBook::EnergyMax(*BS)));
			}
		}
		return FText::GetEmpty();
	}
	// 设计器预览：没有 GameMode，给一个看得见的占位
	return FText::FromString(TEXT("3/5"));
}

bool UHexHandPanelWidget::CanEndTurn() const
{
	AHexDemoGameMode* Mode = GetMode();

	// ⚠️ 设计器里没有 GameMode。返回 true 让按钮以【可按】的样子显示 ——
	//    返回 false 的话美术看到的是 Disabled 那套染色（压暗到 0.45），
	//    会以为贴图导得太暗。
	if (!Mode)
	{
		return true;
	}

	// ⚠️ 与 AHexDemoPlayerController::OnEndTurn 的判断【必须一致】。
	//    两边各写一套的话会出现"按钮亮着但点了没反应"，
	//    或者反过来"空格能用、按钮却是灰的"—— 两种都不报错。
	return Mode->IsInBattle() && !Mode->IsBattleOver();
}

void UHexHandPanelWidget::OnEndTurnClicked()
{
	AHexDemoGameMode* Mode = GetMode();
	if (!Mode)
	{
		return;
	}

	// ⚠️ 这里【必须】再判一次，不能只靠按钮的 SetIsEnabled。
	//    Slate 的点击与我们每帧的刷新之间存在一帧的窗口：
	//    最后一张牌打完导致战斗结束的那一帧，按钮还是 enabled 的。
	//    那一帧点下去会在 BattleOver 之后再走一次 EndPlayerTurn。
	if (!CanEndTurn())
	{
		return;
	}

	// 走 GameMode 而不是自己改状态 —— 纪律 3：表现层只发送输入。
	// 与空格键（PlayerController::OnEndTurn）汇到同一个入口，
	// 所以"点按钮"和"按空格"不可能出现行为差异。
	Mode->EndTurn();
}

// ── 角色 UI 资产

const TB::FHeroUIArt& UHexHandPanelWidget::ResolveHeroArt() const
{
	// 设计器里按 DesignPreviewHeroIndex 挑，运行时按 RunState 的 HeroId。
	//
	// ⚠️ 目前逻辑层只有 warden 一个英雄，所以运行时恒是 Warden。
	//    四组资产都摆进蓝图、三组 Collapsed，以后加英雄时
	//    在这里加 HeroId 分支即可，蓝图不用重做。
	if (IsDesignTime())
	{
		const int32 I = FMath::Clamp(DesignPreviewHeroIndex, 0,
			static_cast<int32>(UE_ARRAY_COUNT(TB::AllHeroes)) - 1);
		return *TB::AllHeroes[I];
	}

	return TB::Default();
}

UTexture2D* UHexHandPanelWidget::GetPortraitTexture() const
{
	// ⚠️ LoadObject 失败是【静默】的（返回 nullptr → 控件不画图层）。
	//    所以自检里要单独报告贴图有没有拿到，否则"头像是空白"
	//    与"头像图层没画"在画面上完全一样。
	return LoadObject<UTexture2D>(nullptr, ResolveHeroArt().PortraitPath);
}

UTexture2D* UHexHandPanelWidget::GetNameArtTexture() const
{
	return LoadObject<UTexture2D>(nullptr, ResolveHeroArt().NameArtPath);
}

void UHexHandPanelWidget::ApplyHeroArt()
{
	const TB::FHeroUIArt& Art = ResolveHeroArt();

	if (Portrait)
	{
		if (UTexture2D* Tex = GetPortraitTexture())
		{
			Portrait->SetBrushFromTexture(Tex, false);
			// 头像是近正方形（实测 617x611 等），按方形槽位画不会变形
			Portrait->SetDesiredSizeOverride(
				FVector2D(TB::PortraitSize, TB::PortraitSize));
			Portrait->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			UE_LOG(LogHexSpire, Warning,
				TEXT("头像贴图加载失败：%s"), Art.PortraitPath);
			Portrait->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// ── 四张字样：只留当前角色那张可见
	//
	// ⚠️ 用 Collapsed 而不是 Hidden。Hidden 仍然【占位】，
	//    三张隐藏的字样会把可见那张挤偏（而且挤的量取决于
	//    哪张最宽，换角色时位置还会跳）。
	struct FPair { UImage* Img; const TCHAR* Name; };
	const FPair Pairs[] = {
		{ NameArt_Warden,    TB::N::NameArtWarden    },
		{ NameArt_Medium,    TB::N::NameArtMedium    },
		{ NameArt_Scrivener, TB::N::NameArtScrivener },
		{ NameArt_Revenant,  TB::N::NameArtRevenant  },
	};

	for (const FPair& P : Pairs)
	{
		if (!P.Img)
		{
			continue;
		}

		const bool bActive = (FCString::Strcmp(P.Name, Art.NameArtWidget) == 0);

		if (bActive)
		{
			if (UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, Art.NameArtPath))
			{
				// ⚠️ 这里【不设】DesiredSizeOverride：外层 ScaleBox 按
				//    原图比例缩放，写死尺寸会把 0.34~0.52 的比例差异
				//    压成同一个形状，字样就变形了。
				P.Img->SetBrushFromTexture(Tex, false);
				P.Img->SetVisibility(ESlateVisibility::HitTestInvisible);
			}
			else
			{
				UE_LOG(LogHexSpire, Warning,
					TEXT("字样贴图加载失败：%s"), Art.NameArtPath);
				P.Img->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
		else
		{
			P.Img->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void UHexHandPanelWidget::RefreshTopBar(AHexDemoGameMode* Mode)
{
	if (!bCppDrivesTopBar)
	{
		// 蓝图接管：C++ 一个字都不写，否则蓝图里设的值会被每帧覆盖。
		// 但角色资产仍要跟着 HeroId 走 —— 那是数据而非外观。
		ApplyHeroArt();
		return;
	}

	ApplyHeroArt();

	if (HeroHP)
	{
		HeroHP->SetText(GetHeroHPText());
		HeroHP->SetColorAndOpacity(FSlateColor(GetHeroHPColor()));
	}
	if (HPBar)
	{
		HPBar->SetPercent(GetHeroHPPercent());
		HPBar->SetFillColorAndOpacity(GetHeroHPColor());
	}
	if (Corruption)
	{
		Corruption->SetText(GetCorruptionText());
		Corruption->SetColorAndOpacity(FSlateColor(GetCorruptionColor()));
	}
	if (CorruptionEffect)
	{
		CorruptionEffect->SetText(GetCorruptionEffectText());
	}
	if (FloorInfo)
	{
		FloorInfo->SetText(GetFloorText());
	}
	if (DeckInfo)
	{
		DeckInfo->SetText(GetDeckText());
	}
	if (RuneInfo)
	{
		RuneInfo->SetText(GetRuneText());
	}

	// ── 战斗专属的三行：战斗外收掉而不是留空行
	const bool bInBattle = Mode && Mode->IsInBattle();

	auto SetBattleRow = [bInBattle](UTextBlock* T, const FText& Txt)
	{
		if (!T)
		{
			return;
		}
		T->SetText(Txt);
		T->SetVisibility(bInBattle
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	};

	SetBattleRow(RoundNum,   GetRoundText());
	SetBattleRow(PileInfo,   GetPileText());

	// ── 顶栏的体力那一行已被右下角的火苗取代
	//
	// ⚠️ 折叠而不是删控件：GetEnergyText() 与 EnergyText 控件都留着，
	//    因为"◆◇ 文字版"是 UI_Asset_Checklist.md 里明确写的
	//    【等美术出图之前的占位】—— 现在图出了（UI_体力槽），
	//    占位就该收起来，否则同一个读数在屏幕上出现两次，
	//    玩家不知道该看哪个，而两处若不同步（比如只改了一边的算法）
	//    看起来就像有个 bug。
	//
	// ⚠️ 不删是为了留退路：想回到文字版只要把这里改回 SetBattleRow。
	//    删掉控件的话蓝图里那一项也要跟着删，来回折腾。
	if (EnergyText)
	{
		EnergyText->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (StatusText)
	{
		const FText Msg = GetStatusText();
		StatusText->SetText(Msg);
		// 空消息时收掉，免得留一块空白抢视觉
		StatusText->SetVisibility(Msg.IsEmpty()
			? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
}

void UHexHandPanelWidget::RefreshActionArea(AHexDemoGameMode* Mode)
{
	const bool bBattle = Mode && Mode->IsInBattle();

	// ── 整块显隐：地图界面没有回合可结束，也没有体力
	//
	// ⚠️ 折叠【整块】而不是逐个折叠子控件：VerticalBox 里
	//    全部子项 Collapsed 后容器本身仍然占位（一个 0 尺寸的空盒），
	//    这本身无害，但 ActionCol 上要是以后加了背景就会留下一块空板。
	if (ActionCol)
	{
		ActionCol->SetVisibility(bBattle
			? ESlateVisibility::SelfHitTestInvisible
			: ESlateVisibility::Collapsed);
	}

	// ── 点击接线
	//
	// ⚠️ 必须在下面那个 `if (!bBattle) return;` 【之前】，
	//    而且不能跟着战斗状态走。接线是【一次性的线路】而不是外观：
	//    放在战斗分支里的话，从地图进战斗的第一帧顺序若反过来
	//    （自检先跑），就会报"点击未接线"—— 那是假警报。
	//    更实际的理由是：接线与"现在能不能按"是两件事，
	//    后者由 SetIsEnabled 表达，混在一起会让"按钮点不动"
	//    同时有两个可能原因，排查要多绕一圈。
	if (EndTurnButton)
	{
		// ⚠️ 绑定放在刷新里（每帧查一次）而不是构造函数里：
		//    蓝图路径下按钮是【被 BindWidget 绑过来的】，
		//    构造函数跑的时候 EndTurnButton 还是 nullptr ——
		//    在那里绑等于永远不绑，而且不报错：按钮显示正常、点了没反应。
		//
		// ⚠️ 用 IsAlreadyBound 挡住重复绑定。OnClicked 是【多播】委托，
		//    每帧 AddDynamic 会堆出成千上万份，一次点击调用几千次
		//    EndTurn —— 表现是"点一下直接跳过好几个回合"。
		if (!EndTurnButton->OnClicked.IsAlreadyBound(
			this, &UHexHandPanelWidget::OnEndTurnClicked))
		{
			EndTurnButton->OnClicked.AddDynamic(
				this, &UHexHandPanelWidget::OnEndTurnClicked);
		}

		// 战斗结束后（胜/负）转 Disabled 那套染色 ——
		// 那时该按的是 Enter 结算，不是结束回合。
		EndTurnButton->SetIsEnabled(CanEndTurn());
	}

	if (!bBattle)
	{
		return;
	}

	// ── 体力火苗
	if (EnergyRow)
	{
		const FHexBattleState* BS = Mode->GetBattleState();
		const int32 Max = BS ? FHexRuleBook::EnergyMax(*BS) : 0;
		const int32 Cur = BS ? BS->Energy : 0;

		for (int32 I = 0; I < Max; ++I)
		{
			UImage* Pip = GetOrCreateEnergyPip(I);
			if (!Pip)
			{
				continue;
			}

			Pip->SetVisibility(ESlateVisibility::HitTestInvisible);

			// ⚠️ 用【同一张贴图染色】区分已用/未用，不是换贴图 ——
			//    目前美术只给了一张火苗图，没有空态图。
			//    Alpha 压到 0.28 而不是整个隐藏：保留火苗轮廓，
			//    让"体力上限是几"仍然一眼可数（§13.2 的要求）。
			//    隐藏掉的话玩家只能看到剩余量，看不出上限。
			Pip->SetColorAndOpacity(I < Cur
				? TB::ColEnergyPipOn : TB::ColEnergyPipOff);
		}

		// 上限变小时（比如卸掉加体力的符文）把多出来的折叠掉
		for (int32 I = Max; I < EnergyPips.Num(); ++I)
		{
			if (EnergyPips[I])
			{
				EnergyPips[I]->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}

	if (EnergyCount)
	{
		EnergyCount->SetText(GetEnergyCountText());
	}
}

void UHexHandPanelWidget::RefreshFromGameMode(AHexDemoGameMode* Mode)
{
	if (!Mode)
	{
		return;
	}

	const FHexBattleState* BS = Mode->GetBattleState();
	FHexBattleFlow* Flow = Mode->GetBattleFlow();
	const bool bBattle = (BS && Flow && Mode->IsInBattle());

	// ══════════════════════════════════════════════════════════════
	// 顶栏先刷，而且【无条件】刷
	// ══════════════════════════════════════════════════════════════
	// ⚠️ 必须在下面的折叠逻辑【之前】，而且不能跟着战斗状态走。
	//    顶栏在地图界面也要显示（HP、腐蚀度、卡组、层数都是跨战斗的
	//    信息，那正是玩家在地图上做"再探一间还是收手"决策的依据）。
	//
	// ⚠️ 这里曾经是 SetVisibility(Collapsed) 把【整个控件】折叠掉，
	//    那是顶栏还在 Canvas 上时的写法。现在顶栏搬进来了，
	//    照抄那套会让顶栏一出战斗就整块消失 —— 而且不报错。
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	RefreshTopBar(Mode);

	if (TopBar)
	{
		TopBar->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}

	// ⚠️ 顶栏自检必须在这里、而且【与战斗状态无关】。
	//    下面那个 LogSelfCheckOnce 在"非战斗直接 return"的后面，
	//    所以它只在战斗中跑得到 —— 而"顶栏在地图界面消失"恰恰是
	//    本次改动最可能的回归（顶栏原先跟着整块控件一起折叠）。
	//    只在战斗中自检的话，这个回归永远测不出来。
	LogTopBarCheckOnce(bBattle);

	// ⚠️ 右下角操作区也必须在下面那些 return 【之前】刷。
	//    它自己会在非战斗时整块折叠，但那个折叠得真的被执行到 ——
	//    放在 return 之后的话，从战斗回到地图时这块会带着
	//    上一场战斗的体力数留在屏幕上（而且不报错）。
	RefreshActionArea(Mode);

	// ⚠️ HandBox / FixedBox 只要求【至少有一个】存在。
	//    以前这里是 && 全都要有，改成 WBP 可绑定后那会让"只摆了手牌区、
	//    没摆固定卡区"的 WBP 整块界面不刷新 —— 一张卡都不显示。
	if (!HandBox && !FixedBox)
	{
		return;
	}

	// 不在战斗中：只收掉【卡牌容器】，顶栏留着
	if (!bBattle)
	{
		if (HandBox)
		{
			HandBox->SetVisibility(ESlateVisibility::Collapsed);
		}
		if (FixedBox)
		{
			FixedBox->SetVisibility(ESlateVisibility::Collapsed);
		}
		if (FixedTitle)
		{
			FixedTitle->SetVisibility(ESlateVisibility::Collapsed);
		}
		return;
	}

	if (HandBox)
	{
		HandBox->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	if (FixedBox)
	{
		FixedBox->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}

	const FHexUnit* Hero = BS->GetHero();
	const int32 SelectedUid = Mode->GetSelectedCardUid();

	// ── 手牌
	if (HandBox)
	{
		const TArray<FHexCardInstance>& Hand = BS->Piles.GetHand();
		int32 Used = 0;

		for (int32 I = 0; I < Hand.Num(); ++I)
		{
			const FHexCardInstance& Inst = Hand[I];
			const FHexCardData* Card = FHexContentLibrary::FindCard(Inst.CardId);
			if (!Card)
			{
				// 卡定义找不到说明 id 打错或配表出错 —— 跳过但留日志，
				// 静默少一张牌会让所有平衡数据失真
				UE_LOG(LogHexSpire, Warning,
					TEXT("手牌里有未知卡 id：%s"), *Inst.CardId.ToString());
				continue;
			}

			UHexCardWidget* W = GetOrCreateCard(HandBox, HandCards, Used, ResolveCardClass());
			if (!W)
			{
				continue;
			}

			FHexCardView V = FHexCardView::Make(
				*Card, Hero,
				Flow->GetCardCost(Inst.Uid),
				Flow->CanPlayCard(Inst.Uid),
				SelectedUid == Inst.Uid);
			// 数字键 1..9 对应前 9 张
			V.HotkeyNumber = (Used < 9) ? (Used + 1) : 0;

			W->SetCardUid(Inst.Uid);
			W->SetCardView(V);
			W->SetVisibility(ESlateVisibility::Visible);

			// ⚠️ 走 SetBaseScale 而不是 SetRenderScale：
			//    选中上浮也在写 RenderTransform，两边各写一次会互相覆盖 ——
			//    表现是卡牌选中后浮起一帧又被这里按回原位，反复抖动。
			//    SetBaseScale 把缩放交给控件自己与上浮合成。
			W->SetBaseScale(1.0f);

			if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(W->Slot))
			{
				S->SetPadding(FMargin(4.0f, 0.0f));
				S->SetVerticalAlignment(VAlign_Bottom);
			}

			++Used;
		}

		HideExtra(HandCards, Used);
	}

	// ── 固定卡
	if (FixedBox)
	{
		const TArray<FHexCardInstance>& Fixed = BS->FixedCards;
		int32 Used = 0;

		for (int32 I = 0; I < Fixed.Num(); ++I)
		{
			const FHexCardInstance& Inst = Fixed[I];
			const FHexCardData* Card = FHexContentLibrary::FindCard(Inst.CardId);
			if (!Card)
			{
				continue;
			}

			UHexCardWidget* W = GetOrCreateCard(FixedBox, FixedCards, Used, ResolveFixedCardClass());
			if (!W)
			{
				continue;
			}

			FHexCardView V = FHexCardView::Make(
				*Card, Hero,
				Flow->GetCardCost(Inst.Uid),
				Flow->CanPlayCard(Inst.Uid),
				SelectedUid == Inst.Uid);

			// Q/W/E —— 刻意不与手牌共用数字键：
			// 共用的话手牌张数一变，固定卡键位就跟着漂移，
			// 而固定卡的全部价值就在于"永远在那儿、永远是同一个键"。
			if (Used < 3)
			{
				V.HotkeyLabel = GFixedKeys[Used];
			}

			W->SetCardUid(Inst.Uid);
			W->SetCardView(V);
			W->SetVisibility(ESlateVisibility::Visible);

			// ⚠️ 横版卡【不再缩放】。
			//    以前固定卡是竖版卡 SetBaseScale(0.72) 缩小出来的 ——
			//    那只是变小，比例仍是竖的，套上横版贴图会整张拉伸变形。
			//    现在它本身就是 196x100 的横版控件，按原尺寸画即可。
			W->SetBaseScale(1.0f);

			if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(W->Slot))
			{
				// ⚠️ 那个"负下边距"的 hack 已删掉。
				//    它是为了吃掉 SetRenderScale 留下的空隙而存在的
				//    （渲染缩放不改变布局占位，0.72 缩放后每张卡下方
				//      会空出 28% 高度）。现在不缩放了，直接用正常间距。
				S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
				S->SetHorizontalAlignment(HAlign_Left);
			}

			++Used;
		}

		HideExtra(FixedCards, Used);

		// WBP 里可能没摆这个标题
		if (FixedTitle)
		{
			FixedTitle->SetVisibility(Used > 0
				? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		}
	}

	LogSelfCheckOnce();
}

TArray<FString> UHexHandPanelWidget::GetUnboundTopBarNames() const
{
	// ⚠️ 逐个列出而不是只数个数：知道"少了 3 个"没用，
	//    要知道少的是哪几个才能去蓝图里改名字。
	TArray<FString> Missing;

	auto Check = [&Missing](const UWidget* W, const TCHAR* Name)
	{
		if (!W)
		{
			Missing.Add(Name);
		}
	};

	Check(TopBar,            TB::N::TopBar);
	Check(Portrait,          TB::N::Portrait);
	Check(NameArt_Warden,    TB::N::NameArtWarden);
	Check(NameArt_Medium,    TB::N::NameArtMedium);
	Check(NameArt_Scrivener, TB::N::NameArtScrivener);
	Check(NameArt_Revenant,  TB::N::NameArtRevenant);
	Check(HeroHP,            TB::N::HeroHP);
	Check(HPBar,             TB::N::HPBar);
	Check(Corruption,        TB::N::Corruption);
	Check(DeckInfo,          TB::N::DeckInfo);
	Check(RuneInfo,          TB::N::RuneInfo);
	Check(RoundNum,          TB::N::RoundNum);
	Check(EnergyText,        TB::N::EnergyText);
	Check(PileInfo,          TB::N::PileInfo);
	Check(StatusText,        TB::N::StatusText);

	// 右下角操作区一并查 —— 它与顶栏同一类失败模式（名字对不上就永远空）
	Check(ActionCol,         TB::N::ActionCol);
	Check(EndTurnButton,     TB::N::EndTurnButton);
	Check(EnergyRow,         TB::N::EnergyRow);
	Check(EnergyCount,       TB::N::EnergyCount);

	return Missing;
}

void UHexHandPanelWidget::LogSelfCheckOnce()
{
	// ══════════════════════════════════════════════════════════════
	// 为什么需要这个自检
	// ══════════════════════════════════════════════════════════════
	// 表现层没法用单元测试覆盖，而 UMG 的失败几乎全是【静默】的：
	//   · 贴图路径错 → 卡面纯色块，不报错
	//   · 控件没加进视口 → 什么都不显示，不报错
	//   · 字体缺中文字形 → 文字画成空白，不报错
	//
	// 更麻烦的是截图也验证不了：-dumpmovie 只抓场景渲染，
	// 【不包含 Slate 层】—— 卡牌没显示和"截图抓不到卡牌"
	// 在图片上完全一样，看图会得出错误结论。
	//
	// 所以改成让控件自己把关键事实写进日志，一条命令行即可验证：
	//   UnrealEditor-Cmd.exe <uproject> -game -HexAutoRoom -nullrhi ...
	if (bSelfCheckLogged)
	{
		return;
	}

	// ⚠️ 必须等几帧再查。GetCachedGeometry 在控件【首次完成布局之前】
	//    返回全零 —— 第一帧就查会得到"尺寸为 0"的假警报，
	//    而假警报会训练人忽略这条日志，比没有日志更糟。
	if (++RefreshCount < 3)
	{
		return;
	}
	bSelfCheckLogged = true;

	int32 HandVisible = 0;
	for (UHexCardWidget* W : HandCards)
	{
		if (W && W->GetVisibility() != ESlateVisibility::Collapsed)
		{
			++HandVisible;
		}
	}

	int32 FixedVisible = 0;
	for (UHexCardWidget* W : FixedCards)
	{
		if (W && W->GetVisibility() != ESlateVisibility::Collapsed)
		{
			++FixedVisible;
		}
	}

	// ⚠️ 要报告【实际用了哪个类】建卡。
	//    "我建了 WBP 但没生效"是这套自动发现最容易出的问题
	//    （路径写错、漏了 _C 后缀、父类没选对），而回退是静默的 ——
	//    界面照常显示，只是版式还是 C++ 那套。没有这一行就只能靠猜。
	UE_LOG(LogHexSpire, Display,
		TEXT("[自检] 手牌控件：手牌 %d 张 · 固定卡 %d 张 · 已在视口=%s · 卡牌类=%s"),
		HandVisible, FixedVisible, IsInViewport() ? TEXT("是") : TEXT("否"),
		ResolvedCardClass ? *ResolvedCardClass->GetName() : TEXT("未解析"));

	// ⚠️ 必须验证【渲染尺寸】而不只是控件数量。
	//    漏掉 WidgetTree->RootWidget 赋值时，控件对象全部存在、
	//    贴图全部加载成功、数量也对 —— 但每个控件的 Slate 表示是
	//    SNullWidget，屏幕上一张卡都没有。
	//    只查控件对象的自检会全部通过，反而给出虚假的安全感。
	//    实际画出来的东西必然有非零尺寸，这才是可信的判据。
	for (UHexCardWidget* W : FixedCards)
	{
		if (!W || W->GetVisibility() == ESlateVisibility::Collapsed)
		{
			continue;
		}
		const FVector2D Size = W->GetCachedGeometry().GetLocalSize();
		const FVector2D Abs = W->GetCachedGeometry().GetAbsoluteSize();
		if (Size.IsNearlyZero())
		{
			UE_LOG(LogHexSpire, Error,
				TEXT("[自检] 卡牌控件渲染尺寸为 0 —— 控件建了但没画出来。"
					 "最可能的原因是 RebuildWidget 里漏了 WidgetTree->RootWidget 赋值。"));
		}
		else
		{
			UE_LOG(LogHexSpire, Display,
				TEXT("[自检]   渲染尺寸 局部=%.0fx%.0f 绝对=%.0fx%.0f"),
				Size.X, Size.Y, Abs.X, Abs.Y);
		}
		break;
	}

	// ── 字体是否真的能画出中文
	//
	// ⚠️ 字体缺字形时 UMG 画的是【空白】而不是方块，
	//    所以"卡名不见了"看起来像布局 bug，排查一定跑偏。
	//    这里把实际生效的字体对象打出来，才能分清
	//    "字体没配上"和"配上了但缺中文字形"。
	{
		UFont* Medium = GEngine ? GEngine->GetMediumFont() : nullptr;
		UE_LOG(LogHexSpire, Display,
			TEXT("[自检] 引擎中号字体=%s"),
			Medium ? *Medium->GetPathName() : TEXT("【空】"));

		// 再报告卡面文字控件实际拿到的 FSlateFontInfo。
		// FontObject 为空 + FontMaterial 为空时，Slate 会回退到
		// FCoreStyle 的默认字体（Roboto，不含中文字形）。
		for (UHexCardWidget* W : HandCards)
		{
			if (!W || W->GetVisibility() == ESlateVisibility::Collapsed)
			{
				continue;
			}
			UE_LOG(LogHexSpire, Display, TEXT("[自检]   %s"), *W->DescribeFont());
			break;
		}
	}

	// ── 控件蓝图的绑定是否真的生效
	//
	// ⚠️ 用了控件蓝图时这条【必查】。蓝图里控件改了名（或美术新建了
	//    一个控件却没按约定命名），对应字段就永远填不进数据，
	//    而这在编译期只有一条 Note、运行时完全静默。
	for (UHexCardWidget* W : HandCards)
	{
		if (!W || W->GetVisibility() == ESlateVisibility::Collapsed)
		{
			continue;
		}

		const TArray<FString> Unbound = W->GetUnboundWidgetNames();
		if (Unbound.Num() == 0)
		{
			UE_LOG(LogHexSpire, Display,
				TEXT("[自检]   卡面控件全部绑定成功"));
		}
		else
		{
			// 不是 Error：少一个可选控件（比如美术故意不要射程）是合法的。
			// 但必须显眼，因为"我改了蓝图但那一项不显示"就是这个原因。
			UE_LOG(LogHexSpire, Warning,
				TEXT("[自检]   卡面有 %d 项未绑定：%s。"
					 "若非有意省略，请检查控件蓝图里的控件名是否与 C++ 属性同名"),
				Unbound.Num(), *FString::Join(Unbound, TEXT(", ")));
		}
		break;
	}

	// ── 每张手牌的实际几何（排列是否整齐）
	//
	// ⚠️ 必须报告【绝对】几何而不是局部尺寸：
	//    局部尺寸恒为 168x232（SizeBox 写死的），看不出问题；
	//    而 RenderTransform 的缩放只体现在绝对尺寸上。
	//    "一大一小"这类问题只有比较绝对尺寸才能定位。
	for (int32 I = 0; I < HandCards.Num(); ++I)
	{
		UHexCardWidget* W = HandCards[I];
		if (!W || W->GetVisibility() == ESlateVisibility::Collapsed)
		{
			continue;
		}
		const FGeometry& G = W->GetCachedGeometry();
		const FVector2D Abs = G.GetAbsoluteSize();
		const FVector2D Pos = G.GetAbsolutePosition();
		UE_LOG(LogHexSpire, Display,
			TEXT("[自检]   手牌#%d《%s》绝对 %.0fx%.0f @ (%.0f,%.0f) 选中=%s"),
			I, *W->GetCardView().DisplayName, Abs.X, Abs.Y, Pos.X, Pos.Y,
			W->GetCardView().bSelected ? TEXT("是") : TEXT("否"));
	}

	// ── 上浮（选中表现）是否真的在动
	//
	// ⚠️ 只报告"有没有选中的卡"是不够的：bSelected 为 true 但上浮
	//    恒为 0 恰恰是最常见的失败（Tick 没开 / transform 被覆盖）。
	//    所以要打【实际位移量】。
	for (UHexCardWidget* W : HandCards)
	{
		if (!W || W->GetVisibility() == ESlateVisibility::Collapsed
			|| !W->GetCardView().bSelected)
		{
			continue;
		}

		const float Lift = W->GetCurrentLift();
		if (Lift <= 0.01f)
		{
			// 选中了却没浮起来。可能是 TickFrequency=Never，
			// 或有别处又写了 RenderTransform 把位移覆盖掉。
			UE_LOG(LogHexSpire, Warning,
				TEXT("[自检] 卡牌《%s》已选中但上浮为 0 —— "
					 "检查 NativeTick 是否被调用、RenderTransform 是否被外部覆盖"),
				*W->GetCardView().DisplayName);
		}
		else
		{
			UE_LOG(LogHexSpire, Display,
				TEXT("[自检]   选中《%s》上浮 %.1f px（目标 %.0f）"),
				*W->GetCardView().DisplayName, Lift, UHexCardWidget::SelectLift);
		}
		break;
	}

	// 逐张报告贴图是否真的加载到了 —— 这是"卡面是不是纯色块"的唯一判据。
	//
	// ⚠️ 同时报告【染色】与【类型文字】。去掉整卡染色后，
	//    "卡面还是彩色方块"的唯一可能原因就是某处又返回了非白的 Tint；
	//    而类型信息如果既没图标又没文字，玩家就完全无从判断卡的类型 ——
	//    这两条都不会导致报错，只能靠日志看出来。
	for (UHexCardWidget* W : FixedCards)
	{
		if (!W || W->GetVisibility() == ESlateVisibility::Collapsed)
		{
			continue;
		}
		const FHexCardView& V = W->GetCardView();
		const FLinearColor Tint = HexCardArt::GetFrameTint(V.CardId, V.CardType);

		UE_LOG(LogHexSpire, Display,
			TEXT("[自检]   固定卡《%s》费用=%d 卡框=%s 图标=%s 类型=%s 染色=%s 描述=\"%s\""),
			*V.DisplayName, V.Cost,
			HexCardArt::GetFrame(V.CardId, V.Rarity) ? TEXT("有") : TEXT("缺"),
			HexCardArt::GetTypeIcon(V.CardId, V.CardType) ? TEXT("有") : TEXT("缺"),
			*HexCardArt::GetTypeName(V.CardType),
			Tint.Equals(FLinearColor::White) ? TEXT("无（按原图）") : TEXT("有"),
			*V.Description);
	}

}

void UHexHandPanelWidget::LogTopBarCheckOnce(bool bInBattle)
{
	// ⚠️ 与卡面自检分开，而且【不要求在战斗中】—— 顶栏在地图界面也在，
	//    只在战斗里自检会漏掉“顶栏一出战斗就消失”这个回归。
	if (bTopBarCheckLogged)
	{
		return;
	}

	// ⚠️ 必须等布局完成。GetCachedGeometry 在首次布局前返回全零，
	//    第一帧就查会得到“尺寸为 0”的假警报，而假警报会训练人
	//    忽略这条日志，比没有日志更糟。
	if (++TopBarRefreshCount < 3)
	{
		return;
	}
	bTopBarCheckLogged = true;

	UE_LOG(LogHexSpire, Display,
		TEXT("[自检] 顶栏自检（当前%s战斗中）"),
		bInBattle ? TEXT("在") : TEXT("不在"));

	// ══════════════════════════════════════════════════════════════
	// 顶栏自检
	// ══════════════════════════════════════════════════════════════
	// ⚠️ 顶栏刚从 Canvas 搬到 UMG，失败模式全是静默的：
	//      · 贴图路径错（资产名有 Revanat/Revenat 拼写不一致）→ 不画图层
	//      · 控件名对不上 BindWidgetOptional → 那一项永远空
	//      · 顶栏尺寸为 0（漏了锚点或 RootWidget）→ 什么都不显示
	//    三种都不报错，而截图也验证不了（-dumpmovie 不含 Slate 层）。
	const TArray<FString> Unbound = GetUnboundTopBarNames();
	if (Unbound.Num() == 0)
	{
		UE_LOG(LogHexSpire, Display,
			TEXT("[自检] 顶栏控件全部绑定成功"));
	}
	else
	{
		// 不是 Error：美术故意删掉某一项（比如不要符文行）是合法的。
		// 但必须显眼 —— "我改了蓝图但那一项不显示"就是这个原因。
		UE_LOG(LogHexSpire, Warning,
			TEXT("[自检] 顶栏有 %d 项未绑定：%s。"
				 "若非有意省略，请检查控件蓝图里的控件名是否与 C++ 属性同名"),
			Unbound.Num(), *FString::Join(Unbound, TEXT(", ")));
	}

	// ── 贴图是否真的加载到（"头像是空白"与"图层没画"在画面上一样）
	const TB::FHeroUIArt& Art = ResolveHeroArt();
	UTexture2D* PortraitTex = GetPortraitTexture();
	UTexture2D* NameArtTex  = GetNameArtTexture();

	UE_LOG(LogHexSpire, Display,
		TEXT("[自检] 顶栏贴图：头像=%s 字样=%s"),
		PortraitTex ? TEXT("有") : TEXT("【缺】"),
		NameArtTex  ? TEXT("有") : TEXT("【缺】"));

	if (!PortraitTex)
	{
		UE_LOG(LogHexSpire, Error,
			TEXT("[自检] 头像贴图加载失败：%s —— 检查资产名"
				 "（注意 Revenant 的头像叫 UI_Revanat_头像）"),
			Art.PortraitPath);
	}
	if (!NameArtTex)
	{
		UE_LOG(LogHexSpire, Error,
			TEXT("[自检] 字样贴图加载失败：%s —— 检查资产名"
				 "（注意字样叫 UI_Revenat_字样，与头像拼写不同）"),
			Art.NameArtPath);
	}
	else
	{
		// 报告实际比例，确认 ScaleBox 有没有把它压变形
		UE_LOG(LogHexSpire, Display,
			TEXT("[自检]   字样原始尺寸 %dx%d（比例 %.3f）—— "
				 "ScaleBox 应按此比例缩放，不应被压成方形"),
			NameArtTex->GetSizeX(), NameArtTex->GetSizeY(),
			NameArtTex->GetSizeY() > 0
				? static_cast<float>(NameArtTex->GetSizeX())
					/ NameArtTex->GetSizeY() : 0.0f);
	}

	// ── 顶栏是否真的画出来了
	//
	// ⚠️ 必须查渲染尺寸而不只是控件是否存在：控件对象全在、
	//    贴图全加载成功、绑定全通过 —— 但漏了锚点或 RootWidget 时
	//    Slate 表示是 SNullWidget，屏幕上什么都没有。
	if (TopBar)
	{
		const FVector2D Size = TopBar->GetCachedGeometry().GetLocalSize();
		if (Size.IsNearlyZero())
		{
			UE_LOG(LogHexSpire, Error,
				TEXT("[自检] 顶栏渲染尺寸为 0 —— 控件建了但没画出来。"
					 "检查 TopBar 的 CanvasPanelSlot 锚点与 RootWidget 赋值"));
		}
		else
		{
			UE_LOG(LogHexSpire, Display,
				TEXT("[自检]   顶栏渲染尺寸 %.0fx%.0f（高度常量 %.0f）"),
				Size.X, Size.Y, TB::BarHeight);
		}
	}

	// ── 状态提示是否还有显示点
	//
	// ⚠️ 这条专门防一类回归：GetStatusMessage() 原先【唯一】的
	//    绘制点在已删除的 DrawTopBar 里。StatusText 没绑上的话，
	//    全部操作反馈（阵亡/胜利/体力不足/目标不合法）会无声消失。
	if (!StatusText)
	{
		UE_LOG(LogHexSpire, Warning,
			TEXT("[自检] StatusText 未绑定 —— 状态提示将【完全不显示】。"
				 "它是 GetStatusMessage() 唯一的显示点"
				 "（原先画在已删除的 DrawTopBar 里）"));
	}

	// ══════════════════════════════════════════════════════════════
	// 右下角操作区自检
	// ══════════════════════════════════════════════════════════════
	// ⚠️ 与顶栏同一类失败模式，而且多了两个【只有右下角才有】的坑：
	//      · AutoSize 没开 → Slate 拿 Offset 当尺寸用，
	//        104px 的按钮被挤成 20px（不报错，只是"按钮好像没出来"）
	//      · 按钮建成了 UImage → 图正常显示但收不到点击
	//    所以这里查的是【渲染尺寸】而不只是控件是否存在。
	UE_LOG(LogHexSpire, Display, TEXT("[自检] 右下角操作区自检"));

	UTexture2D* EndTurnTex = LoadObject<UTexture2D>(nullptr, TB::EndTurnPath);
	UTexture2D* PipTex     = LoadObject<UTexture2D>(nullptr, TB::EnergyPipPath);

	UE_LOG(LogHexSpire, Display,
		TEXT("[自检]   贴图：结束回合=%s 体力火苗=%s"),
		EndTurnTex ? TEXT("有") : TEXT("【缺】"),
		PipTex     ? TEXT("有") : TEXT("【缺】"));

	if (!EndTurnTex)
	{
		UE_LOG(LogHexSpire, Error,
			TEXT("[自检] 结束回合贴图加载失败：%s —— 检查资产名"
				 "（注意它带 _3 后缀）。按钮会回退成引擎默认灰方块"),
			TB::EndTurnPath);
	}
	if (!PipTex)
	{
		UE_LOG(LogHexSpire, Error,
			TEXT("[自检] 体力火苗贴图加载失败：%s —— 检查资产名"),
			TB::EnergyPipPath);
	}

	// ⚠️ 尺寸检查【只在战斗中】做。这一块在地图界面是【故意折叠】的
	//    （地图上没有回合可结束），而折叠控件的 CachedGeometry 就是全零 ——
	//    不分战斗状态地报错等于每次进地图都喊一次狼来了，
	//    而假警报会训练人忽略整条自检，比没有自检更糟。
	//    顶栏那边不需要这个判断，因为顶栏在两个界面都显示。
	if (ActionCol && bInBattle)
	{
		const FVector2D Size = ActionCol->GetCachedGeometry().GetLocalSize();
		if (Size.IsNearlyZero())
		{
			UE_LOG(LogHexSpire, Error,
				TEXT("[自检] 右下角操作区渲染尺寸为 0 —— 控件建了但没画出来。"
					 "检查 ActionCol 的 CanvasPanelSlot 是否开了 AutoSize"
					 "（不开的话 Offset 的 Right/Bottom 会被当成尺寸用）"));
		}
		else
		{
			// 按钮 104 + 火苗一行 ~30 + 间距 → 高度应在 130 上下。
			// 明显小于按钮边长就说明尺寸被挤掉了。
			UE_LOG(LogHexSpire, Display,
				TEXT("[自检]   操作区渲染尺寸 %.0fx%.0f（按钮边长常量 %.0f）"),
				Size.X, Size.Y, TB::EndTurnSize);

			if (Size.X < TB::EndTurnSize - 1.0f)
			{
				UE_LOG(LogHexSpire, Warning,
					TEXT("[自检] 操作区宽度 %.0f 小于按钮边长 %.0f —— "
						 "按钮很可能被槽位挤小了，检查 AutoSize 与 SizeBox"),
					Size.X, TB::EndTurnSize);
			}
		}
	}

	// ⚠️ 专门查类型：蓝图里把按钮摆成 UImage 是很自然的手误
	//    （它就是一张图）。那样 BindWidgetOptional 按名字匹配会失败
	//    （类型不符 → 绑定为空），于是 EndTurnButton 是 nullptr——
	//    图在屏幕上、点了没反应、一行不报错。
	if (!EndTurnButton)
	{
		UE_LOG(LogHexSpire, Warning,
			TEXT("[自检] EndTurnButton 未绑定 —— 结束回合按钮【点不动】"
				 "（空格键仍可用）。蓝图里这个控件必须是 Button 类型，"
				 "摆成 Image 的话图能显示但收不到点击"));
	}
	else
	{
		// "可按" 在地图界面本就是否（没有回合可结束），所以顺带打出
		// 当前是否在战斗 —— 否则读日志的人会把正常状态当成 bug。
		UE_LOG(LogHexSpire, Display,
			TEXT("[自检]   结束回合按钮：类型=%s 点击已接线=%s 可按=%s（当前%s战斗中）"),
			*EndTurnButton->GetClass()->GetName(),
			EndTurnButton->OnClicked.IsBound() ? TEXT("是") : TEXT("【否】"),
			CanEndTurn() ? TEXT("是") : TEXT("否"),
			bInBattle ? TEXT("在") : TEXT("不在"));

		// ⚠️ 这条与"可按"分开报，而且只在战斗中才算异常：
		//    接线断了的表现是"按钮在那儿、点了没反应"，
		//    而那与"按钮此刻被禁用"在画面上完全一样。
		if (!EndTurnButton->OnClicked.IsBound())
		{
			UE_LOG(LogHexSpire, Warning,
				TEXT("[自检] 结束回合按钮的点击【未接线】—— 点了不会有反应"
					 "（空格键仍可用）。检查 RefreshActionArea 里的 AddDynamic"));
		}
	}

	// 火苗个数在地图界面是 0（整块折叠，不建火苗），那是正常的。
	UE_LOG(LogHexSpire, Display,
		TEXT("[自检]   体力火苗已建 %d 个%s"), EnergyPips.Num(),
		bInBattle ? TEXT("") : TEXT("（不在战斗中，这块整块折叠）"));

	// ══════════════════════════════════════════════════════════════
	// 先花掉一点体力（只在 -HexAutoRoom 下）
	// ══════════════════════════════════════════════════════════════
	// ⚠️ 不打一张牌的话体力恒是满的，于是下面那条亮暗检查【永远全亮】——
	//    "染色没生效"和"此刻体力真的是满的"在日志里完全一样，
	//    等于这条自检测不到它要测的东西。
	//    打一张牌之后才有"亮几个暗几个"可对照。
	if (bInBattle && FParse::Param(FCommandLine::Get(), TEXT("HexAutoRoom")))
	{
		if (AHexDemoGameMode* Mode = GetMode())
		{
			const int32 Uid = Mode->GetSelectedCardUid();
			const TArray<FIntVector>& Targets = Mode->GetLegalTargets();

			// 自检开关已经替我们选好了第一张手牌（见 StartPlay）。
			// 没有合法目标时跳过 —— 那是卡本身的问题，不在本条自检范围内。
			if (Uid != 0 && Targets.Num() > 0)
			{
				Mode->PlayCard(Uid, Targets[0]);

				// ⚠️ 打完牌要重刷一次，否则火苗还是上一帧的染色 ——
				//    自检是在 RefreshActionArea 之后跑的，
				//    这里改了逻辑状态就得把表现同步过来再读。
				RefreshActionArea(Mode);
			}
		}
	}

	// ── 火苗的亮/暗是否真的跟着体力走
	//
	// ⚠️ 只报个数不够：已用/未用是靠【同一张贴图染不同色】区分的
	//    （没有空态贴图），而"染色没生效"的表现是五个火苗一样亮 ——
	//    玩家看不出还剩几点体力，而这不报错。
	//    所以把每个火苗的实际 Alpha 打出来，和当前体力对照。
	if (bInBattle && EnergyPips.Num() > 0)
	{
		const AHexDemoGameMode* Mode = GetMode();
		const FHexBattleState* BS = Mode ? Mode->GetBattleState() : nullptr;

		FString Pattern;
		for (const UImage* Pip : EnergyPips)
		{
			if (!Pip)
			{
				Pattern += TEXT("?");
				continue;
			}
			// 亮 = 未消耗，暗 = 已消耗
			Pattern += (Pip->GetColorAndOpacity().A > 0.5f)
				? TEXT("亮") : TEXT("暗");
		}

		UE_LOG(LogHexSpire, Display,
			TEXT("[自检]   火苗亮暗 [%s]（当前体力 %d/%d —— 亮的个数应等于当前体力）"),
			*Pattern, BS ? BS->Energy : -1,
			BS ? FHexRuleBook::EnergyMax(*BS) : -1);
	}

	// ══════════════════════════════════════════════════════════════
	// 真的点一下（只在 -HexAutoRoom 下）
	// ══════════════════════════════════════════════════════════════
	// ⚠️ 上面那条 "点击已接线=是" 只证明委托【非空】，不证明它接到了
	//    正确的函数上。绑错对象、绑到别的 UFUNCTION、或者
	//    OnEndTurnClicked 里被某个判空提前 return —— 三种情况下
	//    IsBound() 都是 true，而按钮点了没反应。
	//
	// ⚠️ 只在自检开关下做，因为它【真的会结束一个回合】。
	//    没有这个开关的话，玩家每局第三帧会被白扣一个回合。
	//    判据用回合数而不是"函数有没有被调用"：后者可以被
	//    一个提前 return 骗过去，回合数变了才说明逻辑层真的动了。
	if (bInBattle && EndTurnButton
		&& FParse::Param(FCommandLine::Get(), TEXT("HexAutoRoom")))
	{
		AHexDemoGameMode* Mode = GetMode();
		const FHexBattleState* BS = Mode ? Mode->GetBattleState() : nullptr;

		if (BS)
		{
			// ⚠️ 是 RoundNumber 而不是 Round —— FHexBattleState 上没有
			//    Round 字段（那个在 FHexRuleViolation 上）。名字相近，
			//    拿错的话编译就报错，不至于静默，但别在这里绕弯。
			const int32 Before = BS->RoundNumber;

			// 走 Slate 的广播而不是直接调 OnEndTurnClicked() ——
			// 直接调会跳过"委托到底连到谁"这一段，那正是要验的东西。
			EndTurnButton->OnClicked.Broadcast();

			const int32 After = Mode->GetBattleState()
				? Mode->GetBattleState()->RoundNumber : Before;

			if (After != Before)
			{
				UE_LOG(LogHexSpire, Display,
					TEXT("[自检]   模拟点击生效：回合 %d → %d"), Before, After);
			}
			else
			{
				// 战斗当场结束（最后一击）时回合数不变是合法的，
				// 所以把结束状态一起打出来，免得被当成接线坏了。
				UE_LOG(LogHexSpire, Warning,
					TEXT("[自检] 模拟点击后回合数没变（仍 %d，战斗已结束=%s）"
						 "—— 若战斗未结束则说明点击没有真正走到 EndTurn"),
					Before, Mode->IsBattleOver() ? TEXT("是") : TEXT("否"));
			}
		}
	}
}
