// Copyright Hex Spire. All Rights Reserved.
//
// 横版卡（左侧固定卡专用）
//
// ══════════════════════════════════════════════════════════════════
// 为什么单独一个类，而不是给 UHexCardWidget 加个 bWide 开关
// ══════════════════════════════════════════════════════════════════
// 因为两者的【控件树结构】不同，不只是尺寸不同：
//   竖版：图标在上、卡名、类型、描述、射程 —— 一列竖排
//   横版：图标在左、卡名+描述在右 —— 左右两栏
//
// 用一个类加开关的话，BuildDefaultTree 里会出现
// "if (bWide) 建这棵树 else 建那棵树" 的分叉，而两棵树共用同一批
// BindWidgetOptional 成员 —— 于是控件蓝图里【无法区分】该摆哪一套，
// 生成器也要跟着分叉。分成两个类之后，一个类对应一套版式、
// 对应一个控件蓝图，关系是清楚的。
//
// ⚠️ 继承 UHexCardWidget 而不是从 UUserWidget 重写：
//    点击处理、上浮动画、DataTable 取值函数（GetCardName 等）、
//    绑定自检 —— 全部直接复用。横版只覆写"控件树怎么搭"。
//
// ══════════════════════════════════════════════════════════════════
// 贴图
// ══════════════════════════════════════════════════════════════════
// 用 /Game/ArtResource/Card/RCard_Attack|RCard_Defend|RCard_Move
// （256x130，比例约 1.95）。中间是留白区，文字画在那里。
//
// ⚠️ 这三张是按【卡类型】取的，不是按卡 Id。
//    固定卡就是攻击/守备/移动三张，与类型一一对应。
//    以后固定卡种类变多时，改 HexCardArt::GetWideFrame 即可。

#pragma once

#include "CoreMinimal.h"
#include "UI/HexCardWidget.h"
#include "HexCardWidgetWide.generated.h"

class UHorizontalBox;

/**
 * 横版卡。
 *
 * 用法与竖版一致：手牌区会自动发现
 * /Game/HexSpire/UI/WB_CardWide（父类选 HexCardWidgetWide），
 * 找不到就用这里的 C++ 默认版式。
 */
UCLASS(Blueprintable, BlueprintType)
class HEXSPIRE_API UHexCardWidgetWide : public UHexCardWidget
{
	GENERATED_BODY()

public:
	UHexCardWidgetWide(const FObjectInitializer& ObjectInitializer);

	/** 横版卡尺寸（按贴图比例，见 HexCardLayout::R） */
	static constexpr float WideWidth = HexCardLayout::R::CardWidth;
	static constexpr float WideHeight = HexCardLayout::R::CardHeight;

	/** 横版卡框（RCard_Attack / RCard_Defend / RCard_Move） */
	virtual UTexture2D* GetCardFrameTexture() const override;

protected:
	/**
	 * 搭横版控件树。
	 *
	 * ⚠️ 覆写的是 BuildDefaultTree 而不是 RebuildWidget ——
	 *    父类的 RebuildWidget 里有"设计器已摆好树就不要重建"的判断，
	 *    绕过它会让控件蓝图的布局在运行时被顶掉。
	 */
	virtual void BuildDefaultTree() override;
};
