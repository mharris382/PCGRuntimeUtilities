// Copyright (c) 2025 Max Harris
// Published by Procedural Architect

#include "ISMQueryFilter.h"

bool FISMQueryFilter::PassesFilter(const FISMInstanceReference& Instance) const
{
    return false;
}

bool FISMQueryFilter::PassesComponentFilter(UISMRuntimeComponent* Component) const
{
    return false;
}

bool FISMQueryFilter::PassesStateFilter(const FISMInstanceState* State) const
{
    return false;
}
