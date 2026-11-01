using UnrealBuildTool;

public class ScavarCore : ModuleRules
{
	public ScavarCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		bEnableExceptions = false;
		bUseRTTI = false;
		// Sim/ is engine-agnostic C++ shared with the plain CMake test build; unity blobs would merge its anonymous namespaces.
		bUseUnity = false;

		PublicIncludePaths.Add(ModuleDirectory); // headers are included as "Sim/<Name>.h" to avoid clashes with engine headers
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
	}
}
