// Copyright Hex Spire. All Rights Reserved.

#include "Commandlets/HexVerifyCommandlet.h"
#include "HexSpire.h"
#include "Verify/HexVerify.h"

UHexVerifyCommandlet::UHexVerifyCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UHexVerifyCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamsMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamsMap);

	FString SuiteName = TEXT("all");
	if (const FString* Found = ParamsMap.Find(TEXT("suite")))
	{
		SuiteName = *Found;
	}

	UE_LOG(LogHexSpire, Display, TEXT(""));
	UE_LOG(LogHexSpire, Display, TEXT("╔══════════════════════════════════════════════╗"));
	UE_LOG(LogHexSpire, Display, TEXT("║   Hex Spire 逻辑层验证 (suite=%-15s) ║"), *SuiteName);
	UE_LOG(LogHexSpire, Display, TEXT("╚══════════════════════════════════════════════╝"));
	UE_LOG(LogHexSpire, Display, TEXT(""));

	const double StartTime = FPlatformTime::Seconds();
	const bool bOk = FHexVerifySuites::RunSuite(SuiteName);
	const double Elapsed = FPlatformTime::Seconds() - StartTime;

	UE_LOG(LogHexSpire, Display, TEXT("耗时 %.3f 秒"), Elapsed);

	if (bOk)
	{
		UE_LOG(LogHexSpire, Display, TEXT("HEXSPIRE_VERIFY_RESULT: PASS"));
		return 0;
	}

	UE_LOG(LogHexSpire, Error, TEXT("HEXSPIRE_VERIFY_RESULT: FAIL"));
	return 1;
}
