//============================================================================================================================================
//                                                       DENOISESTREAMS.H
//============================================================================================================================================
// 🧩 The §E measurement, shared by the two things that make claims about it:
//      · DenoiseReprojectionProof.cpp — the gate's E1–E4c checks;
//      · DenoiseExhibit.cpp           — the chart sheet that draws the same numbers.
//
//    Nothing here is a model of the filter. The streams are the *input* — three zero-mean, unit-scale noise
//    distributions shaped like a diffuse, a glass/BTDF and a subsurface estimator's own — and everything downstream of
//    them is the shipped code: `DenoiseMirror::Accumulator` is ReSTIRViewport.slang's running mean + first two
//    luminance moments, `DenoiseMirror::Run` is AtrousDenoise.slang's compiled `main()`, and `EarlyOutAccepts` is the
//    shader's 3×3 pre-filtered variance test. Sharing the whole measurement (not just the stream definitions) is what
//    makes the chart and the gate the same experiment rather than two similar-looking ones.
//
//    Amplitude model, decided the hard way: one sample per frame with the SAME per-sample noise every frame, i.e. the
//    physics. What falls as the frame is held is the variance OF THE MEAN (s²/N) — which is the quantity the shader
//    keys on. A decrescent per-frame amplitude was tried first and rejected: it hands the filter a convergence the
//    renderer never earned and, at these magnitudes, drives the shader's fp32 moment recursion below its own ulp.

#pragma once

#include "AtrousDenoiseMirror.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace DenoiseStreams {

// A square image of 4-float texels: xyz = radiance, w = variance (of the mean) or depth (surface fields).
struct Field
{
    uint32_t Extent = 0u;
    std::vector<float> Values;

    void Resize(uint32_t N) { Extent = N; Values.assign(static_cast<size_t>(N) * N * 4u, 0.0f); }
    float*       At(uint32_t X, uint32_t Y)       { return &Values[(static_cast<size_t>(Y) * Extent + X) * 4u]; }
    const float* At(uint32_t X, uint32_t Y) const { return &Values[(static_cast<size_t>(Y) * Extent + X) * 4u]; }
};

struct Pcg
{
    uint32_t State = 0u;
    explicit Pcg(uint32_t Seed) : State(Seed * 747796405u + 2891336453u) {}
    uint32_t NextBits()
    {
        State = State * 747796405u + 2891336453u;
        const uint32_t Word = ((State >> ((State >> 28u) + 4u)) ^ State) * 277803737u;
        return (Word >> 22u) ^ Word;
    }
    float Uniform() { return static_cast<float>(NextBits() >> 8u) * (1.0f / 16777216.0f); }
};

// The shipped convergence test, transcribed from AtrousDenoise.slang: the 3×3 pre-filtered variance against
//    kEarlyOutVariance. This is how "identity at convergence" can be asked pixel by pixel of a per-pixel test.
inline bool EarlyOutAccepts(const Field& Source, uint32_t X, uint32_t Y)
{
    double VarianceSum = 0.0, VarianceWeight = 0.0;
    for (int32_t OY = -1; OY <= 1; ++OY)
        for (int32_t OX = -1; OX <= 1; ++OX)
        {
            const int32_t TapX = std::min(std::max(static_cast<int32_t>(X) + OX, 0), static_cast<int32_t>(Source.Extent) - 1);
            const int32_t TapY = std::min(std::max(static_cast<int32_t>(Y) + OY, 0), static_cast<int32_t>(Source.Extent) - 1);
            const double W = static_cast<double>(DenoiseMirror::KernelWeight(OX)) * static_cast<double>(DenoiseMirror::KernelWeight(OY));
            VarianceSum += static_cast<double>(Source.At(static_cast<uint32_t>(TapX), static_cast<uint32_t>(TapY))[3]) * W;
            VarianceWeight += W;
        }
    const double LocalVariance = VarianceWeight > 0.0 ? std::max(VarianceSum / VarianceWeight, 0.0) : 0.0;
    return LocalVariance < static_cast<double>(DenoiseMirror::EarlyOutVariance());
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE STREAMS
//------------------------------------------------------------------------------------------------------------------------

enum class StreamCategory : uint32_t { Lambertian = 0u, Glass = 1u, Subsurface = 2u, Count = 3u };

inline const char* StreamName(StreamCategory Category)
{
    switch (Category)
    {
        case StreamCategory::Lambertian: return "lambertian";
        case StreamCategory::Glass:      return "glass-btdf";
        case StreamCategory::Subsurface: return "subsurface";
        default:                         return "?";
    }
}

// The spatial base each stream modulates: a smooth gradient, so the filter has real structure to preserve.
inline float StreamBase(uint32_t X, uint32_t Y, uint32_t Extent)
{
    return 0.35f + 0.30f * (static_cast<float>(X) / static_cast<float>(Extent))
                  + 0.10f * (static_cast<float>(Y) / static_cast<float>(Extent));
}

// The analytic mean each stream is unbiased towards — a property of the material, never of the frame's noise level.
inline float StreamMean(StreamCategory Category, float Base)
{
    switch (Category)
    {
        case StreamCategory::Lambertian: return Base;
        case StreamCategory::Glass:      return 0.62f * Base;   // 99 % dim paths + 1 % fireflies
        case StreamCategory::Subsurface: return 0.60f * Base;   // exponential transport, truncated at the first bounce
        default:                         return 0.0f;
    }
}

// Zero-mean, unit-scale noise shaped like each lobe's transport: uniform for diffuse, a rare large value for the
//    specular/BTDF chain (fireflies), exponential for subsurface.
inline void StreamSample(StreamCategory Category, float Base, float Amplitude, Pcg& Rng, float* OutSample, float* OutTruth)
{
    const float Mean = StreamMean(Category, Base);
    float Noise = 0.0f;
    switch (Category)
    {
        case StreamCategory::Lambertian:
            Noise = 2.0f * Rng.Uniform() - 1.0f;                    // uniform on [−1, 1]: E = 0, Var = 1/3
            break;
        case StreamCategory::Glass:
            // P(firefly) = 1 % : +60 with probability 0.01, −0.60606 otherwise (E = 0 exactly).
            Noise = Rng.Uniform() < 0.01f ? 60.0f : -0.6060606f;
            break;
        case StreamCategory::Subsurface:
            Noise = -std::log(std::max(Rng.Uniform(), 1.0e-6f)) - 1.0f;   // E = 0, Var = 1
            break;
        default:
            break;
    }
    OutSample[0] = OutSample[1] = OutSample[2] = Mean * (1.0f + Amplitude * Noise);
    OutTruth[0] = OutTruth[1] = OutTruth[2] = Mean;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE MEASUREMENT
//------------------------------------------------------------------------------------------------------------------------

constexpr float kSampleNoise = 0.03f;   // [-] 3 % per-sample radiance noise, ≈ a 33 spp frame

struct StreamMeasurement
{
    StreamCategory Category = StreamCategory::Lambertian;
    uint32_t Frames = 0u;
    uint32_t Holds[3] = { 512u, 2048u, 8192u };   // the sampling points of the fade-out curve

    // E1 / E2 / E2b — the frame-1 A/B and the mean drift at its two ends, in presentation and linear space.
    double FirstFrameFilteredError = 0.0;
    double FirstFrameUnfilteredError = 0.0;
    double FirstFrameDrift = 0.0;
    double FinalFrameDrift = 0.0;

    // E3 — the running mean against the analytic truth, at the reporting mark.
    double FinalLinearError = 0.0;
    double FinalTruth = 0.0;

    // E4 / E4b / E4c — the early-out, per hold: how much of the frame it accepts, how many of those the filter moved
    //    at all, and the mean squared error of the filtered and unfiltered presentations.
    double MilestoneAcceptance[3] = { 0.0, 0.0, 0.0 };
    uint32_t MilestoneAccepted[3] = { 0u, 0u, 0u };
    uint32_t MilestoneBad[3] = { 0u, 0u, 0u };
    double MilestoneMseFiltered[3] = { 0.0, 0.0, 0.0 };
    double MilestoneMseRaw[3] = { 0.0, 0.0, 0.0 };
    uint32_t AcceptedAtFirstFrame = 0u;
};

// One full run of the §E experiment: `Frames` frames of one 1-spp sample each, accumulated by the shipped recursion,
//    filtered every frame by the shipped chain, and measured. Deterministic — same seeds, same numbers, every time.
inline StreamMeasurement MeasureStream(StreamCategory Category, uint32_t Extent, uint32_t Frames,
                                       const uint32_t Holds[3], Field* OutFinalSource = nullptr,
                                       std::vector<float>* OutFinalFilteredPresentation = nullptr)
{
    StreamMeasurement Result;
    Result.Category = Category;
    Result.Frames = Frames;
    for (int I = 0; I < 3; ++I) Result.Holds[I] = Holds[I];

    std::vector<DenoiseMirror::Accumulator> Accumulators(static_cast<size_t>(Extent) * Extent);
    Field Source, Surface, Filtered, FilteredOutput, Truth;
    Source.Resize(Extent); Surface.Resize(Extent); Filtered.Resize(Extent); FilteredOutput.Resize(Extent); Truth.Resize(Extent);
    for (uint32_t Y = 0u; Y < Extent; ++Y)
        for (uint32_t X = 0u; X < Extent; ++X)
        {
            float* F = Surface.At(X, Y);
            F[0] = 0.0f; F[1] = 0.0f; F[2] = 1.0f; F[3] = 1.0f;
        }
    DenoiseMirror::RunConfiguration Config;
    Config.Extent = Extent; Config.StepSize = 1u; Config.Enabled = true; Config.FinalLevel = true;
    Config.Exposure = 1.0f; Config.ColourSaturation = 1.0f;

    const double PixelCount = static_cast<double>(Extent) * Extent;
    const uint32_t ReportingMark = Holds[0];

    for (uint32_t Frame = 0u; Frame < Frames; ++Frame)
    {
        for (uint32_t Y = 0u; Y < Extent; ++Y)
            for (uint32_t X = 0u; X < Extent; ++X)
            {
                const float Base = StreamBase(X, Y, Extent);
                Pcg Rng(0x9E3779B9u ^ (Frame * 2654435761u) ^ (Y * Extent + X) * 40503u);
                float Sample[3], Analytic[3];
                StreamSample(Category, Base, kSampleNoise, Rng, Sample, Analytic);
                float Radiance[3], Variance = 0.0f;
                Accumulators[static_cast<size_t>(Y) * Extent + X].Resolve(Sample, Radiance, &Variance);
                float* S = Source.At(X, Y);
                S[0] = Radiance[0]; S[1] = Radiance[1]; S[2] = Radiance[2]; S[3] = Variance;
                float* T = Truth.At(X, Y);
                T[0] = Analytic[0]; T[1] = Analytic[1]; T[2] = Analytic[2];
            }

        DenoiseMirror::Run(Config, Source.Values.data(), Surface.Values.data(), Filtered.Values.data(), FilteredOutput.Values.data());

        double FilteredError = 0.0, UnfilteredError = 0.0, FilteredMean = 0.0, UnfilteredMean = 0.0;
        for (uint32_t Y = 0u; Y < Extent; ++Y)
            for (uint32_t X = 0u; X < Extent; ++X)
            {
                float UnfilteredPresentation[3];
                const float* S = Source.At(X, Y);
                DenoiseMirror::ToneMap(S[0], S[1], S[2], Config.Exposure, Config.ColourSaturation, UnfilteredPresentation);
                float TruthPresentation[3];
                const float* T = Truth.At(X, Y);
                DenoiseMirror::ToneMap(T[0], T[1], T[2], Config.Exposure, Config.ColourSaturation, TruthPresentation);

                const float* O = FilteredOutput.At(X, Y);
                for (uint32_t C = 0u; C < 3u; ++C)
                {
                    const double Delta = static_cast<double>(O[C] - TruthPresentation[C]);
                    FilteredError += Delta * Delta;
                    const double DeltaUnfiltered = static_cast<double>(UnfilteredPresentation[C] - TruthPresentation[C]);
                    UnfilteredError += DeltaUnfiltered * DeltaUnfiltered;
                }
                FilteredMean   += Filtered.At(X, Y)[0] + Filtered.At(X, Y)[1] + Filtered.At(X, Y)[2];
                UnfilteredMean += S[0] + S[1] + S[2];
                if (Frame == 0u)
                {
                    for (uint32_t C = 0u; C < 3u; ++C)
                    {
                        const double Delta = static_cast<double>(O[C] - TruthPresentation[C]);
                        Result.FirstFrameFilteredError += Delta * Delta;
                        const double DeltaUnfiltered = static_cast<double>(UnfilteredPresentation[C] - TruthPresentation[C]);
                        Result.FirstFrameUnfilteredError += DeltaUnfiltered * DeltaUnfiltered;
                    }
                }
                if (Frame == ReportingMark - 1u)
                {
                    Result.FinalLinearError += std::fabs(static_cast<double>(S[0] - T[0]));
                    Result.FinalTruth += static_cast<double>(T[0]);
                }
                if (Frame == 0u && EarlyOutAccepts(Source, X, Y)) ++Result.AcceptedAtFirstFrame;
            }

        const double Drift = std::fabs(FilteredMean - UnfilteredMean) / std::max(UnfilteredMean, 1.0e-9);
        if (Frame == 0u) Result.FirstFrameDrift = Drift;
        if (Frame == ReportingMark - 1u) Result.FinalFrameDrift = Drift;

        // The fade-out curve. At each hold the shipped convergence test is asked, pixel by pixel, whether it fires —
        //    and every pixel it accepts is checked against the tone map the kernel would have written itself.
        for (uint32_t M = 0u; M < 3u; ++M)
        {
            if (Frame + 1u != Holds[M]) continue;
            uint32_t Accepted = 0u, Bad = 0u;
            for (uint32_t Y = 0u; Y < Extent; ++Y)
                for (uint32_t X = 0u; X < Extent; ++X)
                {
                    if (!EarlyOutAccepts(Source, X, Y)) continue;
                    ++Accepted;
                    const float* O = FilteredOutput.At(X, Y);
                    const float* S = Source.At(X, Y);
                    float Presented[3];
                    DenoiseMirror::ToneMap(S[0], S[1], S[2], Config.Exposure, Config.ColourSaturation, Presented);
                    bool Same = true;
                    for (uint32_t C = 0u; C < 3u; ++C) Same = Same && O[C] == Presented[C];
                    if (!Same) ++Bad;
                }
            Result.MilestoneAcceptance[M] = static_cast<double>(Accepted) / PixelCount;
            Result.MilestoneAccepted[M] = Accepted;
            Result.MilestoneBad[M] = Bad;
            Result.MilestoneMseFiltered[M] = FilteredError / (PixelCount * 3.0);
            Result.MilestoneMseRaw[M] = UnfilteredError / (PixelCount * 3.0);
        }
    }

    if (OutFinalSource != nullptr) *OutFinalSource = Source;
    if (OutFinalFilteredPresentation != nullptr) *OutFinalFilteredPresentation = FilteredOutput.Values;
    return Result;
}

} // namespace DenoiseStreams
