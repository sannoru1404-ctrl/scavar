#include "ScavarNPC.h"
#include "ScavarVitalsComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AScavarNPC::AScavarNPC()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);
	AIControllerClass = nullptr; // steering is done directly; see Tick()
	AutoPossessAI = EAutoPossessAI::Disabled;

	Vitals = CreateDefaultSubobject<UScavarVitalsComponent>(TEXT("Vitals"));

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(GetCapsuleComponent());
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cylinder.Succeeded())
	{
		Body->SetStaticMesh(Cylinder.Object);
	}
	Body->SetRelativeScale3D(FVector(0.5f, 0.5f, 1.7f));

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(GetCapsuleComponent());
	Label->SetRelativeLocation(FVector(0.f, 0.f, 120.f));
	Label->SetWorldSize(20.f);
	Label->SetHorizontalAlignment(EHTA_Center);

	GetCharacterMovement()->MaxWalkSpeed = 380.f;
	GetCharacterMovement()->bRunPhysicsWithNoController = true; // NPCs are steered by the AI subsystem, never possessed
	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
}

void AScavarNPC::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AScavarNPC, Archetype);
}

void AScavarNPC::InitBrain(int64 NpcId, EScavarArchetype InArchetype, int64 InGroupId)
{
	Archetype = InArchetype;
	GroupId = InGroupId;
	BrainState = Scavar::AiNpc();
	BrainState.Id = NpcId;
	BrainState.Arch = static_cast<Scavar::AiArchetype>(static_cast<uint8>(InArchetype));
	BrainState.Home = Scavar::Vec3{GetActorLocation().X / 100.0, GetActorLocation().Y / 100.0, GetActorLocation().Z / 100.0};
	BrainState.Pos = BrainState.Home;
	if (Vitals)
	{
		Vitals->OnDied.AddDynamic(this, &AScavarNPC::HandleDied);
	}
	OnRep_Archetype();
}

void AScavarNPC::OnRep_Archetype()
{
	if (Label)
	{
		Label->SetText(FText::FromString(StaticEnum<EScavarArchetype>()->GetNameStringByValue(static_cast<int64>(Archetype))));
	}
	const Scavar::ArchetypeDef& Def = Scavar::ArchetypeFor(static_cast<Scavar::AiArchetype>(static_cast<uint8>(Archetype)));
	GetCharacterMovement()->MaxWalkSpeed = Def.RunSpeed * 100.f;
}

float AScavarNPC::GetHealthFraction() const
{
	if (!Vitals)
	{
		return 1.f;
	}
	const FScavarVitalsSnapshot& S = Vitals->GetSnapshot();
	float Sum = 0.f;
	for (const float Hp : S.ZoneHp) { Sum += Hp; }
	const float Max = 35.f + 120.f + 60.f + 60.f + 80.f + 80.f;
	return FMath::Clamp(Sum / Max, 0.f, 1.f);
}

void AScavarNPC::SetMoveGoal(const FVector& InGoalCm, bool bRun)
{
	GoalCm = InGoalCm;
	bRunToGoal = bRun;
	bHasGoal = true;
}

void AScavarNPC::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || Vitals->IsDead())
	{
		return;
	}
	BrainState.Pos = Scavar::Vec3{GetActorLocation().X / 100.0, GetActorLocation().Y / 100.0, GetActorLocation().Z / 100.0};
	BrainState.HealthFraction = GetHealthFraction();

	if (bHasGoal)
	{
		FVector ToGoal = GoalCm - GetActorLocation();
		ToGoal.Z = 0.f;
		if (ToGoal.SizeSquared() > FMath::Square(80.f))
		{
			const Scavar::ArchetypeDef& Def = Scavar::ArchetypeFor(BrainState.Arch);
			GetCharacterMovement()->MaxWalkSpeed = (bRunToGoal ? Def.RunSpeed : Def.WalkSpeed) * 100.f;
			AddMovementInput(ToGoal.GetSafeNormal(), 1.f);
		}
	}
}

void AScavarNPC::HandleDied(AActor* /*Victim*/)
{
	bHasGoal = false;
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	SetLifeSpan(300.f);
	OnNpcDied.Broadcast();
}
