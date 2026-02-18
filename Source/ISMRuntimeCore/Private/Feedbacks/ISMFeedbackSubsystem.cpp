// ISMFeedbackSubsystem.cpp

#include "Feedbacks/ISMFeedbackSubsystem.h"
#include "DrawDebugHelpers.h"
#include "Logging/LogMacros.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogISMFeedback, Log, All);

// ===== Subsystem Lifecycle =====

void UISMFeedbackSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    
    UE_LOG(LogISMFeedback, Log, TEXT("ISM Feedback Subsystem initialized"));
    
    // Reset statistics
    ResetStats();
}

void UISMFeedbackSubsystem::Deinitialize()
{
    // Notify all providers they're being unregistered
    for (const TScriptInterface<IISMFeedbackInterface>& Provider : FeedbackProviders)
    {
        if (Provider.GetObject() && Provider.GetObject()->Implements<UISMFeedbackInterface>())
        {
            IISMFeedbackInterface::Execute_OnFeedbackProviderUnregistered(Provider.GetObject());
        }
    }
    
    FeedbackProviders.Empty();
    ProviderObjects.Empty();
    FeedbackQueue.Empty();
    
    UE_LOG(LogISMFeedback, Log, TEXT("ISM Feedback Subsystem deinitialized"));
    
    Super::Deinitialize();
}

bool UISMFeedbackSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
    // Support Game, PIE, and Editor Preview worlds
    return WorldType == EWorldType::Game || 
           WorldType == EWorldType::PIE || 
           WorldType == EWorldType::GamePreview;
}

void UISMFeedbackSubsystem::Tick(float DeltaTime)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(UISMFeedbackSubsystem::Tick);
    
    // Update frame counter
    CurrentFrame++;
    
    // Reset per-frame stats
    CachedStats.RequestsThisFrame = 0;
    
    // Process batched feedback if enabled
    if (bEnableBatching && FeedbackQueue.Num() > 0)
    {
        ProcessFeedbackQueue();
    }
    
    // Periodically clean up invalid providers
    if (CurrentFrame % 100 == 0) // Every 100 frames (~1.67 seconds at 60fps)
    {
        CleanupInvalidProviders();
    }
}

TStatId UISMFeedbackSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UISMFeedbackSubsystem, STATGROUP_Tickables);
}

// ===== Provider Registration =====

bool UISMFeedbackSubsystem::RegisterFeedbackProvider(TScriptInterface<IISMFeedbackInterface> Provider)
{
    if (!Provider.GetObject())
    {
        UE_LOG(LogISMFeedback, Warning, TEXT("RegisterFeedbackProvider: Null provider"));
        return false;
    }
    
    if (!Provider.GetObject()->Implements<UISMFeedbackInterface>())
    {
        UE_LOG(LogISMFeedback, Warning, TEXT("RegisterFeedbackProvider: Object does not implement IISMFeedbackInterface"));
        return false;
    }
    
    // Check if already registered
    if (FeedbackProviders.Contains(Provider))
    {
        UE_LOG(LogISMFeedback, Warning, TEXT("RegisterFeedbackProvider: Provider already registered"));
        return false;
    }
    
    // Add to arrays
    FeedbackProviders.Add(Provider);
    ProviderObjects.Add(Provider.GetObject());
    
    // Re-sort by priority
    SortProvidersByPriority();
    
    // Notify provider
    IISMFeedbackInterface::Execute_OnFeedbackProviderRegistered(Provider.GetObject());
    
    // Update stats
    CachedStats.RegisteredProviders = FeedbackProviders.Num();
    
    UE_LOG(LogISMFeedback, Log, TEXT("Registered feedback provider: %s (Priority: %d)"), 
        *Provider.GetObject()->GetName(),
        IISMFeedbackInterface::Execute_GetFeedbackPriority(Provider.GetObject()));
    
    return true;
}

void UISMFeedbackSubsystem::UnregisterFeedbackProvider(TScriptInterface<IISMFeedbackInterface> Provider)
{
    if (!Provider.GetObject())
    {
        return;
    }
    
    int32 Index = FeedbackProviders.Find(Provider);
    if (Index != INDEX_NONE)
    {
        // Notify provider
        if (Provider.GetObject()->Implements<UISMFeedbackInterface>())
        {
            IISMFeedbackInterface::Execute_OnFeedbackProviderUnregistered(Provider.GetObject());
        }
        
        // Remove from arrays
        FeedbackProviders.RemoveAt(Index);
        ProviderObjects.RemoveAt(Index);
        
        // Update stats
        CachedStats.RegisteredProviders = FeedbackProviders.Num();
        
        UE_LOG(LogISMFeedback, Log, TEXT("Unregistered feedback provider: %s"), 
            *Provider.GetObject()->GetName());
    }
}

TArray<TScriptInterface<IISMFeedbackInterface>> UISMFeedbackSubsystem::GetRegisteredProviders() const
{
    return FeedbackProviders;
}

bool UISMFeedbackSubsystem::IsProviderRegistered(TScriptInterface<IISMFeedbackInterface> Provider) const
{
    return FeedbackProviders.Contains(Provider);
}

void UISMFeedbackSubsystem::SortProvidersByPriority()
{
    FeedbackProviders.Sort([](const TScriptInterface<IISMFeedbackInterface>& A, const TScriptInterface<IISMFeedbackInterface>& B)
    {
        if (!A.GetObject() || !B.GetObject())
        {
            return false;
        }
        
        int32 PriorityA = IISMFeedbackInterface::Execute_GetFeedbackPriority(A.GetObject());
        int32 PriorityB = IISMFeedbackInterface::Execute_GetFeedbackPriority(B.GetObject());
        
        // Higher priority first
        return PriorityA > PriorityB;
    });
}

void UISMFeedbackSubsystem::CleanupInvalidProviders()
{
    int32 RemovedCount = 0;
    
    for (int32 i = ProviderObjects.Num() - 1; i >= 0; i--)
    {
        if (!ProviderObjects[i].IsValid())
        {
            FeedbackProviders.RemoveAt(i);
            ProviderObjects.RemoveAt(i);
            RemovedCount++;
        }
    }
    
    if (RemovedCount > 0)
    {
        CachedStats.RegisteredProviders = FeedbackProviders.Num();
        UE_LOG(LogISMFeedback, Log, TEXT("Cleaned up %d invalid provider(s)"), RemovedCount);
    }
}

// ===== Feedback Requests =====

bool UISMFeedbackSubsystem::RequestFeedback(const FISMFeedbackContext& Context)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(UISMFeedbackSubsystem::RequestFeedback);
    
    // Validate context
    if (!Context.IsValid())
    {
        UE_LOG(LogISMFeedback, Warning, TEXT("RequestFeedback: Invalid context (missing FeedbackTag)"));
        return false;
    }
    
    // Update stats
    CachedStats.TotalRequests++;
    CachedStats.RequestsThisFrame++;
    
    // Draw debug visualization if enabled
    if (bDebugDrawFeedback)
    {
        DebugDrawFeedback(Context);
    }
    
    // Route to providers
    const double StartTime = FPlatformTime::Seconds();
    const bool bHandled = RouteToProviders(Context);
    const double EndTime = FPlatformTime::Seconds();
    
    // Update processing time stats
    const double ProcessingTimeMs = (EndTime - StartTime) * 1000.0;
    ProcessingTimeAccumulator += ProcessingTimeMs;
    ProcessingTimeSamples++;
    CachedStats.AverageProcessingTimeMs = static_cast<float>(ProcessingTimeAccumulator / ProcessingTimeSamples);
    
    // Update handled/unhandled stats
    if (bHandled)
    {
        CachedStats.HandledRequests++;
    }
    else
    {
        CachedStats.UnhandledRequests++;
        
        if (bLogUnhandledFeedback)
        {
            UE_LOG(LogISMFeedback, Warning, TEXT("Unhandled feedback: %s at %s"), 
                *Context.FeedbackTag.ToString(),
                *Context.Location.ToString());
        }
    }
    
    return bHandled;
}

bool UISMFeedbackSubsystem::RequestFeedbackBatched(const FISMFeedbackContext& Context, bool bAllowBatching)
{
    // If batching disabled or not allowed, process immediately
    if (!bEnableBatching || !bAllowBatching)
    {
        return RequestFeedback(Context);
    }
    
    // Add to queue
    FeedbackQueue.Add(Context);
    
    return true;
}

void UISMFeedbackSubsystem::RequestMultipleFeedback(const TArray<FISMFeedbackContext>& Contexts)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(UISMFeedbackSubsystem::RequestMultipleFeedback);
    
    if (bEnableBatching)
    {
        // Add all to queue for batch processing
        FeedbackQueue.Append(Contexts);
    }
    else
    {
        // Process immediately
        for (const FISMFeedbackContext& Context : Contexts)
        {
            RequestFeedback(Context);
        }
    }
}

// ===== Internal Processing =====

bool UISMFeedbackSubsystem::RouteToProviders(const FISMFeedbackContext& Context)
{
    bool bWasHandled = false;
    
    for (const TScriptInterface<IISMFeedbackInterface>& Provider : FeedbackProviders)
    {
        if (!Provider.GetObject() || !Provider.GetObject()->Implements<UISMFeedbackInterface>())
        {
            continue;
        }
        
        // Early out if provider can't handle this tag
        if (!IISMFeedbackInterface::Execute_CanHandleFeedbackTag(Provider.GetObject(), Context.FeedbackTag))
        {
            continue;
        }
        
        // Try to handle feedback
        const bool bHandled = IISMFeedbackInterface::Execute_HandleFeedback(Provider.GetObject(), Context);
        
        if (bHandled)
        {
            bWasHandled = true;
            
            // If not broadcasting to all, stop at first handler
            if (!bBroadcastToAll)
            {
                break;
            }
        }
    }
    
    return bWasHandled;
}

void UISMFeedbackSubsystem::ProcessFeedbackQueue()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(UISMFeedbackSubsystem::ProcessFeedbackQueue);
    
    if (FeedbackQueue.Num() == 0)
    {
        return;
    }
    
    // Determine how many to process this frame
    int32 NumToProcess = FeedbackQueue.Num();
    if (MaxFeedbackPerFrame > 0)
    {
        NumToProcess = FMath::Min(NumToProcess, MaxFeedbackPerFrame);
    }
    
    // Process feedback
    for (int32 i = 0; i < NumToProcess; i++)
    {
        RequestFeedback(FeedbackQueue[i]);
    }
    
    // Remove processed feedback
    FeedbackQueue.RemoveAt(0, NumToProcess);
}

void UISMFeedbackSubsystem::DebugDrawFeedback(const FISMFeedbackContext& Context)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }
    
    // Draw sphere at location
    FColor DrawColor = FColor::Green;
    
    // Color based on message type
    switch (Context.FeedbackMessageType)
    {
        case EISMFeedbackMessageType::ONE_SHOT:
            DrawColor = FColor::Green;
            break;
        case EISMFeedbackMessageType::STARTED:
            DrawColor = FColor::Blue;
            break;
        case EISMFeedbackMessageType::UPDATED:
            DrawColor = FColor::Yellow;
            break;
        case EISMFeedbackMessageType::COMPLETED:
            DrawColor = FColor::Cyan;
            break;
        case EISMFeedbackMessageType::INTERRUPTED:
            DrawColor = FColor::Red;
            break;
    }
    
    // Draw sphere
    const float SphereRadius = 25.0f * Context.Scale;
    DrawDebugSphere(World, Context.Location, SphereRadius, 8, DrawColor, false, DebugDrawDuration, 0, 2.0f);
    
    // Draw normal arrow
    if (!Context.Normal.IsNearlyZero())
    {
        const FVector ArrowEnd = Context.Location + (Context.Normal * 50.0f * Context.Scale);
        DrawDebugDirectionalArrow(World, Context.Location, ArrowEnd, 10.0f, DrawColor, false, DebugDrawDuration, 0, 2.0f);
    }
    
    // Draw velocity arrow
    if (!Context.Velocity.IsNearlyZero())
    {
        const FVector VelocityEnd = Context.Location + (Context.Velocity * 0.1f); // Scale down velocity for visibility
        DrawDebugDirectionalArrow(World, Context.Location, VelocityEnd, 10.0f, FColor::Orange, false, DebugDrawDuration, 0, 1.0f);
    }
    
    // Draw text label
    FString DebugText = FString::Printf(TEXT("%s\nIntensity: %.2f"), 
        *Context.FeedbackTag.ToString(), 
        Context.Intensity);
    
    DrawDebugString(World, Context.Location + FVector(0, 0, 50), DebugText, nullptr, DrawColor, DebugDrawDuration, true);
}

// ===== Statistics =====

void UISMFeedbackSubsystem::ResetStats()
{
    CachedStats.RequestsThisFrame = 0;
    CachedStats.TotalRequests = 0;
    CachedStats.RegisteredProviders = FeedbackProviders.Num();
    CachedStats.HandledRequests = 0;
    CachedStats.UnhandledRequests = 0;
    CachedStats.AverageProcessingTimeMs = 0.0f;
    
    ProcessingTimeAccumulator = 0.0;
    ProcessingTimeSamples = 0;
}