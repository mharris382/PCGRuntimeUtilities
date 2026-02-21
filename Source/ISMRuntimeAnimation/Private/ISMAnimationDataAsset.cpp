#include "ISMAnimationDataAsset.h"

//float UISMAnimationDataAsset::EvaluateFalloff(float Distance) const
//{
//	if (!FalloffCurve)
//	{
//		if (MaxAnimationDistance < 0.0f)
//		{
//			return 1.0f; // No distance limit, full animation
//		}
//		else if (Distance >= MaxAnimationDistance)
//		{
//			return 0.0f; // Beyond max distance, no animation
//		}
//		else if (Distance <= MaxAnimationDistance * FalloffStartFraction)
//		{
//			return 1.0f; // Within falloff start, full animation
//		}
//		else
//		{
//			return 1.0f - ((Distance - MaxAnimationDistance * FalloffStartFraction) / (MaxAnimationDistance * (1.0f - FalloffStartFraction)));
//			// Linear falloff from 1 to 0 between falloff start and max distance
//		}
//	}
//	else
//	{
//		if (MaxAnimationDistance < 0.0f)
//		{
//			return FalloffCurve->GetFloatValue(0.0f); // No distance limit, use curve value at start
//		}
//		else if (Distance >= MaxAnimationDistance)
//		{
//			return FalloffCurve->GetFloatValue(1.0f); // Beyond max distance, use curve value at end
//		}
//		else if (Distance <= MaxAnimationDistance * FalloffStartFraction)
//		{
//			return FalloffCurve->GetFloatValue(0.0f); // Within falloff start, use curve value at start
//		}
//		else
//		{
//			float NormalizedDistance = (Distance - MaxAnimationDistance * FalloffStartFraction) / (MaxAnimationDistance * (1.0f - FalloffStartFraction));
//			return FalloffCurve->GetFloatValue(NormalizedDistance); // Use curve value based on normalized distance between falloff start and max distance
//		}
//	}
//}
