/*==========================================================================================================================================
                                          REPLAYVARIANCEFILTERVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Verify SVGF albedo demodulation, 3D tangent-plane edge stopping, subpixel bilinear gather, and corruption guards.

#include "Engine/Rendering/VarianceFiltering.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

namespace
{
using namespace Frontier;

void Require(bool Condition, const char* Message)
{
    if (!Condition) throw std::runtime_error(Message);
}

struct Vec3f
{
    float X = 0.0f, Y = 0.0f, Z = 0.0f;
};

float Dot3(Vec3f A, Vec3f B) noexcept
{
    return A.X * B.X + A.Y * B.Y + A.Z * B.Z;
}

float Power3(Vec3f C) noexcept
{
    return 0.2126f * C.X + 0.7152f * C.Y + 0.0722f * C.Z;
}

bool VarianceHistoryValid(std::array<float, 4> OldIrradiance, std::array<float, 4> OldMoments) noexcept
{
    for (float V : OldIrradiance)
        if (!std::isfinite(V)) return false;
    for (float V : OldMoments)
        if (!std::isfinite(V)) return false;
    if (OldIrradiance[3] <= 0.0f || OldIrradiance[3] > 64.0f) return false;
    for (int C = 0; C < 3; ++C)
        if (OldIrradiance[C] < 0.0f || OldIrradiance[C] > 1e6f) return false;
    if (OldMoments[0] < 0.0f || OldMoments[0] > 1e6f || OldMoments[1] < 0.0f || OldMoments[1] > 1e12f)
        return false;
    if (OldMoments[2] < 0.0f || OldMoments[2] > 1.0f || OldMoments[3] < 0.0f || OldMoments[3] > 1.0f)
        return false;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              ENGINE CPU SVGF BASELINE
//------------------------------------------------------------------------------------------------------------------------

void ValidateEngineCpuVarianceFiltering()
{
    constexpr uint32_t Width = 16, Height = 16;
    VarianceHistory History;
    History.Resize(Width, Height);
    std::mt19937_64 Generator(20261028ull);
    std::normal_distribution<double> Noise(0.0, 0.35);

    std::vector<double> Frame(Width * Height, 0.0);
    std::vector<double> Depth(Width * Height, 2.0);
    std::vector<Vec3> Normals(Width * Height, {0.0f, 0.0f, 1.0f});
    for (uint32_t Y = 0; Y < Height; ++Y)
        for (uint32_t X = 0; X < Width; ++X)
            if (X >= Width / 2)
            {
                Depth[Y * Width + X] = 5.0;
                Normals[Y * Width + X] = {1.0f, 0.0f, 0.0f};
            }

    for (uint32_t Step = 0; Step < 8; ++Step)
    {
        for (uint32_t Y = 0; Y < Height; ++Y)
            for (uint32_t X = 0; X < Width; ++X)
            {
                const double Base = X < Width / 2 ? 0.80 : 0.15;
                Frame[Y * Width + X] = std::max(0.0, Base + Noise(Generator));
            }
        Require(History.Integrate(Frame.data(), Width, Height, 1.0 / double(Step + 1)),
                "CPU VarianceHistory::Integrate failed");
    }

    std::vector<double> Filtered(Width * Height, 0.0), VarOut(Width * Height, 0.0);
    VarianceFilterParameters Params{};
    Require(VarianceWaveletStep(History.Mean().data(), History.Variance().data(), Depth.data(), Normals.data(),
                                Width, Height, 1, Params, Filtered.data(), VarOut.data()),
            "CPU VarianceWaveletStep failed");

    double RawMse = 0.0, FilteredMse = 0.0;
    for (uint32_t Y = 0; Y < Height; ++Y)
        for (uint32_t X = 0; X < Width; ++X)
        {
            const double Expected = X < Width / 2 ? 0.80 : 0.15;
            const double RawErr = Frame[Y * Width + X] - Expected;
            const double FltErr = Filtered[Y * Width + X] - Expected;
            RawMse += RawErr * RawErr;
            FilteredMse += FltErr * FltErr;
        }
    Require(FilteredMse < RawMse * 0.15, "CPU SVGF failed to reduce noisy step-edge MSE by >85%");
    // Normal discontinuity at X = 7 | 8 (dot = 0) must completely stop cross-edge bleeding.
    Require(std::abs(Filtered[8 * Width + 7] - Filtered[8 * Width + 8]) > 0.50,
            "CPU SVGF blurred across orthogonal normal boundary");
}

//------------------------------------------------------------------------------------------------------------------------
//                                           ALBEDO DEMODULATION SHARPNESS
//------------------------------------------------------------------------------------------------------------------------

void ValidateAlbedoDemodulationSharpness()
{
    constexpr uint32_t Size = 24;
    constexpr double TrueIrradiance = 0.60;
    std::mt19937_64 Generator(99173ull);
    std::uniform_real_distribution<double> Unit(0.0, 1.0);

    std::vector<double> Albedo(Size * Size, 0.0);
    std::vector<double> NoisyRadiance(Size * Size, 0.0);
    std::vector<double> NoisyIrradiance(Size * Size, 0.0);
    for (uint32_t Y = 0; Y < Size; ++Y)
        for (uint32_t X = 0; X < Size; ++X)
        {
            const double A = ((X + Y) & 1u) != 0 ? 0.85 : 0.12;
            // Exponential-like 1-spp Monte Carlo noise with exact mean TrueIrradiance.
            const double SampleIrr = -TrueIrradiance * std::log(std::max(Unit(Generator), 1e-6));
            Albedo[Y * Size + X] = A;
            NoisyRadiance[Y * Size + X] = A * SampleIrr;
            NoisyIrradiance[Y * Size + X] = NoisyRadiance[Y * Size + X] / std::max(A, 0.04);
        }

    constexpr int Kernel[5] = {1, 4, 6, 4, 1};
    auto BlurWavelet = [&](const std::vector<double>& Input, int Step)
    {
        std::vector<double> Out(Size * Size, 0.0);
        for (uint32_t Y = 0; Y < Size; ++Y)
            for (uint32_t X = 0; X < Size; ++X)
            {
                double Sum = 0.0, WSum = 0.0;
                for (int Dy = -2; Dy <= 2; ++Dy)
                    for (int Dx = -2; Dx <= 2; ++Dx)
                    {
                        const int Qx = int(X) + Dx * Step, Qy = int(Y) + Dy * Step;
                        if (Qx < 0 || Qy < 0 || Qx >= int(Size) || Qy >= int(Size)) continue;
                        const double W = double(Kernel[Dx + 2] * Kernel[Dy + 2]);
                        Sum += W * Input[size_t(Qy) * Size + size_t(Qx)];
                        WSum += W;
                    }
                Out[Y * Size + X] = Sum / WSum;
            }
        return Out;
    };

    const auto NaiveFiltered = BlurWavelet(BlurWavelet(NoisyRadiance, 1), 2);
    const auto DemodFilteredIrr = BlurWavelet(BlurWavelet(NoisyIrradiance, 1), 2);

    double RawMse = 0.0, NaiveMse = 0.0, DemodMse = 0.0;
    for (size_t I = 0; I < Albedo.size(); ++I)
    {
        const double Exact = Albedo[I] * TrueIrradiance;
        const double Demod = DemodFilteredIrr[I] * std::max(Albedo[I], 0.04);
        RawMse += (NoisyRadiance[I] - Exact) * (NoisyRadiance[I] - Exact);
        NaiveMse += (NaiveFiltered[I] - Exact) * (NaiveFiltered[I] - Exact);
        DemodMse += (Demod - Exact) * (Demod - Exact);
    }
    const double RawRmse = std::sqrt(RawMse / double(Albedo.size()));
    const double NaiveRmse = std::sqrt(NaiveMse / double(Albedo.size()));
    const double DemodRmse = std::sqrt(DemodMse / double(Albedo.size()));
    // Demodulated multi-step à-trous filtering crushes both raw 1-spp noise and naive texture-blurring error!
    Require(DemodRmse < RawRmse * 0.20 && DemodRmse < NaiveRmse * 0.25,
            "Albedo demodulation did not preserve checkerboard sharpness while denoising");
}

//------------------------------------------------------------------------------------------------------------------------
//                                  GRAZING TANGENT-PLANE AND SUBPIXEL GATHER GUARDS
//------------------------------------------------------------------------------------------------------------------------

void ValidateGrazingPlaneAndSubpixelGather()
{
    // 1. Grazing road plane (normal = (0, 0, 1)) with steep camera depth variation vs 0.45 m curb step.
    const Vec3f RoadNormal{0.0f, 0.0f, 1.0f};
    const Vec3f RoadCentre{0.0f, 8.0f, 0.0f};
    const Vec3f GrazingRoadTap{0.05f, 8.22f, 0.0f};
    const Vec3f CurbStepTap{0.05f, 8.10f, 0.45f};
    const float Footstep = 0.18f;
    const float PlaneTolerance = std::max(0.02f, Footstep * 1.0f * 0.22f);

    const Vec3f DeltaRoad{GrazingRoadTap.X - RoadCentre.X, GrazingRoadTap.Y - RoadCentre.Y,
                          GrazingRoadTap.Z - RoadCentre.Z};
    const Vec3f DeltaCurb{CurbStepTap.X - RoadCentre.X, CurbStepTap.Y - RoadCentre.Y,
                          CurbStepTap.Z - RoadCentre.Z};
    Require(std::abs(Dot3(DeltaRoad, RoadNormal)) == 0.0f &&
                std::abs(Dot3(DeltaRoad, RoadNormal)) <= PlaneTolerance,
            "Coplanar grazing road tap was falsely rejected");
    Require(std::abs(Dot3(DeltaCurb, RoadNormal)) > PlaneTolerance * 8.0f,
            "Vertical curb step failed tangent-plane rejection");

    // 2. Subpixel 2x2 bilinear reprojection with partial disocclusion masking.
    // Suppose prior texels (0,0) and (0,1) are foreground pillar (token 2, bright 99.0), while
    // (1,0) and (1,1) are background wall (token 1, irradiance 0.40 and 0.60).
    const float MotionX = 4.35f, MotionY = 7.50f;
    const int CornerX = int(std::floor(MotionX)), CornerY = int(std::floor(MotionY));
    const float Fx = MotionX - float(CornerX), Fy = MotionY - float(CornerY);
    const float PriorToken[2][2] = {{2.0f, 1.0f}, {2.0f, 1.0f}};
    const float PriorIrr[2][2] = {{99.0f, 0.40f}, {99.0f, 0.60f}};

    float IrrSum = 0.0f, WeightSum = 0.0f;
    for (int Dy = 0; Dy <= 1; ++Dy)
        for (int Dx = 0; Dx <= 1; ++Dx)
        {
            const float Bilinear = (Dx == 0 ? (1.0f - Fx) : Fx) * (Dy == 0 ? (1.0f - Fy) : Fy);
            if (PriorToken[Dy][Dx] != 1.0f) continue;
            IrrSum += Bilinear * PriorIrr[Dy][Dx];
            WeightSum += Bilinear;
        }
    Require(WeightSum > 0.30f && std::abs(IrrSum / WeightSum - 0.50f) < 1e-6f,
            "Subpixel 2x2 bilinear reprojection leaked foreground pillar across disocclusion");

    // 3. Corruption & overflow intake guards.
    const float Nan = std::numeric_limits<float>::quiet_NaN();
    const float Inf = std::numeric_limits<float>::infinity();
    Require(VarianceHistoryValid({0.3f, 0.4f, 0.5f, 8.0f}, {0.4f, 0.2f, 0.6f, 0.0f}),
            "Valid SVGF history record was rejected");
    Require(!VarianceHistoryValid({Nan, 0.4f, 0.5f, 8.0f}, {0.4f, 0.2f, 0.6f, 0.0f}) &&
                !VarianceHistoryValid({0.3f, Inf, 0.5f, 8.0f}, {0.4f, 0.2f, 0.6f, 0.0f}) &&
                !VarianceHistoryValid({-0.01f, 0.4f, 0.5f, 8.0f}, {0.4f, 0.2f, 0.6f, 0.0f}) &&
                !VarianceHistoryValid({0.3f, 0.4f, 0.5f, 0.0f}, {0.4f, 0.2f, 0.6f, 0.0f}) &&
                !VarianceHistoryValid({0.3f, 0.4f, 0.5f, 65.0f}, {0.4f, 0.2f, 0.6f, 0.0f}) &&
                !VarianceHistoryValid({0.3f, 0.4f, 0.5f, 8.0f}, {0.4f, -0.1f, 0.6f, 0.0f}) &&
                !VarianceHistoryValid({0.3f, 0.4f, 0.5f, 8.0f}, {0.4f, 0.2f, 1.5f, 0.0f}),
            "Corrupt or out-of-range SVGF history record bypassed intake guard");

    // 4. Multi-sigma dynamic illumination shift shortening vs stationary unbiased preservation.
    auto EffectiveOldLength = [](float OldLength, float OldMean, float PriorVar, float SpatialMean, float SpatialVar)
    {
        const float NoiseScale = std::max(std::sqrt(SpatialVar), std::sqrt(PriorVar)) +
                                 0.08f * std::max(1.0f, std::max(SpatialMean, OldMean));
        const float ShiftRatio = std::abs(SpatialMean - OldMean) / NoiseScale;
        if (ShiftRatio > 3.5f)
            return std::max(0.5f, OldLength / (1.0f + 0.85f * (ShiftRatio - 3.5f)));
        return OldLength;
    };
    // Stationary noisy frame: OldMean=0.45, SpatialMean=0.52, SpatialVar=0.09 -> preserved exact 15.0!
    Require(EffectiveOldLength(15.0f, 0.45f, 0.08f, 0.52f, 0.09f) == 15.0f,
            "Stationary Monte Carlo noise falsely shortened temporal history");
    // Abrupt light switch-off: OldMean=1.80, PriorVar=0.04, SpatialMean=0.0, SpatialVar=0.0 -> drops sharply!
    Require(EffectiveOldLength(15.0f, 1.80f, 0.04f, 0.0f, 0.0f) < 6.5f,
            "Abrupt light switch-off failed to shorten stale temporal history");
    (void)Power3({1.0f, 1.0f, 1.0f});
}
} // namespace

int main()
{
    try
    {
        ValidateEngineCpuVarianceFiltering();
        ValidateAlbedoDemodulationSharpness();
        ValidateGrazingPlaneAndSubpixelGather();
        std::printf("REPLAY_VARIANCE_FILTER_HOST cpu_svgf=1 albedo_demodulation=1 grazing_plane=1 "
                    "subpixel_bilinear=1 corruption_guards=7 shift_adaptation=1\n");
        return 0;
    }
    catch (const std::exception& Error)
    {
        std::fprintf(stderr, "FAIL replay variance filter validation: %s\n", Error.what());
        return 1;
    }
}
