#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "Sim/LootEconomy.h"
#include "ScavarLoot.generated.h"

UENUM(BlueprintType)
enum class EScavarRiskTier : uint8 { R0, R1, R2, R3, R4 };

UENUM(BlueprintType)
enum class EScavarLootSize : uint8 { Small, Medium, Large };

/** Place these in buildings. Several points sharing BuildingId form one building (restock cooldown and weapon cap are per building). */
UCLASS()
class SCAVARGAMEPLAY_API AScavarLootSpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	AScavarLootSpawnPoint();

	/** Matches the "usage" column of data/items.csv: farm, residential, industrial, medical, storage... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scavar|Loot") TArray<FName> UsageTags;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scavar|Loot") EScavarRiskTier Tier = EScavarRiskTier::R1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scavar|Loot") EScavarLootSize MaxItemSize = EScavarLootSize::Large;
	/** 0 = standalone point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scavar|Loot") int32 BuildingId = 0;
};

/** Server-side Central Economy driver (SYS-04): runs Scavar::RunCycle on a timer and expires ground items. */
UCLASS()
class SCAVARGAMEPLAY_API UScavarLootSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UScavarLootSubsystem, STATGROUP_Tickables); }

	/** Pilot tuning (TUNABLE): spec value is 0.15, but most pilot buildings have fewer than 7 points, which would never allow a weapon. */
	double WeaponPointFraction = 0.35;
	/** Pilot tuning: 120 s so two testers see restocking; spec value is 300 s. */
	double CycleIntervalSeconds = 120.0;

	/** Forces an immediate restock cycle (console / tests). Returns the number of piles spawned. */
	int32 RunCycleNow();

private:
	bool IsServerWorld() const;
	void RebuildPoints();
	void ExpireGroundItems(double Now);

	std::vector<Scavar::LootTypeDef> Types;
	std::vector<Scavar::LootSpawnPoint> Points;
	TArray<FVector> PointLocations;                 // by point index (cm)
	TMap<int64, int64> PointToItem;                 // occupied point -> item id
	TMap<int64, double> BuildingLastSpawn;
	Scavar::Rng Rng;
	double LastCycleSeconds = -1e9;
	bool bReady = false;
};
