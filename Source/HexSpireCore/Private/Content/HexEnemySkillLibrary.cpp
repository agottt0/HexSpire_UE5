// Copyright Hex Spire. All Rights Reserved.
//
// 敌人技能表 —— 代码内建基线（配表范例）
//
// ⚠️ 这些技能【默认没有绑到任何敌人身上】，现有四只敌人的 SkillIds 为空，
//    继续走 profile 默认攻击，数值与配表前逐位相同。
//    它们的作用是：导出 CSV 后策划能看到「一条技能长什么样」。
//
// ⚠️ 全部数值走 §7.5 系数化：Value = FlatValue + Stats[StatRef] × StatRatio。
//    StatRatio 以【敌人自身 ATK】为基准，所以腐蚀度缩放 ATK 时技能自动跟涨，
//    不需要为每个难度档位重配一份技能。
//    例外是离散量（状态层数、击退格数），它们是策略层锚点，填固定值。

#include "Content/HexEnemySkillLibrary.h"

namespace
{
	/**
	 * ⚠️ 符号名带 EnemySkill 前缀：UE 的 unity build 会把同模块多个 .cpp
	 *    合并进一个编译单元，匿名命名空间里的同名符号会直接撞车
	 *    （C2374 重定义，本工程已踩过一次，见 HexUnitAppearance.h 的说明）。
	 */
	TArray<FHexEnemySkillData> BuildAllEnemySkills()
	{
		TArray<FHexEnemySkillData> Out;
		Out.Reserve(6);

		// ── 单体重击：最朴素的一条，作为「怎么配伤害」的样板
		{
			FHexEnemySkillData S;
			S.Id = TEXT("es_slam");
			S.DisplayName = TEXT("重砸");
			S.IntentKind = EHexIntentKind::Attack;
			S.TargetSpec = FHexTargetSpec::Make(EHexTargetShape::Single, 0, 1);
			S.Effects.Add(FHexEffectStep::MakeDamage(0.0f, TEXT("ATK"), 1.2f));
			S.Priority = 10;
			S.TelegraphText = TEXT("抬起前肢，下一击更重。");
			Out.Add(S);
		}

		// ── 范围践踏：展示 Burst + 击退的组合
		//
		// ⚠️ 击退格数是【离散量】，不随 ATK 缩放 ——
		//    否则后期敌人一下把玩家推出战场，走位空间归零。
		{
			FHexEnemySkillData S;
			S.Id = TEXT("es_stomp");
			S.DisplayName = TEXT("震地");
			S.IntentKind = EHexIntentKind::Attack;
			S.TargetSpec = FHexTargetSpec::Make(EHexTargetShape::Burst, 0, 1, /*AreaSize=*/1);
			S.Effects.Add(FHexEffectStep::MakeDamage(0.0f, TEXT("ATK"), 0.8f));
			S.Effects.Add(FHexEffectStep::MakeKnockback(1));
			S.CooldownRounds = 2;
			S.Priority = 20;
			S.TelegraphText = TEXT("蓄力下踏，周围一圈都会被震开。");
			Out.Add(S);
		}

		// ── 远程齿击：追踪型覆写的范例
		{
			FHexEnemySkillData S;
			S.Id = TEXT("es_rend");
			S.DisplayName = TEXT("撕咬");
			S.IntentKind = EHexIntentKind::Attack;
			// 大招锁人：平时可躲的近战也能有一记躲不掉的攻击
			S.bOverrideTargeting = true;
			S.Targeting = EHexIntentTargeting::TrackTarget;
			S.TargetSpec = FHexTargetSpec::Make(EHexTargetShape::Single, 0, 2);
			S.Effects.Add(FHexEffectStep::MakeDamage(0.0f, TEXT("ATK"), 0.7f));
			// 状态层数是离散量
			S.Effects.Add(FHexEffectStep::MakeApplyStatus(TEXT("bleed"), 2));
			S.CooldownRounds = 3;
			S.FirstUsableRound = 2;
			S.Priority = 30;
			S.TelegraphText = TEXT("咬住了，这一口躲不开。");
			Out.Add(S);
		}

		// ── 多段攻击：HitCount 的范例
		{
			FHexEnemySkillData S;
			S.Id = TEXT("es_flurry");
			S.DisplayName = TEXT("连抓");
			S.IntentKind = EHexIntentKind::MultiAttack;
			S.TargetSpec = FHexTargetSpec::Make(EHexTargetShape::Single, 0, 1);
			S.Effects.Add(FHexEffectStep::MakeDamage(0.0f, TEXT("ATK"), 0.55f));
			S.HitCount = 2;
			S.CooldownRounds = 2;
			S.Priority = 15;
			S.TelegraphText = TEXT("两连抓，单次不痛但加起来不轻。");
			Out.Add(S);
		}

		// ── 自我强化：非伤害技能 + Boss 阶段门槛的范例
		//
		// ⚠️ IntentKind 必须是 Buff：UI 据此画"强化"而不是伤害数字。
		//    配成 Attack 却不含伤害步骤，玩家会看到"攻击 0"。
		{
			FHexEnemySkillData S;
			S.Id = TEXT("es_enrage");
			S.DisplayName = TEXT("狂怒");
			S.IntentKind = EHexIntentKind::Buff;
			S.TargetSpec = FHexTargetSpec::MakeSelf();
			{
				FHexEffectStep E = FHexEffectStep::MakeApplyStatus(TEXT("strength"), 2);
				E.TargetFilter = EHexTargetFilter::Self;
				S.Effects.Add(E);
			}
			S.CooldownRounds = 4;
			S.MinBossPhase = 1;
			// 残血才狂暴：给玩家"压到一半会变强"这条可学习的规律
			S.UseBelowSelfHPRatio = 0.5f;
			// 优先级高于普通攻击，所以条件一满足就必然先放 —— 可预测
			S.Priority = 50;
			S.TelegraphText = TEXT("伤口让它更凶了。");
			Out.Add(S);
		}

		// ── 自我防护：GainBlock 的范例
		{
			FHexEnemySkillData S;
			S.Id = TEXT("es_harden");
			S.DisplayName = TEXT("硬化");
			S.IntentKind = EHexIntentKind::Buff;
			S.TargetSpec = FHexTargetSpec::MakeSelf();
			{
				// 格挡吃 DEF 而非 ATK —— 防御型敌人的数值应该跟防御属性挂钩
				FHexEffectStep E = FHexEffectStep::MakeBlock(0.0f, TEXT("DEF"), 1.5f);
				E.TargetFilter = EHexTargetFilter::Self;
				S.Effects.Add(E);
			}
			S.CooldownRounds = 3;
			S.UseBelowSelfHPRatio = 0.6f;
			S.Priority = 40;
			S.TelegraphText = TEXT("表层石甲收紧，下一击会被吃掉大半。");
			Out.Add(S);
		}

		return Out;
	}

	/**
	 * 可变技能表。
	 *
	 * ⚠️ 与 HexContentLibrary 的 MutableCards 同一个模式：
	 *    函数内 static 保证首次调用时才构造，且线程安全（C++11 起）。
	 *    直接用命名空间级 static TArray 会引入静态初始化顺序问题 ——
	 *    另一个翻译单元的静态对象若在构造期查询技能表，会拿到空表。
	 */
	TArray<FHexEnemySkillData>& MutableEnemySkills()
	{
		static TArray<FHexEnemySkillData> Skills = BuildAllEnemySkills();
		return Skills;
	}
}

const TArray<FHexEnemySkillData>& FHexEnemySkillLibrary::AllSkills()
{
	return MutableEnemySkills();
}

const FHexEnemySkillData* FHexEnemySkillLibrary::Find(FName Id)
{
	if (Id.IsNone())
	{
		return nullptr;
	}
	for (const FHexEnemySkillData& S : AllSkills())
	{
		if (S.Id == Id)
		{
			return &S;
		}
	}
	return nullptr;
}

bool FHexEnemySkillLibrary::Override(const FHexEnemySkillData& Skill)
{
	TArray<FHexEnemySkillData>& Skills = MutableEnemySkills();

	for (FHexEnemySkillData& S : Skills)
	{
		if (S.Id == Skill.Id)
		{
			// 整条替换而非字段级合并 —— 理由同 OverrideCard：
			// CSV 的空单元格与"填了 0"在 DataTable 里无法区分。
			S = Skill;
			return true;
		}
	}

	Skills.Add(Skill);
	return false;
}

void FHexEnemySkillLibrary::ResetOverrides()
{
	MutableEnemySkills() = BuildAllEnemySkills();
}
