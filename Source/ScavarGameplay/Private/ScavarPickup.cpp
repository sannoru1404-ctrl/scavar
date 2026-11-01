#include "ScavarPickup.h"
#include "ScavarCollision.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/CollisionProfile.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AScavarPickup::AScavarPickup()
{
	bReplicates = true;
	SetReplicateMovement(false);
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		Mesh->SetStaticMesh(Cube.Object);
	}
	Mesh->SetRelativeScale3D(FVector(0.25f, 0.25f, 0.12f));
	Mesh->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	Mesh->SetCollisionResponseToChannel(SCAVAR_TRACE_INTERACT, ECR_Block);

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(Mesh);
	Label->SetRelativeLocation(FVector(0.f, 0.f, 140.f));
	Label->SetWorldSize(18.f);
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AScavarPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AScavarPickup, DefId);
	DOREPLIFETIME(AScavarPickup, Qty);
}

void AScavarPickup::Configure(int64 InItemId, int64 InGroundContainerId, FName InDefId, int32 InQty, double InSpawnTimeSeconds)
{
	ItemId = InItemId;
	GroundContainerId = InGroundContainerId;
	DefId = InDefId;
	Qty = InQty;
	SpawnTimeSeconds = InSpawnTimeSeconds;
	RefreshLabel();
}

void AScavarPickup::SetQty(int32 NewQty)
{
	Qty = NewQty;
	RefreshLabel();
}

void AScavarPickup::OnRep_Label()
{
	RefreshLabel();
}

void AScavarPickup::RefreshLabel()
{
	if (Label)
	{
		Label->SetText(FText::FromString(Qty > 1 ? FString::Printf(TEXT("%s x%d"), *DefId.ToString(), Qty) : DefId.ToString()));
	}
}
