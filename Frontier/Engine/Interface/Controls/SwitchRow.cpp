/*==========================================================================================================================================
                                                               SWITCHROW.CPP
==========================================================================================================================================*/
// 🧩 Celestial toggle geometry and pointer/keyboard ownership.

#include "Engine/Interface/Controls/SwitchRow.h"
#include "Engine/Interface/Controls/ControlColumn.h"
#include "Engine/Interface/Controls/GlyphRun.h"
#include "Engine/Interface/InspectorControls.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    SWITCH ROW
//------------------------------------------------------------------------------------------------------------------------

bool SwitchRow::Construct(ControlColumn &Column, const char *Id, const char *Caption, bool &Enabled)
{
    const float  Scale = Column.QueryScale();
    const ImVec2 Top   = Column.Space(42.0f);
    const float  Right = Top.x + Column.QueryWidth();
    auto        *Ink   = Column.QueryDrawList();
    ImGui::SetCursorScreenPos(Top);
    const bool Changed = ImGui::InvisibleButton(Id, ImVec2(Column.QueryWidth(), 42.0f * Scale), ImGuiButtonFlags_EnableNav);
    if (Changed) Enabled = !Enabled;
    Ink->AddLine(Top, ImVec2(Right, Top.y), ResolveCssColour(0xB5C7E318), Scale);
    (void)GlyphRun::Construct(Column, Column.QueryFaces().Regular, 10.0f * Scale,
                              ImVec2(Top.x, GlyphRun::ResolveBaseline(Top.y + 13.0f * Scale, 10.0f * Scale, 1.4f)),
                              ResolveCssColour(0xB8C4D4FF), Caption);
    const ImVec2 Switch(Right - 29.0f * Scale, Top.y + 13.0f * Scale);
    Ink->AddRectFilled(Switch, ImVec2(Right, Switch.y + 16.0f * Scale), ResolveCssColour(Enabled ? 0xB7C9E1FF : 0x3F4854FF), 8.0f * Scale);
    Ink->AddCircleFilled(ImVec2(Switch.x + (Enabled ? 21.0f : 8.0f) * Scale, Switch.y + 8.0f * Scale), 5.0f * Scale,
                         ResolveCssColour(Enabled ? 0x344052FF : 0x9BA8BAFF));
    return Changed;
}

} // namespace Frontier
