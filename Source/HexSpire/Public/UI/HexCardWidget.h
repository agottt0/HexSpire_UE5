// Copyright Hex Spire. All Rights Reserved.
//
// 单张卡牌的 UMG 控件
//
// ══════════════════════════════════════════════════════════════════
// 为什么默认是 C++ 控件，但又允许 WidgetBlueprint 覆盖
// ══════════════════════════════════════════════════════════════════
// WBP 是二进制 .uasset：改一个边距要开编辑器、拖控件、保存、提交二进制 diff。
// 灰盒期卡面布局还在调（费用圆盘多大、描述几行、图标占多少），
// 每次迭代都走那条路太慢，且 code review 看不到改了什么。
// 所以【默认】走纯 C++ 控件树：改一行代码 → 重编译 → 立刻看到。
//
// 但美术/策划要自己调版式时，C++ 就成了瓶颈。所以本类同时支持
// 被 WidgetBlueprint 继承：
//
//   1. 编辑器里 New → Widget Blueprint → 父类选 HexCardWidget
//   2. 存成 /Game/HexSpire/UI/WBP_HexCard
//   3. 在设计器里随便摆，控件【命名】成下面 BindWidgetOptional 的名字
//      （Frame / Art / TypeIcon / Cost / Name / Desc / Range / Tag / Dim …）
//   4. 不需要改任何 C++ —— 手牌区会自动发现这个资产并用它建卡
//
// ⚠️ 判据是「设计器里有没有摆控件」：
//    · 设计器树非空 → 代码完全不建控件，只往 Bind 到的控件里灌数据
//    · 设计器树为空 → 代码建默认控件树（即现在的样子）
//    这样空的 WBP 子类也能正常显示，不会得到一张白板。
//
// ⚠️ Bind 是 Optional 的：漏命名某个控件不会导致 CreateWidget 失败，
//    只是那一项不显示。若用 BindWidget（非 Optional），少一个控件
//    会让整个 WBP 编译报错 —— 对"只想挪一下费用位置"的人太苛刻。
//
// ══════════════════════════════════════════════════════════════════
// 布局（用户决策：体力费用移到左上角）
// ══════════════════════════════════════════════════════════════════
//   ┌──────────────┐
//   │(1)           │  ← 左上：体力费用，圆形底衬
//   │              │
//   │    [图标]     │  ← 中间上部：类型图标
//   │              │
//   │    盾 击      │  ← 卡名
//   │ 造成18点伤害   │  ← 描述（实算值）
//   │ 射程 1-1      │
//   └──────────────┘
//
// ⚠️ 左上角放费用而非手牌序号，是因为费用是【每次出牌都要读】的信息，
//    而序号只在用键盘时有用。序号改画在卡底，不抢左上角。
//
// ⚠️ 费用数字底下必须有圆形底衬。美术文档 §12 提醒过：
//    卡面数值是动态的（ATK 成长后伤害会变三位数），
//    插画必须给数字留低对比区域 —— 底衬就是那个区域。
//
// ══════════════════════════════════════════════════════════════════
// 卡面不做整块染色（用户决策）
// ══════════════════════════════════════════════════════════════════
// 之前按 CardType 把整张卡框乘成红/蓝/绿，观感是"彩色方块"。
// 现在卡框按贴图原样画，类型只由【图标 + 类型标签】传达。
//
// ⚠️ 这带来一个必须一起改的连带后果：卡面从深色变成浅色（贴图本身
//    是接近白的纸面，实测中心亮度 ~236/255），原来那套浅色文字
//    （0.94 的米白）压在白纸上等于隐形。所以文本色全部换成深墨色。
//    只改染色不改文字色的话，会得到一张"什么都看不见"的白卡。
//
// ⚠️ 不可用状态也随之反转：深色卡靠"压暗"表示禁用，浅色卡要靠
//    "冲淡"（盖一层浅灰纱）。在白纸上再压暗会变成灰卡，反而更抢眼。
//
// ══════════════════════════════════════════════════════════════════
// 选中表现：上浮（用户决策，取代黄色描边）
// ══════════════════════════════════════════════════════════════════
// 用 RenderTransform 平移，【不是】改布局：
//   · 布局尺寸不变 → 手牌横排不会因为选中而整排抖动重排
//   · 悬停 8px、选中 26px，两级区分开，玩家能分清"我正在看"和"我已选"
//   · 选中额外放大 4% —— 上限刻意压在 4%：手牌间距只有 4px，
//     再大就会盖住邻卡的边。
//
// ⚠️ 上浮方向是 -Y（屏幕向上）。手牌锚在屏幕底边，
//    +Y 会把卡推出屏幕外，只能看到卡的上半截。

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Core/HexSpireEnums.h"
#include "UI/HexCardLayout.h"
#include "HexCardWidget.generated.h"

class UBorder;
class UImage;
class UOverlay;
class USizeBox;
class UTextBlock;
class UVerticalBox;
class FHexUnit;
struct FHexCardData;

/** 卡牌被点击。参数是卡实例 uid。 */
DECLARE_DELEGATE_OneParam(FHexOnCardClicked, int32);

/**
 * 卡面的一次性数据快照。
 *
 * ⚠️ 用快照而不是让控件自己去查逻辑层，是纪律 3 的要求：
 *    表现层不主动驱动逻辑。同时也避免控件持有
 *    FHexCardData* —— 那个指针会在 DataTable 覆写时失效。
 */
struct FHexCardView
{
	FName CardId;
	FString DisplayName;
	FString Description;
	EHexCardType CardType = EHexCardType::Attack;
	EHexRarity Rarity = EHexRarity::Common;

	/** 实际费用（已含符文/状态的增减，非卡定义里的基础值） */
	int32 Cost = 1;

	int32 RangeMin = 0;
	int32 RangeMax = 1;

	/** 键盘序号。<=0 表示不显示。 */
	int32 HotkeyNumber = 0;
	/** 固定卡的按键（Q/W/E）。空则不显示。 */
	FString HotkeyLabel;

	bool bPlayable = true;
	bool bSelected = false;
	bool bExhaust = false;
	bool bCornerstone = false;

	/** 由卡定义 + 当前英雄属性构造（描述里的数值会实算） */
	static FHexCardView Make(
		const FHexCardData& Card, const FHexUnit* Hero,
		int32 InCost, bool bInPlayable, bool bInSelected);
};

/**
 * 一张卡的控件。
 *
 * 用法（C++ 默认版）：
 *   UHexCardWidget* W = CreateWidget<UHexCardWidget>(PC, UHexCardWidget::StaticClass());
 *   W->SetCardView(View);
 *
 * 用法（WBP 版）：把 /Game/HexSpire/UI/WBP_HexCard 的父类设成本类即可，
 * 手牌区会自动优先使用它，见 UHexHandPanelWidget::ResolveCardClass。
 *
 * ⚠️ 控件树在 RebuildWidget 里建一次，SetCardView 只改内容不重建结构 ——
 *    每帧重建控件树会让 Slate 的布局缓存每帧失效，手牌一多就掉帧。
 */
UCLASS(Blueprintable, BlueprintType)
class HEXSPIRE_API UHexCardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UHexCardWidget(const FObjectInitializer& ObjectInitializer);

	// 卡面尺寸 —— 转发 HexCardLayout，不在这里重复定义数值。
	// ⚠️ 布局数值只能有一个来源：控件蓝图生成器读的也是 HexCardLayout，
	//    这里若另写一份，改一处忘一处就会让蓝图和 C++ 版尺寸不一致。
	static constexpr float CardWidth = HexCardLayout::CardWidth;
	static constexpr float CardHeight = HexCardLayout::CardHeight;

	/** 悬停上浮高度（像素） */
	static constexpr float HoverLift = 8.0f;
	/** 选中上浮高度（像素）。比悬停明显高一档，两种状态才分得清。 */
	static constexpr float SelectLift = 26.0f;
	/** 选中额外放大。手牌间距只有 4px，超过 1.04 会盖住邻卡。 */
	static constexpr float SelectScale = 1.04f;

	/** 刷新卡面内容。可每帧调用（只改属性，不重建控件）。 */
	void SetCardView(const FHexCardView& InView);

	// ══════════════════════════════════════════════════════════════
	// 给蓝图【绑定】用的取值函数（DataTable → 卡面文字）
	// ══════════════════════════════════════════════════════════════
	// 用法（在控件蓝图里）：选中 TextBlock → Content 分类的 Text 属性
	// 右侧点 Bind → 选下面对应的函数。之后文字就由数据驱动，
	// 不再需要 C++ 逐个 SetText。
	//
	// ⚠️ 这些值全部【来自 DataTable】：
	//    DT_Cards 的 DisplayName / DescriptionTemplate
	//      → 启动时 FHexCardTableLoader 覆写进 FHexContentLibrary
	//      → FHexCardView::Make() 取出（描述里的 {dmg} 按当前属性实算）
	//      → 这里返回给蓝图
	//    所以改文案只改表，不用碰蓝图也不用编译。
	//
	// ⚠️ 必须是 BlueprintPure（纯函数）。UMG 的属性绑定只接受纯函数，
	//    带副作用的函数在 Bind 下拉里【根本不会出现】——
	//    而 UI 上不会提示为什么，只是列表里找不到，很容易以为没做。

	/** 卡名（DataTable 的 DisplayName） */
	UFUNCTION(BlueprintPure, Category = "卡牌|文字")
	FText GetCardName() const;

	/** 描述（DataTable 的 DescriptionTemplate，占位符已实算） */
	UFUNCTION(BlueprintPure, Category = "卡牌|文字")
	FText GetCardDescription() const;

	/** 体力费用（含符文/状态增减后的实际值） */
	UFUNCTION(BlueprintPure, Category = "卡牌|文字")
	FText GetCardCostText() const;

	/** 射程，形如 "射程 1-1" */
	UFUNCTION(BlueprintPure, Category = "卡牌|文字")
	FText GetCardRangeText() const;

	/** 类型名（攻击/守备/移动…） */
	UFUNCTION(BlueprintPure, Category = "卡牌|文字")
	FText GetCardTypeText() const;

	/** 角标文字（消耗/基石）。都不是时返回空。 */
	UFUNCTION(BlueprintPure, Category = "卡牌|文字")
	FText GetCardTagText() const;

	/** 快捷键提示，形如 "[1]" 或 "[Q]"。没有则返回空。 */
	UFUNCTION(BlueprintPure, Category = "卡牌|文字")
	FText GetCardHotkeyText() const;

	// ── 状态查询（给蓝图做条件染色/显隐用）

	UFUNCTION(BlueprintPure, Category = "卡牌|状态")
	bool IsCardPlayable() const { return View.bPlayable; }

	UFUNCTION(BlueprintPure, Category = "卡牌|状态")
	bool IsCardSelected() const { return View.bSelected; }

	UFUNCTION(BlueprintPure, Category = "卡牌|状态")
	bool IsCardHovered() const { return bHovered; }

	/** 类型强调色 —— 蓝图可绑到类型文字的颜色上 */
	UFUNCTION(BlueprintPure, Category = "卡牌|状态")
	FLinearColor GetCardTypeAccent() const;

	/** 卡面贴图（卡框/插画/图标），供蓝图绑 Brush 用 */
	/**
	 * 卡框贴图。
	 *
	 * ⚠️ virtual：横版卡覆写它去取 RCard_* 那三张横版图。
	 *    ApplyView 只调这一个函数，所以子类换掉它就够了，
	 *    不需要把整个 ApplyView 复制一遍（复制会导致两份逻辑漂移）。
	 */
	UFUNCTION(BlueprintPure, Category = "卡牌|美术")
	virtual UTexture2D* GetCardFrameTexture() const;

	UFUNCTION(BlueprintPure, Category = "卡牌|美术")
	UTexture2D* GetCardTypeIconTexture() const;

	UFUNCTION(BlueprintPure, Category = "卡牌|美术")
	UTexture2D* GetCardArtworkTexture() const;

	/**
	 * C++ 是否接管卡面的【外观】（颜色/显隐/贴图）。
	 *
	 * ⚠️ 这是"我在蓝图里改了颜色却没效果"的开关。
	 *    默认 true：C++ 每帧把颜色/显隐/贴图刷一遍，
	 *    于是【蓝图里设的颜色会被每帧覆盖掉】——
	 *    看起来就是"改了完全没用"，而且没有任何报错。
	 *
	 *    在控件蓝图的 Class Defaults 里把它关掉，C++ 就只负责
	 *    文本内容与布局无关的数据，外观完全交给蓝图（配合上面那些
	 *    BlueprintPure 做属性绑定）。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "卡牌|外观")
	bool bCppDrivesAppearance = true;

	/**
	 * C++ 是否接管卡面【文字内容】。
	 *
	 * 默认 true。若你在蓝图里把 Text 属性 Bind 到了上面那些取值函数，
	 * 建议关掉这个 —— 否则 C++ 的 SetText 与蓝图的绑定会各写一次，
	 * 虽然内容一样（同源），但白做两遍。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "卡牌|外观")
	bool bCppDrivesText = true;

	const FHexCardView& GetCardView() const { return View; }

	/**
	 * 点击回调。
	 *
	 * ⚠️ 必须由控件【消费】点击（返回 Handled），否则点击会穿透到
	 *    棋盘，被 PlayerController 解释成"点了卡片背后那一格" ——
	 *    玩家点《防御》却把角色移走了。
	 */
	FHexOnCardClicked OnCardClicked;

	/** 卡实例 uid —— 点击时回传给 GameMode */
	void SetCardUid(int32 InUid) { CardUid = InUid; }
	int32 GetCardUid() const { return CardUid; }

	/**
	 * 报告哪些控件【没有】绑上（返回缺失控件名，逗号分隔；全绑上则为空）。
	 *
	 * ⚠️ 这是控件蓝图路径唯一可靠的验证手段。
	 *    BindWidget 是【纯按名字匹配】的，蓝图里控件名写错一个字母，
	 *    那个控件就永远收不到数据 —— 费用空白、卡名空白。
	 *    而 BindWidgetOptional 在编译期只产生一条 Note，不报错也不警告，
	 *    运行时更是完全静默。靠肉眼看卡面来发现"少了一项"极不可靠。
	 */
	TArray<FString> GetUnboundWidgetNames() const;

	/**
	 * 描述卡名控件实际生效的字体（自检用）。
	 *
	 * ⚠️ 字体问题只能这样查：缺字形时 UMG 画空白、不报错，
	 *    而截图也分不清"没字体"和"字被裁掉了"。
	 */
	FString DescribeFont() const;

	/**
	 * 当前上浮高度（像素）。
	 *
	 * ⚠️ 只给自检日志用。上浮是"选中"的唯一视觉反馈，而它依赖
	 *    NativeTick 插值 —— 这条链路断掉时不报错、截图也未必看得出，
	 *    所以必须能在命令行里读到实际位移量。
	 */
	float GetCurrentLift() const { return CurrentLift; }

	/**
	 * 基础缩放。固定卡用它整体缩小一圈。
	 *
	 * ⚠️ 必须走这里而不是外部直接调 SetRenderScale：
	 *    选中上浮也在改 RenderTransform，两边各写一次会互相覆盖 ——
	 *    固定卡一选中就会跳回原始大小。
	 *    统一由 ApplyTransform 把「基础缩放 × 选中放大」合成一次写入。
	 */
	void SetBaseScale(float InScale);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	virtual FReply NativeOnMouseButtonDown(
		const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	virtual void NativeOnMouseEnter(
		const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// ══════════════════════════════════════════════════════════════
	// WBP 可绑定的控件
	// ══════════════════════════════════════════════════════════════
	// ⚠️ 全部 Optional。在 WBP 里把控件命名成这些名字即可被填充；
	//    没命名的项不显示，但不会让 WBP 编译失败。
	//
	// ⚠️ 同一批指针也被 C++ 默认控件树复用（RebuildWidget 里 new 出来
	//    赋给同样的成员）。两条路径共用 ApplyView，所以布局换成 WBP 后
	//    数据填充逻辑一行都不用改。

	/** 卡框贴图 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Transient)
	UImage* Frame = nullptr;

	/** 卡面插画（目前多数卡没有，会隐藏） */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Transient)
	UImage* Art = nullptr;

	/** 类型图标 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Transient)
	UImage* TypeIcon = nullptr;

	/** 左上角费用的圆形底衬 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Transient)
	UBorder* CostBadge = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Transient)
	UTextBlock* Cost = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Transient)
	UTextBlock* Name = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Transient)
	UTextBlock* Desc = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Transient)
	UTextBlock* Range = nullptr;

	/** 类型名（攻击/守备/移动…）。取代整卡染色后的类型标识。 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Transient)
	UTextBlock* TypeLabel = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Transient)
	UTextBlock* Hotkey = nullptr;

	/** 消耗/基石角标 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Transient)
	UTextBlock* Tag = nullptr;

	/** 打不出时盖上的一层浅纱 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Transient)
	UBorder* Dim = nullptr;

protected:
	/** 按当前 View 把颜色/文本刷到各控件上 */
	void ApplyView();

	// ── C++ 默认控件树的根。控件蓝图路径下恒为 nullptr。
	//
	// ⚠️ protected 而非 private：横版子类要在自己的 BuildDefaultTree
	//    里给它们赋值。
	UPROPERTY(Transient) USizeBox* Root = nullptr;
	UPROPERTY(Transient) UOverlay* MainOverlay = nullptr;

	/**
	 * 建 C++ 默认控件树（设计器为空时才走）。
	 *
	 * ⚠️ virtual：横版卡（UHexCardWidgetWide）覆写它来搭左右两栏的树。
	 *    子类只需换这一个函数 —— 点击、上浮、DataTable 取值、
	 *    绑定自检全部继承，不必重写。
	 */
	virtual void BuildDefaultTree();

private:

	/**
	 * 把「基础缩放 × 选中放大」和「悬停/选中上浮」合成一次 RenderTransform。
	 *
	 * ⚠️ 平移量要手动乘上 BaseScale。FWidgetTransform::ToSlateRenderTransform
	 *    是 Concatenate(Scale, Shear, Rot, Translation) —— 平移在缩放【之后】
	 *    应用，所以引擎不会替你缩放它：0.72 的固定卡填 26px 就真的位移 26px。
	 *    那对一张小一圈的卡来说相对幅度过大，看着像"弹出去了"。
	 *    乘上 BaseScale 后，上浮距离与卡自身尺寸成比例，两处观感一致。
	 */
	void ApplyTransform();

	FHexCardView View;

	/** 上浮的当前插值位置（像素）。NativeTick 里趋近目标值。 */
	float CurrentLift = 0.0f;

	/** 基础缩放。固定卡 0.72，手牌 1.0。 */
	float BaseScale = 1.0f;



	int32 CardUid = 0;

	/** 鼠标悬停中 */
	bool bHovered = false;
};
