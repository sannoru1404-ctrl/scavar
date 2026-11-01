#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Sim/Items.h"
#include "Sim/Inventory.h"
#include "ScavarWorldInventory.generated.h"

class AScavarPickup;

DECLARE_MULTICAST_DELEGATE_OneParam(FScavarStoreChanged, const Scavar::ChangeSet&);

/**
 * The one authoritative ItemStore of the world (server only). Every mutation goes through Execute(): one transaction, then the
 * change set is handed to listeners (replicated views, persistence). Pickups are visual handles for items lying in Ground containers.
 */
UCLASS()
class SCAVARGAMEPLAY_API UScavarWorldInventory : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	bool IsReady() const { return Store.IsValid(); }
	const Scavar::ItemTable& GetTable() const { return Table; }
	const Scavar::GameData& GetGameData() const { return *GameData; }
	const Scavar::ItemStore* GetStore() const { return Store.Get(); }
	/** Only for the save loader at startup: loading replaces the whole store and must not run inside Execute(). */
	Scavar::ItemStore* GetStoreForLoad() { return Store.Get(); }

	/** Runs Op inside a transaction; rolls back and returns the error if Op fails. Server only. */
	Scavar::Status Execute(TFunctionRef<Scavar::Status(Scavar::ItemStore&)> Op);

	/** Creates the item in its own Ground container and a visible pickup at Location. */
	AScavarPickup* SpawnPickup(FName DefId, int32 Qty, const FVector& Location, float Durability = 100.f);

	/** Gives an item that already sits alone in its Ground container a visible pickup actor (drops, body loot). */
	AScavarPickup* AttachPickup(Scavar::ItemId Item, Scavar::ContainerId Ground, const FVector& Location);

	AScavarPickup* FindPickupByItem(int64 ItemId) const;
	void ForEachPickup(TFunctionRef<void(AScavarPickup&)> Visit) const;

	/** Stable 63-bit id of an account key; stored as the Owner of every container the account owns (stash, returns, body). */
	static int64 HashAccountKey(const FString& AccountKey);
	/** First container of the given kind owned by OwnerHash, or 0. */
	Scavar::ContainerId FindOwnedContainer(int64 OwnerHash, Scavar::ContainerKind Kind) const;

	/** Account-level containers (stash, returns for items rescued from the secure pocket). Created on first use. */
	void GetAccountContainers(const FString& AccountKey, Scavar::ContainerId& OutStash, Scavar::ContainerId& OutReturns);

	FScavarStoreChanged OnChanged;
	/** Persistence hook (SYS-12): called with every committed change set. */
	TFunction<void(const Scavar::ChangeSet&)> PersistSink;

private:
	void RetirePickups(Scavar::ChangeSet& Changes);

	TUniquePtr<Scavar::GameData> GameData;
	Scavar::ItemTable Table;
	Scavar::ItemCatalog Catalog;
	TUniquePtr<Scavar::ItemStore> Store;

	TMap<int64, TWeakObjectPtr<AScavarPickup>> Pickups; // by item id
	struct FAccountContainers { Scavar::ContainerId Stash = 0; Scavar::ContainerId Returns = 0; };
	TMap<FString, FAccountContainers> Accounts;
};
