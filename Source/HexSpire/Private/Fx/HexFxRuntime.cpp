// Copyright Hex Spire. All Rights Reserved.

#include "Fx/HexFxRuntime.h"
#include "HexSpire.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "NiagaraSystem.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "Sound/SoundBase.h"
#include "Kismet/GameplayStatics.h"

const TCHAR* FHexFxRuntime::DefaultLibraryPath()
{
	return TEXT("/Game/HexSpire/Data/DA_FxLibrary.DA_FxLibrary");
}

bool FHexFxRuntime::LoadDefaultLibrary()
{
	Library = LoadObject<UHexFxLibraryAsset>(nullptr, DefaultLibraryPath());

	if (!Library)
	{
		// 库不存在不是错误 —— 只是还没开始配特效。
		// 用 Display 而不是 Warning：灰盒期这是常态，
		// 刷 Warning 会让真正的警告被淹没。
		UE_LOG(LogHexSpire, Display,
			TEXT("特效库未找到（全程不播特效）：%s"), DefaultLibraryPath());
		return false;
	}

	// ⚠️ 成功时把全部 key 打出来。
	//    这一条日志是排查"我配了特效但没播"的第一站 ——
	//    id 拼错时对照这份清单立刻能看出来，
	//    否则就得在编辑器里逐条点开对比。
	UE_LOG(LogHexSpire, Display,
		TEXT("特效库已加载：vfx=%d sfx=%d 兜底vfx=%d 兜底sfx=%d"),
		Library->Vfx.Num(), Library->Sfx.Num(),
		Library->FallbackByEvent.Num(), Library->FallbackSfxByEvent.Num());

	for (const TPair<FName, FHexVfxEntry>& P : Library->Vfx)
	{
		UE_LOG(LogHexSpire, Verbose, TEXT("  vfx: %s"), *P.Key.ToString());
	}
	for (const TPair<FName, FHexSfxEntry>& P : Library->Sfx)
	{
		UE_LOG(LogHexSpire, Verbose, TEXT("  sfx: %s"), *P.Key.ToString());
	}

	return true;
}

void FHexFxRuntime::Preload()
{
	if (!Library)
	{
		return;
	}

	// 同步加载全部软引用。
	// ⚠️ 这里刻意用同步而非异步：战斗开始前有天然的等待窗口
	//    （房间切换），而异步会引入"第一回合的特效还没加载好"的时序问题。
	int32 Loaded = 0;
	for (const TPair<FName, FHexVfxEntry>& P : Library->Vfx)
	{
		if (!P.Value.System.IsNull() && P.Value.System.LoadSynchronous())
		{
			++Loaded;
		}
	}
	for (const TPair<FName, FHexSfxEntry>& P : Library->Sfx)
	{
		if (!P.Value.Sound.IsNull() && P.Value.Sound.LoadSynchronous())
		{
			++Loaded;
		}
	}

	UE_LOG(LogHexSpire, Display, TEXT("特效预热完成：%d 个资产"), Loaded);
}

const FHexVfxEntry* FHexFxRuntime::ResolveVfx(
	FName VfxId, FName EventType, const TCHAR*& OutWhy) const
{
	if (!Library)
	{
		OutWhy = TEXT("库未加载");
		return nullptr;
	}

	// 内容显式配的优先
	if (!VfxId.IsNone())
	{
		if (const FHexVfxEntry* Found = Library->Vfx.Find(VfxId))
		{
			return Found;
		}

		// ⚠️ 配了 id 却查不到 = 拼错或漏配，这是【要告警】的情况。
		//    与"根本没配 id"区别对待：后者是灰盒期常态，不该刷日志。
		OutWhy = TEXT("id 不在库里（拼错或漏配）");
		return nullptr;
	}

	// 没配 id → 按事件类型兜底
	if (const FHexVfxEntry* Fb = Library->FallbackByEvent.Find(EventType))
	{
		return Fb;
	}

	OutWhy = nullptr;   // 没配也没兜底：正常情况，不告警
	return nullptr;
}

const FHexSfxEntry* FHexFxRuntime::ResolveSfx(
	FName SfxId, FName EventType, const TCHAR*& OutWhy) const
{
	if (!Library)
	{
		OutWhy = TEXT("库未加载");
		return nullptr;
	}

	if (!SfxId.IsNone())
	{
		if (const FHexSfxEntry* Found = Library->Sfx.Find(SfxId))
		{
			return Found;
		}
		OutWhy = TEXT("id 不在库里（拼错或漏配）");
		return nullptr;
	}

	if (const FHexSfxEntry* Fb = Library->FallbackSfxByEvent.Find(EventType))
	{
		return Fb;
	}

	OutWhy = nullptr;
	return nullptr;
}

void FHexFxRuntime::PlayVfx(
	UWorld* World,
	FName VfxId,
	FName EventType,
	const FVector& WorldCell,
	AActor* SourceActor,
	AActor* TargetActor)
{
	if (!World)
	{
		return;
	}

	if (!Library && !bWarnedNoLibrary)
	{
		bWarnedNoLibrary = true;
		UE_LOG(LogHexSpire, Display, TEXT("特效库未加载，本场不播特效"));
	}

	const TCHAR* Why = nullptr;
	const FHexVfxEntry* Entry = ResolveVfx(VfxId, EventType, Why);

	if (!Entry)
	{
		// Why 非空 = 这是配置错误，值得告警（且每个 id 只告一次）
		if (Why && !WarnedIds.Contains(VfxId))
		{
			WarnedIds.Add(VfxId);
			UE_LOG(LogHexSpire, Warning,
				TEXT("特效未播放：vfx='%s' 事件='%s' 原因=%s"),
				*VfxId.ToString(), *EventType.ToString(), Why);
		}
		return;
	}

	UNiagaraSystem* Sys = Entry->System.LoadSynchronous();
	if (!Sys)
	{
		if (!WarnedIds.Contains(VfxId))
		{
			WarnedIds.Add(VfxId);
			UE_LOG(LogHexSpire, Warning,
				TEXT("特效资产加载失败：vfx='%s' 路径=%s"),
				*VfxId.ToString(), *Entry->System.ToString());
		}
		return;
	}

	// ── 按挂点方式生成
	//
	// ⚠️ 挂点的 actor 为 nullptr 时【退回世界坐标】而不是不播。
	//    施法者可能已经死了（反伤致死），此时"特效不播"会让
	//    玩家看不到那次反伤发生过。
	AActor* AttachTo = nullptr;
	switch (Entry->Attach)
	{
	case EHexFxAttach::Source:
	case EHexFxAttach::SourceSocket:
		AttachTo = SourceActor;
		break;
	case EHexFxAttach::Target:
		AttachTo = TargetActor;
		break;
	case EHexFxAttach::TargetCell:
	default:
		AttachTo = nullptr;
		break;
	}

	const FVector Scale3D(Entry->Scale);

	if (AttachTo)
	{
		const FName Socket =
			(Entry->Attach == EHexFxAttach::SourceSocket) ? Entry->SocketName : NAME_None;

		UNiagaraComponent* Comp = UNiagaraFunctionLibrary::SpawnSystemAttached(
			Sys,
			AttachTo->GetRootComponent(),
			Socket,
			Entry->Offset,
			FRotator::ZeroRotator,
			EAttachLocation::KeepRelativeOffset,
			/*bAutoDestroy=*/true);

		if (Comp)
		{
			Comp->SetRelativeScale3D(Scale3D);
		}
	}
	else
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			World, Sys,
			WorldCell + Entry->Offset,
			FRotator::ZeroRotator,
			Scale3D,
			/*bAutoDestroy=*/true);
	}
}

void FHexFxRuntime::PlaySfx(
	UWorld* World,
	FName SfxId,
	FName EventType,
	const FVector& WorldCell,
	AActor* SourceActor)
{
	if (!World)
	{
		return;
	}

	const TCHAR* Why = nullptr;
	const FHexSfxEntry* Entry = ResolveSfx(SfxId, EventType, Why);

	if (!Entry)
	{
		if (Why && !WarnedIds.Contains(SfxId))
		{
			WarnedIds.Add(SfxId);
			UE_LOG(LogHexSpire, Warning,
				TEXT("音效未播放：sfx='%s' 事件='%s' 原因=%s"),
				*SfxId.ToString(), *EventType.ToString(), Why);
		}
		return;
	}

	USoundBase* Sound = Entry->Sound.LoadSynchronous();
	if (!Sound)
	{
		if (!WarnedIds.Contains(SfxId))
		{
			WarnedIds.Add(SfxId);
			UE_LOG(LogHexSpire, Warning,
				TEXT("音效资产加载失败：sfx='%s'"), *SfxId.ToString());
		}
		return;
	}

	if (Entry->bAttachToSource && SourceActor)
	{
		UGameplayStatics::SpawnSoundAttached(
			Sound, SourceActor->GetRootComponent(), NAME_None,
			FVector::ZeroVector, EAttachLocation::KeepRelativeOffset,
			/*bStopWhenAttachedToDestroyed=*/false,
			Entry->VolumeMultiplier, Entry->PitchMultiplier);
	}
	else
	{
		UGameplayStatics::PlaySoundAtLocation(
			World, Sound, WorldCell, FRotator::ZeroRotator,
			Entry->VolumeMultiplier, Entry->PitchMultiplier);
	}
}
