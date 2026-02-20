// ISMInstanceDataAsset.cpp  (AABB additions)
// Merge this into your existing ISMInstanceDataAsset.cpp

#include "ISMInstanceDataAsset.h"
#include "Engine/StaticMesh.h"
#include "CustomData/ISMCustomDataSchema.h"
#include "Logging/LogMacros.h"

const FISMCustomDataSchema* UISMInstanceDataAsset::ResolveSchema() const
{
    const UISMRuntimeDeveloperSettings* Settings = UISMRuntimeDeveloperSettings::Get();
    if (!bUsePICDConversion || !Settings)
        return nullptr;
	
    if (GetEffectiveSchemaName() == NAME_None)
        return Settings->GetDefaultSchema();

    auto schema = Settings->ResolveSchema(GetEffectiveSchemaName());
    return schema ? schema : Settings->GetDefaultSchema();
}

FName UISMInstanceDataAsset::GetEffectiveSchemaName() const { return SchemaName; }

#if WITH_EDITOR

TArray<FName> UISMInstanceDataAsset::GetAvailableSchemaNames() const
{
    const UISMRuntimeDeveloperSettings* Settings = UISMRuntimeDeveloperSettings::Get();
    if (!Settings)
        return TArray<FName>();
    return Settings->GetAllSchemaNames();
}

#endif // WITH_EDITOR


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
		UE_LOG(LogTemp, Warning, TEXT("No static mesh assigned to %s. Cached bounds will be invalid."), *GetName());
        CachedLocalBounds = FBox(EForceInit::ForceInit); // mark invalid
        return;
    }

    // GetBoundingBox() returns the local-space tight AABB of the mesh.
    // This is the same box the renderer uses, so it's always accurate.
    FBox MeshBox = StaticMesh->GetBoundingBox();

    if (MeshBox.IsValid)
    {
        CachedLocalBounds = MeshBox;
        UE_LOG(LogTemp, Log, TEXT("Cached bounds for %s: Min=%s, Max=%s"),
            *StaticMesh->GetName(),
            *CachedLocalBounds.Min.ToString(),
            *CachedLocalBounds.Max.ToString()
		);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to get valid bounding box for mesh %s. Using render bounds as fallback."),
            *StaticMesh->GetName());
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
	UE_LOG(LogTemp, Log, TEXT("Property changed: %s"), *PropertyName.ToString());
    if (BoundsDependentProperties.Contains(PropertyName))
    {
		UE_LOG(LogTemp, Log, TEXT("Refreshing cached bounds due to change in %s"), *PropertyName.ToString());
        RefreshCachedBounds();
    }
}
#endif // WITH_EDITOR