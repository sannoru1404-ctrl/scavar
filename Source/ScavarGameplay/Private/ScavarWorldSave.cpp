#include "ScavarWorldSave.h"
#include "ScavarCharacter.h"
#include "ScavarInventoryComponent.h"
#include "ScavarLog.h"
#include "ScavarPickup.h"
#include "ScavarVitalsComponent.h"
#include "ScavarWorldInventory.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include <cstdio>
#include <set>

namespace
{
constexpr float SaveIntervalSeconds = 30.f;

std::string CharKey(const FString& AccountKey)
{
	return std::string("char:") + std::to_string(UScavarWorldInventory::HashAccountKey(AccountKey));
}

std::string ToStd(const FString& S) { return std::string(TCHAR_TO_UTF8(*S)); }
}

bool UScavarWorldSave::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

bool UScavarWorldSave::InitializeWhenInventoryReady()
{
	UScavarWorldInventory* Inventory = GetWorld()->GetSubsystem<UScavarWorldInventory>();
	if (!Inventory || !Inventory->IsReady())
	{
		return false;
	}

	const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("ScavarWorld"));
	const FString Path = Dir / TEXT("world.sqlite");
	IFileManager::Get().MakeDirectory(*Dir, true);
	if (FParse::Param(FCommandLine::Get(), TEXT("ScavarFreshWorld")))
	{
		IFileManager::Get().Delete(*Path);
		IFileManager::Get().Delete(*(Path + TEXT("-wal")));
		IFileManager::Get().Delete(*(Path + TEXT("-shm")));
		UE_LOG(LogScavarPersistence, Log, TEXT("-ScavarFreshWorld: previous save deleted."));
	}

	std::string Error;
	if (!Database.Open(ToStd(Path), &Error))
	{
		UE_LOG(LogScavarPersistence, Error, TEXT("Cannot open %s: %s. Running WITHOUT persistence."), *Path, UTF8_TO_TCHAR(Error.c_str()));
		return true; // not fatal for the pilot: play on, nothing is saved
	}
	bOpened = true;

	// Load must happen inside a transaction-free window: nothing else has touched the store yet (loot is gated on IsReady()).
	{
		std::string LoadError;
		if (!Database.LoadInto(*Inventory->GetStoreForLoad(), &LoadError))
		{
			UE_LOG(LogScavarPersistence, Error, TEXT("Load failed: %s. Running WITHOUT persistence."), UTF8_TO_TCHAR(LoadError.c_str()));
			Database.Close();
			bOpened = false;
			return true;
		}
	}
	Database.ListState("char:", &CachedCharacters, &Error);

	// From now on every committed change goes to the database.
	Inventory->PersistSink = [this](const Scavar::ChangeSet& Changes)
	{
		std::string WriteError;
		if (!Database.Apply(Changes, &WriteError))
		{
			UE_LOG(LogScavarPersistence, Error, TEXT("Saving item changes failed: %s"), UTF8_TO_TCHAR(WriteError.c_str()));
		}
	};

	RecoverOrphanMagwells();
	RestorePickups();
	UE_LOG(LogScavarPersistence, Log, TEXT("World save ready: %s (%d items, %d containers)."), *Path,
		(int32)Inventory->GetStore()->AllItems().size(), (int32)Inventory->GetStore()->AllContainers().size());
	return true;
}

void UScavarWorldSave::RecoverOrphanMagwells()
{
	// A magazine inside a weapon whose owner vanished (crash, kill) would otherwise be lost: put it in the owner's backpack.
	UScavarWorldInventory* Inventory = GetWorld()->GetSubsystem<UScavarWorldInventory>();
	Inventory->Execute([&](Scavar::ItemStore& S)
	{
		std::vector<Scavar::ContainerId> Magwells;
		for (const auto& Entry : S.AllContainers())
		{
			if (Entry.second.Kind == Scavar::ContainerKind::Magwell) { Magwells.push_back(Entry.first); }
		}
		for (const Scavar::ContainerId Id : Magwells)
		{
			const int64 Owner = S.GetContainer(Id)->Owner;
			Scavar::ContainerId Home = Inventory->FindOwnedContainer(Owner, Scavar::ContainerKind::Backpack);
			if (Home == 0) { Home = Inventory->FindOwnedContainer(Owner, Scavar::ContainerKind::Returns); }
			if (Home == 0) { continue; }
			const std::vector<Scavar::ItemId> Contents = S.ItemsIn(Id);
			bool bAllMoved = true;
			for (const Scavar::ItemId Item : Contents)
			{
				bAllMoved = (S.AutoMove(Item, Home, true, Id) == Scavar::Status::Ok) && bAllMoved;
			}
			if (bAllMoved) { S.RemoveContainer(Id); }
		}
		return Scavar::Status::Ok;
	});
}

void UScavarWorldSave::RestorePickups()
{
	UScavarWorldInventory* Inventory = GetWorld()->GetSubsystem<UScavarWorldInventory>();
	std::map<std::string, std::string> Saved;
	std::string Error;
	Database.ListState("pickup:", &Saved, &Error);

	struct FRestore { Scavar::ItemId Item; Scavar::ContainerId Ground; FVector Location; bool bHasLocation; };
	TArray<FRestore> Work;
	for (const auto& Entry : Inventory->GetStore()->AllContainers())
	{
		if (Entry.second.Kind != Scavar::ContainerKind::Ground) { continue; }
		const std::vector<Scavar::ItemId>& Items = Inventory->GetStore()->ItemsIn(Entry.first);
		FRestore R{Items.empty() ? 0 : Items.front(), Entry.first, FVector::ZeroVector, false};
		const auto It = Saved.find("pickup:" + std::to_string(R.Item));
		if (It != Saved.end())
		{
			double X = 0, Y = 0, Z = 0;
			R.bHasLocation = sscanf(It->second.c_str(), "%lf,%lf,%lf", &X, &Y, &Z) == 3;
			R.Location = FVector(X, Y, Z);
		}
		Work.Add(R);
	}

	int32 Restored = 0, Discarded = 0;
	for (const FRestore& R : Work)
	{
		if (R.Item != 0 && R.bHasLocation)
		{
			Inventory->AttachPickup(R.Item, R.Ground, R.Location);
			++Restored;
			continue;
		}
		// Ground items whose position was never saved (dropped within the last 30 s of a crash) cannot be shown anywhere.
		// They are returned to their owner's stash/returns when an owner exists; otherwise they are discarded with a log line.
		++Discarded;
		Inventory->Execute([&](Scavar::ItemStore& S)
		{
			if (R.Item != 0) { S.Remove(R.Item); }
			return S.RemoveContainer(R.Ground);
		});
	}
	UE_LOG(LogScavarPersistence, Log, TEXT("Restored %d pickups, discarded %d ground items without a saved position."), Restored, Discarded);
}

void UScavarWorldSave::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bReady)
	{
		if (!bInitAttempted && InitializeWhenInventoryReady())
		{
			bInitAttempted = true;
			bReady = true;
		}
		return;
	}
	SinceLastSave += DeltaTime;
	if (SinceLastSave >= SaveIntervalSeconds)
	{
		SinceLastSave = 0.f;
		SaveAllNow();
	}
}

void UScavarWorldSave::SavePickups()
{
	UScavarWorldInventory* Inventory = GetWorld()->GetSubsystem<UScavarWorldInventory>();
	std::string Error;
	std::map<std::string, std::string> Old;
	Database.ListState("pickup:", &Old, &Error);
	std::set<std::string> Live;
	Inventory->ForEachPickup([&](AScavarPickup& Pickup)
	{
		const FVector L = Pickup.GetActorLocation();
		const std::string Key = "pickup:" + std::to_string(Pickup.GetItemId());
		Live.insert(Key);
		char Buffer[96];
		snprintf(Buffer, sizeof(Buffer), "%.1f,%.1f,%.1f", L.X, L.Y, L.Z);
		Database.SetState(Key, Buffer, &Error);
	});
	for (const auto& Entry : Old)
	{
		if (Live.find(Entry.first) == Live.end()) { Database.DeleteState(Entry.first, &Error); }
	}
}

void UScavarWorldSave::SaveCharacter(const AScavarCharacter& Character)
{
	const UScavarInventoryComponent* Inventory = Character.GetInventory();
	const UScavarVitalsComponent* Vitals = Character.GetVitals();
	if (!bOpened || !Inventory || !Vitals || Vitals->IsDead() || Inventory->GetAccountKey().IsEmpty())
	{
		return;
	}
	const FVector L = Character.GetActorLocation();
	char Buffer[96];
	snprintf(Buffer, sizeof(Buffer), "%.1f,%.1f,%.1f,%.1f|", L.X, L.Y, L.Z, Character.GetActorRotation().Yaw);
	const std::string Value = std::string(Buffer) + ToStd(Vitals->ExportSave());
	const std::string Key = CharKey(Inventory->GetAccountKey());
	std::string Error;
	Database.SetState(Key, Value, &Error);
	CachedCharacters[Key] = Value;
}

void UScavarWorldSave::SaveCharacters()
{
	for (TActorIterator<AScavarCharacter> It(GetWorld()); It; ++It)
	{
		SaveCharacter(**It);
	}
}

void UScavarWorldSave::ForgetCharacter(const FString& AccountKey)
{
	if (!bOpened) { return; }
	std::string Error;
	Database.DeleteState(CharKey(AccountKey), &Error);
	CachedCharacters.erase(CharKey(AccountKey));
}

bool UScavarWorldSave::TakeCharacterState(const FString& AccountKey, FVector& OutLocation, float& OutYaw, FString& OutVitals)
{
	const auto It = CachedCharacters.find(CharKey(AccountKey));
	if (It == CachedCharacters.end())
	{
		return false;
	}
	const std::string& Value = It->second;
	const size_t Bar = Value.find('|');
	double X = 0, Y = 0, Z = 0, Yaw = 0;
	if (Bar == std::string::npos || sscanf(Value.substr(0, Bar).c_str(), "%lf,%lf,%lf,%lf", &X, &Y, &Z, &Yaw) != 4)
	{
		return false;
	}
	OutLocation = FVector(X, Y, Z);
	OutYaw = static_cast<float>(Yaw);
	OutVitals = FString(UTF8_TO_TCHAR(Value.substr(Bar + 1).c_str()));
	return true;
}

void UScavarWorldSave::SaveValue(const FString& Key, const FString& Value)
{
	if (!bOpened) { return; }
	std::string Error;
	Database.SetState("value:" + ToStd(Key), ToStd(Value), &Error);
}

bool UScavarWorldSave::LoadValue(const FString& Key, FString& OutValue) const
{
	if (!bOpened) { return false; }
	std::string Value, Error;
	// GetState is not const (it prepares statements), but it does not change observable state.
	if (const_cast<Scavar::Db&>(Database).GetState("value:" + ToStd(Key), &Value, &Error))
	{
		OutValue = FString(UTF8_TO_TCHAR(Value.c_str()));
		return true;
	}
	return false;
}

void UScavarWorldSave::SaveAllNow()
{
	if (!bOpened) { return; }
	SavePickups();
	SaveCharacters();
}

void UScavarWorldSave::Deinitialize()
{
	if (bOpened && GetWorld())
	{
		SaveAllNow();
		Database.Close();
		bOpened = false;
	}
	Super::Deinitialize();
}
