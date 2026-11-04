/*==========================================================================================================================================
                                                              NOTESFIELD.CPP
==========================================================================================================================================*/
// 🧩 The optional notes of a record (.entity-notes): a titled box with a Hide action over a multi-line text field.

#include "Engine/Interface/Controls/NotesField.h"

#include "Engine/Interface/Controls/ControlColumn.h"
#include "Engine/Interface/Controls/GlyphRun.h"
#include "Engine/Interface/InspectorControls.h"

#include <imgui.h>

namespace Frontier
{

namespace
{

// 📝 ImGui's resize request for a std::string-backed field.
int ResizeText(ImGuiInputTextCallbackData* Request)
{
    if (Request->EventFlag != ImGuiInputTextFlags_CallbackResize) return 0;
    auto* Text = static_cast<std::string*>(Request->UserData);
    Text->resize(static_cast<size_t>(Request->BufTextLen));
    Request->Buf = Text->data();
    return 0;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    NOTES FIELD
//------------------------------------------------------------------------------------------------------------------------

bool NotesField::Construct(ControlColumn& Column, const char* Id, std::string& Text, bool& Open)
{
    // 📝 .entity-notes: padding 10 px, radius 9 px, #191919 over a #303030 border; "Notes" 10 px, "OPTIONAL" 8 px capitals, the Hide
    //    button 22 px; the field at least 76 px, radius 6 px, #111.
    const EditorTypefaces& Faces    = Column.QueryFaces();
    ImDrawList* const      DrawList = Column.QueryDrawList();
    const float            S        = Column.QueryScale();
    const float            Width    = Column.QueryWidth();
    const ImVec2           Top      = Column.Space(10.0f + 22.0f + 8.0f + 76.0f + 10.0f, 0.0f, 14.0f);
    const ImVec2           Bottom(Top.x + Width, Top.y + 126.0f * S);
    DrawList->AddRectFilled(Top, Bottom, ResolveCssColour(0x191919FF), 9.0f * S);
    DrawList->AddRect(Top, Bottom, ResolveCssColour(0x303030FF), 9.0f * S, ImDrawFlags_None, S);
    const float HeadBaseline = Top.y + 10.0f * S + 11.0f * S + 0.35f * 10.0f * S;
    (void)GlyphRun::Construct(Column, Faces.Medium, 10.0f * S, ImVec2(Top.x + 10.0f * S, HeadBaseline), ResolveCssColour(0xC9C9C9FF), "Notes");
    const float NotesWidth = GlyphRun::Measure(Column, Faces.Medium, 10.0f * S, "Notes");
    (void)GlyphRun::Construct(Column, Faces.Regular, 8.0f * S, ImVec2(Top.x + 18.0f * S + NotesWidth, HeadBaseline), ResolveCssColour(0x6E6E6EFF),
                              "OPTIONAL", 0.8f * S);
    ImGui::PushID(Id);
    const float  HideWidth = 38.0f * S;
    const ImVec2 HideMin(Bottom.x - 10.0f * S - HideWidth, Top.y + 10.0f * S);
    ImGui::SetCursorScreenPos(HideMin);
    if (ImGui::InvisibleButton("##HideNotes", ImVec2(HideWidth, 22.0f * S))) Open = false;
    DrawList->AddRectFilled(HideMin, ImVec2(HideMin.x + HideWidth, HideMin.y + 22.0f * S),
                            ImGui::IsItemHovered() ? ResolveCssColour(0x303030FF) : ResolveCssColour(0x252525FF), 6.0f * S);
    const float HideText = GlyphRun::Measure(Column, Faces.Regular, 8.0f * S, "Hide");
    (void)GlyphRun::Construct(Column, Faces.Regular, 8.0f * S, ImVec2(HideMin.x + 0.5f * (HideWidth - HideText), HideMin.y + 14.5f * S),
                              ResolveCssColour(0x858585FF), "Hide");
    ImGui::SetCursorScreenPos(ImVec2(Top.x + 10.0f * S, Top.y + 40.0f * S));
    ImGui::PushFont(GlyphRun::ResolveFace(Faces.Regular), 10.0f * GlyphRun::EmPixelHeight * S);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f * S);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f * S, 6.0f * S));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ResolveCssColour(0x111111FF));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ResolveCssColour(0x151515FF));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ResolveCssColour(0x151515FF));
    ImGui::PushStyleColor(ImGuiCol_Text, ResolveCssColour(0xCCCCCCFF));
    if (Text.capacity() < 16) Text.reserve(16);
    const bool Changed = ImGui::InputTextMultiline("##NotesText", Text.data(), Text.capacity() + 1, ImVec2(Width - 20.0f * S, 76.0f * S),
                                                   ImGuiInputTextFlags_CallbackResize, ResizeText, &Text);
    if (!ImGui::IsItemActive() && Text.empty()) (void)GlyphRun::Construct(Column, Faces.Regular, 10.0f * S, ImVec2(Top.x + 18.0f * S, Top.y + 58.0f * S),
                                                                         ResolveCssColour(0x5C5C5CFF), "Purpose, ownership, review notes\xE2\x80\xA6");
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);
    ImGui::PopFont();
    ImGui::PopID();
    return Changed;
}

} // namespace Frontier
