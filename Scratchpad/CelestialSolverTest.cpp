// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
//  CelestialSolverTest.cpp — the clock reproduces real astronomy, and reproduces it identically every time
// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
//  Two kinds of assertion here, and both matter for different reasons.
//
//  The first kind checks against values that exist independently of this code: solar declination at the
//  solstices equals the obliquity, the sun crosses the equator at the equinoxes, an observer at the equator on
//  an equinox sees it pass overhead, and above the arctic circle in midsummer it never sets. A self-consistent
//  model that is wrong about all of these would still pass a purely internal test.
//
//  The second kind checks determinism, which is what the multiplayer design rests on. Query(t) must return
//  exactly the same bits regardless of how the clock arrived at t — stepped a frame at a time, jumped, or
//  scrubbed backwards. If that fails, two clients drift and the whole "replicate one number" argument collapses.
//
//  Build: see Scratchpad/CheckCelestialSolver.sh
// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════

#include "GeometricRaster/CelestialSolver.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>

using namespace Frontier;

static int Failures = 0;

static void Expect(bool Condition, const char* What)
{
    std::printf("  %-70s %s\n", What, Condition ? "PASS" : "FAIL");
    if (!Condition) ++Failures;
}

static constexpr double kPi  = 3.14159265358979323846;
static constexpr double kDay = 86400.0;
static double Degrees(double Radians) { return Radians * 180.0 / kPi; }

int main()
{
    std::printf("CelestialSolver — clock, orbits and horizon frame\n");

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n1. Kepler's equation inverts correctly\n");
    {
        // M = E − e·sin E, checked by substituting the solution back. Newton is only trustworthy if it actually
        //    converged, and a silent non-convergence would bias every subsequent position.
        double WorstResidual = 0.0;
        for (double Eccentricity : { 0.0, 0.0167, 0.2, 0.6, 0.9, 0.97 })
            for (int Step = 0; Step < 360; ++Step)
            {
                const double M = 2.0 * kPi * Step / 360.0;
                const double E = CelestialSolver::SolveEccentricAnomaly(M, Eccentricity);
                const double Residual = std::fabs((E - Eccentricity * std::sin(E)) - M);
                if (Residual > WorstResidual) WorstResidual = Residual;
            }
        std::printf("     worst residual over e = 0 … 0.97: %.3e rad\n", WorstResidual);
        Expect(WorstResidual < 1e-9, "Newton converges for every eccentricity a planet or comet could have");

        // A circular orbit has E = M exactly; anything else means the iteration is perturbing a solved case.
        const double Circular = CelestialSolver::SolveEccentricAnomaly(1.0, 0.0);
        Expect(std::fabs(Circular - 1.0) < 1e-15, "a circular orbit returns the mean anomaly untouched");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n2. solar declination matches the seasons\n");
    {
        CelestialSolver Sky;
        CelestialConfiguration Config{};
        Config.Eccentricity      = 0.0;    // a circular orbit puts the solstices exactly a quarter-year apart
        Config.PerihelionRadians = 0.0;
        Sky.AssignConfiguration(Config);

        const double Year = Config.YearLengthSeconds;
        const double Obliquity = Degrees(Config.ObliquityRadians);

        // Ecliptic longitude runs from 0 at the March equinox, so the solstices sit at a quarter and three
        //    quarters of a year and the declination there is ± the axial tilt. That number is the tilt itself,
        //    not something this code chooses, which is what makes it a real check.
        const double Spring = Degrees(Sky.QuerySolarDeclination(0.0));
        const double Summer = Degrees(Sky.QuerySolarDeclination(Year * 0.25));
        const double Autumn = Degrees(Sky.QuerySolarDeclination(Year * 0.50));
        const double Winter = Degrees(Sky.QuerySolarDeclination(Year * 0.75));

        std::printf("     equinox %+.3f°, solstice %+.3f°, equinox %+.3f°, solstice %+.3f°  (tilt %.4f°)\n",
                    Spring, Summer, Autumn, Winter, Obliquity);

        Expect(std::fabs(Spring) < 1e-9,            "the sun is exactly on the equator at the March equinox");
        Expect(std::fabs(Autumn) < 1e-6,            "and again at the September equinox");
        Expect(std::fabs(Summer - Obliquity) < 1e-6, "declination reaches +23.44° at the June solstice");
        Expect(std::fabs(Winter + Obliquity) < 1e-6, "and −23.44° at the December solstice");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n3. no axial tilt means no seasons\n");
    {
        // The strongest available check that obliquity is the *only* source of seasonal variation: zero it and
        //    the declination must vanish for every instant of the year, not merely average to zero.
        CelestialSolver Sky;
        CelestialConfiguration Config{};
        Config.ObliquityRadians = 0.0;
        Sky.AssignConfiguration(Config);

        double Worst = 0.0;
        for (int Day = 0; Day < 365; ++Day)
            Worst = std::fmax(Worst, std::fabs(Sky.QuerySolarDeclination(Day * kDay)));
        std::printf("     worst declination across a whole year: %.3e rad\n", Worst);
        Expect(Worst < 1e-12, "with zero tilt every day is an equinox");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n4. the sun passes overhead at the equator on an equinox\n");
    {
        CelestialSolver Sky;
        CelestialConfiguration Config{};
        Config.LatitudeRadians = 0.0;
        Config.Eccentricity    = 0.0;
        Config.PerihelionRadians = 0.0;
        Sky.AssignConfiguration(Config);

        // Sweep a day and find the highest the sun gets. At the equator on an equinox it must reach the zenith.
        double Highest = -kPi;
        for (int Minute = 0; Minute < 1440; ++Minute)
            Highest = std::fmax(Highest, Sky.QuerySunDirection(Minute * 60.0).Elevation);

        std::printf("     peak elevation: %.4f°\n", Degrees(Highest));
        Expect(Degrees(Highest) > 89.5, "the sun reaches the zenith, within a minute of sweep resolution");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n5. midnight sun above the arctic circle, polar night in winter\n");
    {
        // The sharpest test of the latitude term. Above the arctic circle the sun does not set in midsummer and
        //    does not rise in midwinter — a sign error in the horizon transform breaks this immediately, while
        //    leaving temperate latitudes looking plausible.
        CelestialSolver Sky;
        CelestialConfiguration Config{};
        Config.LatitudeRadians   = 78.0 * kPi / 180.0;   // Svalbard
        Config.Eccentricity      = 0.0;
        Config.PerihelionRadians = 0.0;
        Sky.AssignConfiguration(Config);

        const double Year = Config.YearLengthSeconds;

        double SummerLowest = kPi, WinterHighest = -kPi;
        for (int Minute = 0; Minute < 1440; ++Minute)
        {
            SummerLowest  = std::fmin(SummerLowest,  Sky.QuerySunDirection(Year * 0.25 + Minute * 60.0).Elevation);
            WinterHighest = std::fmax(WinterHighest, Sky.QuerySunDirection(Year * 0.75 + Minute * 60.0).Elevation);
        }
        std::printf("     midsummer minimum %+.3f°, midwinter maximum %+.3f°\n",
                    Degrees(SummerLowest), Degrees(WinterHighest));
        Expect(SummerLowest  > 0.0, "at 78° N the midsummer sun never touches the horizon");
        Expect(WinterHighest < 0.0, "and never clears it in midwinter");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n6. the horizon frame is a unit vector that agrees with its own angles\n");
    {
        CelestialSolver Sky;
        CelestialConfiguration Config{};
        Config.LatitudeRadians = 51.5 * kPi / 180.0;
        Sky.AssignConfiguration(Config);

        double WorstLength = 0.0, WorstElevation = 0.0;
        for (int Hour = 0; Hour < 24 * 40; ++Hour)
        {
            const HorizonDirection D = Sky.QuerySunDirection(Hour * 3600.0);
            const double Length = std::sqrt(D.East * D.East + D.North * D.North + D.Zenith * D.Zenith);
            WorstLength    = std::fmax(WorstLength, std::fabs(Length - 1.0));
            // Zenith is the sine of the elevation by construction; if they disagree one of them is being
            //    computed from a different angle and downstream code will pick whichever it happens to use.
            WorstElevation = std::fmax(WorstElevation, std::fabs(D.Zenith - std::sin(D.Elevation)));
        }
        std::printf("     worst |length−1| %.3e, worst |zenith−sin(elevation)| %.3e\n", WorstLength, WorstElevation);
        Expect(WorstLength    < 1e-12, "the direction is always unit length");
        Expect(WorstElevation < 1e-12, "and its vertical component always matches its stated elevation");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n7. the sky is a pure function of time — the multiplayer guarantee\n");
    {
        // The claim the whole design rests on: a client that is told the time reproduces the sky exactly, no
        //    matter how it got there. Stepping bit-for-bit equals jumping, or two players drift apart.
        CelestialSolver Stepped, Jumped;
        CelestialConfiguration Config{};
        Config.LatitudeRadians = 35.0 * kPi / 180.0;
        Stepped.AssignConfiguration(Config);
        Jumped.AssignConfiguration(Config);

        // 30 000 frames at 60 Hz — over eight hours of play, which is where an integrator would have drifted.
        for (int Frame = 0; Frame < 30000; ++Frame) Stepped.Advance(1.0 / 60.0);
        Jumped.AssignTime(30000.0 / 60.0);

        const HorizonDirection A = Stepped.QuerySunDirection(Stepped.QueryTime());
        const HorizonDirection B = Jumped.QuerySunDirection(Jumped.QueryTime());

        // The accumulated CLOCK differs by floating-point summation — that is expected and is exactly why the
        //    time itself is the replicated quantity rather than the positions.
        std::printf("     stepped clock %.9f s vs jumped %.9f s (difference %.3e)\n",
                    Stepped.QueryTime(), Jumped.QueryTime(), std::fabs(Stepped.QueryTime() - Jumped.QueryTime()));
        Expect(std::fabs(Stepped.QueryTime() - Jumped.QueryTime()) < 1e-6,
               "accumulating a frame at a time stays close to the exact time");

        // Given the SAME time, the answer must be bit-identical, not merely close.
        const HorizonDirection C = Jumped.QuerySunDirection(500.0);
        const HorizonDirection D = Jumped.QuerySunDirection(500.0);
        Expect(C.East == D.East && C.North == D.North && C.Zenith == D.Zenith,
               "the same instant always returns bit-identical directions");

        std::printf("     stepped vs jumped elevation difference: %.3e rad\n", std::fabs(A.Elevation - B.Elevation));
        Expect(std::fabs(A.Elevation - B.Elevation) < 1e-9,
               "and eight hours of stepping lands on the same sun as jumping straight there");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n8. time runs backwards as cleanly as forwards\n");
    {
        // A scrubbing editor and a replay both need this. An integrator cannot do it at all; a closed form gets
        //    it for nothing, which is a large part of why the design is worth the Newton iteration.
        CelestialSolver Sky;
        Sky.AssignConfiguration(CelestialConfiguration{});

        const HorizonDirection Forward = Sky.QuerySunDirection(7.0 * kDay);
        Sky.AssignTime(30.0 * kDay);
        Sky.AssignRate(-1.0);
        for (int Frame = 0; Frame < 60; ++Frame) Sky.Advance(1.0);   // wind back a minute
        const HorizonDirection Rewound = Sky.QuerySunDirection(7.0 * kDay);

        Expect(Forward.Elevation == Rewound.Elevation, "a rewound clock reports the same sky for the same instant");
        Expect(std::fabs(Sky.QueryTime() - (30.0 * kDay - 60.0)) < 1e-9, "a negative rate winds the clock back");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n9. a frozen clock freezes the sky, without a separate code path\n");
    {
        // "Orbits optional" is a rate of zero, not a branch. A second mechanism would be a second thing to get
        //    wrong, and would inevitably disagree with the first about something.
        CelestialSolver Sky;
        Sky.AssignConfiguration(CelestialConfiguration{});
        Sky.AssignTime(12345.0);
        Sky.AssignRate(0.0);

        const HorizonDirection Before = Sky.QuerySunDirection();
        for (int Frame = 0; Frame < 600; ++Frame) Sky.Advance(1.0 / 60.0);
        const HorizonDirection After = Sky.QuerySunDirection();

        Expect(Sky.QueryTime() == 12345.0,        "a zero rate advances the clock not at all");
        Expect(Before.Zenith == After.Zenith,     "so the sun does not move");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n10. the moon keeps step with the sun\n");
    {
        CelestialSolver Sky;
        CelestialConfiguration Config{};
        Sky.AssignConfiguration(Config);

        // Phase must complete exactly one cycle per SYNODIC month. Using the sidereal period instead is a
        //    plausible-looking mistake that runs 2.2 days fast and drifts out of step with the sun over a season.
        Expect(std::fabs(Sky.QueryMoonPhase(0.0) - 0.0) < 1e-12,
               "new moon at the epoch");
        Expect(std::fabs(Sky.QueryMoonPhase(Config.LunarSynodicSeconds * 0.5) - 1.0) < 1e-12,
               "full moon half a synodic month later");
        Expect(std::fabs(Sky.QueryMoonPhase(Config.LunarSynodicSeconds) - 0.0) < 1e-9,
               "and new again after a whole one");

        double Lowest = 2.0, Highest = -1.0;
        for (int Hour = 0; Hour < 24 * 60; ++Hour)
        {
            const double Phase = Sky.QueryMoonPhase(Hour * 3600.0);
            Lowest = std::fmin(Lowest, Phase); Highest = std::fmax(Highest, Phase);
        }
        Expect(Lowest >= 0.0 && Highest <= 1.0, "the illuminated fraction never leaves [0, 1]");

        // The moon must actually move against the stars, or it is nailed to the sky.
        const HorizonDirection M0 = Sky.QueryMoonDirection(0.0);
        const HorizonDirection M1 = Sky.QueryMoonDirection(6.0 * 3600.0);
        Expect(std::fabs(M0.Elevation - M1.Elevation) > 1e-3, "and the moon moves across the sky");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n11. the star field turns on sidereal time, not solar\n");
    {
        // A sidereal day is about four minutes short of a solar one. If the star field turned on solar time it
        //    would drift a full day against the sky over a year — the classic mistake, invisible for a week.
        CelestialSolver Sky;
        CelestialConfiguration Config{};
        Sky.AssignConfiguration(Config);

        // ⚠️ A DIFFERENCE between two samples, not the absolute angle after one day. The claim here is about the
        //    RATE the world turns at, and a rate is a difference — reading the absolute angle only worked while
        //    the rotation happened to start from zero, which was an accident of the epoch rather than anything
        //    the sidereal day depends on. The clock is now phased so that 12:00 is solar noon, and this test
        //    failed on that change while the physics it describes was untouched.
        double Extra = Sky.QuerySiderealAngle(kDay) - Sky.QuerySiderealAngle(0.0);
        while (Extra < 0.0)          Extra += 2.0 * kPi;
        while (Extra >= 2.0 * kPi)   Extra -= 2.0 * kPi;
        const double ExtraMinutes = Extra / (2.0 * kPi) * 1440.0;
        std::printf("     after one solar day the stars have over-turned by %.2f minutes\n", ExtraMinutes);
        Expect(ExtraMinutes > 3.0 && ExtraMinutes < 5.0,
               "the surplus is the familiar ~3.9 minutes a day");
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n11b. the clock's noon is the sun's noon\n");
    {
        // 🔴 A user-facing invariant, and it was wrong. The world's rotation used to start from an arbitrary
        //    zero, so the hour angle only reached zero once the world had turned as far as the sun's right
        //    ascension at epoch — measured on Earth's defaults, solar noon fell at 06:55 EVERY day of the year.
        //    Setting the clock to 12:00 gave a sun at 25° in midsummer and one below the horizon in winter,
        //    which is not a clock anybody can reason with, and it put the diurnal turbidity curve — cleanest at
        //    06:00, dirtiest at 18:00 — into near antiphase with the sun it describes.
        for (double LatitudeDegrees : { 0.0, 45.0, -33.0 })
        {
            CelestialSolver Sky;
            CelestialConfiguration Config{};
            Config.LatitudeRadians = LatitudeDegrees * kPi / 180.0;
            Sky.AssignConfiguration(Config);

            double Highest = -99.0, HighestHour = 0.0;
            for (int Minute = 0; Minute < 1440; ++Minute)
            {
                const double Hour = Minute / 60.0;
                const double Elevation = Sky.QuerySunDirection(Hour * 3600.0).Elevation;
                if (Elevation > Highest) { Highest = Elevation; HighestHour = Hour; }
            }
            std::printf("     latitude %+5.0f: the sun is highest at %.2f h\n", LatitudeDegrees, HighestHour);
            // ⚠️ Not exactly 12:00, and it must not be: the equation of time moves true solar noon by up to a
            //    quarter of an hour either way, and a solver that pinned it exactly would have thrown away the
            //    obliquity and eccentricity terms that test 12 proves are present.
            Expect(std::fabs(HighestHour - 12.0) < 0.30,
                   "solar noon is within a quarter hour of 12:00 — the rest is the equation of time");
        }
    }

    //------------------------------------------------------------------------------------------------------------------
    std::printf("\n12. the equation of time separates into its obliquity and eccentricity terms\n");
    {
        // ⚠️ The equation of time has TWO independent terms: orbital eccentricity AND axial obliquity. An
        //    earlier version of this test zeroed only the eccentricity and expected solar noon to stop moving —
        //    it still wandered 20 minutes, because the obliquity term was untouched. The test was wrong, not the
        //    solver. Both must be zeroed to get a sundial that agrees with a clock, and that is itself the
        //    sharper check: it proves the two terms are separable and that each is really present.
        const auto NoonSpread = [](double Eccentricity, double Obliquity)
        {
            CelestialSolver Sky;
            CelestialConfiguration Config{};
            Config.Eccentricity     = Eccentricity;
            Config.ObliquityRadians = Obliquity;
            Config.LatitudeRadians  = 0.0;
            Sky.AssignConfiguration(Config);

            double Earliest = 2.0, Latest = -1.0;
            for (int Day = 0; Day < 365; Day += 5)
            {
                double Best = -kPi, BestFraction = 0.0;
                for (int Minute = 0; Minute < 1440; ++Minute)
                {
                    const double T = Day * kDay + Minute * 60.0;
                    const double Elevation = Sky.QuerySunDirection(T).Elevation;
                    if (Elevation > Best) { Best = Elevation; BestFraction = Minute / 1440.0; }
                }
                Earliest = std::fmin(Earliest, BestFraction);
                Latest   = std::fmax(Latest,   BestFraction);
            }
            return (Latest - Earliest) * 1440.0;   // minutes
        };

        constexpr double kEarthTilt = 0.40910518;
        const double Neither      = NoonSpread(0.0,       0.0);
        const double TiltOnly     = NoonSpread(0.0,       kEarthTilt);
        const double EccentricOnly= NoonSpread(0.0167086, 0.0);
        const double Both         = NoonSpread(0.0167086, kEarthTilt);

        std::printf("     solar-noon spread over a year (minutes):\n");
        std::printf("       no tilt, circular    %5.1f\n", Neither);
        std::printf("       tilt only            %5.1f\n", TiltOnly);
        std::printf("       eccentricity only    %5.1f\n", EccentricOnly);
        std::printf("       both (Earth)         %5.1f\n", Both);

        Expect(Neither < 1.0,       "with neither term, a sundial agrees with a clock all year");
        Expect(TiltOnly > 5.0,      "obliquity alone moves solar noon — the larger of the two terms");
        Expect(EccentricOnly > 3.0, "eccentricity alone moves it too, independently");
        Expect(Both > TiltOnly * 0.8,
               "and Earth's real spread is at least as large as either term alone");
    }

    std::printf("\n>>> %s (%d failure%s)\n", Failures == 0 ? "ALL PASS" : "FAILURES", Failures, Failures == 1 ? "" : "s");
    return Failures == 0 ? 0 : 1;
}
