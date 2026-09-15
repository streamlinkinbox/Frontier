//==========================================================================================================================================
// Scratchpad/TemporalReprojectionTest.cpp — R7a: the running mean follows the surface through camera motion
//==========================================================================================================================================
// ResolveSurface() in Engine/Shaders/ReSTIRViewport.slang back-projects each pixel through the motion vector R2 wrote
//    and, if the surface it lands on still matches, inherits that pixel's mean and sample count instead of its own.
//    This harness is the numeric proof of that rule. It is a faithful port of the shader arithmetic, not a re-derivation:
//    the validation constants and the incremental-mean update are the same expressions the kernel evaluates.
//
// What is actually being asserted:
//    1. Identity      — with the feature bit clear, every pixel reads its own history, exactly as before R7a.
//    2. Convergence   — a surface tracked across a pan keeps its sample count, so the mean keeps converging.
//    3. Disocclusion  — a pixel whose back-projection lands on a different surface restarts at n = 1 rather than
//                       inheriting a neighbour's colour. This is the check that a smear would fail.
//    4. Off-screen    — back-projecting outside the viewport is treated as disocclusion, never as a wrapped read.
//    5. No surface    — background pixels never reproject and never become a valid source for anything else.
//
// The interesting property is #2 vs #3: a temporal filter that gets #2 right and #3 wrong looks *better* frame to
//    frame while being wrong, because smearing is smooth. Both are asserted against a hand-computed expectation.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

//------------------------------------------------------------------------------------------------------------------------
//                                        THE SHADER RULE, PORTED VERBATIM
//------------------------------------------------------------------------------------------------------------------------

namespace
{

constexpr float kTemporalNormalCos = 0.906307787f;   // cos 25°, shared with the R6 reservoir rule
constexpr float kTemporalDepthTol  = 0.10f;          // 10 % relative, shared with the R6 reservoir rule

struct Vector3
{
    float X = 0.0f, Y = 0.0f, Z = 0.0f;
};

float Dot(const Vector3& A, const Vector3& B) noexcept { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }

struct HistoryPixel
{
    Vector3 Mean;                      // [W·sr⁻¹·m⁻²] running mean radiance
    float   Count  = 0.0f;             // [-]  samples folded into Mean
    Vector3 Normal;                    // [-]  the surface the mean was shaded at
    float   Depth  = -1.0f;            // [m]  ≤ 0 marks "no surface"
};

struct Viewport
{
    int Width  = 0;
    int Height = 0;
};

// Mirrors ResolveSurface(): returns the pixel's new history entry given its radiance and the surface it hit.
HistoryPixel ResolveSurface(const Viewport& View,
                            const std::vector<HistoryPixel>& Previous,
                            const std::vector<std::array<float, 2>>& Motion,   // [uv] CurrentUv - PreviousUv
                            int PixelX, int PixelY,
                            const Vector3& Radiance,
                            const Vector3& Normal,
                            float Depth,
                            bool Reprojection,
                            uint32_t FrameIndex) noexcept
{
    const int Index = PixelY * View.Width + PixelX;

    Vector3 History{};
    float   Count    = 0.0f;
    bool    Resolved = false;

    const bool Reproject = Depth > 0.0f && Reprojection;

    if (FrameIndex > 0u && Reproject)
    {
        const float ExtentX = static_cast<float>(View.Width);
        const float ExtentY = static_cast<float>(View.Height);
        const float CurrentU = (static_cast<float>(PixelX) + 0.5f) / ExtentX;
        const float CurrentV = (static_cast<float>(PixelY) + 0.5f) / ExtentY;

        const int PreviousX = static_cast<int>(std::floor((CurrentU - Motion[Index][0]) * ExtentX));
        const int PreviousY = static_cast<int>(std::floor((CurrentV - Motion[Index][1]) * ExtentY));

        if (PreviousX >= 0 && PreviousY >= 0 && PreviousX < View.Width && PreviousY < View.Height)
        {
            const HistoryPixel& Source = Previous[static_cast<size_t>(PreviousY) * View.Width + PreviousX];
            if (Source.Depth > 0.0f
                && Dot(Normal, Source.Normal) > kTemporalNormalCos
                && std::fabs(Depth - Source.Depth) / std::max(Depth, 1e-3f) < kTemporalDepthTol)
            {
                History = Source.Mean;
                Count   = Source.Count;
            }
            Resolved = true;   // validated or disoccluded, either way the source is decided
        }
        else Resolved = true;  // back-projected off screen
    }

    if (FrameIndex > 0u && !Resolved)
    {
        History = Previous[Index].Mean;
        Count   = Previous[Index].Count;
    }

    Count += 1.0f;
    HistoryPixel Result;
    Result.Mean   = Vector3{ History.X + (Radiance.X - History.X) / Count,
                             History.Y + (Radiance.Y - History.Y) / Count,
                             History.Z + (Radiance.Z - History.Z) / Count };
    Result.Count  = Count;
    Result.Normal = Normal;
    Result.Depth  = Depth;
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      HARNESS
//------------------------------------------------------------------------------------------------------------------------

int Failures = 0;

void Expect(bool Condition, const char* Description)
{
    std::printf("  %-64s %s\n", Description, Condition ? "PASS" : "FAIL");
    if (!Condition) ++Failures;
}

bool Near(float A, float B, float Tolerance = 1e-5f) { return std::fabs(A - B) <= Tolerance; }

}   // namespace

int main()
{
    std::printf("[TemporalReprojection] R7a running-mean reprojection\n\n");

    const Viewport View{ 8, 8 };
    const size_t   Pixels = static_cast<size_t>(View.Width) * View.Height;

    const Vector3 FacingCamera{ 0.0f, 0.0f, 1.0f };
    const Vector3 Grey{ 0.5f, 0.5f, 0.5f };

    //--------------------------------------------------------------------------------------------------------------
    // A flat wall at 10 m, fully converged: every pixel holds a mean of 0.5 over 100 samples.
    //--------------------------------------------------------------------------------------------------------------
    std::vector<HistoryPixel> Previous(Pixels);
    for (HistoryPixel& P : Previous)
    {
        P.Mean   = Grey;
        P.Count  = 100.0f;
        P.Normal = FacingCamera;
        P.Depth  = 10.0f;
    }

    // A one-pixel horizontal pan: this frame's pixel x was at x-1 last frame.
    std::vector<std::array<float, 2>> PanRight(Pixels, { 1.0f / static_cast<float>(View.Width), 0.0f });
    std::vector<std::array<float, 2>> NoMotion(Pixels, { 0.0f, 0.0f });

    //--------------------------------------------------------------------------------------------------------------
    std::printf("1. identity: the feature bit clear reproduces the pre-R7a accumulator\n");
    {
        // Even with motion present, a disabled feature must read the pixel's own history.
        const HistoryPixel R = ResolveSurface(View, Previous, PanRight, 4, 4, Grey, FacingCamera, 10.0f, false, 7u);
        Expect(Near(R.Count, 101.0f), "sample count continues from this pixel's own history");
        Expect(Near(R.Mean.X, 0.5f),  "a converged mean fed identical radiance does not move");
    }

    //--------------------------------------------------------------------------------------------------------------
    std::printf("\n2. convergence: a tracked surface keeps its sample count across a pan\n");
    {
        const HistoryPixel R = ResolveSurface(View, Previous, PanRight, 4, 4, Grey, FacingCamera, 10.0f, true, 7u);
        Expect(Near(R.Count, 101.0f), "count inherited from the back-projected pixel, not reset");
        Expect(Near(R.Mean.X, 0.5f),  "the mean survives the pan intact");

        // The whole interior of the wall must inherit; only the column that back-projects off screen may restart.
        int Inherited = 0;
        for (int Y = 0; Y < View.Height; ++Y)
            for (int X = 0; X < View.Width; ++X)
            {
                const HistoryPixel P = ResolveSurface(View, Previous, PanRight, X, Y, Grey, FacingCamera, 10.0f, true, 7u);
                if (P.Count > 1.0f) ++Inherited;
            }
        Expect(Inherited == View.Width * View.Height - View.Height,
               "every pixel inherits except the one column that panned in from off screen");
    }

    //--------------------------------------------------------------------------------------------------------------
    std::printf("\n3. disocclusion: a foreground edge restarts instead of smearing\n");
    {
        // The pixel now shows something 2 m away — the wall it back-projects onto is at 10 m, far outside 10 %.
        const HistoryPixel R = ResolveSurface(View, Previous, PanRight, 4, 4, Vector3{ 1.0f, 0.0f, 0.0f },
                                              FacingCamera, 2.0f, true, 7u);
        Expect(Near(R.Count, 1.0f),   "depth mismatch is treated as disocclusion, count restarts at 1");
        Expect(Near(R.Mean.X, 1.0f),  "the new surface shows its own radiance, not the wall's grey");
        Expect(Near(R.Mean.Y, 0.0f),  "no grey bleeds into the disoccluded pixel");

        // A surface at the same depth but facing away is equally invalid: 90° apart fails the 25° cone.
        const Vector3 Sideways{ 1.0f, 0.0f, 0.0f };
        const HistoryPixel N = ResolveSurface(View, Previous, PanRight, 4, 4, Grey, Sideways, 10.0f, true, 7u);
        Expect(Near(N.Count, 1.0f), "normal mismatch also restarts, even at a matching depth");

        // Just inside the tolerances must still be accepted, or silhouettes would flicker.
        const HistoryPixel Edge = ResolveSurface(View, Previous, PanRight, 4, 4, Grey, FacingCamera, 10.4f, true, 7u);
        Expect(Near(Edge.Count, 101.0f), "a 4 % depth change stays inside the 10 % tolerance and still inherits");
    }

    //--------------------------------------------------------------------------------------------------------------
    std::printf("\n4. off-screen: back-projection outside the viewport never wraps\n");
    {
        // Column 0 panning right came from x = -1.
        const HistoryPixel R = ResolveSurface(View, Previous, PanRight, 0, 4, Grey, FacingCamera, 10.0f, true, 7u);
        Expect(Near(R.Count, 1.0f), "the leading column restarts rather than sampling the far edge");

        // A violent pan sends every pixel off screen; nothing may inherit.
        std::vector<std::array<float, 2>> Violent(Pixels, { 5.0f, 5.0f });
        int Restarted = 0;
        for (int Y = 0; Y < View.Height; ++Y)
            for (int X = 0; X < View.Width; ++X)
                if (Near(ResolveSurface(View, Previous, Violent, X, Y, Grey, FacingCamera, 10.0f, true, 7u).Count, 1.0f))
                    ++Restarted;
        Expect(Restarted == View.Width * View.Height, "a whip pan restarts the whole image, with no stale reads");
    }

    //--------------------------------------------------------------------------------------------------------------
    std::printf("\n5. surface-less pixels are inert\n");
    {
        // Background writes go through the thin Resolve() wrapper: depth < 0.
        const HistoryPixel R = ResolveSurface(View, Previous, PanRight, 4, 4, Vector3{}, FacingCamera, -1.0f, true, 7u);
        Expect(Near(R.Count, 101.0f), "background still accumulates against its own pixel");
        Expect(R.Depth <= 0.0f,       "background records no surface, so it cannot be inherited from");

        // And a pixel back-projecting onto stored background must not inherit it.
        std::vector<HistoryPixel> Background(Pixels);   // default Depth = -1
        const HistoryPixel S = ResolveSurface(View, Background, PanRight, 4, 4, Grey, FacingCamera, 10.0f, true, 7u);
        Expect(Near(S.Count, 1.0f), "a surface appearing over former background restarts");
    }

    //--------------------------------------------------------------------------------------------------------------
    std::printf("\n6. the first frame has no history at all\n");
    {
        const HistoryPixel R = ResolveSurface(View, Previous, PanRight, 4, 4, Grey, FacingCamera, 10.0f, true, 0u);
        Expect(Near(R.Count, 1.0f), "frame 0 ignores whatever the images happen to contain");
    }

    //--------------------------------------------------------------------------------------------------------------
    std::printf("\n7. a converged pan actually reduces noise (the point of the exercise)\n");
    {
        // Feed a noisy signal around 0.5 to a wall and compare the error after 60 frames with and without
        //    reprojection. Without it, a moving camera can never keep history, so every frame is a fresh n = 1
        //    estimate — that is exactly the artefact R7a exists to remove.
        //
        // Note on the achievable floor: under a *continuous* 1 px/frame pan the oldest surviving history in a
        //    W-wide viewport is W frames old, because every column is eventually replaced by content that entered
        //    from off screen. On this deliberately tiny 8 px viewport the sample count therefore saturates at 8,
        //    not at 60, and the residual error floor is ≈ 1/√8 of the noise amplitude. That is correct behaviour,
        //    so the assertion below is relative (reprojection must beat restarting by a wide margin) and the
        //    absolute convergence case is covered separately in 7b with a camera that comes to rest.
        uint32_t Seed = 12345u;
        const auto Noise = [&Seed]() noexcept
        {
            Seed = Seed * 1664525u + 1013904223u;
            return 0.5f + (static_cast<float>(Seed >> 8u) / 16777216.0f - 0.5f);   // 0.5 ± 0.5
        };

        std::array<float, 2> ErrorByMode{};
        for (int Mode = 0; Mode < 2; ++Mode)
        {
            const bool Reprojection = Mode == 1;
            std::vector<HistoryPixel> History(Pixels);
            for (HistoryPixel& P : History) { P.Normal = FacingCamera; P.Depth = 10.0f; }

            std::vector<HistoryPixel> Next(Pixels);
            for (uint32_t Frame = 0; Frame < 60u; ++Frame)
            {
                for (int Y = 0; Y < View.Height; ++Y)
                    for (int X = 0; X < View.Width; ++X)
                    {
                        const float N = Noise();
                        // Without reprojection a moving camera cannot keep history: model that as a per-frame reset.
                        const uint32_t Effective = Reprojection ? Frame : 0u;
                        Next[static_cast<size_t>(Y) * View.Width + X] =
                            ResolveSurface(View, History, PanRight, X, Y, Vector3{ N, N, N },
                                           FacingCamera, 10.0f, Reprojection, Effective);
                    }
                History.swap(Next);
            }

            float Error = 0.0f;
            for (const HistoryPixel& P : History) Error += std::fabs(P.Mean.X - 0.5f);
            Error /= static_cast<float>(Pixels);

            std::printf("     %-20s mean absolute error %.4f\n", Reprojection ? "reprojected:" : "restarting:", Error);
            ErrorByMode[Mode] = Error;
        }

        Expect(ErrorByMode[1] < ErrorByMode[0] * 0.6f,
               "reprojection cuts the error of a continuous pan by at least 40 %");
        Expect(ErrorByMode[0] > 0.15f, "the restarting reference really is as noisy as claimed");
    }

    //--------------------------------------------------------------------------------------------------------------
    std::printf("\n7b. once the camera stops, the mean converges the whole way\n");
    {
        // Same noisy signal, but the camera pans for 10 frames and then comes to rest — the ordinary case of a
        //    player letting go of the stick. History must survive the pan and then accumulate without limit.
        uint32_t Seed = 99991u;
        const auto Noise = [&Seed]() noexcept
        {
            Seed = Seed * 1664525u + 1013904223u;
            return 0.5f + (static_cast<float>(Seed >> 8u) / 16777216.0f - 0.5f);
        };

        std::vector<HistoryPixel> History(Pixels);
        for (HistoryPixel& P : History) { P.Normal = FacingCamera; P.Depth = 10.0f; }
        std::vector<HistoryPixel> Next(Pixels);

        for (uint32_t Frame = 0; Frame < 200u; ++Frame)
        {
            const std::vector<std::array<float, 2>>& Motion = Frame < 10u ? PanRight : NoMotion;
            for (int Y = 0; Y < View.Height; ++Y)
                for (int X = 0; X < View.Width; ++X)
                {
                    const float N = Noise();
                    Next[static_cast<size_t>(Y) * View.Width + X] =
                        ResolveSurface(View, History, Motion, X, Y, Vector3{ N, N, N },
                                       FacingCamera, 10.0f, true, Frame);
                }
            History.swap(Next);
        }

        float Error = 0.0f;
        float Lowest = 1e9f;
        for (const HistoryPixel& P : History) { Error += std::fabs(P.Mean.X - 0.5f); Lowest = std::min(Lowest, P.Count); }
        Error /= static_cast<float>(Pixels);

        std::printf("     settled camera:      mean absolute error %.4f, lowest sample count %.0f\n", Error, Lowest);
        Expect(Lowest >= 190.0f, "history survives the pan, so counts reach ~200 rather than resetting");
        Expect(Error < 0.02f,    "a settled camera converges to within 2 % of the true mean");
    }

    std::printf("\n>>> %s (%d failure%s)\n", Failures == 0 ? "ALL PASS" : "FAILURES", Failures, Failures == 1 ? "" : "s");
    return Failures == 0 ? 0 : 1;
}
