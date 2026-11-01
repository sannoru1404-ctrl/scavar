#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Sim/Weapon.h"
#include "ScavarWeaponComponent.generated.h"

class UScavarWorldInventory;
class UScavarInventoryComponent;
class UScavarVitalsComponent;

/** What the owning player sees about the weapon in hand (replicated to the owner only). */
USTRUCT(BlueprintType)
struct FScavarWeaponView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) bool bEquipped = false;
	UPROPERTY(BlueprintReadOnly) FName WeaponId;
	UPROPERTY(BlueprintReadOnly) int32 Rounds = 0;      // magazine/internal + chamber
	UPROPERTY(BlueprintReadOnly) bool bChambered = false;
	UPROPERTY(BlueprintReadOnly) uint8 Mode = 0;        // Scavar::FireMode
	UPROPERTY(BlueprintReadOnly) bool bJammed = false;
	UPROPERTY(BlueprintReadOnly) bool bBusy = false;    // reloading / clearing / cycling
	UPROPERTY(BlueprintReadOnly) bool bMelee = false;
};

/**
 * The weapon in the character's hands (SYS-05). The weapon itself is an item in the inventory; this component holds the transient
 * Scavar::WeaponState, drives reload/jam/cycle actions through the item store, and resolves shots on the server:
 * muzzle and aim come from the server's own copy of the pawn (never from the client), rounds are consumed by Scavar::Fire, and the
 * bullet is stepped with Scavar::StepProjectile and traced segment by segment.
 *
 * Pilot simplifications (documented in docs/07 section 1b): hits resolve instantly along the simulated path (drop included, no
 * flight time), world geometry stops bullets completely, hit zones come from the impact height on the target capsule, and there is
 * no lag compensation yet (listen server, two testers).
 */
UCLASS(ClassGroup = (Scavar), meta = (BlueprintSpawnableComponent))
class SCAVARGAMEPLAY_API UScavarWeaponComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UScavarWeaponComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ---- Server API ----
	/** Takes the weapon item into the hands (holsters the current one first). Called when the player "uses" a weapon item. */
	bool Equip(int64 ItemId);
	/** Empties the weapon safely into the inventory. Returns false (and keeps the weapon in hand) when there is no room. */
	bool Holster();
	/** Best-effort cleanup on death so no rounds or magazine vanish with the weapon state. */
	void ReleaseOnDeath();
	void CancelPendingAction();
	int64 GetEquippedItem() const { return EquippedItem; }
	bool IsEquipped() const { return bEquipped; }

	UFUNCTION(Server, Reliable) void ServerTrigger(bool bPressed);
	UFUNCTION(Server, Reliable) void ServerReload();
	UFUNCTION(Server, Reliable) void ServerToggleFireMode();
	UFUNCTION(Server, Reliable) void ServerHolster();
	UFUNCTION(Client, Unreliable) void ClientRecoil(float PitchDegrees, float YawDegrees);

	const FScavarWeaponView& GetView() const { return View; }

private:
	UFUNCTION() void OnRep_View();

	UScavarWorldInventory* Inventory() const;
	UScavarInventoryComponent* OwnerInventory() const;
	UScavarVitalsComponent* Vitals() const;
	double Now() const;
	void Notify(const FString& Message) const;
	void RefreshView();

	void PullTrigger();
	void FireOnce();
	void MeleeStrike(double NowSeconds);
	void FireProjectile(const FVector& Origin, const FVector& Direction, float SpeedMps, float Damage, int32 PenTier);
	void ApplyBulletHit(const FHitResult& Hit, const FVector& Direction, float Damage, int32 PenTier);
	void StartAutoActions(double NowSeconds);
	void StartReload();
	int64 FindBestMagazine(const Scavar::WeaponSpec& Spec) const;
	bool FillMagazineFromAmmo(const Scavar::WeaponSpec& Spec, int64* OutMagazine);
	int64 FindAmmoStackForWeapon() const;
	void WriteBackCondition();
	float SpreadDegrees() const;
	int64 ShooterId() const;

	UPROPERTY(ReplicatedUsing = OnRep_View) FScavarWeaponView View;

	// Server-only.
	Scavar::WeaponState State;
	int64 EquippedItem = 0;
	Scavar::ContainerId Magwell = 0;
	bool bEquipped = false;
	bool bMelee = false;
	bool bTriggerHeld = false;
	double LastMeleeAt = -1e9;
	Scavar::Rng Rng;
};
