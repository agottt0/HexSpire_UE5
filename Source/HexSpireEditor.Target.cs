// Copyright Hex Spire. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class HexSpireEditorTarget : TargetRules
{
	public HexSpireEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		// ⚠️ UE 5.8 必须用 V7。用 V5 会修改 UndefinedIdentifier/UnreachableCode 等
		//    全局警告级别，与共享的 UnrealEditor 构建产物冲突，直接编译失败。
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		ExtraModuleNames.AddRange(new string[] { "HexSpireCore", "HexSpire" });
	}
}
