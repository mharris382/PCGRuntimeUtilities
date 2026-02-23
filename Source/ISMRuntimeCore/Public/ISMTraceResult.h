#pragma once
#include "CoreMinimal.h"

#include "ISMInstanceHandle.h"
#include "ISMTraceResult.generated.h"

UENUM(BlueprintType)
enum class EISMTraceResolveMethod : uint8
{
    /** Trace hit the ISM component directly via Hit.Item */
    Direct,

    /** Trace hit a redirect component — instance resolved by AABB proximity */
    Redirect
};


USTRUCT(BlueprintType)
struct FISMTraceResult
{
    GENERATED_BODY()

    /** The resolved instance handle. Invalid if trace found nothing. */
    UPROPERTY(BlueprintReadOnly, Category = "ISM Trace")
    FISMInstanceHandle Handle;

    /** The underlying physics hit result */
    UPROPERTY(BlueprintReadOnly, Category = "ISM Trace")
    FHitResult PhysicsHit;

    /**
     * How the instance was resolved.
     * Direct = the trace hit the ISM component itself.
     * Redirect = the trace hit a registered redirect component,
     *            instance was resolved via AABB/radius from that hit point.
     */
    UPROPERTY(BlueprintReadOnly, Category = "ISM Trace")
    EISMTraceResolveMethod ResolveMethod = EISMTraceResolveMethod::Direct;

    /** Distance to the resolved instance (may differ from PhysicsHit.Distance for redirects) */
    UPROPERTY(BlueprintReadOnly, Category = "ISM Trace")
    float InstanceDistance = 0.0f;

    bool IsValid() const { return Handle.IsValid(); }
};

