// Copyright Hex Spire. All Rights Reserved.

#include "Battle/HexDamageCalculator.h"
#include "Battle/HexUnit.h"
#include "Rng/HexRngStreams.h"
#include "Core/HexSpireConstants.h"

namespace
{
	int32 DamageStatOf(const FHexUnit* U, FName Ref)
	{
		if (!U)
		{
			return 0;
		}
		static const FName NAtk(TEXT("ATK"));
		static const FName NDef(TEXT("DEF"));
		static const FName NAgi(TEXT("AGI"));
		static const FName NLuk(TEXT("LUK"));
		static const FName NCrit(TEXT("CRIT"));
		static const FName NHp(TEXT("HP"));
		static const FName NHpMax(TEXT("HP_MAX"));

		if (Ref == NAtk)   return U->ATK;
		if (Ref == NDef)   return U->DEF;
		if (Ref == NAgi)   return U->AGI;
		if (Ref == NLuk)   return U->LUK;
		if (Ref == NCrit)  return U->CRIT;
		if (Ref == NHp)    return U->HP;
		if (Ref == NHpMax) return U->HPMax;
		return 0;
	}

	/** ③ 非符文乘区：状态（虚弱/易伤）+ 背击 + 全局符文乘区 */
	float ApplyMultipliers(float V, const FHexDamageContext& Ctx, float GlobalDamageMult)
	{
		float Out = V;

		for (const float M : Ctx.Multipliers)
		{
			Out *= (1.0f + M);
		}

		// 攻击者的"造成伤害"乘区（虚弱为负）
		if (Ctx.Source)
		{
			Out *= (1.0f + Ctx.Source->GetDamageDealtMultiplier());
		}

		// 受击者的"受到伤害"乘区（易伤为正）
		if (Ctx.Target)
		{
			Out *= (1.0f + Ctx.Target->GetDamageTakenMultiplier());
		}

		// 背击（§8.2.3）
		if (Ctx.bFromRear)
		{
			Out *= (1.0f + HexK::BackstabMult);
		}

		// 符文的 DamageMultiplier 规则改写（§6.4 的稀有乘区型）
		Out *= GlobalDamageMult;

		return Out;
	}

	/** ⑤ 减伤：DEF 软曲线（§4.4）。永不免伤。 */
	float ApplyDefense(float V, const FHexDamageContext& Ctx)
	{
		if (!Ctx.Target)
		{
			return V;
		}
		const float D = FMath::Max(0.0f, static_cast<float>(Ctx.Target->DEF - Ctx.DefIgnore));
		const float Reduction = D / (D + HexK::DefSoftcap);
		return V * (1.0f - Reduction);
	}
}

// ───────────────────────────────────────────────────────── 各阶段

float FHexDamageCalculator::BaseValue(const FHexDamageContext& Ctx)
{
	// ① 系数化
	float V = Ctx.Flat + static_cast<float>(DamageStatOf(Ctx.Source, Ctx.StatRef)) * Ctx.StatRatio;

	// ② 平坦加成：显式传入的 + 来自状态的（力量）
	V += Ctx.FlatBonus;
	if (Ctx.Source)
	{
		V += static_cast<float>(Ctx.Source->GetFlatAtkBonus());
	}

	return V;
}

float FHexDamageCalculator::CritMultiplier(const FHexUnit* Source, float ExtraCritMult)
{
	if (!Source)
	{
		return 1.0f;
	}
	// §4.3 分工：CRIT 决定频率，LUK 决定倍率
	const float Base = 1.0f + HexK::CritBaseMult
		+ static_cast<float>(Source->LUK) * HexK::LukToCritDmg;
	return Base * ExtraCritMult;
}

float FHexDamageCalculator::DodgeChance(const FHexDamageContext& Ctx)
{
	// 背击无法被闪避（§8.2.3）；状态 tick 与地形危害也不该被闪避
	if (!Ctx.Target || Ctx.bFromRear || Ctx.bCannotBeDodged)
	{
		return 0.0f;
	}
	const float Agi = static_cast<float>(Ctx.Target->AGI);
	if (Agi <= 0.0f)
	{
		return 0.0f;
	}
	// ⚠️ 递减曲线。线性版本会让 AGI 到 60 后每点收益为 0（违反 §4.4）
	return FMath::Min(
		Agi * HexK::AgiToDodge / (Agi + HexK::DodgeSoftcap),
		HexK::DodgeCap);
}

// ───────────────────────────────────────────────────────── 正式计算

FHexDamageResult FHexDamageCalculator::Calculate(
	const FHexDamageContext& Ctx,
	FHexRngStreams* Rng,
	const FHexValueHook& RuneHook,
	float GlobalDamageMult,
	float GlobalCritMult)
{
	FHexDamageResult R;

	// ── 闪避判定（先判，闪避则完全不结算）
	const float Dodge = DodgeChance(Ctx);
	if (Dodge > 0.0f && Rng && Rng->Chance(EHexRngStream::Combat, Dodge))
	{
		R.bIsDodged = true;
		return R;
	}

	// ── ① + ②
	float V = BaseValue(Ctx);

	// ── ②′ 顺序钩子：符文按槽位 1→6 依次作用
	//    这是 §6.5「[锐化,倍化]=21 而 [倍化,锐化]=19」的实现处
	if (RuneHook)
	{
		V = RuneHook(V, R.RuneLog);
	}

	// ── ③ 乘区
	V = ApplyMultipliers(V, Ctx, GlobalDamageMult);

	// ── ④ 暴击
	if (Rng && Ctx.Source)
	{
		const float CritP = static_cast<float>(Ctx.Source->CRIT) / 100.0f;
		if (CritP > 0.0f && Rng->Chance(EHexRngStream::Combat, CritP))
		{
			R.bIsCrit = true;
			V *= CritMultiplier(Ctx.Source, GlobalCritMult);
		}
	}

	// ── ⑤ 减伤
	V = ApplyDefense(V, Ctx);
	R.Raw = V;

	// ── ⑤′ 取整 + 下限（⚠️ 在扣格挡之前，见头文件注释）
	R.Final = FMath::Max(FMath::FloorToInt(V), HexK::MinDamage);

	// ── ⑥⑦ 分配到护盾 / 格挡 / 生命
	if (Ctx.Target)
	{
		int32 Remaining = R.Final;

		// ⑥ 护盾（独立吸收池，回合结束不清空）
		if (!Ctx.bIgnoreBlock)
		{
			const int32 Barrier = Ctx.Target->GetBarrierAmount();
			R.ToBarrier = FMath::Min(Remaining, Barrier);
			Remaining -= R.ToBarrier;
		}

		// ⑦ 格挡
		if (!Ctx.bIgnoreBlock)
		{
			R.ToBlock = FMath::Min(Remaining, Ctx.Target->Block);
			Remaining -= R.ToBlock;
		}

		R.ToHP = Remaining;
	}
	else
	{
		R.ToHP = R.Final;
	}

	return R;
}

// ───────────────────────────────────────────────────────── 预览

FIntPoint FHexDamageCalculator::Preview(
	const FHexDamageContext& Ctx,
	const FHexValueHook& RuneHook,
	float GlobalDamageMult,
	float GlobalCritMult)
{
	float V = BaseValue(Ctx);

	if (RuneHook)
	{
		// ⚠️ 用临时 log，不污染任何持久状态
		TArray<FHexRuneStep> DummyLog;
		V = RuneHook(V, DummyLog);
	}

	V = ApplyMultipliers(V, Ctx, GlobalDamageMult);

	const float Normal = ApplyDefense(V, Ctx);
	const float Crit = ApplyDefense(V * CritMultiplier(Ctx.Source, GlobalCritMult), Ctx);

	return FIntPoint(
		FMath::Max(FMath::FloorToInt(Normal), HexK::MinDamage),
		FMath::Max(FMath::FloorToInt(Crit), HexK::MinDamage));
}

// ───────────────────────────────────────────────────────── 格挡

int32 FHexDamageCalculator::CalculateBlock(
	const FHexUnit* Source, float Flat, FName StatRef, float Ratio, float BlockMult)
{
	float V = Flat + static_cast<float>(DamageStatOf(Source, StatRef)) * Ratio;

	// 状态敏锐的平坦加成
	if (Source)
	{
		V += static_cast<float>(Source->GetFlatBlockBonus());
	}

	V *= BlockMult;

	return FMath::Max(0, FMath::FloorToInt(V));
}
