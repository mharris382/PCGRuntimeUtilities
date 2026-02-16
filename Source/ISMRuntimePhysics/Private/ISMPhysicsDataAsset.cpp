#include "ISMPhysicsDataAsset.h"
#include "ISMPoolDataAsset.h"

void UISMPhysicsDataAsset::GetRecommendedPoolSettings(int32& OutInitialSize, int32& OutGrowSize) const
{
	OutGrowSize = PoolGrowSize;
	OutInitialSize = InitialPoolSize;
}
