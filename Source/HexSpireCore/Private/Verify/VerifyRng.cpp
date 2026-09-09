// Copyright Hex Spire. All Rights Reserved.
//
// RNG 验证 —— 架构纪律 1 与 5 的机械保证
//
// 为什么要专门验 RNG：确定性是整个验证体系的地基。
// 若 RNG 有偏或不可复现，"10 万场模拟得出的平衡结论"全部作废，
// 而且这种失败是【静默的】—— 不会崩溃，只会给出错误的数据。

#include "Verify/HexVerify.h"
#include "HexSpireCore.h"
#include "Rng/HexRngStreams.h"

bool FHexVerifySuites::VerifyRng(FHexVerifyContext& Ctx)
{
	// ─────────────────────────────── 1. 可复现性：同 seed 必得同序列
	Ctx.Section(TEXT("可复现性（种子分享 / 回放 / bug 复现的前提）"));
	{
		FHexRngStreams A(12345);
		FHexRngStreams B(12345);

		bool bSame = true;
		for (int32 I = 0; I < 1000; ++I)
		{
			if (A.RandRange(EHexRngStream::Combat, 0, 1000000)
				!= B.RandRange(EHexRngStream::Combat, 0, 1000000))
			{
				bSame = false;
				break;
			}
		}
		Ctx.Check(TEXT("同 seed 的两个实例产生完全相同的序列（1000 次）"), bSame);

		// 不同 seed 必须产生不同序列
		FHexRngStreams C(54321);
		FHexRngStreams D(12345);
		bool bDifferent = false;
		for (int32 I = 0; I < 100; ++I)
		{
			if (C.RandRange(EHexRngStream::Combat, 0, 1000000)
				!= D.RandRange(EHexRngStream::Combat, 0, 1000000))
			{
				bDifferent = true;
				break;
			}
		}
		Ctx.Check(TEXT("不同 seed 产生不同序列"), bDifferent);
	}

	// ─────────────────────────────── 2. 分流独立性
	//
	// 这条保证"玩家在商店多点一次鼠标，不会改变下一场战斗的暴击结果"。
	// 若共用一条流，任何 UI 层的意外消费都会让整局漂移。
	Ctx.Section(TEXT("分流独立性"));
	{
		FHexRngStreams A(999);
		FHexRngStreams B(999);

		// A 大量消费 Loot 流
		for (int32 I = 0; I < 500; ++I)
		{
			A.RandFloat(EHexRngStream::Loot);
		}

		// Combat 流应完全不受影响
		bool bCombatUnaffected = true;
		for (int32 I = 0; I < 200; ++I)
		{
			if (A.RandRange(EHexRngStream::Combat, 0, 1000000)
				!= B.RandRange(EHexRngStream::Combat, 0, 1000000))
			{
				bCombatUnaffected = false;
				break;
			}
		}
		Ctx.Check(TEXT("消费 Loot 流不影响 Combat 流"), bCombatUnaffected);

		// 五条流的首个输出必须互不相同（说明派生有效）
		FHexRngStreams E(777);
		TSet<uint64> FirstOutputs;
		for (int32 S = 0; S < static_cast<int32>(EHexRngStream::Count); ++S)
		{
			FirstOutputs.Add(E.Stream(static_cast<EHexRngStream>(S)).NextUInt64());
		}
		Ctx.CheckEqual(TEXT("五条流的首个输出互不相同（seed 派生有效）"),
			FirstOutputs.Num(), static_cast<int32>(EHexRngStream::Count));
	}

	// ─────────────────────────────── 3. 均匀性（卡方检验的简化版）
	Ctx.Section(TEXT("均匀性"));
	{
		FHexRngStreams R(2024);
		const int32 Buckets = 10;
		const int32 Samples = 100000;
		TArray<int32> Counts;
		Counts.SetNumZeroed(Buckets);

		for (int32 I = 0; I < Samples; ++I)
		{
			const int32 V = R.RandRange(EHexRngStream::Combat, 0, Buckets - 1);
			if (V >= 0 && V < Buckets)
			{
				++Counts[V];
			}
		}

		const float ExpectedPerBucket = static_cast<float>(Samples) / Buckets;
		float MaxDeviation = 0.0f;
		for (int32 I = 0; I < Buckets; ++I)
		{
			const float Dev = FMath::Abs(Counts[I] - ExpectedPerBucket) / ExpectedPerBucket;
			MaxDeviation = FMath::Max(MaxDeviation, Dev);
		}
		// 10 万样本分 10 桶，偏差应远小于 5%
		Ctx.Check(TEXT("10 万次整数采样的桶偏差 < 5%"),
			MaxDeviation < 0.05f,
			FString::Printf(TEXT("最大偏差=%.2f%%"), MaxDeviation * 100.0f));

		// 浮点均值应接近 0.5
		double Sum = 0.0;
		for (int32 I = 0; I < Samples; ++I)
		{
			Sum += R.RandFloat(EHexRngStream::Deck);
		}
		const double Mean = Sum / Samples;
		Ctx.Check(TEXT("10 万次浮点采样均值接近 0.5"),
			FMath::Abs(Mean - 0.5) < 0.01,
			FString::Printf(TEXT("均值=%.5f"), Mean));
	}

	// ─────────────────────────────── 4. 边界正确性
	Ctx.Section(TEXT("边界正确性"));
	{
		FHexRngStreams R(31337);

		// NextRange 必须是闭区间 [Min, Max]
		bool bInBounds = true;
		bool bHitMin = false;
		bool bHitMax = false;
		for (int32 I = 0; I < 20000; ++I)
		{
			const int32 V = R.RandRange(EHexRngStream::Combat, 3, 7);
			if (V < 3 || V > 7)
			{
				bInBounds = false;
				break;
			}
			if (V == 3) bHitMin = true;
			if (V == 7) bHitMax = true;
		}
		Ctx.Check(TEXT("RandRange(3,7) 结果始终在 [3,7]"), bInBounds);
		Ctx.Check(TEXT("RandRange 能取到下界与上界（闭区间）"), bHitMin && bHitMax);

		// Min == Max 时必须返回该值
		Ctx.CheckEqual(TEXT("RandRange(5,5) 恒返回 5"),
			R.RandRange(EHexRngStream::Combat, 5, 5), 5);

		// Max < Min 时不得崩溃
		Ctx.CheckEqual(TEXT("RandRange(9,2) 退化返回下界，不崩溃"),
			R.RandRange(EHexRngStream::Combat, 9, 2), 9);

		// 浮点必须在 [0,1)
		bool bFloatOk = true;
		for (int32 I = 0; I < 20000; ++I)
		{
			const float V = R.RandFloat(EHexRngStream::Combat);
			if (V < 0.0f || V >= 1.0f)
			{
				bFloatOk = false;
				break;
			}
		}
		Ctx.Check(TEXT("RandFloat 结果始终在 [0,1)"), bFloatOk);

		// Chance 的两个极端
		Ctx.Check(TEXT("Chance(0) 恒 false"), !R.Chance(EHexRngStream::Combat, 0.0f));
		Ctx.Check(TEXT("Chance(1) 恒 true"), R.Chance(EHexRngStream::Combat, 1.0f));
	}

	// ─────────────────────────────── 5. 洗牌正确性（D2 的地基）
	Ctx.Section(TEXT("Fisher-Yates 洗牌"));
	{
		FHexRngStreams R(8888);

		// 洗牌必须是排列（不丢不重）
		TArray<int32> Deck;
		for (int32 I = 0; I < 11; ++I)
		{
			Deck.Add(I);
		}
		R.Shuffle(Deck, EHexRngStream::Deck);

		TSet<int32> Unique(Deck);
		Ctx.CheckEqual(TEXT("洗牌后元素个数不变"), Deck.Num(), 11);
		Ctx.CheckEqual(TEXT("洗牌后无重复无丢失（是一个排列）"), Unique.Num(), 11);

		// 洗牌必须真的打乱（连续 20 次不可能全部保持原序）
		int32 IdentityCount = 0;
		for (int32 Trial = 0; Trial < 20; ++Trial)
		{
			TArray<int32> D2;
			for (int32 I = 0; I < 11; ++I)
			{
				D2.Add(I);
			}
			R.Shuffle(D2, EHexRngStream::Deck);

			bool bIdentity = true;
			for (int32 I = 0; I < 11; ++I)
			{
				if (D2[I] != I)
				{
					bIdentity = false;
					break;
				}
			}
			if (bIdentity)
			{
				++IdentityCount;
			}
		}
		Ctx.Check(TEXT("20 次洗牌不会全部保持原序"), IdentityCount < 20,
			FString::Printf(TEXT("保持原序次数=%d"), IdentityCount));

		// 洗牌的位置分布应均匀：统计元素 0 落在各位置的次数
		{
			FHexRngStreams R2(4242);
			const int32 Trials = 22000;
			const int32 N = 11;
			TArray<int32> PosCounts;
			PosCounts.SetNumZeroed(N);

			for (int32 T = 0; T < Trials; ++T)
			{
				TArray<int32> D;
				for (int32 I = 0; I < N; ++I)
				{
					D.Add(I);
				}
				R2.Shuffle(D, EHexRngStream::Deck);
				PosCounts[D.IndexOfByKey(0)]++;
			}

			const float Expected = static_cast<float>(Trials) / N;
			float MaxDev = 0.0f;
			for (int32 I = 0; I < N; ++I)
			{
				MaxDev = FMath::Max(MaxDev, FMath::Abs(PosCounts[I] - Expected) / Expected);
			}
			// 这条能抓住"取模偏斜"这类静默 bug
			Ctx.Check(TEXT("洗牌位置分布均匀（偏差 < 8%），无取模偏斜"),
				MaxDev < 0.08f,
				FString::Printf(TEXT("最大偏差=%.2f%%"), MaxDev * 100.0f));
		}

		// 空数组与单元素数组不得崩溃
		{
			TArray<int32> Empty;
			R.Shuffle(Empty, EHexRngStream::Deck);
			Ctx.Check(TEXT("空数组洗牌不崩溃"), Empty.Num() == 0);

			TArray<int32> One = { 42 };
			R.Shuffle(One, EHexRngStream::Deck);
			Ctx.Check(TEXT("单元素洗牌不崩溃且值不变"), One.Num() == 1 && One[0] == 42);
		}
	}

	// ─────────────────────────────── 6. DrawCount：Undo 撤销屏障的依据
	Ctx.Section(TEXT("DrawCount 计数（Undo 撤销屏障）"));
	{
		FHexRngStreams R(1);
		const int32 Before = R.GetDrawCount();

		R.RandFloat(EHexRngStream::Combat);
		R.RandRange(EHexRngStream::Loot, 0, 10);
		R.Chance(EHexRngStream::Event, 0.5f);

		Ctx.CheckEqual(TEXT("三次消费后 DrawCount +3"), R.GetDrawCount() - Before, 3);

		// Chance 的短路分支不应消费 RNG
		const int32 Mid = R.GetDrawCount();
		R.Chance(EHexRngStream::Combat, 0.0f);
		R.Chance(EHexRngStream::Combat, 1.0f);
		Ctx.CheckEqual(TEXT("Chance(0)/Chance(1) 短路，不消费 RNG"),
			R.GetDrawCount() - Mid, 0);

		// 洗牌应按交换次数计数
		const int32 BeforeShuffle = R.GetDrawCount();
		TArray<int32> D;
		for (int32 I = 0; I < 11; ++I)
		{
			D.Add(I);
		}
		R.Shuffle(D, EHexRngStream::Deck);
		Ctx.CheckEqual(TEXT("洗 11 张牌消费 10 次 RNG"),
			R.GetDrawCount() - BeforeShuffle, 10);
	}

	// ─────────────────────────────── 7. 加权抽取
	Ctx.Section(TEXT("加权抽取（掉落池 / 稀有度）"));
	{
		FHexRngStreams R(555);

		// 权重 [1, 0, 0] 必须恒返回 0
		{
			TArray<float> W = { 1.0f, 0.0f, 0.0f };
			bool bAlwaysZero = true;
			for (int32 I = 0; I < 200; ++I)
			{
				if (R.WeightedPick(EHexRngStream::Loot, W) != 0)
				{
					bAlwaysZero = false;
					break;
				}
			}
			Ctx.Check(TEXT("权重 [1,0,0] 恒返回下标 0"), bAlwaysZero);
		}

		// 权重比例应体现在结果分布上
		{
			TArray<float> W = { 3.0f, 1.0f };
			int32 Count0 = 0;
			const int32 Trials = 40000;
			for (int32 I = 0; I < Trials; ++I)
			{
				if (R.WeightedPick(EHexRngStream::Loot, W) == 0)
				{
					++Count0;
				}
			}
			const float Ratio = static_cast<float>(Count0) / Trials;
			Ctx.Check(TEXT("权重 [3,1] 的命中比例接近 0.75"),
				FMath::Abs(Ratio - 0.75f) < 0.02f,
				FString::Printf(TEXT("实际比例=%.4f"), Ratio));
		}

		// 边界：空数组、全零权重不得崩溃
		{
			TArray<float> Empty;
			Ctx.CheckEqual(TEXT("空权重数组返回 0，不崩溃"),
				R.WeightedPick(EHexRngStream::Loot, Empty), 0);

			TArray<float> Zeros = { 0.0f, 0.0f, 0.0f };
			const int32 V = R.WeightedPick(EHexRngStream::Loot, Zeros);
			Ctx.Check(TEXT("全零权重返回合法下标，不崩溃"), V >= 0 && V < 3);
		}
	}

	// ─────────────────────────────── 8. 全零 seed 不得退化
	Ctx.Section(TEXT("退化种子防护"));
	{
		FHexRngStreams R(0);
		bool bNonZero = false;
		for (int32 I = 0; I < 100; ++I)
		{
			if (R.Stream(EHexRngStream::Combat).NextUInt64() != 0)
			{
				bNonZero = true;
				break;
			}
		}
		Ctx.Check(TEXT("seed=0 时不退化为全零输出"), bNonZero);
	}

	// ─────────────────────────────── 9. 序列化往返
	Ctx.Section(TEXT("序列化往返（存档 / 回放）"));
	{
		FHexRngStreams A(777);
		// 先消费一些，让状态偏离初始
		for (int32 I = 0; I < 50; ++I)
		{
			A.RandFloat(EHexRngStream::Combat);
			A.RandRange(EHexRngStream::Loot, 0, 100);
		}

		// 序列化
		TArray<uint8> Buffer;
		{
			FMemoryWriter Writer(Buffer);
			A.Serialize(Writer);
		}

		// 反序列化到 B
		FHexRngStreams B(1);
		{
			FMemoryReader Reader(Buffer);
			B.Serialize(Reader);
		}

		Ctx.CheckEqual(TEXT("反序列化后 DrawCount 一致"),
			B.GetDrawCount(), A.GetDrawCount());
		Ctx.Check(TEXT("反序列化后 MasterSeed 一致"),
			B.GetMasterSeed() == A.GetMasterSeed());

		// 后续序列必须完全一致
		bool bSame = true;
		for (int32 I = 0; I < 500; ++I)
		{
			if (A.RandRange(EHexRngStream::Combat, 0, 1000000)
				!= B.RandRange(EHexRngStream::Combat, 0, 1000000))
			{
				bSame = false;
				break;
			}
		}
		Ctx.Check(TEXT("反序列化后续序列与原实例逐位一致"), bSame);
	}

	return Ctx.NumFailed() == 0;
}
