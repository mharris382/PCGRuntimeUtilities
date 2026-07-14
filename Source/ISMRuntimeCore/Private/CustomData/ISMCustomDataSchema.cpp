#include "CustomData/ISMCustomDataSchema.h"
#include "Logging/LogMacros.h"



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



#pragma region WITH_EDITOR



#pragma endregion

