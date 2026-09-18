//============================================================================================================================================
// 📦 Engine/DisplayPresentation/CelestialSolver.cpp — NOAA solar position, plus a low-order lunar model
//============================================================================================================================================
// The header explains why this is not the reference demo's model. In short: the demo's sun is an equinox-only
//    approximation with no date, and at Benoni it reports +64° at noon on every day of the year against a true
//    +40.33° at the June solstice. This carries the date.

#include "CelestialSolver.h"

#include <cmath>

namespace Frontier {

namespace {

constexpr double kPi      = 3.14159265358979323846;
constexpr double kDegrees = kPi / 180.0;

inline double Radians(double Degrees) noexcept { return Degrees * kDegrees; }
inline double Degrees(double Radians) noexcept { return Radians / kDegrees; }

// Fold into [0, 360). std::fmod keeps the sign of the dividend, which is not what an angle wants.
inline double Wrap360(double Angle) noexcept
{
    double Folded = std::fmod(Angle, 360.0);
    return Folded < 0.0 ? Folded + 360.0 : Folded;
}

// Horizon direction → world vector. Right-handed Z-up (CLAUDE.md §7): +X east, +Y north, +Z up. Azimuth is
//    clockwise from north, so north is +Y and east is +X.
inline void ToDirection(double ElevationDeg, double AzimuthDeg, float Out[3]) noexcept
{
    const double E = Radians(ElevationDeg);
    const double A = Radians(AzimuthDeg);
    const double CosE = std::cos(E);
    Out[0] = static_cast<float>(CosE * std::sin(A));   // east
    Out[1] = static_cast<float>(CosE * std::cos(A));   // north
    Out[2] = static_cast<float>(std::sin(E));          // up
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------

double CelestialSolver::JulianDayFrom(int32_t Year, int32_t Month, int32_t Day, double UtcHours) noexcept
{
    // January and February are counted as months 13 and 14 of the previous year, which is what makes the
    //    30.6001 term land on the right month lengths.
    int32_t Y = Year;
    int32_t M = Month;
    if (M <= 2) { Y -= 1; M += 12; }

    const int32_t A = Y / 100;
    const int32_t B = 2 - A + A / 4;   // Gregorian correction

    return std::floor(365.25 * (Y + 4716)) + std::floor(30.6001 * (M + 1))
         + Day + B - 1524.5 + UtcHours / 24.0;
}

//------------------------------------------------------------------------------------------------------------------------

float CelestialSolver::AirMass(float ElevationDegrees) noexcept
{
    // Kasten-Young: the secant law diverges at the horizon, this stays finite. Beyond 96° zenith the sun is far
    //    enough below the horizon that the path length stops being meaningful; clamp rather than extrapolate.
    const double Zenith = 90.0 - static_cast<double>(ElevationDegrees);
    if (Zenith >= 96.0) return 40.0f;
    const double Denominator = std::cos(Radians(Zenith)) + 0.50572 * std::pow(96.07995 - Zenith, -1.6364);
    if (Denominator <= 0.0) return 40.0f;
    return static_cast<float>(std::fmin(40.0, 1.0 / Denominator));
}

//------------------------------------------------------------------------------------------------------------------------

CelestialFrame CelestialSolver::Solve(const CelestialObservation& At) noexcept
{
    CelestialFrame Frame{};

    const double UtcHours = static_cast<double>(At.LocalHours) - static_cast<double>(At.UtcOffset);
    const double Jd = JulianDayFrom(At.Year, At.Month, At.Day, UtcHours);
    Frame.JulianDay = static_cast<float>(Jd);

    // Julian centuries from J2000.0 — the argument every term below is a polynomial in.
    const double T = (Jd - 2451545.0) / 36525.0;

    // ── Solar longitude ────────────────────────────────────────────────────────────────────────────────────────
    const double MeanLongitude = Wrap360(280.46646 + T * (36000.76983 + T * 0.0003032));
    const double MeanAnomaly   = 357.52911 + T * (35999.05029 - 0.0001537 * T);
    const double Eccentricity  = 0.016708634 - T * (0.000042037 + 0.0000001267 * T);

    const double Mr = Radians(MeanAnomaly);
    // Equation of centre: the correction from the mean (circular) sun to the true (elliptical) one.
    const double Centre = std::sin(Mr)       * (1.914602 - T * (0.004817 + 0.000014 * T))
                        + std::sin(2.0 * Mr) * (0.019993 - 0.000101 * T)
                        + std::sin(3.0 * Mr) *  0.000289;

    const double TrueLongitude = MeanLongitude + Centre;

    // Nutation in longitude, the dominant 18.6-year term, plus aberration.
    const double Omega           = 125.04 - 1934.136 * T;
    const double ApparentLongitude = TrueLongitude - 0.00569 - 0.00478 * std::sin(Radians(Omega));

    // ── Obliquity and declination ──────────────────────────────────────────────────────────────────────────────
    const double MeanObliquity = 23.0 + (26.0 + (21.448 - T * (46.815 + T * (0.00059 - T * 0.001813))) / 60.0) / 60.0;
    const double Obliquity     = MeanObliquity + 0.00256 * std::cos(Radians(Omega));

    const double Declination = Degrees(std::asin(std::sin(Radians(Obliquity)) * std::sin(Radians(ApparentLongitude))));
    Frame.Declination = static_cast<float>(Declination);

    // ── Equation of time ───────────────────────────────────────────────────────────────────────────────────────
    // Apparent minus mean solar time, in minutes. This is what makes a sundial disagree with a clock by up to
    //    ±16 minutes across the year, and it is the term the demo's model has no way to express.
    const double Y2 = std::tan(Radians(Obliquity / 2.0)) * std::tan(Radians(Obliquity / 2.0));
    const double L0r = Radians(MeanLongitude);
    const double EquationOfTime = 4.0 * Degrees(
          Y2 * std::sin(2.0 * L0r)
        - 2.0 * Eccentricity * std::sin(Mr)
        + 4.0 * Eccentricity * Y2 * std::sin(Mr) * std::cos(2.0 * L0r)
        - 0.5 * Y2 * Y2 * std::sin(4.0 * L0r)
        - 1.25 * Eccentricity * Eccentricity * std::sin(2.0 * Mr));
    Frame.EquationOfTime = static_cast<float>(EquationOfTime);

    // ── Hour angle ─────────────────────────────────────────────────────────────────────────────────────────────
    // True solar time at the observer's meridian: the clock, corrected by the equation of time and by how far
    //    east of the zone meridian we stand (4 minutes per degree).
    const double TrueSolarMinutes = std::fmod(UtcHours * 60.0 + EquationOfTime + 4.0 * static_cast<double>(At.Longitude), 1440.0);
    const double HourAngle = TrueSolarMinutes / 4.0 - 180.0;

    // ── Horizon coordinates ────────────────────────────────────────────────────────────────────────────────────
    const double LatR  = Radians(static_cast<double>(At.Latitude));
    const double DecR  = Radians(Declination);
    const double HaR   = Radians(HourAngle);

    const double CosZenith = std::sin(LatR) * std::sin(DecR) + std::cos(LatR) * std::cos(DecR) * std::cos(HaR);
    const double Zenith    = std::acos(std::fmax(-1.0, std::fmin(1.0, CosZenith)));
    const double Elevation = 90.0 - Degrees(Zenith);

    double Azimuth = 0.0;
    const double SinZenith = std::sin(Zenith);
    if (std::fabs(SinZenith) > 1e-9)
    {
        const double CosAz = (std::sin(LatR) * std::cos(Zenith) - std::sin(DecR)) / (std::cos(LatR) * SinZenith);
        Azimuth = Degrees(std::acos(std::fmax(-1.0, std::fmin(1.0, CosAz))));
        // acos cannot tell morning from afternoon; the hour angle can.
        Azimuth = HourAngle > 0.0 ? Wrap360(Azimuth + 180.0) : Wrap360(540.0 - Azimuth);
    }

    Frame.Sun.Elevation = static_cast<float>(Elevation);
    Frame.Sun.Azimuth   = static_cast<float>(Azimuth);
    ToDirection(Elevation, Azimuth, Frame.Sun.Direction);

    // ── Moon ───────────────────────────────────────────────────────────────────────────────────────────────────
    // A low-order model: the mean elongation gives the phase, which is the part a renderer actually shows (the
    //    lit fraction and the terminator). Position uses the principal ecliptic terms only — good to a degree or
    //    so, which is finer than the disc itself. A full ELP series would be false precision here.
    const double MoonMeanLongitude = Wrap360(218.316 + 13.176396 * (Jd - 2451545.0));
    const double MoonMeanAnomaly   = Wrap360(134.963 + 13.064993 * (Jd - 2451545.0));
    const double MoonArgLatitude   = Wrap360(93.272  + 13.229350 * (Jd - 2451545.0));

    const double MoonLongitude = MoonMeanLongitude + 6.289 * std::sin(Radians(MoonMeanAnomaly));
    const double MoonLatitude  = 5.128 * std::sin(Radians(MoonArgLatitude));

    // Ecliptic → equatorial → horizon.
    const double MlR = Radians(MoonLongitude);
    const double MbR = Radians(MoonLatitude);
    const double ObR = Radians(Obliquity);
    const double MoonRa  = std::atan2(std::sin(MlR) * std::cos(ObR) - std::tan(MbR) * std::sin(ObR), std::cos(MlR));
    const double MoonDec = std::asin(std::sin(MbR) * std::cos(ObR) + std::cos(MbR) * std::sin(ObR) * std::sin(MlR));

    // Greenwich mean sidereal time, then the local hour angle of the moon.
    const double Gmst = Wrap360(280.46061837 + 360.98564736629 * (Jd - 2451545.0));
    const double MoonHourAngle = Radians(Wrap360(Gmst + static_cast<double>(At.Longitude) - Degrees(MoonRa)));

    const double MoonCosZ = std::sin(LatR) * std::sin(MoonDec) + std::cos(LatR) * std::cos(MoonDec) * std::cos(MoonHourAngle);
    const double MoonZ    = std::acos(std::fmax(-1.0, std::fmin(1.0, MoonCosZ)));
    const double MoonElevation = 90.0 - Degrees(MoonZ);

    double MoonAzimuth = 0.0;
    const double MoonSinZ = std::sin(MoonZ);
    if (std::fabs(MoonSinZ) > 1e-9)
    {
        const double CosAz = (std::sin(LatR) * std::cos(MoonZ) - std::sin(MoonDec)) / (std::cos(LatR) * MoonSinZ);
        MoonAzimuth = Degrees(std::acos(std::fmax(-1.0, std::fmin(1.0, CosAz))));
        MoonAzimuth = std::sin(MoonHourAngle) > 0.0 ? Wrap360(MoonAzimuth + 180.0) : Wrap360(540.0 - MoonAzimuth);
    }

    Frame.Moon.Elevation = static_cast<float>(MoonElevation);
    Frame.Moon.Azimuth   = static_cast<float>(MoonAzimuth);
    ToDirection(MoonElevation, MoonAzimuth, Frame.Moon.Direction);

    // Local sidereal time: Greenwich mean sidereal plus the observer's longitude. Exposed because the star
    //    catalogue is equatorial and needs it to reach the horizon frame.
    Frame.LocalSiderealTime = static_cast<float>(Wrap360(Gmst + static_cast<double>(At.Longitude)));

    // Phase from the sun-moon elongation. 0 = new, 0.5 = full.
    const double Elongation = Radians(Wrap360(MoonLongitude - ApparentLongitude));
    Frame.MoonPhase        = static_cast<float>(Wrap360(MoonLongitude - ApparentLongitude) / 360.0);
    Frame.MoonIllumination = static_cast<float>((1.0 - std::cos(Elongation)) * 0.5);

    return Frame;
}

void EquatorialToHorizon(const float Equatorial[3], float LocalSiderealDegrees, float LatitudeDegrees,
                         float OutHorizon[3]) noexcept
{
    // Equatorial J2000 in this catalogue is +X toward the vernal equinox, +Z toward the north celestial pole.
    // ① Spin about the pole by the local sidereal angle, bringing the meridian overhead.
    const double Theta = Radians(static_cast<double>(LocalSiderealDegrees));
    const double CosT = std::cos(Theta), SinT = std::sin(Theta);
    const double X1 =  Equatorial[0] * CosT + Equatorial[1] * SinT;
    const double Y1 = -Equatorial[0] * SinT + Equatorial[1] * CosT;
    const double Z1 =  Equatorial[2];

    // ② Tip by the latitude so the celestial pole lands at altitude == latitude rather than overhead.
    //    The pole is (0,0,1) here, so this rotation must send it to north = cos(lat), up = sin(lat). Writing it
    //    with the co-latitude's sine and cosine the other way round sends the pole overhead at the equator
    //    instead of to the horizon — which is what the first attempt did, and the Polaris check caught it.
    const double Lat  = Radians(static_cast<double>(LatitudeDegrees));
    const double CosL = std::cos(Lat), SinL = std::sin(Lat);
    // X1 is toward the meridian, Y1 east, Z1 the celestial pole.
    const double East  = Y1;
    const double North = X1 * SinL + Z1 * CosL;
    const double Up    = -X1 * CosL + Z1 * SinL;

    OutHorizon[0] = static_cast<float>(East);
    OutHorizon[1] = static_cast<float>(North);
    OutHorizon[2] = static_cast<float>(Up);
}

void HorizonToEquatorial(const float Horizon[3], float LocalSiderealDegrees, float LatitudeDegrees,
                         float OutEquatorial[3]) noexcept
{
    // Exactly EquatorialToHorizon with both rotations transposed, applied in the opposite order.
    const double Lat  = Radians(static_cast<double>(LatitudeDegrees));
    const double CosL = std::cos(Lat), SinL = std::sin(Lat);
    const double East = Horizon[0], North = Horizon[1], Up = Horizon[2];
    const double X1 = North * SinL - Up * CosL;
    const double Y1 = East;
    const double Z1 = North * CosL + Up * SinL;

    const double Theta = Radians(static_cast<double>(LocalSiderealDegrees));
    const double CosT = std::cos(Theta), SinT = std::sin(Theta);
    OutEquatorial[0] = static_cast<float>(X1 * CosT - Y1 * SinT);
    OutEquatorial[1] = static_cast<float>(X1 * SinT + Y1 * CosT);
    OutEquatorial[2] = static_cast<float>(Z1);
}

} // namespace Frontier
