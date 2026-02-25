// ISMPCGBridge.h
#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ISMPCGDataChannel.h"
#include "ISMPCGAttributeSchema.h"
#include "ISMQueryFilter.h"
#include "ISMPCGBridge.generated.h"

class UPCGComponent;
class UPCGGraphInterface;
class UISMRuntimeComponent;

/**
 * Options for dispatching a PCG graph with ISM data.
 */
//USTRUCT(BlueprintType)
//struct FISMPCGDispatchOptions
//{
//    GENERATED_BODY()
//
//    /** Schema to use for attribute translation. If null, uses passthrough. */
//    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dispatch")
//    UISMPCGAttributeSchema* Schema = nullptr;
//
//    /** Filter to apply when reading instances into the packet */
//    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dispatch")
//    FISMQueryFilter InstanceFilter;
//
//    /**
//     * Whether to apply the resulting packet back to instances immediately,
//     * or just return it (false = caller is responsible for applying results).
//     */
//    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dispatch")
//    bool bAutoApplyResults = true;
//
//    /**
//     * Whether to only process instances visible in a radius.
//     * -1 = no radius filter.
//     */
//    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dispatch")
//    float ProcessRadius = -1.0f;
//
//    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dispatch")
//    FVector ProcessOrigin = FVector::ZeroVector;
//};
//
///**
 * Stateless translation bridge between ISM Runtime and PCG.
 * This is the API that other modules import and use.
 *
 * All functions are static - no state lives here.
 * The bridge is a pure translator: it does not own data,
 * does not make gameplay decisions, and does not know
 * what the data means semantically.
 *
 * Other modules (ISMRuntimePhysics, ISMRuntimeAnimation, etc.)
 * call into this to serialize/deserialize their instances to/from PCG.
 */
//UCLASS()
//class ISMRUNTIMEPCGINTEROP_API UISMPCGBridge : public UObject
//{
//    GENERATED_BODY()
//
//public:
//
//    // ===== ISM → PCG Direction =====
//
//    /**
//     * Read all (or filtered) instances from a runtime component into a data packet.
//     * This is the "export to PCG" path.
//     *
//     * @param Component  Source ISM Runtime component
//     * @param Schema     How to map ISM fields to PCG attributes (nullable = passthrough)
//     * @param Filter     Which instances to include
//     * @return           Data packet ready to inject into a PCG graph
//     */
//    UFUNCTION(BlueprintCallable, Category = "ISM PCG Bridge")
//    static FISMPCGDataPacket ReadInstancesFromComponent(
//        UISMRuntimeComponent* Component,
//        UISMPCGAttributeSchema* Schema,
//        const FISMQueryFilter& Filter);
//
//    /**
//     * Read instances from multiple components, merging into one packet.
//     * Useful for subsystem-level queries.
//     */
//    static FISMPCGDataPacket ReadInstancesFromComponents(
//        const TArray<UISMRuntimeComponent*>& Components,
//        UISMPCGAttributeSchema* Schema,
//        const FISMQueryFilter& Filter);
//
//    // ===== PCG → ISM Direction =====
//
//    /**
//     * Apply a data packet back to ISM instances.
//     * This is the "import from PCG" path.
//     *
//     * Matches points back to their source handles and applies:
//     * - Transform updates (if transform changed)
//     * - State flag changes
//     * - Tag additions/removals
//     * - Custom data slot writes
//     *
//     * @param Packet     Data packet produced by PCG
//     * @param Schema     How to interpret PCG attributes back to ISM fields
//     * @param bWriteTransforms  Whether to update instance transforms from packet
//     * @param bWriteStates      Whether to update state flags
//     * @param bWriteTags        Whether to update gameplay tags
//     * @param bWriteCustomData  Whether to write custom data slots
//     */
//    UFUNCTION(BlueprintCallable, Category = "ISM PCG Bridge")
//    static void ApplyPacketToInstances(
//        const FISMPCGDataPacket& Packet,
//        UISMPCGAttributeSchema* Schema,
//        bool bWriteTransforms = false,
//        bool bWriteStates = true,
//        bool bWriteTags = true,
//        bool bWriteCustomData = true);
//
//    /**
//     * Apply a packet as NEW instances (for spawn-from-PCG workflows).
//     * Points don't need source handles - they're added to the component fresh.
//     *
//     * @param Packet     Points to spawn
//     * @param Target     Component to add instances to
//     * @param Schema     Attribute mapping
//     * @return           Array of new instance indices
//     */
//    UFUNCTION(BlueprintCallable, Category = "ISM PCG Bridge")
//    static TArray<int32> SpawnInstancesFromPacket(
//        const FISMPCGDataPacket& Packet,
//        UISMRuntimeComponent* Target,
//        UISMPCGAttributeSchema* Schema);
//
//    // ===== PCG Graph Dispatch =====
//
//    /**
//     * Synchronously dispatch a PCG graph with instance data as input.
//     * The graph receives the packet via the ISMRuntimeInput PCG node,
//     * processes it, and returns the result via the ISMRuntimeOutput PCG node.
//     *
//     * This is the "runtime PCG as a transform kernel" pattern.
//     *
//     * WARNING: Synchronous. Expensive for large instance counts.
//     * Use for precompute or low-frequency gameplay events.
//     *
//     * @param Graph          The PCG graph to execute
//     * @param InputPacket    Instance data to feed into the graph
//     * @param OutResultPacket Result data from the graph's output pin
//     * @param Options        Dispatch configuration
//     * @return               True if graph executed successfully
//     */
//    UFUNCTION(BlueprintCallable, Category = "ISM PCG Bridge")
//    static bool DispatchGraphSync(
//        UPCGGraphInterface* Graph,
//        const FISMPCGDataPacket& InputPacket,
//        FISMPCGDataPacket& OutResultPacket,
//        const FISMPCGDispatchOptions& Options);
//
//    /**
//     * Async graph dispatch. Callback fires on game thread when complete.
//     * Prefer this for runtime use.
//     */
//    static void DispatchGraphAsync(
//        UPCGGraphInterface* Graph,
//        const FISMPCGDataPacket& InputPacket,
//        const FISMPCGDispatchOptions& Options,
//        TFunction<void(const FISMPCGDataPacket&)> OnComplete);
//
//    // ===== Point Conversion Utilities (low-level) =====
//
//    /** Convert a single ISM instance to an FISMPCGInstancePoint */
//    static FISMPCGInstancePoint InstanceToPoint(
//        UISMRuntimeComponent* Component,
//        int32 InstanceIndex,
//        UISMPCGAttributeSchema* Schema);
//
//    /** Apply a single point back to its source instance */
//    static void ApplyPointToInstance(
//        const FISMPCGInstancePoint& Point,
//        UISMPCGAttributeSchema* Schema,
//        bool bWriteTransform,
//        bool bWriteState,
//        bool bWriteTags,
//        bool bWriteCustomData);
//};