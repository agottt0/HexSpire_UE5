// Copyright Hex Spire. All Rights Reserved.

#include "UI/HexCardWidget.h"
#include "UI/HexCardArt.h"
#include "UI/HexCardLayout.h"

#include "Battle/HexCardData.h"
#include "Battle/HexUnit.h"
#include "HexSpire.h"

// ⚠️ WidgetTree 是 UUserWidget 上的 TObjectPtr 成员，头文件里只有前置声明。
//    不 include 这个的话 ConstructWidget 会报"使用了未定义类型"。
#include "Blueprint/WidgetTree.h"
#include "Styling/CoreStyle.h"

#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Font.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"

namespace
{
	// ══════════════════════════════════════════════════════════════
	// 配色与尺寸全部来自 HexCardLayout
	// ══════════════════════════════════════════════════════════════
	// ⚠️ 刻意【不在这里重新定义数值】。commandlet 生成控件蓝图时读的是
	//    同一份 HexCardLayout —— 两边各写一份的话，改了 C++ 的边距而
	//    蓝图没跟着变（或反之），就会出现"美术看到的和玩家看到的不一样"，
	//    而这种不一致不报错，只能靠眼睛发现。
	namespace L = HexCardLayout;

	/**
	 * 卡面字体 —— 转发 HexCardLayout::GetCardFont。
	 *
	 * ⚠️ 曾经这里自己取 GEngine->GetMediumFont()，那是错的：
	 *    没有项目配置时它返回瞬态的 DefaultRegularFont，
	 *    存不进资产（蓝图里 FontFace 显示 None）且只有 Regular 一档。
	 *    详见 HexCardLayout.h 字体一节。
	 */
	FSlateFontInfo CardFont(int32 Size, bool bBold = false)
	{
		return HexCardLayout::GetCardFont(Size, bBold);
	}

	/** 给 Overlay 的子项设置对齐与边距 */
	void SetOverlaySlot(UWidget* W, EHorizontalAlignment H, EVerticalAlignment V,
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

// ═══════════════════════════════════════════════════════ FHexCardView

FHexCardView FHexCardView::Make(
	const FHexCardData& Card, const FHexUnit* Hero,
	int32 InCost, bool bInPlayable, bool bInSelected)
{
	FHexCardView V;
	V.CardId = Card.Id;
	V.DisplayName = Card.DisplayName;

	// ⚠️ 必须每次实算而非缓存：卡面数值随 ATK 成长会变（美术文档 §12），
	//    缓存下来的描述会显示成长前的旧数值。
	V.Description = Card.RenderDescription(Hero);

	V.CardType = Card.CardType;
	V.Rarity = Card.Rarity;
	V.Cost = InCost;
	V.RangeMin = Card.TargetSpec.RangeMin;
	V.RangeMax = Card.TargetSpec.RangeMax;
	V.bPlayable = bInPlayable;
	V.bSelected = bInSelected;
	V.bExhaust = Card.bIsExhaust;
	V.bCornerstone = Card.bIsCornerstone;
	return V;
}

// ═══════════════════════════════════════════════════════ 构造

UHexCardWidget::UHexCardWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 卡牌不需要接收键盘焦点 —— 输入全部由 PlayerController 处理
	SetIsFocusable(false);

	// 上浮动画在 NativeTick 里插值。
	//
	// ⚠️ TickFrequency 保持默认的 Auto，不要为了"省性能"改成 Never。
	//    Auto 的语义是"本类或父类覆写了原生 Tick 就 tick"
	//    （UUserWidget::UpdateCanTick → ClassRequiresNativeTick），
	//    本类覆写了 NativeTick，所以会被 tick，C++ 和 WBP 子类都一样。
	//    改成 Never 则上浮会彻底不动 —— 而且编辑器只在 WBP 里给一条
	//    警告，纯 C++ 路径下没有任何提示。
}

// ═══════════════════════════════════════════════════════ 控件树

TSharedRef<SWidget> UHexCardWidget::RebuildWidget()
{
	// ══════════════════════════════════════════════════════════════
	// 两条路径：WBP 设计器摆好的树，或 C++ 默认树
	// ══════════════════════════════════════════════════════════════
	// ⚠️ 判据是 WidgetTree->RootWidget 是否已有内容。
	//    WBP 子类走到这里时，设计器里摆的控件已经被反序列化进
	//    WidgetTree 并绑好了 BindWidgetOptional 指针 ——
	//    此时【绝不能】再建一棵默认树，否则会把设计器的根顶掉，
	//    美术摆了一下午的版式在运行时完全不出现（编辑器预览里却是对的，
	//    因为预览不走 RebuildWidget 这条路 —— 这种"编辑器对、游戏错"
	//    最难查）。
	const bool bDesignerProvidedTree = (WidgetTree && WidgetTree->RootWidget != nullptr);

	if (!bDesignerProvidedTree && !Root)
	{
		BuildDefaultTree();
	}

	ApplyView();

	return Super::RebuildWidget();
}

void UHexCardWidget::BuildDefaultTree()
{
	Root = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), L::N::Root);
	Root->SetWidthOverride(L::CardWidth);
	Root->SetHeightOverride(L::CardHeight);

	MainOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), L::N::Overlay);
	Root->AddChild(MainOverlay);

	// ── ① 卡框（最底层，铺满）
	Frame = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), L::N::Frame);
	MainOverlay->AddChild(Frame);
	SetOverlaySlot(Frame, HAlign_Fill, VAlign_Fill);

	// ── ② 插画（在框之上、文字之下）
	Art = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), L::N::Art);
	MainOverlay->AddChild(Art);
	SetOverlaySlot(Art, HAlign_Fill, VAlign_Fill, L::ArtPad);

	// ── ③ 正文（图标 + 卡名 + 类型 + 描述 + 射程），竖排
	{
		UVerticalBox* Body = WidgetTree->ConstructWidget<UVerticalBox>(
			UVerticalBox::StaticClass(), L::N::Body);
		MainOverlay->AddChild(Body);
		SetOverlaySlot(Body, HAlign_Fill, VAlign_Fill, L::BodyPad);

		// 类型图标
		TypeIcon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), L::N::TypeIcon);
		// ⚠️ 用 SetDesiredSizeOverride 而不是 SetBrushSize（5.8 起后者已弃用，
		//    留着会在下个版本直接编译失败）。
		//    两者语义一致：都是给这张图一个固定的期望尺寸，
		//    否则 UImage 会按贴图原始像素（256×256）撑开卡面。
		TypeIcon->SetDesiredSizeOverride(L::TypeIconSize);
		Body->AddChild(TypeIcon);
		if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(TypeIcon->Slot))
		{
			S->SetHorizontalAlignment(HAlign_Center);
			S->SetPadding(L::TypeRowPad);
		}

		// 卡名
		Name = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), L::N::Name);
		Name->SetFont(CardFont(L::FontName, true));
		Name->SetJustification(ETextJustify::Center);
		Body->AddChild(Name);
		if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(Name->Slot))
		{
			S->SetHorizontalAlignment(HAlign_Fill);
			S->SetPadding(L::NameRowPad);
		}

		// 类型文字 —— 取代原来的整卡染色
		TypeLabel = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), L::N::TypeLabel);
		TypeLabel->SetFont(CardFont(L::FontType, true));
		TypeLabel->SetJustification(ETextJustify::Center);
		Body->AddChild(TypeLabel);
		if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(TypeLabel->Slot))
		{
			S->SetHorizontalAlignment(HAlign_Fill);
			S->SetPadding(L::TypeRowPad);
		}

		// 描述（自动换行 —— 描述长度不定，不换行会溢出卡面）
		Desc = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), L::N::Desc);
		Desc->SetFont(CardFont(L::FontDesc));
		Desc->SetJustification(ETextJustify::Center);
		Desc->SetAutoWrapText(true);
		Body->AddChild(Desc);
		if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(Desc->Slot))
		{
			S->SetHorizontalAlignment(HAlign_Fill);
			S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}

		// 射程
		Range = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), L::N::Range);
		Range->SetFont(CardFont(L::FontRange));
		Range->SetJustification(ETextJustify::Center);
		Body->AddChild(Range);
	}

	// ── ④ 左上角费用（圆形底衬 + 数字）
	//
	// ⚠️ 底衬不是装饰。费用数字要压在插画上，没有低对比底衬时
	//    浅色数字在浅色插画上会读不出来（美术文档 §12）。
	{
		CostBadge = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), L::N::CostBadge);
		CostBadge->SetPadding(L::CostBadgeInner);
		CostBadge->SetBrushColor(L::ColCostBadge);
		MainOverlay->AddChild(CostBadge);
		SetOverlaySlot(CostBadge, HAlign_Left, VAlign_Top, L::CostBadgeSlotPad);

		Cost = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), L::N::Cost);
		Cost->SetFont(CardFont(L::FontCost, true));
		Cost->SetJustification(ETextJustify::Center);
		CostBadge->AddChild(Cost);
	}

	// ── ⑤ 右上角：消耗/基石角标
	Tag = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), L::N::Tag);
	Tag->SetFont(CardFont(L::FontTag, true));
	MainOverlay->AddChild(Tag);
	SetOverlaySlot(Tag, HAlign_Right, VAlign_Top, L::TagPad);

	// ── ⑥ 底部：键盘提示（序号从左上角挪到这里）
	Hotkey = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), L::N::Hotkey);
	Hotkey->SetFont(CardFont(L::FontKey, true));
	MainOverlay->AddChild(Hotkey);
	SetOverlaySlot(Hotkey, HAlign_Center, VAlign_Bottom, L::HotkeyPad);

	// ── ⑦ 不可用遮罩（最上层）
	//
	// ⚠️ 打不出的牌必须【明显】变淡，否则玩家会反复点无效的牌。
	Dim = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), L::N::Dim);
	Dim->SetBrushColor(L::ColDimVeil);
	Dim->SetVisibility(ESlateVisibility::Hidden);
	MainOverlay->AddChild(Dim);
	SetOverlaySlot(Dim, HAlign_Fill, VAlign_Fill);

	// ⚠️ 选中描边控件已删除 —— 选中改为上浮表现（见头文件）。
	//    描边在浅色卡上尤其糟：黄边压在白纸上会和"消耗"角标的
	//    警告色撞在一起，两个不同含义共用一种颜色。

	// ⚠️ 必须把 Root 挂成 WidgetTree 的根，否则整棵树【建了但不显示】。
	//    Super::RebuildWidget() 返回的是 WidgetTree->RootWidget 的 Slate 表示；
	//    不赋值时它是 nullptr → 返回一个 SNullWidget，
	//    于是控件占着布局位置却什么都不画。
	//
	//    这是静默失败：ConstructWidget 全部成功、贴图全部加载成功、
	//    自检日志一切正常（那些查的是控件对象，不是渲染结果），
	//    但屏幕上一张卡都没有。漏了这一行第一次就踩中了。
	WidgetTree->RootWidget = Root;
}

// ═══════════════════════════════════════════════════════ 内容刷新

void UHexCardWidget::SetCardView(const FHexCardView& InView)
{
	View = InView;
	ApplyView();
}

void UHexCardWidget::SetBaseScale(float InScale)
{
	BaseScale = FMath::Max(InScale, 0.01f);
	ApplyTransform();
}

// ═══════════════════════════════════════════════════════ 输入

FReply UHexCardWidget::NativeOnMouseButtonDown(
	const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	// ⚠️ 即使这张牌打不出，也要【消费】点击。
	//    放过去的话点击会穿透到棋盘，被解释成"点了卡背后那一格" ——
	//    玩家点一张费用不足的攻击牌，结果角色走位了。
	//    这比"点了没反应"糟得多：它会实际改变游戏状态。
	if (OnCardClicked.IsBound())
	{
		OnCardClicked.Execute(CardUid);
	}

	return FReply::Handled();
}

void UHexCardWidget::NativeOnMouseEnter(
	const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
	bHovered = true;
	ApplyView();
}

void UHexCardWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	bHovered = false;
	ApplyView();
}

// ═══════════════════════════════════════════════════════ 上浮

void UHexCardWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 目标高度：选中 > 悬停 > 归位。
	// 选中优先于悬停 —— 否则鼠标移开已选中的卡时它会掉下去，
	// 看起来像"选中被取消了"。
	float Target = 0.0f;
	if (View.bSelected)
	{
		Target = SelectLift;
	}
	else if (bHovered && View.bPlayable)
	{
		// ⚠️ 只有可打出的牌才响应悬停。
		//    给打不出的牌做正反馈会让玩家一直去点它。
		Target = HoverLift;
	}

	if (FMath::IsNearlyEqual(CurrentLift, Target, 0.05f))
	{
		// 已到位，直接吸附，避免永远差一点点导致每帧都写 transform
		if (CurrentLift != Target)
		{
			CurrentLift = Target;
			ApplyTransform();
		}
		return;
	}

	// ⚠️ 用指数插值而不是线性：
	//    线性插值在"选中另一张卡"时两张卡的运动速度相同，
	//    观感僵硬；指数插值起步快、收尾缓，更像卡片被抽起来。
	//
	// ⚠️ InterpSpeed 走 DeltaTime，所以帧率无关 —— 用固定步长
	//    会让 30fps 下的上浮慢一倍。
	CurrentLift = FMath::FInterpTo(CurrentLift, Target, InDeltaTime, 18.0f);
	ApplyTransform();
}

void UHexCardWidget::ApplyTransform()
{
	FWidgetTransform T;

	// 选中时额外放大一点点，让它在一排卡里"离开队列"
	const float Scale = BaseScale * (View.bSelected ? SelectScale : 1.0f);
	T.Scale = FVector2D(Scale, Scale);

	// ⚠️ -Y 是屏幕【向上】。手牌锚在屏幕底边，用 +Y 会把卡推到屏幕外，
	//    只剩上半截可见 —— 而那看起来像"卡被裁切了"的渲染 bug。
	//
	// ⚠️ 乘 BaseScale：Slate 的 RenderTransform 是
	//    Concatenate(Scale, ..., Translation)，平移在缩放之后应用，
	//    不会被缩放影响。固定卡（0.72）若照抄 26px，相对自身尺寸
	//    就浮得过高，像弹出去了。
	T.Translation = FVector2D(0.0f, -CurrentLift * BaseScale);

	SetRenderTransform(T);
}

// ═══════════════════════════════════════════════════════ 蓝图取值（数据源）

// ⚠️ 这一组的值全部【源自 DataTable】，不是写死在代码里的。
//    链路：DT_Cards → FHexCardTableLoader → FHexContentLibrary
//          → FHexCardView::Make() → 这里。
//    所以改文案只改表；蓝图只决定"画在哪、多大、什么颜色"。

FText UHexCardWidget::GetCardName() const
{
	return FText::FromString(View.DisplayName);
}

FText UHexCardWidget::GetCardDescription() const
{
	// ⚠️ View.Description 是【已实算】的（{dmg} 已换成数字）。
	//    不要在蓝图里再拼一次数值 —— 那会绕过 §7.5 的系数化，
	//    ATK 成长后蓝图拼出来的数字会与实际伤害不一致。
	return FText::FromString(View.Description);
}

FText UHexCardWidget::GetCardCostText() const
{
	return FText::AsNumber(View.Cost);
}

FText UHexCardWidget::GetCardRangeText() const
{
	return FText::FromString(
		FString::Printf(TEXT("射程 %d-%d"), View.RangeMin, View.RangeMax));
}

FText UHexCardWidget::GetCardTypeText() const
{
	return FText::FromString(HexCardArt::GetTypeName(View.CardType));
}

FText UHexCardWidget::GetCardTagText() const
{
	// 消耗优先于基石：消耗是"用完就没了"的强提醒
	if (View.bExhaust)
	{
		return FText::FromString(TEXT("消耗"));
	}
	if (View.bCornerstone)
	{
		return FText::FromString(TEXT("基石"));
	}
	return FText::GetEmpty();
}

FText UHexCardWidget::GetCardHotkeyText() const
{
	FString Key = View.HotkeyLabel;
	if (Key.IsEmpty() && View.HotkeyNumber > 0)
	{
		Key = FString::FromInt(View.HotkeyNumber);
	}
	return Key.IsEmpty()
		? FText::GetEmpty()
		: FText::FromString(FString::Printf(TEXT("[%s]"), *Key));
}

FLinearColor UHexCardWidget::GetCardTypeAccent() const
{
	return HexCardArt::GetTypeAccent(View.CardType);
}

UTexture2D* UHexCardWidget::GetCardFrameTexture() const
{
	return HexCardArt::GetFrame(View.CardId, View.Rarity);
}

UTexture2D* UHexCardWidget::GetCardTypeIconTexture() const
{
	return HexCardArt::GetTypeIcon(View.CardId, View.CardType);
}

UTexture2D* UHexCardWidget::GetCardArtworkTexture() const
{
	return HexCardArt::GetArtwork(View.CardId);
}

// ═══════════════════════════════════════════════════════ 绑定自检

FString UHexCardWidget::DescribeFont() const
{
	if (!Name)
	{
		return TEXT("卡名控件未绑定，无法报告字体");
	}

	const FSlateFontInfo& F = Name->GetFont();
	return FString::Printf(
		TEXT("卡名字体：对象=%s 字族=%s 字号=%.0f"),
		F.FontObject ? *F.FontObject->GetPathName() : TEXT("【空→回退 Roboto，无中文】"),
		F.TypefaceFontName.IsNone() ? TEXT("(默认)") : *F.TypefaceFontName.ToString(),
		F.Size);
}

TArray<FString> UHexCardWidget::GetUnboundWidgetNames() const
{
	// ⚠️ 逐个列出，而不是只数个数：知道"少了 3 个"没用，
	//    要知道少的是哪几个才能去蓝图里改名字。
	TArray<FString> Missing;

	auto Check = [&Missing](const UWidget* W, const TCHAR* Name)
	{
		if (!W)
		{
			Missing.Add(Name);
		}
	};

	Check(Frame,     HexCardLayout::N::Frame);
	Check(Art,       HexCardLayout::N::Art);
	Check(TypeIcon,  HexCardLayout::N::TypeIcon);
	Check(CostBadge, HexCardLayout::N::CostBadge);
	Check(Cost,      HexCardLayout::N::Cost);
	Check(Name,      HexCardLayout::N::Name);
	Check(TypeLabel, HexCardLayout::N::TypeLabel);
	Check(Desc,      HexCardLayout::N::Desc);
	Check(Range,     HexCardLayout::N::Range);
	Check(Tag,       HexCardLayout::N::Tag);
	Check(Hotkey,    HexCardLayout::N::Hotkey);
	Check(Dim,       HexCardLayout::N::Dim);

	return Missing;
}

// ═══════════════════════════════════════════════════════ 数据 → 控件

void UHexCardWidget::ApplyView()
{
	// ══════════════════════════════════════════════════════════════
	// 每个控件都要判空
	// ══════════════════════════════════════════════════════════════
	// ⚠️ 不能像以前那样"判一个 Frame 非空就认为整棵树都在"。
	//    WBP 路径下这些指针来自 BindWidgetOptional：
	//    美术只摆了卡框和卡名、没摆射程，那 Range 就是 nullptr，
	//    而 Frame 是有效的 —— 只判 Frame 会在下面直接崩。
	//    Optional 绑定的代价就是每个都得判，这是刻意接受的代价：
	//    换成 BindWidget（必需）的话，少摆一个控件会让整个 WBP
	//    编译不过，对"只想挪一下费用位置"的人太苛刻。

	// ══════════════════════════════════════════════════════════════
	// 外观（颜色/显隐/贴图）只在 C++ 接管时才写
	// ══════════════════════════════════════════════════════════════
	// ⚠️ 这个开关是"我在蓝图里改了颜色却没效果"的根因所在。
	//    ApplyView 由 RefreshFromGameMode 【每帧】调用（HUD 的 DrawHUD 里），
	//    所以无条件写颜色的话，蓝图里设好的颜色会被每帧覆盖 ——
	//    表现就是"改了完全没用"，且没有任何报错、日志也正常。
	//    关掉 bCppDrivesAppearance 后，外观完全归蓝图。
	const bool bApp = bCppDrivesAppearance;
	const bool bTxt = bCppDrivesText;

	// ── 卡框：按贴图原样画（不再按类型整块染色）
	if (Frame && bApp)
	{
		// ⚠️ 走虚函数而不是直接调 HexCardArt::GetFrame ——
		//    横版卡覆写了它去取 RCard_* 横版图。
		//    直接调的话横版卡会套上竖版卡框，比例一错整张卡拉伸变形。
		if (UTexture2D* Tex = GetCardFrameTexture())
		{
			Frame->SetBrushFromTexture(Tex, false);
			Frame->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			// 没有卡框资产时不画 —— 看得出缺图，但不崩
			Frame->SetVisibility(ESlateVisibility::Hidden);
		}

		// ⚠️ 这里【只有】美术在表里显式配了 FrameTint 才会不是白色。
		//    去掉类型染色的理由见 HexCardArt::GetFrameTint 的注释。
		FLinearColor Tint = HexCardArt::GetFrameTint(View.CardId, View.CardType);

		// 悬停时轻微提亮。
		// ⚠️ 幅度必须小（1.06 而不是原来的 1.22）：卡面现在是近白的纸，
		//    亮度已接近上限，1.22 会直接过曝成一片纯白，
		//    连卡框的纹理和边界都看不见。
		if (bHovered && View.bPlayable)
		{
			Tint = Tint * 1.06f;
			Tint.A = 1.0f;
		}
		Frame->SetColorAndOpacity(Tint);
	}

	// ── 插画
	if (Art && bApp)
	{
		if (UTexture2D* Tex = HexCardArt::GetArtwork(View.CardId))
		{
			Art->SetBrushFromTexture(Tex, false);
			Art->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			Art->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// ── 类型图标
	if (TypeIcon && bApp)
	{
		if (UTexture2D* Tex = HexCardArt::GetTypeIcon(View.CardId, View.CardType))
		{
			TypeIcon->SetBrushFromTexture(Tex, false);
			// 图标本身是彩色贴图，不染色；不可用时只降不透明度。
			// ⚠️ 不用压暗：卡面是浅色的，压暗的图标反而更显眼。
			TypeIcon->SetColorAndOpacity(
				View.bPlayable ? FLinearColor::White : FLinearColor(1.0f, 1.0f, 1.0f, 0.40f));
			TypeIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			// 技能/姿态/衍生/诅咒还没有图标 —— 折叠掉让文字上移，
			// 而不是留一块空白（空白看起来像 bug）。
			// 类型信息不会因此丢失：TypeLabel 那行文字仍在。
			TypeIcon->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// ── 费用
	if (Cost)
	{
		if (bTxt)
		{
			Cost->SetText(GetCardCostText());
		}
		if (bApp)
		{
			Cost->SetColorAndOpacity(FSlateColor(View.bPlayable ? L::ColCostEnergy : L::ColCostBad));
		}
	}

	// ── 卡名
	if (Name)
	{
		if (bTxt)
		{
			Name->SetText(GetCardName());
		}
		if (bApp)
		{
			Name->SetColorAndOpacity(FSlateColor(View.bPlayable ? L::ColInk : L::ColInkDisabled));
		}
	}

	// ── 类型文字（取代整卡染色的类型标识）
	if (TypeLabel)
	{
		const FString TypeName = HexCardArt::GetTypeName(View.CardType);
		if (bTxt)
		{
			TypeLabel->SetText(FText::FromString(TypeName));
		}
		if (bApp)
		{
			if (TypeName.IsEmpty())
			{
				TypeLabel->SetVisibility(ESlateVisibility::Collapsed);
			}
			else
			{
				// 类型色只用在这一小块文字上 —— 大面积用色就退回"彩色方块"了
				TypeLabel->SetColorAndOpacity(FSlateColor(View.bPlayable
					? HexCardArt::GetTypeAccent(View.CardType) : L::ColInkDisabled));
				TypeLabel->SetVisibility(ESlateVisibility::HitTestInvisible);
			}
		}
	}

	// ── 描述
	if (Desc)
	{
		if (bTxt)
		{
			Desc->SetText(GetCardDescription());
		}
		if (bApp)
		{
			Desc->SetColorAndOpacity(FSlateColor(
				View.bPlayable ? L::ColDescInk : L::ColInkDisabled));
		}
	}

	// ── 射程
	if (Range)
	{
		if (bTxt)
		{
			Range->SetText(GetCardRangeText());
		}
		if (bApp)
		{
			Range->SetColorAndOpacity(FSlateColor(L::ColInkSoft));
		}
	}

	// ── 角标：消耗优先于基石（消耗是"用完就没了"的强提醒）
	if (Tag)
	{
		const FText TagText = GetCardTagText();
		if (bTxt)
		{
			Tag->SetText(TagText);
		}
		if (bApp)
		{
			if (TagText.IsEmpty())
			{
				Tag->SetVisibility(ESlateVisibility::Collapsed);
			}
			else
			{
				Tag->SetColorAndOpacity(FSlateColor(
					View.bExhaust ? L::ColWarn : L::ColCornerstone));
				Tag->SetVisibility(ESlateVisibility::HitTestInvisible);
			}
		}
	}

	// ── 键盘提示
	if (Hotkey)
	{
		const FText KeyText = GetCardHotkeyText();
		if (bTxt)
		{
			Hotkey->SetText(KeyText);
		}
		if (bApp)
		{
			if (KeyText.IsEmpty())
			{
				Hotkey->SetVisibility(ESlateVisibility::Collapsed);
			}
			else
			{
				Hotkey->SetColorAndOpacity(FSlateColor(L::ColInkSoft));
				Hotkey->SetVisibility(ESlateVisibility::HitTestInvisible);
			}
		}
	}

	// ── 不可用遮罩（浅纱，不是压暗 —— 见 L::ColDimVeil 的注释）
	if (Dim && bApp)
	{
		Dim->SetVisibility(View.bPlayable
			? ESlateVisibility::Hidden : ESlateVisibility::HitTestInvisible);
	}

	// ⚠️ 选中不在这里画。它是 RenderTransform 上的上浮，由 NativeTick
	//    插值、ApplyTransform 写入。放在 ApplyView 里直接写 transform
	//    会和插值打架：ApplyView 每帧被 RefreshFromGameMode 调用，
	//    会把插值中间值反复重置，卡牌永远浮不起来。
	//
	//    但选中【放大】依赖 View.bSelected，所以 View 变化后要
	//    重算一次 transform，否则刚选中的那一帧缩放不会更新。
	ApplyTransform();
}
