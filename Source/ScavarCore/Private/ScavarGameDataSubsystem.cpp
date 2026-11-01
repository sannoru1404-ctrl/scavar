#include "ScavarGameDataSubsystem.h"
#include "ScavarLog.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

FString UScavarGameDataSubsystem::ResolveDataDirectory()
{
	const FString Staged = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Data"));
	if (IFileManager::Get().FileExists(*(Staged / TEXT("ammo.csv"))))
	{
		return Staged;
	}
	const FString Dev = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("data"));
	if (IFileManager::Get().FileExists(*(Dev / TEXT("ammo.csv"))))
	{
		return Dev;
	}
	return FString();
}

bool UScavarGameDataSubsystem::LoadFrom(const FString& Directory, FString& OutError)
{
	std::string Error;
	const std::string Dir = TCHAR_TO_UTF8(*Directory);
	bLoaded = Data.Load(Dir, &Error);
	OutError = bLoaded ? FString() : FString(UTF8_TO_TCHAR(Error.c_str()));
	return bLoaded;
}

void UScavarGameDataSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const FString Directory = ResolveDataDirectory();
	if (Directory.IsEmpty())
	{
		UE_LOG(LogScavar, Error, TEXT("Balance tables not found (Content/Data or <Project>/data)."));
		return;
	}
	FString Error;
	if (LoadFrom(Directory, Error))
	{
		UE_LOG(LogScavar, Log, TEXT("Loaded balance tables from %s: %d ammo, %d weapons, %d armor, %d POI"),
			*Directory, (int32)Data.Ammo.size(), (int32)Data.Weapons.size(), (int32)Data.Armor.size(), (int32)Data.Pois.size());
	}
	else
	{
		UE_LOG(LogScavar, Error, TEXT("Failed to load balance tables from %s: %s"), *Directory, *Error);
	}
}
