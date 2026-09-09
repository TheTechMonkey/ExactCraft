#include "ExactCraftConfiguration.h"

#include "Configuration/ConfigManager.h"
#include "Configuration/Properties/ConfigPropertyBool.h"
#include "Configuration/Properties/ConfigPropertyInteger.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "Configuration/Properties/WidgetExtension/CP_Section.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Util/EngineUtil.h"

#define LOCTEXT_NAMESPACE "ExactCraft"

UExactCraftConfiguration::UExactCraftConfiguration()
{
	ConfigId = {TEXT("ExactCraft"), TEXT("")};
	DisplayName = LOCTEXT("ConfigName", "Exact Craft");
	Description = LOCTEXT("ConfigDescription", "Controls Exact Craft speed and visual feedback.");

	static ConstructorHelpers::FClassFinder<UConfigPropertySection> SectionPropertyClass(
		TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertySection"));
	check(SectionPropertyClass.Succeeded());
	RootSection = CastChecked<UConfigPropertySection>(CreateDefaultSubobject(
		TEXT("RootSection"),
		UConfigPropertySection::StaticClass(),
		SectionPropertyClass.Class,
		true,
		false));
	if (UCP_Section* VisualSection = Cast<UCP_Section>(RootSection))
	{
		VisualSection->WidgetType = ECP_SectionWidgetType::CPS_Vertical;
		VisualSection->HasHeader = false;
	}
	static ConstructorHelpers::FClassFinder<UCP_Integer> IntegerPropertyClass(
		TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertyInteger"));
	check(IntegerPropertyClass.Succeeded());
	UCP_Integer* Speed = CastChecked<UCP_Integer>(CreateDefaultSubobject(
		TEXT("CraftingSpeedMultiplier"),
		UCP_Integer::StaticClass(),
		IntegerPropertyClass.Class,
		true,
		false));
	Speed->DisplayName = LOCTEXT("SpeedName", "Manual crafting speed multiplier");
	Speed->Tooltip = LOCTEXT(
		"SpeedTooltip",
		"Drag the bar or enter a number. 1x is vanilla speed and 20x is the maximum.");
	Speed->DefaultValue = 1;
	Speed->Value = 1;
	Speed->WidgetType = ECP_IntegerWidgetType::CPI_Slider;
	Speed->MinValue = 1;
	Speed->MaxValue = 20;
	Speed->bRequiresWorldReload = false;
	RootSection->SectionProperties.Add(TEXT("CraftingSpeedMultiplier"), Speed);

	static ConstructorHelpers::FClassFinder<UCP_Bool> BoolPropertyClass(
		TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertyBool"));
	check(BoolPropertyClass.Succeeded());
	UCP_Bool* CompletionPulse = CastChecked<UCP_Bool>(CreateDefaultSubobject(
		TEXT("ShowCraftCompletionPulse"),
		UCP_Bool::StaticClass(),
		BoolPropertyClass.Class,
		true,
		false));
	CompletionPulse->DisplayName = LOCTEXT("CompletionPulseName", "Show craft completion pulse");
	CompletionPulse->Tooltip = LOCTEXT(
		"CompletionPulseTooltip",
		"Briefly enlarges and highlights the crafted item after each completed Exact Craft craft. The vanilla screen always keeps its original pulse.");
	CompletionPulse->DefaultValue = true;
	CompletionPulse->Value = true;
	CompletionPulse->bRequiresWorldReload = false;
	RootSection->SectionProperties.Add(TEXT("ShowCraftCompletionPulse"), CompletionPulse);

	UCP_Bool* ExactCraftScreen = CastChecked<UCP_Bool>(CreateDefaultSubobject(
		TEXT("UseExactCraftScreen"),
		UCP_Bool::StaticClass(),
		BoolPropertyClass.Class,
		true,
		false));
	ExactCraftScreen->DisplayName = LOCTEXT("ExactCraftScreenName", "Use Exact Craft screen");
	ExactCraftScreen->Tooltip = LOCTEXT(
		"ExactCraftScreenTooltip",
		"Use Exact Craft status information in the recipe display. Disable for the untouched vanilla display. Reopen the workbench after changing this setting.");
	ExactCraftScreen->DefaultValue = true;
	ExactCraftScreen->Value = true;
	ExactCraftScreen->bRequiresWorldReload = false;
	RootSection->SectionProperties.Add(TEXT("UseExactCraftScreen"), ExactCraftScreen);
}

void UExactCraftConfigurationRegistrar::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<UConfigManager>();
	Super::Initialize(Collection);
	if (UConfigManager* ConfigManager = GetGameInstance()->GetSubsystem<UConfigManager>())
	{
		ConfigManager->RegisterModConfiguration(UExactCraftConfiguration::StaticClass());
		PollForConfigurationChanges();
		FEngineUtil::DispatchWhenTimerManagerIsReady(
			TDelegate<void(FTimerManager*)>::CreateUObject(
				this, &UExactCraftConfigurationRegistrar::StartPersistenceTimer));
	}
}

void UExactCraftConfigurationRegistrar::Deinitialize()
{
	PollForConfigurationChanges();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PersistenceTimer);
	}
	Super::Deinitialize();
}

void UExactCraftConfigurationRegistrar::StartPersistenceTimer(FTimerManager* TimerManager)
{
	if (!TimerManager) return;
	TimerManager->SetTimer(
		PersistenceTimer,
		FTimerDelegate::CreateUObject(
			this, &UExactCraftConfigurationRegistrar::PollForConfigurationChanges),
		0.5f,
		true);
}

void UExactCraftConfigurationRegistrar::PollForConfigurationChanges()
{
	UConfigManager* ConfigManager = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UConfigManager>()
		: nullptr;
	if (!ConfigManager) return;

	static const FConfigId ConfigId{TEXT("ExactCraft"), TEXT("")};
	UConfigPropertySection* Root = ConfigManager->GetConfigurationRootSection(ConfigId);
	if (!Root) return;
	const TObjectPtr<UConfigProperty>* SpeedProperty =
		Root->SectionProperties.Find(TEXT("CraftingSpeedMultiplier"));
	const UConfigPropertyInteger* Speed = SpeedProperty
		? Cast<UConfigPropertyInteger>(SpeedProperty->Get())
		: nullptr;
	const TObjectPtr<UConfigProperty>* PulseProperty =
		Root->SectionProperties.Find(TEXT("ShowCraftCompletionPulse"));
	const UConfigPropertyBool* CompletionPulse = PulseProperty
		? Cast<UConfigPropertyBool>(PulseProperty->Get())
		: nullptr;
	const TObjectPtr<UConfigProperty>* ScreenProperty =
		Root->SectionProperties.Find(TEXT("UseExactCraftScreen"));
	const UConfigPropertyBool* ExactCraftScreen = ScreenProperty
		? Cast<UConfigPropertyBool>(ScreenProperty->Get())
		: nullptr;
	if (!Speed || !CompletionPulse || !ExactCraftScreen) return;

	const int32 CurrentSpeed = FMath::Clamp(Speed->Value, 1, 20);
	const int8 CurrentCompletionPulse = CompletionPulse->Value ? 1 : 0;
	const int8 CurrentExactCraftScreen = ExactCraftScreen->Value ? 1 : 0;
	if (LastObservedSpeed == INDEX_NONE || LastObservedCompletionPulse < 0 ||
		LastObservedExactCraftScreen < 0)
	{
		LastObservedSpeed = CurrentSpeed;
		LastObservedCompletionPulse = CurrentCompletionPulse;
		LastObservedExactCraftScreen = CurrentExactCraftScreen;
		return;
	}
	if (CurrentSpeed == LastObservedSpeed &&
		CurrentCompletionPulse == LastObservedCompletionPulse &&
		CurrentExactCraftScreen == LastObservedExactCraftScreen) return;

	LastObservedSpeed = CurrentSpeed;
	LastObservedCompletionPulse = CurrentCompletionPulse;
	LastObservedExactCraftScreen = CurrentExactCraftScreen;
	ConfigManager->MarkConfigurationDirty(ConfigId);
}

float FExactCraftConfigurationStruct::GetCraftingSpeedMultiplier(const UObject* WorldContext)
{
	FExactCraftConfigurationStruct Config;
	const UWorld* World = GEngine && WorldContext
		? GEngine->GetWorldFromContextObject(
			WorldContext,
			EGetWorldErrorMode::ReturnNull)
		: nullptr;
	if (UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr)
	{
		if (UConfigManager* ConfigManager = GameInstance->GetSubsystem<UConfigManager>())
		{
			static const FConfigId ConfigId{TEXT("ExactCraft"), TEXT("")};
			// Read the active property itself so changes made in the SML mod menu
			// take effect immediately. FillConfigurationStruct may return its
			// previously cached copy until SML has processed the editor's dirty
			// notification, which made the last-selected speed appear stuck.
			if (UConfigPropertySection* Root =
				ConfigManager->GetConfigurationRootSection(ConfigId))
			{
				if (const TObjectPtr<UConfigProperty>* Property =
					Root->SectionProperties.Find(TEXT("CraftingSpeedMultiplier")))
				{
					if (const UConfigPropertyInteger* Speed =
						Cast<UConfigPropertyInteger>(Property->Get()))
					{
						return static_cast<float>(FMath::Clamp(Speed->Value, 1, 20));
					}
				}
			}

			// Retain the reflected-struct route as a defensive fallback.
			ConfigManager->FillConfigurationStruct(
				ConfigId,
				FDynamicStructInfo{StaticStruct(), &Config});
		}
	}

	return static_cast<float>(FMath::Clamp(Config.CraftingSpeedMultiplier, 1, 20));
}

bool FExactCraftConfigurationStruct::ShouldShowCraftCompletionPulse(const UObject* WorldContext)
{
	FExactCraftConfigurationStruct Config;
	const UWorld* World = GEngine && WorldContext
		? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	if (UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr)
	{
		if (UConfigManager* ConfigManager = GameInstance->GetSubsystem<UConfigManager>())
		{
			static const FConfigId ConfigId{TEXT("ExactCraft"), TEXT("")};
			if (UConfigPropertySection* Root = ConfigManager->GetConfigurationRootSection(ConfigId))
			{
				if (const TObjectPtr<UConfigProperty>* Property =
					Root->SectionProperties.Find(TEXT("ShowCraftCompletionPulse")))
				{
					if (const UConfigPropertyBool* CompletionPulse =
						Cast<UConfigPropertyBool>(Property->Get()))
					{
						return CompletionPulse->Value;
					}
				}
			}

			ConfigManager->FillConfigurationStruct(
				ConfigId,
				FDynamicStructInfo{StaticStruct(), &Config});
		}
	}
	return Config.ShowCraftCompletionPulse;
}

bool FExactCraftConfigurationStruct::ShouldUseExactCraftScreen(const UObject* WorldContext)
{
	FExactCraftConfigurationStruct Config;
	const UWorld* World = GEngine && WorldContext
		? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	if (UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr)
	{
		if (UConfigManager* ConfigManager = GameInstance->GetSubsystem<UConfigManager>())
		{
			static const FConfigId ConfigId{TEXT("ExactCraft"), TEXT("")};
			if (UConfigPropertySection* Root = ConfigManager->GetConfigurationRootSection(ConfigId))
			{
				if (const TObjectPtr<UConfigProperty>* Property =
					Root->SectionProperties.Find(TEXT("UseExactCraftScreen")))
				{
					if (const UConfigPropertyBool* ExactCraftScreen =
						Cast<UConfigPropertyBool>(Property->Get()))
					{
						return ExactCraftScreen->Value;
					}
				}
			}

			ConfigManager->FillConfigurationStruct(
				ConfigId,
				FDynamicStructInfo{StaticStruct(), &Config});
		}
	}
	return Config.UseExactCraftScreen;
}

#undef LOCTEXT_NAMESPACE
