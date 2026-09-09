// Copyright Hex Spire. All Rights Reserved.

using UnrealBuildTool;

/// <summary>
/// HexSpireCore —— 纯逻辑层（策划案 §12.1 纪律 3）。
///
/// 硬性约束（由 Tools/check_discipline 扫描强制）：
///   · 不得引用 AActor / UWorld / UGameInstance / Tick / Timer
///   · 不得引用 UMG / Slate / 任何渲染相关模块
///   · 不得读取系统时间、不得使用非注入式随机
///   · 战斗必须能在无渲染（commandlet / headless）下完整跑完
///
/// 依赖只允许 Core / CoreUObject —— 这是"能否 headless 跑批量模拟"的技术前提。
/// 一旦这里多出一个 Engine 依赖，battle_sim 与全部验证器就会失效。
/// </summary>
public class HexSpireCore : ModuleRules
{
	public HexSpireCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		// ⚠️ 故意只依赖 Core / CoreUObject。
		//    需要 Engine 的东西说明它属于表现层，应该放到 HexSpire 模块。
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
		});
	}
}
