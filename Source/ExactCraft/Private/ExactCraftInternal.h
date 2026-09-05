#pragma once

#include "CoreMinimal.h"

class UFGManufacturingButton;
class UFGItemDescriptor;
class UFGRecipe;
class UFGWorkBench;
class AFGCharacterPlayer;

namespace ExactCraft
{
	struct FMissingMaterial
	{
		TSubclassOf<UFGItemDescriptor> Item;
		int64 Amount = 0;
	};

    void Begin(UFGWorkBench* WorkBench, int32 RequestedOutput);
    bool CanCompleteRequestedOutput(
		UFGWorkBench* WorkBench,
		int32 RequestedOutput,
		TArray<FMissingMaterial>* OutMissingMaterials = nullptr);
    int32 GetMaximumCraftableOutput(UFGWorkBench* WorkBench);
    bool AllowCraftCompletion(UFGWorkBench* WorkBench);
    void HandleCraftCompleted(UFGWorkBench* WorkBench);
    bool HasReachedCraftLimit(const UFGWorkBench* WorkBench);
    float GetCraftingSpeedMultiplier(UFGWorkBench* WorkBench);
    void RegisterManufacturingButton(UFGWorkBench* WorkBench, UFGManufacturingButton* Button);
    bool ManufacturingButtonPressed(UFGManufacturingButton* Button);
    bool ManufacturingButtonReleased(UFGManufacturingButton* Button);
    bool ShouldForceCanProduce(UFGWorkBench* WorkBench, TSubclassOf<UFGRecipe> Recipe);
	bool ShouldForceRecipeAffordable(AFGCharacterPlayer* Player, TSubclassOf<UFGRecipe> Recipe);
    bool StartPendingRequestFromSpace();
    void Reset(UFGWorkBench* WorkBench, const TCHAR* Reason = TEXT("reset"));
    void RecipeChanged(UFGWorkBench* WorkBench, TSubclassOf<UFGRecipe> Recipe);
}
