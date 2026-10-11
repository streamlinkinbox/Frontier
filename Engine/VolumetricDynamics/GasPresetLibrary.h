//==============================================================================================================================================
//                                                              GASPRESETLIBRARY.H                                                              
//==============================================================================================================================================
// 📦 GENERATED. The browser simulator's 85 settings and 18 presets, transcribed for the engine; edit presets.js, never this file.
//
// 🔴 DO NOT EDIT. Tools/Build/GenerateGasPresets.py writes this file from
//    Experimental/Fluid/src/presets.js, and --check fails the build when the two disagree. A hand
//    edit here is erased by the next generation and, worse, silently diverges the native plume from
//    the browser one it was tuned against until someone notices the two no longer look alike.
//
// The browser file stays authoritative because that is where the tuning is actually done -- with a
//    viewport, sliders and an immediate picture. Transcribing rather than re-authoring is what makes
//    the scene round-trip in step 4 of the port a real check instead of a formality.
//
// ⚠️ Settings marked [-] normalised are fractions of the domain, not world measurements. The browser
//    splats them in normalised coordinates, so enlarging the bounds without shrinking these shrinks
//    the effect in world terms -- the defect found and fixed while widening the explosion bounds.

#pragma once

#include <cstdint>
#include <cstring>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE SETTINGS                                                      
//------------------------------------------------------------------------------------------------------------------------

// One gas effect, fully described. Field order follows presets.js so the two read side by side.
struct GasSettings
{
    int32_t     LatticeResolution        = 64;        // [voxels]  - gridResolution
    int32_t     PressureIterations       = 22;        // [-]       - pressureIterations
    float       TimeScale                = 1.0f;      // [-]       - timeScale
    bool        Paused                   = false;     // [-]       - paused
    bool        MacCormackAdvection      = true;      // [-]       - macCormackAdvection
    bool        EnclosedBox              = false;     // [-]       - enclosedBox
    int32_t     VoxelQuantization        = 0;         // [-]       - voxelQuantization
    bool        AutoGpuGovernor          = false;     // [-]       - autoGpuGovernor
    float       BoundsWidth              = 1.85f;     // [m]       - boundsWidth
    float       BoundsHeight             = 2.1f;      // [m]       - boundsHeight
    bool        DynamicBounds            = true;      // [-]       - dynamicBounds
    float       DynamicBoundsMax         = 1.55f;     // [m]       - dynamicBoundsMax
    const char* InteractionMode          = "orbit";   // [-]       - interactionMode
    bool        AutoTurntable            = false;     // [-]       - autoTurntable
    float       VorticityConfinement     = 7.8f;      // [-]       - vorticityConfinement
    float       Buoyancy                 = 5.8f;      // [m/s2]    - buoyancy
    float       SmokeWeight              = 1.15f;     // [m/s2]    - smokeWeight
    float       BurnRate                 = 1.85f;     // [1/s]     - burnRate
    float       BurnHeat                 = 2.6f;      // [K]       - burnHeat
    float       SootGeneration           = 1.55f;     // [1/s]     - sootGeneration
    float       CombustionExpansion      = 4.4f;      // [-]       - combustionExpansion
    float       CoolingRate              = 1.35f;     // [1/s]     - coolingRate
    float       SmokeDissipation         = 0.18f;     // [1/s]     - smokeDissipation
    float       VelocityDamping          = 0.07f;     // [1/s]     - velocityDamping
    float       TurbulenceStrength       = 3.5f;      // [m/s]     - turbulenceStrength
    float       TurbulenceScale          = 3.8f;      // [1/m]     - turbulenceScale
    float       WindX                    = 0.22f;     // [m/s]     - windX
    float       WindZ                    = 0.0f;      // [m/s]     - windZ
    bool        EmitterEnabled           = true;      // [-]       - emitterEnabled
    float       EmitterRate              = 1.0f;      // [1/s]     - emitterRate
    float       EmitterRadius            = 0.1f;      // [-]       - normalised fraction of the domain
    float       EmitterHeight            = 0.07f;     // [-]       - normalised fraction of the domain
    float       EmitterUpwardVelocity    = 3.2f;      // [m/s]     - emitterUpwardVelocity
    float       EmitterSwirl             = 1.2f;      // [-]       - emitterSwirl
    float       EmitterTemperature       = 3.5f;      // [K]       - emitterTemperature
    float       EmitterFuel              = 2.8f;      // [-]       - emitterFuel
    float       EmitterSmoke             = 1.4f;      // [-]       - emitterSmoke
    float       BlastStrength            = 9.8f;      // [m/s]     - blastStrength
    float       BlastRadius              = 0.18f;     // [-]       - normalised fraction of the domain
    float       BlastTemperature         = 5.4f;      // [K]       - blastTemperature
    float       BlastFuel                = 4.6f;      // [-]       - blastFuel
    float       BlastSmoke               = 2.6f;      // [-]       - blastSmoke
    int32_t     BlastLobes               = 6;         // [-]       - blastLobes
    bool        ShrapnelEnabled          = true;      // [-]       - shrapnelEnabled
    int32_t     RenderChannel            = 0;         // [-]       - renderChannel
    int32_t     ColorPalette             = 0;         // [-]       - colorPalette
    float       RenderScale              = 1.0f;      // [-]       - renderScale
    int32_t     RaymarchSteps            = 96;        // [-]       - raymarchSteps
    int32_t     ShadowSteps              = 6;         // [-]       - shadowSteps
    float       DensityExtinction        = 18.5f;     // [1/m]     - densityExtinction
    float       SmokeAlbedo              = 0.22f;     // [-]       - smokeAlbedo
    float       ShadowDensity            = 11.5f;     // [1/m]     - shadowDensity
    float       FireIntensity            = 5.6f;      // [-]       - fireIntensity
    float       TemperatureScale         = 1.15f;     // [-]       - temperatureScale
    float       InternalScattering       = 2.5f;      // [-]       - internalScattering
    float       PhaseAnisotropy          = 0.38f;     // [-]       - phaseAnisotropy
    float       AmbientIntensity         = 0.35f;     // [-]       - ambientIntensity
    float       SunIntensity             = 2.2f;      // [-]       - sunIntensity
    float       SunAzimuth               = 42.0f;     // [deg]     - sunAzimuth
    float       SunElevation             = 52.0f;     // [deg]     - sunElevation
    float       Exposure                 = 1.25f;     // [-]       - exposure
    float       BloomIntensity           = 0.55f;     // [-]       - bloomIntensity
    float       GodRaysIntensity         = 0.45f;     // [-]       - godRaysIntensity
    float       ShockwaveStrength        = 1.0f;      // [-]       - shockwaveStrength
    int32_t     SliceAxis                = 2;         // [-]       - sliceAxis
    float       SlicePosition            = 0.5f;      // [-]       - normalised fraction of the domain
    int32_t     ObstacleType             = 0;         // [-]       - obstacleType
    float       ObstacleX                = 0.5f;      // [-]       - normalised fraction of the domain
    float       ObstacleY                = 0.26f;     // [-]       - normalised fraction of the domain
    float       ObstacleZ                = 0.5f;      // [-]       - normalised fraction of the domain
    float       ObstacleRadius           = 0.16f;     // [-]       - normalised fraction of the domain
    bool        ColliderAutoMove         = true;      // [-]       - colliderAutoMove
    float       ColliderSpeed            = 1.25f;     // [m/s]     - colliderSpeed
    bool        BoundingBoxShown         = true;      // [-]       - showBoundingBox
    bool        VoxelLatticeLinesShown   = true;      // [-]       - showVoxelGridLines
    bool        ActiveVoxelCellsShown    = false;     // [-]       - showActiveVoxelCells
    bool        FloorLatticeShown        = true;      // [-]       - showFloorGrid
    bool        EmbersShown              = true;      // [-]       - showEmbers
    int32_t     EmberCount               = 550;       // [-]       - emberCount
    float       EmberSize                = 1.0f;      // [-]       - emberSize
    float       EmberIntensity           = 1.4f;      // [-]       - emberIntensity
    float       EmberLifetime            = 1.0f;      // [s]       - emberLifetime
    float       EmberAshiness            = 0.0f;      // [-]       - emberAshiness
    bool        TileSheetOverviewShown   = true;      // [-]       - showAtlasMinimap
    int32_t     TileSheetOverviewReading = 0;         // [-]       - atlasMinimapField
};

constexpr uint32_t GasSettingCount = 85u;


//------------------------------------------------------------------------------------------------------------------------
//                                                      THE PRESETS                                                       
//------------------------------------------------------------------------------------------------------------------------

// A preset is an identity, two lines of presentation, and the settings it differs from the default
//    in. The differences are applied over a default-constructed GasSettings, which is what lets a
//    new setting arrive with a default without touching all 18 presets.
struct GasPreset
{
    const char* Identity        = "";      // [-] - stable key; what a scene stores
    const char* Name            = "";      // [-] - what the preset rail shows
    const char* Description     = "";      // [-] - the line beneath it
    bool        DetonateOnLoad  = false;   // [-] - the preset opens mid-explosion rather than idle
};

constexpr uint32_t GasPresetCount = 18u;

inline const GasPreset* GasPresetLibrary() noexcept
{
    static const GasPreset Library[GasPresetCount] = {
        { "ue5_pyro_default", "UE5 Niagara Pyro Plume + Blast",
          "Balanced hydrocarbon fireball with dynamic expanding bounds, ballistic shrapnel trails, and rolling dark soot.",
          true },
        { "shrapnel_airburst", "Ordnance Shrapnel Airburst",
          "High-explosive airburst ejecting 6 burning ballistic shrapnel fragments that arc through the 3D volume leaving smoke streamers.",
          true },
        { "megaton_open_bounds", "Megaton Wide-Bounds Blast",
          "Extra-large 3.0x simulation bounds with dynamic surge expansion so massive explosions and salvos have full room to mushroom.",
          true },
        { "low_gpu_performance", "Low-End GPU Fast Mode",
          "Ultra-lightweight 24³ voxel grid (13.8K voxels) + 56 raymarch steps for reduced GPU cost; performance depends on the device.",
          true },
        { "tactical_ordnance", "Tactical C4 / HE Blast Wave",
          "High-explosive shockwave detonation with rapid divergence expansion, blinding flash core, and billowing dust/soot cloud.",
          true },
        { "oil_well_inferno", "Raging Oil Well Inferno",
          "High-velocity pressurized fuel blowout generating a roaring flame column and dense carbonaceous black smoke.",
          false },
        { "obstacle_deflection", "Voxel Collider Flow Deflection",
          "Demonstrates a 3D solid sphere collider inside the Eulerian grid—fire and smoke split and curl around the obstacle.",
          true },
        { "wildfire_tornado", "Fire Tornado / Alchemical Vortex",
          "High-swirl fire tornado with copper-emerald blackbody radiation and intense rotational vorticity.",
          true },
        { "ashfall_motes", "Ashfall / Grey Fireflies",
          "A cooling afterburn cloud with dense grey ash motes instead of orange sparks—use the ember sliders to tune size, brightness, lifetime, and ashiness.",
          true },
        { "voxelized_stylized", "Voxelized Stylized Grid",
          "Explicit voxelized resolution look showing discrete 3D grid cells with quantized volumetric shading.",
          true },
        { "dust_tornado", "Dust Tornado / Debris Vortex",
          "A cold ground-up vortex with no fuel at all: heavy dust held in a tall swirling column by strong vorticity and almost no buoyancy.",
          false },
        { "tyre_burnout", "Tyre Burnout",
          "Hot rubber smoke off a spinning tyre: the ring collider sits in the grid, the source is flat on the floor, and the cloud is dragged sideways instead of climbing.",
          false },
        { "ledge_sandfall", "Sand Falling From A Ledge",
          "Dry sand poured off a slab: the source sits high, smoke weight beats buoyancy so the column falls, and bright grains settle out of the base.",
          false },
        { "settling_dust", "Settling Dust",
          "The quiet aftermath: a slow pale cloud that hangs, drifts on a light crosswind and sinks rather than rises. No fire anywhere in it.",
          false },
        { "small_gust", "Small Gust / Drifting Haze",
          "A thin sheet of haze pushed across the domain by a steady crosswind and torn apart by fine turbulence. Cheap enough to leave running.",
          false },
        { "brick_fracture_dust", "Fractured Brick Burst",
          "Masonry breaking: a cold burst with no fuel and a great deal of smoke, throwing heavy chips off a slab. This is the shape a fracture event should spawn.",
          true },
        { "lantern_flame", "Lantern / Oil Lamp Flame",
          "One small steady wick in a tight domain. The box is the smallest the editor allows, so a 96 grid spends every voxel on the flame itself.",
          false },
        { "camp_fire", "Camp Fire",
          "A wood fire in the open: a broad low base, a lazy lean on the breeze, rising sparks and a thin pale smoke rather than a black one.",
          false },
    };
    return Library;
}


//------------------------------------------------------------------------------------------------------------------------
//                                                 CONSTRUCTING A PRESET                                                  
//------------------------------------------------------------------------------------------------------------------------

/// 📦 The settings for one preset: the defaults above, with that preset's differences applied.
/// in    Identity      [-]  stable preset key
/// out   GasSettings   [-]  fully resolved; the defaults unchanged when the key is unknown
/// err   an unknown key yields the defaults rather than refusing, so a scene naming a preset that has
///       since been renamed still opens and still looks like something
/// note  the differences are applied in the order presets.js lists them, which matters only for the
///       reader, since no key is written twice
/// cost  ✔️
/// tag   api, generated, nonallocating, nonthrowing
inline GasSettings ConstructPresetSettings(const char* Identity) noexcept
{
    GasSettings Resolved;
    if (Identity == nullptr) return Resolved;

    if (std::strcmp(Identity, "ue5_pyro_default") == 0)
    {
        Resolved.LatticeResolution = 96;
        Resolved.BlastRadius = 0.13f;
        Resolved.BoundsWidth = 2.6f;
        Resolved.BoundsHeight = 2.95f;
        Resolved.DynamicBounds = true;
        Resolved.DynamicBoundsMax = 1.2f;
        Resolved.ShrapnelEnabled = true;
        Resolved.VoxelQuantization = 0;
        Resolved.RaymarchSteps = 120;
        Resolved.RenderChannel = 0;
        Resolved.ColorPalette = 0;
        Resolved.VorticityConfinement = 7.8f;
        Resolved.Buoyancy = 5.8f;
        Resolved.SmokeWeight = 1.15f;
        Resolved.BurnRate = 1.85f;
        Resolved.BurnHeat = 2.6f;
        Resolved.SootGeneration = 1.55f;
        Resolved.CombustionExpansion = 4.4f;
        Resolved.CoolingRate = 1.35f;
        Resolved.SmokeDissipation = 0.18f;
        Resolved.TurbulenceStrength = 3.5f;
        Resolved.EmitterEnabled = true;
        Resolved.EmitterRate = 1.0f;
        Resolved.EmitterRadius = 0.1f;
        Resolved.EmitterUpwardVelocity = 3.2f;
        Resolved.EmitterSwirl = 1.2f;
        Resolved.EmitterTemperature = 3.5f;
        Resolved.EmitterFuel = 2.8f;
        Resolved.EmitterSmoke = 1.4f;
        Resolved.DensityExtinction = 18.5f;
        Resolved.SmokeAlbedo = 0.22f;
        Resolved.FireIntensity = 5.6f;
        Resolved.InternalScattering = 2.5f;
        Resolved.GodRaysIntensity = 0.45f;
        Resolved.ObstacleType = 0;
        Resolved.EmbersShown = true;
        Resolved.ActiveVoxelCellsShown = false;
    }
    else if (std::strcmp(Identity, "shrapnel_airburst") == 0)
    {
        Resolved.LatticeResolution = 96;
        Resolved.BoundsWidth = 3.3f;
        Resolved.BoundsHeight = 3.45f;
        Resolved.DynamicBounds = true;
        Resolved.DynamicBoundsMax = 1.2f;
        Resolved.ShrapnelEnabled = true;
        Resolved.EmitterEnabled = false;
        Resolved.ColorPalette = 0;
        Resolved.VorticityConfinement = 8.8f;
        Resolved.Buoyancy = 5.2f;
        Resolved.SmokeDissipation = 0.12f;
        Resolved.BlastStrength = 11.5f;
        Resolved.BlastRadius = 0.14f;
        Resolved.BlastTemperature = 6.2f;
        Resolved.GodRaysIntensity = 0.65f;
        Resolved.ObstacleType = 0;
        Resolved.EmbersShown = true;
        Resolved.ActiveVoxelCellsShown = false;
    }
    else if (std::strcmp(Identity, "megaton_open_bounds") == 0)
    {
        Resolved.LatticeResolution = 128;
        Resolved.BoundsWidth = 3.9f;
        Resolved.BoundsHeight = 4.0f;
        Resolved.DynamicBounds = true;
        Resolved.DynamicBoundsMax = 1.2f;
        Resolved.ShrapnelEnabled = true;
        Resolved.VoxelQuantization = 0;
        Resolved.RaymarchSteps = 120;
        Resolved.RenderChannel = 0;
        Resolved.ColorPalette = 0;
        Resolved.VorticityConfinement = 9.2f;
        Resolved.Buoyancy = 6.2f;
        Resolved.SmokeWeight = 1.2f;
        Resolved.BurnRate = 1.75f;
        Resolved.BurnHeat = 3.0f;
        Resolved.SootGeneration = 1.9f;
        Resolved.CombustionExpansion = 5.8f;
        Resolved.CoolingRate = 1.2f;
        Resolved.SmokeDissipation = 0.11f;
        Resolved.TurbulenceStrength = 4.5f;
        Resolved.EmitterEnabled = false;
        Resolved.BlastStrength = 13.5f;
        Resolved.BlastRadius = 0.13f;
        Resolved.BlastTemperature = 6.5f;
        Resolved.BlastFuel = 5.4f;
        Resolved.BlastSmoke = 3.0f;
        Resolved.DensityExtinction = 20.0f;
        Resolved.SmokeAlbedo = 0.26f;
        Resolved.FireIntensity = 6.5f;
        Resolved.InternalScattering = 3.0f;
        Resolved.ObstacleType = 0;
        Resolved.EmbersShown = true;
        Resolved.ActiveVoxelCellsShown = false;
    }
    else if (std::strcmp(Identity, "low_gpu_performance") == 0)
    {
        Resolved.LatticeResolution = 24;
        Resolved.BoundsWidth = 2.0f;
        Resolved.BoundsHeight = 2.2f;
        Resolved.DynamicBounds = true;
        Resolved.PressureIterations = 14;
        Resolved.RaymarchSteps = 56;
        Resolved.ShadowSteps = 4;
        Resolved.RenderScale = 0.85f;
        Resolved.VoxelQuantization = 0;
        Resolved.RenderChannel = 0;
        Resolved.VorticityConfinement = 8.5f;
        Resolved.Buoyancy = 6.0f;
        Resolved.ActiveVoxelCellsShown = false;
    }
    else if (std::strcmp(Identity, "tactical_ordnance") == 0)
    {
        Resolved.LatticeResolution = 96;
        Resolved.BoundsWidth = 3.05f;
        Resolved.BoundsHeight = 3.25f;
        Resolved.DynamicBounds = true;
        Resolved.DynamicBoundsMax = 1.2f;
        Resolved.ShrapnelEnabled = true;
        Resolved.VoxelQuantization = 0;
        Resolved.RaymarchSteps = 120;
        Resolved.RenderChannel = 0;
        Resolved.ColorPalette = 2;
        Resolved.VorticityConfinement = 9.2f;
        Resolved.Buoyancy = 4.6f;
        Resolved.SmokeWeight = 1.3f;
        Resolved.BurnRate = 2.6f;
        Resolved.BurnHeat = 3.2f;
        Resolved.SootGeneration = 2.1f;
        Resolved.CombustionExpansion = 6.5f;
        Resolved.CoolingRate = 1.75f;
        Resolved.SmokeDissipation = 0.14f;
        Resolved.TurbulenceStrength = 4.8f;
        Resolved.EmitterEnabled = false;
        Resolved.BlastStrength = 12.5f;
        Resolved.BlastRadius = 0.15f;
        Resolved.BlastTemperature = 6.4f;
        Resolved.BlastFuel = 5.2f;
        Resolved.BlastSmoke = 3.2f;
        Resolved.DensityExtinction = 22.0f;
        Resolved.SmokeAlbedo = 0.34f;
        Resolved.FireIntensity = 6.8f;
        Resolved.InternalScattering = 3.1f;
        Resolved.ObstacleType = 0;
        Resolved.EmbersShown = true;
        Resolved.ActiveVoxelCellsShown = false;
    }
    else if (std::strcmp(Identity, "oil_well_inferno") == 0)
    {
        Resolved.BoundsWidth = 1.9f;
        Resolved.BoundsHeight = 2.4f;
        Resolved.DynamicBounds = true;
        Resolved.VoxelQuantization = 0;
        Resolved.RenderChannel = 0;
        Resolved.ColorPalette = 1;
        Resolved.VorticityConfinement = 8.8f;
        Resolved.Buoyancy = 7.2f;
        Resolved.SmokeWeight = 1.4f;
        Resolved.BurnRate = 1.6f;
        Resolved.BurnHeat = 3.1f;
        Resolved.SootGeneration = 2.2f;
        Resolved.CombustionExpansion = 3.4f;
        Resolved.CoolingRate = 1.15f;
        Resolved.SmokeDissipation = 0.22f;
        Resolved.TurbulenceStrength = 4.2f;
        Resolved.WindX = 0.65f;
        Resolved.EmitterEnabled = true;
        Resolved.EmitterRate = 1.45f;
        Resolved.EmitterRadius = 0.11f;
        Resolved.EmitterUpwardVelocity = 5.4f;
        Resolved.EmitterSwirl = 1.8f;
        Resolved.EmitterTemperature = 4.2f;
        Resolved.EmitterFuel = 3.6f;
        Resolved.EmitterSmoke = 1.9f;
        Resolved.DensityExtinction = 24.0f;
        Resolved.SmokeAlbedo = 0.12f;
        Resolved.FireIntensity = 6.0f;
        Resolved.InternalScattering = 2.8f;
        Resolved.ObstacleType = 0;
        Resolved.EmbersShown = true;
        Resolved.ActiveVoxelCellsShown = false;
    }
    else if (std::strcmp(Identity, "obstacle_deflection") == 0)
    {
        Resolved.BoundsWidth = 1.85f;
        Resolved.BoundsHeight = 2.1f;
        Resolved.ColorPalette = 0;
        Resolved.VorticityConfinement = 8.4f;
        Resolved.Buoyancy = 6.4f;
        Resolved.BurnRate = 1.75f;
        Resolved.CombustionExpansion = 3.8f;
        Resolved.CoolingRate = 1.2f;
        Resolved.SmokeDissipation = 0.18f;
        Resolved.EmitterEnabled = true;
        Resolved.EmitterRate = 1.25f;
        Resolved.EmitterRadius = 0.11f;
        Resolved.EmitterUpwardVelocity = 4.6f;
        Resolved.ObstacleType = 1;
        Resolved.ObstacleX = 0.5f;
        Resolved.ObstacleY = 0.42f;
        Resolved.ObstacleZ = 0.5f;
        Resolved.ObstacleRadius = 0.16f;
        Resolved.ActiveVoxelCellsShown = false;
    }
    else if (std::strcmp(Identity, "wildfire_tornado") == 0)
    {
        Resolved.BoundsWidth = 1.9f;
        Resolved.BoundsHeight = 2.3f;
        Resolved.ColorPalette = 3;
        Resolved.VorticityConfinement = 9.5f;
        Resolved.Buoyancy = 6.8f;
        Resolved.BurnRate = 1.7f;
        Resolved.CombustionExpansion = 3.5f;
        Resolved.CoolingRate = 1.25f;
        Resolved.SmokeDissipation = 0.2f;
        Resolved.TurbulenceStrength = 3.8f;
        Resolved.EmitterEnabled = true;
        Resolved.EmitterRate = 1.3f;
        Resolved.EmitterRadius = 0.12f;
        Resolved.EmitterUpwardVelocity = 4.2f;
        Resolved.EmitterSwirl = 5.5f;
        Resolved.EmitterTemperature = 3.9f;
        Resolved.EmitterFuel = 3.2f;
        Resolved.ObstacleType = 0;
        Resolved.ActiveVoxelCellsShown = false;
    }
    else if (std::strcmp(Identity, "ashfall_motes") == 0)
    {
        Resolved.EmitterEnabled = false;
        Resolved.BlastStrength = 7.0f;
        Resolved.BlastRadius = 0.2f;
        Resolved.BlastTemperature = 2.4f;
        Resolved.BlastFuel = 1.2f;
        Resolved.BlastSmoke = 3.8f;
        Resolved.ColorPalette = 1;
        Resolved.SmokeDissipation = 0.08f;
        Resolved.EmbersShown = true;
        Resolved.EmberCount = 900;
        Resolved.EmberSize = 1.25f;
        Resolved.EmberIntensity = 0.95f;
        Resolved.EmberLifetime = 1.65f;
        Resolved.EmberAshiness = 1.0f;
        Resolved.ActiveVoxelCellsShown = false;
    }
    else if (std::strcmp(Identity, "voxelized_stylized") == 0)
    {
        Resolved.VoxelQuantization = 0;
        Resolved.RenderChannel = 1;
        Resolved.VorticityConfinement = 8.0f;
        Resolved.ActiveVoxelCellsShown = true;
    }
    else if (std::strcmp(Identity, "dust_tornado") == 0)
    {
        Resolved.LatticeResolution = 96;
        Resolved.BoundsWidth = 2.2f;
        Resolved.BoundsHeight = 3.6f;
        Resolved.DynamicBounds = false;
        Resolved.ColorPalette = 1;
        Resolved.VorticityConfinement = 11.5f;
        Resolved.Buoyancy = 1.3f;
        Resolved.SmokeWeight = 1.9f;
        Resolved.BurnRate = 0.2f;
        Resolved.BurnHeat = 0.5f;
        Resolved.SootGeneration = 0.2f;
        Resolved.CombustionExpansion = 0.0f;
        Resolved.CoolingRate = 3.2f;
        Resolved.SmokeDissipation = 0.05f;
        Resolved.VelocityDamping = 0.03f;
        Resolved.TurbulenceStrength = 2.6f;
        Resolved.TurbulenceScale = 2.4f;
        Resolved.WindX = 0.12f;
        Resolved.EmitterEnabled = true;
        Resolved.EmitterRate = 1.6f;
        Resolved.EmitterRadius = 0.09f;
        Resolved.EmitterHeight = 0.04f;
        Resolved.EmitterUpwardVelocity = 2.6f;
        Resolved.EmitterSwirl = 7.6f;
        Resolved.EmitterTemperature = 0.3f;
        Resolved.EmitterFuel = 0.0f;
        Resolved.EmitterSmoke = 3.4f;
        Resolved.BlastFuel = 0.0f;
        Resolved.BlastTemperature = 2.0f;
        Resolved.BlastSmoke = 5.5f;
        Resolved.DensityExtinction = 15.0f;
        Resolved.SmokeAlbedo = 0.62f;
        Resolved.ShadowDensity = 7.0f;
        Resolved.FireIntensity = 0.5f;
        Resolved.InternalScattering = 1.2f;
        Resolved.PhaseAnisotropy = 0.1f;
        Resolved.ObstacleType = 0;
        Resolved.EmbersShown = true;
        Resolved.EmberCount = 700;
        Resolved.EmberSize = 0.6f;
        Resolved.EmberIntensity = 0.35f;
        Resolved.EmberLifetime = 2.4f;
        Resolved.EmberAshiness = 1.0f;
        Resolved.ActiveVoxelCellsShown = false;
    }
    else if (std::strcmp(Identity, "tyre_burnout") == 0)
    {
        Resolved.LatticeResolution = 96;
        Resolved.BoundsWidth = 2.6f;
        Resolved.BoundsHeight = 1.7f;
        Resolved.DynamicBounds = false;
        Resolved.ColorPalette = 1;
        Resolved.VorticityConfinement = 6.4f;
        Resolved.Buoyancy = 2.3f;
        Resolved.SmokeWeight = 1.25f;
        Resolved.BurnRate = 0.4f;
        Resolved.BurnHeat = 0.8f;
        Resolved.SootGeneration = 3.4f;
        Resolved.CombustionExpansion = 0.4f;
        Resolved.CoolingRate = 2.6f;
        Resolved.SmokeDissipation = 0.07f;
        Resolved.VelocityDamping = 0.05f;
        Resolved.TurbulenceStrength = 3.4f;
        Resolved.TurbulenceScale = 4.2f;
        Resolved.WindX = 1.15f;
        Resolved.WindZ = -0.35f;
        Resolved.EmitterEnabled = true;
        Resolved.EmitterRate = 1.9f;
        Resolved.EmitterRadius = 0.13f;
        Resolved.EmitterHeight = 0.02f;
        Resolved.EmitterUpwardVelocity = 1.4f;
        Resolved.EmitterSwirl = 3.2f;
        Resolved.EmitterTemperature = 0.9f;
        Resolved.EmitterFuel = 0.2f;
        Resolved.EmitterSmoke = 4.2f;
        Resolved.BlastFuel = 0.0f;
        Resolved.BlastTemperature = 2.0f;
        Resolved.BlastSmoke = 5.5f;
        Resolved.DensityExtinction = 27.0f;
        Resolved.SmokeAlbedo = 0.17f;
        Resolved.ShadowDensity = 14.0f;
        Resolved.FireIntensity = 0.8f;
        Resolved.InternalScattering = 0.9f;
        Resolved.ObstacleType = 5;
        Resolved.ObstacleX = 0.5f;
        Resolved.ObstacleY = 0.26f;
        Resolved.ObstacleZ = 0.5f;
        Resolved.ObstacleRadius = 0.17f;
        Resolved.ColliderAutoMove = false;
        Resolved.EmbersShown = false;
        Resolved.ActiveVoxelCellsShown = false;
    }
    else if (std::strcmp(Identity, "ledge_sandfall") == 0)
    {
        Resolved.LatticeResolution = 96;
        Resolved.BoundsWidth = 1.9f;
        Resolved.BoundsHeight = 2.8f;
        Resolved.DynamicBounds = false;
        Resolved.ColorPalette = 1;
        Resolved.VorticityConfinement = 3.6f;
        Resolved.Buoyancy = 0.2f;
        Resolved.SmokeWeight = 3.2f;
        Resolved.BurnRate = 0.2f;
        Resolved.BurnHeat = 0.5f;
        Resolved.SootGeneration = 0.2f;
        Resolved.CombustionExpansion = 0.0f;
        Resolved.CoolingRate = 3.5f;
        Resolved.SmokeDissipation = 0.06f;
        Resolved.VelocityDamping = 0.02f;
        Resolved.TurbulenceStrength = 1.4f;
        Resolved.TurbulenceScale = 5.5f;
        Resolved.WindX = 0.18f;
        Resolved.EmitterEnabled = true;
        Resolved.EmitterRate = 1.35f;
        Resolved.EmitterRadius = 0.07f;
        Resolved.EmitterHeight = 0.92f;
        Resolved.EmitterUpwardVelocity = 0.5f;
        Resolved.EmitterSwirl = 0.4f;
        Resolved.EmitterTemperature = 0.2f;
        Resolved.EmitterFuel = 0.0f;
        Resolved.EmitterSmoke = 3.8f;
        Resolved.BlastFuel = 0.0f;
        Resolved.BlastTemperature = 2.0f;
        Resolved.BlastSmoke = 5.5f;
        Resolved.DensityExtinction = 13.0f;
        Resolved.SmokeAlbedo = 0.74f;
        Resolved.ShadowDensity = 6.0f;
        Resolved.FireIntensity = 0.5f;
        Resolved.InternalScattering = 1.6f;
        Resolved.PhaseAnisotropy = 0.22f;
        Resolved.ObstacleType = 4;
        Resolved.ObstacleX = 0.5f;
        Resolved.ObstacleY = 0.62f;
        Resolved.ObstacleZ = 0.5f;
        Resolved.ObstacleRadius = 0.2f;
        Resolved.ColliderAutoMove = false;
        Resolved.EmbersShown = true;
        Resolved.EmberCount = 850;
        Resolved.EmberSize = 0.5f;
        Resolved.EmberIntensity = 0.3f;
        Resolved.EmberLifetime = 1.3f;
        Resolved.EmberAshiness = 1.0f;
        Resolved.ActiveVoxelCellsShown = false;
    }
    else if (std::strcmp(Identity, "settling_dust") == 0)
    {
        Resolved.LatticeResolution = 64;
        Resolved.BoundsWidth = 2.3f;
        Resolved.BoundsHeight = 1.8f;
        Resolved.DynamicBounds = false;
        Resolved.ColorPalette = 1;
        Resolved.VorticityConfinement = 2.8f;
        Resolved.Buoyancy = 0.6f;
        Resolved.SmokeWeight = 2.4f;
        Resolved.BurnRate = 0.2f;
        Resolved.BurnHeat = 0.5f;
        Resolved.SootGeneration = 0.2f;
        Resolved.CombustionExpansion = 0.0f;
        Resolved.CoolingRate = 3.5f;
        Resolved.SmokeDissipation = 0.04f;
        Resolved.VelocityDamping = 0.12f;
        Resolved.TurbulenceStrength = 1.2f;
        Resolved.TurbulenceScale = 2.8f;
        Resolved.WindX = 0.38f;
        Resolved.EmitterEnabled = true;
        Resolved.EmitterRate = 0.6f;
        Resolved.EmitterRadius = 0.18f;
        Resolved.EmitterHeight = 0.1f;
        Resolved.EmitterUpwardVelocity = 0.8f;
        Resolved.EmitterSwirl = 0.6f;
        Resolved.EmitterTemperature = 0.2f;
        Resolved.EmitterFuel = 0.0f;
        Resolved.EmitterSmoke = 2.4f;
        Resolved.BlastFuel = 0.0f;
        Resolved.BlastTemperature = 2.0f;
        Resolved.BlastSmoke = 5.5f;
        Resolved.DensityExtinction = 11.0f;
        Resolved.SmokeAlbedo = 0.68f;
        Resolved.ShadowDensity = 5.0f;
        Resolved.FireIntensity = 0.5f;
        Resolved.InternalScattering = 1.8f;
        Resolved.PhaseAnisotropy = 0.18f;
        Resolved.ObstacleType = 0;
        Resolved.EmbersShown = false;
        Resolved.ActiveVoxelCellsShown = false;
    }
    else if (std::strcmp(Identity, "small_gust") == 0)
    {
        Resolved.LatticeResolution = 48;
        Resolved.BoundsWidth = 2.5f;
        Resolved.BoundsHeight = 1.5f;
        Resolved.DynamicBounds = false;
        Resolved.ColorPalette = 1;
        Resolved.VorticityConfinement = 4.2f;
        Resolved.Buoyancy = 1.1f;
        Resolved.SmokeWeight = 0.35f;
        Resolved.BurnRate = 0.2f;
        Resolved.BurnHeat = 0.5f;
        Resolved.SootGeneration = 0.2f;
        Resolved.CombustionExpansion = 0.0f;
        Resolved.CoolingRate = 3.0f;
        Resolved.SmokeDissipation = 0.42f;
        Resolved.VelocityDamping = 0.04f;
        Resolved.TurbulenceStrength = 2.4f;
        Resolved.TurbulenceScale = 6.8f;
        Resolved.WindX = 1.75f;
        Resolved.WindZ = 0.55f;
        Resolved.EmitterEnabled = true;
        Resolved.EmitterRate = 0.45f;
        Resolved.EmitterRadius = 0.12f;
        Resolved.EmitterHeight = 0.22f;
        Resolved.EmitterUpwardVelocity = 0.9f;
        Resolved.EmitterSwirl = 0.9f;
        Resolved.EmitterTemperature = 0.2f;
        Resolved.EmitterFuel = 0.0f;
        Resolved.EmitterSmoke = 1.1f;
        Resolved.RaymarchSteps = 64;
        Resolved.BlastFuel = 0.0f;
        Resolved.BlastTemperature = 2.0f;
        Resolved.BlastSmoke = 5.5f;
        Resolved.DensityExtinction = 7.0f;
        Resolved.SmokeAlbedo = 0.6f;
        Resolved.ShadowDensity = 4.0f;
        Resolved.FireIntensity = 0.5f;
        Resolved.InternalScattering = 1.4f;
        Resolved.ObstacleType = 0;
        Resolved.EmbersShown = false;
        Resolved.ActiveVoxelCellsShown = false;
    }
    else if (std::strcmp(Identity, "brick_fracture_dust") == 0)
    {
        Resolved.LatticeResolution = 96;
        Resolved.BoundsWidth = 2.4f;
        Resolved.BoundsHeight = 2.2f;
        Resolved.DynamicBounds = true;
        Resolved.DynamicBoundsMax = 1.2f;
        Resolved.ShrapnelEnabled = true;
        Resolved.ColorPalette = 1;
        Resolved.VorticityConfinement = 6.8f;
        Resolved.Buoyancy = 0.9f;
        Resolved.SmokeWeight = 2.1f;
        Resolved.BurnRate = 0.2f;
        Resolved.BurnHeat = 0.5f;
        Resolved.SootGeneration = 0.2f;
        Resolved.CombustionExpansion = 0.2f;
        Resolved.CoolingRate = 3.4f;
        Resolved.SmokeDissipation = 0.07f;
        Resolved.TurbulenceStrength = 4.4f;
        Resolved.TurbulenceScale = 3.2f;
        Resolved.EmitterEnabled = false;
        Resolved.EmitterFuel = 0.0f;
        Resolved.EmitterTemperature = 0.2f;
        Resolved.BlastStrength = 8.5f;
        Resolved.BlastRadius = 0.11f;
        Resolved.BlastTemperature = 2.0f;
        Resolved.BlastFuel = 0.0f;
        Resolved.BlastSmoke = 6.5f;
        Resolved.BlastLobes = 9;
        Resolved.RaymarchSteps = 110;
        Resolved.DensityExtinction = 26.0f;
        Resolved.SmokeAlbedo = 0.56f;
        Resolved.ShadowDensity = 12.0f;
        Resolved.FireIntensity = 0.5f;
        Resolved.InternalScattering = 1.1f;
        Resolved.ObstacleType = 4;
        Resolved.ObstacleX = 0.5f;
        Resolved.ObstacleY = 0.3f;
        Resolved.ObstacleZ = 0.5f;
        Resolved.ObstacleRadius = 0.22f;
        Resolved.ColliderAutoMove = false;
        Resolved.EmbersShown = true;
        Resolved.EmberCount = 820;
        Resolved.EmberSize = 1.45f;
        Resolved.EmberIntensity = 0.4f;
        Resolved.EmberLifetime = 1.1f;
        Resolved.EmberAshiness = 1.0f;
        Resolved.ActiveVoxelCellsShown = false;
    }
    else if (std::strcmp(Identity, "lantern_flame") == 0)
    {
        Resolved.LatticeResolution = 96;
        Resolved.BoundsWidth = 1.0f;
        Resolved.BoundsHeight = 1.3f;
        Resolved.DynamicBounds = false;
        Resolved.ColorPalette = 0;
        Resolved.PressureIterations = 26;
        Resolved.VorticityConfinement = 3.4f;
        Resolved.Buoyancy = 3.8f;
        Resolved.SmokeWeight = 0.6f;
        Resolved.BurnRate = 1.15f;
        Resolved.BurnHeat = 2.2f;
        Resolved.SootGeneration = 0.3f;
        Resolved.CombustionExpansion = 1.6f;
        Resolved.CoolingRate = 1.9f;
        Resolved.SmokeDissipation = 0.82f;
        Resolved.VelocityDamping = 0.1f;
        Resolved.TurbulenceStrength = 0.85f;
        Resolved.TurbulenceScale = 7.5f;
        Resolved.WindX = 0.06f;
        Resolved.EmitterEnabled = true;
        Resolved.EmitterRate = 0.4f;
        Resolved.EmitterRadius = 0.05f;
        Resolved.EmitterHeight = 0.02f;
        Resolved.EmitterUpwardVelocity = 1.2f;
        Resolved.EmitterSwirl = 0.5f;
        Resolved.EmitterTemperature = 3.2f;
        Resolved.EmitterFuel = 2.4f;
        Resolved.EmitterSmoke = 0.3f;
        Resolved.RaymarchSteps = 128;
        Resolved.DensityExtinction = 12.0f;
        Resolved.SmokeAlbedo = 0.3f;
        Resolved.FireIntensity = 4.2f;
        Resolved.TemperatureScale = 1.05f;
        Resolved.InternalScattering = 2.2f;
        Resolved.AmbientIntensity = 0.18f;
        Resolved.SunIntensity = 0.9f;
        Resolved.Exposure = 1.05f;
        Resolved.BloomIntensity = 0.7f;
        Resolved.GodRaysIntensity = 0.25f;
        Resolved.ObstacleType = 0;
        Resolved.EmbersShown = false;
        Resolved.ActiveVoxelCellsShown = false;
    }
    else if (std::strcmp(Identity, "camp_fire") == 0)
    {
        Resolved.LatticeResolution = 64;
        Resolved.BoundsWidth = 1.6f;
        Resolved.BoundsHeight = 2.2f;
        Resolved.DynamicBounds = false;
        Resolved.ColorPalette = 0;
        Resolved.VorticityConfinement = 7.4f;
        Resolved.Buoyancy = 5.2f;
        Resolved.SmokeWeight = 0.85f;
        Resolved.BurnRate = 1.5f;
        Resolved.BurnHeat = 2.4f;
        Resolved.SootGeneration = 1.05f;
        Resolved.CombustionExpansion = 2.8f;
        Resolved.CoolingRate = 1.55f;
        Resolved.SmokeDissipation = 0.34f;
        Resolved.VelocityDamping = 0.06f;
        Resolved.TurbulenceStrength = 3.0f;
        Resolved.TurbulenceScale = 4.6f;
        Resolved.WindX = 0.32f;
        Resolved.EmitterEnabled = true;
        Resolved.EmitterRate = 0.95f;
        Resolved.EmitterRadius = 0.15f;
        Resolved.EmitterHeight = 0.05f;
        Resolved.EmitterUpwardVelocity = 2.5f;
        Resolved.EmitterSwirl = 1.7f;
        Resolved.EmitterTemperature = 3.1f;
        Resolved.EmitterFuel = 2.3f;
        Resolved.EmitterSmoke = 0.95f;
        Resolved.DensityExtinction = 14.0f;
        Resolved.SmokeAlbedo = 0.38f;
        Resolved.FireIntensity = 4.8f;
        Resolved.InternalScattering = 2.4f;
        Resolved.AmbientIntensity = 0.22f;
        Resolved.SunIntensity = 1.2f;
        Resolved.ObstacleType = 0;
        Resolved.EmbersShown = true;
        Resolved.EmberCount = 430;
        Resolved.EmberSize = 0.8f;
        Resolved.EmberIntensity = 1.6f;
        Resolved.EmberLifetime = 1.9f;
        Resolved.EmberAshiness = 0.25f;
        Resolved.ActiveVoxelCellsShown = false;
    }

    return Resolved;
}


/// 📦 Settings for the preset at a position in the library, for a caller walking all of them.
/// in    Slot          [-]  0 to GasPresetCount - 1
/// out   GasSettings   [-]  the defaults when the slot is out of range
/// cost  ✔️
/// tag   api, generated, nonallocating, nonthrowing
inline GasSettings ConstructPresetSettings(uint32_t Slot) noexcept
{
    if (Slot >= GasPresetCount) return GasSettings{};
    return ConstructPresetSettings(GasPresetLibrary()[Slot].Identity);
}

}   // namespace Frontier
