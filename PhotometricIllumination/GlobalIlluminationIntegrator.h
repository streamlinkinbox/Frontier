//============================================================================================================================================
// 📦 Frontier/PhotometricIllumination/GlobalIlluminationIntegrator.h — ReSTIR GI Radiosity with Jacobian Shift Resampling
//============================================================================================================================================

#pragma once

#include "../GeometricRaster/MaterialCodec.h"
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                              INDIRECT PATH RESERVOIR
//------------------------------------------------------------------------------------------------------------------------

struct IndirectPathReservoir
{
    Vector3                 SampledRadiance;                    // [lux] outgoing indirect radiosity L_o
    Vector3                 HitPosition;                        // [m] surface position of first bounce
    Vector3                 HitNormal;                          // [-] surface normal of first bounce
    float                   WeightSum;                          // [-] accumulated candidate weights
    uint32_t                SampleCount;                        // [count] number of considered indirect paths
    float                   UnbiasedWeight;                     // [-] normalization weight W
};

//------------------------------------------------------------------------------------------------------------------------
//                                      GLOBAL ILLUMINATION INTEGRATOR
//------------------------------------------------------------------------------------------------------------------------

class GlobalIlluminationIntegrator
{
public:
    GlobalIlluminationIntegrator(uint32_t ExtentWidth, uint32_t ExtentHeight) noexcept;
    ~GlobalIlluminationIntegrator() noexcept = default;

    GlobalIlluminationIntegrator(const GlobalIlluminationIntegrator&) = delete;
    GlobalIlluminationIntegrator& operator=(const GlobalIlluminationIntegrator&) = delete;

    void                    IntegrateGlobalIllumination(
                                const VisibilityProjection& Visibility,
                                const GeometryStructure& Geometry,
                                const MaterialCodec& Codec,
                                std::vector<Vector4>& RadianceField) noexcept;

    // Single unified conversion operator for pixel count
    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    [[nodiscard]] float     SampleJacobianShift(const Vector3& OldOrigin, const Vector3& NewOrigin, const Vector3& HitPos, const Vector3& HitNorm) const noexcept;

    uint32_t                Width;                              // [px] viewport width
    uint32_t                Height;                             // [px] viewport height
    std::vector<IndirectPathReservoir> Reservoirs;              // [reservoirs] indirect path reservoirs
};

template<>
inline size_t GlobalIlluminationIntegrator::Convert<size_t>() const noexcept
{
    return Reservoirs.size();
}

} // namespace Frontier
