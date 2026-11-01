#include "ScavarGameInstance.h"
#include "ScavarGameDataSubsystem.h"
#include "ScavarLog.h"

void UScavarGameInstance::Init()
{
	Super::Init();

	const UScavarGameDataSubsystem* DataSubsystem = GetSubsystem<UScavarGameDataSubsystem>();
	if (!DataSubsystem || !DataSubsystem->IsLoaded())
	{
		UE_LOG(LogScavar, Error, TEXT("Game data is not loaded; gameplay systems will not be able to resolve ammo, weapons or armor."));
	}
}
