// Copyright Hex Spire. All Rights Reserved.
//
// 目标解析 —— 策划案 §8.5
//
// TargetSpec → 合法目标格集合 / 实际波及格集合 / 去重后的单位集合
//
// ⚠️ 两个关键点：
//   ① 射程从【最近的己方 footprint 格】起算（§8.2.2 机制点 3）
//      —— 这是大体型"有效射程更长"的来源。
//   ② 多格单位：任意一格被范围覆盖即算命中，但【每次攻击只结算 1 次伤害】
//      （§8.2.2 机制点 2）。不因占多格而多次受伤，否则 L 型 Boss 会被 AOE 秒杀。

#pragma once

#include "CoreMinimal.h"
#include "Core/HexSpireEnums.h"
#include "Battle/HexCardData.h"

class FHexBattleState;
class FHexUnit;

struct HEXSPIRECORE_API FHexTargetResolver
{
	/**
	 * 合法目标格集合（UI 高亮"我能点哪"）。
	 * 结果按 (row, col) 升序，确定性。
	 */
	static void LegalCells(
		const FHexBattleState& State,
		const FHexUnit& Caster,
		const FHexTargetSpec& Spec,
		TArray<FIntVector>& Out);

	/**
	 * 给定一个已选目标格，返回实际波及的全部格
	 * （UI 高亮"会打到哪"，含多格单位的完整占位）。
	 */
	static void AffectedCells(
		const FHexBattleState& State,
		const FHexUnit& Caster,
		const FHexTargetSpec& Spec,
		const FIntVector& TargetCell,
		TArray<FIntVector>& Out);

	/**
	 * 波及格内的单位 id（已去重，按 id 升序）。
	 * ⚠️ 去重是 §8.2.2 机制点 2 的实现：多格单位只结算一次。
	 */
	static void AffectedUnits(
		const FHexBattleState& State,
		const FHexUnit& Caster,
		const FHexTargetSpec& Spec,
		const FIntVector& TargetCell,
		EHexTargetFilter Filter,
		TArray<int32>& Out);

	/** 目标格是否合法 */
	static bool IsLegalTarget(
		const FHexBattleState& State,
		const FHexUnit& Caster,
		const FHexTargetSpec& Spec,
		const FIntVector& TargetCell);

	/** 从施法者到目标格的方向索引（用于 LINE / CONE） */
	static int32 DirectionTo(const FHexUnit& Caster, const FIntVector& TargetCell);
};
