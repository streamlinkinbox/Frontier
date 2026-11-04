/*==========================================================================================================================================
                                                 VISIBILITY PROJECTION
                                     ------------------------------------------------------------------
             Primary camera & cross-portal ray routing, reversed-Z surface visibility, dielectric
             glass transmission masks, and cloud-shadowed + moonlit opaque surface illumination.
==========================================================================================================================================*/
// 🧩 GeometricRaster visibility projection: routes rays across PortalApertures and shades opaque surfaces.

#pragma once

#include "Engine/DeviceExchange/VulkanComputeExchange.h"
#include "Engine/PlanetaryAtmosphere/AtmosphereProjection.h"
#include "Engine/PlanetaryAtmosphere/VolumetricClouds.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

class VisibilityProjection
{
  public:
    bool Initialize(VulkanComputeExchange& Exchange,
                    const std::string&     ShaderBinaryDirectory,
                    std::string*           OutDiagnostic = nullptr);
    void Shutdown(VulkanComputeExchange& Exchange) noexcept;

    bool EnsureDimensions(VulkanComputeExchange& Exchange, uint32_t ViewportWidth, uint32_t ViewportHeight);

    bool Execute(VulkanComputeExchange&      Exchange,
                 const DeviceMemorySpan&     EnsembleSpan,
                 const AtmosphereProjection& AtmosphereLuts,
                 const VolumetricClouds&     Clouds,
                 uint32_t                    ViewportWidth,
                 uint32_t                    ViewportHeight,
                 uint32_t                    FrameIndex);

    [[nodiscard]] const DeviceImageArraySlot& VisibilityArray() const noexcept { return VisibilityArray_; }

    bool ReadbackLayer(VulkanComputeExchange& Exchange, uint32_t LayerIndex, std::vector<float>& OutRgbaFloats);

  private:
    ComputeProgramSlot   Program_{};
    DeviceImageArraySlot VisibilityArray_{};
};

} // namespace Frontier
