// Copyright Hex Spire. All Rights Reserved.

#include "Battle/HexStatusData.h"

const FName FHexStatusLibrary::Strength   = TEXT("strength");
const FName FHexStatusLibrary::Dexterity  = TEXT("dexterity");
const FName FHexStatusLibrary::Weak       = TEXT("weak");
const FName FHexStatusLibrary::Vulnerable = TEXT("vulnerable");
const FName FHexStatusLibrary::Burn       = TEXT("burn");
const FName FHexStatusLibrary::Poison     = TEXT("poison");
const FName FHexStatusLibrary::Bleed      = TEXT("bleed");
const FName FHexStatusLibrary::Stun       = TEXT("stun");
const FName FHexStatusLibrary::Root       = TEXT("root");
const FName FHexStatusLibrary::Barrier    = TEXT("barrier");
const FName FHexStatusLibrary::Chill      = TEXT("chill");

namespace
{
	struct FStatusTable
	{
		TMap<FName, FHexStatusDef> Defs;
		TArray<FName> Order;
		FHexStatusDef Empty;

		void Add(const FHexStatusDef& D)
		{
			Defs.Add(D.Id, D);
			Order.Add(D.Id);
		}

		FStatusTable()
		{
			// ══════════════════════════════════════════════ 增益

			// 【力量】平坦加攻。
			// 走管线 ② 而非乘区，理由：§7.5 要求卡牌数值系数化，
			// 若力量做成乘区，它会与符文乘区叠乘，在后期 ATK 上百时爆炸。
			// 平坦加攻则随 ATK 增长而相对贬值，是自限制的。
			// 每层 +3：镇妖者基础 ATK 10，3 层力量 ≈ +90% 输出，够强但不失控。
			{
				FHexStatusDef D;
				D.Id = FHexStatusLibrary::Strength;
				D.DisplayName = TEXT("力量");
				D.StackMode = EHexStackMode::StackIntensity;
				D.TickTiming = EHexStatusTick::None;
				D.DecayPerRound = 0;      // 不衰减 —— 这是"滚雪球"型增益
				D.MaxStack = 99;
				D.bIsDebuff = false;
				D.FlatAtkPerStack = 3;
				Add(D);
			}

			// 【敏锐】平坦加格挡。与力量对称。
			// 每层 +2 而非 +3：格挡上限是 maxHP 的 25%（镇妖者 20 点），
			// 若每层 +3 则 7 层就撞上限，层数失去意义。
			{
				FHexStatusDef D;
				D.Id = FHexStatusLibrary::Dexterity;
				D.DisplayName = TEXT("敏锐");
				D.StackMode = EHexStackMode::StackIntensity;
				D.DecayPerRound = 0;
				D.MaxStack = 99;
				D.bIsDebuff = false;
				D.FlatBlockPerStack = 2;
				Add(D);
			}

			// 【护盾】独立于 block 的吸收池，回合结束不清空。
			// 与 block 分开的理由：block 每回合清空（镇妖者被动除外），
			// 而护盾是"存下来的资源"。混在一起会让镇妖者被动语义混乱。
			{
				FHexStatusDef D;
				D.Id = FHexStatusLibrary::Barrier;
				D.DisplayName = TEXT("护盾");
				D.StackMode = EHexStackMode::StackIntensity;
				D.DecayPerRound = 0;
				D.MaxStack = 999;
				D.bIsDebuff = false;
				D.bIsAbsorbShield = true;
				Add(D);
			}

			// ══════════════════════════════════════════════ 减益（乘区型）

			// 【虚弱】降低造成的伤害 25%。
			// 为什么是 25% 而不是 StS 的 25%（相同）：这是经过大量验证的数值，
			// 既明显可感知，又不会让"叠 4 层虚弱 = 免疫"。
			// 每回合 -1，最多 10 层 —— 层数上限防止"永久虚弱锁"。
			{
				FHexStatusDef D;
				D.Id = FHexStatusLibrary::Weak;
				D.DisplayName = TEXT("虚弱");
				D.StackMode = EHexStackMode::StackIntensity;
				D.DecayPerRound = -1;
				D.MaxStack = 10;
				D.bIsDebuff = true;
				// ⚠️ 注意：这里不按层数线性叠加乘区，
				//    实际结算时用 (1 - 0.25) 一次，层数只决定持续回合数。
				//    若按层线性叠加，4 层就变成免伤，破坏"永不免伤"原则（§4.4 ⑤）。
				D.DamageDealtMult = -0.25f;
				Add(D);
			}

			// 【易伤】增加受到的伤害 50%。
			// 比虚弱强（50% vs 25%）是有意的：进攻性 debuff 应该比防御性更有回报，
			// 这样"先上易伤再爆发"成为一条真实的连招思路（服务 D6 的组合发现）。
			{
				FHexStatusDef D;
				D.Id = FHexStatusLibrary::Vulnerable;
				D.DisplayName = TEXT("易伤");
				D.StackMode = EHexStackMode::StackIntensity;
				D.DecayPerRound = -1;
				D.MaxStack = 10;
				D.bIsDebuff = true;
				D.DamageTakenMult = 0.5f;
				Add(D);
			}

			// 【缓迟】移动力 -1。巨岩之躯（M 体型控场）用。
			{
				FHexStatusDef D;
				D.Id = FHexStatusLibrary::Chill;
				D.DisplayName = TEXT("缓迟");
				D.StackMode = EHexStackMode::StackIntensity;
				D.DecayPerRound = -1;
				D.MaxStack = 5;
				D.bIsDebuff = true;
				Add(D);
			}

			// ══════════════════════════════════════════════ 减益（持续伤害）

			// 【燃烧】回合结束每层 2 点，可被格挡吸收，每回合 -1 层。
			//
			// 数值推导：《点燃》1 费施加 3 层 → 总伤害 3+2+1=6 层·回合 × 2 = 12 点。
			// 对比《重击》2 费 ATK×1.8 = 18 点（ATK 10）。
			// 即 1 费 12 点 vs 2 费 18 点 —— 燃烧费效略高，但它是【延迟】伤害，
			// 且可被格挡吸收，敌人可能提前死掉导致浪费。这个交换是公平的。
			{
				FHexStatusDef D;
				D.Id = FHexStatusLibrary::Burn;
				D.DisplayName = TEXT("燃烧");
				D.StackMode = EHexStackMode::StackIntensity;
				D.TickTiming = EHexStatusTick::RoundEnd;
				D.DecayPerRound = -1;
				D.MaxStack = 99;
				D.bIsDebuff = true;
				D.TickDamagePerStack = 2;
				D.bTickIgnoresBlock = false;
				Add(D);
			}

			// 【中毒】回合结束每层 1 点，【无视格挡】，每回合 -1 层。
			//
			// 伤害比燃烧低一半，但无视格挡 —— 这是它存在的唯一理由：
			// 对付高格挡敌人（如石傀 DEF 6）的专用解。
			// 若中毒也能被格挡，它与燃烧就是同一张牌的弱化版，没有设计价值。
			{
				FHexStatusDef D;
				D.Id = FHexStatusLibrary::Poison;
				D.DisplayName = TEXT("中毒");
				D.StackMode = EHexStackMode::StackIntensity;
				D.TickTiming = EHexStatusTick::RoundEnd;
				D.DecayPerRound = -1;
				D.MaxStack = 99;
				D.bIsDebuff = true;
				D.TickDamagePerStack = 1;
				D.bTickIgnoresBlock = true;   // ⭐ 与燃烧的唯一区别
				Add(D);
			}

			// 【流血】移动时每层 2 点伤害。
			//
			// 这个状态是为六边形战场专门设计的：它把"位移"变成代价。
			// 对追击型敌人特别有效，也与《冲撞》等推拉卡形成组合
			// （推动敌人 → 触发流血）。这是 §4.1 五件套之"位移即伤害"的延伸。
			{
				FHexStatusDef D;
				D.Id = FHexStatusLibrary::Bleed;
				D.DisplayName = TEXT("流血");
				D.StackMode = EHexStackMode::StackIntensity;
				D.TickTiming = EHexStatusTick::None;  // 不按回合 tick
				D.DecayPerRound = -1;
				D.MaxStack = 99;
				D.bIsDebuff = true;
				D.DamageOnMovePerStack = 2;
				Add(D);
			}

			// ══════════════════════════════════════════════ 减益（行动限制）

			// 【眩晕】跳过下次行动。持续 1 回合，不可叠加层数。
			//
			// ⚠️ 用 RefreshDuration 而非 StackIntensity：
			//    眩晕若能叠层就等于"叠 3 层 = 锁死 3 回合"，
			//    这在只有 1 个玩家单位的游戏里是【单方面处刑】——
			//    敌人眩晕玩家 3 回合，玩家什么都做不了，纯挫败。
			//    所以眩晕只能刷新时长，且上限 1 回合。
			//    这是"打断敌人意图的唯一途径"（架构文档下一步优先级原话），
			//    对玩家是强力工具，对敌人则必须受限。
			{
				FHexStatusDef D;
				D.Id = FHexStatusLibrary::Stun;
				D.DisplayName = TEXT("眩晕");
				D.StackMode = EHexStackMode::RefreshDuration;
				D.DecayPerRound = -1;
				D.MaxStack = 1;
				D.bIsDebuff = true;
				D.bSkipTurn = true;
				Add(D);
			}

			// 【定身】不可移动，但仍可行动（攻击/施法）。
			// 比眩晕弱，因此允许 2 层。对风筝型敌人是强力反制。
			{
				FHexStatusDef D;
				D.Id = FHexStatusLibrary::Root;
				D.DisplayName = TEXT("定身");
				D.StackMode = EHexStackMode::RefreshDuration;
				D.DecayPerRound = -1;
				D.MaxStack = 2;
				D.bIsDebuff = true;
				D.bCannotMove = true;
				Add(D);
			}
		}
	};

	const FStatusTable& Table()
	{
		static const FStatusTable T;
		return T;
	}
}

const FHexStatusDef& FHexStatusLibrary::Get(FName Id)
{
	const FStatusTable& T = Table();
	if (const FHexStatusDef* Found = T.Defs.Find(Id))
	{
		return *Found;
	}
	return T.Empty;
}

bool FHexStatusLibrary::Exists(FName Id)
{
	return Table().Defs.Contains(Id);
}

const TArray<FName>& FHexStatusLibrary::AllIds()
{
	return Table().Order;
}

void FHexStatusInstance::Serialize(FArchive& Ar)
{
	Ar << Id;
	Ar << Stacks;
	Ar << Duration;
	Ar << AbsorbLeft;
}
