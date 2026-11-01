#include "ScavarWeaponComponent.h"
#include "ScavarAi.h"
#include "ScavarCharacter.h"
#include "ScavarInventoryComponent.h"
#include "ScavarLog.h"
#include "ScavarNPC.h"
#include "ScavarPickup.h"
#include "ScavarVitalsComponent.h"
#include "ScavarWorldInventory.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "Sim/Ballistics.h"

namespace
{
FVector ToCm(const Scavar::Vec3& V) { return FVector(V.x * 100.0, V.y * 100.0, V.z * 100.0); }
Scavar::Vec3 ToMeters(const FVector& V) { return Scavar::Vec3{V.X / 100.0, V.Y / 100.0, V.Z / 100.0}; }

struct FMeleeProfile { float Damage; float ReachCm; float CooldownSeconds; };
// Melee numbers are not in the data tables yet; knife and axe are the only melee items of the pilot.
FMeleeProfile MeleeProfileFor(const std::string& Id)
{
	return Id == "M_AXE" ? FMeleeProfile{45.f, 190.f, 0.9f} : FMeleeProfile{30.f, 170.f, 0.6f};
}

// Base cone half-angle in degrees per weapon class (TUNABLE). Shotgun value is the pellet spread.
float BaseSpreadFor(const std::string& Class)
{
	if (Class == "pistol") { return 0.60f; }
	if (Class == "shotgun") { return 2.50f; }
	if (Class == "assault") { return 0.50f; }
	if (Class == "dmr") { return 0.15f; }
	return 0.35f;
}

// Camera kick in degrees per shot (TUNABLE).
float KickFor(const std::string& Class)
{
	if (Class == "pistol") { return 1.0f; }
	if (Class == "shotgun") { return 2.2f; }
	if (Class == "assault") { return 0.7f; }
	if (Class == "dmr") { return 1.2f; }
	return 0.8f;
}

EScavarHitZone ZoneFromImpact(const ACharacter& Target, const FVector& Impact)
{
	const UCapsuleComponent* Capsule = Target.GetCapsuleComponent();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float Relative = FMath::Clamp((Impact.Z - (Target.GetActorLocation().Z - HalfHeight)) / (2.f * HalfHeight), 0.f, 1.f);
	if (Relative > 0.86f) { return EScavarHitZone::Head; }
	if (Relative < 0.45f)
	{
		const float Side = FVector::DotProduct(Impact - Target.GetActorLocation(), Target.GetActorRightVector());
		return Side < 0.f ? EScavarHitZone::LegL : EScavarHitZone::LegR;
	}
	const float Lateral = FVector::DotProduct(Impact - Target.GetActorLocation(), Target.GetActorRightVector());
	if (FMath::Abs(Lateral) > Radius * 0.65f)
	{
		return Lateral < 0.f ? EScavarHitZone::ArmL : EScavarHitZone::ArmR;
	}
	return EScavarHitZone::Torso;
}
}

UScavarWeaponComponent::UScavarWeaponComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
	Rng.Seed(static_cast<uint64>(FDateTime::UtcNow().GetTicks()) ^ 0xBEEF);
}

void UScavarWeaponComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UScavarWeaponComponent, View, COND_OwnerOnly);
}

UScavarWorldInventory* UScavarWeaponComponent::Inventory() const { return GetWorld() ? GetWorld()->GetSubsystem<UScavarWorldInventory>() : nullptr; }
UScavarInventoryComponent* UScavarWeaponComponent::OwnerInventory() const { return GetOwner() ? GetOwner()->FindComponentByClass<UScavarInventoryComponent>() : nullptr; }
UScavarVitalsComponent* UScavarWeaponComponent::Vitals() const { return GetOwner() ? GetOwner()->FindComponentByClass<UScavarVitalsComponent>() : nullptr; }
double UScavarWeaponComponent::Now() const { return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0; }

int64 UScavarWeaponComponent::ShooterId() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	return Pawn && Pawn->GetController() ? static_cast<int64>(Pawn->GetController()->GetUniqueID()) : 0;
}

void UScavarWeaponComponent::Notify(const FString& Message) const
{
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
		{
			PC->ClientMessage(Message);
		}
	}
}

void UScavarWeaponComponent::OnRep_View() {}

void UScavarWeaponComponent::RefreshView()
{
	FScavarWeaponView Next;
	Next.bEquipped = bEquipped;
	Next.bMelee = bMelee;
	if (bEquipped)
	{
		Next.WeaponId = FName(UTF8_TO_TCHAR(State.WeaponId.c_str()));
		Next.Mode = static_cast<uint8>(State.Mode);
		Next.bJammed = State.Jammed;
		Next.bBusy = State.Pending.Kind != Scavar::ActionKind::None;
		if (!bMelee && Inventory() && Inventory()->GetStore())
		{
			Next.Rounds = Scavar::RoundsInWeapon(State, *Inventory()->GetStore());
			Next.bChambered = State.ChamberAmmo >= 0;
		}
	}
	View = Next;
}

bool UScavarWeaponComponent::Equip(int64 ItemId)
{
	UScavarWorldInventory* Inv = Inventory();
	UScavarInventoryComponent* Owned = OwnerInventory();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Inv || !Inv->IsReady() || !Owned)
	{
		return false;
	}
	if (bEquipped && EquippedItem == ItemId)
	{
		return Holster();
	}
	if (bEquipped && !Holster())
	{
		return false;
	}
	const Scavar::ItemInstance* Item = Inv->GetStore()->GetItem(ItemId);
	if (!Item || !Owned->OwnsContainer(Item->Container))
	{
		return false;
	}
	const Scavar::WeaponDef* Def = Inv->GetGameData().FindWeapon(Item->DefId);
	if (!Def)
	{
		return false;
	}

	bMelee = Def->Class == "melee";
	State = Scavar::WeaponState();
	if (!bMelee)
	{
		if (Scavar::MakeWeaponState(Inv->GetGameData(), Item->DefId, &State) != Scavar::WeaponStatus::Ok)
		{
			return false;
		}
		State.Condition = Item->Durability;
		const Scavar::WeaponSpec* Spec = Scavar::FindWeaponSpec(Item->DefId);
		if (Spec && Spec->ExternalMag)
		{
			const Scavar::Status St = Inv->Execute([&](Scavar::ItemStore& S)
			{
				Scavar::Container C;
				C.Owner = UScavarWorldInventory::HashAccountKey(Owned->GetAccountKey());
				C.Kind = Scavar::ContainerKind::Magwell;
				C.W = 1;
				C.H = 1;
				return S.AddContainer(C, &Magwell);
			});
			if (St != Scavar::Status::Ok) { return false; }
		}
	}
	EquippedItem = ItemId;
	bEquipped = true;
	bTriggerHeld = false;
	if (UScavarVitalsComponent* V = Vitals()) { V->NotifyWeaponSwapped(); }
	RefreshView();
	return true;
}

bool UScavarWeaponComponent::Holster()
{
	UScavarWorldInventory* Inv = Inventory();
	UScavarInventoryComponent* Owned = OwnerInventory();
	if (!bEquipped)
	{
		return true;
	}
	if (!Inv || !Owned)
	{
		return false;
	}
	CancelPendingAction();
	bTriggerHeld = false;

	if (!bMelee)
	{
		const Scavar::ContainerId Bag = Owned->GetBackpackId();
		const Scavar::ContainerId Pockets = Owned->GetPocketsId();
		const Scavar::WeaponState Saved = State; // the store rolls back on failure, so the weapon state must too (no vanishing rounds)
		const Scavar::Status St = Inv->Execute([&](Scavar::ItemStore& S)
		{
			Scavar::Status R = Scavar::UnloadWeaponRounds(State, S, Inv->GetGameData(), Bag) == Scavar::WeaponStatus::Ok ? Scavar::Status::Ok : Scavar::Status::NoSpace;
			if (R != Scavar::Status::Ok) { return R; }
			if (State.Magazine != Scavar::InvalidId)
			{
				R = S.AutoMove(State.Magazine, Bag, true, Magwell);
				if (R != Scavar::Status::Ok) { R = S.AutoMove(State.Magazine, Pockets, true, Magwell); }
				if (R != Scavar::Status::Ok) { return R; }
				State.Magazine = Scavar::InvalidId;
			}
			return Magwell != 0 ? S.RemoveContainer(Magwell) : Scavar::Status::Ok;
		});
		if (St != Scavar::Status::Ok)
		{
			State = Saved;
			Notify(TEXT("No room to put the weapon away. Free some space."));
			return false;
		}
		WriteBackCondition();
	}
	bEquipped = false;
	EquippedItem = 0;
	Magwell = 0;
	RefreshView();
	return true;
}

void UScavarWeaponComponent::ReleaseOnDeath()
{
	if (!bEquipped || bMelee)
	{
		bEquipped = false;
		return;
	}
	UScavarWorldInventory* Inv = Inventory();
	UScavarInventoryComponent* Owned = OwnerInventory();
	if (Inv && Owned)
	{
		CancelPendingAction();
		Inv->Execute([&](Scavar::ItemStore& S)
		{
			Scavar::UnloadWeaponRounds(State, S, Inv->GetGameData(), Owned->GetBackpackId());
			if (State.Magazine != Scavar::InvalidId)
			{
				S.AutoMove(State.Magazine, Owned->GetBackpackId(), true, Magwell);
			}
			if (Magwell != 0 && S.ItemsIn(Magwell).empty()) { S.RemoveContainer(Magwell); }
			return Scavar::Status::Ok;
		});
	}
	bEquipped = false;
	EquippedItem = 0;
}

void UScavarWeaponComponent::CancelPendingAction()
{
	if (State.Pending.Kind != Scavar::ActionKind::None)
	{
		Scavar::CancelAction(State);
		RefreshView();
	}
}

void UScavarWeaponComponent::WriteBackCondition()
{
	UScavarWorldInventory* Inv = Inventory();
	if (Inv && Inv->GetStore() && Inv->GetStore()->GetItem(EquippedItem))
	{
		const int64 Id = EquippedItem;
		const float Condition = State.Condition;
		Inv->Execute([&](Scavar::ItemStore& S) { return S.SetDurability(Id, Condition); });
	}
}

void UScavarWeaponComponent::ServerHolster_Implementation() { Holster(); }

void UScavarWeaponComponent::ServerToggleFireMode_Implementation()
{
	UScavarWorldInventory* Inv = Inventory();
	if (!bEquipped || bMelee || !Inv) { return; }
	const Scavar::FireMode Next = State.Mode == Scavar::FireMode::Single ? Scavar::FireMode::Auto : Scavar::FireMode::Single;
	if (Scavar::SetFireMode(State, Inv->GetGameData(), Next) != Scavar::WeaponStatus::Ok)
	{
		Notify(TEXT("This weapon has no other fire mode."));
	}
	RefreshView();
}

void UScavarWeaponComponent::ServerTrigger_Implementation(bool bPressed)
{
	const UScavarVitalsComponent* V = Vitals();
	if (!bEquipped || (V && V->IsDead()))
	{
		bTriggerHeld = false;
		return;
	}
	bTriggerHeld = bPressed;
	if (bPressed)
	{
		PullTrigger();
	}
}

void UScavarWeaponComponent::ServerReload_Implementation()
{
	if (bEquipped && !bMelee)
	{
		StartReload();
	}
}

void UScavarWeaponComponent::PullTrigger()
{
	if (bMelee)
	{
		MeleeStrike(Now());
	}
	else
	{
		FireOnce();
	}
}

void UScavarWeaponComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UScavarWorldInventory* Inv = Inventory();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !bEquipped || !Inv || !Inv->IsReady())
	{
		return;
	}

	// The weapon item must still be where the owner can reach it (dropped or stolen items drop out of the hands).
	const Scavar::ItemInstance* Item = Inv->GetStore()->GetItem(EquippedItem);
	UScavarInventoryComponent* Owned = OwnerInventory();
	if (!Item || !Owned || !Owned->OwnsContainer(Item->Container))
	{
		if (!Holster()) { bEquipped = false; EquippedItem = 0; RefreshView(); }
		return;
	}

	const double T = Now();
	if (!bMelee)
	{
		if (State.Pending.Kind != Scavar::ActionKind::None && T + 1e-6 >= State.Pending.DueAt)
		{
			Inv->Execute([&](Scavar::ItemStore& S)
			{
				return Scavar::CompleteAction(State, S, Inv->GetGameData(), T) == Scavar::WeaponStatus::Ok ? Scavar::Status::Ok : Scavar::Status::InvalidArgument;
			});
			RefreshView();
		}
		StartAutoActions(T);
		if (bTriggerHeld && State.Mode == Scavar::FireMode::Auto && State.Pending.Kind == Scavar::ActionKind::None)
		{
			FireOnce();
		}
	}
}

void UScavarWeaponComponent::StartAutoActions(double NowSeconds)
{
	UScavarWorldInventory* Inv = Inventory();
	if (State.Pending.Kind == Scavar::ActionKind::None && State.NeedsCycle && Inv && Inv->GetStore())
	{
		if (Scavar::BeginCycle(State, *Inv->GetStore(), NowSeconds) == Scavar::WeaponStatus::Ok)
		{
			RefreshView();
		}
	}
}

float UScavarWeaponComponent::SpreadDegrees() const
{
	const UScavarWorldInventory* Inv = Inventory();
	const Scavar::WeaponDef* Def = Inv ? Inv->GetGameData().FindWeapon(State.WeaponId) : nullptr;
	float Degrees = BaseSpreadFor(Def ? Def->Class : std::string());
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		const float Speed = Character->GetVelocity().Size2D();
		Degrees *= 1.f + Speed / 350.f;
		if (Character->bIsCrouched) { Degrees *= 0.8f; }
	}
	// A worn-out weapon is less accurate (condition 100 -> x1, condition 0 -> x2).
	Degrees *= 2.f - FMath::Clamp(State.Condition, 0.f, 100.f) / 100.f;
	return Degrees;
}

void UScavarWeaponComponent::FireOnce()
{
	UScavarWorldInventory* Inv = Inventory();
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Inv || !Pawn || !Inv->GetStore())
	{
		return;
	}

	Scavar::FireResult Result;
	const double T = Now();
	Inv->Execute([&](Scavar::ItemStore& S)
	{
		Result = Scavar::Fire(State, S, Inv->GetGameData(), T, Rng);
		return Result.Status == Scavar::WeaponStatus::Ok ? Scavar::Status::Ok : Scavar::Status::InvalidArgument;
	});

	if (Result.Status != Scavar::WeaponStatus::Ok)
	{
		switch (Result.Status)
		{
		case Scavar::WeaponStatus::EmptyChamber: Notify(TEXT("*click* - empty. Press R to reload.")); break;
		case Scavar::WeaponStatus::Jammed: Notify(TEXT("Jammed! Press R to clear.")); break;
		default: break; // Busy / RateLimited / NeedsCycle are silent
		}
		RefreshView();
		return;
	}

	const FVector Origin = Pawn->GetPawnViewLocation();
	const FRotator Aim = Pawn->GetBaseAimRotation();
	const FVector Forward = Aim.Vector();
	const FVector Muzzle = Origin + Forward * 40.f;
	const float Spread = FMath::DegreesToRadians(SpreadDegrees());
	for (int32 i = 0; i < Result.Projectiles; ++i)
	{
		FireProjectile(Muzzle, FMath::VRandCone(Forward, Spread), Result.MuzzleVelocity, Result.ProjectileDamage, Result.PenTier);
	}

	if (UScavarAiSubsystem* Ai = GetWorld()->GetSubsystem<UScavarAiSubsystem>())
	{
		Ai->ReportGunshot(Muzzle, false, ShooterId());
	}
	const Scavar::WeaponDef* Def = Inv->GetGameData().FindWeapon(State.WeaponId);
	const float Kick = KickFor(Def ? Def->Class : std::string());
	ClientRecoil(Kick * (0.8f + 0.4f * static_cast<float>(Rng.NextUnit())), Kick * 0.5f * (static_cast<float>(Rng.NextUnit()) - 0.5f));
	RefreshView();
}

void UScavarWeaponComponent::ClientRecoil_Implementation(float PitchDegrees, float YawDegrees)
{
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
		{
			PC->AddPitchInput(-PitchDegrees); // the default mouse axis scale makes positive = look down
			PC->AddYawInput(YawDegrees);
		}
	}
}

void UScavarWeaponComponent::FireProjectile(const FVector& Origin, const FVector& Direction, float SpeedMps, float Damage, int32 PenTier)
{
	UWorld* World = GetWorld();
	const Scavar::BallisticsTuning& Tuning = Scavar::DefaultBallisticsTuning();
	Scavar::ProjectileState P = Scavar::MakeProjectile(ToMeters(Origin), Scavar::Vec3{Direction.X, Direction.Y, Direction.Z}, SpeedMps);

	TArray<const AActor*> Ignored;
	Ignored.Add(GetOwner());
	FVector Previous = Origin;
	constexpr int32 MaxSteps = 150; // 2.5 s of flight, plenty for every weapon in the tables
	for (int32 Step = 0; Step < MaxSteps; ++Step)
	{
		Scavar::StepProjectile(P, Tuning.StepSeconds);
		const FVector Next = ToCm(P.Pos);
		if (Next.Z < -50000.f) { return; }

		FHitResult Hit;
		bool bBlocked = false;
		for (int32 Retry = 0; Retry < 4; ++Retry) // loose items never stop bullets
		{
			FCollisionQueryParams Params(SCENE_QUERY_STAT(ScavarBullet), true);
			Params.AddIgnoredActors(Ignored);
			if (!World->LineTraceSingleByObjectType(Hit, Previous, Next, FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects), Params))
			{
				break;
			}
			if (Cast<AScavarPickup>(Hit.GetActor()))
			{
				Ignored.Add(Hit.GetActor());
				continue;
			}
			bBlocked = true;
			break;
		}
		if (bBlocked)
		{
			ApplyBulletHit(Hit, Direction, Damage, PenTier);
			return;
		}
		Previous = Next;
	}
}

void UScavarWeaponComponent::ApplyBulletHit(const FHitResult& Hit, const FVector& Direction, float Damage, int32 PenTier)
{
	ACharacter* Target = Cast<ACharacter>(Hit.GetActor());
	if (!Target)
	{
		return; // geometry: bullet stops (no wall-banging in the pilot)
	}
	UScavarVitalsComponent* TargetVitals = Target->FindComponentByClass<UScavarVitalsComponent>();
	if (!TargetVitals || TargetVitals->IsDead())
	{
		return;
	}
	const EScavarHitZone Zone = ZoneFromImpact(*Target, Hit.ImpactPoint);
	TargetVitals->ApplyProjectileHit(Zone, Damage, PenTier);

	if (AScavarNPC* Npc = Cast<AScavarNPC>(Target))
	{
		if (UScavarAiSubsystem* Ai = GetWorld()->GetSubsystem<UScavarAiSubsystem>())
		{
			Ai->ReportNpcAttacked(Npc, ShooterId());
		}
	}
}

void UScavarWeaponComponent::MeleeStrike(double NowSeconds)
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const UScavarWorldInventory* Inv = Inventory();
	const Scavar::ItemInstance* Item = Inv && Inv->GetStore() ? Inv->GetStore()->GetItem(EquippedItem) : nullptr;
	if (!Pawn || !Item)
	{
		return;
	}
	const FMeleeProfile Profile = MeleeProfileFor(Item->DefId);
	if (NowSeconds - LastMeleeAt < Profile.CooldownSeconds)
	{
		return;
	}
	LastMeleeAt = NowSeconds;

	const FVector Start = Pawn->GetPawnViewLocation();
	const FVector End = Start + Pawn->GetBaseAimRotation().Vector() * Profile.ReachCm;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ScavarMelee), false, GetOwner());
	if (GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(35.f), Params))
	{
		ApplyBulletHit(Hit, (End - Start).GetSafeNormal(), Profile.Damage, 1);
	}
}

void UScavarWeaponComponent::StartReload()
{
	UScavarWorldInventory* Inv = Inventory();
	UScavarInventoryComponent* Owned = OwnerInventory();
	if (!Inv || !Owned || !Inv->GetStore() || State.Pending.Kind != Scavar::ActionKind::None)
	{
		return;
	}
	const double T = Now();
	const Scavar::GameData& Game = Inv->GetGameData();

	if (State.Jammed)
	{
		Scavar::BeginClearJam(State, T);
		RefreshView();
		return;
	}

	const Scavar::WeaponSpec* Spec = Scavar::FindWeaponSpec(State.WeaponId);
	if (!Spec)
	{
		return;
	}

	if (Spec->ExternalMag)
	{
		int64 Mag = FindBestMagazine(*Spec);
		if (Mag == 0 && !FillMagazineFromAmmo(*Spec, &Mag))
		{
			Notify(TEXT("No ammunition."));
			return;
		}
		if (Scavar::BeginReloadMagazine(State, *Inv->GetStore(), Game, Mag, Magwell, Owned->GetBackpackId(), T) != Scavar::WeaponStatus::Ok)
		{
			Notify(TEXT("Cannot reload now."));
		}
	}
	else
	{
		const int64 Stack = FindAmmoStackForWeapon();
		const Scavar::WeaponDef* Def = Game.FindWeapon(State.WeaponId);
		if (Stack == 0 || !Def)
		{
			Notify(TEXT("No ammunition."));
			return;
		}
		const Scavar::ItemInstance* StackItem = Inv->GetStore()->GetItem(Stack);
		const int Capacity = FMath::Max(0, Def->MagSize - State.InternalRounds);
		const int Count = FMath::Min(Capacity, StackItem ? StackItem->Qty : 0);
		if (Count <= 0)
		{
			Notify(TEXT("Already full."));
			return;
		}
		if (Scavar::BeginLoadInternal(State, *Inv->GetStore(), Game, Stack, Count, T) != Scavar::WeaponStatus::Ok)
		{
			Notify(TEXT("Cannot load that ammunition."));
		}
	}
	RefreshView();
}

int64 UScavarWeaponComponent::FindBestMagazine(const Scavar::WeaponSpec& Spec) const
{
	const UScavarWorldInventory* Inv = Inventory();
	const UScavarInventoryComponent* Owned = OwnerInventory();
	if (!Inv || !Owned || !Spec.MagDefId) { return 0; }
	int64 Best = 0;
	int BestRounds = 0;
	for (const Scavar::ContainerId C : {Owned->GetPocketsId(), Owned->GetBackpackId()})
	{
		for (const Scavar::ItemId Id : Inv->GetStore()->ItemsIn(C))
		{
			const Scavar::ItemInstance* It = Inv->GetStore()->GetItem(Id);
			const int Rounds = It ? Scavar::MagRounds(It->Aux) : 0;
			if (It && It->DefId == Spec.MagDefId && Id != State.Magazine && Rounds > BestRounds)
			{
				Best = Id;
				BestRounds = Rounds;
			}
		}
	}
	return Best;
}

int64 UScavarWeaponComponent::FindAmmoStackForWeapon() const
{
	const UScavarWorldInventory* Inv = Inventory();
	const UScavarInventoryComponent* Owned = OwnerInventory();
	const Scavar::WeaponDef* Def = Inv ? Inv->GetGameData().FindWeapon(State.WeaponId) : nullptr;
	if (!Inv || !Owned || !Def) { return 0; }
	for (const Scavar::ContainerId C : {Owned->GetPocketsId(), Owned->GetBackpackId()})
	{
		for (const Scavar::ItemId Id : Inv->GetStore()->ItemsIn(C))
		{
			const Scavar::ItemInstance* It = Inv->GetStore()->GetItem(Id);
			const Scavar::AmmoDef* Ammo = It ? Inv->GetGameData().FindAmmo(It->DefId) : nullptr;
			if (Ammo && Ammo->Caliber == Def->Caliber)
			{
				return Id;
			}
		}
	}
	return 0;
}

bool UScavarWeaponComponent::FillMagazineFromAmmo(const Scavar::WeaponSpec& Spec, int64* OutMagazine)
{
	UScavarWorldInventory* Inv = Inventory();
	UScavarInventoryComponent* Owned = OwnerInventory();
	const int64 Stack = FindAmmoStackForWeapon();
	if (!Inv || !Owned || !Spec.MagDefId || Stack == 0) { return false; }

	// Pick an unattached magazine of the right kind that is empty (or holds the same ammo type).
	int64 Target = 0;
	const Scavar::ItemInstance* StackItem = Inv->GetStore()->GetItem(Stack);
	const int StackAmmo = Scavar::AmmoIndexOf(Inv->GetGameData(), StackItem->DefId);
	for (const Scavar::ContainerId C : {Owned->GetPocketsId(), Owned->GetBackpackId()})
	{
		for (const Scavar::ItemId Id : Inv->GetStore()->ItemsIn(C))
		{
			const Scavar::ItemInstance* It = Inv->GetStore()->GetItem(Id);
			if (It && It->DefId == Spec.MagDefId && Id != State.Magazine && (It->Aux == 0 || Scavar::MagAmmoIndex(It->Aux) == StackAmmo))
			{
				Target = Id;
				break;
			}
		}
		if (Target != 0) { break; }
	}
	if (Target == 0) { return false; }

	int Loaded = 0;
	const int Wanted = StackItem->Qty;
	const Scavar::Status St = Inv->Execute([&](Scavar::ItemStore& S)
	{
		return Scavar::LoadRoundsIntoMagazine(S, Inv->GetGameData(), Target, Stack, Wanted, &Loaded) == Scavar::WeaponStatus::Ok ? Scavar::Status::Ok : Scavar::Status::InvalidArgument;
	});
	if (St != Scavar::Status::Ok || Loaded <= 0) { return false; }
	Notify(FString::Printf(TEXT("Loaded %d rounds into a magazine."), Loaded));
	*OutMagazine = Target;
	return true;
}
