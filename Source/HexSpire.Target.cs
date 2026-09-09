// Copyright Hex Spire. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class HexSpireTarget : TargetRules
{
	public HexSpireTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		ExtraModuleNames.AddRange(new string[] { "HexSpireCore", "HexSpire" });
	}
}
