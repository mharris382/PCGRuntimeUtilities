// ISMFeedbackProvider.cpp
#include "ISMFeedbackProvider.h"
#include "ISMFeedbackSettings.h"
#include "Feedbacks/ISMFeedbackSubsystem.h"  // Note: Feedbacks subfolder
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogISMFeedbackProvider, Log, All);

// ===== Subsystem Lifecycle =====

void UISMFeedbackProvider::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    
    UE_LOG(LogISMFeedbackProvider, Log, TEXT("ISM Feedback Provider initializing..."));
    
    // Load handler database from project settings
    LoadDatabaseFromSettings();
    
    // Register with feedback subsystem
    RegisterWithFeedbackSubsystem();
    
    // Preload assets if configured
    const UISMFeedbackSettings* Settings = UISMFeedbackSettings::Get();
    if (Settings && Settings->bPreloadAllHandlers)
    {
        PreloadAllAssets();
    }
}

void UISMFeedbackProvider::Deinitialize()
{
    // Unregister from feedback subsystem
    UnregisterFromFeedbackSubsystem();
    
    // Clear database reference
    LoadedDatabase = nullptr;
    
    UE_LOG(LogISMFeedbackProvider, Log, TEXT("ISM Feedback Provider deinitialized"));
    
    Super::Deinitialize();
}

bool UISMFeedbackProvider::DoesSupportWorldType(EWorldType::Type WorldType) const
{
    // Support game worlds and PIE
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

// ===== IISMFeedbackInterface Implementation =====

bool UISMFeedbackProvider::HandleFeedback_Implementation(const FISMFeedbackContext& Context)
{
    const UISMFeedbackSettings* Settings = UISMFeedbackSettings::Get();
    
    // Early out if no database
    if (!LoadedDatabase)
    {
        if (Settings && Settings->bLogUnhandledTags)
        {
            UE_LOG(LogISMFeedbackProvider, Warning, 
                TEXT("HandleFeedback: No database loaded for tag '%s'"), 
                *Context.FeedbackTag.ToString());
        }
        return false;
    }
    
    // Find handler in database
    UISMFeedbackHandler* Handler = LoadedDatabase->FindHandler(Context.FeedbackTag);
    if (!Handler)
    {
        if (Settings && Settings->bLogUnhandledTags)
        {
            UE_LOG(LogISMFeedbackProvider, Verbose, 
                TEXT("HandleFeedback: No handler found for tag '%s'"), 
                *Context.FeedbackTag.ToString());
        }
        return false;
    }
    
    // Execute handler
    bool bSuccess = Handler->Execute(Context, GetWorld());
    
    if (Settings && Settings->bEnableDebugLogging)
    {
        UE_LOG(LogISMFeedbackProvider, Log, 
            TEXT("HandleFeedback: Tag '%s' %s"), 
            *Context.FeedbackTag.ToString(),
            bSuccess ? TEXT("executed successfully") : TEXT("failed to execute"));
    }
    
    return bSuccess;
}

bool UISMFeedbackProvider::CanHandleFeedbackTag_Implementation(FGameplayTag FeedbackTag) const
{
    if (!LoadedDatabase)
    {
        return false;
    }
    
    // Fast check: Does this tag exist in our database?
    return LoadedDatabase->FindHandler(FeedbackTag) != nullptr;
}

int32 UISMFeedbackProvider::GetFeedbackPriority_Implementation() const
{
    const UISMFeedbackSettings* Settings = UISMFeedbackSettings::Get();
    return Settings ? Settings->ProviderPriority : 50;
}

void UISMFeedbackProvider::OnFeedbackProviderRegistered_Implementation()
{
    UE_LOG(LogISMFeedbackProvider, Log, TEXT("Registered with ISMFeedbackSubsystem"));
    bIsRegistered = true;
}

void UISMFeedbackProvider::OnFeedbackProviderUnregistered_Implementation()
{
    UE_LOG(LogISMFeedbackProvider, Log, TEXT("Unregistered from ISMFeedbackSubsystem"));
    bIsRegistered = false;
}

// ===== Public API =====

void UISMFeedbackProvider::ReloadDatabase()
{
    UE_LOG(LogISMFeedbackProvider, Log, TEXT("Reloading handler database..."));
    LoadDatabaseFromSettings();
}

void UISMFeedbackProvider::PreloadAllAssets()
{
    if (!LoadedDatabase)
    {
        UE_LOG(LogISMFeedbackProvider, Warning, TEXT("PreloadAllAssets: No database loaded"));
        return;
    }
    
    UE_LOG(LogISMFeedbackProvider, Log, TEXT("Preloading all handler assets..."));
    LoadedDatabase->PreloadAllHandlers();
}

// ===== Helper Methods =====

void UISMFeedbackProvider::LoadDatabaseFromSettings()
{
    const UISMFeedbackSettings* Settings = UISMFeedbackSettings::Get();
    if (!Settings)
    {
        UE_LOG(LogISMFeedbackProvider, Error, TEXT("Failed to get ISMFeedbackSettings"));
        return;
    }
    
    // Get database path from settings
    TSoftObjectPtr<UISMFeedbackHandlerDataAsset> DatabasePtr = Settings->HandlerDatabase;
    if (DatabasePtr.IsNull())
    {
        UE_LOG(LogISMFeedbackProvider, Warning, 
            TEXT("No handler database configured in project settings (ISM Feedback > Handler Database)"));
        return;
    }
    
    // Load database
    LoadedDatabase = DatabasePtr.LoadSynchronous();
    if (!LoadedDatabase)
    {
        UE_LOG(LogISMFeedbackProvider, Error, 
            TEXT("Failed to load handler database: %s"), 
            *DatabasePtr.ToString());
        return;
    }
    
    UE_LOG(LogISMFeedbackProvider, Log, 
        TEXT("Loaded handler database: %s (%d entries)"),
        *LoadedDatabase->GetName(),
        LoadedDatabase->HandlerDB.Num());
}

void UISMFeedbackProvider::RegisterWithFeedbackSubsystem()
{
    UISMFeedbackSubsystem* FeedbackSubsystem = GetFeedbackSubsystem();
    if (!FeedbackSubsystem)
    {
        UE_LOG(LogISMFeedbackProvider, Warning, 
            TEXT("Failed to get ISMFeedbackSubsystem for registration"));
        return;
    }
    
    // Register as feedback provider
    bool bSuccess = FeedbackSubsystem->RegisterFeedbackProvider(this);
    if (bSuccess)
    {
        UE_LOG(LogISMFeedbackProvider, Log, TEXT("Registered with ISMFeedbackSubsystem"));
    }
    else
    {
        UE_LOG(LogISMFeedbackProvider, Warning, TEXT("Failed to register with ISMFeedbackSubsystem"));
    }
}

void UISMFeedbackProvider::UnregisterFromFeedbackSubsystem()
{
    if (!bIsRegistered)
    {
        return;
    }
    
    UISMFeedbackSubsystem* FeedbackSubsystem = GetFeedbackSubsystem();
    if (FeedbackSubsystem)
    {
        FeedbackSubsystem->UnregisterFeedbackProvider(this);
        UE_LOG(LogISMFeedbackProvider, Log, TEXT("Unregistered from ISMFeedbackSubsystem"));
    }
    
    bIsRegistered = false;
}

UISMFeedbackSubsystem* UISMFeedbackProvider::GetFeedbackSubsystem()
{
    // Return cached subsystem if valid
    if (CachedFeedbackSubsystem.IsValid())
    {
        return CachedFeedbackSubsystem.Get();
    }
    
    // Get subsystem from world
    UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }
    
    UISMFeedbackSubsystem* FeedbackSubsystem = World->GetSubsystem<UISMFeedbackSubsystem>();
    CachedFeedbackSubsystem = FeedbackSubsystem;
    
    return FeedbackSubsystem;
}