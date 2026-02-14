
#pragma once

#include "Modules/ModuleManager.h"

class FISMRuntimeCoreModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;


protected:
	void LoadGameplayTags();
};
