#pragma once

#include "CoreMinimal.h"

class UFGManufacturingButton;
class UFGWorkBench;

namespace ExactCraft
{
    void Begin(UFGWorkBench* WorkBench, int32 RequestedCycles);
    int32 GetMaximumCraftableCycles(UFGWorkBench* WorkBench);
    bool AllowCraftCompletion(UFGWorkBench* WorkBench);
    float GetCraftingSpeedMultiplier(UFGWorkBench* WorkBench);
    void RegisterManufacturingButton(UFGWorkBench* WorkBench, UFGManufacturingButton* Button);
    void Reset(UFGWorkBench* WorkBench);
}
