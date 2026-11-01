#include "ScavarHUD.h"
#include "ScavarCharacter.h"
#include "ScavarInventoryComponent.h"
#include "ScavarPickup.h"
#include "ScavarPlayerController.h"
#include "ScavarVitalsComponent.h"
#include "Engine/Canvas.h"

void AScavarHUD::DrawLine(const FString& Text, const FLinearColor& Color)
{
	DrawText(Text, Color, 24.f, Cursor, nullptr, 1.1f);
	Cursor += 20.f;
}

void AScavarHUD::DrawHUD()
{
	Super::DrawHUD();
	const AScavarPlayerController* PC = Cast<AScavarPlayerController>(GetOwningPlayerController());
	const AScavarCharacter* Character = PC ? Cast<AScavarCharacter>(PC->GetPawn()) : nullptr;
	if (!Character || !Canvas)
	{
		return;
	}

	Cursor = 24.f;
	const UScavarVitalsComponent* Vitals = Character->GetVitals();
	const FScavarVitalsSnapshot& V = Vitals->GetSnapshot();
	if (Vitals->IsDead())
	{
		DrawLine(TEXT("YOU DIED - respawning..."), FLinearColor::Red);
		return;
	}

	static const TCHAR* ZoneNames[6] = {TEXT("Head"), TEXT("Torso"), TEXT("L.Arm"), TEXT("R.Arm"), TEXT("L.Leg"), TEXT("R.Leg")};
	for (int32 Index = 0; Index < V.ZoneHp.Num() && Index < 6; ++Index)
	{
		FString Flags;
		if (V.Bleed.IsValidIndex(Index) && V.Bleed[Index] == 1) { Flags += TEXT(" [bleeding]"); }
		if (V.Bleed.IsValidIndex(Index) && V.Bleed[Index] == 2) { Flags += TEXT(" [HEAVY BLEED]"); }
		if (V.Fracture.IsValidIndex(Index) && V.Fracture[Index]) { Flags += TEXT(" [fracture]"); }
		const FLinearColor Color = V.ZoneHp[Index] < 20.f ? FLinearColor::Red : FLinearColor::White;
		DrawLine(FString::Printf(TEXT("%-6s %5.0f%s"), ZoneNames[Index], V.ZoneHp[Index], *Flags), Color);
	}
	DrawLine(FString::Printf(TEXT("Food %3.0f  Water %3.0f  Stamina %3.0f  Warmth %3.0f"), V.Food, V.Water, V.Stamina, V.Warmth), FLinearColor::Yellow);

	const UScavarInventoryComponent* Inventory = Character->GetInventory();
	if (Inventory)
	{
		DrawLine(FString::Printf(TEXT("Carried %.1f kg   [Tab] inventory  [1-9] use  [Alt+1-9] drop  [E] pick up"), Inventory->GetCarriedKg()), FLinearColor::Gray);
		if (PC->IsInventoryOpen())
		{
			int32 Number = 1;
			for (const FScavarItemView& Item : Inventory->GetItems())
			{
				DrawLine(FString::Printf(TEXT("%d. %s x%d  (container %lld @ %d,%d)"), Number++, *Item.DefId.ToString(), Item.Qty, Item.ContainerId, Item.X, Item.Y), FLinearColor::Green);
			}
		}
	}

	if (const AScavarPickup* Looked = PC->FindLookedAtPickup())
	{
		const FString Prompt = FString::Printf(TEXT("[E] %s x%d"), *Looked->GetDefId().ToString(), Looked->GetQty());
		DrawText(Prompt, FLinearColor::White, Canvas->SizeX * 0.5f - 60.f, Canvas->SizeY * 0.55f, nullptr, 1.4f);
	}
}
