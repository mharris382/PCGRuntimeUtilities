// ISMFeedbackSettings.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ISMFeedbackHandlerDataAsset.h"
#include "ISMFeedbackSettings.generated.h"

/**
 * Project settings for ISM Feedback system.
 * Configurable in: Project Settings > Plugins > ISM Feedback
 * 
 * Primary Setting:
 * - Handler Database: Soft object path to the top-level handler database
 * 
 * Setup:
 * 1. Create handler database data asset (DA_FeedbackHandlers)
 * 2. Assign it in project settings
 * 3. Provider subsystem loads it automatically
 * 4. Done!
 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="ISM Feedback"))
class ISMRUNTIMEFEEDBACKS_API UISMFeedbackSettings : public UDeveloperSettings
{
    GENERATED_BODY()
    
public:
    UISMFeedbackSettings();
    
    // ===== Handler Database =====
    
    /**
     * Top-level handler database.
     * This is the MAIN configuration for all feedback in your project.
     * 
     * Setup:
     * 1. Create data asset: Right-click > Miscellaneous > Data Asset > ISMFeedbackHandlerDataAsset
     * 2. Name it: DA_FeedbackHandlers (or similar)
     * 3. Configure handlers (see examples below)
     * 4. Assign here in project settings
     * 
     * The provider subsystem automatically loads this on world initialization.
     */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Database",
        meta=(AllowedClasses="/Script/ISMRuntimeFeedback.ISMFeedbackHandlerDataAsset"))
    TSoftObjectPtr<UISMFeedbackHandlerDataAsset> HandlerDatabase;
    
    /**
     * Priority for this provider.
     * Higher priority = called first if multiple feedback providers exist.
     * 
     * Default: 50 (standard priority)
     */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Provider")
    int32 ProviderPriority = 50;
    
    /**
     * Preload all handler assets on initialization.
     * 
     * True: No runtime hitches, higher memory usage
     * False: Lazy load on demand, lower memory, potential hitches
     * 
     * Recommendation:
     * - Development: false (faster iteration)
     * - Shipping: false (lower memory)
     * - Demo/Preview: true (no hitches)
     */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Performance")
    bool bPreloadAllHandlers = false;
    
    // ===== Debug Settings =====
    
    /**
     * Enable debug logging for feedback system.
     */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Debug")
    bool bEnableDebugLogging = false;
    
    /**
     * Log when feedback tags are not found in database.
     */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Debug")
    bool bLogUnhandledTags = true;
    
    // ===== Singleton Access =====
    
    static const UISMFeedbackSettings* Get()
    {
        return GetDefault<UISMFeedbackSettings>();
    }
    
    // ===== UDeveloperSettings Interface =====
    
    virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
    virtual FText GetSectionText() const override 
    { 
        return NSLOCTEXT("ISMFeedback", "SettingsSection", "ISM Feedback"); 
    }
    
#if WITH_EDITOR
    virtual FText GetSectionDescription() const override
    {
        return NSLOCTEXT("ISMFeedback", "SettingsDescription",
            "Configure the ISM Feedback system. Set the handler database to enable feedback routing.");
    }
#endif
};

/**
 * EXAMPLE HANDLER DATABASE SETUP:
 * 
 * Create: DA_FeedbackHandlers (ISMFeedbackHandlerDataAsset)
 * 
 * HandlerDB:
 *   [0] Level Up:
 *       Tag: "Feedback.UI.LevelUp"
 *       Handler: Audio
 *         Sound: S_LevelUp
 *   
 *   [1] Heavy Impact:
 *       Tag: "Feedback.Impact.Heavy"
 *       Handler: MultiHandler
 *         Handlers:
 *           - Audio (S_Impact_Heavy)
 *           - Niagara (NS_Impact_Heavy)
 *           - CameraShake (Shake_Heavy)
 *   
 *   [2] Footstep (Surface-Dependent):
 *       Tag: "Feedback.Movement.Footstep"
 *       Handler: SurfaceMatcherHandler
 *         SurfaceToTagMap:
 *           SurfaceType1 (Wood) → "Surface.Wood"
 *           SurfaceType2 (Metal) → "Surface.Metal"
 *           SurfaceType3 (Concrete) → "Surface.Concrete"
 *         HandlerDB:
 *           "Surface.Wood" → MultiHandler
 *             - Audio (S_Footstep_Wood)
 *             - Niagara (NS_Dust_Small)
 *           "Surface.Metal" → MultiHandler
 *             - Audio (S_Footstep_Metal)
 *             - Niagara (NS_Spark_Small)
 *           "Surface.Concrete" → MultiHandler
 *             - Audio (S_Footstep_Concrete)
 *             - Niagara (NS_Dust_Small)
 *         DefaultHandler: Audio (S_Footstep_Generic)
 * 
 * Then assign DA_FeedbackHandlers to this setting!
 */