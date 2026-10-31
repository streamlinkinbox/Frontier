/*====================================================================================================================================
                                                    ENVIRONMENTRECORDS.H
====================================================================================================================================*/
// 🧩 Scene records for the ported environment: Sun, atmosphere, clouds, fog and wind settings.

#pragma once

#include "RecordEntry.h"

#include "Engine/Rendering/AtmosphereModel.h"
#include "Engine/Rendering/CloudField.h"
#include "Engine/Rendering/FogMedium.h"

#include <array>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 VehicleHMI SunInspectorDashboard authoring state in its existing units. Manual azimuth is
//    counterclockwise from +X east (the demo's SunVector convention); automatic placement delegates
//    to CelestialModel, whose local output is X east, Y north, Z up.
struct SunPlacement
{
    double ElevationDegrees = 38.0;   // [deg] - Above the horizon; negative is below it
    double AzimuthDegrees   = 145.0;  // [deg] - Counterclockwise from +X east; wrapped to [-180, 180]
    double SolarIlluminance = 12.0;   // [-] - Exhibit illumination scale, not photometric lux
    double EmissionScale    = 1.0;    // [×] - VehicleHMI lighting-preset emitter scale
    double TemperatureKelvin = 5778.0; // [K] - Source blackbody temperature
    double RadiusDegrees     = 0.27;   // [deg] - Apparent angular radius; dashboard displays diameter
    double Glow              = 1.0;   // [x] - Optical aureole multiplier
    double LocalSolarTime    = 12.0;  // [h] - Local solar control time
    double DayDurationHours  = 6.0;   // [h] - Full day-cycle duration
    double LatitudeDegrees   = -26.18; // [deg] - Solar solver observer latitude
    double LongitudeDegrees  = 28.32;  // [deg] - Solar solver observer longitude
    double TimeZoneHours     = 2.0;   // [h] - Local UTC offset
    std::array<double, 3> Tint{1.0, 1.0, 1.0}; // [linear RGB] - Custom source tint
    int32_t ColourSource     = 0;     // [-] - 0 = temperature, 1 = RGB tint
    int32_t LightingPreset   = 0;     // [-] - 0 = custom, 1 = noon, 2 = golden, 3 = dusk, 4 = night
    int32_t SkyYear          = 2026;  // [year] - Civil date used by the celestial solver
    int32_t SkyMonth         = 10;    // [month]
    int32_t SkyDay           = 20;    // [day]
    bool    Enabled          = true;  // [-] - Sunlight quick toggle
    bool    DiscEnabled      = true;  // [-] - Finite solar disc quick toggle
    bool    AutoPosition     = false; // [-] - Link direction to date, time and location
    bool    DayAnimate       = false; // [-] - Authoring day-cycle toggle
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     ENVIRONMENT RECORDS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Each record's constructor builds its own Outliner/Inspector description in editor builds and
//    registers it through RecordEntry::RegisterEditorDescription. Outside the editor profile the
//    records carry only their renderer settings: no description, no field pointers, no UI types.
class SunRecord final : public RecordEntry
{
  public:
    SunRecord();

    SunPlacement Placement;   // [-] - Live inspector-bound settings

    [[nodiscard]] SpatialVector QuerySunDirection() const noexcept;
};

class AtmosphereRecord final : public RecordEntry
{
  public:
    AtmosphereRecord();

    AtmosphereParameters Parameters;   // [-] - Live inspector-bound settings
    bool                 Enabled = true;   // [-] - Atmosphere row toggle
};

class CloudRecord final : public RecordEntry
{
  public:
    CloudRecord();

    CloudParameters Parameters;   // [-] - Live inspector-bound settings
    bool            Enabled = true;   // [-] - Cloud row toggle
};

class FogRecord final : public RecordEntry
{
  public:
    FogRecord();

    FogParameters Parameters;          // [-] - Live inspector-bound settings
    double        ScatteringAlbedo = 0.5;   // [-] - HTML FogScatter default at 6afb6f0
    bool          Enabled = false;          // [-] - HTML FogEnabled default at 6afb6f0
};

// 📝 Wind is the HTML Forcefield entry: one shared weather field that drives cloud advection.
class WindRecord final : public RecordEntry
{
  public:
    WindRecord();

    double SpeedMps   = 7.0;    // [m/s] - HTML CloudWindSpeed default at 6afb6f0
    double BearingDegrees = 90.0;   // [deg] - HTML CloudWindBearing default at 6afb6f0
    double Gust       = 0.25;   // [-] - HTML WindGust default at 6afb6f0
    bool   Enabled    = true;   // [-] - Wind row toggle
};

} // namespace Frontier
