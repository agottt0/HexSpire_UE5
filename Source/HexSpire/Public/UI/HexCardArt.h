// Copyright Hex Spire. All Rights Reserved.
//
// 卡牌美术资产的【唯一路径来源】
//
// ══════════════════════════════════════════════════════════════════
// 为什么路径要集中在这一个文件
// ══════════════════════════════════════════════════════════════════
// 与 HexUnitAppearance 同一个理由：LoadObject 找不到资产时是
// 【静默回退】的 —— 卡面变成纯色块，但一行错误都不报。
// 路径散在各处时，美术改个目录名就会得到"卡牌没图了"的无头案。
//
// 集中在这里之后，换美术只需要改这一个文件。
//
// ══════════════════════════════════════════════════════════════════
// 三级回退链
// ══════════════════════════════════════════════════════════════════
//   ① DataTable 里该卡的 Visual 配置（美术逐张挂的）
//   ② 本文件里按 CardType / Rarity 的默认资产
//   ③ nullptr → 控件画纯色块（永远不崩，但看得出"缺图"）
//
// 这条链让「美术还没交图」和「美术交了图」可以共存 ——
// 灰盒期不必等资产齐全才能跑。

#pragma once

#include "CoreMinimal.h"
#include "Core/HexSpireEnums.h"

class UTexture2D;

namespace HexCardArt
{
	/**
	 * 取卡框贴图。
	 *
	 * ⚠️ 现有的 Card_White 是【JPEG，无 Alpha 通道】。
	 *    这意味着它只能当矩形底衬用，做不了圆角和描边 ——
	 *    圆角需要 Alpha 把四角抠掉。
	 *    美术需要重导成带 Alpha 的 PNG，见 Docs/UI_Asset_Checklist.md。
	 *
	 * @param CardId  卡牌 Id（先查 DataTable 的 Visual 覆写）
	 * @param Rarity  查不到覆写时按稀有度取默认框
	 */
	HEXSPIRE_API UTexture2D* GetFrame(FName CardId, EHexRarity Rarity);

	/**
	 * 取卡类型图标。
	 *
	 * ⚠️ 目前只有 3 张（Attack / Defend / Move），对应
	 *    攻击 / 守备 / 移动。技能、姿态、衍生、诅咒【还没有图标】，
	 *    会回退到 nullptr → 控件不画图标层。
	 *    这不是 bug，是资产缺口，清单里已列为 P0。
	 */
	HEXSPIRE_API UTexture2D* GetTypeIcon(FName CardId, EHexCardType Type);

	/** 取卡面插画。目前全部无图，恒返回 DataTable 里配的那张或 nullptr。 */
	HEXSPIRE_API UTexture2D* GetArtwork(FName CardId);

	/**
	 * 卡框染色。
	 *
	 * ⚠️ 【不再按 CardType 染色】（用户决策）。
	 *    原先把整张白卡框乘成红/蓝/绿来区分类型，观感是"一排彩色方块"，
	 *    而且插画一旦挂上去就会被这层色整体污染 —— 美术交的图看起来
	 *    永远不对色。
	 *
	 *    现在只在【美术在 DataTable 里显式配了 FrameTint】时才染色，
	 *    否则返回白色（= 按贴图原样画）。类型区分改由类型图标 +
	 *    类型文字承担，那也更符合 §13.2「不能只靠颜色传信息」。
	 */
	HEXSPIRE_API FLinearColor GetFrameTint(FName CardId, EHexCardType Type);

	/**
	 * 取【横版】卡框贴图（左侧固定卡用）。
	 *
	 * 按 CardType 取 RCard_Attack / RCard_Defend / RCard_Move
	 * （256x130，中间是留白区，文字画在那里）。
	 *
	 * ⚠️ 与 GetFrame 分开而不是加个参数：横版只有攻击/守备/移动三张，
	 *    没有稀有度概念，也不吃 DataTable 的 CardFrame 覆写
	 *    （那个字段配的是竖版卡框，混用会把横版卡换成竖版图，
	 *      比例一错整张卡都会拉伸变形）。
	 *
	 * ⚠️ 其他类型返回 nullptr —— 固定卡目前就这三种。
	 *    以后变多时在这里加分支，调用方不用改。
	 */
	HEXSPIRE_API UTexture2D* GetWideFrame(EHexCardType Type);

	/**
	 * 卡类型的中文名（"攻击" / "守备" / …）。
	 *
	 * 取代整卡染色后的类型标识。文字对色弱玩家零成本，
	 * 而且在图标资产还缺 4 种（技能/姿态/衍生/诅咒）的当下，
	 * 它是唯一能把类型说清楚的手段。
	 */
	HEXSPIRE_API FString GetTypeName(EHexCardType Type);

	/**
	 * 类型的强调色。
	 *
	 * ⚠️ 只用在【小面积】上：类型文字、费用底衬描边。
	 *    大面积用色就退回"彩色方块"了，那正是这次要改掉的东西。
	 *
	 * ⚠️ 这些值比 GetFrameTint 原来那套【暗】：那套是乘在白贴图上的
	 *    染色（需要偏亮才不至于压黑），而这里是直接画在浅色纸面上的
	 *    前景色 —— 用亮色会读不出来。
	 */
	HEXSPIRE_API FLinearColor GetTypeAccent(EHexCardType Type);

	/** 稀有度光晕强度。0 = 不发光。 */
	HEXSPIRE_API float GetRarityGlow(FName CardId, EHexRarity Rarity);
}
