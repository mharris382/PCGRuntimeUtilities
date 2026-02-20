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
	if(const FISMCustomDataSchema* FoundSchema = SchemaRegistry.Find(SchemaName))
	{
		return &SchemaRegistry[SchemaName];
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
	SchemaRegistry.GetKeys(Names);
	return Names;
}

#pragma region WITH_EDITOR

FText UISMRuntimeDeveloperSettings::GetSectionText() const
{
	return FText::FromString(TEXT("ISM Runtime Materials Schemas"));
}

FText UISMRuntimeDeveloperSettings::GetSectionDescription() const
{
	return FText::FromString(TEXT("Define mapping schemas that tell ISMRuntimeUtils how to visually transfer Per instance Custom Data from ISM onto Dynamic Material Instances"));
}

FName UISMRuntimeDeveloperSettings::GetCategoryName() const
{
	return FName(TEXT("ISM Materials Schemas"));
}

#pragma endregion

