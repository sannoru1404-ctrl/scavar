using System.IO;
using UnrealBuildTool;

public class ScavarPersistence : ModuleRules
{
	public ScavarPersistence(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.NoPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		bEnableExceptions = false;
		bUseRTTI = false;
		bUseUnity = false;

		// SQLite amalgamation (public domain) is vendored by tools/fetch_sqlite.ps1 into ThirdParty/sqlite.
		string SqliteDir = Path.Combine(ModuleDirectory, "ThirdParty", "sqlite");
		PublicIncludePaths.Add(SqliteDir);
		PublicDefinitions.Add("SQLITE_THREADSAFE=1");
		PublicDefinitions.Add("SQLITE_OMIT_LOAD_EXTENSION=1");
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "ScavarCore" });
	}
}
