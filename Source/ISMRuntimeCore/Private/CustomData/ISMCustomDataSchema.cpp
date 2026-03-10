#include "CustomData/ISMCustomDataSchema.h"
#include "Logging/LogMacros.h"

UISMRuntimeDeveloperSettings::UISMRuntimeDeveloperSettings()
{
	
}

const FISMCustomDataSchema* UISMRuntimeDeveloperSettings::ResolveSchema(FName SchemaName) const
{
	if (SchemaName == DefaultSchemaName || SchemaName == NAME_None)
	{
		return &DefaultSchema;
	}
	if (bUseExternalSchemaDatabase && HandlerDatabase.IsValid())
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
	if(SchemaRegistry.Contains(SchemaName))
	{
		 const FISMCustomDataSchema* schema = &SchemaRegistry[SchemaName];
		 return schema;
	}

	UE_LOG(LogTemp, Warning, TEXT("Schema '%s' not found in registry. Falling back to default schema."), *SchemaName.ToString());
	return &DefaultSchema;
}

const FISMCustomDataSchema* UISMRuntimeDeveloperSettings::GetDefaultSchema() const
{
	return &DefaultSchema;
}

const FISMCustomDataChannelDef* FISMCustomDataSchema::FindChannelForIndex(int32 DataIndex) const
{
	if (DataIndex < 0 || DataIndex >= Channels.Num())
	{
		return nullptr;
	}
	return &Channels[DataIndex];
}

TArray<int32> FISMCustomDataSchema::GetMappedIndices() const
{
	TArray<int32> MappedIndices;
	int32 cnt = 0;
	for (size_t i = 0; i < Channels.Num(); i++)
	{
		cnt+= Channels[i].GetWidth();
		MappedIndices.Append(Channels[i].GetOccupiedIndices());
	}
	return MappedIndices;
}

TArray<float> FISMCustomDataSchema::ExtractMappedValues(const TArray<float>& FullCustomData) const
{
	TArray<int32> MappedIndices = GetMappedIndices();
	TArray<float> MappedValues;
	for (int i = 0; i < MappedIndices.Num(); i++)
	{
		int32 idx = MappedIndices[i];
		if(FullCustomData.IsValidIndex(idx))
		{
			MappedValues.Add(FullCustomData[idx]);
		}
		else
		{
			MappedValues.Add(0.f); // Default to 0 for out-of-bounds indices
			UE_LOG(LogTemp, Warning, TEXT("FullCustomData does not contain index %d required by schema. Defaulting to 0."), idx);
		}
	}
	return MappedValues;
}


TArray<FName> UISMRuntimeDeveloperSettings::GetAllSchemaNames() const
{
	TArray<FName> Names;
	if (bUseExternalSchemaDatabase && HandlerDatabase.IsValid())
	{
		TArray<FName> DatabaseNames = HandlerDatabase.Get()->GetRowNames();
		Names.Append(DatabaseNames);
	}
	TArray<FName> KeyNames;
	SchemaRegistry.GetKeys(KeyNames);
	for (FName& Name : KeyNames)
	{
		if (!Names.Contains(Name))
		{
			Names.Add(Name);
		}
	}
	return Names;
}

#pragma region WITH_EDITOR



#pragma endregion

