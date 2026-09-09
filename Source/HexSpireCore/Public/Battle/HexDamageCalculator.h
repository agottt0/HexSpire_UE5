// Copyright Hex Spire. All Rights Reserved.
//
// §4.4 伤害管线的【唯一实现】。禁止在别处算伤害。
//
// ══════════════════════════════════════════════════════════════════
// 管线阶段（⚠️ 顺序有讲究，读完注释再改）
// ══════════════════════════════════════════════════════════════════
//   ①  基础     Flat + Stats[StatRef] × Ratio          （§7.5 强制系数化）
//   ②  平坦加成  非符文来源（状态力量、装备 base），可求和
//   ②′ 顺序钩子  符文槽 1→6 依次 += 或 *=              （§6.5 的核心）
//   ③  乘区     非符文乘区 × Π(1+x)（易伤/虚弱/背击），可求积
//   ④  暴击     × (1 + CritBaseMult + LUK×LukToCritDmg)
//   ⑤  减伤     × (1 - DEF/(DEF+DefSoftcap))
//   ⑤′ 取整下限  Max(Floor(v), MinDamage)  → 此后全 int
//   ⑥  扣护盾    先扣 Barrier（独立吸收池）
//   ⑦  扣格挡    再扣 Block，溢出打 HP
//
// ── 为什么有 ②′ ─────────────────────────────────────────────
// §4.4 要求"加法全做完再乘"，但 §6.5 要求 [锐化,倍化] ≠ [倍化,锐化]。
// 桶式聚合（先求和所有加区再求积所有乘区）会让顺序失效，D6 的免费深度归零。
// 所以在 ② 与 ③ 之间开一个【顺序敏感】阶段，符文在这里按槽位依次作用于
// running value。ATK=10、base=10 时：
//    [锐化,倍化] = (10+5)×1.4 = 21
//    [倍化,锐化] = 10×1.4+5   = 19
// 这个差异必须在 UI 上肉眼可见 —— 它是 §6.5"免费深度"的最小可证形态。
//
// ── 为什么 ⑤′ 在 ⑥⑦ 之前（已确认偏离策划案字面顺序）─────────────
// §4.4 原文把下限写在扣格挡【之后】。按字面实现会导致：
//   Block=999 时仍然掉 MinDamage 血 → 格挡永远无法完全吸收一次攻击
//   → 镇妖者的「格挡不清空」被动失去意义，"堆防御"不再是一条真路线。
// "保证永远能破防"针对的是 DEF 软曲线（⑤），不是 Block（⑦）。
//
// ── 为什么分 Calculate / Preview 两个入口 ────────────────────
// §13.2 要求悬停显示"预计伤害 X（暴击 Y）"。若预览走同一函数就会消耗 RNG，
// 导致"鼠标划过战场"污染确定性，并连带打爆 Undo 的撤销屏障。
// 所以 Preview() 【不接受 rng 参数】，返回 [不暴, 暴] 区间。

#pragma once

#include "CoreMinimal.h"
#include "Core/HexSpireEnums.h"

class FHexUnit;
class FHexRngStreams;

/** ②′ 顺序钩子的单步记录，供符文实验室 UI 与调试用 */
struct HEXSPIRECORE_API FHexRuneStep
{
	FString SourceTag;
	int32 SlotOrder = 0;
	float Before = 0.0f;
	float After = 0.0f;
};

/**
 * ②′ 顺序钩子接口。
 * 由 TriggerBus 构建：把监听 ON_ATTACK 的符文按槽位 1→6 串成链。
 */
using FHexValueHook = TFunction<float(float /*Running*/, TArray<FHexRuneStep>& /*Log*/)>;

/** 伤害计算的输入。纯数据，便于 Preview 复用。 */
struct HEXSPIRECORE_API FHexDamageContext
{
	const FHexUnit* Source = nullptr;
	FHexUnit* Target = nullptr;

	// ① 系数化参数（§7.5）
	float Flat = 0.0f;
	FName StatRef = TEXT("ATK");
	float StatRatio = 0.0f;

	/** ② 非符文平坦加成（状态力量等）。留空则由 Calculate 自动从 Source 状态取。 */
	float FlatBonus = 0.0f;

	/** ③ 非符文乘区（每项是 x，最终 ×Π(1+x)）。自动追加状态与背击。 */
	TArray<float> Multipliers;

	/** 是否来自背击（§8.2.3：加成 + 无法闪避） */
	bool bFromRear = false;

	/** 忽略部分 DEF（如奥术飞弹无视 1 点 DEF） */
	int32 DefIgnore = 0;

	/** 无视格挡（中毒 tick 用） */
	bool bIgnoreBlock = false;

	/** 完全跳过闪避判定（状态 tick 伤害、地形危害不该被闪避） */
	bool bCannotBeDodged = false;

	/** 标签，供符文条件过滤 */
	TArray<FName> Tags;

	/** 来源描述，写进事件日志 */
	FString Tag;
};

/** 伤害计算结果 */
struct HEXSPIRECORE_API FHexDamageResult
{
	/** ⑤′ 取整前的值，仅供调试 */
	float Raw = 0.0f;
	int32 Final = 0;
	int32 ToBarrier = 0;
	int32 ToBlock = 0;
	int32 ToHP = 0;
	bool bIsCrit = false;
	bool bIsDodged = false;
	TArray<FHexRuneStep> RuneLog;
};

struct HEXSPIRECORE_API FHexDamageCalculator
{
	/** ①+② —— 不含符文、不含暴击、不含减伤的"账面基数" */
	static float BaseValue(const FHexDamageContext& Ctx);

	/** ④ 暴击倍率 */
	static float CritMultiplier(const FHexUnit* Source, float ExtraCritMult = 1.0f);

	/**
	 * 闪避率（§4.3：AGI 的次要作用）。背击无法被闪避（§8.2.3）。
	 * ⚠️ 递减曲线，不是线性 —— 见 HexK::AgiToDodge 注释。
	 */
	static float DodgeChance(const FHexDamageContext& Ctx);

	/**
	 * 正式计算。会消耗 RNG（暴击、闪避判定）。
	 *
	 * @param RuneHook ②′ 的顺序钩子，可为空
	 * @param GlobalDamageMult 来自 RuleBook 的 DamageMultiplier（符文乘区）
	 * @param GlobalCritMult   来自 RuleBook 的 CritDamageMultiplier
	 */
	static FHexDamageResult Calculate(
		const FHexDamageContext& Ctx,
		FHexRngStreams* Rng,
		const FHexValueHook& RuneHook = FHexValueHook(),
		float GlobalDamageMult = 1.0f,
		float GlobalCritMult = 1.0f);

	/**
	 * 预览：返回 (不暴击伤害, 暴击伤害)。
	 * ⚠️ 【不消耗 RNG】—— 悬停鼠标不得影响确定性。
	 */
	static FIntPoint Preview(
		const FHexDamageContext& Ctx,
		const FHexValueHook& RuneHook = FHexValueHook(),
		float GlobalDamageMult = 1.0f,
		float GlobalCritMult = 1.0f);

	/**
	 * 格挡获取量（§4.4：GainBlock 也走系数化）。
	 * @param BlockMult 来自 RuleBook 的 BlockMultiplier
	 */
	static int32 CalculateBlock(
		const FHexUnit* Source,
		float Flat,
		FName StatRef,
		float Ratio,
		float BlockMult = 1.0f);
};
