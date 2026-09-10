//============================================================================================================================================
// 📦 Scratchpad/AtrousLevelCost.cpp — what each à-trous level buys, and what it costs
//============================================================================================================================================
// #8 tier-keys kDenoiseLevelCount, which was a fixed 5 for every quality tier — the same mistake #4 had, where the
//    cheapest tier paid the most expensive setting. Choosing the ladder needs two numbers per level count: the work
//    issued, and the noise left behind. A cheaper filter that leaves an unusable image is not a saving.
//
// This harness transcribes AtrousDenoise.slang's filter — the same 5×5 B-spline kernel at 2^level spacing, the same
//    normal / depth / luminance edge stoppers, the same 3×3 pre-filtered variance denominator — and runs it over a
//    synthetic G-buffer whose ground truth is known exactly. Knowing the truth is the point: it turns "looks
//    smoother" into a measured RMSE against the noise-free image.
//
// ⚠️ Not a frame-time measurement. There is no GPU in this sandbox. What is exact here is the DISPATCH COUNT and the
//    per-pixel tap count (both are pure functions of the level count), and the RESIDUAL ERROR, which is a property
//    of the filter's algebra and so transfers to the GPU implementation that shares it.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace
{

constexpr uint32_t kWidth  = 320u;
constexpr uint32_t kHeight = 240u;
constexpr float    kPi     = 3.14159265358979f;

// Push-constant equivalents, matching the values SwapchainExchange.cpp sets.
constexpr float kNormalPower    = 64.0f;
constexpr float kDepthScale     = 0.05f;
constexpr float kLuminanceScale = 4.0f;

uint32_t PcgHash(uint32_t Input)
{
    uint32_t State = Input * 747796405u + 2891336453u;
    uint32_t Word  = ((State >> ((State >> 28u) + 4u)) ^ State) * 277803737u;
    return (Word >> 22u) ^ Word;
}
float RandFloat(uint32_t& Seed)
{
    Seed = PcgHash(Seed);
    return static_cast<float>(Seed) * (1.0f / 4294967296.0f);
}

struct Pixel { float R = 0.0f, G = 0.0f, B = 0.0f, Variance = 0.0f; };
struct Surface { float Nx = 0.0f, Ny = 0.0f, Nz = 0.0f, Depth = 0.0f; };   // Depth <= 0 = background

float Luminance(float R, float G, float B) { return 0.2126f * R + 0.7152f * G + 0.0722f * B; }

// The B-spline row the shader uses: (1, 4, 6, 4, 1) / 16, indexed -2..2.
float KernelWeight(int I)
{
    switch (I) { case -2: case 2: return 0.0625f; case -1: case 1: return 0.25f; default: return 0.375f; }
}

// ── The scene: three flat-shaded quadrants at different depths, plus a lit gradient ────────────────────────────
//    Flat regions are where a denoiser should excel; the boundaries between them are the edges it must not cross.
void BuildTruth(std::vector<Pixel>& Truth, std::vector<Surface>& Surfaces)
{
    Truth.assign(static_cast<size_t>(kWidth) * kHeight, {});
    Surfaces.assign(static_cast<size_t>(kWidth) * kHeight, {});
    for (uint32_t Y = 0u; Y < kHeight; ++Y)
        for (uint32_t X = 0u; X < kWidth; ++X)
        {
            const size_t I = static_cast<size_t>(Y) * kWidth + X;
            const float U = static_cast<float>(X) / static_cast<float>(kWidth);
            const float V = static_cast<float>(Y) / static_cast<float>(kHeight);

            // A circular "object" over a background plane, and a vertical wall on the right third.
            const float Cx = U - 0.38f, Cy = V - 0.55f;
            const bool InCircle = (Cx * Cx + Cy * Cy) < (0.22f * 0.22f);
            const bool InWall   = U > 0.72f;

            Surface S;
            if (InCircle)     { S.Nx = 0.0f; S.Ny = 0.0f; S.Nz = 1.0f; S.Depth = 2.0f; }
            else if (InWall)  { S.Nx = -1.0f; S.Ny = 0.0f; S.Nz = 0.0f; S.Depth = 6.0f; }
            else              { S.Nx = 0.0f; S.Ny = 0.6f; S.Nz = 0.8f; S.Depth = 4.0f + 3.0f * V; }
            Surfaces[I] = S;

            Pixel P;
            if (InCircle)     { P.R = 0.62f; P.G = 0.58f; P.B = 0.50f; }
            else if (InWall)  { P.R = 0.18f; P.G = 0.42f; P.B = 0.22f; }
            else              { P.R = 0.34f + 0.22f * V; P.G = 0.31f + 0.20f * V; P.B = 0.40f + 0.18f * V; }
            // A soft lighting gradient so the filter has real low-frequency signal to preserve.
            const float Falloff = 0.75f + 0.25f * std::cos((U - 0.5f) * kPi);
            P.R *= Falloff; P.G *= Falloff; P.B *= Falloff;
            Truth[I] = P;
        }
}

// Monte-Carlo noise of the kind a path tracer leaves: multiplicative, heavy-tailed, with occasional firefliers.
void AddNoise(const std::vector<Pixel>& Truth, std::vector<Pixel>& Noisy, uint32_t Spp, uint32_t& Seed)
{
    Noisy = Truth;
    for (size_t I = 0u; I < Noisy.size(); ++I)
    {
        float Sum = 0.0f, SumSq = 0.0f;
        const uint32_t Samples = std::max(Spp, 1u);
        for (uint32_t S = 0u; S < Samples; ++S)
        {
            // Exponential-ish estimator: mean 1, high variance, rare large values.
            const float U = std::max(RandFloat(Seed), 1e-6f);
            float Estimate = -std::log(U);
            if (RandFloat(Seed) < 0.004f) Estimate *= 14.0f;    // fireflier
            Sum += Estimate; SumSq += Estimate * Estimate;
        }
        const float Mean = Sum / static_cast<float>(Samples);
        // Sample variance needs the Bessel-corrected estimator; at Samples == 1 the population form collapses to
        //    exactly 0, which makes every edge stopper treat the pixel as fully converged and the filter becomes a
        //    no-op — the 1 spp column read identically at every level until this was fixed. With one sample the
        //    variance is unknown, so fall back to the estimator's own magnitude, which is what a renderer does.
        float Var;
        if (Samples > 1u)
            Var = std::max(SumSq / static_cast<float>(Samples) - Mean * Mean, 0.0f)
                * static_cast<float>(Samples) / static_cast<float>(Samples - 1u)
                / static_cast<float>(Samples);
        else
            Var = Mean * Mean;
        Noisy[I].R = Truth[I].R * Mean;
        Noisy[I].G = Truth[I].G * Mean;
        Noisy[I].B = Truth[I].B * Mean;
        Noisy[I].Variance = Var * Luminance(Truth[I].R, Truth[I].G, Truth[I].B)
                                * Luminance(Truth[I].R, Truth[I].G, Truth[I].B);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//              ONE À-TROUS LEVEL — transcribed from AtrousDenoise.slang
//------------------------------------------------------------------------------------------------------------------------
// Returns the number of taps actually consumed, so the cost figure is measured rather than assumed.
uint64_t FilterLevel(const std::vector<Pixel>& Source, std::vector<Pixel>& Target,
                     const std::vector<Surface>& Surfaces, uint32_t StepSize,
                     bool VarianceEarlyOut, float EarlyOutThreshold, uint64_t& OutSkipped)
{
    uint64_t Taps = 0u;
    const int Step = static_cast<int>(StepSize);

    for (uint32_t Y = 0u; Y < kHeight; ++Y)
    {
        for (uint32_t X = 0u; X < kWidth; ++X)
        {
            const size_t I = static_cast<size_t>(Y) * kWidth + X;
            const Surface& CentreSurface = Surfaces[I];
            const Pixel&   Centre        = Source[I];

            if (CentreSurface.Depth <= 0.0f) { Target[I] = Centre; continue; }   // background passes through

            // 3×3 pre-filtered variance, the shader's luminance denominator.
            float VarianceSum = 0.0f, VarianceWeight = 0.0f;
            for (int Dy = -1; Dy <= 1; ++Dy)
                for (int Dx = -1; Dx <= 1; ++Dx)
                {
                    const int Tx = std::clamp(static_cast<int>(X) + Dx, 0, static_cast<int>(kWidth) - 1);
                    const int Ty = std::clamp(static_cast<int>(Y) + Dy, 0, static_cast<int>(kHeight) - 1);
                    const float W = KernelWeight(Dx) * KernelWeight(Dy);
                    VarianceSum    += Source[static_cast<size_t>(Ty) * kWidth + static_cast<size_t>(Tx)].Variance * W;
                    VarianceWeight += W;
                }
            const float LocalVariance = VarianceWeight > 0.0f ? std::max(VarianceSum / VarianceWeight, 0.0f) : 0.0f;

            // #9: a pixel whose neighbourhood has already converged gains nothing from a wider kernel. Skipping it
            //    keeps its current value — which is what the filter would converge to anyway — and saves 25 taps.
            if (VarianceEarlyOut && LocalVariance < EarlyOutThreshold)
            {
                Target[I] = Centre;
                ++OutSkipped;
                continue;
            }

            const float LuminanceDenominator = kLuminanceScale * std::sqrt(LocalVariance) + 1.0e-4f;
            const float CentreLuminance = Luminance(Centre.R, Centre.G, Centre.B);

            float SumR = 0.0f, SumG = 0.0f, SumB = 0.0f, VarianceOut = 0.0f, WeightSum = 0.0f;

            for (int Dy = -2; Dy <= 2; ++Dy)
            {
                for (int Dx = -2; Dx <= 2; ++Dx)
                {
                    const int Tx = static_cast<int>(X) + Dx * Step;
                    const int Ty = static_cast<int>(Y) + Dy * Step;
                    if (Tx < 0 || Ty < 0 || Tx >= static_cast<int>(kWidth) || Ty >= static_cast<int>(kHeight)) continue;

                    const size_t J = static_cast<size_t>(Ty) * kWidth + static_cast<size_t>(Tx);
                    const Surface& TapSurface = Surfaces[J];
                    if (TapSurface.Depth <= 0.0f) continue;
                    ++Taps;

                    const Pixel& TapColour = Source[J];
                    const float Base = KernelWeight(Dx) * KernelWeight(Dy);

                    const float NdotN = std::max(CentreSurface.Nx * TapSurface.Nx
                                               + CentreSurface.Ny * TapSurface.Ny
                                               + CentreSurface.Nz * TapSurface.Nz, 0.0f);
                    const float NormalWeight = std::pow(NdotN, kNormalPower);

                    const float DepthDelta = std::fabs(CentreSurface.Depth - TapSurface.Depth);
                    const float DepthSpan  = kDepthScale * CentreSurface.Depth
                                           * static_cast<float>(std::max(std::abs(Dx), std::abs(Dy)) * Step) + 1.0e-3f;
                    const float DepthWeight = std::exp(-DepthDelta / DepthSpan);

                    const float LuminanceDelta  = std::fabs(CentreLuminance - Luminance(TapColour.R, TapColour.G, TapColour.B));
                    const float LuminanceWeight = std::exp(-LuminanceDelta / LuminanceDenominator);

                    const float Weight = Base * NormalWeight * DepthWeight * LuminanceWeight;
                    SumR += TapColour.R * Weight; SumG += TapColour.G * Weight; SumB += TapColour.B * Weight;
                    VarianceOut += TapColour.Variance * Weight * Weight;
                    WeightSum += Weight;
                }
            }

            if (WeightSum > 1.0e-8f)
            {
                Target[I].R = SumR / WeightSum; Target[I].G = SumG / WeightSum; Target[I].B = SumB / WeightSum;
                Target[I].Variance = VarianceOut / (WeightSum * WeightSum);
            }
            else Target[I] = Centre;
        }
    }
    return Taps;
}

struct Score
{
    double   Rmse = 0.0;
    uint64_t Taps = 0u;
    uint64_t Skipped = 0u;
    uint32_t Dispatches = 0u;
    std::vector<Pixel> Final;      // the chain's output, so an early-out run can be diffed against a full one
};

Score RunChain(const std::vector<Pixel>& Noisy, const std::vector<Pixel>& Truth,
               const std::vector<Surface>& Surfaces, uint32_t Levels,
               bool VarianceEarlyOut, float Threshold, uint32_t MinEarlyOutLevel)
{
    std::vector<Pixel> A = Noisy, B(Noisy.size());
    Score Result;
    for (uint32_t Level = 0u; Level < Levels; ++Level)
    {
        Result.Taps += FilterLevel(A, B, Surfaces, 1u << Level,
                                   VarianceEarlyOut && Level >= MinEarlyOutLevel, Threshold, Result.Skipped);
        A.swap(B);
        ++Result.Dispatches;
    }
    Result.Final = A;
    double SumSq = 0.0;
    uint64_t Count = 0u;
    for (size_t I = 0u; I < A.size(); ++I)
    {
        if (Surfaces[I].Depth <= 0.0f) continue;
        const double Dr = A[I].R - Truth[I].R, Dg = A[I].G - Truth[I].G, Db = A[I].B - Truth[I].B;
        SumSq += (Dr * Dr + Dg * Dg + Db * Db) / 3.0;
        ++Count;
    }
    Result.Rmse = std::sqrt(SumSq / static_cast<double>(std::max<uint64_t>(Count, 1u)));
    return Result;
}

}   // namespace

int main(int argc, char** argv)
{
    bool  EarlyOut  = false;
    uint32_t MinEarlyOutLevel = 0u;
    // Matches kEarlyOutVariance in AtrousDenoise.slang: sigma = one fifth of an 8-bit step.
    float Threshold = (0.2f / 255.0f) * (0.2f / 255.0f);
    uint32_t Spp = 4u;
    for (int I = 1; I < argc; ++I)
    {
        const std::string_view Arg = argv[I];
        if      (Arg.rfind("earlyout=",  0) == 0) EarlyOut  = std::atoi(argv[I] + 9) != 0;
        else if (Arg.rfind("threshold=", 0) == 0) Threshold = static_cast<float>(std::atof(argv[I] + 10));
        else if (Arg.rfind("minlevel=",  0) == 0) MinEarlyOutLevel = static_cast<uint32_t>(std::atoi(argv[I] + 9));
        else if (Arg.rfind("spp=",       0) == 0) Spp       = static_cast<uint32_t>(std::atoi(argv[I] + 4));
    }

    std::vector<Pixel>   Truth, Noisy;
    std::vector<Surface> Surfaces;
    BuildTruth(Truth, Surfaces);
    uint32_t Seed = 0x1234567u;
    AddNoise(Truth, Noisy, Spp, Seed);

    const Score Unfiltered = RunChain(Noisy, Truth, Surfaces, 0u, false, 0.0f, 0u);

    std::printf("[AtrousLevels] %ux%u, %u spp of synthetic path-tracer noise%s\n",
                kWidth, kHeight, Spp, EarlyOut ? ", variance early-out ON" : "");
    if (EarlyOut) std::printf("[AtrousLevels] early-out threshold %.6g\n", Threshold);
    std::printf("[AtrousLevels] unfiltered RMSE %.5f\n\n", Unfiltered.Rmse);
    std::printf("  %8s %11s %14s %12s %10s\n", "levels", "dispatches", "taps", "RMSE", "vs 5-level");

    Score Five{};
    for (uint32_t Levels = 0u; Levels <= 5u; ++Levels)
    {
        const Score S = RunChain(Noisy, Truth, Surfaces, Levels, EarlyOut, Threshold, MinEarlyOutLevel);
        if (Levels == 5u) Five = S;
    }
    for (uint32_t Levels = 0u; Levels <= 5u; ++Levels)
    {
        const Score S = RunChain(Noisy, Truth, Surfaces, Levels, EarlyOut, Threshold, MinEarlyOutLevel);
        const double Ratio = Five.Rmse > 0.0 ? S.Rmse / Five.Rmse : 1.0;
        std::printf("  %8u %11u %14llu %12.5f %9.2fx", Levels, S.Dispatches,
                    static_cast<unsigned long long>(S.Taps), S.Rmse, Ratio);
        if (EarlyOut && S.Skipped > 0u)
            std::printf("   (%llu px skipped)", static_cast<unsigned long long>(S.Skipped));

        // The question that decides whether the early-out is honest: how far does a SKIPPED pixel end up from
        //    where the full filter would have put it? Measured in 8-bit display steps, because a deviation under
        //    half a step cannot survive quantisation into the presentation image and is therefore invisible.
        if (EarlyOut)
        {
            const Score Reference = RunChain(Noisy, Truth, Surfaces, Levels, false, 0.0f, 0u);
            double WorstStep = 0.0, SumStep = 0.0;
            for (size_t I = 0u; I < S.Final.size(); ++I)
            {
                const double D = std::max({ std::fabs(S.Final[I].R - Reference.Final[I].R),
                                            std::fabs(S.Final[I].G - Reference.Final[I].G),
                                            std::fabs(S.Final[I].B - Reference.Final[I].B) }) * 255.0;
                WorstStep = std::max(WorstStep, D);
                SumStep  += D;
            }
            std::printf("   worst %.3f / mean %.4f 8-bit steps vs full",
                        WorstStep, SumStep / static_cast<double>(S.Final.size()));
        }
        std::printf("\n");
    }
    return 0;
}
