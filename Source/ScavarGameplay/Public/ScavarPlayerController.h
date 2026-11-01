#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ScavarPlayerController.generated.h"

class AScavarPickup;

/** Input routing for the pilot UI plus development console commands (disabled in Shipping builds). */
UCLASS()
class SCAVARGAMEPLAY_API AScavarPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void SetupInputComponent() override;

	/** The pickup the camera currently points at within reach (client side, for the HUD prompt). */
	AScavarPickup* FindLookedAtPickup() const;
	bool IsInventoryOpen() const { return bInventoryOpen; }

	// ---- Development commands (type in the console, ~) ----
	UFUNCTION(Exec) void ScavarGive(FName DefId, int32 Qty = 1);
	UFUNCTION(Exec) void ScavarHit(int32 Zone, float Damage, int32 PenTier = 3);
	UFUNCTION(Exec) void ScavarKill();
	UFUNCTION(Exec) void ScavarHeal();
	UFUNCTION(Exec) void ScavarLootCycle();
	UFUNCTION(Exec) void ScavarSetHour(float Hour);

protected:
	UFUNCTION(Server, Reliable) void ServerGive(FName DefId, int32 Qty);
	UFUNCTION(Server, Reliable) void ServerHit(int32 Zone, float Damage, int32 PenTier);
	UFUNCTION(Server, Reliable) void ServerKill();
	UFUNCTION(Server, Reliable) void ServerHeal();
	UFUNCTION(Server, Reliable) void ServerLootCycle();
	UFUNCTION(Server, Reliable) void ServerSetHour(float Hour);

	void FirePressed();
	void FireReleased();
	void ReloadPressed();
	void FireModePressed();
	void HolsterPressed();
	void InteractPressed();
	void ToggleInventory();
	void SlotPressed(int32 SlotIndex);
	template <int32 Index> void SlotAction() { SlotPressed(Index); }
	void DropModifierPressed() { bDropModifier = true; }
	void DropModifierReleased() { bDropModifier = false; }

private:
	bool CheatsAllowed() const;

	bool bInventoryOpen = false;
	bool bDropModifier = false;
};
