/*============================================================================================================================================
                                                           ANALYTICORIGINS.H
============================================================================================================================================*/
// 🧩 Deterministic C++20 release-origin geometry sampling for Point, SphereVolume, Disc, BoxVolume, SurfaceLattice, and DualElectrode

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Origins/ReleaseOriginTypes.h"

#include <cstdint>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct OriginSamplePoint
{
    Mathematics::Axis3D PositionMeters { 0.0f, 0.0f, 0.0f }; // [m] - Sampled world coordinate (+Z Up)
    Mathematics::Axis3D NormalAxis     { 0.0f, 0.0f, 1.0f }; // [-] - Outward surface/axis direction
    float               CoordinateU    = 0.0f;               // [0-1] - Normalized parameter U
    float               CoordinateV    = 0.0f;               // [0-1] - Normalized parameter V
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] float HashOriginScalar(uint32_t SampleIndex, uint32_t Salt) noexcept;

[[nodiscard]] OriginSamplePoint SampleAnalyticOrigin(
    const ReleaseOriginSpec& Origin,
    uint32_t                 SampleIndex,
    uint32_t                 TotalCount,
    uint32_t                 Seed) noexcept;

} // namespace Frontier::ParticleSpace
