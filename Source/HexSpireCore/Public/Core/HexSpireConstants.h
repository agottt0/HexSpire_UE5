// Copyright Hex Spire. All Rights Reserved.
//
// 全局常量与可调参数的【唯一定义处】—— 对应 Godot 版 src/core/constants.gd
//
// 纪律：数值不散落在逻辑里。想调手感 → 改这里 → 重跑 BattleSim。
// 策划案 §4.4 的所有 K_* 占位常量都落在此处。
//
// ⚠️ 本文件的每一个数值都是 Godot 版【实测调过】的结果，注释记录了
//    每次改动的原因。移植时一个不改（用户决策 q9）。

#pragma once

#include "CoreMinimal.h"

namespace HexK
{
	// ───────────────────────────────────────────────── 伤害管线

	/**
	 * 伤害下限。
	 *
	 * ⚠️ 已确认偏离策划案 §4.4 字面顺序：下限卡在【扣格挡之前】。
	 *   按字面（下限在格挡之后）会让格挡永远无法完全吸收一次攻击 ——
	 *   Block=999 也要掉 1 血，镇妖者的「格挡不清空」被动因此失去意义。
	 *   「保证永远能破防」针对的是 DEF 软曲线，不是 Block。
	 */
	inline constexpr int32 MinDamage = 1;

	/**
	 * 防御减伤软上限：减伤率 = DEF / (DEF + DefSoftcap)
	 *
	 * ⚠️ 曾设为 50.0，那是按 §4.4「DEF=30 时 37.5%」的【后期养成段位】设计的。
	 *   但第一层实际 DEF 只有 1–8，代入后全体减伤只有 2%–14% ——
	 *   DEF 属性事实上不存在，"堆防御"不是一条路线。
	 *   改为 12.0 后：DEF 6 → 33%，DEF 8 → 40%，DEF 2 → 14%，属性开始有意义。
	 *   后期 DEF 上到 30 时是 71%，仍在递减曲线上，不会免伤。
	 */
	inline constexpr float DefSoftcap = 12.0f;

	/**
	 * 格挡上限比例（策划案 §4.2：镇妖者被动「格挡不清空，**可叠加至上限**」）
	 *
	 * ⚠️ 上限曾【完全没实现】，导致镇妖者 10 回合叠到 140 格挡（HP 只有 80）
	 *   —— 失败条件在数学上不存在。实测 6 回合 78 格挡、全程 0 掉血。
	 *
	 * 上限取最大生命的 25%（镇妖者 = 20 点）。为什么不是 40%：
	 *   5 体力最优可买 23 点格挡/回合，若上限 32 点（40%）则
	 *   "当回合新增格挡"总能覆盖敌人输出 → 格挡退化成无敌护盾。
	 *   25% 让格挡回归"抵消一次重击"的定位。
	 */
	inline constexpr float BlockCapRatio = 0.25f;

	/** 格挡上限的绝对下限（低 HP 单位也要有基本的格挡空间） */
	inline constexpr int32 BlockCapMin = 12;

	/** 暴击基础倍率与 LUK 加成（§4.3：CRIT 管频率，LUK 管倍率） */
	inline constexpr float CritBaseMult = 0.5f;
	inline constexpr float LukToCritDmg = 0.02f;

	/**
	 * 闪避（§4.3：AGI 的次要作用之一）
	 *
	 * ⚠️ 公式必须是【递减曲线】：min(AGI×K / (AGI + Softcap), Cap)
	 *   曾实现成线性 min(AGI×K, Cap)，那样 AGI 成长到 60 就直接撞上限，
	 *   之后每点 AGI 收益为 0 —— 违反 §4.4「所有百分比属性走递减曲线」。
	 */
	inline constexpr float AgiToDodge = 0.60f;
	inline constexpr float DodgeSoftcap = 40.0f;
	inline constexpr float DodgeCap = 0.30f;

	/** 移动卡额外位移：每 N 点 AGI 多走 1 格 */
	inline constexpr int32 AgiPerStep = 8;

	// ───────────────────────────────────────────────── 战斗规则

	/**
	 * 地形危害每格伤害。§8.2.2 机制点 7：大体型同时站 N 格则结算 N 次。
	 *
	 * ⚠️ 曾是 3 点，敌人 24–120 HP → 占比 12.5%，「把敌人推进尖刺」无收益，
	 *   五件套之一的「位移即伤害」（§4.1 机制 3+4）形同不存在。
	 */
	inline constexpr int32 HazardDamage = 12;

	/** 撞墙额外伤害（§8.6「位移即伤害」）。被击退撞到墙/单位时结算。 */
	inline constexpr int32 WallSlamDamage = 8;

	// ───────────────────────────────────────────────── 战场规格

	inline constexpr int32 HandLimit = 10;
	inline constexpr int32 BoardCols = 7;
	inline constexpr int32 BoardRows = 7;
	inline constexpr int32 HeroSpawnCol = 4;
	inline constexpr int32 HeroSpawnRow = 1;
	/** §8.2.4：朝上 */
	inline constexpr int32 HeroSpawnFacing = 2;

	/** 敌人生成区（§8.8：我方在下、敌方在上） */
	inline constexpr int32 EnemySpawnRowMin = 5;
	inline constexpr int32 EnemySpawnRowMax = 7;
	inline constexpr int32 EnemySpawnMinDist = 2;

	// ───────────────────────────────────────────────── 背击

	/**
	 * 背击后弧：相对【正后方索引】的偏移。
	 * §8.2.3 只说"背面 2 个方向"，未指明是哪两个。
	 * 改这一行即可调整（含改成 3 方向对称后弧），逻辑不散落在 DamageCalculator 里。
	 */
	inline constexpr int32 BackstabRearOffsets[] = { 0, 1 };
	inline constexpr int32 BackstabRearOffsetCount = 2;

	/** 背击伤害乘区（§8.2.3：加成 + 无法被闪避） */
	inline constexpr float BackstabMult = 0.5f;

	// ───────────────────────────────────────────────── 安全闸（R7）

	/**
	 * 符文触发递归深度上限。超限 → 写 RuleViolations 并停止分发，【绝不崩溃】。
	 * BattleSim 靠这条把死循环变成可统计数据。
	 */
	inline constexpr int32 MaxTriggerDepth = 10;

	/**
	 * 单次 Emit 内的触发总数上限。
	 * 兜住"A 触发 B、B 触发 A"这种深度=2 但宽度爆炸的组合
	 * （策划案 R7 只提了深度，这条是补的）。
	 */
	inline constexpr int32 MaxTriggersPerEmit = 64;

	/** 单次 ResolveAll 的动作总数上限。死循环硬闸。 */
	inline constexpr int32 MaxActionsPerResolve = 2000;

	/**
	 * 单次 ResolveAll 内，由「动作 → 触发时机」翻译产生的 Emit 次数上限。
	 *
	 * ⚠️ 这条是【必须补的】，MaxTriggerDepth 盖不住这个洞：
	 *    Depth 只在 Emit 的调用栈内有效，Emit 一返回就归零。
	 *    而符文效果是先 PushNext 成动作、稍后才执行的，所以
	 *      「符文造成伤害 → 伤害动作执行 → 翻译出 OnDamageDealt
	 *        → 同一符文再次触发 → 又造成伤害」
	 *    这条链上每次 Emit 都是 Depth=0，深度闸【一次都不会命中】。
	 *
	 *    没有这条预算时，一个「监听 OnDamageDealt 且造成伤害」的符文
	 *    会一路刷到 MaxActionsPerResolve(2000)，
	 *    表面症状是 action_overflow + 战斗中止，真凶却是符文自激 ——
	 *    而这种符文将来一定会有人写（"受击反伤"是最常见的符文形态）。
	 *
	 *    256 的取值：正常连锁（一次攻击触发三四个符文、每个派生
	 *    一两个动作）远低于此；自激循环几十次内就会撞线。
	 */
	inline constexpr int32 MaxObserverEmitsPerResolve = 256;

	/** BattleSim 单场回合上限，超过计入 timeout */
	inline constexpr int32 MaxRoundsPerBattle = 50;

	// ───────────────────────────────────────────────── 卡组（D2 / D3）

	/** 初始卡组容量，不含基石卡（§7.6） */
	inline constexpr int32 InitialDeckCapacity = 8;
	/** 卡组容量硬上限 */
	inline constexpr int32 MaxDeckCapacity = 20;
	/** 同名卡在卡组中的最大份数 */
	inline constexpr int32 DefaultMaxCopiesInDeck = 3;

	/**
	 * 固定卡的 uid 起始值。
	 *
	 * ⚠️ 固定卡与卡组卡共用一个 uid 命名空间（PlayCard 只收一个 uid），
	 *    但两者由【不同的分配器】发号：卡组从 1 递增、装备注入从运行时
	 *    计数器取号。一旦撞号，玩家点一张牌会结算成另一张 ——
	 *    而且撞号只在特定卡组长度下发生，极难复现。
	 *    分段到 10000 起，留足空间，并由 VerifyContent 断言不重叠。
	 */
	inline constexpr int32 FixedCardUidBase = 10000;

	// ───────────────────────────────────────────────── 符文（D6）

	/** 符文槽位数，固定 6，开局全开（§6.2） */
	inline constexpr int32 RuneSlotCount = 6;

	// ───────────────────────────────────────────────── 腐蚀度（§9.4）

	/** 每清空一间普通房 +1 */
	inline constexpr int32 CorruptionPerRoom = 1;
	/** 精英房 +2 */
	inline constexpr int32 CorruptionPerEliteRoom = 2;
	/** 腐蚀度每 +1，敌人 HP 提升的比例 */
	inline constexpr float CorruptionEnemyHpStep = 0.08f;
	/** 腐蚀度每 +1，敌人 ATK 提升的比例 */
	inline constexpr float CorruptionEnemyAtkStep = 0.06f;
	/** 腐蚀度每 +1，掉落数量权重提升 */
	inline constexpr float CorruptionLootStep = 0.15f;
	/** 每 N 点腐蚀度，怪物组难度档位 +1 */
	inline constexpr int32 CorruptionPerDifficultyTier = 3;

	// ───────────────────────────────────────────────── 地图（D4）

	/** 第一版一层的房间数（用户决策 q19：入口+2普通+精英+营地+Boss） */
	inline constexpr int32 FloorRoomCount = 6;

	// ───────────────────────────────────────────────── 一层循环的血量预算
	//
	// ⚠️ 这两个常量【不在策划案里】，是一层循环带来的必要补充。
	//
	//    Godot 版的全部数值都按【单场战斗】调的：
	//    "enc_02 总输出 19 点/回合 → 镇妖者撑 4 回合" ——
	//    单场本身就贴着生死线。
	//    而一层循环要连打 4 场且生命跨战斗继承，
	//    血量预算只有 80 + 营地 24 = 104 点，实测需要约 200 点。
	//
	//    HexPlaytest 实测（60 局）：调整前阵亡率 100%、击败 Boss 0%，
	//    且连续三次增强测试机器人（会买格挡、会走位躲避、会追击风筝）
	//    都无法改善 —— 证明是结构性缺口而非打法问题。

	/**
	 * 非 Boss 战斗胜利后回复的最大生命比例。
	 *
	 * 取 0.18 而非更高：它要能补上单场的净损失（约 50 点的三分之一），
	 * 但不能让"打完就满血"—— 那会让血量管理这条压力线消失，
	 * 也会让营地失去意义。
	 */
	inline constexpr float PostBattleHealRatio = 0.18f;

	/**
	 * 营地回复的最大生命比例。
	 *
	 * 从 0.30 提到 0.45：营地是策划案 §9.6 设计好的补给点，
	 * 加强它比新增机制更符合原设计。
	 * 仍然【不是满血】—— 营地是"止损点"而非"重置点"。
	 */
	inline constexpr float CampHealRatio = 0.45f;

	// ───────────────────────────────────────────────── 表现层

	/** 事件播放间隔（秒）。设为 0 = 一键跳过动画（§13.2） */
	inline constexpr float EventPlaybackInterval = 0.12f;

	/**
	 * 六边形 tile 的世界尺寸（虚幻单位 cm）。尖顶六边形。
	 * 高 = 宽 × 2/√3。取宽 200 → 高 230.94。
	 */
	inline constexpr float TileWidth = 200.0f;
	inline constexpr float TileHeight = 230.94f;
}
