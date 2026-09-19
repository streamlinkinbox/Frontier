//============================================================================================================================================
// 📦 Frontier/PhotometricIllumination/DirectIlluminationIntegrator.h — ReSTIR Spatiotemporal Direct Illumination Integrator
//============================================================================================================================================

#pragma once

#include "ClusteredSpace.h"
#include "../GeometricRaster/MaterialCodec.h"
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                              ILLUMINATION RESERVOIR
//------------------------------------------------------------------------------------------------------------------------

struct IlluminationReservoir
{
    uint32_t                SelectedLightIndex;                 // [index] winning candidate light index
    float                   WeightSum;                          // [-] accumulated candidate weights w_sum
    uint32_t                SampleCount;                        // [count] number of considered candidates M
    float                   UnbiasedWeight;                     // [-] normalization weight W

    void ResampleCandidate(uint32_t CandidateIndex, float CandidateWeight, float RandomScalar) noexcept
    {
        WeightSum += CandidateWeight;
        SampleCount += 1;
        if (RandomScalar * WeightSum <= CandidateWeight)
        {
            SelectedLightIndex = CandidateIndex;
        }
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                      DIRECT ILLUMINATION INTEGRATOR
//------------------------------------------------------------------------------------------------------------------------

class DirectIlluminationIntegrator
{
public:
    DirectIlluminationIntegrator(uint32_t ExtentWidth, uint32_t ExtentHeight) noexcept;
    ~DirectIlluminationIntegrator() noexcept = default;

    DirectIlluminationIntegrator(const DirectIlluminationIntegrator&) = delete;
    DirectIlluminationIntegrator& operator=(const DirectIlluminationIntegrator&) = delete;

    void                    IntegrateDirectIllumination(
                                const VisibilityProjection& Visibility,
                                const GeometryStructure& Geometry,
                                const MaterialCodec& Codec,
                                const std::vector<PhotometricLightRecord>& Lights,
                                std::vector<Vector4>& OutRadianceField) noexcept;

    // Single unified conversion operator for pixel count
    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    uint32_t                Width;                              // [px] viewport width
    uint32_t                Height;                             // [px] viewport height
    std::vector<IlluminationReservoir> Reservoirs;              // [reservoirs] per-pixel ReSTIR reservoirs
};

template<>
inline size_t DirectIlluminationIntegrator::Convert<size_t>() const noexcept
{
    return Reservoirs.size();
}

} // namespace Frontier
