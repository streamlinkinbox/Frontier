/*==========================================================================================================================================
                                             REPLAYRAYACCELERATIONVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Verify VK_KHR_ray_query negotiation, BLAS/TLAS build recording, SPIR-V 1.4 opcodes, and SAH + ordered BVH parity.

#include "Engine/Rendering/PathReplayDomain.h"
#include "Engine/Rendering/PathReplayRayQuery.h"
#include "Engine/Rendering/TriangleVisibilitySpace.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace Frontier;

void Require(bool Condition, const char* Message)
{
    if (!Condition)
    {
        throw std::runtime_error(Message);
    }
}

template <class Callback>
void RequireRejection(Callback&& Action, const char* Message)
{
    bool Rejected = false;
    try
    {
        Action();
    }
    catch (const std::exception&)
    {
        Rejected = true;
    }
    Require(Rejected, Message);
}

template <class HandleType>
HandleType MakeMockHandle(std::uintptr_t Tag) noexcept
{
    HandleType Result{};
    std::memcpy(&Result, &Tag, std::min(sizeof(Result), sizeof(Tag)));
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                        MOCK VULKAN ACCELERATION BUILD DRIVER
//------------------------------------------------------------------------------------------------------------------------

struct RecordedBuildEvent
{
    char Kind = 0; // 'B' = BLAS build, 'T' = TLAS build, 'M' = MemoryBarrier.
    std::uint32_t Primitives = 0u;
    VkPipelineStageFlags SrcStage = 0u;
    VkPipelineStageFlags DstStage = 0u;
    VkAccessFlags SrcAccess = 0u;
    VkAccessFlags DstAccess = 0u;
};

static std::vector<RecordedBuildEvent> GlobalBuildEvents;
static std::uint64_t MockZeroSizeOverride = 0u;

VKAPI_ATTR void VKAPI_CALL MockGetBuildSizes(
    VkDevice,
    VkAccelerationStructureBuildTypeKHR BuildType,
    const VkAccelerationStructureBuildGeometryInfoKHR* BuildInfo,
    const std::uint32_t* MaxPrimitiveCounts,
    VkAccelerationStructureBuildSizesInfoKHR* SizeInfo)
{
    Require(BuildType == VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR &&
                BuildInfo != nullptr && MaxPrimitiveCounts != nullptr && SizeInfo != nullptr,
            "Unexpected null pointer in vkGetAccelerationStructureBuildSizesKHR");
    const std::uint64_t Count = MaxPrimitiveCounts[0];
    if (MockZeroSizeOverride != 0u)
    {
        SizeInfo->accelerationStructureSize = 0u;
        SizeInfo->updateScratchSize = 0u;
        SizeInfo->buildScratchSize = 0u;
        return;
    }
    if (BuildInfo->type == VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR)
    {
        Require(BuildInfo->geometryCount == 1u &&
                    BuildInfo->pGeometries[0].geometryType == VK_GEOMETRY_TYPE_TRIANGLES_KHR &&
                    BuildInfo->pGeometries[0].geometry.triangles.vertexStride == 16u &&
                    BuildInfo->pGeometries[0].geometry.triangles.maxVertex == Count * 3u - 1u,
                "Unexpected BLAS triangle geometry layout");
        SizeInfo->accelerationStructureSize = 4096u + Count * 64u;
        SizeInfo->buildScratchSize = 1500u + Count * 32u; // Intentionally unaligned to test alignment rounding.
        SizeInfo->updateScratchSize = 1024u;
    }
    else
    {
        Require(BuildInfo->type == VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR &&
                    BuildInfo->geometryCount == 1u &&
                    BuildInfo->pGeometries[0].geometryType == VK_GEOMETRY_TYPE_INSTANCES_KHR &&
                    Count == 1u,
                "Unexpected TLAS instance geometry layout");
        SizeInfo->accelerationStructureSize = 2048u;
        SizeInfo->buildScratchSize = 900u;
        SizeInfo->updateScratchSize = 512u;
    }
}

VKAPI_ATTR void VKAPI_CALL MockCmdBuildAccelerationStructures(
    VkCommandBuffer,
    std::uint32_t InfoCount,
    const VkAccelerationStructureBuildGeometryInfoKHR* Infos,
    const VkAccelerationStructureBuildRangeInfoKHR* const* RangeInfos)
{
    Require(InfoCount == 1u && Infos != nullptr && RangeInfos != nullptr && RangeInfos[0] != nullptr,
            "Unexpected acceleration build batch");
    RecordedBuildEvent Event{};
    Event.Kind = Infos[0].type == VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR ? 'B' : 'T';
    Event.Primitives = RangeInfos[0][0].primitiveCount;
    GlobalBuildEvents.push_back(Event);
}

VKAPI_ATTR void VKAPI_CALL MockCmdPipelineBarrier(
    VkCommandBuffer,
    VkPipelineStageFlags SrcStage,
    VkPipelineStageFlags DstStage,
    VkDependencyFlags,
    std::uint32_t MemoryBarrierCount,
    const VkMemoryBarrier* MemoryBarriers,
    std::uint32_t,
    const VkBufferMemoryBarrier*,
    std::uint32_t,
    const VkImageMemoryBarrier*)
{
    Require(MemoryBarrierCount == 1u && MemoryBarriers != nullptr, "Expected single VkMemoryBarrier");
    RecordedBuildEvent Event{};
    Event.Kind = 'M';
    Event.SrcStage = SrcStage;
    Event.DstStage = DstStage;
    Event.SrcAccess = MemoryBarriers[0].srcAccessMask;
    Event.DstAccess = MemoryBarriers[0].dstAccessMask;
    GlobalBuildEvents.push_back(Event);
}

//------------------------------------------------------------------------------------------------------------------------
//                                  CAPABILITY, PACKING & RECORDER CONTRACT CHECKS
//------------------------------------------------------------------------------------------------------------------------

void ValidateCapabilityNegotiationAndRecorder()
{
    VkExtensionProperties MakeExt[4]{};
    std::snprintf(MakeExt[0].extensionName, sizeof(MakeExt[0].extensionName), "%s",
                  VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    std::snprintf(MakeExt[1].extensionName, sizeof(MakeExt[1].extensionName), "%s",
                  VK_KHR_RAY_QUERY_EXTENSION_NAME);
    std::snprintf(MakeExt[2].extensionName, sizeof(MakeExt[2].extensionName), "%s",
                  VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    std::snprintf(MakeExt[3].extensionName, sizeof(MakeExt[3].extensionName), "%s",
                  VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);

    // 1. SwiftShader-like Vulkan 1.3 device without RT extensions -> OrderedSoftwareBvh.
    const auto SwiftCaps = InspectPathReplayRayQueryExtensions(nullptr, 0u, VK_API_VERSION_1_3);
    const auto SwiftSelect = SelectPathReplayAccelerationTier(SwiftCaps);
    Require(SwiftSelect.Tier == PathReplayAccelerationTier::OrderedSoftwareBvh &&
                !SwiftSelect.HardwareRayQueryReady && SwiftSelect.FallbackReason != nullptr &&
                PathReplayAccelerationFlags(SwiftSelect.Tier) == PathReplayOrderedTraversal,
            "SwiftShader-class device did not select OrderedSoftwareBvh");

    // 2. Vulkan 1.1 with RT extension strings -> rejected due to SPIR-V 1.4 requirement.
    auto Vk11Caps = InspectPathReplayRayQueryExtensions(MakeExt, 4u, VK_API_VERSION_1_1);
    Vk11Caps.AccelerationStructureFeature = true;
    Vk11Caps.RayQueryFeature = true;
    Vk11Caps.BufferDeviceAddressFeature = true;
    const auto Vk11Select = SelectPathReplayAccelerationTier(Vk11Caps);
    Require(Vk11Select.Tier == PathReplayAccelerationTier::OrderedSoftwareBvh && !Vk11Select.HardwareRayQueryReady,
            "Vulkan 1.1 device was erroneously promoted to HardwareRayQuery");

    // 3. Missing VK_KHR_deferred_host_operations -> rejected (Vulkan spec VUID dependency).
    auto MissingDeferred = InspectPathReplayRayQueryExtensions(MakeExt, 2u, VK_API_VERSION_1_3);
    MissingDeferred.AccelerationStructureFeature = true;
    MissingDeferred.RayQueryFeature = true;
    MissingDeferred.BufferDeviceAddressFeature = true;
    const auto DeferredSelect = SelectPathReplayAccelerationTier(MissingDeferred);
    Require(DeferredSelect.Tier == PathReplayAccelerationTier::OrderedSoftwareBvh &&
                !DeferredSelect.HardwareRayQueryReady,
            "Missing VK_KHR_deferred_host_operations was overlooked");

    // 4. Extensions present on Vulkan 1.3, but rayQuery feature disabled -> OrderedSoftwareBvh.
    auto FeatureOff = InspectPathReplayRayQueryExtensions(MakeExt, 3u, VK_API_VERSION_1_3);
    FeatureOff.AccelerationStructureFeature = true;
    FeatureOff.RayQueryFeature = false;
    FeatureOff.BufferDeviceAddressFeature = true;
    Require(!SelectPathReplayAccelerationTier(FeatureOff).HardwareRayQueryReady,
            "Disabled rayQuery feature bit was overlooked");

    // 5. Full Vulkan 1.3 RT device -> HardwareRayQuery (and honors caller override when PreferHardwareRayQuery=false).
    auto FullRt = FeatureOff;
    FullRt.RayQueryFeature = true;
    const auto RtSelect = SelectPathReplayAccelerationTier(FullRt, true);
    Require(RtSelect.Tier == PathReplayAccelerationTier::HardwareRayQuery &&
                RtSelect.HardwareRayQueryReady && RtSelect.FallbackReason == nullptr &&
                PathReplayAccelerationFlags(RtSelect.Tier) == PathReplayHardwareRayQuery,
            "Full Vulkan 1.3 RT device did not select HardwareRayQuery");
    const auto ForcedSw = SelectPathReplayAccelerationTier(FullRt, false);
    Require(ForcedSw.Tier == PathReplayAccelerationTier::OrderedSoftwareBvh && !ForcedSw.HardwareRayQueryReady,
            "Caller PreferHardwareRayQuery=false override was ignored");

    // 6. BLAS vertex packing and TLAS instance layout.
    std::vector<PathReplayFacet> Facets(2);
    Facets[0].A = {1.0f, 2.0f, 3.0f, 0.5f};
    Facets[0].B = {4.0f, 5.0f, 6.0f, 0.25f};
    Facets[0].C = {7.0f, 8.0f, 9.0f, 0.75f};
    Facets[1].A = {-1.0f, 0.0f, 2.0f, 0.0f};
    Facets[1].B = {0.0f, 3.0f, -2.0f, 0.0f};
    Facets[1].C = {2.0f, -3.0f, 1.0f, 0.0f};
    const auto PackedVertices = PackPathReplayRayQueryVertices(Facets.data(), Facets.size());
    Require(PackedVertices.size() == 6u &&
                PackedVertices[0] == std::array<float, 4>{1.0f, 2.0f, 3.0f, 1.0f} &&
                PackedVertices[2] == std::array<float, 4>{7.0f, 8.0f, 9.0f, 1.0f} &&
                PackedVertices[5] == std::array<float, 4>{2.0f, -3.0f, 1.0f, 1.0f},
            "BLAS vertex packing did not preserve triangle corner positions");
    Require(PackPathReplayRayQueryVertices(nullptr, 0u).empty(), "Empty BLAS vertex pack failed");
    RequireRejection([&] { PackPathReplayRayQueryVertices(nullptr, 1u); }, "Null BLAS facet pointer accepted");
    auto BadFacets = Facets;
    BadFacets[1].B[1] = std::numeric_limits<float>::quiet_NaN();
    RequireRejection([&] { PackPathReplayRayQueryVertices(BadFacets.data(), BadFacets.size()); },
                     "NaN BLAS vertex accepted");

    const auto Instance = MakePathReplayIdentityInstance(0x100000ull, 7u, 0xffu);
    Require(Instance.transform.matrix[0][0] == 1.0f && Instance.transform.matrix[1][1] == 1.0f &&
                Instance.transform.matrix[2][2] == 1.0f && Instance.transform.matrix[0][3] == 0.0f &&
                Instance.instanceCustomIndex == 7u && Instance.mask == 0xffu &&
                (Instance.flags & VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR) != 0u &&
                Instance.accelerationStructureReference == 0x100000ull,
            "Identity TLAS instance fields mismatch");
    RequireRejection([&] { MakePathReplayIdentityInstance(0u); }, "Zero BLAS device address accepted");
    RequireRejection([&] { MakePathReplayIdentityInstance(0x1000ull, 0x01000000u); },
                     "25-bit TLAS instance custom index accepted");

    // 7. BLAS + TLAS sizing and command-buffer recording with barrier & alignment checks.
    PathReplayAccelerationBuildAccess BuildAccess{};
    BuildAccess.Device = MakeMockHandle<VkDevice>(1u);
    BuildAccess.GetBuildSizes = &MockGetBuildSizes;
    BuildAccess.CmdBuildAccelerationStructures = &MockCmdBuildAccelerationStructures;
    BuildAccess.CmdPipelineBarrier = &MockCmdPipelineBarrier;

    PathReplayAccelerationLimits Limits{};
    Limits.MaxGeometryCount = 16u;
    Limits.MaxPrimitiveCount = 100000u;
    Limits.MaxInstanceCount = 16u;
    Limits.MinScratchAlignment = 512u;

    const auto Plan = PlanPathReplayAccelerationBuild(BuildAccess, Limits, 180u);
    Require(Plan.FacetCount == 180u && Plan.ScratchAlignment == 512u &&
                Plan.VertexBufferBytes == 180u * 48u && Plan.InstanceBufferBytes == 64u &&
                (Plan.TlasScratchOffset % 512u) == 0u && Plan.TlasScratchOffset >= Plan.BlasScratchBytes &&
                Plan.TotalScratchBytes >= Plan.TlasScratchOffset + Plan.TlasScratchBytes &&
                (Plan.TotalScratchBytes % 512u) == 0u,
            "Acceleration build plan alignment or byte accounting invalid");

    VkCommandBuffer Cmd = MakeMockHandle<VkCommandBuffer>(9u);
    PathReplayAccelerationBuildTargets Targets{};
    Targets.Blas = MakeMockHandle<VkAccelerationStructureKHR>(101u);
    Targets.Tlas = MakeMockHandle<VkAccelerationStructureKHR>(102u);
    Targets.VertexDeviceAddress = 0x100000ull;
    Targets.InstanceDeviceAddress = 0x200000ull;
    Targets.BlasScratchAddress = 0x300000ull;
    Targets.TlasScratchAddress = 0x300000ull + Plan.TlasScratchOffset;

    GlobalBuildEvents.clear();
    RecordPathReplayAccelerationBuild(Cmd, BuildAccess, Plan, Targets);
    Require(GlobalBuildEvents.size() == 4u &&
                GlobalBuildEvents[0].Kind == 'B' && GlobalBuildEvents[0].Primitives == 180u &&
                GlobalBuildEvents[1].Kind == 'M' &&
                GlobalBuildEvents[1].SrcStage == VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR &&
                GlobalBuildEvents[1].DstStage == VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR &&
                GlobalBuildEvents[2].Kind == 'T' && GlobalBuildEvents[2].Primitives == 1u &&
                GlobalBuildEvents[3].Kind == 'M' &&
                GlobalBuildEvents[3].SrcStage == VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR &&
                GlobalBuildEvents[3].DstStage == VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT &&
                GlobalBuildEvents[3].DstAccess == VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR,
            "Recorded BLAS/TLAS command sequence or barrier stages mismatch");

    // 8. Negative tests: every invalid input must throw before recording a single Vulkan command.
    std::uint32_t RejectedConfigs = 0u;
    auto CheckBuildReject = [&](auto Mutate)
    {
        GlobalBuildEvents.clear();
        RequireRejection(Mutate, "Invalid acceleration build configuration was not rejected");
        Require(GlobalBuildEvents.empty(), "Rejected acceleration build recorded partial commands");
        ++RejectedConfigs;
    };

    CheckBuildReject([&]
    {
        auto BadLimits = Limits;
        BadLimits.MaxPrimitiveCount = 179u;
        PlanPathReplayAccelerationBuild(BuildAccess, BadLimits, 180u);
    });
    CheckBuildReject([&]
    {
        auto BadLimits = Limits;
        BadLimits.MinScratchAlignment = 300u; // Non-power-of-two.
        PlanPathReplayAccelerationBuild(BuildAccess, BadLimits, 180u);
    });
    CheckBuildReject([&]
    {
        MockZeroSizeOverride = 1u;
        try
        {
            PlanPathReplayAccelerationBuild(BuildAccess, Limits, 180u);
        }
        catch (...)
        {
            MockZeroSizeOverride = 0u;
            throw;
        }
    });
    CheckBuildReject([&]
    {
        auto Bad = Targets;
        Bad.VertexDeviceAddress += 2u;
        RecordPathReplayAccelerationBuild(Cmd, BuildAccess, Plan, Bad);
    });
    CheckBuildReject([&]
    {
        auto Bad = Targets;
        Bad.InstanceDeviceAddress += 8u; // Violates 16-byte instance VUID alignment.
        RecordPathReplayAccelerationBuild(Cmd, BuildAccess, Plan, Bad);
    });
    CheckBuildReject([&]
    {
        auto Bad = Targets;
        Bad.BlasScratchAddress += 256u; // Violates 512-byte ScratchAlignment.
        RecordPathReplayAccelerationBuild(Cmd, BuildAccess, Plan, Bad);
    });
    CheckBuildReject([&]
    {
        auto Bad = Targets;
        Bad.TlasScratchAddress = Bad.BlasScratchAddress + 512u; // Overlaps BlasScratchBytes (> 512).
        RecordPathReplayAccelerationBuild(Cmd, BuildAccess, Plan, Bad);
    });
    CheckBuildReject([&]
    {
        auto Bad = Targets;
        Bad.VertexDeviceAddress = Bad.BlasScratchAddress; // Aliases vertex and scratch buffers.
        RecordPathReplayAccelerationBuild(Cmd, BuildAccess, Plan, Bad);
    });
    CheckBuildReject([&]
    {
        auto Bad = Targets;
        Bad.Tlas = Bad.Blas; // Aliased acceleration-structure handles.
        RecordPathReplayAccelerationBuild(Cmd, BuildAccess, Plan, Bad);
    });
    CheckBuildReject([&]
    {
        RecordPathReplayAccelerationBuild(VK_NULL_HANDLE, BuildAccess, Plan, Targets);
    });

    std::printf("RAY_QUERY_ENGINE capability_cases=6 vertex_pack=1 instance_guard=1 "
                "blas_tlas_sequence=4 build_rejections=%u\n",
                RejectedConfigs);
}

//------------------------------------------------------------------------------------------------------------------------
//                           SAH-2 + FRONT-TO-BACK ORDERED BVH MATHEMATICAL EQUIVALENCE PROOF
//------------------------------------------------------------------------------------------------------------------------

struct Vec3
{
    float X = 0.0f, Y = 0.0f, Z = 0.0f;
};

void AppendBoxGeometry(std::vector<std::array<SpatialVector, 3>>& Out,
                       double MinX, double MinY, double MinZ,
                       double MaxX, double MaxY, double MaxZ)
{
    const SpatialVector C[8] = {
        {MinX, MinY, MinZ}, {MaxX, MinY, MinZ}, {MaxX, MaxY, MinZ}, {MinX, MaxY, MinZ},
        {MinX, MinY, MaxZ}, {MaxX, MinY, MaxZ}, {MaxX, MaxY, MaxZ}, {MinX, MaxY, MaxZ},
    };
    const int Quads[6][4] = {
        {0, 1, 2, 3}, {4, 7, 6, 5}, {0, 4, 5, 1},
        {1, 5, 6, 2}, {2, 6, 7, 3}, {3, 7, 4, 0},
    };
    for (const auto& Q : Quads)
    {
        Out.push_back({C[Q[0]], C[Q[1]], C[Q[2]]});
        Out.push_back({C[Q[0]], C[Q[2]], C[Q[3]]});
    }
}

std::vector<std::array<SpatialVector, 3>> BuildUrbanOverpassGeometry()
{
    std::vector<std::array<SpatialVector, 3>> Tris;
    // Roadbed, left/right sidewalks, canyon buildings, overpass deck, 6 support pillars, and 4 multi-box vehicles.
    AppendBoxGeometry(Tris, -6.0, -2.0, -0.2, 6.0, 22.0, 0.0);     // Roadbed (12)
    AppendBoxGeometry(Tris, -6.0, -2.0, 0.002, -4.2, 22.0, 0.22);  // Left sidewalk (12)
    AppendBoxGeometry(Tris, 4.2, -2.0, 0.002, 6.0, 22.0, 0.22);    // Right sidewalk (12)
    AppendBoxGeometry(Tris, -6.4, -2.0, 0.225, -5.82, 22.0, 6.5);  // Left canyon wall (12)
    AppendBoxGeometry(Tris, 5.82, -2.0, 0.225, 6.4, 22.0, 6.5);    // Right canyon wall (12)
    AppendBoxGeometry(Tris, -5.8, 8.0, 3.602, 5.8, 14.5, 4.1);     // Overpass deck (12)
    for (int Side = -1; Side <= 1; Side += 2)
    {
        for (int Row = 0; Row < 3; ++Row)
        {
            const double Px = Side * 3.7;
            const double Py = 4.5 + Row * 5.5;
            AppendBoxGeometry(Tris, Px - 0.35, Py - 0.35, 0.002, Px + 0.35, Py + 0.35, 3.6); // 6 pillars (72)
        }
    }
    const double CarX[4] = {-1.5, 1.4, -1.7, 1.6};
    const double CarY[4] = {3.2, 7.5, 13.0, 18.0};
    for (int Car = 0; Car < 4; ++Car)
    {
        // Lower chassis + upper cabin per vehicle (24 * 4 = 96 -> total 240 triangles).
        AppendBoxGeometry(Tris, CarX[Car] - 0.9, CarY[Car] - 2.0, 0.15, CarX[Car] + 0.9, CarY[Car] + 2.0, 0.85);
        AppendBoxGeometry(Tris, CarX[Car] - 0.78, CarY[Car] - 0.9, 0.852, CarX[Car] + 0.78, CarY[Car] + 0.9, 1.42);
    }
    return Tris;
}

bool IntersectsBoxHost(const TriangleVisibilitySpace::FloatEnclosure& Box,
                       Vec3 Origin, Vec3 Direction, float Far, float& EntryNear)
{
    float Near = 0.0f;
    const float Orig[3] = {Origin.X, Origin.Y, Origin.Z};
    const float Dir[3] = {Direction.X, Direction.Y, Direction.Z};
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        if (std::abs(Dir[Axis]) < 1e-30f)
        {
            if (Orig[Axis] < Box.Minimum[Axis] || Orig[Axis] > Box.Maximum[Axis])
            {
                EntryNear = 1e30f;
                return false;
            }
        }
        else
        {
            const float A = (Box.Minimum[Axis] - Orig[Axis]) / Dir[Axis];
            const float B = (Box.Maximum[Axis] - Orig[Axis]) / Dir[Axis];
            Near = std::max(Near, std::min(A, B));
            Far = std::min(Far, std::max(A, B));
            if (Far < Near)
            {
                EntryNear = 1e30f;
                return false;
            }
        }
    }
    EntryNear = Near;
    return Far >= Near;
}

// Exact watertight Woop-Benthin-Wald FacetStrike matching LightInfluence.slang:873-888.
bool FacetStrikeHost(const TriangleVisibilitySpace::FloatFacet& Triangle,
                     Vec3 Origin, Vec3 Direction, float Minimum, float Maximum,
                     std::array<float, 3>& Strike)
{
    const float Dir[3] = {Direction.X, Direction.Y, Direction.Z};
    const float AbsDir[3] = {std::abs(Dir[0]), std::abs(Dir[1]), std::abs(Dir[2])};
    int Kz = AbsDir[0] > AbsDir[1] ? (AbsDir[0] > AbsDir[2] ? 0 : 2) : (AbsDir[1] > AbsDir[2] ? 1 : 2);
    if (AbsDir[Kz] == 0.0f)
    {
        return false;
    }
    int Kx = (Kz + 1) % 3;
    int Ky = (Kx + 1) % 3;
    if (Dir[Kz] < 0.0f)
    {
        std::swap(Kx, Ky);
    }
    const float Sz = 1.0f / Dir[Kz];
    const float Sx = Dir[Kx] * Sz;
    const float Sy = Dir[Ky] * Sz;
    const float Orig[3] = {Origin.X, Origin.Y, Origin.Z};
    const float A[3] = {Triangle.A[0] - Orig[0], Triangle.A[1] - Orig[1], Triangle.A[2] - Orig[2]};
    const float B[3] = {Triangle.B[0] - Orig[0], Triangle.B[1] - Orig[1], Triangle.B[2] - Orig[2]};
    const float C[3] = {Triangle.C[0] - Orig[0], Triangle.C[1] - Orig[1], Triangle.C[2] - Orig[2]};
    const float Ax = A[Kx] - Sx * A[Kz], Ay = A[Ky] - Sy * A[Kz];
    const float Bx = B[Kx] - Sx * B[Kz], By = B[Ky] - Sy * B[Kz];
    const float Cx = C[Kx] - Sx * C[Kz], Cy = C[Ky] - Sy * C[Kz];
    const float U = Cx * By - Cy * Bx;
    const float V = Ax * Cy - Ay * Cx;
    const float W = Bx * Ay - By * Ax;
    if ((U < 0.0f || V < 0.0f || W < 0.0f) && (U > 0.0f || V > 0.0f || W > 0.0f))
    {
        return false;
    }
    const float Det = U + V + W;
    if (Det == 0.0f)
    {
        return false;
    }
    const float Distance = (U * A[Kz] + V * B[Kz] + W * C[Kz]) * Sz / Det;
    Strike = {Distance, V / Det, W / Det};
    return Distance > Minimum && Distance < Maximum;
}

struct HostHit
{
    std::uint32_t Facet = 0xffffffffu;
    std::array<float, 3> Strike{1e30f, 0.0f, 0.0f};
    std::uint64_t BoxTests = 0u;
    std::uint64_t TriTests = 0u;
};

HostHit TraceLegacyHost(const TriangleVisibilitySpace::FloatAtlas& Atlas,
                        Vec3 Origin, Vec3 Direction, float Minimum, float Maximum, std::uint32_t StackCap = 64u)
{
    HostHit Out{};
    Out.Strike[0] = Maximum;
    if (Atlas.Enclosures.empty())
    {
        return Out;
    }
    std::uint32_t Stack[64]{0u};
    std::uint32_t Count = 1u;
    while (Count > 0u)
    {
        const auto& Box = Atlas.Enclosures[Stack[--Count]];
        float DummyNear = 0.0f;
        ++Out.BoxTests;
        if (!IntersectsBoxHost(Box, Origin, Direction, Out.Strike[0], DummyNear))
        {
            continue;
        }
        if (Box.Links[2] != 0u)
        {
            for (std::uint32_t I = 0u; I < Box.Links[1]; ++I)
            {
                const std::uint32_t Idx = Box.Links[0] + I;
                std::array<float, 3> Cand{};
                ++Out.TriTests;
                if (FacetStrikeHost(Atlas.Corners[Idx], Origin, Direction, Minimum, Out.Strike[0], Cand))
                {
                    Out.Strike = Cand;
                    Out.Facet = Idx;
                }
            }
        }
        else
        {
            if (Count + 2u > StackCap)
            {
                for (std::uint32_t I = 0u; I < Atlas.Corners.size(); ++I)
                {
                    std::array<float, 3> Cand{};
                    ++Out.TriTests;
                    if (FacetStrikeHost(Atlas.Corners[I], Origin, Direction, Minimum, Out.Strike[0], Cand))
                    {
                        Out.Strike = Cand;
                        Out.Facet = I;
                    }
                }
                return Out;
            }
            Stack[Count++] = Box.Links[0];
            Stack[Count++] = Box.Links[1];
        }
    }
    return Out;
}

HostHit TraceOrderedHost(const TriangleVisibilitySpace::FloatAtlas& Atlas,
                         Vec3 Origin, Vec3 Direction, float Minimum, float Maximum, std::uint32_t StackCap = 64u)
{
    HostHit Out{};
    Out.Strike[0] = Maximum;
    if (Atlas.Enclosures.empty())
    {
        return Out;
    }
    float RootNear = 0.0f;
    ++Out.BoxTests;
    if (!IntersectsBoxHost(Atlas.Enclosures[0], Origin, Direction, Out.Strike[0], RootNear))
    {
        return Out;
    }
    std::uint32_t StackNode[64]{0u};
    float StackNear[64]{0.0f};
    std::uint32_t Count = 0u;
    std::uint32_t Current = 0u;
    for (;;)
    {
        const auto& Box = Atlas.Enclosures[Current];
        if (Box.Links[2] != 0u)
        {
            for (std::uint32_t I = 0u; I < Box.Links[1]; ++I)
            {
                const std::uint32_t Idx = Box.Links[0] + I;
                std::array<float, 3> Cand{};
                ++Out.TriTests;
                if (FacetStrikeHost(Atlas.Corners[Idx], Origin, Direction, Minimum, Out.Strike[0], Cand))
                {
                    Out.Strike = Cand;
                    Out.Facet = Idx;
                }
            }
        }
        else
        {
            const std::uint32_t Left = Box.Links[0], Right = Box.Links[1];
            float LeftNear = 0.0f, RightNear = 0.0f;
            Out.BoxTests += 2u;
            const bool HitLeft = IntersectsBoxHost(Atlas.Enclosures[Left], Origin, Direction, Out.Strike[0], LeftNear);
            const bool HitRight =
                IntersectsBoxHost(Atlas.Enclosures[Right], Origin, Direction, Out.Strike[0], RightNear);
            if (HitLeft && HitRight)
            {
                const std::uint32_t NearChild = LeftNear <= RightNear ? Left : Right;
                const std::uint32_t FarChild = LeftNear <= RightNear ? Right : Left;
                const float FarNear = std::max(LeftNear, RightNear);
                if (Count + 1u > StackCap)
                {
                    for (std::uint32_t I = 0u; I < Atlas.Corners.size(); ++I)
                    {
                        std::array<float, 3> Cand{};
                        ++Out.TriTests;
                        if (FacetStrikeHost(Atlas.Corners[I], Origin, Direction, Minimum, Out.Strike[0], Cand))
                        {
                            Out.Strike = Cand;
                            Out.Facet = I;
                        }
                    }
                    return Out;
                }
                StackNode[Count] = FarChild;
                StackNear[Count] = FarNear;
                ++Count;
                Current = NearChild;
                continue;
            }
            if (HitLeft || HitRight)
            {
                Current = HitLeft ? Left : Right;
                continue;
            }
        }
        bool Advanced = false;
        while (Count > 0u)
        {
            --Count;
            if (StackNear[Count] <= Out.Strike[0])
            {
                Current = StackNode[Count];
                Advanced = true;
                break;
            }
        }
        if (!Advanced)
        {
            break;
        }
    }
    return Out;
}

bool OccludedOrderedHost(const TriangleVisibilitySpace::FloatAtlas& Atlas,
                         Vec3 Origin, Vec3 Direction, float Distance, std::uint32_t Endpoint,
                         std::uint64_t& BoxTests, std::uint64_t& TriTests)
{
    if (Atlas.Enclosures.empty() || Distance <= 0.0f)
    {
        return false;
    }
    float RootNear = 0.0f;
    ++BoxTests;
    if (!IntersectsBoxHost(Atlas.Enclosures[0], Origin, Direction, Distance, RootNear))
    {
        return false;
    }
    std::uint32_t Stack[64]{0u};
    std::uint32_t Count = 0u, Current = 0u;
    for (;;)
    {
        const auto& Box = Atlas.Enclosures[Current];
        if (Box.Links[2] != 0u)
        {
            for (std::uint32_t I = 0u; I < Box.Links[1]; ++I)
            {
                const std::uint32_t Idx = Box.Links[0] + I;
                if (Idx != Endpoint)
                {
                    std::array<float, 3> Cand{};
                    ++TriTests;
                    if (FacetStrikeHost(Atlas.Corners[Idx], Origin, Direction, 0.0f, Distance, Cand))
                    {
                        return true;
                    }
                }
            }
        }
        else
        {
            const std::uint32_t Left = Box.Links[0], Right = Box.Links[1];
            float LeftNear = 0.0f, RightNear = 0.0f;
            BoxTests += 2u;
            const bool HitLeft = IntersectsBoxHost(Atlas.Enclosures[Left], Origin, Direction, Distance, LeftNear);
            const bool HitRight = IntersectsBoxHost(Atlas.Enclosures[Right], Origin, Direction, Distance, RightNear);
            if (HitLeft && HitRight)
            {
                Stack[Count++] = LeftNear <= RightNear ? Right : Left;
                Current = LeftNear <= RightNear ? Left : Right;
                continue;
            }
            if (HitLeft || HitRight)
            {
                Current = HitLeft ? Left : Right;
                continue;
            }
        }
        if (Count == 0u)
        {
            break;
        }
        Current = Stack[--Count];
    }
    return false;
}

void ValidateSahAndOrderedTraversalEquivalence()
{
    const auto Geometry = BuildUrbanOverpassGeometry();
    TriangleVisibilitySpace MedianSpace;
    MedianSpace.Build(Geometry);
    const auto MedianAtlas = MedianSpace.PackFloat32();

    TriangleVisibilitySpace SahSpace;
    SahSpace.BuildSurfaceAreaHierarchy(Geometry, 2u);
    const auto SahAtlas = SahSpace.PackFloat32();

    // Certify both trees through ValidatePathReplayDomain (plus HardwareRayQuery enclosure-free mode).
    auto MakeDomainFacets = [](const TriangleVisibilitySpace::FloatAtlas& Atlas)
    {
        std::vector<PathReplayFacet> Surfaces(Atlas.Corners.size());
        for (std::size_t I = 0u; I < Atlas.Corners.size(); ++I)
        {
            Surfaces[I].A = Atlas.Corners[I].A;
            Surfaces[I].B = Atlas.Corners[I].B;
            Surfaces[I].C = Atlas.Corners[I].C;
            Surfaces[I].NormalA = {0.0f, 0.0f, 1.0f, 0.0f};
            Surfaces[I].NormalB = {0.0f, 0.0f, 1.0f, 0.0f};
            Surfaces[I].NormalC = {0.0f, 0.0f, 1.0f, 0.0f};
            Surfaces[I].AlbedoRoughness = {0.6f, 0.6f, 0.6f, 0.5f};
            Surfaces[I].OpticalToken = {0.0f, 1.5f, 0.0f, 1.0f};
            Surfaces[I].Emissive = {0.0f, 0.0f, 0.0f, 0.0f};
        }
        return Surfaces;
    };
    const auto MedianSurfaces = MakeDomainFacets(MedianAtlas);
    const auto SahSurfaces = MakeDomainFacets(SahAtlas);
    std::array<std::array<float, 4>, 12> Camera{};
    Camera[0] = {0.0f, -1.5f, 1.5f, 0.05f};
    Camera[1] = {0.0f, 1.0f, 0.0f, 100.0f};
    Camera[2] = {1.0f, 0.0f, 0.0f, 0.8f};
    Camera[3] = {0.0f, 0.0f, 1.0f, 0.8f};
    Camera[4] = Camera[0];
    Camera[5] = Camera[1];
    Camera[6] = Camera[2];
    Camera[7] = Camera[3];
    PathReplayReceiver DummyReceiver{};
    DummyReceiver.PositionHit = {0.0f, 1.0f, 0.0f, 1.0f};
    DummyReceiver.NormalRoughness = {0.0f, 0.0f, 1.0f, 0.5f};
    DummyReceiver.AlbedoIor = {0.5f, 0.5f, 0.5f, 1.5f};
    DummyReceiver.MotionPositionToken = {0.0f, 1.0f, 0.0f, 1.0f};
    DummyReceiver.MotionNormal = {0.0f, 0.0f, 1.0f, 0.0f};
    DummyReceiver.MotionPixel = {0.0f, 0.0f, 0.05f, 1.0f};
    std::array<PathReplayReceiver, 1> Receivers{DummyReceiver};

    ValidatePathReplayDomain(
        PathReplaySceneView{Receivers, MedianSurfaces, MedianAtlas.Enclosures, MedianAtlas.Corners, {}, Camera, 0u},
        PathReplayDomainSpecification{});
    ValidatePathReplayDomain(
        PathReplaySceneView{Receivers, SahSurfaces, SahAtlas.Enclosures, SahAtlas.Corners, {}, Camera, 0u},
        PathReplayDomainSpecification{});
    ValidatePathReplayDomain(
        PathReplaySceneView{Receivers, SahSurfaces, {}, {}, {}, Camera, 0u},
        PathReplayDomainSpecification{.HardwareRayQuery = true});

    auto NextRand = [](std::uint32_t& State) -> float
    {
        State = State * 747796405u + 2891336453u;
        std::uint32_t Word = ((State >> ((State >> 28u) + 4u)) ^ State) * 277803737u;
        Word = (Word >> 22u) ^ Word;
        return float(Word & 0x00ffffffu) / float(0x01000000u);
    };

    std::uint32_t Rng = 0x51a7f00du;
    std::uint64_t LegacyMedBoxes = 0u, LegacyMedTris = 0u;
    std::uint64_t OrderedMedBoxes = 0u, OrderedMedTris = 0u;
    std::uint64_t OrderedSahBoxes = 0u, OrderedSahTris = 0u;

    for (std::uint32_t RayIndex = 0u; RayIndex < 10000u; ++RayIndex)
    {
        Vec3 Origin{-3.8f + 7.6f * NextRand(Rng), -1.5f + 16.0f * NextRand(Rng), 0.25f + 2.8f * NextRand(Rng)};
        Vec3 Dir{-0.6f + 1.2f * NextRand(Rng), 0.15f + 0.85f * NextRand(Rng), -0.45f + 0.9f * NextRand(Rng)};
        if ((RayIndex % 250u) == 0u)
        {
            Dir = {0.0f, 1.0f, 0.0f}; // Exact axis-aligned ray test.
        }
        const float InvLen = 1.0f / std::sqrt(Dir.X * Dir.X + Dir.Y * Dir.Y + Dir.Z * Dir.Z);
        Dir = {Dir.X * InvLen, Dir.Y * InvLen, Dir.Z * InvLen};

        const auto LegMed = TraceLegacyHost(MedianAtlas, Origin, Dir, 1e-4f, 1e30f);
        const auto OrdMed = TraceOrderedHost(MedianAtlas, Origin, Dir, 1e-4f, 1e30f);
        const auto OrdSah = TraceOrderedHost(SahAtlas, Origin, Dir, 1e-4f, 1e30f);

        Require(LegMed.Facet == OrdMed.Facet && LegMed.Strike == OrdMed.Strike,
                "Ordered front-to-back BVH diverged from legacy BVH on Median-8 tree");
        const std::uint32_t OrigMed =
            LegMed.Facet == 0xffffffffu ? 0xffffffffu : MedianAtlas.OriginalFacets[LegMed.Facet];
        const std::uint32_t OrigSah =
            OrdSah.Facet == 0xffffffffu ? 0xffffffffu : SahAtlas.OriginalFacets[OrdSah.Facet];
        if (OrigMed != OrigSah)
        {
            // Coplanar shared-edge ties must have identical ray distance within 1e-5.
            Require(OrigMed != 0xffffffffu && OrigSah != 0xffffffffu &&
                        std::abs(LegMed.Strike[0] - OrdSah.Strike[0]) <= 1e-5f,
                    "SAH-2 BVH missed closest triangle");
        }
        else if (OrigMed != 0xffffffffu)
        {
            Require(LegMed.Strike == OrdSah.Strike, "SAH-2 BVH strike coordinates differed on hit");
        }

        // Stack-overflow fallback parity at StackCap = 2.
        if (RayIndex < 500u)
        {
            const auto CapHit = TraceOrderedHost(SahAtlas, Origin, Dir, 1e-4f, 1e30f, 2u);
            Require((CapHit.Facet == OrdSah.Facet) ||
                        std::abs(CapHit.Strike[0] - OrdSah.Strike[0]) <= 1e-5f,
                    "Ordered BVH stack-overflow linear fallback diverged");
        }

        LegacyMedBoxes += LegMed.BoxTests;
        LegacyMedTris += LegMed.TriTests;
        OrderedMedBoxes += OrdMed.BoxTests;
        OrderedMedTris += OrdMed.TriTests;
        OrderedSahBoxes += OrdSah.BoxTests;
        OrderedSahTris += OrdSah.TriTests;
    }

    // Shadow occlusion parity across 10,000 NEE segment rays (with endpoint exclusion).
    std::uint64_t ShadowLegBoxes = 0u, ShadowLegTris = 0u, ShadowSahBoxes = 0u, ShadowSahTris = 0u;
    for (std::uint32_t RayIndex = 0u; RayIndex < 10000u; ++RayIndex)
    {
        const std::uint32_t SahEndpoint = RayIndex % std::uint32_t(SahAtlas.Corners.size());
        const auto& Tri = SahAtlas.Corners[SahEndpoint];
        const Vec3 Target{
            (Tri.A[0] + Tri.B[0] + Tri.C[0]) / 3.0f,
            (Tri.A[1] + Tri.B[1] + Tri.C[1]) / 3.0f,
            (Tri.A[2] + Tri.B[2] + Tri.C[2]) / 3.0f,
        };
        const Vec3 Origin{-3.5f + 7.0f * NextRand(Rng), -1.0f + 18.0f * NextRand(Rng), 0.3f + 2.5f * NextRand(Rng)};
        const Vec3 Delta{Target.X - Origin.X, Target.Y - Origin.Y, Target.Z - Origin.Z};
        const float Dist = std::sqrt(Delta.X * Delta.X + Delta.Y * Delta.Y + Delta.Z * Delta.Z);
        if (Dist <= 1e-3f)
        {
            continue;
        }
        const Vec3 Dir{Delta.X / Dist, Delta.Y / Dist, Delta.Z / Dist};
        bool BruteOccluded = false;
        for (std::uint32_t F = 0u; F < SahAtlas.Corners.size(); ++F)
        {
            if (F == SahEndpoint)
            {
                continue;
            }
            std::array<float, 3> Cand{};
            ++ShadowLegTris;
            if (FacetStrikeHost(SahAtlas.Corners[F], Origin, Dir, 0.0f, Dist, Cand))
            {
                BruteOccluded = true;
                break;
            }
        }
        const bool SahOccluded =
            OccludedOrderedHost(SahAtlas, Origin, Dir, Dist, SahEndpoint, ShadowSahBoxes, ShadowSahTris);
        Require(BruteOccluded == SahOccluded, "Ordered SAH shadow occlusion mismatched brute-force oracle");
        (void)ShadowLegBoxes;
    }

    const double TriReductionSameTree =
        100.0 * (1.0 - double(OrderedMedTris) / double(std::max<std::uint64_t>(1u, LegacyMedTris)));
    const double TriReductionSahOrdered =
        100.0 * (1.0 - double(OrderedSahTris) / double(std::max<std::uint64_t>(1u, LegacyMedTris)));
    Require(OrderedMedTris < LegacyMedTris && OrderedMedBoxes < LegacyMedBoxes,
            "Ordered front-to-back traversal failed to reduce node and triangle tests on Median-8 BVH");
    Require(TriReductionSahOrdered >= 50.0,
            "SAH-2 + Ordered traversal reduced triangle tests by less than 50% on urban overpass scene");

    std::printf("ORDERED_SAH_PARITY facets=%zu median_nodes=%zu sah_nodes=%zu "
                "legacy_boxes=%llu legacy_tris=%llu ordered_med_boxes=%llu ordered_med_tris=%llu "
                "ordered_sah_boxes=%llu ordered_sah_tris=%llu same_tree_tri_saved_pct=%.2f "
                "sah_ordered_tri_saved_pct=%.2f shadow_rays_verified=10000\n",
                Geometry.size(), MedianAtlas.Enclosures.size(), SahAtlas.Enclosures.size(),
                static_cast<unsigned long long>(LegacyMedBoxes),
                static_cast<unsigned long long>(LegacyMedTris),
                static_cast<unsigned long long>(OrderedMedBoxes),
                static_cast<unsigned long long>(OrderedMedTris),
                static_cast<unsigned long long>(OrderedSahBoxes),
                static_cast<unsigned long long>(OrderedSahTris),
                TriReductionSameTree, TriReductionSahOrdered);
}

//------------------------------------------------------------------------------------------------------------------------
//                                  SPIR-V 1.4 KHRONOS RAY-QUERY BINARY INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

void VerifyHardwareRayQuerySpirvFile(const std::string& Path, bool ExpectNonOpaqueLoop)
{
    std::ifstream Input(Path, std::ios::binary | std::ios::ate);
    Require(bool(Input), ("Missing hardware ray-query SPIR-V binary: " + Path).c_str());
    const auto ByteSize = Input.tellg();
    Require(ByteSize >= 20 && (ByteSize % 4) == 0, "Invalid SPIR-V binary size");
    std::vector<std::uint32_t> Words(static_cast<std::size_t>(ByteSize) / 4u);
    Input.seekg(0);
    Input.read(reinterpret_cast<char*>(Words.data()), ByteSize);
    Require(Words[0] == 0x07230203u, "Invalid SPIR-V magic number");
    Require(Words[1] >= 0x00010400u, "Hardware ray-query shader must target SPIR-V 1.4 or newer");

    bool HasRayQueryCapability = false;
    bool HasRayQueryExtension = false;
    bool HasAccelStructType = false;
    bool HasRayQueryInit = false;
    bool HasRayQueryProceed = false;
    bool HasConfirmIntersection = false;

    std::size_t Offset = 5u; // Skip 5-word SPIR-V header.
    while (Offset < Words.size())
    {
        const std::uint32_t Header = Words[Offset];
        const std::uint16_t WordCount = static_cast<std::uint16_t>(Header >> 16u);
        const std::uint16_t OpCode = static_cast<std::uint16_t>(Header & 0xffffu);
        Require(WordCount >= 1u && Offset + WordCount <= Words.size(), "Malformed SPIR-V instruction stream");
        if (OpCode == 17u && WordCount >= 2u && Words[Offset + 1u] == 4472u) // OpCapability RayQueryKHR (4472)
        {
            HasRayQueryCapability = true;
        }
        else if (OpCode == 10u && WordCount >= 2u) // OpExtension
        {
            const char* Text = reinterpret_cast<const char*>(&Words[Offset + 1u]);
            const std::size_t MaxChars = (WordCount - 1u) * 4u;
            if (std::strncmp(Text, "SPV_KHR_ray_query", MaxChars) == 0)
            {
                HasRayQueryExtension = true;
            }
        }
        else if (OpCode == 5341u) // OpTypeAccelerationStructureKHR
        {
            HasAccelStructType = true;
        }
        else if (OpCode == 4473u) // OpRayQueryInitializeKHR
        {
            HasRayQueryInit = true;
        }
        else if (OpCode == 4477u) // OpRayQueryProceedKHR
        {
            HasRayQueryProceed = true;
        }
        else if (OpCode == 4476u) // OpRayQueryConfirmIntersectionKHR
        {
            HasConfirmIntersection = true;
        }
        Offset += WordCount;
    }

    Require(HasRayQueryCapability && HasRayQueryExtension && HasAccelStructType &&
                HasRayQueryInit && HasRayQueryProceed,
            ("SPIR-V binary missing required KHR ray-query opcodes: " + Path).c_str());
    if (ExpectNonOpaqueLoop)
    {
        Require(HasConfirmIntersection,
                ("NEE shadow ray-query shader missing OpRayQueryConfirmIntersectionKHR: " + Path).c_str());
    }
    else
    {
        Require(!HasConfirmIntersection,
                ("Opaque primary ray-query shader unexpectedly emitted non-opaque confirmation: " + Path).c_str());
    }
}

void VerifyHardwareRayQuerySpirvDirectory(const std::string& Directory)
{
    VerifyHardwareRayQuerySpirvFile(Directory + "/PrimaryHardwareRT.spv", false);
    VerifyHardwareRayQuerySpirvFile(Directory + "/ReplayGenerateHardwareRT.spv", true);
    VerifyHardwareRayQuerySpirvFile(Directory + "/ReplayResolveHardwareRT.spv", true);
    VerifyHardwareRayQuerySpirvFile(Directory + "/ReplayFinalizeSampleHistoryHardwareRT.spv", true);
    std::printf("RAY_QUERY_SPIRV_VERIFIED files=4 version=1.4 capability_4472=1 "
                "accel_type_5341=1 init_4473=1 proceed_4477=1 confirm_endpoint_skip_4476=1\n");
}

} // namespace

int main(int Argc, char** Argv)
{
    try
    {
        ValidateCapabilityNegotiationAndRecorder();
        ValidateSahAndOrderedTraversalEquivalence();
        if (Argc >= 3 && std::string(Argv[1]) == "--verify-spirv")
        {
            VerifyHardwareRayQuerySpirvDirectory(Argv[2]);
        }
        std::printf("PASS replay ray-query engine, BLAS/TLAS recorder, and ordered SAH BVH equivalence\n");
        return 0;
    }
    catch (const std::exception& Error)
    {
        std::fprintf(stderr, "FAIL replay ray acceleration validation: %s\n", Error.what());
        return 1;
    }
}
