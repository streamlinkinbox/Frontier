//============================================================================================================================================
// 📦 Frontier/PhotometricIllumination/AtmosphereIntegrator.h — Henyey-Greenstein Heterogeneous Media Volumetric Raymarcher
//============================================================================================================================================

#pragma once

#include "../VolumetricDynamics/FluidSolver.h"
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                           ATMOSPHERE INTEGRATION CRITERIA
//------------------------------------------------------------------------------------------------------------------------

struct AtmosphereIntegrationCriteria
{
    Vector3                 RayOrigin;                          // [m] camera origin
    Vector3                 RayDirection;                       // [-] unit view ray direction
    float                   RayMarchDistance;                   // [m] maximum march distance
    uint32_t                StepCount;                          // [steps] numerical integration step count
    float                   AsymmetryFactorG;                   // [-1..1] Henyey-Greenstein g parameter
    float                   AbsorptionCrossSection;             // [1/m] medium absorption sigma_a
    float                   ScatteringCrossSection;             // [1/m] medium scattering sigma_s
};

//------------------------------------------------------------------------------------------------------------------------
//                                                ATMOSPHERE INTEGRATOR
//------------------------------------------------------------------------------------------------------------------------

class AtmosphereIntegrator
{
public:
    AtmosphereIntegrator() noexcept = default;
    ~AtmosphereIntegrator() noexcept = default;

    [[nodiscard]] static float SamplePhaseHenyeyGreenstein(float CosTheta, float g) noexcept;

    [[nodiscard]] Vector4   RaymarchMedia(
                                const AtmosphereIntegrationCriteria& Criteria,
                                const FluidSolver& Fluid,
                                const Vector3& SunDirection,
                                const Vector3& SunRadiance) const noexcept;

    // Single unified conversion operator
    template<typename TargetType>
    [[nodiscard]] TargetType Convert(const Vector4& ScatteringResult) const noexcept;
};

template<>
inline Vector3 AtmosphereIntegrator::Convert<Vector3>(const Vector4& ScatteringResult) const noexcept
{
    return Vector3{ ScatteringResult.x, ScatteringResult.y, ScatteringResult.z };
}

} // namespace Frontier
