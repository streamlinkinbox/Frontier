//============================================================================================================================================
// 📦 Scratchpad/CelestialSolverProof.cpp — the sun is where the almanac says it is
//============================================================================================================================================
// Celestial port, step 1. Checks CelestialSolver against values computed independently from the published NOAA
//    algorithm, not against another run of the same code — the distinction ShadowMatrixProof established, and the
//    reason the PCSS half-angle bug was catchable at all.
//
// It also pins the reason this solver exists rather than the reference demo's: the demo's sun carries no date and
//    is therefore an equinox-only model. That is checked explicitly below, so if anyone ever "simplifies" this
//    back toward the demo the seasons collapse and the gate says so.

#include "DisplayPresentation/CelestialSolver.h"

#include <cmath>
#include <cstdio>
#include <cstdint>

using namespace Frontier;

namespace {

int Failures = 0;

void Check(const char* Label, double Actual, double Expected, double Tolerance, const char* Unit)
{
    const double Error = std::fabs(Actual - Expected);
    const bool   Ok    = Error <= Tolerance;
    if (!Ok) ++Failures;
    std::printf("  %-52s %+9.3f vs %+9.3f %-4s  err %6.3f  %s\n",
                Label, Actual, Expected, Unit, Error, Ok ? "PASS" : "FAIL");
}

// Angular difference that respects the 0/360 seam: an azimuth of 359.9 and 0.1 differ by 0.2, not 359.8.
double AngleDelta(double A, double B)
{
    double D = std::fmod(std::fabs(A - B), 360.0);
    return D > 180.0 ? 360.0 - D : D;
}

void CheckAzimuth(const char* Label, double Actual, double Expected, double Tolerance)
{
    const double Error = AngleDelta(Actual, Expected);
    const bool   Ok    = Error <= Tolerance;
    if (!Ok) ++Failures;
    std::printf("  %-52s %+9.3f vs %+9.3f deg   err %6.3f  %s\n",
                Label, Actual, Expected, Error, Ok ? "PASS" : "FAIL");
}

// Sun-moon separation from a solved frame, in degrees. Elongation says where the moon MUST stand relative
//    to the sun whatever the model's absolute error, so this is the assertion the ranges cannot express.
double SunMoonSeparation(const CelestialFrame& F)
{
    const double Dot = static_cast<double>(F.Sun.Direction[0]) * F.Moon.Direction[0]
                     + static_cast<double>(F.Sun.Direction[1]) * F.Moon.Direction[1]
                     + static_cast<double>(F.Sun.Direction[2]) * F.Moon.Direction[2];
    return std::acos(std::fmax(-1.0, std::fmin(1.0, Dot))) * 180.0 / 3.14159265358979323846;
}

} // namespace

int main()
{
    std::printf("\nCelestialSolver — against independently computed NOAA values\n");
    for (int I = 0; I < 108; ++I) std::putchar('=');
    std::printf("\n\n");

    // ── ① Julian Day against known epochs ──────────────────────────────────────────────────────────────────────
    // J2000.0 is 2000-01-01 12:00 UTC = JD 2451545.0 exactly, by definition. If this is wrong nothing else can
    //    be right, so it is checked first and tightly.
    std::printf("① Julian Day (definitional epochs)\n");
    Check("J2000.0 = 2000-01-01 12:00 UTC",
          CelestialSolver::JulianDayFrom(2000, 1, 1, 12.0), 2451545.0, 1e-6, "d");
    Check("1970-01-01 00:00 UTC (Unix epoch)",
          CelestialSolver::JulianDayFrom(1970, 1, 1, 0.0), 2440587.5, 1e-6, "d");
    Check("2026-09-10 00:00 UTC",
          CelestialSolver::JulianDayFrom(2026, 9, 10, 0.0), 2461293.5, 1e-6, "d");
    std::printf("\n");

    // ── ② Solar position at Benoni across a day ────────────────────────────────────────────────────────────────
    // Reference values from the NOAA algorithm evaluated independently for -26.19, 28.32 on 2026-09-10 (SAST,
    //    UTC+2). Tolerance 0.05 deg: far tighter than a renderer can show, loose enough for double-rounding.
    std::printf("② Sun at Benoni (-26.19, +28.32), 2026-09-10 SAST\n");
    struct Sample { float Hour; double Elevation; double Azimuth; };
    const Sample Day[] = {
        {  6.0f,  -3.04,  86.0 },
        {  9.0f,  35.73,  61.9 },
        { 12.0f,  58.95,   1.8 },
        { 15.0f,  37.26, 299.4 },
        { 18.0f,  -1.28, 274.7 },
    };
    for (const Sample& S : Day)
    {
        CelestialObservation At{};
        At.Year = 2026; At.Month = 9; At.Day = 10;
        At.LocalHours = S.Hour; At.UtcOffset = 2.0f;
        At.Latitude = -26.19f; At.Longitude = 28.32f;
        const CelestialFrame F = CelestialSolver::Solve(At);

        char Label[96];
        std::snprintf(Label, sizeof(Label), "%02.0f:00 elevation", static_cast<double>(S.Hour));
        Check(Label, F.Sun.Elevation, S.Elevation, 0.05, "deg");
        std::snprintf(Label, sizeof(Label), "%02.0f:00 azimuth", static_cast<double>(S.Hour));
        CheckAzimuth(Label, F.Sun.Azimuth, S.Azimuth, 0.05);
    }
    std::printf("\n");

    // ── ③ The seasons — what the demo's model cannot do ────────────────────────────────────────────────────────
    // This is the whole reason the solver carries a date. The demo reports +64.00 at Benoni noon on EVERY day of
    //    the year; the truth swings 47 degrees between the solstices.
    std::printf("③ Seasons at Benoni noon (the demo's model reports +64.00 for all of these)\n");
    struct Season { int Month; int Day; const char* Name; double Elevation; double Declination; };
    const Season Year[] = {
        {  3, 20, "March equinox",    63.66,  -0.08 },
        {  6, 21, "June solstice",    40.33,  23.44 },
        {  9, 10, "the day above",    58.95,   4.85 },
        { 12, 21, "December solstice",87.04, -23.44 },
    };
    for (const Season& S : Year)
    {
        CelestialObservation At{};
        At.Year = 2026; At.Month = S.Month; At.Day = S.Day;
        At.LocalHours = 12.0f; At.UtcOffset = 2.0f;
        At.Latitude = -26.19f; At.Longitude = 28.32f;
        const CelestialFrame F = CelestialSolver::Solve(At);

        char Label[96];
        std::snprintf(Label, sizeof(Label), "%s: noon elevation", S.Name);
        Check(Label, F.Sun.Elevation, S.Elevation, 0.20, "deg");
        std::snprintf(Label, sizeof(Label), "%s: declination", S.Name);
        Check(Label, F.Declination, S.Declination, 0.10, "deg");
    }
    // The seasonal swing itself, stated as one number so the regression is unmissable.
    {
        CelestialObservation Jun{}; Jun.Year = 2026; Jun.Month = 6;  Jun.Day = 21;
        Jun.LocalHours = 12.0f; Jun.UtcOffset = 2.0f; Jun.Latitude = -26.19f; Jun.Longitude = 28.32f;
        CelestialObservation Dec = Jun; Dec.Month = 12; Dec.Day = 21;
        const float Swing = CelestialSolver::Solve(Dec).Sun.Elevation - CelestialSolver::Solve(Jun).Sun.Elevation;
        Check("solstice-to-solstice swing (0 would mean no seasons)", Swing, 46.71, 0.40, "deg");
    }
    std::printf("\n");

    // ── ④ Equation of time ─────────────────────────────────────────────────────────────────────────────────────
    // The analemma: a sundial runs up to ~16 minutes ahead of the clock in early November and ~14 behind in
    //    mid-February. Four points around the year, each ±0.6 min.
    std::printf("④ Equation of time (the analemma)\n");
    struct Eot { int Month; int Day; const char* Name; double Minutes; };
    const Eot Points[] = {
        {  2, 11, "mid-February minimum", -14.24 },
        {  5, 14, "mid-May maximum",        3.65 },
        {  7, 26, "late-July minimum",     -6.54 },
        { 11,  3, "early-November maximum",16.45 },
    };
    for (const Eot& E : Points)
    {
        CelestialObservation At{};
        At.Year = 2026; At.Month = E.Month; At.Day = E.Day;
        At.LocalHours = 12.0f; At.UtcOffset = 0.0f; At.Latitude = 0.0f; At.Longitude = 0.0f;
        const CelestialFrame F = CelestialSolver::Solve(At);
        char Label[96];
        std::snprintf(Label, sizeof(Label), "%s", E.Name);
        Check(Label, F.EquationOfTime, E.Minutes, 0.60, "min");
    }
    std::printf("\n");

    // ── ⑤ Direction vector consistency ─────────────────────────────────────────────────────────────────────────
    // The vector must be unit length and must agree with the elevation/azimuth it was built from, in the
    //    engine's right-handed Z-up convention (+X east, +Y north, +Z up).
    std::printf("⑤ Direction vector (right-handed Z-up: +X east, +Y north, +Z up)\n");
    {
        CelestialObservation At{};
        At.Year = 2026; At.Month = 9; At.Day = 10;
        At.LocalHours = 12.0f; At.UtcOffset = 2.0f;
        At.Latitude = -26.19f; At.Longitude = 28.32f;
        const CelestialFrame F = CelestialSolver::Solve(At);
        const float* D = F.Sun.Direction;

        const double Length = std::sqrt(static_cast<double>(D[0]) * D[0] + static_cast<double>(D[1]) * D[1] + static_cast<double>(D[2]) * D[2]);
        Check("|direction|", Length, 1.0, 1e-5, "-");
        Check("direction.z == sin(elevation)", D[2], std::sin(F.Sun.Elevation * 3.14159265358979323846 / 180.0), 1e-5, "-");

        // Southern hemisphere: the noon sun stands to the NORTH, so +Y must dominate and be positive.
        Check("noon sun is northward (southern hemisphere)", D[1] > 0.0f ? 1.0 : 0.0, 1.0, 0.0, "bool");

        // Northern hemisphere mirror: the same date at +26.19 must put the noon sun to the SOUTH.
        CelestialObservation North = At; North.Latitude = 26.19f;
        const CelestialFrame G = CelestialSolver::Solve(North);
        Check("noon sun is southward (northern hemisphere)", G.Sun.Direction[1] < 0.0f ? 1.0 : 0.0, 1.0, 0.0, "bool");
    }
    std::printf("\n");

    // ── ⑥ Air mass ─────────────────────────────────────────────────────────────────────────────────────────────
    // Kasten-Young. 1.0 at the zenith by definition, ~2 at 30 deg, and finite at the horizon where a plain
    //    secant would blow up.
    std::printf("⑥ Air mass (Kasten-Young 1989)\n");
    Check("zenith (90 deg)",       CelestialSolver::AirMass(90.0f),  1.0000, 0.001, "AM");
    Check("30 deg elevation",      CelestialSolver::AirMass(30.0f),  1.9928, 0.010, "AM");
    Check("horizon (0 deg)",       CelestialSolver::AirMass(0.0f),  37.9196, 0.100, "AM");
    Check("below horizon clamps",  CelestialSolver::AirMass(-10.0f),40.0000, 0.001, "AM");
    std::printf("\n");

    // ── ⑦ Moon ─────────────────────────────────────────────────────────────────────────────────────────────────
    // The low-order model earns only a coarse check: illumination in range, phase in range, and full moon
    //    opposite the sun. Anything tighter would be false precision (see the .cpp note).
    std::printf("⑦ Moon (low-order model: range and opposition only)\n");
    {
        CelestialObservation At{};
        At.Year = 2026; At.Month = 9; At.Day = 10;
        At.LocalHours = 22.0f; At.UtcOffset = 2.0f;
        At.Latitude = -26.19f; At.Longitude = 28.32f;
        const CelestialFrame F = CelestialSolver::Solve(At);
        Check("illumination within [0,1]", (F.MoonIllumination >= 0.0f && F.MoonIllumination <= 1.0f) ? 1.0 : 0.0, 1.0, 0.0, "bool");
        Check("phase within [0,1]",        (F.MoonPhase >= 0.0f && F.MoonPhase <= 1.0f) ? 1.0 : 0.0, 1.0, 0.0, "bool");
        const double Length = std::sqrt(static_cast<double>(F.Moon.Direction[0]) * F.Moon.Direction[0]
                                      + static_cast<double>(F.Moon.Direction[1]) * F.Moon.Direction[1]
                                      + static_cast<double>(F.Moon.Direction[2]) * F.Moon.Direction[2]);
        Check("|moon direction|", Length, 1.0, 1e-5, "-");
        // The azimuth quadrant is pinned, not just the length: an inverted morning/afternoon rule once put the
        //    new moon 108° from the sun instead of 6° — the direction was unit and every range above was green
        //    while the moon stood in the wrong quarter of the sky. Tolerances stay loose (the model is low-order)
        //    but the wrong quadrant misses by tens of degrees, so these still bite.
        {
            CelestialObservation New{};
            New.Year = 2026; New.Month = 9; New.Day = 10;
            New.LocalHours = 15.5f; New.UtcOffset = 2.0f;
            New.Latitude = -26.19f; New.Longitude = 28.32f;
            Check("new moon sits with the sun", SunMoonSeparation(CelestialSolver::Solve(New)), 6.3, 2.0, "deg");
        }
        {
            CelestialObservation Full{};
            Full.Year = 2026; Full.Month = 9; Full.Day = 26;
            Full.LocalHours = 22.0f; Full.UtcOffset = 2.0f;
            Full.Latitude = -26.19f; Full.Longitude = 28.32f;
            Check("full moon opposes the sun", SunMoonSeparation(CelestialSolver::Solve(Full)), 176.5, 3.0, "deg");
        }
    }
    std::printf("\n");

    for (int I = 0; I < 108; ++I) std::putchar('=');
    std::printf("\n%s\n\n", Failures == 0 ? "  the solver agrees with the almanac"
                                          : "  SOLVER DISAGREES WITH THE ALMANAC");
    return Failures == 0 ? 0 : 1;
}
