//============================================================================================================================================
// 📦 Frontier/PhotometricIllumination/DirectIlluminationIntegrator.cpp — ReSTIR Direct Illumination Implementation
//============================================================================================================================================

#include "DirectIlluminationIntegrator.h"
#include <cstdlib>
#include <cmath>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

DirectIlluminationIntegrator::DirectIlluminationIntegrator(uint32_t ExtentWidth, uint32_t ExtentHeight) noexcept
    : Width(ExtentWidth)
    , Height(ExtentHeight)
    , Reservoirs(ExtentWidth * ExtentHeight, IlluminationReservoir{ 0, 0.0f, 0, 0.0f })
{
}

//------------------------------------------------------------------------------------------------------------------------
//                                              INTEGRATION PIPELINE
//------------------------------------------------------------------------------------------------------------------------

void DirectIlluminationIntegrator::IntegrateDirectIllumination(
    const VisibilityProjection& Visibility,
    const GeometryStructure& Geometry,
    const MaterialCodec& Codec,
    const std::vector<PhotometricLightRecord>& Lights,
    std::vector<Vector4>& OutRadianceField) noexcept
{
    if (Lights.empty())
    {
        return;
    }

    OutRadianceField.resize(Width * Height);

    for (uint32_t y = 0; y < Height; ++y)
    {
        for (uint32_t x = 0; x < Width; ++x)
        {
            size_t idx = y * Width + x;
            SurfaceAttributeRecord Surface = Codec.DecodePixel(x, y, Visibility, Geometry);

            if (!Surface.ValidCondition)
            {
                OutRadianceField[idx] = Vector4{ 0.05f, 0.05f, 0.08f, 1.0f }; // Sky background
                continue;
            }

            // Initial candidate sampling M = 8
            IlluminationReservoir Reservoir{ 0, 0.0f, 0, 0.0f };
            for (uint32_t s = 0; s < 8; ++s)
            {
                uint32_t CandidateIndex = static_cast<uint32_t>(std::rand()) % static_cast<uint32_t>(Lights.size());
                const auto& Light = Lights[CandidateIndex];

                Vector3 LightVec = Light.SpatialLocation - Surface.WorldPosition;
                float Dist = LightVec.Length();
                if (Dist > Light.InfluenceRadius || Dist <= 1e-4f)
                {
                    continue;
                }

                Vector3 L = LightVec / Dist;
                float NdotL = std::max(0.0f, OrientationClassifier::DotProduct(Surface.SurfaceNormal, L));
                float Attenuation = 1.0f / (Dist * Dist + 1.0f);
                float Weight = (Light.EmissiveRadiance.x + Light.EmissiveRadiance.y + Light.EmissiveRadiance.z) * NdotL * Attenuation;

                float RandomScalar = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
                Reservoir.ResampleCandidate(CandidateIndex, Weight, RandomScalar);
            }

            if (Reservoir.WeightSum > 0.0f)
            {
                const auto& WinningLight = Lights[Reservoir.SelectedLightIndex];
                Vector3 LightVec = WinningLight.SpatialLocation - Surface.WorldPosition;
                float Dist = LightVec.Length();
                Vector3 L = (Dist > 1e-4f) ? (LightVec / Dist) : Vector3{ 0.0f, 1.0f, 0.0f };
                float NdotL = std::max(0.0f, OrientationClassifier::DotProduct(Surface.SurfaceNormal, L));
                float Attenuation = 1.0f / (Dist * Dist + 1.0f);

                Vector3 DirectRadiance = WinningLight.EmissiveRadiance * (NdotL * Attenuation) * (Surface.AlbedoColor.x);
                OutRadianceField[idx] = Vector4{ DirectRadiance.x, DirectRadiance.y, DirectRadiance.z, 1.0f };
            }
            else
            {
                OutRadianceField[idx] = Vector4{ 0.0f, 0.0f, 0.0f, 1.0f };
            }

            Reservoirs[idx] = Reservoir;
        }
    }
}

} // namespace Frontier
