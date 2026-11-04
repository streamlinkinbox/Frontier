/*==========================================================================================================================================
                                                             DISCLOSUREROW.CPP
==========================================================================================================================================*/
// 🧩 Details/summary interaction for the layer-stack and neighbouring-body drawers.

#include "Engine/Interface/Controls/DisclosureRow.h"
#include "Engine/Interface/Controls/ControlColumn.h"
#include "Engine/Interface/Controls/GlyphRun.h"
#include "Engine/Interface/InspectorControls.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    DISCLOSURE ROW
//------------------------------------------------------------------------------------------------------------------------

bool DisclosureRow::Construct(ControlColumn &Column, const char *Id, const char *Caption, bool &Expanded)
{
    const float  Scale = Column.QueryScale();
    const ImVec2 Top   = Column.Space(38.0f, 0.0f, 12.0f);
    ImGui::SetCursorScreenPos(Top);
    if (ImGui::InvisibleButton(Id, ImVec2(Column.QueryWidth(), 38.0f * Scale), ImGuiButtonFlags_EnableNav))
    {
        Expanded = !Expanded;
    }
    const auto &Faces    = Column.QueryFaces();
    const float Baseline = GlyphRun::ResolveBaseline(Top.y + 12.0f * Scale, 10.0f * Scale, 1.4f);
    (void)GlyphRun::Construct(Column, Faces.Regular, 10.0f * Scale, ImVec2(Top.x + 4.0f * Scale, Baseline), Column.QueryTheme().Muted,
                              Expanded ? "▾" : "▸");
    (void)GlyphRun::Construct(Column, Faces.Regular, 10.0f * Scale, ImVec2(Top.x + 18.0f * Scale, Baseline), Column.QueryTheme().Ink,
                              Caption);
    return Expanded;
}

} // namespace Frontier
