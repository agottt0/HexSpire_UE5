// Copyright Hex Spire. All Rights Reserved.

#include "Data/HexUnitVisualSet.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "HexSpire.h"

USkeletalMesh* UHexUnitVisualSet::LoadMesh() const
{
	// 未配置不是错误 —— 灰盒期大部分单位还没有模型，
	// 调用方回退到占位几何体。这里报 Warning 会让日志被噪声淹没。
	if (SkeletalMesh.IsNull())
	{
		return nullptr;
	}

	// ⚠️ 同步加载。单位数量很少（一场战斗 ≤ 10 个）且只在战斗开始时
	//    各加载一次，卡顿不可感知。异步加载会引入"模型晚一帧出现"
	//    的时序问题 —— 那会让单位的第一个动画播在还没有 mesh 的组件上。
	USkeletalMesh* Mesh = SkeletalMesh.LoadSynchronous();

	if (!Mesh)
	{
		// 配了路径但加载失败 —— 资产被删或改名。这个必须报：
		// 否则"角色不见了"会变成无头案（配置看起来是对的）。
		UE_LOG(LogHexSpire, Warning,
			TEXT("单位 %s 的骨骼网格体加载失败，回退灰盒：%s"),
			*UnitId.ToString(), *SkeletalMesh.ToString());
	}
	return Mesh;
}

UAnimSequence* UHexUnitVisualSet::LoadAnim(EHexUnitAnim Anim) const
{
	// ⚠️ 回退到 Idle 而不是返回 nullptr。
	//    "内容没声明这个状态"在灰盒期是常态，不是错误 ——
	//    当错误处理会让日志淹没，真正的缺资产反而看不见。
	//    这条约定与旧的 FHexUnitAppearance::PathFor 一致，
	//    两边行为必须相同，否则迁移期会出现"换了资产动作就变了"。
	const TSoftObjectPtr<UAnimSequence>* Found = Anims.Find(Anim);

	if (!Found || Found->IsNull())
	{
		if (Anim == EHexUnitAnim::Idle)
		{
			// Idle 自己都没配 → 没有任何可播的，交给调用方决定
			return nullptr;
		}
		return LoadAnim(EHexUnitAnim::Idle);
	}

	UAnimSequence* Seq = Found->LoadSynchronous();

	if (!Seq)
	{
		UE_LOG(LogHexSpire, Warning,
			TEXT("单位 %s 的动画加载失败：%s"),
			*UnitId.ToString(), *Found->ToString());

		// 加载失败也回退 Idle —— 但要避免 Idle 自己失败时无限递归
		if (Anim != EHexUnitAnim::Idle)
		{
			return LoadAnim(EHexUnitAnim::Idle);
		}
	}
	return Seq;
}
