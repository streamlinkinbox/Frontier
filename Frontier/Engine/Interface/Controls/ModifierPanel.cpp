/*==========================================================================================================================================
                                                             MODIFIERPANEL.CPP
==========================================================================================================================================*/
// 🧩 The header of one modifier in a Blender-style modifier stack: the fold chevron, the modifier's glyph, its label and
//    summary, the viewport switch, move up, move down and remove.

#include "Engine/Interface/Controls/ModifierPanel.h"

#include "Engine/Interface/Controls/ControlColumn.h"
#include "Engine/Interface/Controls/GlyphRun.h"
#include "Engine/Interface/Controls/GlyphSymbol.h"
#include "Engine/Interface/InspectorControls.h"

#include <imgui.h>

namespace Frontier
{

namespace
{

// 📝 A 24 px round icon button; Lit draws the glyph in the ink, otherwise dimmed.
bool ConstructIconButton(ControlColumn& Column, const char* Id, ImVec2 Min, const char* Glyph, bool Lit, bool Enabled, const char* Tooltip)
{
    ImDrawList* const DrawList = Column.QueryDrawList();
    const float       S        = Column.QueryScale();
    ImGui::SetCursorScreenPos(Min);
    if (!Enabled) ImGui::BeginDisabled();
    const bool Pressed = ImGui::InvisibleButton(Id, ImVec2(24.0f * S, 24.0f * S)) && Enabled;
    if (!Enabled) ImGui::EndDisabled();
    ControlColumn::ConstructTooltip(Tooltip);
    const bool   Hovered = Enabled && ImGui::IsItemHovered();
    const ImVec2 Centre(Min.x + 12.0f * S, Min.y + 12.0f * S);
    if (Hovered) DrawList->AddCircleFilled(Centre, 12.0f * S, ResolveCssColour(0x2C2C2CFF), 24);
    const ImU32 Ink = !Enabled ? ResolveCssColour(0x4A4A4AFF) : (Lit ? ResolveCssColour(0xE2E2E2FF) : ResolveCssColour(0x6E6E6EFF));
    GlyphSymbol::Construct(DrawList, Glyph, {Min.x + 5.0f * S, Min.y + 5.0f * S}, 14.0f * S, 1.6f, Ink);
    return Pressed;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    MODIFIER PANEL
//------------------------------------------------------------------------------------------------------------------------

ModifierAction ModifierPanel::ConstructHeader(ControlColumn& Column, const char* Id, const ModifierHeader& Header)
{
    const EditorTypefaces& Faces    = Column.QueryFaces();
    ImDrawList* const      DrawList = Column.QueryDrawList();
    const float            S        = Column.QueryScale();
    const float            Width    = Column.QueryWidth();
    const ImVec2           Top      = Column.Space(34.0f, 0.0f, Header.Expanded != nullptr && *Header.Expanded ? 10.0f : 0.0f);
    const bool             Unfolded = Header.Expanded != nullptr && *Header.Expanded;
    const bool             Shown    = Header.Enabled == nullptr || *Header.Enabled;
    ModifierAction         Action   = ModifierAction::None;
    ImGui::PushID(Id);
    // ① the fold: the chevron, the seat and the label are one button
    const float FoldWidth = Width - 4.0f * 26.0f * S;
    ImGui::SetCursorScreenPos(Top);
    if (ImGui::InvisibleButton("##Fold", ImVec2(FoldWidth, 34.0f * S)) && Header.Expanded != nullptr) *Header.Expanded = !*Header.Expanded;
    const bool FoldHovered = ImGui::IsItemHovered();
    GlyphSymbol::Construct(DrawList, Unfolded ? "ChevronDown" : "ChevronRight", {Top.x, Top.y + 10.0f * S}, 14.0f * S, 1.8f,
                           FoldHovered ? ResolveCssColour(0xE0E0E0FF) : ResolveCssColour(0x9A9A9AFF));
    const ImVec2 SeatMin(Top.x + 20.0f * S, Top.y + 4.0f * S);
    DrawList->AddRectFilled(SeatMin, ImVec2(SeatMin.x + 26.0f * S, SeatMin.y + 26.0f * S), Shown ? ResolveCssColour(0x6C77FF26) : ResolveCssColour(0xFFFFFF0A),
                            9.0f * S);
    DrawList->AddRect(SeatMin, ImVec2(SeatMin.x + 26.0f * S, SeatMin.y + 26.0f * S), Shown ? ResolveCssColour(0x6C77FF40) : ResolveCssColour(0xFFFFFF12), 9.0f * S,
                      ImDrawFlags_None, S);
    GlyphSymbol::Construct(DrawList, Header.Glyph, {SeatMin.x + 6.0f * S, SeatMin.y + 6.0f * S}, 14.0f * S, 1.6f,
                           Shown ? ResolveCssColour(0xAAB2FFFF) : ResolveCssColour(0x7A7A7AFF));
    const float LabelLeft = SeatMin.x + 36.0f * S;
    (void)GlyphRun::Construct(Column, Faces.Regular, 12.0f * S, ImVec2(LabelLeft, Top.y + 21.0f * S),
                              Shown ? ResolveCssColour(0xE6E6E6FF) : ResolveCssColour(0x8C8C8CFF), Header.Label);
    if (Header.Summary != nullptr)
    {
        const float LabelWidth = GlyphRun::Measure(Column, Faces.Regular, 12.0f * S, Header.Label);
        (void)GlyphRun::Construct(Column, Faces.Regular, 9.0f * S, ImVec2(LabelLeft + LabelWidth + 9.0f * S, Top.y + 20.5f * S), ResolveCssColour(0x7C7C7CFF),
                                  Header.Summary, 0.3f * S);
    }
    // ② the viewport switch, move up, move down and remove, right to left
    const float Right = Top.x + Width;
    if (Header.Enabled != nullptr &&
        ConstructIconButton(Column, "##Viewport", ImVec2(Right - 4.0f * 26.0f * S + 2.0f * S, Top.y + 5.0f * S), "Monitor", Shown, !Header.Locked,
                            Shown ? "Shown in the viewport: press to skip this modifier" : "Skipped: press to show this modifier in the viewport"))
        *Header.Enabled = !*Header.Enabled;
    if (ConstructIconButton(Column, "##MoveUp", ImVec2(Right - 3.0f * 26.0f * S + 2.0f * S, Top.y + 5.0f * S), "ChevronUp", true, !Header.Locked && !Header.First,
                            "Move up the stack"))
        Action = ModifierAction::MoveUp;
    if (ConstructIconButton(Column, "##MoveDown", ImVec2(Right - 2.0f * 26.0f * S + 2.0f * S, Top.y + 5.0f * S), "ChevronDown", true,
                            !Header.Locked && !Header.Last, "Move down the stack"))
        Action = ModifierAction::MoveDown;
    if (ConstructIconButton(Column, "##Remove", ImVec2(Right - 26.0f * S + 2.0f * S, Top.y + 5.0f * S), "X", true, !Header.Locked, "Remove the modifier"))
        Action = ModifierAction::Remove;
    ImGui::PopID();
    return Action;
}

} // namespace Frontier
