#include "ScavarCharacter.h"
#include "ScavarVitalsComponent.h"
#include "ScavarLog.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"

AScavarCharacter::AScavarCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);

	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
	FirstPersonCamera->SetRelativeLocation(FVector(0.f, 0.f, 64.f));
	FirstPersonCamera->bUsePawnControlRotation = true;

	Vitals = CreateDefaultSubobject<UScavarVitalsComponent>(TEXT("Vitals"));

	bUseControllerRotationYaw = true;
	GetCharacterMovement()->NavAgentProps.bCanCrouch = true;
	GetCharacterMovement()->JumpZVelocity = 350.f;
	GetCharacterMovement()->MaxAcceleration = 1200.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 1400.f;
	UpdateMovementSpeed();
}

void AScavarCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AScavarCharacter, bWantsSprint);
}

void AScavarCharacter::BeginPlay()
{
	Super::BeginPlay();
	if (Vitals)
	{
		Vitals->OnDied.AddDynamic(this, &AScavarCharacter::HandleDied);
	}
}

void AScavarCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	// Axis/action names are mapped in Config/DefaultInput.ini.
	PlayerInputComponent->BindAxis(TEXT("MoveForward"), this, &AScavarCharacter::MoveForward);
	PlayerInputComponent->BindAxis(TEXT("MoveRight"), this, &AScavarCharacter::MoveRight);
	PlayerInputComponent->BindAxis(TEXT("LookUp"), this, &AScavarCharacter::LookUp);
	PlayerInputComponent->BindAxis(TEXT("Turn"), this, &AScavarCharacter::Turn);
	PlayerInputComponent->BindAction(TEXT("Jump"), IE_Pressed, this, &ACharacter::Jump);
	PlayerInputComponent->BindAction(TEXT("Jump"), IE_Released, this, &ACharacter::StopJumping);
	PlayerInputComponent->BindAction(TEXT("Sprint"), IE_Pressed, this, &AScavarCharacter::SprintPressed);
	PlayerInputComponent->BindAction(TEXT("Sprint"), IE_Released, this, &AScavarCharacter::SprintReleased);
	PlayerInputComponent->BindAction(TEXT("Crouch"), IE_Pressed, this, &AScavarCharacter::CrouchPressed);
	PlayerInputComponent->BindAction(TEXT("Crouch"), IE_Released, this, &AScavarCharacter::CrouchReleased);
}

void AScavarCharacter::MoveForward(float Value)
{
	if (Value != 0.f) { AddMovementInput(GetActorForwardVector(), Value); }
}

void AScavarCharacter::MoveRight(float Value)
{
	if (Value != 0.f) { AddMovementInput(GetActorRightVector(), Value); }
}

void AScavarCharacter::LookUp(float Value) { AddControllerPitchInput(Value); }
void AScavarCharacter::Turn(float Value) { AddControllerYawInput(Value); }

void AScavarCharacter::SprintPressed()
{
	bWantsSprint = true;
	ServerSetSprint(true);
}

void AScavarCharacter::SprintReleased()
{
	bWantsSprint = false;
	ServerSetSprint(false);
}

void AScavarCharacter::ServerSetSprint_Implementation(bool bNewWantsSprint)
{
	bWantsSprint = bNewWantsSprint;
}

void AScavarCharacter::CrouchPressed() { Crouch(); }
void AScavarCharacter::CrouchReleased() { UnCrouch(); }

void AScavarCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Vitals->IsDead())
	{
		return;
	}

	UpdateMovementSpeed();

	// Server tells the vitals component how the character is moving so stamina drains correctly.
	if (HasAuthority())
	{
		const float Speed2D = GetVelocity().Size2D();
		Scavar::MoveMode Mode = Scavar::MoveMode::Rest;
		if (Speed2D > 10.f)
		{
			Mode = (bWantsSprint && Vitals->CanSprint()) ? Scavar::MoveMode::Sprint : Scavar::MoveMode::Jog;
		}
		Vitals->SetMoveMode(Mode);
	}
}

void AScavarCharacter::UpdateMovementSpeed()
{
	const bool bLegFracture = Vitals->HasLegFracture();
	const bool bSprintAllowed = bWantsSprint && Vitals->CanSprint();
	const Scavar::MoveMode Mode = bSprintAllowed ? Scavar::MoveMode::Sprint : Scavar::MoveMode::Jog;
	const float Kg = Vitals->GetCarriedKg();

	const float Standing = Scavar::MoveSpeed(Mode, Scavar::Stance::Standing, Kg, bLegFracture, bSprintAllowed) * 100.f; // m/s -> cm/s
	const float Crouched = Scavar::MoveSpeed(Mode, Scavar::Stance::Crouch, Kg, bLegFracture, bSprintAllowed) * 100.f;
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->MaxWalkSpeed = Standing;
	Movement->MaxWalkSpeedCrouched = Crouched;
}

void AScavarCharacter::OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PrevMovementMode, PreviousCustomMode);
	if (GetCharacterMovement()->MovementMode == MOVE_Falling && PrevMovementMode != MOVE_Falling)
	{
		FallStartZ = GetActorLocation().Z;
		bTrackingFall = true;
	}
}

void AScavarCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);
	if (HasAuthority() && bTrackingFall)
	{
		const float HeightMeters = (FallStartZ - GetActorLocation().Z) / 100.f;
		if (HeightMeters > 0.f)
		{
			Vitals->ApplyFall(HeightMeters);
		}
	}
	bTrackingFall = false;
}

void AScavarCharacter::HandleDied(AActor* /*Victim*/)
{
	UE_LOG(LogScavarCombat, Log, TEXT("%s is dead; disabling input."), *GetName());
	DisableInput(nullptr);
	GetCharacterMovement()->DisableMovement();
	GetMesh()->SetSimulatePhysics(false);
}
