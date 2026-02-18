#pragma once

#include "CoreMinimal.h"
#include "ISMInstanceState.generated.h"

/**
 * Common state flags for ISM instances
 */
UENUM(BlueprintType, meta=(Bitflags, UseEnumValuesAsMaskValuesInEditor="true"))
enum class EISMInstanceState : uint8
{
    None        = 0,
    Intact      = 1 << 0,   // Instance is undamaged
    Damaged     = 1 << 1,   // Instance has taken damage
    Destroyed   = 1 << 2,   // Instance is destroyed (hidden, but index still valid)
    Collected   = 1 << 3,   // Resources have been collected
    Hidden      = 1 << 4,   // Instance is hidden for other reasons
    Converting  = 1 << 5,   // Currently converting to physics actor
    Reserved1   = 1 << 6,   // Available for custom use
    Reserved2   = 1 << 7    // Available for custom use
};
ENUM_CLASS_FLAGS(EISMInstanceState)

/**
 * Runtime state data for a single ISM instance
 */
USTRUCT(BlueprintType)
struct ISMRUNTIMECORE_API FISMInstanceState
{
    GENERATED_BODY()
    
    /** State flags using EISMInstanceState enum */
    UPROPERTY(BlueprintReadOnly, Category = "State")
    uint8 StateFlags = 0;
    
    /** Last frame this instance was updated (for optimization) */
    UPROPERTY(BlueprintReadOnly, Category = "State")
    int LastUpdateFrame = 0;
    
    /** Cached transform (optional, for performance) */
    FTransform CachedTransform;
    
    /** Whether cached transform is valid */
    bool bTransformCached = false;


    /** World-space AABB for this instance. Only valid when owning component has bComputeInstanceAABBs = true. */
    FBox WorldBounds;

    /** Whether WorldBounds is currently valid */
    bool bBoundsValid = false;
    
    /** Custom data pointer for module-specific state (damage values, resource data, etc.) */
    void* ModuleData = nullptr;
    
    FISMInstanceState()
    {
        // Start as intact by default
        SetFlag(EISMInstanceState::Intact, true);
    }
    
    /** Check if a specific flag is set */
    bool HasFlag(EISMInstanceState Flag) const
    {
        return (StateFlags & static_cast<uint8>(Flag)) != 0;
    }
    
    /** Set or clear a specific flag */
    void SetFlag(EISMInstanceState Flag, bool bValue)
    {
        if (bValue)
        {
            StateFlags |= static_cast<uint8>(Flag);
        }
        else
        {
            StateFlags &= ~static_cast<uint8>(Flag);
        }
    }
    
    /** Check if instance is considered "active" (not destroyed, collected, or hidden) */
    bool IsActive() const
    {
        return !HasFlag(EISMInstanceState::Destroyed) &&
               !HasFlag(EISMInstanceState::Collected) &&
               !HasFlag(EISMInstanceState::Hidden);
    }
    
    /** Mark this instance as destroyed */
    void MarkDestroyed()
    {
        SetFlag(EISMInstanceState::Intact, false);
        SetFlag(EISMInstanceState::Damaged, false);
        SetFlag(EISMInstanceState::Destroyed, true);
    }

    bool OverlapsWith(const FBox& Other) const
    {
		return bBoundsValid && WorldBounds.Intersect(Other);
    }

    bool Contains(const FVector& Point) const
    {
        return bBoundsValid && WorldBounds.IsInside(Point);
	}
};