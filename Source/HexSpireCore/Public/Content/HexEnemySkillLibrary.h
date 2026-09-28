// Copyright Hex Spire. All Rights Reserved.
//
// 敌人技能库 —— 代码内建基线
//
// 与符文库（Runes/HexRuneLibrary）同样的分工：
// 技能表放在独立文件里，避免 HexContentLibrary.cpp 膨胀到无法审阅。
// FHexContentLibrary 只做转发。
//
// ══════════════════════════════════════════════════════════════════
// 内建技能【刻意没有绑到现有四只敌人身上】
// ══════════════════════════════════════════════════════════════════
// 下面这几条是给策划配表用的【范例与起点】，导出 CSV 后能直接看到
// 「一条技能长什么样」，而不是从空表开始猜字段含义。
//
// 现有四只敌人（扑咬犬/投石手/石傀/攻城虫）的 SkillIds 保持为空，
// 因此它们继续走 profile 默认攻击 —— 数值与配表前逐位相同。
// 理由见 HexEnemySkillData.h 顶部：那些数值是 Godot 版实测调过的，
// 给它们塞「等价」技能会引入 profile 射程（Boss 随阶段变化 1/2/3）
// 在表里无法表达的细节，从而静默改变平衡。
//
// 策划要启用技能，在敌人表的 SkillIds 里填 id 即可。

#pragma once

#include "CoreMinimal.h"
#include "Battle/HexEnemySkillData.h"

struct HEXSPIRECORE_API FHexEnemySkillLibrary
{
	static const TArray<FHexEnemySkillData>& AllSkills();

	/** 未知 id 返回 nullptr —— 调用方须容忍（敌人表可能引用了还没配的技能） */
	static const FHexEnemySkillData* Find(FName Id);

	/**
	 * 用外部数据（DataTable）覆写/追加一条技能。
	 *
	 * ⚠️ 与 OverrideCard 同一套语义与同一条纪律：
	 *    core 自己永远不调用它，只有表现层的配表加载器会调。
	 *    这样 headless 验证与批量模拟拿到的始终是代码内建那套，
	 *    不受配表状态影响。
	 *
	 * 同 id 则【整条替换】，新 id 则追加。
	 * @return true = 覆写了已有技能；false = 追加了新技能
	 */
	static bool Override(const FHexEnemySkillData& Skill);

	/** 撤销全部覆写，回到纯代码内建状态（验证器用它保证基线一致） */
	static void ResetOverrides();
};
