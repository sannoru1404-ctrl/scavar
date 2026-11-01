#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Db.h"
#include "ScavarWorldSave.generated.h"

class AScavarCharacter;

/**
 * Host-side persistence (SYS-12) for the pilot: items and containers go to SQLite on every committed change; pickup positions and
 * living characters (position + vitals) are written every 30 s, on logout and on shutdown. Start the game with -ScavarFreshWorld to
 * delete the save first. Loot spawning waits until this subsystem is ready so a loaded world is never filled twice.
 */
UCLASS()
class SCAVARGAMEPLAY_API UScavarWorldSave : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UScavarWorldSave, STATGROUP_Tickables); }

	/** True once the save has been loaded (or persistence is disabled) so dependent systems may start. */
	bool IsReady() const { return bReady; }

	/** Writes one character now (logout). */
	void SaveCharacter(const AScavarCharacter& Character);
	void ForgetCharacter(const FString& AccountKey);
	/** Returns the stored position/vitals of a living character, if any. */
	bool TakeCharacterState(const FString& AccountKey, FVector& OutLocation, float& OutYaw, FString& OutVitals);

	void SaveAllNow();

private:
	bool InitializeWhenInventoryReady();
	void RestorePickups();
	void RecoverOrphanMagwells();
	void SavePickups();
	void SaveCharacters();

	Scavar::Db Database;
	bool bOpened = false;
	bool bReady = false;
	bool bInitAttempted = false;
	float SinceLastSave = 0.f;
	std::map<std::string, std::string> CachedCharacters; // "char:<hash>" -> "x,y,z,yaw|vitals"
};
