/*==========================================================================================================================================
                                                   PATHREPLAYRAYQUERY.H
==========================================================================================================================================*/
// 🧩 Negotiate Vulkan ray-query tiers, pack BLAS vertices, and record bottom/top acceleration builds for path replay.
#pragma once

#include "PathReplayFootprint.h"
#include "PathReplayVulkan.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                             ACCELERATION TIER NEGOTIATION
//------------------------------------------------------------------------------------------------------------------------

enum class PathReplayAccelerationTier : std::uint8_t
{
    LegacySoftwareBvh = 0,
    OrderedSoftwareBvh = 1,
    HardwareRayQuery = 2,
};

struct PathReplayRayQueryDeviceCapabilities
{
    std::uint32_t ApiVersion = 0; // Vulkan physical-device API version word.
    bool AccelerationStructureExtension = false;
    bool RayQueryExtension = false;
    bool BufferDeviceAddressExtension = false;
    bool DeferredHostOperationsExtension = false;
    bool AccelerationStructureFeature = false;
    bool RayQueryFeature = false;
    bool BufferDeviceAddressFeature = false;
};

struct PathReplayAccelerationSelection
{
    PathReplayAccelerationTier Tier = PathReplayAccelerationTier::OrderedSoftwareBvh;
    bool HardwareRayQueryReady = false;
    const char* FallbackReason = nullptr;
};

// 📝 Scans enumerated VkExtensionProperties for the four KHR extensions required by inline compute ray queries.
PathReplayRayQueryDeviceCapabilities InspectPathReplayRayQueryExtensions(
    const VkExtensionProperties* Extensions,
    std::size_t ExtensionCount,
    std::uint32_t ApiVersion) noexcept;

// 📝 Selects HardwareRayQuery only when Vulkan 1.2+, all four KHR extensions, and all three device features are active;
//    otherwise falls back cleanly to OrderedSoftwareBvh with an explicit diagnostic reason.
PathReplayAccelerationSelection SelectPathReplayAccelerationTier(
    const PathReplayRayQueryDeviceCapabilities& Capabilities,
    bool PreferHardwareRayQuery = true) noexcept;

// 📝 Maps the chosen acceleration tier to its LightComputeFootprint::Primary[3] flag bit.
constexpr std::uint32_t PathReplayAccelerationFlags(PathReplayAccelerationTier Tier) noexcept
{
    return Tier == PathReplayAccelerationTier::HardwareRayQuery
               ? PathReplayHardwareRayQuery
               : (Tier == PathReplayAccelerationTier::OrderedSoftwareBvh ? PathReplayOrderedTraversal : 0u);
}

//------------------------------------------------------------------------------------------------------------------------
//                                        BLAS VERTEX & TLAS INSTANCE PREPARATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Extracts 3 * FacetCount contiguous 16-byte-aligned positions (xyz, 1.0f) from 144-byte PathReplayFacet records.
std::vector<std::array<float, 4>> PackPathReplayRayQueryVertices(
    const PathReplayFacet* Facets,
    std::size_t FacetCount);

// 📝 Builds a single identity-transform TLAS instance with triangle back-face culling disabled to match FacetStrike.
VkAccelerationStructureInstanceKHR MakePathReplayIdentityInstance(
    VkDeviceAddress BlasDeviceAddress,
    std::uint32_t CustomIndex = 0u,
    std::uint8_t Mask = 0xffu);

//------------------------------------------------------------------------------------------------------------------------
//                                      ACCELERATION STRUCTURE SIZING & RECORDING
//------------------------------------------------------------------------------------------------------------------------

struct PathReplayAccelerationLimits
{
    std::uint64_t MaxGeometryCount = 1u;
    std::uint64_t MaxPrimitiveCount = 0u;
    std::uint64_t MaxInstanceCount = 1u;
    std::uint32_t MinScratchAlignment = 256u; // Bytes; must be a non-zero power of two.
};

struct PathReplayAccelerationBuildPlan
{
    std::uint32_t FacetCount = 0u;
    std::uint32_t ScratchAlignment = 256u;  // Bytes.
    std::uint64_t VertexBufferBytes = 0u;   // Bytes (3 * FacetCount * 16).
    std::uint64_t InstanceBufferBytes = 0u; // Bytes (sizeof(VkAccelerationStructureInstanceKHR) == 64).
    std::uint64_t BlasStorageBytes = 0u;    // Bytes required for bottom-level VkAccelerationStructureKHR.
    std::uint64_t TlasStorageBytes = 0u;    // Bytes required for top-level VkAccelerationStructureKHR.
    std::uint64_t BlasScratchBytes = 0u;    // Bytes required for BLAS build scratch.
    std::uint64_t TlasScratchBytes = 0u;    // Bytes required for TLAS build scratch.
    std::uint64_t TlasScratchOffset = 0u;   // Aligned byte offset of TLAS scratch in a combined scratch buffer.
    std::uint64_t TotalScratchBytes = 0u;   // Combined non-overlapping BLAS + TLAS scratch bytes.
};

struct PathReplayAccelerationBuildAccess
{
    VkDevice Device = VK_NULL_HANDLE;
    PFN_vkGetAccelerationStructureBuildSizesKHR GetBuildSizes = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR CmdBuildAccelerationStructures = nullptr;
    PFN_vkCmdPipelineBarrier CmdPipelineBarrier = nullptr;
};

struct PathReplayAccelerationBuildTargets
{
    VkAccelerationStructureKHR Blas = VK_NULL_HANDLE;
    VkAccelerationStructureKHR Tlas = VK_NULL_HANDLE;
    VkDeviceAddress VertexDeviceAddress = 0u;    // 4-byte aligned, >= Plan.VertexBufferBytes.
    VkDeviceAddress InstanceDeviceAddress = 0u;  // 16-byte aligned, >= Plan.InstanceBufferBytes.
    VkDeviceAddress BlasScratchAddress = 0u;     // ScratchAlignment aligned, >= Plan.BlasScratchBytes.
    VkDeviceAddress TlasScratchAddress = 0u;     // ScratchAlignment aligned, >= Plan.TlasScratchBytes.
};

// 📝 Queries exact Vulkan BLAS + TLAS storage and scratch requirements before buffer allocation.
PathReplayAccelerationBuildPlan PlanPathReplayAccelerationBuild(
    const PathReplayAccelerationBuildAccess& Access,
    const PathReplayAccelerationLimits& Limits,
    std::uint32_t FacetCount);

// 📝 Validates alignments, disjoint device-address ranges, and records BLAS build -> barrier -> TLAS build -> compute barrier.
void RecordPathReplayAccelerationBuild(
    VkCommandBuffer Commands,
    const PathReplayAccelerationBuildAccess& Access,
    const PathReplayAccelerationBuildPlan& Plan,
    const PathReplayAccelerationBuildTargets& Targets);

} // namespace Frontier
