#include "ISMPhysicsDataAsset.h"
#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include "ISMPoolDataAsset.h"

void UISMPhysicsDataAsset::GetRecommendedPoolSettings(int32& OutInitialSize, int32& OutGrowSize) const
{
	OutGrowSize = PoolGrowSize;
	OutInitialSize = InitialPoolSize;
}

void UISMPhysicsDataAsset::LogRestingCheck(const AActor* Owner, float LinearVelocity, float AngularVelocity, bool passed)
{
	if(bLogRestingChecks)
	{
		FString OwnerName = Owner ? Owner->GetName() : TEXT("Unknown Actor");
		FString AssetName = GetName();
		FString ResultText = passed ? TEXT("PASSED") : TEXT("FAILED");
		OwnerName = OwnerName + ":" + AssetName + "Resting Check (" + ResultText + ") - ";
		if (bCheckAngularVelocity)
		{
			UE_LOG(LogTemp, Verbose, TEXT("%s Linear Velocity: %.2f, Angular Velocity: %.2f, Thresholds - Linear: %.2f, Angular: %.2f"),
				*OwnerName, LinearVelocity, AngularVelocity, RestingVelocityThreshold, RestingAngularThreshold);
		}

		else {
			UE_LOG(LogTemp, Verbose, TEXT("%s Linear Velocity: %.2f, Threshold: %.2f"),
				*OwnerName, LinearVelocity, RestingVelocityThreshold);
		}
	}
}
