#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "ScavarGameInstance.generated.h"

UCLASS()
class SCAVARGAMEPLAY_API UScavarGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;
};
