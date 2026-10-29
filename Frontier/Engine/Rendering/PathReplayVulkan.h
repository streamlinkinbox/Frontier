/*==========================================================================================================================================
                                                     PATHREPLAYVULKAN.H
==========================================================================================================================================*/
// 🧩 Record bounded software-traced replay passes into a caller-owned Vulkan command buffer.
#pragma once

#include "LightComputeFootprint.h"
#include "PathReplayFootprint.h"
#include <cstdint>
#include <limits>
#include <vulkan/vulkan.h>

namespace Frontier
{
// 📝 Flags occupy LightComputeFootprint::Primary[3]. The two audit flags are diagnostic controls, not quality modes.
inline constexpr std::uint32_t PathReplayRoughOpaque = 64u, PathReplaySampleFacets = 128u,
    PathReplayAuditDoubleCount = 256u, PathReplayAuditStrictMotion = 512u, PathReplayBalanceSampling = 1024u,
    PathReplayTexturedDiffuse = 2048u, PathReplayImportanceRough = 4096u, PathReplayPowerWeightedFacets = 8192u,
    PathReplayUniformFacetMixture = 16384u, PathReplayReconstruct = 32768u, PathReplayResetReconstruction = 65536u,
    PathReplayDiffuseCameraReprojection = 131072u, PathReplaySampleHistory = 262144u,
    PathReplayResetSampleHistory = 524288u, PathReplaySpatialDisk = 1048576u,
    PathReplayVarianceFilter = 2097152u, PathReplayResetVarianceFilter = 4194304u,
    PathReplayOrderedTraversal = 8388608u, PathReplayHardwareRayQuery = 16777216u,
    PathReplayPowerMis = 33554432u, PathReplayCompactStorage = 67108864u,
    PathReplayAuthoredMaterials = 134217728u;

inline constexpr std::uint32_t PathReplayCounterWords = 23, PathReplayReconstructionCounterWords = 24;
inline constexpr std::uint32_t PathReplayCameraFloats = 48;
inline constexpr std::uint64_t PathReplayFixedOutputRecords = 15425;
inline constexpr std::uint32_t PathReplayMaxReceivers =
    static_cast<std::uint32_t>((std::numeric_limits<std::uint32_t>::max() - (PathReplayFixedOutputRecords - 1u)) / 30u);
// 📝 Counts float4 records, not bytes; the recorder separately rejects counts that overflow shader uint indexing.
constexpr std::uint64_t PathReplayRequiredOutputRecords(std::uint32_t Receivers) noexcept
{
    return std::uint64_t(Receivers) * 30u + PathReplayFixedOutputRecords;
}
inline constexpr std::uint32_t PathReplayMaxReconstructedReceivers =
    static_cast<std::uint32_t>((std::numeric_limits<std::uint32_t>::max() - (PathReplayFixedOutputRecords - 1u)) / 36u);
constexpr std::uint64_t PathReplayRequiredReconstructedOutputRecords(std::uint32_t Receivers) noexcept
{
    return std::uint64_t(Receivers) * 36u + PathReplayFixedOutputRecords;
}
inline constexpr std::uint32_t PathReplayMaxSampleHistoryReceivers =
    static_cast<std::uint32_t>((std::numeric_limits<std::uint32_t>::max() - (PathReplayFixedOutputRecords - 1u)) / 40u);
constexpr std::uint64_t PathReplayRequiredSampleHistoryOutputRecords(std::uint32_t Receivers) noexcept
{
    return std::uint64_t(Receivers) * 40u + PathReplayFixedOutputRecords;
}
inline constexpr std::uint32_t PathReplayMaxVarianceFilteredReceivers =
    static_cast<std::uint32_t>((std::numeric_limits<std::uint32_t>::max() - (PathReplayFixedOutputRecords - 1u)) / 46u);
constexpr std::uint64_t PathReplayRequiredVarianceFilteredOutputRecords(std::uint32_t Receivers) noexcept
{
    return std::uint64_t(Receivers) * 46u + PathReplayFixedOutputRecords;
}

// 📝 Compact output storage strips the unused 25*N + 15425 legacy prefix across reservoir, seed-bank and SVGF modes.
inline constexpr std::uint32_t PathReplayCompactBaseSlabs = 6u;
inline constexpr std::uint32_t PathReplayCompactSampleHistorySlabs = 10u;
inline constexpr std::uint32_t PathReplayCompactVarianceFilteredSlabs = 17u;
constexpr std::uint32_t PathReplayCompactSlabCount(bool SampleHistory, bool VarianceFilter) noexcept
{
    return VarianceFilter ? PathReplayCompactVarianceFilteredSlabs :
        (SampleHistory ? PathReplayCompactSampleHistorySlabs : PathReplayCompactBaseSlabs);
}
constexpr std::uint32_t PathReplayMaxCompactReceivers(bool SampleHistory = false, bool VarianceFilter = false) noexcept
{
    return static_cast<std::uint32_t>((std::uint64_t(UINT32_MAX) + 1u) /
                                      PathReplayCompactSlabCount(SampleHistory, VarianceFilter));
}
constexpr std::uint64_t PathReplayRequiredCompactOutputRecords(std::uint32_t Receivers, bool SampleHistory = false,
                                                               bool VarianceFilter = false) noexcept
{
    return std::uint64_t(Receivers) * PathReplayCompactSlabCount(SampleHistory, VarianceFilter);
}
// 📝 Invalid logical slots/pixels return UINT64_MAX, never aliasing into an active compact slab.
constexpr std::uint64_t PathReplayCompactSlot(std::uint32_t Slot, std::uint32_t Receivers,
                                              std::uint32_t Pixel) noexcept
{
    if (Pixel >= Receivers) return UINT64_MAX;
    std::uint32_t Slab = UINT32_MAX;
    if (Slot <= 4u) Slab = Slot + 1u;
    else if (Slot >= 11u && Slot <= 14u) Slab = Slot - 5u;
    else if (Slot == 9u) Slab = 10u;
    else if (Slot >= 15u && Slot <= 20u) Slab = Slot - 4u;
    return Slab == UINT32_MAX ? UINT64_MAX : std::uint64_t(Slab) * Receivers + Pixel;
}

struct PathReplayVulkanAccess
{
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSet Descriptors = VK_NULL_HANDLE;
    VkPipeline Primary = VK_NULL_HANDLE, Generate = VK_NULL_HANDLE, Resolve = VK_NULL_HANDLE;
    PFN_vkCmdBindPipeline BindPipeline = nullptr;
    PFN_vkCmdBindDescriptorSets BindDescriptors = nullptr;
    PFN_vkCmdPushConstants PushConstants = nullptr;
    PFN_vkCmdDispatch Dispatch = nullptr;
    PFN_vkCmdPipelineBarrier PipelineBarrier = nullptr;
    std::uint32_t MaxGroupCountX = 0, MaxPushConstantBytes = 0;
    VkPipeline GatherReconstruction = VK_NULL_HANDLE, FinalizeReconstruction = VK_NULL_HANDLE;
    VkPipeline GatherSampleHistory = VK_NULL_HANDLE, FinalizeSampleHistory = VK_NULL_HANDLE;
    VkPipeline GatherVarianceHistory = VK_NULL_HANDLE, IntegrateVarianceMoments = VK_NULL_HANDLE,
               FilterVarianceWavelet = VK_NULL_HANDLE;
};

// 📝 Counts describe the descriptor ranges, not merely the sizes of the underlying allocations.
struct PathReplayVulkanBuffers
{
    std::uint64_t ReceiverRecords = 0, EnclosureRecords = 0, FacetRecords = 0;
    std::uint64_t AngleFloats = 0, OutputColourRecords = 0, CounterWords = 0;
    bool AccelerationStructureBound = false;
};

// 📝 Records optional primary visibility, replay generation and resolve with compute-to-compute barriers.
//    Sampling[1] is the initial candidate count, 1–4; legacy zero means one. Changing it revises the sampling
//    policy: callers must update their reservoir history cap and reset incompatible colour reconstruction.
//    PathReplayReconstruct adds an old-guide gather before resolve and colour-history finalization afterward;
//    it requires both extra pipelines, 24 counter words and PathReplayRequiredReconstructedOutputRecords(N).
//    The caller must set PathReplayResetReconstruction for first use and every incompatible scene, camera,
//    material, lighting, history allocation or sampling-policy revision. PathReplayDiffuseCameraReprojection
//    permits strict same-world-point history only for an unchanged diffuse, untextured primary scene and a
//    verified camera-only transition. The caller must establish those conditions and valid previous projection;
//    the recorder cannot inspect buffer contents. It is not a general motion denoiser.
//    PathReplaySampleHistory is mutually exclusive with colour reconstruction. Without reservoir reuse it
//    averages one fresh path and up to three previously unselected seeds, all evaluated at the current surface;
//    with temporal and/or spatial reuse those seeds instead enter generation as one-sample reservoir
//    candidates (a seed already entered temporally is not counted twice) and resolve owns the output. It requires
//    primary, untextured diffuse inputs, one initial candidate, two additional pipelines, 24 counter words and
//    PathReplayRequiredSampleHistoryOutputRecords(N). Reset on first use, allocation/policy changes and all scene
//    revisions except a validated camera-only transition; never use this contract for animated scene reuse. The
//    caller must validate the same diffuse camera domain and previous projection as above, and must invalidate
//    snapshots after failed submission. These paths cost up to four evaluations; independence is not guaranteed.
//    PathReplayVarianceFilter adds subpixel motion-reprojected temporal demodulated-irradiance moments before
//    resolve and three edge-stopping B3-spline wavelet passes afterward (withDimensions[2..3] reserved as zero
//    on caller input). It is mutually exclusive with PathReplayReconstruct and PathReplayAuditDoubleCount,
//    composes with PathReplaySampleHistory, PathReplaySpatialDisk, PathReplayTexturedDiffuse,
//    PathReplayRoughOpaque and PathReplayAuthoredMaterials, and requires GatherVarianceHistory,
//    IntegrateVarianceMoments, FilterVarianceWavelet, 24 counter words and
//    PathReplayRequiredVarianceFilteredOutputRecords(N) (or 17*N with PathReplayCompactStorage). Set
//    PathReplayResetVarianceFilter on first use, allocation/policy revisions and scene/lighting changes.
//    PathReplayPowerMis selects overflow-safe Veach beta=2 power-heuristic MIS weights and requires
//    PathReplayBalanceSampling | PathReplaySampleFacets without PathReplayAuditDoubleCount.
//    PathReplayCompactStorage remaps OutputColour into 6*N, 10*N (SampleHistory) or 17*N (VarianceFilter)
//    float4 slabs without the legacy 25*N + 15425 prefix; it rejects legacy PathReplayReconstruct and
//    invalidates retained histories across layout transitions.
//    PathReplayAuthoredMaterials enables multi-strata PBR scattering, procedural metallic paint flakes,
//    normal/albedo mip pyramids, Charlie fabric sheen and thin-sheet glazing transmittance, requiring
//    PathReplayRoughOpaque | PathReplayImportanceRough and camera projection bounds.
//    Bindings 0–7 must match LightInfluence.slang (plus binding 8 SceneAcceleration when PathReplayHardwareRayQuery
//    is set and Buffers.AccelerationStructureBound is true); PathReplayOrderedTraversal enables front-to-back
//    child-ordered software BVH traversal and is mutually exclusive with PathReplayHardwareRayQuery. The pipeline
//    layout must expose a 128-byte compute push constant range at offset zero. The caller validates the scene and
//    material atlas, keeps descriptor ranges and objects alive, and supplies all input/output barriers, queue
//    ownership, submission and history lifecycle. In particular, this function does not allocate buffers, schedule a
//    separate reflection pass or present frames; BLAS/TLAS construction lives in PathReplayRayQuery. Its rough opaque
//    replay paths can contain specular as well as diffuse transport. Invalid arguments throw before recording any
//    command. The caller must not alias buffers in ways that race.
void RecordPathReplay(VkCommandBuffer Commands, const PathReplayVulkanAccess &Access,
                      const PathReplayVulkanBuffers &Buffers, const LightComputeFootprint &Extent);
} // namespace Frontier
