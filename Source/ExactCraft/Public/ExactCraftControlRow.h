#pragma once

#include "CoreMinimal.h"
#include "Components/Border.h"
#include "Components/EditableTextBox.h"
#include "ExactCraftControlRow.generated.h"

class UFGManufacturingButton;
class UFGRecipe;
class UFGWorkBench;
class UButton;
class UPanelWidget;
class USizeBox;
class USlider;
class UTextBlock;
class UWidget;
class UUserWidget;

UCLASS()
class EXACTCRAFT_API UExactCraftQuantityBox final : public UEditableTextBox
{
	GENERATED_BODY()

public:
	void SetControlRow(class UExactCraftControlRow* InControlRow) { ControlRow = InControlRow; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	FReply HandleKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent);

	UPROPERTY()
	TObjectPtr<class UExactCraftControlRow> ControlRow;
};

UCLASS()
class EXACTCRAFT_API UExactCraftControlRow final : public UBorder
{
	GENERATED_BODY()

public:
	void InitializeFor(
		UFGWorkBench* InWorkBench,
		UFGManufacturingButton* InButton,
		UPanelWidget* InScreenOverlay);
	void AttachNativeManufacturingScreen(UWidget* NativeCraftAmount, UUserWidget* NativeScreen);
	void HandleRecipeChanged(TSubclassOf<UFGRecipe> NewRecipe, bool bPreserveQuantity = false);
	void HandleQuantitySpacePressed();
	void SetRequestActive(bool bActive);
	void SetCraftStep(
		TSubclassOf<UFGRecipe> Recipe,
		int32 StepNumber,
		int32 StepCount,
		int32 CompletedCycles,
		int32 TotalCycles);
	void RefreshCraftInputEnabled();
	bool IsCraftButtonUnderLocation(const FVector2D& ScreenPosition) const;
	int32 GetRequestedOutput() const { return RequestedOutput; }
	bool CanAffordRequestedOutput() const { return bRequestedOutputAffordable; }
	virtual void BeginDestroy() override;

private:
	UFUNCTION()
	void HandleSliderChanged(float Value);

	UFUNCTION()
	void HandleInfinityClicked();

	UFUNCTION()
	void HandleMaximumClicked();

	UFUNCTION()
	void HandleValueCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void HandleValueChanged(const FText& Text);

	UFUNCTION()
	void RefreshMaximum();

	void ApplyRequestedOutput(int32 Output);
	void EnsureCraftInputEnabled();
	void RefreshRequestedOutputAffordability(bool bForce = false);
	void RefreshMaximumLabel();
	void RefreshNativeCraftAmountVisibility();
	void RefreshNativeIngredientPillPositions();
	void ScheduleNativeIngredientPillPositions(int32 RemainingFrames = 2);
	bool HasVanillaIngredientsForOneCycle() const;
	void RefreshProductProgress();
	void RefreshReadout();
	int32 GetOutputPerCycle() const;
	int32 NormalizeOutput(int32 Output) const;

	UPROPERTY()
	TObjectPtr<UFGWorkBench> WorkBench;

	UPROPERTY()
	TObjectPtr<UFGManufacturingButton> ManufacturingButton;

	UPROPERTY()
	TObjectPtr<USlider> CycleSlider;

	UPROPERTY()
	TObjectPtr<UTextBlock> DefaultLabel;

	UPROPERTY()
	TObjectPtr<UExactCraftQuantityBox> CycleReadout;

	UPROPERTY()
	TObjectPtr<UTextBlock> MaximumLabel;

	UPROPERTY()
	TObjectPtr<UButton> MaximumButton;

	UPROPERTY()
	TObjectPtr<UWidget> NativeCraftAmountContainer;

	UPROPERTY()
	TObjectPtr<UWidget> NativeWarningContainer;

	UPROPERTY()
	TObjectPtr<UUserWidget> NativeManufacturingScreen;

	UPROPERTY()
	TObjectPtr<UButton> MissingInfoButton;

	UPROPERTY()
	TObjectPtr<UTextBlock> MissingInfoLabel;

	UPROPERTY()
	TObjectPtr<USizeBox> MissingInfoContainer;

	UPROPERTY()
	TObjectPtr<USizeBox> MissingStatusContainer;

	UPROPERTY()
	TObjectPtr<UTextBlock> MissingStatusLabel;

	UPROPERTY()
	TObjectPtr<UTextBlock> QueueStatusLabel;

	FTimerHandle RefreshTimer;
	TSubclassOf<UFGRecipe> LastRecipe;
	int32 MaximumOutput = 0;
	int32 SliderMaximum = 100;
	int32 RequestedOutput = 0;
	int32 AffordabilityRequestedOutput = INDEX_NONE;
	uint32 AffordabilityResourcesHash = 0;
	TSubclassOf<UFGRecipe> AffordabilityRecipe;
	TSubclassOf<UFGRecipe> DisplayedStepRecipe;
	int32 DisplayedStepNumber = 0;
	int32 DisplayedStepCount = 0;
	int32 DisplayedCompletedCycles = 0;
	int32 DisplayedTotalCycles = 0;
	bool bUpdatingControls = false;
	bool bRequestActive = false;
	bool bRequestedOutputAffordable = false;
	FString MissingMaterialsLabel;
	FString MissingMaterialsDetails;
};
