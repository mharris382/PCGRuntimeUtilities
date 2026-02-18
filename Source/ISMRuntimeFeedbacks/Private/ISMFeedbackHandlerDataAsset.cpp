// ISMFeedbackHandlerDataAsset.cpp
#include "ISMFeedbackHandlerDataAsset.h"
#include "Logging/LogMacros.h"
#include "ISMFeedbackHandler.h"

//DEFINE_LOG_CATEGORY_STATIC(LogTemp, Log, All);
//DEFINE_LOG_CATEGORY_STATIC(LogISMFeedbackDatabase, Log, All);


namespace
{
    void LogISMFeedbackDatabase(FString Msg, bool isDBCategory)
    {
        if (isDBCategory)
        {
            UE_LOG(LogTemp, Log, TEXT("%s"), *Msg);
        }
        else
        {
            UE_LOG(LogTemp, Log, TEXT("%s"), *Msg);
        }
    }
}
// ===== Handler Database =====

UISMFeedbackHandler* UISMFeedbackHandlerDataAsset::FindHandler(FGameplayTag FeedbackTag) const
{
    if (!FeedbackTag.IsValid())
    {
        return nullptr;
    }

    // Search HandlerDB for matching tag
    for (const FTagHandlerEntry& Entry : HandlerDB)
    {
        if (Entry.Tag == FeedbackTag)
        {
            return Entry.Handler;
        }
    }

    return nullptr;
}

bool UISMFeedbackHandlerDataAsset::ExecuteFeedback(
    FGameplayTag FeedbackTag,
    const FISMFeedbackContext& Context,
    UObject* WorldContext)
{
    // Find handler
    UISMFeedbackHandler* Handler = FindHandler(FeedbackTag);
    if (!Handler)
    {
		LogISMFeedbackDatabase(FString::Printf(TEXT("ExecuteFeedback: No handler found for tag '%s' in database '%s'"), *FeedbackTag.ToString(), *DatabaseName.ToString()), true);
       //UE_LOG(LogISMFeedbackDatabase, Verbose,
       //    TEXT("ExecuteFeedback: No handler found for tag '%s'"),
       //    *FeedbackTag.ToString());
        return false;
    }

    // Execute handler
    return Handler->Execute(Context, WorldContext);
}

void UISMFeedbackHandlerDataAsset::PreloadAllHandlers()
{
	LogISMFeedbackDatabase(FString::Printf(TEXT("Preloading all handlers in database '%s' (%d entries)"), *DatabaseName.ToString(), HandlerDB.Num()), true);
    //UE_LOG(LogISMFeedbackDatabase, Log,
    //    TEXT("Preloading all handlers in database '%s' (%d entries)"),
    //    *DatabaseName.ToString(),
    //    HandlerDB.Num());

    int32 PreloadedCount = 0;

    // Preload all handlers
    for (const FTagHandlerEntry& Entry : HandlerDB)
    {
        if (Entry.Handler)
        {
            Entry.Handler->PreloadAssets();
            PreloadedCount++;
        }
    }

	LogISMFeedbackDatabase(FString::Printf(TEXT("Preloaded %d handlers in database '%s'"), PreloadedCount, *DatabaseName.ToString()), true);
    //UE_LOG(LogISMFeedbackDatabase, Log,
    //    TEXT("Preloaded %d handlers"),
    //    PreloadedCount);
}




// ===== Composite Handler: Multi Handler =====

bool UISMFeedbackMultiHandler::Execute_Implementation(const FISMFeedbackContext& Context, UObject* WorldContext)
{
    if (Handlers.Num() == 0)
    {
		LogISMFeedbackDatabase(FString::Printf(TEXT("MultiHandler: No child handlers configured in database '%s'"), *Context.FeedbackTag.ToString()), false);
        return false;
    }

    bool bAnySuccess = false;

    // Execute all child handlers
    for (UISMFeedbackHandler* Handler : Handlers)
    {
        if (Handler)
        {
            bool bSuccess = Handler->Execute(Context, WorldContext);
            bAnySuccess = bAnySuccess || bSuccess;
        }
    }

    return bAnySuccess;
}

void UISMFeedbackMultiHandler::PreloadAssets_Implementation()
{
    // Preload all child handlers
    for (UISMFeedbackHandler* Handler : Handlers)
    {
        if (Handler)
        {
            Handler->PreloadAssets();
        }
    }
}