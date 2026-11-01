#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Sim/Damage.h"
#include "Sim/Survival.h"
#include "ScavarVitalsComponent.generated.h"

/** Same order as Scavar::HitZone. */
UENUM(BlueprintType)
enum class EScavarHitZone : uint8
{
	Head,
	Torso,
	ArmL,
	ArmR,
	LegL,
	LegR
};

UENUM(BlueprintType)
enum class EScavarTreatment : uint8
{
	Bandage,
	Tourniquet,
	HemostaticPack,
	Splint,
	Analgesic,
	AutoMedKit
};

/** What the owning client is allowed to know about its own body (SYS-06/07). Other clients only receive bDead. */
USTRUCT(BlueprintType)
struct FScavarVitalsSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) TArray<float> ZoneHp;
	UPROPERTY(BlueprintReadOnly) TArray<uint8> Bleed;       // 0 none, 1 light, 2 heavy
	UPROPERTY(BlueprintReadOnly) TArray<bool> Fracture;
	UPROPERTY(BlueprintReadOnly) float Food = 100.f;
	UPROPERTY(BlueprintReadOnly) float Water = 100.f;
	UPROPERTY(BlueprintReadOnly) float Warmth = 100.f;
	UPROPERTY(BlueprintReadOnly) float Stamina = 100.f;
	UPROPERTY(BlueprintReadOnly) float AnalgesiaSeconds = 0.f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FScavarVitalsChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FScavarDied, AActor*, Victim);
/** Fired on the server when a timed treatment ends; bCompleted == false means it was interrupted and no resource may be consumed. */
DECLARE_MULTICAST_DELEGATE_OneParam(FScavarTreatmentFinished, bool /*bCompleted*/);
/** Fired on the server after armor lost durability, so the inventory can persist the wear on the item. */
DECLARE_MULTICAST_DELEGATE_OneParam(FScavarArmorWorn, const Scavar::ArmorLoadout&);

/**
 * Health, armor, hunger/thirst/cold and stamina. The server owns all state and runs the engine-agnostic Scavar::Sim code;
 * this component only schedules ticks, replicates results and raises events. Nothing here trusts the client.
 */
UCLASS(ClassGroup = (Scavar), meta = (BlueprintSpawnableComponent))
class SCAVARGAMEPLAY_API UScavarVitalsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UScavarVitalsComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ---- Server API (no-ops with a warning on clients) ----
	/** One projectile or pellet. Damage is pre-multiplier damage from data/ammo.csv. Returns true if this hit killed. */
	UFUNCTION(BlueprintCallable, Category = "Scavar|Vitals")
	bool ApplyProjectileHit(EScavarHitZone Zone, float Damage, int32 PenTier);

	UFUNCTION(BlueprintCallable, Category = "Scavar|Vitals")
	void ApplyFall(float HeightMeters);

	/** Starts a timed treatment; interrupted by damage, weapon swap or sprint. Item consumption is the inventory's job (M2). */
	UFUNCTION(BlueprintCallable, Category = "Scavar|Vitals")
	bool BeginTreatment(EScavarTreatment Kind, EScavarHitZone Zone);

	/** Server only. Clamps to 0..100. */
	void AddNutrition(float Food, float Water);
	/** First wounded zone that the given treatment can fix, for quick-use without choosing a body part. */
	bool FindTreatableZone(EScavarTreatment Kind, EScavarHitZone& OutZone) const;
	bool IsTreating() const { return bTreating; }
	/** Development only: full health, no wounds, full needs. Not available for dead characters. */
	void DebugRestoreAll();

	void NotifyWeaponSwapped();
	void SetMoveMode(Scavar::MoveMode Mode);
	void SetArmor(const Scavar::ArmorLoadout& NewArmor);
	void SetCarriedKg(float Kg) { CarriedKg = Kg; }

	// ---- Queries (valid on server and owning client) ----
	UFUNCTION(BlueprintPure, Category = "Scavar|Vitals") bool IsDead() const { return bDead; }
	UFUNCTION(BlueprintPure, Category = "Scavar|Vitals") float GetStamina() const;
	UFUNCTION(BlueprintPure, Category = "Scavar|Vitals") bool HasLegFracture() const;
	bool CanSprint() const;
	float GetCarriedKg() const { return CarriedKg; }
	const FScavarVitalsSnapshot& GetSnapshot() const { return OwnerSnapshot; }

	FScavarTreatmentFinished OnTreatmentFinished;
	FScavarArmorWorn OnArmorWorn;
	UPROPERTY(BlueprintAssignable, Category = "Scavar|Vitals") FScavarVitalsChanged OnVitalsChanged;
	UPROPERTY(BlueprintAssignable, Category = "Scavar|Vitals") FScavarDied OnDied;

private:
	UFUNCTION() void OnRep_Snapshot();
	UFUNCTION() void OnRep_Dead();

	void HandleDeath();
	void RefreshSnapshot();

	UPROPERTY(ReplicatedUsing = OnRep_Snapshot) FScavarVitalsSnapshot OwnerSnapshot;
	UPROPERTY(ReplicatedUsing = OnRep_Dead) bool bDead = false;

	// Server-only authoritative state.
	Scavar::CharacterHealth Health;
	Scavar::ArmorLoadout Armor;
	Scavar::SurvivalState Survival;
	Scavar::StaminaState Stamina;
	Scavar::MoveMode MoveMode = Scavar::MoveMode::Rest;
	Scavar::TreatmentSession Treatment;
	bool bTreating = false;
	float CarriedKg = 0.f;
	Scavar::Rng Rng;
};
