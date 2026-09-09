// Copyright Hex Spire. All Rights Reserved.
//
// 验证器的命令行入口 —— headless 运行，退出码即结论。
//
// 用法：
//   UnrealEditor-Cmd.exe <uproject> -run=HexSpireVerify
//   UnrealEditor-Cmd.exe <uproject> -run=HexSpireVerify -suite=hex
//
// ⚠️ Commandlet 需要 UObject 反射，所以放在 HexSpire 模块（依赖 Engine），
//    而不是纯逻辑的 HexSpireCore。被验证的逻辑本身仍全在 Core 里。

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "HexVerifyCommandlet.generated.h"

UCLASS()
class HEXSPIRE_API UHexVerifyCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UHexVerifyCommandlet();

	/** @return 0 = 全部通过；1 = 存在失败 */
	virtual int32 Main(const FString& Params) override;
};
