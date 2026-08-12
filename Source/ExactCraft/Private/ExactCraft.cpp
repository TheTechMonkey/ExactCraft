#include "ExactCraft.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/OverlaySlot.h"
#include "Components/PanelWidget.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "ExactCraftControlRow.h"
#include "ExactCraftInternal.h"
#include "FGInventoryComponent.h"
#include "FGRecipe.h"
#include "FGWorkBench.h"
#include "Patching/NativeHookManager.h"
#include "UI/FGManufacturingButton.h"

DEFINE_LOG_CATEGORY(LogExactCraft);

namespace ExactCraft
{
	struct FCraftRequest
	{
		TWeakObjectPtr<UFGManufacturingButton> Button;
		TWeakObjectPtr<UExactCraftControlRow> ControlRow;
		int32 RemainingCycles = 0;
	};

	static TMap<TWeakObjectPtr<UFGWorkBench>, FCraftRequest> Requests;

	static void InvokeButtonFunction(UFGManufacturingButton* Button, const FName FunctionName)
	{
		if (!IsValid(Button))
		{
			return;
		}
		if (UFunction* Function = Button->FindFunction(FunctionName))
		{
			Button->ProcessEvent(Function, nullptr);
		}
	}

	void RegisterManufacturingButton(UFGWorkBench* WorkBench, UFGManufacturingButton* Button)
	{
		if (!IsValid(WorkBench) || !IsValid(Button))
		{
			return;
		}
		Requests.FindOrAdd(WorkBench).Button = Button;

		UPanelWidget* Parent = Cast<UPanelWidget>(Button->GetParent());
		UWidgetTree* Tree = Button->GetTypedOuter<UWidgetTree>();
		if (!Parent || !Tree)
		{
			UE_LOG(LogExactCraft, Warning, TEXT("Could not locate manufacturing-button container"));
			return;
		}

		// Mount inside the vanilla recipe screen itself. This keeps the control
		// locked to the INPUT/OUTPUT strip at every resolution and UI scale.
		UPanelWidget* Target = Cast<UPanelWidget>(Tree->FindWidget(TEXT("mScreenOverlay")));
		if (!Target)
		{
			UE_LOG(LogExactCraft, Error, TEXT("Manual-manufacturing root panel was not found; ExactCraft controls were not inserted"));
			return;
		}
		if (UWidget* ScreenLabel = Tree->FindWidget(TEXT("ScreenLabel")))
		{
			ScreenLabel->SetVisibility(ESlateVisibility::Collapsed);
		}

		for (int32 Index = 0; Index < Target->GetChildrenCount(); ++Index)
		{
			if (Target->GetChildAt(Index)->IsA<UExactCraftControlRow>()) return;
		}

		UExactCraftControlRow* Row = Tree->ConstructWidget<UExactCraftControlRow>();
		Row->InitializeFor(WorkBench, Button);
		Requests.FindOrAdd(WorkBench).ControlRow = Row;

		Target->AddChild(Row);
		if (UOverlaySlot* OverlaySlot = Cast<UOverlaySlot>(Row->Slot))
		{
			OverlaySlot->SetHorizontalAlignment(HAlign_Fill);
			OverlaySlot->SetVerticalAlignment(VAlign_Bottom);
			// The vanilla label is translated upward from this slot. Keep our
			// replacement in the recessed strip below it instead.
			// Leave the small vanilla rivets visible at both ends. Raising the
			// bottom edge four pixels and reducing the row's vertical padding
			// seats the panel inside the existing recessed frame.
			OverlaySlot->SetPadding(FMargin(14.0f, 0.0f, 14.0f, -28.0f));
		}

		UE_LOG(LogExactCraft, Display, TEXT("ExactCraft slider inserted over INPUT/OUTPUT strip"));
	}

	void Begin(UFGWorkBench* WorkBench, const int32 RequestedCycles)
	{
		if (!IsValid(WorkBench) || !WorkBench->GetCurrentRecipe())
		{
			return;
		}

		FCraftRequest& Request = Requests.FindOrAdd(WorkBench);
		if (Request.RemainingCycles > 0 || RequestedCycles <= 0)
		{
			return;
		}
		Request.RemainingCycles = RequestedCycles;
	}

	int32 GetMaximumCraftableCycles(UFGWorkBench* WorkBench)
	{
		if (!IsValid(WorkBench) || !WorkBench->GetCurrentRecipe())
		{
			return 0;
		}
		UFGInventoryComponent* Inventory = WorkBench->GetInventory();
		if (!IsValid(Inventory))
		{
			Inventory = WorkBench->GetPlayerInventory();
		}
		if (!IsValid(Inventory))
		{
			return 0;
		}

		int32 Cycles = MAX_int32;
		for (const FItemAmount& Ingredient :
			UFGRecipe::GetIngredients(WorkBench, WorkBench->GetCurrentRecipe()))
		{
			if (Ingredient.ItemClass && Ingredient.Amount > 0)
			{
				Cycles = FMath::Min(
					Cycles,
					Inventory->GetNumItems(Ingredient.ItemClass) / Ingredient.Amount);
			}
		}
		if (Cycles == MAX_int32)
		{
			Cycles = 0;
		}

		return FMath::Max(0, Cycles);
	}

	void HandleCraftCompleted(UFGWorkBench* WorkBench)
	{
		FCraftRequest* Request = Requests.Find(WorkBench);
		if (!Request || Request->RemainingCycles <= 0)
		{
			return;
		}
		--Request->RemainingCycles;
		if (Request->RemainingCycles == 0)
		{
			InvokeButtonFunction(Request->Button.Get(), TEXT("OnReleasedButton"));
		}
	}

	void Reset(UFGWorkBench* WorkBench)
	{
		if (FCraftRequest* Request = Requests.Find(WorkBench))
		{
			Request->RemainingCycles = 0;
		}
	}

	void RecipeChanged(UFGWorkBench* WorkBench, const TSubclassOf<UFGRecipe> Recipe)
	{
		Reset(WorkBench);
		if (FCraftRequest* Request = Requests.Find(WorkBench))
		{
			if (UExactCraftControlRow* Row = Request->ControlRow.Get())
			{
				Row->HandleRecipeChanged(Recipe);
			}
		}
	}
}

void FExactCraftModule::StartupModule()
{
#if !WITH_EDITOR
	SUBSCRIBE_METHOD_AFTER(
		UFGWorkBench::SetupManufacturingButton,
		[](UFGWorkBench* WorkBench, UFGManufacturingButton* Button)
		{
			ExactCraft::RegisterManufacturingButton(WorkBench, Button);
		});

	SUBSCRIBE_METHOD_AFTER(
		UFGWorkBench::RemoveIngredientsAndAwardRewards,
		[](UFGWorkBench* WorkBench, UFGInventoryComponent*, TSubclassOf<UFGRecipe>)
		{
			ExactCraft::HandleCraftCompleted(WorkBench);
		});

	SUBSCRIBE_METHOD_AFTER(
		UFGWorkBench::SetRecipe,
		[](UFGWorkBench* WorkBench, TSubclassOf<UFGRecipe> Recipe)
		{
			ExactCraft::RecipeChanged(WorkBench, Recipe);
		});
#endif
}

void FExactCraftModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FExactCraftModule, ExactCraft)
