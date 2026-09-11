// Copyright Hex Spire. All Rights Reserved.
//
// 把代码内建的卡池导出成 CSV —— 配表的起点
//
// 用法：
//   UnrealEditor-Cmd.exe <uproject> -run=HexExportCards
//   UnrealEditor-Cmd.exe <uproject> -run=HexExportCards -out="D:\cards.csv"
//
// 默认输出到 Saved/Export/Cards.csv。
//
// ══════════════════════════════════════════════════════════════════
// 为什么要导出而不是让策划从空表开始填
// ══════════════════════════════════════════════════════════════════
// 那 12 张卡的数值是 Godot 版实测调过的（每条都带"为什么是这个数"
// 的注释），费效基准线、连刺的取整损失、防御的 4.2 倍过剩……
// 从空表开始填等于把这些经验全部丢掉重来。
//
// 导出后策划拿到的是一张"已经平衡过"的表，改动有参照物。
//
// ⚠️ 导出的 CSV 里【没有美术字段的内容】（Visual 那一组为空）——
//    那些是美术要填的，代码内建里本来就不存在。
//    导入后在编辑器里逐张挂贴图即可。

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "HexExportCardsCommandlet.generated.h"

UCLASS()
class HEXSPIRE_API UHexExportCardsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UHexExportCardsCommandlet();

	/** @return 0 = 成功；1 = 写文件失败 */
	virtual int32 Main(const FString& Params) override;
};
