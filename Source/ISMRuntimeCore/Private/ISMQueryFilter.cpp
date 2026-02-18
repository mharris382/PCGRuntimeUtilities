// Copyright (c) 2025 Max Harris
// Published by Procedural Architect

// ISMQueryFilter.cpp
#include "ISMQueryFilter.h"
#include "ISMRuntimeComponent.h"
#include "ISMInstanceHandle.h"
#include "ISMInstanceState.h"
#include "Components/InstancedStaticMeshComponent.h"

bool FISMQueryFilter::PassesFilter(const FISMInstanceReference& Instance) const
{
    if (!Instance.IsValid())
    {
        return false;
    }

    UISMRuntimeComponent* Comp = Instance.Component.Get();
    if (!Comp)
    {
        return false;
    }

    // ===== Gameplay Tag Filtering =====

    FGameplayTagContainer InstanceTags = Comp->GetInstanceTags(Instance.InstanceIndex);

    // Required tags check
    if (RequiredTags.Num() > 0 && !InstanceTags.HasAll(RequiredTags))
    {
        return false;
    }

    // Excluded tags check
    if (ExcludedTags.Num() > 0 && InstanceTags.HasAny(ExcludedTags))
    {
        return false;
    }

    // Tag query check (most flexible)
    if (!TagQuery.IsEmpty() && !TagQuery.Matches(InstanceTags))
    {
        return false;
    }

    // ===== Interface Filtering =====

    for (TSubclassOf<UInterface> InterfaceClass : RequiredInterfaces)
    {
        if (!InterfaceClass)
        {
            continue;
        }

        if (!Comp->GetClass()->ImplementsInterface(InterfaceClass))
        {
            return false;
        }
    }

    // ===== State Filtering =====

    const FISMInstanceState* State = Comp->GetInstanceState(Instance.InstanceIndex);

    if (!PassesStateFilter(State))
    {
        return false;
    }

    // ===== Mesh Filtering =====

    if (AllowedMeshes.Num() > 0)
    {
        UStaticMesh* InstanceMesh = Comp->ManagedISMComponent ?
            Comp->ManagedISMComponent->GetStaticMesh() : nullptr;

        if (!AllowedMeshes.Contains(InstanceMesh))
        {
            return false;
        }
    }

    // ===== AABB Filtering =====

    if (bFilterByAABB && AABBOverlapBox.IsValid!=0)
    {
        if (!Comp->bComputeInstanceAABBs)
        {
            // Component opted out — caller decides whether to exclude or pass through
            if (bExcludeIfAABBUnavailable)
            {
                return false;
            }
        }
        else
        {
            FBox InstanceBounds = Comp->GetInstanceWorldBounds(Instance.InstanceIndex);
            if (!InstanceBounds.IsValid || !InstanceBounds.Intersect(AABBOverlapBox))
            {
                return false;
            }
        }
    }

    // ===== Custom Filter =====

    if (CustomFilter && !CustomFilter(Instance))
    {
        return false;
    }

    return true;
}

bool FISMQueryFilter::PassesComponentFilter(UISMRuntimeComponent* Component) const
{
    if (!Component)
    {
        return false;
    }

    // ===== Tag Filtering (Component Level) =====

    // Required tags - component must have ALL of them
    if (RequiredTags.Num() > 0 && !Component->ISMComponentTags.HasAll(RequiredTags))
    {
        return false;
    }

    // Excluded tags - component must have NONE of them
    if (ExcludedTags.Num() > 0 && Component->ISMComponentTags.HasAny(ExcludedTags))
    {
        return false;
    }

    // Tag query
    if (!TagQuery.IsEmpty() && !TagQuery.Matches(Component->ISMComponentTags))
    {
        return false;
    }

    // ===== Interface Filtering =====

    for (TSubclassOf<UInterface> InterfaceClass : RequiredInterfaces)
    {
        if (!InterfaceClass)
        {
            continue;
        }

        if (!Component->GetClass()->ImplementsInterface(InterfaceClass))
        {
            return false;
        }
    }

    // ===== Mesh Filtering =====

    if (AllowedMeshes.Num() > 0)
    {
        UStaticMesh* ComponentMesh = Component->ManagedISMComponent ?
            Component->ManagedISMComponent->GetStaticMesh() : nullptr;

        if (!AllowedMeshes.Contains(ComponentMesh))
        {
            return false;
        }
    }

    return true;
}

bool FISMQueryFilter::PassesStateFilter(const FISMInstanceState* State) const
{
    if (!State)
    {
        // If no state exists, check if we require specific states
        return RequiredStates.Num() == 0;
    }

    // Required states - instance must have ALL of them
    for (EISMInstanceState RequiredState : RequiredStates)
    {
        if (!State->HasFlag(RequiredState))
        {
            return false;
        }
    }

    // Excluded states - instance must have NONE of them
    for (EISMInstanceState ExcludedState : ExcludedStates)
    {
        if (State->HasFlag(ExcludedState))
        {
            return false;
        }
    }

    return true;
}