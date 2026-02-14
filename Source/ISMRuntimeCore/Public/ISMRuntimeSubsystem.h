#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "ISMInstanceHandle.h"
#include "ISMQueryFilter.h"
#include "ISMRuntimeSubsystem.generated.h"

// Forward declarations
class UISMRuntimeComponent;

/**
 * Statistics for runtime monitoring
 */
USTRUCT(BlueprintType)
struct FISMRuntimeStats
{
    GENERATED_BODY()
    
    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    int32 RegisteredComponentCount = 0;
    
    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    int32 TotalInstanceCount = 0;
    
    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    int32 ActiveInstanceCount = 0;
    
    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    int32 DestroyedInstanceCount = 0;
    
    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    float LastFrameProcessingTimeMs = 0.0f;
};

/**
 * World subsystem that coordinates all ISM runtime components.
 * Provides global queries, frame budget management, and component registration.
 */
UCLASS()
class ISMRUNTIMECORE_API UISMRuntimeSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
    
public:
    // ===== Subsystem Lifecycle =====
    
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
    
    // ===== Component Registration =====
    
    /** Register a runtime component with this subsystem */
    bool RegisterRuntimeComponent(UISMRuntimeComponent* Component);
    
    /** Unregister a runtime component from this subsystem */
    void UnregisterRuntimeComponent(UISMRuntimeComponent* Component);
    
    /** Get all registered components */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
    TArray<UISMRuntimeComponent*> GetAllComponents() const;
    
    /** Get components with specific tag */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
    TArray<UISMRuntimeComponent*> GetComponentsWithTag(FGameplayTag Tag) const;
    
    /** Get components implementing a specific interface */
/*    template<typename InterfaceType>
    TArray<UISMRuntimeComponent*> GetComponentsWithInterface() const;*/
    
    // ===== Global Queries =====
    
    /** Query instances across all components within radius */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Query")
    TArray<FISMInstanceHandle> QueryInstancesInRadius(
        const FVector& Location,
        float Radius,
        const FISMQueryFilter& Filter) const;
    
    /** Query instances across all components within box */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Query")
    TArray<FISMInstanceHandle> QueryInstancesInBox(
        const FBox& Box,
        const FISMQueryFilter& Filter) const;
    
    /** Find nearest instance across all components */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime|Query")
    FISMInstanceHandle FindNearestInstance(
        const FVector& Location,
        const FISMQueryFilter& Filter,
        float MaxDistance = -1.0f) const;
    
    /** Find component that owns a specific instance */
    UISMRuntimeComponent* FindComponentForInstance(const FISMInstanceReference& Instance) const;
    
    // ===== Statistics =====
    
    /** Get current runtime statistics */
    UFUNCTION(BlueprintCallable, Category = "ISM Runtime")
    FISMRuntimeStats GetRuntimeStats() const;
    
    /** Refresh statistics (called automatically each frame) */
    void UpdateStatistics();
    
protected:
    // ===== Component Storage =====
    
    /** All registered components */
    UPROPERTY()
    TArray<TWeakObjectPtr<UISMRuntimeComponent>> AllComponents;
    
    /** Components indexed by tag for fast lookup */
    TMap<FGameplayTag, TArray<TWeakObjectPtr<UISMRuntimeComponent>>> ComponentsByTag;
    
    /** Cached statistics */
    FISMRuntimeStats CachedStats;
    
    /** Frame number when stats were last updated */
    uint32 StatsUpdateFrame = 0;
    
    // ===== Helper Functions =====
    
    /** Clean up invalid component references */
    void CleanupInvalidComponents();
    
    /** Rebuild tag index for a component */
    void RebuildTagIndexForComponent(UISMRuntimeComponent* Component);
};
/*
// Template implementation
template<typename InterfaceType>
TArray<UISMRuntimeComponent*> UISMRuntimeSubsystem::GetComponentsWithInterface() const
{
    TArray<UISMRuntimeComponent*> Result;
    
    for (const TWeakObjectPtr<UISMRuntimeComponent>& CompPtr : AllComponents)
    {
        if (UISMRuntimeComponent* Comp = CompPtr.Get())
        {
            if (Comp->GetClass()->ImplementsInterface(InterfaceType::StaticClass()))
            {
                Result.Add(Comp);
            }
        }
    }
    
    return Result;
}*/