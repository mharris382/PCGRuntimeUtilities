// Copyright (c) 2025 Max Harris
// Published by Procedural Architect

#include "ISMRuntimeCore.h"

#define LOCTEXT_NAMESPACE "FISMRuntimeCoreModule"

void FISMRuntimeCoreModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
}

void FISMRuntimeCoreModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FISMRuntimeCoreModule, ISMRuntimeCore)