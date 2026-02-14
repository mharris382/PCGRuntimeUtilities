// ISMRuntimeActor.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/BoxComponent.h"
#include "ISMInstanceHandle.h"
#include "ISMRuntimeComponent.h"
#include "ISMRuntimeActor.generated.h"

/**
 * Mapping of ISM component to the runtime component class to use.
 * Allows different ISM components to use different runtime component types.
 */
USTRUCT(BlueprintType)
struct FISMComponentMapping
{
    GENERATED_BODY()
    
    /** The ISM component to manage (can be left null to auto-detect by index) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Runtime")
    UInstancedStaticMeshComponent* ISMComponent = nullptr;
    
    /** The runtime component class to use for this ISM */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Runtime")
    TSubclassOf<UISMRuntimeComponent> RuntimeComponentClass;
    
    /** Optional: Override spatial index cell size for this component */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Runtime", meta=(ClampMin="100.0"))
    float OverrideCellSize = 0.0f; // 0 = use default
};

/**
 * Actor that automatically creates and manages ISMRuntimeComponents for all ISM components.
 * Useful for quick setup and PCG integration.
 * 
 * Features:
 * - Auto-discovers ISM components on the actor
 * - Creates appropriate runtime components for each ISM
 * - Tracks total bounds for query optimization
 * - Provides box collision for PCG overlap queries
 * 
 * Usage:
 * 1. Create Blueprint subclass (e.g., BP_TreeCluster)
 * 2. Set DefaultRuntimeComponentClass
 * 3. Add ISM components in Blueprint
 * 4. Runtime components auto-created on BeginPlay
 */
UCLASS(Blueprintable, ClassGroup=(ISMRuntime))
class ISMRUNTIMECORE_API AISMRuntimeActor : public AActor
{
    GENERATED_BODY()
    
public:
    AISMRuntimeActor();
    
    // ===== Configuration =====
    
    /** 
     * Default runtime component class to use for all ISMs.
     * Can be overridden per-ISM in CustomMappings.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Runtime")
    TSubclassOf<UISMRuntimeComponent> DefaultRuntimeComponentClass;
    
    /**
     * Custom mappings for specific ISM components.
     * Overrides DefaultRuntimeComponentClass for specified ISMs.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Runtime")
    TArray<FISMComponentMapping> CustomMappings;
    
    /**
     * Whether to automatically create runtime components on construction (editor) or BeginPlay (runtime).
     * Disable if you want manual control.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Runtime")
    bool bAutoCreateRuntimeComponents = true;
    
    /**
     * Whether to update bounds automatically as instances are added/removed.
     * Enables query optimization but has small overhead.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Runtime")
    bool bAutoUpdateBounds = true;
    
    /**
     * Padding to add to auto-calculated bounds (in cm).
     * Useful to account for instance scales or future additions.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Runtime", meta=(ClampMin="0.0"))
    float BoundsPadding = 500.0f;
    
    // ===== Components =====
    
    /**
     * Box component for PCG overlap queries and bounds visualization.
     * Auto-sized to fit all instances.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ISM Runtime")
    UBoxComponent* BoundsBox;
    
    // ===== Lifecycle =====
    
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndReason) override;
    
#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
    
    // ===== Runtime Component Management =====
    
    /**
     * Create runtime components for all ISM components on this actor.
     * Called automatically if bAutoCreateRuntimeComponents is true.
     * @return Number of runtime components created
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
    int32 CreateRuntimeComponents();
    
    /**
     * Destroy all runtime components.
     * Useful for cleanup or recreation.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
    void DestroyRuntimeComponents();
    
    /**
     * Recreate all runtime components (destroy + create).
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
    void RecreateRuntimeComponents();
    
    /**
     * Get all runtime components on this actor.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
    TArray<UISMRuntimeComponent*> GetRuntimeComponents() const;
    
    /**
     * Get the runtime component managing a specific ISM component.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
    UISMRuntimeComponent* GetRuntimeComponentForISM(UInstancedStaticMeshComponent* ISMComponent) const;
    
    // ===== Bounds Management =====
    
    /**
     * Recalculate bounds to fit all instances from all ISM components.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
    void RecalculateBounds();
    
    /**
     * Get the current bounds (world space).
     */
    UFUNCTION(BlueprintPure, Category = "ISM Runtime")
    FBox GetWorldBounds() const { return CachedWorldBounds; }
    
    /**
     * Check if a location is within this actor's bounds.
     * Useful for query optimization.
     */
    UFUNCTION(BlueprintPure, Category = "ISM Runtime")
    bool IsLocationInBounds(const FVector& WorldLocation, float Tolerance = 0.0f) const;
    
    /**
     * Check if a sphere overlaps this actor's bounds.
     */
    UFUNCTION(BlueprintPure, Category = "ISM Runtime")
    bool DoesSphereOverlapBounds(const FVector& Center, float Radius) const;
    
    // ===== Query Optimization =====
    
    /**
     * Query instances within radius, but only if the query overlaps this actor's bounds.
     * Returns empty array if query doesn't overlap bounds (optimization).
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
    TArray<FISMInstanceHandle> QueryInstancesInRadius(
        const FVector& Location,
        float Radius,
        const FISMQueryFilter& Filter) const;
    
protected:
    // ===== Internal State =====
    
    /** Created runtime components (tracked for cleanup) */
    UPROPERTY(Transient)
    TArray<UISMRuntimeComponent*> CreatedRuntimeComponents;
    
    /** Cached world-space bounds of all instances */
    FBox CachedWorldBounds;
    
    /** Whether bounds have been calculated */
    bool bBoundsCalculated = false;
    
    // ===== Helper Functions =====
    
    /** Find all ISM components on this actor */
    TArray<UInstancedStaticMeshComponent*> FindAllISMComponents() const;
    
    /** Determine which runtime component class to use for an ISM */
    TSubclassOf<UISMRuntimeComponent> GetRuntimeComponentClassForISM(
        UInstancedStaticMeshComponent* ISMComponent) const;
    
    /** Create a single runtime component for an ISM */
    UISMRuntimeComponent* CreateRuntimeComponentForISM(
        UInstancedStaticMeshComponent* ISMComponent,
        TSubclassOf<UISMRuntimeComponent> ComponentClass);
    
    /** Update the bounds box component */
    void UpdateBoundsVisualization();
    
    /** Calculate bounds from all instances */
    FBox CalculateCombinedBounds() const;
};