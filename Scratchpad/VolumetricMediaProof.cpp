//============================================================================================================================================
// 📦 Scratchpad/VolumetricMediaProof.cpp — clouds stay in the troposphere, one march serves every medium
//============================================================================================================================================
// Celestial step 5.

#include "DisplayPresentation/VolumetricMedia.h"
#include "SpatialInterface/VolumeMarker.h"
#include "DisplayPresentation/FidelityClassifier.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <initializer_list>

using namespace Frontier;

namespace {
int Failures = 0;
void Expect(bool Condition, const char* What)
{
    if (!Condition) ++Failures;
    std::printf("  %-68s %s\n", What, Condition ? "PASS" : "FAIL");
}
} // namespace

int main()
{
    std::printf("\nVolumetricMedia — clouds, fog, and the markers that move them\n");
    for (int I = 0; I < 108; ++I) std::putchar('=');
    std::printf("\n\n");

    WindSettings Wind{};
    VolumetricBudget Budget{};

    // ── ① the ceiling ──────────────────────────────────────────────────────────────────────────────────────────
    // Clouds are tropospheric. Without a ceiling a Base of 200 km puts cloud OUTSIDE a 60 km atmosphere, which
    // renders happily and shows a shell floating in vacuum from orbit — the thing that looked wrong from space.
    std::printf("1. clouds cannot leave the troposphere\n");
    std::printf("     %12s %12s %12s   %s\n", "requested", "slab base", "slab top", "state");
    {
        bool Clamped = true, Ordinary = true;
        const float Requested[] = { 1500.0f, 8000.0f, 13000.0f, 60000.0f, 200000.0f };
        for (float Base : Requested)
        {
            CloudLayerSettings Cloud{};
            Cloud.Enabled = true; Cloud.Base = Base; Cloud.Thickness = 1200.0f;
            float SlabBase = 0.0f, SlabTop = 0.0f;
            const bool Exists = VolumetricMedia::SlabExtent(Cloud, SlabBase, SlabTop);
            std::printf("     %12.0f %12.0f %12.0f   %s\n", Base, SlabBase, SlabTop,
                        Exists ? "cloud" : "none (above the ceiling)");
            if (SlabTop > Cloud.CeilingMetres + 0.5f) Clamped = false;
            if (Base <= 12000.0f && !Exists) Ordinary = false;
        }
        Expect(Clamped, "no slab ever reaches above the ceiling");
        Expect(Ordinary, "ordinary altitudes still produce a slab");

        // And the density function must agree with the extent, or the march would step through empty space.
        CloudLayerSettings High{};
        High.Enabled = true; High.Base = 100000.0f; High.Thickness = 2000.0f;
        const float Above[3] = { 0.0f, 0.0f, 100500.0f };
        Expect(VolumetricMedia::CloudDensity(High, Wind, Above, 0.0f) == 0.0f,
               "a layer pushed above the ceiling has zero density everywhere");

        // From orbit, looking down, the march must find nothing above the ceiling.
        CloudLayerSettings Normal{};
        Normal.Enabled = true; Normal.Base = 1500.0f; Normal.Thickness = 1200.0f; Normal.Coverage = 0.9f;
        LocalVolumeSettings None{};
        const float Eye[3] = { 0.0f, 0.0f, 400000.0f };
        const float Down[3] = { 0.0f, 0.0f, -1.0f };
        const float Sun[3] = { 0.0f, 0.6f, 0.8f };
        const float Radiance[3] = { 20.0f, 20.0f, 20.0f }, Ambient[3] = { 1.0f, 1.0f, 1.0f };
        const VolumetricSample FromOrbit = VolumetricMedia::March(Normal, None, None, Wind, Budget,
                                                                  Eye, Down, 1.0e6f, Sun, Radiance, Ambient, 0.0f);
        std::printf("     from 400 km looking down: %u steps, transmittance %.3f\n",
                    FromOrbit.StepsTaken, FromOrbit.Transmittance);
        Expect(FromOrbit.StepsTaken > 0u, "the layer is still found from orbit (it is below, not absent)");
        Expect(FromOrbit.Transmittance < 0.99f, "and it actually occludes");
    }

    // ── ② one march, shared ────────────────────────────────────────────────────────────────────────────────────
    std::printf("\n2. one march over the union, not one per medium\n");
    {
        CloudLayerSettings Cloud{};
        Cloud.Enabled = true; Cloud.Base = 1000.0f; Cloud.Thickness = 800.0f; Cloud.Coverage = 0.7f;
        LocalVolumeSettings Fog{};
        Fog.Enabled = true;
        Fog.Centre[0] = 0.0f; Fog.Centre[1] = 900.0f; Fog.Centre[2] = 300.0f;
        Fog.HalfSize[0] = 400.0f; Fog.HalfSize[1] = 400.0f; Fog.HalfSize[2] = 200.0f;
        LocalVolumeSettings None{};

        const float Eye[3] = { 0.0f, 0.0f, 50.0f };
        float Dir[3] = { 0.0f, 0.85f, 0.53f };
        const float L = std::sqrt(Dir[0]*Dir[0] + Dir[1]*Dir[1] + Dir[2]*Dir[2]);
        for (int C = 0; C < 3; ++C) Dir[C] /= L;
        const float Sun[3] = { 0.0f, 0.5f, 0.87f };
        const float Radiance[3] = { 20.0f, 19.0f, 17.0f }, Ambient[3] = { 0.7f, 0.8f, 1.0f };

        const VolumetricSample CloudOnly = VolumetricMedia::March(Cloud, None, None, Wind, Budget,
                                              Eye, Dir, 1.0e5f, Sun, Radiance, Ambient, 0.0f);
        const VolumetricSample FogOnly   = VolumetricMedia::March(CloudLayerSettings{}, None, Fog, Wind, Budget,
                                              Eye, Dir, 1.0e5f, Sun, Radiance, Ambient, 0.0f);
        const VolumetricSample Both      = VolumetricMedia::March(Cloud, None, Fog, Wind, Budget,
                                              Eye, Dir, 1.0e5f, Sun, Radiance, Ambient, 0.0f);

        std::printf("     steps:           cloud %u, fog %u, both %u (the union spans the gap between them)\n",
                    CloudOnly.StepsTaken, FogOnly.StepsTaken, Both.StepsTaken);
        std::printf("     shadow marches:  cloud %u, fog %u, both %u\n",
                    CloudOnly.ShadowMarches, FogOnly.ShadowMarches, Both.ShadowMarches);
        // ⚠️ The saving is NOT fewer steps. The union of two volumes is genuinely longer than either, and with a
        //    bounded step size a longer span costs more steps — that is correct behaviour, not a regression.
        //    What one march buys is one sun-shadow march per occupied step instead of one per MEDIUM per step,
        //    so fog shadows cloud and cloud shadows fog for free. An earlier version of this proof asserted the
        //    step count and was simply measuring the wrong quantity.
        Expect(Both.ShadowMarches <= CloudOnly.ShadowMarches + FogOnly.ShadowMarches,
               "the shared sun-shadow march is not paid twice");

        std::printf("     transmittance: cloud %.4f, fog %.4f, both %.4f\n",
                    CloudOnly.Transmittance, FogOnly.Transmittance, Both.Transmittance);
        Expect(Both.Transmittance <= CloudOnly.Transmittance + 1e-4f &&
               Both.Transmittance <= FogOnly.Transmittance + 1e-4f,
               "two media occlude at least as much as either alone");
    }

    // ── ③ the media behave ─────────────────────────────────────────────────────────────────────────────────────
    std::printf("\n3. coverage and density do what they say\n");
    {
        const float P[3] = { 120.0f, 240.0f, 1600.0f };
        CloudLayerSettings Low{}, High{};
        Low.Enabled = High.Enabled = true;
        Low.Base = High.Base = 1500.0f; Low.Thickness = High.Thickness = 1000.0f;
        Low.Coverage = 0.2f; High.Coverage = 0.9f;
        uint32_t LowHits = 0u, HighHits = 0u;
        for (int I = 0; I < 400; ++I)
        {
            const float Q[3] = { P[0] + static_cast<float>(I) * 37.0f, P[1] + static_cast<float>(I) * 19.0f, P[2] };
            if (VolumetricMedia::CloudDensity(Low, Wind, Q, 0.0f) > 0.0f) ++LowHits;
            if (VolumetricMedia::CloudDensity(High, Wind, Q, 0.0f) > 0.0f) ++HighHits;
        }
        std::printf("     coverage 0.2 fills %u of 400 samples, coverage 0.9 fills %u\n", LowHits, HighHits);
        Expect(HighHits > LowHits, "more coverage fills more of the sky");

        // The height profile must vanish outside the slab and peak inside it.
        Expect(VolumetricMedia::HeightProfile(CloudTypeCategory::Cumulus, 0.0f, 0.5f) < 0.05f,
               "the profile is empty at the very base");
        Expect(VolumetricMedia::HeightProfile(CloudTypeCategory::Cumulus, 0.2f, 0.5f) > 0.5f,
               "and full-bodied a fifth of the way up (cumulus has a flat base)");
    }

    // ── ④ height fog is analytic ───────────────────────────────────────────────────────────────────────────────
    std::printf("\n4. height fog integrates in closed form\n");
    {
        FogSettings Fog{};
        Fog.HeightEnabled = true; Fog.HeightDensity = 0.02f; Fog.FalloffHeight = 400.0f;
        // Against a numerical integral of the same exponential profile.
        const float Start = 10.0f, End = 800.0f, Distance = 1000.0f;
        const float Closed = VolumetricMedia::HeightFogOpticalDepth(Fog, Start, End, Distance);
        double Numeric = 0.0;
        constexpr int kSteps = 20000;
        for (int I = 0; I < kSteps; ++I)
        {
            const float T = (static_cast<float>(I) + 0.5f) / static_cast<float>(kSteps);
            const float Z = Start + (End - Start) * T;
            Numeric += Fog.HeightDensity * std::exp(-Z / Fog.FalloffHeight) * (Distance / kSteps);
        }
        std::printf("     closed form %.6f against numerical %.6f\n", Closed, Numeric);
        Expect(std::fabs(Closed - Numeric) < 1e-3, "the closed form matches the integral");
    }

    // ── ⑤ the marker ───────────────────────────────────────────────────────────────────────────────────────────
    // A local volume has no surface, so the editor needs a proxy to select and drag. The properties that matter:
    // it holds a constant pixel size at any distance, it hides behind the camera, and a drag lands the volume
    // exactly under the pointer.
    std::printf("\n5. the volume marker can be found and dragged\n");
    {
        const float Eye[3] = { 0.0f, 0.0f, 100.0f };
        const float Forward[3] = { 0.0f, 1.0f, 0.0f };
        const float Right[3]   = { 1.0f, 0.0f, 0.0f };
        const float Up[3]      = { 0.0f, 0.0f, 1.0f };
        const uint32_t W = 1280u, H = 720u;
        const float Fov = 60.0f * 3.14159265358979323846f / 180.0f;

        // Constant screen size: the whole reason a marker exists is to be findable from far away.
        float Radius[3];
        int Index = 0;
        for (float Distance : { 100.0f, 1000.0f, 10000.0f })
        {
            const float World[3] = { 0.0f, Distance, 100.0f };
            const MarkerProjection P = VolumeMarkerProjection::Project(World, Eye, Forward, Right, Up, Fov, W, H);
            Radius[Index++] = P.Radius;
        }
        Expect(Radius[0] == Radius[1] && Radius[1] == Radius[2],
               "the marker holds its pixel size at 100 m, 1 km and 10 km");

        // Dead centre projects to the middle of the frame.
        const float Centre[3] = { 0.0f, 500.0f, 100.0f };
        const MarkerProjection Middle = VolumeMarkerProjection::Project(Centre, Eye, Forward, Right, Up, Fov, W, H);
        std::printf("     a marker straight ahead lands at (%.1f, %.1f) of %ux%u\n", Middle.X, Middle.Y, W, H);
        Expect(std::fabs(Middle.X - static_cast<float>(W) * 0.5f) < 0.5f &&
               std::fabs(Middle.Y - static_cast<float>(H) * 0.5f) < 0.5f,
               "a marker on the view axis lands in the centre of the frame");

        const float Behind[3] = { 0.0f, -500.0f, 100.0f };
        const MarkerProjection Back = VolumeMarkerProjection::Project(Behind, Eye, Forward, Right, Up, Fov, W, H);
        Expect(!Back.OnScreen, "a marker behind the camera does not draw");

        // Picking: the nearer of two overlapping markers wins.
        VolumeMarker Markers[2]{};
        Markers[0].World[0] = 0.0f; Markers[0].World[1] = 900.0f; Markers[0].World[2] = 100.0f;
        Markers[1].World[0] = 0.0f; Markers[1].World[1] = 300.0f; Markers[1].World[2] = 100.0f;
        const int32_t Picked = VolumeMarkerProjection::Pick(Markers, 2u, Eye, Forward, Right, Up, Fov, W, H,
                                                            static_cast<float>(W) * 0.5f,
                                                            static_cast<float>(H) * 0.5f);
        Expect(Picked == 1, "clicking two overlapping markers selects the nearer");

        // A drag must land the volume under the pointer, not merely near it.
        const float Start[3] = { 0.0f, 500.0f, 100.0f };
        float Moved[3];
        VolumeMarkerProjection::DragToPointer(Start, Eye, Forward, Right, Up, Fov, W, H, 900.0f, 200.0f, Moved);
        const MarkerProjection After = VolumeMarkerProjection::Project(Moved, Eye, Forward, Right, Up, Fov, W, H);
        std::printf("     dragged to pointer (900, 200), marker now projects to (%.1f, %.1f)\n", After.X, After.Y);
        Expect(std::fabs(After.X - 900.0f) < 0.5f && std::fabs(After.Y - 200.0f) < 0.5f,
               "the dragged volume lands exactly under the pointer");

        // The drag plane keeps the volume at its original depth rather than pulling it toward the camera.
        const float DepthBefore = VolumeMarkerProjection::Project(Start, Eye, Forward, Right, Up, Fov, W, H).Depth;
        Expect(std::fabs(After.Depth - DepthBefore) < 0.5f, "and stays at the depth it started at");

        Expect(VolumeMarkerProjection::GlyphPath(VolumeMarkerCategory::LocalCloud) != nullptr &&
               VolumeMarkerProjection::GlyphPath(VolumeMarkerCategory::LocalFog) != nullptr,
               "every marker category has an SVG glyph");
    }

    // ── ⑥ god rays ─────────────────────────────────────────────────────────────────────────────────────────────
    // A crepuscular shaft is the sun-visibility term the march already computes, evaluated against SCENE
    // occlusion rather than only against cloud density. The properties that matter, and that a screen-space
    // radial blur cannot deliver:
    //    · the beam is made of MEDIUM — no fog, no shaft, however sharp the shadow,
    //    · gaps in the occluder produce brighter columns than the shadowed parts, which is the effect itself,
    //    · it works with the sun off-screen, because nothing is being smeared outward from the sun's pixel.
    std::printf("\n6. god rays are the sun-visibility term, carved by scene occlusion\n");
    {
        // A slatted occluder overhead: alternating 20 m bands of blocked and open sky. This is the classic
        //    louvre / cloud-gap arrangement, and it makes the answer countable rather than impressionistic.
        // ⚠️ THE QUERY MUST FOLLOW THE SUN RAY. "Is the sun visible from P" means: walk from P toward the sun and
        //    see what you meet. An earlier version of this occluder tested the slat directly above P, which
        //    answers "which slat is P under" — a different question, and one that produces vertical columns
        //    that do not move when the sun moves. The renders looked wrong and the assertions still passed,
        //    because a gap was still brighter than a slat; only the shafts' ANGLE was meaningless.
        struct Louvre
        {
            static float Visibility(const float P[3], void* Context) noexcept
            {
                const float PlaneZ = 120.0f;
                const float* Sun = static_cast<const float*>(Context);
                if (P[2] >= PlaneZ) return 1.0f;                 // above the slats: nothing blocks
                if (Sun[2] <= 1e-3f) return 1.0f;                // sun on the horizon: no shaft geometry
                const float T = (PlaneZ - P[2]) / Sun[2];        // along the sun ray to the occluder plane
                const float X = P[0] + Sun[0] * T;               // where it crosses
                return (static_cast<int>(std::floor(X / 20.0f)) & 1) == 0 ? 1.0f : 0.0f;
            }
        };

        LocalVolumeSettings Haze{};
        Haze.Enabled = true;
        Haze.Centre[0] = 0.0f; Haze.Centre[1] = 120.0f; Haze.Centre[2] = 60.0f;
        Haze.HalfSize[0] = 200.0f; Haze.HalfSize[1] = 60.0f; Haze.HalfSize[2] = 60.0f;
        Haze.Density = 2.2f; Haze.Coverage = 0.98f; Haze.Scale = 400.0f;
        LocalVolumeSettings None{};
        CloudLayerSettings NoCloud{};

        VolumetricBudget Shafted = Budget;  Shafted.GodRaySamples = 16u;
        VolumetricBudget Plain   = Budget;  Plain.GodRaySamples = 0u;

        const float Eye[3] = { 0.0f, 0.0f, 40.0f };
        const float Sun[3] = { 0.0f, 0.20f, 0.98f };
        const float Radiance[3] = { 30.0f, 29.0f, 27.0f }, Ambient[3] = { 0.4f, 0.5f, 0.7f };

        // Sample across the slats. A lit column and a shadowed one must differ; without the shaft term they
        //    cannot, because nothing else in the march knows the occluder exists.
        auto ColumnBrightness = [&](float X, const VolumetricBudget& B, bool WithScene) -> double
        {
            float Direction[3] = { X - Eye[0], 120.0f - Eye[1], 60.0f - Eye[2] };
            const float L = std::sqrt(Direction[0]*Direction[0] + Direction[1]*Direction[1] + Direction[2]*Direction[2]);
            for (int C = 0; C < 3; ++C) Direction[C] /= L;
            const VolumetricSample S = VolumetricMedia::March(
                NoCloud, Haze, None, Wind, B, Eye, Direction, 1000.0f, Sun, Radiance, Ambient, 0.0f,
                WithScene ? &Louvre::Visibility : nullptr, const_cast<float*>(Sun));
            return S.Scatter[0] + S.Scatter[1] + S.Scatter[2];
        };

        const double OpenBand    = ColumnBrightness(10.0f, Shafted, true);    // band 0: open
        const double BlockedBand = ColumnBrightness(30.0f, Shafted, true);    // band 1: blocked
        const double NoShafts    = ColumnBrightness(30.0f, Plain, false);
        std::printf("     open column %.4f, blocked column %.4f, shafts off %.4f\n",
                    OpenBand, BlockedBand, NoShafts);
        Expect(OpenBand > BlockedBand * 1.5,
               "a gap in the occluder is markedly brighter than the shadowed band");
        Expect(NoShafts > BlockedBand,
               "with shafts off the occluder is ignored entirely, as it was before");

        // No medium, no shaft. This is what separates a real beam from a screen-space glow: the light is only
        //    visible because something is scattering it toward the eye.
        LocalVolumeSettings Vacuum = Haze;
        Vacuum.Enabled = false;
        float Direction[3] = { 10.0f - Eye[0], 120.0f, 20.0f };
        const float DL = std::sqrt(Direction[0]*Direction[0] + Direction[1]*Direction[1] + Direction[2]*Direction[2]);
        for (int C = 0; C < 3; ++C) Direction[C] /= DL;
        const VolumetricSample Empty = VolumetricMedia::March(
            NoCloud, Vacuum, None, Wind, Shafted, Eye, Direction, 1000.0f, Sun, Radiance, Ambient, 0.0f,
            &Louvre::Visibility, const_cast<float*>(Sun));
        std::printf("     with no medium at all: %.6f\n",
                    Empty.Scatter[0] + Empty.Scatter[1] + Empty.Scatter[2]);
        Expect(Empty.Scatter[0] + Empty.Scatter[1] + Empty.Scatter[2] < 1e-6,
               "no medium means no shaft, however sharp the shadow");

        // The sun off-screen. A radial blur has no pixel to work from here; this does not care, because the
        //    term is evaluated in world space at each march step.
        const float Behind[3] = { 0.0f, -0.20f, 0.98f };
        const VolumetricSample OffScreen = VolumetricMedia::March(
            NoCloud, Haze, None, Wind, Shafted, Eye, Direction, 1000.0f, Behind, Radiance, Ambient, 0.0f,
            &Louvre::Visibility, const_cast<float*>(Sun));
        std::printf("     sun behind the camera: %.4f of in-scatter still resolved\n",
                    OffScreen.Scatter[0] + OffScreen.Scatter[1] + OffScreen.Scatter[2]);
        Expect(OffScreen.Scatter[0] + OffScreen.Scatter[1] + OffScreen.Scatter[2] > 0.0,
               "shafts still resolve with the sun out of frame");

        // The budget must actually gate the work, or the tier ladder is decorative.
        const VolumetricSample Counted = VolumetricMedia::March(
            NoCloud, Haze, None, Wind, Shafted, Eye, Direction, 1000.0f, Sun, Radiance, Ambient, 0.0f,
            &Louvre::Visibility, const_cast<float*>(Sun));
        const VolumetricSample Uncounted = VolumetricMedia::March(
            NoCloud, Haze, None, Wind, Plain, Eye, Direction, 1000.0f, Sun, Radiance, Ambient, 0.0f,
            &Louvre::Visibility, const_cast<float*>(Sun));
        std::printf("     occlusion lookups: budget 16 -> %u, budget 0 -> %u\n",
                    Counted.ShaftSamples, Uncounted.ShaftSamples);
        Expect(Counted.ShaftSamples > 0u && Uncounted.ShaftSamples == 0u,
               "GodRaySamples 0 costs nothing at all, so Minimal really is free");

        // ⚠️ THE SHAFTS MUST MOVE WHEN THE SUN MOVES. Every brightness assertion above passes even with an
        //    occlusion query that ignores the sun entirely — a gap is still brighter than a slat, so brightness
        //    alone cannot tell a real shaft from a vertical column sitting under a hole. That was the actual
        //    defect: the renders looked wrong long before any check complained.
        //
        //    ⚠️ And the obvious test does not work either. Comparing two sun azimuths directly scored 21.3% for
        //    the correct occluder and 13.3% for the broken one — because HenyeyGreenstein depends on the angle
        //    between the view ray and the sun, so swinging the sun changes every sample whether or not anything
        //    is occluding. The metric was mostly measuring the phase function.
        //
        //    So each sun angle is NORMALISED against an unoccluded march at the same angle. That divides the
        //    phase change out and leaves only the shadow pattern, which is the thing under test.
        {
            const float East[3] = { 0.55f, 0.20f, 0.81f };
            const float West[3] = { -0.55f, 0.20f, 0.81f };
            double Difference = 0.0, Magnitude = 0.0;
            for (int I = 0; I < 24; ++I)
            {
                const float X = -60.0f + static_cast<float>(I) * 5.0f;
                float Ray[3] = { X - Eye[0], 120.0f - Eye[1], 60.0f - Eye[2] };
                const float RL = std::sqrt(Ray[0]*Ray[0] + Ray[1]*Ray[1] + Ray[2]*Ray[2]);
                for (int C = 0; C < 3; ++C) Ray[C] /= RL;

                auto Shadowed = [&](const float* SunDirection) -> double
                {
                    const VolumetricSample Occluded = VolumetricMedia::March(
                        NoCloud, Haze, None, Wind, Shafted, Eye, Ray, 1000.0f, SunDirection, Radiance, Ambient,
                        0.0f, &Louvre::Visibility, const_cast<float*>(SunDirection));
                    const VolumetricSample Open = VolumetricMedia::March(
                        NoCloud, Haze, None, Wind, Plain, Eye, Ray, 1000.0f, SunDirection, Radiance, Ambient,
                        0.0f, nullptr, nullptr);
                    const double A = Occluded.Scatter[0] + Occluded.Scatter[1] + Occluded.Scatter[2];
                    const double B = Open.Scatter[0] + Open.Scatter[1] + Open.Scatter[2];
                    return B > 1e-9 ? A / B : 1.0;      // the shadow pattern alone, phase divided out
                };

                const double Ea = Shadowed(East), We = Shadowed(West);
                Difference += std::fabs(Ea - We);
                Magnitude  += Ea + We;
            }
            const double Relative = Magnitude > 1e-9 ? Difference / Magnitude : 0.0;
            std::printf("     swinging the sun east/west moves the shadow pattern by %.1f%%\n", Relative * 100.0);
            // 15.3% with a correct occluder against 2.7% for a sun-independent one, measured; 8% sits between them.
            Expect(Relative > 0.08,
                   "the shafts follow the sun — a sun-independent occluder scores near zero here");
        }
    }

    // ── ⑦ the tier ladder actually reaches the march ───────────────────────────────────────────────────────────
    // The budgets were seated in FidelityClassifier during step 0 and it is easy for them to stay decorative.
    // This walks the five tiers, builds the budget the way a caller must, and checks the march responds.
    std::printf("\n7. the tier ladder drives the march\n");
    {
        FidelityClassifier Classifier;
        const FidelityCategory Tiers[5] = { FidelityCategory::MinimalFidelity, FidelityCategory::EconomyFidelity,
                                            FidelityCategory::StandardFidelity, FidelityCategory::UltraFidelity,
                                            FidelityCategory::ReferenceFidelity };
        const char* Names[5] = { "Minimal", "Economy", "Standard", "Ultra", "Reference" };

        struct Louvre2 { static float Visibility(const float P[3], void*) noexcept
            { return (static_cast<int>(std::floor(P[0] / 20.0f)) & 1) == 0 ? 1.0f : 0.0f; } };

        LocalVolumeSettings Haze{};
        Haze.Enabled = true;
        Haze.Centre[0] = 0.0f; Haze.Centre[1] = 120.0f; Haze.Centre[2] = 60.0f;
        Haze.HalfSize[0] = 200.0f; Haze.HalfSize[1] = 80.0f; Haze.HalfSize[2] = 60.0f;
        Haze.Density = 1.0f; Haze.Coverage = 0.95f; Haze.Scale = 500.0f;
        LocalVolumeSettings None{};
        CloudLayerSettings NoCloud{};

        const float Eye[3] = { 0.0f, 0.0f, 40.0f };
        float Direction[3] = { 0.05f, 0.94f, 0.34f };
        const float DL = std::sqrt(Direction[0]*Direction[0] + Direction[1]*Direction[1] + Direction[2]*Direction[2]);
        for (int C = 0; C < 3; ++C) Direction[C] /= DL;
        const float Sun[3] = { 0.0f, 0.20f, 0.98f };
        const float Radiance[3] = { 30.0f, 29.0f, 27.0f }, Ambient[3] = { 0.4f, 0.5f, 0.7f };

        std::printf("     %-11s %7s %7s %9s %11s\n", "tier", "cloud", "local", "godray", "shaft taps");
        uint32_t Previous = 0u;
        bool Rising = true, MinimalFree = false;
        for (int T = 0; T < 5; ++T)
        {
            const FidelityCriteria Criteria = Classifier.ConstructCriteria(Tiers[T]);
            VolumetricBudget Budgeted{};
            Budgeted.CloudSteps    = Criteria.CloudMarchStepCount;
            Budgeted.LocalSteps    = Criteria.LocalVolumeStepCount;
            Budgeted.LightTaps     = Criteria.CloudLightTapCount;
            Budgeted.CoverageMargin= Criteria.CloudCoverageMargin;
            Budgeted.GodRaySamples = Criteria.GodRaySampleCount;

            const VolumetricSample Sample = VolumetricMedia::March(
                NoCloud, Haze, None, Wind, Budgeted, Eye, Direction, 1000.0f,
                Sun, Radiance, Ambient, 0.0f, &Louvre2::Visibility, nullptr);

            std::printf("     %-11s %7u %7u %9u %11u\n", Names[T],
                        Criteria.CloudMarchStepCount, Criteria.LocalVolumeStepCount,
                        Criteria.GodRaySampleCount, Sample.ShaftSamples);

            if (T == 0 && Sample.ShaftSamples == 0u) MinimalFree = true;
            if (T > 0 && Sample.ShaftSamples < Previous) Rising = false;
            Previous = Sample.ShaftSamples;
        }
        Expect(MinimalFree, "Minimal spends nothing on shafts, as the ladder says");
        Expect(Rising, "higher tiers spend more on them");
    }

    std::printf("\n");
    for (int I = 0; I < 108; ++I) std::putchar('=');
    std::printf("\n%s\n\n", Failures == 0 ? "  the media behave" : "  THE MEDIA DO NOT BEHAVE");
    return Failures == 0 ? 0 : 1;
}
