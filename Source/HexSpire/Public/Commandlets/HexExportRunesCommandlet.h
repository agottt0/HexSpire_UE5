// Copyright Hex Spire. All Rights Reserved.
//
// 把代码内建的 31 个符文导出成 CSV —— 符文配表的起点
//
// 用法：
//   UnrealEditor-Cmd.exe <uproject> -run=HexExportRunes
//   UnrealEditor-Cmd.exe <uproject> -run=HexExportRunes -out="D:\out\Runes.csv"
//
// 默认输出到 Saved/Export/Runes.csv。
//
// 为什么导出而不是从空表开始填：31 个符文的触发器、限次、数值钩子
// 都是配着 TriggerBus 的语义写的（VerifyRunes 有端到端断言钉着），
// 从空表填会把这些语义细节全丢掉重来。
//
// ⚠️ 目标 90–120 个符文（§6.1），后期扩量主要在 DT_Runes 里加行 ——
//    这份导出让表从第一天起就有 31 行可参照的范例。

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "HexExportRunesCommandlet.generated.h"

UCLASS()
class HEXSPIRE_API UHexExportRunesCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UHexExportRunesCommandlet();

	/** @return 0 = 成功；1 = 写文件失败 */
	virtual int32 Main(const FString& Params) override;
};
