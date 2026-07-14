// Fill out your copyright notice in the Description page of Project Settings.


#include "Settings/ISMRuntimeSchemaSettings.h"



UISMRuntimeDeveloperSettings::UISMRuntimeDeveloperSettings(){	}

const FISMCustomDataSchema* UISMRuntimeDeveloperSettings::ResolveSchema(FName SchemaName) const
{
    if (SchemaName == DefaultSchemaName || SchemaName == NAME_None)
    {
        return &DefaultSchema;
    }
    if (HandlerDatabase.IsValid())
    {
        if(const FISMCustomDataSchema* FoundRow = HandlerDatabase->FindRow<FISMCustomDataSchema>(SchemaName, TEXT(""), false))
        {
            return FoundRow;
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Schema '%s' not found in external database. Falling back to registry."), *SchemaName.ToString());
        }
    }
    UE_LOG(LogTemp, Warning, TEXT("Schema '%s' not found in registry. Falling back to default schema."), *SchemaName.ToString());
    return &DefaultSchema;
}

const FISMCustomDataSchema* UISMRuntimeDeveloperSettings::GetDefaultSchema() const
{
    return &DefaultSchema;
}

TArray<FName> UISMRuntimeDeveloperSettings::GetAllSchemaNames() const
{
    TArray<FName> Names = TArray<FName>();
    if (HandlerDatabase.IsValid())
    {
        auto db = HandlerDatabase.LoadSynchronous();
        if (db)
        {
            Names.Append(db->GetRowNames());
        }
    }
    Names.Add(DefaultSchemaName);
    return Names;
}
