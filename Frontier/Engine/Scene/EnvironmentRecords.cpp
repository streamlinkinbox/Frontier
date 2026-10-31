// 🧩 Scene records for the ported environment: Sun, atmosphere, clouds, fog and wind settings.

#include "Engine/Scene/EnvironmentRecords.h"

#include "Engine/Rendering/CelestialModel.h"
#if defined(FRONTIER_WITH_INTERFACE_SDK)
    #include "Engine/Interface/SunInspectorDashboard.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace Frontier
{

namespace
{
constexpr double DegreesToRadians = 3.14159265358979323846 / 180.0;   // [rad/deg]
} // namespace

SunRecord::SunRecord() : RecordEntry(RecordClassification::Environment)
{
#if defined(FRONTIER_EDITOR_PROFILE)
    RecordEditorDescription Description;
    Description.StableId    = "sun";
    Description.Group       = "Environment";
    Description.DisplayName = "Sun";
    Description.Category    = "Directional light";
    Description.Glyph       = "NativeSun";
    Description.Colour      = "#e8c27a";
    Description.Description = "Light, direction & atmosphere";
    Description.Enabled     = &Placement.Enabled;
    Description.QueryLiveMeta = [this] {
        const double Elevation = std::asin(std::clamp(QuerySunDirection().Z, -1.0, 1.0)) / DegreesToRadians;
        char Value[24] = {};
        std::snprintf(Value, sizeof(Value), "%.0f°", Elevation);
        return std::string(Value);
    };
    Description.QueryLiveState = [this] {
        if (!Placement.Enabled) return std::string{};
        return std::string(QuerySunDirection().Z > 0.0 ? "live" : "warm");
    };
    Description.ScalarFields = {
        {"Elevation", "deg", -90.0, 90.0, 0.1, 1, &Placement.ElevationDegrees},
        {"Azimuth", "deg", -180.0, 180.0, 0.1, 1, &Placement.AzimuthDegrees},
        {"Solar illuminance", "x", 0.0, 60.0, 0.1, 1, &Placement.SolarIlluminance}};
    Description.Switches = {{"Sun disc", &Placement.DiscEnabled}, {"Day cycle", &Placement.DayAnimate}};
#if defined(FRONTIER_WITH_INTERFACE_SDK)
    Description.ConstructInspector = [this] { ConstructSunInspectorDashboard(Placement); };
#endif
    RegisterEditorDescription(std::move(Description));
#endif
}

SpatialVector SunRecord::QuerySunDirection() const noexcept
{
    if (Placement.AutoPosition)
    {
        const double Julian = JulianDate(Placement.SkyYear, static_cast<unsigned>(Placement.SkyMonth),
                                         static_cast<unsigned>(Placement.SkyDay),
                                         Placement.LocalSolarTime - Placement.TimeZoneHours);
        const SolarPosition Position = SunPosition(Julian, Placement.LatitudeDegrees, Placement.LongitudeDegrees);
        return SunDirectionLocal(Position);
    }

    const double Elevation = Placement.ElevationDegrees * DegreesToRadians;   // [rad]
    const double Azimuth   = Placement.AzimuthDegrees * DegreesToRadians;     // [rad]
    const double CosElevation = std::cos(Elevation);
    // VehicleHMI convention: azimuth is counterclockwise from +X east; local axes are X east, Y north, Z up.
    return {CosElevation * std::cos(Azimuth), CosElevation * std::sin(Azimuth), std::sin(Elevation)};
}

AtmosphereRecord::AtmosphereRecord() : RecordEntry(RecordClassification::Environment)
{
#if defined(FRONTIER_EDITOR_PROFILE)
    RecordEditorDescription Description;
    Description.StableId    = "atmosphere";
    Description.Group       = "Environment";
    Description.DisplayName = "Atmosphere";
    Description.Category    = "Environment";
    Description.Glyph       = "NativeAtmosphere";
    Description.Colour      = "#99aafa";
    Description.Description = "Rayleigh, Mie and ozone column";
    Description.Enabled     = &Enabled;
    Description.OutlinerToggle = &Enabled;
    Description.QueryLiveState = [this] { return Enabled ? "live" : ""; };
    Description.ScalarFields = {
        {"Rayleigh R", "1/km", 0.0, 0.05, 0.0001, 4, &Parameters.RayleighScattering.R, "Rayleigh"},
        {"Rayleigh G", "1/km", 0.0, 0.05, 0.0001, 4, &Parameters.RayleighScattering.G, "Rayleigh"},
        {"Rayleigh B", "1/km", 0.0, 0.05, 0.0001, 4, &Parameters.RayleighScattering.B, "Rayleigh"},
        {"Scale height", "km", 1.0, 20.0, 0.1, 1, &Parameters.RayleighScaleHeightKm, "Rayleigh"},
        {"Scattering", "1/km", 0.0, 0.02, 0.0001, 4, &Parameters.MieScattering, "Mie aerosols"},
        {"Absorption", "1/km", 0.0, 0.02, 0.0001, 4, &Parameters.MieAbsorption, "Mie aerosols"},
        {"Scale height", "km", 0.1, 5.0, 0.05, 2, &Parameters.MieScaleHeightKm, "Mie aerosols"},
        {"Phase g", "-", -0.95, 0.95, 0.01, 2, &Parameters.MiePhaseG, "Mie aerosols"},
        {"Ozone R", "1/km", 0.0, 0.01, 0.00001, 5, &Parameters.OzoneAbsorption.R, "Ozone layer"},
        {"Ozone G", "1/km", 0.0, 0.01, 0.00001, 5, &Parameters.OzoneAbsorption.G, "Ozone layer"},
        {"Ozone B", "1/km", 0.0, 0.01, 0.00001, 5, &Parameters.OzoneAbsorption.B, "Ozone layer"},
        {"Peak altitude", "km", 10.0, 50.0, 0.1, 1, &Parameters.OzonePeakKm, "Ozone layer"},
        {"Layer width", "km", 1.0, 30.0, 0.1, 1, &Parameters.OzoneWidthKm, "Ozone layer"},
        {"Ground albedo", "-", 0.0, 1.0, 0.01, 2, &Parameters.GroundAlbedo, "Surface response"}};
    RegisterEditorDescription(std::move(Description));
#endif
}

CloudRecord::CloudRecord() : RecordEntry(RecordClassification::Environment)
{
#if defined(FRONTIER_EDITOR_PROFILE)
    RecordEditorDescription Description;
    Description.StableId    = "clouds";
    Description.Group       = "Environment";
    Description.DisplayName = "Clouds";
    Description.Category    = "Environment";
    Description.Glyph       = "NativeClouds";
    Description.Colour      = "#99aafa";
    Description.Description = "Seeded density slab, sun in-scattering and self-shadowing";
    Description.Enabled     = &Enabled;
    Description.OutlinerToggle = &Enabled;
    Description.QueryLiveState = [this] { return Enabled ? "live" : ""; };
    // 📝 VehicleHMI cloud-layer controls mapped to the existing slab, transport, and scattering settings.
    Description.ScalarFields = {
        {"Coverage", "-", 0.0, 1.0, 0.01, 2, &Parameters.Coverage, "Layer"},
        {"Density scale", "x", 0.0, 4.0, 0.01, 2, &Parameters.DensityScale, "Layer"},
        {"Base height", "m", 0.0, 20000.0, 10.0, 0, &Parameters.BaseHeightM, "Layer"},
        {"Top height", "m", 100.0, 30000.0, 10.0, 0, &Parameters.TopHeightM, "Layer"},
        {"Wind X", "m/s", -40.0, 40.0, 0.1, 1, &Parameters.WindXMps, "Advection"},
        {"Wind Y", "m/s", -40.0, 40.0, 0.1, 1, &Parameters.WindYMps, "Advection"},
        {"Wind time", "s", 0.0, 3600.0, 1.0, 0, &Parameters.WindTimeS, "Advection"},
        {"Extinction", "1/m", 0.0, 0.05, 0.0005, 4, &Parameters.ExtinctionPerM, "Optical response"},
        {"Anisotropy", "-", -0.8, 0.8, 0.01, 2, &Parameters.PhaseG, "Optical response"}};
    RegisterEditorDescription(std::move(Description));
#endif
}

FogRecord::FogRecord() : RecordEntry(RecordClassification::Environment)
{
#if defined(FRONTIER_EDITOR_PROFILE)
    RecordEditorDescription Description;
    Description.StableId    = "fog";
    Description.Group       = "Environment";
    Description.DisplayName = "Fog";
    Description.Category    = "Environment";
    Description.Glyph       = "NativeFog";
    Description.Colour      = "#99aafa";
    Description.Description = "Exponential height fog with cloud-shadowed single scattering";
    Description.Enabled     = &Enabled;
    Description.OutlinerToggle = &Enabled;
    Description.QueryLiveState = [this] { return Enabled ? "live" : ""; };
    Description.ScalarFields = {
        {"Density", "1/m", 0.0, 0.05, 0.0001, 4, &Parameters.DensityAtBase, "Medium"},
        {"Base height", "m", 0.0, 500.0, 1.0, 0, &Parameters.BaseHeightM, "Medium"},
        {"Falloff height", "m", 1.0, 500.0, 1.0, 0, &Parameters.FalloffHeightM, "Medium"},
        {"Scatter", "-", 0.0, 1.0, 0.01, 2, &ScatteringAlbedo, "Sun response"},
        {"Fog tint R", "linear", 0.0, 1.0, 0.01, 2, &Parameters.Colour.R, "Colour"},
        {"Fog tint G", "linear", 0.0, 1.0, 0.01, 2, &Parameters.Colour.G, "Colour"},
        {"Fog tint B", "linear", 0.0, 1.0, 0.01, 2, &Parameters.Colour.B, "Colour"}};
    RegisterEditorDescription(std::move(Description));
#endif
}

WindRecord::WindRecord() : RecordEntry(RecordClassification::Environment)
{
#if defined(FRONTIER_EDITOR_PROFILE)
    RecordEditorDescription Description;
    Description.StableId    = "wind";
    Description.Group       = "Forcefield";
    Description.DisplayName = "Wind";
    Description.Category    = "ForceField";
    Description.Glyph       = "Wind";
    Description.Colour      = "#a7f3d0";
    Description.Description = "Shared weather force field";
    Description.Enabled     = &Enabled;
    Description.OutlinerToggle = &Enabled;
    Description.QueryLiveState = [this] { return Enabled ? "live" : ""; };
    Description.ScalarFields = {
        {"Speed", "m/s", 0.0, 40.0, 0.1, 1, &SpeedMps, "Wind vector"},
        {"Bearing", "deg", 0.0, 360.0, 1.0, 0, &BearingDegrees, "Wind vector"},
        {"Gust", "-", 0.0, 1.0, 0.01, 2, &Gust, "Turbulence"}};
    RegisterEditorDescription(std::move(Description));
#endif
}

} // namespace Frontier
