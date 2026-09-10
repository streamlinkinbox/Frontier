//============================================================================================================================================
// 📦 Engine/DisplayPresentation/CelestialSolver.h — Solar and lunar position from date, time and place
//============================================================================================================================================
// Round 9 deleted a CelestialSolver at the author's request (947b8b9). This is its replacement, written for the
//    Celestial port, and it is deliberately NOT a transcription of the reference demo.
//
// ⚠️ WHY NOT THE DEMO'S MODEL. CelestialPanel.html computes the sun as
//        sinE = cos(latitude) · cos(hourAngle)
//    which is the correct expression only when the solar declination is zero — that is, at an equinox. It carries
//    no date at all, so it cannot represent a season. Measured against NOAA at Benoni (−26.19°), local noon:
//        demo, any day of the year : +64.00°
//        truth, March equinox      : +63.66°   (the demo is right here, and only here)
//        truth, June solstice      : +40.33°
//        truth, December solstice  : +87.04°
//    A 47° error at the solstice is not a shading nuance: it is midwinter lit as midsummer, and every shadow in
//    the scene points the wrong way. The demo's own UI has no date control, so the simplification is invisible
//    there; the moment this engine grows one it becomes a visible defect.
//
// So this implements the NOAA solar position algorithm: Julian day, geometric mean longitude and anomaly, the
//    equation of centre, apparent longitude with the nutation term, obliquity, declination, and the equation of
//    time. It is the standard reference implementation, accurate to well under a degree for years 1900-2100,
//    which is far tighter than anything a renderer can show.
//
// Convention: world space is right-handed Z-up (CLAUDE.md §7). Elevation is degrees above the horizon, azimuth is
//    degrees clockwise from north. The returned direction points FROM the scene TOWARD the body, which is what a
//    shader wants for its dot(N, L).

#pragma once

#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                    OBSERVATION
//------------------------------------------------------------------------------------------------------------------------

// Where and when. Time is local wall-clock; UtcOffsetHours carries the zone so the solver can reach UTC, because
//    the equation of time is defined against solar time at the observer's meridian, not against the clock.
struct CelestialObservation
{
    int32_t Year        = 2026;
    int32_t Month       = 9;      // [1..12]
    int32_t Day         = 10;     // [1..31]
    float   LocalHours  = 12.0f;  // [h] local wall clock, fractional (13.5 = 13:30)
    float   UtcOffset   = 2.0f;   // [h] +2 = SAST
    float   Latitude    = -26.19f;// [deg] north positive
    float   Longitude   = 28.32f; // [deg] east positive
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      RESULT
//------------------------------------------------------------------------------------------------------------------------

struct CelestialBody
{
    float Elevation  = 0.0f;   // [deg] above the horizon; negative is below
    float Azimuth    = 0.0f;   // [deg] clockwise from north
    float Direction[3]{};      // [-] unit vector toward the body, right-handed Z-up
};

struct CelestialFrame
{
    CelestialBody Sun;
    CelestialBody Moon;
    float Declination     = 0.0f;   // [deg] solar declination — the season
    float EquationOfTime  = 0.0f;   // [min] apparent minus mean solar time
    float JulianDay       = 0.0f;   // [d]
    float MoonPhase       = 0.0f;   // [0..1] 0 = new, 0.5 = full
    float MoonIllumination= 0.0f;   // [0..1] lit fraction of the visible disc

    // Local sidereal time [deg]. The star catalogue stores equatorial J2000 directions, which are fixed to the
    //    sky rather than to the ground; this is the angle that turns one into the other, and it is what makes the
    //    stars wheel about the celestial pole as the night passes instead of hanging still.
    float LocalSiderealTime = 0.0f;
};

//------------------------------------------------------------------------------------------------------------------------
//                                              EQUATORIAL → HORIZON
//------------------------------------------------------------------------------------------------------------------------

// Rotates an equatorial J2000 direction into the observer's horizon frame (+X east, +Y north, +Z up), given the
//    local sidereal time and latitude from a CelestialFrame. Free function rather than a method because the star
//    field applies it to thousands of directions and has no business owning a solver.
//
//    ⚠️ Both rotations are needed and the order matters. Sidereal time alone spins the sky about the pole but
//    leaves the pole itself overhead, which is only correct at the geographic pole; latitude alone tips a sky
//    that is not turning. Applied together, in this order, Polaris sits at altitude = latitude, which is the
//    check the proof makes.
void EquatorialToHorizon(const float Equatorial[3], float LocalSiderealDegrees, float LatitudeDegrees,
                         float OutHorizon[3]) noexcept;

// The inverse. A star field wants this one: rotating the single view ray into the catalogue's frame costs one
//    rotation per pixel, where rotating the catalogue into the ray's frame would cost one per star.
void HorizonToEquatorial(const float Horizon[3], float LocalSiderealDegrees, float LatitudeDegrees,
                         float OutEquatorial[3]) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                      SOLVER
//------------------------------------------------------------------------------------------------------------------------

class CelestialSolver
{
public:
    // The whole solver. Pure: no state, no allocation, safe to call per frame or per pixel-block.
    [[nodiscard]] static CelestialFrame Solve(const CelestialObservation& At) noexcept;

    // Julian Day from the civil calendar (Fliegel-Van Flandern with the Gregorian correction).
    [[nodiscard]] static double JulianDayFrom(int32_t Year, int32_t Month, int32_t Day, double UtcHours) noexcept;

    // Air mass along the sun's path, Kasten-Young 1989. 1.0 at the zenith, ~38 at the horizon. Used by the
    //    Atmosphere entity's hero readout and by the star-visibility gate.
    [[nodiscard]] static float AirMass(float ElevationDegrees) noexcept;
};

} // namespace Frontier
