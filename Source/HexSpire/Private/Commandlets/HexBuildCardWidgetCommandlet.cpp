// Copyright Hex Spire. All Rights Reserved.

#include "Commandlets/HexBuildCardWidgetCommandlet.h"

#include "UI/HexCardLayout.h"
#include "UI/HexCardWidget.h"
#include "UI/HexCardWidgetWide.h"
#include "UI/HexHandPanelWidget.h"
#include "HexSpire.h"

#if WITH_EDITOR

#include "WidgetBlueprint.h"
#include "WidgetBlueprintOperationUtils.h"
#include "Blueprint/WidgetTree.h"

#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"

#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

namespace L = HexCardLayout;

namespace
{
	/**
	 * 与运行时【同一个】字体函数。
	 *
	 * ⚠️ 必须同源。生成器把 FontObject 写进资产，运行时控件读它 ——
	 *    两边各写一套的话，"没建蓝图"和"建了蓝图"的字体会不一样。
	 */
	FSlateFontInfo LayoutFont(int32 Size, bool bBold = false)
	{
		return HexCardLayout::GetCardFont(Size, bBold);
	}

	/**
	 * 建一个控件并【标记成变量】。
	 *
	 * ⚠️ bIsVariable 必须设 true。
	 *    不设也能在设计器层级里看到控件、也能拖动，
	 *    但它不会成为蓝图变量 —— 美术在蓝图图表里读不到它，
	 *    而且层级面板里显示成非粗体，看不出"这个是给代码用的"。
	 *    对 BindWidget 的运行时绑定来说 bIsVariable 无关（那是纯按名字匹配），
	 *    所以漏了它不会报错，只是可用性变差 —— 属于容易漏的那类。
	 */
	template <typename T>
	T* MakeWidget(UWidgetTree* Tree, const TCHAR* Name)
	{
		T* W = Tree->ConstructWidget<T>(T::StaticClass(), FName(Name));
		if (W)
		{
			W->bIsVariable = true;
		}
		return W;
	}

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

	void SetVBoxPad(UWidget* W, const FMargin& Pad,
		EHorizontalAlignment H = HAlign_Fill, bool bFill = false)
	{
		if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(W->Slot))
		{
			S->SetPadding(Pad);
			S->SetHorizontalAlignment(H);
			if (bFill)
			{
				S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			}
		}
	}

	/**
	 * 载入控件蓝图；不存在就【自动建】一个。
	 *
	 * ⚠️ 自动建是刻意的。要求手工"先去编辑器新建一个空蓝图、父类选对、
	 *    名字拼对、目录放对"有四个地方能出错，而错了之后的表现是
	 *    静默回退到 C++ 版（界面正常、改动不生效、无报错）——
	 *    上一轮就是因为名字对不上（WB_Card vs WBP_HexCard）白折腾了一轮。
	 *    能自动做对的事情不要留给手工。
	 */
	UWidgetBlueprint* LoadWidgetBP(const TCHAR* AssetPath, UClass* WantedParent)
	{
		UWidgetBlueprint* BP = LoadObject<UWidgetBlueprint>(nullptr, AssetPath);

		if (!BP)
		{
			const FString PackagePath(AssetPath);
			FString Dir, AssetName;
			PackagePath.Split(TEXT("/"), &Dir, &AssetName,
				ESearchCase::CaseSensitive, ESearchDir::FromEnd);

			UPackage* Package = CreatePackage(*PackagePath);
			if (!Package)
			{
				UE_LOG(LogHexSpire, Error, TEXT("无法创建包：%s"), AssetPath);
				return nullptr;
			}

			// ⚠️ 用 FWidgetBlueprintOperationUtils::CreateWidgetBlueprint 而不是
			//    自己 NewObject<UWidgetBlueprint>：后者不会建生成类、
			//    不注册资产、也不编译，得到的是一个打不开的坏资产。
			BP = FWidgetBlueprintOperationUtils::CreateWidgetBlueprint(
				Package, *AssetName, BPTYPE_Normal,
				TSubclassOf<UUserWidget>(WantedParent),
				/*RootWidgetClass*/nullptr,
				TEXT("HexBuildCardWidget"), /*bRegisterAndCompile*/true);

			if (!BP)
			{
				UE_LOG(LogHexSpire, Error,
					TEXT("自动创建控件蓝图失败：%s（父类 %s）"),
					AssetPath, *WantedParent->GetName());
				return nullptr;
			}

			UE_LOG(LogHexSpire, Display,
				TEXT("控件蓝图不存在，已自动创建：%s（父类 %s）"),
				AssetPath, *WantedParent->GetName());
		}

		// ⚠️ 父类不对时【不能】往里塞树：控件名会和 BindWidget 属性对不上，
		//    生成出来的东西看着像卡面，但运行时一个字段都填不进去。
		if (!BP->ParentClass || !BP->ParentClass->IsChildOf(WantedParent))
		{
			UE_LOG(LogHexSpire, Error,
				TEXT("控件蓝图 %s 的父类是 %s，不是 %s —— 已跳过"),
				AssetPath,
				BP->ParentClass ? *BP->ParentClass->GetName() : TEXT("null"),
				*WantedParent->GetName());
			return nullptr;
		}
		return BP;
	}

	/** 编译并存盘 */
	bool CompileAndSave(UWidgetBlueprint* BP)
	{
		// ⚠️ MarkBlueprintAsStructurallyModified 只重建骨架类，
		//    不做完整编译。不补一次 CompileBlueprint 就存盘的话，
		//    资产里的生成类与 CDO 仍是旧的 —— 下次加载可能拿到空树。
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
		FKismetEditorUtilities::CompileBlueprint(BP);

		UPackage* Package = BP->GetOutermost();
		Package->MarkPackageDirty();

		const FString Filename = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());

		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_None;

		const bool bOk = UPackage::SavePackage(Package, BP, *Filename, Args);
		if (!bOk)
		{
			UE_LOG(LogHexSpire, Error, TEXT("存盘失败：%s"), *Filename);
		}
		return bOk;
	}
}

namespace
{
	/**
	 * 把卡面控件树建进蓝图。
	 *
	 * 结构（与 UHexCardWidget::BuildDefaultTree 一致）：
	 *   CardRoot (SizeBox 168x232)
	 *     └ CardOverlay (Overlay)
	 *         ├ Frame      (Image, 铺满)
	 *         ├ Art        (Image, 内缩)
	 *         ├ Body       (VerticalBox)
	 *         │   ├ TypeIcon  (Image 52x52)
	 *         │   ├ Name      (TextBlock 15 粗)
	 *         │   ├ TypeLabel (TextBlock 9 粗)
	 *         │   ├ Desc      (TextBlock 11, 自动换行, Fill)
	 *         │   └ Range     (TextBlock 10)
	 *         ├ CostBadge  (Border, 左上) └ Cost (TextBlock 19 粗)
	 *         ├ Tag        (TextBlock 10 粗, 右上)
	 *         ├ Hotkey     (TextBlock 11 粗, 底部居中)
	 *         └ Dim        (Border, 铺满, 半透白)
	 */
	void BuildCardTree(UWidgetTree* Tree)
	{
		USizeBox* Root = MakeWidget<USizeBox>(Tree, L::N::Root);
		Root->SetWidthOverride(L::CardWidth);
		Root->SetHeightOverride(L::CardHeight);

		// ⚠️ 光靠 WidthOverride 不够 —— 实测它【存不进资产】。
		//    资产里 WidthOverride 的数值(168)和 bOverride_HeightOverride
		//    都在，但 bOverride_WidthOverride 这一位没有被序列化，
		//    于是运行时宽度不受约束：Desc 开了自动换行，
		//    文字有多长卡就被撑多宽 —— 实测三张手牌变成 129 / 384 / 184 px，
		//    也就是"一大一小排列不整齐"的真正原因。
		//    （C++ 路径没这问题：它每次运行都重新调 SetWidthOverride，
		//     不经过序列化，所以一直是整齐的 112 px。）
		//
		//    Min/Max DesiredWidth 是独立的属性，能正常存盘，
		//    两个都设成卡宽就把宽度【夹死】了，不依赖那一位。
		Root->SetMinDesiredWidth(L::CardWidth);
		Root->SetMaxDesiredWidth(L::CardWidth);
		Root->SetMinDesiredHeight(L::CardHeight);
		Root->SetMaxDesiredHeight(L::CardHeight);
		Tree->RootWidget = Root;

		UOverlay* Ov = MakeWidget<UOverlay>(Tree, L::N::Overlay);
		Root->AddChild(Ov);

		// ── 卡框
		UImage* Frame = MakeWidget<UImage>(Tree, L::N::Frame);
		Ov->AddChild(Frame);
		SetOverlayPad(Frame, HAlign_Fill, VAlign_Fill);

		// ⚠️ 必须在这里就把卡框贴图设上。
		//    运行时 ApplyView 会按卡的稀有度重新设一次，所以从"功能"看
		//    这一步可以省 —— 但省掉之后【设计器里是一个纯白方块】，
		//    美术调布局时看不到卡框边界，等于在盲摆。
		//    设计器所见即所得比少写两行重要。
		if (UTexture2D* Tex = LoadObject<UTexture2D>(
			nullptr, TEXT("/Game/ArtResource/Card/Card_White.Card_White")))
		{
			Frame->SetBrushFromTexture(Tex, false);
		}

		// ── 插画
		UImage* Art = MakeWidget<UImage>(Tree, L::N::Art);
		Ov->AddChild(Art);
		SetOverlayPad(Art, HAlign_Fill, VAlign_Fill, L::ArtPad);

		// ── 正文
		{
			UVerticalBox* Body = MakeWidget<UVerticalBox>(Tree, L::N::Body);
			Ov->AddChild(Body);
			SetOverlayPad(Body, HAlign_Fill, VAlign_Fill, L::BodyPad);

			UImage* Icon = MakeWidget<UImage>(Tree, L::N::TypeIcon);
			// 占位图标（运行时按卡类型替换）。同理：不设的话设计器里是白块。
			if (UTexture2D* Tex = LoadObject<UTexture2D>(
				nullptr, TEXT("/Game/ArtResource/Card/Attack.Attack")))
			{
				Icon->SetBrushFromTexture(Tex, false);
			}
			// ⚠️ 用 SetDesiredSizeOverride：SetBrushSize 在 5.8 已弃用，
			//    留着会在下个版本编译失败。
			Icon->SetDesiredSizeOverride(L::TypeIconSize);
			Body->AddChild(Icon);
			SetVBoxPad(Icon, L::IconRowPad, HAlign_Center);

			UTextBlock* Name = MakeWidget<UTextBlock>(Tree, L::N::Name);
			Name->SetFont(LayoutFont(L::FontName, true));
			Name->SetJustification(ETextJustify::Center);
			Name->SetColorAndOpacity(FSlateColor(L::ColInk));
			// ⚠️ 占位文字不是装饰：空的 TextBlock 在设计器里高度为 0，
			//    美术会以为"这一行不存在"，从而把间距调错。
			//    运行时 SetCardView 会覆盖它。
			Name->SetText(FText::FromString(TEXT("盾击")));
			Body->AddChild(Name);
			SetVBoxPad(Name, L::NameRowPad);

			UTextBlock* Type = MakeWidget<UTextBlock>(Tree, L::N::TypeLabel);
			Type->SetFont(LayoutFont(L::FontType, true));
			Type->SetJustification(ETextJustify::Center);
			Type->SetColorAndOpacity(FSlateColor(L::ColInkSoft));
			Type->SetText(FText::FromString(TEXT("攻击")));
			Body->AddChild(Type);
			SetVBoxPad(Type, L::TypeRowPad);

			UTextBlock* Desc = MakeWidget<UTextBlock>(Tree, L::N::Desc);
			Desc->SetFont(LayoutFont(L::FontDesc));
			Desc->SetJustification(ETextJustify::Center);
			Desc->SetAutoWrapText(true);
			Desc->SetColorAndOpacity(FSlateColor(L::ColDescInk));
			Desc->SetText(FText::FromString(TEXT("造成 12 点伤害，并击退 1 格。")));
			Body->AddChild(Desc);
			SetVBoxPad(Desc, FMargin(0.0f), HAlign_Fill, /*bFill*/true);

			UTextBlock* Range = MakeWidget<UTextBlock>(Tree, L::N::Range);
			Range->SetFont(LayoutFont(L::FontRange));
			Range->SetJustification(ETextJustify::Center);
			Range->SetColorAndOpacity(FSlateColor(L::ColInkSoft));
			Range->SetText(FText::FromString(TEXT("射程 1-1")));
			Body->AddChild(Range);
		}

		// ── 左上角费用
		{
			UBorder* Badge = MakeWidget<UBorder>(Tree, L::N::CostBadge);
			Badge->SetPadding(L::CostBadgeInner);
			Badge->SetBrushColor(L::ColCostBadge);
			Ov->AddChild(Badge);
			SetOverlayPad(Badge, HAlign_Left, VAlign_Top, L::CostBadgeSlotPad);

			UTextBlock* Cost = MakeWidget<UTextBlock>(Tree, L::N::Cost);
			Cost->SetFont(LayoutFont(L::FontCost, true));
			Cost->SetJustification(ETextJustify::Center);
			Cost->SetColorAndOpacity(FSlateColor(L::ColCostEnergy));
			Cost->SetText(FText::FromString(TEXT("1")));
			Badge->AddChild(Cost);
		}

		// ── 右上角角标
		UTextBlock* Tag = MakeWidget<UTextBlock>(Tree, L::N::Tag);
		Tag->SetFont(LayoutFont(L::FontTag, true));
		Tag->SetColorAndOpacity(FSlateColor(L::ColWarn));
		Tag->SetText(FText::FromString(TEXT("消耗")));
		Ov->AddChild(Tag);
		SetOverlayPad(Tag, HAlign_Right, VAlign_Top, L::TagPad);

		// ── 卡底快捷键
		UTextBlock* Key = MakeWidget<UTextBlock>(Tree, L::N::Hotkey);
		Key->SetFont(LayoutFont(L::FontKey, true));
		Key->SetColorAndOpacity(FSlateColor(L::ColInkSoft));
		Key->SetText(FText::FromString(TEXT("[1]")));
		Ov->AddChild(Key);
		SetOverlayPad(Key, HAlign_Center, VAlign_Bottom, L::HotkeyPad);

		// ── 不可用遮罩（半透白：冲淡而非压暗）
		UBorder* Dim = MakeWidget<UBorder>(Tree, L::N::Dim);
		Dim->SetBrushColor(L::ColDimVeil);
		// ⚠️ 设计器里默认隐藏。它是"打不出"时才盖上的浅纱，
		//    默认可见的话美术看到的整张卡都是发白的，会误以为颜色配错了。
		//    运行时 ApplyView 会按 bPlayable 控制它的可见性。
		Dim->SetVisibility(ESlateVisibility::Hidden);
		Ov->AddChild(Dim);
		SetOverlayPad(Dim, HAlign_Fill, VAlign_Fill);
	}

	/**
	 * 横版卡控件树（左侧固定卡）。
	 *
	 * 结构与 UHexCardWidgetWide::BuildDefaultTree 一致：
	 *   CardRoot (SizeBox 196x100)
	 *     └ CardOverlay (Overlay)
	 *         ├ Frame       (Image, 铺满, RCard_Attack)
	 *         ├ ContentRow  (HorizontalBox, 内缩到贴图留白区)
	 *         │   ├ TypeIcon (Image 34x34)
	 *         │   ├ TextCol  (VerticalBox, Fill)
	 *         │   │   ├ Name (13 粗)
	 *         │   │   └ Desc (9, 自动换行)
	 *         │   └ Hotkey   (10 粗)
	 *         ├ CostBadge   (Border, 左上) └ Cost (15 粗)
	 *         └ Dim         (Border, 铺满, 默认隐藏)
	 *
	 * ⚠️ 控件名必须与父类 UHexCardWidget 的 BindWidgetOptional 一致 ——
	 *    绑定是纯按名字匹配的，名字错了那一项永远收不到数据，
	 *    而且只有一条编译期 Note，运行时完全静默。
	 */
	void BuildWideCardTree(UWidgetTree* Tree)
	{
		USizeBox* Root = MakeWidget<USizeBox>(Tree, L::N::Root);
		Root->SetWidthOverride(L::R::CardWidth);
		Root->SetHeightOverride(L::R::CardHeight);

		// ⚠️ 同竖版：WidthOverride 存不进资产（bOverride_ 那一位不序列化），
		//    必须用 Min/Max DesiredWidth 把尺寸夹死，否则文字会把卡撑宽。
		Root->SetMinDesiredWidth(L::R::CardWidth);
		Root->SetMaxDesiredWidth(L::R::CardWidth);
		Root->SetMinDesiredHeight(L::R::CardHeight);
		Root->SetMaxDesiredHeight(L::R::CardHeight);
		Tree->RootWidget = Root;

		UOverlay* Ov = MakeWidget<UOverlay>(Tree, L::N::Overlay);
		Root->AddChild(Ov);

		// ── 横版卡框
		UImage* Frame = MakeWidget<UImage>(Tree, L::N::Frame);
		Ov->AddChild(Frame);
		SetOverlayPad(Frame, HAlign_Fill, VAlign_Fill);
		// 设计器里要看得见卡框，否则是一个纯白方块，等于盲摆
		if (UTexture2D* Tex = LoadObject<UTexture2D>(
			nullptr, TEXT("/Game/ArtResource/Card/RCard_Attack.RCard_Attack")))
		{
			Frame->SetBrushFromTexture(Tex, false);
		}

		// ── 内容行
		{
			UHorizontalBox* Row = MakeWidget<UHorizontalBox>(Tree, TEXT("ContentRow"));
			Ov->AddChild(Row);
			SetOverlayPad(Row, HAlign_Fill, VAlign_Fill, L::R::ContentPad);

			UImage* Icon = MakeWidget<UImage>(Tree, L::N::TypeIcon);
			Icon->SetDesiredSizeOverride(L::R::IconSize);
			if (UTexture2D* Tex = LoadObject<UTexture2D>(
				nullptr, TEXT("/Game/ArtResource/Card/Attack.Attack")))
			{
				Icon->SetBrushFromTexture(Tex, false);
			}
			Row->AddChild(Icon);
			if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(Icon->Slot))
			{
				S->SetVerticalAlignment(VAlign_Center);
				S->SetPadding(L::R::IconPad);
			}

			{
				UVerticalBox* TextCol = MakeWidget<UVerticalBox>(Tree, TEXT("TextCol"));
				Row->AddChild(TextCol);
				if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(TextCol->Slot))
				{
					// 撑满剩余宽度，否则描述换行会把快捷键挤出卡外
					S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
					S->SetVerticalAlignment(VAlign_Center);
				}

				UTextBlock* Name = MakeWidget<UTextBlock>(Tree, L::N::Name);
				Name->SetFont(LayoutFont(L::R::FontName, true));
				Name->SetColorAndOpacity(FSlateColor(L::ColInk));
				// 占位文字：空 TextBlock 在设计器里高度为 0，会让人以为这行不存在
				Name->SetText(FText::FromString(TEXT("攻击")));
				TextCol->AddChild(Name);

				UTextBlock* Desc = MakeWidget<UTextBlock>(Tree, L::N::Desc);
				Desc->SetFont(LayoutFont(L::R::FontDesc));
				Desc->SetAutoWrapText(true);
				Desc->SetColorAndOpacity(FSlateColor(L::ColDescInk));
				Desc->SetText(FText::FromString(TEXT("造成 12 点伤害。")));
				TextCol->AddChild(Desc);
			}

			UTextBlock* Key = MakeWidget<UTextBlock>(Tree, L::N::Hotkey);
			Key->SetFont(LayoutFont(L::R::FontKey, true));
			Key->SetColorAndOpacity(FSlateColor(L::ColInkSoft));
			Key->SetText(FText::FromString(TEXT("[Q]")));
			Row->AddChild(Key);
			if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(Key->Slot))
			{
				S->SetVerticalAlignment(VAlign_Center);
				S->SetPadding(L::R::HotkeyPad);
			}
		}

		// ── 左上角费用
		{
			UBorder* Badge = MakeWidget<UBorder>(Tree, L::N::CostBadge);
			Badge->SetPadding(L::R::CostInner);
			Badge->SetBrushColor(L::ColCostBadge);
			Ov->AddChild(Badge);
			SetOverlayPad(Badge, HAlign_Left, VAlign_Top, L::R::CostSlotPad);

			UTextBlock* Cost = MakeWidget<UTextBlock>(Tree, L::N::Cost);
			Cost->SetFont(LayoutFont(L::R::FontCost, true));
			Cost->SetJustification(ETextJustify::Center);
			Cost->SetColorAndOpacity(FSlateColor(L::ColCostEnergy));
			Cost->SetText(FText::FromString(TEXT("1")));
			Badge->AddChild(Cost);
		}

		// ── 不可用遮罩（设计器里默认隐藏，否则整张卡发白）
		UBorder* Dim = MakeWidget<UBorder>(Tree, L::N::Dim);
		Dim->SetBrushColor(L::ColDimVeil);
		Dim->SetVisibility(ESlateVisibility::Hidden);
		Ov->AddChild(Dim);
		SetOverlayPad(Dim, HAlign_Fill, VAlign_Fill);
	}

	/**
	 * 手牌区控件树。
	 *
	 * ⚠️ 这里的控件名必须与 UHexHandPanelWidget 的 BindWidgetOptional
	 *    同名：HandBox / FixedBox / FixedTitle。
	 */
	void BuildHandPanelTree(UWidgetTree* Tree)
	{
		UCanvasPanel* Canvas = MakeWidget<UCanvasPanel>(Tree, TEXT("PanelRoot"));
		Tree->RootWidget = Canvas;

		// 手牌：底边居中横排
		UHorizontalBox* Hand = MakeWidget<UHorizontalBox>(Tree, TEXT("HandBox"));
		Canvas->AddChild(Hand);
		if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Hand->Slot))
		{
			S->SetAnchors(FAnchors(0.5f, 1.0f, 0.5f, 1.0f));
			S->SetAlignment(FVector2D(0.5f, 1.0f));
			S->SetOffsets(FMargin(0.0f, 0.0f, 0.0f, 16.0f));
			S->SetAutoSize(true);
		}

		// 固定卡：左侧竖排（标题 + 容器）
		UVerticalBox* LeftCol = MakeWidget<UVerticalBox>(Tree, TEXT("LeftCol"));
		Canvas->AddChild(LeftCol);
		if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(LeftCol->Slot))
		{
			// 锚在左侧垂直中点偏下 —— 避开顶栏与图例
			S->SetAnchors(FAnchors(0.0f, 0.52f, 0.0f, 0.52f));
			S->SetAlignment(FVector2D(0.0f, 0.5f));
			S->SetOffsets(FMargin(14.0f, 0.0f, 0.0f, 0.0f));
			S->SetAutoSize(true);
		}

		UTextBlock* Title = MakeWidget<UTextBlock>(Tree, TEXT("FixedTitle"));
		Title->SetFont(LayoutFont(10));
		Title->SetText(FText::FromString(TEXT("固定卡 · 常驻")));
		Title->SetColorAndOpacity(FSlateColor(FLinearColor(0.62f, 0.62f, 0.60f)));
		LeftCol->AddChild(Title);

		UVerticalBox* Fixed = MakeWidget<UVerticalBox>(Tree, TEXT("FixedBox"));
		LeftCol->AddChild(Fixed);
	}
}

#endif // WITH_EDITOR

UHexBuildCardWidgetCommandlet::UHexBuildCardWidgetCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UHexBuildCardWidgetCommandlet::Main(const FString& Params)
{
#if !WITH_EDITOR
	// 打包目标里 UMGEditor/UnrealEd 都不存在，这个 commandlet 无从运行。
	UE_LOG(LogHexSpire, Error, TEXT("HexBuildCardWidget 只能在编辑器构建下运行"));
	return 1;
#else
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamsMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamsMap);

	// ⚠️ 覆盖非空的控件树需要显式 -force。
	//    美术在设计器里调好布局之后，误跑一次这个命令会把改动全部冲掉，
	//    而且【没有撤销】（是对资产的批量写入 + 存盘）。
	//    所以默认只填空树，非空时拒绝并提示。
	const bool bForce = Switches.Contains(TEXT("force"));

	struct FTarget
	{
		const TCHAR* AssetPath;
		UClass* Parent;
		void (*Build)(UWidgetTree*);
		const TCHAR* Label;
	};

	const FTarget Targets[] =
	{
		{ TEXT("/Game/HexSpire/UI/WB_Card"),
		  UHexCardWidget::StaticClass(),      &BuildCardTree,      TEXT("卡面") },
		{ TEXT("/Game/HexSpire/UI/WB_CardWide"),
		  UHexCardWidgetWide::StaticClass(), &BuildWideCardTree,  TEXT("横版固定卡") },
		{ TEXT("/Game/HexSpire/UI/WB_HandPanel"),
		  UHexHandPanelWidget::StaticClass(), &BuildHandPanelTree, TEXT("手牌区") },
	};

	int32 Built = 0;
	int32 Skipped = 0;

	for (const FTarget& T : Targets)
	{
		UWidgetBlueprint* BP = LoadWidgetBP(T.AssetPath, T.Parent);
		if (!BP)
		{
			++Skipped;
			continue;
		}

		if (!BP->WidgetTree)
		{
			UE_LOG(LogHexSpire, Error,
				TEXT("%s 蓝图 %s 没有 WidgetTree，已跳过"), T.Label, T.AssetPath);
			++Skipped;
			continue;
		}

		// 已有内容且没加 -force → 拒绝，避免冲掉美术的改动
		const bool bHasContent = (BP->WidgetTree->RootWidget != nullptr);
		if (bHasContent && !bForce)
		{
			UE_LOG(LogHexSpire, Warning,
				TEXT("%s 蓝图 %s 已有控件树，跳过。"
					 "确实要用 C++ 基线覆盖它请加 -force（会丢失设计器里的改动）"),
				T.Label, T.AssetPath);
			++Skipped;
			continue;
		}

		BP->WidgetTree->Modify();

		if (bHasContent)
		{
			// ⚠️ 必须连 RootWidget 一起清。只删子控件的话，旧的根仍然挂着，
			//    新建的根会被它顶掉 —— 结果是"生成成功但设计器里还是旧布局"。
			TArray<UWidget*> Old;
			BP->WidgetTree->GetAllWidgets(Old);

			BP->WidgetTree->RootWidget = nullptr;
			for (UWidget* W : Old)
			{
				if (!W)
				{
					continue;
				}

				// ⚠️ 删控件的同时必须注销它的变量 GUID。
				//    WidgetVariableNameToGuidMap 是独立于控件树的一份表，
				//    清树【不会】清它。留着旧条目的话，下面重新
				//    OnVariableAdded 同名控件会触发引擎的
				//    ensure(!WidgetVariableNameToGuidMap.Contains(VariableName))
				//    —— 实测 -force 重跑时必然弹断言（WidgetBlueprint.cpp:1027）。
				BP->OnVariableRemoved(W->GetFName());
				BP->WidgetTree->RemoveWidget(W);
			}
		}

		T.Build(BP->WidgetTree);

		// ⚠️ 每个控件都要在 WidgetVariableNameToGuidMap 里登记 GUID。
		//    5.8 的编译器会 ensure 这一点（ValidateAndFixUpVariableGuids），
		//    漏登记会在 Development 编辑器下弹断言。
		//    OnVariableAdded 就是负责登记的那一步。
		TArray<UWidget*> All;
		BP->WidgetTree->GetAllWidgets(All);
		for (UWidget* W : All)
		{
			if (!W)
			{
				continue;
			}

			// ⚠️ 只给【还没登记】的名字加 GUID。
			//    OnVariableAdded 内部是 ensure 而非幂等操作，
			//    对已存在的名字再调一次就会弹断言。
			if (!BP->WidgetVariableNameToGuidMap.Contains(W->GetFName()))
			{
				BP->OnVariableAdded(W->GetFName());
			}
		}

		if (CompileAndSave(BP))
		{
			UE_LOG(LogHexSpire, Display,
				TEXT("已生成%s布局 → %s（%d 个控件）"),
				T.Label, T.AssetPath, All.Num());
			++Built;
		}
		else
		{
			++Skipped;
		}
	}

	UE_LOG(LogHexSpire, Display,
		TEXT("控件蓝图布局生成完成：成功 %d 个，跳过 %d 个"), Built, Skipped);

	// 一个都没成功时返回非 0，让批处理/CI 能发现
	return (Built > 0) ? 0 : 1;
#endif
}
