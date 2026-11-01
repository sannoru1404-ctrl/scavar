#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Sim/GameData.h"
#include "ScavarGameDataSubsystem.generated.h"

/** Loads data/*.csv (balance tables, single source of truth) once per game instance. Server and client use identical tables. */
UCLASS()
class SCAVARCORE_API UScavarGameDataSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	bool IsLoaded() const { return bLoaded; }
	const Scavar::GameData& GetData() const { return Data; }

	/** Content/Data (staged builds) or <ProjectDir>/data (development checkout). Empty if neither exists. */
	static FString ResolveDataDirectory();

	/** Loads from an explicit directory; used by tests. */
	bool LoadFrom(const FString& Directory, FString& OutError);

private:
	Scavar::GameData Data;
	bool bLoaded = false;
};
