/*==========================================================================================================================================
                                                     DUALTIERVULKAN.CPP
==========================================================================================================================================*/
// 🧩 Implementation of DualTierVulkan submission graph resolution, transient target aliasing, Vulkan layout/barrier
//    translation, and RTX vs GTX VkCommandBuffer dispatch recording.

#include "Engine/Rendering/DualTierVulkan.h"

#include <algorithm>

namespace Frontier
{

namespace
{

uint32_t DivideRoundUp(uint32_t Numerator, uint32_t Denominator) noexcept
{
    return (Numerator + Denominator - 1u) / Denominator;
}

VkImageAspectFlags ResolveImageAspect(const std::string& TargetName,
                                      const std::vector<RenderTargetDeclaration>& Declarations) noexcept
{
    for (const RenderTargetDeclaration& Decl : Declarations)
    {
        if (Decl.Name == TargetName)
        {
            return Decl.Format == TargetFormat::Depth32Float
                ? VK_IMAGE_ASPECT_DEPTH_BIT
                : VK_IMAGE_ASPECT_COLOR_BIT;
        }
    }
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

} // namespace

//========================================================================================================================
//                                        VULKAN IMAGE LAYOUT & STAGE TRANSLATION
//========================================================================================================================

VulkanUsageBarrierState TranslateTargetUsageToVulkan(TargetUsage Usage) noexcept
{
    switch (Usage)
    {
        case TargetUsage::Undefined:
            return { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0u, VK_IMAGE_LAYOUT_UNDEFINED };
        case TargetUsage::Sampled:
            return { VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                     VK_ACCESS_SHADER_READ_BIT,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        case TargetUsage::StorageRead:
            return { VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                     VK_ACCESS_SHADER_READ_BIT,
                     VK_IMAGE_LAYOUT_GENERAL };
        case TargetUsage::DepthRead:
            return { VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                     VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_SHADER_READ_BIT,
                     VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
        case TargetUsage::TransferSource:
            return { VK_PIPELINE_STAGE_TRANSFER_BIT,
                     VK_ACCESS_TRANSFER_READ_BIT,
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL };
        case TargetUsage::StorageWrite:
            return { VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                     VK_ACCESS_SHADER_WRITE_BIT,
                     VK_IMAGE_LAYOUT_GENERAL };
        case TargetUsage::StorageReadWrite:
            return { VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                     VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                     VK_IMAGE_LAYOUT_GENERAL };
        case TargetUsage::ColourWrite:
            return { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        case TargetUsage::DepthWrite:
            return { VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                     VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                     VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
        case TargetUsage::TransferDestination:
            return { VK_PIPELINE_STAGE_TRANSFER_BIT,
                     VK_ACCESS_TRANSFER_WRITE_BIT,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL };
        case TargetUsage::Present:
            return { VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                     0u,
                     VK_IMAGE_LAYOUT_PRESENT_SRC_KHR };
    }
    return { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0u, VK_IMAGE_LAYOUT_UNDEFINED };
}

//========================================================================================================================
//                                      RESOLVED DUAL-TIER SUBMISSION & ATLAS PLAN
//========================================================================================================================

bool ResolveDualTierSubmissionPlan(
    uint32_t                Width,
    uint32_t                Height,
    DualTierExecutionMode   Mode,
    DualTierSubmissionPlan& OutPlan,
    std::string&            OutDiagnostic)
{
    OutPlan               = DualTierSubmissionPlan{};
    OutPlan.ExecutionMode = Mode;
    OutPlan.Width         = std::max(1u, Width);
    OutPlan.Height        = std::max(1u, Height);
    const uint32_t W      = OutPlan.Width;
    const uint32_t H      = OutPlan.Height;
    const bool     IsRtx  = (Mode == DualTierExecutionMode::HardwareRayQueryRtx);

    // Persistent + Transient Render Targets
    OutPlan.Targets = {
        { "DeformedWheelCage",      TargetFormat::Rgba16Float,  640u, 1u,    false, TargetUsage::Undefined },
        { "SurfaceNormalRoughness", TargetFormat::Rgba16Float,  W,    H,     false, TargetUsage::Undefined },
        { "SurfaceAlbedoMaterial",  TargetFormat::Rgba8Unorm,   W,    H,     false, TargetUsage::Undefined },
        { "SurfaceMotionVectors",   TargetFormat::Rg16Float,    W,    H,     false, TargetUsage::Undefined },
        { "SurfaceDepth",           TargetFormat::Depth32Float, W,    H,     false, TargetUsage::Undefined },
        { "DirectIllumination",     TargetFormat::Rgba16Float,  W,    H,     false, TargetUsage::Undefined },
        { "IndirectIrradianceRaw",  TargetFormat::Rgba16Float,  W,    H,     false, TargetUsage::Undefined },
        { "SpecularReflections",    TargetFormat::Rgba16Float,  W,    H,     false, TargetUsage::Undefined },
        { "FilteredLighting",       TargetFormat::Rgba16Float,  W,    H,     false, TargetUsage::Undefined },
        { "GlazingComposite",       TargetFormat::Rgba16Float,  W,    H,     false, TargetUsage::Undefined },
        { "AtmosphericComposite",   TargetFormat::Rgba16Float,  W,    H,     false, TargetUsage::Undefined },
        { "LightingHistory",        TargetFormat::Rgba16Float,  W,    H,     true,  TargetUsage::Sampled   },
        { "Swapchain",              TargetFormat::Bgra8Unorm,   W,    H,     true,  TargetUsage::Present   }
    };

    if (IsRtx)
    {
        OutPlan.Targets.push_back(
            { "ReservoirCurrent", TargetFormat::Rg32Uint, W, H, false, TargetUsage::Undefined });
        OutPlan.Targets.push_back(
            { "ReservoirTemporalHistory", TargetFormat::Rg32Uint, W, H, true, TargetUsage::StorageRead });
    }
    else
    {
        OutPlan.Targets.push_back(
            { "ToroidalDistanceCascades", TargetFormat::Rgba16Float, 256u, 64u, true, TargetUsage::Sampled });
        OutPlan.Targets.push_back(
            { "SurfaceCardAtlas", TargetFormat::Rgba16Float, 168u, 1u, true, TargetUsage::Sampled });
    }

    // 1. Volumetric 32x5 Neo-Hookean FEM Tyre Deformation (10,448-byte VehicleVisualRecord)
    OutPlan.Units.push_back(SubmissionUnitDeclaration{
        "VolumetricTyreDeformation",
        { { "DeformedWheelCage", TargetUsage::StorageWrite } },
        {}
    });

    if (IsRtx)
    {
        // 2A. RTX BLAS dynamic wheel refit + 8-instance TLAS build (VK_KHR_acceleration_structure)
        OutPlan.Units.push_back(SubmissionUnitDeclaration{
            "RayQueryAccelerationBuild",
            { { "DeformedWheelCage", TargetUsage::StorageRead } },
            { "VolumetricTyreDeformation" }
        });

        // 3A. Primary G-Buffer Surface Raster
        OutPlan.Units.push_back(SubmissionUnitDeclaration{
            "SurfaceRaster",
            { { "DeformedWheelCage",      TargetUsage::StorageRead },
              { "SurfaceNormalRoughness", TargetUsage::StorageWrite },
              { "SurfaceAlbedoMaterial",  TargetUsage::StorageWrite },
              { "SurfaceMotionVectors",   TargetUsage::StorageWrite },
              { "SurfaceDepth",           TargetUsage::DepthWrite } },
            { "RayQueryAccelerationBuild" }
        });

        // 4A. Inline RayQuery Direct Shadow + Initial RIS Reservoir Generation (VK_KHR_ray_query)
        OutPlan.Units.push_back(SubmissionUnitDeclaration{
            "RayQueryDirectAndRis",
            { { "SurfaceNormalRoughness", TargetUsage::Sampled },
              { "SurfaceAlbedoMaterial",  TargetUsage::Sampled },
              { "SurfaceDepth",           TargetUsage::DepthRead },
              { "DirectIllumination",     TargetUsage::StorageWrite },
              { "ReservoirCurrent",       TargetUsage::StorageWrite },
              { "SpecularReflections",    TargetUsage::StorageWrite } },
            { "RayQueryAccelerationBuild" }
        });

        // 5A. ReSTIR GI Temporal + Jacobian Spatial Reservoir Resampling
        OutPlan.Units.push_back(SubmissionUnitDeclaration{
            "ReservoirTemporalSpatialReuse",
            { { "SurfaceNormalRoughness",   TargetUsage::Sampled },
              { "SurfaceMotionVectors",     TargetUsage::Sampled },
              { "ReservoirTemporalHistory", TargetUsage::StorageRead },
              { "ReservoirCurrent",         TargetUsage::StorageRead },
              { "IndirectIrradianceRaw",    TargetUsage::StorageWrite },
              { "ReservoirTemporalHistory", TargetUsage::StorageWrite } },
            {}
        });

        // 6A. 4-Step 5x5 B3-Spline À-Trous VarianceWavelet SVGF Filter
        OutPlan.Units.push_back(SubmissionUnitDeclaration{
            "VarianceWaveletFilter",
            { { "SurfaceNormalRoughness", TargetUsage::Sampled },
              { "SurfaceAlbedoMaterial",  TargetUsage::Sampled },
              { "SurfaceDepth",           TargetUsage::DepthRead },
              { "LightingHistory",        TargetUsage::Sampled },
              { "DirectIllumination",     TargetUsage::Sampled },
              { "IndirectIrradianceRaw",  TargetUsage::Sampled },
              { "SpecularReflections",    TargetUsage::Sampled },
              { "FilteredLighting",       TargetUsage::StorageWrite } },
            {}
        });
    }
    else
    {
        // 2B. GTX 4-Cascade Toroidal Signed Distance Clipmap Update (SceneClip0..3)
        OutPlan.Units.push_back(SubmissionUnitDeclaration{
            "ToroidalDistanceClipmap",
            { { "DeformedWheelCage",        TargetUsage::StorageRead },
              { "ToroidalDistanceCascades", TargetUsage::StorageWrite },
              { "ToroidalDistanceCascades", TargetUsage::Sampled } },
            { "VolumetricTyreDeformation" }
        });

        // 3B. GTX 168-Texel Multi-Bounce SurfaceCard Radiosity Solve
        OutPlan.Units.push_back(SubmissionUnitDeclaration{
            "SurfaceCardRadiosity",
            { { "ToroidalDistanceCascades", TargetUsage::Sampled },
              { "SurfaceCardAtlas",         TargetUsage::StorageWrite },
              { "SurfaceCardAtlas",         TargetUsage::Sampled } },
            {}
        });

        // 4B. Primary G-Buffer Surface Raster
        OutPlan.Units.push_back(SubmissionUnitDeclaration{
            "SurfaceRaster",
            { { "DeformedWheelCage",      TargetUsage::StorageRead },
              { "SurfaceNormalRoughness", TargetUsage::StorageWrite },
              { "SurfaceAlbedoMaterial",  TargetUsage::StorageWrite },
              { "SurfaceMotionVectors",   TargetUsage::StorageWrite },
              { "SurfaceDepth",           TargetUsage::DepthWrite } },
            {}
        });

        // 5B. GTX Distance-Field Cone-Traced Indirect GI + Soft Solar Shadows
        OutPlan.Units.push_back(SubmissionUnitDeclaration{
            "DistanceFieldConeIrradiance",
            { { "SurfaceNormalRoughness",   TargetUsage::Sampled },
              { "SurfaceAlbedoMaterial",    TargetUsage::Sampled },
              { "SurfaceDepth",             TargetUsage::DepthRead },
              { "ToroidalDistanceCascades", TargetUsage::Sampled },
              { "SurfaceCardAtlas",         TargetUsage::Sampled },
              { "DirectIllumination",       TargetUsage::StorageWrite },
              { "IndirectIrradianceRaw",    TargetUsage::StorageWrite } },
            {}
        });

        // 6B. GTX Roughness-Gated Two-Level Ordered Software BVH Reflections + Lighting Combine
        OutPlan.Units.push_back(SubmissionUnitDeclaration{
            "OrderedBvhReflections",
            { { "SurfaceNormalRoughness", TargetUsage::Sampled },
              { "SurfaceAlbedoMaterial",  TargetUsage::Sampled },
              { "SurfaceDepth",           TargetUsage::DepthRead },
              { "LightingHistory",        TargetUsage::Sampled },
              { "DirectIllumination",     TargetUsage::Sampled },
              { "IndirectIrradianceRaw",  TargetUsage::Sampled },
              { "SpecularReflections",    TargetUsage::StorageWrite },
              { "FilteredLighting",       TargetUsage::StorageWrite } },
            {}
        });
    }

    // 7. Solid Dielectric Cabin Glazing Refraction (74 VehicleGlass facets, IOR 1.52)
    OutPlan.Units.push_back(SubmissionUnitDeclaration{
        "DielectricCanopyRefraction",
        { { "FilteredLighting",  TargetUsage::Sampled },
          { "SurfaceDepth",      TargetUsage::DepthRead },
          { "GlazingComposite",  TargetUsage::StorageWrite } },
        {}
    });

    // 8. LUT Atmosphere + Volumetric Cumulus Clouds + Koschmieder Fog + DefaultFlareGhosts Optics
    OutPlan.Units.push_back(SubmissionUnitDeclaration{
        "SolarAtmosphereCloudFog",
        { { "GlazingComposite",     TargetUsage::Sampled },
          { "SurfaceDepth",         TargetUsage::DepthRead },
          { "AtmosphericComposite", TargetUsage::StorageWrite } },
        {}
    });

    // 9. Temporal History Commit (returns LightingHistory to RestingUsage::Sampled)
    OutPlan.Units.push_back(SubmissionUnitDeclaration{
        "HistoryCommit",
        { { "FilteredLighting",      TargetUsage::Sampled },
          { "SurfaceMotionVectors",  TargetUsage::Sampled },
          { "LightingHistory",       TargetUsage::StorageWrite },
          { "LightingHistory",       TargetUsage::Sampled } },
        {}
    });

    // 10. Narkowicz ACES Tone-Mapping & Swapchain Present
    OutPlan.Units.push_back(SubmissionUnitDeclaration{
        "DisplayPresent",
        { { "AtmosphericComposite", TargetUsage::Sampled },
          { "Swapchain",            TargetUsage::ColourWrite },
          { "Swapchain",            TargetUsage::Present } },
        { "HistoryCommit" }
    });

    if (!ResolveSubmissionOrder(OutPlan.Units, OutPlan.Order, OutDiagnostic))
    {
        return false;
    }
    if (!DeriveBarriers(OutPlan.Units, OutPlan.Order, OutPlan.Targets, OutPlan.Barriers, OutDiagnostic))
    {
        return false;
    }
    if (!ResolveTransientTargetAtlas(OutPlan.Units, OutPlan.Order, OutPlan.Targets, OutPlan.Atlas, OutDiagnostic))
    {
        return false;
    }
    return true;
}

//========================================================================================================================
//                                      VULKAN COMMAND-BUFFER SUBMISSION RECORDER
//========================================================================================================================

DualTierRecordingSummary RecordDualTierVulkanSubmission(
    VkCommandBuffer                Commands,
    const DualTierVulkanAccess&    Access,
    const DualTierVulkanPipelines& Pipelines,
    const DualTierSubmissionPlan&  Plan,
    const DualTierVulkanResources& Resources,
    const DualTierPushConstants&   BaseConstants,
    std::string&                   OutDiagnostic)
{
    DualTierRecordingSummary Summary{};
    Summary.TransientAliasedBytes   = Plan.Atlas.TotalBytes;
    Summary.TransientUnaliasedBytes = Plan.Atlas.UnaliasedBytes;

    if (Commands == VK_NULL_HANDLE)
    {
        OutDiagnostic = "RecordDualTierVulkanSubmission refused VK_NULL_HANDLE command buffer.";
        return Summary;
    }
    if (Access.CmdBindPipeline == nullptr || Access.CmdBindDescriptorSets == nullptr ||
        Access.CmdPushConstants == nullptr || Access.CmdDispatch == nullptr || Access.CmdPipelineBarrier == nullptr)
    {
        OutDiagnostic = "RecordDualTierVulkanSubmission requires non-null Vulkan dispatch table pointers.";
        return Summary;
    }
    if (Access.PipelineLayout == VK_NULL_HANDLE || Access.GlobalDescriptorSet == VK_NULL_HANDLE)
    {
        OutDiagnostic = "RecordDualTierVulkanSubmission requires valid PipelineLayout and GlobalDescriptorSet handles.";
        return Summary;
    }

    const bool IsRtx = (Plan.ExecutionMode == DualTierExecutionMode::HardwareRayQueryRtx);
    if (IsRtx && Access.CmdBuildAccelerationStructures == nullptr)
    {
        OutDiagnostic = "HardwareRayQueryRtx submission requires vkCmdBuildAccelerationStructuresKHR.";
        return Summary;
    }

    const uint32_t GroupsX8 = DivideRoundUp(Plan.Width, 8u);
    const uint32_t GroupsY8 = DivideRoundUp(Plan.Height, 8u);
    size_t         BarrierCursor = 0u;

    const auto EmitBarriersUpToPosition = [&](uint32_t Position) {
        while (BarrierCursor < Plan.Barriers.Barriers.size() &&
               Plan.Barriers.Barriers[BarrierCursor].BeforePosition == Position)
        {
            const DerivedBarrier&          B   = Plan.Barriers.Barriers[BarrierCursor++];
            const VulkanUsageBarrierState  Src = TranslateTargetUsageToVulkan(B.From);
            const VulkanUsageBarrierState  Dst = TranslateTargetUsageToVulkan(B.To);

            if (B.Kind == BarrierKind::LayoutTransition)
            {
                auto It = Resources.TargetImages.find(B.Target);
                VkImage ImageHandle = (It != Resources.TargetImages.end()) ? It->second : VK_NULL_HANDLE;
                VkImageMemoryBarrier ImageBarrier{};
                ImageBarrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                ImageBarrier.srcAccessMask                   = Src.AccessMask;
                ImageBarrier.dstAccessMask                   = Dst.AccessMask;
                ImageBarrier.oldLayout                       = Src.Layout;
                ImageBarrier.newLayout                       = Dst.Layout;
                ImageBarrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                ImageBarrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                ImageBarrier.image                           = ImageHandle;
                ImageBarrier.subresourceRange.aspectMask     = ResolveImageAspect(B.Target, Plan.Targets);
                ImageBarrier.subresourceRange.baseMipLevel   = 0u;
                ImageBarrier.subresourceRange.levelCount     = 1u;
                ImageBarrier.subresourceRange.baseArrayLayer = 0u;
                ImageBarrier.subresourceRange.layerCount     = 1u;

                Access.CmdPipelineBarrier(Commands, Src.StageMask, Dst.StageMask, 0u,
                                          0u, nullptr, 0u, nullptr, 1u, &ImageBarrier);
                ++Summary.ImageLayoutTransitions;
            }
            else
            {
                VkMemoryBarrier MemBarrier{};
                MemBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                MemBarrier.srcAccessMask = Src.AccessMask;
                MemBarrier.dstAccessMask = Dst.AccessMask;
                Access.CmdPipelineBarrier(Commands, Src.StageMask, Dst.StageMask, 0u,
                                          1u, &MemBarrier, 0u, nullptr, 0u, nullptr);
                ++Summary.ExecutionMemoryBarriers;
            }
        }
    };

    const auto DispatchComputeUnit = [&](VkPipeline Pipeline, uint32_t Gx, uint32_t Gy, uint32_t Gz, uint32_t SubParam) {
        Access.CmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
        Access.CmdBindDescriptorSets(Commands, VK_PIPELINE_BIND_POINT_COMPUTE,
                                     Access.PipelineLayout, 0u, 1u, &Access.GlobalDescriptorSet, 0u, nullptr);
        DualTierPushConstants Push = BaseConstants;
        Push.ViewportWidth         = Plan.Width;
        Push.ViewportHeight        = Plan.Height;
        Push.ExecutionTier         = IsRtx ? 0u : 1u;
        Push.StepOrdinal           = SubParam;
        Access.CmdPushConstants(Commands, Access.PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                                0u, sizeof(DualTierPushConstants), &Push);
        Access.CmdDispatch(Commands, std::max(1u, Gx), std::max(1u, Gy), std::max(1u, Gz));
        ++Summary.ComputeDispatches;
    };

    for (uint32_t Pos = 0u; Pos < static_cast<uint32_t>(Plan.Order.Order.size()); ++Pos)
    {
        EmitBarriersUpToPosition(Pos);
        const std::string& UnitName = Plan.Units[Plan.Order.Order[Pos]].Name;

        if (UnitName == "VolumetricTyreDeformation")
        {
            // 4 wheels x ceil((1984 tyre + 786 rim) / 64) = 4 x 44 workgroups
            for (uint32_t Wheel = 0u; Wheel < 4u; ++Wheel)
            {
                DispatchComputeUnit(Pipelines.VolumetricTyreDeformation, DivideRoundUp(2770u, 64u), 1u, 1u, Wheel);
            }
        }
        else if (UnitName == "RayQueryAccelerationBuild")
        {
            PathReplayAccelerationBuildAccess BuildAccess{};
            BuildAccess.Device                         = Access.Device;
            BuildAccess.GetBuildSizes                  = Access.GetAccelerationBuildSizes;
            BuildAccess.CmdBuildAccelerationStructures = Access.CmdBuildAccelerationStructures;
            BuildAccess.CmdPipelineBarrier             = Access.CmdPipelineBarrier;
            RecordPathReplayAccelerationBuild(Commands, BuildAccess, Resources.AccelerationPlan, Resources.AccelerationTargets);
            Summary.AccelerationStructureBuilds += 2u; // BLAS + TLAS
            Summary.ExecutionMemoryBarriers     += 2u;
        }
        else if (UnitName == "ToroidalDistanceClipmap")
        {
            for (uint32_t Cascade = 0u; Cascade < 4u; ++Cascade)
            {
                DispatchComputeUnit(Pipelines.ToroidalDistanceClipmap, 8u, 8u, 8u, Cascade);
            }
        }
        else if (UnitName == "SurfaceCardRadiosity")
        {
            DispatchComputeUnit(Pipelines.SurfaceCardRadiosity, DivideRoundUp(168u, 64u), 1u, 1u, 168u);
        }
        else if (UnitName == "SurfaceRaster")
        {
            DispatchComputeUnit(Pipelines.SurfaceRasterCompute, GroupsX8, GroupsY8, 1u, 0u);
        }
        else if (UnitName == "RayQueryDirectAndRis")
        {
            DispatchComputeUnit(Pipelines.RayQueryDirectAndRis, GroupsX8, GroupsY8, 1u, 0u);
        }
        else if (UnitName == "ReservoirTemporalSpatialReuse")
        {
            DispatchComputeUnit(Pipelines.ReservoirTemporalSpatial, GroupsX8, GroupsY8, 1u, 20u);
        }
        else if (UnitName == "VarianceWaveletFilter")
        {
            for (uint32_t Step : { 1u, 2u, 4u, 8u })
            {
                DispatchComputeUnit(Pipelines.VarianceWaveletFilter, GroupsX8, GroupsY8, 1u, Step);
            }
        }
        else if (UnitName == "DistanceFieldConeIrradiance")
        {
            DispatchComputeUnit(Pipelines.DistanceFieldConeIrradiance, GroupsX8, GroupsY8, 1u, 0u);
        }
        else if (UnitName == "OrderedBvhReflections")
        {
            DispatchComputeUnit(Pipelines.OrderedBvhReflections, GroupsX8, GroupsY8, 1u, 0u);
        }
        else if (UnitName == "DielectricCanopyRefraction")
        {
            DispatchComputeUnit(Pipelines.DielectricCanopyRefraction, GroupsX8, GroupsY8, 1u, 74u);
        }
        else if (UnitName == "SolarAtmosphereCloudFog")
        {
            DispatchComputeUnit(Pipelines.SolarAtmosphereCloudFog, GroupsX8, GroupsY8, 1u, 0u);
        }
        else if (UnitName == "HistoryCommit")
        {
            DispatchComputeUnit(Pipelines.HistoryCommit, GroupsX8, GroupsY8, 1u, 0u);
        }
        else if (UnitName == "DisplayPresent")
        {
            DispatchComputeUnit(Pipelines.DisplayPresent, GroupsX8, GroupsY8, 1u, 0u);
        }
        ++Summary.RecordedUnits;
    }

    // Flush trailing end-of-frame barriers (if any)
    while (BarrierCursor < Plan.Barriers.Barriers.size())
    {
        EmitBarriersUpToPosition(Plan.Barriers.Barriers[BarrierCursor].BeforePosition);
    }

    Summary.Succeeded = true;
    return Summary;
}

} // namespace Frontier
