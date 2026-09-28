// Copyright Hex Spire. All Rights Reserved.
//
// 符文槽控件 —— 符文面板里的一格（§6.5）
//
// 与 UHexCardWidget 同一套约定：
//   · 控件树在 RebuildWidget 里建一次（C++ 默认树），
//     存在 /Game/HexSpire/UI/WBP_HexRuneSlot（父类 = 本类）时
//     由 HandPanel 自动改用它，同名控件经 BindWidgetOptional 绑定
//   · 点击上报给面板（DECLARE_DELEGATE），自己不动任何逻辑状态
//   · 全部绑定控件 Optional：美术删掉某个元素不显示但不崩
//
// ⚠️ 符文与卡牌刻意【不共用】控件类：卡有费用/目标/描述模板，
//    符文有槽位号/类别/机制文本，两套控件树结构不同 ——
//    与"固定卡为什么单开横版类"同一个理由（HexHandPanelWidget.h）。

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HexRuneSlotWidget.generated.h"

class UBorder;
class UTextBlock;
class UImage;
class UTexture2D;
struct FHexRuneData;

/** 槽位被点击（参数 = 槽位下标 0..5） */
DECLARE_DELEGATE_OneParam(FHexOnRuneSlotClicked, int32);

UCLASS(Blueprintable, BlueprintType)
class HEXSPIRE_API UHexRuneSlotWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UHexRuneSlotWidget(const FObjectInitializer& ObjectInitializer);

	/**
	 * 按符文数据刷新。
	 * @param InSlotIndex   槽位下标 0..5（显示为 1..6）
	 * @param Rune          nullptr = 空槽（「空槽是邀请」，§6.2）
	 * @param bLocked       战斗中锁定（只影响显示，逻辑锁在 RunState）
	 * @param bSelected     重排时的"已选中待交换"高亮
	 * @param FlashStrength 触发脉冲强度 0..1（1 = 刚触发；驱动放大+金闪）
	 */
	void SetRune(int32 InSlotIndex, const FHexRuneData* Rune,
		bool bLocked, bool bSelected, float FlashStrength = 0.0f);

	int32 GetSlotIndex() const { return SlotIndex; }

	/** 面板绑定：点击上报 */
	FHexOnRuneSlotClicked OnSlotClicked;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	virtual FReply NativeOnMouseButtonDown(
		const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	/** 悬停：目标缩放抬到 1.22（放大看详细描述），离开缩回 */
	virtual void NativeOnMouseEnter(
		const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;

	/** 缩放插值（悬停放大 / 触发脉冲共用同一条渲染缩放通道） */
	virtual void NativeTick(
		const FGeometry& MyGeometry, float InDeltaTime) override;

	// ── 绑定控件（WBP 子类里同名控件自动绑定；C++ 默认树复用同一批指针）

	/**
	 * 卡面美术（TCard_1..TCard_6，按槽位号取）。
	 *
	 * ⚠️ 贴图按【槽位】不按符文：6 张 TCard 是 6 个槽的卡面，
	 *    符文换槽时卡面不跟着走 —— 这让"第 3 槽"有稳定的视觉锚点，
	 *    重排时玩家能靠卡面认出"我把它挪到了哪"。
	 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Transient)
	UImage* CardArt = nullptr;

	/** 外框。选中态的金色蒙层落在它上（平时全透明）。 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Transient)
	UBorder* SlotBorder = nullptr;

	/** 槽位号（"1"…"6"） */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Transient)
	UTextBlock* SlotNum = nullptr;

	/** 符文名。空槽显示"—"。 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Transient)
	UTextBlock* RuneName = nullptr;

	/** 类别短标（"规则" / "触发" / "条件" / "乘区" / "诅咒"） */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Transient)
	UTextBlock* RuneCategory = nullptr;

private:
	int32 SlotIndex = 0;

	/** 卡面贴图缓存（每帧刷新时不重复 LoadObject） */
	UPROPERTY(Transient)
	UTexture2D* CachedArt = nullptr;

	int32 CachedArtIndex = -1;

	// ── 渲染缩放动画（悬停放大 + 触发脉冲）
	bool bHovered = false;
	float LastFlash = 0.0f;
	float CurrentScale = 1.0f;
};
