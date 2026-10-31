/*==========================================================================================================================================
                                                      DUALTIERVULKAN.H
==========================================================================================================================================*/
// 🧩 Vulkan command-buffer submission planner and recorder for Project Everest's Dual-Tier GPU pipeline:
//    - Tier A (HardwareRayQueryRtx): VolumetricTyreDeformation -> VK_KHR_acceleration_structure BLAS/TLAS build ->
//      SurfaceRaster -> VK_KHR_ray_query ReSTIR GI -> 4-step VarianceWavelet SVGF -> DielectricCanopy -> SolarOptics -> DisplayPresent.
//    - Tier B (ToroidalSdfBvhGtx): VolumetricTyreDeformation -> 4-Cascade ToroidalDistanceClipmap -> 168-Texel SurfaceCardRadiosity ->
//      SurfaceRaster -> DistanceFieldConeIrradiance -> OrderedBvhReflections -> DielectricCanopy -> SolarOptics -> DisplayPresent.

#pragma once

#include "Engine/Rendering/BarrierDerivation.h"
#include "Engine/Rendering/DualTierRenderer.h"
#include "Engine/Rendering/PathReplayRayQuery.h"
#include "Engine/Rendering/SubmissionOrder.h"
#include "Engine/Rendering/TransientTargetAtlas.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                        VULKAN IMAGE LAYOUT & STAGE TRANSLATION
//------------------------------------------------------------------------------------------------------------------------

struct VulkanUsageBarrierState
{
    VkPipelineStageFlags StageMask  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkAccessFlags        AccessMask = 0u;
    VkImageLayout        Layout     = VK_IMAGE_LAYOUT_UNDEFINED;
};

[[nodiscard]] VulkanUsageBarrierState TranslateTargetUsageToVulkan(TargetUsage Usage) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                      RESOLVED DUAL-TIER SUBMISSION & ATLAS PLAN
//------------------------------------------------------------------------------------------------------------------------

struct DualTierSubmissionPlan
{
    DualTierExecutionMode                  ExecutionMode = DualTierExecutionMode::HardwareRayQueryRtx;
    uint32_t                               Width         = 0u;
    uint32_t                               Height        = 0u;
    std::vector<SubmissionUnitDeclaration> Units{};
    std::vector<RenderTargetDeclaration>   Targets{};
    ResolvedSubmissionOrder                Order{};
    DerivedBarriers                        Barriers{};
    ResolvedTransientAtlas                 Atlas{};
};

[[nodiscard]] bool ResolveDualTierSubmissionPlan(
    uint32_t                Width,
    uint32_t                Height,
    DualTierExecutionMode   Mode,
    DualTierSubmissionPlan& OutPlan,
    std::string&            OutDiagnostic);

//------------------------------------------------------------------------------------------------------------------------
//                                      VULKAN ACCESS TABLE & RECORDING CONTRACT
//------------------------------------------------------------------------------------------------------------------------

struct alignas(16) DualTierPushConstants
{
    uint32_t ViewportWidth        = 1920u;
    uint32_t ViewportHeight       = 1080u;
    uint32_t ExecutionTier        = 0u;      // 0 = HardwareRayQueryRtx, 1 = ToroidalSdfBvhGtx
    uint32_t InspectionMode       = 0u;
    uint32_t StepOrdinal          = 0u;      // Wheel index (0..3) or SVGF step (1, 2, 4, 8) or Cascade (0..3)
    uint32_t ClusteredLightCount  = 32u;
    uint32_t SurfaceCardTexels    = 168u;
    uint32_t VehicleRecordBytes   = 10448u;
    float    SolarDirection[4]    = { -0.646f, 0.452f, 0.616f, 12.0f };
    float    VehicleWorldPose[4]  = { -40.0f, 0.0f, 0.53f, 0.0f };
};

static_assert(sizeof(DualTierPushConstants) == 64u,
              "DualTierPushConstants fits inside Vulkan's universal 128-byte minimum push-constant guarantee.");

struct DualTierVulkanAccess
{
    VkDevice                                 Device                         = VK_NULL_HANDLE;
    VkPipelineLayout                         PipelineLayout                 = VK_NULL_HANDLE;
    VkDescriptorSet                          GlobalDescriptorSet            = VK_NULL_HANDLE;
    PFN_vkCmdBindPipeline                    CmdBindPipeline                = nullptr;
    PFN_vkCmdBindDescriptorSets              CmdBindDescriptorSets          = nullptr;
    PFN_vkCmdPushConstants                   CmdPushConstants               = nullptr;
    PFN_vkCmdDispatch                        CmdDispatch                    = nullptr;
    PFN_vkCmdPipelineBarrier                 CmdPipelineBarrier             = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR GetAccelerationBuildSizes   = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR  CmdBuildAccelerationStructures = nullptr;
};

struct DualTierVulkanPipelines
{
    VkPipeline VolumetricTyreDeformation    = VK_NULL_HANDLE;
    VkPipeline ToroidalDistanceClipmap      = VK_NULL_HANDLE;
    VkPipeline SurfaceCardRadiosity         = VK_NULL_HANDLE;
    VkPipeline SurfaceRasterCompute         = VK_NULL_HANDLE;
    VkPipeline RayQueryDirectAndRis         = VK_NULL_HANDLE;
    VkPipeline ReservoirTemporalSpatial     = VK_NULL_HANDLE;
    VkPipeline VarianceWaveletFilter        = VK_NULL_HANDLE;
    VkPipeline DistanceFieldConeIrradiance  = VK_NULL_HANDLE;
    VkPipeline OrderedBvhReflections        = VK_NULL_HANDLE;
    VkPipeline DielectricCanopyRefraction   = VK_NULL_HANDLE;
    VkPipeline SolarAtmosphereCloudFog      = VK_NULL_HANDLE;
    VkPipeline HistoryCommit                = VK_NULL_HANDLE;
    VkPipeline DisplayPresent               = VK_NULL_HANDLE;
};

struct DualTierVulkanResources
{
    std::unordered_map<std::string, VkImage> TargetImages{};
    PathReplayAccelerationBuildPlan          AccelerationPlan{};
    PathReplayAccelerationBuildTargets       AccelerationTargets{};
};

struct DualTierRecordingSummary
{
    bool     Succeeded                    = false;
    uint32_t RecordedUnits                = 0u;
    uint32_t ImageLayoutTransitions       = 0u;
    uint32_t ExecutionMemoryBarriers      = 0u;
    uint32_t ComputeDispatches            = 0u;
    uint32_t AccelerationStructureBuilds  = 0u;
    uint64_t TransientAliasedBytes        = 0u;
    uint64_t TransientUnaliasedBytes      = 0u;
};

[[nodiscard]] DualTierRecordingSummary RecordDualTierVulkanSubmission(
    VkCommandBuffer                Commands,
    const DualTierVulkanAccess&    Access,
    const DualTierVulkanPipelines& Pipelines,
    const DualTierSubmissionPlan&  Plan,
    const DualTierVulkanResources& Resources,
    const DualTierPushConstants&   BaseConstants,
    std::string&                   OutDiagnostic);

} // namespace Frontier
