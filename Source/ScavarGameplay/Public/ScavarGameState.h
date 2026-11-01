#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Sim/WorldClock.h"
#include "ScavarGameState.generated.h"

class ADirectionalLight;

/**
 * The single world clock (SYS-11). The server owns a Scavar::WorldClock, ticks it, saves it, and replicates its serialized state once a
 * second; every machine keeps an identical local copy that it advances between updates (the clock is deterministic), then points the
 * level's directional light at the computed sun position.
 */
UCLASS()
class SCAVARGAMEPLAY_API AScavarGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AScavarGameState();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Real minutes per 24 game hours (spec default 240, ServerConfig DayLengthMinutes). */
	UPROPERTY(EditDefaultsOnly, Category = "Scavar|Clock") float DayLengthMinutes = 240.f;

	float GetHourOfDay() const { return static_cast<float>(Clock.HourOfDay()); }
	bool IsNight() const { return Clock.IsNight(); }
	Scavar::Weather GetWeather() const { return Clock.CurrentWeather(); }
	float GetAmbientTemperatureC() const { return static_cast<float>(Clock.AmbientTemperatureC()); }

	/** Server, development only: jumps to the given game hour (weather restarts). */
	void DebugSetHour(float Hour);

	FString SerializeClock() const { return FString(UTF8_TO_TCHAR(Clock.Serialize().c_str())); }

private:
	UFUNCTION() void OnRep_ClockState();
	void ApplySun();
	void LoadSavedClock();

	UPROPERTY(ReplicatedUsing = OnRep_ClockState) FString ClockState;

	Scavar::WorldClock Clock;
	TWeakObjectPtr<ADirectionalLight> Sun;
	float SinceBroadcast = 0.f;
	float SinceSave = 0.f;
	bool bClockLoaded = false;
};
