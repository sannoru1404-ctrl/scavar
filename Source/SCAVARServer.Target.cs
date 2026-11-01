using UnrealBuildTool;
using System.Collections.Generic;

public class SCAVARServerTarget : TargetRules
{
	public SCAVARServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "ScavarCore", "ScavarPersistence", "ScavarGameplay" });
	}
}
