//============================================================================================================================================
// 📦 Project-Zero/Source/RendererHost.h — ReSTIR DI & GI Photometric Frame Execution and PPM Image Exporter
//============================================================================================================================================

#pragma once

#include "TracingIndex.h"
#include "RayTracingSolver.h"
#include "SkyFogIntegrator.h"
#include "../../../Engine/GeometricRaster/CameraProjection.h"
#include "../../../Engine/DeviceExchange/OrientationClassifier.h"
#include "../../../Engine/DisplayPresentation/CloudShadowStaging.h"
#include <cstdint>
#include <vector>
#include <string>

namespace Frontier::ProjectZero {

//------------------------------------------------------------------------------------------------------------------------
//                                              PHOTOMETRIC RESERVOIR
//------------------------------------------------------------------------------------------------------------------------

struct PhotometricReservoir
{
    Vector3                 SampledLightPoint;                  // [m] position on luminaire surface
    Vector3                 SampledRadiance;                    // [lux] candidate radiant flux
    float                   WeightSum;                          // [-] accumulated candidate weights
    uint32_t                SampleCount;                        // [count] considered candidate count M
    float                   UnbiasedWeight;                     // [-] normalization weight W

    void ResampleCandidate(const Vector3& LightPoint, const Vector3& Radiance, float Weight, float RandomScalar) noexcept
    {
        WeightSum += Weight;
        SampleCount += 1;
        if (RandomScalar * WeightSum <= Weight)
        {
            SampledLightPoint = LightPoint;
            SampledRadiance   = Radiance;
        }
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                              INDIRECT GI RESERVOIR
//------------------------------------------------------------------------------------------------------------------------

struct IndirectGIReservoir
{
    Vector3                 BounceHitPosition;                  // [m] secondary bounce contact position
    Vector3                 BounceHitNormal;                    // [-] secondary bounce contact normal
    Vector3                 IndirectRadiance;                   // [lux] secondary incoming radiance
    float                   WeightSum;                          // [-] accumulated candidate weights
    uint32_t                SampleCount;                        // [count] considered indirect paths M
    float                   UnbiasedWeight;                     // [-] normalization weight W
    float                   KeptWeight;                         // [-] target weight of the kept sample

    void ResampleIndirect(const Vector3& HitPos, const Vector3& HitNorm, const Vector3& Radiance, float Weight, float RandomScalar, uint32_t MergedCount = 1u) noexcept
    {
        WeightSum += Weight;
        SampleCount += MergedCount;
        if (RandomScalar * WeightSum <= Weight)
        {
            BounceHitPosition = HitPos;
            BounceHitNormal   = HitNorm;
            IndirectRadiance  = Radiance;
            KeptWeight        = Weight;
        }
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    RENDERER HOST
//------------------------------------------------------------------------------------------------------------------------

class RendererHost
{
public:
    RendererHost(uint32_t ViewportWidth, uint32_t ViewportHeight) noexcept;
    ~RendererHost() noexcept = default;

    RendererHost(const RendererHost&) = delete;
    RendererHost& operator=(const RendererHost&) = delete;

    void                    RenderShowcaseFrame(const Frontier::CameraProjection& ActiveCamera, double SunHour, SkyFogIntegrator::FogScenario Scenario, uint32_t SpatialPassCount = 2, uint32_t BounceCount = 8, bool FlareEnabled = true, float FlareVariety = 0.0f, bool CloudShadows = true, float CloudTime = Frontier::kCloudShadowShowcaseDiorama.TimeSeconds, uint32_t CloudType = Frontier::kCloudShadowShowcaseDiorama.Type, float CloudCoverage = Frontier::kCloudShadowShowcaseDiorama.Coverage, float CloudScale = Frontier::kCloudShadowShowcaseDiorama.Scale, float CloudBase = Frontier::kCloudShadowShowcaseDiorama.Base, float CloudThickness = Frontier::kCloudShadowShowcaseDiorama.Thickness, float CloudDensity = Frontier::kCloudShadowShowcaseDiorama.Density) noexcept;
    void                    AssignToneExposure(float LinearExposure) noexcept { ToneExposure = LinearExposure; }
    [[nodiscard]] bool      ExportPpmImage(const std::string& OutputPath) const noexcept;
    [[nodiscard]] bool      ExportShowcaseImage(const std::string& OutputPath) const noexcept;
    void                    ExportLdrPixels(std::vector<uint8_t>& OutRgba) const noexcept;

    [[nodiscard]] uint32_t  QueryWidth() const noexcept { return Width; }
    [[nodiscard]] uint32_t  QueryHeight() const noexcept { return Height; }
    [[nodiscard]] const std::vector<Vector3>& QueryAccumulatedBuffer() const noexcept { return AccumulatedBuffer; }

    // Single unified conversion operator for pixel count
    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    [[nodiscard]] RayStructure GeneratePrimaryRay(const Frontier::CameraProjection& ActiveCamera, uint32_t PixelX, uint32_t PixelY, float JitterX, float JitterY) const noexcept;
    [[nodiscard]] Vector3   SampleCosineHemisphere(const Vector3& Normal, float u1, float u2) const noexcept;
    [[nodiscard]] float     EvaluateJacobian(const Vector3& x1, const Vector3& x2, const Vector3& y, const Vector3& n_y) const noexcept;
    void                    ApplyLensFlare(const Frontier::CameraProjection& ActiveCamera, const Vector3& SunWorld,
                                           float SunElevationDeg, const Vector3& SunFlareTint, float FlareVariety) noexcept;

    uint32_t                Width;                              // [px] viewport width
    uint32_t                Height;                             // [px] viewport height
    float                   ToneExposure = 1.05f;               // [-] linear pre-tonemap multiplier
    RayTracingSolver        Scene;                              // [scene] analytical showcase geometry
    SkyFogIntegrator        SkyFog;                             // [atmosphere] celestial sky and fog volumes
    std::vector<HitIntersection>      PrimaryHits;              // [visibility] primary visibility buffer
    std::vector<PhotometricReservoir> DirectReservoirs;         // [restir_di] direct illumination reservoirs
    std::vector<IndirectGIReservoir>  IndirectReservoirs;       // [restir_gi] global illumination reservoirs
    std::vector<Vector3>              AccumulatedBuffer;        // [radiance] HDR color radiance buffer
};

template<>
inline size_t RendererHost::Convert<size_t>() const noexcept
{
    return static_cast<size_t>(Width * Height);
}

template<>
inline uint32_t RendererHost::Convert<uint32_t>() const noexcept
{
    return Width * Height;
}

} // namespace Frontier::ProjectZero
