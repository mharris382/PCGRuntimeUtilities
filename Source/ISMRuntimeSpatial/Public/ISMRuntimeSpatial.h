#pragma once

#include "Modules/ModuleManager.h"

class FISMRuntimeSpatial : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};
