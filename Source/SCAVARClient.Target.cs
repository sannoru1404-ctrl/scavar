using UnrealBuildTool;
using System.Collections.Generic;

public class SCAVARClientTarget : TargetRules
{
	public SCAVARClientTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Client;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "ScavarCore", "ScavarPersistence", "ScavarGameplay" });
	}
}
