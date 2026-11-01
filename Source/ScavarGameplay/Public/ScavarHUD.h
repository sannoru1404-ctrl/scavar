#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "ScavarHUD.generated.h"

/** Functional pilot HUD drawn with the canvas: vitals, inventory list (Tab), pickup prompt. Real UMG widgets replace it after the pilot. */
UCLASS()
class SCAVARGAMEPLAY_API AScavarHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void DrawLine(const FString& Text, const FLinearColor& Color);
	float Cursor = 0.f;
};
