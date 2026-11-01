#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Sim/Inventory.h"
#include "ScavarInventoryComponent.generated.h"

class AScavarPickup;
class UScavarWorldInventory;
class UScavarVitalsComponent;

USTRUCT(BlueprintType)
struct FScavarItemView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) int64 ItemId = 0;
	UPROPERTY(BlueprintReadOnly) FName DefId;
	UPROPERTY(BlueprintReadOnly) int32 Qty = 1;
	UPROPERTY(BlueprintReadOnly) int64 ContainerId = 0;
	UPROPERTY(BlueprintReadOnly) int32 X = 0;
	UPROPERTY(BlueprintReadOnly) int32 Y = 0;
	UPROPERTY(BlueprintReadOnly) bool bRot = false;
};

USTRUCT(BlueprintType)
struct FScavarContainerView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) int64 ContainerId = 0;
	UPROPERTY(BlueprintReadOnly) uint8 Kind = 0; // Scavar::ContainerKind
	UPROPERTY(BlueprintReadOnly) int32 W = 1;
	UPROPERTY(BlueprintReadOnly) int32 H = 1;
};

/**
 * A character's view of the authoritative ItemStore (SYS-03). All state-changing calls are server RPCs that validate ownership and
 * reach, then run a single transaction. Clients only ever receive a read-only copy of their own containers.
 */
UCLASS(ClassGroup = (Scavar), meta = (BlueprintSpawnableComponent))
class SCAVARGAMEPLAY_API UScavarInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UScavarInventoryComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// ---- Server ----
	/** Creates pockets, backpack and secure container, moves anything waiting in the account's Returns into the secure pocket, adds the starter kit. */
	void InitializeForPlayer(const FString& InAccountKey, bool bGiveStarterKit);
	/** Moves the secure pocket to the account Returns and drops the rest of the body's items as pickups around the corpse. */
	void HandleOwnerDied();
	/** Adds an item straight into the first container with room. Returns false when everything is full. */
	bool GiveItem(FName DefId, int32 Qty);

	UFUNCTION(Server, Reliable) void ServerMoveItem(int64 ItemId, int64 DstContainer, int32 X, int32 Y, bool bRot);
	UFUNCTION(Server, Reliable) void ServerPickup(AScavarPickup* Pickup);
	UFUNCTION(Server, Reliable) void ServerDropItem(int64 ItemId);
	UFUNCTION(Server, Reliable) void ServerUseItem(int64 ItemId);

	// ---- Queries (owning client and server) ----
	const TArray<FScavarItemView>& GetItems() const { return Items; }
	const TArray<FScavarContainerView>& GetContainers() const { return Containers; }
	float GetCarriedKg() const { return CarriedKg; }

	FSimpleMulticastDelegate OnInventoryChanged;

private:
	UFUNCTION() void OnRep_View();
	UFUNCTION(Client, Reliable) void ClientNotify(const FString& Message);

	bool Owns(Scavar::ContainerId Id) const;
	UScavarWorldInventory* World() const;
	UScavarVitalsComponent* Vitals() const;
	FVector FeetLocation() const;
	void HandleStoreChanged(const Scavar::ChangeSet& Changes);
	void RefreshView();
	void OnTreatmentFinished(bool bCompleted);

	UPROPERTY(ReplicatedUsing = OnRep_View) TArray<FScavarItemView> Items;
	UPROPERTY(ReplicatedUsing = OnRep_View) TArray<FScavarContainerView> Containers;
	UPROPERTY(Replicated) float CarriedKg = 0.f;

	// Server-only.
	FString AccountKey;
	Scavar::ContainerId Pockets = 0, Backpack = 0, Secure = 0, Stash = 0, Returns = 0;
	FDelegateHandle ChangedHandle;
	int64 PendingTreatmentItem = 0;
	bool bInitialized = false;
};
