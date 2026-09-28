// Copyright Hex Spire. All Rights Reserved.
//
// 把代码内建的英雄与敌人导出成 CSV —— 配表的起点
//
// 用法：
//   UnrealEditor-Cmd.exe <uproject> -run=HexExportUnits
//   UnrealEditor-Cmd.exe <uproject> -run=HexExportUnits -outdir="D:\out"
//
// 默认输出到 Saved/Export/Heroes.csv 与 Saved/Export/Enemies.csv。
//
// ══════════════════════════════════════════════════════════════════
// 为什么要导出而不是从空表开始填
// ══════════════════════════════════════════════════════════════════
// 与卡牌导出同一个理由（见 HexExportCardsCommandlet.h）：
// 四只敌人的数值是 Godot 版实测调过的，镇妖者的抽牌数 3 更是
// 基石卡机制的必要配套（填 5 会让卡组每回合被抽光）。
// 从空表开始填等于把这些推导全丢掉重来。
//
// ⚠️ 导出的 CSV 里【没有资产引用】—— 那些在 DA_UnitVisual_* 里，
//    代码内建的数值结构里本来就不存在（分层见 HexHeroTableRow.h 顶部）。

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "HexExportUnitsCommandlet.generated.h"

UCLASS()
class HEXSPIRE_API UHexExportUnitsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UHexExportUnitsCommandlet();

	/** @return 0 = 成功；1 = 写文件失败 */
	virtual int32 Main(const FString& Params) override;
};
