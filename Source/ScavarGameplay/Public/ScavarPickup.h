#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ScavarPickup.generated.h"

class UStaticMeshComponent;
class UTextRenderComponent;

/** An item lying in the world. Authority lives in the server's ItemStore (a private Ground container); this actor is only its visual and interaction handle. */
UCLASS()
class SCAVARGAMEPLAY_API AScavarPickup : public AActor
{
	GENERATED_BODY()

public:
	AScavarPickup();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server only. */
	void Configure(int64 InItemId, int64 InGroundContainerId, FName InDefId, int32 InQty, double InSpawnTimeSeconds);
	void SetQty(int32 NewQty);

	int64 GetItemId() const { return ItemId; }
	int64 GetGroundContainerId() const { return GroundContainerId; }
	double GetSpawnTimeSeconds() const { return SpawnTimeSeconds; }
	FName GetDefId() const { return DefId; }
	int32 GetQty() const { return Qty; }

protected:
	UFUNCTION() void OnRep_Label();
	void RefreshLabel();

	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mesh;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> Label;

	UPROPERTY(ReplicatedUsing = OnRep_Label) FName DefId;
	UPROPERTY(ReplicatedUsing = OnRep_Label) int32 Qty = 1;

	// Server-only bookkeeping.
	int64 ItemId = 0;
	int64 GroundContainerId = 0;
	double SpawnTimeSeconds = 0.0;
};
