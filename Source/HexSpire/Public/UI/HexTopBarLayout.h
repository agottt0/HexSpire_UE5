// Copyright Hex Spire. All Rights Reserved.
//
// 顶栏（左上角头像/字样 + 局内状态读数）布局的【唯一数据源】
//
// ══════════════════════════════════════════════════════════════════
// 为什么顶栏从 Canvas 搬到了 UMG
// ══════════════════════════════════════════════════════════════════
// 顶栏原先是 AHexDemoHUD::DrawTopBar 用 Canvas 画的（0,0 → 全宽×74）。
// 那条路走不通，原因不是"想统一技术栈"，而是一个硬约束：
//
//   ⚠️ HUD 的 Canvas 绘制【永远画在所有 UMG 之上】。
//
// 于是"把头像放到左上角"与"顶栏用 Canvas 画"直接冲突 ——
// 任何摆在左上角的 UMG 控件都会被那条 74px 的实心面板整块盖住，
// 而且不报错：控件存在、贴图加载成功、自检全过，屏幕上就是看不见。
// 想在角上放头像，顶栏就必须一起搬进 UMG。
//
// ══════════════════════════════════════════════════════════════════
// 为什么数值集中在这个文件
// ══════════════════════════════════════════════════════════════════
// 与 HexCardLayout.h 同一个理由：布局有【两条消费路径】
//   ① 运行时：UHexHandPanelWidget::RebuildWidget() 搭 C++ 控件树
//   ② 生成时：HexBuildCardWidget commandlet 把树写进控件蓝图资产
//
// 两边必须产出外观一致的顶栏（没建蓝图时跑 ①，建了跑 ②）。
// 各写一份边距的话，改一处忘一处就会出现"美术在蓝图里看到的和
// 玩家看到的不一样"，而这种不一致不报错、只能靠眼睛发现。

#pragma once

#include "CoreMinimal.h"
#include "Layout/Margin.h"

namespace HexTopBarLayout
{
	// ══════════════════════════════════════════════════════════════
	// 控件名 —— 必须与 UHexHandPanelWidget 的 BindWidgetOptional 同名
	// ══════════════════════════════════════════════════════════════
	// ⚠️ BindWidget 的绑定是【纯按名字匹配】的（引擎拿控件 FName 去查
	//    同名 UPROPERTY），既不看类型也不看位置。名字写错的后果是：
	//    控件在蓝图里存在、看得见、能拖，但运行时【永远收不到数据】,
	//    而 Optional 绑定在编译期只给一条 Note，运行时完全静默。
	//    这是本文件里最容易出也最难查的错，所以用常量而不是字面量。
	namespace N
	{
		/** 顶栏根容器（锚顶边拉伸） */
		inline const TCHAR* TopBar     = TEXT("TopBar");
		/** 顶栏内部的横排：字样 → 头像 → 读数列 */
		inline const TCHAR* TopRow     = TEXT("TopRow");

		/** 头像（近正方形） */
		inline const TCHAR* Portrait   = TEXT("Portrait");

		/**
		 * 字样的外层 ScaleBox。
		 *
		 * ⚠️ 必须是 ScaleBox 而不是定尺寸的 SizeBox —— 见下方
		 *    NameArtRatio 一节：四个角色的字样比例差到 0.34~0.52。
		 */
		inline const TCHAR* NameArtBox = TEXT("NameArtBox");

		// 四个角色的字样各一个 Image，三个 Collapsed。
		// ⚠️ Collapsed 不参与布局，所以叠在同一个 ScaleBox 里
		//    不会把可见那张挤偏（Hidden 会占位，别用它）。
		inline const TCHAR* NameArtWarden    = TEXT("NameArt_Warden");
		inline const TCHAR* NameArtMedium    = TEXT("NameArt_Medium");
		inline const TCHAR* NameArtScrivener = TEXT("NameArt_Scrivener");
		inline const TCHAR* NameArtRevenant  = TEXT("NameArt_Revenant");

		/** 读数列（HP / 腐蚀度 / 卡组 / 符文 / 回合 / 体力） */
		inline const TCHAR* StatCol    = TEXT("StatCol");

		inline const TCHAR* HeroHP     = TEXT("HeroHP");
		inline const TCHAR* HPBar      = TEXT("HPBar");
		inline const TCHAR* Corruption = TEXT("Corruption");
		inline const TCHAR* DeckInfo   = TEXT("DeckInfo");
		inline const TCHAR* RuneInfo   = TEXT("RuneInfo");
		inline const TCHAR* RoundNum   = TEXT("RoundNum");
		inline const TCHAR* EnergyText = TEXT("EnergyText");
		inline const TCHAR* PileInfo   = TEXT("PileInfo");

		/**
		 * 状态提示（"体力不足" / "你已阵亡" / …）。
		 *
		 * ⚠️ 这一条【必须存在】。它原先画在 DrawTopBar 内部（14,80），
		 *    是 Mode->GetStatusMessage() 的【唯一】显示点。
		 *    漏掉它的后果是全部操作反馈（阵亡、胜利、体力不足、
		 *    目标不合法、房间切换）无声消失 —— 玩家做了非法操作
		 *    却得不到任何解释，而代码一行不报错。
		 */
		inline const TCHAR* StatusText = TEXT("StatusText");

		// ══════════════════════════════════════════════════════════
		// 右下角操作区（结束回合按钮 + 体力火苗）
		// ══════════════════════════════════════════════════════════
		// ⚠️ 这一块【不在顶栏容器里】，它锚右下角。放在同一个文件里是
		//    因为它与顶栏共享 ColText/字体那套常量，而且和顶栏一样
		//    有"运行时树 + 生成器"两条消费路径 —— 分两个文件的话
		//    两处会各自漂移。

		/** 右下角竖排容器：体力排在上，结束回合按钮在下 */
		inline const TCHAR* ActionCol    = TEXT("ActionCol");

		/**
		 * 结束回合按钮。
		 *
		 * ⚠️ 必须是 UButton 而不是 UImage。
		 *    UImage 收不到点击（它不是可聚焦控件，也没有 OnClicked），
		 *    拿 UImage 当按钮的表现是"图显示正常、点了没反应"——
		 *    而且不报错，很容易误判成逻辑层的问题。
		 */
		inline const TCHAR* EndTurnButton = TEXT("EndTurnButton");

		/**
		 * 体力火苗的横排容器。
		 *
		 * ⚠️ 火苗【个数由体力上限决定】，所以是运行时增删的 ——
		 *    与手牌同一个道理，不能在蓝图里摆死 5 个。
		 *    EnergyMax 受符文/装备影响（见 FHexRuleBook::EnergyMax），
		 *    摆死的话加了 +1 体力的符文后第 6 点永远看不见。
		 */
		inline const TCHAR* EnergyRow    = TEXT("EnergyRow");

		/** 体力读数（"2/5"），跟在火苗右边 */
		inline const TCHAR* EnergyCount  = TEXT("EnergyCount");
	}

	// ══════════════════════════════════════════════════════════════
	// 尺寸
	// ══════════════════════════════════════════════════════════════

	/**
	 * 顶栏高度。
	 *
	 * ⚠️ DrawPileBrowser 原先写死 Y = 120.0f，那是"74px 顶栏之下"的
	 *    隐含依赖。现在它改读 PileBrowserTop，两者不会再各自漂移。
	 */
	inline constexpr float BarHeight = 96.0f;

	/** 牌堆浏览器的顶端 —— 顶栏之下再留一点缝给状态提示 */
	inline constexpr float PileBrowserTop = BarHeight + 46.0f;

	/** 头像边长。头像实测都是近正方形（617x611 等，比例 ≈1.01）。 */
	inline constexpr float PortraitSize = 76.0f;

	/**
	 * 字样槽位的尺寸上限。
	 *
	 * ⚠️ 这是【上限】而不是实际尺寸，实际由 ScaleBox 按原图比例缩放。
	 *    四个角色的字样实测比例差异很大：
	 *        Warden    289x799  → 0.362
	 *        Medium    311x760  → 0.409
	 *        Scrivener 296x566  → 0.523
	 *        Revenant  286x836  → 0.342
	 *    写死宽高会让其中三个被拉伸变形，而美术会以为是自己导错了图。
	 *    ScaleToFit + 只给上限，才能让每张都按原比例画。
	 */
	inline constexpr float NameArtMaxWidth  = 34.0f;
	inline constexpr float NameArtMaxHeight = 86.0f;

	/** HP 条尺寸 */
	inline constexpr float HPBarWidth  = 190.0f;
	inline constexpr float HPBarHeight = 9.0f;

	// ══════════════════════════════════════════════════════════════
	// 边距
	// ══════════════════════════════════════════════════════════════

	/** 顶栏内容相对顶栏边框的内缩 */
	inline const FMargin BarPad      = FMargin(12.0f, 6.0f, 12.0f, 6.0f);

	/** 字样与头像之间 */
	inline const FMargin NameArtPad  = FMargin(0.0f, 0.0f, 8.0f, 0.0f);
	/** 头像与读数列之间 */
	inline const FMargin PortraitPad = FMargin(0.0f, 0.0f, 14.0f, 0.0f);

	/** 读数各行之间的下边距 */
	inline const FMargin RowPad      = FMargin(0.0f, 0.0f, 0.0f, 2.0f);

	/** 状态提示相对屏幕左上的位置（顶栏下方） */
	inline const FMargin StatusPad   = FMargin(14.0f, BarHeight + 6.0f, 0.0f, 0.0f);

	// ══════════════════════════════════════════════════════════════
	// 字号
	// ══════════════════════════════════════════════════════════════
	inline constexpr int32 FontHP     = 13;
	inline constexpr int32 FontStat   = 11;
	inline constexpr int32 FontSmall  = 10;
	inline constexpr int32 FontStatus = 12;

	// ══════════════════════════════════════════════════════════════
	// 颜色 —— 顶栏是【深色面板】（与浅色卡面相反）
	// ══════════════════════════════════════════════════════════════
	// ⚠️ 与 HexCardLayout 的 ColInk 系列刻意不共用：卡面是浅色纸面，
	//    前景必须用深墨色；顶栏是深色底衬，同一套深色压上去等于隐形。

	inline const FLinearColor ColBarBg  = FLinearColor(0.04f, 0.05f, 0.07f, 0.85f);
	inline const FLinearColor ColText   = FLinearColor(0.92f, 0.92f, 0.90f);
	inline const FLinearColor ColDim    = FLinearColor(0.62f, 0.62f, 0.60f);
	inline const FLinearColor ColGood   = FLinearColor(0.35f, 0.90f, 0.45f);
	inline const FLinearColor ColWarn   = FLinearColor(0.98f, 0.78f, 0.20f);
	inline const FLinearColor ColBad    = FLinearColor(0.95f, 0.30f, 0.25f);
	inline const FLinearColor ColEnergy = FLinearColor(0.35f, 0.75f, 1.00f);

	/** HP 条的槽底与填充 */
	inline const FLinearColor ColHPTrack = FLinearColor(0.15f, 0.05f, 0.05f, 0.90f);
	inline const FLinearColor ColHPFill  = FLinearColor(0.25f, 0.85f, 0.35f, 0.95f);

	// ══════════════════════════════════════════════════════════════
	// 右下角操作区
	// ══════════════════════════════════════════════════════════════
	// 摆在右下角的理由：手牌横排锚在【底边中点】，固定卡锚在【左侧】,
	// 图例在右侧垂直居中（见 AHexDemoHUD::DrawLegend）。右下角是
	// 底部唯一没被占用的位置，而"结束回合"必须离手牌近 ——
	// 出完牌后的下一个动作就是它。

	/**
	 * 结束回合按钮的边长。
	 *
	 * ⚠️ 按钮贴图 UI_结束回合_3 实测 1266x1243（比例 1.019），
	 *    去掉透明边后内容区 1219x1161（比例 1.050）—— 近正方形，
	 *    所以按正方形槽位画不会变形，不需要像字样那样套 ScaleBox。
	 *
	 * ⚠️ 不要按"比例 1.0"写死成圆形去裁：那张图的外框是六边形带角的
	 *    印章造型，上下留白比左右多（实测上 2.3% / 下 4.3%），
	 *    裁掉会切到角。整张原图画进正方形槽位即可。
	 */
	inline constexpr float EndTurnSize = 104.0f;

	/**
	 * 单个体力火苗的尺寸。
	 *
	 * ⚠️ 按贴图原比例定，不能写成正方形。
	 *    UI_体力槽 实测 1402x1122，去透明边后内容区 1154x1000
	 *    （比例 1.154）—— 火苗是【竖向】的形状，但整图偏宽，
	 *    因为左右两侧各有一簇小火苗。写成正方形会把它压扁。
	 */
	inline constexpr float EnergyPipWidth  = 34.0f;
	inline constexpr float EnergyPipHeight = 30.0f;

	/** 火苗之间的间距。留窄一点，让一排火苗读起来是"一组"而不是散点。 */
	inline const FMargin EnergyPipPad = FMargin(1.0f, 0.0f, 1.0f, 0.0f);

	/** 体力读数与火苗之间 */
	inline const FMargin EnergyCountPad = FMargin(6.0f, 0.0f, 0.0f, 0.0f);

	/** 体力排与按钮之间 */
	inline const FMargin EnergyRowPad = FMargin(0.0f, 0.0f, 0.0f, 4.0f);

	/**
	 * 操作区相对屏幕右下角的内缩。
	 *
	 * ⚠️ 右边距要比 DrawLegend 的 12px 大一些不是为了美观 ——
	 *    图例锚的是【垂直居中】，操作区锚的是【底部】，两者不会
	 *    在同一高度上撞。这里的 20px 只是让按钮别贴死屏幕边缘
	 *    （贴边的按钮在 overscan 的显示器上会被切掉一条）。
	 */
	inline const FMargin ActionPad = FMargin(0.0f, 0.0f, 20.0f, 20.0f);

	/**
	 * 体力耗尽时火苗的染色。
	 *
	 * ⚠️ 用【同一张贴图染成暗色】而不是换一张"空槽"贴图：
	 *    目前只有一张火苗图，没有空态图。硬要用两张的话得先等美术出图，
	 *    而在那之前"已用掉的体力"就完全看不出来 —— 那比染色难看得多。
	 *    Alpha 压到 0.28 而不是纯灰：保留火苗形状，让"有几格体力上限"
	 *    仍然一眼可数（这是 §13.2 要求的）。
	 */
	inline const FLinearColor ColEnergyPipOn  = FLinearColor(1.00f, 1.00f, 1.00f, 1.00f);
	inline const FLinearColor ColEnergyPipOff = FLinearColor(0.34f, 0.38f, 0.46f, 0.28f);

	/** 结束回合按钮在不可按时（非战斗/战斗已结束）的染色 */
	inline const FLinearColor ColEndTurnOn  = FLinearColor(1.00f, 1.00f, 1.00f, 1.00f);
	inline const FLinearColor ColEndTurnOff = FLinearColor(0.55f, 0.55f, 0.58f, 0.45f);

	/** 悬停时轻微提亮 —— 按钮上没有文字标签，必须靠这个给出"可点"的反馈 */
	inline const FLinearColor ColEndTurnHover = FLinearColor(1.18f, 1.14f, 1.06f, 1.00f);

	/** 按下时压暗，给出"确实按到了"的反馈 */
	inline const FLinearColor ColEndTurnPress = FLinearColor(0.78f, 0.76f, 0.74f, 1.00f);

	/**
	 * 体力火苗与结束回合按钮的贴图。
	 *
	 * ⚠️ 按【资产实际名字】写死，含中文与那个 _3 后缀。
	 *    LoadObject 失败是静默的（回退 nullptr → 不画图层，不报错），
	 *    所以自检里要单独报告有没有拿到。
	 */
	inline const TCHAR* EnergyPipPath = TEXT("/Game/ArtResource/UI/UI_体力槽.UI_体力槽");
	inline const TCHAR* EndTurnPath   = TEXT("/Game/ArtResource/UI/UI_结束回合_3.UI_结束回合_3");

	// ══════════════════════════════════════════════════════════════
	// 角色 UI 资产
	// ══════════════════════════════════════════════════════════════
	// ⚠️ 路径按【资产实际名字】写，不能按规律推 —— 实测有两处不一致：
	//      头像叫 UI_Revanat_头像（Rev-a-nat）
	//      字样叫 UI_Revenat_字样（Rev-e-nat）
	//    推测出来的名字会 LoadObject 失败，而失败是【静默】的
	//    （回退 nullptr → 控件不画图层，不报错）。
	//
	// ⚠️ 资产名含中文。源文件是 UTF-8，TEXT() 里的中文能正确传到
	//    LoadObject —— 已在日志侧验证过本工程 C++ 的中文字面量
	//    可以完整往返（见 Saved/Logs 里的"[自检] 自动进入房间"等行）。

	/** 一个角色的一组 UI 资产 */
	struct FHeroUIArt
	{
		/** 与 N::NameArt* 对应的控件名，用于按角色切显隐 */
		const TCHAR* NameArtWidget = nullptr;

		/** 头像贴图路径 */
		const TCHAR* PortraitPath = nullptr;

		/** 字样贴图路径（竖条，放在头像左侧） */
		const TCHAR* NameArtPath = nullptr;
	};

	inline const FHeroUIArt Warden = {
		N::NameArtWarden,
		TEXT("/Game/ArtResource/UI/UI_Warden_头像.UI_Warden_头像"),
		TEXT("/Game/ArtResource/UI/UI_Warden_字样.UI_Warden_字样"),
	};

	inline const FHeroUIArt Medium = {
		N::NameArtMedium,
		TEXT("/Game/ArtResource/UI/UI_Medium_头像.UI_Medium_头像"),
		TEXT("/Game/ArtResource/UI/UI_Medium_字样.UI_Medium_字样"),
	};

	inline const FHeroUIArt Scrivener = {
		N::NameArtScrivener,
		TEXT("/Game/ArtResource/UI/UI_Scrivener_头像.UI_Scrivener_头像"),
		TEXT("/Game/ArtResource/UI/UI_Scrivener_字样.UI_Scrivener_字样"),
	};

	/** ⚠️ 头像 Revanat / 字样 Revenat —— 拼写不一致是资产的实际情况 */
	inline const FHeroUIArt Revenant = {
		N::NameArtRevenant,
		TEXT("/Game/ArtResource/UI/UI_Revanat_头像.UI_Revanat_头像"),
		TEXT("/Game/ArtResource/UI/UI_Revenat_字样.UI_Revenat_字样"),
	};

	/** 四个角色，顺序即蓝图里控件的堆叠顺序 */
	inline const FHeroUIArt* AllHeroes[] = { &Warden, &Medium, &Scrivener, &Revenant };

	/**
	 * 默认显示哪个角色。
	 *
	 * ⚠️ 目前逻辑层只有 warden 一个英雄（见 HexUnitAppearance.cpp：
	 *    玩家固定用 Warden 模型）。四组资产都摆进蓝图、三组 Collapsed，
	 *    以后加英雄时切显隐即可，不必重新指定贴图。
	 */
	inline const FHeroUIArt& Default() { return Warden; }
}
