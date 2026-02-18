#pragma once

#include "Modules/ModuleManager.h"

class FISMRuntimeFeedbacks : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};
