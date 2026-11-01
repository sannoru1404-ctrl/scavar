#include "ScavarGameState.h"
#include "ScavarLog.h"
#include "ScavarWorldSave.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

AScavarGameState::AScavarGameState()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void AScavarGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AScavarGameState, ClockState);
}

void AScavarGameState::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		Scavar::WorldClockConfig Config;
		Config.DayLengthMinutes = DayLengthMinutes;
		Clock = Scavar::WorldClock(Config);
		ClockState = SerializeClock();
	}
}

void AScavarGameState::EndPlay(const EEndPlayReason::Type Reason)
{
	if (HasAuthority() && bClockLoaded && GetWorld())
	{
		if (UScavarWorldSave* Save = GetWorld()->GetSubsystem<UScavarWorldSave>())
		{
			Save->SaveValue(TEXT("clock"), SerializeClock());
		}
	}
	Super::EndPlay(Reason);
}

void AScavarGameState::LoadSavedClock()
{
	// The save becomes available a moment after BeginPlay; until then the clock runs from its default start.
	const UScavarWorldSave* Save = GetWorld()->GetSubsystem<UScavarWorldSave>();
	if (!Save || !Save->IsReady())
	{
		return;
	}
	bClockLoaded = true;
	FString Saved;
	if (Save->LoadValue(TEXT("clock"), Saved))
	{
		Scavar::WorldClock Restored;
		if (Scavar::WorldClock::Restore(TCHAR_TO_UTF8(*Saved), Restored))
		{
			Clock = Restored;
			ClockState = SerializeClock();
			UE_LOG(LogScavar, Log, TEXT("World clock restored: day %lld, %.1f h."), Clock.DayIndex(), Clock.HourOfDay());
		}
	}
}

void AScavarGameState::OnRep_ClockState()
{
	Scavar::WorldClock Restored;
	if (Scavar::WorldClock::Restore(TCHAR_TO_UTF8(*ClockState), Restored))
	{
		Clock = Restored;
	}
}

void AScavarGameState::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Clock.Tick(DeltaSeconds);

	if (HasAuthority())
	{
		if (!bClockLoaded)
		{
			LoadSavedClock();
		}
		SinceBroadcast += DeltaSeconds;
		if (SinceBroadcast >= 1.f)
		{
			SinceBroadcast = 0.f;
			ClockState = SerializeClock();
		}
		SinceSave += DeltaSeconds;
		if (SinceSave >= 30.f && bClockLoaded)
		{
			SinceSave = 0.f;
			if (UScavarWorldSave* Save = GetWorld()->GetSubsystem<UScavarWorldSave>())
			{
				Save->SaveValue(TEXT("clock"), SerializeClock());
			}
		}
	}
	ApplySun();
}

void AScavarGameState::ApplySun()
{
	if (!Sun.IsValid())
	{
		for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
		{
			Sun = *It;
			if (It->ActorHasTag(TEXT("Sun"))) { break; }
		}
	}
	if (ADirectionalLight* Light = Sun.Get())
	{
		const float Elevation = static_cast<float>(Clock.SunElevationDeg());
		const float Azimuth = static_cast<float>(Clock.SunAzimuthDeg());
		// The light points from the sun toward the ground: pitch is minus the elevation.
		Light->SetActorRotation(FRotator(-Elevation, Azimuth, 0.f));
		if (UDirectionalLightComponent* Component = Light->GetComponent())
		{
			Component->SetIntensity(Elevation > 0.f ? 8.f : 0.2f);
		}
	}
}

void AScavarGameState::DebugSetHour(float Hour)
{
	if (!HasAuthority())
	{
		return;
	}
	Scavar::WorldClockConfig Config = Clock.Config();
	Config.StartHour = FMath::Fmod(FMath::Max(0.f, Hour), 24.f);
	Clock = Scavar::WorldClock(Config);
	ClockState = SerializeClock();
	bClockLoaded = true;
}
