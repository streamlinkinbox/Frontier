// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
//  AtmosphereScatteringTest.cpp — the sky is blue, the sunset is red, and the night is dark, for physical reasons
// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
//  A verbatim CPU port of AtmosphereScattering.slang. Every constant and every expression is copied, so a change
//  to one that is not made to the other shows up here rather than on the GPU.
//
//  What makes these assertions worth anything is that none of them is a taste judgement. "The sky is blue" is
//  checked as blue radiance exceeding red by a factor the Rayleigh λ⁻⁴ law predicts; "the sunset is red" is
//  checked as the blue/red ratio INVERTING between zenith and horizon at low sun. A model that merely produced
//  a pleasant gradient would fail all of them.
//
//  Build: see Scratchpad/CheckAtmosphereScattering.sh
// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <initializer_list>
#include <vector>

//------------------------------------------------------------------------------------------------------------------------
//                                            PORT OF AtmosphereScattering.slang
//------------------------------------------------------------------------------------------------------------------------
// The port itself now lives in the engine, because adaptive exposure needs it too — see AtmosphereModel.h. A
//    second copy here would have been a third description of one atmosphere.

#include "DisplayPresentation/AtmosphereModel.h"

using namespace Frontier::AtmosphereModel;

static int Failures = 0;
static void Expect(bool Condition, const char* What)
{
    std::printf("  %-70s %s\n", What, Condition ? "PASS" : "FAIL");
    if (!Condition) ++Failures;
}

int main()
{
    std::printf("AtmosphereScattering — single-scattering sky\n");

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n1. the daytime sky is blue, by the ratio Rayleigh predicts\n");
    {
        // λ⁻⁴ makes the blue coefficient 33.1 / 5.802 ≈ 5.7× the red one. The observed radiance ratio is smaller
        //    than that because blue is also extinguished faster, but it must be comfortably above 1.
        const Vec3 Sky = SkyRadiance(2.0f, kZenith, SunAtElevation(60.0f), Vec3(kSunLux), kView, kLight);
        std::printf("     zenith radiance R %.4f  G %.4f  B %.4f   (B/R %.2f)\n",
                    static_cast<double>(Sky.x), static_cast<double>(Sky.y), static_cast<double>(Sky.z),
                    static_cast<double>(Sky.z / Sky.x));
        Expect(Sky.z > Sky.y && Sky.y > Sky.x, "blue exceeds green exceeds red — the Rayleigh ordering");
        Expect(Sky.z / Sky.x > 2.0f,           "and blue is at least twice red, not a marginal tint");
        Expect(Sky.x > 0.0f,                   "every channel carries some light");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n2. the horizon is brighter than the zenith\n");
    {
        // A ray toward the horizon passes through far more air, so it accumulates more scattering. This is the
        //    single clearest sign the march is integrating along the real path rather than shading a dome.
        const Vec3 Sun     = SunAtElevation(45.0f);
        const Vec3 Zenith  = SkyRadiance(2.0f, kZenith, Sun, Vec3(kSunLux), kView, kLight);
        const Vec3 Horizon = SkyRadiance(2.0f, Normalize(Vec3{ 1.0f, 0.0f, 0.02f }), Sun, Vec3(kSunLux), kView, kLight);
        const float ZenithLuma  = 0.2126f * Zenith.x  + 0.7152f * Zenith.y  + 0.0722f * Zenith.z;
        const float HorizonLuma = 0.2126f * Horizon.x + 0.7152f * Horizon.y + 0.0722f * Horizon.z;
        std::printf("     zenith %.4f, horizon %.4f  (ratio %.2f)\n",
                    static_cast<double>(ZenithLuma), static_cast<double>(HorizonLuma),
                    static_cast<double>(HorizonLuma / ZenithLuma));
        Expect(HorizonLuma > ZenithLuma, "the longer path through air is brighter");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n3. the sunset reddens — the colour ratio INVERTS at the horizon\n");
    {
        // The assertion that a gradient cannot fake. At high sun the sky toward the sun is blue-dominant; at a
        //    grazing angle the same direction has had its blue scattered out along a very long path and becomes
        //    red-dominant. It is a change of SIGN, not of degree.
        const Vec3 HighSun = SunAtElevation(60.0f);
        const Vec3 LowSun  = SunAtElevation(1.0f);

        const Vec3 TowardHigh = SkyRadiance(2.0f, Normalize(Vec3{ 0.0f, std::cos(1.0f * 3.14159265f / 180.0f),
                                                                        std::sin(1.0f * 3.14159265f / 180.0f) }),
                                            HighSun, Vec3(kSunLux), kView, kLight);
        const Vec3 TowardLow  = SkyRadiance(2.0f, Normalize(Vec3{ 0.0f, std::cos(1.0f * 3.14159265f / 180.0f),
                                                                        std::sin(1.0f * 3.14159265f / 180.0f) }),
                                            LowSun,  Vec3(kSunLux), kView, kLight);

        std::printf("     toward the horizon, high sun  B/R %.3f\n", static_cast<double>(TowardHigh.z / TowardHigh.x));
        std::printf("     toward the horizon, sun at 1° B/R %.3f\n", static_cast<double>(TowardLow.z  / TowardLow.x));
        Expect(TowardHigh.z / TowardHigh.x > 1.0f, "with the sun high, that direction is blue-dominant");
        Expect(TowardLow.z  / TowardLow.x  < TowardHigh.z / TowardHigh.x,
               "and a setting sun shifts it decisively toward red");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n4. night is dark — the sky goes out when the sun does\n");
    {
        // Single scattering with a correct planet-shadow test gives a genuinely black night. If this failed the
        //    ground would glow at midnight because the sun was still lighting samples through the planet.
        const Vec3 Day   = SkyRadiance(2.0f, kZenith, SunAtElevation( 45.0f), Vec3(kSunLux), kView, kLight);
        const Vec3 Dusk  = SkyRadiance(2.0f, kZenith, SunAtElevation(  0.0f), Vec3(kSunLux), kView, kLight);
        const Vec3 Night = SkyRadiance(2.0f, kZenith, SunAtElevation(-10.0f), Vec3(kSunLux), kView, kLight);

        const auto Luma = [](Vec3 c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; };
        std::printf("     zenith luminance: day %.5f, sunset %.5f, 10° below %.7f\n",
                    static_cast<double>(Luma(Day)), static_cast<double>(Luma(Dusk)), static_cast<double>(Luma(Night)));
        Expect(Luma(Dusk)  < Luma(Day),          "the sky dims as the sun sets");
        Expect(Luma(Night) < Luma(Dusk) * 0.10f, "and is far darker once the sun is properly below the horizon");
        Expect(Luma(Night) >= 0.0f,              "never negative");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n5. the sun's own glow is forward-scattered, not uniform\n");
    {
        // Mie asymmetry 0.8 concentrates scattering toward the sun. Looking at the sun's direction must be much
        //    brighter than looking away from it at the same elevation — that difference IS the aureole.
        const Vec3 Sun = SunAtElevation(20.0f);
        const Vec3 Toward = SkyRadiance(2.0f, Normalize(Vec3{ 0.0f, std::cos(0.35f), std::sin(0.35f) }),
                                        Sun, Vec3(kSunLux), kView, kLight);
        const Vec3 Away   = SkyRadiance(2.0f, Normalize(Vec3{ 0.0f, -std::cos(0.35f), std::sin(0.35f) }),
                                        Sun, Vec3(kSunLux), kView, kLight);
        const auto Luma = [](Vec3 c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; };
        std::printf("     toward the sun %.4f, away %.4f  (ratio %.2f)\n",
                    static_cast<double>(Luma(Toward)), static_cast<double>(Luma(Away)),
                    static_cast<double>(Luma(Toward) / Luma(Away)));
        Expect(Luma(Toward) > Luma(Away) * 1.5f, "the sky near the sun is markedly brighter — the Mie aureole");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n6. the phase functions are normalised and finite\n");
    {
        // Integrating a phase function over the sphere must give 1: it redistributes light, it does not create
        //    it. A mis-normalised phase silently scales the whole sky and would be tuned around rather than fixed.
        const auto Integrate = [](bool Mie)
        {
            double Sum = 0.0;
            const int N = 2000;
            for (int i = 0; i < N; ++i)
            {
                const double Cos = -1.0 + 2.0 * (i + 0.5) / N;
                const double P   = Mie ? MiePhase(static_cast<float>(Cos), kMieAsymmetry)
                                       : RayleighPhase(static_cast<float>(Cos));
                Sum += P * 2.0 * 3.14159265358979 * (2.0 / N);
            }
            return Sum;
        };
        const double R = Integrate(false), M = Integrate(true);
        std::printf("     ∫ Rayleigh dΩ = %.4f, ∫ Mie dΩ = %.4f  (both must be 1)\n", R, M);
        Expect(std::fabs(R - 1.0) < 0.01, "the Rayleigh phase integrates to unity");
        Expect(std::fabs(M - 1.0) < 0.02, "and so does Cornette–Shanks");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n7. sphere intersection is stable at planetary scale\n");
    {
        // The textbook quadratic catastrophically cancels for a ray starting 2 m above a 6360 km sphere, which is
        //    the normal case here. A wrong root shows as a shimmering horizon rather than an obvious failure.
        const Vec3 Origin{ 0.0f, 0.0f, kPlanetRadius + 2.0f };
        const float Up = RaySphereNearest(Origin, kZenith, kAtmosphereRadius);
        std::printf("     straight up to the top of the atmosphere: %.1f m (expect ~%.0f)\n",
                    static_cast<double>(Up), static_cast<double>(kAtmosphereThickness - 2.0f));
        Expect(std::fabs(Up - (kAtmosphereThickness - 2.0f)) < 50.0f, "the vertical distance is right to 50 m");

        // Looking down must hit the ground almost immediately.
        const float Down = RaySphereNearest(Origin, Vec3{ 0.0f, 0.0f, -1.0f }, kPlanetRadius);
        Expect(Down > 0.0f && Down < 10.0f, "looking straight down hits the ground within metres");

        // A horizontal ray must travel hundreds of kilometres before leaving the air.
        const float Flat = RaySphereNearest(Origin, Vec3{ 1.0f, 0.0f, 0.0f }, kAtmosphereRadius);
        std::printf("     horizontal to the top of the atmosphere: %.0f km\n", static_cast<double>(Flat / 1000.0f));
        Expect(Flat > 800000.0f && Flat < 1500000.0f, "the horizontal path is the expected ~1100 km");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n8. quality tiers agree with the reference\n");
    {
        // The tiers must differ in cost, not in answer. If Low disagreed materially with Ultra the setting would
        //    be a look change rather than a performance one, and a player would see a different sky per machine.
        const Vec3 Sun = SunAtElevation(30.0f);
        const auto Luma = [](Vec3 c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; };

        struct Tier { const char* Name; int View, Light; };
        const Tier Tiers[] = { { "Low", 16, 4 }, { "Medium", 32, 8 }, { "High", 48, 12 } };
        const float ReferenceZenith  = Luma(SkyRadiance(2.0f, kZenith, Sun, Vec3(kSunLux), 64, 16));
        const float ReferenceHorizon = Luma(SkyRadiance(2.0f, Normalize(Vec3{ 1.0f, 0.0f, 0.02f }), Sun, Vec3(kSunLux), 64, 16));

        for (const Tier& T : Tiers)
        {
            const float Zenith  = Luma(SkyRadiance(2.0f, kZenith, Sun, Vec3(kSunLux), T.View, T.Light));
            const float Horizon = Luma(SkyRadiance(2.0f, Normalize(Vec3{ 1.0f, 0.0f, 0.02f }), Sun, Vec3(kSunLux), T.View, T.Light));
            const float ZenithError  = std::fabs(Zenith  - ReferenceZenith)  / ReferenceZenith  * 100.0f;
            const float HorizonError = std::fabs(Horizon - ReferenceHorizon) / ReferenceHorizon * 100.0f;
            std::printf("     %-7s %2d × %2d   zenith %+.2f %%   horizon %+.2f %%\n",
                        T.Name, T.View, T.Light, static_cast<double>(ZenithError), static_cast<double>(HorizonError));
            Expect(ZenithError  < 3.0f,  "zenith is within 3 % of the reference at this tier");
            Expect(HorizonError < 12.0f, "and the horizon within 12 %, where the path is longest");
        }
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n9. zero illuminance restores the pre-A3 black sky exactly\n");
    {
        // The identity switch. Every image made before the sky existed must still be reproducible, or the phase
        //    cannot be A/B tested against what came before.
        const Vec3 Dark = SkyRadiance(2.0f, kZenith, SunAtElevation(45.0f), Vec3(0.0f), kView, kLight);
        Expect(Dark.x == 0.0f && Dark.y == 0.0f && Dark.z == 0.0f, "no illuminance means no radiance at all");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n10. the transmittance table reproduces the march it replaces\n");
    {
        // The table exists to remove SunTransmittance()'s inner march. It is only a speed-up if it returns the
        //    same answer — otherwise it is a different sky that happens to render faster.
        float Worst = 0.0f; float WorstAltitude = 0.0f, WorstCos = 0.0f;
        for (int Row = 0; Row < 16; ++Row)
            for (int Column = 0; Column < 32; ++Column)
            {
                const float U = (Column + 0.5f) / 32.0f, V = (Row + 0.5f) / 16.0f;
                float Altitude, CosSunZenith;
                TransmittanceInverse(U, V, Altitude, CosSunZenith);

                const Vec3 Table = ComputeTransmittanceTexel(U, V);

                const Vec3  Origin{ 0.0f, 0.0f, kPlanetRadius + Altitude };
                const float SinZenith = std::sqrt(std::fmax(0.0f, 1.0f - CosSunZenith * CosSunZenith));
                const Vec3  Marched = SunTransmittance(Origin, Vec3{ SinZenith, 0.0f, CosSunZenith }, 64);

                const float Error = std::fmax(std::fabs(Table.x - Marched.x),
                                    std::fmax(std::fabs(Table.y - Marched.y), std::fabs(Table.z - Marched.z)));
                if (Error > Worst) { Worst = Error; WorstAltitude = Altitude; WorstCos = CosSunZenith; }
            }
        std::printf("     worst disagreement %.3e (at %.0f m, cos %.2f)\n",
                    static_cast<double>(Worst), static_cast<double>(WorstAltitude), static_cast<double>(WorstCos));
        Expect(Worst < 1e-5f, "the table and the march agree to five decimal places");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n11. transmittance behaves the way an atmosphere must\n");
    {
        // ⚠️ Asked in PHYSICAL terms and routed through the forward map, never by hardcoded texel coordinates.
        //    This section used to name texels directly — U=1 for overhead, U=0.5 for grazing — which silently
        //    encoded one particular parameterisation. When the transmittance table moved to Bruneton's
        //    distance mapping (U=0 is the zenith, U=1 the horizon, and below the horizon is not stored at all)
        //    those constants pointed at different angles and the section reported vertical and grazing swapped.
        const auto At = [](float Altitude, float CosZenith)
        {
            float U, V;
            TransmittanceParameterisation(Altitude, CosZenith, U, V);
            return ComputeTransmittanceTexel(U, V);
        };

        // Overhead sun at sea level: most light gets through, and blue is attenuated hardest.
        const Vec3 Overhead = At(0.0f, 1.0f);
        std::printf("     sun overhead at sea level: R %.4f G %.4f B %.4f\n",
                    static_cast<double>(Overhead.x), static_cast<double>(Overhead.y), static_cast<double>(Overhead.z));
        Expect(Overhead.x > 0.85f && Overhead.x < 1.0f, "red mostly survives a vertical path");
        Expect(Overhead.z < Overhead.x,                 "blue is attenuated more than red — why the sun looks warm");

        // 🔴 Below the horizon the ground blocks the beam completely. The table no longer has a texel for that
        //    case — Bruneton's mapping spans zenith to horizon and nothing beyond — so the claim is put to the
        //    march itself, which is where it actually has to hold.
        const Vec3 Below = SunTransmittance(Vec3{ 0.0f, 0.0f, kPlanetRadius + 1.0f },
                                            Normalize(Vec3{ std::cos(-0.2f), 0.0f, std::sin(-0.2f) }), 64);
        Expect(Below.x == 0.0f && Below.y == 0.0f && Below.z == 0.0f,
               "a sun below the horizon transmits nothing — no light through the planet");

        // Higher up there is less air overhead, so more survives. Monotone in altitude, straight up.
        bool Monotone = true;
        float Previous = -1.0f;
        for (int Row = 0; Row < 16; ++Row)
        {
            const float Value = At((Row + 0.5f) / 16.0f * kAtmosphereThickness, 1.0f).z;
            if (Value < Previous - 1e-6f) Monotone = false;
            Previous = Value;
        }
        Expect(Monotone, "transmittance rises with altitude, without wobble");

        // A grazing sun travels through far more air than an overhead one.
        const Vec3 Grazing = At(0.0f, 0.0f);   // cos = 0, exactly at the horizon
        std::printf("     grazing sun at sea level:  R %.4f G %.4f B %.4f\n",
                    static_cast<double>(Grazing.x), static_cast<double>(Grazing.y), static_cast<double>(Grazing.z));
        Expect(Grazing.x < Overhead.x, "a grazing path attenuates more than a vertical one");
        Expect(Grazing.z < Grazing.x,  "and reddens the beam, which is the sunset");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n12. multiple scattering adds light where single scattering cannot\n");
    {
        // The whole reason this table exists. Single scattering leaves the sky too dark away from the sun and
        //    makes twilight black; the second-order term is what fills both in. It must be positive, bounded,
        //    and largest where the single-scattering answer was weakest.
        const Vec3 Overhead = ComputeMultiScatterTexel(1.0f, 0.0f, 64, 20);
        const Vec3 Twilight = ComputeMultiScatterTexel(0.48f, 0.0f, 64, 20);   // sun just below the horizon

        std::printf("     overhead sun: R %.5f G %.5f B %.5f\n",
                    static_cast<double>(Overhead.x), static_cast<double>(Overhead.y), static_cast<double>(Overhead.z));
        std::printf("     twilight:     R %.5f G %.5f B %.5f\n",
                    static_cast<double>(Twilight.x), static_cast<double>(Twilight.y), static_cast<double>(Twilight.z));

        Expect(Overhead.x > 0.0f && Overhead.y > 0.0f && Overhead.z > 0.0f,
               "the transfer factor is positive — it adds light rather than removing it");
        Expect(Overhead.z > Overhead.x, "and is blue-dominant, as multiply-scattered skylight is");
        Expect(Overhead.x < 1.0f && Overhead.y < 1.0f && Overhead.z < 1.0f,
               "and bounded below one, so the geometric series converged rather than running away");
        Expect(Twilight.z > 0.0f, "twilight retains some scattered light instead of going black");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n13. the table parameterisation resolves the layer that matters\n");
    {
        // Density falls exponentially, so a LINEAR altitude axis spends its rows on thin air and starves the
        //    first kilometre. The square-root distribution is what fixes that, and this measures the difference
        //    rather than asserting it: how much of the table's range is spent below 10 km.
        int SqrtRows = 0, LinearRows = 0;
        for (unsigned Row = 0; Row < kTransmittanceHeight; ++Row)
        {
            const float V = (Row + 0.5f) / kTransmittanceHeight;
            if (V * V * kAtmosphereThickness < 10000.0f) ++SqrtRows;      // the parameterisation in use
            if (V * kAtmosphereThickness     < 10000.0f) ++LinearRows;    // the naive alternative
        }
        std::printf("     rows below 10 km: square-root %d of %u, linear %d of %u\n",
                    SqrtRows, kTransmittanceHeight, LinearRows, kTransmittanceHeight);
        Expect(SqrtRows > LinearRows * 2,
               "the square-root axis gives the dense lower atmosphere far more rows than a linear one would");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n14. the sun disc samples uniformly across its true angular size\n");
    {
        // A4. A verbatim port of SampleSunPoint's direction maths. Two things must hold: every sample lies
        //    inside the 0.268° cap, and the distribution is UNIFORM ON THE CAP rather than bunched at the
        //    centre. Bunching would narrow the penumbra without making the shadow obviously wrong, so it is the
        //    kind of error that survives a visual check.
        const Vec3 SunDirection = Normalize(Vec3{ 0.3f, 0.4f, 0.866f });

        const auto SampleDirection = [&](float U1, float U2)
        {
            const float CosMax   = std::cos(kSunAngularRadius);
            const float CosTheta = 1.0f - U1 * (1.0f - CosMax);
            const float SinTheta = std::sqrt(std::fmax(0.0f, 1.0f - CosTheta * CosTheta));
            const float Phi      = 6.28318530f * U2;
            const Vec3  Up       = std::fabs(SunDirection.z) < 0.99f ? Vec3{ 0.0f, 0.0f, 1.0f } : Vec3{ 1.0f, 0.0f, 0.0f };
            // cross(Up, Sun)
            const Vec3  Tangent  = Normalize(Vec3{ Up.y * SunDirection.z - Up.z * SunDirection.y,
                                                   Up.z * SunDirection.x - Up.x * SunDirection.z,
                                                   Up.x * SunDirection.y - Up.y * SunDirection.x });
            const Vec3  Bitangent{ SunDirection.y * Tangent.z - SunDirection.z * Tangent.y,
                                   SunDirection.z * Tangent.x - SunDirection.x * Tangent.z,
                                   SunDirection.x * Tangent.y - SunDirection.y * Tangent.x };
            return Normalize(SunDirection * CosTheta + Tangent * (SinTheta * std::cos(Phi))
                                                     + Bitangent * (SinTheta * std::sin(Phi)));
        };

        float WorstAngle = 0.0f;
        int   InnerHalf  = 0;
        const int Samples = 20000;
        for (int i = 0; i < Samples; ++i)
        {
            // A deterministic low-discrepancy pair, so the result does not wander between runs.
            const float U1 = (i + 0.5f) / Samples;
            const float U2 = std::fmod(i * 0.618033988f, 1.0f);
            const Vec3  D  = SampleDirection(U1, U2);
            const float Angle = std::acos(std::fmin(1.0f, Dot(D, SunDirection)));
            WorstAngle = std::fmax(WorstAngle, Angle);
            // Uniform on a cap means HALF the samples fall inside the radius that bisects its AREA, which for a
            //    small cap is r/√2 — not r/2, which is what a naive linear radius would give.
            if (Angle <= kSunAngularRadius / 1.41421356f) ++InnerHalf;
        }
        const float InnerFraction = static_cast<float>(InnerHalf) / Samples;
        std::printf("     widest sample %.6f rad (limit %.6f), inner-half-area fraction %.3f (want 0.500)\n",
                    static_cast<double>(WorstAngle), static_cast<double>(kSunAngularRadius),
                    static_cast<double>(InnerFraction));
        // ⚠️ 1 % of tolerance, not 0.1 %. acos is ill-conditioned near 1: at this angle cos(θ) = 1 − 1.09e-5,
        //    and a SINGLE float ulp there (1.19e-7) moves the recovered angle by 2.5e-5 rad — 0.54 % of the
        //    radius. A tighter bound would be measuring the reconstruction, not the sampler.
        Expect(WorstAngle <= kSunAngularRadius * 1.01f, "no sample escapes the sun's angular radius");
        Expect(WorstAngle >  kSunAngularRadius * 0.99f,  "and the full disc is actually reached");
        Expect(std::fabs(InnerFraction - 0.5f) < 0.02f,
               "half the samples fall in the inner half of the AREA — uniform, not centre-bunched");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n15. the penumbra a 0.268° source casts is physically sized\n");
    {
        // The reason to sample a disc at all. A point light gives a hard edge; the sun's finite size gives a
        //    penumbra that widens with the distance between occluder and receiver, at a rate set by its angular
        //    diameter. These are the numbers to compare a screenshot against.
        std::printf("     occluder height   penumbra width\n");
        for (float Height : { 0.5f, 1.0f, 2.0f, 5.0f })
        {
            const float Width = 2.0f * Height * std::tan(kSunAngularRadius);
            std::printf("       %4.1f m           %6.1f mm\n",
                        static_cast<double>(Height), static_cast<double>(Width * 1000.0f));
        }
        const float AtOneMetre = 2.0f * 1.0f * std::tan(kSunAngularRadius);
        Expect(AtOneMetre > 0.008f && AtOneMetre < 0.011f,
               "a 1 m occluder casts a ~9 mm penumbra, as the real sun does");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n16. sky lighting converges on the correct irradiance\n");
    {
        // A5. An escaped bounce ray gathers sky radiance and is weighted by the BSDF throughput. For a diffuse
        //    surface the BSDF sample is cosine-distributed, so summing radiance over those samples estimates
        //    ∫ L(ω)·cosθ dω / π — the irradiance divided by π. Checked here against an explicit hemisphere
        //    integral of the SAME sky, because if the estimator disagreed with the integral the room would be
        //    lit to the wrong level and there would be nothing to compare it with in-engine.
        const Vec3 Sun = SunAtElevation(50.0f);
        const Vec3 Up{ 0.0f, 0.0f, 1.0f };

        // Reference: integrate L·cosθ over the upper hemisphere on a regular grid.
        double ReferenceR = 0.0, ReferenceG = 0.0, ReferenceB = 0.0;
        const int Zeniths = 48, Azimuths = 96;
        for (int i = 0; i < Zeniths; ++i)
            for (int j = 0; j < Azimuths; ++j)
            {
                const float Theta = (i + 0.5f) / Zeniths * 1.57079633f;
                const float Phi   = (j + 0.5f) / Azimuths * 6.28318530f;
                const Vec3  D{ std::sin(Theta) * std::cos(Phi), std::sin(Theta) * std::sin(Phi), std::cos(Theta) };
                const Vec3  L = SkyRadiance(2.0f, D, Sun, Vec3(kSunLux), 24, 8);
                const float Weight = std::sin(Theta) * std::cos(Theta)
                                   * (1.57079633f / Zeniths) * (6.28318530f / Azimuths);
                ReferenceR += L.x * Weight; ReferenceG += L.y * Weight; ReferenceB += L.z * Weight;
            }

        // Estimator: cosine-distributed samples, averaged. That average IS irradiance / π, so multiply back.
        double EstimateR = 0.0, EstimateG = 0.0, EstimateB = 0.0;
        const int Samples = 4096;
        for (int i = 0; i < Samples; ++i)
        {
            const float U1 = (i + 0.5f) / Samples;
            const float U2 = std::fmod(i * 0.618033988f, 1.0f);
            const float R  = std::sqrt(U1);                    // cosine-weighted: radius ∝ √u
            const float Phi = 6.28318530f * U2;
            const Vec3  D{ R * std::cos(Phi), R * std::sin(Phi), std::sqrt(std::fmax(0.0f, 1.0f - U1)) };
            const Vec3  L = SkyRadiance(2.0f, D, Sun, Vec3(kSunLux), 24, 8);
            EstimateR += L.x; EstimateG += L.y; EstimateB += L.z;
        }
        const double Scale = 3.14159265358979 / Samples;
        EstimateR *= Scale; EstimateG *= Scale; EstimateB *= Scale;

        std::printf("     hemisphere integral  R %.2f G %.2f B %.2f\n", ReferenceR, ReferenceG, ReferenceB);
        std::printf("     cosine estimator     R %.2f G %.2f B %.2f\n", EstimateR, EstimateG, EstimateB);
        const double ErrorB = std::fabs(EstimateB - ReferenceB) / ReferenceB * 100.0;
        std::printf("     blue channel error   %.2f %%\n", ErrorB);

        Expect(ErrorB < 3.0, "the escaped-bounce estimator matches an explicit hemisphere integral");
        Expect(EstimateB > EstimateR, "and the skylight is blue, as diffuse daylight is");
        (void)Up;
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n17. sky lighting is a real contribution, not a rounding error\n");
    {
        // Worth measuring rather than assuming: if skylight were a fraction of a percent of the sun there would
        //    be no reason to pay for it. Irradiance from the whole sky against the sun's direct beam on a
        //    horizontal surface — the familiar result is that the sky is a large minority of daylight, and
        //    dominant in shadow, which is exactly why a shadowed wall is blue rather than black.
        const Vec3 Sun = SunAtElevation(50.0f);

        double SkyLuma = 0.0;
        const int Zeniths = 32, Azimuths = 64;
        for (int i = 0; i < Zeniths; ++i)
            for (int j = 0; j < Azimuths; ++j)
            {
                const float Theta = (i + 0.5f) / Zeniths * 1.57079633f;
                const float Phi   = (j + 0.5f) / Azimuths * 6.28318530f;
                const Vec3  D{ std::sin(Theta) * std::cos(Phi), std::sin(Theta) * std::sin(Phi), std::cos(Theta) };
                const Vec3  L = SkyRadiance(2.0f, D, Sun, Vec3(kSunLux), 24, 8);
                const float Weight = std::sin(Theta) * std::cos(Theta)
                                   * (1.57079633f / Zeniths) * (6.28318530f / Azimuths);
                SkyLuma += (0.2126f * L.x + 0.7152f * L.y + 0.0722f * L.z) * Weight;
            }

        // Direct beam on a horizontal surface: illuminance × transmittance × cosine.
        const Vec3  Beam = ComputeTransmittanceTexel(Sun.z * 0.5f + 0.5f, 0.0f);
        const double SunLuma = (0.2126 * Beam.x + 0.7152 * Beam.y + 0.0722 * Beam.z) * kSunLux * Sun.z;

        std::printf("     sky irradiance %.0f, direct sun %.0f  (sky is %.1f %% of the total)\n",
                    SkyLuma, SunLuma, SkyLuma / (SkyLuma + SunLuma) * 100.0);
        Expect(SkyLuma > SunLuma * 0.05,
               "skylight is a material fraction of daylight — worth the cost of gathering it");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n18. the moon's terminator is a curve on a sphere, not a chord\n");
    {
        // A7. Phase is the one part of a moon that is instantly wrong if simplified. A straight chord across the
        //    disc gives a "D" at half phase and horns pointing the wrong way at crescent. The correct boundary
        //    comes from the sphere's own normal, and this measures the lit AREA against what geometry demands.
        const auto LitFraction = [](float Phase)
        {
            const float PhaseAngle = (1.0f - Phase) * 3.14159265f;
            const Vec3  ToSun{ std::sin(PhaseAngle), 0.0f, std::cos(PhaseAngle) };
            int Lit = 0, Total = 0;
            const int Grid = 400;
            for (int Y = 0; Y < Grid; ++Y)
                for (int X = 0; X < Grid; ++X)
                {
                    const float U = (X + 0.5f) / Grid * 2.0f - 1.0f;
                    const float V = (Y + 0.5f) / Grid * 2.0f - 1.0f;
                    const float R2 = U * U + V * V;
                    if (R2 > 1.0f) continue;
                    ++Total;
                    const Vec3 Normal{ U, V, std::sqrt(std::fmax(0.0f, 1.0f - R2)) };
                    if (Dot(Normal, ToSun) > 0.0f) ++Lit;
                }
            return static_cast<float>(Lit) / static_cast<float>(Total);
        };

        std::printf("     phase   lit area   expected\n");
        bool AllRight = true;
        for (float Phase : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            const float Measured = LitFraction(Phase);
            // ⚠️ The lit AREA of the visible disc is (1 + cos θ)/2 where θ is the elongation, NOT the phase
            //    parameter itself. An earlier version of this test asserted "area == phase" and failed at the
            //    quarters while passing at 0, 0.5 and 1 — the three points where the two happen to coincide.
            //    The code was right; the expectation conflated the shape parameter with the area it produces.
            const float Elongation = (1.0f - Phase) * 3.14159265f;
            const float Expected   = (1.0f + std::cos(Elongation)) * 0.5f;
            std::printf("     %.2f    %.4f     %.4f\n",
                        static_cast<double>(Phase), static_cast<double>(Measured), static_cast<double>(Expected));
            if (std::fabs(Measured - Expected) > 0.02f) AllRight = false;
        }
        Expect(AllRight, "the lit area follows (1 − cos θ)/2 at every phase");

        // A straight chord would give exactly half the disc at phase 0.5 too, so the discriminating test is a
        //    CRESCENT: a chord lights far more of the disc than a sphere does.
        const float CrescentSphere = LitFraction(0.15f);
        const float CrescentChord  = 0.15f * 2.0f;   // a chord at 15 % offset lights ~30 % of the disc area
        std::printf("     crescent at 0.15: sphere %.3f, a straight chord would give ~%.3f\n",
                    static_cast<double>(CrescentSphere), static_cast<double>(CrescentChord));
        Expect(CrescentSphere < CrescentChord * 0.75f,
               "a crescent lights markedly less than a chord would — the shapes are genuinely different");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n19. the night sky is visible only because exposure adapts\n");
    {
        // Why A7 waited for A6b. These are the luminances involved and what a FIXED daylight exposure does to
        //    them — the stars are not dim, they are eight orders of magnitude below the sky that set the
        //    exposure.
        const float DaylightExposure = 0.18f / 8000.0f;   // exposed for a noon sky
        struct Source { const char* Name; float Luminance; };
        const Source Sources[] = {
            { "moonlit sky", 0.10f  },
            { "moon disc",   2500.0f },
            { "bright star", 0.001f },
        };
        std::printf("     under a DAYLIGHT exposure:\n");
        for (const Source& S : Sources)
            std::printf("       %-12s renders at %.9f\n", S.Name,
                        static_cast<double>(S.Luminance * DaylightExposure));

        const float NightExposure = 0.18f / 0.10f;        // adapted to the moonlit sky
        std::printf("     under an ADAPTED exposure:\n");
        for (const Source& S : Sources)
            std::printf("       %-12s renders at %.6f\n", S.Name,
                        static_cast<double>(S.Luminance * NightExposure));

        Expect(0.001f * DaylightExposure < 1.0f / 255.0f,
               "a star under daylight exposure is below one 8-bit step — invisible");
        Expect(0.001f * NightExposure    > 0.0f,
               "and adaptation is what brings it into range");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n20. the procedural star field has the right NUMBER of stars\n");
    {
        // The grid resolution and the hash threshold together set the star count, and they are easy to get wrong
        //    by an order of magnitude because the fraction of CELLS holding a star is not the fraction of
        //    DIRECTIONS that land on one — the sphere shell only intersects a thin slice of the grid.
        //
        //    This shipped at 700 / 0.982, which is 120 352 stars: thirteen times the real sky. That does not
        //    read as a rich sky, it reads as noise, because the eye stops resolving individual points and sees
        //    texture. Counting is the only way to catch it; it looks plausible in a screenshot either way.
        constexpr float kCells     = 260.0f;   // must match StarField() in AtmosphereScattering.slang
        constexpr float kThreshold = 0.990f;

        const auto Fract = [](float V) { return V - std::floor(V); };
        const auto Hash  = [&](float Cx, float Cy, float Cz)
        {
            return Fract(std::sin(Cx * 12.9898f + Cy * 78.233f + Cz * 37.719f) * 43758.5453f);
        };

        // Walk the sphere far more densely than the grid, and count DISTINCT cells that pass the threshold.
        static std::vector<unsigned char> Seen(1u << 22, 0u);
        std::fill(Seen.begin(), Seen.end(), static_cast<unsigned char>(0));

        long Stars = 0;
        const int Probes = static_cast<int>(kCells * kCells * 40.0f);
        for (int I = 0; I < Probes; ++I)
        {
            const double Fraction = (I + 0.5) / Probes;
            const double CosTheta = 1.0 - 2.0 * Fraction;
            const double SinTheta = std::sqrt(std::fmax(0.0, 1.0 - CosTheta * CosTheta));
            const double Phi      = I * 2.39996323;
            const float  Cx = std::floor(static_cast<float>(SinTheta * std::cos(Phi)) * kCells);
            const float  Cy = std::floor(static_cast<float>(SinTheta * std::sin(Phi)) * kCells);
            const float  Cz = std::floor(static_cast<float>(CosTheta) * kCells);
            if (Hash(Cx, Cy, Cz) < kThreshold) continue;

            const long Key = ((static_cast<long>(Cx) + 2048L) * 4093L + (static_cast<long>(Cy) + 2048L)) * 4093L
                           + (static_cast<long>(Cz) + 2048L);
            const size_t Slot = static_cast<size_t>((static_cast<unsigned long>(Key) * 2654435761UL) & ((1u << 22) - 1u));
            if (!Seen[Slot]) { Seen[Slot] = 1u; ++Stars; }
        }

        std::printf("     %ld distinct stars on the sphere (naked-eye sky is ~9 100 to magnitude 6.5)\n", Stars);
        Expect(Stars > 5000 && Stars < 15000,
               "the star count is within a factor of ~1.6 of the real naked-eye sky");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n21. stars are hidden by daylight, not switched off by it\n");
    {
        // Stars are in the sky at noon — they are simply outshone. Nothing in the renderer gates them on time of
        //    day, and that is deliberate: a time-of-day switch would get the common case right and every
        //    interesting case wrong. High altitude, a total eclipse and deep twilight all reveal stars in
        //    daylight, and all three fall out of the physics for free.
        const Vec3  Sun      = SunAtElevation(57.0f);
        const Vec3  Zenith{ 0.0f, 0.0f, 1.0f };
        const float StarPeak = 0.4f;   // the StarBrightness default
        const auto  Luma     = [](Vec3 C) { return 0.2126f * C.x + 0.7152f * C.y + 0.0722f * C.z; };

        // At sea level the daytime sky must drown them: below one 8-bit step of the exposed image.
        const float SeaLevel = Luma(SkyRadiance(2.0f, Zenith, Sun, Vec3(kSunLux), 48, 12));
        const float SeaRatio = StarPeak / SeaLevel;
        std::printf("     sea level noon: sky %.1f, star/sky %.2e\n",
                    static_cast<double>(SeaLevel), static_cast<double>(SeaRatio));
        Expect(SeaRatio < 1.0f / 255.0f, "at sea level the noon sky hides the stars, as it must");

        // ⚠️ But the SAME code must reveal them from altitude, because there is less air overhead to scatter.
        //    This is the check a time-of-day switch would fail: the sun is in exactly the same place.
        const float HighUp    = Luma(SkyRadiance(40000.0f, Zenith, Sun, Vec3(kSunLux), 48, 12));
        const float HighRatio = StarPeak / HighUp;
        std::printf("     40 km noon:     sky %.4f, star/sky %.2e\n",
                    static_cast<double>(HighUp), static_cast<double>(HighRatio));
        Expect(HighRatio > 1.0f / 255.0f,
               "from 40 km the same midday sun leaves the stars visible — altitude, not a flag");
        Expect(HighUp < SeaLevel * 0.05f, "and the sky there is far darker for the physical reason");

        // A total eclipse is the sun's illuminance collapsing, which the same parameter already expresses.
        const float Eclipsed = Luma(SkyRadiance(2.0f, Zenith, Sun, Vec3(kSunLux * 0.0001f), 48, 12));
        std::printf("     eclipse (0.01%% sun): sky %.4f, star/sky %.2e\n",
                    static_cast<double>(Eclipsed), static_cast<double>(StarPeak / Eclipsed));
        Expect(StarPeak / Eclipsed > 1.0f / 255.0f,
               "and an eclipse reveals them too, through the same illuminance term");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n22. stars are wide enough not to flicker\n");
    {
        // Reported as "the stars flicker on and off". A point source narrower than about a pixel appears only
        //    when a pixel centre happens to land inside it, so the smallest camera movement makes the whole
        //    field blink. It reads as a rendering fault rather than as stars.
        //
        //    These must match StarField() in AtmosphereScattering.slang.
        constexpr float kCells   = 260.0f;
        constexpr float kFalloff = 120.0f;
        constexpr float kCutoff  = 0.002f;

        // exp(-d² · Falloff) = Cutoff  ⇒  d = √(−ln(Cutoff) / Falloff), in cell units.
        const float CutoffCells   = std::sqrt(-std::log(kCutoff) / kFalloff);
        const float AngularRadius = CutoffCells / kCells;
        const float FovRadians    = 55.0f * 3.14159265f / 180.0f;

        std::printf("     angular radius %.6f rad (%.4f°)\n",
                    static_cast<double>(AngularRadius), static_cast<double>(AngularRadius * 180.0f / 3.14159265f));
        for (int Height : { 720, 1080, 2160 })
        {
            const float Diameter = 2.0f * AngularRadius * (Height / FovRadians);
            std::printf("     %4dp: %.2f px across\n", Height, static_cast<double>(Diameter));
        }

        const float At1080 = 2.0f * AngularRadius * (1080.0f / FovRadians);
        Expect(At1080 > 1.5f, "a star covers more than 1.5 px at 1080p, so it survives sub-pixel motion");
        Expect(At1080 < 4.0f, "but is still a point rather than a blob");

        // The previous value, kept so the regression is recognisable if someone tightens it again.
        const float Old = 2.0f * (std::sqrt(-std::log(kCutoff) / 900.0f) / kCells) * (1080.0f / FovRadians);
        std::printf("     the previous falloff of 900 gave %.2f px — sub-pixel, hence the flicker\n",
                    static_cast<double>(Old));
        Expect(Old < 1.0f, "and the old value really was sub-pixel — this is the bug fixed");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n23. a clear sunrise differs from a clear sunset, and ONLY through the air\n");
    {
        // 🔴 The claim being tested. The scattering model is symmetric about the horizon: at equal sun elevation
        //    morning and evening are the same geometry, so they are the same picture. That is correct physics and
        //    it is why no change to the march could ever separate them. The difference is the AIR — overnight the
        //    boundary layer cools, convection stops, aerosols settle out; an afternoon of heating stirs them back
        //    up. So dawn is cleaner: paler, whiter, less orange.
        const Vec3 LowSun  = SunAtElevation(2.0f);
        const Vec3 Horizon = Normalize(Vec3{ 0.0f, std::cos(0.05f), std::sin(0.05f) });   // ~3° up, toward the sun
        const auto Luma    = [](Vec3 C) { return 0.2126f * C.x + 0.7152f * C.y + 0.0722f * C.z; };

        // First: with the SAME air, the two are identical. This is the negative result that makes the parameter
        //    necessary rather than decorative — if these ever differ, something asymmetric has crept in.
        gAerosolTurbidity = 1.0f;
        const Vec3 Morning = SkyRadiance(2.0f, Horizon, LowSun, Vec3(kSunLux), kView, kLight);
        gAerosolTurbidity = 1.0f;
        const Vec3 Evening = SkyRadiance(2.0f, Horizon, LowSun, Vec3(kSunLux), kView, kLight);
        Expect(Morning.x == Evening.x && Morning.z == Evening.z,
               "at equal turbidity dawn and dusk are the SAME picture — the model is symmetric");

        // Now the real pair, at the default curve's two extremes.
        gAerosolTurbidity = 0.65f;                                        // 06:00
        const Vec3 Dawn = SkyRadiance(2.0f, Horizon, LowSun, Vec3(kSunLux), kView, kLight);
        gAerosolTurbidity = 1.35f;                                        // 18:00
        const Vec3 Dusk = SkyRadiance(2.0f, Horizon, LowSun, Vec3(kSunLux), kView, kLight);

        const float DawnWarmth = Dawn.x / Dawn.z;      // red over blue: how orange the glow is
        const float DuskWarmth = Dusk.x / Dusk.z;
        std::printf("     toward the sun at 2 deg:  dawn R/B %.3f   dusk R/B %.3f\n",
                    static_cast<double>(DawnWarmth), static_cast<double>(DuskWarmth));
        Expect(DuskWarmth > DawnWarmth, "the dusty evening glow is the redder of the two");
        Expect(DawnWarmth < DuskWarmth * 0.95f, "and by a visible margin, not a rounding difference");

        // Paler: less aerosol scatters less light into the line of sight near the horizon.
        std::printf("     horizon luminance:        dawn %.2f     dusk %.2f\n",
                    static_cast<double>(Luma(Dawn)), static_cast<double>(Luma(Dusk)));
        Expect(Luma(Dawn) < Luma(Dusk), "the cleaner morning horizon is the fainter of the two");

        // ⚠️ And the zenith must barely move. Turbidity scales Mie only — Rayleigh is the air molecules
        //    themselves, which do not care how dusty the day is. If this ever changed much, turbidity would have
        //    been wired into the wrong coefficient and would be acting as a global brightness control.
        gAerosolTurbidity = 0.65f;
        const float ZenithDawn = Luma(SkyRadiance(2.0f, kZenith, LowSun, Vec3(kSunLux), kView, kLight));
        gAerosolTurbidity = 1.35f;
        const float ZenithDusk = Luma(SkyRadiance(2.0f, kZenith, LowSun, Vec3(kSunLux), kView, kLight));
        const float ZenithShift  = std::fabs(ZenithDusk - ZenithDawn) / ZenithDawn;
        const float HorizonShift = std::fabs(Luma(Dusk) - Luma(Dawn)) / Luma(Dawn);
        std::printf("     zenith moves %.1f%%, horizon moves %.1f%% — aerosols hug the ground\n",
                    static_cast<double>(ZenithShift * 100.0f), static_cast<double>(HorizonShift * 100.0f));
        Expect(ZenithShift < 0.15f,               "the zenith is nearly unchanged — Rayleigh is untouched");
        Expect(HorizonShift > ZenithShift * 2.0f, "while the horizon, which is mostly aerosol, changes far more");

        gAerosolTurbidity = 1.0f;   // leave the reference atmosphere for anything that follows
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n24. the day's aerosol curve is cleanest at dawn and dirtiest at dusk\n");
    {
        // A verbatim port of QueryDiurnalTurbidity in ReSTIRIntegrator.h.
        const auto Turbidity = [](float Mean, float Swing, double DayFraction)
        {
            constexpr double kTwoPi = 6.283185307179586;
            const double Hour  = DayFraction * 24.0;
            const double Phase = kTwoPi * (Hour - 18.0) / 24.0;
            const double Value = static_cast<double>(Mean) + static_cast<double>(Swing) * std::cos(Phase);
            return static_cast<float>(Value < 0.05 ? 0.05 : Value);
        };
        constexpr float kMean = 1.0f, kSwing = 0.35f;

        float Lowest = 1e9f, Highest = -1e9f; int LowestHour = -1, HighestHour = -1;
        for (int Hour = 0; Hour < 24; ++Hour)
        {
            const float T = Turbidity(kMean, kSwing, Hour / 24.0);
            if (T < Lowest)  { Lowest  = T; LowestHour  = Hour; }
            if (T > Highest) { Highest = T; HighestHour = Hour; }
        }
        std::printf("     06:00 %.3f   12:00 %.3f   18:00 %.3f   00:00 %.3f\n",
                    static_cast<double>(Turbidity(kMean, kSwing,  6.0 / 24.0)),
                    static_cast<double>(Turbidity(kMean, kSwing, 12.0 / 24.0)),
                    static_cast<double>(Turbidity(kMean, kSwing, 18.0 / 24.0)),
                    static_cast<double>(Turbidity(kMean, kSwing,  0.0 / 24.0)));
        Expect(LowestHour  == 6,  "the air is cleanest at 06:00");
        Expect(HighestHour == 18, "and dirtiest at 18:00");

        // Noon and midnight land exactly on the mean, which is what makes the default a superset of the pre-A7b
        //    sky rather than a different one: at those hours the atmosphere IS the reference atmosphere.
        Expect(std::fabs(Turbidity(kMean, kSwing, 12.0 / 24.0) - kMean) < 1e-5f, "noon sits exactly on the mean");
        Expect(std::fabs(Turbidity(kMean, kSwing,  0.0 / 24.0) - kMean) < 1e-5f, "and so does midnight");

        // ⚠️ Continuous everywhere INCLUDING across midnight. A piecewise curve is easier to reason about and
        //    steps visibly wherever the pieces meet, which on a fast clock reads as the sky flickering once a day.
        float LargestStep = 0.0f;
        for (int Step = 0; Step < 1440; ++Step)
        {
            const float A = Turbidity(kMean, kSwing, Step / 1440.0);
            const float B = Turbidity(kMean, kSwing, ((Step + 1) % 1440) / 1440.0);
            LargestStep = std::fmax(LargestStep, std::fabs(B - A));
        }
        std::printf("     largest one-minute step %.6f (across midnight included)\n",
                    static_cast<double>(LargestStep));
        Expect(LargestStep < 0.002f, "no discontinuity anywhere on the day, midnight included");

        // The identity switch: swing 0 reproduces the pre-A7b atmosphere at every hour.
        bool Flat = true;
        for (int Hour = 0; Hour < 24; ++Hour)
            if (std::fabs(Turbidity(kMean, 0.0f, Hour / 24.0) - kMean) > 1e-6f) Flat = false;
        Expect(Flat, "swing 0 is flat all day — the identity switch back to the pre-A7b sky");

        // And the floor, because turbidity multiplies EXTINCTION: zero or negative air amplifies light and the
        //    march diverges rather than merely looking wrong.
        Expect(Turbidity(0.1f, 5.0f, 6.0 / 24.0) >= 0.05f, "an over-large swing is floored, never negative");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n25. the world does not end at the horizon — the planet has a surface\n");
    {
        // 🔴 The defect this pins. With no ground in the model, a ray passing below the horizon returned only
        //    the radiance of the short slice of air in front of it, which at eye height is almost nothing.
        //    Measured before the fix: 0.2° below horizontal fell from 7473 to 74 cd/m², and to 15 by 1° down —
        //    a hundredfold cliff into a black void, exactly along the horizon line.
        //
        //    That void is not cosmetic. In an outdoor scene the ground plate ends a few hundred metres out and
        //    the void fills the bottom half of the frame, which dragged the metered scene luminance from 5752
        //    to 67 cd/m² and raised the exposure 86× — everything lit blew to white, and it came right when the
        //    camera climbed high enough to cover the void. That is the reported "fine up close, brighter as I
        //    move away".
        const Vec3 Sun = SunAtElevation(50.0f);
        const auto Luma = [](Vec3 C) { return 0.2126f * C.x + 0.7152f * C.y + 0.0722f * C.z; };
        const auto Look = [&](float Degrees)
        {
            const float R = Degrees * 3.14159265f / 180.0f;
            return Luma(SkyRadiance(2.0f, Normalize(Vec3{ 0.0f, std::cos(R), std::sin(R) }), Sun,
                                    Vec3(kSunLux), 48, 12));
        };

        std::printf("     view elevation   luminance\n");
        for (float Degrees : { 1.0f, 0.1f, -0.1f, -1.0f, -5.0f, -20.0f })
            std::printf("     %10.1f deg  %10.1f\n", static_cast<double>(Degrees), static_cast<double>(Look(Degrees)));

        const float Above = Look(0.1f), Below = Look(-0.1f);
        const float Cliff = Above / Below;
        std::printf("     across the horizon the step is %.2fx\n", static_cast<double>(Cliff));
        Expect(Cliff < 8.0f, "no cliff into a void at the horizon — the ground is lit and visible");

        // Well below the horizon it must settle to a plausible sunlit ground, not to zero. 0.10 albedo under a
        //    50° sun through clear air is a few thousand cd/m²; the assertion is only that it is in that world.
        const float Ground = Look(-20.0f);
        std::printf("     20 deg down reads %.1f cd/m2 (a 0.10-albedo surface under this sun)\n",
                    static_cast<double>(Ground));
        Expect(Ground > 100.0f,   "the ground is genuinely lit, not a dim remnant of the air in front of it");
        Expect(Ground < 20000.0f, "and it is not brighter than the sky that lights it");

        // ⚠️ And it must go out with the sun. A ground that stayed lit at night would be worse than the void:
        //    the horizon would glow after dark for no reason a viewer could name.
        const Vec3  Night = SunAtElevation(-10.0f);
        const float NightGround = Luma(SkyRadiance(2.0f, Normalize(Vec3{ 0.0f, std::cos(-0.35f), std::sin(-0.35f) }),
                                                   Night, Vec3(kSunLux), 48, 12));
        std::printf("     with the sun 10 deg below, the same direction reads %.4f\n",
                    static_cast<double>(NightGround));
        Expect(NightGround < Ground * 0.01f, "and it goes dark when the sun sets, rather than glowing all night");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n26. the tables resolve twilight, which is where the whole sunrise lives\n");
    {
        // 🔴 The reported defect, traced. Multiple scattering is what makes a dawn horizon PALE rather than
        //    blood red — measured, it takes the horizon from R/B 238 down to 5.4 — so an error in that term
        //    lands directly on the colour. The table storing it was parameterised LINEAR IN COSINE, which spends
        //    its texels where nothing happens: at the horizon one texel of 32 spanned 3.58° of sun elevation, so
        //    the whole of civil twilight fell inside two of them.
        //
        //    Reconstruction is compared against the integral evaluated AT the angle, which is the thing the
        //    table is standing in for.
        const auto Forward = [](float Cosine)
        {
            const float Sign = Cosine < 0.0f ? -1.0f : 1.0f;
            return 0.5f + 0.5f * Sign * std::sqrt(std::fabs(Cosine));
        };

        // ⚠️ ComputeMultiScatterTexel takes a TEXEL COORDINATE and inverts the parameterisation itself, so
        //    reaching a particular sun angle means going through the forward map. Passing cos*0.5+0.5 was what
        //    that coordinate used to mean, and after the change it silently asks about a different angle — every
        //    number in the first run of this section was wrong for exactly that reason.
        const auto EvaluateAt = [&](float Cosine)
        { return ComputeMultiScatterTexel(Forward(Cosine), 0.0f, 48, 16); };
        const auto Sample = [&](float Cosine)
        {
            // Built through TransmittanceInverse and read through the forward map, exactly as the shader does.
            const float X = std::fmin(std::fmax(Forward(Cosine) * float(kMultiScatterSize) - 0.5f, 0.0f),
                                      float(kMultiScatterSize) - 1.0f);
            const int   I = static_cast<int>(std::floor(X));
            const float F = X - static_cast<float>(I);
            const auto  Texel = [&](int K)
            {
                const int   Clamped = K < 0 ? 0 : (K > int(kMultiScatterSize) - 1 ? int(kMultiScatterSize) - 1 : K);
                float Altitude = 0.0f, CosSunZenith = 0.0f;
                MultiScatterInverse((static_cast<float>(Clamped) + 0.5f) / float(kMultiScatterSize), 0.0f,
                                     Altitude, CosSunZenith);
                return EvaluateAt(CosSunZenith);
            };
            return Texel(I) * (1.0f - F) + Texel(I + 1) * F;
        };
        const auto Luma = [](Vec3 C) { return 0.2126f * C.x + 0.7152f * C.y + 0.0722f * C.z; };

        // ⚠️ The angle a single texel spans at the horizon is the whole point, so it is asserted directly.
        float Altitude = 0.0f, EdgeCosine = 0.0f;
        MultiScatterInverse(0.5f + 1.0f / float(kMultiScatterSize), 0.0f, Altitude, EdgeCosine);
        const float TexelDegrees = std::asin(std::fmin(1.0f, std::fabs(EdgeCosine))) * 180.0f / 3.14159265f;
        std::printf("     one texel spans %.3f deg of sun elevation at the horizon\n",
                    static_cast<double>(TexelDegrees));
        Expect(TexelDegrees < 0.5f, "a texel is a fraction of a degree at the horizon, not several degrees");

        std::printf("     sun elev      exact   reconstructed   error\n");
        float Worst = 1.0f;
        for (float Degrees : { 10.0f, 2.0f, 0.0f, -2.0f, -4.0f, -6.0f })
        {
            const float Cosine = std::sin(Degrees * 3.14159265f / 180.0f);
            const float Exact  = Luma(EvaluateAt(Cosine));
            const float Table  = Luma(Sample(Cosine));
            const float Error  = Exact > 0.0f ? std::fmax(Table / Exact, Exact / std::fmax(Table, 1e-12f)) : 1.0f;
            Worst = std::fmax(Worst, Error);
            std::printf("     %8.1f %10.6f %15.6f %7.2fx\n", static_cast<double>(Degrees),
                        static_cast<double>(Exact), static_cast<double>(Table), static_cast<double>(Error));
        }
        std::printf("     worst reconstruction error through twilight: %.2fx\n", static_cast<double>(Worst));
        Expect(Worst < 1.35f, "the table reproduces twilight to within a third of a stop");

        // What actually shipped, on the same angles, so the regression is recognisable: 32 texels, linear in
        //    cosine. Comparing against 64-texel linear would flatter the mapping by hiding the size behind it.
        constexpr int kWas = 32;
        const auto Linear = [&](float Cosine)
        {
            const float X = std::fmin(std::fmax((Cosine * 0.5f + 0.5f) * float(kWas) - 0.5f, 0.0f),
                                      float(kWas) - 1.0f);
            const int   I = static_cast<int>(std::floor(X));
            const float F = X - static_cast<float>(I);
            const auto  Texel = [&](int K)
            {
                const int Clamped = K < 0 ? 0 : (K > kWas - 1 ? kWas - 1 : K);
                // The OLD mapping: the texel held the value at cosine = 2u - 1.
                return EvaluateAt((static_cast<float>(Clamped) + 0.5f) / float(kWas) * 2.0f - 1.0f);
            };
            return Texel(I) * (1.0f - F) + Texel(I + 1) * F;
        };
        float WorstLinear = 1.0f;
        for (float Degrees : { 0.0f, -2.0f, -4.0f })
        {
            const float Cosine = std::sin(Degrees * 3.14159265f / 180.0f);
            const float Exact  = Luma(EvaluateAt(Cosine));
            const float Table  = Luma(Linear(Cosine));
            if (Exact > 0.0f) WorstLinear = std::fmax(WorstLinear, std::fmax(Table / Exact, Exact / Table));
        }
        std::printf("     what shipped (32 texels, linear) reached %.2fx on the same angles\n",
                    static_cast<double>(WorstLinear));
        Expect(WorstLinear > Worst * 1.5f, "the old table really was worse — this is the bug being fixed");
    }

    std::printf("\n>>> %s (%d failure%s)\n", Failures == 0 ? "ALL PASS" : "FAILURES", Failures, Failures == 1 ? "" : "s");
    return Failures == 0 ? 0 : 1;
}
