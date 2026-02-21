#include "Batching/ISMBatchTypes.h"
#include "ISMRuntimeComponent.h"

bool FISMBatchSnapshot::IsValid() const
{
	return SourceComponent.IsValid() ;
}