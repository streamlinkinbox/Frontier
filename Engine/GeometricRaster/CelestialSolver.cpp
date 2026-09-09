//============================================================================================================================================
//                                                       CELESTIALSOLVER.CPP
//============================================================================================================================================

#include "CelestialSolver.h"

#include <cmath>

namespace Frontier {

namespace {

constexpr double kPi    = 3.14159265358979323846;
constexpr double kTwoPi = 6.28318530717958647692;

// Wrap into [0, 2π). Angles here come from dividing very large elapsed-second counts by a period, so the raw
//    value can be in the thousands of radians and fmod alone can return a negative.
double Wrap(double Radians) noexcept
{
    double Value = std::fmod(Radians, kTwoPi);
    if (Value < 0.0) Value += kTwoPi;
    return Value;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    KEPLER'S EQUATION
//------------------------------------------------------------------------------------------------------------------------

// M = E − e·sin E has no closed-form inverse. Newton converges quadratically for the eccentricities of planetary
//    orbits, so a handful of iterations reaches double precision — this is nothing like the cost of integrating,
//    and unlike integrating it gives the same answer on every machine for every t.
double CelestialSolver::SolveEccentricAnomaly(double MeanAnomaly, double Eccentricity) noexcept
{
    const double M = Wrap(MeanAnomaly);

    // For a near-circular orbit E ≈ M and the loop exits immediately; the offset start matters only for the
    //    highly eccentric case, where beginning at M can converge toward the wrong root near periapsis.
    double E = (Eccentricity < 0.8) ? M : kPi;

    for (int Iteration = 0; Iteration < 32; ++Iteration)
    {
        const double Residual   = E - Eccentricity * std::sin(E) - M;
        const double Derivative = 1.0 - Eccentricity * std::cos(E);
        if (std::fabs(Derivative) < 1e-15) break;               // e → 1 makes the derivative vanish at periapsis
        const double Step = Residual / Derivative;
        E -= Step;
        if (std::fabs(Step) < 1e-14) break;                     // converged to the limit of the representation
    }
    return E;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FRAME CONVERSION
//------------------------------------------------------------------------------------------------------------------------

// Equatorial (right ascension, declination) → the observer's horizon frame. This is where latitude enters, and
//    it is why the same sun is overhead at the equator and skims the horizon near the poles.
HorizonDirection CelestialSolver::ToHorizon(double RightAscension, double Declination, double Seconds) const noexcept
{
    const double HourAngle = Wrap(QuerySiderealAngle(Seconds) + Config.LongitudeRadians - RightAscension);

    const double SinDec = std::sin(Declination),  CosDec = std::cos(Declination);
    const double SinLat = std::sin(Config.LatitudeRadians), CosLat = std::cos(Config.LatitudeRadians);
    const double SinHour = std::sin(HourAngle),   CosHour = std::cos(HourAngle);

    const double SinAltitude = SinDec * SinLat + CosDec * CosLat * CosHour;
    const double Altitude    = std::asin(SinAltitude > 1.0 ? 1.0 : (SinAltitude < -1.0 ? -1.0 : SinAltitude));

    // atan2 form rather than acos: acos loses precision near the horizon and cannot distinguish east from west.
    const double Azimuth = Wrap(std::atan2(-CosDec * SinHour,
                                            SinDec * CosLat - CosDec * SinLat * CosHour));

    HorizonDirection Out{};
    Out.Elevation = Altitude;
    Out.Azimuth   = Azimuth;
    // Azimuth is measured clockwise from north, so north is +Y and east is +X.
    Out.North  = std::cos(Altitude) * std::cos(Azimuth);
    Out.East   = std::cos(Altitude) * std::sin(Azimuth);
    Out.Zenith = std::sin(Altitude);
    return Out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE SUN
//------------------------------------------------------------------------------------------------------------------------

double CelestialSolver::QuerySiderealAngle(double Seconds) const noexcept
{
    // A sidereal day is shorter than a solar one: the world must turn slightly more than a full revolution to
    //    bring the sun back to the meridian, because it has also moved along its orbit. Dropping this term makes
    //    the star field drift against the sun by a full day over a year.
    if (Config.DayLengthSeconds <= 0.0) return 0.0;
    const double SolarTurns    = Seconds / Config.DayLengthSeconds;
    const double OrbitalTurns  = (Config.YearLengthSeconds > 0.0) ? Seconds / Config.YearLengthSeconds : 0.0;

    // 🔴 THE CLOCK MUST AGREE WITH THE SUN. Without the phase below the rotation starts from an arbitrary zero,
    //    and the hour angle only reaches zero when the world has turned as far as the sun's right ascension at
    //    epoch. Measured on Earth's defaults that put solar noon at 06:55 — every day of the year, so not the
    //    equation of time but a straight five-hour epoch error. "Time of day 12:00" gave a sun at 25° in
    //    midsummer and one BELOW THE HORIZON in winter, which is why a shaft of sunlight could not be found by
    //    setting the clock to noon and looking.
    //
    //    ⚠️ It is also what the diurnal turbidity curve is measured against. That curve is cleanest at 06:00 and
    //    dirtiest at 18:00, and with the clock five hours out its "dawn" fell an hour before solar noon — the
    //    aerosol load was in almost perfect antiphase with the sun it was meant to describe.
    //
    //    Derived, not tuned: the phase is exactly the sun's right ascension at epoch, less the observer's
    //    longitude, less half a turn. That makes a day fraction of 0.5 solar noon by construction, for any
    //    latitude, obliquity, orbit or day length a project chooses.
    return Wrap(kTwoPi * (SolarTurns + OrbitalTurns) + SolarNoonPhase());
}

double CelestialSolver::SolarNoonPhase() const noexcept
{
    return Wrap(QuerySolarRightAscension(0.0) - Config.LongitudeRadians - kPi);
}

double CelestialSolver::QuerySolarRightAscension(double Seconds) const noexcept
{
    if (Config.YearLengthSeconds <= 0.0) return 0.0;

    const double MeanAnomaly = kTwoPi * Seconds / Config.YearLengthSeconds;
    const double E           = SolveEccentricAnomaly(MeanAnomaly, Config.Eccentricity);
    const double TrueAnomaly = 2.0 * std::atan2(std::sqrt(1.0 + Config.Eccentricity) * std::sin(E * 0.5),
                                                std::sqrt(1.0 - Config.Eccentricity) * std::cos(E * 0.5));
    const double Longitude   = Wrap(TrueAnomaly + Config.PerihelionRadians);
    return std::atan2(std::cos(Config.ObliquityRadians) * std::sin(Longitude), std::cos(Longitude));
}

double CelestialSolver::QuerySolarDeclination(double Seconds) const noexcept
{
    if (Config.YearLengthSeconds <= 0.0) return 0.0;

    // Mean anomaly → true anomaly → ecliptic longitude. The eccentric step is what makes the sun run fast near
    //    perihelion and slow near aphelion; with e = 0 it collapses to a uniform sweep, as it should.
    const double MeanAnomaly = kTwoPi * Seconds / Config.YearLengthSeconds;
    const double E           = SolveEccentricAnomaly(MeanAnomaly, Config.Eccentricity);
    const double TrueAnomaly = 2.0 * std::atan2(std::sqrt(1.0 + Config.Eccentricity) * std::sin(E * 0.5),
                                                std::sqrt(1.0 - Config.Eccentricity) * std::cos(E * 0.5));
    const double Longitude   = Wrap(TrueAnomaly + Config.PerihelionRadians);

    // Declination follows from projecting the ecliptic longitude onto the tilted equator. This is the entire
    //    mechanism of the seasons: with obliquity 0 it is identically zero and every day is an equinox.
    const double SinDeclination = std::sin(Config.ObliquityRadians) * std::sin(Longitude);
    return std::asin(SinDeclination > 1.0 ? 1.0 : (SinDeclination < -1.0 ? -1.0 : SinDeclination));
}

double CelestialSolver::QuerySolarDayFraction(double Seconds) const noexcept
{
    if (Config.DayLengthSeconds <= 0.0) return 0.0;
    const double Fraction = std::fmod(Seconds / Config.DayLengthSeconds, 1.0);
    return Fraction < 0.0 ? Fraction + 1.0 : Fraction;
}

HorizonDirection CelestialSolver::QuerySunDirection(double Seconds) const noexcept
{
    if (Config.YearLengthSeconds <= 0.0)
        return ToHorizon(0.0, 0.0, Seconds);

    const double MeanAnomaly = kTwoPi * Seconds / Config.YearLengthSeconds;
    const double E           = SolveEccentricAnomaly(MeanAnomaly, Config.Eccentricity);
    const double TrueAnomaly = 2.0 * std::atan2(std::sqrt(1.0 + Config.Eccentricity) * std::sin(E * 0.5),
                                                std::sqrt(1.0 - Config.Eccentricity) * std::cos(E * 0.5));
    const double Longitude   = Wrap(TrueAnomaly + Config.PerihelionRadians);

    const double SinObliquity = std::sin(Config.ObliquityRadians);

    const double RightAscension = QuerySolarRightAscension(Seconds);
    const double SinDeclination = SinObliquity * std::sin(Longitude);
    const double Declination    = std::asin(SinDeclination > 1.0 ? 1.0 : (SinDeclination < -1.0 ? -1.0 : SinDeclination));

    return ToHorizon(RightAscension, Declination, Seconds);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE MOON
//------------------------------------------------------------------------------------------------------------------------

HorizonDirection CelestialSolver::QueryMoonDirection(double Seconds) const noexcept
{
    if (Config.LunarPeriodSeconds <= 0.0) return ToHorizon(0.0, 0.0, Seconds);

    // A circular inclined orbit. The real moon's motion has large periodic terms — evection, variation — that a
    //    game sky does not need; what it does need is a moon that rises in roughly the right place, keeps a
    //    phase consistent with the sun, and never disagrees with itself between machines.
    const double Angle       = kTwoPi * Seconds / Config.LunarPeriodSeconds;
    const double Inclination = Config.LunarInclinationRadians;

    // Position in the orbital plane, then tilted by the inclination and by the obliquity into equatorial space.
    const double X = std::cos(Angle);
    const double Y = std::sin(Angle) * std::cos(Inclination);
    const double Z = std::sin(Angle) * std::sin(Inclination);

    const double CosObliquity = std::cos(Config.ObliquityRadians);
    const double SinObliquity = std::sin(Config.ObliquityRadians);
    const double EquatorialY  = Y * CosObliquity - Z * SinObliquity;
    const double EquatorialZ  = Y * SinObliquity + Z * CosObliquity;

    const double RightAscension = std::atan2(EquatorialY, X);
    const double Declination    = std::asin(EquatorialZ > 1.0 ? 1.0 : (EquatorialZ < -1.0 ? -1.0 : EquatorialZ));

    return ToHorizon(RightAscension, Declination, Seconds);
}

double CelestialSolver::QueryMoonPhase(double Seconds) const noexcept
{
    if (Config.LunarSynodicSeconds <= 0.0) return 0.0;

    // Phase is driven by the SYNODIC period — the moon's motion relative to the sun — not the sidereal one. Using
    //    the sidereal period makes the phase cycle 2.2 days short and the moon drifts out of step with the sun
    //    over a season, which looks correct for a week and then does not.
    const double Elongation = kTwoPi * Seconds / Config.LunarSynodicSeconds;
    return 0.5 * (1.0 - std::cos(Elongation));   // 0 at new, 1 at full
}

} // namespace Frontier
