// Copyright Hex Spire. All Rights Reserved.

#include "Battle/HexCardData.h"
#include "Battle/HexUnit.h"

namespace
{
	/** 取单位的某项属性。StatRef 为空或未知时返回 0（纯 flat 效果） */
	int32 StatOf(const FHexUnit* U, FName Ref)
	{
		if (!U)
		{
			return 0;
		}
		// 用 FName 比较而非字符串比较 —— 每次伤害计算都会走这里，
		// 10 万场模拟下字符串比较是可观开销。
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
}

// ───────────────────────────────────────────────────────── FHexEffectStep

int32 FHexEffectStep::ValueFor(const FHexUnit* Source) const
{
	const float V = FlatValue + static_cast<float>(StatOf(Source, StatRef)) * StatRatio;
	return FMath::Max(0, FMath::FloorToInt(V));
}

FHexEffectStep FHexEffectStep::MakeDamage(float Flat, FName Stat, float Ratio, int32 InRepeat)
{
	FHexEffectStep S;
	S.Op = EHexEffectOp::DealDamage;
	S.FlatValue = Flat;
	S.StatRef = Stat;
	S.StatRatio = Ratio;
	S.Repeat = InRepeat;
	S.TargetFilter = EHexTargetFilter::Enemy;
	return S;
}

FHexEffectStep FHexEffectStep::MakeBlock(float Flat, FName Stat, float Ratio)
{
	FHexEffectStep S;
	S.Op = EHexEffectOp::GainBlock;
	S.FlatValue = Flat;
	S.StatRef = Stat;
	S.StatRatio = Ratio;
	S.TargetFilter = EHexTargetFilter::Self;
	return S;
}

FHexEffectStep FHexEffectStep::MakeMove(int32 InDistance)
{
	FHexEffectStep S;
	S.Op = EHexEffectOp::MoveSelf;
	S.Distance = InDistance;
	S.TargetFilter = EHexTargetFilter::Self;
	return S;
}

FHexEffectStep FHexEffectStep::MakeDash(int32 InDistance)
{
	FHexEffectStep S;
	S.Op = EHexEffectOp::Dash;
	S.Distance = InDistance;
	S.TargetFilter = EHexTargetFilter::Self;
	return S;
}

FHexEffectStep FHexEffectStep::MakeKnockback(int32 InDistance)
{
	FHexEffectStep S;
	S.Op = EHexEffectOp::Knockback;
	S.Distance = InDistance;
	S.TargetFilter = EHexTargetFilter::Enemy;
	return S;
}

FHexEffectStep FHexEffectStep::MakeApplyStatus(FName InStatusId, int32 InStacks)
{
	FHexEffectStep S;
	S.Op = EHexEffectOp::ApplyStatus;
	S.StatusId = InStatusId;
	S.StatusStacks = InStacks;
	S.TargetFilter = EHexTargetFilter::Enemy;
	return S;
}

FHexEffectStep FHexEffectStep::MakeDraw(int32 Count)
{
	FHexEffectStep S;
	S.Op = EHexEffectOp::DrawCard;
	S.Repeat = Count;
	S.TargetFilter = EHexTargetFilter::Self;
	return S;
}

FHexEffectStep FHexEffectStep::MakeGainEnergy(int32 Amount)
{
	FHexEffectStep S;
	S.Op = EHexEffectOp::GainEnergy;
	S.FlatValue = static_cast<float>(Amount);
	S.TargetFilter = EHexTargetFilter::Self;
	return S;
}

FHexEffectStep FHexEffectStep::MakeOp(EHexEffectOp InOp)
{
	FHexEffectStep S;
	S.Op = InOp;
	return S;
}

// ───────────────────────────────────────────────────────── FHexTargetSpec

FHexTargetSpec FHexTargetSpec::MakeSelf()
{
	FHexTargetSpec T;
	T.Shape = EHexTargetShape::SelfShape;
	T.RangeMin = 0;
	T.RangeMax = 0;
	T.bRequiresLineOfSight = false;
	return T;
}

FHexTargetSpec FHexTargetSpec::Make(
	EHexTargetShape InShape, int32 InRangeMin, int32 InRangeMax, int32 InAreaSize, bool bLoS)
{
	FHexTargetSpec T;
	T.Shape = InShape;
	T.RangeMin = InRangeMin;
	T.RangeMax = InRangeMax;
	T.AreaSize = InAreaSize;
	T.bRequiresLineOfSight = bLoS;
	return T;
}

FHexTargetSpec FHexTargetSpec::MakeTile(int32 InRangeMin, int32 InRangeMax)
{
	FHexTargetSpec T;
	T.Shape = EHexTargetShape::Tile;
	T.RangeMin = InRangeMin;
	T.RangeMax = InRangeMax;
	T.bRequiresLineOfSight = false;
	T.bCanTargetEmptyCell = true;
	return T;
}

FHexTargetSpec FHexTargetSpec::MakeDashPath(int32 InRangeMin, int32 InRangeMax)
{
	FHexTargetSpec T;
	T.Shape = EHexTargetShape::DashPath;
	T.RangeMin = InRangeMin;
	T.RangeMax = InRangeMax;
	// ⚠️ bRequiresLineOfSight 必须为 false。
	//    视线判定把【单位】也算作遮挡物之外，还会因石柱直接否决目标；
	//    冲撞需要的是"墙挡、人不挡"，这条规则由 LegalCells 里的
	//    DashPath 分支自己实现，不能借用视线。
	T.bRequiresLineOfSight = false;
	T.bCanTargetEmptyCell = true;
	return T;
}

// ───────────────────────────────────────────────────────── FHexCardData

FString FHexCardData::RenderDescription(const FHexUnit* Source) const
{
	if (DescriptionTemplate.IsEmpty())
	{
		return FString();
	}

	FString Out = DescriptionTemplate;

	// 汇总各类占位符的实算值。
	// ⚠️ 卡面数值是【动态的】—— 随 ATK 成长会变成三位数（美术文档 §12 提醒过
	//    插画必须给数字留低对比区域）。所以这里必须每次实算而非缓存。
	int32 TotalDamage = 0;
	int32 TotalBlock = 0;
	int32 Knockback = 0;
	int32 MoveDist = 0;
	int32 StatusStacksSum = 0;
	int32 DrawCount = 0;
	int32 HitCount = 1;

	for (const FHexEffectStep& S : Effects)
	{
		switch (S.Op)
		{
		case EHexEffectOp::DealDamage:
			TotalDamage += S.ValueFor(Source);
			HitCount = FMath::Max(HitCount, S.Repeat);
			break;
		case EHexEffectOp::GainBlock:
			TotalBlock += S.ValueFor(Source);
			break;
		case EHexEffectOp::Knockback:
		case EHexEffectOp::Pull:
			Knockback = FMath::Max(Knockback, S.Distance);
			break;
		case EHexEffectOp::MoveSelf:
		case EHexEffectOp::Dash:
		case EHexEffectOp::Blink:
			MoveDist = FMath::Max(MoveDist, S.Distance);
			break;
		case EHexEffectOp::ApplyStatus:
			StatusStacksSum += S.StatusStacks;
			break;
		case EHexEffectOp::DrawCard:
			DrawCount += S.Repeat;
			break;
		default:
			break;
		}
	}

	Out = Out.Replace(TEXT("{dmg}"), *FString::FromInt(TotalDamage));
	Out = Out.Replace(TEXT("{block}"), *FString::FromInt(TotalBlock));
	Out = Out.Replace(TEXT("{kb}"), *FString::FromInt(Knockback));
	Out = Out.Replace(TEXT("{move}"), *FString::FromInt(MoveDist));
	Out = Out.Replace(TEXT("{stacks}"), *FString::FromInt(StatusStacksSum));
	Out = Out.Replace(TEXT("{draw}"), *FString::FromInt(DrawCount));
	Out = Out.Replace(TEXT("{hits}"), *FString::FromInt(HitCount));

	return Out;
}
