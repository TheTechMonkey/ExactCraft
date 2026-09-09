#pragma once

#include "Configuration/ModConfiguration.h"
#include "Configuration/Properties/WidgetExtension/CP_Bool.h"
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
	virtual void Deinitialize() override;

private:
	void StartPersistenceTimer(class FTimerManager* TimerManager);
	void PollForConfigurationChanges();

	FTimerHandle PersistenceTimer;
	int32 LastObservedSpeed = INDEX_NONE;
	int8 LastObservedCompletionPulse = -1;
	int8 LastObservedExactCraftScreen = -1;
};

USTRUCT(BlueprintType)
struct EXACTCRAFT_API FExactCraftConfigurationStruct
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite)
	int32 CraftingSpeedMultiplier = 1;

	UPROPERTY(BlueprintReadWrite)
	bool ShowCraftCompletionPulse = true;

	UPROPERTY(BlueprintReadWrite)
	bool UseExactCraftScreen = true;

	static float GetCraftingSpeedMultiplier(const UObject* WorldContext);
	static bool ShouldShowCraftCompletionPulse(const UObject* WorldContext);
	static bool ShouldUseExactCraftScreen(const UObject* WorldContext);
};
