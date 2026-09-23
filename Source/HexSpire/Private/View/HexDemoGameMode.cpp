// Copyright Hex Spire. All Rights Reserved.

#include "View/HexDemoGameMode.h"
#include "View/HexBoardVisual.h"
#include "View/HexUnitVisual.h"
#include "View/HexDemoPlayerController.h"
#include "View/HexDemoHUD.h"
#include "Data/HexCardTableLoader.h"

#include "Run/HexRunState.h"
#include "Battle/HexBattleState.h"
#include "Battle/HexBattleFlow.h"
#include "Battle/HexEnemyAI.h"
#include "Battle/HexUnit.h"
#include "Content/HexContentLibrary.h"
#include "Content/HexLayouts.h"
#include "Map/HexFloorMap.h"
#include "Hex/HexCoord.h"
#include "Hex/HexFootprint.h"
#include "Rng/HexRngStreams.h"
#include "Core/HexSpireConstants.h"
#include "HexSpire.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "EngineUtils.h"

AHexDemoGameMode::AHexDemoGameMode()
{
	PrimaryActorTick.bCanEverTick = true;

	PlayerControllerClass = AHexDemoPlayerController::StaticClass();
	HUDClass = AHexDemoHUD::StaticClass();
	// 不需要 Pawn —— 这是纯鼠标操作的战棋，玩家没有"化身"
	DefaultPawnClass = nullptr;
}

AHexDemoGameMode::~AHexDemoGameMode() = default;

void AHexDemoGameMode::StartPlay()
{
	Super::StartPlay();

	// ── 卡牌配表：必须在 StartNewRun【之前】应用
	//
	// ⚠️ 顺序不能反。StartNewRun 会构造起始卡组，那时会按卡定义
	//    拷贝数值；配表若晚于它应用，本局的卡组仍是旧数值 ——
	//    表面上"改表没生效"，重开一局才对，极容易误判成配表坏了。
	//
	// ⚠️ 表不存在是正常情况（还没开始配表），此时全部走代码内建。
	//    见 FHexCardTableLoader 的说明。
	FHexCardTableLoader::ApplyDefaultTable();

	// ── 棋盘：优先复用关卡里已放置的那一个
	//
	// ⚠️ 这条"先找再建"很重要：
	//    早期版本无条件 spawn 一个新棋盘，于是你在编辑器里
	//    摆好位置、调好相机角度，一点 Play 全部作废 ——
	//    运行时用的是另一个刚 spawn 在原点的棋盘。
	//    现在关卡里的那个才是权威，编辑器里的调整立刻生效。
	{
		for (TActorIterator<AHexBoardVisual> It(GetWorld()); It; ++It)
		{
			Board = *It;
			break;
		}

		if (!Board)
		{
			// 关卡里没放（例如手工新建的空关卡）→ 兜底生成一个
			Board = GetWorld()->SpawnActor<AHexBoardVisual>(
				AHexBoardVisual::StaticClass(), FTransform::Identity);
			UE_LOG(LogHexSpire, Warning,
				TEXT("关卡里没有 HexBoardVisual，已在原点生成一个。"
					 "建议把它拖进关卡以便在编辑器里调整。"));
		}
	}

	SetupCamera();

	// 用时间做种子，每次启动都是新局
	StartNewRun(0);

	// ── 自检开关：-HexAutoRoom
	//
	// ⚠️ 存在的理由是【表现层没法用单元测试覆盖】。
	//    单位的模型/动画只在进入战斗后才装配，而进战斗需要点鼠标。
	//    没有这个开关，每次改表现层都得手动开编辑器点一遍，
	//    "资产悄悄回退成灰盒"这类问题就会漏到很后面才发现。
	//    加上它之后，一条命令行就能验证整条链路。
	if (FParse::Param(FCommandLine::Get(), TEXT("HexAutoRoom")))
	{
		TArray<FHexRoomChoice> Choices;
		GetRoomChoices(Choices);

		if (Choices.Num() > 0)
		{
			UE_LOG(LogHexSpire, Display,
				TEXT("[自检] 自动进入房间 %d"), Choices[0].RoomId);
			EnterRoom(Choices[0].RoomId);

			// ── 顺带选中第一张手牌
			//
			// ⚠️ 这不是调试残留，是【选中表现的唯一自动化入口】。
			//    选中表现现在是"卡牌上浮"（RenderTransform 位移），
			//    而它需要 NativeTick 每帧插值才会动。
			//    这条链路的失败模式全是静默的：
			//      · TickFrequency 被改成 Never → 一动不动
			//      · 外部又调了 SetRenderScale → 位移被覆盖，抖动
			//      · 上浮方向写成 +Y → 卡片沉到屏幕外
			//    三种情况都不报错，且截图未必抓得到 Slate 层。
			//    自动选一张牌之后，手牌控件的自检就能把上浮的
			//    实际位移量打进日志，命令行即可验证。
			if (const FHexBattleState* BS = GetBattleState())
			{
				const TArray<FHexCardInstance>& Hand = BS->Piles.GetHand();
				if (Hand.Num() > 0)
				{
					SelectCard(Hand[0].Uid);
					UE_LOG(LogHexSpire, Display,
						TEXT("[自检] 自动选中手牌 uid=%d"), Hand[0].Uid);
				}
			}
		}
		else
		{
			UE_LOG(LogHexSpire, Error, TEXT("[自检] 没有可进入的房间"));
		}
	}

	// ── 自检开关：-HexAutoReward
	//
	// ⚠️ 层结算三选一是【获得符文的唯一途径】，但它在自动化里
	//    一次都跑不到：试玩机器人击败 Boss 的比率是 0%，
	//    所以那段代码从未被执行过（"PASS" 只是因为没走到）。
	//    这个开关直接把局面推到层结算，让整条链路可被命令行验证：
	//      生成选项 → 显示面板 → 选择 → 真的装进槽位
	if (FParse::Param(FCommandLine::Get(), TEXT("HexAutoReward")))
	{
		RunState->GenerateFloorRewards(*Rng, PendingRewards);
		bAwaitingRewardChoice = PendingRewards.Num() > 0;

		UE_LOG(LogHexSpire, Display,
			TEXT("[自检] 层结算奖励 %d 项"), PendingRewards.Num());
		for (int32 I = 0; I < PendingRewards.Num(); ++I)
		{
			UE_LOG(LogHexSpire, Display, TEXT("[自检]   [%d] %s"),
				I + 1, *PendingRewards[I].DisplayName);
		}

		// ⚠️ 默认【只生成、不选择】，让奖励面板停在屏幕上 ——
		//    否则截图抓到的是选完之后的房间列表，看不到面板本身。
		//    要验证"选择"这一步，加 -HexAutoRewardPick。
		if (FParse::Param(FCommandLine::Get(), TEXT("HexAutoRewardPick")))
		{
			for (int32 I = 0; I < PendingRewards.Num(); ++I)
			{
				if (PendingRewards[I].Kind == FHexRewardOption::EKind::Rune)
				{
					const int32 Before = RunState->RuneLoadout.GetFilledCount();
					ChooseReward(I);
					UE_LOG(LogHexSpire, Display,
						TEXT("[自检] 选择奖励后：槽位 %d → %d，背包 %d 个"),
						Before, RunState->RuneLoadout.GetFilledCount(),
						RunState->RuneInventory.Num());
					break;
				}
			}
		}
		else
		{
			UE_LOG(LogHexSpire, Display,
				TEXT("[自检] 保留待选状态（加 -HexAutoRewardPick 可自动选择）"));
		}
	}
}

void AHexDemoGameMode::SetupCamera()
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	if (!PC)
	{
		return;
	}

	// ── 优先复用关卡里带 "HexDemoCamera" 标签的相机
	//
	// ⚠️ 同样是为了让编辑器里的调整生效：
	//    相机角度是这个游戏最需要手感微调的东西（美术文档 §5.3
	//    要求锁死视角，而"锁在哪个角度"只能靠肉眼试）。
	//    在编辑器里拖相机 → 点 Play 立刻看到结果，比改代码重编译快得多。
	for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(TEXT("HexDemoCamera")))
		{
			PC->SetViewTarget(*It);
			UE_LOG(LogHexSpire, Display, TEXT("使用关卡中的相机：%s"),
				*It->GetActorLabel());
			return;
		}
	}

	// ── 关卡里没有 → 按锁死参数生成一个
	//
	// 倾角 55° 是试出来的：
	//   再平（<45°）→ 后排格子被前排单位挡住，看不清 footprint
	//   再陡（>65°）→ 接近正俯视，单位的高度差（体型区分）消失
	const FVector Center = Board ? Board->GetBoardCenter() : FVector::ZeroVector;

	const float Pitch = -55.0f;
	const float Distance = 1750.0f;

	// 从棋盘正南方向后拉（+Y 是 row 增长方向，玩家在下、敌人在上）
	const FVector CamLoc = Center
		+ FVector(0.0f, -Distance * FMath::Cos(FMath::DegreesToRadians(-Pitch)),
			Distance * FMath::Sin(FMath::DegreesToRadians(-Pitch)));

	ACameraActor* Cam = GetWorld()->SpawnActor<ACameraActor>(
		ACameraActor::StaticClass(), CamLoc, FRotator(Pitch, 90.0f, 0.0f));

	if (Cam)
	{
		if (UCameraComponent* CC = Cam->GetCameraComponent())
		{
			CC->SetFieldOfView(60.0f);
		}
		PC->SetViewTarget(Cam);
	}
}

// ══════════════════════════════════════════════════════════ 局流程

void AHexDemoGameMode::StartNewRun(uint64 Seed)
{
	const uint64 MasterSeed = (Seed != 0)
		? Seed
		: static_cast<uint64>(FDateTime::Now().GetTicks());

	Rng = MakeUnique<FHexRngStreams>(MasterSeed);
	RunState = MakeUnique<FHexRunState>(MasterSeed);
	RunState->BeginRun(TEXT("warden"), *Rng);

	BattleState.Reset();
	BattleFlow.Reset();
	bInBattle = false;
	SelectedCardUid = 0;
	ClearUnitVisuals();

	UE_LOG(LogHexSpire, Display, TEXT("新的一局：seed=%llu"), MasterSeed);

	StatusMessage = FString::Printf(
		TEXT("第 %d 层 · 种子 %llu —— 按数字键选择房间进入"),
		RunState->FloorIndex, MasterSeed);

	// 地图阶段：棋盘先按空旷厅堂显示（占位），进房后按房间地形重建
	if (Board)
	{
		FHexGrid Placeholder;
		FHexLayouts::Build(TEXT("open_hall"), Placeholder);
		Board->BuildFromGrid(Placeholder);
		Board->ClearHighlights();
		Board->CommitHighlights();
	}
}

void AHexDemoGameMode::GetRoomChoices(TArray<FHexRoomChoice>& Out) const
{
	Out.Reset();
	if (!RunState)
	{
		return;
	}

	TArray<int32> Accessible;
	RunState->Map.GetAccessibleRooms(Accessible);

	// 补偿②：营地已发现时，Boss 候选集合会收窄
	TArray<int32> BossCandidates;
	RunState->Map.GetBossCandidates(BossCandidates);

	for (const int32 Id : Accessible)
	{
		const FHexRoomNode* R = RunState->Map.FindRoom(Id);
		if (!R)
		{
			continue;
		}

		FHexRoomChoice C;
		C.RoomId = Id;
		C.bTypeKnown = (R->Visibility == EHexRoomVisibility::Revealed
			|| R->Visibility == EHexRoomVisibility::Cleared);

		if (R->Visibility == EHexRoomVisibility::Cleared)
		{
			C.Label = TEXT("（已清空）");
		}
		else if (C.bTypeKnown)
		{
			switch (R->Type)
			{
			case EHexRoomType::Entrance: C.Label = TEXT("入口"); break;
			case EHexRoomType::Combat:   C.Label = TEXT("战斗"); break;
			case EHexRoomType::Elite:    C.Label = TEXT("精英"); break;
			case EHexRoomType::Camp:     C.Label = TEXT("营地"); break;
			case EHexRoomType::Boss:     C.Label = TEXT("★ BOSS"); break;
			default:                     C.Label = TEXT("房间"); break;
			}
		}
		else
		{
			// ⚠️ D4 的核心：类型未知时【不能】泄露它是什么房。
			//    但补偿②允许提示"它可能是 Boss"——
			//    这正是"找到营地"的奖励（见 FHexFloorMap::GetBossCandidates）。
			C.Label = BossCandidates.Contains(Id)
				? TEXT("？？？（邻接营地·可能是 BOSS）")
				: TEXT("？？？");
		}

		Out.Add(C);
	}
}

bool AHexDemoGameMode::EnterRoom(int32 RoomId)
{
	if (!RunState || bInBattle)
	{
		return false;
	}

	// ⚠️ 层结算未处理完不许进下一间房。
	//    不拦的话玩家直接点房间就把奖励绕过去了 ——
	//    而层结算三选一是获得符文的唯一途径，
	//    绕过一次就等于永久少一个符文，且没有任何提示。
	if (bAwaitingRewardChoice)
	{
		StatusMessage = TEXT("请先选择本层奖励（数字键选择，0 放弃）");
		return false;
	}

	if (!RunState->Map.EnterRoom(RoomId))
	{
		StatusMessage = TEXT("那间房去不了");
		return false;
	}

	const FHexRoomNode* R = RunState->Map.FindRoom(RoomId);
	if (!R)
	{
		return false;
	}

	// 已清空的房间：只是路过，不触发任何东西
	if (R->Visibility == EHexRoomVisibility::Cleared)
	{
		StatusMessage = TEXT("路过已清空的房间");
		return true;
	}

	if (R->Type == EHexRoomType::Camp)
	{
		RestAtCamp();
		return true;
	}

	if (R->IsCombat())
	{
		BeginBattleForRoom(RoomId);
		return true;
	}

	// 入口等非战斗房
	RunState->OnRoomCleared();
	StatusMessage = TEXT("这间房什么也没有");
	return true;
}

void AHexDemoGameMode::RestAtCamp()
{
	if (!RunState)
	{
		return;
	}

	const int32 Healed = RunState->RestAtCamp();
	StatusMessage = FString::Printf(
		TEXT("营地休息：回复 %d 点生命（%d/%d）。Boss 就在营地旁边。"),
		Healed, RunState->HeroHP, RunState->HeroHPMax);
}

// ══════════════════════════════════════════════════════════ 战斗

void AHexDemoGameMode::BeginBattleForRoom(int32 RoomId)
{
	const FHexRoomNode* Room = RunState->Map.FindRoom(RoomId);
	if (!Room)
	{
		return;
	}

	BattleState = MakeUnique<FHexBattleState>(
		RunState->GetMasterSeed() + static_cast<uint64>(RoomId) * 7919);

	// ── 地形
	FHexLayouts::Build(Room->LayoutId, BattleState->Grid);

	// ── 英雄
	const FHexHeroData* Hero = FHexContentLibrary::FindHero(RunState->HeroId);
	if (!Hero)
	{
		UE_LOG(LogHexSpire, Error, TEXT("找不到英雄 %s"), *RunState->HeroId.ToString());
		return;
	}

	{
		FHexUnit U;
		U.SourceId = Hero->Id;
		U.DisplayName = Hero->DisplayName;
		U.Team = EHexTeam::Player;
		U.SizeClass = Hero->SizeClass;

		// ⚠️ 生命【跨战斗继承】—— 这是肉鸽的核心压力来源。
		//    每场满血开打的话，"要不要多探一间"就没有风险了。
		U.HPMax = RunState->HeroHPMax;
		U.HP = RunState->HeroHP;

		// 属性 = 基线 + 装备加成
		U.ATK = Hero->BaseATK + RunState->EquipLoadout.GetStatBonus(EHexStat::ATK, Hero->BaseATK);
		U.DEF = Hero->BaseDEF + RunState->EquipLoadout.GetStatBonus(EHexStat::DEF, Hero->BaseDEF);
		U.AGI = Hero->BaseAGI + RunState->EquipLoadout.GetStatBonus(EHexStat::AGI, Hero->BaseAGI);
		U.LUK = Hero->BaseLUK + RunState->EquipLoadout.GetStatBonus(EHexStat::LUK, Hero->BaseLUK);
		U.CRIT = Hero->BaseCRIT + RunState->EquipLoadout.GetStatBonus(EHexStat::CRIT, Hero->BaseCRIT);

		U.Anchor = FHexCoord::OffsetToCube(HexK::HeroSpawnCol, HexK::HeroSpawnRow);
		U.Facing = HexK::HeroSpawnFacing;

		BattleState->HeroUnitId = BattleState->AddUnit(U);
	}

	BattleState->HeroEnergyMaxBase = Hero->EnergyMax;
	BattleState->HeroDrawBase = Hero->CardsDrawnPerTurn;
	BattleState->DeckCapacityBase = RunState->DeckCapacity;

	// ── 符文 / 装备 / 被动
	BattleState->RuneLoadout = RunState->RuneLoadout;
	BattleState->EquipLoadout = RunState->EquipLoadout;
	BattleState->HeroPassiveRules = Hero->PassiveRules;

	// ── 敌人布阵
	{
		TArray<FHexEncounterEntry> Entries;
		FHexContentLibrary::GetEncounter(Room->EncounterId, Entries);

		int32 Col = 2;
		int32 Row = HexK::EnemySpawnRowMax;

		for (const FHexEncounterEntry& E : Entries)
		{
			const FHexEnemyData* Data = FHexContentLibrary::FindEnemy(E.EnemyId);
			if (!Data)
			{
				continue;
			}

			for (int32 I = 0; I < E.Count; ++I)
			{
				FHexUnit U = FHexContentLibrary::MakeEnemyUnit(
					*Data, RunState->FloorIndex, RunState->Corruption);
				U.Facing = 5;   // 朝下（面向玩家）

				// 找一个能放下它 footprint 的位置
				bool bPlaced = false;
				for (int32 Attempt = 0; Attempt < 60 && !bPlaced; ++Attempt)
				{
					const FIntVector Anchor = FHexCoord::OffsetToCube(Col, Row);
					U.Anchor = Anchor;

					// ⚠️ 必须走 CanPlace（R9：所有生成/位移/转向的唯一出口），
					//    即使是 S 体型也不走快路径。
					const FHexSizeClassDef& SizeDef =
						FHexFootprint::GetSizeDef(U.SizeClass);

					if (FHexFootprint::CanPlace(
							BattleState->Grid, Anchor, SizeDef.Footprint,
							U.Facing, /*IgnoreUnitId=*/-1, SizeDef.bCanCrushRubble))
					{
						BattleState->AddUnit(U);
						bPlaced = true;
					}

					// 蛇形扫过敌方生成区
					Col += 2;
					if (Col > HexK::BoardCols)
					{
						Col = 2;
						--Row;
						if (Row < HexK::EnemySpawnRowMin)
						{
							Row = HexK::EnemySpawnRowMax;
							Col = 3;
						}
					}
				}

				if (!bPlaced)
				{
					UE_LOG(LogHexSpire, Warning,
						TEXT("放不下敌人 %s（体型 %d）—— 地形太窄"),
						*E.EnemyId.ToString(), static_cast<int32>(U.SizeClass));
				}
			}
		}
	}

	BattleState->RebuildOccupancy();

	// ── 卡组（含装备注入的衍生卡）
	{
		TArray<FHexCardInstance> Deck = RunState->Deck;

		TArray<FName> Injected;
		RunState->EquipLoadout.GetInjectedCardIds(Injected);
		for (const FName& Cid : Injected)
		{
			FHexCardInstance Inst;
			Inst.Uid = NextRuntimeUid++;
			Inst.CardId = Cid;
			Deck.Add(Inst);
		}

		BattleState->Piles.BeginBattle(Deck, BattleState->Rng);
	}

	// ── 固定卡：直接搬过来，【不】参与洗牌
	//    它们常驻整场战斗，屏幕左侧固定卡区就是读的这个数组。
	BattleState->FixedCards = RunState->FixedCards;

	// ── 流程
	BattleFlow = MakeUnique<FHexBattleFlow>(*BattleState);
	BattleFlow->SetCardLookup([](FName Id) { return FHexContentLibrary::FindCard(Id); });
	BattleFlow->BeginBattle();

	bInBattle = true;
	SelectedCardUid = 0;

	// ⚠️ 战斗中锁定符文重排（§6.5）。
	//    不锁的话玩家可以每次出牌前重排一遍去找最优顺序，
	//    "顺序即策略"就退化成"每回合手动最优化"，
	//    既破坏节奏也让符文顺序失去决策意义。
	RunState->bRuneLayoutLocked = true;

	// 地形重建 + 单位生成
	ClearUnitVisuals();
	if (Board)
	{
		Board->BuildFromGrid(BattleState->Grid);
	}
	RefreshVisuals();

	const FString RoomName = (Room->Type == EHexRoomType::Boss)
		? TEXT("BOSS 战")
		: (Room->Type == EHexRoomType::Elite ? TEXT("精英战") : TEXT("战斗"));

	// ⚠️ 必须在这里记下房间类型。
	//    战斗结束时房间已被标记 Cleared，那时再查会判不出"刚打的是 Boss"，
	//    层结算就永远不会触发（而且不报错）。
	CurrentRoomType = Room->Type;

	StatusMessage = FString::Printf(
		TEXT("%s 开始！腐蚀度 %d —— 数字键选手牌 / QWE 选左侧固定卡，再点目标格；空格结束回合"),
		*RoomName, RunState->Corruption);
}

bool AHexDemoGameMode::PlayCard(int32 CardUid, const FIntVector& TargetCell)
{
	if (!bInBattle || !BattleFlow)
	{
		return false;
	}

	// ⚠️ 必须在 PlayCard 之【前】取卡牌类型。
	//    打出后这张卡就离开手牌进了弃牌堆（或被消耗），
	//    事后再查会查不到 —— 然后动画就永远不播，
	//    而且不报任何错，很难想到是时序问题。
	EHexCardType PlayedType = EHexCardType::Skill;
	bool bKnowType = false;
	if (BattleState)
	{
		for (const FHexCardInstance& C : BattleState->Piles.GetHand())
		{
			if (C.Uid == CardUid)
			{
				if (const FHexCardData* Def = FHexContentLibrary::FindCard(C.CardId))
				{
					PlayedType = Def->CardType;
					bKnowType = true;
				}
				break;
			}
		}
	}

	const EHexPlayResult R = BattleFlow->PlayCard(CardUid, TargetCell);

	switch (R)
	{
	case EHexPlayResult::Success:
		SelectedCardUid = 0;
		CachedLegalTargets.Reset();
		RefreshVisuals();
		if (bKnowType)
		{
			PlayHeroCardAnim(PlayedType);
		}
		return true;

	case EHexPlayResult::NotEnoughEnergy:
		StatusMessage = TEXT("体力不足");
		break;
	case EHexPlayResult::IllegalTarget:
		StatusMessage = TEXT("目标不合法");
		break;
	case EHexPlayResult::NotPlayerPhase:
		StatusMessage = TEXT("不是你的回合");
		break;
	case EHexPlayResult::CardNotInHand:
	case EHexPlayResult::CardNotFound:
		StatusMessage = TEXT("这张牌不在手上");
		break;
	default:
		break;
	}

	return false;
}

void AHexDemoGameMode::EndTurn()
{
	if (!bInBattle || !BattleFlow)
	{
		return;
	}

	SelectedCardUid = 0;
	CachedLegalTargets.Reset();

	BattleFlow->EndPlayerTurn();
	RefreshVisuals();

	if (BattleFlow->IsBattleOver())
	{
		if (BattleState->Phase == EHexBattlePhase::BattleLose)
		{
			StatusMessage = TEXT("你死了。按 R 重开一局。");
		}
		else
		{
			StatusMessage = TEXT("战斗胜利！按 Enter 结算并回到地图。");
		}
	}
}

bool AHexDemoGameMode::IsBattleOver() const
{
	return BattleFlow.IsValid() && BattleFlow->IsBattleOver();
}

bool AHexDemoGameMode::IsPlayerDefeated() const
{
	return BattleState.IsValid()
		&& BattleState->Phase == EHexBattlePhase::BattleLose;
}

void AHexDemoGameMode::FinishBattleAndReturnToMap()
{
	if (!bInBattle || !BattleState || !RunState)
	{
		return;
	}

	// ── 生命回写（跨战斗继承）
	if (const FHexUnit* Hero = BattleState->GetHero())
	{
		RunState->HeroHP = FMath::Max(0, Hero->HP);
	}

	// ── 卡组归还（含【消耗】的卡 —— 消耗只在本场生效）
	{
		TArray<FHexCardInstance> Full;
		BattleState->Piles.EndBattle(Full);

		// 剔除装备注入的衍生卡：它们随装备来，不属于卡组
		TArray<FName> Injected;
		RunState->EquipLoadout.GetInjectedCardIds(Injected);

		RunState->Deck.Reset();
		for (const FHexCardInstance& C : Full)
		{
			if (!Injected.Contains(C.CardId))
			{
				RunState->Deck.Add(C);
			}
		}
	}

	// ── 腐蚀度 + 统计
	const int32 Delta = RunState->OnRoomCleared();

	// ── 层结算：打赢 Boss → 生成三选一奖励（§6.6 / §9.8）
	//
	// ⚠️ 这一段原先【完全不存在】：GenerateFloorRewards 只被
	//    试玩 commandlet 调用过（而且是无脑取 Rewards[0]）。
	//    对真实玩家而言，打赢 Boss 拿不到任何符文 ——
	//    符文系统做得再完整也没有入口，整个 D6 是死的。
	{
		const bool bWasBoss = (CurrentRoomType == EHexRoomType::Boss);
		if (bWasBoss)
		{
			RunState->GenerateFloorRewards(*Rng, PendingRewards);
			bAwaitingRewardChoice = PendingRewards.Num() > 0;
		}
	}

	bInBattle = false;
	BattleFlow.Reset();
	BattleState.Reset();
	ClearUnitVisuals();

	// 回到地图 → 解锁符文重排（§6.5 战斗外自由）
	RunState->bRuneLayoutLocked = false;

	if (Board)
	{
		Board->ClearHighlights();
		Board->CommitHighlights();
	}

	if (bAwaitingRewardChoice)
	{
		StatusMessage = FString::Printf(
			TEXT("★ BOSS 已倒下！层结算 —— 按数字键选择一项奖励（共 %d 项），按 0 全部放弃"),
			PendingRewards.Num());
	}
	else
	{
		StatusMessage = FString::Printf(
			TEXT("清空！腐蚀度 +%d（现 %d）· 生命 %d/%d —— 选择下一间房"),
			Delta, RunState->Corruption, RunState->HeroHP, RunState->HeroHPMax);
	}
}

// ══════════════════════════════════════════════════════════ 层结算

void AHexDemoGameMode::ChooseReward(int32 Index)
{
	if (!bAwaitingRewardChoice || !RunState)
	{
		return;
	}

	if (!PendingRewards.IsValidIndex(Index))
	{
		DeclineRewards();
		return;
	}

	const FHexRewardOption Chosen = PendingRewards[Index];
	const bool bOk = RunState->ApplyReward(Chosen, *Rng);

	PendingRewards.Reset();
	bAwaitingRewardChoice = false;

	if (!bOk)
	{
		StatusMessage = TEXT("那项奖励无法应用（可能卡组已满）—— 已跳过");
		return;
	}

	// ⚠️ 符文奖励在满槽时会进背包而不是直接装上（ApplyReward 的语义）。
	//    必须告诉玩家这件事，否则他会以为奖励丢了 ——
	//    §6.6 要求"三选一界面下方显示当前 6 槽让玩家指定覆盖目标"，
	//    第一版先用文字说明 + EquipRuneFromInventory 供后续 UI 调用。
	if (Chosen.Kind == FHexRewardOption::EKind::Rune)
	{
		const bool bInBag = RunState->RuneInventory.Contains(Chosen.ContentId);
		StatusMessage = bInBag
			? FString::Printf(
				TEXT("获得符文《%s》—— 6 槽已满，已放入背包（需替换某个槽位才能生效）"),
				*Chosen.DisplayName)
			: FString::Printf(TEXT("获得符文《%s》，已装入空槽"), *Chosen.DisplayName);
	}
	else
	{
		StatusMessage = FString::Printf(TEXT("已获得：%s"), *Chosen.DisplayName);
	}
}

void AHexDemoGameMode::DeclineRewards()
{
	if (!bAwaitingRewardChoice)
	{
		return;
	}
	PendingRewards.Reset();
	bAwaitingRewardChoice = false;
	StatusMessage = TEXT("已放弃本层奖励 —— 选择下一间房");
}

// ══════════════════════════════════════════════════════════ 选择与高亮

void AHexDemoGameMode::SelectCard(int32 CardUid)
{
	SelectedCardUid = CardUid;
	CachedLegalTargets.Reset();

	if (bInBattle && BattleFlow && CardUid != 0)
	{
		BattleFlow->GetLegalTargets(CardUid, CachedLegalTargets);

		if (CachedLegalTargets.Num() == 0)
		{
			StatusMessage = TEXT("这张牌现在没有合法目标");
		}
	}

	RefreshHighlights();
}

void AHexDemoGameMode::ClearSelection()
{
	SelectedCardUid = 0;
	CachedLegalTargets.Reset();
	RefreshHighlights();
}

void AHexDemoGameMode::SetHoverCell(const FIntVector& Cell)
{
	if (bHasHover && HoverCell == Cell)
	{
		return;
	}
	HoverCell = Cell;
	bHasHover = true;
	RefreshHighlights();
}

void AHexDemoGameMode::RefreshHighlights()
{
	if (!Board)
	{
		return;
	}

	Board->ClearHighlights();

	if (!bInBattle || !BattleState)
	{
		Board->CommitHighlights();
		return;
	}

	// ── §13.2 硬需求 3：自身 footprint 描边
	if (const FHexUnit* Hero = BattleState->GetHero())
	{
		TArray<FIntVector> Cells;
		Hero->GetCells(Cells);
		Board->AddHighlight(Cells, EHexHighlight::SelfFootprint);
	}

	// ── §13.2 硬需求 2：敌人意图必须区分「可躲」与「追踪」
	for (const FHexUnit& U : BattleState->GetUnits())
	{
		if (U.Team != EHexTeam::Enemy || !U.bIsAlive || !U.Intent.IsValid())
		{
			continue;
		}
		// 眩晕的敌人不会行动，画意图会误导玩家
		if (U.ShouldSkipTurn())
		{
			continue;
		}

		const bool bTracking =
			(U.Intent.Targeting == EHexIntentTargeting::TrackTarget);

		if (U.Intent.Kind == EHexIntentKind::Attack
			|| U.Intent.Kind == EHexIntentKind::MultiAttack)
		{
			if (bTracking)
			{
				// 追踪型：画在【玩家当前位置】——它跟着走
				if (const FHexUnit* Tracked = BattleState->FindUnit(U.Intent.TrackedUnitId))
				{
					TArray<FIntVector> Cells;
					Tracked->GetCells(Cells);
					Board->AddHighlight(Cells, EHexHighlight::IntentTracking);
				}
			}
			else
			{
				// 可躲型：画在【冻结的目标格】——走开就打空
				Board->AddHighlight(U.Intent.TargetCells, EHexHighlight::IntentDodgeable);
			}
		}
		else if (U.Intent.Kind == EHexIntentKind::Move)
		{
			// 移动意图：显示它要去哪（让玩家能预判堵路）
			TArray<FIntVector> Dest;
			FHexFootprint::Cells(
				U.Intent.MoveToAnchor, U.SizeClass, U.Intent.MoveToFacing, Dest);
			if (Dest.Num() > 0)
			{
				Board->AddHighlight(Dest, EHexHighlight::MoveTarget);
			}
		}
	}

	// ── 选中卡的合法目标（§13.2 硬需求 3：移动落点预览）
	if (SelectedCardUid != 0)
	{
		Board->AddHighlight(CachedLegalTargets, EHexHighlight::LegalTarget);

		// 悬停时显示波及范围（§13.2 硬需求 1 的空间部分）
		if (bHasHover && CachedLegalTargets.Contains(HoverCell) && BattleFlow)
		{
			TArray<FIntVector> Affected;
			BattleFlow->GetAffectedCells(SelectedCardUid, HoverCell, Affected);
			Board->AddHighlight(Affected, EHexHighlight::AffectedArea);
		}
	}

	if (bHasHover)
	{
		Board->AddHighlight(HoverCell, EHexHighlight::Hover);
	}

	Board->CommitHighlights();
}

// ══════════════════════════════════════════════════════════ 可视化同步

void AHexDemoGameMode::PlayHeroCardAnim(EHexCardType Type)
{
	// ⚠️ 为什么"出手"必须由流程层显式触发，而"受击"不用：
	//    受击可以从掉血差分出来（表现层自己比对上一次的 HP）。
	//    但出手不行 —— 打空、被闪避、纯 buff 卡时没有任何状态变化，
	//    表现层无从得知玩家刚刚做了什么。
	if (!BattleState)
	{
		return;
	}

	AHexUnitVisual** V = UnitVisuals.Find(BattleState->HeroUnitId);
	if (!V || !*V)
	{
		return;
	}

	switch (Type)
	{
	case EHexCardType::Attack:
		(*V)->PlayOneShot(EHexUnitAnim::Attack);
		break;
	case EHexCardType::Guard:
		// 守备暂时也用挥击动作 —— 模板的 AS_Defend 是"举盾站定"的
		// 循环姿态，一次性播完会立刻弹回 Idle，看起来像抽搐。
		// 要用它得改成"保持到回合结束"，属于状态而非动作，先不做。
		(*V)->PlayOneShot(EHexUnitAnim::Attack);
		break;
	default:
		(*V)->PlayOneShot(EHexUnitAnim::Cast);
		break;
	}
}

void AHexDemoGameMode::ClearUnitVisuals()
{
	for (const TPair<int32, AHexUnitVisual*>& P : UnitVisuals)
	{
		if (P.Value)
		{
			P.Value->Destroy();
		}
	}
	UnitVisuals.Reset();
}

void AHexDemoGameMode::RefreshVisuals()
{
	if (!Board || !BattleState)
	{
		return;
	}

	for (const FHexUnit& U : BattleState->GetUnits())
	{
		AHexUnitVisual** Found = UnitVisuals.Find(U.Id);
		AHexUnitVisual* V = Found ? *Found : nullptr;

		if (!V)
		{
			V = GetWorld()->SpawnActor<AHexUnitVisual>(
				AHexUnitVisual::StaticClass(),
				Board->CellToWorld(U.Anchor), FRotator::ZeroRotator);
			if (!V)
			{
				continue;
			}
			UnitVisuals.Add(U.Id, V);
		}

		V->SyncFromUnit(U, *Board);
	}

	RefreshHighlights();
}

void AHexDemoGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// ⚠️ 逻辑已经瞬时算完，这里只做"把逻辑状态搬到画面上"。
	//    每帧同步是最省心的做法：不需要维护"哪些东西变了"的脏标记，
	//    而单位数量只有个位数，开销可以忽略。
	if (bInBattle && BattleState)
	{
		for (const FHexUnit& U : BattleState->GetUnits())
		{
			if (AHexUnitVisual** V = UnitVisuals.Find(U.Id))
			{
				if (*V)
				{
					(*V)->SyncFromUnit(U, *Board);
				}
			}
		}

		// 事件日志：目前只清空（灰盒期不播动画）。
		// 接入动画时在这里 Drain 并逐条播放。
		TArray<FHexBattleEvent> Events;
		BattleState->DrainEvents(Events);
	}
}
