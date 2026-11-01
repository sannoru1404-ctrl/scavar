#include "Misc/AutomationTest.h"
#include "ScavarGameDataSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FScavarGameDataLoadsTest, "Scavar.Data.LoadsAndValidates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FScavarGameDataLoadsTest::RunTest(const FString& Parameters)
{
	const FString Directory = UScavarGameDataSubsystem::ResolveDataDirectory();
	TestFalse(TEXT("data directory found"), Directory.IsEmpty());
	if (Directory.IsEmpty())
	{
		return false;
	}

	UScavarGameDataSubsystem* Subsystem = NewObject<UScavarGameDataSubsystem>();
	FString Error;
	TestTrue(*FString::Printf(TEXT("tables load: %s"), *Error), Subsystem->LoadFrom(Directory, Error));
	const Scavar::GameData& Data = Subsystem->GetData();
	TestEqual(TEXT("ammo rows"), (int32)Data.Ammo.size(), 12);
	TestEqual(TEXT("weapon rows"), (int32)Data.Weapons.size(), 16);
	TestEqual(TEXT("armor rows"), (int32)Data.Armor.size(), 5);
	TestEqual(TEXT("POI rows"), (int32)Data.Pois.size(), 27);
	return true;
}

#endif
