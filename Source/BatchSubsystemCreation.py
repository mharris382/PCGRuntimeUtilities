


EVENT_STRUCT_TEMPLATE = """
USTRUCT(BlueprintType)
struct PROCEDURALSHELFCLUTTER_API F{0}Event
{
	GENERATED_BODY()

    F{0}() = default;
};
"""

ENGINE_SUBSYSTEM_HEADER_TEMPLATE = """
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/{1}.h"
#include "Delegates/DelegateCombinations.h"
#include "{0}Subsystem.generated.h"

{4}

{2}

UCLASS(BlueprintType)
class PROCEDURALSHELFCLUTTER_API U{0} : public U{1}
{
	GENERATED_BODY()

public:
    {3}
};
"""

ENGINE_SUBSYSTEM_CPP_TEMPLATE = """
#include "{0}Subsystem.h"
"""

class GlobalLookupDeclaration:
    KeyClassType = ""
    KeyClassInclude = ""
    ValueStruct = ""

class SubsystemModuleDefinition:

    MODULE_NAME_ALL_CAPS = ""
    SubsystemName = "Unnamed"  # {0}
    SubsystemClass = "EngineSubsystem",  # {1}
    NoParamDelegateList = []   
    ParamDelegateList = {}  #format is DelegateName : [StructTypeName, StructParamName]
    HasGlobalLookup = None # if it does have a global lookup then we need to 

    def get_delegate_signatures(self):  # goes in {2}
        return "TODO"
    
    def get_delegate_declarations(self): # goes in {3}
        return "TODO"
    
    def get_struct_definitions(self):  # goes in {4}
        return "TODO"


