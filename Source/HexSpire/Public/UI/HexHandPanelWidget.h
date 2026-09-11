// Copyright Hex Spire. All Rights Reserved.
//
// 手牌区 + 固定卡区的容器控件
//
// ══════════════════════════════════════════════════════════════════
// 它取代了 HexDemoHUD 的 DrawHandPanel / DrawFixedCardPanel
// ══════════════════════════════════════════════════════════════════
// HUD 其余部分（顶栏、意图连线、伤害预览、牌堆浏览器、图例）继续用
// DrawHUD 画 —— 那些是【贴着世界坐标】的浮层，用 Canvas 更直接：
// 意图连线要从敌人画到目标格，UMG 做这个反而要自己算屏幕坐标。
//
// 只有卡牌换成 UMG，因为卡牌需要圆角、遮罩、贴图分层、动画，
// 那些是 Canvas 做不到的。
//
// ══════════════════════════════════════════════════════════════════
// 点击命中测试的归属（重要）
// ══════════════════════════════════════════════════════════════════
// ⚠️ 换成 UMG 后，命中测试【不再由 PlayerController 手算矩形】。
//    Slate 自己会做命中测试，卡牌控件收到点击后向上报。
//
//    这修掉了原本的一个结构性隐患：HUD 画卡、PlayerController 手算
//    同一套矩形，两边各写一遍坐标 —— 改动其一就会出现"看到的卡
//    和点到的卡错位"。现在布局只有 Slate 一个来源。
//
// ⚠️ 但棋盘的点击仍由 PlayerController 处理，所以顺序很关键：
//    卡牌控件必须【消费】掉落在自己身上的点击，否则点卡会穿透
//    到棋盘，被解释成"点了卡片背后那一格" ——
//    于是玩家点《防御》却把角色移走了。
//    这由 OnMouseButtonDown 返回 Handled 保证。
//
// ══════════════════════════════════════════════════════════════════
// 卡面版式可以交给 WidgetBlueprint（不必改 C++）
// ══════════════════════════════════════════════════════════════════
// 默认用 C++ 版 UHexCardWidget 建卡。但只要工程里存在
//   /Game/HexSpire/UI/WBP_HexCard      （父类 = HexCardWidget）
// 本控件就【自动】改用它，不需要改任何代码 —— 见 ResolveCardClass。
//
// 同理，整块手牌区的布局（手牌居中横排、固定卡左侧竖排、间距、锚点）
// 也能换成 WBP：以本类为父类建 WBP_HexHandPanel，在设计器里摆两个容器，
// 命名成 HandBox / FixedBox / FixedTitle 即可被绑定。
//
// ⚠️ 手牌区的 WBP 需要 HUD 那边改一行去加载它（EnsureHandPanel 里
//    CreateWidget 的类），而卡牌的 WBP 不需要 —— 因为卡是本控件在
//    运行时建的，而手牌区本身是 HUD 建的。
//    这个不对称是刻意的：卡面版式改得频繁，手牌区布局几乎不动。

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/HexCardWidget.h"
#include "HexHandPanelWidget.generated.h"

class AHexDemoGameMode;
class UHorizontalBox;
class UVerticalBox;
class UCanvasPanel;
class UTextBlock;

/**
 * 手牌 + 固定卡的容器。
 *
 * 每帧由 HUD 调用 RefreshFromGameMode()，按逻辑状态同步卡牌。
 *
 * ⚠️ 控件是【复用】的而非每帧重建：手牌张数变化时才增删控件，
 *    否则只刷新内容。每帧重建会让 Slate 布局缓存每帧失效。
 */
UCLASS(Blueprintable, BlueprintType)
class HEXSPIRE_API UHexHandPanelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UHexHandPanelWidget(const FObjectInitializer& ObjectInitializer);

	/** 按当前逻辑状态刷新全部卡牌 */
	void RefreshFromGameMode(AHexDemoGameMode* Mode);

	/**
	 * 手牌区自己该用哪个类建（给 HUD 调）。
	 *
	 * 与卡面同一套规则：存在 WB_HandPanel 就用它，否则用 C++ 版。
	 * 这样"手牌怎么排列"也能在蓝图里改 —— 间距、锚点、
	 * 手牌居中还是靠右、固定卡放左边还是下边，都不必碰代码。
	 *
	 * ⚠️ 必须是 static：HUD 要在【创建控件之前】就知道用哪个类，
	 *    那时还没有实例可用。
	 */
	static UClass* ResolvePanelClass();

	/**
	 * 建卡用的控件类。可在 WBP 子类里指定自己的卡牌 WBP。
	 *
	 * ⚠️ 留空时【自动去找】/Game/HexSpire/UI/WBP_HexCard（见 ResolveCardClass），
	 *    找不到才回退到 C++ 版 UHexCardWidget。
	 *    这条自动发现是"美术不碰 C++ 也能改版式"的关键：
	 *    按约定命名建一个 WBP 就生效，不需要谁来改代码或配蓝图。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "卡牌")
	TSubclassOf<UHexCardWidget> CardWidgetClass;

	/**
	 * 固定卡（左侧横版卡）用的控件类。
	 *
	 * 留空时自动找 /Game/HexSpire/UI/WB_CardWide，
	 * 找不到才回退到 C++ 版 UHexCardWidgetWide。
	 *
	 * ⚠️ 与手牌分开是刻意的：固定卡是【横版】（196x100），
	 *    手牌是竖版（168x232），两套版式的控件树结构不同
	 *    （横版是图标在左、文字在右的两栏；竖版是一列竖排）。
	 *    共用一个类的话，控件蓝图里无法区分该摆哪一套。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "卡牌")
	TSubclassOf<UHexCardWidget> FixedCardWidgetClass;

	/**
	 * 设计器里预览几张假卡。
	 *
	 * ⚠️ 存在的理由：手牌是【运行时】按实际手牌数量建出来的，
	 *    所以设计器里 HandBox 永远是空的 —— 你在里面调间距、
	 *    改锚点、把手牌挪到靠右，全都看不到效果，只能盲摆。
	 *    这一条让设计器里出现 N 张占位卡，所见即所得。
	 *
	 * ⚠️ 只在设计器生效（NativePreConstruct 里判 IsDesignTime），
	 *    运行时一张都不建 —— 否则会多出几张点不动的假卡。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "预览",
		meta = (ClampMin = "0", ClampMax = "10"))
	int32 DesignPreviewCardCount = 4;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	/** 设计器预览用。运行时不做任何事。 */
	virtual void NativePreConstruct() override;

	// ── 容器。WBP 子类里同名控件会被自动绑定（见 BindWidgetOptional）。
	//
	// ⚠️ 放在 protected 而不是 private：UHT 不允许 private 成员带
	//    BlueprintReadOnly（会直接编译报错），而 BindWidget 系列
	//    通常要配合它才能在 WBP 图表里读到。
	//
	// ⚠️ 与卡牌控件一样用 Optional：WBP 里只摆了手牌横排、没摆固定卡区时，
	//    FixedBox 是 nullptr，此时固定卡不显示但不崩。
	//    RefreshFromGameMode 因此必须逐个判空，不能"判一个当全有"。

	/** 手牌：底部横排 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Transient)
	UHorizontalBox* HandBox = nullptr;

	/** 固定卡：左侧竖排 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Transient)
	UVerticalBox* FixedBox = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Transient)
	UTextBlock* FixedTitle = nullptr;


private:
	/**
	 * 取或建第 Index 个卡控件。
	 * @param Container 放进哪个容器（手牌横排 / 固定卡竖排）
	 * @param Pool      对应的控件池
	 */
	UHexCardWidget* GetOrCreateCard(
		UPanelWidget* Container, TArray<UHexCardWidget*>& Pool, int32 Index,
		UClass* CardClass);

	/**
	 * 定下用哪个类建卡：显式指定 → 约定路径的 WBP → C++ 版。
	 *
	 * ⚠️ 只解析一次并缓存进 ResolvedCardClass。
	 *    每次建卡都 LoadClass 一遍的话，WBP 不存在时会走一次
	 *    完整的失败查找 —— 手牌每回合增删控件，那是可测的开销。
	 */
	UClass* ResolveCardClass();

	/** 同上，但解析固定卡用的【横版】控件类 */
	UClass* ResolveFixedCardClass();

	/** 把池里多出来的控件折叠掉（不销毁，留着复用） */
	static void HideExtra(TArray<UHexCardWidget*>& Pool, int32 UsedCount);

	/**
	 * 首次刷新后把关键事实写进日志（只写一次）。
	 *
	 * ⚠️ 存在的理由：UMG 的失败几乎全是静默的（贴图路径错、控件
	 *    没进视口、字体缺字形），而截图也验证不了 ——
	 *    -dumpmovie 只抓场景渲染，不含 Slate 层。
	 *    让控件自报家门是唯一能在命令行里验证的办法。
	 */
	void LogSelfCheckOnce();

	bool bSelfCheckLogged = false;

	/** 刷新次数。自检要等布局完成（前几帧 GetCachedGeometry 是全零）。 */
	int32 RefreshCount = 0;

	/** 手牌卡控件池 */
	UPROPERTY(Transient) TArray<UHexCardWidget*> HandCards;

	/** 固定卡控件池 */
	UPROPERTY(Transient) TArray<UHexCardWidget*> FixedCards;

	/** ResolveCardClass 的缓存结果 */
	UPROPERTY(Transient) UClass* ResolvedCardClass = nullptr;

	/** ResolveFixedCardClass 的缓存结果 */
	UPROPERTY(Transient) UClass* ResolvedFixedCardClass = nullptr;

	UPROPERTY(Transient) UCanvasPanel* RootCanvas = nullptr;

};
