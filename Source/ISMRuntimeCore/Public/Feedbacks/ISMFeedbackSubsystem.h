// ISMFeedbackSubsystem.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "Feedbacks/ISMFeedbackContext.h"
#include "Feedbacks/ISMFeedbackInterface.h"
#include "ISMFeedbackSubsystem.generated.h"

/**
 * Statistics for feedback system monitoring
 */
USTRUCT(BlueprintType)
struct FISMFeedbackStats
{
    GENERATED_BODY()
    
    /** Total number of feedback requests this frame */
    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    int32 RequestsThisFrame = 0;
    
    /** Total number of feedback requests since subsystem initialization */
    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    int32 TotalRequests = 0;
    
    /** Number of registered feedback providers */
    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    int32 RegisteredProviders = 0;
    
    /** Number of requests that were handled by at least one provider */
    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    int32 HandledRequests = 0;
    
    /** Number of requests that were not handled by any provider */
    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    int32 UnhandledRequests = 0;
    
    /** Average time spent processing feedback requests (milliseconds) */
    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    float AverageProcessingTimeMs = 0.0f;
};

/**
 * World subsystem that routes feedback requests to registered providers.
 * 
 * Design Philosophy:
 * - Central routing system for all audio/VFX feedback
 * - Decouples ISM modules from specific audio/VFX implementations
 * - Supports multiple feedback providers simultaneously
 * - Priority-based provider ordering
 * - Optional feedback batching for performance
 * - Comprehensive debugging and statistics
 * 
 * Usage Pattern:
 * 
 *   // In your ISM module (Physics, Destruction, Resources, etc.):
 *   FISMFeedbackContext Context;
 *   Context.FeedbackTag = FGameplayTag::RequestGameplayTag("Feedback.Impact.Wood");
 *   Context.Location = ImpactPoint;
 *   Context.Intensity = ImpactForce / MaxForce;
 *   
 *   if (UISMFeedbackSubsystem* FeedbackSystem = GetWorld()->GetSubsystem<UISMFeedbackSubsystem>())
 *   {
 *       FeedbackSystem->RequestFeedback(Context);
 *   }
 * 
 * Provider Registration:
 * 
 *   // In your audio/VFX manager component:
 *   virtual void BeginPlay() override
 *   {
 *       Super::BeginPlay();
 *       
 *       if (UISMFeedbackSubsystem* FeedbackSystem = GetWorld()->GetSubsystem<UISMFeedbackSubsystem>())
 *       {
 *           FeedbackSystem->RegisterFeedbackProvider(this);
 *       }
 *   }
 */
UCLASS()
class ISMRUNTIMECORE_API UISMFeedbackSubsystem : public UWorldSubsystem, public FTickableGameObject
{
    GENERATED_BODY()
    
public:
    // ===== Subsystem Lifecycle =====
    
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    
    // ===== Provider Registration =====
    
    /**
     * Register a feedback provider.
     * Providers are sorted by priority (high to low).
     * 
     * @param Provider - Object implementing IISMFeedbackInterface
     * @return True if registration succeeded
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Feedback")
    bool RegisterFeedbackProvider(TScriptInterface<IISMFeedbackInterface> Provider);
    
    /**
     * Unregister a feedback provider.
     * 
     * @param Provider - Object to unregister
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Feedback")
    void UnregisterFeedbackProvider(TScriptInterface<IISMFeedbackInterface> Provider);
    
    /**
     * Get all registered feedback providers.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Feedback")
    TArray<TScriptInterface<IISMFeedbackInterface>> GetRegisteredProviders() const;
    
    /**
     * Check if a specific provider is registered.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Feedback")
    bool IsProviderRegistered(TScriptInterface<IISMFeedbackInterface> Provider) const;
    
    // ===== Feedback Requests =====
    
    /**
     * Request feedback immediately.
     * Routes to all registered providers until one handles it (or all if bBroadcastToAll is true).
     * 
     * @param Context - Complete feedback context
     * @return True if at least one provider handled the feedback
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Feedback")
    bool RequestFeedback(const FISMFeedbackContext& Context);
    
    /**
     * Request feedback with optional batching.
     * If batching is enabled, feedback is queued and processed later.
     * 
     * @param Context - Complete feedback context
     * @param bAllowBatching - Whether this request can be batched (default true)
     * @return True if request was accepted (doesn't guarantee handling)
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Feedback")
    bool RequestFeedbackBatched(const FISMFeedbackContext& Context, bool bAllowBatching = true);
    
    /**
     * Request multiple feedback events efficiently.
     * Useful for batch destruction or large-scale events.
     * 
     * @param Contexts - Array of feedback contexts to process
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Feedback")
    void RequestMultipleFeedback(const TArray<FISMFeedbackContext>& Contexts);
    
    // ===== Configuration =====
    
    /**
     * Enable or disable feedback batching.
     * Batching can improve performance for many simultaneous events.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Feedback|Performance")
    bool bEnableBatching = true;
    
    /**
     * Maximum number of feedback requests to process per frame.
     * -1 = unlimited (process all)
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Feedback|Performance", meta=(EditCondition="bEnableBatching"))
    int32 MaxFeedbackPerFrame = -1;
    
    /**
     * Whether to broadcast feedback to ALL providers (true) or stop at first handler (false).
     * True = All providers get the feedback (good for audio + VFX + analytics)
     * False = Stop at first provider that returns true (more efficient)
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Feedback|Behavior")
    bool bBroadcastToAll = true;
    
    /**
     * Whether to log unhandled feedback requests.
     * Useful for debugging missing audio/VFX.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Feedback|Debug")
    bool bLogUnhandledFeedback = false;
    
    /**
     * Whether to draw debug visualization for feedback requests.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Feedback|Debug")
    bool bDebugDrawFeedback = false;
    
    /**
     * Duration to display debug visualization (seconds).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Feedback|Debug", meta=(EditCondition="bDebugDrawFeedback"))
    float DebugDrawDuration = 2.0f;
    
    // ===== Statistics =====
    
    /**
     * Get current feedback statistics.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Feedback")
    FISMFeedbackStats GetFeedbackStats() const { return CachedStats; }
    
    /**
     * Reset statistics counters.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Feedback")
    void ResetStats();
    
protected:
    // ===== Provider Management =====
    
    /** Registered feedback providers, sorted by priority */
    TArray<TScriptInterface<IISMFeedbackInterface>> FeedbackProviders;
    
    /** Weak pointers for cleanup detection */
    TArray<TWeakObjectPtr<UObject>> ProviderObjects;
    
    /** Sort providers by priority */
    void SortProvidersByPriority();
    
    /** Clean up invalid provider references */
    void CleanupInvalidProviders();
    
    // ===== Batching =====
    
    /** Queued feedback requests (when batching enabled) */
    TArray<FISMFeedbackContext> FeedbackQueue;
    
    /** Process queued feedback requests */
    void ProcessFeedbackQueue();
    
    // ===== Internal Processing =====
    
    /** Route a single feedback request to providers */
    bool RouteToProviders(const FISMFeedbackContext& Context);
    
    /** Draw debug visualization for a feedback request */
    void DebugDrawFeedback(const FISMFeedbackContext& Context);
    
    // ===== Statistics =====
    
    /** Cached statistics */
    UPROPERTY(Transient)
    FISMFeedbackStats CachedStats;
    
    /** Frame number for per-frame stat tracking */
    uint64 CurrentFrame = 0;
    
    /** Processing time accumulator for averaging */
    double ProcessingTimeAccumulator = 0.0;
    
    /** Number of samples for averaging */
    int32 ProcessingTimeSamples = 0;
};