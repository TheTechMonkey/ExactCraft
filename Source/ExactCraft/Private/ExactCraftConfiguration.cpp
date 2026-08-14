#include "ExactCraftConfiguration.h"

#include "Configuration/ConfigManager.h"
#include "Configuration/Properties/ConfigPropertyInteger.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "Configuration/Properties/WidgetExtension/CP_Section.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "ExactCraft"

UExactCraftConfiguration::UExactCraftConfiguration()
{
	ConfigId = {TEXT("ExactCraft"), TEXT("")};
	DisplayName = LOCTEXT("ConfigName", "Exact Craft");
	Description = LOCTEXT("ConfigDescription", "Controls optional manual-crafting speed.");

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
}

void UExactCraftConfigurationRegistrar::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<UConfigManager>();
	Super::Initialize(Collection);
	if (UConfigManager* ConfigManager = GetGameInstance()->GetSubsystem<UConfigManager>())
	{
		ConfigManager->RegisterModConfiguration(UExactCraftConfiguration::StaticClass());
	}
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

#undef LOCTEXT_NAMESPACE
