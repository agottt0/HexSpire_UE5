// Copyright Hex Spire. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

HEXSPIRECORE_API DECLARE_LOG_CATEGORY_EXTERN(LogHexSpireCore, Log, All);

class FHexSpireCoreModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
