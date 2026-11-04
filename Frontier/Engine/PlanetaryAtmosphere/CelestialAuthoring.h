/*==========================================================================================================================================
                                                            CELESTIALAUTHORING.H
==========================================================================================================================================*/
// 🧩 One conversion from authored celestial parameters to the native body's rendering descriptors.

#pragma once

#include "Engine/Scene/EngineConfigurations.h"
#include "Engine/PlanetaryAtmosphere/PlanetaryProjection.h"

namespace Frontier
{

[[nodiscard]] Vec3                    ResolveCelestialDirection(float AzimuthDegrees, float ElevationDegrees) noexcept;
[[nodiscard]] LunarBodyDescriptor     ResolveAuthoredMoon(const MoonInstanceConfiguration &Moon, float RadianceScale = 1) noexcept;
[[nodiscard]] PlanetaryBodyDescriptor ResolveAuthoredPlanet(const PlanetConfiguration &Planet, Vec3 Direction) noexcept;

} // namespace Frontier
