#include "ExactCraftConfiguration.h"

#include "Configuration/ConfigManager.h"
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
	static ConstructorHelpers::FClassFinder<UCP_Float> FloatPropertyClass(
		TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertyFloat"));
	check(FloatPropertyClass.Succeeded());
	UCP_Float* Speed = CastChecked<UCP_Float>(CreateDefaultSubobject(
		TEXT("CraftingSpeedMultiplier"),
		UCP_Float::StaticClass(),
		FloatPropertyClass.Class,
		true,
		false));
	Speed->DisplayName = LOCTEXT("SpeedName", "Manual crafting speed multiplier");
	Speed->Tooltip = LOCTEXT(
		"SpeedTooltip",
		"Drag the bar or enter a number. 1x is vanilla speed and 20x is the maximum.");
	Speed->DefaultValue = 1.0f;
	Speed->Value = 1.0f;
	Speed->WidgetType = ECP_FloatWidgetType::CPF_Slider;
	Speed->MinValue = 1.0f;
	Speed->MaxValue = 20.0f;
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
			ConfigManager->FillConfigurationStruct(
				ConfigId,
				FDynamicStructInfo{StaticStruct(), &Config});
		}
	}

	return FMath::Clamp(Config.CraftingSpeedMultiplier, 1.0f, 20.0f);
}

#undef LOCTEXT_NAMESPACE
