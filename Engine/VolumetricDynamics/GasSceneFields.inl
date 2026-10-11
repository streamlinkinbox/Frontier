//==============================================================================================================================================
//                                                              GASSCENEFIELDS.INL                                                              
//==============================================================================================================================================
// 📦 GENERATED. Crosses the 85 settings between TOML and GasSettings; edit presets.js, never this file.
//
// 🔴 DO NOT EDIT. Tools/Build/GenerateGasSceneCodec.py writes this from presets.js, and --check
//    fails the build when the two disagree. Included by GasSceneCodec.h inside namespace Frontier.
//
// The spelling of a number here must match SceneTomlCodec.js exactly, because the round-trip check
//    compares the two emitted files rather than their parsed contents. Comparing contents would pass
//    while the two disagreed about how to write a number, and the corpus would churn in version
//    control every time a different tool saved it.

// clang-format off

namespace GasSceneDetail {

//------------------------------------------------------------------------------------------------------------------------
//                                                   SPELLING A NUMBER                                                    
//------------------------------------------------------------------------------------------------------------------------

// Shortest representation that reads back as the same number — the same rule JavaScript's Number
//    printing follows, which is why the two emitters agree. A trailing ".0" is appended when the
//    shortest form has no point in it, because TOML would otherwise read a float back as an integer.
template <typename Number>
inline std::string SpellShortest(Number Reading, bool AsFloat) noexcept
{
    char Buffer[64];
    const auto Written = std::to_chars(Buffer, Buffer + sizeof(Buffer), Reading);
    if (Written.ec != std::errc{}) return AsFloat ? "0.0" : "0";
    std::string Spelt(Buffer, Written.ptr);
    if (AsFloat && Spelt.find('.') == std::string::npos && Spelt.find('e') == std::string::npos)
        Spelt += ".0";
    return Spelt;
}

inline std::string SpellFloat(double Reading) noexcept  { return SpellShortest(Reading, true); }
inline std::string SpellFloat(float Reading) noexcept   { return SpellShortest(Reading, true); }
inline std::string SpellCount(int32_t Reading) noexcept  { return std::to_string(Reading); }


//------------------------------------------------------------------------------------------------------------------------
//                                                CROSSING ONE SETTING IN                                                 
//------------------------------------------------------------------------------------------------------------------------

enum class Crossing : uint32_t
{
    Read      = 0u,   // [-] - understood and applied
    Unknown   = 1u,   // [-] - this build has never heard of it; reported, not refused
    WrongType = 2u,   // [-] - understood, but the file spells it as something it is not
};

inline Crossing CrossSetting(GasSettings& Target, std::string_view Key, const toml::node& Node) noexcept
{
    if (Key == "gridResolution")
        return ReadCount(Node, Target.LatticeResolution) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "pressureIterations")
        return ReadCount(Node, Target.PressureIterations) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "timeScale")
        return ReadFloat(Node, Target.TimeScale) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "paused")
        return ReadSwitch(Node, Target.Paused) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "macCormackAdvection")
        return ReadSwitch(Node, Target.MacCormackAdvection) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "enclosedBox")
        return ReadSwitch(Node, Target.EnclosedBox) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "voxelQuantization")
        return ReadCount(Node, Target.VoxelQuantization) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "autoGpuGovernor")
        return ReadSwitch(Node, Target.AutoGpuGovernor) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "boundsWidth")
        return ReadFloat(Node, Target.BoundsWidth) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "boundsHeight")
        return ReadFloat(Node, Target.BoundsHeight) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "dynamicBounds")
        return ReadSwitch(Node, Target.DynamicBounds) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "dynamicBoundsMax")
        return ReadFloat(Node, Target.DynamicBoundsMax) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "interactionMode")
        return Node.is_string() ? Crossing::Read : Crossing::WrongType;
    else if (Key == "autoTurntable")
        return ReadSwitch(Node, Target.AutoTurntable) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "vorticityConfinement")
        return ReadFloat(Node, Target.VorticityConfinement) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "buoyancy")
        return ReadFloat(Node, Target.Buoyancy) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "smokeWeight")
        return ReadFloat(Node, Target.SmokeWeight) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "burnRate")
        return ReadFloat(Node, Target.BurnRate) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "burnHeat")
        return ReadFloat(Node, Target.BurnHeat) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "sootGeneration")
        return ReadFloat(Node, Target.SootGeneration) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "combustionExpansion")
        return ReadFloat(Node, Target.CombustionExpansion) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "coolingRate")
        return ReadFloat(Node, Target.CoolingRate) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "smokeDissipation")
        return ReadFloat(Node, Target.SmokeDissipation) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "velocityDamping")
        return ReadFloat(Node, Target.VelocityDamping) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "turbulenceStrength")
        return ReadFloat(Node, Target.TurbulenceStrength) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "turbulenceScale")
        return ReadFloat(Node, Target.TurbulenceScale) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "windX")
        return ReadFloat(Node, Target.WindX) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "windZ")
        return ReadFloat(Node, Target.WindZ) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "emitterEnabled")
        return ReadSwitch(Node, Target.EmitterEnabled) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "emitterRate")
        return ReadFloat(Node, Target.EmitterRate) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "emitterRadius")
        return ReadFloat(Node, Target.EmitterRadius) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "emitterHeight")
        return ReadFloat(Node, Target.EmitterHeight) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "emitterUpwardVelocity")
        return ReadFloat(Node, Target.EmitterUpwardVelocity) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "emitterSwirl")
        return ReadFloat(Node, Target.EmitterSwirl) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "emitterTemperature")
        return ReadFloat(Node, Target.EmitterTemperature) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "emitterFuel")
        return ReadFloat(Node, Target.EmitterFuel) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "emitterSmoke")
        return ReadFloat(Node, Target.EmitterSmoke) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "blastStrength")
        return ReadFloat(Node, Target.BlastStrength) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "blastRadius")
        return ReadFloat(Node, Target.BlastRadius) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "blastTemperature")
        return ReadFloat(Node, Target.BlastTemperature) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "blastFuel")
        return ReadFloat(Node, Target.BlastFuel) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "blastSmoke")
        return ReadFloat(Node, Target.BlastSmoke) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "blastLobes")
        return ReadCount(Node, Target.BlastLobes) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "shrapnelEnabled")
        return ReadSwitch(Node, Target.ShrapnelEnabled) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "renderChannel")
        return ReadCount(Node, Target.RenderChannel) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "colorPalette")
        return ReadCount(Node, Target.ColorPalette) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "renderScale")
        return ReadFloat(Node, Target.RenderScale) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "raymarchSteps")
        return ReadCount(Node, Target.RaymarchSteps) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "shadowSteps")
        return ReadCount(Node, Target.ShadowSteps) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "densityExtinction")
        return ReadFloat(Node, Target.DensityExtinction) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "smokeAlbedo")
        return ReadFloat(Node, Target.SmokeAlbedo) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "shadowDensity")
        return ReadFloat(Node, Target.ShadowDensity) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "fireIntensity")
        return ReadFloat(Node, Target.FireIntensity) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "temperatureScale")
        return ReadFloat(Node, Target.TemperatureScale) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "internalScattering")
        return ReadFloat(Node, Target.InternalScattering) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "phaseAnisotropy")
        return ReadFloat(Node, Target.PhaseAnisotropy) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "ambientIntensity")
        return ReadFloat(Node, Target.AmbientIntensity) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "sunIntensity")
        return ReadFloat(Node, Target.SunIntensity) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "sunAzimuth")
        return ReadFloat(Node, Target.SunAzimuth) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "sunElevation")
        return ReadFloat(Node, Target.SunElevation) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "exposure")
        return ReadFloat(Node, Target.Exposure) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "bloomIntensity")
        return ReadFloat(Node, Target.BloomIntensity) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "godRaysIntensity")
        return ReadFloat(Node, Target.GodRaysIntensity) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "shockwaveStrength")
        return ReadFloat(Node, Target.ShockwaveStrength) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "sliceAxis")
        return ReadCount(Node, Target.SliceAxis) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "slicePosition")
        return ReadFloat(Node, Target.SlicePosition) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "obstacleType")
        return ReadCount(Node, Target.ObstacleType) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "obstacleX")
        return ReadFloat(Node, Target.ObstacleX) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "obstacleY")
        return ReadFloat(Node, Target.ObstacleY) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "obstacleZ")
        return ReadFloat(Node, Target.ObstacleZ) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "obstacleRadius")
        return ReadFloat(Node, Target.ObstacleRadius) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "colliderAutoMove")
        return ReadSwitch(Node, Target.ColliderAutoMove) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "colliderSpeed")
        return ReadFloat(Node, Target.ColliderSpeed) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "showBoundingBox")
        return ReadSwitch(Node, Target.BoundingBoxShown) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "showVoxelGridLines")
        return ReadSwitch(Node, Target.VoxelLatticeLinesShown) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "showActiveVoxelCells")
        return ReadSwitch(Node, Target.ActiveVoxelCellsShown) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "showFloorGrid")
        return ReadSwitch(Node, Target.FloorLatticeShown) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "showEmbers")
        return ReadSwitch(Node, Target.EmbersShown) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "emberCount")
        return ReadCount(Node, Target.EmberCount) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "emberSize")
        return ReadFloat(Node, Target.EmberSize) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "emberIntensity")
        return ReadFloat(Node, Target.EmberIntensity) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "emberLifetime")
        return ReadFloat(Node, Target.EmberLifetime) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "emberAshiness")
        return ReadFloat(Node, Target.EmberAshiness) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "showAtlasMinimap")
        return ReadSwitch(Node, Target.TileSheetOverviewShown) ? Crossing::Read : Crossing::WrongType;
    else if (Key == "atlasMinimapField")
        return ReadCount(Node, Target.TileSheetOverviewReading) ? Crossing::Read : Crossing::WrongType;
    return Crossing::Unknown;
}


//------------------------------------------------------------------------------------------------------------------------
//                                               CROSSING EVERY SETTING OUT                                               
//------------------------------------------------------------------------------------------------------------------------

// Sorted by name, matching the browser emitter. Not the declaration order, deliberately.
inline void SpellSettings(const GasSettings& Settings, std::string& Out) noexcept
{
    Out += "ambientIntensity = " + SpellFloat(Settings.AmbientIntensity) + "\n";
    Out += "atlasMinimapField = " + SpellCount(Settings.TileSheetOverviewReading) + "\n";
    Out += "autoGpuGovernor = "; Out += Settings.AutoGpuGovernor ? "true" : "false"; Out += "\n";
    Out += "autoTurntable = "; Out += Settings.AutoTurntable ? "true" : "false"; Out += "\n";
    Out += "blastFuel = " + SpellFloat(Settings.BlastFuel) + "\n";
    Out += "blastLobes = " + SpellCount(Settings.BlastLobes) + "\n";
    Out += "blastRadius = " + SpellFloat(Settings.BlastRadius) + "\n";
    Out += "blastSmoke = " + SpellFloat(Settings.BlastSmoke) + "\n";
    Out += "blastStrength = " + SpellFloat(Settings.BlastStrength) + "\n";
    Out += "blastTemperature = " + SpellFloat(Settings.BlastTemperature) + "\n";
    Out += "bloomIntensity = " + SpellFloat(Settings.BloomIntensity) + "\n";
    Out += "boundsHeight = " + SpellFloat(Settings.BoundsHeight) + "\n";
    Out += "boundsWidth = " + SpellFloat(Settings.BoundsWidth) + "\n";
    Out += "buoyancy = " + SpellFloat(Settings.Buoyancy) + "\n";
    Out += "burnHeat = " + SpellFloat(Settings.BurnHeat) + "\n";
    Out += "burnRate = " + SpellFloat(Settings.BurnRate) + "\n";
    Out += "colliderAutoMove = "; Out += Settings.ColliderAutoMove ? "true" : "false"; Out += "\n";
    Out += "colliderSpeed = " + SpellFloat(Settings.ColliderSpeed) + "\n";
    Out += "colorPalette = " + SpellCount(Settings.ColorPalette) + "\n";
    Out += "combustionExpansion = " + SpellFloat(Settings.CombustionExpansion) + "\n";
    Out += "coolingRate = " + SpellFloat(Settings.CoolingRate) + "\n";
    Out += "densityExtinction = " + SpellFloat(Settings.DensityExtinction) + "\n";
    Out += "dynamicBounds = "; Out += Settings.DynamicBounds ? "true" : "false"; Out += "\n";
    Out += "dynamicBoundsMax = " + SpellFloat(Settings.DynamicBoundsMax) + "\n";
    Out += "emberAshiness = " + SpellFloat(Settings.EmberAshiness) + "\n";
    Out += "emberCount = " + SpellCount(Settings.EmberCount) + "\n";
    Out += "emberIntensity = " + SpellFloat(Settings.EmberIntensity) + "\n";
    Out += "emberLifetime = " + SpellFloat(Settings.EmberLifetime) + "\n";
    Out += "emberSize = " + SpellFloat(Settings.EmberSize) + "\n";
    Out += "emitterEnabled = "; Out += Settings.EmitterEnabled ? "true" : "false"; Out += "\n";
    Out += "emitterFuel = " + SpellFloat(Settings.EmitterFuel) + "\n";
    Out += "emitterHeight = " + SpellFloat(Settings.EmitterHeight) + "\n";
    Out += "emitterRadius = " + SpellFloat(Settings.EmitterRadius) + "\n";
    Out += "emitterRate = " + SpellFloat(Settings.EmitterRate) + "\n";
    Out += "emitterSmoke = " + SpellFloat(Settings.EmitterSmoke) + "\n";
    Out += "emitterSwirl = " + SpellFloat(Settings.EmitterSwirl) + "\n";
    Out += "emitterTemperature = " + SpellFloat(Settings.EmitterTemperature) + "\n";
    Out += "emitterUpwardVelocity = " + SpellFloat(Settings.EmitterUpwardVelocity) + "\n";
    Out += "enclosedBox = "; Out += Settings.EnclosedBox ? "true" : "false"; Out += "\n";
    Out += "exposure = " + SpellFloat(Settings.Exposure) + "\n";
    Out += "fireIntensity = " + SpellFloat(Settings.FireIntensity) + "\n";
    Out += "godRaysIntensity = " + SpellFloat(Settings.GodRaysIntensity) + "\n";
    Out += "gridResolution = " + SpellCount(Settings.LatticeResolution) + "\n";
    Out += "interactionMode = \""; Out += Settings.InteractionMode; Out += "\"\n";
    Out += "internalScattering = " + SpellFloat(Settings.InternalScattering) + "\n";
    Out += "macCormackAdvection = "; Out += Settings.MacCormackAdvection ? "true" : "false"; Out += "\n";
    Out += "obstacleRadius = " + SpellFloat(Settings.ObstacleRadius) + "\n";
    Out += "obstacleType = " + SpellCount(Settings.ObstacleType) + "\n";
    Out += "obstacleX = " + SpellFloat(Settings.ObstacleX) + "\n";
    Out += "obstacleY = " + SpellFloat(Settings.ObstacleY) + "\n";
    Out += "obstacleZ = " + SpellFloat(Settings.ObstacleZ) + "\n";
    Out += "paused = "; Out += Settings.Paused ? "true" : "false"; Out += "\n";
    Out += "phaseAnisotropy = " + SpellFloat(Settings.PhaseAnisotropy) + "\n";
    Out += "pressureIterations = " + SpellCount(Settings.PressureIterations) + "\n";
    Out += "raymarchSteps = " + SpellCount(Settings.RaymarchSteps) + "\n";
    Out += "renderChannel = " + SpellCount(Settings.RenderChannel) + "\n";
    Out += "renderScale = " + SpellFloat(Settings.RenderScale) + "\n";
    Out += "shadowDensity = " + SpellFloat(Settings.ShadowDensity) + "\n";
    Out += "shadowSteps = " + SpellCount(Settings.ShadowSteps) + "\n";
    Out += "shockwaveStrength = " + SpellFloat(Settings.ShockwaveStrength) + "\n";
    Out += "showActiveVoxelCells = "; Out += Settings.ActiveVoxelCellsShown ? "true" : "false"; Out += "\n";
    Out += "showAtlasMinimap = "; Out += Settings.TileSheetOverviewShown ? "true" : "false"; Out += "\n";
    Out += "showBoundingBox = "; Out += Settings.BoundingBoxShown ? "true" : "false"; Out += "\n";
    Out += "showEmbers = "; Out += Settings.EmbersShown ? "true" : "false"; Out += "\n";
    Out += "showFloorGrid = "; Out += Settings.FloorLatticeShown ? "true" : "false"; Out += "\n";
    Out += "showVoxelGridLines = "; Out += Settings.VoxelLatticeLinesShown ? "true" : "false"; Out += "\n";
    Out += "shrapnelEnabled = "; Out += Settings.ShrapnelEnabled ? "true" : "false"; Out += "\n";
    Out += "sliceAxis = " + SpellCount(Settings.SliceAxis) + "\n";
    Out += "slicePosition = " + SpellFloat(Settings.SlicePosition) + "\n";
    Out += "smokeAlbedo = " + SpellFloat(Settings.SmokeAlbedo) + "\n";
    Out += "smokeDissipation = " + SpellFloat(Settings.SmokeDissipation) + "\n";
    Out += "smokeWeight = " + SpellFloat(Settings.SmokeWeight) + "\n";
    Out += "sootGeneration = " + SpellFloat(Settings.SootGeneration) + "\n";
    Out += "sunAzimuth = " + SpellFloat(Settings.SunAzimuth) + "\n";
    Out += "sunElevation = " + SpellFloat(Settings.SunElevation) + "\n";
    Out += "sunIntensity = " + SpellFloat(Settings.SunIntensity) + "\n";
    Out += "temperatureScale = " + SpellFloat(Settings.TemperatureScale) + "\n";
    Out += "timeScale = " + SpellFloat(Settings.TimeScale) + "\n";
    Out += "turbulenceScale = " + SpellFloat(Settings.TurbulenceScale) + "\n";
    Out += "turbulenceStrength = " + SpellFloat(Settings.TurbulenceStrength) + "\n";
    Out += "velocityDamping = " + SpellFloat(Settings.VelocityDamping) + "\n";
    Out += "vorticityConfinement = " + SpellFloat(Settings.VorticityConfinement) + "\n";
    Out += "voxelQuantization = " + SpellCount(Settings.VoxelQuantization) + "\n";
    Out += "windX = " + SpellFloat(Settings.WindX) + "\n";
    Out += "windZ = " + SpellFloat(Settings.WindZ) + "\n";
}

}   // namespace GasSceneDetail

// clang-format on
