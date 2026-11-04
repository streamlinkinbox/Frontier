/*==========================================================================================================================================
                                                            WORLDVECTOROVERLAY.H
==========================================================================================================================================*/
// 🧩 Perspective projection of real 3D vector samples onto a viewport; an explicitly X-ray diagnostic, not a force simulation.

#pragma once

#include "Engine/Scene/EnvironmentDiagnostics.h"
#include <imgui.h>
#include <span>

namespace Frontier
{

struct WorldVectorView
{
    std::array<double, 3> Eye{};
    std::array<double, 3> Forward{1, 0, 0};
    std::array<double, 3> Right{0, -1, 0};
    std::array<double, 3> Up{0, 0, 1};
    double                VerticalFieldOfView = 1.0471975511965976;
};

class WorldVectorOverlay
{
  public:
    static size_t Construct(ImDrawList                        *Ink,
                            ImVec2                             Minimum,
                            ImVec2                             Extent,
                            const WorldVectorView             &View,
                            std::span<const FieldVectorSample> Samples,
                            double                             Seconds,
                            float                              InterfaceScale = 1);
};

} // namespace Frontier
