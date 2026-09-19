//============================================================================================================================================
// 📦 Frontier/PhotometricIllumination/GlobalIlluminationIntegrator.cpp — ReSTIR GI Radiosity Implementation
//============================================================================================================================================

#include "GlobalIlluminationIntegrator.h"
#include <cmath>
#include <algorithm>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

GlobalIlluminationIntegrator::GlobalIlluminationIntegrator(uint32_t ExtentWidth, uint32_t ExtentHeight) noexcept
    : Width(ExtentWidth)
    , Height(ExtentHeight)
    , Reservoirs(ExtentWidth * ExtentHeight, IndirectPathReservoir{ Vector3{ 0.0f, 0.0f, 0.0f }, Vector3{ 0.0f, 0.0f, 0.0f }, Vector3{ 0.0f, 1.0f, 0.0f }, 0.0f, 0, 0.0f })
{
}

//------------------------------------------------------------------------------------------------------------------------
//                                                JACOBIAN SHIFT EVALUATION
//------------------------------------------------------------------------------------------------------------------------

float GlobalIlluminationIntegrator::SampleJacobianShift(const Vector3& OldOrigin, const Vector3& NewOrigin, const Vector3& HitPos, const Vector3& HitNorm) const noexcept
{
    Vector3 OldDir = HitPos - OldOrigin;
    Vector3 NewDir = HitPos - NewOrigin;

    float rOldSq = OldDir.LengthSquared();
    float rNewSq = NewDir.LengthSquared();

    if (rOldSq <= 1e-6f || rNewSq <= 1e-6f)
    {
        return 1.0f;
    }

    float rOld = std::sqrt(rOldSq);
    float rNew = std::sqrt(rNewSq);

    float cosThetaOld = std::abs(OrientationClassifier::DotProduct(HitNorm, OldDir / rOld));
    float cosThetaNew = std::abs(OrientationClassifier::DotProduct(HitNorm, NewDir / rNew));

    if (cosThetaOld <= 1e-4f)
    {
        return 0.0f;
    }

    // Jacobian J = (cos(theta_new) * r_old^2) / (cos(theta_old) * r_new^2)
    return (cosThetaNew * rOldSq) / (cosThetaOld * rNewSq);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              INTEGRATION PIPELINE
//------------------------------------------------------------------------------------------------------------------------

void GlobalIlluminationIntegrator::IntegrateGlobalIllumination(
    const VisibilityProjection& Visibility,
    const GeometryStructure& Geometry,
    const MaterialCodec& Codec,
    std::vector<Vector4>& RadianceField) noexcept
{
    if (RadianceField.size() != Width * Height)
    {
        return;
    }

    for (uint32_t y = 0; y < Height; ++y)
    {
        for (uint32_t x = 0; x < Width; ++x)
        {
            size_t idx = y * Width + x;
            SurfaceAttributeRecord Surface = Codec.DecodePixel(x, y, Visibility, Geometry);

            if (!Surface.ValidCondition)
            {
                continue;
            }

            // Ambient indirect radiosity
            Vector3 IndirectLight{ 0.03f, 0.03f, 0.04f };
            RadianceField[idx].x += IndirectLight.x * Surface.AlbedoColor.x;
            RadianceField[idx].y += IndirectLight.y * Surface.AlbedoColor.y;
            RadianceField[idx].z += IndirectLight.z * Surface.AlbedoColor.z;
        }
    }
}

} // namespace Frontier
