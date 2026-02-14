// Copyright (c) 2025 Max Harris
// Published by Procedural Architect

#include "ISMRuntimeCore.h"
#include "GameplayTagsManager.h"
//#include "IPluginManager.h"

#define LOCTEXT_NAMESPACE "FISMRuntimeCoreModule"

void FISMRuntimeCoreModule::StartupModule()
{
    UE_LOG(LogTemp, Log, TEXT("ISMRuntimeCore: Module started"));

    LoadGameplayTags();
}

void FISMRuntimeCoreModule::ShutdownModule()
{
    UE_LOG(LogTemp, Log, TEXT("ISMRuntimeCore: Module shutdown"));
}

void FISMRuntimeCoreModule::LoadGameplayTags()
{
    // Get the plugin's config directory
    //FString PluginBaseDir = IPluginManager::Get().FindPlugin(TEXT("ISMRuntimeUtilities"))->GetBaseDir();
    //FString TagsConfigPath = FPaths::Combine(PluginBaseDir, TEXT("Config/Tags/ISMRuntimeTags.ini"));
    //
    //if (FPaths::FileExists(TagsConfigPath))
    //{
    //    UE_LOG(LogTemp, Log, TEXT("ISMRuntimeCore: Loading gameplay tags from %s"), *TagsConfigPath);
    //
    //    // Load the tags
    //    UGameplayTagsManager& TagManager = UGameplayTagsManager::Get();
    //    
    //    TagManager.LoadGameplayTagTables(TagsConfigPath);
    //
    //    UE_LOG(LogTemp, Log, TEXT("ISMRuntimeCore: Gameplay tags loaded successfully"));
    //}
    //else
    //{
    //    UE_LOG(LogTemp, Warning, TEXT("ISMRuntimeCore: Could not find tags config at %s"), *TagsConfigPath);
    //}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FISMRuntimeCoreModule, ISMRuntimeCore)