/*==========================================================================================================================================
                                                               AXISFIELDS.CPP
==========================================================================================================================================*/
// 🧩 The native demo's horizontal X/Y/Z number inputs, with explicit finite checks before changing authoring coordinates.

#include "Engine/Interface/Controls/AxisFields.h"
#include "Engine/Interface/Controls/ControlColumn.h"
#include "Engine/Interface/Controls/GlyphRun.h"
#include "Engine/Interface/InspectorControls.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    AXIS FIELDS
//------------------------------------------------------------------------------------------------------------------------

bool AxisFields::Construct(ControlColumn &Column,
                           const char    *Id,
                           const char    *Caption,
                           float (&Coordinates)[3],
                           const std::array<float, 3> &Minimum,
                           const std::array<float, 3> &Maximum)
{
    const float Scale  = Column.QueryScale();
    const auto &Faces  = Column.QueryFaces();
    const auto  Origin = Column.Space(59.4f, 7.6f, 0);
    GlyphRun::Construct(Column, Faces.Regular, 11 * Scale, {Origin.x, GlyphRun::ResolveBaseline(Origin.y, 11 * Scale, 1.4f)},
                        Column.QueryTheme().Muted, Caption);
    const float Width   = (Column.QueryWidth() - 12 * Scale) / 3;
    bool        Changed = false;
    ImGui::PushID(Id);
    ImGui::PushFont(GlyphRun::ResolveFace(Faces.Regular), 11 * GlyphRun::EmPixelHeight * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {7 * Scale, (32 - 11 * GlyphRun::EmPixelHeight) * Scale / 2});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10 * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, Scale);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ResolveCssColour(0x282828FF));
    ImGui::PushStyleColor(ImGuiCol_Border, ResolveCssColour(0x3E3E3EFF));
    ImGui::PushStyleColor(ImGuiCol_Text, ResolveCssColour(0xE9E9E9FF));
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        ImGui::PushID(Axis);
        ImGui::SetCursorScreenPos({Origin.x + Axis * (Width + 6 * Scale), Origin.y + 27.4f * Scale});
        ImGui::SetNextItemWidth(Width);
        float Coordinate = Coordinates[Axis];
        if (ImGui::InputFloat("##Coordinate", &Coordinate, 0, 0, "%.0f") && std::isfinite(Coordinate))
        {
            Coordinate = std::clamp(Coordinate, Minimum[Axis], Maximum[Axis]);
            Changed |= Coordinate != Coordinates[Axis];
            Coordinates[Axis] = Coordinate;
        }
        ImGui::PopID();
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(3);
    ImGui::PopFont();
    ImGui::PopID();
    return Changed;
}

} // namespace Frontier
