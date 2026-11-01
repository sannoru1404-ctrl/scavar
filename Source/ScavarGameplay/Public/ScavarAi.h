#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScavarNPC.h"
#include "Sim/AiDirector.h"
#include "ScavarAi.generated.h"

/** Place in the level: a region that may host NPC groups of one archetype (SYS-08 rule 5). */
UCLASS()
class SCAVARGAMEPLAY_API AScavarAiZone : public AActor
{
	GENERATED_BODY()

public:
	AScavarAiZone();

	UPROPERTY(EditAnywhere, Category = "Scavar|AI") EScavarArchetype Archetype = EScavarArchetype::Marauder;
	UPROPERTY(EditAnywhere, Category = "Scavar|AI") int32 MaxGroups = 2;
	UPROPERTY(EditAnywhere, Category = "Scavar|AI") int32 MinGroupSize = 2;
	UPROPERTY(EditAnywhere, Category = "Scavar|AI") int32 MaxGroupSize = 3;
	/** Spec: 30-60 minutes after a group is killed. Pilot default is shorter so two testers can see respawns (TUNABLE). */
	UPROPERTY(EditAnywhere, Category = "Scavar|AI") float CooldownMinSeconds = 900.f;
	UPROPERTY(EditAnywhere, Category = "Scavar|AI") float CooldownMaxSeconds = 1800.f;
	/** Candidate spawn points relative to this actor (cm). If empty, six points on a 400 m ring are generated. */
	UPROPERTY(EditAnywhere, Category = "Scavar|AI", meta = (MakeEditWidget = true)) TArray<FVector> SpawnPointOffsets;
	UPROPERTY(EditAnywhere, Category = "Scavar|AI") float RingRadiusCm = 40000.f;
};

/** Server-side AI driver: runs the zone director, spawns/despawns groups, ticks every NPC brain and executes its commands. */
UCLASS()
class SCAVARGAMEPLAY_API UScavarAiSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UScavarAiSubsystem, STATGROUP_Tickables); }

	/** Weapons call this when a shot is fired so NPCs can hear it (SYS-05 loudness table). */
	void ReportGunshot(const FVector& LocationCm, bool bSuppressed, int64 SourceId);
	/** Weapons call this when a person damages an NPC so the NPC remembers the attacker. */
	void ReportNpcAttacked(AScavarNPC* Npc, int64 AttackerId);

	int32 GetActiveNpcCount() const { return Npcs.Num(); }

private:
	bool IsServerWorld() const;
	void BuildZones();
	void RunDirector(double Now, const TArray<FVector>& PlayerLocationsCm);
	void SpawnGroup(const Scavar::SpawnOrder& Order);
	void DespawnGroup(int64 GroupId);
	void TickNpcs(float Dt, double Now);
	void ExecuteCommand(AScavarNPC& Npc, const Scavar::AiCommand& Command, double Now);
	void ApplyNpcAttack(AScavarNPC& Npc, int64 TargetId);
	void HandleNpcDied(AScavarNPC* Npc);
	AActor* FindPlayerPawn(int64 TargetId) const;

	TArray<TWeakObjectPtr<AScavarNPC>> Npcs;
	std::vector<Scavar::AiZone> Zones;
	TMap<int64, EScavarArchetype> ZoneArchetype;
	Scavar::DirectorState Director;
	Scavar::DirectorTuning DirectorTune;
	Scavar::Rng Rng;
	std::vector<Scavar::AiSound> PendingSounds;
	TMap<int64, double> RecentShooters; // player id -> time of last shot
	double LastDirectorSeconds = -1e9;
	float Accumulator = 0.f;
	int64 NextNpcId = 1;
	bool bReady = false;
};
