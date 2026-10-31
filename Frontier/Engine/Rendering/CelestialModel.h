/*==========================================================================================================================================
                                                       CELESTIAL MODEL
                                     ------------------------------------------------------------------
                 Solar position, twilight phases, lunar phase and a deterministic star catalogue
                      (plan §8.1: CelestialSky, NativeTwilight, NightStars, SkyProjection).
==========================================================================================================================================*/

// 🧩 Celestial model: where the sun and moon are, what the light is doing about it.
//
// Low-precision solar algorithm (NOAA/Meeus truncated series, ~0.01-0.05 degree class — far beyond
// visual needs), civil-date Julian conversion, twilight classification by solar elevation, lunar
// age/illumination from a fixed new-moon epoch, and a hash-generated star catalogue (uniform sphere,
// power-law magnitudes) that is bit-identical for a given seed on every platform. Angles in the API
// are radians unless the name says Degrees; times are Julian dates (UTC).

#pragma once

#include "ClusterNesting.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

// Julian date from a proleptic Gregorian civil date + UTC fractional hours.
[[nodiscard]] double JulianDate(int Year, unsigned Month, unsigned Day, double UtcHours) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                            SUN
//------------------------------------------------------------------------------------------------------------------------

struct SolarPosition
{
    double ElevationRad = 0.0;   // [rad] above horizon (negative below)
    double AzimuthRad   = 0.0;   // [rad] clockwise from north
    double DeclinationRad = 0.0; // [rad]
};

[[nodiscard]] SolarPosition SunPosition(double Julian, double LatitudeDeg, double LongitudeDeg) noexcept;

// Unit vector toward the sun in a local frame: X = east, Y = north, Z = up.
[[nodiscard]] SpatialVector SunDirectionLocal(const SolarPosition& Position) noexcept;

enum class DayPhase : uint8_t
{
    Day,               // sun above -0.833 deg
    CivilTwilight,     // -6 .. -0.833
    NauticalTwilight,  // -12 .. -6
    AstronomicalTwilight, // -18 .. -12
    Night              // below -18
};

[[nodiscard]] DayPhase ClassifyDayPhase(double SolarElevationRad) noexcept;
[[nodiscard]] const char* DayPhaseName(DayPhase Phase) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                            MOON
//------------------------------------------------------------------------------------------------------------------------

struct LunarPhase
{
    double AgeDays = 0.0;        // [days] since new moon, [0, synodic)
    double Illumination = 0.0;   // [-] lit fraction of the disc, [0, 1]
};

inline constexpr double SynodicMonthDays = 29.530588853;
inline constexpr double NewMoonEpochJulian = 2451550.26;   // 2000-01-06 ~18:14 UTC

[[nodiscard]] LunarPhase MoonPhase(double Julian) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STARS
//------------------------------------------------------------------------------------------------------------------------

struct StarRecord
{
    SpatialVector Direction{};   // unit vector (equatorial-ish frame; caller rotates)
    double        Magnitude = 0.0;   // [mag] lower is brighter
    double        ColourIndex = 0.0; // [-] blue (-1) .. red (+1), deterministic per star
};

// Deterministic catalogue: uniform sphere directions, magnitudes from a power law on the seed stream.
[[nodiscard]] std::vector<StarRecord> GenerateStarCatalogue(uint32_t Count, uint64_t Seed);

} // namespace Frontier
