// ISMSelectionSet.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "ISMInstanceHandle.h"
#include "Delegates/DelegateCombinations.h"
#include "ISMQueryFilter.h"
#include "ISMSelectionSet.generated.h"

class UISMRuntimeComponent;

/**
 * How multi-instance selection is accumulated.
 */
UENUM(BlueprintType)
enum class EISMSelectionMode : uint8
{
    /** Replace current selection entirely */
    Replace,

    /** Add to existing selection */
    Add,

    /** Remove from existing selection */
    Remove,

    /** Toggle each instance (add if not selected, remove if selected) */
    Toggle
};

/**
 * Delegate fired when selection contents change.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnISMSelectionChanged, const TArray<FISMInstanceHandle>&, CurrentSelection);

/**
 * Manages an ordered set of ISM instance handles representing the player's
 * current selection. Provides batch operations used by the interaction module
 * and the preview conversion system.
 *
 * Designed to be either:
 *   (a) A component on the player/controller — owns persistent selection state
 *   (b) A transient struct owned by a preview context — scoped to one operation
 *
 * Does NOT implement highlight rendering or UI. It writes selection state to
 * ISMRuntimeComponent per-instance tags so the material/debugger can react.
 * The tag used is configurable so games can drive their own highlight materials.
 */
UCLASS(Blueprintable, ClassGroup = (ISMRuntime), meta = (BlueprintSpawnableComponent))
class ISMRUNTIMEINTERACTION_API UISMSelectionSet : public UActorComponent
{
    GENERATED_BODY()

public:
    UISMSelectionSet();

    // ===== Configuration =====

    /**
     * Tag written to selected instances so materials and other systems
     * can react without polling this component.
     * Default maps to "ISM.State.Selected" — define this tag in your project.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Selection")
    FGameplayTag SelectedTag;

    /**
     * Maximum number of instances that can be selected simultaneously.
     * -1 = unlimited.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ISM Selection", meta = (ClampMin = "-1"))
    int32 MaxSelection = -1;

    // ===== Selection Operations =====

    /**
     * Select a single instance.
     * @param Handle    The instance to select
     * @param Mode      How to combine with existing selection
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Selection")
    void SelectInstance(const FISMInstanceHandle& Handle, EISMSelectionMode Mode = EISMSelectionMode::Replace);

    /**
     * Select multiple instances at once.
     * More efficient than calling SelectInstance in a loop — tag writes are batched.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Selection")
    void SelectInstances(const TArray<FISMInstanceHandle>& Handles, EISMSelectionMode Mode = EISMSelectionMode::Replace);

    /**
     * Select all instances within radius that pass the filter.
     * Delegates to the subsystem for cross-component queries.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Selection")
    void SelectInstancesInRadius(const FVector& Center, float Radius,
        const FISMQueryFilter& Filter, EISMSelectionMode Mode = EISMSelectionMode::Replace);

    /**
     * Select all instances whose AABB overlaps the given box.
     * Intended for box-select / drag-select workflows.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Selection")
    void SelectInstancesInBox(const FBox& Box,
        const FISMQueryFilter& Filter, EISMSelectionMode Mode = EISMSelectionMode::Replace);

    /** Deselect a single instance */
    UFUNCTION(BlueprintCallable, Category = "ISM Selection")
    void DeselectInstance(const FISMInstanceHandle& Handle);

    /** Deselect all instances */
    UFUNCTION(BlueprintCallable, Category = "ISM Selection")
    void ClearSelection();

    // ===== Queries =====

    UFUNCTION(BlueprintCallable, Category = "ISM Selection")
    bool IsSelected(const FISMInstanceHandle& Handle) const;

    UFUNCTION(BlueprintCallable, Category = "ISM Selection")
    int32 GetSelectionCount() const { return SelectedHandles.Num(); }

    UFUNCTION(BlueprintCallable, Category = "ISM Selection")
    bool IsEmpty() const { return SelectedHandles.IsEmpty(); }

    UFUNCTION(BlueprintCallable, Category = "ISM Selection")
    const TArray<FISMInstanceHandle>& GetSelectedHandles() const { return SelectedHandles; }

    /** Returns only selected handles that are still valid (component not destroyed, etc.) */
    UFUNCTION(BlueprintCallable, Category = "ISM Selection")
    TArray<FISMInstanceHandle> GetValidSelectedHandles() const;

    // ===== Batch Operations =====

    /**
     * Destroy all selected instances.
     * Selection is cleared after destruction.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Selection|Batch")
    void DestroySelected();

    /**
     * Convert all selected instances to actors using the given context.
     * Returns the converted actors. Selection is NOT cleared — handles remain
     * valid and track the converted state.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Selection|Batch")
    TArray<AActor*> ConvertSelectedToActors(const FISMConversionContext& Context);

    /**
     * Return all selected converted instances back to ISM.
     * Only affects instances that are currently converted.
     */
    UFUNCTION(BlueprintCallable, Category = "ISM Selection|Batch")
    void ReturnSelectedToISM(bool bDestroyActors = true, bool bUpdateTransforms = true);

    // ===== Events =====

    /** Fired whenever the selection contents change */
    UPROPERTY(BlueprintAssignable, Category = "ISM Selection|Events")
    FOnISMSelectionChanged OnSelectionChanged;

protected:
    /** The current selection, ordered by selection time (oldest first) */
    UPROPERTY(BlueprintReadOnly, Category = "ISM Selection")
    TArray<FISMInstanceHandle> SelectedHandles;

    /** Write or remove the SelectedTag on an instance */
    void ApplySelectionTag(const FISMInstanceHandle& Handle, bool bAdd) const;

    /** Apply tag changes for a batch of handles efficiently */
    void ApplySelectionTagBatch(const TArray<FISMInstanceHandle>& Handles, bool bAdd) const;

    /** Remove stale handles (component destroyed, instance destroyed, etc.) */
    void PruneInvalidHandles();

    /** Broadcast OnSelectionChanged */
    void BroadcastSelectionChanged();

    /** Enforce MaxSelection, removing oldest entries if needed */
    void EnforceSelectionLimit();
};