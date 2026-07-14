// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"



#include "CustomData/ISMCustomDataSchema.h"
#include "ISMRuntimeSchemaSettings.generated.h"
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
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "ISM Runtime CustomData"))
class ISMRUNTIMECORE_API UISMRuntimeDeveloperSettings : public UISMRuntimeSettingsBase
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



 


    UPROPERTY(Config, EditAnywhere, Category = "PICD Schemas", meta = (DisplayName = "PICD Schema Registry", ToolTip = ""))
    TSoftObjectPtr<UDataTable> HandlerDatabase;


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
    bool HasSchema(FName SchemaName) const 
    { 
        if(!HandlerDatabase.IsValid())
        {
            return false;
        }
        else
        {
			bool found = HandlerDatabase->FindRow<FISMCustomDataSchema>(SchemaName, TEXT(""), false) != nullptr;
			return found;
        }
    }

    /** Get all registered schema names (for editor dropdown population) */
    TArray<FName> GetAllSchemaNames() const;

#if WITH_EDITOR
    virtual FText GetSectionText() const override { return FText::FromString(TEXT("ISM Runtime Materials Schemas")); }
    virtual FText GetSectionDescription() const override { return FText::FromString(TEXT("Define mapping schemas that tell ISMRuntimeUtils how to visually transfer Per instance Custom Data from ISM onto Dynamic Material Instances")); }
#endif

private:
	bool bInitializedDefaultSchema = false;
	bool HasInitializedDefaultSchema() const { return bInitializedDefaultSchema ; }
};