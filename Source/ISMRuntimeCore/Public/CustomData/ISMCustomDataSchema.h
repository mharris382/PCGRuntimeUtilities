#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ISMCustomDataSchema.generated.h"

// ============================================================
//  Schema Channel Definition
// ============================================================

/**
 * Defines how a single PICD channel (or consecutive channels for vectors)
 * maps to a named material parameter.
 *
 * The parameter name must exist on any material that claims to use this schema.
 * On ISM materials: parameter is driven by a PICD sample node (with default value fallback).
 * On converted actor DMIs: parameter is set directly as a scalar or vector param.
 * Same material, same parameter name, same channel index — zero extra setup.
 */
USTRUCT(BlueprintType)
struct ISMRUNTIMECORE_API FISMCustomDataChannelDef
{
    GENERATED_BODY()

    /** First (or only) custom data index this channel occupies */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Channel")
    int32 DataIndex = 0;

    /** Material parameter name this channel drives */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Channel")
    FName ParameterName;

    /** Whether this is a vector parameter (consumes ComponentCount consecutive indices) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Channel")
    bool bIsVector = false;

    /**
     * Number of float channels consumed for vector params (2-4).
     * Indices [DataIndex, DataIndex + ComponentCount) are packed into FLinearColor RGBA.
     * Unused components default to 0.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Channel",
              meta = (EditCondition = "bIsVector", EditConditionHides, ClampMin = "2", ClampMax = "4"))
    int32 ComponentCount = 3;

    /**
     * Optional human-readable description of what this channel controls.
     * Shown as a tooltip in the schema editor.
     * e.g. "RGB color tint applied to albedo"
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Channel")
    FString Description;

    /** Total number of data indices consumed by this channel definition */
    int32 GetWidth() const { return bIsVector ? ComponentCount : 1; }

    TArray<int32> GetOccupiedIndices() const
    {
        TArray<int32> Indices;
        for (int32 i = 0; i < GetWidth(); i++)
        {
            Indices.Add(DataIndex + i);
        }
        return Indices;
	}

    /** True if the given data index falls within this channel's range */
    bool OccupiesIndex(int32 Index) const
    {
        return Index >= DataIndex && Index < DataIndex + GetWidth();
    }
};

// ============================================================
//  Schema
// ============================================================

/**
 * A named PICD-to-material-parameter mapping schema.
 *
 * Defines the contract between:
 *   - How PICD indices are ordered and interpreted
 *   - What material parameter names those indices drive
 *
 * A schema makes no reference to specific materials. Any material that exposes
 * the parameter names defined here is compatible with this schema.
 *
 * Registered globally in UISMRuntimeDeveloperSettings under a unique FName key.
 * Referenced from UISMInstanceDataAsset by SchemaName.
 *
 * Example — a "Standard" project schema:
 *   Index 0,1,2 → "ColorTint"     (Vector3)
 *   Index 3     → "Roughness"     (Scalar)
 *   Index 4     → "EmissiveScale" (Scalar)
 *   Index 5     → "WindInfluence" (Scalar)
 */
USTRUCT(BlueprintType)
struct ISMRUNTIMECORE_API FISMCustomDataSchema
{
    GENERATED_BODY()

    /**
     * Human-readable display name shown in editor dropdowns.
     * Does not need to match the registry key FName.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
    FString DisplayName;

    /**
     * Optional description of what this schema is intended for.
     * Shown as a tooltip in the asset editor dropdown.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
    FString Description;

    /** The channel definitions that make up this schema */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
    TArray<FISMCustomDataChannelDef> Channels;

    /**
     * Which material slot indices this schema applies to.
     * Empty = applies to all slots (most common — same schema drives all slots).
     * Non-empty = only the listed slot indices participate in DMI pooling.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
    TArray<int32> ApplicableSlots;

    // ===== Helpers =====

    /** True if this schema has at least one channel defined */
    bool IsValid() const { return Channels.Num() > 0; }

    /** True if this schema applies to the given material slot */
    bool AppliesToSlot(int32 SlotIndex) const
    {
        return ApplicableSlots.Num() == 0 || ApplicableSlots.Contains(SlotIndex);
    }

    /**
     * Get the channel definition that occupies a given data index.
     * Returns nullptr if no channel covers that index.
     */
    const FISMCustomDataChannelDef* FindChannelForIndex(int32 DataIndex) const;

    /**
     * Get all data indices that participate in DMI pool signature building.
     * Only these indices are compared when determining if two instances share a DMI.
     * Indices not covered by any channel are ignored for pooling.
     */
    TArray<int32> GetMappedIndices() const;

    /**
     * Extract only the mapped values from a full custom data array.
     * Used to build the pool signature — unmapped indices are excluded.
     */
    TArray<float> ExtractMappedValues(const TArray<float>& FullCustomData) const;
};

// ============================================================
//  Project Settings
// ============================================================

/**
 * Project-wide ISM Runtime configuration.
 * Accessible via Project Settings → Plugins → ISM Runtime.
 *
 * Stores:
 *   - The global PICD schema registry (FName → FISMCustomDataSchema)
 *   - The project default schema name
 *   - Global DMI pool sizing defaults
 *   - Hot DMI pool sizing defaults
 *
 * INI section: [/Script/ISMRuntimeCore.ISMRuntimeDeveloperSettings]
 * INI file: DefaultGame.ini
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "ISM Runtime"))
class ISMRUNTIMECORE_API UISMRuntimeDeveloperSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UISMRuntimeDeveloperSettings();

    // ===== Schema Registry =====

        /**
     * The schema used when no SchemaName is specified on the InstanceDataAsset
     * and bUsePICDConversion is true.
     * Must match a key in SchemaRegistry, or be NAME_None to disable default behavior.
     */
    UPROPERTY(Config, EditAnywhere, Category = "PICD Schemas",
        meta = (DisplayName = "Default Schema",
            ToolTip = "Schema applied when no explicit SchemaName is set. \nSet to None to require explicit schema assignment."))
    FName DefaultSchemaName = NAME_None;

    UPROPERTY(Config, EditAnywhere, Category = "PICD Schemas",
        meta = (DisplayName = "Default Schema",
            ToolTip = "Schema applied when no explicit SchemaName is set. \nSet to None to require explicit schema assignment."))
    FISMCustomDataSchema DefaultSchema; // Cached pointer to the default schema for quick access at runtime


    /**
     * Global PICD schema registry.
     * Key: unique FName identifier used by UISMInstanceDataAsset::SchemaName
     * Value: the schema definition
     *
     * Editor: displayed as an editable map with a custom detail panel.
     * Entries here populate the dropdown in UISMInstanceDataAsset.
     */
    UPROPERTY(Config, EditAnywhere, Category = "PICD Schemas",
              meta = (DisplayName = "PICD Schema Registry",
                      ToolTip = "Define your project's PICD channel mapping schemas here.\n Each schema is identified by an FName key and can be referenced \n from any ISM Instance Data Asset."))
    TMap<FName, FISMCustomDataSchema> SchemaRegistry;





    // ===== DMI Pool Defaults =====

    /**
     * Default maximum shared DMI pool size per material template.
     * 0 = unlimited. Can be overridden per-batch in FISMAnimBatchConfig.
     */
    UPROPERTY(Config, EditAnywhere, Category = "DMI Pool",
              meta = (DisplayName = "Max Shared Pool Size Per Template", ClampMin = "0"))
    int32 DefaultMaxSharedPoolSize = 0;

    /**
     * Default number of hot DMI slots pre-warmed per material template.
     * Tune to your expected max concurrent animated instances project-wide.
     */
    UPROPERTY(Config, EditAnywhere, Category = "DMI Pool",
              meta = (DisplayName = "Default Hot Pool Size Per Template", ClampMin = "1", ClampMax = "128"))
    int32 DefaultHotPoolSizePerTemplate = 8;

    /**
     * Default number of frames before stale shared DMI pool entries are evicted.
     * 0 = never auto-evict (manual only).
     */
    UPROPERTY(Config, EditAnywhere, Category = "DMI Pool",
              meta = (DisplayName = "DMI Eviction Age (Frames)", ClampMin = "0"))
    int32 DefaultEvictionAgeFrames = 600;

    // ===== Helpers (callable at runtime, no subsystem needed) =====

    /** Get the singleton settings instance */
    static const UISMRuntimeDeveloperSettings* Get()
    {
        return GetDefault<UISMRuntimeDeveloperSettings>();
    }

    /**
     * Resolve a schema by name. Returns nullptr if not found.
     * Passing NAME_None returns the default schema (if configured).
     */
    const FISMCustomDataSchema* ResolveSchema(FName SchemaName) const;

    /**
     * Get the default schema. Returns nullptr if DefaultSchemaName is NAME_None
     * or doesn't match any registered schema.
     */
    const FISMCustomDataSchema* GetDefaultSchema() const;

    /** True if a schema with the given name exists in the registry */
    bool HasSchema(FName SchemaName) const { return SchemaRegistry.Contains(SchemaName); }

    /** Get all registered schema names (for editor dropdown population) */
    TArray<FName> GetAllSchemaNames() const;

#if WITH_EDITOR
    virtual FText GetSectionText() const override;
    virtual FText GetSectionDescription() const override;
    virtual FName GetCategoryName() const override;
#endif

private:
	bool bInitializedDefaultSchema = false;
	bool HasInitializedDefaultSchema() const { return bInitializedDefaultSchema && SchemaRegistry.Contains(DefaultSchemaName); }
};