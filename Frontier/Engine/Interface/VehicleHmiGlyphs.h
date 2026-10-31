#pragma once

#if defined(FRONTIER_WITH_INTERFACE_SDK)

#include <imgui.h>

#include <string_view>

namespace Frontier
{

// VehicleHMI Outliner glyph adapter: Lucide paths are tessellated to ImDrawList; custom full-colour SVGs
// use IconPresentation's ThorVG rasterizer when its texture atlas is attached. Lucide 1.57.0 path source:
// VehicleHmiPrototype/Source/EditorIcons.js (ISC License).
void DrawVehicleHmiGlyph(ImDrawList* Ink, std::string_view Name, ImVec2 Centre, float Size, ImU32 Colour,
                         float StrokeWidth = 1.6f, float Opacity = 1.0f, float RotationDegrees = 0.0f);

} // namespace Frontier

#endif // FRONTIER_WITH_INTERFACE_SDK
