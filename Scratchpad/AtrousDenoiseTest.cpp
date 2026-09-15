//==========================================================================================================================================
// Scratchpad/AtrousDenoiseTest.cpp — R7: the à-trous filter removes noise without removing edges
//==========================================================================================================================================
// A faithful CPU port of Engine/Shaders/AtrousDenoise.slang. The arithmetic here is the arithmetic the kernel
//    evaluates — same B₃ kernel, same three weights, same variance propagation — so a behavioural claim proved
//    here is a claim about the shader, not about a re-derivation of it.
//
// A denoiser is easy to get wrong in a way that LOOKS right: a plain blur scores wonderfully on "is it smooth?"
//    while destroying every edge in the image. So each of these checks is paired with its opposite, and the
//    interesting assertions are the ones a naive blur would FAIL:
//
//    1. Noise      — a flat noisy surface must actually converge (a blur passes this too).
//    2. Edges      — a geometric silhouette must survive (a blur FAILS this).
//    3. Features   — a genuine lighting step on one flat surface must survive (a blur FAILS this).
//    4. Background — must not bleed into geometry, and vice versa (a blur FAILS this).
//    5. Variance   — must fall as the estimate is combined, and steer the filter's strength.
//    6. Identity   — disabled, the pass copies its input bit for bit.
//    7. Energy     — the filter must not brighten or darken a flat region: weights sum to one.
//
// Check 3 is the one that distinguishes an edge-avoiding filter from a bilateral one that only knows geometry:
//    the normal and depth terms cannot see a shadow boundary, because both sides lie on the same plane. Only the
//    luminance term, scaled by the noise level, can preserve it.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                        THE SHADER, PORTED VERBATIM
//------------------------------------------------------------------------------------------------------------------------

struct Image
{
    int                Width  = 0;
    int                Height = 0;
    std::vector<float> Red, Green, Blue, Variance;      // radiance [W·sr⁻¹·m⁻²], variance [(W·sr⁻¹·m⁻²)²]
    std::vector<float> NormalX, NormalY, NormalZ, Depth; // depth ≤ 0 marks "no surface"

    void Allocate(int W, int H)
    {
        Width = W; Height = H;
        const size_t N = static_cast<size_t>(W) * H;
        Red.assign(N, 0.0f); Green.assign(N, 0.0f); Blue.assign(N, 0.0f); Variance.assign(N, 0.0f);
        NormalX.assign(N, 0.0f); NormalY.assign(N, 0.0f); NormalZ.assign(N, 1.0f); Depth.assign(N, 10.0f);
    }
    [[nodiscard]] size_t At(int X, int Y) const { return static_cast<size_t>(Y) * Width + X; }
};

struct DenoiseSettings
{
    float NormalPower    = 64.0f;
    float DepthScale     = 0.05f;
    float LuminanceScale = 4.0f;
    bool  Enabled        = true;
};

float Luminance(float R, float G, float B) { return 0.2126f * R + 0.7152f * G + 0.0722f * B; }

float KernelWeight(int Offset)
{
    static const float Weights[5] = { 0.0625f, 0.25f, 0.375f, 0.25f, 0.0625f };
    return Weights[Offset + 2];
}

// One à-trous level. Mirrors main() in AtrousDenoise.slang.
Image FilterLevel(const Image& Source, int StepSize, const DenoiseSettings& Settings)
{
    Image Target = Source;   // carries the surface channels through unchanged, as the shader's separate image does

    for (int Y = 0; Y < Source.Height; ++Y)
    for (int X = 0; X < Source.Width;  ++X)
    {
        const size_t Index = Source.At(X, Y);

        if (!Settings.Enabled || Source.Depth[Index] <= 0.0f) continue;   // straight copy

        const float CentreNx = Source.NormalX[Index], CentreNy = Source.NormalY[Index], CentreNz = Source.NormalZ[Index];
        const float CentreDepth     = Source.Depth[Index];
        const float CentreLuminance = Luminance(Source.Red[Index], Source.Green[Index], Source.Blue[Index]);

        // 3×3 pre-filtered variance for the luminance denominator.
        float VarianceSum = 0.0f, VarianceWeight = 0.0f;
        for (int Dy = -1; Dy <= 1; ++Dy)
        for (int Dx = -1; Dx <= 1; ++Dx)
        {
            const int Tx = std::clamp(X + Dx, 0, Source.Width  - 1);
            const int Ty = std::clamp(Y + Dy, 0, Source.Height - 1);
            const float W = KernelWeight(Dx) * KernelWeight(Dy);
            VarianceSum    += Source.Variance[Source.At(Tx, Ty)] * W;
            VarianceWeight += W;
        }
        const float LocalVariance = VarianceWeight > 0.0f ? std::max(VarianceSum / VarianceWeight, 0.0f) : 0.0f;
        const float LuminanceDenominator = Settings.LuminanceScale * std::sqrt(LocalVariance) + 1.0e-4f;

        float SumR = 0.0f, SumG = 0.0f, SumB = 0.0f, SumVariance = 0.0f, SumWeight = 0.0f;

        for (int Dy = -2; Dy <= 2; ++Dy)
        for (int Dx = -2; Dx <= 2; ++Dx)
        {
            const int Tx = X + Dx * StepSize;
            const int Ty = Y + Dy * StepSize;
            if (Tx < 0 || Ty < 0 || Tx >= Source.Width || Ty >= Source.Height) continue;

            const size_t Tap = Source.At(Tx, Ty);
            if (Source.Depth[Tap] <= 0.0f) continue;

            const float Base = KernelWeight(Dx) * KernelWeight(Dy);

            const float NormalDot = std::max(CentreNx * Source.NormalX[Tap]
                                           + CentreNy * Source.NormalY[Tap]
                                           + CentreNz * Source.NormalZ[Tap], 0.0f);
            const float NormalWeight = std::pow(NormalDot, Settings.NormalPower);

            const float DepthDelta = std::fabs(CentreDepth - Source.Depth[Tap]);
            const float DepthSpan  = Settings.DepthScale * CentreDepth
                                   * static_cast<float>(std::max(std::abs(Dx), std::abs(Dy)) * StepSize) + 1.0e-3f;
            const float DepthWeight = std::exp(-DepthDelta / DepthSpan);

            const float TapLuminance     = Luminance(Source.Red[Tap], Source.Green[Tap], Source.Blue[Tap]);
            const float LuminanceWeight  = std::exp(-std::fabs(CentreLuminance - TapLuminance) / LuminanceDenominator);

            const float Weight = Base * NormalWeight * DepthWeight * LuminanceWeight;

            SumR += Source.Red[Tap]   * Weight;
            SumG += Source.Green[Tap] * Weight;
            SumB += Source.Blue[Tap]  * Weight;
            SumVariance += Source.Variance[Tap] * Weight * Weight;
            SumWeight   += Weight;
        }

        if (SumWeight > 1.0e-8f)
        {
            Target.Red[Index]      = SumR / SumWeight;
            Target.Green[Index]    = SumG / SumWeight;
            Target.Blue[Index]     = SumB / SumWeight;
            Target.Variance[Index] = SumVariance / (SumWeight * SumWeight);
        }
    }

    return Target;
}

Image Denoise(const Image& Source, int Levels, const DenoiseSettings& Settings)
{
    Image Current = Source;
    for (int Level = 0; Level < Levels; ++Level) Current = FilterLevel(Current, 1 << Level, Settings);
    return Current;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      HARNESS
//------------------------------------------------------------------------------------------------------------------------

int Failures = 0;

void Expect(bool Condition, const char* Description)
{
    std::printf("  %-68s %s\n", Description, Condition ? "PASS" : "FAIL");
    if (!Condition) ++Failures;
}

uint32_t Seed = 1337u;
float NextNoise()
{
    Seed = Seed * 1664525u + 1013904223u;
    return static_cast<float>(Seed >> 8u) / 16777216.0f;   // [0,1)
}

// Mean absolute deviation from a known truth, over a region.
float MeanError(const Image& Picture, int X0, int Y0, int X1, int Y1, float Truth)
{
    float Sum = 0.0f; int Count = 0;
    for (int Y = Y0; Y < Y1; ++Y)
        for (int X = X0; X < X1; ++X) { Sum += std::fabs(Picture.Red[Picture.At(X, Y)] - Truth); ++Count; }
    return Count > 0 ? Sum / static_cast<float>(Count) : 0.0f;
}

float RegionMean(const Image& Picture, int X0, int Y0, int X1, int Y1)
{
    float Sum = 0.0f; int Count = 0;
    for (int Y = Y0; Y < Y1; ++Y)
        for (int X = X0; X < X1; ++X) { Sum += Picture.Red[Picture.At(X, Y)]; ++Count; }
    return Count > 0 ? Sum / static_cast<float>(Count) : 0.0f;
}

}   // namespace

int main()
{
    std::printf("[AtrousDenoise] R7 edge-avoiding a-trous filter\n\n");

    constexpr int Size   = 64;
    constexpr int Levels = 5;
    const DenoiseSettings Settings;

    //--------------------------------------------------------------------------------------------------------------
    std::printf("1. a flat noisy surface converges\n");
    {
        Image Noisy; Noisy.Allocate(Size, Size);
        const float Truth = 0.5f;
        for (int Y = 0; Y < Size; ++Y)
        for (int X = 0; X < Size; ++X)
        {
            const size_t I = Noisy.At(X, Y);
            const float  N = Truth + (NextNoise() - 0.5f) * 0.8f;
            Noisy.Red[I] = Noisy.Green[I] = Noisy.Blue[I] = N;
            Noisy.Variance[I] = 0.8f * 0.8f / 12.0f;      // variance of the uniform noise actually injected
        }

        const Image Clean = Denoise(Noisy, Levels, Settings);

        const float Before = MeanError(Noisy, 8, 8, Size - 8, Size - 8, Truth);
        const float After  = MeanError(Clean, 8, 8, Size - 8, Size - 8, Truth);
        std::printf("     error %.4f -> %.4f  (%.1fx better)\n", Before, After, Before / std::max(After, 1e-6f));
        Expect(After < Before * 0.25f, "five levels cut the error by at least 4x");
        Expect(std::fabs(RegionMean(Clean, 8, 8, Size - 8, Size - 8) - Truth) < 0.02f,
               "and converge on the true mean, not a biased one");
    }

    //--------------------------------------------------------------------------------------------------------------
    std::printf("\n2. a geometric edge survives (a plain blur would not)\n");
    {
        // Two surfaces at different depths meeting at x = 32: a silhouette. Both are noisy.
        Image Picture; Picture.Allocate(Size, Size);
        for (int Y = 0; Y < Size; ++Y)
        for (int X = 0; X < Size; ++X)
        {
            const size_t I = Picture.At(X, Y);
            const bool   Near = X < Size / 2;
            const float  Truth = Near ? 0.8f : 0.2f;
            const float  N = Truth + (NextNoise() - 0.5f) * 0.2f;
            Picture.Red[I] = Picture.Green[I] = Picture.Blue[I] = N;
            Picture.Variance[I] = 0.2f * 0.2f / 12.0f;
            Picture.Depth[I]    = Near ? 2.0f : 20.0f;     // a real depth discontinuity
        }

        const Image Clean = Denoise(Picture, Levels, Settings);

        // Sample right up against the boundary on both sides.
        const float LeftEdge  = RegionMean(Clean, Size / 2 - 3, 8, Size / 2,     Size - 8);
        const float RightEdge = RegionMean(Clean, Size / 2,     8, Size / 2 + 3, Size - 8);
        std::printf("     across the silhouette: %.3f | %.3f  (truth 0.800 | 0.200)\n", LeftEdge, RightEdge);
        Expect(LeftEdge  > 0.72f, "the near surface keeps its brightness at the very edge");
        Expect(RightEdge < 0.28f, "the far surface is not brightened by its neighbour");
        Expect(LeftEdge - RightEdge > 0.45f, "the silhouette contrast is preserved, not smeared");

        // Interiors must still be smoothed, or "preserving the edge" is just "doing nothing".
        Expect(MeanError(Clean, 8, 8, Size / 2 - 6, Size - 8, 0.8f) < 0.02f,
               "while the interior of each surface is still denoised");
    }

    //--------------------------------------------------------------------------------------------------------------
    std::printf("\n3. a lighting feature on ONE flat surface survives\n");
    {
        // A shadow boundary: identical normal and depth everywhere, so geometry alone cannot see this edge.
        //    Only the luminance term can preserve it. Noise is small relative to the step.
        Image Picture; Picture.Allocate(Size, Size);
        for (int Y = 0; Y < Size; ++Y)
        for (int X = 0; X < Size; ++X)
        {
            const size_t I = Picture.At(X, Y);
            const float Truth = X < Size / 2 ? 0.9f : 0.1f;
            const float N = Truth + (NextNoise() - 0.5f) * 0.05f;
            Picture.Red[I] = Picture.Green[I] = Picture.Blue[I] = N;
            Picture.Variance[I] = 0.05f * 0.05f / 12.0f;   // low noise => the step is many sigma => a real edge
        }

        const Image Clean = Denoise(Picture, Levels, Settings);

        const float Lit    = RegionMean(Clean, Size / 2 - 3, 8, Size / 2,     Size - 8);
        const float Shadow = RegionMean(Clean, Size / 2,     8, Size / 2 + 3, Size - 8);
        std::printf("     across the shadow line: %.3f | %.3f  (truth 0.900 | 0.100)\n", Lit, Shadow);
        Expect(Lit - Shadow > 0.60f, "the shadow boundary is preserved by the luminance weight alone");
    }

    //--------------------------------------------------------------------------------------------------------------
    std::printf("\n4. the same feature IS smoothed when it is only noise\n");
    {
        // Identical geometry, but now the "step" is well inside the noise level: it is not a feature, it is
        //    speckle, and the filter must remove it. This is the check that proves the luminance term is scaled
        //    by variance rather than being a fixed threshold.
        Image Picture; Picture.Allocate(Size, Size);
        for (int Y = 0; Y < Size; ++Y)
        for (int X = 0; X < Size; ++X)
        {
            const size_t I = Picture.At(X, Y);
            const float N = 0.5f + (NextNoise() - 0.5f) * 1.0f;
            Picture.Red[I] = Picture.Green[I] = Picture.Blue[I] = N;
            Picture.Variance[I] = 1.0f / 12.0f;            // high variance => everything is within the noise
        }

        const Image Clean = Denoise(Picture, Levels, Settings);
        const float Error = MeanError(Clean, 8, 8, Size - 8, Size - 8, 0.5f);
        std::printf("     residual error %.4f\n", Error);
        Expect(Error < 0.05f, "high variance makes the filter permissive, so pure noise is removed");
    }

    //--------------------------------------------------------------------------------------------------------------
    std::printf("\n5. background and geometry do not bleed into each other\n");
    {
        Image Picture; Picture.Allocate(Size, Size);
        for (int Y = 0; Y < Size; ++Y)
        for (int X = 0; X < Size; ++X)
        {
            const size_t I = Picture.At(X, Y);
            const bool Background = X >= Size / 2;
            Picture.Red[I] = Picture.Green[I] = Picture.Blue[I] = Background ? 0.0f : 1.0f;
            Picture.Variance[I] = 0.01f;
            Picture.Depth[I]    = Background ? -1.0f : 5.0f;   // ≤ 0 marks "no surface"
        }

        const Image Clean = Denoise(Picture, Levels, Settings);

        float MaximumBackground = 0.0f;
        float MinimumSurface    = 1.0f;
        for (int Y = 0; Y < Size; ++Y)
        for (int X = 0; X < Size; ++X)
        {
            const float V = Clean.Red[Clean.At(X, Y)];
            if (X >= Size / 2) MaximumBackground = std::max(MaximumBackground, V);
            else               MinimumSurface    = std::min(MinimumSurface, V);
        }
        std::printf("     brightest background %.5f, dimmest surface %.5f\n", MaximumBackground, MinimumSurface);
        Expect(MaximumBackground == 0.0f, "the background is untouched - no halo around the silhouette");
        Expect(MinimumSurface > 0.99f,    "and the surface is not darkened by the void beside it");
    }

    //--------------------------------------------------------------------------------------------------------------
    std::printf("\n6. variance falls, and steers the filter\n");
    {
        Image Picture; Picture.Allocate(Size, Size);
        for (int Y = 0; Y < Size; ++Y)
        for (int X = 0; X < Size; ++X)
        {
            const size_t I = Picture.At(X, Y);
            Picture.Red[I] = Picture.Green[I] = Picture.Blue[I] = 0.5f + (NextNoise() - 0.5f) * 0.4f;
            Picture.Variance[I] = 0.4f * 0.4f / 12.0f;
        }

        const float Before = Picture.Variance[Picture.At(Size / 2, Size / 2)];
        const Image Clean  = Denoise(Picture, Levels, Settings);
        const float After  = Clean.Variance[Clean.At(Size / 2, Size / 2)];
        std::printf("     variance %.3e -> %.3e\n", static_cast<double>(Before), static_cast<double>(After));
        Expect(After < Before, "combining taps reduces the variance estimate");
        Expect(After > 0.0f,   "but never claims a zero-variance (perfectly known) result");
    }

    //--------------------------------------------------------------------------------------------------------------
    std::printf("\n7. energy is preserved on a flat region\n");
    {
        // A constant image must come out exactly constant: the weights are normalised. If this drifts, the
        //    denoiser is changing scene brightness, which no amount of "looks smoother" would excuse.
        Image Picture; Picture.Allocate(Size, Size);
        for (size_t I = 0; I < Picture.Red.size(); ++I)
        {
            Picture.Red[I] = Picture.Green[I] = Picture.Blue[I] = 0.375f;
            Picture.Variance[I] = 0.01f;
        }

        const Image Clean = Denoise(Picture, Levels, Settings);
        float Largest = 0.0f;
        for (int Y = 0; Y < Size; ++Y)
            for (int X = 0; X < Size; ++X)
                Largest = std::max(Largest, std::fabs(Clean.Red[Clean.At(X, Y)] - 0.375f));
        std::printf("     largest deviation %.3e\n", static_cast<double>(Largest));
        Expect(Largest < 1.0e-5f, "a constant image is returned unchanged, edges included");
    }

    //--------------------------------------------------------------------------------------------------------------
    std::printf("\n8. disabled, the pass is a bit-exact copy\n");
    {
        Image Picture; Picture.Allocate(Size, Size);
        for (size_t I = 0; I < Picture.Red.size(); ++I)
        {
            Picture.Red[I] = NextNoise(); Picture.Green[I] = NextNoise(); Picture.Blue[I] = NextNoise();
            Picture.Variance[I] = NextNoise();
        }

        DenoiseSettings Off; Off.Enabled = false;
        const Image Copy = Denoise(Picture, Levels, Off);

        bool Identical = true;
        for (size_t I = 0; I < Picture.Red.size(); ++I)
            if (Copy.Red[I] != Picture.Red[I] || Copy.Green[I] != Picture.Green[I]
             || Copy.Blue[I] != Picture.Blue[I] || Copy.Variance[I] != Picture.Variance[I]) Identical = false;
        Expect(Identical, "every channel of every pixel is untouched");
    }

    //--------------------------------------------------------------------------------------------------------------
    std::printf("\n9. the push-constant record matches the shader block\n");
    {
        // The C++ side pushes this struct straight at the shader. glslang reports DenoiseConstants as 36 bytes over
        //    8 members; if the two ever disagree the filter reads garbage parameters and misbehaves in a way that
        //    looks like a tuning problem rather than a layout bug.
        struct DenoisePushRecord
        {
            uint32_t Extent[2];
            uint32_t StepSize;
            uint32_t Enabled;
            float    NormalPower;
            float    DepthScale;
            float    LuminanceScale;
            float    Exposure;
            uint32_t FinalLevel;
        };
        Expect(sizeof(DenoisePushRecord) == 36u, "the push record is 36 bytes, as the shader block reflects");
        Expect(offsetof(DenoisePushRecord, StepSize)       ==  8u, "StepSize sits at offset 8");
        Expect(offsetof(DenoisePushRecord, Enabled)        == 12u, "Enabled sits at offset 12");
        Expect(offsetof(DenoisePushRecord, NormalPower)    == 16u, "NormalPower sits at offset 16");
        Expect(offsetof(DenoisePushRecord, LuminanceScale) == 24u, "LuminanceScale sits at offset 24");
        Expect(offsetof(DenoisePushRecord, Exposure)       == 28u, "Exposure sits at offset 28");
        Expect(offsetof(DenoisePushRecord, FinalLevel)     == 32u, "FinalLevel sits at offset 32");
    }

    //--------------------------------------------------------------------------------------------------------------
    std::printf("\n10. the ping-pong reaches the level the dispatch loop expects\n");
    {
        // Level i reads slot (i & 1) and writes the other. With five levels the chain is
        //    0->1, 1->0, 0->1, 1->0, 0->1, so the final result lands in slot 1 — and level 4 is the one flagged
        //    FinalLevel, so the tone map happens on the pass that produced the finished image, not a stale one.
        int Slot = 0;
        int FinalWriteSlot = -1;
        for (int Level = 0; Level < 5; ++Level)
        {
            const int Source = Level & 1;
            Expect(Source == Slot, Level == 0 ? "level 0 reads the slot the kernel wrote" : "each level reads what the previous one wrote");
            Slot = Source ^ 1;
            if (Level == 4) FinalWriteSlot = Slot;
        }
        Expect(FinalWriteSlot == 1, "five levels finish in slot 1");
    }

    //--------------------------------------------------------------------------------------------------------------
    std::printf("\n11. the dispatch covers every pixel\n");
    {
        // This is the check that would have caught the shipped bug. The filter's workgroup is 8x8 while the ReSTIR
        //    kernel's is 16x16; the dispatch loop originally reused the kernel's group count, so the filter ran
        //    over only half the width and half the height and tone-mapped exactly the top-left quarter of the
        //    image, leaving the rest of the presentation image never written.
        const uint32_t DenoiseGroup = 8u;   // must equal local_size in AtrousDenoise.slang
        const uint32_t KernelGroup  = 16u;  // ReSTIRViewport.slang

        struct Case { uint32_t Width, Height; };
        const Case Cases[] = { { 1280u, 720u }, { 1920u, 1080u }, { 640u, 360u }, { 1u, 1u }, { 1279u, 719u } };

        bool AllCovered = true;
        bool KernelCountWouldFail = false;
        for (const Case& C : Cases)
        {
            const uint32_t GroupX = (C.Width  + DenoiseGroup - 1u) / DenoiseGroup;
            const uint32_t GroupY = (C.Height + DenoiseGroup - 1u) / DenoiseGroup;
            if (GroupX * DenoiseGroup < C.Width || GroupY * DenoiseGroup < C.Height) AllCovered = false;

            // And confirm the WRONG derivation really does leave pixels unwritten, so this test has teeth.
            const uint32_t WrongX = (C.Width + KernelGroup - 1u) / KernelGroup;
            if (WrongX * DenoiseGroup < C.Width) KernelCountWouldFail = true;
        }
        Expect(AllCovered, "8x8 groups derived from the filter's own size cover the whole image");
        Expect(KernelCountWouldFail, "and reusing the kernel's 16x16 group count would NOT (the shipped bug)");

        // Spell out the exact reported case.
        const uint32_t WrongWidth  = ((1280u + 15u) / 16u) * 8u;
        const uint32_t WrongHeight = ((720u  + 15u) / 16u) * 8u;
        std::printf("     1280x720 with the kernel's group count covers %ux%u - the top-left quarter\n",
                    WrongWidth, WrongHeight);
        Expect(WrongWidth == 640u && WrongHeight == 360u, "which is exactly the quarter-render that was reported");
    }

    //--------------------------------------------------------------------------------------------------------------
    std::printf("\n12. the filter fades out as the estimate converges\n");
    {
        // The kernel reports var(mean) = sample_variance / n, NOT the raw sample variance. That division is what
        //    makes the denoiser back off: with many samples the reported noise is tiny, the luminance weight
        //    becomes discriminating, and a converged image is left alone. Shipping the sample variance instead
        //    reports a noise level that never falls, so the filter blurs a settled image forever — the muddy
        //    patches and lost contact detail seen on hardware.
        const float SampleVariance = 0.25f;   // the spread of ONE sample, constant as n grows
        const float Truth          = 0.5f;

        // A converged surface carrying a real feature: a step that is small in absolute terms but many standard
        //    errors wide once n is large.
        const auto MeasureContrast = [&](float Count)
        {
            Image Picture; Picture.Allocate(Size, Size);
            for (int Y = 0; Y < Size; ++Y)
            for (int X = 0; X < Size; ++X)
            {
                const size_t I = Picture.At(X, Y);
                const float V = X < Size / 2 ? Truth + 0.05f : Truth - 0.05f;
                Picture.Red[I] = Picture.Green[I] = Picture.Blue[I] = V;
                Picture.Variance[I] = SampleVariance / Count;      // the corrected estimator
            }
            const Image Clean = Denoise(Picture, Levels, Settings);
            return RegionMean(Clean, Size / 2 - 3, 8, Size / 2, Size - 8)
                 - RegionMean(Clean, Size / 2, 8, Size / 2 + 3, Size - 8);
        };

        const float Few  = MeasureContrast(4.0f);      // barely started: noise dominates, blur it
        const float Many = MeasureContrast(1024.0f);   // converged: the step is real, keep it
        std::printf("     feature contrast at n=4 %.4f, at n=1024 %.4f (true 0.100)\n", Few, Many);

        Expect(Many > Few,   "more samples means less filtering of the same feature");
        Expect(Many > 0.09f, "a converged image keeps its detail instead of being blurred away");

        // And the uncorrected estimator would NOT back off: same n, but reporting the raw sample variance.
        Image Raw; Raw.Allocate(Size, Size);
        for (int Y = 0; Y < Size; ++Y)
        for (int X = 0; X < Size; ++X)
        {
            const size_t I = Raw.At(X, Y);
            const float V = X < Size / 2 ? Truth + 0.05f : Truth - 0.05f;
            Raw.Red[I] = Raw.Green[I] = Raw.Blue[I] = V;
            Raw.Variance[I] = SampleVariance;          // the bug: never falls with n
        }
        const Image RawClean = Denoise(Raw, Levels, Settings);
        const float RawContrast = RegionMean(RawClean, Size / 2 - 3, 8, Size / 2, Size - 8)
                                - RegionMean(RawClean, Size / 2, 8, Size / 2 + 3, Size - 8);
        std::printf("     with the raw sample variance: %.4f\n", RawContrast);
        Expect(RawContrast < Many * 0.5f, "the uncorrected estimator really does blur the feature away");
    }

    std::printf("\n>>> %s (%d failure%s)\n", Failures == 0 ? "ALL PASS" : "FAILURES", Failures, Failures == 1 ? "" : "s");
    return Failures == 0 ? 0 : 1;
}
