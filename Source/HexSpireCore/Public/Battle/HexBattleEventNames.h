// Copyright Hex Spire. All Rights Reserved.
//
// 战斗事件名 —— 逻辑层与表现层的【共享词汇表】
//
// ══════════════════════════════════════════════════════════════════
// 为什么必须有这个文件
// ══════════════════════════════════════════════════════════════════
// 事件名是逻辑层与表现层之间的协议。它曾经只以两种形式存在：
//   ① HexActionResolver.cpp 匿名命名空间里的 EV_* 常量（写入方）
//   ② HexBattleFlow.cpp 里手打的 TEXT("damage_dealt") 字面量（读取方）
//
// 也就是说同一个名字在 core 内部就已经有两份独立定义了 ——
// 而表现层要订阅事件时只能再手打第三份。
//
// 这种重复的失败方式是【静默】的：改名时只改一处，
// 另一处的比较永远不成立。症状是"某个特效突然不播了"
// 或"某个符文突然不触发了"，没有编译错误、没有运行时报错，
// 因为 FName 比较失败是完全合法的代码。
//
// 集中到这里之后，改名会让所有引用点一起更新，漏改变成编译错误。
//
// ══════════════════════════════════════════════════════════════════
// 为什么用 inline const FName 而不是宏或 TCHAR* 常量
// ══════════════════════════════════════════════════════════════════
// · 宏不带类型，拼接错误要到运行时才暴露。
// · const TCHAR* 每次比较都要构造临时 FName（字符串哈希），
//   而事件比较在 DispatchTriggersForAction 里是每动作每事件跑一遍的热路径。
// · inline 变量（C++17）保证跨编译单元只有一份实例 ——
//   这一点很关键：UE 的 unity build 会把整个模块的 .cpp 拼进
//   同一个编译单元，普通的头文件内 const 定义在那里会撞
//   C2374「重定义；多次初始化」（HexUnitAppearance.h 顶部有同一个坑的完整记录）。

#pragma once

#include "CoreMinimal.h"

/**
 * 事件名常量。
 *
 * ⚠️ 新增事件时【必须】加在这里，不要在 .cpp 里直接写字面量。
 *    由 Tools/check_discipline 扫描强制（规则：Battle 目录下的
 *    LogEvent 调用实参不得是裸 TEXT 字面量）。
 *
 * ⚠️ 这些名字会进存档（事件日志用于回放），所以【改名是破坏性变更】。
 *    旧回放里的事件名不会随代码更新 —— 改名等于让旧回放静默失效。
 *    要改请连带处理回放版本号。
 */
namespace HexEv
{
	// ── 资源
	inline const FName EnergyChanged   = TEXT("energy_changed");

	// ── 伤害与生命
	inline const FName DamageDealt     = TEXT("damage_dealt");
	inline const FName Dodged          = TEXT("dodged");
	inline const FName Healed          = TEXT("healed");
	inline const FName BlockGained     = TEXT("block_gained");
	inline const FName BlockCleared    = TEXT("block_cleared");
	inline const FName BlockBroken     = TEXT("block_broken");
	inline const FName UnitDied        = TEXT("unit_died");

	// ── 位移
	inline const FName UnitMoved       = TEXT("unit_moved");
	inline const FName UnitRotated     = TEXT("unit_rotated");
	inline const FName Knockback       = TEXT("knockback");
	inline const FName WallSlam        = TEXT("wall_slam");
	inline const FName Trampled        = TEXT("trampled");

	// ── 状态
	inline const FName StatusApplied   = TEXT("status_applied");
	inline const FName StatusRemoved   = TEXT("status_removed");
	inline const FName StatusTicked    = TEXT("status_ticked");
	inline const FName StatusExpired   = TEXT("status_expired");

	// ── 牌堆
	inline const FName CardPlayed      = TEXT("card_played");
	inline const FName CardsDrawn      = TEXT("cards_drawn");
	inline const FName CardDiscarded   = TEXT("card_discarded");
	inline const FName CardExhausted   = TEXT("card_exhausted");
	inline const FName DeckReshuffled  = TEXT("deck_reshuffled");

	// ── 地形
	inline const FName TerrainChanged  = TEXT("terrain_changed");
	inline const FName HazardTriggered = TEXT("hazard_triggered");
	inline const FName HazardExpired   = TEXT("hazard_expired");

	// ── 流程
	inline const FName BattleStart       = TEXT("battle_start");
	inline const FName BattleWin         = TEXT("battle_win");
	inline const FName BattleLose        = TEXT("battle_lose");
	inline const FName PhaseChanged      = TEXT("phase_changed");
	inline const FName RoundAdvanced     = TEXT("round_advanced");
	inline const FName PlayerPhaseBegin  = TEXT("player_phase_begin");
	inline const FName EnemyPhaseBegin   = TEXT("enemy_phase_begin");
	inline const FName ExplorePhaseBegin = TEXT("explore_phase_begin");

	// ── 敌人与意图
	inline const FName IntentsUpdated  = TEXT("intents_updated");
	inline const FName IntentWhiffed   = TEXT("intent_whiffed");
	inline const FName EnemySkillBegin = TEXT("enemy_skill_begin");
	inline const FName EnemyStunned    = TEXT("enemy_stunned");
	inline const FName EnemyRooted     = TEXT("enemy_rooted");

	// ── 符文
	inline const FName RuneTriggered   = TEXT("rune_triggered");
}
