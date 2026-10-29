/*==========================================================================================================================================
                                                  PATHREPLAYRAYQUERY.CPP
==========================================================================================================================================*/
// 🧩 Implement Vulkan ray-query capability negotiation, BLAS vertex packing, and validated BLAS/TLAS build recording.

#include "PathReplayRayQuery.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace Frontier
{
namespace
{

void RequireRayQuery(bool Condition, const char* Message)
{
    if (!Condition)
    {
        throw std::invalid_argument(Message);
    }
}

constexpr bool IsPowerOfTwo(std::uint64_t Value) noexcept
{
    return Value != 0u && (Value & (Value - 1u)) == 0u;
}

bool AlignUpChecked(std::uint64_t Value, std::uint64_t Alignment, std::uint64_t& Aligned) noexcept
{
    if (!IsPowerOfTwo(Alignment))
    {
        return false;
    }
    const std::uint64_t Mask = Alignment - 1u;
    if (Value > std::numeric_limits<std::uint64_t>::max() - Mask)
    {
        return false;
    }
    Aligned = (Value + Mask) & ~Mask;
    return true;
}

struct DeviceRange
{
    VkDeviceAddress Start = 0u;
    std::uint64_t Length = 0u;
};

bool RangesDisjoint(const DeviceRange& First, const DeviceRange& Second) noexcept
{
    if (First.Length == 0u || Second.Length == 0u)
    {
        return true;
    }
    const std::uint64_t FirstEnd = First.Start + First.Length;
    const std::uint64_t SecondEnd = Second.Start + Second.Length;
    return FirstEnd <= Second.Start || SecondEnd <= First.Start;
}

bool RangeValid(VkDeviceAddress Start, std::uint64_t Length) noexcept
{
    return Start != 0u && Length > 0u && Start <= std::numeric_limits<VkDeviceAddress>::max() - Length;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                             ACCELERATION TIER NEGOTIATION
//------------------------------------------------------------------------------------------------------------------------

PathReplayRayQueryDeviceCapabilities InspectPathReplayRayQueryExtensions(
    const VkExtensionProperties* Extensions,
    std::size_t ExtensionCount,
    std::uint32_t ApiVersion) noexcept
{
    PathReplayRayQueryDeviceCapabilities Capabilities{};
    Capabilities.ApiVersion = ApiVersion;
    if (Extensions == nullptr)
    {
        return Capabilities;
    }
    for (std::size_t Index = 0u; Index < ExtensionCount; ++Index)
    {
        const char* Name = Extensions[Index].extensionName;
        if (std::strcmp(Name, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) == 0)
        {
            Capabilities.AccelerationStructureExtension = true;
        }
        else if (std::strcmp(Name, VK_KHR_RAY_QUERY_EXTENSION_NAME) == 0)
        {
            Capabilities.RayQueryExtension = true;
        }
        else if (std::strcmp(Name, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME) == 0)
        {
            Capabilities.BufferDeviceAddressExtension = true;
        }
        else if (std::strcmp(Name, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) == 0)
        {
            Capabilities.DeferredHostOperationsExtension = true;
        }
    }
    if (ApiVersion >= VK_API_VERSION_1_2)
    {
        // Buffer device address is promoted to Vulkan 1.2 core, so extension presence is satisfied by 1.2+.
        Capabilities.BufferDeviceAddressExtension = true;
    }
    return Capabilities;
}

PathReplayAccelerationSelection SelectPathReplayAccelerationTier(
    const PathReplayRayQueryDeviceCapabilities& Capabilities,
    bool PreferHardwareRayQuery) noexcept
{
    PathReplayAccelerationSelection Result{};
    if (!PreferHardwareRayQuery)
    {
        Result.Tier = PathReplayAccelerationTier::OrderedSoftwareBvh;
        Result.HardwareRayQueryReady = false;
        Result.FallbackReason = "Caller requested ordered software BVH traversal";
        return Result;
    }
    if (Capabilities.ApiVersion < VK_API_VERSION_1_2)
    {
        Result.FallbackReason = "Vulkan 1.2+ required for SPIR-V 1.4 inline ray query";
        return Result;
    }
    if (!Capabilities.AccelerationStructureExtension || !Capabilities.RayQueryExtension)
    {
        Result.FallbackReason = "Device lacks VK_KHR_acceleration_structure or VK_KHR_ray_query";
        return Result;
    }
    if (!Capabilities.DeferredHostOperationsExtension)
    {
        Result.FallbackReason = "Device lacks VK_KHR_deferred_host_operations required by acceleration structures";
        return Result;
    }
    if (!Capabilities.BufferDeviceAddressExtension)
    {
        Result.FallbackReason = "Device lacks buffer device address support";
        return Result;
    }
    if (!Capabilities.AccelerationStructureFeature || !Capabilities.RayQueryFeature ||
        !Capabilities.BufferDeviceAddressFeature)
    {
        Result.FallbackReason = "Required rayQuery, accelerationStructure, or bufferDeviceAddress feature disabled";
        return Result;
    }
    Result.Tier = PathReplayAccelerationTier::HardwareRayQuery;
    Result.HardwareRayQueryReady = true;
    Result.FallbackReason = nullptr;
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                        BLAS VERTEX & TLAS INSTANCE PREPARATION
//------------------------------------------------------------------------------------------------------------------------

std::vector<std::array<float, 4>> PackPathReplayRayQueryVertices(
    const PathReplayFacet* Facets,
    std::size_t FacetCount)
{
    if (FacetCount == 0u)
    {
        return {};
    }
    RequireRayQuery(Facets != nullptr, "Null facet array passed to BLAS vertex packer");
    RequireRayQuery(FacetCount <= (std::numeric_limits<std::uint32_t>::max() / 3u),
                    "Facet count overflows 32-bit BLAS vertex indexing");
    std::vector<std::array<float, 4>> Vertices(FacetCount * 3u);
    for (std::size_t Index = 0u; Index < FacetCount; ++Index)
    {
        const PathReplayFacet& Facet = Facets[Index];
        const std::array<float, 4>* Corners[3] = {&Facet.A, &Facet.B, &Facet.C};
        for (std::size_t Corner = 0u; Corner < 3u; ++Corner)
        {
            const auto& Src = *Corners[Corner];
            RequireRayQuery(std::isfinite(Src[0]) && std::isfinite(Src[1]) && std::isfinite(Src[2]),
                            "Non-finite vertex coordinate in BLAS vertex packer");
            Vertices[Index * 3u + Corner] = {Src[0], Src[1], Src[2], 1.0f};
        }
    }
    return Vertices;
}

VkAccelerationStructureInstanceKHR MakePathReplayIdentityInstance(
    VkDeviceAddress BlasDeviceAddress,
    std::uint32_t CustomIndex,
    std::uint8_t Mask)
{
    RequireRayQuery(BlasDeviceAddress != 0u, "Zero BLAS device address in TLAS instance");
    RequireRayQuery((CustomIndex & ~0x00ffffffu) == 0u, "TLAS instance custom index exceeds 24 bits");
    VkAccelerationStructureInstanceKHR Instance{};
    Instance.transform.matrix[0][0] = 1.0f;
    Instance.transform.matrix[1][1] = 1.0f;
    Instance.transform.matrix[2][2] = 1.0f;
    Instance.instanceCustomIndex = CustomIndex;
    Instance.mask = Mask;
    Instance.instanceShaderBindingTableRecordOffset = 0u;
    Instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    Instance.accelerationStructureReference = BlasDeviceAddress;
    return Instance;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      ACCELERATION STRUCTURE SIZING & RECORDING
//------------------------------------------------------------------------------------------------------------------------

PathReplayAccelerationBuildPlan PlanPathReplayAccelerationBuild(
    const PathReplayAccelerationBuildAccess& Access,
    const PathReplayAccelerationLimits& Limits,
    std::uint32_t FacetCount)
{
    RequireRayQuery(Access.Device != VK_NULL_HANDLE && Access.GetBuildSizes != nullptr,
                    "Invalid Vulkan device or build-size entry point for acceleration planning");
    RequireRayQuery(FacetCount > 0u && FacetCount <= (std::numeric_limits<std::uint32_t>::max() / 3u),
                    "Invalid facet count for acceleration build plan");
    RequireRayQuery(Limits.MaxGeometryCount >= 1u && Limits.MaxInstanceCount >= 1u &&
                        FacetCount <= Limits.MaxPrimitiveCount,
                    "Scene exceeds physical-device acceleration structure limits");
    RequireRayQuery(IsPowerOfTwo(Limits.MinScratchAlignment) && Limits.MinScratchAlignment <= (1u << 20),
                    "Invalid acceleration structure scratch alignment");

    const std::uint32_t EffectiveAlignment = std::max<std::uint32_t>(Limits.MinScratchAlignment, 256u);
    const std::uint32_t VertexCount = FacetCount * 3u;

    VkAccelerationStructureGeometryKHR BlasGeometry{};
    BlasGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    BlasGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    BlasGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    BlasGeometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    BlasGeometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    BlasGeometry.geometry.triangles.vertexStride = sizeof(std::array<float, 4>);
    BlasGeometry.geometry.triangles.maxVertex = VertexCount - 1u;
    BlasGeometry.geometry.triangles.indexType = VK_INDEX_TYPE_NONE_KHR;

    VkAccelerationStructureBuildGeometryInfoKHR BlasBuildInfo{};
    BlasBuildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    BlasBuildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    BlasBuildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    BlasBuildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    BlasBuildInfo.geometryCount = 1u;
    BlasBuildInfo.pGeometries = &BlasGeometry;

    VkAccelerationStructureBuildSizesInfoKHR BlasSizes{};
    BlasSizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    Access.GetBuildSizes(Access.Device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                         &BlasBuildInfo, &FacetCount, &BlasSizes);

    VkAccelerationStructureGeometryKHR TlasGeometry{};
    TlasGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    TlasGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    TlasGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    TlasGeometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    TlasGeometry.geometry.instances.arrayOfPointers = VK_FALSE;

    VkAccelerationStructureBuildGeometryInfoKHR TlasBuildInfo{};
    TlasBuildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    TlasBuildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    TlasBuildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    TlasBuildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    TlasBuildInfo.geometryCount = 1u;
    TlasBuildInfo.pGeometries = &TlasGeometry;

    const std::uint32_t InstanceCount = 1u;
    VkAccelerationStructureBuildSizesInfoKHR TlasSizes{};
    TlasSizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    Access.GetBuildSizes(Access.Device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                         &TlasBuildInfo, &InstanceCount, &TlasSizes);

    RequireRayQuery(BlasSizes.accelerationStructureSize > 0u && TlasSizes.accelerationStructureSize > 0u &&
                        BlasSizes.buildScratchSize > 0u && TlasSizes.buildScratchSize > 0u,
                    "Driver returned zero storage or scratch size for acceleration structure");

    std::uint64_t TlasScratchOffset = 0u;
    std::uint64_t TotalScratch = 0u;
    RequireRayQuery(AlignUpChecked(BlasSizes.buildScratchSize, EffectiveAlignment, TlasScratchOffset) &&
                        TlasScratchOffset <= std::numeric_limits<std::uint64_t>::max() - TlasSizes.buildScratchSize &&
                        AlignUpChecked(TlasScratchOffset + TlasSizes.buildScratchSize, EffectiveAlignment, TotalScratch),
                    "Acceleration scratch allocation overflows 64-bit addressing");

    PathReplayAccelerationBuildPlan Plan{};
    Plan.FacetCount = FacetCount;
    Plan.ScratchAlignment = EffectiveAlignment;
    Plan.VertexBufferBytes = std::uint64_t(VertexCount) * sizeof(std::array<float, 4>);
    Plan.InstanceBufferBytes = sizeof(VkAccelerationStructureInstanceKHR);
    Plan.BlasStorageBytes = BlasSizes.accelerationStructureSize;
    Plan.TlasStorageBytes = TlasSizes.accelerationStructureSize;
    Plan.BlasScratchBytes = BlasSizes.buildScratchSize;
    Plan.TlasScratchBytes = TlasSizes.buildScratchSize;
    Plan.TlasScratchOffset = TlasScratchOffset;
    Plan.TotalScratchBytes = TotalScratch;
    return Plan;
}

void RecordPathReplayAccelerationBuild(
    VkCommandBuffer Commands,
    const PathReplayAccelerationBuildAccess& Access,
    const PathReplayAccelerationBuildPlan& Plan,
    const PathReplayAccelerationBuildTargets& Targets)
{
    RequireRayQuery(Commands != VK_NULL_HANDLE && Access.CmdBuildAccelerationStructures != nullptr &&
                        Access.CmdPipelineBarrier != nullptr,
                    "Invalid command buffer or Vulkan acceleration-build function table");
    RequireRayQuery(Plan.FacetCount > 0u && Plan.FacetCount <= (std::numeric_limits<std::uint32_t>::max() / 3u) &&
                        IsPowerOfTwo(Plan.ScratchAlignment) && Plan.VertexBufferBytes == std::uint64_t(Plan.FacetCount) * 48u &&
                        Plan.InstanceBufferBytes == sizeof(VkAccelerationStructureInstanceKHR) &&
                        Plan.BlasStorageBytes > 0u && Plan.TlasStorageBytes > 0u &&
                        Plan.BlasScratchBytes > 0u && Plan.TlasScratchBytes > 0u,
                    "Invalid acceleration build plan passed to recorder");
    RequireRayQuery(Targets.Blas != VK_NULL_HANDLE && Targets.Tlas != VK_NULL_HANDLE && Targets.Blas != Targets.Tlas,
                    "Invalid or aliased BLAS/TLAS handles");
    RequireRayQuery(RangeValid(Targets.VertexDeviceAddress, Plan.VertexBufferBytes) &&
                        RangeValid(Targets.InstanceDeviceAddress, Plan.InstanceBufferBytes) &&
                        RangeValid(Targets.BlasScratchAddress, Plan.BlasScratchBytes) &&
                        RangeValid(Targets.TlasScratchAddress, Plan.TlasScratchBytes),
                    "Invalid or overflowing acceleration device address range");
    RequireRayQuery((Targets.VertexDeviceAddress % 4u) == 0u &&
                        (Targets.InstanceDeviceAddress % 16u) == 0u &&
                        (Targets.BlasScratchAddress % Plan.ScratchAlignment) == 0u &&
                        (Targets.TlasScratchAddress % Plan.ScratchAlignment) == 0u,
                    "Misaligned vertex, instance, or scratch device address for acceleration build");

    const DeviceRange Ranges[4] = {
        {Targets.VertexDeviceAddress, Plan.VertexBufferBytes},
        {Targets.InstanceDeviceAddress, Plan.InstanceBufferBytes},
        {Targets.BlasScratchAddress, Plan.BlasScratchBytes},
        {Targets.TlasScratchAddress, Plan.TlasScratchBytes},
    };
    for (std::size_t First = 0u; First < 4u; ++First)
    {
        for (std::size_t Second = First + 1u; Second < 4u; ++Second)
        {
            RequireRayQuery(RangesDisjoint(Ranges[First], Ranges[Second]),
                            "Overlapping device address ranges in acceleration build targets");
        }
    }

    const std::uint32_t VertexCount = Plan.FacetCount * 3u;
    VkAccelerationStructureGeometryKHR BlasGeometry{};
    BlasGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    BlasGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    BlasGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    BlasGeometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    BlasGeometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    BlasGeometry.geometry.triangles.vertexData.deviceAddress = Targets.VertexDeviceAddress;
    BlasGeometry.geometry.triangles.vertexStride = sizeof(std::array<float, 4>);
    BlasGeometry.geometry.triangles.maxVertex = VertexCount - 1u;
    BlasGeometry.geometry.triangles.indexType = VK_INDEX_TYPE_NONE_KHR;

    VkAccelerationStructureBuildGeometryInfoKHR BlasBuildInfo{};
    BlasBuildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    BlasBuildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    BlasBuildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    BlasBuildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    BlasBuildInfo.dstAccelerationStructure = Targets.Blas;
    BlasBuildInfo.geometryCount = 1u;
    BlasBuildInfo.pGeometries = &BlasGeometry;
    BlasBuildInfo.scratchData.deviceAddress = Targets.BlasScratchAddress;

    VkAccelerationStructureBuildRangeInfoKHR BlasRange{};
    BlasRange.primitiveCount = Plan.FacetCount;
    const VkAccelerationStructureBuildRangeInfoKHR* BlasRangePtr = &BlasRange;
    Access.CmdBuildAccelerationStructures(Commands, 1u, &BlasBuildInfo, &BlasRangePtr);

    VkMemoryBarrier BlasToTlasBarrier{};
    BlasToTlasBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    BlasToTlasBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    BlasToTlasBarrier.dstAccessMask =
        VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    Access.CmdPipelineBarrier(Commands,
                              VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                              VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                              0u, 1u, &BlasToTlasBarrier, 0u, nullptr, 0u, nullptr);

    VkAccelerationStructureGeometryKHR TlasGeometry{};
    TlasGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    TlasGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    TlasGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    TlasGeometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    TlasGeometry.geometry.instances.arrayOfPointers = VK_FALSE;
    TlasGeometry.geometry.instances.data.deviceAddress = Targets.InstanceDeviceAddress;

    VkAccelerationStructureBuildGeometryInfoKHR TlasBuildInfo{};
    TlasBuildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    TlasBuildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    TlasBuildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    TlasBuildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    TlasBuildInfo.dstAccelerationStructure = Targets.Tlas;
    TlasBuildInfo.geometryCount = 1u;
    TlasBuildInfo.pGeometries = &TlasGeometry;
    TlasBuildInfo.scratchData.deviceAddress = Targets.TlasScratchAddress;

    VkAccelerationStructureBuildRangeInfoKHR TlasRange{};
    TlasRange.primitiveCount = 1u;
    const VkAccelerationStructureBuildRangeInfoKHR* TlasRangePtr = &TlasRange;
    Access.CmdBuildAccelerationStructures(Commands, 1u, &TlasBuildInfo, &TlasRangePtr);

    VkMemoryBarrier TlasToComputeBarrier{};
    TlasToComputeBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    TlasToComputeBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    TlasToComputeBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    Access.CmdPipelineBarrier(Commands,
                              VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                              VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                              0u, 1u, &TlasToComputeBarrier, 0u, nullptr, 0u, nullptr);
}

} // namespace Frontier
