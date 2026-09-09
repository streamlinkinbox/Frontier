// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
//  SpatialTapJitterTest.cpp — proof that ReSTIR spatial reuse taps form a CONNECTED graph, not a fixed lattice
// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
//
//  The bug this exists to prevent
//  ──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
//  Spatial reuse originally tapped a fixed cross at a fixed radius of 11 px: the same four offsets, for every pixel, on every
//  frame. That makes the reuse graph STATIC. Pixel (x, y) can only ever exchange reservoirs with (x ± 11, y) and (x, y ± 11), so
//  the image partitions into 121 residue classes by (x mod 11, y mod 11) that never share a light sample.
//
//  Each class pools a different subset of samples and converges to its own slightly different estimate. Temporal accumulation does
//  not average that away — it CEMENTS it, because the same graph is reinforced every frame. So the artifact is invisible while
//  Monte-Carlo noise dominates and becomes sharpest once the image converges, which is exactly backwards from how noise behaves and
//  is why it was mistaken for a denoiser bug and then for a synchronisation bug.
//
//  The fix is to rotate the cross and jitter its radius per pixel per frame. This is a CORRECTNESS fix, not a tuning knob: the
//  ReSTIR estimator already assumes neighbours are drawn from a distribution, not from a fixed stencil.
//
//  Build:  g++ -std=c++20 -O2 -Wall -Wextra Scratchpad/SpatialTapJitterTest.cpp -o /tmp/STJ && /tmp/STJ
// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <set>
#include <utility>
#include <vector>

// ──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
//  Verbatim port of the shader's generator. Must stay identical to ReSTIRViewport.slang.
// ──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

constexpr float kSpatialRadiusMinPx = 4.0f;
constexpr float kSpatialRadiusMaxPx = 16.0f;
constexpr int   kSpatialTaps        = 4;

uint32_t PcgHash(uint32_t Value)
{
    uint32_t State = Value * 747796405u + 2891336453u;
    uint32_t Word  = ((State >> ((State >> 28u) + 4u)) ^ State) * 277803737u;
    return (Word >> 22u) ^ Word;
}

float RandFloat(uint32_t& Seed)
{
    Seed = PcgHash(Seed);
    return static_cast<float>(Seed) * (1.0f / 4294967296.0f);
}

uint32_t MakeSeed(uint32_t X, uint32_t Y, uint32_t Frame)
{
    return PcgHash(X + Y * 7919u + Frame * 1000003u);
}

// Returns the four tap offsets for one pixel on one frame, exactly as the shader derives them.
void SpatialTaps(int X, int Y, uint32_t Frame, int ViewportWidth, int OffsetX[4], int OffsetY[4])
{
    uint32_t TapSeed = PcgHash(MakeSeed(static_cast<uint32_t>(X), static_cast<uint32_t>(Y), Frame) ^ 0x9E3779B9u);
    const float Scale  = static_cast<float>(ViewportWidth) / 1280.0f;
    const float Angle  = RandFloat(TapSeed) * 6.28318531f;
    const float Radius = (kSpatialRadiusMinPx + (kSpatialRadiusMaxPx - kSpatialRadiusMinPx) * RandFloat(TapSeed)) * Scale;

    for (int Tap = 0; Tap < kSpatialTaps; ++Tap)
    {
        const float Theta = Angle + static_cast<float>(Tap) * 1.57079633f;
        OffsetX[Tap] = static_cast<int>(std::lround(Radius * std::cos(Theta)));
        OffsetY[Tap] = static_cast<int>(std::lround(Radius * std::sin(Theta)));
    }
}

// ──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

int Failures = 0;

void Expect(bool Condition, const char* What)
{
    std::printf("  %-68s %s\n", What, Condition ? "PASS" : "FAIL");
    if (!Condition) ++Failures;
}

// Connected components of the reuse graph, unioned over a window of frames.
int ReuseComponents(int Width, int Height, int Frames, bool Jittered, float FixedRadius)
{
    std::vector<std::vector<int>> Adjacency(static_cast<size_t>(Width * Height));

    for (int Frame = 0; Frame < Frames; ++Frame)
        for (int Y = 0; Y < Height; ++Y)
            for (int X = 0; X < Width; ++X)
            {
                int OffsetX[4], OffsetY[4];
                if (Jittered)
                {
                    SpatialTaps(X, Y, static_cast<uint32_t>(Frame), 1280, OffsetX, OffsetY);
                }
                else
                {
                    const int R = static_cast<int>(FixedRadius);
                    OffsetX[0] =  R; OffsetY[0] = 0;
                    OffsetX[1] = -R; OffsetY[1] = 0;
                    OffsetX[2] = 0;  OffsetY[2] =  R;
                    OffsetX[3] = 0;  OffsetY[3] = -R;
                }

                for (int Tap = 0; Tap < kSpatialTaps; ++Tap)
                {
                    const int Nx = X + OffsetX[Tap];
                    const int Ny = Y + OffsetY[Tap];
                    if (Nx < 0 || Ny < 0 || Nx >= Width || Ny >= Height) continue;
                    Adjacency[static_cast<size_t>(Y * Width + X)].push_back(Ny * Width + Nx);
                    Adjacency[static_cast<size_t>(Ny * Width + Nx)].push_back(Y * Width + X);
                }
            }

    std::vector<int> Component(static_cast<size_t>(Width * Height), -1);
    int Count = 0;
    for (int Index = 0; Index < Width * Height; ++Index)
    {
        if (Component[static_cast<size_t>(Index)] >= 0) continue;
        std::vector<int> Stack{ Index };
        Component[static_cast<size_t>(Index)] = Count;
        while (!Stack.empty())
        {
            const int At = Stack.back();
            Stack.pop_back();
            for (int To : Adjacency[static_cast<size_t>(At)])
                if (Component[static_cast<size_t>(To)] < 0)
                {
                    Component[static_cast<size_t>(To)] = Count;
                    Stack.push_back(To);
                }
        }
        ++Count;
    }
    return Count;
}

// Relative amplitude of the fixed (x mod 11, y mod 11) class pattern after accumulating a heavy-tailed light signal.
double ResidueClassBias(int Frames, bool Jittered)
{
    const int Width = 121, Height = 121;
    std::vector<double> Estimate(static_cast<size_t>(Width * Height), 0.0);
    std::vector<double> Count(static_cast<size_t>(Width * Height), 0.0);

    for (int Frame = 0; Frame < Frames; ++Frame)
    {
        std::vector<double> NextEstimate = Estimate;
        std::vector<double> NextCount    = Count;

        for (int Y = 0; Y < Height; ++Y)
            for (int X = 0; X < Width; ++X)
            {
                const size_t Here = static_cast<size_t>(Y * Width + X);
                uint32_t Seed = MakeSeed(static_cast<uint32_t>(X), static_cast<uint32_t>(Y), static_cast<uint32_t>(Frame));

                // The pixel's own fresh sample: heavy tailed, like a real light sample. Unbiased, mean 0.5.
                const double Sample = RandFloat(Seed) < 0.1f ? 5.0 * static_cast<double>(RandFloat(Seed)) : 0.0;
                NextCount[Here]    += 1.0;
                NextEstimate[Here] += (Sample - NextEstimate[Here]) / NextCount[Here];

                int OffsetX[4], OffsetY[4];
                if (Jittered)
                {
                    SpatialTaps(X, Y, static_cast<uint32_t>(Frame), 1280, OffsetX, OffsetY);
                }
                else
                {
                    OffsetX[0] =  11; OffsetY[0] = 0;
                    OffsetX[1] = -11; OffsetY[1] = 0;
                    OffsetX[2] = 0;   OffsetY[2] =  11;
                    OffsetX[3] = 0;   OffsetY[3] = -11;
                }

                for (int Tap = 0; Tap < kSpatialTaps; ++Tap)
                {
                    const int Nx = X + OffsetX[Tap];
                    const int Ny = Y + OffsetY[Tap];
                    if (Nx < 0 || Ny < 0 || Nx >= Width || Ny >= Height) continue;
                    const size_t There = static_cast<size_t>(Ny * Width + Nx);
                    const double Weight = Count[There];
                    if (Weight <= 0.0) continue;
                    NextCount[Here]    += Weight;
                    NextEstimate[Here] += (Estimate[There] - NextEstimate[Here]) * Weight / NextCount[Here];
                }
            }

        Estimate = NextEstimate;
        Count    = NextCount;
    }

    // How much do the 121 residue classes differ from each other?
    std::vector<double> ClassSum(121, 0.0);
    std::vector<int>    ClassCount(121, 0);
    double GlobalMean = 0.0;
    int    GlobalCount = 0;

    for (int Y = 20; Y < Height - 20; ++Y)
        for (int X = 20; X < Width - 20; ++X)
        {
            const int Class = (X % 11) * 11 + (Y % 11);
            ClassSum[static_cast<size_t>(Class)]   += Estimate[static_cast<size_t>(Y * Width + X)];
            ClassCount[static_cast<size_t>(Class)] += 1;
            GlobalMean += Estimate[static_cast<size_t>(Y * Width + X)];
            ++GlobalCount;
        }
    GlobalMean /= GlobalCount;

    double Variance = 0.0;
    for (size_t Class = 0; Class < 121; ++Class)
        if (ClassCount[Class] > 0)
        {
            const double Delta = ClassSum[Class] / ClassCount[Class] - GlobalMean;
            Variance += Delta * Delta;
        }
    return std::sqrt(Variance / 121.0) / GlobalMean;
}

int main()
{
    std::printf("ReSTIR spatial tap jitter — proof\n");

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n1. the reuse graph is connected\n");
    {
        // The whole point. A fixed cross of radius r partitions a grid into r² classes that never share a sample;
        //    jittering must collapse that to a single pool so every pixel's estimate draws on the same population.
        const int Fixed    = ReuseComponents(121, 121, 8, false, 11.0f);
        const int Jittered = ReuseComponents(121, 121, 8, true,  0.0f);
        std::printf("     fixed r=11: %d components,  jittered: %d\n", Fixed, Jittered);
        Expect(Fixed == 121,  "a fixed cross really does isolate 121 residue classes (the bug)");
        Expect(Jittered == 1, "jittered taps make the whole image one sample pool");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n2. connectivity does not need a long frame window\n");
    {
        // If connectivity only appeared after many frames, the lattice would still be visible during the first
        //    seconds of accumulation — precisely when the user is moving the camera around.
        Expect(ReuseComponents(121, 121, 1, true, 0.0f) == 1, "one single frame already connects the image");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n3. the fixed pattern actually disappears\n");
    {
        // Connectivity is necessary but not sufficient — measure the visible artifact directly. The fixed cross holds
        //    a class-to-class bias that does NOT decay with frame count; that non-decay is the signature the user saw
        //    as a lattice sharpening rather than fading as the image converged.
        const double Fixed32     = ResidueClassBias(32,  false);
        const double Fixed128    = ResidueClassBias(128, false);
        const double Jittered32  = ResidueClassBias(32,  true);
        const double Jittered128 = ResidueClassBias(128, true);

        std::printf("     fixed    : %.5f at 32 frames -> %.5f at 128\n", Fixed32, Fixed128);
        std::printf("     jittered : %.5f at 32 frames -> %.5f at 128\n", Jittered32, Jittered128);

        Expect(Fixed128 > 0.1,               "the fixed cross holds a large class bias even at 128 frames");
        Expect(Fixed128 > Fixed32 * 0.8,     "and that bias does NOT decay with accumulation — it is cemented");
        Expect(Jittered128 < Fixed128 * 0.1, "jittering removes at least 90 % of it");
        Expect(Jittered128 < Jittered32,     "and what remains keeps falling as the image converges, like real noise");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n4. the taps stay well formed\n");
    {
        float Lowest = 1e9f, Highest = -1e9f;
        int   Degenerate = 0, Total = 0;
        for (uint32_t Frame = 0; Frame < 64u; ++Frame)
            for (int Y = 0; Y < 64; ++Y)
                for (int X = 0; X < 64; ++X)
                {
                    int OffsetX[4], OffsetY[4];
                    SpatialTaps(X, Y, Frame, 1280, OffsetX, OffsetY);
                    for (int Tap = 0; Tap < kSpatialTaps; ++Tap)
                    {
                        const float R = std::sqrt(static_cast<float>(OffsetX[Tap] * OffsetX[Tap] + OffsetY[Tap] * OffsetY[Tap]));
                        Lowest  = std::min(Lowest,  R);
                        Highest = std::max(Highest, R);
                        if (OffsetX[Tap] == 0 && OffsetY[Tap] == 0) ++Degenerate;
                        ++Total;
                    }
                }
        std::printf("     radius %.2f .. %.2f px, %d degenerate of %d\n", Lowest, Highest, Degenerate, Total);
        Expect(Lowest  >= 3.0f,  "no tap collapses onto its own pixel's immediate neighbourhood");
        Expect(Highest <= 18.0f, "no tap reaches further than the band plus rounding");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n5. no direction or distance is systematically favoured\n");
    {
        // A biased tap distribution would replace the lattice with a directional smear, which is not an improvement.
        double MeanX = 0.0, MeanY = 0.0, MeanRadius = 0.0;
        int Count = 0;
        for (uint32_t Frame = 0; Frame < 256u; ++Frame)
            for (int Y = 0; Y < 16; ++Y)
                for (int X = 0; X < 16; ++X)
                {
                    int OffsetX[4], OffsetY[4];
                    SpatialTaps(X, Y, Frame, 1280, OffsetX, OffsetY);
                    for (int Tap = 0; Tap < kSpatialTaps; ++Tap)
                    {
                        MeanX += OffsetX[Tap];
                        MeanY += OffsetY[Tap];
                        MeanRadius += std::sqrt(static_cast<double>(OffsetX[Tap] * OffsetX[Tap] + OffsetY[Tap] * OffsetY[Tap]));
                        ++Count;
                    }
                }
        MeanX /= Count; MeanY /= Count; MeanRadius /= Count;
        std::printf("     mean offset (%.4f, %.4f) px, mean radius %.2f px\n", MeanX, MeanY, MeanRadius);
        Expect(std::fabs(MeanX) < 0.05 && std::fabs(MeanY) < 0.05, "the tap cloud is centred — no directional bias");
        Expect(MeanRadius > 8.0 && MeanRadius < 12.0,              "mean reach still matches the old fixed 11 px");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n6. one pixel sees many different neighbours over time\n");
    {
        // The mechanism of the fix: a pixel that always talks to the same four neighbours cannot escape its class.
        std::set<std::pair<int, int>> Directions;
        for (uint32_t Frame = 0; Frame < 256u; ++Frame)
        {
            int OffsetX[4], OffsetY[4];
            SpatialTaps(40, 40, Frame, 1280, OffsetX, OffsetY);
            for (int Tap = 0; Tap < kSpatialTaps; ++Tap) Directions.insert({ OffsetX[Tap], OffsetY[Tap] });
        }
        std::printf("     %zu distinct offsets over 256 frames (a fixed cross would give 4)\n", Directions.size());
        Expect(Directions.size() > 100, "a single pixel reaches hundreds of distinct neighbours");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n7. the cross keeps its shape\n");
    {
        // Still four taps at 90°: the jitter aims the cross, it does not deform it, so per-frame coverage is even.
        double Worst = 0.0;
        for (uint32_t Frame = 0; Frame < 16u; ++Frame)
            for (int Y = 0; Y < 32; ++Y)
                for (int X = 0; X < 32; ++X)
                {
                    int OffsetX[4], OffsetY[4];
                    SpatialTaps(X, Y, Frame, 1280, OffsetX, OffsetY);
                    for (int Tap = 0; Tap < kSpatialTaps; ++Tap)
                    {
                        const double A1 = std::atan2(static_cast<double>(OffsetY[Tap]), static_cast<double>(OffsetX[Tap]));
                        const double A2 = std::atan2(static_cast<double>(OffsetY[(Tap + 1) & 3]), static_cast<double>(OffsetX[(Tap + 1) & 3]));
                        double Delta = A2 - A1;
                        while (Delta < 0.0)          Delta += 6.283185307;
                        while (Delta > 6.283185307)  Delta -= 6.283185307;
                        Worst = std::max(Worst, std::fabs(Delta - 1.570796327));
                    }
                }
        std::printf("     worst deviation from 90°: %.4f rad\n", Worst);
        Expect(Worst < 0.35, "the four taps stay roughly square (integer rounding only)");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n8. resolution independence is preserved\n");
    {
        // The radius scales with width exactly as the old constant did, so a render-scale change does not alter reuse
        //    reach in world terms.
        double Mean640 = 0.0, Mean1280 = 0.0;
        int Count = 0;
        for (uint32_t Frame = 0; Frame < 64u; ++Frame)
            for (int Y = 0; Y < 16; ++Y)
                for (int X = 0; X < 16; ++X)
                {
                    int Ax[4], Ay[4], Bx[4], By[4];
                    SpatialTaps(X, Y, Frame, 640,  Ax, Ay);
                    SpatialTaps(X, Y, Frame, 1280, Bx, By);
                    for (int Tap = 0; Tap < kSpatialTaps; ++Tap)
                    {
                        Mean640  += std::sqrt(static_cast<double>(Ax[Tap] * Ax[Tap] + Ay[Tap] * Ay[Tap]));
                        Mean1280 += std::sqrt(static_cast<double>(Bx[Tap] * Bx[Tap] + By[Tap] * By[Tap]));
                        ++Count;
                    }
                }
        Mean640 /= Count; Mean1280 /= Count;
        std::printf("     mean radius %.2f px at 640 wide, %.2f px at 1280\n", Mean640, Mean1280);
        Expect(Mean1280 > Mean640 * 1.8 && Mean1280 < Mean640 * 2.2, "radius scales with render width, as before");
    }

    std::printf("\n>>> %s (%d failure%s)\n", Failures == 0 ? "ALL PASS" : "FAILURES", Failures, Failures == 1 ? "" : "s");
    return Failures == 0 ? 0 : 1;
}
