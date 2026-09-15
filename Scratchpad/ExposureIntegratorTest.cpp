// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
//  ExposureIntegratorTest.cpp — adaptive exposure behaves like an eye, and identically at any frame rate
// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
//  Four things here are easy to get wrong and hard to notice afterwards:
//
//    · a linear mean instead of a log mean — the sun entering frame blacks out the image;
//    · frame-rate dependent easing — the transition looks different on different hardware;
//    · symmetric adaptation — dark rooms resolve implausibly fast, or bright doorways implausibly slowly;
//    · no bounds — a black frame drives exposure to infinity and amplifies noise into a grey blizzard.
//
//  Each is asserted directly rather than inferred from a screenshot.
//
//  Build: see Scratchpad/CheckExposureIntegrator.sh
// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════

#include "DisplayPresentation/ExposureIntegrator.h"

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <initializer_list>
#include <limits>
#include <vector>

using namespace Frontier;

static int Failures = 0;

static void Expect(bool Condition, const char* What)
{
    std::printf("  %-70s %s\n", What, Condition ? "PASS" : "FAIL");
    if (!Condition) ++Failures;
}

// Run EXACTLY the given wall-clock duration at a given frame rate, feeding one constant measurement.
//
// ⚠️ The obvious `for (t = 0; t < Seconds; t += Step)` is wrong for this test: at 15 Hz it runs 8 steps of
//    1/15 s, simulating 0.533 s rather than 0.500 — 6.7 % more time than 360 Hz gets. That difference then
//    reads as frame-rate dependence in the integrator, which is precisely what the test is trying to detect.
//    The first version of this harness failed for exactly that reason, and the integrator was innocent.
static float Settle(ExposureIntegrator& Exposure, float Luminance, float Seconds, float FrameRate)
{
    Exposure.ObserveLuminance(std::log(Luminance));
    const float Step  = 1.0f / FrameRate;
    float       Left  = Seconds;
    while (Left > 1.0e-6f)
    {
        const float This = std::fmin(Step, Left);
        Exposure.Advance(This);
        Left -= This;
    }
    return Exposure.QueryAdaptedLuminance();
}

//------------------------------------------------------------------------------------------------------------------------
//                                    PORT OF THE HISTOGRAM METER (LuminanceReduce.slang + the readback)
//------------------------------------------------------------------------------------------------------------------------
// The shader fills the buckets and the host trims and averages them; both halves are reproduced here so the
//    whole rule can be exercised without a device. The gate checks the constants still agree with both sides.

static const int   kHistogramBins   = 256;
static const float kLogLuminanceLow = -30.0f;
static const float kLogLuminanceHigh =  30.0f;
static const float kMedianStops     = 6.0f;
static const float kCentreSigma     = 0.18f;
static const int   kCentreWeightPeak = 31;

struct MeterTap { float Luminance, X, Y; };   // X, Y are screen position in 0..1

// Returns the metered luminance, or −1 when the frame carried nothing measurable at all.
static float MeterWith(const std::vector<MeterTap>& Taps, bool CentreWeighted, float MedianStops = kMedianStops)
{
    double Bins[kHistogramBins] = {};
    for (const MeterTap& T : Taps)
    {
        if (!(T.Luminance > 0.0f)) continue;
        const float Normalised = (std::log2(T.Luminance) - kLogLuminanceLow) / (kLogLuminanceHigh - kLogLuminanceLow);
        int Bin = static_cast<int>(Normalised * kHistogramBins);
        Bin = Bin < 0 ? 0 : (Bin > kHistogramBins - 1 ? kHistogramBins - 1 : Bin);

        const float Dx = T.X - 0.5f, Dy = T.Y - 0.5f;
        const float Falloff = std::exp(-(Dx * Dx + Dy * Dy) / (kCentreSigma * kCentreSigma));
        Bins[Bin] += CentreWeighted
                   ? 1.0 + static_cast<double>(static_cast<int>(kCentreWeightPeak * Falloff + 0.5f))
                   : 1.0;
    }

    double Total = 0.0;
    for (double W : Bins) Total += W;
    if (Total <= 0.0) return -1.0f;

    const double Width = (kLogLuminanceHigh - kLogLuminanceLow) / double(kHistogramBins);
    const auto BinLog2 = [&](int Index) { return kLogLuminanceLow + (Index + 0.5) * Width; };

    // Where the scene is: the median, which no minority of very bright or very dark pixels can move.
    double Seen = 0.0; int MedianIndex = 0;
    for (int Index = 0; Index < kHistogramBins; ++Index)
    {
        Seen += Bins[Index];
        if (Seen >= Total * 0.5) { MedianIndex = Index; break; }
    }
    const double Anchor = BinLog2(MedianIndex);

    // Everything within a few stops of it. A distance in stops is what separates a bright OUTLIER from a bright
    //    SUBJECT, where an area fraction cannot: sunlit ground sits two stops from its sky, a hole in a roof
    //    sits eleven stops above the room it lights.
    double Weighted = 0.0, Used = 0.0;
    for (int Index = 0; Index < kHistogramBins; ++Index)
    {
        if (Bins[Index] <= 0.0) continue;
        const double Centre = BinLog2(Index);
        if (std::fabs(Centre - Anchor) > MedianStops) continue;
        Weighted += Centre * Bins[Index];
        Used     += Bins[Index];
    }
    if (Used <= 0.0) return static_cast<float>(std::exp2(Anchor));
    return static_cast<float>(std::exp2(Weighted / Used));
}

static float MeterFrame(const std::vector<MeterTap>& Taps)            { return MeterWith(Taps, true); }
static float MeterFrameUnweighted(const std::vector<MeterTap>& Taps)  { return MeterWith(Taps, false); }

// The same rule with every tap counting equally, for showing what centre weighting buys.
static float MeterFrameUnweighted(const std::vector<MeterTap>& Taps);

// A frame holding a centred subject of the given half-size against a surround.
static std::vector<MeterTap> SubjectFrame(float Subject, float Surround, float HalfSize)
{
    std::vector<MeterTap> Taps;
    const int N = 48;
    for (int J = 0; J < N; ++J)
        for (int I = 0; I < N; ++I)
        {
            const float X = (I + 0.5f) / N, Y = (J + 0.5f) / N;
            const bool  Inside = std::fabs(X - 0.5f) < HalfSize && std::fabs(Y - 0.5f) < HalfSize;
            Taps.push_back({ Inside ? Subject : Surround, X, Y });
        }
    return Taps;
}

int main()
{
    std::printf("ExposureIntegrator — adaptive exposure\n");

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n1. manual mode reproduces the fixed slider exactly\n");
    {
        // The identity switch. Every image made before adaptive exposure existed must still be reproducible,
        //    or the A/B against the previous phases is not honest.
        ExposureIntegrator Exposure;
        ExposureConfiguration Config{};
        Config.Mode           = ExposureModeCategory::Manual;
        Config.ManualExposure = 1.05f;
        Exposure.AssignConfiguration(Config);

        Exposure.ObserveLuminance(std::log(50000.0f));   // a blindingly bright scene
        for (int Frame = 0; Frame < 600; ++Frame) Exposure.Advance(1.0f / 60.0f);

        Expect(Exposure.QueryExposure() == 1.05f, "manual exposure ignores the measurement entirely");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n2. a correctly exposed scene lands on the key value\n");
    {
        // The definition of "correct": a scene whose average luminance is L must be rendered so that L maps to
        //    the key for that luminance. If this is wrong every scene is uniformly too dark or too bright.
        //
        // ⚠️ The key is a CONSTANT only in daylight. Below the photopic level it falls, deliberately, because
        //    an exposure of Key/L renders every scene at the same mid-grey and a starlit field would arrive
        //    looking like an overcast afternoon. Asserting against the constant here was asserting that night
        //    must look like day — so the expectation follows the curve, and test 11 is what pins the curve
        //    itself down.
        ExposureConfiguration Config{};
        for (float Luminance : { 0.001f, 0.18f, 1.0f, 100.0f, 8000.0f })
        {
            const float Exposure = ExposureIntegrator::ExposureForLuminance(Luminance, Config);
            const float Rendered = Luminance * Exposure;
            const float Expected = ExposureIntegrator::KeyForLuminance(Luminance, Config);
            const bool  Clamped  = Exposure <= Config.MinimumExposure || Exposure >= Config.MaximumExposure;
            std::printf("     %9.3f cd/m² → exposure %8.3f → renders as %.4f  (key %.4f)%s\n",
                        static_cast<double>(Luminance), static_cast<double>(Exposure),
                        static_cast<double>(Rendered), static_cast<double>(Expected),
                        Clamped ? "  (clamped)" : "");
            if (!Clamped)
                Expect(std::fabs(Rendered - Expected) < 1e-4f, "renders at the key value for its luminance");
        }
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n3. adaptation is frame-rate independent\n");
    {
        // The one that silently differs between machines. `Adapted += (Observed − Adapted) * Rate * Δτ` adapts
        //    faster at higher frame rates, so a transition authored at 60 Hz looks wrong at 144. The
        //    1 − e^(−Δτ/τ) form does not, and this measures it across a 24× spread.
        float Results[4];
        int Index = 0;
        for (float FrameRate : { 15.0f, 30.0f, 60.0f, 360.0f })
        {
            ExposureIntegrator Exposure;
            Exposure.AssignConfiguration(ExposureConfiguration{});
            Exposure.ObserveLuminance(std::log(0.18f));
            Exposure.Snap();
            Results[Index++] = Settle(Exposure, 18.0f, 0.5f, FrameRate);
        }
        std::printf("     after 0.5 s at 15 / 30 / 60 / 360 Hz: %.4f  %.4f  %.4f  %.4f\n",
                    static_cast<double>(Results[0]), static_cast<double>(Results[1]),
                    static_cast<double>(Results[2]), static_cast<double>(Results[3]));

        float Lowest = Results[0], Highest = Results[0];
        for (float Value : Results) { Lowest = std::fmin(Lowest, Value); Highest = std::fmax(Highest, Value); }
        Expect((Highest - Lowest) / Highest < 0.05f,
               "a 24× spread in frame rate changes the result by under 5 %");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n4. adaptation is asymmetric — bright fast, dark slow\n");
    {
        // Both directions cover the same log distance, so any difference in progress is the asymmetry and not
        //    the size of the change.
        ExposureConfiguration Config{};

        ExposureIntegrator Brightening;
        Brightening.AssignConfiguration(Config);
        Brightening.ObserveLuminance(std::log(0.1f));
        Brightening.Snap();
        const float AfterBright = Settle(Brightening, 10.0f, 0.5f, 60.0f);

        ExposureIntegrator Darkening;
        Darkening.AssignConfiguration(Config);
        Darkening.ObserveLuminance(std::log(10.0f));
        Darkening.Snap();
        const float AfterDark = Settle(Darkening, 0.1f, 0.5f, 60.0f);

        // Progress measured in log space, where both journeys are the same length.
        const float BrightProgress = (std::log(AfterBright) - std::log(0.1f)) / (std::log(10.0f) - std::log(0.1f));
        const float DarkProgress   = (std::log(10.0f) - std::log(AfterDark)) / (std::log(10.0f) - std::log(0.1f));
        std::printf("     after 0.5 s: brightening %.1f %% of the way, darkening %.1f %%\n",
                    static_cast<double>(BrightProgress * 100.0f), static_cast<double>(DarkProgress * 100.0f));

        Expect(BrightProgress > DarkProgress * 1.5f, "the eye adapts to bright markedly faster than to dark");
        Expect(BrightProgress > 0.6f,  "and a bright doorway resolves within half a second");
        Expect(DarkProgress   < 0.35f, "while a dark room genuinely takes its time");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n5. the sun entering frame does not black out the image\n");
    {
        // The reason the measurement is a log mean. Modelled directly: a frame that is 99 % dim room and 1 %
        //    sun disc. A LINEAR average is dominated by the sun; a log average is not, and the difference is
        //    the whole argument.
        const float RoomLuminance = 1.0f;
        const float SunLuminance  = 1.6e9f;   // the solar disc, in cd/m²
        const float SunFraction   = 0.01f;

        const float LinearMean = (1.0f - SunFraction) * RoomLuminance + SunFraction * SunLuminance;
        const float LogMean    = std::exp((1.0f - SunFraction) * std::log(RoomLuminance)
                                        + SunFraction * std::log(SunLuminance));

        ExposureConfiguration Config{};
        const float LinearExposure = ExposureIntegrator::ExposureForLuminance(LinearMean, Config);
        const float LogExposure    = ExposureIntegrator::ExposureForLuminance(LogMean, Config);

        std::printf("     linear mean %.3e → exposure %.6f → the room renders at %.6f\n",
                    static_cast<double>(LinearMean), static_cast<double>(LinearExposure),
                    static_cast<double>(RoomLuminance * LinearExposure));
        std::printf("     log mean    %.3e → exposure %.6f → the room renders at %.6f\n",
                    static_cast<double>(LogMean), static_cast<double>(LogExposure),
                    static_cast<double>(RoomLuminance * LogExposure));

        Expect(RoomLuminance * LinearExposure < 0.01f,
               "a linear mean really would black the room out — the failure this avoids");
        Expect(RoomLuminance * LogExposure > 0.05f,
               "the log mean keeps the room visible with the sun in shot");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n6. exposure is bounded at both ends\n");
    {
        // Without a ceiling a nearly black frame drives exposure toward infinity and amplifies sensor noise
        //    into a grey blizzard; without a floor, staring at the sun drives it to zero.
        ExposureConfiguration Config{};

        const float Darkest  = ExposureIntegrator::ExposureForLuminance(0.0f,   Config);
        const float Brightest= ExposureIntegrator::ExposureForLuminance(1.0e9f, Config);
        std::printf("     a black frame gives %.2f, the sun's disc gives %.5f\n",
                    static_cast<double>(Darkest), static_cast<double>(Brightest));

        Expect(Darkest   <= Config.MaximumExposure, "a black frame cannot drive exposure past the ceiling");
        Expect(Brightest >= Config.MinimumExposure, "and the sun cannot drive it below the floor");
        Expect(std::isfinite(Darkest) && std::isfinite(Brightest), "neither bound produces a non-finite value");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n7. a bad measurement cannot poison the adaptation permanently\n");
    {
        // A NaN in the adapted value would propagate forever — every later frame compares against it and stays
        //    NaN, so the screen goes black for the rest of the session rather than for one frame.
        ExposureIntegrator Exposure;
        Exposure.AssignConfiguration(ExposureConfiguration{});

        Exposure.ObserveLuminance(std::numeric_limits<float>::quiet_NaN());
        Exposure.Advance(1.0f / 60.0f);
        Expect(std::isfinite(Exposure.QueryExposure()), "a NaN measurement is rejected rather than absorbed");

        Exposure.ObserveLuminance(-std::numeric_limits<float>::infinity());   // log of a wholly black frame
        Exposure.Advance(1.0f / 60.0f);
        Expect(std::isfinite(Exposure.QueryExposure()), "and so is the log of a completely black frame");

        // And it still tracks a good measurement afterwards.
        const float Recovered = Settle(Exposure, 1.0f, 3.0f, 60.0f);
        Expect(std::isfinite(Recovered) && Recovered > 0.5f && Recovered < 2.0f,
               "adaptation recovers and tracks the next valid measurement");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n8. adaptation settles rather than oscillating\n");
    {
        // An exponential approach must be monotone. Overshoot would read as the image pulsing after every
        //    change in view, which is the kind of thing that gets blamed on the denoiser.
        ExposureIntegrator Exposure;
        Exposure.AssignConfiguration(ExposureConfiguration{});
        Exposure.ObserveLuminance(std::log(0.1f));
        Exposure.Snap();
        Exposure.ObserveLuminance(std::log(100.0f));

        float Previous = Exposure.QueryAdaptedLuminance();
        bool  Monotone = true;
        for (int Frame = 0; Frame < 600; ++Frame)
        {
            Exposure.Advance(1.0f / 60.0f);
            const float Now = Exposure.QueryAdaptedLuminance();
            if (Now < Previous - 1e-6f) Monotone = false;
            Previous = Now;
        }
        std::printf("     settled at %.4f (target 100.0)\n", static_cast<double>(Previous));
        Expect(Monotone, "the approach never reverses — no overshoot, no pulsing");
        Expect(Previous > 99.0f && Previous <= 100.01f, "and it reaches the target without exceeding it");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n9. the night sky becomes visible, which is the point\n");
    {
        // The reason this phase exists. A7's stars and moonlit sky are physically 8 orders of magnitude below a
        //    noon sky; under one fixed exposure they are a black screen. These are the exposures adaptation
        //    supplies, and whether each scene then lands in a visible range.
        ExposureConfiguration Config{};
        struct Scene { const char* Name; float Luminance; };
        const Scene Scenes[] = {
            { "noon sky",       8000.0f  },
            { "overcast",       2000.0f  },
            { "sunset",           50.0f  },
            { "civil twilight",    3.0f  },
            { "moonlit sky",       0.1f  },
        };
        bool AllVisible = true;
        for (const Scene& S : Scenes)
        {
            const float Exposure = ExposureIntegrator::ExposureForLuminance(S.Luminance, Config);
            const float Rendered = S.Luminance * Exposure;
            std::printf("     %-16s %9.3f cd/m² → exposure %8.2f → %.3f\n",
                        S.Name, static_cast<double>(S.Luminance),
                        static_cast<double>(Exposure), static_cast<double>(Rendered));
            if (Rendered < 0.02f || Rendered > 1.0f) AllVisible = false;
        }
        Expect(AllVisible, "every scene from noon to moonlight lands in a visible range");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n10. the meter is SCALE INVARIANT — the same rule at noon and at midnight\n");
    {
        // 🔴 The defect this replaces. The reduction used to skip any pixel below a fixed 1e-2 cd/m², which is a
        //    daylight constant sitting in a place every scale of scene passes through. Measured across a sunset:
        //    at 9° below the horizon the sky fell under the floor while the ground was still above it, so the
        //    sky went black against a correctly exposed ground; by 18° below, EVERY pixel was excluded, the
        //    sample count reached zero and the meter stopped updating and held its last daylight reading. That
        //    is a permanently black night with no stars in it.
        //
        //    A percentile of the frame's own distribution has no absolute constant in it, so the identical
        //    frame scaled down by six orders of magnitude must meter six orders of magnitude lower — exactly.
        // ⚠️ Invariance holds to the histogram's RESOLUTION, not exactly: every tap is averaged as though it sat
        //    at its bucket's centre, so sliding a scene across a boundary moves the reading by up to half a
        //    bucket. That is 0.12 of a stop at 256 buckets — the bound is asserted rather than assumed, because
        //    it is what sets the bin count, and a coarser histogram would drift visibly as the camera pans.
        // ⚠️ The claim is PROPORTIONALITY, not a particular value. What the meter returns for a given frame
        //    depends on that frame's content — a subject against a darker surround does not read as the
        //    subject's own luminance, and should not. Scale invariance is that the readings agree with EACH
        //    OTHER once divided by the scale, and asserting they each equal 1.0 was asserting the wrong thing.
        //
        //    The bound is the histogram's resolution: bins are 0.23 of a stop, and the median anchor can land
        //    one bin either way as a distribution slides across a boundary.
        const float Quantisation = std::exp2(2.0f * (kLogLuminanceHigh - kLogLuminanceLow) / kHistogramBins);
        float Lowest = 1e30f, Highest = 0.0f;
        bool  Measured = true;
        std::printf("     scene scale        metered      ratio to scene   (spread bound %.3f)\n",
                    static_cast<double>(Quantisation));
        for (float Scale : { 1.0e4f, 1.0f, 1.0e-2f, 1.0e-4f, 1.0e-6f })
        {
            const float Metered = MeterFrame(SubjectFrame(1.0f * Scale, 0.2f * Scale, 0.30f));
            const float Ratio   = Metered / Scale;
            std::printf("     %10.1e   %14.6e   %10.4f\n",
                        static_cast<double>(Scale), static_cast<double>(Metered), static_cast<double>(Ratio));
            if (!(Metered > 0.0f)) Measured = false;
            Lowest = std::fmin(Lowest, Ratio); Highest = std::fmax(Highest, Ratio);
        }
        std::printf("     the ratio varies by %.4fx across ten orders of magnitude\n",
                    static_cast<double>(Highest / Lowest));
        Expect(Measured, "every scale still produces a reading — no absolute floor remains");
        Expect(Highest / Lowest < Quantisation,
               "and they agree with each other to the histogram's own resolution");

        // The specific frame that used to return nothing at all: a night sky with stars in it.
        std::vector<MeterTap> Night;
        for (int I = 0; I < 2304; ++I)
            Night.push_back({ 1.0e-4f, (I % 48 + 0.5f) / 48.0f, (I / 48 + 0.5f) / 48.0f });
        Night[1100].Luminance = 0.4f;   // one star
        const float NightMetered = MeterFrame(Night);
        std::printf("     a night sky at 1e-4 with one star meters %.3e\n", static_cast<double>(NightMetered));
        Expect(NightMetered > 0.0f,      "a night frame still produces a reading — the old rule produced none");
        Expect(NightMetered < 1.0e-3f,   "and it reads the sky, not the star");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n11. dark adaptation — night renders as night, and stars come through it\n");
    {
        ExposureConfiguration Config{};

        // Above the photopic level nothing changes at all, which is the identity switch: every image made
        //    before this curve existed is reproduced exactly.
        bool Identity = true;
        for (float L : { 5.0f, 50.0f, 2000.0f, 120000.0f })
            if (std::fabs(ExposureIntegrator::KeyForLuminance(L, Config) - Config.KeyValue) > 1e-6f) Identity = false;
        Expect(Identity, "at and above the photopic level the key is exactly KeyValue — nothing changes in daylight");

        std::printf("     scene                luminance      key    renders at\n");
        struct Row { const char* Name; float Luminance; };
        const Row Rows[] = {
            { "noon",            8000.0f  },
            { "overcast",        2000.0f  },
            { "civil twilight",     3.0f  },
            { "moonlit sky",        0.1f  },
            { "starlit sky",     1.0e-4f  },
        };
        float PreviousRendered = 1e9f;
        bool  Monotonic = true;
        for (const Row& R : Rows)
        {
            const float Key = ExposureIntegrator::KeyForLuminance(R.Luminance, Config);
            const float Rendered = R.Luminance * ExposureIntegrator::ExposureForLuminance(R.Luminance, Config);
            std::printf("     %-18s %10.4f  %7.4f  %10.4f\n", R.Name,
                        static_cast<double>(R.Luminance), static_cast<double>(Key),
                        static_cast<double>(Rendered));
            if (Rendered > PreviousRendered + 1e-6f) Monotonic = false;
            PreviousRendered = Rendered;
        }
        Expect(Monotonic, "a darker scene always renders darker — the eye never fully compensates");

        // 🔴 The point of the whole curve. A full-compensation exposure renders the night sky at mid-grey and
        //    the stars as a barely brighter grey on top of it; with dark adaptation the sky is nearly black and
        //    the same stars are points of light well above white.
        const float SkyLuminance  = 1.0e-4f;
        const float StarLuminance = 0.052f;   // a bright star's peak: StarBrightness 0.4 × the field's 0.13
        const float Exposure = ExposureIntegrator::ExposureForLuminance(SkyLuminance, Config);
        const float SkyRenders  = SkyLuminance  * Exposure;
        const float StarRenders = StarLuminance * Exposure;
        std::printf("     night: sky renders %.4f, a bright star renders %.2f  (contrast %.0f:1)\n",
                    static_cast<double>(SkyRenders), static_cast<double>(StarRenders),
                    static_cast<double>(StarRenders / SkyRenders));
        Expect(SkyRenders  < 0.05f, "the night sky renders dark, not as grey daylight");
        Expect(StarRenders > 1.0f,  "while a bright star saturates — visible as a point of light");

        // What the same scene would have done with the curve switched off, so the difference is on the record.
        ExposureConfiguration Flat = Config;
        Flat.ScotopicExponent = 0.0f;
        const float FlatSky = SkyLuminance * ExposureIntegrator::ExposureForLuminance(SkyLuminance, Flat);
        std::printf("     with dark adaptation off the same sky renders %.3f — grey, not night\n",
                    static_cast<double>(FlatSky));
        Expect(FlatSky > 0.15f, "and with the exponent at zero it really does render as mid-grey");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n12. framing moves the exposure less than it used to\n");
    {
        // Reported as "close to the object it looks fine, move away and the scene gets brighter". Part of that
        //    was the sky model's missing ground, fixed separately; the rest is that an UNWEIGHTED frame average
        //    lets the amount of empty space in shot decide how bright the subject renders. Every camera meters
        //    centre-weighted for exactly this reason.
        const float Subject = 1000.0f, Surround = 200.0f;
        float Lowest = 1e9f, Highest = 0.0f, FlatLowest = 1e9f, FlatHighest = 0.0f;
        std::printf("     subject size    metered    renders at   (unweighted would be)\n");
        for (float Half : { 0.45f, 0.30f, 0.18f, 0.10f })
        {
            const float Metered = MeterFrame(SubjectFrame(Subject, Surround, Half));
            const float Rendered = Subject * ExposureIntegrator::ExposureForLuminance(Metered, ExposureConfiguration{});

            // The same frame with every tap counting equally, to show what the weighting is actually buying.
            const float Flat = MeterFrameUnweighted(SubjectFrame(Subject, Surround, Half));
            const float FlatRendered = Subject * ExposureIntegrator::ExposureForLuminance(Flat, ExposureConfiguration{});

            std::printf("     %11.2f   %8.1f   %9.4f   %14.4f\n",
                        static_cast<double>(Half), static_cast<double>(Metered),
                        static_cast<double>(Rendered), static_cast<double>(FlatRendered));
            Lowest = std::fmin(Lowest, Rendered); Highest = std::fmax(Highest, Rendered);
            FlatLowest = std::fmin(FlatLowest, FlatRendered); FlatHighest = std::fmax(FlatHighest, FlatRendered);
        }
        const float Spread = Highest / Lowest, FlatSpread = FlatHighest / FlatLowest;
        std::printf("     spread across framings: %.2fx, against %.2fx unweighted\n",
                    static_cast<double>(Spread), static_cast<double>(FlatSpread));
        Expect(Spread < FlatSpread / 1.15f, "centre weighting measurably steadies the subject");
        Expect(Spread < 3.5f,              "and the residual is bounded");

        // ⚠️ Stated plainly because it is a limit, not a bug: an average meter CANNOT hold a small bright
        //    subject at a fixed brightness against a dark surround, and neither can a real camera — that is
        //    what exposure compensation exists for. The reported white-out was not this. It was the sky model
        //    having no ground, which put a black void across the bottom half of the frame and dragged the
        //    reading by 86×; that is fixed in the atmosphere, not here.
        std::printf("     (an average meter cannot fully hold a shrinking subject — a camera does not either)\n");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n13. a bright hole in a dark room does not pump the exposure as the camera moves\n");
    {
        // 🔴 Reported as "moving back and forth still changes the brightness of the sky and the Cornell box",
        //    with the sharp observation that whatever it was could not be the box's own shading, because the SKY
        //    was moving too and the sky does not depend on where the camera stands. It does not — but the
        //    exposure does, and the exposure is global.
        //
        //    A Cornell frame with the roof oculus in shot is BIMODAL: a room near 1 cd/m² and a hole showing sky
        //    at thousands. Walking about changes how much of the frame the hole covers, and a percentile window
        //    that is narrow at the top lets that second mode slide in and out of the average.
        const float Room = 1.0f, SkyThroughHole = 3000.0f;
        const auto Frame = [&](float SkyShare)
        {
            std::vector<MeterTap> Taps;
            const int N = 48;
            for (int J = 0; J < N; ++J)
                for (int I = 0; I < N; ++I)
                {
                    const float X = (I + 0.5f) / N, Y = (J + 0.5f) / N;
                    // The hole sits high in frame, as a roof opening does.
                    const bool  Sky = Y < SkyShare;
                    const float Wall = Room * (0.5f + 1.0f * ((I * 7 + J * 13) % 97) / 97.0f);
                    Taps.push_back({ Sky ? SkyThroughHole : Wall, X, Y });
                }
            return Taps;
        };

        float Lowest = 1e9f, Highest = 0.0f;
        std::printf("     sky share of frame   metered\n");
        for (float Share : { 0.00f, 0.02f, 0.05f, 0.10f, 0.20f, 0.35f })
        {
            const float Metered = MeterFrame(Frame(Share));
            std::printf("     %17.0f %%   %9.3f\n", static_cast<double>(Share * 100.0f),
                        static_cast<double>(Metered));
            Lowest = std::fmin(Lowest, Metered); Highest = std::fmax(Highest, Metered);
        }
        const float Stops = std::log2(Highest / Lowest);
        std::printf("     the reading moves %.2f stops across the whole sweep\n", static_cast<double>(Stops));
        Expect(Stops < 0.25f, "the exposure does not move as the hole comes into and out of shot");

        // What the previous window did on the identical frames, so the regression is recognisable.
        float NarrowLow = 1e9f, NarrowHigh = 0.0f;
        for (float Share : { 0.00f, 0.02f, 0.05f, 0.10f, 0.20f, 0.35f })
        {
            // Twelve stops is wide enough to let the oculus sky back in, which is what the percentile window
            //    effectively did — the same frames, metered as they used to be.
            const float Metered = MeterWith(Frame(Share), true, 12.0f);
            NarrowLow = std::fmin(NarrowLow, Metered); NarrowHigh = std::fmax(NarrowHigh, Metered);
        }
        std::printf("     the previous 20/5 window moved %.2f stops on the same frames\n",
                    static_cast<double>(std::log2(NarrowHigh / NarrowLow)));
        Expect(std::log2(NarrowHigh / NarrowLow) > 1.5f,
               "a window that admits the hole really does pump — this is the bug being fixed");

        // ⚠️ And the wider window must not have made the meter blind to a scene that genuinely IS bright. A
        //    landscape is mostly sky, and there the sky is the subject rather than an outlier.
        std::vector<MeterTap> Outdoor;
        for (int J = 0; J < 48; ++J)
            for (int I = 0; I < 48; ++I)
            {
                const float X = (I + 0.5f) / 48.0f, Y = (J + 0.5f) / 48.0f;
                Outdoor.push_back({ Y < 0.59f ? 2000.0f : 10000.0f, X, Y });
            }
        const float Landscape = MeterFrame(Outdoor);
        std::printf("     a landscape of 2000 sky over 10000 ground still meters %.0f\n",
                    static_cast<double>(Landscape));
        Expect(Landscape > 1500.0f && Landscape < 9000.0f,
               "a daylight landscape still meters as daylight, not as the darkest thing in it");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n14. colour drains out of the image at the light levels where it drains out of the eye\n");
    {
        // 🔴 Reported as "sunrise still looks like a sunset in reverse, there is no white line on the horizon",
        //    with screenshots taken between 15 and 27 degrees BELOW the horizon — an hour or two before sunrise.
        //    The model is right about the light there: the horizon glow at −15° measures 0.078 cd/m². What was
        //    wrong is that it was rendered at full saturation, and it is almost entirely red, so the red channel
        //    clipped while blue stayed black and a faint glow became a lurid orange band.
        //
        //    Cones give out before rods do. Below about 3 cd/m² colour drains from what a person sees and by
        //    0.003 it is gone — you can still make out a landscape at midnight but not what colour it is.
        ExposureIntegrator Exposure;
        ExposureConfiguration Config{};
        Exposure.AssignConfiguration(Config);

        std::printf("     adapted luminance   colour\n");
        struct Row { const char* Name; float Luminance; };
        const Row Rows[] = {
            { "noon",              8000.0f },
            { "overcast",          2000.0f },
            { "a lit room",           30.0f },
            { "deep dusk",             1.0f },
            { "pre-dawn glow",         0.05f },
            { "starlight",           1.0e-4f },
        };
        float Previous = 2.0f;
        bool  Monotonic = true;
        for (const Row& R : Rows)
        {
            Exposure.ObserveLuminance(std::log(R.Luminance));
            Exposure.Snap();
            const float S = Exposure.QueryColourSaturation();
            std::printf("     %-18s %10.4f  %6.2f\n", R.Name, static_cast<double>(R.Luminance),
                        static_cast<double>(S));
            if (S > Previous + 1e-6f) Monotonic = false;
            Previous = S;
        }
        Expect(Monotonic, "colour never increases as the scene darkens");

        Exposure.ObserveLuminance(std::log(8000.0f)); Exposure.Snap();
        Expect(std::fabs(Exposure.QueryColourSaturation() - 1.0f) < 1e-6f,
               "daylight is fully saturated — nothing about a normal scene changes");
        Exposure.ObserveLuminance(std::log(1.0e-4f)); Exposure.Snap();
        Expect(Exposure.QueryColourSaturation() < 0.01f,
               "and starlight is achromatic, as it is to the eye");

        // 🔴 The specific frame that was wrong. A red-dominated glow at 0.078 cd/m², rendered with and without.
        Exposure.ObserveLuminance(std::log(0.0016f));   // what the meter anchors on in a pre-dawn frame
        Exposure.Snap();
        const float S = Exposure.QueryColourSaturation();
        const float R = 0.150f, G = 0.017f, B = 0.0033f;          // the measured pre-dawn horizon, linear
        const float Y = 0.2126f * R + 0.7152f * G + 0.0722f * B;
        const float Rm = Y + (R - Y) * S, Bm = Y + (B - Y) * S;
        std::printf("     the −15° horizon: R/B %.0f at full colour, %.2f after desaturation (sat %.2f)\n",
                    static_cast<double>(R / B), static_cast<double>(Rm / std::fmax(Bm, 1e-9f)),
                    static_cast<double>(S));
        Expect(R / B > 20.0f,                     "the glow really is almost pure red — this is why it looked lurid");
        Expect(Rm / std::fmax(Bm, 1e-9f) < 2.0f,  "and desaturated it is very nearly neutral — a pale band");

        // ⚠️ And sunrise itself must be untouched: the whole point is to fix the hour BEFORE it, not to wash
        //    the colour out of the thing the user is waiting for.
        Exposure.ObserveLuminance(std::log(160.0f));   // the frame at actual sunrise
        Exposure.Snap();
        std::printf("     at sunrise the adapted level is photopic, colour %.2f\n",
                    static_cast<double>(Exposure.QueryColourSaturation()));
        Expect(Exposure.QueryColourSaturation() > 0.99f, "sunrise keeps all of its colour");

        // Manual mode is the identity switch for the whole adaptive path, and that must include this.
        ExposureConfiguration Manual = Config;
        Manual.Mode = ExposureModeCategory::Manual;
        ExposureIntegrator Fixed;
        Fixed.AssignConfiguration(Manual);
        Fixed.ObserveLuminance(std::log(1.0e-4f)); Fixed.Snap();
        Expect(std::fabs(Fixed.QueryColourSaturation() - 1.0f) < 1e-6f,
               "manual exposure keeps full colour — every pre-A7d image is still reproducible");
    }

    std::printf("\n>>> %s (%d failure%s)\n", Failures == 0 ? "ALL PASS" : "FAILURES", Failures, Failures == 1 ? "" : "s");
    return Failures == 0 ? 0 : 1;
}
