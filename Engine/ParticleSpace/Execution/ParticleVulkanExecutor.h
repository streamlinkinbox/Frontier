/*============================================================================================================================================
                                                        PARTICLEVULKANEXECUTOR.H
============================================================================================================================================*/
// 🧩 Vulkan 1.3 Compute + Raster executor routed through GeometricRaster::VulkanInterchange (hardware ICD or SwiftShader)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Compute/ComputeDispatchEncoder.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/ParticleSpace/Execution/ParticleMetricsCollector.h"
#include "Engine/ParticleSpace/ParticleBuffers.h"
#include "Engine/ParticleSpace/ParticleDocument.h"
#include "Engine/ParticleSpace/ParticleSoftwareExecutor.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Frontier::ParticleSpace::Execution
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ParticleRenderViewportSpec
{
    uint32_t WidthPx           = 800u;  // [px]  - Offscreen render target width
    uint32_t HeightPx          = 600u;  // [px]  - Offscreen render target height
    bool     RenderFloorGrid   = true;  // [-]   - Draw reference floor grid
    bool     RenderColliderBall= true;  // [-]   - Draw interactive chrome collider ball
    bool     CapturePixels     = true;  // [-]   - Download RGBA8 pixels into host vector
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        CLASSES
//------------------------------------------------------------------------------------------------------------------------

class ParticleVulkanExecutor
{
public:
    ParticleVulkanExecutor() = default;
    ~ParticleVulkanExecutor();

    ParticleVulkanExecutor(const ParticleVulkanExecutor&)            = delete;
    ParticleVulkanExecutor& operator=(const ParticleVulkanExecutor&) = delete;

    [[nodiscard]] bool InitializeExecutor(
        GeometricRaster::VulkanInterchange* BoundDevice,
        const ParticleDocument&             Document,
        const std::filesystem::path&        ShaderDirectory = {});

    void TeardownExecutor() noexcept;

    [[nodiscard]] bool ResetDocument(const ParticleDocument& Document);

    [[nodiscard]] bool AdvanceRenderStep(
        const ParticleDocument&           Document,
        float                             DeltaSeconds,
        const ParticleRenderViewportSpec& ViewportSpec);

    [[nodiscard]] bool                       Ready() const noexcept { return Initialized; }
    [[nodiscard]] std::span<const uint8_t>   InspectRenderedRgba8() const noexcept { return CapturedRgbaPixels; }
    [[nodiscard]] uint32_t                   QueryWidthPx() const noexcept { return TargetWidthPx; }
    [[nodiscard]] uint32_t                   QueryHeightPx() const noexcept { return TargetHeightPx; }
    [[nodiscard]] ParticleMetricsSnapshot    QueryMetrics() const noexcept { return MetricsCollector.QuerySnapshot(); }
    [[nodiscard]] const ParticleSoftwareExecutor& QuerySoftwareParity() const noexcept { return SoftwareParity; }
    [[nodiscard]] ParticleSlot               InspectGpuSlot(uint32_t Index) const noexcept;

private:
    enum class RasterProgramSlot : uint8_t
    {
        BillboardAdditive = 0u,
        BillboardAlpha    = 1u,
        Ribbon            = 2u,
        Solid             = 3u,
        Winged            = 4u,
        LineArc           = 5u,
        Lens              = 6u,
        SurfaceBacking    = 7u,
        OverlayGrid       = 8u,
        Count             = 9u,
    };

    struct RasterExecutableUnit
    {
        VkShaderModule VertexBinary   = VK_NULL_HANDLE;
        VkShaderModule FragmentBinary = VK_NULL_HANDLE;
        VkPipeline     Executable     = VK_NULL_HANDLE;
    };

    [[nodiscard]] bool CreateGpuExecutables(const std::filesystem::path& ShaderDir);
    [[nodiscard]] bool EnsureRenderTargets(uint32_t WidthPx, uint32_t HeightPx);
    void               DestroyRenderTargets() noexcept;

    GeometricRaster::VulkanInterchange*      DeviceOwner        = nullptr;
    ParticleSoftwareExecutor                 SoftwareParity     {};
    ParticleMetricsCollector                 MetricsCollector   {};

    VkShaderModule                           ComputeBinaries[4] {};
    VkPipeline                               ComputeUnits[4]    {};

    VkDescriptorSetLayout                    UnifiedSetLayout     = VK_NULL_HANDLE;
    VkDescriptorPool                         UnifiedDescriptorBank = VK_NULL_HANDLE;
    VkDescriptorSet                          UnifiedDescriptorSlot = VK_NULL_HANDLE;
    VkPipelineLayout                         UnifiedLayout        = VK_NULL_HANDLE;
    VkRenderPass                             OffscreenTargetScope = VK_NULL_HANDLE;
    RasterExecutableUnit                     RasterUnits[9]     {};

    GeometricRaster::DeviceBufferResource    GlobalUniformBuf   {};
    GeometricRaster::DeviceBufferResource    RigUniformBuf      {};
    GeometricRaster::DeviceBufferResource    ColourLutBuf       {};
    GeometricRaster::DeviceBufferResource    ParticleSlotBuf    {};
    GeometricRaster::DeviceBufferResource    StrandVertexBuf    {};
    GeometricRaster::DeviceBufferResource    ForceSourceBuf     {};
    GeometricRaster::DeviceBufferResource    ForceWaypointBuf   {};
    GeometricRaster::DeviceBufferResource    TelemetryBuf       {};
    GeometricRaster::DeviceBufferResource    StagingReadbackBuf {};

    GeometricRaster::DeviceImageResource     ColorTargetImg     {};
    GeometricRaster::DeviceImageResource     DepthTargetImg     {};
    VkFramebuffer                            OffscreenFramebuffer = VK_NULL_HANDLE;

    uint32_t                                 AllocatedParticleSlots = 0u;
    uint32_t                                 AllocatedStrandVerts   = 0u;
    uint32_t                                 TargetWidthPx          = 0u;
    uint32_t                                 TargetHeightPx         = 0u;
    double                                   ElapsedSeconds         = 0.0;
    uint64_t                                 StepCount              = 0ull;
    bool                                     Initialized            = false;
    std::vector<uint8_t>                     CapturedRgbaPixels     {};
};

} // namespace Frontier::ParticleSpace::Execution
