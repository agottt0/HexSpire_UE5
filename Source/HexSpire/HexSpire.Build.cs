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

		// ══════════════════════════════════════════════════════════════
		// 仅编辑器：把 C++ 的卡面布局【写进】控件蓝图资产
		// ══════════════════════════════════════════════════════════════
		// HexBuildCardWidget commandlet 用这几个模块把控件树生成到
		// WB_Card / WB_HandPanel 里，之后美术就能在设计器里直接拖布局。
		//
		// ⚠️ 必须包在 bBuildEditor 里。UMGEditor 带
		//    [SupportedTargetTypes(Editor, Program)]，在 Game/Shipping
		//    目标下引用它会直接链接失败 —— 也就是说打包会炸，
		//    而编辑器里一切正常，问题只在出包时才暴露。
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"UnrealEd",   // UBaseWidgetBlueprint::WidgetTree、FKismetEditorUtilities
				"UMGEditor",  // UWidgetBlueprint、FWidgetBlueprintOperationUtils
			});
		}
	}
}
