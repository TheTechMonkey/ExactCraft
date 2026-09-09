#include "ExactCraftControlRow.h"

#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "ExactCraftConfiguration.h"
#include "ExactCraftInternal.h"
#include "FGInventoryComponent.h"
#include "FGRecipe.h"
#include "FGWorkBench.h"
#include "InputCoreTypes.h"
#include "Resources/FGItemDescriptor.h"
#include "TimerManager.h"
#include "UI/FGManufacturingButton.h"
#include "Widgets/Input/SEditableTextBox.h"

#define LOCTEXT_NAMESPACE "ExactCraft"

namespace
{
	const FLinearColor PanelColor(0.025f, 0.028f, 0.030f, 0.98f);
	const FLinearColor TrackColor(0.095f, 0.095f, 0.095f, 1.0f);
	const FLinearColor FicsitOrange(0.95f, 0.40f, 0.055f, 1.0f);

	UTextBlock* MakeLabel(UWidgetTree* Tree, const FText& Text, const int32 Size)
	{
		UTextBlock* Label = Tree->ConstructWidget<UTextBlock>();
		Label->SetText(Text);
		Label->SetColorAndOpacity(FSlateColor(FLinearColor(0.78f, 0.80f, 0.80f, 1.0f)));
		Label->SetJustification(ETextJustify::Center);
		FSlateFontInfo Font = Label->GetFont();
		Font.Size = Size;
		Label->SetFont(Font);
		return Label;
	}

	void GatherNestedWidgets(UWidgetTree* Tree, TArray<UWidget*>& OutWidgets, TSet<UWidgetTree*>& VisitedTrees)
	{
		if (!IsValid(Tree) || VisitedTrees.Contains(Tree)) return;
		VisitedTrees.Add(Tree);

		TArray<UWidget*> Widgets;
		Tree->GetAllWidgets(Widgets);
		for (UWidget* Widget : Widgets)
		{
			if (!IsValid(Widget)) continue;
			OutWidgets.Add(Widget);
			if (UUserWidget* NestedWidget = Cast<UUserWidget>(Widget))
			{
				GatherNestedWidgets(NestedWidget->WidgetTree, OutWidgets, VisitedTrees);
			}
		}
	}
}

TSharedRef<SWidget> UExactCraftQuantityBox::RebuildWidget()
{
	TSharedRef<SWidget> Widget = Super::RebuildWidget();
	if (MyEditableTextBlock.IsValid())
	{
		MyEditableTextBlock->SetOnKeyDownHandler(
			FOnKeyDown::CreateUObject(this, &UExactCraftQuantityBox::HandleKeyDown));
	}
	return Widget;
}

FReply UExactCraftQuantityBox::HandleKeyDown(
	const FGeometry&,
	const FKeyEvent& KeyEvent)
{
	if (KeyEvent.GetKey() == EKeys::SpaceBar && IsValid(ControlRow))
	{
		ControlRow->HandleQuantitySpacePressed();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void UExactCraftControlRow::InitializeFor(
	UFGWorkBench* InWorkBench,
	UFGManufacturingButton* InButton,
	UPanelWidget* InScreenOverlay)
{
	WorkBench = InWorkBench;
	ManufacturingButton = InButton;
	LastRecipe = IsValid(InWorkBench) ? InWorkBench->GetCurrentRecipe() : nullptr;
	UWidgetTree* Tree = GetTypedOuter<UWidgetTree>();
	if (!Tree || !IsValid(InButton)) return;

	SetVisibility(ESlateVisibility::Visible);
	FSlateBrush PanelBrush;
	PanelBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
	PanelBrush.TintColor = FSlateColor(PanelColor);
	PanelBrush.OutlineSettings = FSlateBrushOutlineSettings(
		1.0f,
		FSlateColor(FLinearColor(0.27f, 0.29f, 0.28f, 0.95f)),
		1.0f);
	SetBrush(PanelBrush);
	// A shallow inset seam, matching the recipe-screen frame rather than a
	// rounded overlay panel.
	SetPadding(FMargin(9.0f, 0.0f));

	UHorizontalBox* Layout = Tree->ConstructWidget<UHorizontalBox>();
	AddChild(Layout);

	DefaultLabel = MakeLabel(Tree, LOCTEXT("DefaultMode", "DEFAULT"), 9);
	UButton* InfinityButton = Tree->ConstructWidget<UButton>();
	FButtonStyle InvisibleButtonStyle;
	InvisibleButtonStyle.Normal.DrawAs = ESlateBrushDrawType::NoDrawType;
	InvisibleButtonStyle.Hovered.DrawAs = ESlateBrushDrawType::NoDrawType;
	InvisibleButtonStyle.Pressed.DrawAs = ESlateBrushDrawType::NoDrawType;
	InvisibleButtonStyle.Disabled.DrawAs = ESlateBrushDrawType::NoDrawType;
	InvisibleButtonStyle.NormalPadding = FMargin(0.0f);
	InvisibleButtonStyle.PressedPadding = FMargin(0.0f);
	InfinityButton->SetStyle(InvisibleButtonStyle);
	InfinityButton->SetToolTipText(LOCTEXT(
		"DefaultModeTooltip",
		"Vanilla continuous crafting. No target amount or Exact Craft queue."));
	InfinityButton->OnClicked.AddDynamic(this, &UExactCraftControlRow::HandleInfinityClicked);
	if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(InfinityButton->AddChild(DefaultLabel)))
	{
		ButtonSlot->SetHorizontalAlignment(HAlign_Center);
		ButtonSlot->SetVerticalAlignment(VAlign_Center);
	}
	UHorizontalBoxSlot* InfinitySlot = Layout->AddChildToHorizontalBox(InfinityButton);
	InfinitySlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	InfinitySlot->SetVerticalAlignment(VAlign_Center);
	InfinitySlot->SetPadding(FMargin(2.0f, 0.0f, 10.0f, 0.0f));

	CycleSlider = Tree->ConstructWidget<USlider>();
	CycleSlider->SetMinValue(0.0f);
	CycleSlider->SetMaxValue(1.0f);
	CycleSlider->SetValue(0.0f);
	CycleSlider->SetStepSize(1.0f);
	CycleSlider->IsFocusable = true;
	FSliderStyle SliderStyle;
	UTexture2D* WhiteTexture = LoadObject<UTexture2D>(
		nullptr, TEXT("/Game/FactoryGame/Interface/UI/Assets/Shared/01_White.01_White"));
	auto ConfigureBar = [WhiteTexture](FSlateBrush& Brush)
	{
		Brush.DrawAs = ESlateBrushDrawType::Box;
		Brush.ImageSize = FVector2D(300.0f, 9.0f);
		Brush.TintColor = FSlateColor(TrackColor);
		Brush.SetResourceObject(WhiteTexture);
	};
	auto ConfigureThumb = [WhiteTexture](FSlateBrush& Brush, const FLinearColor& Color)
	{
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.ImageSize = FVector2D(13.0f, 28.0f);
		Brush.TintColor = FSlateColor(Color);
		Brush.OutlineSettings = FSlateBrushOutlineSettings(
			2.0f, FSlateColor(FLinearColor(0.015f, 0.015f, 0.015f, 1.0f)), 1.0f);
		Brush.SetResourceObject(WhiteTexture);
	};
	ConfigureBar(SliderStyle.NormalBarImage);
	ConfigureBar(SliderStyle.HoveredBarImage);
	ConfigureBar(SliderStyle.DisabledBarImage);
	ConfigureThumb(SliderStyle.NormalThumbImage, FLinearColor(0.64f, 0.66f, 0.66f, 1.0f));
	ConfigureThumb(SliderStyle.HoveredThumbImage, FicsitOrange);
	ConfigureThumb(SliderStyle.DisabledThumbImage, FLinearColor(0.24f, 0.24f, 0.24f, 1.0f));
	SliderStyle.BarThickness = 9.0f;
	CycleSlider->SetWidgetStyle(SliderStyle);
	CycleSlider->OnValueChanged.AddDynamic(this, &UExactCraftControlRow::HandleSliderChanged);
	UHorizontalBoxSlot* SliderSlot = Layout->AddChildToHorizontalBox(CycleSlider);
	SliderSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	SliderSlot->SetVerticalAlignment(VAlign_Center);
	SliderSlot->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));

	CycleReadout = Tree->ConstructWidget<UExactCraftQuantityBox>();
	CycleReadout->SetControlRow(this);
	CycleReadout->SetText(FText::GetEmpty());
	CycleReadout->SetHintText(LOCTEXT("AmountHint", "AMOUNT"));
	CycleReadout->SetJustification(ETextJustify::Center);
	CycleReadout->SetSelectAllTextWhenFocused(true);
	// Let Enter perform a normal text-box commit and return focus to the
	// crafting screen. Keeping focus here prevents Enter from completing the
	// edit reliably because the workbench also handles keyboard crafting input.
	CycleReadout->SetClearKeyboardFocusOnCommit(true);
	CycleReadout->SetIsReadOnly(false);
	CycleReadout->SetForegroundColor(FLinearColor(1.0f, 0.60f, 0.10f, 1.0f));
	CycleReadout->WidgetStyle.Padding = FMargin(4.0f, 0.0f);
	FSlateFontInfo ReadoutFont = CycleReadout->WidgetStyle.TextStyle.Font;
	// The control opens in DEFAULT mode, where the field displays the longer
	// AMOUNT hint. RefreshReadout switches this to the numeric size as soon as
	// an exact quantity is selected.
	ReadoutFont.Size = 8;
	CycleReadout->WidgetStyle.TextStyle.SetFont(ReadoutFont);
	CycleReadout->WidgetStyle.BackgroundImageNormal.DrawAs = ESlateBrushDrawType::RoundedBox;
	CycleReadout->WidgetStyle.BackgroundImageNormal.TintColor =
		FSlateColor(FLinearColor(0.015f, 0.022f, 0.020f, 1.0f));
	CycleReadout->WidgetStyle.BackgroundImageNormal.OutlineSettings = FSlateBrushOutlineSettings(
		1.0f, FSlateColor(FLinearColor(0.42f, 0.46f, 0.43f, 1.0f)), 1.0f);
	CycleReadout->WidgetStyle.BackgroundImageHovered = CycleReadout->WidgetStyle.BackgroundImageNormal;
	CycleReadout->WidgetStyle.BackgroundImageFocused = CycleReadout->WidgetStyle.BackgroundImageNormal;
	CycleReadout->OnTextChanged.AddDynamic(this, &UExactCraftControlRow::HandleValueChanged);
	CycleReadout->OnTextCommitted.AddDynamic(this, &UExactCraftControlRow::HandleValueCommitted);
	USizeBox* ReadoutSize = Tree->ConstructWidget<USizeBox>();
	ReadoutSize->SetWidthOverride(68.0f);
	ReadoutSize->SetHeightOverride(30.0f);
	ReadoutSize->AddChild(CycleReadout);
	UHorizontalBoxSlot* ReadoutSlot = Layout->AddChildToHorizontalBox(ReadoutSize);
	ReadoutSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	ReadoutSlot->SetVerticalAlignment(VAlign_Center);
	ReadoutSlot->SetPadding(FMargin(0.0f, 0.0f, 9.0f, 0.0f));

	MaximumLabel = MakeLabel(Tree, LOCTEXT("MaximumInitial", "MAX 0"), 10);
	MaximumButton = Tree->ConstructWidget<UButton>();
	MaximumButton->SetStyle(InvisibleButtonStyle);
	MaximumButton->SetToolTipText(LOCTEXT("MaximumTooltip", "Craft the maximum currently affordable amount"));
	MaximumButton->OnClicked.AddDynamic(this, &UExactCraftControlRow::HandleMaximumClicked);
	if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(MaximumButton->AddChild(MaximumLabel)))
	{
		ButtonSlot->SetHorizontalAlignment(HAlign_Center);
		ButtonSlot->SetVerticalAlignment(VAlign_Center);
	}
	UHorizontalBoxSlot* MaximumSlot = Layout->AddChildToHorizontalBox(MaximumButton);
	MaximumSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	MaximumSlot->SetVerticalAlignment(VAlign_Center);

	// Everything placed inside the native recipe display belongs to the optional
	// ExactCraft screen. Vanilla mode gets only the bottom quantity control row.
	if (FExactCraftConfigurationStruct::ShouldUseExactCraftScreen(InWorkBench))
	{
	MissingInfoLabel = MakeLabel(Tree, LOCTEXT("MissingInfo", "i"), 9);
	MissingInfoLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	MissingInfoButton = Tree->ConstructWidget<UButton>();
	FButtonStyle InfoButtonStyle;
	auto ConfigureInfoBrush = [](FSlateBrush& Brush, const FLinearColor& Fill, const FLinearColor& Outline)
	{
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.ImageSize = FVector2D(22.0f, 22.0f);
		Brush.TintColor = FSlateColor(Fill);
		Brush.OutlineSettings = FSlateBrushOutlineSettings(
			11.0f, FSlateColor(Outline), 1.5f);
	};
	ConfigureInfoBrush(InfoButtonStyle.Normal,
		FLinearColor(0.04f, 0.045f, 0.045f, 0.98f), FicsitOrange);
	ConfigureInfoBrush(InfoButtonStyle.Hovered,
		FLinearColor(0.95f, 0.40f, 0.055f, 1.0f), FLinearColor::White);
	ConfigureInfoBrush(InfoButtonStyle.Pressed,
		FLinearColor(0.75f, 0.28f, 0.03f, 1.0f), FLinearColor::White);
	InfoButtonStyle.Disabled = InfoButtonStyle.Normal;
	InfoButtonStyle.NormalPadding = FMargin(0.0f);
	InfoButtonStyle.PressedPadding = FMargin(0.0f);
	MissingInfoButton->SetStyle(InfoButtonStyle);
	if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(MissingInfoButton->AddChild(MissingInfoLabel)))
	{
		ButtonSlot->SetHorizontalAlignment(HAlign_Center);
		ButtonSlot->SetVerticalAlignment(VAlign_Center);
	}
	MissingInfoContainer = Tree->ConstructWidget<USizeBox>();
	MissingInfoContainer->SetWidthOverride(22.0f);
	MissingInfoContainer->SetHeightOverride(22.0f);
	MissingInfoContainer->SetVisibility(ESlateVisibility::Collapsed);
	MissingInfoContainer->AddChild(MissingInfoButton);
	if (IsValid(InScreenOverlay))
	{
		MissingStatusContainer = Tree->ConstructWidget<USizeBox>();
		MissingStatusContainer->SetWidthOverride(148.0f);
		MissingStatusContainer->SetHeightOverride(24.0f);
		MissingStatusContainer->SetVisibility(ESlateVisibility::Collapsed);

		UBorder* MissingStatusBorder = Tree->ConstructWidget<UBorder>();
		FSlateBrush MissingStatusBrush;
		MissingStatusBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
		MissingStatusBrush.TintColor = FSlateColor(FLinearColor(0.95f, 0.55f, 0.20f, 1.0f));
		MissingStatusBrush.OutlineSettings = FSlateBrushOutlineSettings(
			4.0f, FSlateColor(FLinearColor(0.95f, 0.55f, 0.20f, 1.0f)), 0.0f);
		MissingStatusBorder->SetBrush(MissingStatusBrush);
		MissingStatusBorder->SetHorizontalAlignment(HAlign_Center);
		MissingStatusBorder->SetVerticalAlignment(VAlign_Center);
		MissingStatusLabel = MakeLabel(
			Tree, LOCTEXT("RequestedAmountUnaffordable", "Can't afford Recipe"), 10);
		MissingStatusLabel->SetColorAndOpacity(
			FSlateColor(FLinearColor(0.12f, 0.14f, 0.14f, 1.0f)));
		MissingStatusBorder->AddChild(MissingStatusLabel);
		MissingStatusContainer->AddChild(MissingStatusBorder);
		InScreenOverlay->AddChild(MissingStatusContainer);
		if (UOverlaySlot* StatusSlot = Cast<UOverlaySlot>(MissingStatusContainer->Slot))
		{
			StatusSlot->SetHorizontalAlignment(HAlign_Center);
			StatusSlot->SetVerticalAlignment(VAlign_Bottom);
			StatusSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 41.0f));
		}

		InScreenOverlay->AddChild(MissingInfoContainer);
		if (UOverlaySlot* InfoSlot = Cast<UOverlaySlot>(MissingInfoContainer->Slot))
		{
			InfoSlot->SetHorizontalAlignment(HAlign_Center);
			InfoSlot->SetVerticalAlignment(VAlign_Bottom);
			InfoSlot->SetPadding(FMargin(90.0f, 0.0f, 0.0f, 42.0f));
		}
	}
	}
	RefreshMaximum();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			RefreshTimer, this, &UExactCraftControlRow::RefreshMaximum, 0.25f, true);
	}
}

void UExactCraftControlRow::AttachNativeManufacturingScreen(
	UWidget* NativeCraftAmount,
	UUserWidget* NativeScreen)
{
	if (!IsValid(NativeScreen)) return;
	NativeManufacturingScreen = NativeScreen;
	if (IsValid(NativeCraftAmount))
	{
		NativeCraftAmountContainer = NativeCraftAmount;
	}
	if (IsValid(NativeScreen->WidgetTree))
	{
		NativeWarningContainer = NativeScreen->WidgetTree->FindWidget(TEXT("mWarning"));
		if (IsValid(NativeWarningContainer))
		{
			NativeWarningContainer->RemoveFromParent();
			NativeWarningContainer->SetVisibility(ESlateVisibility::Collapsed);
		}
		else
		{
			UE_LOG(LogExactCraft, Warning, TEXT("Native mWarning pill was not found"));
		}

		if (UOverlay* ProgressOverlay = Cast<UOverlay>(
			NativeScreen->WidgetTree->FindWidget(TEXT("mProgress"))))
		{
			QueueStatusLabel = MakeLabel(NativeScreen->WidgetTree, FText::GetEmpty(), 10);
			QueueStatusLabel->SetVisibility(ESlateVisibility::Collapsed);
			UOverlaySlot* QueueStatusSlot = ProgressOverlay->AddChildToOverlay(QueueStatusLabel);
			QueueStatusSlot->SetHorizontalAlignment(HAlign_Center);
			QueueStatusSlot->SetVerticalAlignment(VAlign_Center);
			QueueStatusSlot->SetPadding(FMargin(8.0f, 0.0f));
		}
		else
		{
			UE_LOG(LogExactCraft, Warning, TEXT("Native mProgress overlay was not found"));
		}
	}
	ScheduleNativeIngredientPillPositions();
	RefreshNativeCraftAmountVisibility();
}

void UExactCraftControlRow::BeginDestroy()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimer);
	}
	Super::BeginDestroy();
}

void UExactCraftControlRow::HandleRecipeChanged(
	const TSubclassOf<UFGRecipe> NewRecipe,
	const bool bPreserveQuantity)
{
	LastRecipe = NewRecipe;
	MaximumOutput = -1;
	if (!bPreserveQuantity)
	{
		RequestedOutput = 0;
		SliderMaximum = 100;
	}
	else if (bRequestActive)
	{
		// An internal dependency recipe change must not rescale or rewrite the
		// quantity selected for the root recipe.
		return;
	}
	RefreshMaximum();
	ScheduleNativeIngredientPillPositions();
}

void UExactCraftControlRow::HandleQuantitySpacePressed()
{
	if (!IsValid(CycleReadout)) return;

	const FString Value = CycleReadout->GetText().ToString().TrimStartAndEnd();
	if (!Value.IsEmpty() && Value != TEXT("\u221e") && !Value.Equals(TEXT("inf"), ESearchCase::IgnoreCase))
	{
		ApplyRequestedOutput(FCString::Atoi(*Value));
	}
	if (RequestedOutput <= 0 || !IsValid(ManufacturingButton)) return;

	ExactCraft::Begin(WorkBench, RequestedOutput);
}

void UExactCraftControlRow::SetRequestActive(const bool bActive)
{
	bRequestActive = bActive;
	RefreshNativeCraftAmountVisibility();
	if (bRequestActive)
	{
		EnsureCraftInputEnabled();
	}
	if (!bRequestActive)
	{
		if (IsValid(QueueStatusLabel))
		{
			QueueStatusLabel->SetVisibility(ESlateVisibility::Collapsed);
			QueueStatusLabel->SetText(FText::GetEmpty());
		}
		DisplayedStepRecipe = nullptr;
		DisplayedStepNumber = 0;
		DisplayedStepCount = 0;
		DisplayedCompletedCycles = 0;
		DisplayedTotalCycles = 0;
		MaximumOutput = -1;
		RefreshMaximum();
	}
}

void UExactCraftControlRow::SetCraftStep(
	const TSubclassOf<UFGRecipe> Recipe,
	const int32 StepNumber,
	const int32 StepCount,
	const int32 CompletedCycles,
	const int32 TotalCycles)
{
	DisplayedStepRecipe = Recipe;
	DisplayedStepNumber = StepNumber;
	DisplayedStepCount = StepCount;
	DisplayedCompletedCycles = CompletedCycles;
	DisplayedTotalCycles = TotalCycles;
	RefreshProductProgress();
	RefreshNativeCraftAmountVisibility();
}

void UExactCraftControlRow::RefreshProductProgress()
{
	if (!bRequestActive || !DisplayedStepRecipe || !IsValid(QueueStatusLabel))
	{
		return;
	}

	const TArray<FItemAmount> Products = UFGRecipe::GetProducts(DisplayedStepRecipe);
	const int32 OutputPerCycle = Products.IsEmpty() ? 1 : FMath::Max(1, Products[0].Amount);
	const FText ItemName = Products.IsEmpty() || !Products[0].ItemClass
		? UFGRecipe::GetRecipeName(DisplayedStepRecipe)
		: UFGItemDescriptor::GetItemName(Products[0].ItemClass);
	const int64 CompletedItems = static_cast<int64>(DisplayedCompletedCycles) * OutputPerCycle;
	const int64 TotalItems = static_cast<int64>(DisplayedTotalCycles) * OutputPerCycle;

	if (DisplayedStepNumber >= DisplayedStepCount)
	{
		QueueStatusLabel->SetText(FText::Format(
			LOCTEXT("FinalCraftProgressFormat", "CRAFTING {0}  |  {1} / {2}  |  FINAL STEP"),
			ItemName,
			FText::AsNumber(CompletedItems),
			FText::AsNumber(TotalItems)));
	}
	else
	{
		QueueStatusLabel->SetText(FText::Format(
			LOCTEXT("QueueCraftProgressFormat", "CRAFTING {0}  |  {1} / {2}  |  STEP {3} OF {4}"),
			ItemName,
			FText::AsNumber(CompletedItems),
			FText::AsNumber(TotalItems),
			FText::AsNumber(DisplayedStepNumber),
			FText::AsNumber(DisplayedStepCount)));
	}
	QueueStatusLabel->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UExactCraftControlRow::HandleSliderChanged(const float Value)
{
	if (bUpdatingControls) return;
	// DEFAULT is a separate mode, not a numeric point on the amount slider.
	// Any deliberate slider interaction selects at least one recipe batch.
	ApplyRequestedOutput(FMath::Max(GetOutputPerCycle(), FMath::RoundToInt(Value)));
}

void UExactCraftControlRow::HandleInfinityClicked()
{
	ExactCraft::Reset(WorkBench, TEXT("continuous crafting selected"));
	ApplyRequestedOutput(0);
}

void UExactCraftControlRow::HandleMaximumClicked()
{
	ApplyRequestedOutput(MaximumOutput);
}

void UExactCraftControlRow::HandleValueCommitted(const FText& Text, const ETextCommit::Type CommitMethod)
{
	const FString Value = Text.ToString().TrimStartAndEnd();
	if (Value.IsEmpty() || Value == TEXT("\u221e") || Value.Equals(TEXT("inf"), ESearchCase::IgnoreCase))
	{
		ApplyRequestedOutput(0);
	}
	else
	{
		ApplyRequestedOutput(FCString::Atoi(*Value));
	}
	// ClearKeyboardFocusOnCommit handles the text field itself. On Enter, return
	// focus to the vanilla manufacturing control on the next tick so Space works
	// immediately without making the readout read-only or requiring another click.
	if (CommitMethod == ETextCommit::OnEnter && GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]
		{
			if (IsValid(ManufacturingButton))
			{
				ManufacturingButton->SetKeyboardFocus();
			}
		}));
	}
}

void UExactCraftControlRow::HandleValueChanged(const FText& Text)
{
	if (bUpdatingControls) return;

	const FString Value = Text.ToString().TrimStartAndEnd();
	if (Value.IsEmpty() || Value == TEXT("\u221e") || Value.Equals(TEXT("inf"), ESearchCase::IgnoreCase))
	{
		RequestedOutput = 0;
	}
	else
	{
		RequestedOutput = NormalizeOutput(FCString::Atoi(*Value));
	}

	SliderMaximum = FMath::Max3(100, MaximumOutput, RequestedOutput);
	bUpdatingControls = true;
	CycleSlider->SetMaxValue(static_cast<float>(SliderMaximum));
	CycleSlider->SetStepSize(
		static_cast<float>(GetOutputPerCycle()) / static_cast<float>(SliderMaximum));
	CycleSlider->SetValue(static_cast<float>(RequestedOutput));
	bUpdatingControls = false;
	RefreshRequestedOutputAffordability(true);
	EnsureCraftInputEnabled();
}

void UExactCraftControlRow::RefreshMaximum()
{
	// The vanilla Blueprint may restore the hammer row after recipe updates.
	if (IsValid(NativeCraftAmountContainer))
	{
		NativeCraftAmountContainer->SetVisibility(ESlateVisibility::Collapsed);
	}
	RefreshRequestedOutputAffordability();
	RefreshNativeCraftAmountVisibility();
	EnsureCraftInputEnabled();
	if (bRequestActive)
	{
		// Vanilla refreshes this readout using the selected root recipe whenever an
		// item is added. Reassert the dependency item so the display stays truthful.
		RefreshProductProgress();
		return;
	}

	const TSubclassOf<UFGRecipe> CurrentRecipe = IsValid(WorkBench)
		? WorkBench->GetCurrentRecipe()
		: nullptr;
	if (CurrentRecipe != LastRecipe)
	{
		// SetRecipe's native hook owns recipe-change cancellation and quantity
		// reset. This timer is only a display refresh; independently cancelling
		// here races with Exact Craft's internal dependency recipe switches.
		LastRecipe = CurrentRecipe;
		MaximumOutput = -1;
	}

	const int32 NewMaximum = ExactCraft::GetMaximumCraftableOutput(WorkBench);
	if (NewMaximum == MaximumOutput)
	{
		RefreshMaximumLabel();
		return;
	}
	MaximumOutput = NewMaximum;
	SliderMaximum = FMath::Max3(100, MaximumOutput, RequestedOutput);
	bUpdatingControls = true;
	CycleSlider->SetMaxValue(static_cast<float>(SliderMaximum));
	CycleSlider->SetStepSize(
		static_cast<float>(GetOutputPerCycle()) / static_cast<float>(SliderMaximum));
	// The affordable maximum naturally falls while crafting. Do not rewrite the
	// user's selection as ingredients are consumed; doing so made the slider and
	// typed quantity appear to drift or refuse to stay committed.
	CycleSlider->SetValue(static_cast<float>(RequestedOutput));
	bUpdatingControls = false;
	RefreshMaximumLabel();
	RefreshReadout();
}

int32 UExactCraftControlRow::GetOutputPerCycle() const
{
	if (!IsValid(WorkBench) || !WorkBench->GetCurrentRecipe()) return 1;
	const TArray<FItemAmount> Products = UFGRecipe::GetProducts(WorkBench->GetCurrentRecipe());
	return Products.IsEmpty() ? 1 : FMath::Max(1, Products[0].Amount);
}

int32 UExactCraftControlRow::NormalizeOutput(const int32 Output) const
{
	if (Output <= 0) return 0;
	const int32 OutputPerCycle = GetOutputPerCycle();
	return FMath::Clamp(
		FMath::DivideAndRoundUp(Output, OutputPerCycle) * OutputPerCycle,
		OutputPerCycle,
		999999);
}

void UExactCraftControlRow::ApplyRequestedOutput(const int32 Output)
{
	RequestedOutput = NormalizeOutput(Output);
	SliderMaximum = FMath::Max3(100, MaximumOutput, RequestedOutput);
	bUpdatingControls = true;
	CycleSlider->SetMaxValue(static_cast<float>(SliderMaximum));
	CycleSlider->SetStepSize(
		static_cast<float>(GetOutputPerCycle()) / static_cast<float>(SliderMaximum));
	CycleSlider->SetValue(static_cast<float>(RequestedOutput));
	bUpdatingControls = false;
	RefreshRequestedOutputAffordability(true);
	EnsureCraftInputEnabled();
	RefreshReadout();
}

void UExactCraftControlRow::RefreshRequestedOutputAffordability(const bool bForce)
{
	if (bRequestActive) return;
	if (!IsValid(WorkBench) || RequestedOutput <= 0)
	{
		bRequestedOutputAffordable = false;
		AffordabilityRequestedOutput = RequestedOutput;
		AffordabilityRecipe = IsValid(WorkBench) ? WorkBench->GetCurrentRecipe() : nullptr;
		AffordabilityResourcesHash = 0;
		MissingMaterialsLabel.Reset();
		MissingMaterialsDetails.Reset();
		RefreshNativeCraftAmountVisibility();
		return;
	}

	const TSubclassOf<UFGRecipe> Recipe = WorkBench->GetCurrentRecipe();
	const uint32 ResourcesHash = ExactCraft::GetCraftingResourcesHash(WorkBench);
	if (!bForce && AffordabilityRequestedOutput == RequestedOutput &&
		AffordabilityRecipe == Recipe && AffordabilityResourcesHash == ResourcesHash)
	{
		return;
	}

	AffordabilityRequestedOutput = RequestedOutput;
	AffordabilityRecipe = Recipe;
	AffordabilityResourcesHash = ResourcesHash;
	TArray<ExactCraft::FMissingMaterial> MissingMaterials;
	bRequestedOutputAffordable = ExactCraft::CanCompleteRequestedOutput(
		WorkBench, RequestedOutput, &MissingMaterials);
	MissingMaterialsLabel.Reset();
	MissingMaterialsDetails.Reset();
	if (!bRequestedOutputAffordable && !MissingMaterials.IsEmpty())
	{
		const ExactCraft::FMissingMaterial& First = MissingMaterials[0];
		const FString FirstName = UFGItemDescriptor::GetItemName(First.Item).ToString();
		if (MissingMaterials.Num() == 1)
		{
			MissingMaterialsLabel = FString::Printf(TEXT("NEED %lld %s"), First.Amount, *FirstName);
		}
		else
		{
			MissingMaterialsLabel = TEXT("MISSING INGREDIENTS");
		}

		TArray<FString> Lines;
		Lines.Reserve(MissingMaterials.Num());
		for (const ExactCraft::FMissingMaterial& Missing : MissingMaterials)
		{
			Lines.Add(FString::Printf(
				TEXT("%lld %s"),
				Missing.Amount,
				*UFGItemDescriptor::GetItemName(Missing.Item).ToString()));
		}
		MissingMaterialsDetails = FString::Printf(
			TEXT("Missing raw materials for the selected amount:\n%s"),
			*FString::Join(Lines, TEXT("\n")));
	}
	RefreshMaximumLabel();
	RefreshNativeCraftAmountVisibility();
}

void UExactCraftControlRow::RefreshNativeCraftAmountVisibility()
{
	RefreshNativeIngredientPillPositions();
	if (IsValid(NativeCraftAmountContainer))
	{
		NativeCraftAmountContainer->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (IsValid(NativeWarningContainer))
	{
		NativeWarningContainer->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (!IsValid(MissingInfoButton) || !IsValid(MissingInfoContainer)) return;
	const bool bHasRecipe = IsValid(WorkBench) && WorkBench->GetCurrentRecipe() != nullptr;
	const bool bDefaultMissing = RequestedOutput <= 0 && !bRequestActive && bHasRecipe &&
		!HasVanillaIngredientsForOneCycle();
	const bool bExactMissing = RequestedOutput > 0 && !bRequestActive &&
		!bRequestedOutputAffordable && !MissingMaterialsDetails.IsEmpty();
	const bool bShowStatus = bDefaultMissing || bExactMissing;
	if (IsValid(MissingStatusContainer))
	{
		MissingStatusContainer->SetVisibility(
			bShowStatus ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (IsValid(MissingStatusLabel))
	{
		MissingStatusLabel->SetText(bDefaultMissing
			? LOCTEXT("DefaultCannotAfford", "Can't afford Recipe")
			: LOCTEXT("ExactMissingIngredients", "Missing Ingredients"));
	}
	MissingInfoContainer->SetVisibility(
		bExactMissing ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	MissingInfoButton->SetToolTipText(
		bExactMissing ? FText::FromString(MissingMaterialsDetails) : FText::GetEmpty());
}

void UExactCraftControlRow::RefreshNativeIngredientPillPositions()
{
	if (!IsValid(NativeManufacturingScreen) || !IsValid(NativeManufacturingScreen->WidgetTree)) return;
	TArray<UWidget*> Widgets;
	TSet<UWidgetTree*> VisitedTrees;
	GatherNestedWidgets(NativeManufacturingScreen->WidgetTree, Widgets, VisitedTrees);
	for (UWidget* Widget : Widgets)
	{
		UTextBlock* AmountText = Cast<UTextBlock>(Widget);
		if (!IsValid(AmountText) || AmountText->GetFName() != TEXT("mStackSizeLbl")) continue;
		UUserWidget* OwnerWidget = AmountText->GetTypedOuter<UUserWidget>();
		if (!IsValid(OwnerWidget) || !OwnerWidget->GetClass()->GetName().StartsWith(TEXT("Widget_CostSlotWrapper")))
		{
			continue;
		}

		UWidget* PillOverlay = AmountText->GetParent();
		for (int32 Depth = 0; IsValid(PillOverlay) && Depth < 4; ++Depth)
		{
			if (PillOverlay->GetFName() == TEXT("StackSizeOverlay")) break;
			PillOverlay = PillOverlay->GetParent();
		}
		if (IsValid(PillOverlay) && PillOverlay->GetFName() == TEXT("StackSizeOverlay"))
		{
			const FVector2D TileSize = OwnerWidget->GetCachedGeometry().GetLocalSize();
			if (TileSize.X > 0.0f && TileSize.Y > 0.0f)
			{
				const bool bLargeOutputTile = FMath::Max(TileSize.X, TileSize.Y) > 80.0f;
				PillOverlay->SetRenderTranslation(
					bLargeOutputTile ? FVector2D::ZeroVector : FVector2D(0.0f, 22.0f));
			}
		}
	}
}

void UExactCraftControlRow::ScheduleNativeIngredientPillPositions(const int32 RemainingFrames)
{
	RefreshNativeIngredientPillPositions();
	if (RemainingFrames <= 0) return;
	if (UWorld* World = GetWorld())
	{
		const TWeakObjectPtr<UExactCraftControlRow> WeakThis(this);
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda(
			[WeakThis, RemainingFrames]
			{
				if (UExactCraftControlRow* ControlRow = WeakThis.Get())
				{
					ControlRow->ScheduleNativeIngredientPillPositions(RemainingFrames - 1);
				}
			}));
	}
}

bool UExactCraftControlRow::HasVanillaIngredientsForOneCycle() const
{
	if (!IsValid(WorkBench) || !WorkBench->GetCurrentRecipe()) return false;
	for (const FItemAmount& Ingredient : UFGRecipe::GetIngredients(
		WorkBench, WorkBench->GetCurrentRecipe()))
	{
		if (Ingredient.ItemClass && Ingredient.Amount > 0 &&
			ExactCraft::GetAvailableItemCount(WorkBench, Ingredient.ItemClass) < Ingredient.Amount)
		{
			return false;
		}
	}
	return true;
}

void UExactCraftControlRow::RefreshMaximumLabel()
{
	if (!IsValid(MaximumLabel) || !IsValid(MaximumButton)) return;
	MaximumLabel->SetText(FText::Format(LOCTEXT("MaximumFormat", "MAX {0}"), MaximumOutput));
	MaximumLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.78f, 0.80f, 0.80f, 1.0f)));
	MaximumButton->SetToolTipText(LOCTEXT(
		"MaximumTooltip",
		"Set the largest exact amount currently craftable."));
}

void UExactCraftControlRow::EnsureCraftInputEnabled()
{
	if (RequestedOutput <= 0 || !IsValid(ManufacturingButton)) return;

	// A numbered request may begin from upstream raw materials even when vanilla
	// considers the final recipe unaffordable. Keep the button clickable so an
	// initial mouse hold can reach Exact Craft; its press hook owns that request.
	ManufacturingButton->SetIsEnabled(true);
	if (IsValid(ManufacturingButton->mInternalButton))
	{
		ManufacturingButton->mInternalButton->SetIsEnabled(true);
	}
}

void UExactCraftControlRow::RefreshCraftInputEnabled()
{
	EnsureCraftInputEnabled();
}

bool UExactCraftControlRow::IsCraftButtonUnderLocation(const FVector2D& ScreenPosition) const
{
	return IsValid(ManufacturingButton) &&
		IsValid(ManufacturingButton->mInternalButton) &&
		ManufacturingButton->mInternalButton->IsVisible() &&
		ManufacturingButton->mInternalButton->GetCachedGeometry().IsUnderLocation(ScreenPosition);
}

void UExactCraftControlRow::RefreshReadout()
{
	const bool bWasUpdating = bUpdatingControls;
	bUpdatingControls = true;
	FSlateFontInfo ReadoutFont = CycleReadout->WidgetStyle.TextStyle.Font;
	ReadoutFont.Size = RequestedOutput <= 0 ? 8 : 16;
	CycleReadout->WidgetStyle.TextStyle.SetFont(ReadoutFont);
	CycleReadout->SynchronizeProperties();
	CycleReadout->SetText(RequestedOutput <= 0 ? FText::GetEmpty() : FText::AsNumber(RequestedOutput));
	if (IsValid(DefaultLabel))
	{
		DefaultLabel->SetColorAndOpacity(FSlateColor(
			RequestedOutput <= 0
				? FLinearColor(1.0f, 0.60f, 0.10f, 1.0f)
				: FLinearColor(0.78f, 0.80f, 0.80f, 1.0f)));
	}
	bUpdatingControls = bWasUpdating;
}

#undef LOCTEXT_NAMESPACE
