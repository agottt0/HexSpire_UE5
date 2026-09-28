// Copyright Hex Spire. All Rights Reserved.
//
// 符文配表加载器 —— 找表、校验、逐条覆写进 FHexRuneLibrary
//
// 与 FHexUnitTableLoader 同一套模式（长注释见那边）：
//   · 表不存在不是错误（还没开始配表就全走代码内建）
//   · 行结构必须校验，选错时 FindRow 返回按错误布局解读的垃圾
//   · 表用 TStrongObjectPtr 持有，防 GC
//
// ⚠️ 时机约束（比单位表严格）：必须在【任何 RunState 建立之前】应用。
//    RuneLoadout 持有指进符文库数组的裸指针，数组在覆写时可能搬家。
//    见 FHexRuneLibrary::OverrideRune 的注释。

#pragma once

#include "CoreMinimal.h"

class UDataTable;

class HEXSPIRE_API FHexRuneTableLoader
{
public:
	/** 默认表路径 */
	static const TCHAR* DefaultTablePath();

	/**
	 * 应用一张符文配表。
	 * @return 应用的行数（覆写 + 新增）
	 */
	static int32 Apply(const UDataTable* Table);

	/** 加载默认路径的表并应用。表不存在时静默返回 0。 */
	static int32 ApplyDefaults();
};
