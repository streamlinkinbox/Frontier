/*==========================================================================================================================================
                                                      GASPARAMETERCATALOGUE.CPP
==========================================================================================================================================*/
// 🧩 See GasParameterCatalogue.h.

#include "GasParameterCatalogue.h"

#include <algorithm>
#include <cmath>

namespace Frontier::FluidDynamics
{

namespace
{

constexpr double PressureSweepsDefault = 16.0;

constexpr std::array<GasParameterEntry, GasParameterCount> Catalogue = {{
    // source — multipliers on the tyre model's own rates, which stay the physical port
    {"Emission",         &GasParameters::Emission,         1.0,  0.0,  4.0,  0.05, "×",      "Emission rate",         GasSection::Source},
    {"Heat",             &GasParameters::Heat,             1.0,  0.0,  4.0,  0.05, "×",      "Temperature",           GasSection::Source},
    {"Expansion",        &GasParameters::Expansion,        1.0,  0.0,  3.0,  0.05, "×",      "Volume expansion",      GasSection::Source},
    {"Soot",             &GasParameters::Soot,             1.0,  0.0,  4.0,  0.05, "×",      "Soot yield",            GasSection::Source},
    {"Momentum",         &GasParameters::Momentum,         9.0,  0.0,  24.0, 0.5,  "1/s",    "Carrier drag",          GasSection::Source},
    {"TreadEjection",    &GasParameters::TreadEjection,    1.0,  0.0,  3.0,  0.05, "×",      "Tread ejection",        GasSection::Source},
    // simulation
    {"Buoyancy",         &GasParameters::Buoyancy,         2.4,  0.0,  8.0,  0.1,  "m/s²·K", "Buoyancy",             GasSection::Simulation},
    {"Dissipation",      &GasParameters::Dissipation,      0.10, 0.0,  0.9,  0.01, "1/s",    "Dissipation",           GasSection::Simulation},
    {"Cooling",          &GasParameters::Cooling,          0.55, 0.0,  0.95, 0.01, "1/s",    "Cooling",               GasSection::Simulation},
    {"Vorticity",        &GasParameters::Vorticity,        3.0,  0.0,  6.0,  0.1,  "ε",      "Vorticity confinement", GasSection::Simulation},
    {"Turbulence",       &GasParameters::Turbulence,       0.60, 0.0,  2.0,  0.02, "m/s",    "Turbulence",            GasSection::Simulation},
    {"TurbulenceScale",  &GasParameters::TurbulenceScale,  3.7,  0.2,  4.0,  0.05, "1/m",    "Turbulence frequency",  GasSection::Simulation},
    {"WindSpeed",        &GasParameters::WindSpeed,        0.8,  0.0,  12.0, 0.1,  "m/s",    "Wind speed",            GasSection::Simulation},
    {"WindHeading",      &GasParameters::WindHeading,      30.0, 0.0,  360.0, 1.0, "°",      "Wind heading",          GasSection::Simulation},
    {"TimeScale",        &GasParameters::TimeScale,        1.0,  0.25, 2.0,  0.05, "×",      "Time scale",            GasSection::Simulation},
    {"PressureSweeps",   &GasParameters::PressureSweeps,   PressureSweepsDefault, 4.0, 40.0, 2.0, "", "Pressure iterations", GasSection::Simulation},
    // appearance
    {"Density",          &GasParameters::Density,          1.0,  0.2,  3.0,  0.05, "×",      "Density",               GasSection::Appearance},
    {"Albedo",           &GasParameters::Albedo,           0.88, 0.2,  0.98, 0.01, "",       "Albedo",                GasSection::Appearance},
    {"Phase",            &GasParameters::Phase,            0.38, 0.0,  0.9,  0.01, "g",      "Forward scattering",    GasSection::Appearance},
    {"MultiScatter",     &GasParameters::MultiScatter,     0.75, 0.0,  1.5,  0.05, "",       "Multiple scattering",   GasSection::Appearance},
    {"Ambient",          &GasParameters::Ambient,          1.0,  0.0,  3.0,  0.05, "×",      "Ambient light",         GasSection::Appearance},
    {"Detail",           &GasParameters::Detail,           0.70, 0.0,  1.0,  0.02, "",       "Detail",                GasSection::Appearance},
    {"DetailScale",      &GasParameters::DetailScale,      2.2,  0.2,  4.0,  0.05, "1/m",    "Detail frequency",      GasSection::Appearance},
    {"Erosion",          &GasParameters::Erosion,          1.0,  0.0,  1.5,  0.05, "",       "Edge erosion",          GasSection::Appearance},
    {"Warp",             &GasParameters::Warp,             0.0,  0.0,  1.0,  0.05, "",       "Billow warp",           GasSection::Appearance},
    {"Powder",           &GasParameters::Powder,           0.0,  0.0,  1.0,  0.05, "",       "Powder",                GasSection::Appearance},
    // fire
    {"FlameIntensity",   &GasParameters::FlameIntensity,   1.0,  0.0,  4.0,  0.05, "×",      "Flame intensity",       GasSection::Fire},
    {"FlameTemperature", &GasParameters::FlameTemperature, 1.0,  0.7,  1.6,  0.01, "×",      "Flame temperature",     GasSection::Fire},
    {"FlameCore",        &GasParameters::FlameCore,        1800.0, 1300.0, 2400.0, 10.0, "K", "Flame core",           GasSection::Fire},
    {"FlameTip",         &GasParameters::FlameTip,         1150.0, 900.0,  1600.0, 10.0, "K", "Flame tip",            GasSection::Fire},
    {"FlameFlicker",     &GasParameters::FlameFlicker,     0.6,  0.0,  1.0,  0.02, "",       "Flicker",               GasSection::Fire},
    {"FlameLoss",        &GasParameters::FlameLoss,        0.96, 0.5,  0.995, 0.005, "1/s",  "Flame decay",           GasSection::Fire},
    {"FireLight",        &GasParameters::FireLight,        1.0,  0.0,  4.0,  0.05, "×",      "Fire light",            GasSection::Fire}
}};

// 📝 Look presets for the gas: each a partial override of the catalogue defaults.
constexpr GasPresetValue ThickBurnout[]  = {{"Emission", 2.2}, {"Density", 1.6}, {"Dissipation", 0.12}, {"Albedo", 0.9}, {"Vorticity", 2.2},
                                            {"Expansion", 1.4}, {"MultiScatter", 0.9}};
constexpr GasPresetValue LightHaze[]     = {{"Emission", 0.6}, {"Density", 0.55}, {"Dissipation", 0.35}, {"Detail", 0.75}, {"Erosion", 0.9},
                                            {"Albedo", 0.92}};
constexpr GasPresetValue Inferno[]       = {{"FlameIntensity", 2.4}, {"FlameTemperature", 1.18}, {"FlameFlicker", 0.85}, {"Soot", 2.2}, {"Heat", 1.8},
                                            {"FireLight", 2.2}, {"Buoyancy", 3.4}};
constexpr GasPresetValue Crosswind[]     = {{"WindSpeed", 5.5}, {"WindHeading", 95.0}, {"Turbulence", 0.7}, {"Dissipation", 0.28}};
// 📝 The look before the reference pass: big warped billows, softer and cloudier.
constexpr GasPresetValue LegacyBillows[] = {{"Density", 1.0}, {"Dissipation", 0.22}, {"Vorticity", 1.6}, {"Turbulence", 0.4}, {"TurbulenceScale", 1.1},
                                            {"Albedo", 0.86}, {"Phase", 0.45}, {"MultiScatter", 0.45}, {"Detail", 0.65}, {"DetailScale", 0.85},
                                            {"Erosion", 0.75}, {"Warp", 0.35}, {"Powder", 0.2}, {"WindSpeed", 0.0}};

constexpr std::array<GasPreset, GasPresetCount> Presets = {{
    {"Tyre smoke",     nullptr,        0},
    {"Thick burnout",  ThickBurnout,   std::size(ThickBurnout)},
    {"Light haze",     LightHaze,      std::size(LightHaze)},
    {"Inferno",        Inferno,        std::size(Inferno)},
    {"Crosswind",      Crosswind,      std::size(Crosswind)},
    {"Legacy billows", LegacyBillows,  std::size(LegacyBillows)}
}};

constexpr std::array<PlumeGrade, 4> Grades = {{
    {"Low",    {64, 64, 20},   0.40, 8,  32},
    {"Medium", {96, 96, 24},   0.30, 12, 40},
    {"High",   {128, 128, 32}, 0.25, 16, 48},
    {"Ultra",  {160, 160, 48}, 0.20, 20, 64}
}};

} // namespace

const std::array<GasParameterEntry, GasParameterCount>& QueryGasParameterCatalogue() noexcept
{
    return Catalogue;
}

const GasParameterEntry* FindGasParameter(std::string_view Key) noexcept
{
    for (const GasParameterEntry& Entry : Catalogue)
    {
        if (Entry.Key == Key)
        {
            return &Entry;
        }
    }
    return nullptr;
}

GasParameters ConstructGasParameters() noexcept
{
    GasParameters Gas;
    for (const GasParameterEntry& Entry : Catalogue)
    {
        Gas.*(Entry.Field) = Entry.Default;
    }
    return Gas;
}

const std::array<GasPreset, GasPresetCount>& QueryGasPresets() noexcept
{
    return Presets;
}

GasParameters ApplyGasPreset(const GasPreset& Preset) noexcept
{
    GasParameters Gas = ConstructGasParameters();
    for (size_t Index = 0; Index < Preset.Count; ++Index)
    {
        if (const GasParameterEntry* Entry = FindGasParameter(Preset.Values[Index].Key))
        {
            Gas.*(Entry->Field) = Preset.Values[Index].Value;
        }
    }
    return Gas;
}

const std::array<PlumeGrade, 4>& QueryPlumeGrades() noexcept
{
    return Grades;
}

PlumeSettings MapPlumeSettings(const GasParameters& Gas, bool Enabled) noexcept
{
    PlumeSettings Settings;   // ...PlumeDefaults
    Settings.PlumeEnabled         = Enabled;
    Settings.PlumeEmissionRate    = Gas.Emission;
    Settings.PlumeDensity         = 1.2 * Gas.Density;
    Settings.PlumeHeat            = Gas.Heat;
    Settings.PlumeSoot            = Gas.Soot;
    Settings.PlumeBuoyancy        = Gas.Buoyancy;
    Settings.PlumeDissipation     = Gas.Dissipation;
    Settings.PlumeCooling         = Gas.Cooling;
    Settings.PlumeVorticity       = Gas.Vorticity;
    Settings.PlumeTurbulence      = Gas.Turbulence;
    Settings.PlumeTurbulenceScale = Gas.TurbulenceScale;
    Settings.PlumeWindSpeed       = Gas.WindSpeed;
    Settings.PlumeWindDirection   = Gas.WindHeading;
    Settings.PlumeAlbedo          = Gas.Albedo;
    Settings.PlumeAnisotropy      = Gas.Phase;
    Settings.PlumeScattering      = Gas.MultiScatter;
    Settings.PlumeAmbient         = Gas.Ambient;
    Settings.PlumeDetail          = Gas.Detail;
    Settings.PlumeDetailScale     = Gas.DetailScale;
    Settings.PlumeFire            = Gas.FlameIntensity;
    Settings.PlumeFlameCore       = Gas.FlameCore * Gas.FlameTemperature;
    Settings.PlumeFlameTip        = Gas.FlameTip * Gas.FlameTemperature;
    Settings.PlumeFireLight       = Gas.FireLight;
    Settings.PlumeFlameDecay      = -std::log(std::max(0.005, 1.0 - Gas.FlameLoss));
    return Settings;
}

} // namespace Frontier::FluidDynamics
