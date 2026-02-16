#include "ISMResourceDataAsset.h"

float UISMResourceDataAsset::GetSpeedMultiplier(const FGameplayTagContainer& CollectorTags) const
{
    float Multiplier = 1.0f;

    for (const auto& Pair : CollectorSpeedModifiers)
    {
        if (CollectorTags.HasTag(Pair.Key))
        {
            Multiplier *= Pair.Value;
        }
    }

    return FMath::Max(Multiplier, 0.01f); // Prevent zero or negative
}

float UISMResourceDataAsset::GetYieldMultiplier(const FGameplayTagContainer& CollectorTags) const
{
    float Multiplier = 1.0f;

    for (const auto& Pair : CollectorYieldModifiers)
    {
        if (CollectorTags.HasTag(Pair.Key))
        {
            Multiplier *= Pair.Value;
        }
    }

    return FMath::Max(Multiplier, 0.0f); // Prevent negative
}