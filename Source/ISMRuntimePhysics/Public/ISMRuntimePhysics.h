#pragma once

#include "Modules/ModuleManager.h"

class FISMRuntimePhysics : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};
