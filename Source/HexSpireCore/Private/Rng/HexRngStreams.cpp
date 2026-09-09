// Copyright Hex Spire. All Rights Reserved.

#include "Rng/HexRngStreams.h"

namespace
{
	FORCEINLINE uint64 Rotl64(uint64 X, int32 K)
	{
		return (X << K) | (X >> (64 - K));
	}

	/** splitmix64：用于从单个 seed 铺开 xoshiro 的 256 位状态 */
	FORCEINLINE uint64 SplitMix64(uint64& X)
	{
		X += 0x9E3779B97F4A7C15ULL;
		uint64 Z = X;
		Z = (Z ^ (Z >> 30)) * 0xBF58476D1CE4E5B9ULL;
		Z = (Z ^ (Z >> 27)) * 0x94D049BB133111EBULL;
		return Z ^ (Z >> 31);
	}
}

// ───────────────────────────────────────────────────────── FHexRandom

void FHexRandom::Seed64(uint64 Seed)
{
	// 全 0 状态会让 xoshiro 永久输出 0，必须避开
	uint64 X = (Seed == 0) ? 0xDEADBEEFCAFEBABEULL : Seed;
	for (int32 I = 0; I < 4; ++I)
	{
		State[I] = SplitMix64(X);
	}
}

uint64 FHexRandom::NextUInt64()
{
	// xoshiro256**
	const uint64 Result = Rotl64(State[1] * 5, 7) * 9;
	const uint64 T = State[1] << 17;

	State[2] ^= State[0];
	State[3] ^= State[1];
	State[1] ^= State[2];
	State[0] ^= State[3];
	State[2] ^= T;
	State[3] = Rotl64(State[3], 45);

	return Result;
}

float FHexRandom::NextFloat()
{
	// 取高 24 位映射到 [0,1)，避免低位随机性问题
	const uint32 Bits = static_cast<uint32>(NextUInt64() >> 40); // 24 bits
	return static_cast<float>(Bits) * (1.0f / 16777216.0f);
}

int32 FHexRandom::NextRange(int32 Min, int32 Max)
{
	if (Max <= Min)
	{
		return Min;
	}
	const uint64 Span = static_cast<uint64>(Max) - static_cast<uint64>(Min) + 1;

	// 无偏拒绝采样：丢弃会造成取模偏斜的尾部区间。
	// 洗牌的正确性依赖这一点 —— 直接 % Span 会让前若干个值概率略高，
	// 在 10 万次洗牌的统计里是可观测的。
	const uint64 Limit = UINT64_MAX - (UINT64_MAX % Span) - 1;
	uint64 R = NextUInt64();
	while (R > Limit)
	{
		R = NextUInt64();
	}
	return Min + static_cast<int32>(R % Span);
}

void FHexRandom::Serialize(FArchive& Ar)
{
	for (int32 I = 0; I < 4; ++I)
	{
		Ar << State[I];
	}
}

// ───────────────────────────────────────────────────────── FHexRngStreams

FHexRngStreams::FHexRngStreams(uint64 InMasterSeed)
{
	Reseed(InMasterSeed);
}

void FHexRngStreams::Reseed(uint64 InMasterSeed)
{
	MasterSeed = InMasterSeed;
	DrawCount = 0;
	for (int32 I = 0; I < static_cast<int32>(EHexRngStream::Count); ++I)
	{
		Streams[I].Seed64(DeriveSeed(InMasterSeed, I));
	}
}

uint64 FHexRngStreams::DeriveSeed(uint64 Master, int32 StreamIndex)
{
	uint64 X = Master + static_cast<uint64>(StreamIndex + 1) * 0x9E3779B97F4A7C15ULL;
	return SplitMix64(X);
}

float FHexRngStreams::RandFloat(EHexRngStream S)
{
	++DrawCount;
	return Stream(S).NextFloat();
}

int32 FHexRngStreams::RandRange(EHexRngStream S, int32 Min, int32 Max)
{
	++DrawCount;
	return Stream(S).NextRange(Min, Max);
}

bool FHexRngStreams::Chance(EHexRngStream S, float P)
{
	if (P <= 0.0f)
	{
		return false;
	}
	if (P >= 1.0f)
	{
		return true;
	}
	return RandFloat(S) < P;
}

int32 FHexRngStreams::WeightedPick(EHexRngStream S, const TArray<float>& Weights)
{
	if (Weights.Num() == 0)
	{
		return 0;
	}
	float Total = 0.0f;
	for (const float W : Weights)
	{
		Total += FMath::Max(0.0f, W);
	}
	if (Total <= 0.0f)
	{
		return 0;
	}
	const float Roll = RandFloat(S) * Total;
	float Accum = 0.0f;
	for (int32 I = 0; I < Weights.Num(); ++I)
	{
		Accum += FMath::Max(0.0f, Weights[I]);
		if (Roll < Accum)
		{
			return I;
		}
	}
	return Weights.Num() - 1;
}

void FHexRngStreams::Serialize(FArchive& Ar)
{
	Ar << MasterSeed;
	Ar << DrawCount;
	for (int32 I = 0; I < static_cast<int32>(EHexRngStream::Count); ++I)
	{
		Streams[I].Serialize(Ar);
	}
}
