#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "ISMInstanceState.h"
#include "ISMInstanceHandle.h"
#include "ISMQueryFilter.generated.h"

/**
 * Filter for querying ISM instances with various criteria
 */
USTRUCT(BlueprintType)
struct ISMRUNTIMECORE_API FISMQueryFilter
{
    GENERATED_BODY()
    
    // ===== Gameplay Tag Filtering =====
    
    /** Instances must have ALL of these tags */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter|Tags")
    FGameplayTagContainer RequiredTags;
    
    /** Instances must NOT have ANY of these tags */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter|Tags")
    FGameplayTagContainer ExcludedTags;
    
    /** Advanced tag query (more flexible than RequiredTags/ExcludedTags) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter|Tags")
    FGameplayTagQuery TagQuery;
    
    // ===== Interface Filtering (C++ only) =====
    
    /** Components must implement these interfaces (checked via Cast) */
    UPROPERTY()
    TArray<TSubclassOf<UInterface>> RequiredInterfaces;
    
    // ===== State Filtering =====
    
    /** Instances must have ALL of these state flags */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter|State")
    TArray<EISMInstanceState> RequiredStates;
    
    /** Instances must NOT have ANY of these state flags */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter|State")
    TArray<EISMInstanceState> ExcludedStates;
    
    // ===== Mesh Filtering =====
    
    /** Only return instances using one of these meshes (empty = allow all) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter|Mesh")
    TArray<UStaticMesh*> AllowedMeshes;
    
    // ===== Result Limiting =====
    
    /** Maximum number of results to return (-1 = unlimited) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter|Limits")
    int32 MaxResults = -1;
    
    /** Sort results by distance (closest first) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter|Limits")
    bool bSortByDistance = false;


    // ===== AABB Filtering =====

    /** If set, only return instances whose AABB overlaps this box.
    Requires bComputeInstanceAABBs on the owning component. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter|AABB")
    FBox AABBOverlapBox = FBox(EForceInit::ForceInit);

    /** Whether AABBOverlapBox is active (IsValid() check equivalent) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter|AABB")
    bool bFilterByAABB = false;

    /** If true, instances on components with bComputeInstanceAABBs=false
        are excluded when bFilterByAABB is active. If false, they pass through. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter|AABB")
    bool bExcludeIfAABBUnavailable = false;
    
    // ===== Custom Filter (C++ only) =====
    
    /** Custom filter function for advanced filtering logic */
    TFunction<bool(const FISMInstanceReference&)> CustomFilter;
    
    // ===== Filter Methods =====
    
    /** Check if an instance passes all filter criteria */
    bool PassesFilter(const FISMInstanceReference& Instance) const;
    
    /** Check if a component passes component-level filters (before checking instances) */
    bool PassesComponentFilter(class UISMRuntimeComponent* Component) const;
    
    /** Check if instance passes state filters */
    bool PassesStateFilter(const struct FISMInstanceState* State) const;
};