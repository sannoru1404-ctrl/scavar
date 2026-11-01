using UnrealBuildTool;
using System.Collections.Generic;

public class SCAVARTarget : TargetRules
{
	public SCAVARTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "ScavarCore", "ScavarPersistence", "ScavarGameplay" });
	}
}
