#include "ScavarPlayerController.h"
#include "ScavarCharacter.h"
#include "ScavarCollision.h"
#include "ScavarInventoryComponent.h"
#include "ScavarLoot.h"
#include "ScavarLog.h"
#include "ScavarPickup.h"
#include "ScavarVitalsComponent.h"
#include "ScavarWeaponComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"

namespace
{
constexpr float InteractReachCm = 300.f;
}

void AScavarPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindAction(TEXT("Fire"), IE_Pressed, this, &AScavarPlayerController::FirePressed);
	InputComponent->BindAction(TEXT("Fire"), IE_Released, this, &AScavarPlayerController::FireReleased);
	InputComponent->BindAction(TEXT("Reload"), IE_Pressed, this, &AScavarPlayerController::ReloadPressed);
	InputComponent->BindAction(TEXT("FireMode"), IE_Pressed, this, &AScavarPlayerController::FireModePressed);
	InputComponent->BindAction(TEXT("Holster"), IE_Pressed, this, &AScavarPlayerController::HolsterPressed);
	InputComponent->BindAction(TEXT("Interact"), IE_Pressed, this, &AScavarPlayerController::InteractPressed);
	InputComponent->BindAction(TEXT("Inventory"), IE_Pressed, this, &AScavarPlayerController::ToggleInventory);
	InputComponent->BindAction(TEXT("DropModifier"), IE_Pressed, this, &AScavarPlayerController::DropModifierPressed);
	InputComponent->BindAction(TEXT("DropModifier"), IE_Released, this, &AScavarPlayerController::DropModifierReleased);
	InputComponent->BindAction(TEXT("Slot1"), IE_Pressed, this, &AScavarPlayerController::SlotAction<0>);
	InputComponent->BindAction(TEXT("Slot2"), IE_Pressed, this, &AScavarPlayerController::SlotAction<1>);
	InputComponent->BindAction(TEXT("Slot3"), IE_Pressed, this, &AScavarPlayerController::SlotAction<2>);
	InputComponent->BindAction(TEXT("Slot4"), IE_Pressed, this, &AScavarPlayerController::SlotAction<3>);
	InputComponent->BindAction(TEXT("Slot5"), IE_Pressed, this, &AScavarPlayerController::SlotAction<4>);
	InputComponent->BindAction(TEXT("Slot6"), IE_Pressed, this, &AScavarPlayerController::SlotAction<5>);
	InputComponent->BindAction(TEXT("Slot7"), IE_Pressed, this, &AScavarPlayerController::SlotAction<6>);
	InputComponent->BindAction(TEXT("Slot8"), IE_Pressed, this, &AScavarPlayerController::SlotAction<7>);
	InputComponent->BindAction(TEXT("Slot9"), IE_Pressed, this, &AScavarPlayerController::SlotAction<8>);
}

AScavarPickup* AScavarPlayerController::FindLookedAtPickup() const
{
	const AScavarCharacter* Character = Cast<AScavarCharacter>(GetPawn());
	if (!Character || !GetWorld())
	{
		return nullptr;
	}
	FVector Eye;
	FRotator Rotation;
	GetPlayerViewPoint(Eye, Rotation);
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ScavarInteract), false, Character);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Eye, Eye + Rotation.Vector() * InteractReachCm, SCAVAR_TRACE_INTERACT, Params))
	{
		return Cast<AScavarPickup>(Hit.GetActor());
	}
	return nullptr;
}

void AScavarPlayerController::FirePressed()
{
	if (const AScavarCharacter* Character = Cast<AScavarCharacter>(GetPawn())) { Character->GetWeapon()->ServerTrigger(true); }
}

void AScavarPlayerController::FireReleased()
{
	if (const AScavarCharacter* Character = Cast<AScavarCharacter>(GetPawn())) { Character->GetWeapon()->ServerTrigger(false); }
}

void AScavarPlayerController::ReloadPressed()
{
	if (const AScavarCharacter* Character = Cast<AScavarCharacter>(GetPawn())) { Character->GetWeapon()->ServerReload(); }
}

void AScavarPlayerController::FireModePressed()
{
	if (const AScavarCharacter* Character = Cast<AScavarCharacter>(GetPawn())) { Character->GetWeapon()->ServerToggleFireMode(); }
}

void AScavarPlayerController::HolsterPressed()
{
	if (const AScavarCharacter* Character = Cast<AScavarCharacter>(GetPawn())) { Character->GetWeapon()->ServerHolster(); }
}

void AScavarPlayerController::InteractPressed()
{
	AScavarPickup* Pickup = FindLookedAtPickup();
	const AScavarCharacter* Character = Cast<AScavarCharacter>(GetPawn());
	if (Pickup && Character && Character->GetInventory())
	{
		Character->GetInventory()->ServerPickup(Pickup);
	}
}

void AScavarPlayerController::ToggleInventory()
{
	bInventoryOpen = !bInventoryOpen;
}

void AScavarPlayerController::SlotPressed(int32 SlotIndex)
{
	const AScavarCharacter* Character = Cast<AScavarCharacter>(GetPawn());
	UScavarInventoryComponent* Inventory = Character ? Character->GetInventory() : nullptr;
	if (!Inventory || !Inventory->GetItems().IsValidIndex(SlotIndex))
	{
		return;
	}
	const FScavarItemView& Item = Inventory->GetItems()[SlotIndex];
	if (bDropModifier)
	{
		Inventory->ServerDropItem(Item.ItemId);
	}
	else
	{
		Inventory->ServerUseItem(Item.ItemId);
	}
}

bool AScavarPlayerController::CheatsAllowed() const
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return true;
#endif
}

void AScavarPlayerController::ScavarGive(FName DefId, int32 Qty) { ServerGive(DefId, Qty); }
void AScavarPlayerController::ScavarHit(int32 Zone, float Damage, int32 PenTier) { ServerHit(Zone, Damage, PenTier); }
void AScavarPlayerController::ScavarKill() { ServerKill(); }
void AScavarPlayerController::ScavarHeal() { ServerHeal(); }
void AScavarPlayerController::ScavarLootCycle() { ServerLootCycle(); }

void AScavarPlayerController::ServerGive_Implementation(FName DefId, int32 Qty)
{
	const AScavarCharacter* Character = Cast<AScavarCharacter>(GetPawn());
	if (CheatsAllowed() && Character && Character->GetInventory() && Qty > 0 && Qty <= 1000)
	{
		const bool bOk = Character->GetInventory()->GiveItem(DefId, Qty);
		UE_LOG(LogScavar, Log, TEXT("[cheat] give %s x%d -> %s"), *DefId.ToString(), Qty, bOk ? TEXT("ok") : TEXT("no room / unknown item"));
	}
}

void AScavarPlayerController::ServerHit_Implementation(int32 Zone, float Damage, int32 PenTier)
{
	const AScavarCharacter* Character = Cast<AScavarCharacter>(GetPawn());
	if (CheatsAllowed() && Character && Zone >= 0 && Zone < 6 && Damage >= 0.f && Damage <= 1000.f && PenTier >= 1 && PenTier <= 6)
	{
		Character->GetVitals()->ApplyProjectileHit(static_cast<EScavarHitZone>(Zone), Damage, PenTier);
	}
}

void AScavarPlayerController::ServerKill_Implementation()
{
	const AScavarCharacter* Character = Cast<AScavarCharacter>(GetPawn());
	if (CheatsAllowed() && Character)
	{
		Character->GetVitals()->ApplyProjectileHit(EScavarHitZone::Head, 1000.f, 1);
	}
}

void AScavarPlayerController::ServerHeal_Implementation()
{
	const AScavarCharacter* Character = Cast<AScavarCharacter>(GetPawn());
	if (CheatsAllowed() && Character)
	{
		Character->GetVitals()->DebugRestoreAll();
	}
}

void AScavarPlayerController::ServerLootCycle_Implementation()
{
	if (CheatsAllowed() && GetWorld())
	{
		if (UScavarLootSubsystem* Loot = GetWorld()->GetSubsystem<UScavarLootSubsystem>())
		{
			Loot->RunCycleNow();
		}
	}
}
