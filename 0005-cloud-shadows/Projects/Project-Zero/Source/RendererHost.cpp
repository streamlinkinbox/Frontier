//============================================================================================================================================
// 📦 Project-Zero/Source/RendererHost.cpp — ReSTIR DI & GI Frame Execution and Image Export Implementation
//============================================================================================================================================

#include "RendererHost.h"
#include <fstream>
#include <cmath>
#include <algorithm>
#include <random>
#include <iostream>
#include "../Shaders/SlangInterchange.h"
#include "../Shaders/FlareSpecification.slang"
// The flare core is compiled ONLY in this translation unit (its routines
// are non-inline, like the sky/fog core in SkyFogIntegrator.cpp). Undefine
// the prelude keyword macros so nothing below can be macro-rewritten.
#undef in
#undef uniform

namespace Frontier::ProjectZero {

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

RendererHost::RendererHost(uint32_t ViewportWidth, uint32_t ViewportHeight) noexcept
    : Width(ViewportWidth)
    , Height(ViewportHeight)
    , Scene{}
    , PrimaryHits(ViewportWidth * ViewportHeight)
    , DirectReservoirs(ViewportWidth * ViewportHeight)
    , IndirectReservoirs(ViewportWidth * ViewportHeight)
    , AccumulatedBuffer(ViewportWidth * ViewportHeight, Vector3{ 0.0f, 0.0f, 0.0f })
{
}

//------------------------------------------------------------------------------------------------------------------------
//                                                RAY GENERATION & SAMPLING
//------------------------------------------------------------------------------------------------------------------------

RayStructure RendererHost::GeneratePrimaryRay(const Frontier::CameraProjection& ActiveCamera, uint32_t PixelX, uint32_t PixelY, float JitterX, float JitterY) const noexcept
{
    float u = (static_cast<float>(PixelX) + JitterX) / static_cast<float>(Width);
    float v = (static_cast<float>(PixelY) + JitterY) / static_cast<float>(Height);
    Frontier::ViewRay Ray = ActiveCamera.ConstructRay(u, v);
    return RayStructure{ Ray.OriginLocation, Ray.UnitDirection, Ray.NearClippingDistance, Ray.FarClippingDistance };
}

Vector3 RendererHost::SampleCosineHemisphere(const Vector3& Normal, float u1, float u2) const noexcept
{
    float r = std::sqrt(u1);
    float theta = 2.0f * 3.14159265359f * u2;

    float x = r * std::cos(theta);
    float y = r * std::sin(theta);
    float z = std::sqrt(std::max(0.0f, 1.0f - u1));

    Vector3 Up = (std::abs(Normal.y) < 0.999f) ? Vector3{ 0.0f, 1.0f, 0.0f } : Vector3{ 1.0f, 0.0f, 0.0f };
    Vector3 Tangent = OrientationClassifier::CrossProduct(Up, Normal).Normalized();
    Vector3 Bitangent = OrientationClassifier::CrossProduct(Normal, Tangent);

    return (Tangent * x + Bitangent * y + Normal * z).Normalized();
}

float RendererHost::EvaluateJacobian(const Vector3& x1, const Vector3& x2, const Vector3& y, const Vector3& n_y) const noexcept
{
    Vector3 d1 = y - x1;
    Vector3 d2 = y - x2;
    float lenSq1 = d1.LengthSquared();
    float lenSq2 = d2.LengthSquared();

    if (lenSq1 <= 1e-6f || lenSq2 <= 1e-6f)
    {
        return 1.0f;
    }

    float len1 = std::sqrt(lenSq1);
    float len2 = std::sqrt(lenSq2);

    float cos1 = std::abs(OrientationClassifier::DotProduct(n_y, d1 / len1));
    float cos2 = std::abs(OrientationClassifier::DotProduct(n_y, d2 / len2));

    if (cos1 <= 1e-4f)
    {
        return 0.0f;
    }

    return std::min((cos2 * lenSq1) / (cos1 * lenSq2), 10.0f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                           RESTIR DI & GI RENDER PIPELINE
//------------------------------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------------------------------
//                                     SHOWCASE SUN SKY FOG RENDER PIPELINE
//------------------------------------------------------------------------------------------------------------------------

void RendererHost::RenderShowcaseFrame(const Frontier::CameraProjection& ActiveCamera, double SunHour, SkyFogIntegrator::FogScenario Scenario, uint32_t SpatialPassCount, uint32_t BounceCount, bool FlareEnabled, float FlareVariety, bool CloudShadows, float CloudTime, uint32_t CloudType, float CloudCoverage, float CloudScale, float CloudBase, float CloudThickness, float CloudDensity) noexcept
{
    std::mt19937 Rng(1337);
    std::uniform_real_distribution<float> Dist(0.0f, 1.0f);

    Scene.ConstructShowcaseScene();
    SkyFog.AssignSunHour(SunHour);
    SkyFog.AssignFogScenario(Scenario);
    SkyFog.AssignCloudShadow(CloudShadows, CloudTime, CloudType, CloudCoverage, CloudScale, CloudBase, CloudThickness, CloudDensity);

    const auto& Materials = Scene.QueryMaterials();
    const Vector3 SunWorld = SkyFog.QuerySunDirectionWorld();
    const Vector3 SunRadiance = SkyFog.QuerySunRadiance();
    const Vector3 MoonWorld = SkyFog.QueryMoonDirectionWorld();
    const Vector3 MoonRadiance = SkyFog.QueryMoonRadiance();
    const Vector3 FogLightWorld = SkyFogIntegrator::WorldFromRender(SkyFog.QueryFogLightDirection());
    const Vector3 SkyAmbient = SkyFog.QueryAmbientRadiance();
    const float FogFar = SkyFog.QueryFogFarDistance();

    // Phase 1: Visibility Buffer Primary Ray Trace from Active Camera
    for (uint32_t y = 0; y < Height; ++y)
    {
        for (uint32_t x = 0; x < Width; ++x)
        {
            size_t idx = y * Width + x;
            RayStructure PrimaryRay = GeneratePrimaryRay(ActiveCamera, x, y, 0.5f, 0.5f);
            PrimaryHits[idx] = Scene.EvaluateIntersection(PrimaryRay);
        }
    }

    // Phase 2: Direct Sun Illumination with Shadow Segment Test
    std::vector<Vector3> DirectLight(Width * Height, Vector3{ 0.0f, 0.0f, 0.0f });
    for (uint32_t y = 0; y < Height; ++y)
    {
        for (uint32_t x = 0; x < Width; ++x)
        {
            size_t idx = y * Width + x;
            const auto& Hit = PrimaryHits[idx];

            if (!Hit.ValidCondition)
            {
                continue;
            }

            const auto& Mat = Materials[Hit.MaterialIndex];
            float SunCos = std::max(0.0f, OrientationClassifier::DotProduct(Hit.SurfaceNormal, SunWorld));
            float SunVisible = Scene.EvaluateOcclusion(Hit.HitLocation + Hit.SurfaceNormal * 0.001f, Hit.HitLocation + SunWorld * 1000.0f) ? 0.0f : 1.0f;
            float MoonCos = std::max(0.0f, OrientationClassifier::DotProduct(Hit.SurfaceNormal, MoonWorld));
            float MoonVisible = Scene.EvaluateOcclusion(Hit.HitLocation + Hit.SurfaceNormal * 0.001f, Hit.HitLocation + MoonWorld * 1000.0f) ? 0.0f : 1.0f;
            float CloudShade = SkyFog.QueryCloudShadow(Hit.HitLocation, SunWorld);
            DirectLight[idx] = Mat.AlbedoColor * (SunRadiance * (SunCos * SunVisible * CloudShade) + MoonRadiance * (MoonCos * MoonVisible));
        }
    }

    // Phase 3: ReSTIR GI Initial Candidate Bounce Ray Tracing (8 samples)
    for (uint32_t y = 0; y < Height; ++y)
    {
        for (uint32_t x = 0; x < Width; ++x)
        {
            size_t idx = y * Width + x;
            const auto& Hit = PrimaryHits[idx];
            IndirectGIReservoir GIReservoir{ Vector3{ 0.0f, 0.0f, 0.0f }, Vector3{ 0.0f, 0.0f, 1.0f }, Vector3{ 0.0f, 0.0f, 0.0f }, 0.0f, 0, 0.0f, 0.0f };

            if (!Hit.ValidCondition)
            {
                IndirectReservoirs[idx] = GIReservoir;
                continue;
            }

            for (uint32_t s = 0; s < BounceCount; ++s)
            {
                Vector3 BounceDir = SampleCosineHemisphere(Hit.SurfaceNormal, Dist(Rng), Dist(Rng));
                RayStructure BounceRay{ Hit.HitLocation + Hit.SurfaceNormal * 0.001f, BounceDir, 0.001f, 200.0f };

                HitIntersection BounceHit = Scene.EvaluateIntersection(BounceRay);
                Vector3 BounceRadiance{ 0.0f, 0.0f, 0.0f };
                Vector3 BouncePos = BounceRay.SpatialOrigin;
                Vector3 BounceNormal = BounceDir;
                if (BounceHit.ValidCondition)
                {
                    const auto& BounceMat = Materials[BounceHit.MaterialIndex];
                    float BounceSunCos = std::max(0.0f, OrientationClassifier::DotProduct(BounceHit.SurfaceNormal, SunWorld));
                    float BounceSunVis = Scene.EvaluateOcclusion(BounceHit.HitLocation + BounceHit.SurfaceNormal * 0.001f, BounceHit.HitLocation + SunWorld * 1000.0f) ? 0.0f : 1.0f;
                    float BounceMoonCos = std::max(0.0f, OrientationClassifier::DotProduct(BounceHit.SurfaceNormal, MoonWorld));
                    float BounceMoonVis = Scene.EvaluateOcclusion(BounceHit.HitLocation + BounceHit.SurfaceNormal * 0.001f, BounceHit.HitLocation + MoonWorld * 1000.0f) ? 0.0f : 1.0f;
                    float BounceCloudShade = SkyFog.QueryCloudShadow(BounceHit.HitLocation, SunWorld);
                    BounceRadiance = BounceMat.AlbedoColor * (SunRadiance * (BounceSunCos * BounceSunVis * BounceCloudShade) + MoonRadiance * (BounceMoonCos * BounceMoonVis) + SkyAmbient);
                    BouncePos = BounceHit.HitLocation;
                    BounceNormal = BounceHit.SurfaceNormal;
                }
                else
                {
                    // Open sky above the bounce point: bare diffuse sky
                    // ambient (the primary albedo applies at composition).
                    BounceRadiance = SkyAmbient;
                }

                float Weight = (BounceRadiance.x + BounceRadiance.y + BounceRadiance.z) * 0.3333f;
                GIReservoir.ResampleIndirect(BouncePos, BounceNormal, BounceRadiance, Weight, Dist(Rng));
            }

            if (GIReservoir.WeightSum > 0.0f && GIReservoir.SampleCount > 0)
            {
                GIReservoir.UnbiasedWeight = GIReservoir.WeightSum / static_cast<float>(GIReservoir.SampleCount);
            }

            IndirectReservoirs[idx] = GIReservoir;
        }
    }

    // Phase 4: ReSTIR GI Spatial Resampling Pass with Jacobian Shift
    std::vector<IndirectGIReservoir> SpatialIndirectReservoirs = IndirectReservoirs;
    for (uint32_t pass = 0; pass < SpatialPassCount; ++pass)
    {
        for (uint32_t y = 0; y < Height; ++y)
        {
            for (uint32_t x = 0; x < Width; ++x)
            {
                size_t idx = y * Width + x;
                const auto& CenterHit = PrimaryHits[idx];
                if (!CenterHit.ValidCondition)
                {
                    continue;
                }

                IndirectGIReservoir SpatialGI = IndirectReservoirs[idx];

                for (uint32_t n = 0; n < 8; ++n)
                {
                    int nx = static_cast<int>(x) + (static_cast<int>(Rng() % 11) - 5);
                    int ny = static_cast<int>(y) + (static_cast<int>(Rng() % 11) - 5);

                    if (nx < 0 || nx >= static_cast<int>(Width) || ny < 0 || ny >= static_cast<int>(Height))
                    {
                        continue;
                    }

                    size_t nIdx = static_cast<size_t>(ny) * Width + static_cast<size_t>(nx);
                    const auto& NeighborHit = PrimaryHits[nIdx];

                    if (!NeighborHit.ValidCondition)
                    {
                        continue;
                    }
                    if (OrientationClassifier::DotProduct(CenterHit.SurfaceNormal, NeighborHit.SurfaceNormal) < 0.90f)
                    {
                        continue;
                    }
                    if (std::abs(CenterHit.RayDistance - NeighborHit.RayDistance) > 1.00f)
                    {
                        continue;
                    }

                    const auto& NeighborGI = IndirectReservoirs[nIdx];
                    if (NeighborGI.WeightSum <= 0.0f)
                    {
                        continue;
                    }

                    float Jacobian = EvaluateJacobian(NeighborHit.HitLocation, CenterHit.HitLocation, NeighborGI.BounceHitPosition, NeighborGI.BounceHitNormal);
                    float ShiftedWeight = (NeighborGI.IndirectRadiance.x + NeighborGI.IndirectRadiance.y + NeighborGI.IndirectRadiance.z) * 0.3333f * NeighborGI.UnbiasedWeight * Jacobian;

                    SpatialGI.ResampleIndirect(NeighborGI.BounceHitPosition, NeighborGI.BounceHitNormal, NeighborGI.IndirectRadiance, ShiftedWeight, Dist(Rng));
                }

                const uint32_t MCap = 20u * (BounceCount > 0u ? BounceCount : 1u);
                if (SpatialGI.SampleCount > MCap)
                {
                    SpatialGI.WeightSum *= static_cast<float>(MCap) / static_cast<float>(SpatialGI.SampleCount);
                    SpatialGI.SampleCount = MCap;
                }

                if (SpatialGI.WeightSum > 0.0f && SpatialGI.SampleCount > 0)
                {
                    SpatialGI.UnbiasedWeight = SpatialGI.WeightSum / static_cast<float>(SpatialGI.SampleCount);
                }

                SpatialIndirectReservoirs[idx] = SpatialGI;
            }
        }
        IndirectReservoirs = SpatialIndirectReservoirs;
    }

    // Phase 5: Indirect Radiosity Bilateral Spatial Filtering
    std::vector<Vector3> FilteredIndirect(Width * Height, Vector3{ 0.0f, 0.0f, 0.0f });
    for (uint32_t y = 0; y < Height; ++y)
    {
        for (uint32_t x = 0; x < Width; ++x)
        {
            size_t idx = y * Width + x;
            const auto& CenterHit = PrimaryHits[idx];

            if (!CenterHit.ValidCondition)
            {
                continue;
            }

            Vector3 IndirectAcc{ 0.0f, 0.0f, 0.0f };
            float   WeightTotal = 0.0f;

            for (int dy = -3; dy <= 3; ++dy)
            {
                for (int dx = -3; dx <= 3; ++dx)
                {
                    int qx = static_cast<int>(x) + dx;
                    int qy = static_cast<int>(y) + dy;

                    if (qx < 0 || qx >= static_cast<int>(Width) || qy < 0 || qy >= static_cast<int>(Height))
                    {
                        continue;
                    }

                    size_t qIdx = static_cast<size_t>(qy) * Width + static_cast<size_t>(qx);
                    const auto& NeighborHit = PrimaryHits[qIdx];

                    if (!NeighborHit.ValidCondition)
                    {
                        continue;
                    }

                    float SpatialDistSq = static_cast<float>(dx * dx + dy * dy);
                    float SpatialW = std::exp(-SpatialDistSq / 9.0f);

                    float NormalDot = std::max(0.0f, OrientationClassifier::DotProduct(CenterHit.SurfaceNormal, NeighborHit.SurfaceNormal));
                    float NormalW = std::pow(NormalDot, 16.0f);

                    float DepthDiff = std::abs(CenterHit.RayDistance - NeighborHit.RayDistance);
                    float DepthW = std::exp(-DepthDiff * 3.0f);

                    float TotalW = SpatialW * NormalW * DepthW;
                    const auto& GIRes = IndirectReservoirs[qIdx];
                    if (GIRes.WeightSum > 0.0f && GIRes.KeptWeight > 1e-6f)
                    {
                        // Each neighbor shades its own unbiased RIS estimate
                        // (kept radiance times W over kept target weight); the
                        // bilateral kernel averages estimates, not raw samples.
                        float NormRatio = std::min(GIRes.UnbiasedWeight / GIRes.KeptWeight, 8.0f);
                        Vector3 Estimate = GIRes.IndirectRadiance * NormRatio;
                        IndirectAcc += Estimate * TotalW;
                        WeightTotal += TotalW;
                    }
                }
            }

            if (WeightTotal > 0.0f)
            {
                FilteredIndirect[idx] = IndirectAcc * (1.0f / WeightTotal);
            }
        }
    }

    // Phase 6: Radiance Composition with Aerial Perspective and Fog March
    for (uint32_t y = 0; y < Height; ++y)
    {
        for (uint32_t x = 0; x < Width; ++x)
        {
            size_t idx = y * Width + x;
            const auto& Hit = PrimaryHits[idx];
            RayStructure PrimaryRay = GeneratePrimaryRay(ActiveCamera, x, y, 0.5f, 0.5f);
            const Vector3 RenderOrigin = SkyFogIntegrator::RenderFromWorld(PrimaryRay.SpatialOrigin);
            const Vector3 RenderDir = SkyFogIntegrator::RenderFromWorld(PrimaryRay.RayDirection);

            if (!Hit.ValidCondition)
            {
                const Vector3 MidPoint = PrimaryRay.SpatialOrigin + PrimaryRay.RayDirection * (FogFar * 0.5f);
                float FogVisible = Scene.EvaluateOcclusion(MidPoint, MidPoint + FogLightWorld * 1000.0f) ? 0.0f : 1.0f;
                float Transmittance = 1.0f;
                Vector3 Scatter = SkyFog.MarchFog(RenderOrigin, RenderDir, 0.0f, FogFar, FogVisible, Transmittance);
                // Below-horizon misses sample the horizon zenith instead of
                // the panel underground: distant haze, not a black seam.
                Vector3 SkyDir = RenderDir;
                if (SkyDir.y < 0.0f)
                {
                    SkyDir.y = 0.0f;
                    float SkyLenSq = SkyDir.x * SkyDir.x + SkyDir.z * SkyDir.z;
                    SkyDir = (SkyLenSq > 1e-12f) ? SkyDir / std::sqrt(SkyLenSq) : RenderDir;
                }
                Vector3 Sky = SkyFog.ComputeSkyRadiance(SkyDir);
                AccumulatedBuffer[idx] = Scatter + Sky * Transmittance;
                continue;
            }

            const auto& HitMat = Materials[Hit.MaterialIndex];
            Vector3 Surface = DirectLight[idx] + HitMat.AlbedoColor * FilteredIndirect[idx];
            Vector3 Aerial = SkyFog.ApplyAerialPerspective(Surface, RenderOrigin, RenderDir, Hit.RayDistance);
            const Vector3 MidPoint = PrimaryRay.SpatialOrigin + PrimaryRay.RayDirection * (Hit.RayDistance * 0.5f);
            float FogVisible = Scene.EvaluateOcclusion(MidPoint, MidPoint + FogLightWorld * 1000.0f) ? 0.0f : 1.0f;
            float Transmittance = 1.0f;
            Vector3 Scatter = SkyFog.MarchFog(RenderOrigin, RenderDir, 0.0f, Hit.RayDistance, FogVisible, Transmittance);
            AccumulatedBuffer[idx] = Scatter + Aerial * Transmittance;
        }
    }

    // Phase 7: panel `post` lens flare mirror, composited in linear HDR
    // before the panel post chain in the export.
    if (FlareEnabled)
    {
        float SunElevDeg = std::asin(std::clamp(SunWorld.z, -1.0f, 1.0f)) * 57.295779513f;
        ApplyLensFlare(ActiveCamera, SunWorld, SunElevDeg, SkyFog.QuerySunFlareTint(), FlareVariety);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  IMAGE EXPORT
//------------------------------------------------------------------------------------------------------------------------

void RendererHost::ApplyLensFlare(const Frontier::CameraProjection& ActiveCamera, const Vector3& SunWorld,
    float SunElevationDeg, const Vector3& SunFlareTint, float FlareVariety) noexcept
{
    FlarePostConfig FlareCfg = FlarePostDefaults();
    FlareCfg.variety = FlareVariety;

    const float TanHalf = std::tan(ActiveCamera.QueryFieldOfViewRadians() * 0.5f);
    const float Aspect = ActiveCamera.QueryAspectRatio();
    const Vector3& Fwd = ActiveCamera.QueryForwardVector();
    const Vector3& Rgt = ActiveCamera.QueryRightVector();
    const Vector3& Up = ActiveCamera.QueryUpwardVector();

    // Reference `AddLensFlare` projection: the panel flares the sun only, so
    // the showcase does the same — moon and stars never carry flare, and a
    // sun behind the camera (or below the horizon ramp) contributes nothing.
    const float SunCx = OrientationClassifier::DotProduct(SunWorld, Rgt);
    const float SunCy = OrientationClassifier::DotProduct(SunWorld, Up);
    const float SunCz = OrientationClassifier::DotProduct(SunWorld, Fwd);
    const bool SunFront = SunCz > 0.02f;
    const float SunU = SunFront ? SunCx / SunCz / TanHalf : 99.0f;
    const float SunV = SunFront ? SunCy / SunCz / TanHalf : 99.0f;
    const float SunVis = FlareSourceVisibility(SunCz, SunElevationDeg, SunU, SunV);

    if (SunVis <= 0.0f)
    {
        return;
    }

    for (uint32_t y = 0; y < Height; ++y)
    {
        float v = 1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(Height) * 2.0f;
        for (uint32_t x = 0; x < Width; ++x)
        {
            float u = ((static_cast<float>(x) + 0.5f) / static_cast<float>(Width) * 2.0f - 1.0f) * Aspect;
            size_t idx = static_cast<size_t>(y) * Width + x;
            float3 sunF = FlareLensKernel(u, v, SunU, SunV, SunVis, FlareCfg);
            AccumulatedBuffer[idx].x += sunF.x * SunFlareTint.x;
            AccumulatedBuffer[idx].y += sunF.y * SunFlareTint.y;
            AccumulatedBuffer[idx].z += sunF.z * SunFlareTint.z;
        }
    }
}

bool RendererHost::ExportPpmImage(const std::string& OutputPath) const noexcept
{
    std::ofstream Out(OutputPath, std::ios::binary);
    if (!Out.is_open())
    {
        return false;
    }

    Out << "P6\n" << Width << " " << Height << "\n255\n";

    for (uint32_t y = 0; y < Height; ++y)
    {
        for (uint32_t x = 0; x < Width; ++x)
        {
            size_t idx = y * Width + x;
            Vector3 Radiance = AccumulatedBuffer[idx];

            // ACES Film Tone Mapping Curve
            auto AcesFilm = [](float x) -> float
            {
                float a = 2.51f;
                float b = 0.03f;
                float c = 2.43f;
                float d = 0.59f;
                float e = 0.14f;
                return std::clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
            };

            float r = AcesFilm(Radiance.x * ToneExposure);
            float g = AcesFilm(Radiance.y * ToneExposure);
            float b = AcesFilm(Radiance.z * ToneExposure);

            // Gamma 2.2 correction
            constexpr float InvGamma = 1.0f / 2.2f;
            r = std::pow(r, InvGamma);
            g = std::pow(g, InvGamma);
            b = std::pow(b, InvGamma);

            uint8_t R8 = static_cast<uint8_t>(std::clamp(r * 255.0f, 0.0f, 255.0f));
            uint8_t G8 = static_cast<uint8_t>(std::clamp(g * 255.0f, 0.0f, 255.0f));
            uint8_t B8 = static_cast<uint8_t>(std::clamp(b * 255.0f, 0.0f, 255.0f));

            Out.put(static_cast<char>(R8));
            Out.put(static_cast<char>(G8));
            Out.put(static_cast<char>(B8));
        }
    }

    return true;
}

// The gated panel post chain (exposure, tonemap, vignette) so the
// showcase matches the harness proof frames pixel-structure. Single
// shared LDR core: both the PPM file export and the live window feed
// on these exact bytes, so the window shows what the proofs show.
void RendererHost::ExportLdrPixels(std::vector<uint8_t>& OutRgba) const noexcept
{
    OutRgba.resize(static_cast<size_t>(Width) * static_cast<size_t>(Height) * 4U);
    for (uint32_t y = 0; y < Height; ++y)
    {
        for (uint32_t x = 0; x < Width; ++x)
        {
            size_t idx = static_cast<size_t>(y) * Width + x;
            Vector3 Display = SkyFog.ApplyPanelPost(AccumulatedBuffer[idx], x, y, Width, Height);

            size_t o = idx * 4U;
            OutRgba[o + 0U] = static_cast<uint8_t>(std::clamp(Display.x * 255.0f, 0.0f, 255.0f));
            OutRgba[o + 1U] = static_cast<uint8_t>(std::clamp(Display.y * 255.0f, 0.0f, 255.0f));
            OutRgba[o + 2U] = static_cast<uint8_t>(std::clamp(Display.z * 255.0f, 0.0f, 255.0f));
            OutRgba[o + 3U] = 255U;
        }
    }
}

bool RendererHost::ExportShowcaseImage(const std::string& OutputPath) const noexcept
{
    std::ofstream Out(OutputPath, std::ios::binary);
    if (!Out.is_open())
    {
        return false;
    }

    Out << "P6\n" << Width << " " << Height << "\n255\n";

    std::vector<uint8_t> LdrRgba;
    ExportLdrPixels(LdrRgba);
    for (size_t i = 0; i < LdrRgba.size(); i += 4U)
    {
        Out.put(static_cast<char>(LdrRgba[i + 0U]));
        Out.put(static_cast<char>(LdrRgba[i + 1U]));
        Out.put(static_cast<char>(LdrRgba[i + 2U]));
    }

    return true;
}

} // namespace Frontier::ProjectZero
