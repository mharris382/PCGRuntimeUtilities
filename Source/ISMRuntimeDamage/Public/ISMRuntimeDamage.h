#pragma once

#include "Modules/ModuleManager.h"

class FISMRuntimeDamage : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};
