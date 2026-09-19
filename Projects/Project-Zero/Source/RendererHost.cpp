//============================================================================================================================================
// 📦 Project-Zero/Source/RendererHost.cpp — ReSTIR DI & GI Frame Execution and Image Export Implementation
//============================================================================================================================================

#include "RendererHost.h"
#include <fstream>
#include <cmath>
#include <algorithm>
#include <random>
#include <iostream>

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

RayRecord RendererHost::GeneratePrimaryRay(const Frontier::CameraProjection& ActiveCamera, uint32_t PixelX, uint32_t PixelY, float JitterX, float JitterY) const noexcept
{
    float u = (static_cast<float>(PixelX) + JitterX) / static_cast<float>(Width);
    float v = (static_cast<float>(PixelY) + JitterY) / static_cast<float>(Height);
    Frontier::ViewRay Ray = ActiveCamera.ConstructRay(u, v);
    return RayRecord{ Ray.OriginLocation, Ray.UnitDirection, Ray.NearClippingDistance, Ray.FarClippingDistance };
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

    return (cos2 * lenSq1) / (cos1 * lenSq2);
}

//------------------------------------------------------------------------------------------------------------------------
//                                           RESTIR DI & GI RENDER PIPELINE
//------------------------------------------------------------------------------------------------------------------------

void RendererHost::RenderReSTIRFrame(const Frontier::CameraProjection& ActiveCamera, uint32_t SpatialPassCount) noexcept
{
    std::mt19937 Rng(1337);
    std::uniform_real_distribution<float> Dist(0.0f, 1.0f);

    const auto& Materials = Scene.QueryMaterials();

    // Luminaire description: Ceiling Quad light
    Vector3 LightMin{ -0.28f, 1.995f, 0.72f };
    Vector3 LightMax{  0.28f, 1.995f, 1.28f };
    Vector3 LightNormal{ 0.0f, -1.0f, 0.0f };
    Vector3 LightEmission{ 12.0f, 12.0f, 12.0f };
    float   LightArea = (LightMax.x - LightMin.x) * (LightMax.z - LightMin.z);

    // Phase 1: Visibility Buffer Primary Ray Trace from Active Camera
    for (uint32_t y = 0; y < Height; ++y)
    {
        for (uint32_t x = 0; x < Width; ++x)
        {
            size_t idx = y * Width + x;
            RayRecord PrimaryRay = GeneratePrimaryRay(ActiveCamera, x, y, 0.5f, 0.5f);
            PrimaryHits[idx] = Scene.EvaluateIntersection(PrimaryRay);
        }
    }

    // Phase 2: Direct Illumination with Stratified Luminaire Integration
    std::vector<Vector3> DirectBuffer(Width * Height, Vector3{ 0.0f, 0.0f, 0.0f });
    for (uint32_t y = 0; y < Height; ++y)
    {
        for (uint32_t x = 0; x < Width; ++x)
        {
            size_t idx = y * Width + x;
            const auto& Hit = PrimaryHits[idx];

            if (!Hit.ValidCondition || Hit.MaterialIndex == 3)
            {
                continue;
            }

            const auto& Mat = Materials[Hit.MaterialIndex];
            Vector3 DirectSum{ 0.0f, 0.0f, 0.0f };
            constexpr int GridSteps = 4;
            float CellWeight = 1.0f / static_cast<float>(GridSteps * GridSteps);

            for (int gy = 0; gy < GridSteps; ++gy)
            {
                for (int gx = 0; gx < GridSteps; ++gx)
                {
                    float u = (static_cast<float>(gx) + Dist(Rng)) / static_cast<float>(GridSteps);
                    float v = (static_cast<float>(gy) + Dist(Rng)) / static_cast<float>(GridSteps);

                    float lx = LightMin.x + u * (LightMax.x - LightMin.x);
                    float lz = LightMin.z + v * (LightMax.z - LightMin.z);
                    Vector3 LightPoint{ lx, LightMin.y, lz };

                    if (!Scene.EvaluateOcclusion(Hit.HitLocation + Hit.SurfaceNormal * 0.001f, LightPoint))
                    {
                        Vector3 DirToLight = LightPoint - Hit.HitLocation;
                        float DistSq = DirToLight.LengthSquared();
                        float DistLen = std::sqrt(DistSq);
                        Vector3 L = DirToLight / DistLen;

                        float CosTheta = std::max(0.0f, OrientationClassifier::DotProduct(Hit.SurfaceNormal, L));
                        float CosLight = std::max(0.0f, OrientationClassifier::DotProduct(LightNormal, L * -1.0f));

                        if (CosTheta > 0.0f && CosLight > 0.0f)
                        {
                            float Geom = (CosTheta * CosLight) / DistSq;
                            DirectSum += Mat.AlbedoColor * (LightEmission * (Geom * LightArea * CellWeight * 0.40f));
                        }
                    }
                }
            }

            DirectBuffer[idx] = DirectSum;
        }
    }

    // Phase 3: ReSTIR GI Initial Candidate Bounce Ray Tracing (8 samples)
    for (uint32_t y = 0; y < Height; ++y)
    {
        for (uint32_t x = 0; x < Width; ++x)
        {
            size_t idx = y * Width + x;
            const auto& Hit = PrimaryHits[idx];
            IndirectGIReservoir GIReservoir{ Vector3{ 0.0f, 0.0f, 0.0f }, Vector3{ 0.0f, 1.0f, 0.0f }, Vector3{ 0.0f, 0.0f, 0.0f }, 0.0f, 0, 0.0f };

            if (!Hit.ValidCondition || Hit.MaterialIndex == 3)
            {
                IndirectReservoirs[idx] = GIReservoir;
                continue;
            }

            for (uint32_t s = 0; s < 8; ++s)
            {
                Vector3 BounceDir = SampleCosineHemisphere(Hit.SurfaceNormal, Dist(Rng), Dist(Rng));
                RayRecord BounceRay{ Hit.HitLocation + Hit.SurfaceNormal * 0.001f, BounceDir, 0.001f, 50.0f };

                HitIntersection BounceHit = Scene.EvaluateIntersection(BounceRay);
                if (BounceHit.ValidCondition && BounceHit.MaterialIndex != 3)
                {
                    const auto& BounceMat = Materials[BounceHit.MaterialIndex];

                    float lx = LightMin.x + Dist(Rng) * (LightMax.x - LightMin.x);
                    float lz = LightMin.z + Dist(Rng) * (LightMax.z - LightMin.z);
                    Vector3 LightCenter{ lx, LightMin.y, lz };

                    Vector3 ToLight = LightCenter - BounceHit.HitLocation;
                    float dSq = ToLight.LengthSquared();
                    float dLen = std::sqrt(dSq);
                    Vector3 L = ToLight / dLen;

                    float CosTheta = std::max(0.0f, OrientationClassifier::DotProduct(BounceHit.SurfaceNormal, L));
                    float CosLight = std::max(0.0f, OrientationClassifier::DotProduct(LightNormal, L * -1.0f));

                    Vector3 BounceRadiance{ 0.0f, 0.0f, 0.0f };
                    if (!Scene.EvaluateOcclusion(BounceHit.HitLocation + BounceHit.SurfaceNormal * 0.001f, LightCenter))
                    {
                        float Geom = (CosTheta * CosLight) / (dSq + 0.05f);
                        BounceRadiance = BounceMat.AlbedoColor * (LightEmission * (Geom * LightArea * 0.35f));
                    }

                    float Weight = (BounceRadiance.x + BounceRadiance.y + BounceRadiance.z) * 0.3333f;
                    GIReservoir.ResampleIndirect(BounceHit.HitLocation, BounceHit.SurfaceNormal, BounceRadiance, Weight, Dist(Rng));
                }
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
                if (!CenterHit.ValidCondition || CenterHit.MaterialIndex == 3)
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

                    size_t nIdx = ny * Width + nx;
                    const auto& NeighborHit = PrimaryHits[nIdx];

                    if (!NeighborHit.ValidCondition || NeighborHit.MaterialIndex == 3)
                    {
                        continue;
                    }
                    if (OrientationClassifier::DotProduct(CenterHit.SurfaceNormal, NeighborHit.SurfaceNormal) < 0.90f)
                    {
                        continue;
                    }
                    if (std::abs(CenterHit.RayDistance - NeighborHit.RayDistance) > 0.10f)
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
    std::vector<Vector3> FilteredIndirectBuffer(Width * Height, Vector3{ 0.0f, 0.0f, 0.0f });
    for (uint32_t y = 0; y < Height; ++y)
    {
        for (uint32_t x = 0; x < Width; ++x)
        {
            size_t idx = y * Width + x;
            const auto& CenterHit = PrimaryHits[idx];

            if (!CenterHit.ValidCondition || CenterHit.MaterialIndex == 3)
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

                    size_t qIdx = qy * Width + qx;
                    const auto& NeighborHit = PrimaryHits[qIdx];

                    if (!NeighborHit.ValidCondition || NeighborHit.MaterialIndex == 3)
                    {
                        continue;
                    }

                    float SpatialDistSq = static_cast<float>(dx * dx + dy * dy);
                    float SpatialW = std::exp(-SpatialDistSq / 9.0f);

                    float NormalDot = std::max(0.0f, OrientationClassifier::DotProduct(CenterHit.SurfaceNormal, NeighborHit.SurfaceNormal));
                    float NormalW = std::pow(NormalDot, 16.0f);

                    float DepthDiff = std::abs(CenterHit.RayDistance - NeighborHit.RayDistance);
                    float DepthW = std::exp(-DepthDiff * 15.0f);

                    float TotalW = SpatialW * NormalW * DepthW;
                    const auto& GIRes = IndirectReservoirs[qIdx];
                    if (GIRes.WeightSum > 0.0f)
                    {
                        IndirectAcc += GIRes.IndirectRadiance * TotalW;
                        WeightTotal += TotalW;
                    }
                }
            }

            if (WeightTotal > 0.0f)
            {
                FilteredIndirectBuffer[idx] = IndirectAcc * (1.0f / WeightTotal);
            }
        }
    }

    // Phase 6: Radiance Composition
    for (uint32_t y = 0; y < Height; ++y)
    {
        for (uint32_t x = 0; x < Width; ++x)
        {
            size_t idx = y * Width + x;
            const auto& Hit = PrimaryHits[idx];

            if (!Hit.ValidCondition)
            {
                AccumulatedBuffer[idx] = Vector3{ 0.02f, 0.02f, 0.03f };
                continue;
            }

            const auto& Mat = Materials[Hit.MaterialIndex];

            // Emissive surface (Ceiling light)
            if (Hit.MaterialIndex == 3)
            {
                AccumulatedBuffer[idx] = Mat.EmissiveRadiance;
                continue;
            }

            Vector3 DirectRad = DirectBuffer[idx];
            Vector3 IndirectRad = Mat.AlbedoColor * (FilteredIndirectBuffer[idx] * 0.40f);
            Vector3 AmbientRad = Mat.AlbedoColor * 0.015f;

            AccumulatedBuffer[idx] = DirectRad + IndirectRad + AmbientRad;
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  IMAGE EXPORT
//------------------------------------------------------------------------------------------------------------------------

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

            constexpr float Exposure = 1.05f;
            float r = AcesFilm(Radiance.x * Exposure);
            float g = AcesFilm(Radiance.y * Exposure);
            float b = AcesFilm(Radiance.z * Exposure);

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

} // namespace Frontier::ProjectZero
