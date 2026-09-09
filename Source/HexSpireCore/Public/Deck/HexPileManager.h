// Copyright Hex Spire. All Rights Reserved.
//
// 牌堆循环 —— D2 / 策划案 §7.4
//
// 采用《杀戮尖塔》标准结构：抽牌堆 / 手牌 / 弃牌堆 / 消耗区。
//
// ⚠️ D2 的核心红利是【概率可推算】：玩家能看弃牌堆、能算"我的《重击》
//    还有几回合回来"→ 决策可归因。这要求：
//      · 弃牌堆内容完全可见且有序
//      · 抽牌堆内容可查看（乱序显示），顺序不可见
//    不给这个 UI 等于放弃 D2 的价值（§13.2）。
//
// ⚠️ 小卡组特性（§7.4.4）：卡组 11 张、每回合抽 5 →
//    约 2.2 回合走完一轮，一场 4 回合的战斗会洗回 1–2 次。
//    这让 OnDeckReshuffled 成为高频符文钩子 ——
//    这是 D2 + D3 组合出的独有设计空间，StS 本身很少用到。

#pragma once

#include "CoreMinimal.h"
#include "Core/HexSpireEnums.h"

class FHexRngStreams;
struct FHexCardData;

/** 卡牌实例（同一张卡定义可以有多个实例，各自有独立的升级态） */
struct HEXSPIRECORE_API FHexCardInstance
{
	/** 全局唯一实例 id —— 用于精确定位"手牌里的第 2 张攻击" */
	int32 Uid = 0;

	/** 指向卡牌定义 */
	FName CardId;

	bool bUpgraded = false;

	/** 本场战斗内的临时费用修正（某些符文/状态会改单张卡的费用） */
	int32 TempCostDelta = 0;

	void Serialize(FArchive& Ar);
};

/**
 * 四区牌堆管理。
 *
 * ⚠️ 不变量（由 CheckInvariants 校验，验证器会跑）：
 *    抽牌堆 + 手牌 + 弃牌堆 + 消耗区 = 卡组全集，无重复无丢失。
 *    这条不变量能抓住绝大多数牌堆 bug（丢牌、复制牌、幽灵牌）。
 */
class HEXSPIRECORE_API FHexPileManager
{
public:
	/**
	 * 战斗开始：卡组洗混成抽牌堆。
	 * @param Deck 完整卡组（基石卡 + 非基石卡 + 注入的衍生卡）
	 */
	void BeginBattle(const TArray<FHexCardInstance>& Deck, FHexRngStreams& Rng);

	/**
	 * 抽 N 张。
	 *
	 * 流程（§7.4.2）：
	 *   · 抽牌堆不足时：把弃牌堆洗混 → 成为新抽牌堆 → 触发 OnDeckReshuffled
	 *   · 若抽牌堆与弃牌堆都空 → 停止抽牌，不报错
	 *   · 手牌达到上限时停止抽牌（多余的牌【不抽出】，留在抽牌堆）
	 *
	 * @param OutDrawn        实际抽到的卡实例
	 * @param OutReshuffled   本次抽牌过程中是否发生了洗回（调用方据此 emit 时机）
	 * @return 实际抽到的张数
	 */
	int32 Draw(int32 Count, int32 HandLimit, FHexRngStreams& Rng,
		TArray<FHexCardInstance>& OutDrawn, bool& OutReshuffled);

	/**
	 * 打出一张手牌后的归宿处理。
	 * @param bExhaust 带【消耗】→ 消耗区；否则 → 弃牌堆
	 * @return 是否成功（手牌中不存在该 uid 时返回 false）
	 */
	bool ResolvePlayedCard(int32 Uid, bool bExhaust);

	/** 弃掉一张指定手牌 */
	bool DiscardFromHand(int32 Uid);

	/**
	 * 回合结束：弃掉全部手牌（§7.4.2）。
	 * 这是制造"这回合必须用完"紧迫感的机制，不可省略。
	 * @param OutDiscarded 被弃掉的卡（调用方逐张 emit OnCardDiscarded）
	 */
	void DiscardHand(TArray<FHexCardInstance>& OutDiscarded);

	/** 把弃牌堆洗回抽牌堆。返回是否真的洗了（弃牌堆为空则不洗） */
	bool ReshuffleDiscardIntoDraw(FHexRngStreams& Rng);

	/** 把某张牌置于抽牌堆顶 */
	void PutOnTopOfDraw(const FHexCardInstance& Card);

	/** 战斗结束：三堆 + 消耗区全部归还卡组（§7.4.2） */
	void EndBattle(TArray<FHexCardInstance>& OutFullDeck);

	// ── 查询

	const TArray<FHexCardInstance>& GetDrawPile() const { return DrawPile; }
	const TArray<FHexCardInstance>& GetHand() const { return Hand; }
	const TArray<FHexCardInstance>& GetDiscardPile() const { return DiscardPile; }
	const TArray<FHexCardInstance>& GetExhaustPile() const { return ExhaustPile; }

	int32 NumDraw() const { return DrawPile.Num(); }
	int32 NumHand() const { return Hand.Num(); }
	int32 NumDiscard() const { return DiscardPile.Num(); }
	int32 NumExhaust() const { return ExhaustPile.Num(); }

	/** 手牌中是否存在该 uid */
	bool HandContains(int32 Uid) const;

	const FHexCardInstance* FindInHand(int32 Uid) const;

	/**
	 * 不变量校验：四区之和 = 全集，无重复无丢失。
	 * @param OutError 失败原因
	 */
	bool CheckInvariants(FString& OutError) const;

	void Serialize(FArchive& Ar);

	uint32 ContentHash() const;

private:
	TArray<FHexCardInstance> DrawPile;    // [0] = 堆顶
	TArray<FHexCardInstance> Hand;
	TArray<FHexCardInstance> DiscardPile;
	TArray<FHexCardInstance> ExhaustPile;

	/** BeginBattle 时记录的全集，用于不变量校验 */
	TArray<int32> AllUids;
};
