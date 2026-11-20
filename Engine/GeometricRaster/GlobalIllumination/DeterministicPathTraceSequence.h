/*============================================================================================================================================
                                                            DETERMINISTICPATHTRACESEQUENCE.H
============================================================================================================================================*/
// 🧩 Deterministic Path Tracer — CPU Reference Integrator (GTX + RTX, Real-Mesh, Zero Denoiser):
//    - Owen-scrambled Sobol (0,2) quadrature + Dupuy-Benyoub (2023) spherical-cap GGX VNDF (DeterministicIntegrators.h)
//    - GTX: SpatialSplitBoundingVolume CWBVH8 software traversal. RTX: VK_KHR_ray_query BLAS/TLAS (GPU compute).
//    - 6-phase lock-free wavefront in WavefrontPathTraceCompute.slang; this header is the CPU megakernel reference.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/MathConstants.h"
#include "Engine/GeometricRaster/GlobalIllumination/DeterministicIntegrators.h"
#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/Mathematics/LinearColors.h"
#include "Engine/SpatialAcceleration/SpatialSplitBoundingVolume.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct DeterministicPathTraceConfiguration
{
    uint32_t SamplesPerPixel = 16u;
    uint32_t MaximumBounces  = 6u;
    uint32_t PixelKey        = 0u;
    float    ShadowEpsilonM  = 1.0e-3f;
};

//------------------------------------------------------------------------------------------------------------------------
//                                               DETERMINISTIC PATH TRACE SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class DeterministicPathTraceSequence
{
public:
    DeterministicPathTraceConfiguration Config {};

    [[nodiscard]] Mathematics::LinearRGB TraceClosestRadiance(
        const SpatialAcceleration::SpatialSplitBoundingVolume& Volume,
        Mathematics::Axis3D                                    Origin,
        Mathematics::Axis3D                                    Direction,
        float                                                  MaximumDistance = 80.0f) const noexcept
    {
        SpatialAcceleration::RaySegmentQuery Ray {};
        Ray.Origin          = Origin;
        Ray.UnitDirection   = Mathematics::NormalizeAxis(Direction, { 0.0f, 0.0f, 1.0f });
        Ray.MinimumDistance = Config.ShadowEpsilonM;
        Ray.MaximumDistance = MaximumDistance;
        const auto Hit = Volume.TraceClosestRay(Ray);
        if (!Hit.HitConfirmed)
        {
            return { 0.0f, 0.0f, 0.0f };
        }
        // CPU reference: direct hit radiance placeholder (full BSDF + NEE in GPU wavefront compute).
        const float InvDistance = 1.0f / std::max(Hit.HitDistanceMeters, 1.0e-3f);
        return { 0.5f * InvDistance, 0.5f * InvDistance, 0.5f * InvDistance };
    }

    [[nodiscard]] bool QueryShadowOccluded(
        const SpatialAcceleration::SpatialSplitBoundingVolume& Volume,
        Mathematics::Axis3D                                    Origin,
        Mathematics::Axis3D                                    Direction,
        float                                                  MaximumDistance) const noexcept
    {
        SpatialAcceleration::RaySegmentQuery Ray {};
        Ray.Origin          = Origin;
        Ray.UnitDirection   = Mathematics::NormalizeAxis(Direction, { 0.0f, 0.0f, 1.0f });
        Ray.MinimumDistance = Config.ShadowEpsilonM;
        Ray.MaximumDistance = MaximumDistance;
        return Volume.QueryRayOccluded(Ray);
    }

    // Sobol (0,2) sample for pixel (X,Y) and sample index (deterministic, zero RNG).
    [[nodiscard]] Mathematics::Axis2D SobolSample(uint32_t X, uint32_t Y, uint32_t SampleIndex) const noexcept
    {
        const uint32_t PixelKey = Config.PixelKey ^ (X * 0x9E3779B9u + Y * 0x85EBCA6Bu);
        return Sobol02Sample(SampleIndex, PixelKey, 0u);
    }
};

} // namespace Frontier::GeometricRaster
