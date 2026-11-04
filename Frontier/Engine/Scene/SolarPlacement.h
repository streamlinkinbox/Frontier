/*==========================================================================================================================================
                                                          SOLARPLACEMENT.H
==========================================================================================================================================*/
// 🧩 The question the Sun page and the project's day clock ask the celestial solver: where the sun stands for a calendar
//    date, a place and a local solar time. The project registers the solver (the GlobalIllumination port's SolveCelestial)
//    so the Scene and Interface libraries never link the renderer.

#pragma once

#include <string>

namespace Frontier
{

struct SolarPlacementQuery
{
    std::string CalendarDate     = "2026-10-20";   // [-] YYYY-MM-DD (the demo's SkyDate)
    double      LatitudeDegrees  = -26.18;         // [°]
    double      LongitudeDegrees = 28.32;          // [°]
    double      UtcOffsetHours   = 2.0;            // [h] (the demo's TimeZone)
    double      LocalSolarHours  = 12.0;           // [h] (the demo's SunTime)
};

struct SolarPlacement
{
    double ElevationDegrees = 0.0;   // [°]
    double AzimuthDegrees   = 0.0;   // [°] counter-clockwise from +X east
};

using SolarPlacementSolver = SolarPlacement (*)(const SolarPlacementQuery& Query);

} // namespace Frontier
