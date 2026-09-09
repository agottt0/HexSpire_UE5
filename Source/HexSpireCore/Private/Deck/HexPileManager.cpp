// Copyright Hex Spire. All Rights Reserved.

#include "Deck/HexPileManager.h"
#include "Rng/HexRngStreams.h"

void FHexCardInstance::Serialize(FArchive& Ar)
{
	Ar << Uid;
	Ar << CardId;
	Ar << bUpgraded;
	Ar << TempCostDelta;
}

// ───────────────────────────────────────────────────────── 战斗生命周期

void FHexPileManager::BeginBattle(const TArray<FHexCardInstance>& Deck, FHexRngStreams& Rng)
{
	DrawPile = Deck;
	Hand.Reset();
	DiscardPile.Reset();
	ExhaustPile.Reset();

	AllUids.Reset(Deck.Num());
	for (const FHexCardInstance& C : Deck)
	{
		AllUids.Add(C.Uid);
	}
	AllUids.Sort();

	// ⚠️ 必须走注入式 RNG 的 Deck 流（纪律 1）。
	//    用 TArray::Sort 配随机比较器或 FMath::Rand 都会破坏确定性。
	Rng.Shuffle(DrawPile, EHexRngStream::Deck);
}

int32 FHexPileManager::Draw(
	int32 Count, int32 HandLimit, FHexRngStreams& Rng,
	TArray<FHexCardInstance>& OutDrawn, bool& OutReshuffled)
{
	OutDrawn.Reset();
	OutReshuffled = false;

	int32 Drawn = 0;
	for (int32 I = 0; I < Count; ++I)
	{
		// 手牌上限：多余的牌【不抽出】，留在抽牌堆（§7.4.2）
		if (Hand.Num() >= HandLimit)
		{
			break;
		}

		if (DrawPile.Num() == 0)
		{
			// 抽牌堆空 → 洗回弃牌堆
			if (!ReshuffleDiscardIntoDraw(Rng))
			{
				// 抽牌堆与弃牌堆都空 → 停止抽牌，不报错（§7.4.2 明确要求）
				break;
			}
			OutReshuffled = true;
		}

		if (DrawPile.Num() == 0)
		{
			break;
		}

		// 从堆顶抽（下标 0）
		FHexCardInstance Card = DrawPile[0];
		DrawPile.RemoveAt(0, EAllowShrinking::No);
		Hand.Add(Card);
		OutDrawn.Add(Card);
		++Drawn;
	}

	return Drawn;
}

bool FHexPileManager::ResolvePlayedCard(int32 Uid, bool bExhaust)
{
	for (int32 I = 0; I < Hand.Num(); ++I)
	{
		if (Hand[I].Uid != Uid)
		{
			continue;
		}
		FHexCardInstance Card = Hand[I];
		Hand.RemoveAt(I, EAllowShrinking::No);

		if (bExhaust)
		{
			// 带【消耗】→ 消耗区，本场战斗不再出现（战斗结束后归还卡组）
			ExhaustPile.Add(Card);
		}
		else
		{
			DiscardPile.Add(Card);
		}
		return true;
	}
	return false;
}

bool FHexPileManager::DiscardFromHand(int32 Uid)
{
	for (int32 I = 0; I < Hand.Num(); ++I)
	{
		if (Hand[I].Uid == Uid)
		{
			DiscardPile.Add(Hand[I]);
			Hand.RemoveAt(I, EAllowShrinking::No);
			return true;
		}
	}
	return false;
}

void FHexPileManager::DiscardHand(TArray<FHexCardInstance>& OutDiscarded)
{
	OutDiscarded = Hand;
	for (const FHexCardInstance& C : Hand)
	{
		DiscardPile.Add(C);
	}
	Hand.Reset();
}

bool FHexPileManager::ReshuffleDiscardIntoDraw(FHexRngStreams& Rng)
{
	if (DiscardPile.Num() == 0)
	{
		return false;
	}

	// 弃牌堆整体移入抽牌堆底部后整体洗混。
	// ⚠️ 这里 DrawPile 通常已经是空的（抽空才洗回），
	//    但为了让 ShuffleDiscardIntoDraw 效果卡（主动洗回）也能复用，
	//    这里做通用处理：合并后整体洗。
	for (const FHexCardInstance& C : DiscardPile)
	{
		DrawPile.Add(C);
	}
	DiscardPile.Reset();

	Rng.Shuffle(DrawPile, EHexRngStream::Deck);
	return true;
}

void FHexPileManager::PutOnTopOfDraw(const FHexCardInstance& Card)
{
	DrawPile.Insert(Card, 0);
}

void FHexPileManager::EndBattle(TArray<FHexCardInstance>& OutFullDeck)
{
	OutFullDeck.Reset();
	// 顺序：抽牌堆 → 手牌 → 弃牌堆 → 消耗区（固定顺序，确定性）
	OutFullDeck.Append(DrawPile);
	OutFullDeck.Append(Hand);
	OutFullDeck.Append(DiscardPile);
	OutFullDeck.Append(ExhaustPile);

	DrawPile.Reset();
	Hand.Reset();
	DiscardPile.Reset();
	ExhaustPile.Reset();
}

// ───────────────────────────────────────────────────────── 查询

bool FHexPileManager::HandContains(int32 Uid) const
{
	return FindInHand(Uid) != nullptr;
}

const FHexCardInstance* FHexPileManager::FindInHand(int32 Uid) const
{
	for (const FHexCardInstance& C : Hand)
	{
		if (C.Uid == Uid)
		{
			return &C;
		}
	}
	return nullptr;
}

bool FHexPileManager::CheckInvariants(FString& OutError) const
{
	OutError.Reset();

	TArray<int32> Current;
	Current.Reserve(AllUids.Num());

	for (const FHexCardInstance& C : DrawPile)    { Current.Add(C.Uid); }
	for (const FHexCardInstance& C : Hand)        { Current.Add(C.Uid); }
	for (const FHexCardInstance& C : DiscardPile) { Current.Add(C.Uid); }
	for (const FHexCardInstance& C : ExhaustPile) { Current.Add(C.Uid); }

	if (Current.Num() != AllUids.Num())
	{
		OutError = FString::Printf(
			TEXT("牌数不符：四区共 %d 张，卡组全集 %d 张（抽%d 手%d 弃%d 消%d）"),
			Current.Num(), AllUids.Num(),
			DrawPile.Num(), Hand.Num(), DiscardPile.Num(), ExhaustPile.Num());
		return false;
	}

	Current.Sort();
	for (int32 I = 0; I < Current.Num(); ++I)
	{
		if (Current[I] != AllUids[I])
		{
			OutError = FString::Printf(
				TEXT("牌集不符：位置 %d 实际 uid=%d 期望 uid=%d（可能有重复或幽灵牌）"),
				I, Current[I], AllUids[I]);
			return false;
		}
	}

	// 重复检测
	for (int32 I = 1; I < Current.Num(); ++I)
	{
		if (Current[I] == Current[I - 1])
		{
			OutError = FString::Printf(TEXT("发现重复卡实例 uid=%d"), Current[I]);
			return false;
		}
	}

	return true;
}

void FHexPileManager::Serialize(FArchive& Ar)
{
	auto SerializePile = [&Ar](TArray<FHexCardInstance>& Pile)
	{
		int32 N = Pile.Num();
		Ar << N;
		if (Ar.IsLoading())
		{
			Pile.SetNum(N);
		}
		for (FHexCardInstance& C : Pile)
		{
			C.Serialize(Ar);
		}
	};

	SerializePile(DrawPile);
	SerializePile(Hand);
	SerializePile(DiscardPile);
	SerializePile(ExhaustPile);
	Ar << AllUids;
}

uint32 FHexPileManager::ContentHash() const
{
	uint32 H = 0;
	auto HashPile = [&H](const TArray<FHexCardInstance>& Pile)
	{
		H = HashCombine(H, GetTypeHash(Pile.Num()));
		for (const FHexCardInstance& C : Pile)
		{
			H = HashCombine(H, GetTypeHash(C.Uid));
		}
	};
	HashPile(DrawPile);
	HashPile(Hand);
	HashPile(DiscardPile);
	HashPile(ExhaustPile);
	return H;
}
