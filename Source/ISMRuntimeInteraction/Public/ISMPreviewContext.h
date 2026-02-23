// ISMPreviewContext.h
#pragma once

#include "CoreMinimal.h"
#include "Interfaces/ISMConvertible.h"
#include "ISMInstanceHandle.h"
#include "ISMSelectionSet.h"
#include "Delegates/DelegateCombinations.h"
#include "ISMPreviewContext.generated.h"

class UISMRuntimeComponent;

/**
 * What kind of pending action the preview represents.
 */
UENUM(BlueprintType)
enum class EISMPreviewType : uint8
{
    /** Preview of destroying selected instances (e.g. Satisfactory-style deletion) */
    Destroy,

    /**
     * Preview of placing a new instance that doesn't exist yet.
     * A reserved/pending instance handle is maintained in the spatial index
     * so other queries can discover the pending placement.
     */
    Placement,

    /**
     * Preview of moving existing selected instances to a new location.
     * Source instances are hidden, preview actors shown at destination.
     */
    Move
};

/**
 * Current state of the preview operation.
 */
UENUM(BlueprintType)
enum class EISMPreviewState : uint8
{
    /** Preview is active and awaiting confirmation or cancellation */
    Pending,

    /** Preview was confirmed — destructive operations have been applied */
    Confirmed,

    /** Preview was cancelled — all instances restored to original state */
    Cancelled
};

/**
 * Delegate fired when a preview is confirmed or cancelled.
 * @param PreviewType   What kind of preview this was
 * @param State         Whether it was confirmed or cancelled
 * @param AffectedHandles  The handles involved in the operation
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnISMPreviewResolved,
    EISMPreviewType, PreviewType,
    EISMPreviewState, State,
    const TArray<FISMInstanceHandle>&, AffectedHandles);

/**
 * Wraps a selection set into a coherent pending operation with confirm/cancel
 * semantics. Manages the lifecycle of preview actors and reserved instance slots.
 *
 * DESTROY PREVIEW flow:
 *   1. Caller populates a SelectionSet with target instances
 *   2. Construct FISMPreviewContext (Destroy type) from the selection set
 *   3. BeginPreview() converts instances to preview actors (ghost material)
 *   4. Confirm() destroys the original ISM instances, cleans up preview actors
 *   5. Cancel() returns instances to ISM, restoring original state
 *
 * PLACEMENT PREVIEW flow:
 *   1. Caller provides a desired transform and source component
 *   2. BeginPreview() adds a Reserved instance to the spatial index
 *      (other queries can discover it, AABB is valid immediately)
 *   3. UpdatePlacementTransform() moves the preview each frame cheaply
 *   4. Confirm() clears Reserved flag, instance becomes real
 *   5. Cancel() removes the reserved instance entirely
 *
 * This component is intended to be short-lived — created when a preview
 * begins, destroyed when it resolves. It does not persist between operations.
 */
UCLASS(Blueprintable, ClassGroup = (ISMRuntime), meta = (BlueprintSpawnableComponent))
class ISMRUNTIMEINTERACTION_API UISMPreviewContext : public UActorComponent
{
    GENERATED_BODY()

public:
    UISMPreviewContext();

    // ===== Configuration =====

    /**
     * Actor class to spawn as the preview mesh.
     * Should use a ghost/translucent material. If null, the interaction
     * component's default preview actor class is used.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Preview")
    TSubclassOf<AActor> PreviewActorClass;

    /**
     * Custom data index used to signal "preview mode" to the preview actor's
     * material. The interaction module sets this value on spawn; the material
     * drives the ghost effect from it.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Preview")
    int32 PreviewMaterialDataIndex = 0;

    /**
     * Value written to PreviewMaterialDataIndex to activate ghost appearance.
     * 1.0 = preview, 0.0 = normal.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Preview")
    float PreviewMaterialDataValue = 1.0f;

    // ===== Lifecycle =====

    /**
     * Begin a destroy or move preview using the given selection set.
     * Converts selected instances to preview actors.
     * @param InSelectionSet    The instances to preview. Must not be empty.
     * @param InPreviewType     Destroy or Move
     * @return True if preview started successfully
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Preview")
    bool BeginPreview(UISMSelectionSet* InSelectionSet, EISMPreviewType InPreviewType);

    /**
     * Begin a placement preview for a new instance that doesn't exist yet.
     * Adds a reserved instance to the spatial index so the pending placement
     * is discoverable by overlap queries.
     * @param TargetComponent   The component to add the reserved instance to
     * @param InitialTransform  Starting world transform for the preview
     * @return True if preview started successfully
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Preview")
    bool BeginPlacementPreview(UISMRuntimeComponent* TargetComponent, const FTransform& InitialTransform);

    /**
     * Update the placement preview transform each frame.
     * Only valid during a Placement preview. Cheap — moves the preview actor
     * and updates the reserved instance transform in the spatial index.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Preview")
    void UpdatePlacementTransform(const FTransform& NewTransform);

    /**
     * Confirm the pending operation.
     * Destroy preview: destroys original instances, removes preview actors.
     * Placement preview: clears Reserved flag, instance becomes permanent.
     * Move preview: updates instance transforms, removes preview actors.
     * Fires OnPreviewResolved with State = Confirmed.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Preview")
    void Confirm();

    /**
     * Cancel the pending operation.
     * All instances are restored to their original state.
     * Preview actors are removed.
     * Fires OnPreviewResolved with State = Cancelled.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Preview")
    void Cancel();

    // ===== State Queries =====

    UFUNCTION(BlueprintCallable, Category = "ISM Preview")
    EISMPreviewState GetPreviewState() const { return PreviewState; }

    UFUNCTION(BlueprintCallable, Category = "ISM Preview")
    EISMPreviewType GetPreviewType() const { return PreviewType; }

    UFUNCTION(BlueprintCallable, Category = "ISM Preview")
    bool IsPending() const { return PreviewState == EISMPreviewState::Pending; }

    /** Get the reserved handle for a placement preview (invalid for other types) */
    UFUNCTION(BlueprintCallable, Category = "ISM Preview")
    FISMInstanceHandle GetPlacementHandle() const { return PlacementHandle; }

    /** Get all handles involved in this preview (source instances) */
    UFUNCTION(BlueprintCallable, Category = "ISM Preview")
    const TArray<FISMInstanceHandle>& GetPreviewHandles() const { return PreviewHandles; }

    /** Get all spawned preview actors */
    UFUNCTION(BlueprintCallable, Category = "ISM Preview")
    const TArray<AActor*> GetPreviewActors() const;

    // ===== Events =====

    /** Fired when the preview is confirmed or cancelled */
    UPROPERTY(BlueprintAssignable, Category = "ISM Preview|Events")
    FOnISMPreviewResolved OnPreviewResolved;

protected:
    // ===== Internal State =====

    UPROPERTY()
    EISMPreviewType PreviewType = EISMPreviewType::Destroy;

    UPROPERTY()
    EISMPreviewState PreviewState = EISMPreviewState::Cancelled;

    /** Handles involved in this preview operation (source instances) */
    UPROPERTY()
    TArray<FISMInstanceHandle> PreviewHandles;

    /** Spawned preview actors, one per handle (parallel arrays) */
    UPROPERTY()
    TArray<TWeakObjectPtr<AActor>> PreviewActors;

    /**
     * For placement previews: the reserved instance handle in the spatial index.
     * Other systems can query and discover this as a pending occupant.
     */
    UPROPERTY()
    FISMInstanceHandle PlacementHandle;

    /** The selection set this preview was built from (weak ref, not owned) */
    UPROPERTY()
    TWeakObjectPtr<UISMSelectionSet> SourceSelectionSet;

    // ===== Internal Helpers =====

    /** Spawn a preview actor for the given handle */
    AActor* SpawnPreviewActorForHandle(const FISMInstanceHandle& Handle);

    /** Remove and destroy all preview actors */
    void CleanupPreviewActors();

    /** Apply the preview material data value to a spawned preview actor */
    void ApplyPreviewMaterial(AActor* PreviewActor) const;

    /** Validate that the preview is in Pending state before an operation */
    bool EnsurePending(const FString& OperationName) const;
};