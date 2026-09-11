// Copyright Hex Spire. All Rights Reserved.

#include "UI/HexHandPanelWidget.h"
#include "UI/HexCardWidget.h"
#include "UI/HexCardWidgetWide.h"
#include "UI/HexCardArt.h"
#include "View/HexDemoGameMode.h"

#include "Battle/HexBattleState.h"
#include "Battle/HexBattleFlow.h"
#include "Battle/HexUnit.h"
#include "Content/HexContentLibrary.h"
#include "Deck/HexPileManager.h"
#include "HexSpire.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Styling/CoreStyle.h"
#include "Kismet/GameplayStatics.h"


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

	WidgetTree->RootWidget = RootCanvas;

	return Super::RebuildWidget();
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

void UHexHandPanelWidget::RefreshFromGameMode(AHexDemoGameMode* Mode)
{
	// ⚠️ HandBox / FixedBox 只要求【至少有一个】存在。
	//    以前这里是 && 全都要有，改成 WBP 可绑定后那会让"只摆了手牌区、
	//    没摆固定卡区"的 WBP 整块界面不刷新 —— 一张卡都不显示。
	if (!Mode || (!HandBox && !FixedBox))
	{
		return;
	}

	const FHexBattleState* BS = Mode->GetBattleState();
	FHexBattleFlow* Flow = Mode->GetBattleFlow();

	// 不在战斗中：整块隐藏（地图界面仍由 HUD 用 Canvas 画）
	if (!BS || !Flow || !Mode->IsInBattle())
	{
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

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
