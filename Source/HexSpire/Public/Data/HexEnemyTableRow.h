// Copyright Hex Spire. All Rights Reserved.
//
// 敌人 DataTable 行结构 —— 给策划配数值用
//
// 分层与 FHexCardTableRow 一致（见那个文件顶部）：
//   HexSpireCore  FHexEnemyData        逻辑层权威结构，纯 C++
//   HexSpire      FHexEnemyTableRow    配表结构
//                 ↓ ToEnemyData()
//
// ══════════════════════════════════════════════════════════════════
// 敌人表是【横向对比】价值最高的一张
// ══════════════════════════════════════════════════════════════════
// 平衡敌人本质上就是横向对比：把四只怪的 HP 并排看才能发现某只突然跳一截，
// 把 PreferredDistance 和攻击射程并排看才能发现第 3 只永远进不了射程
// —— 那个 bug 的症状是"这只怪整场只后退"，不报任何错。
// DataTable 的表格视图是唯一能一眼看出这类问题的界面。
//
// 资产引用不在这张表里，在 DA_UnitVisual_* （见 HexHeroTableRow.h 顶部的分界线说明）。
// 两边靠 FName Id 对应：本行的 RowName == UHexUnitVisualSet::UnitId。
//
// ⚠️ 六属性填的是【第 1 层、腐蚀度 0】的基线值。
//    MakeEnemyUnit 会按层数与腐蚀度缩放 HP/ATK。
//    不要预先把难度加成算进表里 —— 那会被缩放二次放大。

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Core/HexSpireEnums.h"
#include "Content/HexContentLibrary.h"
#include "HexEnemyTableRow.generated.h"

/**
 * 敌人配表行。
 *
 * ⚠️ RowName 必须等于敌人 Id（如 biting_hound）。
 *    不一致会让覆写静默落空。Loader 会校验并报错。
 */
USTRUCT(BlueprintType)
struct HEXSPIRE_API FHexEnemyTableRow : public FTableRowBase
{
	GENERATED_BODY()

	// ═════════════════════════════════════ 基础

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "基础")
	FString DisplayName;

	/** 体型。S=1格 / M=3格 / L=6格，影响 footprint 与射程起算点（§8.5）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "基础")
	EHexSizeClass SizeClass = EHexSizeClass::S;

	// ═════════════════════════════════════ 六属性（第1层基线值）

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "属性",
		meta = (ClampMin = "1"))
	int32 BaseHP = 24;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "属性",
		meta = (ClampMin = "0"))
	int32 BaseATK = 13;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "属性",
		meta = (ClampMin = "0"))
	int32 BaseDEF = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "属性",
		meta = (ClampMin = "0"))
	int32 BaseAGI = 8;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "属性",
		meta = (ClampMin = "0"))
	int32 BaseLUK = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "属性",
		meta = (ClampMin = "0"))
	int32 BaseCRIT = 0;

	// ═════════════════════════════════════ AI

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI")
	EHexAIProfile AIProfile = EHexAIProfile::Aggressive;

	/**
	 * 意图是否可躲。
	 *
	 * ⚠️ §13.2 要求玩家能读出"能躲 / 不能躲"的区分（实线 vs 虚线+连线）。
	 *    全表都填 TrackTarget 会让这条 UX 彻底消失 —— 玩家学不到
	 *    "走开就能躲"这个核心互动。杂兵应当以 FixedTile 为主。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI")
	EHexIntentTargeting IntentTargeting = EHexIntentTargeting::FixedTile;

	/**
	 * 移动力（每回合可走的格数预算）。
	 *
	 * 这是 AGI 之外唯一能表达"这只怪逼得多紧"的旋钮 ——
	 * "慢速重装"与"高速游走"靠它区分。
	 *
	 * ⚠️ 填 0 会让该敌人永远不移动。MakeEnemyUnit 在入口处 clamp，
	 *    所以填错不会死锁，但行为会与预期不同且不报错。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI",
		meta = (ClampMin = "0"))
	int32 MoveBudget = 2;

	/**
	 * 风筝型的理想距离（离目标多远就不再靠近）。
	 *
	 * 只对 RangedKiter 有意义，其他 profile 一律贴近到 1。
	 *
	 * ⚠️ 填得比攻击射程还大会让敌人永远进不了射程 → 整场只后退，
	 *    不报任何错。由 VerifyContent 断言 PreferredDistance <= 攻击射程。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI",
		meta = (ClampMin = "1"))
	int32 PreferredDistance = 3;

	/**
	 * 攻击射程覆写。-1 = 用 AIProfile 的默认射程。
	 *
	 * ⚠️ 填 1 会让该敌人永远产不出【可躲型】攻击意图 ——
	 *    两段明示规则规定距离 ≤1 一律转追踪
	 *    （见 HexEnemyAI.cpp 里 AttackRangeOf 的长注释）。
	 *    这会静默削掉 §13.2 的可预判性，而不是报错。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI",
		meta = (ClampMin = "-1"))
	int32 AttackRangeOverride = -1;

	// ═════════════════════════════════════ 技能

	/**
	 * 技能 id 列表（引用敌人技能库）。
	 *
	 * ⚠️ 留空 = 走 profile 默认攻击（AttackRangeOf + 1.0×ATK），
	 *    行为与"技能系统存在之前"逐位相同。这是刻意的默认值 ——
	 *    现有四只敌人的数值是实测调过的，不该因为多了技能框架而被动改变。
	 *
	 * ⚠️ 引用不存在的 id 会记一条警告并跳过该 id，
	 *    而不是让敌人整场站着不动。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "技能")
	TArray<FName> SkillIds;

	// ═════════════════════════════════════ 等级与抗性

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "等级")
	bool bIsElite = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "等级")
	bool bIsBoss = false;

	/**
	 * 击退抗性覆写。-1 = 用体型默认值。
	 *
	 * 填一个很大的数（如 999）等于免疫击退 —— 攻城虫就是这么配的。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "等级",
		meta = (ClampMin = "-1"))
	int32 KnockbackResistOverride = -1;

	// ═════════════════════════════════════ 文本

	/** 图鉴文本：告诉玩家这只怪的应对方式。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "文本",
		meta = (MultiLine = "true"))
	FString CodexText;

	/**
	 * 转成逻辑层的敌人定义。
	 * @param InId 敌人 Id（取自 RowName）
	 */
	FHexEnemyData ToEnemyData(FName InId) const;

	/** 由逻辑层结构填充本行（导出 CSV 用） */
	void FromEnemyData(const FHexEnemyData& In);
};
