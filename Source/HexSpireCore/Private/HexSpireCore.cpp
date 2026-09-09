// Copyright Hex Spire. All Rights Reserved.

#include "HexSpireCore.h"

DEFINE_LOG_CATEGORY(LogHexSpireCore);

void FHexSpireCoreModule::StartupModule()
{
	UE_LOG(LogHexSpireCore, Log, TEXT("HexSpireCore module started."));
}

void FHexSpireCoreModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FHexSpireCoreModule, HexSpireCore);
