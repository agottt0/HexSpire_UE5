// Copyright Hex Spire. All Rights Reserved.
//
// 特效 / 音效的运行时播放入口 —— 表现层
//
// 职责边界：只负责「给我一个 id 和一个位置，我把它播出来」。
// 它不知道战斗规则，也不决定什么时候播 —— 那是 FHexVisualQueue 的事。
//
// ══════════════════════════════════════════════════════════════════
// 为什么播放失败一律「不播 + 记日志」，而不是断言或回退到占位特效
// ══════════════════════════════════════════════════════════════════
// 灰盒期资产必然不齐。若缺资产会崩或会卡住流程，那么在美术交图之前
// 游戏根本跑不起来 —— 而验证战斗逻辑恰恰不需要特效。
//
// 也不回退到「占位特效」：一个显眼的粉色方块会让每一次配置遗漏
// 都变成视觉噪声，而策划分不清"这是没配"还是"这就是设计"。
// 静默不播 + 日志告警的组合，让缺失只出现在需要它的人眼前。
//
// ⚠️ 但【每一次回退都要留日志】。静默回退的排查成本极高：
//    画面上只是"没看见特效"，你会先怀疑代码，
//    而真实原因往往只是 id 拼错一个字母。

#pragma once

#include "CoreMinimal.h"
#include "Fx/HexFxLibraryAsset.h"

class UWorld;
class AActor;
class USceneComponent;

/**
 * 特效播放器。
 *
 * 生命周期与战斗一致：AHexDemoGameMode 持有一个，战斗开始时 Init。
 */
class HEXSPIRE_API FHexFxRuntime
{
public:
	/**
	 * 默认库路径。
	 *
	 * ⚠️ 库不存在【不是错误】—— 那只是"还没开始配特效"，
	 *    此时全程不播。所以只在找到库时记 Display 日志。
	 */
	static const TCHAR* DefaultLibraryPath();

	/**
	 * 加载默认库。
	 * @return 是否成功（失败时本对象仍可安全使用，只是什么都不播）
	 */
	bool LoadDefaultLibrary();

	/**
	 * 播放一个特效。
	 *
	 * @param VfxId       逻辑层给的 id。为空则尝试 EventType 兜底。
	 * @param EventType   事件名，用于兜底查表
	 * @param WorldCell   目标格的世界坐标
	 * @param SourceActor 施法者（可为 nullptr）
	 * @param TargetActor 受击者（可为 nullptr）
	 */
	void PlayVfx(
		UWorld* World,
		FName VfxId,
		FName EventType,
		const FVector& WorldCell,
		AActor* SourceActor,
		AActor* TargetActor);

	void PlaySfx(
		UWorld* World,
		FName SfxId,
		FName EventType,
		const FVector& WorldCell,
		AActor* SourceActor);

	/**
	 * 预热：把库里全部软引用同步加载进内存。
	 *
	 * ⚠️ 在战斗【开始前】调用。不预热的话第一次播某个特效时
	 *    会触发同步加载 —— 在战斗中表现为一次卡顿，
	 *    而且恰好发生在"第一次用这张卡"的时刻，很像是卡本身有问题。
	 */
	void Preload();

	bool IsLoaded() const { return Library != nullptr; }

	const UHexFxLibraryAsset* GetLibrary() const { return Library; }

private:
	/**
	 * 查特效配置。先查 id，再查事件兜底，都没有返回 nullptr。
	 * @param OutWhy 找不到时填原因，供调用方记日志
	 */
	const FHexVfxEntry* ResolveVfx(FName VfxId, FName EventType, const TCHAR*& OutWhy) const;
	const FHexSfxEntry* ResolveSfx(FName SfxId, FName EventType, const TCHAR*& OutWhy) const;

	/**
	 * 库资产。用裸指针 + AddToRoot 语义由 GameMode 的 UPROPERTY 持有，
	 * 这里【不做 GC 保护】—— 本类是普通 C++ 对象，
	 * 声明 UPROPERTY 的地方在 AHexDemoGameMode。
	 */
	const UHexFxLibraryAsset* Library = nullptr;

	/**
	 * 已经告警过的 id —— 避免同一个拼错的 id 每帧刷一条日志。
	 *
	 * ⚠️ 必须有：一场战斗里同一张卡会打十几次，
	 *    不去重的话日志会被同一条警告淹没，真正的其他问题反而看不见。
	 */
	mutable TSet<FName> WarnedIds;

	/** 库缺失只告警一次（否则每个事件刷一条） */
	mutable bool bWarnedNoLibrary = false;
};
