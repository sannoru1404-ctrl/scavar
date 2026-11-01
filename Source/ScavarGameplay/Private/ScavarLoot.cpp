#include "ScavarLoot.h"
#include "ScavarLog.h"
#include "ScavarPickup.h"
#include "ScavarWorldInventory.h"
#include "ScavarWorldSave.h"
#include "EngineUtils.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

AScavarLootSpawnPoint::AScavarLootSpawnPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
#if WITH_EDITORONLY_DATA
	SetCanBeDamaged(false);
#endif
}

bool UScavarLootSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld();
}

bool UScavarLootSubsystem::IsServerWorld() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

void UScavarLootSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!IsServerWorld())
	{
		return;
	}
	UScavarWorldInventory* Inventory = InWorld.GetSubsystem<UScavarWorldInventory>();
	if (!Inventory || !Inventory->IsReady())
	{
		UE_LOG(LogScavarInventory, Warning, TEXT("Loot disabled: world inventory is not ready."));
		return;
	}
	Types = Inventory->GetTable().BuildLootTypes(Inventory->GetGameData(), Scavar::Release::Slice);
	Rng.Seed(static_cast<uint64>(FDateTime::UtcNow().GetTicks()));
	RebuildPoints();
	bReady = true;
	UE_LOG(LogScavarInventory, Log, TEXT("Loot economy ready: %d item types, %d spawn points."), (int32)Types.size(), (int32)Points.size());
}

void UScavarLootSubsystem::RebuildPoints()
{
	Points.clear();
	PointLocations.Reset();
	int64 NextId = 1;
	for (TActorIterator<AScavarLootSpawnPoint> It(GetWorld()); It; ++It)
	{
		const AScavarLootSpawnPoint* P = *It;
		Scavar::LootSpawnPoint Out;
		Out.Id = NextId++;
		Out.Pos = Scavar::Vec3{P->GetActorLocation().X / 100.0, P->GetActorLocation().Y / 100.0, P->GetActorLocation().Z / 100.0};
		for (const FName Tag : P->UsageTags)
		{
			Out.UsageTags.push_back(TCHAR_TO_UTF8(*Tag.ToString()));
		}
		Out.Tier = static_cast<Scavar::RiskTier>(static_cast<uint8>(P->Tier));
		Out.Size = static_cast<Scavar::LootSize>(static_cast<uint8>(P->MaxItemSize));
		Out.BuildingId = P->BuildingId != 0 ? P->BuildingId : 1000000 + Out.Id; // standalone points never share a cooldown
		Points.push_back(std::move(Out));
		PointLocations.Add(P->GetActorLocation());
	}
}

void UScavarLootSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bReady)
	{
		return;
	}
	// Never fill a world that is still being loaded from disk (it would double the loot).
	if (const UScavarWorldSave* Save = GetWorld()->GetSubsystem<UScavarWorldSave>(); Save && !Save->IsReady())
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	Scavar::LootTuning Tuning;
	Tuning.CycleIntervalS = CycleIntervalSeconds;
	Tuning.WeaponPointFraction = WeaponPointFraction;
	if (Scavar::CycleDue(LastCycleSeconds, Now, Tuning))
	{
		RunCycleNow();
	}
	ExpireGroundItems(Now);
}

int32 UScavarLootSubsystem::RunCycleNow()
{
	UWorld* World = GetWorld();
	UScavarWorldInventory* Inventory = World ? World->GetSubsystem<UScavarWorldInventory>() : nullptr;
	if (!bReady || !Inventory || !Inventory->GetStore())
	{
		return 0;
	}
	const Scavar::ItemStore& Store = *Inventory->GetStore();
	const double Now = World->GetTimeSeconds();
	LastCycleSeconds = Now;

	// Forget points whose pile has been picked up or has expired.
	for (auto It = PointToItem.CreateIterator(); It; ++It)
	{
		const Scavar::ItemInstance* Item = Store.GetItem(It.Value());
		const Scavar::Container* C = Item ? Store.GetContainer(Item->Container) : nullptr;
		if (!Item || !C || C->Kind != Scavar::ContainerKind::Ground)
		{
			It.RemoveCurrent();
		}
	}

	Scavar::LootWorldSnapshot Snapshot;
	Snapshot.NowS = Now;
	for (const auto& Entry : Store.AllItems())
	{
		Snapshot.Counts[Entry.second.DefId] += 1;
	}
	for (const TPair<int64, double>& Entry : BuildingLastSpawn)
	{
		Snapshot.BuildingLastSpawnS[Entry.Key] = Entry.Value;
	}
	for (const TPair<int64, int64>& Entry : PointToItem)
	{
		Snapshot.OccupiedPointIds.insert(Entry.Key);
		const Scavar::LootSpawnPoint& P = Points[static_cast<size_t>(Entry.Key - 1)];
		const Scavar::ItemInstance* Item = Store.GetItem(Entry.Value);
		const Scavar::WeaponDef* W = Item ? Inventory->GetGameData().FindWeapon(Item->DefId) : nullptr;
		if (W && W->Class != "melee" && W->Class != "throwable")
		{
			Snapshot.WeaponPilesByBuilding[P.BuildingId] += 1;
		}
	}

	TArray<FVector> PlayerLocations;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (const APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr)
		{
			Snapshot.PlayerPositions.push_back(Scavar::Vec3{Pawn->GetActorLocation().X / 100.0, Pawn->GetActorLocation().Y / 100.0, Pawn->GetActorLocation().Z / 100.0});
			PlayerLocations.Add(Pawn->GetPawnViewLocation());
		}
	}
	// A point is "visible" if any player within 120 m has an unobstructed line to it.
	Snapshot.IsVisibleToAnyPlayer = [World, &PlayerLocations](const Scavar::Vec3& Pos)
	{
		const FVector Target(Pos.x * 100.0, Pos.y * 100.0, Pos.z * 100.0 + 50.0);
		for (const FVector& Eye : PlayerLocations)
		{
			if (FVector::Dist(Eye, Target) < 12000.f && !World->LineTraceTestByChannel(Eye, Target, ECC_Visibility))
			{
				return true;
			}
		}
		return false;
	};

	Scavar::LootTuning Tuning;
	Tuning.CycleIntervalS = CycleIntervalSeconds;
	Tuning.WeaponPointFraction = WeaponPointFraction;
	const std::vector<Scavar::SpawnDecision> Decisions = Scavar::RunCycle(Types, Points, Snapshot, Tuning, Rng);

	int32 Spawned = 0;
	for (const Scavar::SpawnDecision& D : Decisions)
	{
		const Scavar::ItemRow* Row = Inventory->GetTable().Find(D.DefId);
		if (!Row || D.PointId < 1 || D.PointId > static_cast<int64>(PointLocations.Num()))
		{
			continue;
		}
		const int32 Qty = Rng.RangeInt(Row->QtyMin, Row->QtyMax);
		const FVector Location = PointLocations[static_cast<int32>(D.PointId - 1)];
		if (AScavarPickup* Pickup = Inventory->SpawnPickup(FName(UTF8_TO_TCHAR(D.DefId.c_str())), Qty, Location))
		{
			PointToItem.Add(D.PointId, Pickup->GetItemId());
			BuildingLastSpawn.Add(D.BuildingId, Now);
			++Spawned;
		}
	}
	UE_LOG(LogScavarInventory, Log, TEXT("Loot cycle: %d piles spawned (%d decisions)."), Spawned, (int32)Decisions.size());
	return Spawned;
}

void UScavarLootSubsystem::ExpireGroundItems(double Now)
{
	UScavarWorldInventory* Inventory = GetWorld()->GetSubsystem<UScavarWorldInventory>();
	if (!Inventory)
	{
		return;
	}
	std::vector<Scavar::GroundItemRecord> Records;
	Inventory->ForEachPickup([&](AScavarPickup& Pickup)
	{
		Scavar::GroundItemRecord R;
		R.ItemId = Pickup.GetItemId();
		R.DefId = TCHAR_TO_UTF8(*Pickup.GetDefId().ToString());
		R.DroppedAtS = Pickup.GetSpawnTimeSeconds();
		R.OnGround = true;
		Records.push_back(std::move(R));
	});
	const std::vector<int64_t> Expired = Scavar::ExpiredGroundItems(Records, Types, Now);
	for (const int64_t Id : Expired)
	{
		Inventory->Execute([&](Scavar::ItemStore& S) { return S.Remove(Id); });
	}
}
