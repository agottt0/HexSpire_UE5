// Copyright Hex Spire. All Rights Reserved.
//
// 卡面布局的【唯一数据源】
//
// ══════════════════════════════════════════════════════════════════
// 为什么要抽出这个文件
// ══════════════════════════════════════════════════════════════════
// 卡面布局有两条消费路径：
//   ① 运行时：UHexCardWidget::BuildDefaultTree() 搭 C++ 控件树
//   ② 生成时：HexBuildCardWidget commandlet 把树写进控件蓝图资产
//
// 两条路径必须产出【外观一致】的卡面 —— 因为没建蓝图时跑 ①，
// 建了蓝图时跑 ②。如果各写一份边距/字号，改了一处忘了另一处，
// 就会出现"美术在蓝图里看到的和玩家看到的不一样"，
// 而这种不一致不会报错，只能靠眼睛发现。
//
// 所以数值全部集中在这里，两边都只读它。
//
// ⚠️ 控件【名字】也在这里定义，且必须与 UHexCardWidget 上
//    BindWidgetOptional 属性的名字【逐字一致】。
//    BindWidget 的绑定是【纯按名字匹配】的（引擎在
//    WidgetBlueprintGeneratedClass::InitializeWidgetStatic 里
//    拿控件 FName 去查同名 UPROPERTY），既不看类型标注也不看别的。
//    名字写错的后果：那个控件在蓝图里存在、看得见、能拖，
//    但运行时【永远不会被填数据】—— 费用是空的、卡名是空的。
//    而且编译器对 Optional 绑定只给一条 Note，不报错也不警告。
//    这是本文件里最容易出、也最难查的错，所以名字用常量而不是字面量。

#pragma once

#include "CoreMinimal.h"

namespace HexCardLayout
{
	// ══════════════════════════════════════════════════════════════
	// 控件名 —— 必须与 UHexCardWidget 的 BindWidgetOptional 属性同名
	// ══════════════════════════════════════════════════════════════
	namespace N
	{
		inline const TCHAR* Root      = TEXT("CardRoot");
		inline const TCHAR* Overlay   = TEXT("CardOverlay");
		inline const TCHAR* Body      = TEXT("Body");

		inline const TCHAR* Frame     = TEXT("Frame");
		inline const TCHAR* Art       = TEXT("Art");
		inline const TCHAR* TypeIcon  = TEXT("TypeIcon");
		inline const TCHAR* CostBadge = TEXT("CostBadge");
		inline const TCHAR* Cost      = TEXT("Cost");
		inline const TCHAR* Name      = TEXT("Name");
		inline const TCHAR* TypeLabel = TEXT("TypeLabel");
		inline const TCHAR* Desc      = TEXT("Desc");
		inline const TCHAR* Range     = TEXT("Range");
		inline const TCHAR* Tag       = TEXT("Tag");
		inline const TCHAR* Hotkey    = TEXT("Hotkey");
		inline const TCHAR* Dim       = TEXT("Dim");
	}

	// ══════════════════════════════════════════════════════════════
	// 尺寸与边距
	// ══════════════════════════════════════════════════════════════

	/** 卡面尺寸。与 HUD 的手牌区一致。 */
	inline constexpr float CardWidth  = 168.0f;
	inline constexpr float CardHeight = 232.0f;

	/** 插画区：给上方费用条和下方文字区留出空间 */
	inline const FMargin ArtPad  = FMargin(8.0f, 30.0f, 8.0f, 74.0f);
	/** 正文区（图标+卡名+类型+描述+射程） */
	inline const FMargin BodyPad = FMargin(10.0f, 34.0f, 10.0f, 8.0f);

	/** 左上角费用底衬的位置与内边距 */
	inline const FMargin CostBadgeSlotPad = FMargin(6.0f, 5.0f, 0.0f, 0.0f);
	inline const FMargin CostBadgeInner   = FMargin(7.0f, 2.0f, 7.0f, 3.0f);

	/** 右上角角标 */
	inline const FMargin TagPad    = FMargin(0.0f, 8.0f, 8.0f, 0.0f);
	/** 卡底快捷键 */
	inline const FMargin HotkeyPad = FMargin(0.0f, 0.0f, 0.0f, 2.0f);

	inline const FVector2D TypeIconSize = FVector2D(52.0f, 52.0f);

	// ── 正文各行的下边距
	inline const FMargin IconRowPad  = FMargin(0.0f, 0.0f, 0.0f, 4.0f);
	inline const FMargin NameRowPad  = FMargin(0.0f, 0.0f, 0.0f, 1.0f);
	inline const FMargin TypeRowPad  = FMargin(0.0f, 0.0f, 0.0f, 4.0f);

	// ══════════════════════════════════════════════════════════════
	// 横版卡（固定卡专用）
	// ══════════════════════════════════════════════════════════════
	// 左侧固定卡用【横板】样式，与手牌的竖版卡是两套版式。
	//
	// ⚠️ 不能靠"把竖版卡缩小"来做横版。之前固定卡就是竖版卡
	//    SetBaseScale(0.72) 缩小的，那只是变小，比例仍是 168x232（竖）。
	//    横版贴图是 256x130（比例 1.95，横），套竖版布局会导致
	//    文字区完全对不上贴图的留白位置。
	//
	// ⚠️ 尺寸按贴图比例定：256x130 → 取 196x100（约 1.96）。
	//    比例对不上会让贴图被拉伸变形，而美术会以为是自己导错了图。
	namespace R
	{
		inline constexpr float CardWidth  = 196.0f;
		inline constexpr float CardHeight = 100.0f;

		/**
		 * 文字区相对整卡的内边距。
		 *
		 * ⚠️ 这几个数是【量出来的】而不是猜的：
		 *    对 RCard_Attack 做亮度采样，中间留白区（亮度约 215）
		 *    大致占 x 的 17%~92%、y 的 17%~83%，四周是暗色边框。
		 *    文字必须落在留白区内 —— 压到暗边框上会读不出来。
		 */
		inline const FMargin ContentPad = FMargin(
			CardWidth  * 0.17f,   // 左：让出左侧暗边
			CardHeight * 0.17f,   // 上
			CardWidth  * 0.08f,   // 右
			CardHeight * 0.17f);  // 下

		/** 左侧类型图标尺寸。横版卡地方小，图标要比竖版的 52 小一圈。 */
		inline const FVector2D IconSize = FVector2D(34.0f, 34.0f);

		/** 图标与文字之间的间距 */
		inline const FMargin IconPad = FMargin(0.0f, 0.0f, 6.0f, 0.0f);

		/** 费用底衬（横版卡放左上角，同竖版逻辑） */
		inline const FMargin CostSlotPad = FMargin(4.0f, 3.0f, 0.0f, 0.0f);
		inline const FMargin CostInner   = FMargin(5.0f, 1.0f, 5.0f, 2.0f);

		/** 快捷键放右侧居中（横版没有"卡底"可用） */
		inline const FMargin HotkeyPad = FMargin(0.0f, 0.0f, 6.0f, 0.0f);

		// 横版卡地方紧，字号整体比竖版小
		inline constexpr int32 FontName  = 13;
		inline constexpr int32 FontDesc  = 9;
		inline constexpr int32 FontCost  = 15;
		inline constexpr int32 FontKey   = 10;
	}

	// ══════════════════════════════════════════════════════════════
	// 字体
	// ══════════════════════════════════════════════════════════════
	// ⚠️ 【不要】用 GEngine->GetMediumFont()。它在没有项目配置时返回的是
	//    /Engine/Transient.DefaultRegularFont —— 一个运行时动态创建的
	//    【瞬态】UFont（见 UnrealEngine.cpp 的 CreateFontObjectFromDefaultFont）。
	//    它有两个致命问题：
	//      ① 瞬态对象【无法序列化进资产】。控件蓝图里存下来的 FontObject
	//         永远是空 —— 编辑器里 FontFace 显示 None，运行时回退到
	//         Roboto 单体（不含中文字形）。
	//      ② 它的 typeface 只有 "Regular" 一项。我们却按 "Bold" 去取，
	//         查不到就静默回退，粗体从来没生效过。
	//
	//    改用真实资产 /Engine/EngineFonts/Roboto：它是带
	//    CompositeFallbackFont（DroidSansFallback）的复合字体，
	//    中文走回退子字体，且 typeface 里确实有 Regular/Bold/Italic/Light。
	//
	// ⚠️ 中文字形来自 DroidSansFallback，它是【思源之前的老字体】，
	//    字形偏瘦、标点位置与中文排版惯例有差异，只能算"能读"。
	//    正式版仍需换一套授权可商用的中文字体
	//    （见 Docs/UI_Asset_Checklist.md 字体一节）—— 这只是让灰盒期能看清字。
	inline const TCHAR* FontAssetPath = TEXT("/Engine/EngineFonts/Roboto.Roboto");

	inline const TCHAR* FaceRegular = TEXT("Regular");
	inline const TCHAR* FaceBold    = TEXT("Bold");

	/**
	 * 取卡面用字体。运行时控件与蓝图生成器【共用】这一个函数。
	 *
	 * ⚠️ 两边必须同源。若生成器写死一套、运行时另写一套，
	 *    没建蓝图时和建了蓝图时字体会不一样，而且不报错。
	 */
	HEXSPIRE_API FSlateFontInfo GetCardFont(int32 Size, bool bBold = false);

	// ══════════════════════════════════════════════════════════════
	// 字号
	// ══════════════════════════════════════════════════════════════
	inline constexpr int32 FontCost  = 19;
	inline constexpr int32 FontName  = 15;
	inline constexpr int32 FontType  = 9;
	inline constexpr int32 FontDesc  = 11;
	inline constexpr int32 FontRange = 10;
	inline constexpr int32 FontTag   = 10;
	inline constexpr int32 FontKey   = 11;

	// ══════════════════════════════════════════════════════════════
	// 颜色 —— 卡面是【浅色纸面】
	// ══════════════════════════════════════════════════════════════
	// ⚠️ 卡框贴图实测中心亮度约 236/255（近白）。
	//    浅色文字压在上面等于隐形，所以前景一律用深墨色。

	inline const FLinearColor ColInk         = FLinearColor(0.10f, 0.11f, 0.13f);
	inline const FLinearColor ColInkSoft     = FLinearColor(0.36f, 0.37f, 0.40f);
	inline const FLinearColor ColInkDisabled = FLinearColor(0.55f, 0.56f, 0.58f);
	inline const FLinearColor ColDescInk     = FLinearColor(0.22f, 0.23f, 0.26f);

	inline const FLinearColor ColCostBadge  = FLinearColor(0.09f, 0.11f, 0.16f, 0.94f);
	inline const FLinearColor ColCostEnergy = FLinearColor(0.70f, 0.90f, 1.00f);
	inline const FLinearColor ColCostBad    = FLinearColor(1.00f, 0.52f, 0.44f);

	inline const FLinearColor ColWarn        = FLinearColor(0.72f, 0.44f, 0.04f);
	inline const FLinearColor ColCornerstone = FLinearColor(0.20f, 0.38f, 0.58f);

	/**
	 * 不可用遮罩：半透【白】。
	 *
	 * ⚠️ 是冲淡而不是压暗。浅色卡上盖深色纱会变成一张灰卡 ——
	 *    反而比可用的白卡更抢眼，与"别点这张"的意图正好相反。
	 */
	inline const FLinearColor ColDimVeil = FLinearColor(0.86f, 0.87f, 0.88f, 0.55f);
}
