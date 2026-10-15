// Copyright Hex Spire. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * 导出 commandlet 共用的 CSV 序列化助手。
 *
 * ⚠️ 为什么放在具名命名空间而不是各 .cpp 的匿名命名空间里：
 *    这几个函数原本在 HexExportCards / HexExportUnits 各自的匿名
 *    命名空间里各写了一份。UE 默认开 unity（jumbo）build，会把多个
 *    .cpp 拼进同一个 Module.HexSpire.cpp 再编译；而 C++ 里同一 TU 中的
 *    多个匿名命名空间【指向同一个命名空间】，于是重复定义成了一条编译错误，
 *    整个 HexSpire 模块编不过：
 *        error C2995: EnumName 函数模板已经定义
 *        error C2084: Csv 已有主体
 *    任何"看起来人畜无害"的匿名命名空间助手，在 jumbo build 下都是
 *    全模块的重名池。共用助手一律走具名命名空间。
 *    （同类坑：HexUnitAppearance.h 顶部记录的 inline const FName 撞 C2374。）
 */
namespace HexCsvExport
{
	/**
	 * 取枚举值的【标识名】（如 "Attack"），而不是 DisplayName（"攻击"）。
	 *
	 * ⚠️ 必须用标识名。DataTable 导入 CSV 时按枚举的标识名匹配，
	 *    写中文 DisplayName 会导入失败并静默填成枚举的第 0 项 ——
	 *    于是所有卡都变成"攻击"类型、所有敌人都是 Aggressive，而且不报错。
	 */
	template <typename TEnum>
	FString EnumName(TEnum Value)
	{
		const UEnum* E = StaticEnum<TEnum>();
		if (!E)
		{
			return FString::FromInt(static_cast<int32>(Value));
		}
		// GetNameStringByValue 返回不带作用域前缀的短名
		return E->GetNameStringByValue(static_cast<int64>(Value));
	}

	/**
	 * CSV 字段转义。
	 *
	 * ⚠️ 描述模板与图鉴文本里既有中文逗号也有半角逗号
	 *    （"造成 {dmg} 点伤害，并击退 {kb} 格"），不转义会让那一行的列数
	 *    错位 —— 后面所有字段整体左移，而 DataTable 导入时只会报一条
	 *    模糊的行解析警告。
	 */
	inline FString Csv(const FString& In)
	{
		if (!In.Contains(TEXT(",")) && !In.Contains(TEXT("\"")) && !In.Contains(TEXT("\n")))
		{
			return In;
		}
		FString Out = In.Replace(TEXT("\""), TEXT("\"\""));
		return FString::Printf(TEXT("\"%s\""), *Out);
	}

	/**
	 * FName 数组 → UE 数组字面量：("a","b","c")，空数组 → 空串。
	 *
	 * ⚠️ 不能用 "a|b|c" 这类自造分隔符。FArrayProperty::ImportText
	 *    要求单元格以 '(' 开头（空串除外，空串视为空数组），否则整格
	 *    解析失败 —— 该列静默变成空数组，只在导入日志里留一条含糊的
	 *    Problem 警告。DT_Cards 首次建表时 Tags 就是这么全丢的；
	 *    CornerstoneCardIds 丢了的症状是"角色走不了路"。
	 *    字面量本身含逗号，所以整格必须再过一遍 Csv()。
	 */
	inline FString NamesLiteral(const TArray<FName>& In)
	{
		if (In.Num() == 0)
		{
			return FString();
		}
		TArray<FString> Parts;
		Parts.Reserve(In.Num());
		for (const FName& N : In)
		{
			Parts.Add(FString::Printf(TEXT("\"%s\""), *N.ToString()));
		}
		return FString::Printf(TEXT("(%s)"), *FString::Join(Parts, TEXT(",")));
	}
}
