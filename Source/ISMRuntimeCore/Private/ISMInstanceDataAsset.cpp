// ISMInstanceDataAsset.cpp  (AABB additions)
// Merge this into your existing ISMInstanceDataAsset.cpp

#include "ISMInstanceDataAsset.h"
#include "Engine/StaticMesh.h"

// ---------------------------------------------------------------
//  GetEffectiveLocalBounds
//  Single access point for everything that needs local-space bounds.
//  Applies padding on the way out so the stored CachedLocalBounds
//  always reflects raw mesh data.
// ---------------------------------------------------------------
FBox UISMInstanceDataAsset::GetEffectiveLocalBounds() const
{
    if (!CachedLocalBounds.IsValid)
    {
        return FBox(EForceInit::ForceInit); // caller checks IsValid
    }

    // Manual override wins over everything
    if (bOverrideBounds)
    {
        return BoundsOverride;
    }

    FBox Result = CachedLocalBounds;

    // Uniform padding
    if (BoundsPadding > 0.0f)
    {
        Result = Result.ExpandBy(BoundsPadding);
    }

    // Per-axis padding (additive with uniform)
    if (!BoundsPaddingExtent.IsNearlyZero())
    {
        Result.Min -= BoundsPaddingExtent;
        Result.Max += BoundsPaddingExtent;
    }

    return Result;
}

// ---------------------------------------------------------------
//  RefreshCachedBounds  (editor only)
//  Reads the mesh's local-space bounding box and caches it.
//  Called from PostEditChangeProperty whenever StaticMesh or
//  padding properties change.
// ---------------------------------------------------------------
#if WITH_EDITOR
void UISMInstanceDataAsset::RefreshCachedBounds()
{
    if (!StaticMesh)
    {
        CachedLocalBounds = FBox(EForceInit::ForceInit); // mark invalid
        return;
    }

    // GetBoundingBox() returns the local-space tight AABB of the mesh.
    // This is the same box the renderer uses, so it's always accurate.
    FBox MeshBox = StaticMesh->GetBoundingBox();

    if (MeshBox.IsValid)
    {
        CachedLocalBounds = MeshBox;
    }
    else
    {
        // Fallback: build from the render data bounds (sphere-based)
        const FBoxSphereBounds& RenderBounds = StaticMesh->GetBounds();
        CachedLocalBounds = FBox(
            RenderBounds.Origin - RenderBounds.BoxExtent,
            RenderBounds.Origin + RenderBounds.BoxExtent
        );
    }
}

void UISMInstanceDataAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    const FName PropertyName = PropertyChangedEvent.GetPropertyName();

    // Refresh whenever the mesh changes or padding changes (padding affects
    // GetEffectiveLocalBounds output so callers need a consistent signal)
    static const TArray<FName> BoundsDependentProperties =
    {
        GET_MEMBER_NAME_CHECKED(UISMInstanceDataAsset, StaticMesh),
        GET_MEMBER_NAME_CHECKED(UISMInstanceDataAsset, BoundsPadding),
        GET_MEMBER_NAME_CHECKED(UISMInstanceDataAsset, BoundsPaddingExtent),
        GET_MEMBER_NAME_CHECKED(UISMInstanceDataAsset, bOverrideBounds),
        GET_MEMBER_NAME_CHECKED(UISMInstanceDataAsset, BoundsOverride),
    };

    if (BoundsDependentProperties.Contains(PropertyName))
    {
        RefreshCachedBounds();
    }
}
#endif // WITH_EDITOR