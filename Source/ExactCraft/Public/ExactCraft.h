#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogExactCraft, Log, All);

class FExactCraftModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    TSharedPtr<class IInputProcessor> InputProcessor;
    FTSTicker::FDelegateHandle RequestTickerHandle;
};
