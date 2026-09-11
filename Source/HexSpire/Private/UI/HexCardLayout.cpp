// Copyright Hex Spire. All Rights Reserved.

#include "UI/HexCardLayout.h"
#include "HexSpire.h"

#include "Engine/Font.h"
#include "Styling/CoreStyle.h"

namespace HexCardLayout
{
	FSlateFontInfo GetCardFont(int32 Size, bool bBold)
	{
		// ⚠️ 静态缓存：LoadObject 每次调用都要做字符串查找，
		//    而卡面每次刷新都会取好几次字体。
		//    加载失败也缓存（存 nullptr），否则每帧都会重试一个
		//    注定失败的路径并刷一条警告，把日志淹掉。
		static bool bTried = false;
		static UFont* CardFontAsset = nullptr;

		if (!bTried)
		{
			bTried = true;
			CardFontAsset = LoadObject<UFont>(nullptr, FontAssetPath);

			if (!CardFontAsset)
			{
				// 这个【必须报错】：回退字体不含中文字形，
				// 而缺字形时 UMG 画的是空白 —— 卡面会变成"没有文字"，
				// 看起来像布局 bug，排查会跑偏到布局上去。
				UE_LOG(LogHexSpire, Error,
					TEXT("卡面字体加载失败：%s —— 中文将无法显示（UMG 缺字形时画空白，不报错）"),
					FontAssetPath);
			}
		}

		FSlateFontInfo Info = FCoreStyle::GetDefaultFontStyle(
			bBold ? "Bold" : "Regular", Size);

		if (CardFontAsset)
		{
			Info.FontObject = CardFontAsset;
			Info.Size = Size;
			// Roboto 的 typeface 里确实有 Regular / Bold（实测资产内容），
			// 与瞬态的 DefaultRegularFont 不同 —— 后者只有 Regular。
			Info.TypefaceFontName = FName(bBold ? FaceBold : FaceRegular);
		}

		return Info;
	}
}
