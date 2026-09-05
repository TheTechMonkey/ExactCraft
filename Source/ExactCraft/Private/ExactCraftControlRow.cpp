#include "ExactCraftControlRow.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/OverlaySlot.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
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
	const FLinearColor VanillaAffordOrange(0.791f, 0.315f, 0.074f, 1.0f);
	const FLinearColor VanillaAffordText(0.042f, 0.034f, 0.028f, 1.0f);

	uint32 GetInventoryHash(const UFGInventoryComponent* Inventory)
	{
		if (!IsValid(Inventory)) return 0;
		uint32 Hash = GetTypeHash(Inventory->GetSizeLinear());
		for (int32 Index = 0; Index < Inventory->GetSizeLinear(); ++Index)
		{
			FInventoryStack Stack;
			Inventory->GetStackFromIndex(Index, Stack);
			Hash = HashCombineFast(Hash, PointerHash(Stack.Item.GetItemClass().Get()));
			Hash = HashCombineFast(Hash, GetTypeHash(Stack.NumItems));
		}
		return Hash;
	}

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
	UFGManufacturingButton* InButton)
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

	UTextBlock* InfinityLabel = MakeLabel(Tree, FText::FromString(TEXT("\u221e")), 18);
	UButton* InfinityButton = Tree->ConstructWidget<UButton>();
	FButtonStyle InvisibleButtonStyle;
	InvisibleButtonStyle.Normal.DrawAs = ESlateBrushDrawType::NoDrawType;
	InvisibleButtonStyle.Hovered.DrawAs = ESlateBrushDrawType::NoDrawType;
	InvisibleButtonStyle.Pressed.DrawAs = ESlateBrushDrawType::NoDrawType;
	InvisibleButtonStyle.Disabled.DrawAs = ESlateBrushDrawType::NoDrawType;
	InvisibleButtonStyle.NormalPadding = FMargin(0.0f);
	InvisibleButtonStyle.PressedPadding = FMargin(0.0f);
	InfinityButton->SetStyle(InvisibleButtonStyle);
	InfinityButton->SetToolTipText(LOCTEXT("InfinityTooltip", "Craft continuously"));
	InfinityButton->OnClicked.AddDynamic(this, &UExactCraftControlRow::HandleInfinityClicked);
	if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(InfinityButton->AddChild(InfinityLabel)))
	{
		ButtonSlot->SetHorizontalAlignment(HAlign_Center);
		ButtonSlot->SetVerticalAlignment(VAlign_Center);
	}
	UHorizontalBoxSlot* InfinitySlot = Layout->AddChildToHorizontalBox(InfinityButton);
	InfinitySlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	InfinitySlot->SetVerticalAlignment(VAlign_Center);
	InfinitySlot->SetPadding(FMargin(2.0f, 0.0f, 8.0f, 0.0f));

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
	CycleReadout->SetHintText(FText::FromString(TEXT("\u221e")));
	CycleReadout->SetJustification(ETextJustify::Center);
	CycleReadout->SetSelectAllTextWhenFocused(true);
	// Let Enter perform a normal text-box commit and return focus to the
	// crafting screen. Keeping focus here prevents Enter from completing the
	// edit reliably because the workbench also handles keyboard crafting input.
	CycleReadout->SetClearKeyboardFocusOnCommit(true);
	CycleReadout->SetIsReadOnly(false);
	CycleReadout->SetForegroundColor(FLinearColor(1.0f, 0.60f, 0.10f, 1.0f));
	FSlateFontInfo ReadoutFont = CycleReadout->WidgetStyle.TextStyle.Font;
	ReadoutFont.Size = 16;
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
	RefreshMaximum();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			RefreshTimer, this, &UExactCraftControlRow::RefreshMaximum, 0.25f, true);
	}
}

void UExactCraftControlRow::AttachQueueStatusBadge(UPanelWidget* Target)
{
	if (!IsValid(Target) || IsValid(QueueStatusContainer)) return;
	UWidgetTree* Tree = GetTypedOuter<UWidgetTree>();
	if (!Tree) return;

	QueueStatusLabel = MakeLabel(Tree, LOCTEXT("QueueReady", "READY FOR QUEUE"), 9);
	QueueStatusLabel->SetColorAndOpacity(FSlateColor(VanillaAffordText));
	QueueStatusLabel->SetRenderTranslation(FVector2D(0.0f, 2.0f));

	QueueStatusBadge = Tree->ConstructWidget<UBorder>();
	FSlateBrush BadgeBrush;
	BadgeBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
	// Keep the brush itself white so SetBrushColor supplies the color once.
	// Tinting both made the orange too dark and saturated.
	BadgeBrush.TintColor = FSlateColor(FLinearColor::White);
	BadgeBrush.OutlineSettings = FSlateBrushOutlineSettings(4.0f);
	QueueStatusBadge->SetBrush(BadgeBrush);
	QueueStatusBadge->SetPadding(FMargin(6.0f, 1.0f));
	QueueStatusBadge->AddChild(QueueStatusLabel);

	QueueStatusContainer = Tree->ConstructWidget<USizeBox>();
	QueueStatusContainer->SetWidthOverride(142.0f);
	QueueStatusContainer->SetHeightOverride(22.0f);
	QueueStatusContainer->SetRenderTranslation(FVector2D(0.5f, -1.0f));
	QueueStatusContainer->AddChild(QueueStatusBadge);
	Target->AddChild(QueueStatusContainer);
	if (UOverlaySlot* StatusSlot = Cast<UOverlaySlot>(QueueStatusContainer->Slot))
	{
		StatusSlot->SetHorizontalAlignment(HAlign_Center);
		StatusSlot->SetVerticalAlignment(VAlign_Bottom);
		StatusSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 28.0f));
	}
	RefreshQueueStatusBadge();
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
	RefreshQueueStatusBadge();
	if (bRequestActive)
	{
		EnsureCraftInputEnabled();
	}
	if (!bRequestActive)
	{
		DisplayedStepRecipe = nullptr;
		DisplayedStepNumber = 0;
		DisplayedStepCount = 0;
		DisplayedCompletedCycles = 0;
		DisplayedTotalCycles = 0;
		MaximumOutput = -1;
		RefreshMaximum();
	}
}

void UExactCraftControlRow::SetProductLabels(
	UTextBlock* InProductLabel,
	UTextBlock* InStepLabel)
{
	ProductLabel = InProductLabel;
	StepLabel = InStepLabel;
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
}

void UExactCraftControlRow::RefreshProductProgress()
{
	if (!bRequestActive || !DisplayedStepRecipe ||
		!IsValid(ProductLabel) || !IsValid(StepLabel))
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

	ProductLabel->SetText(FText::Format(
		LOCTEXT("CraftProductProgressFormat", "{0}\n{1} / {2}"),
		ItemName,
		FText::AsNumber(CompletedItems),
		FText::AsNumber(TotalItems)));
	StepLabel->SetText(FText::Format(
		LOCTEXT("CraftStepFormat", "STEP {0} OF {1}"),
		FText::AsNumber(DisplayedStepNumber),
		FText::AsNumber(DisplayedStepCount)));
	ProductLabel->SetVisibility(ESlateVisibility::HitTestInvisible);
	StepLabel->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UExactCraftControlRow::HandleSliderChanged(const float Value)
{
	if (bUpdatingControls) return;
	ApplyRequestedOutput(FMath::RoundToInt(Value));
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
	RefreshRequestedOutputAffordability();
	RefreshQueueStatusBadge();
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
		AffordabilityInventoryHash = 0;
		MissingMaterialsLabel.Reset();
		MissingMaterialsDetails.Reset();
		RefreshQueueStatusBadge();
		return;
	}

	UFGInventoryComponent* Inventory = WorkBench->GetInventory();
	if (!IsValid(Inventory)) Inventory = WorkBench->GetPlayerInventory();
	const TSubclassOf<UFGRecipe> Recipe = WorkBench->GetCurrentRecipe();
	const uint32 InventoryHash = GetInventoryHash(Inventory);
	if (!bForce && AffordabilityRequestedOutput == RequestedOutput &&
		AffordabilityRecipe == Recipe && AffordabilityInventoryHash == InventoryHash)
	{
		return;
	}

	AffordabilityRequestedOutput = RequestedOutput;
	AffordabilityRecipe = Recipe;
	AffordabilityInventoryHash = InventoryHash;
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
	RefreshQueueStatusBadge();
}

void UExactCraftControlRow::RefreshQueueStatusBadge()
{
	if (!IsValid(QueueStatusContainer) || !IsValid(QueueStatusBadge) ||
		!IsValid(QueueStatusLabel))
	{
		return;
	}
	if (bRequestActive || RequestedOutput <= 0 || HasVanillaIngredientsForOneCycle())
	{
		QueueStatusContainer->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	QueueStatusContainer->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (bRequestedOutputAffordable)
	{
		QueueStatusLabel->SetText(LOCTEXT("QueueReady", "READY FOR QUEUE"));
		QueueStatusBadge->SetBrushColor(VanillaAffordOrange);
	}
	else
	{
		QueueStatusLabel->SetText(LOCTEXT("QueueMissing", "MISSING INGREDIENTS"));
		QueueStatusBadge->SetBrushColor(VanillaAffordOrange);
	}
}

bool UExactCraftControlRow::HasVanillaIngredientsForOneCycle() const
{
	if (!IsValid(WorkBench) || !WorkBench->GetCurrentRecipe()) return false;
	UFGInventoryComponent* Inventory = WorkBench->GetInventory();
	if (!IsValid(Inventory)) Inventory = WorkBench->GetPlayerInventory();
	if (!IsValid(Inventory)) return false;

	for (const FItemAmount& Ingredient : UFGRecipe::GetIngredients(
		WorkBench, WorkBench->GetCurrentRecipe()))
	{
		if (Ingredient.ItemClass && Ingredient.Amount > 0 &&
			Inventory->GetNumItems(Ingredient.ItemClass) < Ingredient.Amount)
		{
			return false;
		}
	}
	return true;
}

void UExactCraftControlRow::RefreshMaximumLabel()
{
	if (!IsValid(MaximumLabel) || !IsValid(MaximumButton)) return;
	if (!bRequestedOutputAffordable && !MissingMaterialsLabel.IsEmpty())
	{
		MaximumLabel->SetText(FText::FromString(MissingMaterialsLabel));
		MaximumLabel->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.37f, 0.12f, 1.0f)));
		MaximumButton->SetToolTipText(FText::FromString(MissingMaterialsDetails));
		return;
	}

	MaximumLabel->SetText(FText::Format(LOCTEXT("MaximumFormat", "MAX {0}"), MaximumOutput));
	MaximumLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.78f, 0.80f, 0.80f, 1.0f)));
	MaximumButton->SetToolTipText(LOCTEXT("MaximumTooltip", "Craft the maximum currently affordable amount"));
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
	CycleReadout->SetText(RequestedOutput <= 0 ? FText::GetEmpty() : FText::AsNumber(RequestedOutput));
	bUpdatingControls = bWasUpdating;
}

#undef LOCTEXT_NAMESPACE
