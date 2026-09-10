// Copyright Hex Spire. All Rights Reserved.

#include "View/HexUnitAppearance.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "HexSpire.h"

namespace
{
	// ── 模板动画目录（全部共用 S_Mannequin 骨架，108 骨骼）
	//
	// 已用 Tools/probe_template_assets.py 实测确认：
	//   SK_Mannequin / SK_Mannequin_Female / 01_Warden_Bined_UE4SK
	//   三者骨架完全一致，动画可直接互换，无需 IK 重定向。
	#define TPL_ANIM TEXT("/Game/TurnBasedStrategyRPGTemplate/Animations/")

	const TCHAR* P_Idle   = TPL_ANIM TEXT("AS_Idle.AS_Idle");
	const TCHAR* P_Walk   = TPL_ANIM TEXT("AS_Walk.AS_Walk");
	const TCHAR* P_Melee  = TPL_ANIM TEXT("AS_SwordsmanCombo1.AS_SwordsmanCombo1");
	const TCHAR* P_Cast   = TPL_ANIM TEXT("AS_MageAttack.AS_MageAttack");
	const TCHAR* P_Shoot  = TPL_ANIM TEXT("AS_ArcherShoot.AS_ArcherShoot");
	const TCHAR* P_Hit    = TPL_ANIM TEXT("AS_GetHit.AS_GetHit");
	const TCHAR* P_Die    = TPL_ANIM TEXT("AS_Die.AS_Die");

	#undef TPL_ANIM

	/** 玩家：镇妖者。用工程自有的 Warden 模型（已绑到同一骨架）。 */
	const FHexUnitAppearance GWarden = {
		TEXT("/Game/ArtResource/Character/Player/Warden/01_Warden_Bined_UE4SK.01_Warden_Bined_UE4SK"),
		P_Idle, P_Walk, P_Melee, P_Cast, P_Shoot, P_Hit, P_Die
	};

	/** 敌人：暂用模板 Mannequin 占位，等专属怪物模型到位后替换。 */
	const FHexUnitAppearance GEnemy = {
		TEXT("/Game/TurnBasedStrategyRPGTemplate/Meshes/SK_Mannequin.SK_Mannequin"),
		P_Idle, P_Walk, P_Melee, P_Cast, P_Shoot, P_Hit, P_Die
	};

	/**
	 * Boss / 精英：用女性 Mannequin。
	 *
	 * ⚠️ 这【不是】性别设定，纯粹是为了让 Boss 的剪影与杂兵不同。
	 *    模板只给了两个人形模型，在拿到专属模型之前，
	 *    "剪影可区分"比"设定正确"优先 —— 玩家必须一眼认出 Boss。
	 */
	const FHexUnitAppearance GBoss = {
		TEXT("/Game/TurnBasedStrategyRPGTemplate/Meshes/SK_Mannequin_Female.SK_Mannequin_Female"),
		P_Idle, P_Walk, P_Melee, P_Cast, P_Shoot, P_Hit, P_Die
	};
}

const TCHAR* FHexUnitAppearance::PathFor(EHexUnitAnim Anim) const
{
	switch (Anim)
	{
	case EHexUnitAnim::Idle:   return IdlePath;
	case EHexUnitAnim::Walk:   return WalkPath;
	case EHexUnitAnim::Attack: return AttackPath;
	case EHexUnitAnim::Cast:   return CastPath;
	case EHexUnitAnim::Shoot:  return ShootPath;
	case EHexUnitAnim::GetHit: return GetHitPath;
	case EHexUnitAnim::Die:    return DiePath;
	default:                   return IdlePath;
	}
}

namespace HexAppearance
{
	const FHexUnitAppearance& For(EHexTeam Team, bool bIsElite, bool bIsBoss)
	{
		if (Team == EHexTeam::Player)
		{
			return GWarden;
		}
		if (bIsBoss || bIsElite)
		{
			return GBoss;
		}
		return GEnemy;
	}

	USkeletalMesh* LoadMesh(const FHexUnitAppearance& Look)
	{
		if (!Look.SkeletalMeshPath)
		{
			return nullptr;
		}

		// ⚠️ 同步加载。单位数量很少（一场战斗 ≤ 10 个），
		//    且只在战斗开始时各加载一次，卡顿不可感知。
		//    异步加载会引入"模型晚一帧出现"的时序问题，不值得。
		USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, Look.SkeletalMeshPath);

		if (!Mesh)
		{
			// 资产被删/改名时走到这里。调用方会回退到灰盒，
			// 但必须留日志 —— 否则"角色不见了"会变成无头案。
			UE_LOG(LogHexSpire, Warning,
				TEXT("骨骼网格体加载失败，回退灰盒：%s"), Look.SkeletalMeshPath);
		}
		return Mesh;
	}

	UAnimSequence* LoadAnim(const FHexUnitAppearance& Look, EHexUnitAnim Anim)
	{
		const TCHAR* Path = Look.PathFor(Anim);
		if (!Path)
		{
			return nullptr;
		}

		UAnimSequence* Seq = LoadObject<UAnimSequence>(nullptr, Path);
		if (!Seq)
		{
			UE_LOG(LogHexSpire, Warning, TEXT("动画加载失败：%s"), Path);
		}
		return Seq;
	}
}
