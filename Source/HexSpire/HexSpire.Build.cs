// Copyright Hex Spire. All Rights Reserved.

using UnrealBuildTool;

/// <summary>
/// HexSpire —— 表现层。
///
/// 职责：Actor、UMG、输入、动画驱动、相机、特效。
/// 铁律（策划案 §12.1 纪律 3）：只订阅事件、只发送输入，绝不驱动逻辑。
/// 逻辑瞬时算完，表现层按 event_log 回放。
/// </summary>
public class HexSpire : ModuleRules
{
	public HexSpire(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"UMG",
			"Slate",
			"SlateCore",
			"HexSpireCore",

			// 六边形地面在运行时程序化生成。
			// ⚠️ 刻意【不做成 StaticMesh 资产】：
			//    .uasset 是二进制，改一次尺寸就得开编辑器重导，
			//    而 tile 尺寸（HexK::TileWidth/TileHeight）还在调。
			//    程序化生成让"改常量 → 重编译 → 立刻看到"成为一步操作。
			"ProceduralMeshComponent",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
		});
	}
}
