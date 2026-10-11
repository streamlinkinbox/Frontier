//==============================================================================================================================================
//                                                            FLUIDEDITORCONTROLS.H                                                             
//==============================================================================================================================================
// 📦 GENERATED. The Fluid app's 82 controls, 18 preset cards and 9 debug channels, for the native editor.
//
// 🔴 DO NOT EDIT. Tools/Build/GenerateFluidEditorControls.py writes this file from
//    Experimental/Fluid/src/SceneSpecification.js, and --check fails the build when the two
//    disagree. Limits, steps, labels and the order of the choice lists are authored in the browser,
//    against a live picture; the native page only draws them.
//
// The short Label is what the inspector prints beside the pill and the long Heading is the
//    specification's own name for the setting, kept because it is what the browser's tooltip and
//    its exported scene both carry.
//
// Source revision: 33c5f2e13dc3d6912a8bb2d887a4e8061b401b6b

#pragma once

#include "GasPresetLibrary.h"

#include <cstdint>
#include <cstring>

namespace Frontier::FluidEditor
{

//----------------------------------------------------------------------------------------------------------------------
//                                                     ONE CONTROL                                                      
//----------------------------------------------------------------------------------------------------------------------

enum class ControlKind : uint8_t
{
    Range  = 0,   // [-] - a number with a pill, a unit cell and a slider
    Toggle = 1,   // [-] - a switch
    Choice = 2,   // [-] - a select, whose options are listed in order
};

struct ControlChoice
{
    int         Ordinal = 0;    // [-] - the value stored for this option
    const char* Label   = "";   // [-] - what the select shows
};

struct ControlRow
{
    const char*          Key         = "";                   // [-] - the browser key; what a scene stores
    const char*          Label       = "";                   // [-] - the inspector's short name
    const char*          Heading     = "";                   // [-] - the specification's long name
    const char*          Unit        = "";                   // [-] - the pill's unit cell
    ControlKind          Kind        = ControlKind::Range;   // [-]
    float                Low         = 0.0f;                 // [-] - range only
    float                High        = 0.0f;                 // [-] - range only
    float                Step        = 0.0f;                 // [-] - range only
    const ControlChoice* Choices     = nullptr;              // [-] - choice only
    uint32_t             ChoiceCount = 0u;                   // [-]
};

inline constexpr ControlChoice GridResolutionChoices[] = {
    { 16, "16\xc2\xb3" },
    { 24, "24\xc2\xb3" },
    { 32, "32\xc2\xb3" },
    { 48, "48\xc2\xb3" },
    { 64, "64\xc2\xb3" },
    { 96, "96\xc2\xb3" },
    { 128, "128\xc2\xb3" },
};

inline constexpr ControlChoice AtlasMinimapFieldChoices[] = {
    { 0, "State Atlas" },
    { 1, "Velocity Atlas" },
    { 2, "Vorticity Curl Atlas" },
    { 3, "Pressure & 3D Irradiance Atlas" },
};

inline constexpr ControlChoice SliceAxisChoices[] = {
    { 0, "X-Axis Slice" },
    { 1, "Y-Axis Slice" },
    { 2, "Z-Axis Slice" },
};

inline constexpr ControlChoice RenderChannelChoices[] = {
    { 0, "Combined Lit" },
    { 1, "Voxelized DDA Cubes" },
    { 2, "Primary Density \xcf\x81" },
    { 3, "Temperature T" },
    { 4, "Fuel Core f" },
    { 5, "3D Velocity Vectors u" },
    { 6, "Vorticity / Curl Magnitude |\xe2\x88\x87\xc3\x97u|" },
    { 7, "Poisson Pressure & Irradiance p" },
    { 8, "3D Grid Cross-Section Slice Inspector" },
};

inline constexpr ControlChoice ColorPaletteChoices[] = {
    { 0, "Physical Planckian Blackbody" },
    { 1, "Cinema Napalm / Rich Hydrocarbon" },
    { 2, "Thermobaric White-Hot Ordnance" },
    { 3, "Wildfire / Copper Emerald Flame" },
    { 4, "Cherenkov Blue Plasma Core" },
    { 5, "Arcane Void Magenta / Corrupt Pyro" },
};

inline constexpr ControlChoice ObstacleTypeChoices[] = {
    { 0, "None" },
    { 1, "Moving / Interactive Sphere" },
    { 2, "Vertical Bridge Pier / Pillar" },
    { 3, "Horizontal cylinder" },
    { 4, "Deflector slab" },
    { 5, "Tyre ring" },
};

constexpr uint32_t FluidControlCount = 82u;

/// 📦 Every control the Fluid app offers, in the specification's own order.
/// out   const ControlRow*   [-]  FluidControlCount entries, never null
/// cost  ✔️
inline const ControlRow* FluidControls() noexcept
{
    static const ControlRow Rail[FluidControlCount] = {
        { "dynamicBounds", "Expand around bursts", "Dynamic Auto-Expanding Blast Bounds",
          "\xe2\x80\x94", ControlKind::Toggle, 0.0f, 0.0f, 0.0f, nullptr, 0u },
        { "boundsWidth", "Width / depth", "Base Bounds Width X / Z",
          "m", ControlKind::Range, 1.0f, 4.0f, 0.05f, nullptr, 0u },
        { "boundsHeight", "Height", "Base Bounds Height Y",
          "m", ControlKind::Range, 1.2f, 4.0f, 0.05f, nullptr, 0u },
        { "dynamicBoundsMax", "Expansion limit", "Dynamic Blast Surge Multiplier",
          "\xc3\x97", ControlKind::Range, 1.1f, 2.2f, 0.05f, nullptr, 0u },
        { "gridResolution", "Voxel resolution", "Voxel Domain Dimensions",
          "\xe2\x80\x94", ControlKind::Choice, 0.0f, 0.0f, 0.0f, GridResolutionChoices, 7u },
        { "voxelQuantization", "Voxel quantization", "Voxel Quantization",
          "\xe2\x80\x94", ControlKind::Range, 0.0f, 1.0f, 0.01f, nullptr, 0u },
        { "autoGpuGovernor", "Adaptive raymarch quality", "Auto Low-End GPU FPS Governor",
          "\xe2\x80\x94", ControlKind::Toggle, 0.0f, 0.0f, 0.0f, nullptr, 0u },
        { "showActiveVoxelCells", "Active voxel cells", "UE5 Sparse Voxel Octree Wireframe",
          "\xe2\x80\x94", ControlKind::Toggle, 0.0f, 0.0f, 0.0f, nullptr, 0u },
        { "showAtlasMinimap", "Atlas minimap", "Show Live 2D Tiled 3D Voxel Atlas PiP",
          "\xe2\x80\x94", ControlKind::Toggle, 0.0f, 0.0f, 0.0f, nullptr, 0u },
        { "atlasMinimapField", "Atlas field", "Voxel Atlas Minimap Channel",
          "\xe2\x80\x94", ControlKind::Choice, 0.0f, 0.0f, 0.0f, AtlasMinimapFieldChoices, 4u },
        { "pressureIterations", "Pressure iterations", "Jacobi Pressure + Irradiance Iterations",
          "\xe2\x80\x94", ControlKind::Range, 6.0f, 48.0f, 2.0f, nullptr, 0u },
        { "timeScale", "Time scale", "Simulation Time Scale",
          "\xc3\x97", ControlKind::Range, 0.1f, 2.0f, 0.05f, nullptr, 0u },
        { "macCormackAdvection", "MacCormack advection", "MacCormack / BFECC Sharp Advection",
          "\xe2\x80\x94", ControlKind::Toggle, 0.0f, 0.0f, 0.0f, nullptr, 0u },
        { "enclosedBox", "Closed domain walls", "Closed Top/Side Domain Walls",
          "\xe2\x80\x94", ControlKind::Toggle, 0.0f, 0.0f, 0.0f, nullptr, 0u },
        { "sliceAxis", "Slice axis", "Slice Plane Axis",
          "\xe2\x80\x94", ControlKind::Choice, 0.0f, 0.0f, 0.0f, SliceAxisChoices, 3u },
        { "slicePosition", "Slice depth", "Slice Plane Depth",
          "\xe2\x80\x94", ControlKind::Range, 0.05f, 0.95f, 0.01f, nullptr, 0u },
        { "vorticityConfinement", "Vorticity", "Vorticity Confinement",
          "\xe2\x80\x94", ControlKind::Range, 0.0f, 14.0f, 0.1f, nullptr, 0u },
        { "buoyancy", "Buoyancy", "Thermal Buoyancy Lift",
          "\xe2\x80\x94", ControlKind::Range, 0.0f, 12.0f, 0.1f, nullptr, 0u },
        { "smokeWeight", "Smoke weight", "Soot Weight / Downward Drag",
          "\xe2\x80\x94", ControlKind::Range, 0.0f, 3.5f, 0.05f, nullptr, 0u },
        { "turbulenceStrength", "Turbulence strength", "Sub-Grid Curl Noise Injection",
          "\xe2\x80\x94", ControlKind::Range, 0.0f, 8.0f, 0.1f, nullptr, 0u },
        { "turbulenceScale", "Turbulence scale", "Turbulence Noise Frequency",
          "\xe2\x80\x94", ControlKind::Range, 1.0f, 10.0f, 0.1f, nullptr, 0u },
        { "burnRate", "Burn rate", "Fuel Reaction / Burn Speed",
          "\xe2\x80\x94", ControlKind::Range, 0.2f, 4.5f, 0.05f, nullptr, 0u },
        { "burnHeat", "Heat release", "Exothermic Heat Release",
          "\xe2\x80\x94", ControlKind::Range, 0.5f, 5.0f, 0.05f, nullptr, 0u },
        { "sootGeneration", "Soot generation", "Combustion Soot / Smoke Yield",
          "\xe2\x80\x94", ControlKind::Range, 0.2f, 4.0f, 0.05f, nullptr, 0u },
        { "combustionExpansion", "Combustion expansion", "Gas Divergence Expansion",
          "\xe2\x80\x94", ControlKind::Range, 0.0f, 10.0f, 0.1f, nullptr, 0u },
        { "coolingRate", "Cooling", "Stefan-Boltzmann Radiative Cooling",
          "\xe2\x80\x94", ControlKind::Range, 0.2f, 3.5f, 0.05f, nullptr, 0u },
        { "smokeDissipation", "Smoke dissipation", "Smoke Dissipation Rate",
          "\xe2\x80\x94", ControlKind::Range, 0.02f, 1.2f, 0.02f, nullptr, 0u },
        { "windX", "Crosswind X", "Crosswind Force X",
          "\xe2\x80\x94", ControlKind::Range, -2.5f, 2.5f, 0.05f, nullptr, 0u },
        { "renderChannel", "Render channel", "Render Mode / Diagnostic Field",
          "\xe2\x80\x94", ControlKind::Choice, 0.0f, 0.0f, 0.0f, RenderChannelChoices, 9u },
        { "colorPalette", "Colour palette", "Blackbody Radiation Palette",
          "\xe2\x80\x94", ControlKind::Choice, 0.0f, 0.0f, 0.0f, ColorPaletteChoices, 6u },
        { "raymarchSteps", "Raymarch steps", "Primary Raymarch Steps",
          "\xe2\x80\x94", ControlKind::Range, 32.0f, 160.0f, 4.0f, nullptr, 0u },
        { "shadowSteps", "Shadow steps", "Self-Shadow Light March Steps",
          "\xe2\x80\x94", ControlKind::Range, 3.0f, 12.0f, 1.0f, nullptr, 0u },
        { "renderScale", "Viewport resolution scale", "Viewport Render Resolution Scale",
          "\xc3\x97", ControlKind::Range, 0.5f, 1.25f, 0.05f, nullptr, 0u },
        { "fireIntensity", "Fire intensity", "Blackbody Fire Core Intensity",
          "\xe2\x80\x94", ControlKind::Range, 0.5f, 12.0f, 0.1f, nullptr, 0u },
        { "bloomIntensity", "Bloom", "3D Volumetric Fire Bloom / Glare",
          "\xe2\x80\x94", ControlKind::Range, 0.0f, 2.0f, 0.05f, nullptr, 0u },
        { "godRaysIntensity", "Light shafts", "Crepuscular God-Ray Sun Shafts",
          "\xe2\x80\x94", ControlKind::Range, 0.0f, 1.5f, 0.05f, nullptr, 0u },
        { "shockwaveStrength", "Shockwave refraction", "Supersonic Blast Refraction Wave",
          "\xe2\x80\x94", ControlKind::Range, 0.0f, 2.0f, 0.05f, nullptr, 0u },
        { "temperatureScale", "Temperature scale", "Kelvin Temperature LUT Scale",
          "\xc3\x97", ControlKind::Range, 0.4f, 2.2f, 0.05f, nullptr, 0u },
        { "internalScattering", "Internal scattering", "3D Internal Fire \xe2\x86\x92 Smoke Glow",
          "\xe2\x80\x94", ControlKind::Range, 0.0f, 6.0f, 0.1f, nullptr, 0u },
        { "densityExtinction", "Smoke extinction", "Smoke Optical Extinction",
          "\xe2\x80\x94", ControlKind::Range, 4.0f, 40.0f, 0.5f, nullptr, 0u },
        { "smokeAlbedo", "Smoke albedo", "Smoke Scattering Albedo",
          "\xe2\x80\x94", ControlKind::Range, 0.04f, 0.9f, 0.01f, nullptr, 0u },
        { "shadowDensity", "Shadow density", "Self-Shadow Optical Thickness",
          "\xe2\x80\x94", ControlKind::Range, 2.0f, 25.0f, 0.5f, nullptr, 0u },
        { "phaseAnisotropy", "Phase anisotropy", "Henyey-Greenstein Silver-Lining",
          "\xe2\x80\x94", ControlKind::Range, -0.5f, 0.85f, 0.02f, nullptr, 0u },
        { "sunElevation", "Sun elevation", "Directional Sun Elevation",
          "\xc2\xb0", ControlKind::Range, 10.0f, 88.0f, 1.0f, nullptr, 0u },
        { "sunAzimuth", "Sun azimuth", "Directional Sun Azimuth",
          "\xc2\xb0", ControlKind::Range, 0.0f, 360.0f, 2.0f, nullptr, 0u },
        { "exposure", "Exposure", "ACES Filmic Exposure",
          "\xe2\x80\x94", ControlKind::Range, 0.5f, 2.5f, 0.05f, nullptr, 0u },
        { "emitterEnabled", "Continuous emission", "Enable Continuous Fuel/Fire Plume",
          "\xe2\x80\x94", ControlKind::Toggle, 0.0f, 0.0f, 0.0f, nullptr, 0u },
        { "emitterRate", "Emission rate", "Fuel & Heat Injection Rate",
          "\xc3\x97", ControlKind::Range, 0.1f, 2.5f, 0.05f, nullptr, 0u },
        { "emitterRadius", "Source radius", "Nozzle / Burner Radius",
          "m", ControlKind::Range, 0.05f, 0.25f, 0.005f, nullptr, 0u },
        { "emitterUpwardVelocity", "Upward velocity", "Jet Upward Velocity",
          "\xe2\x80\x94", ControlKind::Range, 0.5f, 8.0f, 0.1f, nullptr, 0u },
        { "emitterSwirl", "Swirl", "Vortex Swirl Impulse",
          "\xe2\x80\x94", ControlKind::Range, 0.0f, 8.0f, 0.1f, nullptr, 0u },
        { "shrapnelEnabled", "Shrapnel streamers", "Eject 3D Ballistic Shrapnel Streamers",
          "\xe2\x80\x94", ControlKind::Toggle, 0.0f, 0.0f, 0.0f, nullptr, 0u },
        { "blastStrength", "Shockwave velocity", "Radial Shockwave Velocity",
          "\xe2\x80\x94", ControlKind::Range, 2.0f, 18.0f, 0.25f, nullptr, 0u },
        { "blastRadius", "Burst radius", "Fireball Detonation Radius",
          "\xe2\x80\x94", ControlKind::Range, 0.1f, 0.35f, 0.01f, nullptr, 0u },
        { "blastTemperature", "Burst temperature", "Detonation Core Temperature",
          "\xe2\x80\x94", ControlKind::Range, 2.0f, 9.0f, 0.1f, nullptr, 0u },
        { "blastLobes", "Lobe frequency", "Cauliflower Lobe Frequency",
          "\xe2\x80\x94", ControlKind::Range, 2.0f, 12.0f, 0.5f, nullptr, 0u },
        { "obstacleType", "Collider shape", "Collider Geometry Shape",
          "\xe2\x80\x94", ControlKind::Choice, 0.0f, 0.0f, 0.0f, ObstacleTypeChoices, 6u },
        { "obstacleY", "Position Y", "Collider Altitude",
          "\xe2\x80\x94", ControlKind::Range, 0.2f, 0.78f, 0.01f, nullptr, 0u },
        { "obstacleX", "Position X", "Collider Horizontal Offset",
          "\xe2\x80\x94", ControlKind::Range, 0.2f, 0.8f, 0.01f, nullptr, 0u },
        { "obstacleZ", "Position Z", "Collider Horizontal Offset",
          "\xe2\x80\x94", ControlKind::Range, 0.2f, 0.8f, 0.01f, nullptr, 0u },
        { "colliderAutoMove", "Animate collider", "Animate collider",
          "\xe2\x80\x94", ControlKind::Toggle, 0.0f, 0.0f, 0.0f, nullptr, 0u },
        { "colliderSpeed", "Animation speed", "Moving Collider Speed",
          "\xe2\x80\x94", ControlKind::Range, 0.1f, 3.0f, 0.05f, nullptr, 0u },
        { "obstacleRadius", "Collider radius", "Collider Size / Radius",
          "m", ControlKind::Range, 0.08f, 0.28f, 0.005f, nullptr, 0u },
        { "showBoundingBox", "Domain bounds", "Show 3D Voxel Domain Wireframe",
          "\xe2\x80\x94", ControlKind::Toggle, 0.0f, 0.0f, 0.0f, nullptr, 0u },
        { "showVoxelGridLines", "Voxel grid lines", "Show Voxel Resolution Cell Subdivisions",
          "\xe2\x80\x94", ControlKind::Toggle, 0.0f, 0.0f, 0.0f, nullptr, 0u },
        { "showFloorGrid", "Studio floor", "Show Lit Studio Floor + Volumetric Shadows",
          "\xe2\x80\x94", ControlKind::Toggle, 0.0f, 0.0f, 0.0f, nullptr, 0u },
        { "showEmbers", "Embers / ash", "Show GPU-Advected Fireflies / Ash Motes",
          "\xe2\x80\x94", ControlKind::Toggle, 0.0f, 0.0f, 0.0f, nullptr, 0u },
        { "emberCount", "Particle count", "Firefly / Ash Mote Count",
          "\xe2\x80\x94", ControlKind::Range, 0.0f, 1000.0f, 10.0f, nullptr, 0u },
        { "emberSize", "Particle size", "Firefly / Ash Mote Size",
          "\xe2\x80\x94", ControlKind::Range, 0.35f, 2.5f, 0.05f, nullptr, 0u },
        { "emberIntensity", "Particle brightness", "Firefly Brightness / Ash Visibility",
          "\xe2\x80\x94", ControlKind::Range, 0.1f, 3.0f, 0.05f, nullptr, 0u },
        { "emberLifetime", "Particle lifetime", "Mote Lifetime / Drift Speed",
          "\xc3\x97", ControlKind::Range, 0.25f, 3.0f, 0.05f, nullptr, 0u },
        { "emberAshiness", "Ash blend", "Ashiness",
          "\xe2\x80\x94", ControlKind::Range, 0.0f, 1.0f, 0.01f, nullptr, 0u },
        { "emitterHeight", "Source height", "Source height",
          "m", ControlKind::Range, 0.01f, 1.5f, 0.01f, nullptr, 0u },
        { "emitterTemperature", "Temperature injection", "Temperature injection",
          "\xe2\x80\x94", ControlKind::Range, 0.0f, 9.0f, 0.1f, nullptr, 0u },
        { "emitterFuel", "Fuel injection", "Fuel injection",
          "\xe2\x80\x94", ControlKind::Range, 0.0f, 9.0f, 0.1f, nullptr, 0u },
        { "emitterSmoke", "Smoke injection", "Smoke injection",
          "\xe2\x80\x94", ControlKind::Range, 0.0f, 9.0f, 0.1f, nullptr, 0u },
        { "blastFuel", "Burst fuel", "Burst fuel",
          "\xe2\x80\x94", ControlKind::Range, 0.0f, 9.0f, 0.1f, nullptr, 0u },
        { "blastSmoke", "Burst smoke", "Burst smoke",
          "\xe2\x80\x94", ControlKind::Range, 0.0f, 9.0f, 0.1f, nullptr, 0u },
        { "windZ", "Crosswind Z", "Crosswind Z",
          "\xe2\x80\x94", ControlKind::Range, -2.5f, 2.5f, 0.05f, nullptr, 0u },
        { "velocityDamping", "Velocity damping", "Velocity damping",
          "\xe2\x80\x94", ControlKind::Range, 0.0f, 2.0f, 0.01f, nullptr, 0u },
        { "sunIntensity", "Sun intensity", "Sun intensity",
          "\xe2\x80\x94", ControlKind::Range, 0.0f, 8.0f, 0.1f, nullptr, 0u },
        { "ambientIntensity", "Ambient light", "Ambient light",
          "\xe2\x80\x94", ControlKind::Range, 0.0f, 3.0f, 0.05f, nullptr, 0u },
    };
    return Rail;
}

/// 📦 The control a key names.
/// in    Key    [-]  a browser key
/// out   const ControlRow*   [-]  null when nothing carries that key
/// cost  ✔️  linear over 82 entries
inline const ControlRow* ReadControl(const char* Key) noexcept
{
    if (!Key) return nullptr;
    const ControlRow* Rail = FluidControls();
    for (uint32_t Index = 0u; Index < FluidControlCount; ++Index)
        if (std::strcmp(Rail[Index].Key, Key) == 0) return &Rail[Index];
    return nullptr;
}

//----------------------------------------------------------------------------------------------------------------------
//                                                   THE PRESET RAIL                                                    
//----------------------------------------------------------------------------------------------------------------------
// PresetPresentation: the short name, the filter it answers to, the line beneath it and the glyph.
//    GasPresetLibrary.h carries the same 18 identities with their tuning; this carries how they read.

struct PresetCard
{
    const char* Identity = "";   // [-] - the key shared with GasPresetLibrary.h
    const char* Name     = "";   // [-] - the card's title
    const char* Category = "";   // [-] - all / fire / smoke / blast
    const char* Summary  = "";   // [-] - the line beneath the title
    const char* Glyph    = "";   // [-] - the swatch drawing
};

constexpr uint32_t FluidPresetCardCount = 18u;

/// 📦 The preset rail, in the order the browser lists it.
/// out   const PresetCard*   [-]  FluidPresetCardCount entries, never null
/// cost  ✔️
inline const PresetCard* FluidPresetCards() noexcept
{
    static const PresetCard Rail[FluidPresetCardCount] = {
        { "ue5_pyro_default", "Pyro plume",
          "fire", "Rolling flame \xc2\xb7 dark soot", "flame" },
        { "oil_well_inferno", "Oil well inferno",
          "fire", "Pressurised fuel column", "flame" },
        { "shrapnel_airburst", "Shrapnel airburst",
          "blast", "Ballistic smoke streamers", "burst" },
        { "megaton_open_bounds", "Wide-bounds blast",
          "blast", "Large expanding fireball", "burst" },
        { "tactical_ordnance", "Tactical detonation",
          "blast", "Flash core \xc2\xb7 dust cloud", "burst" },
        { "obstacle_deflection", "Smoke deflection",
          "smoke", "Flow around a collider", "smoke" },
        { "wildfire_tornado", "Wildfire tornado",
          "fire", "Spiralling emerald fire", "rotate" },
        { "ashfall_motes", "Ashfall",
          "smoke", "Drifting smoke & ash", "smoke" },
        { "voxelized_stylized", "Voxel fire",
          "fire", "Discrete volume shading", "box" },
        { "dust_tornado", "Dust tornado",
          "smoke", "Cold debris vortex", "rotate" },
        { "tyre_burnout", "Tyre burnout",
          "smoke", "Rubber smoke off a ring", "cylinder" },
        { "ledge_sandfall", "Ledge sandfall",
          "smoke", "Sand poured off a slab", "wall" },
        { "settling_dust", "Settling dust",
          "smoke", "Slow pale aftermath", "smoke" },
        { "small_gust", "Small gust",
          "smoke", "Thin haze on a crosswind", "orbit" },
        { "brick_fracture_dust", "Brick fracture",
          "blast", "Cold masonry burst", "burst" },
        { "lantern_flame", "Lantern flame",
          "fire", "One small steady wick", "flame" },
        { "camp_fire", "Camp fire",
          "fire", "Open wood fire with sparks", "flame" },
        { "low_gpu_performance", "Lightweight plume",
          "fire", "24\xc2\xb3 grid \xc2\xb7 lower GPU cost", "flame" },
    };
    return Rail;
}

//----------------------------------------------------------------------------------------------------------------------
//                                                  READING A SETTING                                                   
//----------------------------------------------------------------------------------------------------------------------
// The inspector paints whatever the host's GasSettings holds, so the page needs one bridge from a
//    browser key to the member that carries it. Both sides of that bridge are generated from the
//    same two browser files, which is the only reason it can be trusted to stay complete.

/// 📦 The value a control is currently showing.
/// in    Settings   [-]  the effect being edited
/// in    Key        [-]  a browser key
/// out   Reading    [-]  written only when the key is known; booleans read 0 or 1
/// out   bool       [-]  false when no member carries that key
/// cost  ✔️  linear over the 82 bridged keys
inline bool ReadSetting(const GasSettings& Settings, const char* Key, float& Reading) noexcept
{
    if (!Key) return false;
    if (std::strcmp(Key, "dynamicBounds") == 0) { Reading = Settings.DynamicBounds ? 1.0f : 0.0f; return true; }
    if (std::strcmp(Key, "boundsWidth") == 0) { Reading = float(Settings.BoundsWidth); return true; }
    if (std::strcmp(Key, "boundsHeight") == 0) { Reading = float(Settings.BoundsHeight); return true; }
    if (std::strcmp(Key, "dynamicBoundsMax") == 0) { Reading = float(Settings.DynamicBoundsMax); return true; }
    if (std::strcmp(Key, "gridResolution") == 0) { Reading = float(Settings.LatticeResolution); return true; }
    if (std::strcmp(Key, "voxelQuantization") == 0) { Reading = float(Settings.VoxelQuantization); return true; }
    if (std::strcmp(Key, "autoGpuGovernor") == 0) { Reading = Settings.AutoGpuGovernor ? 1.0f : 0.0f; return true; }
    if (std::strcmp(Key, "showActiveVoxelCells") == 0) { Reading = Settings.ActiveVoxelCellsShown ? 1.0f : 0.0f; return true; }
    if (std::strcmp(Key, "showAtlasMinimap") == 0) { Reading = Settings.TileSheetOverviewShown ? 1.0f : 0.0f; return true; }
    if (std::strcmp(Key, "atlasMinimapField") == 0) { Reading = float(Settings.TileSheetOverviewReading); return true; }
    if (std::strcmp(Key, "pressureIterations") == 0) { Reading = float(Settings.PressureIterations); return true; }
    if (std::strcmp(Key, "timeScale") == 0) { Reading = float(Settings.TimeScale); return true; }
    if (std::strcmp(Key, "macCormackAdvection") == 0) { Reading = Settings.MacCormackAdvection ? 1.0f : 0.0f; return true; }
    if (std::strcmp(Key, "enclosedBox") == 0) { Reading = Settings.EnclosedBox ? 1.0f : 0.0f; return true; }
    if (std::strcmp(Key, "sliceAxis") == 0) { Reading = float(Settings.SliceAxis); return true; }
    if (std::strcmp(Key, "slicePosition") == 0) { Reading = float(Settings.SlicePosition); return true; }
    if (std::strcmp(Key, "vorticityConfinement") == 0) { Reading = float(Settings.VorticityConfinement); return true; }
    if (std::strcmp(Key, "buoyancy") == 0) { Reading = float(Settings.Buoyancy); return true; }
    if (std::strcmp(Key, "smokeWeight") == 0) { Reading = float(Settings.SmokeWeight); return true; }
    if (std::strcmp(Key, "turbulenceStrength") == 0) { Reading = float(Settings.TurbulenceStrength); return true; }
    if (std::strcmp(Key, "turbulenceScale") == 0) { Reading = float(Settings.TurbulenceScale); return true; }
    if (std::strcmp(Key, "burnRate") == 0) { Reading = float(Settings.BurnRate); return true; }
    if (std::strcmp(Key, "burnHeat") == 0) { Reading = float(Settings.BurnHeat); return true; }
    if (std::strcmp(Key, "sootGeneration") == 0) { Reading = float(Settings.SootGeneration); return true; }
    if (std::strcmp(Key, "combustionExpansion") == 0) { Reading = float(Settings.CombustionExpansion); return true; }
    if (std::strcmp(Key, "coolingRate") == 0) { Reading = float(Settings.CoolingRate); return true; }
    if (std::strcmp(Key, "smokeDissipation") == 0) { Reading = float(Settings.SmokeDissipation); return true; }
    if (std::strcmp(Key, "windX") == 0) { Reading = float(Settings.WindX); return true; }
    if (std::strcmp(Key, "renderChannel") == 0) { Reading = float(Settings.RenderChannel); return true; }
    if (std::strcmp(Key, "colorPalette") == 0) { Reading = float(Settings.ColorPalette); return true; }
    if (std::strcmp(Key, "raymarchSteps") == 0) { Reading = float(Settings.RaymarchSteps); return true; }
    if (std::strcmp(Key, "shadowSteps") == 0) { Reading = float(Settings.ShadowSteps); return true; }
    if (std::strcmp(Key, "renderScale") == 0) { Reading = float(Settings.RenderScale); return true; }
    if (std::strcmp(Key, "fireIntensity") == 0) { Reading = float(Settings.FireIntensity); return true; }
    if (std::strcmp(Key, "bloomIntensity") == 0) { Reading = float(Settings.BloomIntensity); return true; }
    if (std::strcmp(Key, "godRaysIntensity") == 0) { Reading = float(Settings.GodRaysIntensity); return true; }
    if (std::strcmp(Key, "shockwaveStrength") == 0) { Reading = float(Settings.ShockwaveStrength); return true; }
    if (std::strcmp(Key, "temperatureScale") == 0) { Reading = float(Settings.TemperatureScale); return true; }
    if (std::strcmp(Key, "internalScattering") == 0) { Reading = float(Settings.InternalScattering); return true; }
    if (std::strcmp(Key, "densityExtinction") == 0) { Reading = float(Settings.DensityExtinction); return true; }
    if (std::strcmp(Key, "smokeAlbedo") == 0) { Reading = float(Settings.SmokeAlbedo); return true; }
    if (std::strcmp(Key, "shadowDensity") == 0) { Reading = float(Settings.ShadowDensity); return true; }
    if (std::strcmp(Key, "phaseAnisotropy") == 0) { Reading = float(Settings.PhaseAnisotropy); return true; }
    if (std::strcmp(Key, "sunElevation") == 0) { Reading = float(Settings.SunElevation); return true; }
    if (std::strcmp(Key, "sunAzimuth") == 0) { Reading = float(Settings.SunAzimuth); return true; }
    if (std::strcmp(Key, "exposure") == 0) { Reading = float(Settings.Exposure); return true; }
    if (std::strcmp(Key, "emitterEnabled") == 0) { Reading = Settings.EmitterEnabled ? 1.0f : 0.0f; return true; }
    if (std::strcmp(Key, "emitterRate") == 0) { Reading = float(Settings.EmitterRate); return true; }
    if (std::strcmp(Key, "emitterRadius") == 0) { Reading = float(Settings.EmitterRadius); return true; }
    if (std::strcmp(Key, "emitterUpwardVelocity") == 0) { Reading = float(Settings.EmitterUpwardVelocity); return true; }
    if (std::strcmp(Key, "emitterSwirl") == 0) { Reading = float(Settings.EmitterSwirl); return true; }
    if (std::strcmp(Key, "shrapnelEnabled") == 0) { Reading = Settings.ShrapnelEnabled ? 1.0f : 0.0f; return true; }
    if (std::strcmp(Key, "blastStrength") == 0) { Reading = float(Settings.BlastStrength); return true; }
    if (std::strcmp(Key, "blastRadius") == 0) { Reading = float(Settings.BlastRadius); return true; }
    if (std::strcmp(Key, "blastTemperature") == 0) { Reading = float(Settings.BlastTemperature); return true; }
    if (std::strcmp(Key, "blastLobes") == 0) { Reading = float(Settings.BlastLobes); return true; }
    if (std::strcmp(Key, "obstacleType") == 0) { Reading = float(Settings.ObstacleType); return true; }
    if (std::strcmp(Key, "obstacleY") == 0) { Reading = float(Settings.ObstacleY); return true; }
    if (std::strcmp(Key, "obstacleX") == 0) { Reading = float(Settings.ObstacleX); return true; }
    if (std::strcmp(Key, "obstacleZ") == 0) { Reading = float(Settings.ObstacleZ); return true; }
    if (std::strcmp(Key, "colliderAutoMove") == 0) { Reading = Settings.ColliderAutoMove ? 1.0f : 0.0f; return true; }
    if (std::strcmp(Key, "colliderSpeed") == 0) { Reading = float(Settings.ColliderSpeed); return true; }
    if (std::strcmp(Key, "obstacleRadius") == 0) { Reading = float(Settings.ObstacleRadius); return true; }
    if (std::strcmp(Key, "showBoundingBox") == 0) { Reading = Settings.BoundingBoxShown ? 1.0f : 0.0f; return true; }
    if (std::strcmp(Key, "showVoxelGridLines") == 0) { Reading = Settings.VoxelLatticeLinesShown ? 1.0f : 0.0f; return true; }
    if (std::strcmp(Key, "showFloorGrid") == 0) { Reading = Settings.FloorLatticeShown ? 1.0f : 0.0f; return true; }
    if (std::strcmp(Key, "showEmbers") == 0) { Reading = Settings.EmbersShown ? 1.0f : 0.0f; return true; }
    if (std::strcmp(Key, "emberCount") == 0) { Reading = float(Settings.EmberCount); return true; }
    if (std::strcmp(Key, "emberSize") == 0) { Reading = float(Settings.EmberSize); return true; }
    if (std::strcmp(Key, "emberIntensity") == 0) { Reading = float(Settings.EmberIntensity); return true; }
    if (std::strcmp(Key, "emberLifetime") == 0) { Reading = float(Settings.EmberLifetime); return true; }
    if (std::strcmp(Key, "emberAshiness") == 0) { Reading = float(Settings.EmberAshiness); return true; }
    if (std::strcmp(Key, "emitterHeight") == 0) { Reading = float(Settings.EmitterHeight); return true; }
    if (std::strcmp(Key, "emitterTemperature") == 0) { Reading = float(Settings.EmitterTemperature); return true; }
    if (std::strcmp(Key, "emitterFuel") == 0) { Reading = float(Settings.EmitterFuel); return true; }
    if (std::strcmp(Key, "emitterSmoke") == 0) { Reading = float(Settings.EmitterSmoke); return true; }
    if (std::strcmp(Key, "blastFuel") == 0) { Reading = float(Settings.BlastFuel); return true; }
    if (std::strcmp(Key, "blastSmoke") == 0) { Reading = float(Settings.BlastSmoke); return true; }
    if (std::strcmp(Key, "windZ") == 0) { Reading = float(Settings.WindZ); return true; }
    if (std::strcmp(Key, "velocityDamping") == 0) { Reading = float(Settings.VelocityDamping); return true; }
    if (std::strcmp(Key, "sunIntensity") == 0) { Reading = float(Settings.SunIntensity); return true; }
    if (std::strcmp(Key, "ambientIntensity") == 0) { Reading = float(Settings.AmbientIntensity); return true; }
    return false;
}

//----------------------------------------------------------------------------------------------------------------------
//                                                  THE DEBUG CHANNELS                                                  
//----------------------------------------------------------------------------------------------------------------------
// DEBUG_CHANNELS (presets.js). ConnectInterface() fills both the viewport bar's select and the
//    diagnostics overlay's from this list, which is why these read shorter than the renderChannel
//    control's own option labels -- the list, not the control, is what a user actually sees.

constexpr uint32_t DebugChannelCount = 9u;

/// 📦 The channels the viewport can show, in order.
/// out   const ControlChoice*   [-]  DebugChannelCount entries, never null
/// cost  ✔️
inline const ControlChoice* DebugChannels() noexcept
{
    static const ControlChoice Rail[DebugChannelCount] = {
        { 0, "Lit volume" },
        { 1, "Voxel DDA" },
        { 2, "Smoke density" },
        { 3, "Temperature" },
        { 4, "Fuel" },
        { 5, "Velocity" },
        { 6, "Vorticity" },
        { 7, "Pressure / irradiance" },
        { 8, "Cross-section" },
    };
    return Rail;
}

}   // namespace Frontier::FluidEditor
