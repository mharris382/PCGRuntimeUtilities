// Copyright (c) 2025 Max Harris
// Published by Procedural Architect

#include "ISMRuntimeSubsystem.h"
#include "ISMRuntimeComponent.h"
#include "ISMInstanceHandle.h"
#include "ISMInstanceState.h"
#include "Batching/ISMBatchScheduler.h"
#include "ISMQueryFilter.h"
#include "Engine/World.h"
#include "Logging/LogMacros.h"


#pragma region SUBSYSTEM_LIFECYCLE

void UISMRuntimeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    
    UE_LOG(LogTemp, Log, TEXT("ISMRuntimeSubsystem: Initialized for world %s"), 
        *GetWorld()->GetName());

    // Reserve some space
    AllComponents.Reserve(32);
    
    CachedStats = FISMRuntimeStats();
    StatsUpdateFrame = 0;

    if (!BatchScheduler)
    {
        BatchScheduler = NewObject<UISMBatchScheduler>(this);
    }
    BatchScheduler->Initialize(this);

}

void UISMRuntimeSubsystem::Deinitialize()
{
    // Clean up all registered components
    AllComponents.Empty();
    ComponentsByTag.Empty();

    if(BatchScheduler)
    {
        BatchScheduler->Deinitialize();
        BatchScheduler = nullptr;
	}
    
    UE_LOG(LogTemp, Log, TEXT("ISMRuntimeSubsystem: Deinitialized"));
    
    Super::Deinitialize();
}

bool UISMRuntimeSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
    // Support Game, PIE, and Editor worlds
    return WorldType == EWorldType::Game || 
           WorldType == EWorldType::PIE || 
           WorldType == EWorldType::Editor;
}

bool UISMRuntimeSubsystem::IsTickable() const
{
    return BatchScheduler && BatchScheduler->HasPendingWork();
}

void UISMRuntimeSubsystem::Tick(float DeltaTime)
{
    if (BatchScheduler)
    {
        BatchScheduler->Tick(DeltaTime);
	}
}

UISMBatchScheduler* UISMRuntimeSubsystem::GetOrCreateBatchSchduler()
{
	if (!BatchScheduler)
    {
        BatchScheduler = NewObject<UISMBatchScheduler>(this);
        BatchScheduler->Initialize(this);
    }
    return BatchScheduler;
}




#pragma endregion


#pragma region COMPONENT_REGISTRATION

bool UISMRuntimeSubsystem::RegisterRuntimeComponent(UISMRuntimeComponent* Component)
{
    if (!Component)
    {
        return false;
    }
    
    // Check if already registered
    for (const TWeakObjectPtr<UISMRuntimeComponent>& ExistingComp : AllComponents)
    {
        if (ExistingComp.Get() == Component)
        {
            return true; // Already registered
        }
    }
    
    // Add to main list
    AllComponents.Add(Component);
    
    // Index by tags
    RebuildTagIndexForComponent(Component);
    
    UE_LOG(LogTemp, Verbose, TEXT("ISMRuntimeSubsystem: Registered component %s with %d instances"),
        *Component->GetOwner()->GetName(),
        Component->GetInstanceCount());

	return true;
}

void UISMRuntimeSubsystem::UnregisterRuntimeComponent(UISMRuntimeComponent* Component)
{
    if (!Component)
    {
        return;
    }
    
    // Remove from main list
    AllComponents.RemoveAll([Component](const TWeakObjectPtr<UISMRuntimeComponent>& Comp)
    {
        return Comp.Get() == Component;
    });
    
    // Remove from tag index
    for (auto& Pair : ComponentsByTag)
    {
        Pair.Value.RemoveAll([Component](const TWeakObjectPtr<UISMRuntimeComponent>& Comp)
        {
            return Comp.Get() == Component;
        });
    }
    
    // Clean up empty tag entries
    TArray<FGameplayTag> EmptyTags;
    for (const auto& Pair : ComponentsByTag)
    {
        if (Pair.Value.Num() == 0)
        {
            EmptyTags.Add(Pair.Key);
        }
    }
    
    for (const FGameplayTag& Tag : EmptyTags)
    {
        ComponentsByTag.Remove(Tag);
    }
    
    UE_LOG(LogTemp, Verbose, TEXT("ISMRuntimeSubsystem: Unregistered component %s"),
        *Component->GetOwner()->GetName());
}

TArray<UISMRuntimeComponent*> UISMRuntimeSubsystem::GetAllComponents() const
{
    TArray<UISMRuntimeComponent*> ValidComponents;
    ValidComponents.Reserve(AllComponents.Num());
    
    for (const TWeakObjectPtr<UISMRuntimeComponent>& CompPtr : AllComponents)
    {
        if (UISMRuntimeComponent* Comp = CompPtr.Get())
        {
            ValidComponents.Add(Comp);
        }
    }
    
    return ValidComponents;
}

TArray<UISMRuntimeComponent*> UISMRuntimeSubsystem::GetComponentsWithTag(FGameplayTag Tag) const
{
    TArray<UISMRuntimeComponent*> ValidComponents;
    
    if (const TArray<TWeakObjectPtr<UISMRuntimeComponent>>* Components = ComponentsByTag.Find(Tag))
    {
        ValidComponents.Reserve(Components->Num());
        
        for (const TWeakObjectPtr<UISMRuntimeComponent>& CompPtr : *Components)
        {
            if (UISMRuntimeComponent* Comp = CompPtr.Get())
            {
                ValidComponents.Add(Comp);
            }
        }
    }
    
    return ValidComponents;
}


#pragma endregion


#pragma region QUERIES

TArray<FISMInstanceReference> UISMRuntimeSubsystem::QueryInstancesInRadius(
    const FVector& Location,
    float Radius,
    const FISMQueryFilter& Filter) const
{
    TArray<FISMInstanceReference> Results;
    
    // Get components to search (use tag filtering if available)
    TArray<UISMRuntimeComponent*> ComponentsToSearch;
    
    if (!Filter.RequiredTags.IsEmpty())
    {
        // Find components with required tags
        TSet<UISMRuntimeComponent*> ComponentSet;
        
        for (const FGameplayTag& Tag : Filter.RequiredTags)
        {
            TArray<UISMRuntimeComponent*> TaggedComponents = GetComponentsWithTag(Tag);
            ComponentSet.Append(TaggedComponents);
        }
        
        ComponentsToSearch = ComponentSet.Array();
    }
    else
    {
        // Search all components
        ComponentsToSearch = GetAllComponents();
    }
    
    // Query each component
    for (UISMRuntimeComponent* Comp : ComponentsToSearch)
    {
        if (!Comp)
        {
            continue;
        }
        
        // Pre-filter by component-level criteria
        if (!Filter.PassesComponentFilter(Comp))
        {
            continue;
        }
        
        // Query this component's spatial index
        TArray<int32> InstanceIndices = Comp->GetInstancesInRadius(Location, Radius, false);
        
        // Filter instances and convert to references
        for (int32 Index : InstanceIndices)
        {
            FISMInstanceReference Ref;
            Ref.Component = Comp;
            Ref.InstanceIndex = Index;
            
            if (Filter.PassesFilter(Ref))
            {
                Results.Add(Ref);
                
                // Check max results
                if (Filter.MaxResults > 0 && Results.Num() >= Filter.MaxResults)
                {
                    return Results;
                }
            }
        }
    }
    
    // Sort by distance if requested
    if (Filter.bSortByDistance && Results.Num() > 0)
    {
        Results.Sort([&Location](const FISMInstanceReference& A, const FISMInstanceReference& B)
        {
            float DistA = FVector::DistSquared(A.GetLocation(), Location);
            float DistB = FVector::DistSquared(B.GetLocation(), Location);
            return DistA < DistB;
        });
    }
    
    return Results;
}

TArray<FISMInstanceReference> UISMRuntimeSubsystem::QueryInstancesInBox(
    const FBox& Box,
    const FISMQueryFilter& Filter) const
{
    TArray<FISMInstanceReference> Results;
    
    // Get components to search
    TArray<UISMRuntimeComponent*> ComponentsToSearch;
    
    if (!Filter.RequiredTags.IsEmpty())
    {
        TSet<UISMRuntimeComponent*> ComponentSet;
        
        for (const FGameplayTag& Tag : Filter.RequiredTags)
        {
            TArray<UISMRuntimeComponent*> TaggedComponents = GetComponentsWithTag(Tag);
            ComponentSet.Append(TaggedComponents);
        }
        
        ComponentsToSearch = ComponentSet.Array();
    }
    else
    {
        ComponentsToSearch = GetAllComponents();
    }
    
    // Query each component
    for (UISMRuntimeComponent* Comp : ComponentsToSearch)
    {
        if (!Comp)
        {
            continue;
        }
        
        if (!Filter.PassesComponentFilter(Comp))
        {
            continue;
        }
        
        TArray<int32> InstanceIndices = Comp->GetInstancesInBox(Box, false);
        
        for (int32 Index : InstanceIndices)
        {
            FISMInstanceReference Ref;
            Ref.Component = Comp;
            Ref.InstanceIndex = Index;
            
            if (Filter.PassesFilter(Ref))
            {
                Results.Add(Ref);
                
                if (Filter.MaxResults > 0 && Results.Num() >= Filter.MaxResults)
                {
                    return Results;
                }
            }
        }
    }
    
    return Results;
}


TArray<FISMInstanceHandle> UISMRuntimeSubsystem::QueryInstancesOverlappingBox(
    const FBox& Box,
    const FISMQueryFilter& Filter) const
{
    TArray<FISMInstanceHandle> Results;

    if (!Box.IsValid)
    {
        return Results;
    }

    for (const TWeakObjectPtr<UISMRuntimeComponent>& CompPtr : AllComponents)
    {
        UISMRuntimeComponent* Comp = CompPtr.Get();
        if (!Comp || !Comp->IsISMInitialized())
        {
            continue;
        }

        // Component-level filter first (tags, interfaces) — cheap
        if (!Filter.PassesComponentFilter(Comp))
        {
            continue;
        }

        // If this component opted out of AABB and the filter requires AABB data, skip it
        if (!Comp->bComputeInstanceAABBs && Filter.bFilterByAABB && Filter.bExcludeIfAABBUnavailable)
        {
            continue;
        }

        // Coarse: does the query box overlap this component's aggregate bounds?
        // Avoids per-instance work for distant components entirely.
        if (Comp->IsBoundsValid())
        {
            // We'd love CachedInstanceBounds here — expose a getter if not already public
            // For now fall through; individual GetInstancesOverlappingBox handles the rest
        }

        // Per-instance AABB test
        TArray<int32> Overlapping = Comp->GetInstancesOverlappingBox(Box, false);

        for (int32 Idx : Overlapping)
        {
            FISMInstanceHandle Handle = Comp->GetInstanceHandle(Idx);

            if (Filter.PassesFilter(Handle))
            {
                Results.Add(Handle);
            }

            if (Filter.MaxResults > 0 && Results.Num() >= Filter.MaxResults)
            {
                return Results;
            }
        }
    }

    return Results;
}


// ============================================================
//  QueryInstancesOverlappingInstance
//  Finds all instances across all components whose AABB overlaps
//  the given handle's AABB. The handle itself is excluded.
// ============================================================

TArray<FISMInstanceHandle> UISMRuntimeSubsystem::QueryInstancesOverlappingInstance(
    const FISMInstanceHandle& Handle,
    const FISMQueryFilter& Filter) const
{
    TArray<FISMInstanceHandle> Results;

    UISMRuntimeComponent* OwnerComp = Handle.Component.Get();
    if (!OwnerComp || !OwnerComp->bComputeInstanceAABBs)
    {
        return Results;
    }

    // Get the world-space AABB of the query instance
    FBox QueryBounds = OwnerComp->GetInstanceWorldBounds(Handle.InstanceIndex);
    if (!QueryBounds.IsValid)
    {
        return Results;
    }

    // Reuse the box query, then strip out the handle itself
    Results = QueryInstancesOverlappingBox(QueryBounds, Filter);

    Results.RemoveAllSwap([&Handle](const FISMInstanceHandle& Result)
        {
            return Result.Component == Handle.Component
                && Result.InstanceIndex == Handle.InstanceIndex;
        });

    return Results;
}


FISMInstanceReference UISMRuntimeSubsystem::FindNearestInstance(
    const FVector& Location,
    const FISMQueryFilter& Filter,
    float MaxDistance) const
{
    FISMInstanceReference NearestRef;
    float NearestDistSq = MaxDistance > 0.0f ? (MaxDistance * MaxDistance) : MAX_FLT;
    
    // Get components to search
    TArray<UISMRuntimeComponent*> ComponentsToSearch = GetAllComponents();
    
    for (UISMRuntimeComponent* Comp : ComponentsToSearch)
    {
        if (!Comp)
        {
            continue;
        }
        
        if (!Filter.PassesComponentFilter(Comp))
        {
            continue;
        }
        
        // Query nearby instances
        float SearchRadius = MaxDistance > 0.0f ? MaxDistance : 5000.0f;
        TArray<int32> CandidateIndices = Comp->GetInstancesInRadius(Location, SearchRadius, false);
        
        for (int32 Index : CandidateIndices)
        {
            FISMInstanceReference Ref;
            Ref.Component = Comp;
            Ref.InstanceIndex = Index;
            
            if (!Filter.PassesFilter(Ref))
            {
                continue;
            }
            
            float DistSq = FVector::DistSquared(Location, Ref.GetLocation());
            
            if (DistSq < NearestDistSq)
            {
                NearestDistSq = DistSq;
                NearestRef = Ref;
            }
        }
    }
    
    return NearestRef;
}





UISMRuntimeComponent* UISMRuntimeSubsystem::FindComponentForInstance(const FISMInstanceReference& Instance) const
{
    return Instance.Component.Get();
}


#pragma endregion


#pragma region ISM_INSTANCE_STATS

FISMRuntimeStats UISMRuntimeSubsystem::GetRuntimeStats() const
{
    // Update stats if stale
    if (StatsUpdateFrame != GFrameCounter)
    {
        const_cast<UISMRuntimeSubsystem*>(this)->UpdateStatistics();
    }
    
    return CachedStats;
}

void UISMRuntimeSubsystem::UpdateStatistics()
{
    // Clean up invalid components first
    const_cast<UISMRuntimeSubsystem*>(this)->CleanupInvalidComponents();
    
    CachedStats.RegisteredComponentCount = 0;
    CachedStats.TotalInstanceCount = 0;
    CachedStats.ActiveInstanceCount = 0;
    CachedStats.DestroyedInstanceCount = 0;
    
    for (const TWeakObjectPtr<UISMRuntimeComponent>& CompPtr : AllComponents)
    {
        if (UISMRuntimeComponent* Comp = CompPtr.Get())
        {
            CachedStats.RegisteredComponentCount++;
            CachedStats.TotalInstanceCount += Comp->GetInstanceCount();
            CachedStats.ActiveInstanceCount += Comp->GetActiveInstanceCount();
            CachedStats.DestroyedInstanceCount += (Comp->GetInstanceCount() - Comp->GetActiveInstanceCount());
        }
    }
    
    StatsUpdateFrame = GFrameCounter;
}

#pragma endregion


#pragma region COMPONENT_STORAGE

void UISMRuntimeSubsystem::CleanupInvalidComponents()
{
    // Remove invalid component references
    AllComponents.RemoveAll([](const TWeakObjectPtr<UISMRuntimeComponent>& Comp)
    {
        return !Comp.IsValid();
    });
    
    // Clean up tag index
    for (auto& Pair : ComponentsByTag)
    {
        Pair.Value.RemoveAll([](const TWeakObjectPtr<UISMRuntimeComponent>& Comp)
        {
            return !Comp.IsValid();
        });
    }
}

void UISMRuntimeSubsystem::RebuildTagIndexForComponent(UISMRuntimeComponent* Component)
{
    if (!Component)
    {
        return;
    }
    
    // Add component to tag index for all its tags
    for (const FGameplayTag& Tag : Component->ISMComponentTags)
    {
        TArray<TWeakObjectPtr<UISMRuntimeComponent>>& Components = ComponentsByTag.FindOrAdd(Tag);
        
        // Only add if not already present
        bool bAlreadyIndexed = false;
        for (const TWeakObjectPtr<UISMRuntimeComponent>& ExistingComp : Components)
        {
            if (ExistingComp.Get() == Component)
            {
                bAlreadyIndexed = true;
                break;
            }
        }
        
        if (!bAlreadyIndexed)
        {
            Components.Add(Component);
        }
    }
}
#pragma endregion
