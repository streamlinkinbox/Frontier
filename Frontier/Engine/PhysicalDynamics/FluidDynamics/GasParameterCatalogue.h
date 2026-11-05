/*==========================================================================================================================================
                                                      GASPARAMETERCATALOGUE.H
==========================================================================================================================================*/
// 🧩 Every quantity the Smoke & fire editor exposes — source, simulation, appearance and fire — with its default, range, step, unit and label;
//    the six look presets; the map from those quantities to the plume's transport and optics settings; the four quality grades. A port of the
//    HMI prototype's SmokeSequence.js (GasParameterCatalogue, MapPlumeSettings), InspectorPages.js (GasPresets) and PlumeSpace.js
//    (PlumeGrades, PlumeDefaults), value for value.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Frontier::FluidDynamics
{

// 📝 The editor's gas: one double per catalogue entry, in the catalogue's order.
struct GasParameters
{
    // source — multipliers on the tyre model's own rates, which stay the physical port
    double Emission         = 1.0;
    double Heat             = 1.0;
    double Expansion        = 1.0;
    double Soot             = 1.0;
    double Momentum         = 9.0;
    double TreadEjection    = 1.0;
    // simulation
    double Buoyancy         = 2.4;
    double Dissipation      = 0.10;
    double Cooling          = 0.55;
    double Vorticity        = 3.0;
    double Turbulence       = 0.60;
    double TurbulenceScale  = 3.7;
    double WindSpeed        = 0.8;
    double WindHeading      = 30.0;
    double TimeScale        = 1.0;
    double PressureSweeps   = 16.0;
    // appearance
    double Density          = 1.0;
    double Albedo           = 0.88;
    double Phase            = 0.38;
    double MultiScatter     = 0.75;
    double Ambient          = 1.0;
    double Detail           = 0.70;
    double DetailScale      = 2.2;
    double Erosion          = 1.0;
    double Warp             = 0.0;
    double Powder           = 0.0;
    // fire
    double FlameIntensity   = 1.0;
    double FlameTemperature = 1.0;
    double FlameCore        = 1800.0;
    double FlameTip         = 1150.0;
    double FlameFlicker     = 0.6;
    double FlameLoss        = 0.96;
    double FireLight        = 1.0;
};

enum class GasSection : uint8_t
{
    Source,
    Simulation,
    Appearance,
    Fire
};

struct GasParameterEntry
{
    std::string_view     Key;
    double GasParameters::* Field;
    double               Default;
    double               Minimum;
    double               Maximum;
    double               Step;
    std::string_view     Unit;
    std::string_view     Label;
    GasSection           Section;
};

inline constexpr size_t GasParameterCount = 33;

[[nodiscard]] const std::array<GasParameterEntry, GasParameterCount>& QueryGasParameterCatalogue() noexcept;
[[nodiscard]] const GasParameterEntry*                                FindGasParameter(std::string_view Key) noexcept;
[[nodiscard]] GasParameters                                           ConstructGasParameters() noexcept;

// 📝 A look preset: a partial override of the catalogue defaults. Applying one is Object.assign(Gas, ConstructGasParameters(), Values).
struct GasPresetValue
{
    std::string_view Key;
    double           Value;
};

struct GasPreset
{
    std::string_view            Name;
    const GasPresetValue*       Values;
    size_t                      Count;
};

inline constexpr size_t GasPresetCount = 6;

[[nodiscard]] const std::array<GasPreset, GasPresetCount>& QueryGasPresets() noexcept;
[[nodiscard]] GasParameters                                ApplyGasPreset(const GasPreset& Preset) noexcept;

// 📝 PlumeGrades: the smoke-and-fire quality slider, independent of the GI grade. Extent [fine cells, each even], CellExtent [m],
//    PressureSweeps (even), MarchSamples.
struct PlumeGrade
{
    std::string_view          Name;
    std::array<uint32_t, 3>   Extent;
    double                    CellExtent;      // [m]
    uint32_t                  PressureSweeps;  // [-]
    uint32_t                  MarchSamples;    // [-]
};

[[nodiscard]] const std::array<PlumeGrade, 4>& QueryPlumeGrades() noexcept;

// 📝 Every transport and optics reading, at the values the exhibit opens with (PlumeDefaults). Inspection is the host's debug channel; 10 is
//    the plume's own view.
struct PlumeSettings
{
    bool   PlumeEnabled         = true;
    uint32_t PlumeGrade         = 2;
    double PlumeEmissionRate    = 1.0;
    double PlumeDensity         = 1.2;
    double PlumeHeat            = 1.0;
    double PlumeBuoyancy        = 2.4;
    double PlumeExtinction      = 3.6;
    double PlumeAlbedo          = 0.88;
    double PlumeFire            = 1.0;
    double PlumeSoot            = 1.0;
    double PlumeSootAlbedo      = 0.05;
    double PlumeVorticity       = 3.0;
    double PlumeTurbulence      = 0.6;
    double PlumeTurbulenceScale = 3.7;
    double PlumeDissipation     = 0.10;
    double PlumeCooling         = 0.55;
    double PlumeFlameDecay      = 4.0;
    double PlumeWindSpeed       = 0.8;
    double PlumeWindDirection   = 30.0;
    double PlumeScattering      = 0.75;
    double PlumeAnisotropy      = 0.38;
    double PlumeAmbient         = 1.0;
    double PlumeFireLight       = 1.0;
    double PlumeDetail          = 0.7;
    double PlumeDetailScale     = 2.2;
    double PlumeFlameCore       = 1800.0;
    double PlumeFlameTip        = 1150.0;
    double PlumeEmbers          = 1.0;
    int32_t Inspection          = 0;
};

// 📝 MapPlumeSettings(Gas, Enabled).
[[nodiscard]] PlumeSettings MapPlumeSettings(const GasParameters& Gas, bool Enabled = true) noexcept;

} // namespace Frontier::FluidDynamics
