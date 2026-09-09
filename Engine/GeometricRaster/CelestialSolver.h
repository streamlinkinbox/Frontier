//============================================================================================================================================
//                                                        CELESTIALSOLVER.H
//============================================================================================================================================
// 🧩 The authoritative clock, and every celestial direction derived from it.
//
//    ONE 64-bit value — elapsed simulation seconds — determines where every sun, moon and star in every world
//    is, at every instant. Nothing celestial is ever stored, replicated or interpolated: a joining client
//    receives that single number and reproduces the whole sky exactly. There is no drift because there is
//    nothing to drift, and no authority conflict because there is nothing to disagree about.
//
//    🔴 Positions are CLOSED FORM, never integrated. An integrator accumulates rounding differently on
//    different machines and diverges over hours of play, which for a networked game means two players
//    eventually see different suns. `Query(t)` is a pure function: identical everywhere, reversible, and
//    scrubbable for free — a time-of-day slider is just a different argument.
//
//    Named …Solver because Kepler's equation has no closed-form inverse and is satisfied by Newton iteration,
//    which is precisely `Solver` (CLAUDE.md §2.6). It is not an `Integrator`: nothing here advances a
//    differential equation.
//
//    Engine ⇄ project seam. This knows orbital elements, obliquity and sidereal rotation. It does not know
//    that one body is "the sun of the showroom world" — the project owns that naming, and supplies the
//    elements.

#pragma once

#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                    DIRECTIONS
//------------------------------------------------------------------------------------------------------------------------

// A direction in the observer's local horizon frame, which is what an atmosphere needs: the sky-view LUT is
//    parameterised by elevation above the horizon, not by anything ecliptic.
//    Engine convention is Z-up, +X east, +Y north (CLAUDE.md §7).
struct HorizonDirection
{
    double East      = 0.0;   // [-] unit vector, +X
    double North     = 0.0;   // [-] unit vector, +Y
    double Zenith    = 0.0;   // [-] unit vector, +Z — negative means the body is below the horizon
    double Elevation = 0.0;   // [rad] above the horizon; negative below
    double Azimuth   = 0.0;   // [rad] clockwise from north, the surveying convention
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

// Where and how fast the observer's world turns. Defaults are Earth's, so a project that supplies nothing gets
//    a recognisable sky rather than a degenerate one.
struct CelestialConfiguration
{
    double LatitudeRadians   = 0.0;                  // [rad] observer latitude, + north
    double LongitudeRadians  = 0.0;                  // [rad] observer longitude, + east
    double ObliquityRadians  = 0.40910518;           // [rad] axial tilt — 23.4392811°, the source of seasons
    double DayLengthSeconds  = 86400.0;              // [s]   one solar rotation
    double YearLengthSeconds = 365.256363 * 86400.0; // [s]   one orbit; the sidereal year

    // Orbital shape. Eccentricity 0 gives a circular orbit and a sun that is never early or late; Earth's
    //    0.0167 is what makes solar noon wander by up to ±16 minutes across the year.
    double Eccentricity      = 0.0167086;            // [-]
    double PerihelionRadians = 1.79660147;           // [rad] longitude of perihelion

    // The moon. Its orbit is inclined to the ecliptic, which is why eclipses are rare rather than monthly.
    double LunarPeriodSeconds     = 27.321661 * 86400.0;  // [s]   sidereal month
    double LunarInclinationRadians= 0.08979719;           // [rad] 5.145°
    double LunarSynodicSeconds    = 29.530589 * 86400.0;  // [s]   new moon to new moon; drives the phase
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       SOLVER
//------------------------------------------------------------------------------------------------------------------------

class CelestialSolver
{
public:
    void AssignConfiguration(const CelestialConfiguration& Value) noexcept { Config = Value; }
    [[nodiscard]] const CelestialConfiguration& QueryConfiguration() const noexcept { return Config; }

    // ── The clock ────────────────────────────────────────────────────────────────────────────────────────────────
    // Advance is a convenience for the common case of a running world. Assign is the authoritative path: a
    //    networked client is TOLD the time rather than accumulating its own, so it cannot drift, and a scrubbing
    //    editor sets it directly. Rate 0 freezes the sky without disabling any of this — "optional orbits" is a
    //    rate, not a separate code path.
    void   Advance(double DeltaSeconds) noexcept { Elapsed += DeltaSeconds * Rate; }
    void   AssignTime(double Seconds) noexcept { Elapsed = Seconds; }
    void   AssignRate(double Multiplier) noexcept { Rate = Multiplier; }
    [[nodiscard]] double QueryTime() const noexcept { return Elapsed; }
    [[nodiscard]] double QueryRate() const noexcept { return Rate; }

    // ── Derived sky ──────────────────────────────────────────────────────────────────────────────────────────────
    // All pure functions of the time argument. The no-argument forms use the clock; the explicit forms exist so a
    //    caller can ask about any instant without disturbing it — which is what a time-of-day preview needs.
    [[nodiscard]] HorizonDirection QuerySunDirection(double Seconds) const noexcept;

    // The sun's right ascension, exposed because the clock's alignment is defined in terms of it: a day
    //    fraction of 0.5 is solar noon, and that is only true if the world's rotation starts from the sun's
    //    own position at epoch rather than from an arbitrary zero.
    [[nodiscard]] double QuerySolarRightAscension(double Seconds) const noexcept;

private:
    // The constant that ties the world's rotation to the sun, so that a day fraction of 0.5 is solar noon.
    [[nodiscard]] double SolarNoonPhase() const noexcept;
public:
    [[nodiscard]] HorizonDirection QuerySunDirection() const noexcept { return QuerySunDirection(Elapsed); }

    [[nodiscard]] HorizonDirection QueryMoonDirection(double Seconds) const noexcept;
    [[nodiscard]] HorizonDirection QueryMoonDirection() const noexcept { return QueryMoonDirection(Elapsed); }

    // Illuminated fraction, 0 at new moon and 1 at full. Derived from the sun–moon elongation, so it stays
    //    consistent with the two directions above rather than being an independent counter that could disagree.
    [[nodiscard]] double QueryMoonPhase(double Seconds) const noexcept;
    [[nodiscard]] double QueryMoonPhase() const noexcept { return QueryMoonPhase(Elapsed); }

    // The angle the star field has turned through, for rotating a star cube map.
    [[nodiscard]] double QuerySiderealAngle(double Seconds) const noexcept;

    // Solar declination — the sun's angle above the celestial equator. ±obliquity at the solstices, zero at the
    //    equinoxes. Exposed because it is the one quantity with textbook values to check against.
    [[nodiscard]] double QuerySolarDeclination(double Seconds) const noexcept;

    // Local solar time as a fraction of a day, 0.5 being solar noon. Differs from clock time by the equation of
    //    time, which is why a sundial and a watch disagree by up to a quarter of an hour.
    [[nodiscard]] double QuerySolarDayFraction(double Seconds) const noexcept;

    // Kepler's equation M = E − e·sin E, inverted by Newton iteration. Exposed for the proof: it is the only
    //    part of this file that is not a direct formula, so it is the only part that can fail to converge.
    [[nodiscard]] static double SolveEccentricAnomaly(double MeanAnomaly, double Eccentricity) noexcept;

private:
    [[nodiscard]] HorizonDirection ToHorizon(double RightAscension, double Declination, double Seconds) const noexcept;

    CelestialConfiguration Config{};
    double                 Elapsed = 0.0;   // [s] the authoritative value; everything else is derived
    double                 Rate    = 1.0;   // [-] 0 freezes the sky, 60 runs a day in 24 minutes
};

} // namespace Frontier
