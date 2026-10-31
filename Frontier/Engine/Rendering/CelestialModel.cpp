/*==========================================================================================================================================
                                                   CELESTIAL MODEL DEFINITION
==========================================================================================================================================*/

#include "CelestialModel.h"

#include "Engine/Foundation/DeterministicRandom.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{

constexpr double Pi = 3.14159265358979323846;
constexpr double DegToRad = Pi / 180.0;

double WrapDegrees(double Degrees) noexcept
{
    Degrees = std::fmod(Degrees, 360.0);
    return Degrees < 0.0 ? Degrees + 360.0 : Degrees;
}
double WrapPi(double Radians) noexcept
{
    while (Radians > Pi) Radians -= 2.0 * Pi;
    while (Radians < -Pi) Radians += 2.0 * Pi;
    return Radians;
}

} // namespace

double JulianDate(int Year, unsigned Month, unsigned Day, double UtcHours) noexcept
{
    const int A = (14 - static_cast<int>(Month)) / 12;
    const int Y = Year + 4800 - A;
    const int M = static_cast<int>(Month) + 12 * A - 3;
    const long JDN = static_cast<long>(Day) + (153L * M + 2) / 5 + 365L * Y + Y / 4 - Y / 100 + Y / 400 - 32045;
    return static_cast<double>(JDN) + (UtcHours - 12.0) / 24.0;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            SUN
//------------------------------------------------------------------------------------------------------------------------

SolarPosition SunPosition(double Julian, double LatitudeDeg, double LongitudeDeg) noexcept
{
    const double N = Julian - 2451545.0;   // days since J2000.0

    const double MeanLongitude = WrapDegrees(280.460 + 0.9856474 * N);
    const double MeanAnomaly   = WrapDegrees(357.528 + 0.9856003 * N) * DegToRad;
    const double EclipticLongitude =
        (MeanLongitude + 1.915 * std::sin(MeanAnomaly) + 0.020 * std::sin(2.0 * MeanAnomaly)) * DegToRad;
    const double Obliquity = (23.439 - 0.0000004 * N) * DegToRad;

    const double Declination = std::asin(std::sin(Obliquity) * std::sin(EclipticLongitude));
    const double RightAscension = std::atan2(std::cos(Obliquity) * std::sin(EclipticLongitude),
                                             std::cos(EclipticLongitude));

    // Greenwich mean sidereal time (hours) -> local hour angle.
    double GmstHours = 18.697374558 + 24.06570982441908 * N;
    GmstHours = std::fmod(GmstHours, 24.0);
    if (GmstHours < 0.0) GmstHours += 24.0;
    const double LocalSiderealRad = (GmstHours * 15.0 + LongitudeDeg) * DegToRad;
    const double HourAngle = WrapPi(LocalSiderealRad - RightAscension);

    const double Latitude = LatitudeDeg * DegToRad;
    const double SinElevation = std::sin(Declination) * std::sin(Latitude) +
                                std::cos(Declination) * std::cos(Latitude) * std::cos(HourAngle);
    const double Elevation = std::asin(std::clamp(SinElevation, -1.0, 1.0));
    const double Azimuth = std::atan2(std::sin(HourAngle),
                                      std::cos(HourAngle) * std::sin(Latitude) -
                                          std::tan(Declination) * std::cos(Latitude)) + Pi;   // from north, clockwise

    SolarPosition Result;
    Result.ElevationRad   = Elevation;
    Result.AzimuthRad     = std::fmod(Azimuth, 2.0 * Pi);
    Result.DeclinationRad = Declination;
    return Result;
}

SpatialVector SunDirectionLocal(const SolarPosition& Position) noexcept
{
    const double CosEl = std::cos(Position.ElevationRad);
    // Azimuth clockwise from north: east = sin(az), north = cos(az).
    return {CosEl * std::sin(Position.AzimuthRad), CosEl * std::cos(Position.AzimuthRad),
            std::sin(Position.ElevationRad)};
}

DayPhase ClassifyDayPhase(double SolarElevationRad) noexcept
{
    const double Deg = SolarElevationRad / DegToRad;
    if (Deg > -0.833) return DayPhase::Day;
    if (Deg > -6.0) return DayPhase::CivilTwilight;
    if (Deg > -12.0) return DayPhase::NauticalTwilight;
    if (Deg > -18.0) return DayPhase::AstronomicalTwilight;
    return DayPhase::Night;
}

const char* DayPhaseName(DayPhase Phase) noexcept
{
    switch (Phase)
    {
        case DayPhase::Day: return "Day";
        case DayPhase::CivilTwilight: return "CivilTwilight";
        case DayPhase::NauticalTwilight: return "NauticalTwilight";
        case DayPhase::AstronomicalTwilight: return "AstronomicalTwilight";
        case DayPhase::Night: return "Night";
    }
    return "Night";
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            MOON
//------------------------------------------------------------------------------------------------------------------------

LunarPhase MoonPhase(double Julian) noexcept
{
    double Age = std::fmod(Julian - NewMoonEpochJulian, SynodicMonthDays);
    if (Age < 0.0) Age += SynodicMonthDays;
    const double PhaseAngle = 2.0 * Pi * Age / SynodicMonthDays;
    LunarPhase Result;
    Result.AgeDays      = Age;
    Result.Illumination = 0.5 * (1.0 - std::cos(PhaseAngle));
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            STARS
//------------------------------------------------------------------------------------------------------------------------

std::vector<StarRecord> GenerateStarCatalogue(uint32_t Count, uint64_t Seed)
{
    std::vector<StarRecord> Stars;
    Stars.reserve(Count);
    DeterministicRandom Random(Seed, 0x57A2ull);
    for (uint32_t I = 0; I < Count; ++I)
    {
        // Uniform sphere: z uniform in [-1, 1], azimuth uniform.
        const double Z     = 2.0 * Random.NextUnit() - 1.0;
        const double Phi   = 2.0 * Pi * Random.NextUnit();
        const double R     = std::sqrt(std::max(1.0 - Z * Z, 0.0));
        StarRecord   Star;
        Star.Direction = {R * std::cos(Phi), R * std::sin(Phi), Z};
        // Power-law magnitudes: many faint, few bright (mag in [-1, 6], mean ~3.8).
        Star.Magnitude   = -1.0 + 7.0 * std::pow(Random.NextUnit(), 0.45);
        Star.ColourIndex = 2.0 * Random.NextUnit() - 1.0;
        Stars.push_back(Star);
    }
    return Stars;
}

} // namespace Frontier
