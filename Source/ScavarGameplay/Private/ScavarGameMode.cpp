#include "ScavarGameMode.h"
#include "ScavarCharacter.h"
#include "ScavarLog.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerController.h"

AScavarGameMode::AScavarGameMode()
{
	DefaultPawnClass = AScavarCharacter::StaticClass();
}

AActor* AScavarGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	TArray<FVector> Occupied;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* Other = It->Get();
		if (Other && Other != Player && Other->GetPawn())
		{
			Occupied.Add(Other->GetPawn()->GetActorLocation());
		}
	}

	TArray<APlayerStart*> Candidates;
	TArray<APlayerStart*> All;
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		APlayerStart* Start = *It;
		All.Add(Start);
		bool bFarEnough = true;
		for (const FVector& Location : Occupied)
		{
			if (FVector::Dist(Location, Start->GetActorLocation()) < MinSpawnDistanceFromPlayers)
			{
				bFarEnough = false;
				break;
			}
		}
		if (bFarEnough)
		{
			Candidates.Add(Start);
		}
	}

	const TArray<APlayerStart*>& Pool = Candidates.Num() > 0 ? Candidates : All;
	if (Pool.Num() == 0)
	{
		UE_LOG(LogScavar, Warning, TEXT("No PlayerStart in the level; falling back to default placement."));
		return Super::ChoosePlayerStart_Implementation(Player);
	}
	if (Candidates.Num() == 0)
	{
		UE_LOG(LogScavar, Warning, TEXT("No spawn point is %.0f cm away from every player; using any spawn point."), MinSpawnDistanceFromPlayers);
	}
	return Pool[FMath::RandRange(0, Pool.Num() - 1)];
}
