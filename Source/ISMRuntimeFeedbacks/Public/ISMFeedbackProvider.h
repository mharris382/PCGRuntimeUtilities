// ISMFeedbackProvider.h (TOP-LEVEL ENTRY POINT)
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Feedbacks/ISMFeedbackInterface.h"  // Note: Feedbacks subfolder
#include "Feedbacks/ISMFeedbackContext.h"     // Note: Feedbacks subfolder
#include "ISMFeedbackHandlerDataAsset.h"
#include "ISMFeedbackProvider.generated.h"

/**
 * World subsystem that provides feedback handling for ISMRuntimeFeedback module.
 * 
 * This is the TOP-LEVEL ENTRY POINT for the entire feedback implementation.
 * 
 * Responsibilities:
 * - Implements IISMFeedbackInterface (from Core)
 * - Auto-registers with ISMFeedbackSubsystem (from Core)
 * - Loads handler database from project settings
 * - Routes feedback requests to handler database
 * 
 * Architecture:
 *   ISMFeedbackSubsystem (Core)
 *   └─ Registers → ISMFeedbackProvider (Feedback module)
 *      └─ Uses → HandlerDatabase
 *         └─ Contains → Handlers (leaf or composite)
 *            └─ Execute feedback
 * 
 * Setup (Automatic):
 * 1. Configure project settings: ISM Feedback > Handler Database
 * 2. Create handler database data asset (DA_FeedbackHandlers)
 * 3. This subsystem initializes automatically on world creation
 * 4. Loads database from settings
 * 5. Registers with ISMFeedbackSubsystem
 * 6. Ready to handle feedback!
 */
UCLASS()
class ISMRUNTIMEFEEDBACKS_API UISMFeedbackProvider : public UWorldSubsystem, public IISMFeedbackInterface
{
    GENERATED_BODY()
    
public:
    // ===== Subsystem Lifecycle =====
    
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
    
    // ===== IISMFeedbackInterface Implementation =====
    
    /**
     * Handle feedback request.
     * 
     * Flow:
     * 1. Look up handler in database by Context.FeedbackTag
     * 2. If found, execute handler
     * 3. Handler may be leaf (audio, VFX) or composite (matcher)
     * 4. Composite handlers derive tags and delegate
     * 
     * @param Context - Feedback context with tag, location, intensity, etc.
     * @return True if handler found and executed
     */
    virtual bool HandleFeedback_Implementation(const FISMFeedbackContext& Context) override;
    
    /**
     * Check if we can handle this tag.
     * Fast early-out: Is this tag in our database?
     * 
     * @param FeedbackTag - Tag to check
     * @return True if tag exists in database
     */
    virtual bool CanHandleFeedbackTag_Implementation(FGameplayTag FeedbackTag) const override;
    
    /**
     * Provider priority (from project settings).
     * Higher priority providers are called first.
     */
    virtual int32 GetFeedbackPriority_Implementation() const override;
    
    /**
     * Called when registered with ISMFeedbackSubsystem.
     */
    virtual void OnFeedbackProviderRegistered_Implementation() override;
    
    /**
     * Called when unregistered from ISMFeedbackSubsystem.
     */
    virtual void OnFeedbackProviderUnregistered_Implementation() override;
    
    // ===== Public API =====
    
    /**
     * Get the loaded handler database.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Feedback")
    UISMFeedbackHandlerDataAsset* GetHandlerDatabase() const { return LoadedDatabase; }
    
    /**
     * Reload database from project settings.
     * Useful for hot-reload during development.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Feedback")
    void ReloadDatabase();
    
    /**
     * Preload all handler assets.
     * Call during loading screen to avoid runtime hitches.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Feedback")
    void PreloadAllAssets();
    
protected:
    // ===== Internal State =====
    
    /** Loaded handler database (from project settings) */
    UPROPERTY(Transient)
    TObjectPtr<UISMFeedbackHandlerDataAsset> LoadedDatabase = nullptr;
    
    /** Cached feedback subsystem reference */
    TWeakObjectPtr<class UISMFeedbackSubsystem> CachedFeedbackSubsystem;
    
    /** Whether we're currently registered */
    bool bIsRegistered = false;
    
    // ===== Helper Methods =====
    
    /**
     * Load handler database from project settings.
     */
    void LoadDatabaseFromSettings();
    
    /**
     * Register with ISMFeedbackSubsystem.
     */
    void RegisterWithFeedbackSubsystem();
    
    /**
     * Unregister from ISMFeedbackSubsystem.
     */
    void UnregisterFromFeedbackSubsystem();
    
    /**
     * Get feedback subsystem (cached).
     */
    class UISMFeedbackSubsystem* GetFeedbackSubsystem();
};