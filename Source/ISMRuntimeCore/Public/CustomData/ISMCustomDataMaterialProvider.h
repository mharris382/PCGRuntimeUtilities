#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ISMCustomDataMaterialProvider.generated.h"

UINTERFACE(MinimalAPI, BlueprintType)
class UISMCustomDataMaterialProvider : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface implemented by actors that want to participate in PICD-to-DMI material resolution
 * when converted from an ISM instance.
 *
 * The conversion system (UISMCustomDataConversionSystem) uses the ISM's own material as the
 * DMI template and the instance's CachedCustomData to resolve a pooled DMI. This interface
 * lets the converted actor intercept that process per-slot — accepting, overriding, or
 * ignoring DMI assignment independently per slot.
 *
 * Implementing this interface is optional. If not implemented:
 *   - The system finds the first UMeshComponent on the actor
 *   - Calls SetMaterial(SlotIndex, DMI) directly
 *
 * Implement this when:
 *   - Your actor has multiple mesh components
 *   - You want to blend or modify the resolved DMI before applying
 *   - You want to skip DMI application for specific slots
 *   - Your actor manages materials through a non-standard pipeline
 */
class ISMRUNTIMECORE_API IISMCustomDataMaterialProvider
{
    GENERATED_BODY()

public:
    /**
     * Apply a resolved pooled DMI to the given mesh slot.
     *
     * The DMI has already been parameterized from the instance's custom data
     * and schema mappings. The actor may accept it as-is, modify it, or ignore it.
     *
     * Returning without calling SetMaterial is valid — it means the actor is
     * managing that slot's material through its own logic.
     *
     * @param SlotIndex   The material slot being resolved
     * @param DMI         The pooled, parameterized DMI ready to apply
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ISM Custom Data")
    void ApplyDMIToSlot(int32 SlotIndex, UMaterialInstanceDynamic* DMI);
    virtual void ApplyDMIToSlot_Implementation(int32 SlotIndex, UMaterialInstanceDynamic* DMI) {}

    /**
     * Return the number of material slots this actor exposes for DMI resolution.
     * The conversion system iterates [0, GetMaterialSlotCount()) and calls ApplyDMIToSlot
     * for each slot covered by the schema's ApplicableSlots.
     *
     * Default: 1. Override for multi-material meshes.
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ISM Custom Data")
    int32 GetMaterialSlotCount() const;
    virtual int32 GetMaterialSlotCount_Implementation() const { return 1; }

    /**
     * Optional: return true if this actor wants to skip DMI application for a specific slot.
     * Called before ApplyDMIToSlot — returning true skips the call entirely.
     *
     * Default: false (all applicable slots receive DMI offers).
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ISM Custom Data")
    bool ShouldSkipSlot(int32 SlotIndex) const;
    virtual bool ShouldSkipSlot_Implementation(int32 SlotIndex) const { return false; }
};