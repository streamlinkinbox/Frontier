//============================================================================================================================================
//                                                 DISTANCEFIELDINTEGRATOR.CPP
//============================================================================================================================================
// 📦 Distance Field Global Illumination integrator with Surface Cache sampling, soft shadows, and ReSTIR GI resampling.

#include "DistanceFieldIntegrator.h"
#include <cmath>
#include <algorithm>
#include <random>

#if defined(_OPENMP)
    #include <omp.h>
#endif

namespace Frontier {

namespace {

constexpr float kPi = 3.14159265358979323846f;

inline float Dot(const Vector3& A, const Vector3& B) noexcept
{
    return A.x * B.x + A.y * B.y + A.z * B.z;
}

inline Vector3 Cross(const Vector3& A, const Vector3& B) noexcept
{
    return {
        A.y * B.z - A.z * B.y,
        A.z * B.x - A.x * B.z,
        A.x * B.y - A.y * B.x
    };
}

// Fast linear pseudo-random generator
inline float HashFloat(uint32_t& Seed) noexcept
{
    Seed = Seed * 1664525u + 1013904223u;
    return static_cast<float>(Seed & 0x00FFFFFFu) / 16777216.0f;
}

Vector3 SampleCosineHemisphere(Vector3 Normal, float U1, float U2) noexcept
{
    const float RadialDistance = std::sqrt(U1);
    const float AzimuthAngle   = 2.0f * kPi * U2;

    const float LocalX = RadialDistance * std::cos(AzimuthAngle);
    const float LocalY = RadialDistance * std::sin(AzimuthAngle);
    const float LocalZ = std::sqrt(std::max(0.0f, 1.0f - U1));

    Vector3 TangentX = (std::abs(Normal.z) < 0.999f)
        ? Cross(Normal, Vector3{ 0.0f, 0.0f, 1.0f }).Normalized()
        : Cross(Normal, Vector3{ 1.0f, 0.0f, 0.0f }).Normalized();
    Vector3 TangentY = Cross(Normal, TangentX).Normalized();

    return (TangentX * LocalX + TangentY * LocalY + Normal * LocalZ).Normalized();
}

float Luminance(Vector3 C) noexcept
{
    return C.x * 0.2126f + C.y * 0.7152f + C.z * 0.0722f;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

DistanceFieldIntegrator::DistanceFieldIntegrator(uint32_t InViewportWidth, uint32_t InViewportHeight) noexcept
{
    Resize(InViewportWidth, InViewportHeight);
}

void DistanceFieldIntegrator::Resize(uint32_t InViewportWidth, uint32_t InViewportHeight) noexcept
{
    ViewportWidth  = std::max(16u, InViewportWidth);
    ViewportHeight = std::max(16u, InViewportHeight);

    const size_t TotalPixels = static_cast<size_t>(ViewportWidth) * ViewportHeight;
    TemporalReservoirBuffer.assign(TotalPixels, DistanceFieldReservoir{});
    CurrentReservoirBuffer.assign(TotalPixels, DistanceFieldReservoir{});
    DepthBuffer.assign(TotalPixels, 1e6f);
    NormalBuffer.assign(TotalPixels, Vector3{ 0.0f, 0.0f, 1.0f });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                RESERVOIR LOGIC
//------------------------------------------------------------------------------------------------------------------------

void DistanceFieldIntegrator::UpdateReservoir(DistanceFieldReservoir& Receiver,
                                              Vector3 CandidateRadiance,
                                              Vector3 CandidateDirection,
                                              float CandidateWeight,
                                              float TargetP) noexcept
{
    Receiver.WeightSum += CandidateWeight;
    Receiver.SampleCountM += 1.0f;

    // Fixed deterministic or fractional selection
    if (CandidateWeight > 0.0f)
    {
        const float SelectProb = CandidateWeight / Receiver.WeightSum;
        if (SelectProb >= 0.5f || Receiver.SampleCountM <= 1.0f)
        {
            Receiver.SampleRadiance     = CandidateRadiance;
            Receiver.SampleDirection    = CandidateDirection;
            Receiver.TargetProbability  = TargetP;
        }
    }

    if (Receiver.TargetProbability > 1e-6f && Receiver.SampleCountM > 0.0f)
    {
        Receiver.UnbiasedWeightW = Receiver.WeightSum / (Receiver.SampleCountM * Receiver.TargetProbability);
    }
    else
    {
        Receiver.UnbiasedWeightW = 0.0f;
    }
}

void DistanceFieldIntegrator::MergeReservoirs(DistanceFieldReservoir& Receiver,
                                              const DistanceFieldReservoir& Source,
                                              float TargetP) noexcept
{
    if (Source.SampleCountM <= 0.0f || Source.UnbiasedWeightW <= 0.0f) return;

    const float Weight = Source.UnbiasedWeightW * Source.SampleCountM * TargetP;
    Receiver.WeightSum += Weight;
    Receiver.SampleCountM += Source.SampleCountM;

    const float SelectProb = (Receiver.WeightSum > 0.0f) ? (Weight / Receiver.WeightSum) : 0.0f;
    if (SelectProb >= 0.5f || Receiver.SampleCountM <= Source.SampleCountM)
    {
        Receiver.SampleRadiance     = Source.SampleRadiance;
        Receiver.SampleDirection    = Source.SampleDirection;
        Receiver.TargetProbability  = TargetP;
    }

    if (Receiver.TargetProbability > 1e-6f && Receiver.SampleCountM > 0.0f)
    {
        Receiver.UnbiasedWeightW = Receiver.WeightSum / (Receiver.SampleCountM * Receiver.TargetProbability);
    }
    else
    {
        Receiver.UnbiasedWeightW = 0.0f;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FRAME EXECUTION
//------------------------------------------------------------------------------------------------------------------------

void DistanceFieldIntegrator::ExecuteFrame(const GlobalDistanceFieldSpace& DistanceField,
                                           const SurfaceCacheStructure& SurfaceCache,
                                           Vector3 CameraPosition,
                                           Vector3 CameraTarget,
                                           Vector3 SunDirection,
                                           Vector3 SunRadiance,
                                           const DistanceFieldIntegratorSettings& Settings,
                                           std::vector<uint8_t>& OutRgbImage) noexcept
{
    const uint32_t Width  = ViewportWidth;
    const uint32_t Height = ViewportHeight;
    const size_t TotalPixels = static_cast<size_t>(Width) * Height;

    OutRgbImage.resize(TotalPixels * 3u);

    // Camera basis vectors
    const Vector3 CamForward = (CameraTarget - CameraPosition).Normalized();
    const Vector3 CamRight   = Cross(CamForward, Vector3{ 0.0f, 0.0f, 1.0f }).Normalized();
    const Vector3 CamUp      = Cross(CamRight, CamForward).Normalized();
    const float AspectRatio  = static_cast<float>(Width) / static_cast<float>(Height);
    const float HalfFovTan   = std::tan(45.0f * kPi / 360.0f);

    const float LightAngleRad = Settings.SoftShadowLightAngleDegrees * kPi / 180.0f;

    // PASS 1: Primary ray marching + Direct Lighting & Shadows + Initial GI candidates
#if defined(_OPENMP)
    #pragma omp parallel for collapse(2) schedule(dynamic, 4)
#endif
    for (int32_t Y = 0; Y < static_cast<int32_t>(Height); ++Y)
    {
        for (int32_t X = 0; X < static_cast<int32_t>(Width); ++X)
        {
            const size_t PixelIndex = static_cast<size_t>(Y) * Width + static_cast<size_t>(X);
            uint32_t RngSeed = static_cast<uint32_t>(PixelIndex * 1973u + 9277u);

            const float ScreenU = ((static_cast<float>(X) + 0.5f) / static_cast<float>(Width) * 2.0f - 1.0f) * AspectRatio * HalfFovTan;
            const float ScreenV = (1.0f - (static_cast<float>(Y) + 0.5f) / static_cast<float>(Height) * 2.0f) * HalfFovTan;
            const Vector3 PrimaryRayDir = (CamForward + CamRight * ScreenU + CamUp * ScreenV).Normalized();

            // Trace primary visibility ray
            DistanceFieldHitRecord PrimaryHit{};
            if (Settings.EnableGlobalDistanceField)
            {
                PrimaryHit = DistanceField.MarchSceneRay(CameraPosition,
                                                         PrimaryRayDir,
                                                         0.2f,
                                                         12.0f,
                                                         0.002f,
                                                         Settings.MaxRayMarchSteps,
                                                         Settings.StepRelaxationFactor);
            }
            else
            {
                // Single field fallback
                PrimaryHit = DistanceField.GetGlobalVolume().MarchRay(CameraPosition,
                                                                     PrimaryRayDir,
                                                                     0.2f,
                                                                     12.0f,
                                                                     0.002f,
                                                                     Settings.MaxRayMarchSteps,
                                                                     Settings.StepRelaxationFactor);
            }

            DepthBuffer[PixelIndex]  = PrimaryHit.HasHit ? PrimaryHit.TravelDistance : 1e6f;
            NormalBuffer[PixelIndex] = PrimaryHit.HasHit ? PrimaryHit.SurfaceNormal : Vector3{ 0.0f, 0.0f, 1.0f };

            DistanceFieldReservoir& InitialReservoir = CurrentReservoirBuffer[PixelIndex];
            InitialReservoir = DistanceFieldReservoir{};

            if (PrimaryHit.HasHit)
            {
                const Vector3 HitPosition = PrimaryHit.HitPosition;
                const Vector3 HitNormal   = PrimaryHit.SurfaceNormal;

                // Gather initial candidate indirect rays
                const uint32_t CandidateCount = std::max(1u, Settings.GlobalIlluminationRayCount);
                for (uint32_t C = 0u; C < CandidateCount; ++C)
                {
                    const float U1 = HashFloat(RngSeed);
                    const float U2 = HashFloat(RngSeed);
                    const Vector3 IndirectDir = SampleCosineHemisphere(HitNormal, U1, U2);

                    const Vector3 BounceOrigin = HitPosition + HitNormal * 0.015f;
                    const DistanceFieldHitRecord BounceHit = DistanceField.MarchSceneRay(BounceOrigin,
                                                                                         IndirectDir,
                                                                                         0.02f,
                                                                                         4.5f,
                                                                                         0.003f,
                                                                                         Settings.MaxRayMarchSteps / 2u,
                                                                                         Settings.StepRelaxationFactor);

                    Vector3 SampleRadiance{};
                    if (BounceHit.HasHit)
                    {
                        if (Settings.EnableSurfaceCache)
                        {
                            SampleRadiance = SurfaceCache.SampleRadianceFromWorld(BounceHit.HitPosition, BounceHit.SurfaceNormal);
                        }
                        else
                        {
                            SampleRadiance = Vector3{ 0.4f, 0.4f, 0.4f };
                        }
                    }
                    else
                    {
                        const float SkyGrad = std::max(0.0f, IndirectDir.z) * 0.5f + 0.5f;
                        SampleRadiance = Vector3{ 0.35f, 0.52f, 0.80f } * (SkyGrad * 0.35f);
                    }

                    const float TargetP = Luminance(SampleRadiance);
                    const float Weight = (TargetP > 1e-5f) ? 1.0f : 0.0f;
                    UpdateReservoir(InitialReservoir, SampleRadiance, IndirectDir, Weight, TargetP);
                }
            }
        }
    }

    // PASS 2: ReSTIR Spatio-Temporal Reuse
    std::vector<DistanceFieldReservoir> FinalReservoirBuffer = CurrentReservoirBuffer;

    if (Settings.EnableReSTIR)
    {
#if defined(_OPENMP)
        #pragma omp parallel for collapse(2) schedule(dynamic, 4)
#endif
        for (int32_t Y = 0; Y < static_cast<int32_t>(Height); ++Y)
        {
            for (int32_t X = 0; X < static_cast<int32_t>(Width); ++X)
            {
                const size_t PixelIndex = static_cast<size_t>(Y) * Width + static_cast<size_t>(X);
                const float PixelDepth  = DepthBuffer[PixelIndex];
                if (PixelDepth >= 1e5f) continue;

                const Vector3 PixelNormal = NormalBuffer[PixelIndex];
                DistanceFieldReservoir MergedReservoir = CurrentReservoirBuffer[PixelIndex];

                // Temporal reuse from previous frame
                DistanceFieldReservoir TemporalSample = TemporalReservoirBuffer[PixelIndex];
                if (TemporalSample.SampleCountM > 0.0f)
                {
                    TemporalSample.SampleCountM = std::min(TemporalSample.SampleCountM, Settings.ReSTIRHistoryCeiling);
                    MergeReservoirs(MergedReservoir, TemporalSample, MergedReservoir.TargetProbability);
                }

                // Spatial cross-reuse with bilateral validation
                const int32_t Radius = static_cast<int32_t>(Settings.ReSTIRSpatialRadius);
                const int32_t MinX = std::max(0, X - Radius);
                const int32_t MaxX = std::min(static_cast<int32_t>(Width) - 1, X + Radius);
                const int32_t MinY = std::max(0, Y - Radius);
                const int32_t MaxY = std::min(static_cast<int32_t>(Height) - 1, Y + Radius);

                for (int32_t NY = MinY; NY <= MaxY; NY += 2)
                {
                    for (int32_t NX = MinX; NX <= MaxX; NX += 2)
                    {
                        if (NX == X && NY == Y) continue;
                        const size_t NeighborIdx = static_cast<size_t>(NY) * Width + static_cast<size_t>(NX);
                        const float NeighborDepth = DepthBuffer[NeighborIdx];

                        // Bilateral depth guard
                        if (std::abs(NeighborDepth - PixelDepth) > 0.15f * PixelDepth) continue;

                        // Bilateral normal guard
                        const Vector3 NeighborNormal = NormalBuffer[NeighborIdx];
                        if (Dot(PixelNormal, NeighborNormal) < 0.85f) continue;

                        const DistanceFieldReservoir& NeighborRes = CurrentReservoirBuffer[NeighborIdx];
                        MergeReservoirs(MergedReservoir, NeighborRes, MergedReservoir.TargetProbability);
                    }
                }

                FinalReservoirBuffer[PixelIndex] = MergedReservoir;
                TemporalReservoirBuffer[PixelIndex] = MergedReservoir;
            }
        }
    }

    // PASS 3: Final Composition & Shading
#if defined(_OPENMP)
    #pragma omp parallel for collapse(2) schedule(dynamic, 4)
#endif
    for (int32_t Y = 0; Y < static_cast<int32_t>(Height); ++Y)
    {
        for (int32_t X = 0; X < static_cast<int32_t>(Width); ++X)
        {
            const size_t PixelIndex = static_cast<size_t>(Y) * Width + static_cast<size_t>(X);
            const float PixelDepth  = DepthBuffer[PixelIndex];
            const size_t OutOffset  = PixelIndex * 3u;

            if (PixelDepth >= 1e5f)
            {
                // Background sky
                OutRgbImage[OutOffset + 0u] = 28u;
                OutRgbImage[OutOffset + 1u] = 34u;
                OutRgbImage[OutOffset + 2u] = 44u;
                continue;
            }

            const float ScreenU = ((static_cast<float>(X) + 0.5f) / static_cast<float>(Width) * 2.0f - 1.0f) * AspectRatio * HalfFovTan;
            const float ScreenV = (1.0f - (static_cast<float>(Y) + 0.5f) / static_cast<float>(Height) * 2.0f) * HalfFovTan;
            const Vector3 PrimaryRayDir = (CamForward + CamRight * ScreenU + CamUp * ScreenV).Normalized();
            const Vector3 HitPosition   = CameraPosition + PrimaryRayDir * PixelDepth;
            const Vector3 HitNormal     = NormalBuffer[PixelIndex];

            // Distance field soft shadows toward sun
            const Vector3 ShadowOrigin = HitPosition + HitNormal * 0.015f;
            const float SoftShadowFactor = DistanceField.MarchSceneSoftShadow(ShadowOrigin,
                                                                              SunDirection,
                                                                              0.02f,
                                                                              5.0f,
                                                                              LightAngleRad,
                                                                              Settings.SoftShadowSteps);

            const float NDotL = std::max(0.0f, Dot(HitNormal, SunDirection));
            const Vector3 DirectSun = SunRadiance * (NDotL * SoftShadowFactor);

            // ReSTIR indirect GI
            const DistanceFieldReservoir& Res = FinalReservoirBuffer[PixelIndex];
            const Vector3 IndirectGI = Res.SampleRadiance * std::clamp(Res.UnbiasedWeightW, 0.0f, 3.0f);

            // Base material albedo: ShaderBall warm orange, ground neutral gray
            Vector3 Albedo = (HitPosition.z > 0.015f) ? Vector3{ 0.88f, 0.38f, 0.16f } : Vector3{ 0.45f, 0.45f, 0.45f };

            Vector3 FinalColor{};

            switch (Settings.VisualizationMode)
            {
            case DistanceFieldVisualizationMode::FullGlobalIllumination:
                FinalColor = (DirectSun + IndirectGI + Vector3{ 0.03f, 0.03f, 0.04f }) * Albedo;
                break;

            case DistanceFieldVisualizationMode::IndirectIlluminationOnly:
                FinalColor = IndirectGI * Albedo * 2.0f;
                break;

            case DistanceFieldVisualizationMode::SoftShadowsOnly:
                FinalColor = Vector3{ SoftShadowFactor, SoftShadowFactor, SoftShadowFactor };
                break;

            case DistanceFieldVisualizationMode::SurfaceCacheAtlasView:
                FinalColor = SurfaceCache.SampleRadianceFromWorld(HitPosition, HitNormal);
                break;

            case DistanceFieldVisualizationMode::DistanceVolumeSlices:
            {
                const float DistVal = DistanceField.SampleSceneDistance(HitPosition);
                const float NormDist = std::clamp(DistVal * 5.0f + 0.5f, 0.0f, 1.0f);
                FinalColor = Vector3{ NormDist, NormDist * 0.7f, 1.0f - NormDist };
                break;
            }

            case DistanceFieldVisualizationMode::SurfaceNormalGradient:
                FinalColor = HitNormal * 0.5f + Vector3{ 0.5f, 0.5f, 0.5f };
                break;
            }

            // ACES tone mapping + gamma correction (2.2)
            auto ToneMap = [](float V) noexcept -> uint8_t
            {
                const float Clamped = std::max(0.0f, V);
                const float Mapped  = (Clamped * (2.51f * Clamped + 0.03f)) / (Clamped * (2.43f * Clamped + 0.59f) + 0.14f);
                const float Gamma   = std::pow(std::clamp(Mapped, 0.0f, 1.0f), 1.0f / 2.2f);
                return static_cast<uint8_t>(std::clamp(Gamma * 255.0f, 0.0f, 255.0f));
            };

            OutRgbImage[OutOffset + 0u] = ToneMap(FinalColor.x);
            OutRgbImage[OutOffset + 1u] = ToneMap(FinalColor.y);
            OutRgbImage[OutOffset + 2u] = ToneMap(FinalColor.z);
        }
    }
}

} // namespace Frontier
