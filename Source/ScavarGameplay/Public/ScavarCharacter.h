#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ScavarCharacter.generated.h"

class UCameraComponent;
class UScavarVitalsComponent;
class UScavarInventoryComponent;
class UScavarWeaponComponent;

/** First-person survivor. Movement speeds come from Scavar::MoveSpeed (SYS-02); the server validates sprinting. */
UCLASS()
class SCAVARGAMEPLAY_API AScavarCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AScavarCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Landed(const FHitResult& Hit) override;
	virtual void OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode) override;

	virtual void PossessedBy(AController* NewController) override;

	UScavarVitalsComponent* GetVitals() const { return Vitals; }
	UScavarInventoryComponent* GetInventory() const { return Inventory; }
	UScavarWeaponComponent* GetWeapon() const { return Weapon; }

protected:
	void MoveForward(float Value);
	void MoveRight(float Value);
	void LookUp(float Value);
	void Turn(float Value);
	void SprintPressed();
	void SprintReleased();
	void CrouchPressed();
	void CrouchReleased();

	UFUNCTION(Server, Reliable)
	void ServerSetSprint(bool bNewWantsSprint);

	UFUNCTION()
	void HandleDied(AActor* Victim);

	void UpdateMovementSpeed();
	void Respawn(AController* DeadController);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Scavar") TObjectPtr<UCameraComponent> FirstPersonCamera;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Scavar") TObjectPtr<UScavarVitalsComponent> Vitals;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Scavar") TObjectPtr<UScavarInventoryComponent> Inventory;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Scavar") TObjectPtr<UScavarWeaponComponent> Weapon;

	/** Seconds the corpse stays before the player is respawned (the body itself lives BodyLifetimeSeconds). */
	UPROPERTY(EditDefaultsOnly, Category = "Scavar") float RespawnDelaySeconds = 5.f;
	UPROPERTY(EditDefaultsOnly, Category = "Scavar") float BodyLifetimeSeconds = 3600.f;

	UPROPERTY(Replicated) bool bWantsSprint = false;

private:
	float FallStartZ = 0.f;
	bool bTrackingFall = false;
};
