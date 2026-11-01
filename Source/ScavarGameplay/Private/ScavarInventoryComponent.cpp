#include "ScavarInventoryComponent.h"
#include "ScavarPickup.h"
#include "ScavarVitalsComponent.h"
#include "ScavarWorldInventory.h"
#include "ScavarLog.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"

namespace
{
constexpr float MaxInteractDistanceCm = 320.f;

FName ToName(const std::string& S) { return FName(UTF8_TO_TCHAR(S.c_str())); }

// Removes exactly one unit of a stack. The store has no "decrement", so a stack is split into a scratch container which is then deleted.
Scavar::Status ConsumeOne(Scavar::ItemStore& S, Scavar::ItemId Id)
{
	const Scavar::ItemInstance* Item = S.GetItem(Id);
	if (!Item)
	{
		return Scavar::Status::NoSuchItem;
	}
	if (Item->Qty == 1)
	{
		return S.Remove(Id);
	}
	Scavar::Container Scratch;
	Scratch.Kind = Scavar::ContainerKind::Ground;
	Scratch.W = 1;
	Scratch.H = 1;
	Scavar::ContainerId ScratchId = 0;
	Scavar::Status St = S.AddContainer(Scratch, &ScratchId);
	if (St != Scavar::Status::Ok) { return St; }
	Scavar::ItemId One = 0;
	St = S.Split(Id, 1, ScratchId, 0, 0, false, &One);
	if (St != Scavar::Status::Ok) { return St; }
	St = S.Remove(One);
	if (St != Scavar::Status::Ok) { return St; }
	return S.RemoveContainer(ScratchId);
}
}

UScavarInventoryComponent::UScavarInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UScavarInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UScavarInventoryComponent, Items, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UScavarInventoryComponent, Containers, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UScavarInventoryComponent, CarriedKg, COND_OwnerOnly);
}

void UScavarInventoryComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UScavarWorldInventory* Inventory = World())
	{
		Inventory->OnChanged.Remove(ChangedHandle);
	}
	Super::EndPlay(Reason);
}

UScavarWorldInventory* UScavarInventoryComponent::World() const
{
	return GetWorld() ? GetWorld()->GetSubsystem<UScavarWorldInventory>() : nullptr;
}

UScavarVitalsComponent* UScavarInventoryComponent::Vitals() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UScavarVitalsComponent>() : nullptr;
}

FVector UScavarInventoryComponent::FeetLocation() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->GetActorLocation() - FVector(0.f, 0.f, 80.f) : FVector::ZeroVector;
}

bool UScavarInventoryComponent::Owns(Scavar::ContainerId Id) const
{
	return Id != 0 && (Id == Pockets || Id == Backpack || Id == Secure);
}

void UScavarInventoryComponent::InitializeForPlayer(const FString& InAccountKey, bool bGiveStarterKit)
{
	UScavarWorldInventory* Inventory = World();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Inventory || !Inventory->IsReady() || bInitialized)
	{
		return;
	}
	AccountKey = InAccountKey;
	Inventory->GetAccountContainers(AccountKey, Stash, Returns);

	Inventory->Execute([&](Scavar::ItemStore& S)
	{
		Scavar::Container C;
		C.Kind = Scavar::ContainerKind::Pockets; C.W = 4; C.H = 1;
		Scavar::Status St = S.AddContainer(C, &Pockets);
		if (St != Scavar::Status::Ok) { return St; }
		C.Kind = Scavar::ContainerKind::Backpack; C.W = 4; C.H = 5;
		St = S.AddContainer(C, &Backpack);
		if (St != Scavar::Status::Ok) { return St; }
		C.Kind = Scavar::ContainerKind::Secure; C.W = 2; C.H = 2;
		St = S.AddContainer(C, &Secure);
		if (St != Scavar::Status::Ok) { return St; }

		// Items rescued from the previous life's secure pocket come back first (docs/03 section 2, pilot: no hub, so automatically).
		const std::vector<Scavar::ItemId> Waiting = S.ItemsIn(Returns);
		for (const Scavar::ItemId Id : Waiting)
		{
			S.AutoMove(Id, Secure, true, Returns); // leftovers stay safely in Returns until there is room
		}
		return Scavar::Status::Ok;
	});
	bInitialized = true;
	ChangedHandle = Inventory->OnChanged.AddUObject(this, &UScavarInventoryComponent::HandleStoreChanged);

	if (bGiveStarterKit)
	{
		GiveItem(TEXT("MED_BANDAGE"), 1);
		GiveItem(TEXT("FOOD_BREAD"), 1);
		GiveItem(TEXT("DRINK_WATER_BOTTLE"), 1);
		GiveItem(TEXT("M_KNIFE"), 1);
	}

	if (UScavarVitalsComponent* V = Vitals())
	{
		V->OnTreatmentFinished.AddUObject(this, &UScavarInventoryComponent::OnTreatmentFinished);
	}
	RefreshView();
}

bool UScavarInventoryComponent::GiveItem(FName DefId, int32 Qty)
{
	UScavarWorldInventory* Inventory = World();
	if (!Inventory || !bInitialized)
	{
		return false;
	}
	const std::string Def = TCHAR_TO_UTF8(*DefId.ToString());
	const Scavar::Status Result = Inventory->Execute([&](Scavar::ItemStore& S)
	{
		Scavar::Status St = S.AutoPlace(Def, Qty, Pockets, true);
		if (St != Scavar::Status::Ok) { St = S.AutoPlace(Def, Qty, Backpack, true); }
		return St;
	});
	return Result == Scavar::Status::Ok;
}

void UScavarInventoryComponent::HandleOwnerDied()
{
	UScavarWorldInventory* Inventory = World();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Inventory || !bInitialized)
	{
		return;
	}

	const FVector Origin = FeetLocation();
	struct FDrop { Scavar::ItemId Item; Scavar::ContainerId Ground; };
	TArray<FDrop> Drops;

	Inventory->Execute([&](Scavar::ItemStore& S)
	{
		Scavar::Status St = S.ResolveDeath(Secure, Returns, {Pockets, Backpack});
		if (St != Scavar::Status::Ok) { return St; }
		// Everything left on the body becomes loose items around the corpse (pilot simplification of corpse looting).
		for (const Scavar::ContainerId Body : {Pockets, Backpack})
		{
			const std::vector<Scavar::ItemId> Contents = S.ItemsIn(Body);
			for (const Scavar::ItemId Id : Contents)
			{
				const Scavar::ItemInstance* Item = S.GetItem(Id);
				const Scavar::ItemRow* Row = Inventory->GetTable().Find(Item->DefId);
				if (!Row) { continue; }
				Scavar::Container G;
				G.Kind = Scavar::ContainerKind::Ground; G.W = Row->W; G.H = Row->H;
				Scavar::ContainerId Ground = 0;
				St = S.AddContainer(G, &Ground);
				if (St != Scavar::Status::Ok) { return St; }
				St = S.Move(Id, Ground, 0, 0, false, Body);
				if (St != Scavar::Status::Ok) { return St; }
				Drops.Add({Id, Ground});
			}
			St = S.RemoveContainer(Body);
			if (St != Scavar::Status::Ok) { return St; }
		}
		return Scavar::Status::Ok;
	});

	int32 Index = 0;
	for (const FDrop& Drop : Drops)
	{
		const float Angle = (2.f * PI * Index++) / FMath::Max(1, Drops.Num());
		const FVector Offset(FMath::Cos(Angle) * 60.f, FMath::Sin(Angle) * 60.f, 8.f);
		Inventory->AttachPickup(Drop.Item, Drop.Ground, Origin + Offset);
	}
	Pockets = Backpack = Secure = 0;
	Inventory->OnChanged.Remove(ChangedHandle);
	bInitialized = false;
	Items.Reset();
	Containers.Reset();
}

void UScavarInventoryComponent::ServerMoveItem_Implementation(int64 ItemId, int64 DstContainer, int32 X, int32 Y, bool bRot)
{
	UScavarWorldInventory* Inventory = World();
	if (!Inventory || !bInitialized || !Owns(DstContainer) || X < 0 || Y < 0 || X > 64 || Y > 64)
	{
		return;
	}
	const Scavar::ItemInstance* Item = Inventory->GetStore()->GetItem(ItemId);
	if (!Item || !Owns(Item->Container))
	{
		return; // only items already in my own containers may be rearranged; pickups use ServerPickup
	}
	const Scavar::ContainerId Src = Item->Container;
	const Scavar::Status Result = Inventory->Execute([&](Scavar::ItemStore& S) { return S.Move(ItemId, DstContainer, X, Y, bRot, Src); });
	if (Result != Scavar::Status::Ok)
	{
		ClientNotify(FString::Printf(TEXT("Move failed: %s"), UTF8_TO_TCHAR(Scavar::StatusName(Result))));
	}
}

void UScavarInventoryComponent::ServerPickup_Implementation(AScavarPickup* Pickup)
{
	UScavarWorldInventory* Inventory = World();
	if (!Inventory || !bInitialized || !IsValid(Pickup) || !GetOwner())
	{
		return;
	}
	if (FVector::Dist(Pickup->GetActorLocation(), GetOwner()->GetActorLocation()) > MaxInteractDistanceCm)
	{
		return;
	}
	const Scavar::ItemId Id = Pickup->GetItemId();
	const Scavar::ContainerId Ground = Pickup->GetGroundContainerId();
	const Scavar::Status Result = Inventory->Execute([&](Scavar::ItemStore& S)
	{
		Scavar::Status St = S.AutoMove(Id, Pockets, true, Ground);
		if (St != Scavar::Status::Ok) { St = S.AutoMove(Id, Backpack, true, Ground); }
		return St;
	});
	if (Result != Scavar::Status::Ok)
	{
		ClientNotify(Result == Scavar::Status::NoSpace ? TEXT("No room in pockets or backpack.") : TEXT("Cannot pick that up."));
	}
}

void UScavarInventoryComponent::ServerDropItem_Implementation(int64 ItemId)
{
	UScavarWorldInventory* Inventory = World();
	if (!Inventory || !bInitialized)
	{
		return;
	}
	const Scavar::ItemInstance* Item = Inventory->GetStore()->GetItem(ItemId);
	if (!Item || !Owns(Item->Container) || Item->Container == Secure)
	{
		return;
	}
	const Scavar::ItemRow* Row = Inventory->GetTable().Find(Item->DefId);
	if (!Row)
	{
		return;
	}
	const Scavar::ContainerId Src = Item->Container;
	Scavar::ContainerId Ground = 0;
	const Scavar::Status Result = Inventory->Execute([&](Scavar::ItemStore& S)
	{
		Scavar::Container G;
		G.Kind = Scavar::ContainerKind::Ground; G.W = Row->W; G.H = Row->H;
		Scavar::Status St = S.AddContainer(G, &Ground);
		if (St != Scavar::Status::Ok) { return St; }
		return S.Move(ItemId, Ground, 0, 0, false, Src);
	});
	if (Result == Scavar::Status::Ok)
	{
		const FVector Forward = GetOwner()->GetActorForwardVector() * 80.f;
		Inventory->AttachPickup(ItemId, Ground, FeetLocation() + Forward + FVector(0.f, 0.f, 8.f));
	}
}

void UScavarInventoryComponent::ServerUseItem_Implementation(int64 ItemId)
{
	UScavarWorldInventory* Inventory = World();
	UScavarVitalsComponent* V = Vitals();
	if (!Inventory || !V || !bInitialized || V->IsDead())
	{
		return;
	}
	const Scavar::ItemInstance* Item = Inventory->GetStore()->GetItem(ItemId);
	if (!Item || !Owns(Item->Container))
	{
		return;
	}
	const Scavar::ConsumableEffect* Effect = Inventory->GetTable().FindConsumable(Item->DefId);
	if (!Effect)
	{
		return;
	}

	if (Effect->Treatment.has_value())
	{
		if (PendingTreatmentItem != 0)
		{
			return; // one treatment at a time
		}
		const EScavarTreatment Kind = static_cast<EScavarTreatment>(static_cast<uint8>(*Effect->Treatment));
		EScavarHitZone Zone = EScavarHitZone::Torso;
		if (V->FindTreatableZone(Kind, Zone) && V->BeginTreatment(Kind, Zone))
		{
			PendingTreatmentItem = ItemId; // consumed only if the treatment completes (AT-020)
		}
		else
		{
			ClientNotify(TEXT("Nothing to treat."));
		}
		return;
	}

	V->AddNutrition(Effect->FoodGain, Effect->WaterGain);
	Inventory->Execute([&](Scavar::ItemStore& S) { return ConsumeOne(S, ItemId); });
}

void UScavarInventoryComponent::OnTreatmentFinished(bool bCompleted)
{
	UScavarWorldInventory* Inventory = World();
	const int64 ItemId = PendingTreatmentItem;
	PendingTreatmentItem = 0;
	if (bCompleted && Inventory && ItemId != 0 && Inventory->GetStore()->GetItem(ItemId))
	{
		Inventory->Execute([&](Scavar::ItemStore& S) { return ConsumeOne(S, ItemId); });
	}
}

void UScavarInventoryComponent::HandleStoreChanged(const Scavar::ChangeSet& Changes)
{
	if (bInitialized && !Changes.Empty())
	{
		RefreshView();
	}
}

void UScavarInventoryComponent::RefreshView()
{
	UScavarWorldInventory* Inventory = World();
	if (!Inventory || !Inventory->GetStore())
	{
		return;
	}
	const Scavar::ItemStore& Store = *Inventory->GetStore();
	TArray<FScavarItemView> NewItems;
	TArray<FScavarContainerView> NewContainers;
	float Kg = 0.f;
	for (const Scavar::ContainerId Id : {Pockets, Backpack, Secure})
	{
		const Scavar::Container* C = Store.GetContainer(Id);
		if (!C) { continue; }
		FScavarContainerView CV;
		CV.ContainerId = Id; CV.Kind = static_cast<uint8>(C->Kind); CV.W = C->W; CV.H = C->H;
		NewContainers.Add(CV);
		Kg += Store.ContainerWeight(Id);
		for (const Scavar::ItemId ItemId : Store.ItemsIn(Id))
		{
			const Scavar::ItemInstance* It = Store.GetItem(ItemId);
			FScavarItemView IV;
			IV.ItemId = ItemId; IV.DefId = ToName(It->DefId); IV.Qty = It->Qty; IV.ContainerId = Id;
			IV.X = It->X; IV.Y = It->Y; IV.bRot = It->Rot;
			NewItems.Add(IV);
		}
	}
	Items = MoveTemp(NewItems);
	Containers = MoveTemp(NewContainers);
	CarriedKg = Kg;
	if (UScavarVitalsComponent* V = Vitals())
	{
		V->SetCarriedKg(Kg);
	}
	OnInventoryChanged.Broadcast();
}

void UScavarInventoryComponent::OnRep_View()
{
	if (UScavarVitalsComponent* V = Vitals())
	{
		V->SetCarriedKg(CarriedKg);
	}
	OnInventoryChanged.Broadcast();
}

void UScavarInventoryComponent::ClientNotify_Implementation(const FString& Message)
{
	UE_LOG(LogScavarInventory, Log, TEXT("[inventory] %s"), *Message);
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
		{
			PC->ClientMessage(Message);
		}
	}
}
