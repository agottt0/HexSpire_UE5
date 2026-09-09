// Copyright Hex Spire. All Rights Reserved.

#include "Run/HexRunState.h"
#include "Content/HexContentLibrary.h"
#include "Runes/HexRuneLibrary.h"
#include "Rng/HexRngStreams.h"

// ══════════════════════════════════════════════════════════ 统计

void FHexFloorStats::Serialize(FArchive& Ar)
{
	Ar << RoomsCleared;
	Ar << BattlesWon;
	Ar << TotalRounds;
	Ar << DamageDealt;
	Ar << DamageTaken;
	Ar << CardsPlayed;
	Ar << EnemiesKilled;
	Ar << EndHPRatio;
}

// ══════════════════════════════════════════════════════════ 开局

FHexRunState::FHexRunState(uint64 InMasterSeed)
	: MasterSeed(InMasterSeed)
{
}

void FHexRunState::BeginRun(FName InHeroId, FHexRngStreams& Rng)
{
	HeroId = InHeroId;

	const FHexHeroData* Hero = FHexContentLibrary::FindHero(HeroId);
	if (!Hero)
	{
		// 未知英雄不静默兜底：整局的属性、卡组、卡池标签全依赖它，
		// 兜底会产出一局"看起来正常但数值全错"的游戏，比直接失败更难查。
		ensureMsgf(false, TEXT("BeginRun: 未知英雄 id %s"), *HeroId.ToString());
		return;
	}

	HeroHPMax = Hero->BaseHP;
	HeroHP = HeroHPMax;

	DeckCapacity = HexK::InitialDeckCapacity;
	Corruption = 0;
	Shards = 0;

	// 起始卡组（3 基石 + 5 普通，留 3 空位 —— 用户决策 q18）
	FHexContentLibrary::BuildStartingDeck(*Hero, Deck);

	// uid 分配器要跳过起始卡组已用的号段
	NextCardUid = 1;
	for (const FHexCardInstance& C : Deck)
	{
		NextCardUid = FMath::Max(NextCardUid, C.Uid + 1);
	}

	RuneLoadout = FHexRuneLoadout();
	EquipLoadout = FHexEquipLoadout();
	RuneInventory.Reset();
	EquipInventory.Reset();

	BeginFloor(1, Rng);
}

void FHexRunState::BeginFloor(int32 InFloorIndex, FHexRngStreams& Rng)
{
	FloorIndex = InFloorIndex;
	Stats.Reset();
	Map.Generate(FloorIndex, Rng);

	// ⚠️ 腐蚀度【不在进入新层时重置】。
	//    它是整局的累计压力（§9.4），跨层继承才能形成"越往上越危险"的曲线。
	//    若每层清零，第 3 层与第 1 层的难度会一样，肉鸽的加压曲线消失。
}

// ══════════════════════════════════════════════════════════ 卡组容量（D3）

int32 FHexRunState::GetUsedCapacity() const
{
	int32 Used = 0;
	for (const FHexCardInstance& Inst : Deck)
	{
		const FHexCardData* C = FHexContentLibrary::FindCard(Inst.CardId);
		// 基石卡与衍生卡不占容量（§7.6）
		if (C && C->bCountsTowardCapacity)
		{
			++Used;
		}
	}
	return Used;
}

int32 FHexRunState::CountCopiesInDeck(FName CardId) const
{
	int32 N = 0;
	for (const FHexCardInstance& Inst : Deck)
	{
		if (Inst.CardId == CardId)
		{
			++N;
		}
	}
	return N;
}

bool FHexRunState::AddCardToDeck(FName CardId)
{
	const FHexCardData* C = FHexContentLibrary::FindCard(CardId);
	if (!C)
	{
		return false;
	}

	// 同名份数上限：防止"8 张重击"这类退化卡组
	const int32 MaxCopies = C->MaxCopiesInDeck > 0
		? C->MaxCopiesInDeck
		: HexK::DefaultMaxCopiesInDeck;
	if (CountCopiesInDeck(CardId) >= MaxCopies)
	{
		return false;
	}

	// ⚠️ D3 替换制：占容量的卡在满容量时【必须先挤掉一张】。
	//    这里返回 false 由 UI 强制玩家做选择 —— 若在此静默替换，
	//    §3.2「挤掉哪张」这个核心决策就被代码代替玩家做了。
	if (C->bCountsTowardCapacity && !HasCapacityRoom())
	{
		return false;
	}

	FHexCardInstance Inst;
	Inst.Uid = NextCardUid++;
	Inst.CardId = CardId;
	Deck.Add(Inst);
	return true;
}

bool FHexRunState::RemoveCardFromDeck(int32 Uid)
{
	const int32 Index = Deck.IndexOfByPredicate(
		[Uid](const FHexCardInstance& C) { return C.Uid == Uid; });

	if (Index == INDEX_NONE)
	{
		return false;
	}

	// ⚠️ 基石卡不可移除（§7.2：它是英雄的身份）。
	//    移除后玩家会失去《攻击》或《移动》，战斗可能直接无法进行。
	const FHexCardData* C = FHexContentLibrary::FindCard(Deck[Index].CardId);
	if (C && C->bIsCornerstone)
	{
		return false;
	}

	Deck.RemoveAt(Index);
	return true;
}

// ══════════════════════════════════════════════════════════ 房间推进

int32 FHexRunState::OnRoomCleared()
{
	const FHexRoomNode* Room = Map.FindRoom(Map.GetCurrentRoomId());
	const bool bWasCombat = Room && Room->IsCombat();
	const bool bWasBoss = Room && Room->Type == EHexRoomType::Boss;

	const int32 Delta = Map.ClearCurrentRoom();

	// Delta 为 0 时说明这间房已经清过（重复调用），不该重复计统计
	if (Delta > 0 || bWasCombat)
	{
		++Stats.RoomsCleared;
		if (bWasCombat)
		{
			++Stats.BattlesWon;
		}
	}

	Corruption += Delta;

	// ══════════════════════════════════════════════════════
	// 战斗胜利回血（HexK::PostBattleHealRatio）
	// ══════════════════════════════════════════════════════
	//
	// ⚠️ 这条不在策划案里，是【一层循环带来的必要补充】。
	//
	//    Godot 版的全部数值都是按【单场战斗】调的
	//    （"enc_02 总输出 19 点/回合 → 镇妖者撑 4 回合"），
	//    也就是说单场战斗本身就贴着生死线。
	//    而一层循环要连打 4 场且生命跨战斗继承 ——
	//    血量预算 80 + 营地 24 = 104 点，实测需要约 200 点。
	//
	//    HexPlaytest 的数据：60 局阵亡率 100%、击败 Boss 0%，
	//    且连续三次增强机器人（会格挡、会走位、会追击）都无法改善，
	//    证明这是结构性缺口而非打法问题。
	//
	//    Boss 房不回血：那是终点，回血没有意义，
	//    而且"打完 Boss 满血进层结算"会削弱终局的紧张感。
	if (bWasCombat && !bWasBoss && HeroHP > 0)
	{
		const int32 Heal = FMath::Max(1,
			FMath::FloorToInt(HeroHPMax * HexK::PostBattleHealRatio));
		HeroHP = FMath::Min(HeroHPMax, HeroHP + Heal);
	}

	return Delta;
}

int32 FHexRunState::RestAtCamp()
{
	const FHexRoomNode* Room = Map.FindRoom(Map.GetCurrentRoomId());
	if (!Room || Room->Type != EHexRoomType::Camp)
	{
		return 0;
	}

	// ⚠️ 营地只能用一次。
	//    漏掉这个检查时，玩家站在营地反复点"休息"就能回满血 ——
	//    血量管理这条压力线彻底消失，而且它不报错、不崩溃。
	//    （现在允许走回已清空的房间，所以这个检查是必须的。）
	if (Room->Visibility == EHexRoomVisibility::Cleared)
	{
		return 0;
	}

	// 营地回复（HexK::CampHealRatio = 45%）。
	// ⚠️ 为什么不是满血：满血营地会让"血量管理"这条压力线消失 ——
	//    玩家可以随意挨打，反正营地能补满。
	//    45% 让营地成为"止损点"而非"重置点"，与 §4.1「硬但公平」一致。
	const int32 Heal = FMath::Max(1,
		FMath::FloorToInt(HeroHPMax * HexK::CampHealRatio));
	const int32 Before = HeroHP;
	HeroHP = FMath::Min(HeroHPMax, HeroHP + Heal);

	Map.ClearCurrentRoom();
	++Stats.RoomsCleared;

	return HeroHP - Before;
}

// ══════════════════════════════════════════════════════════ 层结算（§9.5）

void FHexRunState::GenerateFloorRewards(
	FHexRngStreams& Rng, TArray<FHexRewardOption>& Out) const
{
	Out.Reset();

	// ── 已持有的符文（含已装备与背包里的），三选一必须排除它们
	TSet<FName> Owned;
	{
		TArray<TPair<int32, const FHexRuneData*>> Equipped;
		RuneLoadout.GetRunesInOrder(Equipped);
		for (const TPair<int32, const FHexRuneData*>& P : Equipped)
		{
			if (P.Value)
			{
				Owned.Add(P.Value->Id);
			}
		}
		for (const FName& R : RuneInventory)
		{
			Owned.Add(R);
		}
	}

	// ── 候选符文池
	//
	// ⚠️ 诅咒符文【不进层结算的三选一】。
	//    三选一是"必须选一个"的场合，塞进诅咒等于强迫玩家吃亏。
	//    诅咒符文的正确出口是事件房与高腐蚀度掉落（玩家可以拒绝）。
	TArray<FName> Candidates;
	for (const FHexRuneData& R : FHexRuneLibrary::AllRunes())
	{
		if (R.bIsCursed)
		{
			continue;
		}
		if (Owned.Contains(R.Id))
		{
			continue;
		}
		Candidates.Add(R.Id);
	}

	// ── 抽 3 个（不足则给全部）
	//
	// ⚠️ 用 Fisher-Yates 的前 3 项而非"抽 3 次"：
	//    抽 3 次可能抽到重复，三选一里出现两个相同选项等于只有两个选项。
	Rng.Shuffle(Candidates, EHexRngStream::Loot);

	const int32 Take = FMath::Min(3, Candidates.Num());
	for (int32 I = 0; I < Take; ++I)
	{
		const FHexRuneData* R = FHexRuneLibrary::FindRune(Candidates[I]);
		if (!R)
		{
			continue;
		}

		FHexRewardOption Opt;
		Opt.Kind = FHexRewardOption::EKind::Rune;
		Opt.ContentId = R->Id;
		Opt.DisplayName = R->DisplayName;
		// ⚠️ 用 MechanicText 而非 FlavorText：
		//    三选一是需要推理的决策点，玩家要看的是"它做什么"（R8）。
		Opt.Description = R->MechanicText;
		Out.Add(Opt);
	}

	// ── 固定奖励：卡组容量 +1~2（用户决策 q17）
	{
		FHexRewardOption Opt;
		Opt.Kind = FHexRewardOption::EKind::DeckCapacity;
		// 腐蚀度高时给 +2 —— 又一条"多探有回报"的正反馈
		Opt.Amount = (Corruption >= HexK::CorruptionPerDifficultyTier) ? 2 : 1;
		Opt.DisplayName = TEXT("卡组容量");
		Opt.Description = FString::Printf(
			TEXT("卡组容量 +%d（当前 %d/%d）"), Opt.Amount, GetUsedCapacity(), DeckCapacity);
		Out.Add(Opt);
	}

	// ── 固定奖励：定向碎片
	{
		FHexRewardOption Opt;
		Opt.Kind = FHexRewardOption::EKind::Shards;
		// 碎片量随腐蚀度增长（§9.4 的掉落正反馈在碎片上的体现）
		Opt.Amount = 40 + FMath::FloorToInt(
			static_cast<float>(Corruption) * HexK::CorruptionLootStep * 40.0f);
		Opt.DisplayName = TEXT("碎片");
		Opt.Description = FString::Printf(
			TEXT("获得 %d 枚碎片（可用于重塑装备词条）"), Opt.Amount);
		Out.Add(Opt);
	}
}

bool FHexRunState::ApplyReward(const FHexRewardOption& Option, FHexRngStreams& Rng)
{
	switch (Option.Kind)
	{
	case FHexRewardOption::EKind::Rune:
	{
		const FHexRuneData* R = FHexRuneLibrary::FindRune(Option.ContentId);
		if (!R)
		{
			return false;
		}

		// 有空槽就直接装上；满槽则进背包由玩家决定替换哪个
		// （6 槽有序，替换哪个槽是有意义的选择 —— §6.5）
		const int32 Empty = RuneLoadout.FindFirstEmptySlot();
		if (Empty != INDEX_NONE)
		{
			RuneLoadout.SetSlot(Empty, R);
		}
		else
		{
			RuneInventory.AddUnique(R->Id);
		}
		return true;
	}

	case FHexRewardOption::EKind::DeckCapacity:
		DeckCapacity = FMath::Min(HexK::MaxDeckCapacity, DeckCapacity + Option.Amount);
		return true;

	case FHexRewardOption::EKind::Shards:
		Shards += Option.Amount;
		return true;

	case FHexRewardOption::EKind::Card:
		return AddCardToDeck(Option.ContentId);

	case FHexRewardOption::EKind::Equip:
	{
		const FHexEquipInstance Inst = FHexEquipGenerator::GenerateRandom(
			EHexEquipSlot::Weapon, Corruption, Rng, NextCardUid++);
		if (!Inst.IsValid())
		{
			return false;
		}
		EquipInventory.Add(Inst);
		return true;
	}

	default:
		return false;
	}
}

// ══════════════════════════════════════════════════════════ 序列化

void FHexRunState::Serialize(FArchive& Ar)
{
	Ar << MasterSeed;
	Ar << HeroId;
	Ar << FloorIndex;
	Ar << Corruption;
	Ar << Shards;
	Ar << DeckCapacity;
	Ar << HeroHP;
	Ar << HeroHPMax;
	Ar << NextCardUid;

	int32 DeckCount = Deck.Num();
	Ar << DeckCount;
	if (Ar.IsLoading())
	{
		Deck.SetNum(DeckCount);
	}
	for (int32 I = 0; I < DeckCount; ++I)
	{
		Deck[I].Serialize(Ar);
	}

	Ar << RuneInventory;

	int32 EquipCount = EquipInventory.Num();
	Ar << EquipCount;
	if (Ar.IsLoading())
	{
		EquipInventory.SetNum(EquipCount);
	}
	for (int32 I = 0; I < EquipCount; ++I)
	{
		EquipInventory[I].Serialize(Ar);
	}

	EquipLoadout.Serialize(Ar);
	Map.Serialize(Ar);
	Stats.Serialize(Ar);

	// ⚠️ RuneLoadout 存的是【指针】，不能直接序列化。
	//    这里按槽位存 id，加载时从符文库重新解引用。
	//    这也是为什么符文表必须是"代码内建的稳定表"——
	//    若符文表顺序变化，存档会指向错误的符文。
	{
		TArray<FName> SlotIds;
		SlotIds.SetNum(FHexRuneLoadout::SlotCount);

		if (Ar.IsSaving())
		{
			for (int32 I = 0; I < FHexRuneLoadout::SlotCount; ++I)
			{
				const FHexRuneData* R = RuneLoadout.GetSlot(I);
				SlotIds[I] = R ? R->Id : NAME_None;
			}
		}

		Ar << SlotIds;

		if (Ar.IsLoading())
		{
			RuneLoadout = FHexRuneLoadout();
			for (int32 I = 0; I < FHexRuneLoadout::SlotCount && I < SlotIds.Num(); ++I)
			{
				if (!SlotIds[I].IsNone())
				{
					RuneLoadout.SetSlot(I, FHexRuneLibrary::FindRune(SlotIds[I]));
				}
			}
		}
	}
}

uint32 FHexRunState::ContentHash() const
{
	uint32 H = 0x52554E21u;  // "RUN!"

	H = HashCombine(H, GetTypeHash(HeroId));
	H = HashCombine(H, GetTypeHash(FloorIndex));
	H = HashCombine(H, GetTypeHash(Corruption));
	H = HashCombine(H, GetTypeHash(Shards));
	H = HashCombine(H, GetTypeHash(DeckCapacity));
	H = HashCombine(H, GetTypeHash(HeroHP));

	for (const FHexCardInstance& C : Deck)
	{
		H = HashCombine(H, GetTypeHash(C.CardId));
		H = HashCombine(H, GetTypeHash(C.Uid));
	}

	H = HashCombine(H, EquipLoadout.ContentHash());
	H = HashCombine(H, Map.ContentHash());

	for (int32 I = 0; I < FHexRuneLoadout::SlotCount; ++I)
	{
		const FHexRuneData* R = RuneLoadout.GetSlot(I);
		H = HashCombine(H, GetTypeHash(R ? R->Id : NAME_None));
	}

	return H;
}
