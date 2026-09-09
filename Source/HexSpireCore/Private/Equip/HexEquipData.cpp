// Copyright Hex Spire. All Rights Reserved.

#include "Equip/HexEquipData.h"
#include "Rng/HexRngStreams.h"

// ══════════════════════════════════════════════════════════ 攻击覆写

void FHexAttackOverride::ApplyTo(FHexTargetSpec& Spec) const
{
	if (!bActive)
	{
		return;
	}

	Spec.Shape = Shape;
	Spec.RangeMin = RangeMin;
	Spec.RangeMax = RangeMax;
	Spec.AreaSize = AreaSize;
	Spec.bRequiresLineOfSight = bRequiresLineOfSight;

	// ⚠️ 与 FHexTargetSpec::MakeTile 保持一致的语义：
	//    只有 Tile 形状能指向空格。武器把《攻击》改成 Tile 形状时
	//    必须同步这个标记，否则玩家点空格会被判非法目标。
	Spec.bCanTargetEmptyCell = (Shape == EHexTargetShape::Tile);
}

// ══════════════════════════════════════════════════════════ 实例

void FHexEquipInstance::Serialize(FArchive& Ar)
{
	Ar << Uid;
	Ar << EquipId;

	uint8 R = static_cast<uint8>(Rarity);
	Ar << R;
	if (Ar.IsLoading())
	{
		Rarity = static_cast<EHexRarity>(R);
	}

	Ar << RolledAffixIds;
	Ar << ReforgeCount;
}

uint32 FHexEquipInstance::ContentHash() const
{
	uint32 H = GetTypeHash(EquipId);
	H = HashCombine(H, GetTypeHash(Uid));
	H = HashCombine(H, GetTypeHash(static_cast<uint8>(Rarity)));
	H = HashCombine(H, GetTypeHash(ReforgeCount));
	// 词条顺序参与哈希：roll 的顺序是 RNG 消费顺序的直接体现，
	// 确定性验证要求同 seed 下连顺序都一致
	for (const FName& A : RolledAffixIds)
	{
		H = HashCombine(H, GetTypeHash(A));
	}
	return H;
}

// ══════════════════════════════════════════════════════════ 稀有度 → 词条数

int32 FHexEquipLibrary::AffixCountForRarity(EHexRarity Rarity)
{
	switch (Rarity)
	{
	case EHexRarity::Common:     return 1;
	case EHexRarity::Uncommon:   return 2;
	case EHexRarity::Rare:       return 3;
	case EHexRarity::Epic:       return 4;
	case EHexRarity::Legendary:  return 5;
	// 诅咒装备：词条多但必然掺杂负面词条（负面词条在词条表里
	// 用负的 FlatValue 表达，走同一套 roll 逻辑）
	case EHexRarity::Cursed:     return 4;
	default:                     return 1;
	}
}

// ══════════════════════════════════════════════════════════ 生成器

namespace
{
	/**
	 * 收集某槽位 + 某稀有度下所有可 roll 的词条下标。
	 *
	 * ⚠️ 返回【下标】而非指针：加权抽取要构造权重数组，
	 *    下标能直接对应，避免两个数组不同步。
	 */
	void CollectRollableAffixes(
		EHexEquipSlot Slot,
		EHexRarity Rarity,
		const TArray<FName>& AlreadyHave,
		TArray<int32>& OutIndices,
		TArray<float>& OutWeights)
	{
		OutIndices.Reset();
		OutWeights.Reset();

		const TArray<FHexAffixDef>& All = FHexEquipLibrary::AllAffixes();

		// 先把已有词条折算成【族】集合。
		// ⚠️ 按族去重而非按 id：否则 "锋利+3 / 锐利+5 / 凶戾+8"
		//    这三个同属性档位会同时命中，一件装备给 +16 攻击力。
		TSet<FName> TakenFamilies;
		for (const FName& Have : AlreadyHave)
		{
			if (const FHexAffixDef* A = FHexEquipLibrary::FindAffix(Have))
			{
				TakenFamilies.Add(A->EffectiveFamily());
			}
		}

		for (int32 I = 0; I < All.Num(); ++I)
		{
			const FHexAffixDef& A = All[I];

			// Weight=0 表示只能固定挂载，不参与随机
			if (A.Weight <= 0)
			{
				continue;
			}
			if (!A.AllowsSlot(Slot))
			{
				continue;
			}
			// 稀有度门槛：强词条不该出现在普通装备上
			if (static_cast<uint8>(Rarity) < static_cast<uint8>(A.MinRarity))
			{
				continue;
			}
			// 同族互斥（同时覆盖了"完全相同的词条"这种情况）
			if (TakenFamilies.Contains(A.EffectiveFamily()))
			{
				continue;
			}

			OutIndices.Add(I);
			OutWeights.Add(static_cast<float>(A.Weight));
		}
	}

	/** 按稀有度与词条池 roll 出随机词条 */
	void RollAffixes(
		EHexEquipSlot Slot,
		EHexRarity Rarity,
		const TArray<FName>& Inherent,
		FHexRngStreams& Rng,
		TArray<FName>& OutRolled)
	{
		OutRolled.Reset();

		const int32 Want = FHexEquipLibrary::AffixCountForRarity(Rarity);

		// 已有词条 = 固有 + 已 roll 出的，两者都要去重
		TArray<FName> Taken = Inherent;

		for (int32 N = 0; N < Want; ++N)
		{
			TArray<int32> Indices;
			TArray<float> Weights;
			CollectRollableAffixes(Slot, Rarity, Taken, Indices, Weights);

			// 词条池被抽干：不报错，就少几条。
			// 这在灰盒期很常见（词条表还小），不该阻塞流程。
			if (Indices.Num() == 0)
			{
				break;
			}

			const int32 Pick = Rng.WeightedPick(EHexRngStream::Loot, Weights);
			const int32 AffixIndex = Indices[FMath::Clamp(Pick, 0, Indices.Num() - 1)];
			const FName Id = FHexEquipLibrary::AllAffixes()[AffixIndex].Id;

			OutRolled.Add(Id);
			Taken.Add(Id);
		}
	}
}

FHexEquipInstance FHexEquipGenerator::Generate(
	FName EquipId, EHexRarity Rarity, FHexRngStreams& Rng, int32 Uid)
{
	FHexEquipInstance Inst;

	const FHexEquipData* Data = FHexEquipLibrary::FindEquip(EquipId);
	if (!Data)
	{
		// 未知 id 返回无效实例（Uid=0）。调用方用 IsValid() 判断。
		// 不 ensure：掉落表打错 id 时应当跳过这次掉落，而不是中断整场战斗。
		return Inst;
	}

	Inst.Uid = Uid;
	Inst.EquipId = EquipId;

	// 装备自身的稀有度下限优先：一件"传说级基座"不该以普通稀有度掉出
	Inst.Rarity = static_cast<uint8>(Rarity) < static_cast<uint8>(Data->BaseRarity)
		? Data->BaseRarity
		: Rarity;

	RollAffixes(Data->Slot, Inst.Rarity, Data->InherentAffixIds, Rng, Inst.RolledAffixIds);

	return Inst;
}

EHexRarity FHexEquipGenerator::RollRarity(int32 Corruption, FHexRngStreams& Rng)
{
	// ⚠️ 腐蚀度的正反馈（§9.4）必须能被玩家【感受到】，
	//    否则 D4 盲探里"多探一间房"的风险收益不成立。
	//
	// 基础权重（腐蚀度 0）：普通 60 / 精良 28 / 稀有 10 / 史诗 2
	// 每点腐蚀度按 CorruptionLootStep(0.15) 把权重往高稀有度搬。
	const float C = static_cast<float>(FMath::Max(0, Corruption));
	const float Shift = C * HexK::CorruptionLootStep;

	TArray<float> Weights;
	Weights.Add(FMath::Max(1.0f, 60.0f - 6.0f * Shift));   // Common
	Weights.Add(FMath::Max(1.0f, 28.0f - 1.0f * Shift));   // Uncommon
	Weights.Add(10.0f + 4.0f * Shift);                     // Rare
	Weights.Add(2.0f + 3.0f * Shift);                      // Epic

	const int32 Pick = Rng.WeightedPick(EHexRngStream::Loot, Weights);
	switch (Pick)
	{
	case 0:  return EHexRarity::Common;
	case 1:  return EHexRarity::Uncommon;
	case 2:  return EHexRarity::Rare;
	default: return EHexRarity::Epic;
	}
}

FHexEquipInstance FHexEquipGenerator::GenerateRandom(
	EHexEquipSlot Slot, int32 Corruption, FHexRngStreams& Rng, int32 Uid)
{
	TArray<FName> Candidates;
	FHexEquipLibrary::GetEquipIdsForSlot(Slot, Candidates);

	if (Candidates.Num() == 0)
	{
		return FHexEquipInstance();
	}

	// ⚠️ 先掷稀有度再挑基座，顺序固定。
	//    反过来（先挑基座再掷稀有度）也能跑，但两种顺序消费 RNG 的
	//    次序不同，会导致存档/回放不兼容。顺序本身是协议的一部分。
	const EHexRarity Rarity = RollRarity(Corruption, Rng);

	const int32 Index = Rng.RandRange(EHexRngStream::Loot, 0, Candidates.Num() - 1);
	return Generate(Candidates[Index], Rarity, Rng, Uid);
}

void FHexEquipGenerator::Reforge(FHexEquipInstance& Inst, FHexRngStreams& Rng)
{
	const FHexEquipData* Data = FHexEquipLibrary::FindEquip(Inst.EquipId);
	if (!Data)
	{
		return;
	}

	// 稀有度与固有词条【不变】—— 重塑改的是运气，不是品质。
	// 若重塑能提升稀有度，玩家会无脑重塑到传说，稀有度维度崩塌。
	RollAffixes(Data->Slot, Inst.Rarity, Data->InherentAffixIds, Rng, Inst.RolledAffixIds);

	++Inst.ReforgeCount;
}

int32 FHexEquipGenerator::ReforgeCost(const FHexEquipInstance& Inst)
{
	// 基础消耗按稀有度，每次重塑后 +50%（向下取整）。
	// ⚠️ 递增是必须的：固定消耗下玩家会重塑到完美词条为止，
	//    随机性带来的取舍消失，装备变成"存够碎片就一定最优"。
	const int32 Base = 20 + 15 * static_cast<int32>(Inst.Rarity);
	const float Mult = 1.0f + 0.5f * static_cast<float>(Inst.ReforgeCount);
	return FMath::FloorToInt(static_cast<float>(Base) * Mult);
}

// ══════════════════════════════════════════════════════════ 三槽装载

FHexEquipLoadout::FHexEquipLoadout()
{
	for (int32 I = 0; I < SlotCount; ++I)
	{
		Slots[I] = FHexEquipInstance();
	}
}

const FHexEquipInstance* FHexEquipLoadout::GetSlot(EHexEquipSlot Slot) const
{
	const int32 I = static_cast<int32>(Slot);
	if (I < 0 || I >= SlotCount)
	{
		return nullptr;
	}
	return Slots[I].IsValid() ? &Slots[I] : nullptr;
}

bool FHexEquipLoadout::Equip(const FHexEquipInstance& Inst)
{
	if (!Inst.IsValid())
	{
		return false;
	}

	const FHexEquipData* Data = FHexEquipLibrary::FindEquip(Inst.EquipId);
	if (!Data)
	{
		return false;
	}

	const int32 I = static_cast<int32>(Data->Slot);
	if (I < 0 || I >= SlotCount)
	{
		return false;
	}

	// 直接覆盖旧装备。调用方负责把换下来的装备放回背包 ——
	// 这里不管理背包，否则 core 会被拖进物品栏的复杂度里。
	Slots[I] = Inst;
	return true;
}

void FHexEquipLoadout::Unequip(EHexEquipSlot Slot)
{
	const int32 I = static_cast<int32>(Slot);
	if (I >= 0 && I < SlotCount)
	{
		Slots[I] = FHexEquipInstance();
	}
}

int FHexEquipLoadout::GetFilledCount() const
{
	int N = 0;
	for (int32 I = 0; I < SlotCount; ++I)
	{
		if (Slots[I].IsValid())
		{
			++N;
		}
	}
	return N;
}

void FHexEquipLoadout::ForEachAffix(
	TFunctionRef<void(EHexEquipSlot, const FHexAffixDef&)> Fn) const
{
	// 按槽位下标升序遍历 —— 确定性要求（纪律 5）
	for (int32 I = 0; I < SlotCount; ++I)
	{
		const FHexEquipInstance& Inst = Slots[I];
		if (!Inst.IsValid())
		{
			continue;
		}

		const FHexEquipData* Data = FHexEquipLibrary::FindEquip(Inst.EquipId);
		if (!Data)
		{
			continue;
		}

		const EHexEquipSlot Slot = static_cast<EHexEquipSlot>(I);

		// 固有词条先于随机词条 —— UI 展示顺序与结算顺序保持一致
		for (const FName& Aid : Data->InherentAffixIds)
		{
			if (const FHexAffixDef* A = FHexEquipLibrary::FindAffix(Aid))
			{
				Fn(Slot, *A);
			}
		}
		for (const FName& Aid : Inst.RolledAffixIds)
		{
			if (const FHexAffixDef* A = FHexEquipLibrary::FindAffix(Aid))
			{
				Fn(Slot, *A);
			}
		}
	}
}

int32 FHexEquipLoadout::GetStatBonus(EHexStat Stat, int32 BaseValue) const
{
	int32 Flat = 0;
	float Percent = 0.0f;

	ForEachAffix([&](EHexEquipSlot, const FHexAffixDef& A)
	{
		if (A.Stat != Stat)
		{
			return;
		}
		if (A.Kind == EHexAffixKind::StatFlat)
		{
			Flat += A.FlatValue;
		}
		else if (A.Kind == EHexAffixKind::StatPercent)
		{
			// ⚠️ 百分比【相加】而非相乘：三件 +15% = +45%，不是 ×1.15³=+52%。
			//    相乘会产生玩家算不明白的复利，且与 §4.4「加法先做完」的
			//    整体口径冲突。
			Percent += A.PercentValue;
		}
	});

	return Flat + FMath::FloorToInt(static_cast<float>(BaseValue) * Percent);
}

FHexRuneLoadout::FAggregated FHexEquipLoadout::AggregateRule(EHexGameRule Rule) const
{
	FHexRuneLoadout::FAggregated Out;

	// 收集后显式排序，与符文的聚合语义完全一致：
	// 先按 ApplyOrder 升序，同序按槽位升序。
	struct FEntry
	{
		int32 SlotIndex;
		const FHexRuleOverride* Override;
	};
	TArray<FEntry> Entries;

	ForEachAffix([&](EHexEquipSlot Slot, const FHexAffixDef& A)
	{
		if (A.Kind != EHexAffixKind::RuleOverride)
		{
			return;
		}
		if (A.Rule.Rule != Rule)
		{
			return;
		}
		Entries.Add({ static_cast<int32>(Slot), &A.Rule });
	});

	if (Entries.Num() == 0)
	{
		return Out;
	}

	Entries.Sort([](const FEntry& A, const FEntry& B)
	{
		if (A.Override->ApplyOrder != B.Override->ApplyOrder)
		{
			return A.Override->ApplyOrder < B.Override->ApplyOrder;
		}
		return A.SlotIndex < B.SlotIndex;
	});

	Out.bFound = true;
	for (const FEntry& E : Entries)
	{
		const FHexRuleOverride& O = *E.Override;
		if (O.bIsDelta)
		{
			Out.IntDelta += O.IntValue;
			Out.FloatDelta += O.FloatValue;
		}
		else
		{
			// 覆盖型取【最后一个】（排序后的末位）
			Out.bHasOverride = true;
			Out.IntOverride = O.IntValue;
			Out.FloatOverride = O.FloatValue;
			Out.bBoolOverride = O.bBoolValue;
		}
	}

	return Out;
}

void FHexEquipLoadout::GetOverriddenRules(TArray<EHexGameRule>& Out) const
{
	Out.Reset();

	TSet<EHexGameRule> Seen;
	ForEachAffix([&](EHexEquipSlot, const FHexAffixDef& A)
	{
		if (A.Kind == EHexAffixKind::RuleOverride)
		{
			Seen.Add(A.Rule.Rule);
		}
	});

	for (const EHexGameRule R : Seen)
	{
		Out.Add(R);
	}

	// 按枚举值排序 —— TSet 的遍历顺序不保证稳定，
	// 直接输出会让 RuleAggregate 的构建顺序漂移
	Out.Sort([](const EHexGameRule& A, const EHexGameRule& B)
	{
		return static_cast<uint8>(A) < static_cast<uint8>(B);
	});
}

FHexAttackOverride FHexEquipLoadout::GetAttackOverride() const
{
	const FHexEquipInstance* Weapon = GetSlot(EHexEquipSlot::Weapon);
	if (!Weapon)
	{
		return FHexAttackOverride();
	}

	const FHexEquipData* Data = FHexEquipLibrary::FindEquip(Weapon->EquipId);
	if (!Data)
	{
		return FHexAttackOverride();
	}

	return Data->AttackOverride;
}

void FHexEquipLoadout::GetInjectedCardIds(TArray<FName>& Out) const
{
	Out.Reset();

	for (int32 I = 0; I < SlotCount; ++I)
	{
		if (!Slots[I].IsValid())
		{
			continue;
		}
		const FHexEquipData* Data = FHexEquipLibrary::FindEquip(Slots[I].EquipId);
		if (!Data)
		{
			continue;
		}
		Out.Append(Data->InjectedCardIds);
	}
}

void FHexEquipLoadout::GetTriggersInOrder(
	TArray<TPair<int32, const FHexRuneTrigger*>>& Out) const
{
	Out.Reset();

	// SlotOrder 10/11/12 —— 这三个槽位号在 HexTriggerBus.h 的
	// 分层约定里早已预留给装备（0=英雄被动 / 1-6=符文 / 10-12=装备 / 20+=状态）。
	// 保证「符文永远在装备之前结算」，这是 §6.5 顺序语义的一部分。
	ForEachAffix([&](EHexEquipSlot Slot, const FHexAffixDef& A)
	{
		if (A.Kind != EHexAffixKind::Trigger)
		{
			return;
		}
		const int32 SlotOrder = 10 + static_cast<int32>(Slot);
		Out.Add(TPair<int32, const FHexRuneTrigger*>(SlotOrder, &A.Trigger));
	});
}

void FHexEquipLoadout::GetAffixesOfSlot(
	EHexEquipSlot Slot, TArray<const FHexAffixDef*>& Out) const
{
	Out.Reset();

	ForEachAffix([&](EHexEquipSlot S, const FHexAffixDef& A)
	{
		if (S == Slot)
		{
			Out.Add(&A);
		}
	});
}

void FHexEquipLoadout::Serialize(FArchive& Ar)
{
	for (int32 I = 0; I < SlotCount; ++I)
	{
		Slots[I].Serialize(Ar);
	}
}

uint32 FHexEquipLoadout::ContentHash() const
{
	uint32 H = 0x45515550u;  // "EQUP"
	for (int32 I = 0; I < SlotCount; ++I)
	{
		H = HashCombine(H, Slots[I].ContentHash());
	}
	return H;
}
