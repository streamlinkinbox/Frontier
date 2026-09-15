//============================================================================================================================================
// 📦 Scratchpad/AtmosphericOpticsProof.cpp — the bow is where optics says it is
//============================================================================================================================================
// Celestial step 7. The rainbow is checked against published angles rather than against itself, because the whole
//    argument for deriving it is that the derivation produces facts a hand-tuned arc cannot.

#include "DisplayPresentation/AtmosphericOptics.h"

#include <cmath>
#include <cstdio>
#include <cstdint>

using namespace Frontier;

namespace {
int Failures = 0;
void Check(const char* Label, double Actual, double Expected, double Tolerance, const char* Unit)
{
    const double Error = std::fabs(Actual - Expected);
    const bool Ok = Error <= Tolerance;
    if (!Ok) ++Failures;
    std::printf("  %-56s %8.3f vs %8.3f %-4s err %6.3f  %s\n",
                Label, Actual, Expected, Unit, Error, Ok ? "PASS" : "FAIL");
}
void Expect(bool Condition, const char* What)
{
    if (!Condition) ++Failures;
    std::printf("  %-68s %s\n", What, Condition ? "PASS" : "FAIL");
}
constexpr double kPi = 3.14159265358979323846;
double Degrees(double R) { return R * 180.0 / kPi; }
} // namespace

int main()
{
    std::printf("\nAtmosphericOptics — the rainbow, from Descartes\n");
    for (int I = 0; I < 108; ++I) std::putchar('=');
    std::printf("\n\n");

    // ── ① refractive index ─────────────────────────────────────────────────────────────────────────────────────
    std::printf("1. water disperses, which is why there are colours at all\n");
    Check("n at 700 nm (red)",    AtmosphericOptics::WaterIndex(700.0f), 1.3306, 0.002, "-");
    Check("n at 400 nm (violet)", AtmosphericOptics::WaterIndex(400.0f), 1.3433, 0.002, "-");
    Expect(AtmosphericOptics::WaterIndex(400.0f) > AtmosphericOptics::WaterIndex(700.0f),
           "violet is bent more than red");

    // ── ② the Descartes angles ─────────────────────────────────────────────────────────────────────────────────
    // Published: primary red 42.4 deg, violet 40.7; secondary red 50.4, violet 53.4.
    std::printf("\n2. the bows sit at their published angles\n");
    Check("primary red 700 nm",     Degrees(AtmosphericOptics::RainbowAngle(700.0f, 1u)), 42.4, 0.3, "deg");
    Check("primary violet 400 nm",  Degrees(AtmosphericOptics::RainbowAngle(400.0f, 1u)), 40.7, 0.3, "deg");
    Check("secondary red 700 nm",   Degrees(AtmosphericOptics::RainbowAngle(700.0f, 2u)), 50.4, 0.3, "deg");
    Check("secondary violet 400 nm",Degrees(AtmosphericOptics::RainbowAngle(400.0f, 2u)), 53.4, 0.3, "deg");

    // ── ③ the reversal ─────────────────────────────────────────────────────────────────────────────────────────
    // The property a hand-drawn arc almost always gets wrong: the second bow's colours run the other way, because
    // the extra internal reflection flips the ordering. It is not an artistic choice.
    std::printf("\n3. the secondary bow's colours are reversed\n");
    {
        const float PrimaryRed    = AtmosphericOptics::RainbowAngle(700.0f, 1u);
        const float PrimaryViolet = AtmosphericOptics::RainbowAngle(400.0f, 1u);
        const float SecondRed     = AtmosphericOptics::RainbowAngle(700.0f, 2u);
        const float SecondViolet  = AtmosphericOptics::RainbowAngle(400.0f, 2u);
        std::printf("     primary:   red %.2f deg is OUTSIDE violet %.2f deg\n",
                    Degrees(PrimaryRed), Degrees(PrimaryViolet));
        std::printf("     secondary: red %.2f deg is INSIDE violet %.2f deg\n",
                    Degrees(SecondRed), Degrees(SecondViolet));
        Expect(PrimaryRed > PrimaryViolet, "primary: red on the outside");
        Expect(SecondRed < SecondViolet,   "secondary: red on the inside — the order is flipped");
        Expect(SecondViolet > PrimaryRed,  "the secondary bow lies outside the primary");
    }

    // ── ④ Alexander's band ─────────────────────────────────────────────────────────────────────────────────────
    std::printf("\n4. Alexander's dark band lies between the bows\n");
    {
        RainbowSettings Settings{};
        const float Inside  = 0.68f;   // ~39 deg, inside the primary
        const float Between = 0.80f;   // ~46 deg, between the two
        const float Outside = 0.97f;   // ~56 deg, outside the secondary
        const float A = AtmosphericOptics::AlexanderAttenuation(Settings, Inside);
        const float B = AtmosphericOptics::AlexanderAttenuation(Settings, Between);
        const float C = AtmosphericOptics::AlexanderAttenuation(Settings, Outside);
        std::printf("     attenuation: inside %.3f, between %.3f, outside %.3f\n", A, B, C);
        Expect(B < A && B < C, "the sky between the bows is darker than either side");
    }

    // ── ⑤ the bow only appears where it should ─────────────────────────────────────────────────────────────────
    std::printf("\n5. a bow needs rain, a low sun, and the right direction\n");
    {
        RainbowSettings Settings{};
        // Sun low in the east; the antisolar point is then low in the west.
        float Sun[3] = { 0.0f, -0.30f, 0.20f };
        const float L = std::sqrt(Sun[0]*Sun[0] + Sun[1]*Sun[1] + Sun[2]*Sun[2]);
        for (int I = 0; I < 3; ++I) Sun[I] /= L;

        float Rgb[3];
        // Straight at the antisolar point: inside the bow, no colour.
        float Anti[3] = { -Sun[0], -Sun[1], -Sun[2] };
        AtmosphericOptics::Rainbow(Settings, Anti, Sun, 1.0f, 5000.0f, Rgb);
        Expect(Rgb[0] + Rgb[1] + Rgb[2] < 1e-4f, "nothing at the antisolar point itself");

        // 42 degrees off it: the bow.
        float OnBow[3];
        {
            // Rotate the antisolar direction by 42 degrees in the vertical plane.
            const float A = 42.0f * static_cast<float>(kPi) / 180.0f;
            const float Up[3] = { 0.0f, 0.0f, 1.0f };
            float Side[3] = { Anti[1] * Up[2] - Anti[2] * Up[1],
                              Anti[2] * Up[0] - Anti[0] * Up[2],
                              Anti[0] * Up[1] - Anti[1] * Up[0] };
            const float SL = std::sqrt(Side[0]*Side[0] + Side[1]*Side[1] + Side[2]*Side[2]);
            for (int I = 0; I < 3; ++I) Side[I] /= SL;
            float Perp[3] = { Side[1] * Anti[2] - Side[2] * Anti[1],
                              Side[2] * Anti[0] - Side[0] * Anti[2],
                              Side[0] * Anti[1] - Side[1] * Anti[0] };
            for (int I = 0; I < 3; ++I) OnBow[I] = Anti[I] * std::cos(A) + Perp[I] * std::sin(A);
        }
        AtmosphericOptics::Rainbow(Settings, OnBow, Sun, 1.0f, 5000.0f, Rgb);
        const float OnBowSum = Rgb[0] + Rgb[1] + Rgb[2];
        std::printf("     at 42 deg from the antisolar point: rgb %.4f %.4f %.4f\n", Rgb[0], Rgb[1], Rgb[2]);
        Expect(OnBowSum > 0.05f, "there is a bow at 42 degrees");

        // No rain, no bow — however good the geometry.
        AtmosphericOptics::Rainbow(Settings, OnBow, Sun, 0.0f, 5000.0f, Rgb);
        Expect(Rgb[0] + Rgb[1] + Rgb[2] < 1e-6f, "no rain means no bow");

        // Sun high overhead: the bow is below the horizon and must not appear.
        float High[3] = { 0.0f, 0.0f, 1.0f };
        AtmosphericOptics::Rainbow(Settings, OnBow, High, 1.0f, 5000.0f, Rgb);
        std::printf("     with the sun overhead: rgb %.4f %.4f %.4f\n", Rgb[0], Rgb[1], Rgb[2]);
        Expect(Rgb[0] + Rgb[1] + Rgb[2] < OnBowSum,
               "a high sun gives no bow at that direction");
    }

    // ── ⑤b the bow is in the air, not on the floor ─────────────────────────────────────────────────────────────
    // A rainbow is light returned by drops suspended between the viewer and whatever is behind them. A ray that
    // hits the ground two metres away has crossed almost no rain and must show almost no bow. The first render
    // of this system painted the arc across the ground plane because the caller passed one constant visibility
    // for every pixel, and the bow appeared to be lying on the floor.
    std::printf("\n5b. the bow needs a depth of rain to form in\n");
    {
        RainbowSettings Settings{};
        float Sun[3] = { 0.0f, -0.30f, 0.20f };
        const float L = std::sqrt(Sun[0]*Sun[0] + Sun[1]*Sun[1] + Sun[2]*Sun[2]);
        for (int I = 0; I < 3; ++I) Sun[I] /= L;
        float Anti[3] = { -Sun[0], -Sun[1], -Sun[2] };
        float OnBow[3];
        {
            const float A = 42.0f * static_cast<float>(kPi) / 180.0f;
            const float Up[3] = { 0.0f, 0.0f, 1.0f };
            float Side[3] = { Anti[1] * Up[2] - Anti[2] * Up[1],
                              Anti[2] * Up[0] - Anti[0] * Up[2],
                              Anti[0] * Up[1] - Anti[1] * Up[0] };
            const float SL = std::sqrt(Side[0]*Side[0] + Side[1]*Side[1] + Side[2]*Side[2]);
            for (int I = 0; I < 3; ++I) Side[I] /= SL;
            float Perp[3] = { Side[1] * Anti[2] - Side[2] * Anti[1],
                              Side[2] * Anti[0] - Side[0] * Anti[2],
                              Side[0] * Anti[1] - Side[1] * Anti[0] };
            for (int I = 0; I < 3; ++I) OnBow[I] = Anti[I] * std::cos(A) + Perp[I] * std::sin(A);
        }

        float Sky[3], Wall[3], Mid[3];
        AtmosphericOptics::Rainbow(Settings, OnBow, Sun, 1.0f, 5000.0f, Sky);
        AtmosphericOptics::Rainbow(Settings, OnBow, Sun, 1.0f, 2.0f, Wall);
        AtmosphericOptics::Rainbow(Settings, OnBow, Sun, 1.0f, 150.0f, Mid);
        const float SkySum  = Sky[0] + Sky[1] + Sky[2];
        const float WallSum = Wall[0] + Wall[1] + Wall[2];
        const float MidSum  = Mid[0] + Mid[1] + Mid[2];
        std::printf("     open sky (5 km of rain): %.4f\n", SkySum);
        std::printf("     a wall 2 m away        : %.4f\n", WallSum);
        std::printf("     rain 150 m deep        : %.4f\n", MidSum);
        Expect(SkySum > 0.05f, "the bow is full strength against open sky");
        Expect(WallSum < SkySum * 0.05f, "and essentially absent on geometry two metres away");
        Expect(MidSum > WallSum && MidSum < SkySum, "a shallow shower fades rather than cutting off");
    }

    // ── ⑥ aerial perspective ───────────────────────────────────────────────────────────────────────────────────
    std::printf("\n6. distance takes the colour of the air\n");
    {
        AtmosphereMedium Medium{};
        AtmosphereLight Light{};
        Light.Direction[0] = 0.0f; Light.Direction[1] = 0.6f; Light.Direction[2] = 0.8f;
        const float Direction[3] = { 0.0f, 1.0f, 0.0f };
        const float Sky[3] = { 0.22f, 0.32f, 0.53f };

        float Near[3], NearScatter[3], Far[3], FarScatter[3];
        AtmosphericOptics::AerialPerspective(Medium, Light, 100.0f, 2.0f, 2.0f, Direction, Sky, Near, NearScatter);
        AtmosphericOptics::AerialPerspective(Medium, Light, 40000.0f, 2.0f, 2.0f, Direction, Sky, Far, FarScatter);
        std::printf("     100 m : transmittance %.4f %.4f %.4f\n", Near[0], Near[1], Near[2]);
        std::printf("     40 km : transmittance %.4f %.4f %.4f\n", Far[0], Far[1], Far[2]);
        Expect(Far[0] < Near[0] && Far[1] < Near[1] && Far[2] < Near[2],
               "further is hazier");
        Expect(Far[2] < Far[0], "blue is extinguished fastest, so distant things go warm then pale");
        Expect(FarScatter[2] > NearScatter[2], "and the air's own light builds with distance");
        Expect(Near[0] > 0.99f, "at 100 m there is essentially no haze");
    }

    // ── ⑦ lens flare ───────────────────────────────────────────────────────────────────────────────────────────
    // A flare is an artefact of the camera, not the world. The properties that separate a real one from a
    // decorative sprite: it is occluded with the sun, it lies on the sun-to-centre line because that is the
    // optical axis, and it leaves with the sun rather than hanging in frame.
    std::printf("\n7. the lens flare belongs to the lens\n");
    {
        AtmosphericOptics::LensFlareSettings Flare{};
        const float Aspect = 16.0f / 9.0f;
        const float Sun[2] = { 0.72f, 0.30f };

        // Sample a grid and total the energy, which is a stabler measure than any single pixel.
        auto TotalEnergy = [&](const float SunUv[2], float Visibility) -> double
        {
            double Sum = 0.0;
            for (int Y = 0; Y < 90; ++Y)
                for (int X = 0; X < 160; ++X)
                {
                    const float Uv[2] = { (static_cast<float>(X) + 0.5f) / 160.0f,
                                          (static_cast<float>(Y) + 0.5f) / 90.0f };
                    float Rgb[3];
                    AtmosphericOptics::LensFlare(Flare, Uv, SunUv, Visibility, Aspect, Rgb);
                    Sum += Rgb[0] + Rgb[1] + Rgb[2];
                }
            return Sum;
        };

        const double Visible  = TotalEnergy(Sun, 1.0f);
        const double Occluded = TotalEnergy(Sun, 0.0f);
        const double Half     = TotalEnergy(Sun, 0.5f);
        std::printf("     sun visible %.3f, half-occluded %.3f, fully occluded %.3f\n",
                    Visible, Half, Occluded);
        Expect(Visible > 1.0, "a visible sun produces a flare");
        Expect(Occluded == 0.0, "an occluded sun produces NONE — light that never entered the lens cannot bounce");
        Expect(Half < Visible && Half > Occluded, "partial occlusion scales it rather than switching it");

        const float OffScreen[2] = { 1.6f, 0.3f };
        std::printf("     sun off-screen: %.4f\n", TotalEnergy(OffScreen, 1.0f));
        Expect(TotalEnergy(OffScreen, 1.0f) == 0.0, "a sun outside the frame produces no flare");

        // The ghosts must lie along the sun-to-centre line, which is what makes it read as a lens rather than a
        //    starburst pinned to the sun.
        const float Centre[2] = { 0.5f, 0.5f };
        double OnAxis = 0.0, OffAxis = 0.0;
        for (int I = 1; I < 40; ++I)
        {
            const float T = static_cast<float>(I) / 40.0f;
            // Along the line from the sun through the centre and out the other side.
            const float Along[2] = { Sun[0] + (Centre[0] - Sun[0]) * T * 2.0f,
                                     Sun[1] + (Centre[1] - Sun[1]) * T * 2.0f };
            // The same distance from centre, but rotated a quarter turn away from that line.
            const float Dx = Along[0] - Centre[0], Dy = Along[1] - Centre[1];
            const float Rotated[2] = { Centre[0] - Dy, Centre[1] + Dx };
            float A[3], B[3];
            AtmosphericOptics::LensFlare(Flare, Along, Sun, 1.0f, Aspect, A);
            AtmosphericOptics::LensFlare(Flare, Rotated, Sun, 1.0f, Aspect, B);
            OnAxis  += A[0] + A[1] + A[2];
            OffAxis += B[0] + B[1] + B[2];
        }
        std::printf("     energy along the sun-centre axis %.4f, perpendicular %.4f\n", OnAxis, OffAxis);
        Expect(OnAxis > OffAxis * 2.0, "the ghosts lie on the optical axis, not scattered around the sun");

        // Tier keying: fewer ghosts must mean less flare, so the ladder has something to scale.
        AtmosphericOptics::LensFlareSettings Minimal = Flare; Minimal.GhostCount = 0u;
        AtmosphericOptics::LensFlareSettings Rich = Flare;    Rich.GhostCount = 8u;
        double Lean = 0.0, Full = 0.0;
        for (int Y = 0; Y < 90; ++Y)
            for (int X = 0; X < 160; ++X)
            {
                const float Uv[2] = { (static_cast<float>(X) + 0.5f) / 160.0f,
                                      (static_cast<float>(Y) + 0.5f) / 90.0f };
                float A[3], B[3];
                AtmosphericOptics::LensFlare(Minimal, Uv, Sun, 1.0f, Aspect, A);
                AtmosphericOptics::LensFlare(Rich, Uv, Sun, 1.0f, Aspect, B);
                Lean += A[0] + A[1] + A[2];
                Full += B[0] + B[1] + B[2];
            }
        std::printf("     0 ghosts %.3f, 8 ghosts %.3f\n", Lean, Full);
        Expect(Full > Lean, "the ghost count is a real budget the tiers can spend");
    }

    // ── ⑧ the four lens types are different lenses, not one effect with a slider ────────────────────────────────
    // Each type is a different optical assembly and emphasises different components. The test that matters is
    // that they are DISTINGUISHABLE BY SHAPE rather than by brightness: anamorphic with the intensity turned up
    // is not starburst, so comparing totals would not catch a stub that returned the same image four times.
    std::printf("\n8. the four lens types are optically distinct\n");
    {
        using Category = AtmosphericOptics::LensFlareCategory;
        const float Aspect = 16.0f / 9.0f;
        const float Sun[2] = { 0.68f, 0.34f };

        struct Named { Category Type; const char* Name; };
        const Named Types[] = { { Category::Cinematic,  "Cinematic"  },
                                { Category::Anamorphic, "Anamorphic" },
                                { Category::Starburst,  "Starburst"  },
                                { Category::Halo,       "Halo"       } };

        // Measure each type's energy in three regions that isolate its signature component:
        //    · a narrow horizontal band through the sun   -> the streak
        //    · a ring at the halo radius about the axis    -> the halo
        //    · the ghost line between the sun and centre   -> the ghosts
        struct Signature { double Streak, Halo, Ghosts, Total; };
        Signature Measured[4]{};

        for (int T = 0; T < 4; ++T)
        {
            AtmosphericOptics::LensFlareSettings Settings{};
            Settings.Category = Types[T].Type;
            for (int Y = 0; Y < 180; ++Y)
                for (int X = 0; X < 320; ++X)
                {
                    const float Uv[2] = { (static_cast<float>(X) + 0.5f) / 320.0f,
                                          (static_cast<float>(Y) + 0.5f) / 180.0f };
                    float Rgb[3];
                    AtmosphericOptics::LensFlare(Settings, Uv, Sun, 1.0f, Aspect, Rgb);
                    const double E = Rgb[0] + Rgb[1] + Rgb[2];
                    Measured[T].Total += E;

                    // Streak band: same height as the sun, well away from it horizontally.
                    if (std::fabs(Uv[1] - Sun[1]) < 0.012f && std::fabs(Uv[0] - Sun[0]) > 0.25f)
                        Measured[T].Streak += E;

                    // Halo ring about the optical axis at 0.25 of the way from centre to sun.
                    const float Hx = (Uv[0] - 0.5f) * Aspect - (Sun[0] - 0.5f) * Aspect * 0.25f;
                    const float Hy = (Uv[1] - 0.5f) - (Sun[1] - 0.5f) * 0.25f;
                    const float Ring = std::sqrt(Hx * Hx + Hy * Hy);
                    if (std::fabs(Ring - Settings.HaloRadius) < 0.02f)
                        Measured[T].Halo += E;

                    // Ghost line: between the sun and the far side of centre, off the streak band.
                    if (std::fabs(Uv[1] - Sun[1]) > 0.05f)
                    {
                        const float Dx = Uv[0] - 0.5f, Dy = Uv[1] - 0.5f;
                        const float Sx = Sun[0] - 0.5f, Sy = Sun[1] - 0.5f;
                        const float Cross = std::fabs(Dx * Sy - Dy * Sx);
                        if (Cross < 0.010f) Measured[T].Ghosts += E;
                    }
                }
        }

        std::printf("     %-11s %10s %10s %10s %10s\n", "type", "streak", "halo", "ghosts", "total");
        for (int T = 0; T < 4; ++T)
            std::printf("     %-11s %10.3f %10.3f %10.3f %10.3f\n", Types[T].Name,
                        Measured[T].Streak, Measured[T].Halo, Measured[T].Ghosts, Measured[T].Total);

        // Anamorphic: the streak is the whole point, and it has neither ghosts nor halo.
        Expect(Measured[1].Streak > Measured[0].Streak * 1.5,
               "Anamorphic's streak is far stronger than Cinematic's");
        Expect(Measured[1].Ghosts < Measured[0].Ghosts,
               "Anamorphic has no ghosting to speak of");

        // Halo: the ring dominates and the ghosts are gone.
        Expect(Measured[3].Ghosts < Measured[0].Ghosts * 0.5,
               "Halo drops the ghosts");
        Expect(Measured[3].Halo > Measured[1].Halo,
               "and keeps the ring that Anamorphic does not have");

        // Starburst: it is the only type with spokes, which show as energy away from every other feature.
        AtmosphericOptics::LensFlareSettings Burst{};
        Burst.Category = Category::Starburst;
        AtmosphericOptics::LensFlareSettings Anam{};
        Anam.Category = Category::Anamorphic;
        // ⚠️ Counted as the NUMBER OF LOBES around the ring, which took two tries to get right.
        //
        //    Total energy on the ring does not work: the ambient bloom is shared by every lens and contributes
        //    to all 720 samples while the spokes touch a few percent, so Starburst scored 274.6 against
        //    Anamorphic's 266.9 — a ratio of 1.03, measuring the term they have in common.
        //
        //    Variance does not work either: an anamorphic streak crosses the ring at exactly two points, which
        //    is maximally uneven, and it scored 1.116 against Starburst's 0.626. Variance cannot tell two lobes
        //    from twelve.
        //
        //    The distinguishing property is the lobe COUNT. A streak gives two, an iris gives one per spoke.
        auto RingLobes = [&](const AtmosphericOptics::LensFlareSettings& Settings) -> int
        {
            double Previous = -1.0;
            bool   Rising = false;
            int    Lobes = 0;
            double Peak = 0.0, Mean = 0.0;
            double Samples[1440];
            for (int I = 0; I < 1440; ++I)
            {
                const double A = static_cast<double>(I) / 1440.0 * 6.28318530718;
                const float Uv[2] = { Sun[0] + static_cast<float>(std::cos(A)) * 0.045f,
                                      Sun[1] + static_cast<float>(std::sin(A)) * 0.045f };
                float Rgb[3];
                AtmosphericOptics::LensFlare(Settings, Uv, Sun, 1.0f, Aspect, Rgb);
                Samples[I] = Rgb[0] + Rgb[1] + Rgb[2];
                Mean += Samples[I];
                if (Samples[I] > Peak) Peak = Samples[I];
            }
            Mean /= 1440.0;
            // Only count lobes that rise meaningfully above the shared bloom floor.
            const double Threshold = Mean + (Peak - Mean) * 0.25;
            for (int I = 0; I < 1440; ++I)
            {
                const double V = Samples[I];
                if (V > Previous && V > Threshold) Rising = true;
                else if (Rising && V < Previous) { ++Lobes; Rising = false; }
                Previous = V;
            }
            return Lobes;
        };
        const int BurstLobes = RingLobes(Burst);
        const int AnamLobes  = RingLobes(Anam);
        const int HaloLobes  = RingLobes([&]{ AtmosphericOptics::LensFlareSettings H{};
                                              H.Category = Category::Halo; return H; }());
        std::printf("     lobes around the sun: Starburst %d, Anamorphic %d, Halo %d\n",
                    BurstLobes, AnamLobes, HaloLobes);
        Expect(BurstLobes > AnamLobes * 2, "Starburst puts many spokes around the sun where a streak gives two");

        // And the decisive structural check: no two types share a mix, so none is a rescaling of another.
        bool AllDistinct = true;
        for (int A = 0; A < 4; ++A)
            for (int B = A + 1; B < 4; ++B)
            {
                const AtmosphericOptics::LensFlareMix Ma = AtmosphericOptics::MixFor(Types[A].Type);
                const AtmosphericOptics::LensFlareMix Mb = AtmosphericOptics::MixFor(Types[B].Type);
                if (Ma.Ghosts == Mb.Ghosts && Ma.Halo == Mb.Halo &&
                    Ma.Streak == Mb.Streak && Ma.Burst == Mb.Burst) AllDistinct = false;
            }
        Expect(AllDistinct, "no two types share a component mix");

        // Aperture blades change the spoke count, which is optics rather than taste: even blades give 2N.
        AtmosphericOptics::LensFlareSettings Six = Burst;  Six.ApertureBlades = 6u;
        AtmosphericOptics::LensFlareSettings Nine = Burst; Nine.ApertureBlades = 9u;
        int SixPeaks = 0, NinePeaks = 0;
        double PreviousSix = 0.0, PreviousNine = 0.0;
        bool RisingSix = false, RisingNine = false;
        for (int I = 0; I <= 1440; ++I)
        {
            const double A = static_cast<double>(I) / 1440.0 * 6.28318530718;
            const float Uv[2] = { Sun[0] + static_cast<float>(std::cos(A)) * 0.045f,
                                  Sun[1] + static_cast<float>(std::sin(A)) * 0.045f };
            float S6[3], S9[3];
            AtmosphericOptics::LensFlare(Six, Uv, Sun, 1.0f, Aspect, S6);
            AtmosphericOptics::LensFlare(Nine, Uv, Sun, 1.0f, Aspect, S9);
            const double E6 = S6[0] + S6[1] + S6[2], E9 = S9[0] + S9[1] + S9[2];
            if (E6 > PreviousSix + 1e-5) RisingSix = true;
            else if (RisingSix && E6 < PreviousSix - 1e-5) { ++SixPeaks; RisingSix = false; }
            if (E9 > PreviousNine + 1e-5) RisingNine = true;
            else if (RisingNine && E9 < PreviousNine - 1e-5) { ++NinePeaks; RisingNine = false; }
            PreviousSix = E6; PreviousNine = E9;
        }
        std::printf("     spokes: 6 blades -> %d peaks, 9 blades -> %d peaks\n", SixPeaks, NinePeaks);
        Expect(SixPeaks != NinePeaks, "the aperture blade count changes the spoke pattern");
    }

    std::printf("\n");
    for (int I = 0; I < 108; ++I) std::putchar('=');
    std::printf("\n%s\n\n", Failures == 0 ? "  the optics behave" : "  THE OPTICS DO NOT BEHAVE");
    return Failures == 0 ? 0 : 1;
}
