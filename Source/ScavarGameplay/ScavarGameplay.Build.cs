using UnrealBuildTool;

public class ScavarGameplay : ModuleRules
{
	public ScavarGameplay(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		bUseRTTI = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "NetCore", "ScavarCore", "ScavarPersistence"
		});
	}
}
