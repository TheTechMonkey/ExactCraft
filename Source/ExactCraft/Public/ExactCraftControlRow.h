#pragma once

#include "CoreMinimal.h"
#include "Components/Border.h"
#include "ExactCraftControlRow.generated.h"

class UEditableTextBox;
class UFGManufacturingButton;
class UFGRecipe;
class UFGWorkBench;
class USlider;
class UTextBlock;

UCLASS()
class EXACTCRAFT_API UExactCraftControlRow final : public UBorder
{
	GENERATED_BODY()

public:
	void InitializeFor(UFGWorkBench* InWorkBench, UFGManufacturingButton* InButton);
	void HandleRecipeChanged(TSubclassOf<UFGRecipe> NewRecipe);
	virtual void BeginDestroy() override;

private:
	UFUNCTION()
	void HandleManufacturePressed(float ProduceMultiplier);

	UFUNCTION()
	void HandleSliderChanged(float Value);

	UFUNCTION()
	void HandleInfinityClicked();

	UFUNCTION()
	void HandleMaximumClicked();

	UFUNCTION()
	void HandleValueCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void RefreshMaximum();

	void ApplyRequestedCycles(int32 Cycles);
	void RefreshReadout();
	int32 GetRequestedCycles() const;

	UPROPERTY()
	TObjectPtr<UFGWorkBench> WorkBench;

	UPROPERTY()
	TObjectPtr<UFGManufacturingButton> ManufacturingButton;

	UPROPERTY()
	TObjectPtr<USlider> CycleSlider;

	UPROPERTY()
	TObjectPtr<UEditableTextBox> CycleReadout;

	UPROPERTY()
	TObjectPtr<UTextBlock> MaximumLabel;

	FTimerHandle RefreshTimer;
	TSubclassOf<UFGRecipe> LastRecipe;
	int32 MaximumCycles = 0;
	int32 RequestedCycles = 0;
	bool bUpdatingControls = false;
};
