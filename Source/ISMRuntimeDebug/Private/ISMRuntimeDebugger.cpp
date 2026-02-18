// ISMRuntimeDebugger.cpp
#include "ISMRuntimeDebugger.h"
#include "ISMRuntimeComponent.h"
#include "ISMRuntimeSubsystem.h"
#include "ISMInstanceDataAsset.h"
#include "ISMInstanceState.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "SceneView.h"
#include "SceneManagement.h"
#include "DrawDebugHelpers.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"

// ------------------------------------------------------------
//  Construction
// ------------------------------------------------------------

UISMRuntimeDebugger::UISMRuntimeDebugger()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;

    // Debug components only need to tick at ~20fps — no reason to burn
    // a full game thread tick budget on visualization.
    PrimaryComponentTick.TickInterval = 0.05f;

#if UE_BUILD_SHIPPING
    bEnabled = false;
#endif
}

// ------------------------------------------------------------
//  BeginPlay
// ------------------------------------------------------------

void UISMRuntimeDebugger::BeginPlay()
{
    Super::BeginPlay();

#if UE_BUILD_SHIPPING
    // Never run in shipping regardless of config
    SetComponentTickEnabled(false);
    return;
#endif

    if (bEditorOnly && !GIsEditor)
    {
        SetComponentTickEnabled(false);
    }
}

// ------------------------------------------------------------
//  TickComponent
// ------------------------------------------------------------

void UISMRuntimeDebugger::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

#if UE_BUILD_SHIPPING
    return;
#endif

    if (!bEnabled || !GetWorld())
    {
        return;
    }

    FVector CameraLocation;
    FConvexVolume Frustum;
    if (!GetCameraInfo(CameraLocation, Frustum))
    {
        return;
    }

    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    DrawDebugForAllComponents(nullptr);
}

// ------------------------------------------------------------
//  GetComponentsToDraw
//  Returns either the explicit target list or all subsystem components.
// ------------------------------------------------------------

TArray<UISMRuntimeComponent*> UISMRuntimeDebugger::GetComponentsToDraw() const
{
    if (TargetComponents.Num() > 0)
    {
        TArray<UISMRuntimeComponent*> Valid;
        Valid.Reserve(TargetComponents.Num());
        for (UISMRuntimeComponent* Comp : TargetComponents)
        {
            if (IsValid(Comp))
            {
                Valid.Add(Comp);
            }
        }
        return Valid;
    }

    // Fall back to subsystem
    if (UWorld* World = GetWorld())
    {
        if (UISMRuntimeSubsystem* Subsystem = World->GetSubsystem<UISMRuntimeSubsystem>())
        {
            return Subsystem->GetAllComponents();
        }
    }

    return {};
}

// ------------------------------------------------------------
//  GetCameraInfo
//  Pulls camera location and frustum from the first local player.
//  Returns false if no valid view is available (editor with no
//  viewport focus, dedicated server, etc.)
// ------------------------------------------------------------

bool UISMRuntimeDebugger::GetCameraInfo(FVector& OutLocation, FConvexVolume& OutFrustum) const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return false;
    }

    APlayerController* PC = World->GetFirstPlayerController();
    if (!PC || !PC->PlayerCameraManager)
    {
        return false;
    }

    OutLocation = PC->PlayerCameraManager->GetCameraLocation();

    // Build frustum from the player's view info
    FMinimalViewInfo ViewInfo;
    PC->PlayerCameraManager->GetCameraViewPoint(OutLocation, ViewInfo.Rotation);
    ViewInfo.Location   = OutLocation;
    ViewInfo.FOV        = PC->PlayerCameraManager->GetFOVAngle();

    // Build a projection matrix so we can extract the frustum planes.
    // We use a conservative near/far to avoid clipping debug geometry.
    const FIntPoint ViewportSize   = GEngine ? GEngine->GameViewport ?
        FIntPoint(1920, 1080) : FIntPoint(1920, 1080) : FIntPoint(1920, 1080);

    const float AspectRatio = (float)ViewportSize.X / (float)ViewportSize.Y;
    FMatrix ProjectionMatrix = FReversedZPerspectiveMatrix(
        FMath::DegreesToRadians(ViewInfo.FOV * 0.5f),
        AspectRatio,
        1.0f,    // near
        100000.0f // far — generous to catch big scene debug
    );

    FMatrix ViewMatrix = FTranslationMatrix(-OutLocation) *
        FInverseRotationMatrix(PC->PlayerCameraManager->GetCameraRotation());

    FMatrix ViewProjectionMatrix = ViewMatrix * ProjectionMatrix;
    GetViewFrustumBounds(OutFrustum, ViewProjectionMatrix, false);

    return true;
}

// ------------------------------------------------------------
//  DrawDebugForComponent
//  Core per-component draw loop. All heavy decisions (culling,
//  budget cap, state color) happen here in one pass.
// ------------------------------------------------------------

void UISMRuntimeDebugger::DrawDebugForComponent(
    const UISMRuntimeComponent* Comp,
    const FLinearColor& BaseColor,
    int32 ActiveFlags,
    const FConvexVolume& Frustum,
    const FVector& CameraLocation,
    UWorld* World) const
{
    if (!Comp || !World)
    {
        return;
    }

    const bool bDrawAABB           = (ActiveFlags & (int32)EISMDebugDrawFlags::InstanceAABB)    != 0;
    const bool bDrawCenter         = (ActiveFlags & (int32)EISMDebugDrawFlags::InstanceCenter)  != 0;
    const bool bDrawIndex          = (ActiveFlags & (int32)EISMDebugDrawFlags::InstanceIndex)   != 0;
    const bool bDrawStateColor     = (ActiveFlags & (int32)EISMDebugDrawFlags::StateFlags)      != 0;
    const bool bDrawCompBounds     = (ActiveFlags & (int32)EISMDebugDrawFlags::ComponentBounds) != 0;

    // Component aggregate bounds — one draw, cheap
    if (bDrawCompBounds)
    {
        DrawComponentBounds(Comp, BaseColor.CopyWithNewOpacity(0.6f), World);
    }

    const int32 TotalInstances = Comp->GetInstanceCount();
    const float MaxDistSq      = (MaxDrawDistance > 0.0f)
        ? FMath::Square(MaxDrawDistance)
        : FLT_MAX;
    const float MaxLabelDistSq = FMath::Square(MaxLabelDistance);

    int32 DrawnCount = 0;

    for (int32 i = 0; i < TotalInstances; ++i)
    {
        // Budget cap
        if (MaxInstancesPerComponent > 0 && DrawnCount >= MaxInstancesPerComponent)
        {
            break;
        }

        // State checks
        const FISMInstanceState* State = Comp->GetInstanceState(i);

        if (bSkipDestroyedInstances && State && State->HasFlag(EISMInstanceState::Destroyed))
        {
            continue;
        }

        // Resolve draw color for this instance
        FLinearColor InstanceColor = BaseColor;
        if (bDrawStateColor && State)
        {
            if (State->HasFlag(EISMInstanceState::Destroyed))
            {
                InstanceColor = DestroyedColor;
            }
            else if (State->HasFlag(EISMInstanceState::Hidden))
            {
                InstanceColor = HiddenColor;
            }
        }

        // Distance cull (center point — cheap)
        FVector Center = Comp->GetInstanceLocation(i);
        float DistSq   = FVector::DistSquared(CameraLocation, Center);

        if (DistSq > MaxDistSq)
        {
            continue;
        }

        // Frustum cull using AABB when available, center point as fallback
        if (bFrustumCull)
        {
            bool bVisible = false;

            if (Comp->bComputeInstanceAABBs && State && State->bBoundsValid)
            {
                // Test the full AABB against the frustum — more accurate,
                // avoids popping at frustum edges for large instances.
                bVisible = Frustum.IntersectBox(
                    State->WorldBounds.GetCenter(),
                    State->WorldBounds.GetExtent()
                );
            }
            else
            {
                // Fallback: treat instance as a point with a small fudge radius
                bVisible = Frustum.IntersectSphere(Center, 100.0f);
            }

            if (!bVisible)
            {
                continue;
            }
        }

        // ---- Draw this instance ----

        if (bDrawAABB && Comp->bComputeInstanceAABBs && State && State->bBoundsValid)
        {
            DrawInstanceAABB(State->WorldBounds, InstanceColor, LineThickness, World);
        }

        if (bDrawCenter)
        {
            DrawDebugPoint(World, Center, 6.0f, InstanceColor.ToFColor(true), false, -1.0f);
        }

        if (bDrawIndex && DistSq <= MaxLabelDistSq)
        {
            DrawDebugString(
                World,
                Center + FVector(0, 0, 50.0f),
                FString::FromInt(i),
                nullptr,
                InstanceColor.ToFColor(true),
                -1.0f,
                false,
                1.0f
            );
        }

        ++DrawnCount;
    }
}

// ------------------------------------------------------------
//  DrawInstanceAABB
//  Draws a box using batched debug lines. We call DrawDebugBox
//  rather than rolling our own line list — UE batches consecutive
//  debug line calls within the same frame internally.
// ------------------------------------------------------------

void UISMRuntimeDebugger::DrawInstanceAABB(
    const FBox& WorldBounds,
    const FLinearColor& Color,
    float Thickness,
    UWorld* World) const
{
    if (!World || !WorldBounds.IsValid)
    {
        return;
    }

    DrawDebugBox(
        World,
        WorldBounds.GetCenter(),
        WorldBounds.GetExtent(),
        FQuat::Identity,
        Color.ToFColor(true),
        false,      // persistent — false means it clears next frame, right for per-tick debug
        -1.0f,      // lifetime (-1 = one frame)
        0,          // depth priority
        Thickness
    );
}

// ------------------------------------------------------------
//  DrawComponentBounds
// ------------------------------------------------------------

void UISMRuntimeDebugger::DrawComponentBounds(
    const UISMRuntimeComponent* Comp,
    const FLinearColor& Color,
    UWorld* World) const
{
    if (!Comp || !World || !Comp->IsBoundsValid())
    {
        return;
    }

    // Access via the public getter we noted needs adding.
    // Using the ISM component's own bounds as a fallback if
    // CachedInstanceBounds getter isn't exposed yet.
    if (Comp->ManagedISMComponent)
    {
        FBoxSphereBounds ISMBounds = Comp->ManagedISMComponent->Bounds;
        DrawDebugBox(
            World,
            ISMBounds.Origin,
            ISMBounds.BoxExtent,
            FQuat::Identity,
            Color.ToFColor(true),
            false,
            -1.0f,
            0,
            ComponentBoundsThickness
        );
    }
}

// ------------------------------------------------------------
//  ResolveComponentColor
//  Priority: ComponentOverride > DataAsset DebugColor > DefaultColor
// ------------------------------------------------------------

FLinearColor UISMRuntimeDebugger::ResolveComponentColor(const UISMRuntimeComponent* Comp) const
{
    if (!Comp)
    {
        return DefaultColor;
    }

    // Check per-component override first
    for (const FISMDebugComponentOverride& Override : ComponentOverrides)
    {
        if (Override.Component == Comp && Override.bUseColorOverride)
        {
            return Override.ColorOverride;
        }
    }

    // DataAsset debug color
    if (Comp->InstanceData)
    {
        return Comp->InstanceData->DebugColor;
    }

    return DefaultColor;
}

// ------------------------------------------------------------
//  ResolveComponentFlags
// ------------------------------------------------------------

int32 UISMRuntimeDebugger::ResolveComponentFlags(const UISMRuntimeComponent* Comp) const
{
    if (!Comp)
    {
        return DrawFlags;
    }

    for (const FISMDebugComponentOverride& Override : ComponentOverrides)
    {
        if (Override.Component == Comp && Override.bUseFlagsOverride)
        {
            return Override.FlagsOverride;
        }
    }

    return DrawFlags;
}

// ------------------------------------------------------------
//  Runtime API
// ------------------------------------------------------------

void UISMRuntimeDebugger::AddTargetComponent(UISMRuntimeComponent* Component)
{
    if (Component && !TargetComponents.Contains(Component))
    {
        TargetComponents.Add(Component);
    }
}

void UISMRuntimeDebugger::RemoveTargetComponent(UISMRuntimeComponent* Component)
{
    TargetComponents.Remove(Component);
}


// Add this function to ISMRuntimeDebugger.cpp, before TickComponent.
// It exists as a named entry point for cases where a caller already has
// a FSceneView (e.g. a custom viewport client or editor extension).
// For normal gameplay the tick calls GetCameraInfo() directly instead.

void UISMRuntimeDebugger::DrawDebugForAllComponents(const FSceneView* View)
{
#if UE_BUILD_SHIPPING
    return;
#endif

    if (!bEnabled || !GetWorld())
    {
        return;
    }

    UWorld* World = GetWorld();

    FVector CameraLocation;
    FConvexVolume Frustum;

    if (View)
    {
        // Use the provided view directly — more accurate than PlayerController
        // when called from an editor viewport or custom renderer pass.
        CameraLocation = View->ViewMatrices.GetViewOrigin();
        GetViewFrustumBounds(Frustum, View->ViewMatrices.GetViewProjectionMatrix(), false);
    }
    else
    {
        // Fall back to PlayerController path when no view is provided
        if (!GetCameraInfo(CameraLocation, Frustum))
        {
            return;
        }
    }

    TArray<UISMRuntimeComponent*> Components = GetComponentsToDraw();

    for (UISMRuntimeComponent* Comp : Components)
    {
        if (!Comp || !Comp->IsISMInitialized())
        {
            continue;
        }

        FLinearColor Color = ResolveComponentColor(Comp);
        int32 ActiveFlags = ResolveComponentFlags(Comp);

        DrawDebugForComponent(Comp, Color, ActiveFlags, Frustum, CameraLocation, World);
    }
}