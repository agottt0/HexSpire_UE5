// Copyright Hex Spire. All Rights Reserved.
//
// 注入式 RNG —— 架构纪律 1（策划案 §12.1）
//
// ⚠️ 这是普通 C++ 类而非 Subsystem，因为 core 不许引用引擎全局设施。
//   由 BattleState 通过构造参数【注入】。BattleSim 与验证器直接 new 一个。
//
// ⚠️ 禁止在任何游戏逻辑中使用 FMath::Rand() / FRandomStream 全局实例 /
//   TArray::Sort 的随机比较。它们会破坏确定性，且在 10 万场模拟里
//   "每次都随机成功"，极难发现。洗牌请用本类的 Shuffle()。
//
// 单一主 seed 派生 5 条独立子流，各流状态可存档，
// 从而支持：种子分享、录像回放、bug 精确复现、可重复的自动化测试。

#pragma once

#include "CoreMinimal.h"
#include "Core/HexSpireEnums.h"

/**
 * xoshiro256** 风格的确定性 PRNG。
 *
 * ⚠️ 不用 UE 的 FRandomStream —— 它内部是 32 位 LCG，
 *   周期短且低位随机性差，在"洗 11 张牌 × 10 万次"这种场景下
 *   会出现可观测的偏斜。
 */
struct HEXSPIRECORE_API FHexRandom
{
	uint64 State[4] = { 0, 0, 0, 0 };

	FHexRandom() = default;
	explicit FHexRandom(uint64 Seed) { Seed64(Seed); }

	void Seed64(uint64 Seed);

	/** [0, 2^64) 均匀分布 */
	uint64 NextUInt64();

	/** [0, 1) 均匀分布 */
	float NextFloat();

	/**
	 * [Min, Max] 闭区间均匀整数。
	 * 用无偏拒绝采样，避免取模偏斜（洗牌正确性依赖这一点）。
	 */
	int32 NextRange(int32 Min, int32 Max);

	void Serialize(FArchive& Ar);
};

/**
 * 五条独立 RNG 子流的持有者。
 *
 * 分流的意义：玩家在商店里多点一次鼠标，不应该改变下一场战斗的暴击结果。
 * 若共用一条流，任何 UI 层的意外消费都会让整局"漂移"，
 * 种子复现与回放全部失效。
 */
class HEXSPIRECORE_API FHexRngStreams
{
public:
	explicit FHexRngStreams(uint64 InMasterSeed = 0);

	void Reseed(uint64 InMasterSeed);

	FHexRandom& Stream(EHexRngStream S) { return Streams[static_cast<int32>(S)]; }
	const FHexRandom& Stream(EHexRngStream S) const { return Streams[static_cast<int32>(S)]; }

	FHexRandom& Map() { return Stream(EHexRngStream::Map); }
	FHexRandom& Loot() { return Stream(EHexRngStream::Loot); }
	FHexRandom& Combat() { return Stream(EHexRngStream::Combat); }
	FHexRandom& Deck() { return Stream(EHexRngStream::Deck); }
	FHexRandom& Event() { return Stream(EHexRngStream::Event); }

	/**
	 * Fisher-Yates 洗牌。就地修改，使用指定流。
	 * 这是全项目【唯一】允许的洗牌实现。
	 */
	template<typename T>
	void Shuffle(TArray<T>& Arr, EHexRngStream S)
	{
		FHexRandom& R = Stream(S);
		for (int32 I = Arr.Num() - 1; I > 0; --I)
		{
			const int32 J = R.NextRange(0, I);
			++DrawCount;
			Arr.Swap(I, J);
		}
	}

	/** 掷一次 [0,1)，并计入 DrawCount */
	float RandFloat(EHexRngStream S);

	/** 掷一次 [Min,Max]，并计入 DrawCount */
	int32 RandRange(EHexRngStream S, int32 Min, int32 Max);

	/** 概率判定：P ∈ [0,1] */
	bool Chance(EHexRngStream S, float P);

	/** 加权抽取：返回命中的下标；权重全为 0 时返回 0 */
	int32 WeightedPick(EHexRngStream S, const TArray<float>& Weights);

	uint64 GetMasterSeed() const { return MasterSeed; }

	/**
	 * 任何流被消费的累计次数。
	 * Undo 用它判断"是否已产生随机结果"（撤销屏障，架构文档 §4.7）。
	 */
	int32 GetDrawCount() const { return DrawCount; }

	void Serialize(FArchive& Ar);

private:
	/**
	 * splitmix64 从主 seed 派生子 seed。必须是纯函数。
	 * 避免相邻 seed 产生相关序列。
	 */
	static uint64 DeriveSeed(uint64 Master, int32 StreamIndex);

	uint64 MasterSeed = 0;
	int32 DrawCount = 0;
	FHexRandom Streams[static_cast<int32>(EHexRngStream::Count)];
};
