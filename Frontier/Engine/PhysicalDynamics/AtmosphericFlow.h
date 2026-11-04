/*==========================================================================================================================================
                                                              ATMOSPHERICFLOW.H
==========================================================================================================================================*/
// 🧩 Scene wind coefficients feeding the existing atmospheric force field, with explicit Z-up/Y-up conversion.

#pragma once

#include "Engine/PhysicalDynamics/ForceField.h"
#include "Engine/Scene/EngineConfigurations.h"
#include <array>
#include <span>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    SCENE FLOW
//------------------------------------------------------------------------------------------------------------------------

// 📝 Sources are the level's ForceSource records (scene axes), on the Force Field's timeline clock (Controls.TimelineSeconds);
//    their flows join the field the renderer and the editor read. ElapsedSeconds drives the wind.
void                                ConfigureAtmosphericFlow(ForceFieldSpace                          &Field,
                                                             const ForceFieldConfiguration            &Controls,
                                                             const WindConfiguration                  &Wind,
                                                             double                                    ElapsedSeconds,
                                                             std::span<const ForceSourceConfiguration> Sources = {});
[[nodiscard]] std::array<double, 3> EvaluateWorldFlow(const ForceFieldSpace       &Field,
                                                      const std::array<double, 3> &Position,
                                                      bool                         Turbulence = true);

} // namespace Frontier
