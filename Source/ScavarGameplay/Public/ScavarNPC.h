#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Sim/Ai.h"
#include "ScavarNPC.generated.h"

class UScavarVitalsComponent;
class UTextRenderComponent;

/** Same order as Scavar::AiArchetype. */
UENUM(BlueprintType)
enum class EScavarArchetype : uint8
{
	Militia,
	RoadGuard,
	Marauder,
	Disoriented,
	Wolf,
	Deer
};

/**
 * Server-driven NPC body. Decisions come from Scavar::TickNpc (pure, tested); this actor only executes the resulting commands:
 * steering toward a point, biting, and nothing else. Health lives in the shared vitals component, so players can hurt NPCs the
 * same way NPCs hurt players. The brain never reads any medical field to decide hostility (NoDiagnosisHostility).
 */
UCLASS()
class SCAVARGAMEPLAY_API AScavarNPC : public ACharacter
{
	GENERATED_BODY()

public:
	AScavarNPC();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: assigns identity before the first AI tick. */
	void InitBrain(int64 NpcId, EScavarArchetype InArchetype, int64 InGroupId);
	void SetMoveGoal(const FVector& GoalCm, bool bRun);
	void ClearMoveGoal() { bHasGoal = false; }

	UScavarVitalsComponent* GetVitals() const { return Vitals; }
	Scavar::AiNpc& Brain() { return BrainState; }
	const Scavar::AiNpc& Brain() const { return BrainState; }
	int64 GetGroupId() const { return GroupId; }
	EScavarArchetype GetArchetype() const { return Archetype; }

	/** Fraction 0..1 of total zone HP; used only for flee/retreat thresholds. */
	float GetHealthFraction() const;

	FSimpleMulticastDelegate OnNpcDied;

private:
	UFUNCTION() void HandleDied(AActor* Victim);
	UFUNCTION() void OnRep_Archetype();

	UPROPERTY(VisibleAnywhere) TObjectPtr<UScavarVitalsComponent> Vitals;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> Label;
	UPROPERTY(ReplicatedUsing = OnRep_Archetype) EScavarArchetype Archetype = EScavarArchetype::Marauder;

	Scavar::AiNpc BrainState;
	int64 GroupId = 0;
	FVector GoalCm = FVector::ZeroVector;
	bool bHasGoal = false;
	bool bRunToGoal = false;
};
