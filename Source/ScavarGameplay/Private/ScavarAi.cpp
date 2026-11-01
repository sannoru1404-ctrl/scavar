#include "ScavarAi.h"
#include "ScavarCharacter.h"
#include "ScavarGameState.h"
#include "ScavarLog.h"
#include "ScavarVitalsComponent.h"
#include "ScavarWorldInventory.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

namespace
{
constexpr float AiStepSeconds = 0.1f;
constexpr float DirectorStepSeconds = 1.0f;
constexpr double ShooterMemorySeconds = 30.0;
constexpr float MaxTargetRangeCm = 25000.f;

Scavar::Vec3 ToMeters(const FVector& V) { return Scavar::Vec3{V.X / 100.0, V.Y / 100.0, V.Z / 100.0}; }
FVector ToCm(const Scavar::Vec3& V) { return FVector(V.x * 100.0, V.y * 100.0, V.z * 100.0); }

struct FNpcAttackProfile { float Damage; int32 PenTier; float RangeCm; float CooldownSeconds; };

// Marauders and militia carry pistols/shotguns in the pilot; guards carry rifles; wolves bite. Numbers mirror data/ammo.csv rows.
FNpcAttackProfile AttackProfileFor(Scavar::AiArchetype Arch)
{
	switch (Arch)
	{
	case Scavar::AiArchetype::RoadGuard: return {55.f, 3, 8000.f, 0.5f};   // 7.62x39 PS
	case Scavar::AiArchetype::Militia:   return {28.f, 1, 4000.f, 0.8f};   // 9x18 FMJ
	case Scavar::AiArchetype::Wolf:      return {25.f, 1, 180.f, 1.2f};    // bite
	default:                             return {28.f, 1, 5000.f, 0.9f};   // marauder, 9x18 FMJ
	}
}

EScavarHitZone RollZone(Scavar::Rng& Rng)
{
	const double R = Rng.NextUnit();
	if (R < 0.10) { return EScavarHitZone::Head; }
	if (R < 0.60) { return EScavarHitZone::Torso; }
	if (R < 0.70) { return EScavarHitZone::ArmL; }
	if (R < 0.80) { return EScavarHitZone::ArmR; }
	if (R < 0.90) { return EScavarHitZone::LegL; }
	return EScavarHitZone::LegR;
}
}

AScavarAiZone::AScavarAiZone()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

bool UScavarAiSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld();
}

bool UScavarAiSubsystem::IsServerWorld() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

void UScavarAiSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!IsServerWorld())
	{
		return;
	}
	Rng.Seed(static_cast<uint64>(FDateTime::UtcNow().GetTicks()) ^ 0xA11CE);
	BuildZones();
	bReady = true;
	UE_LOG(LogScavarAI, Log, TEXT("AI director ready: %d zones."), (int32)Zones.size());
}

void UScavarAiSubsystem::BuildZones()
{
	Zones.clear();
	ZoneArchetype.Reset();
	int64 NextZoneId = 1;
	for (TActorIterator<AScavarAiZone> It(GetWorld()); It; ++It)
	{
		const AScavarAiZone* Z = *It;
		Scavar::AiZone Out;
		Out.Id = NextZoneId++;
		Out.MaxGroups = Z->MaxGroups;
		Out.CooldownMinS = Z->CooldownMinSeconds;
		Out.CooldownMaxS = Z->CooldownMaxSeconds;
		Scavar::AiGroupTemplate Template;
		Template.Archetype = static_cast<Scavar::AiArchetype>(static_cast<uint8>(Z->Archetype));
		Template.MinSize = Z->MinGroupSize;
		Template.MaxSize = Z->MaxGroupSize;
		Out.Templates.push_back(Template);
		if (Z->SpawnPointOffsets.Num() > 0)
		{
			for (const FVector& Offset : Z->SpawnPointOffsets)
			{
				Out.SpawnPoints.push_back(ToMeters(Z->GetActorTransform().TransformPosition(Offset)));
			}
		}
		else
		{
			for (int32 i = 0; i < 6; ++i)
			{
				const float Angle = 2.f * PI * i / 6.f;
				Out.SpawnPoints.push_back(ToMeters(Z->GetActorLocation() + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Z->RingRadiusCm));
			}
		}
		ZoneArchetype.Add(Out.Id, Z->Archetype);
		Zones.push_back(std::move(Out));
	}
}

void UScavarAiSubsystem::ReportGunshot(const FVector& LocationCm, bool bSuppressed, int64 SourceId)
{
	if (!IsServerWorld())
	{
		return;
	}
	Scavar::AiSound Sound;
	Sound.Pos = ToMeters(LocationCm);
	Sound.Kind = bSuppressed ? Scavar::SoundKind::GunshotSuppressed : Scavar::SoundKind::Gunshot;
	Sound.SourceId = SourceId;
	PendingSounds.push_back(Sound);
	RecentShooters.Add(SourceId, GetWorld()->GetTimeSeconds());
}

void UScavarAiSubsystem::ReportNpcAttacked(AScavarNPC* Npc, int64 AttackerId)
{
	if (IsServerWorld() && Npc)
	{
		Scavar::NotifyAttacked(Npc->Brain(), AttackerId);
	}
}

AActor* UScavarAiSubsystem::FindPlayerPawn(int64 TargetId) const
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (It->Get() && It->Get()->GetPawn() && static_cast<int64>(It->Get()->GetUniqueID()) == TargetId)
		{
			return It->Get()->GetPawn();
		}
	}
	return nullptr;
}

void UScavarAiSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bReady)
	{
		return;
	}
	Accumulator += DeltaTime;
	if (Accumulator < AiStepSeconds)
	{
		return;
	}
	const float Step = Accumulator;
	Accumulator = 0.f;
	const double Now = GetWorld()->GetTimeSeconds();

	TArray<FVector> PlayerLocations;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const AScavarCharacter* Pawn = It->Get() ? Cast<AScavarCharacter>(It->Get()->GetPawn()) : nullptr;
		if (Pawn && !Pawn->GetVitals()->IsDead())
		{
			PlayerLocations.Add(Pawn->GetActorLocation());
		}
	}
	if (Now - LastDirectorSeconds >= DirectorStepSeconds)
	{
		LastDirectorSeconds = Now;
		RunDirector(Now, PlayerLocations);
	}
	TickNpcs(Step, Now);
	PendingSounds.clear();
}

void UScavarAiSubsystem::RunDirector(double Now, const TArray<FVector>& PlayerLocationsCm)
{
	// keep group positions current so the director can despawn groups nobody is near
	for (Scavar::ActiveGroup& Group : Director.Groups)
	{
		FVector Sum = FVector::ZeroVector;
		int32 Count = 0;
		for (const TWeakObjectPtr<AScavarNPC>& Weak : Npcs)
		{
			if (const AScavarNPC* Npc = Weak.Get(); Npc && Npc->GetGroupId() == Group.Id)
			{
				Sum += Npc->GetActorLocation();
				++Count;
			}
		}
		if (Count > 0)
		{
			Group.Pos = ToMeters(Sum / Count);
		}
	}

	Scavar::DirectorWorld World;
	World.Now = Now;
	for (const FVector& P : PlayerLocationsCm) { World.Players.push_back(ToMeters(P)); }
	UWorld* UnrealWorld = GetWorld();
	World.VisibleToAnyPlayer = [UnrealWorld, &PlayerLocationsCm](const Scavar::Vec3& Pos)
	{
		const FVector Target = ToCm(Pos) + FVector(0.f, 0.f, 100.f);
		for (const FVector& Eye : PlayerLocationsCm)
		{
			if (FVector::Dist(Eye, Target) < 30000.f && !UnrealWorld->LineTraceTestByChannel(Eye, Target, ECC_Visibility))
			{
				return true;
			}
		}
		return false;
	};

	const Scavar::DirectorOutput Out = Scavar::DirectorTick(Director, Zones, World, DirectorTune, Rng);
	for (const int64 GroupId : Out.Despawns) { DespawnGroup(GroupId); }
	for (const Scavar::SpawnOrder& Order : Out.Spawns) { SpawnGroup(Order); }
}

void UScavarAiSubsystem::SpawnGroup(const Scavar::SpawnOrder& Order)
{
	UWorld* World = GetWorld();
	for (int32 i = 0; i < Order.Size; ++i)
	{
		const float Angle = 2.f * PI * i / FMath::Max(1, Order.Size);
		FVector Location = ToCm(Order.Pos) + FVector(FMath::Cos(Angle) * 150.f, FMath::Sin(Angle) * 150.f, 0.f);
		FHitResult Ground;
		if (World->LineTraceSingleByChannel(Ground, Location + FVector(0, 0, 2000.f), Location - FVector(0, 0, 5000.f), ECC_Visibility))
		{
			Location = Ground.ImpactPoint;
		}
		Location.Z += 110.f;

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		AScavarNPC* Npc = World->SpawnActor<AScavarNPC>(AScavarNPC::StaticClass(), Location, FRotator::ZeroRotator, Params);
		if (!Npc)
		{
			continue;
		}
		const EScavarArchetype Arch = static_cast<EScavarArchetype>(static_cast<uint8>(Order.Archetype));
		Npc->InitBrain(NextNpcId++, Arch, Order.GroupId);
		Npc->OnNpcDied.AddUObject(this, &UScavarAiSubsystem::HandleNpcDied, Npc);
		Npcs.Add(Npc);
	}
	UE_LOG(LogScavarAI, Log, TEXT("Spawned group %lld: %d x archetype %d (active NPCs: %d)"), Order.GroupId, Order.Size, (int32)Order.Archetype, Npcs.Num());
}

void UScavarAiSubsystem::DespawnGroup(int64 GroupId)
{
	for (int32 i = Npcs.Num() - 1; i >= 0; --i)
	{
		AScavarNPC* Npc = Npcs[i].Get();
		if (!Npc) { Npcs.RemoveAt(i); continue; }
		if (Npc->GetGroupId() == GroupId)
		{
			Npc->Destroy();
			Npcs.RemoveAt(i);
		}
	}
}

void UScavarAiSubsystem::TickNpcs(float Dt, double Now)
{
	// players as AI targets
	std::vector<Scavar::AiTarget> Targets;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const AScavarCharacter* Pawn = It->Get() ? Cast<AScavarCharacter>(It->Get()->GetPawn()) : nullptr;
		if (!Pawn || Pawn->GetVitals()->IsDead()) { continue; }
		Scavar::AiTarget T;
		T.Id = static_cast<int64>(It->Get()->GetUniqueID());
		T.Pos = ToMeters(Pawn->GetActorLocation());
		T.Stance = Pawn->bIsCrouched ? Scavar::TargetStance::Crouch : Scavar::TargetStance::Standing;
		const float Speed = Pawn->GetVelocity().Size2D();
		T.Move = Speed < 10.f ? Scavar::MoveKind::Still
		       : (Pawn->bIsCrouched ? Scavar::MoveKind::Crouch : (Speed > 450.f ? Scavar::MoveKind::Sprint : Scavar::MoveKind::Walk));
		const double* LastShot = RecentShooters.Find(T.Id);
		T.AttackedOthers = LastShot && (Now - *LastShot) < ShooterMemorySeconds;
		Targets.push_back(T);
	}

	Scavar::AiWorldView View;
	View.Time = Now;
	const AScavarGameState* State = GetWorld()->GetGameState<AScavarGameState>();
	View.Night = State && State->IsNight();
	View.Targets = std::move(Targets);
	View.Sounds = PendingSounds;
	UWorld* UnrealWorld = GetWorld();
	View.HasLineOfSight = [UnrealWorld](const Scavar::Vec3& From, const Scavar::Vec3& To)
	{
		return !UnrealWorld->LineTraceTestByChannel(ToCm(From) + FVector(0, 0, 160.f), ToCm(To) + FVector(0, 0, 120.f), ECC_Visibility);
	};

	// group bookkeeping
	TMap<int64, FIntPoint> GroupCounts; // total, alive
	for (const TWeakObjectPtr<AScavarNPC>& Weak : Npcs)
	{
		if (const AScavarNPC* Npc = Weak.Get())
		{
			FIntPoint& C = GroupCounts.FindOrAdd(Npc->GetGroupId());
			C.X += 1;
			C.Y += Npc->GetVitals()->IsDead() ? 0 : 1;
		}
	}

	for (int32 i = Npcs.Num() - 1; i >= 0; --i)
	{
		AScavarNPC* Npc = Npcs[i].Get();
		if (!Npc) { Npcs.RemoveAt(i); continue; }
		if (Npc->GetVitals()->IsDead()) { continue; }

		Scavar::AiGroupInfo Group;
		const FIntPoint Counts = GroupCounts.FindRef(Npc->GetGroupId());
		Group.Total = FMath::Max(1, Counts.X);
		Group.Alive = FMath::Max(1, Counts.Y);
		Group.ThreatCount = 0;
		for (const Scavar::AiTarget& T : View.Targets)
		{
			if (T.Pos.Dist2D(Npc->Brain().Pos) < 60.0) { ++Group.ThreatCount; }
		}
		Group.ThreatCount = FMath::Max(1, Group.ThreatCount);

		const std::vector<Scavar::AiCommand> Commands = Scavar::TickNpc(Npc->Brain(), Group, View, Dt, Rng);
		bool bMoved = false;
		for (const Scavar::AiCommand& Command : Commands)
		{
			bMoved = bMoved || Command.Type == Scavar::AiCommandType::MoveTo || Command.Type == Scavar::AiCommandType::Flee;
			ExecuteCommand(*Npc, Command, Now);
		}
		if (!bMoved)
		{
			Npc->ClearMoveGoal();
		}

		// Predators without a ranged attack (CanShoot == false) only receive MoveTo from the brain; the bite is applied here.
		const Scavar::ArchetypeDef& Def = Scavar::ArchetypeFor(Npc->Brain().Arch);
		if (!Def.CanShoot && Npc->Brain().State == Scavar::AiState::Engage && Npc->Brain().FocusTarget != 0 && Npc->Brain().FireCooldown <= 0.0)
		{
			const AActor* Focus = FindPlayerPawn(Npc->Brain().FocusTarget);
			const FNpcAttackProfile Bite = AttackProfileFor(Npc->Brain().Arch);
			if (Focus && FVector::Dist(Focus->GetActorLocation(), Npc->GetActorLocation()) <= Bite.RangeCm)
			{
				ApplyNpcAttack(*Npc, Npc->Brain().FocusTarget);
				Npc->Brain().FireCooldown = Bite.CooldownSeconds;
			}
		}
	}
}

void UScavarAiSubsystem::ExecuteCommand(AScavarNPC& Npc, const Scavar::AiCommand& Command, double Now)
{
	switch (Command.Type)
	{
	case Scavar::AiCommandType::MoveTo:
		Npc.SetMoveGoal(ToCm(Command.Pos), Command.Run);
		break;
	case Scavar::AiCommandType::Flee:
		Npc.SetMoveGoal(ToCm(Command.Pos), true);
		break;
	case Scavar::AiCommandType::Shoot:
		ApplyNpcAttack(Npc, Command.TargetId);
		break;
	case Scavar::AiCommandType::Speak:
	{
		const TCHAR* Line = Command.Speech == Scavar::SpeechKind::Warn ? TEXT("Stop right there!")
		                  : (Command.Speech == Scavar::SpeechKind::AskHelp ? TEXT("Please... can you help me?") : TEXT("Over here!"));
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			if (APlayerController* PC = It->Get(); PC && PC->GetPawn() && FVector::Dist(PC->GetPawn()->GetActorLocation(), Npc.GetActorLocation()) < 3500.f)
			{
				PC->ClientMessage(FString::Printf(TEXT("[%s] %s"), *StaticEnum<EScavarArchetype>()->GetNameStringByValue(static_cast<int64>(Npc.GetArchetype())), Line));
			}
		}
		break;
	}
	case Scavar::AiCommandType::ReportAllies:
		break; // group-level alerting is handled inside the brain through shared awareness
	}
}

void UScavarAiSubsystem::ApplyNpcAttack(AScavarNPC& Npc, int64 TargetId)
{
	AActor* TargetPawn = FindPlayerPawn(TargetId);
	AScavarCharacter* Target = Cast<AScavarCharacter>(TargetPawn);
	if (!Target || Target->GetVitals()->IsDead())
	{
		return;
	}
	const FNpcAttackProfile Profile = AttackProfileFor(Npc.Brain().Arch);
	const float Distance = FVector::Dist(Npc.GetActorLocation(), Target->GetActorLocation());
	if (Distance > Profile.RangeCm)
	{
		Npc.SetMoveGoal(Target->GetActorLocation(), true); // wolves close in to bite range
		return;
	}
	const Scavar::ArchetypeDef& Def = Scavar::ArchetypeFor(Npc.Brain().Arch);
	const bool bMelee = Profile.RangeCm < 500.f;
	if (!bMelee)
	{
		ReportGunshot(Npc.GetActorLocation(), false, Npc.Brain().Id);
	}
	const double HitChance = FMath::Clamp(Def.Accuracy * (1.0 - Distance / (Profile.RangeCm * 1.4)), 0.05, 0.95);
	if (Rng.Chance(HitChance))
	{
		Target->GetVitals()->ApplyProjectileHit(RollZone(Rng), Profile.Damage, Profile.PenTier);
	}
}

void UScavarAiSubsystem::HandleNpcDied(AScavarNPC* Npc)
{
	if (!Npc)
	{
		return;
	}
	// A small drop for the pilot: bandage / bread / pistol ammo, rarely a pistol.
	if (UScavarWorldInventory* Inventory = GetWorld()->GetSubsystem<UScavarWorldInventory>())
	{
		const double R = Rng.NextUnit();
		const FName Drop = R < 0.10 ? FName("W_PM") : (R < 0.45 ? FName("A_918_FMJ") : (R < 0.75 ? FName("MED_BANDAGE") : FName("FOOD_BREAD")));
		const Scavar::ItemRow* Row = Inventory->GetTable().Find(TCHAR_TO_UTF8(*Drop.ToString()));
		if (Row && Npc->GetArchetype() != EScavarArchetype::Disoriented)
		{
			Inventory->SpawnPickup(Drop, Row->QtyMin == Row->QtyMax ? Row->QtyMin : Rng.RangeInt(Row->QtyMin, Row->QtyMax), Npc->GetActorLocation() - FVector(0, 0, 80.f));
		}
	}

	// When the whole group is dead, start the zone cooldown.
	const int64 GroupId = Npc->GetGroupId();
	bool bAnyAlive = false;
	for (const TWeakObjectPtr<AScavarNPC>& Weak : Npcs)
	{
		const AScavarNPC* Other = Weak.Get();
		if (Other && Other != Npc && Other->GetGroupId() == GroupId && !Other->GetVitals()->IsDead())
		{
			bAnyAlive = true;
			break;
		}
	}
	if (!bAnyAlive)
	{
		Scavar::DirectorGroupKilled(Director, Zones, GroupId, GetWorld()->GetTimeSeconds(), Rng);
	}
}
