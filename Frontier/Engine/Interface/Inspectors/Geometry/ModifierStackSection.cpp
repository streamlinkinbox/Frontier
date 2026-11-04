/*==========================================================================================================================================
                                                         MODIFIERSTACKSECTION.CPP
==========================================================================================================================================*/
// 🧩 The geometry inspector's modifier stack, after Blender's: Add modifier, one card per Array modifier with its fold,
//    viewport switch, order and removal, and what the stack makes against the placements the scene archive holds.

#include "Engine/Interface/Inspectors/Geometry/GeometrySections.h"

#include "Engine/Interface/Controls/CardTitleRow.h"
#include "Engine/Interface/Controls/GlyphSymbol.h"
#include "Engine/Interface/Controls/ModifierPanel.h"
#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/Controls/TransformTable.h"
#include "Engine/Interface/InspectorControls.h"
#include "Engine/Scene/GeometryLayout.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace Frontier
{

namespace
{

// 📝 A modifier card: a shade darker than the generic card, so the stack reads as panels of their own.
CardLook ResolveModifierCardLook()
{
    CardLook Look      = ResolveGenericCardLook();
    Look.Fill          = ResolveCssColour(0x171717FF);
    Look.Radius        = 18.0f;
    Look.PaddingTop    = 8.0f;
    Look.PaddingSide   = 12.0f;
    Look.PaddingBottom = 10.0f;
    Look.MarginBottom  = 10.0f;
    return Look;
}

// 📝 Blender's names for a second and third modifier of a kind: "Array", "Array.001", "Array.002".
std::string ResolveModifierLabel(const std::vector<ArrayModifierConfiguration>& Stack)
{
    for (int Ordinal = 0;; ++Ordinal)
    {
        char Label[24];
        if (Ordinal == 0) std::snprintf(Label, sizeof(Label), "Array");
        else std::snprintf(Label, sizeof(Label), "Array.%03d", Ordinal);
        if (std::none_of(Stack.begin(), Stack.end(), [&](const ArrayModifierConfiguration& Modifier) { return Modifier.Label == Label; })) return Label;
    }
}

// 📝 A read-only setting row: the label at the left, its reading at the right (Blender's Fit Type, fixed here).
void ConstructReadingRow(ControlColumn& Page, const char* Label, const char* Reading, const char* Tooltip)
{
    const EditorTypefaces& Faces = Page.QueryFaces();
    const float            S     = Page.QueryScale();
    const ImVec2           Top   = Page.Space(26.0f, 2.0f, 6.0f);
    ImGui::SetCursorScreenPos(Top);
    ImGui::Dummy(ImVec2(Page.QueryWidth(), 26.0f * S));
    ControlColumn::ConstructTooltip(Tooltip);
    (void)GlyphRun::Construct(Page, Faces.Regular, 11.0f * S, ImVec2(Top.x, Top.y + 17.0f * S), Page.QueryTheme().ControlLabel, Label);
    const float Advance = GlyphRun::Measure(Page, Faces.Regular, 11.0f * S, Reading);
    (void)GlyphRun::Construct(Page, Faces.Regular, 11.0f * S, ImVec2(Top.x + Page.QueryWidth() - Advance, Top.y + 17.0f * S), Page.QueryTheme().Ink, Reading);
}

// 📝 Add modifier: a full-width button opening the menu of modifier kinds (only Generate › Array exists here). True on a pick.
bool ConstructAddModifier(ControlColumn& Page, bool Enabled)
{
    const EditorTypefaces& Faces    = Page.QueryFaces();
    ImDrawList* const      DrawList = Page.QueryDrawList();
    const float            S        = Page.QueryScale();
    const float            Width    = Page.QueryWidth();
    const ImVec2           Top      = Page.Space(36.0f, 0.0f, 12.0f);
    ImGui::SetCursorScreenPos(Top);
    if (!Enabled) ImGui::BeginDisabled();
    if (ImGui::InvisibleButton("##AddModifier", ImVec2(Width, 36.0f * S)) && Enabled) ImGui::OpenPopup("##ModifierMenu");
    if (!Enabled) ImGui::EndDisabled();
    ControlColumn::ConstructTooltip(Enabled ? "Add a modifier to the end of the stack" : "The object is locked");
    const bool  Hovered = Enabled && ImGui::IsItemHovered();
    const float Fade    = Enabled ? 1.0f : 0.45f;
    DrawList->AddRectFilled(Top, ImVec2(Top.x + Width, Top.y + 36.0f * S), FadeColour(Hovered ? ResolveCssColour(0x262626FF) : ResolveCssColour(0x1D1D1DFF), Fade),
                            12.0f * S);
    DrawList->AddRect(Top, ImVec2(Top.x + Width, Top.y + 36.0f * S), FadeColour(ResolveCssColour(0xFFFFFF14), Fade), 12.0f * S, ImDrawFlags_None, S);
    const float LabelWidth = GlyphRun::Measure(Page, Faces.Regular, 11.0f * S, "Add modifier");
    const float Left       = Top.x + 0.5f * (Width - LabelWidth - 20.0f * S);
    GlyphSymbol::Construct(DrawList, "Plus", {Left, Top.y + 11.0f * S}, 14.0f * S, 1.7f, FadeColour(ResolveCssColour(0xD6D6D6FF), Fade));
    (void)GlyphRun::Construct(Page, Faces.Regular, 11.0f * S, ImVec2(Left + 20.0f * S, Top.y + 22.5f * S), FadeColour(ResolveCssColour(0xD6D6D6FF), Fade),
                              "Add modifier");
    GlyphSymbol::Construct(DrawList, "ChevronDown", {Top.x + Width - 26.0f * S, Top.y + 11.0f * S}, 14.0f * S, 1.6f, FadeColour(ResolveCssColour(0x8A8A8AFF), Fade));

    bool Picked = false;
    ImGui::SetNextWindowPos(ImVec2(Top.x, Top.y + 40.0f * S));
    ImGui::SetNextWindowSize(ImVec2(Width, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 12.0f * S);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * S, 10.0f * S));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ResolveCssColour(0x1A1A1AFF));
    ImGui::PushStyleColor(ImGuiCol_Border, ResolveCssColour(0xFFFFFF14));
    if (ImGui::BeginPopup("##ModifierMenu"))
    {
        ImDrawList* const Menu = ImGui::GetWindowDrawList();
        const ImVec2      Head = ImGui::GetCursorScreenPos();
        (void)GlyphRun::Rasterize(Menu, &Faces, Faces.Regular, 8.0f * S, ImVec2(Head.x + 2.0f * S, Head.y + 9.0f * S), ResolveCssColour(0x777777FF), "GENERATE",
                                  1.2f * S, false);
        ImGui::Dummy(ImVec2(1.0f, 16.0f * S));
        const ImVec2 Row = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##PickArray", ImVec2(Width - 20.0f * S, 40.0f * S))) Picked = true;
        if (ImGui::IsItemHovered()) Menu->AddRectFilled(Row, ImVec2(Row.x + Width - 20.0f * S, Row.y + 40.0f * S), ResolveCssColour(0xFFFFFF0A), 10.0f * S);
        GlyphSymbol::Construct(Menu, "Copy", {Row.x + 10.0f * S, Row.y + 13.0f * S}, 14.0f * S, 1.6f, ResolveCssColour(0xAAB2FFFF));
        (void)GlyphRun::Rasterize(Menu, &Faces, Faces.Regular, 11.0f * S, ImVec2(Row.x + 34.0f * S, Row.y + 17.0f * S), ResolveCssColour(0xE6E6E6FF), "Array", 0.0f, false);
        (void)GlyphRun::Rasterize(Menu, &Faces, Faces.Regular, 9.0f * S, ImVec2(Row.x + 34.0f * S, Row.y + 31.0f * S), ResolveCssColour(0x7A7A7AFF),
                                  "Copies of everything above it, stepped by an offset", 0.0f, false);
        if (Picked) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    return Picked;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    MODIFIER STACK
//------------------------------------------------------------------------------------------------------------------------

bool ModifierStackSection::Construct(ControlColumn& Page, GeometryConfiguration& Parameters)
{
    auto&       Stack    = Parameters.Modifiers;
    const auto& Resolved = Parameters.Resolved;
    const bool  Locked   = Parameters.Locked;
    bool        Changed  = false;
    char        Text[320];

    // ① the stack's heading and Add modifier
    std::snprintf(Text, sizeof(Text), "STACK \xC2\xB7 %zu", Stack.size());
    CardTitleLook Title;
    Title.Glyph        = "Wrench";
    Title.Caption      = Text;
    Title.MarginBottom = 10.0f;
    ImGui::PushID("ModifierStack");
    (void)CardTitleRow::Construct(Page, "ModifierTitle", "Modifiers", Title);
    if (ConstructAddModifier(Page, !Locked))
    {
        ArrayModifierConfiguration Added;
        Added.Label = ResolveModifierLabel(Stack);
        Stack.push_back(std::move(Added));
        Changed = true;
    }

    // ② one card per modifier, top to bottom
    const std::array<double, 3> Extent = {Resolved.Extent[0], Resolved.Extent[1], Resolved.Extent[2]};
    int                         MoveFrom = -1, MoveTo = -1, RemoveAt = -1;
    for (size_t Index = 0; Index < Stack.size(); ++Index)
    {
        ArrayModifierConfiguration& Modifier = Stack[Index];
        ImGui::PushID(static_cast<int>(Index));
        Page.BeginCard(ResolveModifierCardLook(), nullptr, nullptr);
        const std::array<double, 3> Step   = ResolveArrayStep(Modifier, Extent);
        const double                Length = std::sqrt(Step[0] * Step[0] + Step[1] * Step[1] + Step[2] * Step[2]);
        char                        Summary[64];
        if (Modifier.RotationStepDegrees != 0.0f && Length < 1.0e-6)
            std::snprintf(Summary, sizeof(Summary), "\xC3\x97%u \xC2\xB7 %.1f\xC2\xB0", Modifier.Count, Modifier.RotationStepDegrees);
        else std::snprintf(Summary, sizeof(Summary), "\xC3\x97%u \xC2\xB7 %.2f m", Modifier.Count, Length);
        const bool     WasExpanded = Modifier.Expanded, WasEnabled = Modifier.Enabled;
        ModifierHeader Header;
        Header.Label    = Modifier.Label.c_str();
        Header.Summary  = Summary;
        Header.Glyph    = "Copy";
        Header.Expanded = &Modifier.Expanded;
        Header.Enabled  = &Modifier.Enabled;
        Header.First    = Index == 0;
        Header.Last     = Index + 1 == Stack.size();
        Header.Locked   = Locked;
        switch (ModifierPanel::ConstructHeader(Page, "Header", Header))
        {
            case ModifierAction::MoveUp:   MoveFrom = static_cast<int>(Index); MoveTo = static_cast<int>(Index) - 1; break;
            case ModifierAction::MoveDown: MoveFrom = static_cast<int>(Index); MoveTo = static_cast<int>(Index) + 1; break;
            case ModifierAction::Remove:   RemoveAt = static_cast<int>(Index); break;
            case ModifierAction::None:     break;
        }
        Changed |= Modifier.Expanded != WasExpanded || Modifier.Enabled != WasEnabled;
        if (Modifier.Expanded)
        {
            ConstructReadingRow(Page, "Fit type", "Fixed count", "Blender's Fit Length and Fit Curve are not offered");
            Changed |= SliderRow::ConstructInteger(Page, "Count", "Count", Modifier.Count, 1u, 512u, "copies", !Locked);
            bool Relative = Modifier.RelativeOffset;
            if (SwitchRow::Construct(Page, "Relative", "Relative offset", Relative) && !Locked)
            {
                Modifier.RelativeOffset = Relative;
                Changed                 = true;
            }
            if (Modifier.RelativeOffset)
            {
                const TransformTableRow Factor{"Factor", "\xC3\x97 extent", Modifier.RelativeFactor, -1000.0f, 1000.0f, 0.01f, "%.2f", {1.0f, 0.0f, 0.0f}};
                Changed |= TransformTable::Construct(Page, "RelativeFactor", &Factor, 1, !Locked, true);
            }
            bool Constant = Modifier.ConstantOffset;
            if (SwitchRow::Construct(Page, "Constant", "Constant offset", Constant) && !Locked)
            {
                Modifier.ConstantOffset = Constant;
                Changed                 = true;
            }
            if (Modifier.ConstantOffset)
            {
                const TransformTableRow Distance{"Distance", "m", Modifier.ConstantMetres, -10000.0f, 10000.0f, 0.01f, "%.2f", {1.0f, 0.0f, 0.0f}};
                Changed |= TransformTable::Construct(Page, "ConstantDistance", &Distance, 1, !Locked, true);
            }
            Changed |= SliderRow::Construct(Page, "Rotation", "Rotation step", Modifier.RotationStepDegrees, -180.0f, 180.0f, 0.1f, "\xC2\xB0", 1.0f, !Locked,
                                            "Turn about the object's Z axis added by each copy (Blender's Object Offset, about Z)");
            static const char* const Pivots[2] = {"Each copy", "Object origin"};
            int                      Pivot     = Modifier.RadialPivot ? 1 : 0;
            if (ChoiceRow::Construct(Page, "Pivot", "Turn about", Pivots, 2, Pivot, !Locked))
            {
                Modifier.RadialPivot = Pivot == 1;
                Changed              = true;
            }
            const double Span = Length * static_cast<double>(std::max(1u, Modifier.Count) - 1u);
            if (Modifier.RotationStepDegrees != 0.0f)
                std::snprintf(Text, sizeof(Text), "%u copies, each %.2f m on from the last and turned %.2f\xC2\xB0 about %s (%.1f\xC2\xB0 in all).",
                              Modifier.Count, Length, Modifier.RotationStepDegrees, Modifier.RadialPivot ? "the object's origin" : "its own origin",
                              Modifier.RotationStepDegrees * static_cast<float>(std::max(1u, Modifier.Count) - 1u));
            else
                std::snprintf(Text, sizeof(Text), "%u copies, each %.2f m on from the last: %.2f m from the first to the last.", Modifier.Count, Length, Span);
            CaptionText::Construct(Page, Text, 2.0f, 0.0f);
        }
        Page.EndCard();
        ImGui::PopID();
    }
    if (MoveFrom >= 0 && MoveTo >= 0 && MoveTo < static_cast<int>(Stack.size()))
    {
        std::swap(Stack[static_cast<size_t>(MoveFrom)], Stack[static_cast<size_t>(MoveTo)]);
        Changed = true;
    }
    if (RemoveAt >= 0)
    {
        Stack.erase(Stack.begin() + RemoveAt);
        Changed = true;
    }

    // ③ what the stack makes against what the archive holds
    if (!Resolved.Bound)
    {
        CaptionText::Construct(Page, Resolved.Diagnostic.empty() ? "Waiting for the scene archive: the viewport binds the object once it is up."
                                                                 : Resolved.Diagnostic.c_str(), 0.0f, 16.0f);
    }
    else
    {
        const size_t Bases  = Parameters.BaseCopies == 0 ? Resolved.Layout : std::min<size_t>(Parameters.BaseCopies, Resolved.Layout);
        const size_t Copies = CountModifierCopies(Bases, Stack);
        // 📝 Copies past the archive's placements come from the reserve (the scene archive's former stress pool) while it lasts.
        const size_t Drawable = std::min<size_t>(Copies, Resolved.Authored + Resolved.Reserved + Resolved.ReserveFree / std::max<uint32_t>(1u, Resolved.Parts));
        const size_t Shown    = Parameters.Visible ? Drawable : 0;
        // 📝 A shape's facet set is named "Shape <Name>"; the caption names the shape.
        std::string ShapeName = Resolved.Shapes.empty() ? std::string("engine shape") : Resolved.Shapes.front();
        if (ShapeName.rfind("Shape ", 0) == 0) ShapeName = ShapeName.substr(6);
        if (Stack.empty() && Resolved.Shape)
            std::snprintf(Text, sizeof(Text), "No modifiers: one %s, laid out from the reserve (%u occupants free). Add an Array to repeat it.",
                          ShapeName.c_str(), Resolved.ReserveFree);
        else if (Stack.empty() && Resolved.Authored > 0)
            std::snprintf(Text, sizeof(Text), "No modifiers: the object shows the scene archive's %u placement%s. Add an Array to repeat it.", Resolved.Authored,
                          Resolved.Authored == 1 ? "" : "s");
        else if (Stack.empty())
            std::snprintf(Text, sizeof(Text), "No modifiers: %zu cop%s of the archive's layout, laid out from the reserve.", Copies, Copies == 1 ? "y" : "ies");
        else if (Resolved.Authored > 0)
            std::snprintf(Text, sizeof(Text), "The stack makes %zu cop%s of %zu base cop%s; the scene archive holds %u, the reserve lays out the other %zu, and the "
                                              "viewport shows %zu.",
                          Copies, Copies == 1 ? "y" : "ies", Bases, Bases == 1 ? "y" : "ies", Resolved.Authored,
                          Copies > Resolved.Authored ? std::min<size_t>(Copies, Drawable) - std::min<size_t>(Copies, Resolved.Authored) : 0u, Shown);
        else
            std::snprintf(Text, sizeof(Text), "The stack makes %zu cop%s of %zu base cop%s, laid out from the reserve; the viewport shows %zu.", Copies,
                          Copies == 1 ? "y" : "ies", Bases, Bases == 1 ? "y" : "ies", Shown);
        CaptionText::Construct(Page, Text, 0.0f, Copies > Drawable ? 4.0f : 16.0f);
        if (Copies > Drawable)
        {
            std::snprintf(Text, sizeof(Text), "%zu cop%s not drawn: the reserve is in use (%u occupants free). Remove copies elsewhere to free some.",
                          Copies - Drawable, Copies - Drawable == 1 ? "y is" : "ies are", Resolved.ReserveFree);
            const float  S      = Page.QueryScale();
            const float  Height = ParagraphBlock::Construct(Page, Page.QueryFaces().Regular, 10.0f * S, 1.6f, Page.QueryCursor(), Page.QueryWidth(),
                                                            ResolveCautionInk(), Text, true) / S;
            const ImVec2 Top    = Page.Space(Height, 0.0f, 16.0f);
            (void)ParagraphBlock::Construct(Page, Page.QueryFaces().Regular, 10.0f * S, 1.6f, Top, Page.QueryWidth(), ResolveCautionInk(), Text);
        }
    }
    ImGui::PopID();
    return Changed;
}

} // namespace Frontier
