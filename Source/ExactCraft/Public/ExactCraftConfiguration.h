#pragma once

#include "Configuration/ModConfiguration.h"
#include "Configuration/Properties/WidgetExtension/CP_Integer.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ExactCraftConfiguration.generated.h"

UCLASS()
class EXACTCRAFT_API UExactCraftConfiguration : public UModConfiguration
{
	GENERATED_BODY()

public:
	UExactCraftConfiguration();
};

UCLASS()
class EXACTCRAFT_API UExactCraftConfigurationRegistrar : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
};

USTRUCT(BlueprintType)
struct EXACTCRAFT_API FExactCraftConfigurationStruct
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite)
	int32 CraftingSpeedMultiplier = 1;

	static float GetCraftingSpeedMultiplier(const UObject* WorldContext);
};
