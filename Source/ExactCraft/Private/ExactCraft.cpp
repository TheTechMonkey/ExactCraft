#include "ExactCraft.h"

#include "AkAudioEvent.h"
#include "AkComponent.h"
#include "AkGameplayStatics.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Containers/Ticker.h"
#include "Components/OverlaySlot.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "ExactCraftConfiguration.h"
#include "ExactCraftControlRow.h"
#include "ExactCraftHitCadence.h"
#include "ExactCraftInternal.h"
#include "FGCharacterPlayer.h"
#include "FGInventoryComponent.h"
#include "FGRecipe.h"
#include "FGRecipeManager.h"
#include "FGWorkBench.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Patching/NativeHookManager.h"
#include "Resources/FGItemDescriptor.h"
#include "TimerManager.h"
#include "UI/FGManufacturingButton.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY(LogExactCraft);

namespace ExactCraft
{
	struct FCraftStep
	{
		TSubclassOf<UFGRecipe> Recipe;
		int32 Cycles = 0;
	};

	struct FCraftRequest
	{
		TWeakObjectPtr<UFGManufacturingButton> Button;
		TWeakObjectPtr<UExactCraftControlRow> ControlRow;
		TSubclassOf<UFGRecipe> RootRecipe;
		TArray<FCraftStep> Steps;
		int32 CurrentStep = INDEX_NONE;
		int32 RemainingCycles = 0;
		bool bActive = false;
		bool bInternalRecipeChange = false;
		bool bAdvanceScheduled = false;
		bool bAutomaticRun = true;
		bool bMouseHeld = false;
		TSet<TWeakObjectPtr<UWidgetAnimation>> AnimationsBeforeCompletion;
		FExactCraftHitCadence HammerCadence;
		TWeakObjectPtr<UAkComponent> HammerComponent;
		bool bHammerFeedbackEnabled = false;
	};

	struct FPlanningState
	{
		TMap<TSubclassOf<UFGItemDescriptor>, int64> Counts;
		TMap<TSubclassOf<UFGItemDescriptor>, int64> Missing;
		TArray<FCraftStep> Steps;
	};

	struct FPlanningContext
	{
		UFGWorkBench* WorkBench = nullptr;
		UFGInventoryComponent* Inventory = nullptr;
		AFGRecipeManager* RecipeManager = nullptr;
		TSet<TSubclassOf<UFGRecipe>> AllowedRecipes;
	};

	static TMap<TWeakObjectPtr<UFGWorkBench>, FCraftRequest> Requests;
	static TMap<TWeakObjectPtr<UFGManufacturingButton>, TWeakObjectPtr<UFGWorkBench>> ButtonWorkBenches;
	static TStrongObjectPtr<UAkAudioEvent> HammerHitEvent;

	static void PrepareHammerFeedback(UFGWorkBench* WorkBench, FCraftRequest& Request)
	{
		Request.HammerCadence.Reset();
		Request.HammerComponent.Reset();
		Request.bHammerFeedbackEnabled = false;
		AFGCharacterPlayer* Player = WorkBench->GetWorkBenchUser();
		if (!IsValid(Player) || !Player->IsLocallyControlled() || !IsValid(Player->GetRootComponent())) return;

		bool bComponentCreated = false;
		Request.HammerComponent = UAkGameplayStatics::GetOrCreateAkComponent(
			Player->GetRootComponent(), bComponentCreated);

		if (!HammerHitEvent.IsValid())
		{
			// Verified in the installed game's IoStore index and crafting-widget
			// imports. Reference the vanilla asset; do not package a copied sound.
			HammerHitEvent.Reset(LoadObject<UAkAudioEvent>(nullptr, TEXT(
				"/Game/WwiseAudio/Events/Interface/WorkBench/WorkBench/Play_Workbench_Craft_Hit.Play_Workbench_Craft_Hit")));
		}
		if (!HammerHitEvent.IsValid())
		{
			UE_LOG(LogExactCraft, Warning, TEXT("Hammer feedback unavailable: vanilla hit event could not be loaded"));
			return;
		}
		if (!HammerHitEvent->IsLoaded()) HammerHitEvent->LoadData();

		Request.bHammerFeedbackEnabled = Request.HammerComponent.IsValid();
		if (!Request.bHammerFeedbackEnabled)
		{
			UE_LOG(LogExactCraft, Warning, TEXT("Hammer feedback unavailable: player audio component is missing"));
		}
	}

	static void TickHammerFeedback(UFGWorkBench* WorkBench, FCraftRequest& Request, const float RealDeltaSeconds)
	{
		const AFGCharacterPlayer* Player = WorkBench->GetWorkBenchUser();
		const UWorld* World = WorkBench->GetWorld();
		const bool bMayPlay = Request.bActive && Request.bHammerFeedbackEnabled &&
			!Request.bAdvanceScheduled && Request.RemainingCycles > 0 && Request.Button.IsValid() &&
			IsValid(Player) && Player->IsLocallyControlled() && IsValid(World) && !World->IsPaused() &&
			World->AllowAudioPlayback() &&
			Request.HammerComponent.IsValid() && HammerHitEvent.IsValid();
		if (!Request.HammerCadence.Tick(RealDeltaSeconds, bMayPlay)) return;

		// Sound only. Never broadcast OnManufacturePressed or arm the held-button
		// timer here: either would feed production a second time. Posting on the
		// existing game object also preserves its owner-destruction/audio settings.
		const int32 PlayingId = HammerHitEvent->PostOnGameObject(
			Request.HammerComponent.Get(), FOnAkPostEventCallback(), 0);
		if (PlayingId == 0)
		{
			Request.bHammerFeedbackEnabled = false;
			UE_LOG(LogExactCraft, Warning, TEXT("Hammer feedback failed: audio engine rejected the hit event; crafting is unchanged"));
			return;
		}
	}

	static void StopHammerFeedback(FCraftRequest& Request)
	{
		// Hits are one-shots. Stop scheduling them; do not stop other sounds on
		// the player's shared component or cut off the final hit's natural tail.
		Request.bHammerFeedbackEnabled = false;
		Request.HammerCadence.Reset();
		Request.HammerComponent.Reset();
	}

	static FString RecipeName(const TSubclassOf<UFGRecipe> Recipe)
	{
		return Recipe ? UFGRecipe::GetRecipeName(Recipe).ToString() : TEXT("None");
	}

	static FString ItemName(const TSubclassOf<UFGItemDescriptor> Item)
	{
		return Item ? UFGItemDescriptor::GetItemName(Item).ToString() : TEXT("Unknown item");
	}

	static bool IsAlternateRecipe(const TSubclassOf<UFGRecipe> Recipe)
	{
		return Recipe && Recipe->GetPathName().Contains(
			TEXT("/AlternateRecipes/"), ESearchCase::IgnoreCase);
	}

	static void ShowMessage(const FString& Message)
	{
		UE_LOG(LogExactCraft, Warning, TEXT("%s"), *Message);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(
				-1, 6.0f, FColor(255, 145, 30), FString::Printf(TEXT("Exact Craft: %s"), *Message));
		}
	}

	static void InvokeButtonFunction(UFGManufacturingButton* Button, const FName FunctionName)
	{
		if (!IsValid(Button)) return;
		if (UFunction* Function = Button->FindFunction(FunctionName))
		{
			Button->ProcessEvent(Function, nullptr);
		}
	}

	static bool InvokeOuterWidgetFunction(
		UFGManufacturingButton* Button,
		const FName FunctionName)
	{
		for (UObject* Object = Button; IsValid(Object); Object = Object->GetOuter())
		{
			UFunction* Function = Object->FindFunction(FunctionName);
			if (!Function) continue;
			if (Function->ParmsSize != 0)
			{
				UE_LOG(LogExactCraft, Warning, TEXT("Cannot call %s because its signature requires parameters"),
					*FunctionName.ToString());
				return false;
			}
			Object->ProcessEvent(Function, nullptr);
			return true;
		}
		UE_LOG(LogExactCraft, Warning, TEXT("Vanilla crafting widget function was not found: %s"),
			*FunctionName.ToString());
		return false;
	}

	static bool SetOwningWidgetBool(
		UFGManufacturingButton* Button,
		const FName PropertyName,
		const bool Value)
	{
		// Start outside UFGManufacturingButton deliberately. It has its own native
		// mIsHolding flag, and setting that flag makes NativeTick broadcast another
		// manufacture press later. The visual/audio state lives on the owning
		// Widget_ManualManufacturing instance.
		for (UObject* Object = IsValid(Button) ? Button->GetOuter() : nullptr;
			IsValid(Object); Object = Object->GetOuter())
		{
			FBoolProperty* Property = FindFProperty<FBoolProperty>(Object->GetClass(), PropertyName);
			if (!Property) continue;
			Property->SetPropertyValue_InContainer(Object, Value);
			return true;
		}
		UE_LOG(LogExactCraft, Warning, TEXT("Vanilla crafting widget state was not found: %s"),
			*PropertyName.ToString());
		return false;
	}

	static void SetVanillaCraftingFeedback(UFGManufacturingButton* Button, const bool bActive)
	{
		// Keep the existing visual feedback and background crafting loop. Runtime
		// audio logs confirm that these calls do not post the separate hammer hit.
		SetOwningWidgetBool(Button, TEXT("IsCraftButtonPressed"), bActive);
		SetOwningWidgetBool(Button, TEXT("mIsHolding"), bActive);
		InvokeOuterWidgetFunction(Button, TEXT("SetCraftButtonFeedback"));
		InvokeOuterWidgetFunction(
			Button,
			bActive ? TEXT("PlayCraftingButtonSFX") : TEXT("StopCraftingButtonSFX"));

		for (UObject* Object = Button; IsValid(Object); Object = Object->GetOuter())
		{
			UUserWidget* Widget = Cast<UUserWidget>(Object);
			FObjectPropertyBase* AnimationProperty =
				FindFProperty<FObjectPropertyBase>(Object->GetClass(), TEXT("LEDAnim"));
			if (!Widget || !AnimationProperty) continue;

			UWidgetAnimation* Animation = Cast<UWidgetAnimation>(
				AnimationProperty->GetObjectPropertyValue_InContainer(Object));
			if (!IsValid(Animation)) continue;

			if (bActive)
			{
				if (!Widget->IsAnimationPlaying(Animation))
				{
					// Zero loops means loop indefinitely. This is visual feedback;
					// the missing hammer event is handled independently above.
					Widget->PlayAnimation(
						Animation, 0.0f, 0, EUMGSequencePlayMode::Forward, 1.0f, false);
				}
			}
			else
			{
				Widget->StopAnimation(Animation);
			}
			break;
		}
	}

	static int32 ProductAmount(
		const TSubclassOf<UFGRecipe> Recipe,
		const TSubclassOf<UFGItemDescriptor> Item)
	{
		for (const FItemAmount& Product : UFGRecipe::GetProducts(Recipe))
		{
			if (Product.ItemClass == Item) return FMath::Max(0, Product.Amount);
		}
		return 0;
	}

	static int64 GetCount(
		FPlanningState& State,
		const FPlanningContext& Context,
		const TSubclassOf<UFGItemDescriptor> Item)
	{
		if (const int64* Existing = State.Counts.Find(Item)) return *Existing;
		const int64 Actual = IsValid(Context.Inventory)
			? Context.Inventory->GetNumItems(Item)
			: 0;
		State.Counts.Add(Item, Actual);
		return Actual;
	}

	static void AddStep(
		FPlanningState& State,
		const TSubclassOf<UFGRecipe> Recipe,
		const int32 Cycles)
	{
		if (Cycles <= 0) return;
		if (!State.Steps.IsEmpty() && State.Steps.Last().Recipe == Recipe)
		{
			State.Steps.Last().Cycles += Cycles;
		}
		else
		{
			State.Steps.Add({Recipe, Cycles});
		}
	}

	static void ConsolidateAndOrderSteps(FPlanningState& State, const FPlanningContext& Context)
	{
		if (State.Steps.Num() < 2) return;

		TArray<TSubclassOf<UFGRecipe>> Recipes;
		TMap<TSubclassOf<UFGRecipe>, int32> TotalCycles;
		TMap<TSubclassOf<UFGRecipe>, int32> FirstIndex;
		for (int32 Index = 0; Index < State.Steps.Num(); ++Index)
		{
			const FCraftStep& Step = State.Steps[Index];
			if (!Step.Recipe || Step.Cycles <= 0) continue;
			if (!TotalCycles.Contains(Step.Recipe))
			{
				Recipes.Add(Step.Recipe);
				FirstIndex.Add(Step.Recipe, Index);
			}
			TotalCycles.FindOrAdd(Step.Recipe) += Step.Cycles;
		}

		TMap<TSubclassOf<UFGRecipe>, TSet<TSubclassOf<UFGRecipe>>> Edges;
		TMap<TSubclassOf<UFGRecipe>, int32> InDegree;
		for (const TSubclassOf<UFGRecipe>& Recipe : Recipes) InDegree.Add(Recipe, 0);

		for (const TSubclassOf<UFGRecipe>& Producer : Recipes)
		{
			const TArray<FItemAmount> Products = UFGRecipe::GetProducts(Producer);
			for (const TSubclassOf<UFGRecipe>& Consumer : Recipes)
			{
				if (Producer == Consumer) continue;
				bool bDependency = false;
				for (const FItemAmount& Ingredient : UFGRecipe::GetIngredients(Context.WorkBench, Consumer))
				{
					if (!Ingredient.ItemClass) continue;
					bDependency = Products.ContainsByPredicate([&Ingredient](const FItemAmount& Product)
					{
						return Product.ItemClass == Ingredient.ItemClass && Product.Amount > 0;
					});
					if (bDependency) break;
				}
				if (bDependency && !Edges.FindOrAdd(Producer).Contains(Consumer))
				{
					Edges.FindOrAdd(Producer).Add(Consumer);
					++InDegree.FindOrAdd(Consumer);
				}
			}
		}

		TArray<TSubclassOf<UFGRecipe>> Ordered;
		TSet<TSubclassOf<UFGRecipe>> Emitted;
		while (Ordered.Num() < Recipes.Num())
		{
			TSubclassOf<UFGRecipe> Next = nullptr;
			int32 BestIndex = MAX_int32;
			for (const TSubclassOf<UFGRecipe>& Recipe : Recipes)
			{
				if (Emitted.Contains(Recipe) || InDegree.FindRef(Recipe) != 0) continue;
				const int32 Index = FirstIndex.FindRef(Recipe);
				if (Index < BestIndex)
				{
					Next = Recipe;
					BestIndex = Index;
				}
			}
			if (!Next)
			{
				UE_LOG(LogExactCraft, Warning, TEXT("Could not consolidate dependency steps because their recipe graph is cyclic"));
				return;
			}
			Ordered.Add(Next);
			Emitted.Add(Next);
			if (const TSet<TSubclassOf<UFGRecipe>>* Dependents = Edges.Find(Next))
			{
				for (const TSubclassOf<UFGRecipe>& Dependent : *Dependents)
				{
					--InDegree.FindOrAdd(Dependent);
				}
			}
		}

		State.Steps.Reset(Ordered.Num());
		for (const TSubclassOf<UFGRecipe>& Recipe : Ordered)
		{
			State.Steps.Add({Recipe, TotalCycles.FindRef(Recipe)});
		}
	}

	static bool EnsureItem(
		FPlanningState& State,
		const FPlanningContext& Context,
		TSet<TSubclassOf<UFGItemDescriptor>>& Visiting,
		TSubclassOf<UFGItemDescriptor> Item,
		int64 Needed);

	static bool PlanRecipe(
		FPlanningState& State,
		const FPlanningContext& Context,
		TSet<TSubclassOf<UFGItemDescriptor>>& Visiting,
		const TSubclassOf<UFGRecipe> Recipe,
		const int32 Cycles)
	{
		if (!Recipe || Cycles <= 0) return false;

		bool bCanComplete = true;
		for (const FItemAmount& Ingredient : UFGRecipe::GetIngredients(Context.WorkBench, Recipe))
		{
			if (!Ingredient.ItemClass || Ingredient.Amount <= 0) continue;
			const int64 Required = static_cast<int64>(Ingredient.Amount) * Cycles;
			if (!EnsureItem(State, Context, Visiting, Ingredient.ItemClass, Required))
			{
				// Keep walking the remaining ingredient branches so an unaffordable
				// request reports every missing raw material, not just the first one.
				bCanComplete = false;
			}
		}
		if (!bCanComplete) return false;

		AddStep(State, Recipe, Cycles);
		for (const FItemAmount& Product : UFGRecipe::GetProducts(Recipe))
		{
			if (!Product.ItemClass || Product.Amount <= 0) continue;
			const int64 Produced = static_cast<int64>(Product.Amount) * Cycles;
			State.Counts.FindOrAdd(Product.ItemClass) =
				GetCount(State, Context, Product.ItemClass) + Produced;
		}
		return true;
	}

	static int64 MissingTotal(const FPlanningState& State)
	{
		int64 Total = 0;
		for (const TPair<TSubclassOf<UFGItemDescriptor>, int64>& Entry : State.Missing)
		{
			Total += Entry.Value;
		}
		return Total;
	}

	static bool EnsureItem(
		FPlanningState& State,
		const FPlanningContext& Context,
		TSet<TSubclassOf<UFGItemDescriptor>>& Visiting,
		const TSubclassOf<UFGItemDescriptor> Item,
		const int64 Needed)
	{
		if (!Item || Needed <= 0) return true;

		const int64 Available = GetCount(State, Context, Item);
		if (Available >= Needed)
		{
			State.Counts.FindOrAdd(Item) = Available - Needed;
			return true;
		}

		const int64 Deficit = Needed - Available;
		State.Counts.FindOrAdd(Item) = 0;
		if (Visiting.Contains(Item))
		{
			State.Missing.FindOrAdd(Item) += Deficit;
			return false;
		}

		TArray<TSubclassOf<UFGRecipe>> Candidates;
		if (IsValid(Context.RecipeManager))
		{
			Candidates = Context.RecipeManager->FindRecipesByProduct(Item, true, true);
			Candidates.RemoveAll([&Context](const TSubclassOf<UFGRecipe> Candidate)
			{
				return !Context.AllowedRecipes.Contains(Candidate);
			});
			Candidates.Sort([](const TSubclassOf<UFGRecipe> A, const TSubclassOf<UFGRecipe> B)
			{
				return UFGRecipe::GetManufacturingMenuPriority(A) <
					UFGRecipe::GetManufacturingMenuPriority(B);
			});
		}

		if (Candidates.IsEmpty())
		{
			State.Missing.FindOrAdd(Item) += Deficit;
			return false;
		}

		Visiting.Add(Item);
		bool bHaveBestFailure = false;
		FPlanningState BestFailure;
		int64 BestMissing = MAX_int64;
		for (const TSubclassOf<UFGRecipe>& Candidate : Candidates)
		{
			const int32 PerCycle = ProductAmount(Candidate, Item);
			if (PerCycle <= 0) continue;
			const int64 Cycles64 = FMath::DivideAndRoundUp(Deficit, static_cast<int64>(PerCycle));
			if (Cycles64 > MAX_int32) continue;

			FPlanningState Trial = State;
			if (PlanRecipe(Trial, Context, Visiting, Candidate, static_cast<int32>(Cycles64)))
			{
				const int64 ProducedAvailable = GetCount(Trial, Context, Item);
				if (ProducedAvailable >= Deficit)
				{
					Trial.Counts.FindOrAdd(Item) = ProducedAvailable - Deficit;
					State = MoveTemp(Trial);
					Visiting.Remove(Item);
					return true;
				}
			}

			const int64 TrialMissing = MissingTotal(Trial);
			if (!bHaveBestFailure || TrialMissing < BestMissing)
			{
				bHaveBestFailure = true;
				BestMissing = TrialMissing;
				BestFailure = MoveTemp(Trial);
			}
		}
		Visiting.Remove(Item);

		if (bHaveBestFailure)
		{
			// The failed trial also contains the inventory consumed while tracing
			// this branch. Preserve it so sibling branches calculate shortages
			// against the same finite inventory instead of counting it twice.
			State = MoveTemp(BestFailure);
		}
		else
		{
			State.Missing.FindOrAdd(Item) += Deficit;
		}
		return false;
	}

	static void BuildAllowedRecipes(
		FPlanningContext& Context,
		const TSubclassOf<UFGRecipe> RootRecipe)
	{
		if (!IsValid(Context.RecipeManager)) return;
		TArray<TSubclassOf<UFGRecipe>> Available;
		if (IsValid(Context.WorkBench))
		{
			Context.RecipeManager->GetAvailableRecipesForProducer(
				Context.WorkBench->GetClass(), Available);
		}

		// Dependency planning follows only standard hand-crafting recipes.
		// An alternate remains valid solely when the player explicitly selected it
		// as the root recipe.
		Available.RemoveAll([](const TSubclassOf<UFGRecipe> Recipe)
		{
			return IsAlternateRecipe(Recipe);
		});

		Context.AllowedRecipes.Append(Available);
		Context.AllowedRecipes.Add(RootRecipe);
	}

	static bool BuildPlan(
		UFGWorkBench* WorkBench,
		const int32 RequestedOutput,
		TArray<FCraftStep>& OutSteps,
		FString& OutFailure,
		TArray<FMissingMaterial>* OutMissingMaterials = nullptr)
	{
		if (OutMissingMaterials) OutMissingMaterials->Reset();
		const TSubclassOf<UFGRecipe> RootRecipe = WorkBench->GetCurrentRecipe();
		const TArray<FItemAmount> Products = UFGRecipe::GetProducts(RootRecipe);
		if (Products.IsEmpty() || !Products[0].ItemClass || Products[0].Amount <= 0)
		{
			OutFailure = TEXT("the selected recipe has no usable output");
			return false;
		}

		UFGInventoryComponent* Inventory = WorkBench->GetInventory();
		if (!IsValid(Inventory)) Inventory = WorkBench->GetPlayerInventory();
		if (!IsValid(Inventory))
		{
			OutFailure = TEXT("player inventory is unavailable");
			return false;
		}

		const int32 RootCycles = FMath::DivideAndRoundUp(RequestedOutput, Products[0].Amount);
		FPlanningContext Context;
		Context.WorkBench = WorkBench;
		Context.Inventory = Inventory;
		Context.RecipeManager = AFGRecipeManager::Get(WorkBench);
		BuildAllowedRecipes(Context, RootRecipe);

		FPlanningState State;
		TSet<TSubclassOf<UFGItemDescriptor>> Visiting;
		if (!PlanRecipe(State, Context, Visiting, RootRecipe, RootCycles))
		{
			if (!State.Missing.IsEmpty())
			{
				TArray<FMissingMaterial> MissingMaterials;
				for (const TPair<TSubclassOf<UFGItemDescriptor>, int64>& Missing : State.Missing)
				{
					if (Missing.Key && Missing.Value > 0)
					{
						MissingMaterials.Add({Missing.Key, Missing.Value});
					}
				}
				MissingMaterials.Sort([](const FMissingMaterial& Left, const FMissingMaterial& Right)
				{
					return ItemName(Left.Item) < ItemName(Right.Item);
				});

				TArray<FString> Parts;
				for (const FMissingMaterial& Missing : MissingMaterials)
				{
					Parts.Add(FString::Printf(TEXT("%lld %s"), Missing.Amount, *ItemName(Missing.Item)));
				}
				OutFailure = FString::Printf(TEXT("need %s"), *FString::Join(Parts, TEXT(", ")));
				if (OutMissingMaterials) *OutMissingMaterials = MoveTemp(MissingMaterials);
			}
			else
			{
				OutFailure = TEXT("no complete hand-crafting path is available at this station");
			}
			return false;
		}

		ConsolidateAndOrderSteps(State, Context);
		OutSteps = MoveTemp(State.Steps);
		return !OutSteps.IsEmpty();
	}

	static void SelectStep(UFGWorkBench* WorkBench, FCraftRequest& Request)
	{
		if (!Request.Steps.IsValidIndex(Request.CurrentStep)) return;
		const FCraftStep& Step = Request.Steps[Request.CurrentStep];
		Request.RemainingCycles = Step.Cycles;
		Request.bInternalRecipeChange = true;
		WorkBench->SetRecipe(Step.Recipe);
		if (UExactCraftControlRow* Row = Request.ControlRow.Get())
		{
			Row->SetCraftStep(
				Step.Recipe,
				Request.CurrentStep + 1,
				Request.Steps.Num(),
				0,
				Step.Cycles);
		}
	}

	static void AdvanceStep(UFGWorkBench* WorkBench)
	{
		FCraftRequest* Request = Requests.Find(WorkBench);
		if (!Request || !Request->bActive) return;
		Request->bAdvanceScheduled = false;
		++Request->CurrentStep;
		if (Request->Steps.IsValidIndex(Request->CurrentStep))
		{
			SelectStep(WorkBench, *Request);
			return;
		}

		Request->bActive = false;
		Request->RemainingCycles = 0;
		StopHammerFeedback(*Request);
		if (WorkBench->GetCurrentRecipe() != Request->RootRecipe)
		{
			Request->bInternalRecipeChange = true;
			WorkBench->SetRecipe(Request->RootRecipe);
		}
		if (UExactCraftControlRow* Row = Request->ControlRow.Get()) Row->SetRequestActive(false);
		SetVanillaCraftingFeedback(Request->Button.Get(), false);
	}

	void RegisterManufacturingButton(UFGWorkBench* WorkBench, UFGManufacturingButton* Button)
	{
		if (!IsValid(WorkBench) || !IsValid(Button)) return;
		Requests.FindOrAdd(WorkBench).Button = Button;
		ButtonWorkBenches.FindOrAdd(Button) = WorkBench;

		UWidgetTree* Tree = Button->GetTypedOuter<UWidgetTree>();
		if (!Tree) return;
		UPanelWidget* Target = Cast<UPanelWidget>(Tree->FindWidget(TEXT("mScreenOverlay")));
		if (!Target)
		{
			UE_LOG(LogExactCraft, Error, TEXT("Manual-manufacturing root panel was not found"));
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
		if (UOverlaySlot* Slot = Cast<UOverlaySlot>(Row->Slot))
		{
			Slot->SetHorizontalAlignment(HAlign_Fill);
			Slot->SetVerticalAlignment(VAlign_Bottom);
			Slot->SetPadding(FMargin(14.0f, 0.0f, 14.0f, -28.0f));
		}
		Row->AttachQueueStatusBadge(Target);

		UTextBlock* ProductLabel = Cast<UTextBlock>(Tree->FindWidget(TEXT("mAddedToInventoryText")));
		UTextBlock* StepLabel = Cast<UTextBlock>(Tree->FindWidget(TEXT("mTotalInInventoryText")));
		Row->SetProductLabels(ProductLabel, StepLabel);
		if (!ProductLabel || !StepLabel)
		{
			UE_LOG(LogExactCraft, Warning, TEXT("Vanilla product-progress labels were not found"));
		}
	}

	static void BeginWithMode(
		UFGWorkBench* WorkBench,
		const int32 RequestedOutput,
		const bool bAutomaticRun)
	{
		if (!IsValid(WorkBench) || !WorkBench->GetCurrentRecipe() || RequestedOutput <= 0) return;
		FCraftRequest& Request = Requests.FindOrAdd(WorkBench);
		if (Request.bActive) return;

		TArray<FCraftStep> Steps;
		FString Failure;
		if (!BuildPlan(WorkBench, RequestedOutput, Steps, Failure))
		{
			ShowMessage(Failure);
			return;
		}

		Request.RootRecipe = WorkBench->GetCurrentRecipe();
		Request.Steps = MoveTemp(Steps);
		Request.CurrentStep = 0;
		Request.bActive = true;
		Request.bAdvanceScheduled = false;
		Request.bAutomaticRun = bAutomaticRun;
		Request.bMouseHeld = !bAutomaticRun;
		if (UExactCraftControlRow* Row = Request.ControlRow.Get()) Row->SetRequestActive(true);

		SelectStep(WorkBench, Request);
		SetVanillaCraftingFeedback(Request.Button.Get(), true);
		PrepareHammerFeedback(WorkBench, Request);
	}

	void Begin(UFGWorkBench* WorkBench, const int32 RequestedOutput)
	{
		BeginWithMode(WorkBench, RequestedOutput, true);
	}

	bool CanCompleteRequestedOutput(
		UFGWorkBench* WorkBench,
		const int32 RequestedOutput,
		TArray<FMissingMaterial>* OutMissingMaterials)
	{
		if (!IsValid(WorkBench) || RequestedOutput <= 0) return false;
		TArray<FCraftStep> Steps;
		FString Failure;
		return BuildPlan(WorkBench, RequestedOutput, Steps, Failure, OutMissingMaterials);
	}

	static void ResumeFeedback(UFGWorkBench* WorkBench, FCraftRequest& Request)
	{
		SetVanillaCraftingFeedback(Request.Button.Get(), true);
		PrepareHammerFeedback(WorkBench, Request);
	}

	static void PauseFeedback(FCraftRequest& Request)
	{
		StopHammerFeedback(Request);
		SetVanillaCraftingFeedback(Request.Button.Get(), false);
	}

	static UFGManufacturingButton* FindNumberedButtonAt(const FVector2D& ScreenPosition)
	{
		for (auto It = ButtonWorkBenches.CreateIterator(); It; ++It)
		{
			UFGManufacturingButton* Button = It.Key().Get();
			if (!IsValid(Button))
			{
				It.RemoveCurrent();
				continue;
			}

			UFGWorkBench* WorkBench = It.Value().Get();
			FCraftRequest* Request = IsValid(WorkBench) ? Requests.Find(WorkBench) : nullptr;
			UExactCraftControlRow* Row = Request ? Request->ControlRow.Get() : nullptr;
			if (!IsValid(Row) || Row->GetRequestedOutput() <= 0) continue;

			if (Row->IsCraftButtonUnderLocation(ScreenPosition)) return Button;
		}
		return nullptr;
	}

	bool ManufacturingButtonPressed(UFGManufacturingButton* Button)
	{
		const TWeakObjectPtr<UFGWorkBench>* WeakWorkBench = ButtonWorkBenches.Find(Button);
		UFGWorkBench* WorkBench = WeakWorkBench ? WeakWorkBench->Get() : nullptr;
		if (!IsValid(WorkBench)) return false;

		FCraftRequest* Request = Requests.Find(WorkBench);
		if (Request && Request->bActive)
		{
			const bool bWasRunning = Request->bAutomaticRun || Request->bMouseHeld;
			Request->bAutomaticRun = false;
			Request->bMouseHeld = true;
			if (!bWasRunning)
			{
				ResumeFeedback(WorkBench, *Request);
			}
			return true;
		}

		UExactCraftControlRow* Row = Request ? Request->ControlRow.Get() : nullptr;
		const int32 RequestedOutput = IsValid(Row) ? Row->GetRequestedOutput() : 0;
		if (RequestedOutput <= 0) return false;
		BeginWithMode(WorkBench, RequestedOutput, false);
		// A numbered mouse request owns this press even if planning reports missing
		// materials. Never fall through into vanilla partial crafting.
		return true;
	}

	bool ManufacturingButtonReleased(UFGManufacturingButton* Button)
	{
		const TWeakObjectPtr<UFGWorkBench>* WeakWorkBench = ButtonWorkBenches.Find(Button);
		UFGWorkBench* WorkBench = WeakWorkBench ? WeakWorkBench->Get() : nullptr;
		FCraftRequest* Request = IsValid(WorkBench) ? Requests.Find(WorkBench) : nullptr;
		if (!Request || !Request->bActive || Request->bAutomaticRun) return false;

		Request->bMouseHeld = false;
		PauseFeedback(*Request);
		return true;
	}

	bool ShouldForceCanProduce(UFGWorkBench* WorkBench, const TSubclassOf<UFGRecipe> Recipe)
	{
		if (!IsValid(WorkBench) || !Recipe || Recipe != WorkBench->GetCurrentRecipe()) return false;
		FCraftRequest* Request = Requests.Find(WorkBench);
		if (!Request) return false;
		if (Request->bActive)
		{
			return Request->Steps.IsValidIndex(Request->CurrentStep) &&
				Request->Steps[Request->CurrentStep].Recipe == Recipe &&
				Request->RemainingCycles > 0;
		}
		const UExactCraftControlRow* Row = Request->ControlRow.Get();
		return IsValid(Row) && Row->GetRequestedOutput() > 0;
	}

	bool ShouldForceRecipeAffordable(
		AFGCharacterPlayer* Player,
		const TSubclassOf<UFGRecipe> Recipe)
	{
		if (!IsValid(Player) || !Recipe) return false;
		for (auto Iterator = Requests.CreateIterator(); Iterator; ++Iterator)
		{
			UFGWorkBench* WorkBench = Iterator.Key().Get();
			if (!IsValid(WorkBench))
			{
				Iterator.RemoveCurrent();
				continue;
			}
			if (WorkBench->GetWorkBenchUser() == Player &&
				ShouldForceCanProduce(WorkBench, Recipe))
			{
				return true;
			}
		}
		return false;
	}

	bool TickRequests(const float DeltaSeconds)
	{
		for (auto Iterator = Requests.CreateIterator(); Iterator; ++Iterator)
		{
			UFGWorkBench* WorkBench = Iterator.Key().Get();
			if (!IsValid(WorkBench))
			{
				Iterator.RemoveCurrent();
				continue;
			}
			FCraftRequest& Request = Iterator.Value();
			if (!Request.bActive) continue;
			if (!IsValid(WorkBench->GetWorkBenchUser()))
			{
				// Closing a workbench cancels vanilla manual crafting. Exact Craft must
				// also discard its entire dependency request rather than leaving it
				// paused to resume unexpectedly the next time the bench is opened.
				if (Request.RootRecipe && WorkBench->GetCurrentRecipe() != Request.RootRecipe)
				{
					Request.bInternalRecipeChange = true;
					WorkBench->SetRecipe(Request.RootRecipe);
				}
				Reset(WorkBench, TEXT("workbench closed"));
				continue;
			}
			if (!Request.bAutomaticRun && !Request.bMouseHeld) continue;

			TickHammerFeedback(WorkBench, Request, DeltaSeconds);
			WorkBench->Produce(DeltaSeconds * GetCraftingSpeedMultiplier(WorkBench));
		}
		return true;
	}

	bool StartPendingRequestFromSpace()
	{
		for (auto Iterator = Requests.CreateIterator(); Iterator; ++Iterator)
		{
			UFGWorkBench* WorkBench = Iterator.Key().Get();
			if (!IsValid(WorkBench))
			{
				Iterator.RemoveCurrent();
				continue;
			}

			FCraftRequest& Request = Iterator.Value();
			UExactCraftControlRow* Row = Request.ControlRow.Get();
			if (!IsValid(Row) || Row->GetRequestedOutput() <= 0 ||
				!IsValid(WorkBench->GetWorkBenchUser()))
			{
				continue;
			}

			if (Request.bActive)
			{
				if (Request.bAutomaticRun)
				{
					Request.bAutomaticRun = false;
					Request.bMouseHeld = false;
					PauseFeedback(Request);
				}
				else
				{
					Request.bAutomaticRun = true;
					Request.bMouseHeld = false;
					ResumeFeedback(WorkBench, Request);
				}
				return true;
			}

			const int32 RequestedOutput = Row->GetRequestedOutput();
			Begin(WorkBench, RequestedOutput);

			FCraftRequest* StartedRequest = Requests.Find(WorkBench);
			return true;
		}
		return false;
	}

	int32 GetMaximumCraftableOutput(UFGWorkBench* WorkBench)
	{
		if (!IsValid(WorkBench)) return 0;
		const TSubclassOf<UFGRecipe> CurrentRecipe = WorkBench->GetCurrentRecipe();
		if (!CurrentRecipe) return 0;
		UFGInventoryComponent* Inventory = WorkBench->GetInventory();
		if (!IsValid(Inventory)) Inventory = WorkBench->GetPlayerInventory();
		if (!IsValid(Inventory)) return 0;

		int32 Cycles = MAX_int32;
		for (const FItemAmount& Ingredient : UFGRecipe::GetIngredients(WorkBench, CurrentRecipe))
		{
			if (Ingredient.ItemClass && Ingredient.Amount > 0)
			{
				Cycles = FMath::Min(Cycles, Inventory->GetNumItems(Ingredient.ItemClass) / Ingredient.Amount);
			}
		}
		if (Cycles == MAX_int32) Cycles = 0;
		const TArray<FItemAmount> Products = UFGRecipe::GetProducts(CurrentRecipe);
		const int32 OutputPerCycle = Products.IsEmpty() ? 1 : FMath::Max(1, Products[0].Amount);
		return FMath::Max(0, Cycles) * OutputPerCycle;
	}

	static void CaptureAnimationsBeforeCompletion(UFGWorkBench* WorkBench, FCraftRequest& Request)
	{
		Request.AnimationsBeforeCompletion.Reset();
		if (FExactCraftConfigurationStruct::ShouldShowCraftCompletionPulse(WorkBench)) return;

		for (UObject* Object = Request.Button.Get(); IsValid(Object); Object = Object->GetOuter())
		{
			UUserWidget* Widget = Cast<UUserWidget>(Object);
			if (!Widget) continue;
			for (TFieldIterator<FObjectPropertyBase> Property(Object->GetClass()); Property; ++Property)
			{
				UWidgetAnimation* Animation = Cast<UWidgetAnimation>(
					Property->GetObjectPropertyValue_InContainer(Object));
				if (IsValid(Animation) && Widget->IsAnimationPlaying(Animation))
				{
					Request.AnimationsBeforeCompletion.Add(Animation);
				}
			}
		}
	}

	static void SuppressNewCompletionAnimations(UFGWorkBench* WorkBench, FCraftRequest& Request)
	{
		if (FExactCraftConfigurationStruct::ShouldShowCraftCompletionPulse(WorkBench))
		{
			Request.AnimationsBeforeCompletion.Reset();
			return;
		}

		for (UObject* Object = Request.Button.Get(); IsValid(Object); Object = Object->GetOuter())
		{
			UUserWidget* Widget = Cast<UUserWidget>(Object);
			if (!Widget) continue;
			for (TFieldIterator<FObjectPropertyBase> Property(Object->GetClass()); Property; ++Property)
			{
				UWidgetAnimation* Animation = Cast<UWidgetAnimation>(
					Property->GetObjectPropertyValue_InContainer(Object));
				if (!IsValid(Animation) || !Widget->IsAnimationPlaying(Animation) ||
					Request.AnimationsBeforeCompletion.Contains(Animation))
				{
					continue;
				}
				Widget->StopAnimation(Animation);
			}
		}
		Request.AnimationsBeforeCompletion.Reset();
	}

	bool AllowCraftCompletion(UFGWorkBench* WorkBench)
	{
		FCraftRequest* Request = Requests.Find(WorkBench);
		if (!Request) return true;
		CaptureAnimationsBeforeCompletion(WorkBench, *Request);
		if (!Request->bActive) return true;
		if (!Request->Steps.IsValidIndex(Request->CurrentStep) ||
			WorkBench->GetCurrentRecipe() != Request->Steps[Request->CurrentStep].Recipe ||
			Request->RemainingCycles <= 0)
		{
			return false;
		}
		--Request->RemainingCycles;
		return true;
	}

	void HandleCraftCompleted(UFGWorkBench* WorkBench)
	{
		FCraftRequest* Request = Requests.Find(WorkBench);
		if (!Request) return;
		SuppressNewCompletionAnimations(WorkBench, *Request);
		if (!Request->bActive || !Request->Steps.IsValidIndex(Request->CurrentStep)) return;
		const FCraftStep& Step = Request->Steps[Request->CurrentStep];
		if (UExactCraftControlRow* Row = Request->ControlRow.Get())
		{
			Row->SetCraftStep(
				Step.Recipe,
				Request->CurrentStep + 1,
				Request->Steps.Num(),
				Step.Cycles - Request->RemainingCycles,
				Step.Cycles);
		}
		if (Request->RemainingCycles > 0 || Request->bAdvanceScheduled) return;
		Request->bAdvanceScheduled = true;
		if (UWorld* World = WorkBench->GetWorld())
		{
			const TWeakObjectPtr<UFGWorkBench> WeakWorkBench(WorkBench);
			World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([WeakWorkBench]
			{
				if (UFGWorkBench* ValidWorkBench = WeakWorkBench.Get()) AdvanceStep(ValidWorkBench);
			}));
		}
	}

	bool HasReachedCraftLimit(const UFGWorkBench* WorkBench)
	{
		const FCraftRequest* Request = Requests.Find(const_cast<UFGWorkBench*>(WorkBench));
		return Request && Request->bActive && Request->RemainingCycles <= 0;
	}

	float GetCraftingSpeedMultiplier(UFGWorkBench* WorkBench)
	{
		return FExactCraftConfigurationStruct::GetCraftingSpeedMultiplier(WorkBench);
	}

	void Reset(UFGWorkBench* WorkBench, const TCHAR* Reason)
	{
		if (FCraftRequest* Request = Requests.Find(WorkBench))
		{
			if (Request->bActive)
			{
				SetVanillaCraftingFeedback(Request->Button.Get(), false);
			}
			Request->RootRecipe = nullptr;
			StopHammerFeedback(*Request);
			Request->Steps.Reset();
			Request->CurrentStep = INDEX_NONE;
			Request->RemainingCycles = 0;
			Request->bActive = false;
			Request->bInternalRecipeChange = false;
			Request->bAdvanceScheduled = false;
			if (UExactCraftControlRow* Row = Request->ControlRow.Get()) Row->SetRequestActive(false);
		}
	}

	void RecipeChanged(UFGWorkBench* WorkBench, const TSubclassOf<UFGRecipe> Recipe)
	{
		FCraftRequest* Request = Requests.Find(WorkBench);
		if (Request && Request->bInternalRecipeChange)
		{
			Request->bInternalRecipeChange = false;
			if (UExactCraftControlRow* Row = Request->ControlRow.Get())
			{
				Row->HandleRecipeChanged(Recipe, true);
			}
			return;
		}

		Reset(WorkBench, TEXT("the selected recipe changed"));
		if (Request)
		{
			if (UExactCraftControlRow* Row = Request->ControlRow.Get())
			{
				Row->HandleRecipeChanged(Recipe, false);
			}
		}
	}
}

class FExactCraftInputProcessor final : public IInputProcessor
{
public:
	virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}

	virtual bool HandleMouseButtonDownEvent(FSlateApplication&, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) return false;
		if (bExactMouseHeld) return true;

		UFGManufacturingButton* Button = ExactCraft::FindNumberedButtonAt(
			MouseEvent.GetScreenSpacePosition());
		if (!IsValid(Button) || !ExactCraft::ManufacturingButtonPressed(Button)) return false;

		HeldManufacturingButton = Button;
		bExactMouseHeld = true;
		return true;
	}

	virtual bool HandleMouseButtonUpEvent(FSlateApplication&, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !bExactMouseHeld) return false;

		if (UFGManufacturingButton* Button = HeldManufacturingButton.Get())
		{
			ExactCraft::ManufacturingButtonReleased(Button);
		}
		HeldManufacturingButton.Reset();
		bExactMouseHeld = false;
		return true;
	}

	virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& KeyEvent) override
	{
		if (KeyEvent.GetKey() != EKeys::SpaceBar) return false;

		// Once a numbered request owns this physical Space press, consume every
		// repeat until release. Passing repeats back to the native workbench lets
		// its hold-to-craft control restart the same completed batch.
		if (KeyEvent.IsRepeat()) return bExactSpaceHeld;

		bExactSpaceHeld = ExactCraft::StartPendingRequestFromSpace();
		return bExactSpaceHeld;
	}

	virtual bool HandleKeyUpEvent(FSlateApplication&, const FKeyEvent& KeyEvent) override
	{
		if (KeyEvent.GetKey() != EKeys::SpaceBar) return false;

		const bool bHandledExactPress = bExactSpaceHeld;
		bExactSpaceHeld = false;
		return bHandledExactPress;
	}

	virtual const TCHAR* GetDebugName() const override
	{
		return TEXT("ExactCraftInputProcessor");
	}

private:
	bool bExactSpaceHeld = false;
	bool bExactMouseHeld = false;
	TWeakObjectPtr<UFGManufacturingButton> HeldManufacturingButton;
};

void FExactCraftModule::StartupModule()
{
#if !WITH_EDITOR
	SUBSCRIBE_METHOD(
		UFGManufacturingButton::OnPressedButton,
		[](auto& Scope, UFGManufacturingButton* Button)
		{
			if (ExactCraft::ManufacturingButtonPressed(Button)) Scope.Cancel();
		});

	SUBSCRIBE_METHOD(
		UFGManufacturingButton::OnReleasedButton,
		[](auto& Scope, UFGManufacturingButton* Button)
		{
			if (ExactCraft::ManufacturingButtonReleased(Button)) Scope.Cancel();
		});

	SUBSCRIBE_METHOD_AFTER(
		UFGWorkBench::SetupManufacturingButton,
		[](UFGWorkBench* WorkBench, UFGManufacturingButton* Button)
		{
			ExactCraft::RegisterManufacturingButton(WorkBench, Button);
		});

	SUBSCRIBE_METHOD(
		UFGWorkBench::CanProduce,
		[](auto& Scope, const UFGWorkBench* WorkBench, TSubclassOf<UFGRecipe> Recipe, UFGInventoryComponent*)
		{
			if (ExactCraft::ShouldForceCanProduce(const_cast<UFGWorkBench*>(WorkBench), Recipe))
			{
				Scope.Override(true);
			}
		});

	SUBSCRIBE_METHOD(
		UFGRecipe::IsRecipeAffordable,
		[](auto& Scope, AFGCharacterPlayer* Player, TSubclassOf<UFGRecipe> Recipe)
		{
			if (ExactCraft::ShouldForceRecipeAffordable(Player, Recipe))
			{
				Scope.Override(true);
			}
		});

	SUBSCRIBE_METHOD(
		UFGWorkBench::CraftComplete,
		[](auto& Scope, UFGWorkBench* WorkBench)
		{
			if (!ExactCraft::AllowCraftCompletion(WorkBench)) Scope.Cancel();
		});

	SUBSCRIBE_METHOD_AFTER(
		UFGWorkBench::CraftComplete,
		[](UFGWorkBench* WorkBench)
		{
			ExactCraft::HandleCraftCompleted(WorkBench);
		});

	SUBSCRIBE_METHOD_AFTER(
		UFGWorkBench::SetRecipe,
		[](UFGWorkBench* WorkBench, TSubclassOf<UFGRecipe> Recipe)
		{
			ExactCraft::RecipeChanged(WorkBench, Recipe);
		});

	if (FSlateApplication::IsInitialized())
	{
		InputProcessor = MakeShared<FExactCraftInputProcessor>();
		FSlateApplication::Get().RegisterInputPreProcessor(InputProcessor, 0);
	}
	else
	{
		UE_LOG(LogExactCraft, Error, TEXT("Slate was not initialized; direct Space input could not be registered"));
	}
	RequestTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateStatic(&ExactCraft::TickRequests));
#endif
}

void FExactCraftModule::ShutdownModule()
{
	if (RequestTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(RequestTickerHandle);
		RequestTickerHandle.Reset();
	}
	if (InputProcessor.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(InputProcessor);
	}
	InputProcessor.Reset();
	ExactCraft::HammerHitEvent.Reset();
}

IMPLEMENT_MODULE(FExactCraftModule, ExactCraft)
