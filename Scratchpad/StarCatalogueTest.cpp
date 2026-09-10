// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
//  StarCatalogueTest.cpp — the catalogue loads, the constellations are real, and the binning finds every star
// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
//  The binning is the part that fails silently. If a cell lookup is wrong the sky is simply empty, and if a
//  boundary star is missed it winks out only while the camera pans — neither produces an error, and both look
//  like something else. So the central assertion here is exhaustive: sample thousands of directions, and for
//  each one confirm that every star the brute-force search finds is also in the cell the lookup returns.
//
//  Build: see Scratchpad/CheckStarCatalogue.sh
// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════

#include "GeometricRaster/StarCatalogueIndex.h"
#include "DisplayPresentation/CelestialSolver.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace Frontier;

static int Failures = 0;

static void Expect(bool Condition, const char* What)
{
    std::printf("  %-70s %s\n", What, Condition ? "PASS" : "FAIL");
    if (!Condition) ++Failures;
}

static float Dot(const StarRecord& A, float X, float Y, float Z)
{
    return A.DirectionX * X + A.DirectionY * Y + A.DirectionZ * Z;
}

int main(int argc, char** argv)
{
    const std::string Path = (argc > 1) ? argv[1] : "EngineContent/StarCatalogue/BrightStars.bin";

    std::printf("StarCatalogueIndex — %s\n", Path.c_str());

    StarCatalogueIndex Catalogue;
    const bool Loaded = Catalogue.Load(Path);

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n1. the catalogue loads\n");
    {
        Expect(Loaded, "the binary parses");
        if (!Loaded)
        {
            std::printf("\n>>> FAILURES (cannot continue without a catalogue)\n");
            return 1;
        }
        std::printf("     %u stars → %zu after boundary duplication, %zu cells\n",
                    Catalogue.QuerySourceCount(), Catalogue.QueryStars().size(), Catalogue.QueryCells().size());
        Expect(Catalogue.QuerySourceCount() > 100u, "it contains a usable number of stars");
        Expect(Catalogue.QueryCells().size() == StarCatalogueIndex::kCellCount, "every cell has an entry");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n2. a missing or corrupt file degrades to an empty sky\n");
    {
        // A missing catalogue must mean "no stars", never a crash or garbage directions — this runs on machines
        //    that may not have the asset, and the failure has to be boring.
        StarCatalogueIndex Absent;
        Expect(!Absent.Load("EngineContent/StarCatalogue/NoSuchFile.bin"), "a missing file is refused");
        Expect(Absent.Empty(), "and leaves the index empty rather than partly filled");

        StarCatalogueIndex Garbage;
        Expect(!Garbage.Load("Tools/StarCatalogue/BrightStars.csv"), "a file with the wrong magic is refused");
        Expect(Garbage.Empty(), "and also leaves nothing behind");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n3. every star is a unit vector with sane photometry\n");
    {
        float WorstLength = 0.0f;
        bool  SaneLuminance = true, SaneColour = true;
        for (const StarRecord& S : Catalogue.QueryStars())
        {
            const float Length = std::sqrt(S.DirectionX * S.DirectionX + S.DirectionY * S.DirectionY
                                         + S.DirectionZ * S.DirectionZ);
            WorstLength = std::fmax(WorstLength, std::fabs(Length - 1.0f));
            if (!(S.Luminance > 0.0f) || S.Luminance > 100.0f) SaneLuminance = false;
            if (S.ColourRed < 0.0f || S.ColourRed > 1.001f
             || S.ColourGreen < 0.0f || S.ColourGreen > 1.001f
             || S.ColourBlue < 0.0f || S.ColourBlue > 1.001f) SaneColour = false;
        }
        std::printf("     worst |direction| − 1: %.3e\n", static_cast<double>(WorstLength));
        Expect(WorstLength < 1e-5f, "directions are unit length");
        Expect(SaneLuminance,       "luminances are positive and bounded");
        Expect(SaneColour,          "colours are inside [0, 1]");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n4. the cell lookup finds every star it should — the assertion that matters\n");
    {
        // Exhaustive: for thousands of directions, brute-force every star within the glow radius and confirm the
        //    binned cell contains it. A lookup that missed stars would show as an emptier sky, not as an error,
        //    and a boundary miss would only appear while panning.
        constexpr float kSearchRadius = 0.0025f;   // matches kStarAngularRadius in the index
        const float CosRadius = std::cos(kSearchRadius);

        uint32_t Probes = 0u, Expected = 0u, Missed = 0u;
        for (int I = 0; I < 20000; ++I)
        {
            // A Fibonacci sphere: uniform coverage, and deterministic so a failure reproduces.
            const float Fraction = (I + 0.5f) / 20000.0f;
            const float CosTheta = 1.0f - 2.0f * Fraction;
            const float SinTheta = std::sqrt(std::fmax(0.0f, 1.0f - CosTheta * CosTheta));
            const float Phi      = I * 2.39996323f;
            const float X = SinTheta * std::cos(Phi), Y = SinTheta * std::sin(Phi), Z = CosTheta;
            ++Probes;

            const uint32_t Cell = StarCatalogueIndex::CellForDirection(X, Y, Z);
            const StarCellRecord& Bucket = Catalogue.QueryCells()[Cell];

            for (const StarRecord& S : Catalogue.QueryStars())
            {
                if (Dot(S, X, Y, Z) <= CosRadius) continue;      // not within the glow of this direction
                ++Expected;

                bool Found = false;
                for (uint32_t J = 0u; J < Bucket.Count; ++J)
                {
                    const StarRecord& Candidate = Catalogue.QueryStars()[Bucket.First + J];
                    if (std::fabs(Candidate.DirectionX - S.DirectionX) < 1e-6f
                     && std::fabs(Candidate.DirectionY - S.DirectionY) < 1e-6f
                     && std::fabs(Candidate.DirectionZ - S.DirectionZ) < 1e-6f) { Found = true; break; }
                }
                if (!Found) ++Missed;
            }
        }
        std::printf("     %u probes, %u star hits expected, %u missed by the cell lookup\n",
                    Probes, Expected, Missed);
        Expect(Expected > 0u, "the probe set actually lands on stars, so the test is meaningful");
        Expect(Missed == 0u,  "the binned cell contains every star within the glow radius");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n5. binning is worth what it costs\n");
    {
        // The justification for the whole structure. If the average cell held most of the catalogue there would
        //    be no point binning at all.
        uint32_t Largest = 0u, Occupied = 0u;
        for (const StarCellRecord& Cell : Catalogue.QueryCells())
        {
            Largest = std::max(Largest, Cell.Count);
            if (Cell.Count > 0u) ++Occupied;
        }
        const double Average = static_cast<double>(Catalogue.QueryStars().size())
                             / static_cast<double>(StarCatalogueIndex::kCellCount);
        const double Duplication = static_cast<double>(Catalogue.QueryStars().size())
                                 / static_cast<double>(Catalogue.QuerySourceCount());

        std::printf("     average %.2f stars per cell, largest %u, %u of %u cells occupied\n",
                    Average, Largest, Occupied, StarCatalogueIndex::kCellCount);
        std::printf("     boundary duplication factor %.3f\n", Duplication);
        std::printf("     a pixel tests %u stars at worst instead of %u — %.0f× fewer\n",
                    Largest, Catalogue.QuerySourceCount(),
                    static_cast<double>(Catalogue.QuerySourceCount()) / std::max(1u, Largest));

        Expect(Largest < Catalogue.QuerySourceCount() / 4u,
               "the worst cell holds far less than the whole catalogue");
        Expect(Duplication < 1.6,
               "boundary duplication stays modest — it is not silently copying every star everywhere");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n6. the sky is the real sky\n");
    {
        // Geometry that exists independently of this repository. A parsing bug — reading right ascension as
        //    degrees rather than hours, say — still yields a plausible-looking sky, so the check has to be
        //    against known separations rather than against appearances.
        const auto Brightest = [&](float& X, float& Y, float& Z)
        {
            float Best = -1.0f;
            for (const StarRecord& S : Catalogue.QueryStars())
                if (S.Luminance > Best) { Best = S.Luminance; X = S.DirectionX; Y = S.DirectionY; Z = S.DirectionZ; }
            return Best;
        };
        float Sx = 0.0f, Sy = 0.0f, Sz = 0.0f;
        const float Peak = Brightest(Sx, Sy, Sz);

        // Sirius: magnitude −1.46, so luminance 10^(0.4 × 1.46) ≈ 3.83.
        std::printf("     brightest star luminance %.3f (Sirius should be ~3.83)\n", static_cast<double>(Peak));
        Expect(Peak > 3.5f && Peak < 4.2f, "the brightest star is Sirius at the right luminance");

        // Sirius sits at declination −16.7°, so its Z component is sin(−16.7°) ≈ −0.287.
        std::printf("     its declination component %.3f (should be ≈ −0.287)\n", static_cast<double>(Sz));
        Expect(std::fabs(Sz - (-0.287f)) < 0.01f, "and at the right declination — right ascension read as hours");
    }

    // ── 7. the sky is fixed to the pole, not to the ground ────────────────────────────────────────────────────
    // The catalogue is equatorial J2000; the renderer needs horizon coordinates. Two checks, both facts that
    //    hold independently of this code: Polaris stands at an altitude equal to the observer's latitude, and a
    //    star on the celestial equator rises and sets. Getting the second rotation's sine and cosine the wrong
    //    way round passes neither — the first attempt put Polaris overhead at the equator.
    std::printf("\n7. equatorial directions reach the horizon frame correctly\n");
    {
        const double Ra = 2.53 * 15.0 * 3.14159265358979323846 / 180.0;
        const double Dec = 89.26 * 3.14159265358979323846 / 180.0;
        const float Polaris[3] = { static_cast<float>(std::cos(Dec) * std::cos(Ra)),
                                   static_cast<float>(std::cos(Dec) * std::sin(Ra)),
                                   static_cast<float>(std::sin(Dec)) };
        bool AltitudeHolds = true;
        for (float Latitude : { 0.0f, 45.0f, -26.19f, 51.5f })
        {
            double Sum = 0.0;
            for (int Hour = 0; Hour < 24; ++Hour)
            {
                float Horizon[3];
                EquatorialToHorizon(Polaris, static_cast<float>(Hour * 15.0), Latitude, Horizon);
                Sum += std::asin(std::fmax(-1.0f, std::fmin(1.0f, Horizon[2]))) * 180.0 / 3.14159265358979323846;
            }
            // Polaris is 0.74 deg off the true pole, so it traces a small circle; the MEAN is the invariant.
            if (std::fabs(Sum / 24.0 - static_cast<double>(Latitude)) > 1.0) AltitudeHolds = false;
        }
        Expect(AltitudeHolds, "Polaris stands at an altitude equal to the latitude");

        const float Equator[3] = { 1.0f, 0.0f, 0.0f };
        double Lowest = 1e9, Highest = -1e9;
        for (int Hour = 0; Hour < 24; ++Hour)
        {
            float Horizon[3];
            EquatorialToHorizon(Equator, static_cast<float>(Hour * 15.0), -26.19f, Horizon);
            const double Altitude = std::asin(std::fmax(-1.0f, std::fmin(1.0f, Horizon[2]))) * 180.0 / 3.14159265358979323846;
            Lowest = std::fmin(Lowest, Altitude); Highest = std::fmax(Highest, Altitude);
        }
        std::printf("     an equatorial star runs from %.1f to %.1f deg over a day\n", Lowest, Highest);
        Expect(Lowest < -10.0 && Highest > 10.0, "a star on the celestial equator rises and sets");

        // The inverse is what the star field actually calls, once per pixel. If it disagrees with the forward
        //    transform the sky is simply empty, with no error anywhere.
        double Worst = 0.0;
        for (int I = 0; I < 2000; ++I)
        {
            const double A = I * 2.399963;
            const double B = std::acos(1.0 - 2.0 * ((I + 0.5) / 2000.0));
            const float Direction[3] = { static_cast<float>(std::sin(B) * std::cos(A)),
                                         static_cast<float>(std::sin(B) * std::sin(A)),
                                         static_cast<float>(std::cos(B)) };
            float Horizon[3], Back[3];
            EquatorialToHorizon(Direction, 137.4f, -26.19f, Horizon);
            HorizonToEquatorial(Horizon, 137.4f, -26.19f, Back);
            for (int K = 0; K < 3; ++K) Worst = std::fmax(Worst, std::fabs(static_cast<double>(Back[K]) - Direction[K]));
        }
        std::printf("     round trip over 2000 directions, worst error %.3e\n", Worst);
        Expect(Worst < 1e-5, "horizon and equatorial transforms are exact inverses");
    }

    // ── 8. the patterns people can name are in the right places ───────────────────────────────────────────────
    // Angular separations are the strongest available check: they are published, they do not depend on epoch
    //    conventions, and a catalogue read with right ascension in degrees instead of hours still looks like a
    //    sky while getting every one of these wrong.
    std::printf("\n8. recognisable constellations at their true separations\n");
    {
        const std::vector<StarRecord>& All = Catalogue.QueryStars();
        auto Nearest = [&](double RightAscensionHours, double DeclinationDegrees) -> const StarRecord*
        {
            const double Ra = RightAscensionHours * 15.0 * 3.14159265358979323846 / 180.0;
            const double Dec = DeclinationDegrees * 3.14159265358979323846 / 180.0;
            const float Target[3] = { static_cast<float>(std::cos(Dec) * std::cos(Ra)),
                                      static_cast<float>(std::cos(Dec) * std::sin(Ra)),
                                      static_cast<float>(std::sin(Dec)) };
            const StarRecord* Best = nullptr; double Closest = 1e9;
            for (const StarRecord& Star : All)
            {
                const double Dot = Star.DirectionX * Target[0] + Star.DirectionY * Target[1] + Star.DirectionZ * Target[2];
                const double Angle = std::acos(std::fmax(-1.0, std::fmin(1.0, Dot)));
                if (Angle < Closest) { Closest = Angle; Best = &Star; }
            }
            return Closest < 0.02 ? Best : nullptr;
        };
        struct Pattern { const char* Name; double Ra1, Dec1, Ra2, Dec2, Truth; };
        const Pattern Patterns[] = {
            { "Orion's belt, Mintaka to Alnitak",  5.5334,  -0.2991,  5.6793,  -1.9426,  2.70 },
            { "the Plough, Dubhe to Alkaid",      11.0621,  61.7510, 13.7923,  49.3133, 25.60 },
            { "Southern Cross, Acrux to Gacrux",  12.4433, -63.0991, 12.5194, -57.1132,  5.99 },
            { "the Pointers, Rigil to Hadar",     14.6600, -60.8340, 14.0637, -60.3730,  4.44 },
        };
        for (const Pattern& P : Patterns)
        {
            const StarRecord* A = Nearest(P.Ra1, P.Dec1);
            const StarRecord* B = Nearest(P.Ra2, P.Dec2);
            char What[160];
            if (A == nullptr || B == nullptr)
            {
                std::snprintf(What, sizeof(What), "%s is in the catalogue", P.Name);
                Expect(false, What);
                continue;
            }
            const double Dot = A->DirectionX * B->DirectionX + A->DirectionY * B->DirectionY + A->DirectionZ * B->DirectionZ;
            const double Measured = std::acos(std::fmax(-1.0, std::fmin(1.0, Dot))) * 180.0 / 3.14159265358979323846;
            std::snprintf(What, sizeof(What), "%s spans %.2f deg against %.2f", P.Name, Measured, P.Truth);
            Expect(std::fabs(Measured - P.Truth) <= 0.15, What);
        }
    }

    std::printf("\n>>> %s (%d failure%s)\n", Failures == 0 ? "ALL PASS" : "FAILURES", Failures, Failures == 1 ? "" : "s");
    return Failures == 0 ? 0 : 1;
}
