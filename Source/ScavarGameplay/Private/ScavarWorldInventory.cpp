#include "ScavarWorldInventory.h"
#include "ScavarGameDataSubsystem.h"
#include "ScavarLog.h"
#include "ScavarPickup.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

bool UScavarWorldInventory::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld();
}

void UScavarWorldInventory::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (InWorld.GetNetMode() == NM_Client)
	{
		return; // clients never own item state
	}

	const UGameInstance* GameInstance = InWorld.GetGameInstance();
	const UScavarGameDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UScavarGameDataSubsystem>() : nullptr;
	if (!Data || !Data->IsLoaded())
	{
		UE_LOG(LogScavarInventory, Error, TEXT("Game data is not loaded; the world inventory is disabled."));
		return;
	}

	GameData = MakeUnique<Scavar::GameData>(Data->GetData());
	std::string Error;
	const std::string Dir = TCHAR_TO_UTF8(*UScavarGameDataSubsystem::ResolveDataDirectory());
	if (!Table.Load(Dir, *GameData, &Error) || !Table.BuildCatalog(Catalog))
	{
		UE_LOG(LogScavarInventory, Error, TEXT("Item table failed to load: %s"), UTF8_TO_TCHAR(Error.c_str()));
		return;
	}
	Store = MakeUnique<Scavar::ItemStore>(&Catalog);
	UE_LOG(LogScavarInventory, Log, TEXT("World inventory ready: %d item definitions."), (int32)Table.Rows.size());
}

Scavar::Status UScavarWorldInventory::Execute(TFunctionRef<Scavar::Status(Scavar::ItemStore&)> Op)
{
	if (!Store)
	{
		return Scavar::Status::InvalidArgument;
	}
	Store->BeginTx();
	const Scavar::Status Result = Op(*Store);
	if (Result != Scavar::Status::Ok)
	{
		Store->Rollback();
		Store->DrainChanges();
		return Result;
	}
	Store->Commit();
	Scavar::ChangeSet Changes = Store->DrainChanges();
	RetirePickups(Changes);
	if (PersistSink)
	{
		PersistSink(Changes);
	}
	OnChanged.Broadcast(Changes);
	return Result;
}

void UScavarWorldInventory::RetirePickups(Scavar::ChangeSet& Changes)
{
	TArray<Scavar::ContainerId> EmptyGrounds;
	for (const Scavar::ItemChange& Change : Changes.Items)
	{
		TWeakObjectPtr<AScavarPickup>* Found = Pickups.Find(Change.Id);
		if (!Found)
		{
			continue;
		}
		AScavarPickup* Pickup = Found->Get();
		const Scavar::ItemInstance* Item = Store->GetItem(Change.Id);
		const bool bStillOnGround = Pickup && Item && Item->Container == Pickup->GetGroundContainerId();
		if (bStillOnGround)
		{
			Pickup->SetQty(Item->Qty);
			continue;
		}
		if (Pickup)
		{
			EmptyGrounds.Add(Pickup->GetGroundContainerId());
			Pickup->Destroy();
		}
		Pickups.Remove(Change.Id);
	}

	// Ground containers are single-use: delete them once their item has left.
	bool bAny = false;
	Store->BeginTx();
	for (const Scavar::ContainerId Id : EmptyGrounds)
	{
		if (Store->RemoveContainer(Id) == Scavar::Status::Ok)
		{
			bAny = true;
		}
	}
	Store->Commit();
	if (bAny)
	{
		const Scavar::ChangeSet Extra = Store->DrainChanges();
		Changes.Containers.insert(Changes.Containers.end(), Extra.Containers.begin(), Extra.Containers.end());
		Changes.Items.insert(Changes.Items.end(), Extra.Items.begin(), Extra.Items.end());
	}
}

AScavarPickup* UScavarWorldInventory::SpawnPickup(FName DefId, int32 Qty, const FVector& Location, float Durability)
{
	UWorld* World = GetWorld();
	if (!Store || !World || Qty < 1)
	{
		return nullptr;
	}
	const std::string Def = TCHAR_TO_UTF8(*DefId.ToString());
	const Scavar::ItemRow* Row = Table.Find(Def);
	if (!Row)
	{
		UE_LOG(LogScavarInventory, Warning, TEXT("SpawnPickup: unknown item %s"), *DefId.ToString());
		return nullptr;
	}

	Scavar::ContainerId Ground = 0;
	Scavar::ItemId Item = 0;
	const Scavar::Status Result = Execute([&](Scavar::ItemStore& S)
	{
		Scavar::Container C;
		C.Kind = Scavar::ContainerKind::Ground;
		C.W = Row->W;
		C.H = Row->H;
		Scavar::Status St = S.AddContainer(C, &Ground);
		if (St != Scavar::Status::Ok) { return St; }
		return S.Create(Def, Qty, Ground, 0, 0, false, &Item, Durability);
	});
	if (Result != Scavar::Status::Ok)
	{
		UE_LOG(LogScavarInventory, Warning, TEXT("SpawnPickup %s failed: %s"), *DefId.ToString(), UTF8_TO_TCHAR(Scavar::StatusName(Result)));
		return nullptr;
	}

	return AttachPickup(Item, Ground, Location);
}

AScavarPickup* UScavarWorldInventory::AttachPickup(Scavar::ItemId Item, Scavar::ContainerId Ground, const FVector& Location)
{
	UWorld* World = GetWorld();
	const Scavar::ItemInstance* Instance = Store ? Store->GetItem(Item) : nullptr;
	if (!World || !Instance)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AScavarPickup* Pickup = World->SpawnActor<AScavarPickup>(AScavarPickup::StaticClass(), Location, FRotator::ZeroRotator, Params);
	if (Pickup)
	{
		Pickup->Configure(Item, Ground, FName(UTF8_TO_TCHAR(Instance->DefId.c_str())), Instance->Qty, World->GetTimeSeconds());
		Pickups.Add(Item, Pickup);
	}
	return Pickup;
}

AScavarPickup* UScavarWorldInventory::FindPickupByItem(int64 ItemId) const
{
	const TWeakObjectPtr<AScavarPickup>* Found = Pickups.Find(ItemId);
	return Found ? Found->Get() : nullptr;
}

void UScavarWorldInventory::ForEachPickup(TFunctionRef<void(AScavarPickup&)> Visit) const
{
	for (const TPair<int64, TWeakObjectPtr<AScavarPickup>>& Entry : Pickups)
	{
		if (AScavarPickup* Pickup = Entry.Value.Get())
		{
			Visit(*Pickup);
		}
	}
}

int64 UScavarWorldInventory::HashAccountKey(const FString& AccountKey)
{
	uint64 Hash = 1469598103934665603ULL; // FNV-1a 64
	FTCHARToUTF8 Utf8(*AccountKey);
	for (int32 i = 0; i < Utf8.Length(); ++i)
	{
		Hash ^= static_cast<uint8>(Utf8.Get()[i]);
		Hash *= 1099511628211ULL;
	}
	return static_cast<int64>((Hash >> 1) | 1ULL); // positive and never 0
}

Scavar::ContainerId UScavarWorldInventory::FindOwnedContainer(int64 OwnerHash, Scavar::ContainerKind Kind) const
{
	if (Store)
	{
		for (const auto& Entry : Store->AllContainers())
		{
			if (Entry.second.Owner == OwnerHash && Entry.second.Kind == Kind)
			{
				return Entry.first;
			}
		}
	}
	return 0;
}

void UScavarWorldInventory::GetAccountContainers(const FString& AccountKey, Scavar::ContainerId& OutStash, Scavar::ContainerId& OutReturns)
{
	FAccountContainers& Entry = Accounts.FindOrAdd(AccountKey);
	const int64 OwnerHash = HashAccountKey(AccountKey);
	if (Entry.Stash == 0) { Entry.Stash = FindOwnedContainer(OwnerHash, Scavar::ContainerKind::Stash); }
	if (Entry.Returns == 0) { Entry.Returns = FindOwnedContainer(OwnerHash, Scavar::ContainerKind::Returns); }
	if (Entry.Stash == 0 && Store)
	{
		Execute([&](Scavar::ItemStore& S)
		{
			Scavar::Container Stash;
			Stash.Owner = OwnerHash;
			Stash.Kind = Scavar::ContainerKind::Stash;
			Stash.W = 8;
			Stash.H = 10;
			Scavar::Status St = S.AddContainer(Stash, &Entry.Stash);
			if (St != Scavar::Status::Ok) { return St; }
			Scavar::Container Returns;
			Returns.Owner = OwnerHash;
			Returns.Kind = Scavar::ContainerKind::Returns;
			Returns.W = 10;
			Returns.H = 10;
			return S.AddContainer(Returns, &Entry.Returns);
		});
	}
	OutStash = Entry.Stash;
	OutReturns = Entry.Returns;
}
