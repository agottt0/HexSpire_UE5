// Copyright Hex Spire. All Rights Reserved.
//
// 单位可视化 —— 表现层
//
// ══════════════════════════════════════════════════════════════════
// 骨骼模型接入后的结构（2026-09 改）
// ══════════════════════════════════════════════════════════════════
//   BodySkel    骨骼网格体 + 动画（模板资产，共用 S_Mannequin 骨架）
//   BodyMesh    灰盒圆柱 —— 【保留】作为资产加载失败时的回退
//   GroundRing  脚下光圈：队伍色 / 威胁等级 / debuff
//   FacingMesh  朝向指示器
//   HealthBar   生命条 + 格挡
//
// ══════════════════════════════════════════════════════════════════
// 为什么颜色信息移到"脚下光圈"，而不是给模型染色
// ══════════════════════════════════════════════════════════════════
// 灰盒时期，队伍色和 debuff 色是直接改圆柱的材质参数（"Color"）。
// 换成真模型后这条路走不通：
//   M_Warden / M_RedBody 这些美术材质【不保证有名为 Color 的参数】。
//   SetVectorParameterValue 在参数不存在时是【静默失败】的 ——
//   不报错、不警告，只是颜色纹丝不动。
//   结果就是"敌人中了易伤但看不出来"，而玩家算不清伤害就没法做决策。
//
// 而且给整个角色染色本身就会毁掉美术：模型一旦有了贴图，
// 再叠一层队伍色只会让它变成脏兮兮的单色块。
//
// 改用脚下光圈后：
//   · 颜色语言与美术资产【完全解耦】，换任何模型都不受影响
//   · 光圈用的是引擎 BasicShapeMaterial，Color 参数一定存在
//   · 俯视战棋里"脚下光圈"是通用做法（XCOM / 魔兽 / 星际都如此），
//     玩家不需要学习就能读懂
//
// 纪律 3（策划案 §12.1）：本文件只读逻辑状态，绝不修改。

#include "View/HexUnitVisual.h"
#include "View/HexBoardVisual.h"
#include "View/HexUnitAppearance.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"
#include "Battle/HexUnit.h"
#include "Hex/HexCoord.h"
#include "Core/HexSpireConstants.h"
#include "HexSpire.h"

namespace
{
	/** 引擎自带的圆柱：默认 100×100×100，中心在原点 */
	const TCHAR* CylinderPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const TCHAR* CubePath = TEXT("/Engine/BasicShapes/Cube.Cube");

	/**
	 * 引擎自带的基础材质。
	 * ⚠️ 用 BasicShapeMaterial 而不是自建材质 —— 它支持 Color 参数，
	 *    可以做成动态材质实例改颜色，且【不需要创建任何 .uasset】。
	 */
	const TCHAR* BasicMatPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

	/**
	 * Mannequin 的原始身高（uu）。实测值，见 probe_template_assets.py。
	 *
	 * ⚠️ 这个数必须准，它是所有体型缩放的分母。
	 *    如果直接 1:1 摆上去：183uu 的人站在 200uu 宽的格子上，
	 *    从 55° 俯视时会把后排格子挡得死死的 —— 而站位是本作的核心。
	 */
	constexpr float MannequinHeight = 183.0f;
}

AHexUnitVisual::AHexUnitVisual()
{
	// ⚠️ 需要 Tick 来做移动插值与朝向平滑。
	//    动画状态切换用 Timer（一次性动画播完回 Idle），不占 Tick。
	PrimaryActorTick.bCanEverTick = true;

	// ── 用空的 SceneComponent 作根
	//
	// ⚠️ 旧版把 BodyMesh（圆柱）当根，导致它的缩放会传递给所有子组件：
	//    血条得反向除掉父级缩放才能保持粗细一致，很容易算错。
	//    独立的根让每个组件的缩放互不干扰。
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	// ── 骨骼模型
	BodySkel = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("BodySkel"));
	BodySkel->SetupAttachment(Root);
	BodySkel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// 单节点模式：不用 AnimBlueprint，纯 C++ 调 PlayAnimation
	BodySkel->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	// ⚠️ 必须关掉，否则单位走出相机时动画停更，
	//    转回来会看到姿势突变（回合制里镜头是固定的，代价可忽略）
	BodySkel->VisibilityBasedAnimTickOption =
		EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	// ── 灰盒回退体
	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	BodyMesh->SetupAttachment(Root);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// ── 脚下光圈（承载全部颜色语言）
	GroundRing = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GroundRing"));
	GroundRing->SetupAttachment(Root);
	GroundRing->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	FacingMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Facing"));
	FacingMesh->SetupAttachment(Root);
	FacingMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	HealthBarBackMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HealthBack"));
	HealthBarBackMesh->SetupAttachment(Root);
	HealthBarBackMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	HealthBarMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HealthBar"));
	HealthBarMesh->SetupAttachment(Root);
	HealthBarMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(CylinderPath);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(CubePath);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMat(BasicMatPath);

	if (Cylinder.Succeeded())
	{
		BodyMesh->SetStaticMesh(Cylinder.Object);
		GroundRing->SetStaticMesh(Cylinder.Object);
	}
	if (Cube.Succeeded())
	{
		FacingMesh->SetStaticMesh(Cube.Object);
		HealthBarMesh->SetStaticMesh(Cube.Object);
		HealthBarBackMesh->SetStaticMesh(Cube.Object);
	}

	if (BasicMat.Succeeded())
	{
		BodyMaterial = UMaterialInstanceDynamic::Create(BasicMat.Object, this);
		RingMaterial = UMaterialInstanceDynamic::Create(BasicMat.Object, this);
		FacingMaterial = UMaterialInstanceDynamic::Create(BasicMat.Object, this);
		HealthMaterial = UMaterialInstanceDynamic::Create(BasicMat.Object, this);
		HealthBackMaterial = UMaterialInstanceDynamic::Create(BasicMat.Object, this);

		BodyMesh->SetMaterial(0, BodyMaterial);
		GroundRing->SetMaterial(0, RingMaterial);
		FacingMesh->SetMaterial(0, FacingMaterial);
		HealthBarMesh->SetMaterial(0, HealthMaterial);
		HealthBarBackMesh->SetMaterial(0, HealthBackMaterial);

		if (FacingMaterial)
		{
			FacingMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor::White);
		}
		if (HealthMaterial)
		{
			HealthMaterial->SetVectorParameterValue(
				TEXT("Color"), FLinearColor(0.15f, 0.85f, 0.25f));
		}
		if (HealthBackMaterial)
		{
			HealthBackMaterial->SetVectorParameterValue(
				TEXT("Color"), FLinearColor(0.08f, 0.02f, 0.02f));
		}
	}
}

void AHexUnitVisual::SizeForClass(EHexSizeClass SizeClass, float& OutRadius, float& OutHeight)
{
	// ⚠️ 体型必须【一眼可辨】（D8 的全部机制都建立在体型差异上）。
	//    半径按 footprint 覆盖范围给：
	//      S 占 1 格 → 略小于一格
	//      M 占 3 格（三角形）→ 约 1.6 格宽
	//      L 占 6 格 → 约 2.4 格宽
	//    高度差比半径差更夸张，因为俯视视角下高度差比宽度差更显眼。
	//
	// ⚠️ 高度在接入人形模型后【整体上调】过一次。
	//    原值（S=140）是按灰盒圆柱定的，圆柱是实心的、视觉重量足；
	//    换成人形后同样高度显得明显偏小 —— 人是"瘦"的，
	//    而且 55° 俯视会把高度压缩掉 sin(55°)≈0.82，
	//    140uu 的人投影后只剩 ~115uu，站在 200uu 宽的格子上像个玩具。
	switch (SizeClass)
	{
	case EHexSizeClass::M:
		OutRadius = HexK::TileWidth * 0.80f;
		OutHeight = 255.0f;
		break;
	case EHexSizeClass::L:
		OutRadius = HexK::TileWidth * 1.20f;
		OutHeight = 350.0f;
		break;
	case EHexSizeClass::S:
	default:
		OutRadius = HexK::TileWidth * 0.36f;
		OutHeight = 180.0f;
		break;
	}
}

FLinearColor AHexUnitVisual::ColorForTeam(EHexTeam Team, bool bIsElite, bool bIsBoss)
{
	if (Team == EHexTeam::Player)
	{
		// 镇妖者：青灰道袍（美术文档的东方怪谈基调）
		return FLinearColor(0.35f, 0.62f, 0.72f);
	}

	// 敌人按威胁等级分色 —— 玩家进房时要能立刻判断"这间房难不难"
	if (bIsBoss)
	{
		return FLinearColor(0.75f, 0.10f, 0.35f);   // 深红紫：Boss
	}
	if (bIsElite)
	{
		return FLinearColor(0.85f, 0.45f, 0.10f);   // 橙：精英
	}
	return FLinearColor(0.60f, 0.25f, 0.25f);       // 暗红：杂兵
}

// ══════════════════════════════════════════════════════════ 外观装配

void AHexUnitVisual::EnsureAppearance(const FHexUnit& Unit)
{
	if (bAppearanceReady)
	{
		return;
	}
	bAppearanceReady = true;

	Look = &HexAppearance::For(Unit.Team, Unit.bIsElite, Unit.bIsBoss);

	USkeletalMesh* Mesh = HexAppearance::LoadMesh(*Look);
	if (Mesh)
	{
		BodySkel->SetSkeletalMesh(Mesh);
		bUsingSkeletalMesh = true;

		// 骨骼模型顶上了 → 灰盒圆柱退场
		BodyMesh->SetVisibility(false);

		// 预载常用动画。Idle 与 Die 必须有，其余可缺。
		AnimIdle = HexAppearance::LoadAnim(*Look, EHexUnitAnim::Idle);
		AnimWalk = HexAppearance::LoadAnim(*Look, EHexUnitAnim::Walk);
		AnimGetHit = HexAppearance::LoadAnim(*Look, EHexUnitAnim::GetHit);
		AnimDie = HexAppearance::LoadAnim(*Look, EHexUnitAnim::Die);
		AnimAttack = HexAppearance::LoadAnim(*Look, EHexUnitAnim::Attack);

		// ⚠️ 成功时也要留一条日志。
		//    资产回退是【静默】的 —— 画面上只是"变成了灰盒"，
		//    如果不打日志，你会以为是自己没改对代码。
		UE_LOG(LogHexSpire, Display,
			TEXT("单位 %d 外观=骨骼模型 动画[idle=%d walk=%d hit=%d die=%d atk=%d]"),
			Unit.Id, AnimIdle != nullptr, AnimWalk != nullptr,
			AnimGetHit != nullptr, AnimDie != nullptr, AnimAttack != nullptr);

		PlayAnim(EHexUnitAnim::Idle);
	}
	else
	{
		// ⚠️ 回退到灰盒而不是留空。
		//    资产问题不该演变成"棋盘上什么都没有"——
		//    那会让人误以为是逻辑层出了 bug，排查方向完全跑偏。
		bUsingSkeletalMesh = false;
		BodyMesh->SetVisibility(true);
		BodySkel->SetVisibility(false);

		UE_LOG(LogHexSpire, Warning, TEXT("单位 %d 外观=灰盒回退"), Unit.Id);
	}
}

// ══════════════════════════════════════════════════════════ 动画

float AHexUnitVisual::PlayAnim(EHexUnitAnim Anim, bool bLooping)
{
	if (!bUsingSkeletalMesh)
	{
		return 0.0f;
	}

	UAnimSequence* Seq = nullptr;
	switch (Anim)
	{
	case EHexUnitAnim::Idle:   Seq = AnimIdle;   break;
	case EHexUnitAnim::Walk:   Seq = AnimWalk;   break;
	case EHexUnitAnim::Attack: Seq = AnimAttack; break;
	case EHexUnitAnim::GetHit: Seq = AnimGetHit; break;
	case EHexUnitAnim::Die:    Seq = AnimDie;    break;
	default: break;
	}

	// 缺哪个动画就退回 Idle —— 宁可动作不对，也不能变 T-pose。
	// T-pose 看起来像"模型坏了"，会把注意力引到错误的方向。
	if (!Seq)
	{
		Seq = AnimIdle;
		bLooping = true;
	}
	if (!Seq)
	{
		return 0.0f;
	}

	BodySkel->PlayAnimation(Seq, bLooping);
	CurrentAnim = Anim;

	// ⚠️ 时长直接问 AnimSequence 要，不要绕 GetSingleNodeInstance()。
	//    那个实例是 PlayAnimation 内部按需创建的，
	//    取它还得多 include 一个私有头，而且返回值可能为 null。
	return Seq->GetPlayLength();
}

void AHexUnitVisual::PlayOneShot(EHexUnitAnim Anim)
{
	if (!bUsingSkeletalMesh || bIsDeadVisual)
	{
		return;
	}

	const float Len = PlayAnim(Anim, false);

	// 播完切回 Idle。
	//
	// ⚠️ 用 Timer 而不是每帧查播放进度：
	//    单节点模式下动画播到末尾会【停在最后一帧】，
	//    没有"播放结束"事件可监听。轮询判断"是否接近末尾"
	//    在低帧率下会漏判，然后角色就永远僵在攻击姿势。
	if (Len > 0.0f)
	{
		GetWorldTimerManager().SetTimer(
			AnimResetTimer, this, &AHexUnitVisual::ReturnToIdle, Len, false);
	}
}

void AHexUnitVisual::ReturnToIdle()
{
	if (bIsDeadVisual)
	{
		return;
	}
	PlayAnim(EHexUnitAnim::Idle, true);
}

// ══════════════════════════════════════════════════════════ 同步

void AHexUnitVisual::SyncFromUnit(const FHexUnit& Unit, const AHexBoardVisual& Board)
{
	UnitId = Unit.Id;

	EnsureAppearance(Unit);

	float Radius = 0.0f;
	float Height = 0.0f;
	SizeForClass(Unit.SizeClass, Radius, Height);

	// ── 位置：footprint 的几何中心
	//
	// ⚠️ 不能直接用 Anchor —— M/L 体型的 anchor 是 footprint 的一个角，
	//    把模型放在 anchor 上会让它明显偏出自己占的格子。
	//    §8.2.1 的 footprint 是三角形，几何中心才是视觉正确的位置。
	TArray<FIntVector> Cells;
	Unit.GetCells(Cells);

	FVector Sum = FVector::ZeroVector;
	for (const FIntVector& C : Cells)
	{
		Sum += Board.CellToWorld(C);
	}
	const FVector Ground = Cells.Num() > 0
		? Sum / static_cast<float>(Cells.Num())
		: Board.CellToWorld(Unit.Anchor);

	// ⚠️ Actor 原点现在在【脚底】（骨骼模型的原点就在脚底）。
	//    旧版原点在圆柱中心，所以所有子组件的 Z 都是相对中心算的。
	//    改成脚底后，各组件的 Z 才能用"离地高度"直接表达。
	if (!bHasTargetLoc)
	{
		SetActorLocation(Ground);
		bHasTargetLoc = true;
	}
	TargetLocation = Ground;

	// ── 体型缩放
	if (!bInitialized || CachedSize != Unit.SizeClass)
	{
		if (bUsingSkeletalMesh)
		{
			// 按目标高度等比缩放。等比而非拉伸 —— 拉伸会让人形明显变形。
			const float S = Height / MannequinHeight;
			BodySkel->SetRelativeScale3D(FVector(S));
		}

		BodyMesh->SetRelativeScale3D(FVector(
			Radius * 2.0f / 100.0f,
			Radius * 2.0f / 100.0f,
			Height / 100.0f));
		// 灰盒圆柱中心在原点，抬半个身高才能站在地面上
		BodyMesh->SetRelativeLocation(FVector(0, 0, Height * 0.5f));

		// 光圈：贴地的扁圆盘，半径跟 footprint 走
		GroundRing->SetRelativeScale3D(FVector(
			Radius * 2.15f / 100.0f, Radius * 2.15f / 100.0f, 0.03f));
		GroundRing->SetRelativeLocation(FVector(0, 0, 3.0f));

		CachedSize = Unit.SizeClass;
		bInitialized = true;
	}

	// ── 颜色：全部落在脚下光圈上（见文件头说明）
	if (RingMaterial)
	{
		FLinearColor C = ColorForTeam(Unit.Team, Unit.bIsElite, Unit.bIsBoss);

		// ⚠️ 受到 debuff 时偏色 —— 灰盒期没有状态图标，
		//    但"这个敌人身上有 debuff"必须能看出来，否则玩家算不清伤害。
		if (Unit.GetDamageTakenMultiplier() > 0.01f)
		{
			// 易伤：偏紫
			C = FMath::Lerp(C, FLinearColor(0.85f, 0.20f, 0.85f), 0.35f);
		}
		if (Unit.GetDamageDealtMultiplier() < -0.01f)
		{
			// 虚弱：偏灰
			C = FMath::Lerp(C, FLinearColor(0.35f, 0.35f, 0.35f), 0.35f);
		}
		if (Unit.ShouldSkipTurn())
		{
			// 眩晕：偏黄（这是玩家最需要看到的状态 ——
			// 它意味着这个敌人本回合不会行动，威胁评估完全不同）
			C = FMath::Lerp(C, FLinearColor(0.95f, 0.90f, 0.20f), 0.55f);
		}

		RingMaterial->SetVectorParameterValue(TEXT("Color"), C);
	}
	// 灰盒回退时颜色仍走身体（此时没有模型可言）
	if (!bUsingSkeletalMesh && BodyMaterial)
	{
		BodyMaterial->SetVectorParameterValue(
			TEXT("Color"), ColorForTeam(Unit.Team, Unit.bIsElite, Unit.bIsBoss));
	}

	// ── 朝向
	//
	// ⚠️ 必须走 FHexCoord::FacingDir()，不能用 Dirs[Facing]（陷阱 H1）。
	{
		const FIntVector Dir = FHexCoord::FacingDir(Unit.Facing);
		const FVector2D From = FHexCoord::CubeToWorld2D(Unit.Anchor);
		const FVector2D To = FHexCoord::CubeToWorld2D(Unit.Anchor + Dir);
		FVector2D D2 = To - From;
		if (!D2.IsNearlyZero())
		{
			D2.Normalize();
		}

		// 有了真模型就【转模型本身】——
		// 这比任何指示器都直观，而且背击判定看的就是这个方向。
		TargetYaw = FMath::RadiansToDegrees(FMath::Atan2(D2.Y, D2.X));
		if (!bHasTargetYaw)
		{
			BodySkel->SetRelativeRotation(FRotator(0.0f, TargetYaw, 0.0f));
			bHasTargetYaw = true;
		}

		// 指示器保留：55° 俯视下人形的朝向仍然不够果断，
		// 而 §8.2.3 的背击要求玩家能精确判断朝向。放在贴地高度，不挡模型。
		const float Reach = Radius * 1.15f;
		FacingMesh->SetRelativeLocation(FVector(
			D2.X * Reach, D2.Y * Reach, 8.0f));
		FacingMesh->SetRelativeScale3D(FVector(0.26f, 0.26f, 0.06f));
	}

	// ── 生命条
	{
		const float Ratio = Unit.HPMax > 0
			? FMath::Clamp(static_cast<float>(Unit.HP) / Unit.HPMax, 0.0f, 1.0f)
			: 0.0f;

		const float BarWidth = FMath::Max(Radius * 2.0f, 80.0f);
		// 离地高度 = 身高 + 余量（原点已在脚底）
		const float BarZ = Height + 45.0f;

		HealthBarBackMesh->SetRelativeLocation(FVector(0, 0, BarZ));
		HealthBarBackMesh->SetRelativeScale3D(FVector(BarWidth / 100.0f, 0.14f, 0.06f));

		// 前景条按比例缩短，并左移保持左对齐
		HealthBarMesh->SetRelativeScale3D(
			FVector(BarWidth * Ratio / 100.0f, 0.16f, 0.08f));
		HealthBarMesh->SetRelativeLocation(FVector(
			-BarWidth * 0.5f * (1.0f - Ratio), 0, BarZ + 1.0f));

		if (HealthMaterial)
		{
			// 血量越低越红 —— 这是"还能撑几回合"的最快读数
			const FLinearColor Full(0.15f, 0.85f, 0.25f);
			const FLinearColor Low(0.90f, 0.15f, 0.10f);
			HealthMaterial->SetVectorParameterValue(
				TEXT("Color"), FMath::Lerp(Low, Full, Ratio));
		}

		// ── 格挡显示
		//
		// ⚠️ 格挡必须与生命分开显示（§4.4 ⑦：格挡先扣、溢出打 HP）。
		//    混在一条里玩家算不出"这一击会不会破防"。
		if (HealthBackMaterial)
		{
			const FLinearColor NoBlock(0.08f, 0.02f, 0.02f);
			const FLinearColor HasBlock(0.20f, 0.45f, 0.85f);
			const float BlockRatio = Unit.HPMax > 0
				? FMath::Clamp(static_cast<float>(Unit.Block) / Unit.HPMax, 0.0f, 1.0f)
				: 0.0f;
			HealthBackMaterial->SetVectorParameterValue(
				TEXT("Color"), FMath::Lerp(NoBlock, HasBlock, FMath::Min(1.0f, BlockRatio * 3.0f)));
		}
	}

	// ── 受击动画
	//
	// ⚠️ 这里【从状态差分推事件】，而不是让逻辑层来通知。
	//    理由是纪律 3：逻辑层不该知道表现层的存在，
	//    给它加一个"播受击动画"的回调就是反向依赖。
	//    表现层自己记住上一次的 HP，掉血就播 —— 完全自洽。
	if (bHasCachedHP && Unit.HP < CachedHP && Unit.bIsAlive)
	{
		PlayOneShot(EHexUnitAnim::GetHit);
	}
	CachedHP = Unit.HP;
	bHasCachedHP = true;

	if (!Unit.bIsAlive)
	{
		SetDead();
	}
}

void AHexUnitVisual::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// ── 移动插值
	//
	// ⚠️ 瞬移会让人看不出"谁动了、动到哪"。
	//    回合制里每次移动都是一次重要信息，必须让眼睛跟得上。
	if (bHasTargetLoc)
	{
		const FVector Cur = GetActorLocation();
		if (!Cur.Equals(TargetLocation, 1.0f))
		{
			// 速度取够快：动画好看和"等它走完"之间，后者更烦人
			const FVector New = FMath::VInterpConstantTo(
				Cur, TargetLocation, DeltaSeconds, 900.0f);
			SetActorLocation(New);

			if (bUsingSkeletalMesh && CurrentAnim != EHexUnitAnim::Walk
				&& !bIsDeadVisual)
			{
				PlayAnim(EHexUnitAnim::Walk, true);
			}
		}
		else if (bUsingSkeletalMesh && CurrentAnim == EHexUnitAnim::Walk)
		{
			PlayAnim(EHexUnitAnim::Idle, true);
		}
	}

	// ── 朝向平滑
	if (bHasTargetYaw && bUsingSkeletalMesh)
	{
		const FRotator Cur = BodySkel->GetRelativeRotation();
		if (!FMath::IsNearlyEqual(FRotator::NormalizeAxis(Cur.Yaw),
			FRotator::NormalizeAxis(TargetYaw), 0.5f))
		{
			const float NewYaw = FMath::FixedTurn(Cur.Yaw, TargetYaw, 720.0f * DeltaSeconds);
			BodySkel->SetRelativeRotation(FRotator(0.0f, NewYaw, 0.0f));
		}
	}
}

void AHexUnitVisual::SetDead()
{
	if (bIsDeadVisual)
	{
		return;
	}
	bIsDeadVisual = true;

	// 播死亡动画并停在最后一帧（单节点模式的默认行为）
	GetWorldTimerManager().ClearTimer(AnimResetTimer);

	if (bUsingSkeletalMesh && AnimDie)
	{
		PlayAnim(EHexUnitAnim::Die, false);
	}
	else
	{
		// 灰盒回退：压扁 + 变暗。
		// "这个单位已经死了"必须明确，否则玩家会把尸体当活的算威胁。
		const FVector S = BodyMesh->GetRelativeScale3D();
		BodyMesh->SetRelativeScale3D(FVector(S.X, S.Y, 0.08f));
		BodyMesh->SetRelativeLocation(FVector(0, 0, 4.0f));

		if (BodyMaterial)
		{
			BodyMaterial->SetVectorParameterValue(
				TEXT("Color"), FLinearColor(0.10f, 0.08f, 0.08f));
		}
	}

	// 光圈变暗留在地上 —— 标记"这里有尸体"，但不再参与威胁判断
	if (RingMaterial)
	{
		RingMaterial->SetVectorParameterValue(
			TEXT("Color"), FLinearColor(0.10f, 0.08f, 0.08f));
	}

	FacingMesh->SetVisibility(false);
	HealthBarMesh->SetVisibility(false);
	HealthBarBackMesh->SetVisibility(false);
}
