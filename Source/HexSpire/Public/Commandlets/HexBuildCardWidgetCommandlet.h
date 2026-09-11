// Copyright Hex Spire. All Rights Reserved.
//
// 把 C++ 的卡面布局【写进】控件蓝图资产
//
// ══════════════════════════════════════════════════════════════════
// 它解决什么问题
// ══════════════════════════════════════════════════════════════════
// 以 UHexCardWidget 为父类建一个控件蓝图后，设计器里是【空的】——
// 因为 C++ 的布局是运行时在 BuildDefaultTree() 里搭出来的，
// 资产里根本没有那棵树，所以没有任何控件可拖。
//
// 这个 commandlet 把那棵树【实体化】进资产：跑一次之后，
// 设计器里就能看到 Frame / Art / Cost / Name / Desc … 每个控件，
// 位置、边距、字号都能直接拖动修改，不需要碰 C++。
//
// ══════════════════════════════════════════════════════════════════
// 为什么是 commandlet 而不是「手动在设计器里摆一遍」
// ══════════════════════════════════════════════════════════════════
// 手摆的话，C++ 版和蓝图版会是两套【独立】的布局：
// 改了 C++ 的边距，蓝图不会跟着变；反之亦然。
// 而两者外观必须一致（没建蓝图时跑 C++ 版），于是每次调整都要
// 记得改两个地方 —— 这种"必须同时改两处"的约定一定会被忘掉。
//
// 让生成器与 C++ 共用同一份布局定义（见 HexCardLayout.h），
// 就只有一个数据源：C++ 运行时读它搭树，commandlet 读它生成资产。
//
// ⚠️ 生成是【覆盖式】的：会先清掉蓝图里已有的控件树。
//    所以美术改过布局之后【不要】再跑这个 —— 会把改动全冲掉。
//    它只用在两个时候：① 首次把布局灌进空蓝图；② 想放弃改动、回到 C++ 基线。
//    命令行需要显式加 -force 才会覆盖非空的树，就是为了防手滑。
//
// 用法：
//   UnrealEditor-Cmd.exe <uproject> -run=HexBuildCardWidget
//   UnrealEditor-Cmd.exe <uproject> -run=HexBuildCardWidget -force

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "HexBuildCardWidgetCommandlet.generated.h"

UCLASS()
class HEXSPIRE_API UHexBuildCardWidgetCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UHexBuildCardWidgetCommandlet();

	virtual int32 Main(const FString& Params) override;
};
