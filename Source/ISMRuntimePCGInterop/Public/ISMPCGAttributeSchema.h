// ISMPCGAttributeSchema.h
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "ISMPCGAttributeSchema.generated.h"

UENUM(BlueprintType)
enum class EISMPCGAttributeTarget : uint8
{
    // ISM -> PCG direction
    CustomDataSlot,     // Maps to per-instance float custom data by index
    StateFlag,          // Maps a named PCG attribute to/from an EISMInstanceState bit
    GameplayTag,        // Encodes tag presence as float (0/1)
    
    // Freeform payload
    FloatPayload,       // Named float in FISMPCGInstancePoint::FloatPayload
    IntPayload,
    VectorPayload,
    
    // Transform components (useful for PCG attribute nodes)
    LocationX, LocationY, LocationZ,
    Scale,
};

USTRUCT(BlueprintType)
struct FISMPCGAttributeMapping
{
    GENERATED_BODY()

    /** Name of the attribute in the PCG graph */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schema")
    FName PCGAttributeName;

    /** What ISM field this maps to */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schema")
    EISMPCGAttributeTarget TargetField = EISMPCGAttributeTarget::FloatPayload;

    /** For CustomDataSlot: which slot index */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schema",
        meta=(EditCondition="TargetField == EISMPCGAttributeTarget::CustomDataSlot"))
    int32 CustomDataIndex = 0;

    /** For GameplayTag mapping: which tag */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schema",
        meta=(EditCondition="TargetField == EISMPCGAttributeTarget::GameplayTag"))
    FGameplayTag MappedTag;

    /** Default value if attribute is missing */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schema")
    float DefaultValue = 0.0f;
};

/**
 * Data asset defining how PCG attributes map to ISM Runtime fields.
 * Assign one of these to any component or PCG element that uses the interop layer.
 * Different modules can define their own schemas without touching each other.
 */
UCLASS(BlueprintType)
class ISMRUNTIMEPCGINTEROP_API UISMPCGAttributeSchema : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
    TArray<FISMPCGAttributeMapping> Mappings;

    /**
     * Whether unrecognized PCG attributes should be passed through
     * into FloatPayload automatically (by name).
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
    bool bPassThroughUnmappedFloats = true;

    /**
     * Whether to serialize gameplay tags as individual float attributes
     * in the PCG graph (tag name = attribute name, value = 0/1).
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
    bool bSerializeTagsAsAttributes = false;

    /** Find mapping for a given PCG attribute name */
    const FISMPCGAttributeMapping* FindMapping(FName PCGAttributeName) const;
};