// Copyright Hex Spire. All Rights Reserved.
//
// 符文表的查询入口。
//
// 为什么与 HexContentLibrary 分开：
//   符文是 D6 的核心系统，目标 90–120 个（策划案 §6.1）。
//   放在内容库里会让那个文件膨胀到无法审阅。
//   FHexContentLibrary 的 FindRune/AllRunes/GetRuneIdsByRarity 转发到这里。

#pragma once

#include "CoreMinimal.h"
#include "Core/HexSpireEnums.h"
#include "Runes/HexRuneData.h"

struct HEXSPIRECORE_API FHexRuneLibrary
{
	/** 全部符文（第一版 17 个）。函数内静态，首次调用构造一次。 */
	static const TArray<FHexRuneData>& AllRunes();

	static const FHexRuneData* FindRune(FName Id);

	/**
	 * 配表覆写：同 Id 整条替换，新 Id 追加（语义与 OverrideCard 一致）。
	 *
	 * ⚠️ 【必须在任何 RuneLoadout 建立之前调用】（启动期，与其他
	 *    loader 同一时机）。FHexRuneLoadout 与 TriggerBus 持有的都是
	 *    指进本库数组的裸指针 —— 数组在 Add 时可能整体搬家，
	 *    局中覆写会让已装备符文的指针全部悬空，崩在下一次触发。
	 *
	 * @return true = 覆写了已有符文；false = 追加了新符文
	 */
	static bool OverrideRune(const FHexRuneData& Rune);

	/** 还原为代码内建（测试与验证器用） */
	static void ResetRuneOverrides();

	/** 按稀有度筛选（三选一抽取用） */
	static void GetIdsByRarity(EHexRarity Rarity, TArray<FName>& Out);

	/**
	 * 按类别计数 —— 供验证器校验 §6.4 的类别占比。
	 *
	 * ⚠️ 这不是"顺手加的工具函数"：§6.4 的占比（规则改写 35% / 触发器 30% /
	 *    条件 15% / 乘区 ≤8% / 诅咒 12%）是设计约束，扩到 90 个符文时
	 *    极容易不知不觉写歪（人总是倾向写更好写的乘区）。
	 *    VerifyRunes 会用这个函数把占比钉住。
	 */
	static int32 CountByCategory(EHexRuneCategory Category);
};
