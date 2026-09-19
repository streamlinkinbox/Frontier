//============================================================================================================================================
// 📦 Frontier/PhotometricIllumination/AtmosphereIntegrator.cpp — Henyey-Greenstein Atmosphere Implementation
//============================================================================================================================================

#include "AtmosphereIntegrator.h"
#include <cmath>
#include <algorithm>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                PHASE FUNCTION
//------------------------------------------------------------------------------------------------------------------------

float AtmosphereIntegrator::SamplePhaseHenyeyGreenstein(float CosTheta, float g) noexcept
{
    float g2 = g * g;
    float denom = 1.0f + g2 - 2.0f * g * CosTheta;
    denom = std::max(denom, 1e-4f);
    return (1.0f - g2) / (4.0f * 3.14159265f * std::pow(denom, 1.5f));
}

//------------------------------------------------------------------------------------------------------------------------
//                                              RAYMARCHING PIPELINE
//------------------------------------------------------------------------------------------------------------------------

Vector4 AtmosphereIntegrator::RaymarchMedia(
    const AtmosphereIntegrationCriteria& Criteria,
    const FluidSolver& Fluid,
    const Vector3& SunDirection,
    const Vector3& SunRadiance) const noexcept
{
    if (Criteria.StepCount == 0 || Criteria.RayMarchDistance <= 0.0f)
    {
        return Vector4{ 0.0f, 0.0f, 0.0f, 1.0f };
    }

    float StepLength = Criteria.RayMarchDistance / static_cast<float>(Criteria.StepCount);
    float Transmittance = 1.0f;
    Vector3 InScatteredRadiance{ 0.0f, 0.0f, 0.0f };

    float CosTheta = OrientationClassifier::DotProduct(Criteria.RayDirection, SunDirection);
    float Phase = SamplePhaseHenyeyGreenstein(CosTheta, Criteria.AsymmetryFactorG);

    for (uint32_t step = 0; step < Criteria.StepCount; ++step)
    {
        float t = (static_cast<float>(step) + 0.5f) * StepLength;
        Vector3 SamplePos = Criteria.RayOrigin + Criteria.RayDirection * t;

        int32_t gx = static_cast<int32_t>(SamplePos.x);
        int32_t gy = static_cast<int32_t>(SamplePos.y);
        int32_t gz = static_cast<int32_t>(SamplePos.z);

        float LocalDensity = 0.0f;
        float LocalTemp = 300.0f;

        if (gx >= 0 && gy >= 0 && gz >= 0)
        {
            LocalDensity = Fluid.QueryDensity(static_cast<uint32_t>(gx), static_cast<uint32_t>(gy), static_cast<uint32_t>(gz));
            LocalTemp = Fluid.QueryTemperature(static_cast<uint32_t>(gx), static_cast<uint32_t>(gy), static_cast<uint32_t>(gz));
        }

        if (LocalDensity > 1e-4f)
        {
            float Extinction = (Criteria.AbsorptionCrossSection + Criteria.ScatteringCrossSection) * LocalDensity;
            float StepTransmittance = std::exp(-Extinction * StepLength);

            Vector3 StepScattering = SunRadiance * (Criteria.ScatteringCrossSection * LocalDensity * Phase);

            if (LocalTemp > 600.0f)
            {
                float Blackbody = (LocalTemp - 600.0f) * 0.005f;
                StepScattering += Vector3{ Blackbody * 1.5f, Blackbody * 0.8f, Blackbody * 0.2f };
            }

            InScatteredRadiance += StepScattering * (Transmittance * (1.0f - StepTransmittance));
            Transmittance *= StepTransmittance;

            if (Transmittance < 0.001f)
            {
                break;
            }
        }
    }

    return Vector4{ InScatteredRadiance.x, InScatteredRadiance.y, InScatteredRadiance.z, Transmittance };
}

} // namespace Frontier
