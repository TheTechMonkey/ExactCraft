#include "ExactCraftControlRow.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "ExactCraftInternal.h"
#include "FGWorkBench.h"
#include "TimerManager.h"
#include "UI/FGManufacturingButton.h"

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

	CycleReadout = Tree->ConstructWidget<UEditableTextBox>();
	CycleReadout->SetText(FText::FromString(TEXT("\u221e")));
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
	UButton* MaximumButton = Tree->ConstructWidget<UButton>();
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

	InButton->OnManufacturePressed.AddDynamic(
		this, &UExactCraftControlRow::HandleManufacturePressed);
	RefreshMaximum();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			RefreshTimer, this, &UExactCraftControlRow::RefreshMaximum, 0.25f, true);
	}
}

void UExactCraftControlRow::BeginDestroy()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimer);
	}
	Super::BeginDestroy();
}

void UExactCraftControlRow::HandleRecipeChanged(const TSubclassOf<UFGRecipe> NewRecipe)
{
	LastRecipe = NewRecipe;
	RequestedCycles = 0;
	MaximumCycles = -1;
	ExactCraft::Reset(WorkBench);
	RefreshMaximum();
}

void UExactCraftControlRow::HandleManufacturePressed(float)
{
	if (RequestedCycles > 0)
	{
		ExactCraft::Begin(WorkBench, RequestedCycles);
	}
}

void UExactCraftControlRow::HandleSliderChanged(const float Value)
{
	if (bUpdatingControls) return;
	ApplyRequestedCycles(FMath::Clamp(FMath::RoundToInt(Value), 0, MaximumCycles));
}

void UExactCraftControlRow::HandleInfinityClicked()
{
	ApplyRequestedCycles(0);
}

void UExactCraftControlRow::HandleMaximumClicked()
{
	ApplyRequestedCycles(MaximumCycles);
}

void UExactCraftControlRow::HandleValueCommitted(const FText& Text, const ETextCommit::Type CommitMethod)
{
	const FString Value = Text.ToString().TrimStartAndEnd();
	if (Value.IsEmpty() || Value == TEXT("\u221e") || Value.Equals(TEXT("inf"), ESearchCase::IgnoreCase))
	{
		ApplyRequestedCycles(0);
	}
	else
	{
		ApplyRequestedCycles(FMath::Clamp(FCString::Atoi(*Value), 0, MaximumCycles));
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

void UExactCraftControlRow::RefreshMaximum()
{
	const TSubclassOf<UFGRecipe> CurrentRecipe = IsValid(WorkBench)
		? WorkBench->GetCurrentRecipe()
		: nullptr;
	const bool bRecipeChanged = CurrentRecipe != LastRecipe;
	if (bRecipeChanged)
	{
		LastRecipe = CurrentRecipe;
		RequestedCycles = 0;
		ExactCraft::Reset(WorkBench);
	}

	const int32 NewMaximum = ExactCraft::GetMaximumCraftableCycles(WorkBench);
	if (!bRecipeChanged && NewMaximum == MaximumCycles) return;
	MaximumCycles = NewMaximum;
	bUpdatingControls = true;
	CycleSlider->SetMaxValue(FMath::Max(1.0f, static_cast<float>(MaximumCycles)));
	CycleSlider->SetStepSize(1.0f / FMath::Max(1.0f, static_cast<float>(MaximumCycles)));
	if (RequestedCycles > MaximumCycles)
	{
		RequestedCycles = MaximumCycles;
	}
	CycleSlider->SetValue(static_cast<float>(RequestedCycles));
	bUpdatingControls = false;
	MaximumLabel->SetText(FText::Format(LOCTEXT("MaximumFormat", "MAX {0}"), MaximumCycles));
	RefreshReadout();
}

void UExactCraftControlRow::ApplyRequestedCycles(const int32 Cycles)
{
	RequestedCycles = FMath::Clamp(Cycles, 0, MaximumCycles);
	bUpdatingControls = true;
	CycleSlider->SetValue(static_cast<float>(RequestedCycles));
	bUpdatingControls = false;
	RefreshReadout();
}

void UExactCraftControlRow::RefreshReadout()
{
	CycleReadout->SetText(RequestedCycles <= 0
		? FText::FromString(TEXT("\u221e"))
		: FText::AsNumber(RequestedCycles));
}

int32 UExactCraftControlRow::GetRequestedCycles() const
{
	return RequestedCycles;
}

#undef LOCTEXT_NAMESPACE
