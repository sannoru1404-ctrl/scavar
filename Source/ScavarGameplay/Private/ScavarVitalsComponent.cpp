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
			OnTreatmentFinished.Broadcast(Status == Scavar::TreatmentStatus::Completed);
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

	if (Result.ArmorWear > 0.f)
	{
		OnArmorWorn.Broadcast(Armor);
	}
	if (bTreating)
	{
		Scavar::InterruptTreatment(Treatment, Scavar::TreatmentInterrupt::DamageTaken);
		bTreating = false;
		OnTreatmentFinished.Broadcast(false);
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
		OnTreatmentFinished.Broadcast(false);
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
		OnTreatmentFinished.Broadcast(false);
	}
}

void UScavarVitalsComponent::SetMoveMode(Scavar::MoveMode Mode)
{
	MoveMode = Mode;
	if (Mode == Scavar::MoveMode::Sprint && bTreating)
	{
		Scavar::InterruptTreatment(Treatment, Scavar::TreatmentInterrupt::SprintStarted);
		bTreating = false;
		OnTreatmentFinished.Broadcast(false);
	}
}

void UScavarVitalsComponent::AddNutrition(float Food, float Water)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Health.Dead)
	{
		return;
	}
	Survival.Food = FMath::Clamp(Survival.Food + Food, 0.f, 100.f);
	Survival.Water = FMath::Clamp(Survival.Water + Water, 0.f, 100.f);
	RefreshSnapshot();
}

bool UScavarVitalsComponent::FindTreatableZone(EScavarTreatment Kind, EScavarHitZone& OutZone) const
{
	for (int32 Index = 0; Index < Scavar::ZoneCount; ++Index)
	{
		if (Scavar::CanTreat(ToSim(Kind), Health, static_cast<Scavar::HitZone>(Index)))
		{
			OutZone = static_cast<EScavarHitZone>(Index);
			return true;
		}
	}
	return false;
}

FString UScavarVitalsComponent::ExportSave() const
{
	TArray<FString> Parts;
	for (int32 i = 0; i < Scavar::ZoneCount; ++i) { Parts.Add(FString::SanitizeFloat(Health.Hp[i])); }
	for (int32 i = 0; i < Scavar::ZoneCount; ++i) { Parts.Add(FString::FromInt(static_cast<int32>(Health.Bleed[i]))); }
	for (int32 i = 0; i < Scavar::ZoneCount; ++i) { Parts.Add(Health.Fracture[i] ? TEXT("1") : TEXT("0")); }
	Parts.Add(FString::SanitizeFloat(Survival.Food));
	Parts.Add(FString::SanitizeFloat(Survival.Water));
	Parts.Add(FString::SanitizeFloat(Survival.Warmth));
	Parts.Add(FString::SanitizeFloat(Stamina.Value));
	Parts.Add(FString::SanitizeFloat(Health.AnalgesiaSeconds));
	return FString::Join(Parts, TEXT(","));
}

bool UScavarVitalsComponent::ImportSave(const FString& Text)
{
	TArray<FString> Parts;
	Text.ParseIntoArray(Parts, TEXT(","));
	if (!GetOwner() || !GetOwner()->HasAuthority() || Parts.Num() != 3 * Scavar::ZoneCount + 5)
	{
		return false;
	}
	Scavar::CharacterHealth Loaded = Scavar::MakeHealth();
	for (int32 i = 0; i < Scavar::ZoneCount; ++i)
	{
		Loaded.Hp[i] = FMath::Clamp(FCString::Atof(*Parts[i]), 0.f, 500.f);
		const int32 Bleed = FMath::Clamp(FCString::Atoi(*Parts[Scavar::ZoneCount + i]), 0, 2);
		Loaded.Bleed[i] = static_cast<Scavar::BleedLevel>(Bleed);
		Loaded.Fracture[i] = Parts[2 * Scavar::ZoneCount + i] == TEXT("1");
	}
	const int32 Base = 3 * Scavar::ZoneCount;
	Survival.Food = FMath::Clamp(FCString::Atof(*Parts[Base]), 0.f, 100.f);
	Survival.Water = FMath::Clamp(FCString::Atof(*Parts[Base + 1]), 0.f, 100.f);
	Survival.Warmth = FMath::Clamp(FCString::Atof(*Parts[Base + 2]), 0.f, 100.f);
	Stamina.Value = FMath::Clamp(FCString::Atof(*Parts[Base + 3]), 0.f, 100.f);
	Loaded.AnalgesiaSeconds = FMath::Max(0.f, FCString::Atof(*Parts[Base + 4]));
	Health = Loaded;
	RefreshSnapshot();
	return true;
}

void UScavarVitalsComponent::DebugRestoreAll()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Health.Dead)
	{
		return;
	}
	Health = Scavar::MakeHealth();
	Survival = Scavar::SurvivalState();
	Stamina = Scavar::StaminaState();
	bTreating = false;
	RefreshSnapshot();
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
