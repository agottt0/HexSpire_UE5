// Copyright Hex Spire. All Rights Reserved.

#include "Verify/HexVerify.h"
#include "HexSpireCore.h"
#include "Hex/HexCoord.h"

FHexVerifyContext::FHexVerifyContext(const FString& InSuiteName)
	: SuiteName(InSuiteName)
{
	UE_LOG(LogHexSpireCore, Display, TEXT("═══ 验证套件: %s ═══"), *SuiteName);
}

void FHexVerifyContext::Check(const FString& Name, bool bCondition, const FString& Detail)
{
	FHexVerifyResult R;
	R.Name = Name;
	R.bPassed = bCondition;
	R.Detail = Detail;
	Results.Add(R);

	if (bCondition)
	{
		UE_LOG(LogHexSpireCore, Display, TEXT("  [通过] %s"), *Name);
	}
	else
	{
		UE_LOG(LogHexSpireCore, Error, TEXT("  [失败] %s :: %s"), *Name, *Detail);
	}
}

void FHexVerifyContext::CheckEqual(const FString& Name, int32 Actual, int32 Expected)
{
	Check(Name, Actual == Expected,
		FString::Printf(TEXT("实际=%d 期望=%d"), Actual, Expected));
}

void FHexVerifyContext::CheckNearlyEqual(const FString& Name, float Actual, float Expected, float Tolerance)
{
	Check(Name, FMath::Abs(Actual - Expected) <= Tolerance,
		FString::Printf(TEXT("实际=%.6f 期望=%.6f 容差=%.6f"), Actual, Expected, Tolerance));
}

void FHexVerifyContext::CheckEqualCoord(const FString& Name, const FIntVector& Actual, const FIntVector& Expected)
{
	Check(Name, Actual == Expected,
		FString::Printf(TEXT("实际=%s cube(%d,%d,%d) 期望=%s cube(%d,%d,%d)"),
			*FHexCoord::ToOffsetString(Actual), Actual.X, Actual.Y, Actual.Z,
			*FHexCoord::ToOffsetString(Expected), Expected.X, Expected.Y, Expected.Z));
}

void FHexVerifyContext::Fail(const FString& Name, const FString& Detail)
{
	Check(Name, false, Detail);
}

void FHexVerifyContext::Section(const FString& Title)
{
	UE_LOG(LogHexSpireCore, Display, TEXT("── %s"), *Title);
}

int32 FHexVerifyContext::NumPassed() const
{
	int32 N = 0;
	for (const FHexVerifyResult& R : Results)
	{
		if (R.bPassed)
		{
			++N;
		}
	}
	return N;
}

int32 FHexVerifyContext::NumFailed() const
{
	return Results.Num() - NumPassed();
}

bool FHexVerifyContext::Summarize() const
{
	const int32 Passed = NumPassed();
	const int32 Failed = NumFailed();

	if (Failed == 0)
	{
		UE_LOG(LogHexSpireCore, Display,
			TEXT("═══ %s: 全部通过 (%d/%d) ═══"), *SuiteName, Passed, Results.Num());
		return true;
	}

	UE_LOG(LogHexSpireCore, Error,
		TEXT("═══ %s: 失败 %d 项 / 共 %d 项 ═══"), *SuiteName, Failed, Results.Num());
	for (const FHexVerifyResult& R : Results)
	{
		if (!R.bPassed)
		{
			UE_LOG(LogHexSpireCore, Error, TEXT("    ✗ %s :: %s"), *R.Name, *R.Detail);
		}
	}
	return false;
}

// ───────────────────────────────────────────────────────── 调度

bool FHexVerifySuites::RunSuite(const FString& SuiteName)
{
	const FString Lower = SuiteName.ToLower();

	if (Lower == TEXT("all") || Lower.IsEmpty())
	{
		return RunAll();
	}

	if (Lower == TEXT("hex"))
	{
		FHexVerifyContext Ctx(TEXT("HexCoord"));
		VerifyHex(Ctx);
		return Ctx.Summarize();
	}
	if (Lower == TEXT("footprint"))
	{
		FHexVerifyContext Ctx(TEXT("HexFootprint"));
		VerifyFootprint(Ctx);
		return Ctx.Summarize();
	}
	if (Lower == TEXT("pathfinding") || Lower == TEXT("path"))
	{
		FHexVerifyContext Ctx(TEXT("HexPathfinder"));
		VerifyPathfinding(Ctx);
		return Ctx.Summarize();
	}
	if (Lower == TEXT("rng"))
	{
		FHexVerifyContext Ctx(TEXT("RngStreams"));
		VerifyRng(Ctx);
		return Ctx.Summarize();
	}
	if (Lower == TEXT("content"))
	{
		FHexVerifyContext Ctx(TEXT("ContentLibrary"));
		VerifyContent(Ctx);
		return Ctx.Summarize();
	}
	if (Lower == TEXT("damage") || Lower == TEXT("dmg"))
	{
		FHexVerifyContext Ctx(TEXT("DamagePipeline"));
		VerifyDamage(Ctx);
		return Ctx.Summarize();
	}
	if (Lower == TEXT("equip"))
	{
		FHexVerifyContext Ctx(TEXT("Equipment"));
		VerifyEquip(Ctx);
		return Ctx.Summarize();
	}
	if (Lower == TEXT("map") || Lower == TEXT("run"))
	{
		FHexVerifyContext Ctx(TEXT("FloorMap"));
		VerifyMap(Ctx);
		return Ctx.Summarize();
	}
	if (Lower == TEXT("status"))
	{
		FHexVerifyContext Ctx(TEXT("StatusEffects"));
		VerifyStatus(Ctx);
		return Ctx.Summarize();
	}
	if (Lower == TEXT("deck") || Lower == TEXT("piles"))
	{
		FHexVerifyContext Ctx(TEXT("PileManager"));
		VerifyDeck(Ctx);
		return Ctx.Summarize();
	}
	if (Lower == TEXT("rules") || Lower == TEXT("rulebook"))
	{
		FHexVerifyContext Ctx(TEXT("RuleBook"));
		VerifyRules(Ctx);
		return Ctx.Summarize();
	}
	if (Lower == TEXT("trigger") || Lower == TEXT("bus"))
	{
		FHexVerifyContext Ctx(TEXT("TriggerBus"));
		VerifyTrigger(Ctx);
		return Ctx.Summarize();
	}
	if (Lower == TEXT("ai") || Lower == TEXT("enemy"))
	{
		FHexVerifyContext Ctx(TEXT("EnemyAI"));
		VerifyAI(Ctx);
		return Ctx.Summarize();
	}
	if (Lower == TEXT("battle") || Lower == TEXT("flow"))
	{
		FHexVerifyContext Ctx(TEXT("BattleFlow"));
		VerifyBattle(Ctx);
		return Ctx.Summarize();
	}

	UE_LOG(LogHexSpireCore, Error, TEXT("未知验证套件: %s"), *SuiteName);
	return false;
}

bool FHexVerifySuites::RunAll()
{
	bool bAllOk = true;

	{
		FHexVerifyContext Ctx(TEXT("HexCoord"));
		VerifyHex(Ctx);
		bAllOk &= Ctx.Summarize();
	}
	{
		FHexVerifyContext Ctx(TEXT("HexFootprint"));
		VerifyFootprint(Ctx);
		bAllOk &= Ctx.Summarize();
	}
	{
		FHexVerifyContext Ctx(TEXT("HexPathfinder"));
		VerifyPathfinding(Ctx);
		bAllOk &= Ctx.Summarize();
	}
	{
		FHexVerifyContext Ctx(TEXT("RngStreams"));
		VerifyRng(Ctx);
		bAllOk &= Ctx.Summarize();
	}
	{
		FHexVerifyContext Ctx(TEXT("ContentLibrary"));
		VerifyContent(Ctx);
		bAllOk &= Ctx.Summarize();
	}
	{
		FHexVerifyContext Ctx(TEXT("DamagePipeline"));
		VerifyDamage(Ctx);
		bAllOk &= Ctx.Summarize();
	}
	{
		FHexVerifyContext Ctx(TEXT("Equipment"));
		VerifyEquip(Ctx);
		bAllOk &= Ctx.Summarize();
	}
	{
		FHexVerifyContext Ctx(TEXT("FloorMap"));
		VerifyMap(Ctx);
		bAllOk &= Ctx.Summarize();
	}
	{
		FHexVerifyContext Ctx(TEXT("StatusEffects"));
		VerifyStatus(Ctx);
		bAllOk &= Ctx.Summarize();
	}
	{
		FHexVerifyContext Ctx(TEXT("PileManager"));
		VerifyDeck(Ctx);
		bAllOk &= Ctx.Summarize();
	}
	{
		FHexVerifyContext Ctx(TEXT("RuleBook"));
		VerifyRules(Ctx);
		bAllOk &= Ctx.Summarize();
	}
	{
		FHexVerifyContext Ctx(TEXT("TriggerBus"));
		VerifyTrigger(Ctx);
		bAllOk &= Ctx.Summarize();
	}
	{
		FHexVerifyContext Ctx(TEXT("EnemyAI"));
		VerifyAI(Ctx);
		bAllOk &= Ctx.Summarize();
	}
	// ⚠️ BattleFlow 放最后：它是集成测试，依赖前面所有模块。
	//    前面某块坏了，这里会以难懂的方式失败 —— 先看前面的结论。
	{
		FHexVerifyContext Ctx(TEXT("BattleFlow"));
		VerifyBattle(Ctx);
		bAllOk &= Ctx.Summarize();
	}

	if (bAllOk)
	{
		UE_LOG(LogHexSpireCore, Display, TEXT("★★★ 全部验证套件通过 ★★★"));
	}
	else
	{
		UE_LOG(LogHexSpireCore, Error, TEXT("✗✗✗ 存在失败的验证套件 ✗✗✗"));
	}
	return bAllOk;
}
