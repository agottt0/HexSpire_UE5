// Copyright Hex Spire. All Rights Reserved.

#include "UI/HexCardArt.h"
#include "Data/HexCardTableLoader.h"
#include "Data/HexCardTableRow.h"
#include "HexSpire.h"

#include "Engine/Texture2D.h"

namespace
{
	#define CARD_TEX TEXT("/Game/ArtResource/Card/")

	// ── 现有的 4 张卡牌贴图（工程里实际存在的全部）
	const TCHAR* P_FrameWhite = CARD_TEX TEXT("Card_White.Card_White");
	const TCHAR* P_IconAttack = CARD_TEX TEXT("Attack.Attack");
	const TCHAR* P_IconDefend = CARD_TEX TEXT("Defend.Defend");
	const TCHAR* P_IconMove   = CARD_TEX TEXT("Move.Move");

	// ── 横版卡框（左侧固定卡）。256x130，中间留白给文字。
	const TCHAR* P_WideAttack = CARD_TEX TEXT("RCard_Attack.RCard_Attack");
	const TCHAR* P_WideDefend = CARD_TEX TEXT("RCard_Defend.RCard_Defend");
	const TCHAR* P_WideMove   = CARD_TEX TEXT("RCard_Move.RCard_Move");

	#undef CARD_TEX

	/**
	 * 带缓存的同步加载。
	 *
	 * ⚠️ 必须缓存。手牌每帧重建控件时若每张卡都 LoadObject，
	 *    UE 内部虽有包缓存，但 FindObject 的字符串查找在
	 *    每帧 × 每张卡的量级下是可测的开销。
	 *
	 * ⚠️ 用 TMap<FString, UTexture2D*> 而不是 TStrongObjectPtr：
	 *    这些贴图由 /Game 包持有引用，不会被 GC 掉。
	 *    真正会失效的情况是资产被删除 —— 那时应该报错而非静默续用，
	 *    所以这里刻意不做失效检测。
	 */
	UTexture2D* LoadTexCached(const TCHAR* Path)
	{
		if (!Path)
		{
			return nullptr;
		}

		static TMap<FString, UTexture2D*> Cache;

		const FString Key(Path);
		if (UTexture2D** Found = Cache.Find(Key))
		{
			return *Found;
		}

		UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, Path);
		if (!Tex)
		{
			// 资产被删/改名时走到这里。控件会回退到纯色块，
			// 但必须留日志 —— 否则"卡牌没图"会变成无头案。
			UE_LOG(LogHexSpire, Warning, TEXT("卡牌贴图加载失败：%s"), Path);
		}

		// 失败也写进缓存：避免每帧重试一个注定失败的路径，
		// 那会每帧刷一条警告把日志淹掉。
		Cache.Add(Key, Tex);
		return Tex;
	}

	/** 解析 DataTable 里配的软引用。未配置返回 nullptr。 */
	UTexture2D* ResolveSoft(const TSoftObjectPtr<UTexture2D>& Soft)
	{
		if (Soft.IsNull())
		{
			return nullptr;
		}
		// 同步加载：卡牌贴图很小（256px 级），且只在手牌变化时取一次
		return Soft.LoadSynchronous();
	}
}

namespace HexCardArt
{
	UTexture2D* GetFrame(FName CardId, EHexRarity Rarity)
	{
		// ① DataTable 覆写优先
		if (const FHexCardVisualRow* V = FHexCardTableLoader::FindVisual(CardId))
		{
			if (UTexture2D* T = ResolveSoft(V->CardFrame))
			{
				return T;
			}
		}

		// ② 按稀有度取默认框
		//
		// ⚠️ 目前只有一张白框，所有稀有度共用它，靠染色区分。
		//    等美术交出 5 档卡框后，这里改成 switch(Rarity) 即可，
		//    调用方不需要改。
		(void)Rarity;
		return LoadTexCached(P_FrameWhite);
	}

	UTexture2D* GetTypeIcon(FName CardId, EHexCardType Type)
	{
		if (const FHexCardVisualRow* V = FHexCardTableLoader::FindVisual(CardId))
		{
			if (UTexture2D* T = ResolveSoft(V->TypeIcon))
			{
				return T;
			}
		}

		switch (Type)
		{
		case EHexCardType::Attack: return LoadTexCached(P_IconAttack);
		case EHexCardType::Guard:  return LoadTexCached(P_IconDefend);
		case EHexCardType::Move:   return LoadTexCached(P_IconMove);

		// ⚠️ 技能/姿态/衍生/诅咒【没有图标资产】。
		//    返回 nullptr 让控件不画图标层，而不是拿"攻击"图标顶替 ——
		//    错的图标比没有图标更糟：玩家会把技能卡误读成攻击卡。
		default: return nullptr;
		}
	}

	UTexture2D* GetArtwork(FName CardId)
	{
		if (const FHexCardVisualRow* V = FHexCardTableLoader::FindVisual(CardId))
		{
			return ResolveSoft(V->Artwork);
		}
		return nullptr;
	}

	FLinearColor GetFrameTint(FName CardId, EHexCardType Type)
	{
		// 美术显式配了染色就用它 —— 这是给"一张灰度框复用出多种配色"留的口子
		if (const FHexCardVisualRow* V = FHexCardTableLoader::FindVisual(CardId))
		{
			if (!V->FrameTint.Equals(FLinearColor::White))
			{
				return V->FrameTint;
			}
		}

		// ⚠️ 没配就【不染色】。
		//    原先这里按 CardType 返回红/蓝/绿，把整张卡框乘成一块彩色板。
		//    去掉的理由有两条，都不是审美偏好：
		//      ① 染色是乘在贴图上的，插画挂上来会被整体偏色 ——
		//         美术永远调不准，因为他看到的图和游戏里的不是一个颜色。
		//      ② 只靠颜色区分类型对色弱玩家无效（§13.2 明确禁止）。
		//    类型改由图标 + GetTypeName 的文字承担。
		(void)Type;
		return FLinearColor::White;
	}

	UTexture2D* GetWideFrame(EHexCardType Type)
	{
		switch (Type)
		{
		case EHexCardType::Attack: return LoadTexCached(P_WideAttack);
		case EHexCardType::Guard:  return LoadTexCached(P_WideDefend);
		case EHexCardType::Move:   return LoadTexCached(P_WideMove);

		// ⚠️ 其他类型没有横版图，返回 nullptr 让控件不画卡框，
		//    而不是拿"攻击"的横版图顶替 —— 错的卡框比没有卡框更糟。
		default: return nullptr;
		}
	}

	FString GetTypeName(EHexCardType Type)
	{
		// ⚠️ 刻意手写而不是取 UEnum 的 DisplayName：
		//    DisplayName 是 UENUM 元数据，改了会连带影响编辑器下拉框、
		//    配表列、日志 —— 卡面文案不该和那些耦在一起。
		//    而且 headless 下拿枚举元数据要反射初始化，这里不值当。
		switch (Type)
		{
		case EHexCardType::Attack:  return TEXT("攻击");
		case EHexCardType::Guard:   return TEXT("守备");
		case EHexCardType::Move:    return TEXT("移动");
		case EHexCardType::Skill:   return TEXT("技能");
		case EHexCardType::Stance:  return TEXT("姿态");
		case EHexCardType::Derived: return TEXT("衍生");
		case EHexCardType::Curse:   return TEXT("诅咒");
		default:                    return FString();
		}
	}

	FLinearColor GetTypeAccent(EHexCardType Type)
	{
		// ⚠️ 这些是【前景色】，画在浅色纸面上，所以取深色。
		//    别拿原来 GetFrameTint 那套值过来 —— 那套是给"乘在白贴图上"
		//    设计的（0.86,0.42,0.36 那种），当文字色在白底上是浅粉，读不出来。
		switch (Type)
		{
		case EHexCardType::Attack:  return FLinearColor(0.62f, 0.16f, 0.12f);
		case EHexCardType::Guard:   return FLinearColor(0.13f, 0.32f, 0.58f);
		case EHexCardType::Move:    return FLinearColor(0.10f, 0.40f, 0.24f);
		case EHexCardType::Skill:   return FLinearColor(0.55f, 0.38f, 0.05f);
		case EHexCardType::Stance:  return FLinearColor(0.38f, 0.22f, 0.58f);
		case EHexCardType::Derived: return FLinearColor(0.46f, 0.18f, 0.50f);
		case EHexCardType::Curse:   return FLinearColor(0.28f, 0.24f, 0.26f);
		default:                    return FLinearColor(0.20f, 0.20f, 0.22f);
		}
	}

	float GetRarityGlow(FName CardId, EHexRarity Rarity)
	{
		if (const FHexCardVisualRow* V = FHexCardTableLoader::FindVisual(CardId))
		{
			if (V->RarityGlow > 0.0f)
			{
				return V->RarityGlow;
			}
		}

		// 普通卡不发光 —— 光晕是"这张值得注意"的信号，
		// 每张都发光等于没有信号。
		switch (Rarity)
		{
		case EHexRarity::Uncommon:  return 0.35f;
		case EHexRarity::Rare:      return 0.70f;
		case EHexRarity::Epic:      return 1.10f;
		case EHexRarity::Legendary: return 1.60f;
		case EHexRarity::Cursed:    return 0.85f;
		default:                    return 0.0f;
		}
	}
}
