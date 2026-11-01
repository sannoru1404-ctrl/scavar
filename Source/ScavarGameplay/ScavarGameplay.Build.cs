using UnrealBuildTool;

public class ScavarGameplay : ModuleRules
{
	public ScavarGameplay(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		bUseRTTI = false;
		// Several .cpp files keep identically named helpers in anonymous namespaces (ToCm, ToMeters, ...); unity blobs would merge them.
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "NetCore", "ScavarCore", "ScavarPersistence"
		});
	}
}
