#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ScavarGameMode.generated.h"

/** Server-side rules: spawn placement (SYS-01). Spawns are chosen from APlayerStart actors at least MinSpawnDistanceFromPlayers away from living players. */
UCLASS()
class SCAVARGAMEPLAY_API AScavarGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AScavarGameMode();

	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

	/** 300 m (TUNABLE, SYS-01 / docs/03). Unreal units are centimeters. */
	UPROPERTY(EditDefaultsOnly, Category = "Scavar|Spawn")
	float MinSpawnDistanceFromPlayers = 30000.f;
};
