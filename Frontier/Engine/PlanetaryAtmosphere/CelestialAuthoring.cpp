/*==========================================================================================================================================
                                                           CELESTIALAUTHORING.CPP
==========================================================================================================================================*/
// 🧩 Phase, placement, orientation, surface, rings and atmosphere use the same descriptors in the viewport and body preview.

#include "Engine/PlanetaryAtmosphere/CelestialAuthoring.h"
#include <algorithm>
#include <cmath>

namespace Frontier
{
namespace
{
constexpr float Radians = 3.14159265358979323846f / 180;
Vec3            Cross(Vec3 First, Vec3 Second)
{
    return {First.Y * Second.Z - First.Z * Second.Y, First.Z * Second.X - First.X * Second.Z, First.X * Second.Y - First.Y * Second.X};
}
Vec3 Normalize(Vec3 Direction)
{
    const float Length = std::hypot(Direction.X, Direction.Y, Direction.Z);
    return Length > 1e-6f ? Vec3{Direction.X / Length, Direction.Y / Length, Direction.Z / Length} : Vec3{0, 0, 1};
}
} // namespace

Vec3 ResolveCelestialDirection(float AzimuthDegrees, float ElevationDegrees) noexcept
{
    const float Azimuth = AzimuthDegrees * Radians, Elevation = ElevationDegrees * Radians;
    return {std::cos(Elevation) * std::cos(Azimuth), std::sin(Elevation), -std::cos(Elevation) * std::sin(Azimuth)};
}

LunarBodyDescriptor ResolveAuthoredMoon(const MoonInstanceConfiguration &Moon, float RadianceScale) noexcept
{
    LunarBodyDescriptor Result;
    Result.Preset               = static_cast<LunarBodyPreset>(std::min(5u, Moon.Body));
    Result.DirectionUnit        = ResolveCelestialDirection(Moon.AzimuthDegrees, Moon.ElevationDegrees);
    Result.AngularRadiusRad     = std::clamp(Moon.AngularDiameterDegrees, .1f, 179.9f) * .5f * Radians;
    Result.SpinLongitudeRad     = Moon.SpinDegrees * Radians;
    constexpr float Tilts[]     = {6.7f, 25, 3, 2, 27, 0};
    Result.LibrationLatitudeRad = -(Moon.PitchDegrees + Tilts[std::min(5u, Moon.Body)]) * Radians;
    Result.RollAngleRad         = -Moon.RollDegrees * Radians;
    Result.SurfaceBrightness    = std::clamp(Moon.Brightness, 0.0f, 5.0f) * RadianceScale;
    Result.GlowStrength         = std::clamp(Moon.Glow, 0.0f, 3.0f);
    Result.OverridePhase        = true;
    const auto  Direction       = Result.DirectionUnit;
    const auto  Right           = Normalize(Cross(std::abs(Direction.Y) < .94f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}, Direction));
    const float Phase           = Moon.PhaseDays / 29.53f * 2 * 3.14159265358979323846f;
    Result.PhaseLightDirection =
        Normalize({Direction.X * std::cos(Phase) + Right.X * std::sin(Phase), Direction.Y * std::cos(Phase) + Right.Y * std::sin(Phase),
                   Direction.Z * std::cos(Phase) + Right.Z * std::sin(Phase)});
    Result.PhaseIrradiance = {1.4f, 1.35f, 1.25f};
    return Result;
}

PlanetaryBodyDescriptor ResolveAuthoredPlanet(const PlanetConfiguration &Planet, Vec3 Direction) noexcept
{
    PlanetaryBodyDescriptor             Result;
    constexpr PlanetarySurfaceArchetype Archetypes[] = {
        PlanetarySurfaceArchetype::CloudVeiledWorld, PlanetarySurfaceArchetype::BandedGasGiant, PlanetarySurfaceArchetype::RustyDesertWorld,
        PlanetarySurfaceArchetype::BandedGasGiant};
    Result.Archetype     = Archetypes[std::min(3u, Planet.Body)];
    Result.DirectionUnit = Normalize(Direction);
    const auto  Right    = Normalize(Cross(std::abs(Direction.Y) < .94f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}, Result.DirectionUnit));
    const auto  Up       = Normalize(Cross(Result.DirectionUnit, Right));
    const float Tilt = Planet.TiltDegrees * Radians, Roll = Planet.RollDegrees * Radians;
    const auto  AxisUp = Vec3{Up.X * std::cos(Roll) + Right.X * std::sin(Roll), Up.Y * std::cos(Roll) + Right.Y * std::sin(Roll),
                             Up.Z * std::cos(Roll) + Right.Z * std::sin(Roll)};
    Result.SpinAxisUnit =
        Normalize({AxisUp.X * std::cos(Tilt) - Direction.X * std::sin(Tilt), AxisUp.Y * std::cos(Tilt) - Direction.Y * std::sin(Tilt),
                   AxisUp.Z * std::cos(Tilt) - Direction.Z * std::sin(Tilt)});
    Result.AngularRadiusRad               = std::clamp(Planet.ApparentDiameterDegrees, .05f, 30.0f) * .5f * Radians;
    Result.SpinPhaseRad                   = Planet.SpinDegrees * Radians;
    Result.SurfaceAlbedoScale             = std::clamp(Planet.Brightness, 0.0f, 5.0f);
    Result.SurfaceTint                    = {Planet.SurfaceTint[0], Planet.SurfaceTint[1], Planet.SurfaceTint[2]};
    Result.BandContrast                   = std::clamp(Planet.BandContrast, 0.0f, 2.0f);
    Result.SurfaceDetail                  = std::clamp(Planet.SurfaceDetail, 0.0f, 2.0f);
    Result.CloudVeil                      = std::clamp(Planet.CloudVeil, 0.0f, 1.0f);
    Result.Atmosphere.Enabled             = Planet.AtmosphereShell;
    Result.Atmosphere.ShellHeightFraction = std::clamp(Planet.AtmosphereHeight, .005f, .3f);
    Result.Atmosphere.LimbGlowStrength    = std::clamp(Planet.LimbGlow, 0.0f, 3.0f);
    Result.Atmosphere.RayleighTintRgb     = Planet.Body == 0   ? Vec3{1, .79f, .45f}
                                            : Planet.Body == 2 ? Vec3{.9f, .47f, .25f}
                                                               : Vec3{.42f, .68f, 1};
    Result.Rings.Enabled                  = Planet.RingSystem;
    Result.Rings.InnerRadiusRatio         = std::clamp(Planet.RingInnerRadius, 1.01f, 3.8f);
    Result.Rings.OuterRadiusRatio         = std::clamp(Planet.RingOuterRadius, Result.Rings.InnerRadiusRatio + .05f, 4.0f);
    Result.Rings.PeakOpticalDepth         = std::clamp(Planet.RingOpticalDepth, 0.0f, 4.0f);
    Result.ProceduralSeed                 = 7 + std::min(3u, Planet.Body) * 13;
    return Result;
}

} // namespace Frontier
