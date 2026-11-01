#include "ScavarVitalsComponent.h"
#include "ScavarLog.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Actor.h"

namespace
{
Scavar::HitZone ToSim(EScavarHitZone Zone) { return static_cast<Scavar::HitZone>(static_cast<uint8>(Zone)); }
Scavar::TreatmentKind ToSim(EScavarTreatment Kind) { return static_cast<Scavar::TreatmentKind>(static_cast<uint8>(Kind)); }
}

UScavarVitalsComponent::UScavarVitalsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f;
	SetIsReplicatedByDefault(true);
	Health = Scavar::MakeHealth();
	Rng.Seed(static_cast<uint64>(FDateTime::UtcNow().GetTicks()));
	RefreshSnapshot();
}

void UScavarVitalsComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UScavarVitalsComponent, OwnerSnapshot, COND_OwnerOnly);
	DOREPLIFETIME(UScavarVitalsComponent, bDead);
}

void UScavarVitalsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!GetOwner() || !GetOwner()->HasAuthority() || Health.Dead)
	{
		return;
	}

	Scavar::TickSurvival(Survival, DeltaTime);
	Scavar::TickHealth(Health, Survival, DeltaTime);
	Scavar::TickStamina(Stamina, MoveMode, Survival, DeltaTime);

	if (bTreating)
	{
		const Scavar::TreatmentStatus Status = Scavar::TickTreatment(Treatment, DeltaTime, Health);
		if (Status != Scavar::TreatmentStatus::InProgress)
		{
			bTreating = false;
		}
	}

	if (Health.Dead)
	{
		HandleDeath();
	}
	RefreshSnapshot();
}

bool UScavarVitalsComponent::ApplyProjectileHit(EScavarHitZone Zone, float Damage, int32 PenTier)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		UE_LOG(LogScavarCombat, Warning, TEXT("ApplyProjectileHit ignored on a non-authoritative actor."));
		return false;
	}

	const Scavar::ProjectileHit Hit{ToSim(Zone), Damage, PenTier};
	const Scavar::HitResult Result = Scavar::ApplyHit(Health, Armor, Hit, Rng);
	UE_LOG(LogScavarCombat, Verbose, TEXT("%s hit zone %d: dealt %.1f penetrated=%d blunt=%d killed=%d"),
		*GetOwner()->GetName(), (int32)Zone, Result.DamageDealt, Result.Penetrated, Result.Blunt, Result.Killed);

	if (bTreating)
	{
		Scavar::InterruptTreatment(Treatment, Scavar::TreatmentInterrupt::DamageTaken);
		bTreating = false;
	}
	if (Result.Killed)
	{
		HandleDeath();
	}
	RefreshSnapshot();
	return Result.Killed;
}

void UScavarVitalsComponent::ApplyFall(float HeightMeters)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Health.Dead)
	{
		return;
	}
	Scavar::ResolveFall(Health, HeightMeters, Rng);
	if (bTreating)
	{
		Scavar::InterruptTreatment(Treatment, Scavar::TreatmentInterrupt::DamageTaken);
		bTreating = false;
	}
	if (Health.Dead)
	{
		HandleDeath();
	}
	RefreshSnapshot();
}

bool UScavarVitalsComponent::BeginTreatment(EScavarTreatment Kind, EScavarHitZone Zone)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Health.Dead || bTreating)
	{
		return false;
	}
	if (!Scavar::CanTreat(ToSim(Kind), Health, ToSim(Zone)))
	{
		return false;
	}
	Treatment = Scavar::BeginTreatment(ToSim(Kind), ToSim(Zone));
	bTreating = true;
	return true;
}

void UScavarVitalsComponent::NotifyWeaponSwapped()
{
	if (bTreating)
	{
		Scavar::InterruptTreatment(Treatment, Scavar::TreatmentInterrupt::WeaponSwapped);
		bTreating = false;
	}
}

void UScavarVitalsComponent::SetMoveMode(Scavar::MoveMode Mode)
{
	MoveMode = Mode;
	if (Mode == Scavar::MoveMode::Sprint && bTreating)
	{
		Scavar::InterruptTreatment(Treatment, Scavar::TreatmentInterrupt::SprintStarted);
		bTreating = false;
	}
}

void UScavarVitalsComponent::SetArmor(const Scavar::ArmorLoadout& NewArmor)
{
	Armor = NewArmor;
}

float UScavarVitalsComponent::GetStamina() const
{
	return (GetOwner() && GetOwner()->HasAuthority()) ? Stamina.Value : OwnerSnapshot.Stamina;
}

bool UScavarVitalsComponent::HasLegFracture() const
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		return Health.Fracture[Scavar::ZoneIndex(Scavar::HitZone::LegL)] || Health.Fracture[Scavar::ZoneIndex(Scavar::HitZone::LegR)];
	}
	return OwnerSnapshot.Fracture.IsValidIndex(4) && (OwnerSnapshot.Fracture[4] || OwnerSnapshot.Fracture[5]);
}

bool UScavarVitalsComponent::CanSprint() const
{
	Scavar::StaminaState State;
	State.Value = GetStamina();
	return Scavar::CanSprint(State, CarriedKg);
}

void UScavarVitalsComponent::HandleDeath()
{
	if (bDead)
	{
		return;
	}
	bDead = true;
	bTreating = false;
	UE_LOG(LogScavarCombat, Log, TEXT("%s died."), GetOwner() ? *GetOwner()->GetName() : TEXT("<none>"));
	OnDied.Broadcast(GetOwner());
	OnVitalsChanged.Broadcast();
}

void UScavarVitalsComponent::RefreshSnapshot()
{
	FScavarVitalsSnapshot Next;
	Next.ZoneHp.SetNumUninitialized(Scavar::ZoneCount);
	Next.Bleed.SetNumUninitialized(Scavar::ZoneCount);
	Next.Fracture.SetNumUninitialized(Scavar::ZoneCount);
	for (int32 Index = 0; Index < Scavar::ZoneCount; ++Index)
	{
		Next.ZoneHp[Index] = Health.Hp[Index];
		Next.Bleed[Index] = static_cast<uint8>(Health.Bleed[Index]);
		Next.Fracture[Index] = Health.Fracture[Index];
	}
	Next.Food = Survival.Food;
	Next.Water = Survival.Water;
	Next.Warmth = Survival.Warmth;
	Next.Stamina = Stamina.Value;
	Next.AnalgesiaSeconds = Health.AnalgesiaSeconds;
	OwnerSnapshot = Next;
}

void UScavarVitalsComponent::OnRep_Snapshot()
{
	OnVitalsChanged.Broadcast();
}

void UScavarVitalsComponent::OnRep_Dead()
{
	if (bDead)
	{
		OnDied.Broadcast(GetOwner());
	}
	OnVitalsChanged.Broadcast();
}
