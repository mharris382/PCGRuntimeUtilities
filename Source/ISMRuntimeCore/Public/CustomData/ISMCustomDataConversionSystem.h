#pragma once

#include "CoreMinimal.h"
#include "ISMInstanceHandle.h"
#include "CustomData/ISMCustomDataSchema.h"
#include "ISMCustomDataConversionSystem.generated.h"

// Forward declarations
class UISMCustomDataSubsystem;
class UISMRuntimeComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
struct FISMCustomDataSchema;

/**
 * Result of a PICD conversion attempt.
 * Returned by UISMCustomDataConversionSystem::ResolveConversion for diagnostics
 * and to pass resolved data to the actor.
 */
USTRUCT()
struct ISMRUNTIMECORE_API FISMCustomDataConversionResult
{
    GENERATED_BODY()

    /** Whether conversion succeeded and a DMI was resolved */
    bool bSuccess = false;

    /**
     * Reason conversion was skipped or failed.
     * Empty string on success.
     */
    FString SkipReason;

    /** The schema that was used (nullptr if skipped) */
    const FISMCustomDataSchema* ResolvedSchema = nullptr;

    /** Schema name that was resolved (for diagnostics) */
    FName ResolvedSchemaName = NAME_None;

    /**
     * Resolved DMIs per material slot.
     * Key: slot index on the converted actor's mesh
     * Value: pooled DMI ready to apply
     * Empty on failure.
     */
    TMap<int32, UMaterialInstanceDynamic*> DMIsBySlot;

    /** Whether a new DMI was created (cache miss) or reused (cache hit) */
    bool bCacheHit = false;
};

/**
 * Stateless utility system responsible for the full PICD → DMI resolution
 * pipeline at instance conversion time.
 *
 * Called by the conversion path in ISMRuntimeCore (and ISMRuntimePhysics)
 * when an instance transitions from ISM to a physics/gameplay actor.
 *
 * Resolution order:
 *   1. Check bUsePICDConversion on InstanceDataAsset → skip if false
 *   2. Resolve schema: InstanceDataAsset::SchemaName → project default → skip if none
 *   3. For each applicable material slot on the ISM component:
 *      a. Get the material currently on that slot as the DMI template
 *      b. Build pool signature from instance PICD + schema mapped indices
 *      c. Get or create pooled DMI from UISMCustomDataSubsystem
 *   4. Offer resolved DMIs to the converted actor via IISMCustomDataMaterialProvider
 *      → actor accepts, overrides, or ignores each slot independently
 *   5. For slots not handled by the interface, apply DMI directly to
 *      the first UMeshComponent found on the actor
 *
 * This class has no state — all inputs come in, results go out.
 * Instance as a UObject only for UE reflection; all methods are static.
 */
UCLASS()
class ISMRUNTIMECORE_API UISMCustomDataConversionSystem : public UObject
{
    GENERATED_BODY()

public:
    /**
     * Full pipeline: resolve schema, build DMIs, apply to actor.
     * The primary entry point called by the conversion system.
     *
     * @param InstanceHandle    The instance being converted (provides PICD, component ref)
     * @param ConvertedActor    The newly spawned actor to receive materials
     * @param World             World context for subsystem access
     * @return                  Result struct with success state and diagnostics
     */
    static FISMCustomDataConversionResult ResolveAndApply(
        const FISMInstanceHandle& InstanceHandle,
        AActor* ConvertedActor,
        UWorld* World);

    /**
     * Resolve DMIs without applying them to any actor.
     * Useful for pre-resolving materials before an actor is spawned,
     * or for updating an existing actor's materials after PICD changes.
     *
     * @param InstanceHandle    The instance to resolve materials for
     * @param World             World context
     * @return                  Result with resolved DMIs (not yet applied to anything)
     */
    static FISMCustomDataConversionResult ResolveDMIs(
        const FISMInstanceHandle& InstanceHandle,
        UWorld* World);

    /**
     * Apply a previously resolved conversion result to an actor.
     * Separated from ResolveDMIs to allow deferred application.
     *
     * Calls IISMCustomDataMaterialProvider on the actor for each slot.
     * Falls back to direct mesh component application if interface not implemented.
     *
     * @param Result            Previously resolved conversion result
     * @param ConvertedActor    Actor to apply materials to
     */
    static void ApplyToActor(
        const FISMCustomDataConversionResult& Result,
        AActor* ConvertedActor);

    /**
     * Re-apply materials after PICD values change on a live converted actor.
     * Called by ISMWriteInstanceCustomData when an instance is currently converted.
     *
     * Resolves new DMIs based on updated CachedCustomData and applies them.
     * If the new signature matches the existing DMI (values didn't change the
     * mapped channels), the existing DMI is reused with no pool lookup.
     *
     * @param InstanceHandle    Handle with updated CachedCustomData
     * @param ConvertedActor    The live actor to update
     * @param World             World context
     */
    static void RefreshActorMaterials(
        const FISMInstanceHandle& InstanceHandle,
        AActor* ConvertedActor,
        UWorld* World);

    /**
     * Determine whether a given instance should attempt PICD conversion.
     * Checks bUsePICDConversion, schema resolvability, and PICD data validity.
     * Fast path used before any DMI work begins.
     */
    static bool ShouldAttemptConversion(
        const FISMInstanceHandle& InstanceHandle,
        FString* OutSkipReason = nullptr);

private:
    /**
     * Resolve the schema for a given instance.
     * Checks InstanceDataAsset::SchemaName → project default → nullptr.
     *
     * @param InstanceHandle    Instance to resolve schema for
     * @param OutSchemaName     Filled with the resolved schema's registry name
     * @return                  The schema, or nullptr if none resolvable
     */
    static const FISMCustomDataSchema* ResolveSchema(
        const FISMInstanceHandle& InstanceHandle,
        FName& OutSchemaName);

    /**
     * Get material slots to process for a given schema and ISM component.
     * Respects FISMCustomDataSchema::ApplicableSlots (empty = all slots).
     */
    static TArray<int32> GetApplicableSlots(
        const FISMCustomDataSchema& Schema,
        UISMRuntimeComponent* Component);

    /**
     * Get or create a pooled DMI for one material slot.
     *
     * @param Template          The ISM's material for this slot (used as DMI template)
     * @param CustomData        Full PICD values for the instance
     * @param Schema            The resolved schema
     * @param SlotIndex         Which slot this DMI is for
     * @param Subsystem         The DMI pool subsystem
     * @param bOutCacheHit      Set to true if an existing DMI was reused
     * @return                  A valid DMI, or nullptr if Template is null
     */
    static UMaterialInstanceDynamic* ResolvePooledDMI(
        UMaterialInterface* Template,
        const TArray<float>& CustomData,
        const FISMCustomDataSchema& Schema,
        int32 SlotIndex,
        UISMCustomDataSubsystem* Subsystem,
        bool& bOutCacheHit);

    /**
     * Apply a DMI to an actor for a given slot.
     * Tries IISMCustomDataMaterialProvider first, falls back to direct mesh application.
     *
     * @return true if the material was applied by any means
     */
    static bool ApplyDMIToActorSlot(
        AActor* Actor,
        int32 SlotIndex,
        UMaterialInstanceDynamic* DMI);

    /**
     * Fallback: find the first UMeshComponent on an actor and apply material directly.
     * Used when the actor doesn't implement IISMCustomDataMaterialProvider.
     */
    static bool ApplyDMIToFirstMeshComponent(
        AActor* Actor,
        int32 SlotIndex,
        UMaterialInstanceDynamic* DMI);
};