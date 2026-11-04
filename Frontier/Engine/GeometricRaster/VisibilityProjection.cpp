/*==========================================================================================================================================
                                             VISIBILITY PROJECTION DEFINITION
==========================================================================================================================================*/
// 🧩 Executes RasterizeVisibilityProjectionMain into the 4-layer RGBA16F VisibilityArray.

#include "Engine/GeometricRaster/VisibilityProjection.h"

#include <algorithm>
#include <array>

namespace Frontier
{
namespace
{
struct PushHeader
{
    uint32_t Counts[4];
    float    Tuning[4];
};
} // namespace

bool VisibilityProjection::Initialize(VulkanComputeExchange& Exchange,
                                      const std::string&     ShaderBinaryDirectory,
                                      std::string*           OutDiagnostic)
{
    const std::array<VkDescriptorType, 4> Bindings{{
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         // Ensemble
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // TransLut
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // CloudShadowLut
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          // VisibilityArray (4 layers)
    }};

    if (!Exchange.LoadComputeProgram(ShaderBinaryDirectory + "/RasterizeVisibilityProjection.spv",
                                     "RasterizeVisibilityProjectionMain",
                                     Bindings,
                                     sizeof(PushHeader),
                                     Program_,
                                     OutDiagnostic))
    {
        return false;
    }

    return Exchange.AllocateRgba32fArray(8u, 8u, 4u, VisibilityArray_);
}

void VisibilityProjection::Shutdown(VulkanComputeExchange& Exchange) noexcept
{
    Exchange.ReleaseImage(VisibilityArray_);
    Exchange.ReleaseProgram(Program_);
}

bool VisibilityProjection::EnsureDimensions(VulkanComputeExchange& Exchange,
                                            uint32_t               ViewportWidth,
                                            uint32_t               ViewportHeight)
{
    const uint32_t W = std::max(1u, ViewportWidth);
    const uint32_t H = std::max(1u, ViewportHeight);
    if (VisibilityArray_.Width == W && VisibilityArray_.Height == H && VisibilityArray_.ArrayLayers == 4u)
    {
        return true;
    }
    return Exchange.AllocateRgba32fArray(W, H, 4u, VisibilityArray_);
}

bool VisibilityProjection::Execute(VulkanComputeExchange&      Exchange,
                                   const DeviceMemorySpan&     EnsembleSpan,
                                   const AtmosphereProjection& AtmosphereLuts,
                                   const VolumetricClouds&     Clouds,
                                   uint32_t                    ViewportWidth,
                                   uint32_t                    ViewportHeight,
                                   uint32_t                    FrameIndex)
{
    const uint32_t W = std::max(1u, ViewportWidth);
    const uint32_t H = std::max(1u, ViewportHeight);
    if (!EnsureDimensions(Exchange, W, H))
    {
        return false;
    }

    const PushHeader Push{{W, H, FrameIndex, 0u}, {0.0f, 0.0f, 0.0f, 0.0f}};
    const std::array<ComputeDescriptorArgument, 4> Args{{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &EnsembleSpan, nullptr},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &AtmosphereLuts.TransmittanceArray()},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &Clouds.CloudShadowArray()},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &VisibilityArray_},
    }};
    return Exchange.DispatchCompute(Program_, Args, &Push, sizeof(Push), (W + 7u) / 8u, (H + 7u) / 8u, 1u);
}

bool VisibilityProjection::ReadbackLayer(VulkanComputeExchange& Exchange,
                                         uint32_t               LayerIndex,
                                         std::vector<float>&    OutRgbaFloats)
{
    return Exchange.DownloadFloatRgbaLayer(VisibilityArray_, LayerIndex, OutRgbaFloats);
}

} // namespace Frontier
