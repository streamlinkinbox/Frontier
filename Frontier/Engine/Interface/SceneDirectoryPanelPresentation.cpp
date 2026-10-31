/*==========================================================================================================================================
                                                NATIVE VEHICLEHMI OUTLINER PRESENTATION
==========================================================================================================================================*/
// Port of Experimental/VehicleHmiPrototype/Source/EditorShell.js and Style/FrontierEditor.css.
// The model stays record-backed; the Outliner geometry, row states, interactions and Lucide glyphs follow the source.

#include "SceneDirectoryPanel.h"

#if defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/DropdownControls.h"
#include "Engine/Interface/StackRailControls.h"
#include "Engine/Interface/VehicleHmiGlyphs.h"
#include "Engine/Scene/SceneDirectory.h"
#include "SceneDirectoryPanelRegistration.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace Frontier
{

namespace
{

ImU32 ParseHexColourOrFallback(const std::string& Hex, RecordClassification Classification) noexcept
{
    if (Hex.size() == 7 && Hex[0] == '#')
    {
        char* End = nullptr;
        const unsigned long Value = std::strtoul(Hex.c_str() + 1, &End, 16);
        if (End == Hex.c_str() + 7)
        {
            return KitInkHex(static_cast<int>(Value));
        }
    }
    switch (Classification)
    {
        case RecordClassification::Vehicle:      return KitInkHex(0xe1d34a);
        case RecordClassification::Tyre:         return KitInkHex(0xe1d34a);
        case RecordClassification::Environment:  return KitInkHex(0x99aafa);
        case RecordClassification::Geometry:     return KitInkHex(0xb494f4);
        case RecordClassification::Illumination: return KitInkHex(0x69dfa5);
        case RecordClassification::Camera:       return KitInkHex(0x9fb4d6);
        case RecordClassification::Physics:      return KitInkHex(0xe8833a);
        case RecordClassification::Interface:    return KitInkHex(0xa8bbeb);
        case RecordClassification::Content:      return KitInkHex(0xa8bbeb);
        case RecordClassification::Unclassified: return KitInkHex(0x9fb4d6);
        case RecordClassification::Count:        break;
    }
    return KitInkHex(0x9fb4d6);
}

bool IsDarkWorkspaceTheme(const ImGuiStyle& Style) noexcept
{
    const ImVec4 Background = Style.Colors[ImGuiCol_WindowBg];
    return (0.2126f * Background.x + 0.7152f * Background.y + 0.0722f * Background.z) < 0.35f;
}

void DrawTrackedText(ImDrawList* Ink, ImFont* Font, float Size, ImVec2 Position, ImU32 Colour,
                     const char* Text, float Tracking)
{
    KitDrawTracked(Ink, Font, Size, Position, Colour, Text, Tracking);
}

void DrawRoundSurface(ImDrawList* Ink, ImVec2 Minimum, ImVec2 Maximum, ImU32 Fill, ImU32 Border, float Radius)
{
    Ink->AddRectFilled(Minimum, Maximum, Fill, Radius);
    if ((Border >> 24) != 0)
    {
        Ink->AddRect(Minimum, Maximum, Border, Radius, ImDrawFlags_None, 1.0f);
    }
}

} // namespace

DockPanelSpecification MakeSceneDirectoryPanelSpecification(SceneDirectoryPanel& Panel)
{
    DockPanelSpecification Specification;
    Specification.Key              = "Outliner";
    Specification.Caption          = "Outliner";
    Specification.ConstructContent = [&Panel]() { Panel.ConstructInterface(); };
    Specification.Open             = true;
    return Specification;
}

void SceneDirectoryPanel::ConstructInterface()
{
    SceneDirectory&       Directory = SceneDirectory::Instance();
    const KitFaces        Faces     = ResolveKitFaces();
    const ImGuiStyle&     Style     = ImGui::GetStyle();
    const bool            DarkHmi   = IsDarkWorkspaceTheme(Style);
    static std::array<char, 128> FilterBuffer{};
    static std::string          FilterSynchronised;

    const ImVec2 WindowPosition = ImGui::GetWindowPos();
    const ImVec2 WindowSize     = ImGui::GetWindowSize();
    const ImVec2 CursorTop      = ImGui::GetCursorScreenPos();
    const float  PanelLeft      = WindowPosition.x + 14.0f;
    const float  PanelRight     = WindowPosition.x + WindowSize.x - 14.0f;
    const float  PanelWidth     = std::max(120.0f, PanelRight - PanelLeft);
    const float  BodyTop        = CursorTop.y + 15.0f; // Window padding + 15 px = CSS .outliner-body 22 px
    ImDrawList*  Ink            = ImGui::GetWindowDrawList();

    // Source tokens: --column #161616, --edge #2a2a2a, --text #dcdcdc, --on #3ec46d.
    const ImU32 PanelBackground = DarkHmi ? KitInkHex(0x161616) : ImGui::ColorConvertFloat4ToU32(Style.Colors[ImGuiCol_WindowBg]);
    const ImU32 TileBackground  = DarkHmi ? KitInkHex(0x1c1c1c) : ImGui::ColorConvertFloat4ToU32(Style.Colors[ImGuiCol_FrameBg]);
    const ImU32 TileBorder      = DarkHmi ? KitInkHex(0x2c2c2c) : ImGui::ColorConvertFloat4ToU32(Style.Colors[ImGuiCol_Border]);
    const ImU32 SearchBackground= DarkHmi ? KitInkHex(0x1d1d1d) : ImGui::ColorConvertFloat4ToU32(Style.Colors[ImGuiCol_FrameBg]);
    const ImU32 SearchBorder    = DarkHmi ? KitInkHex(0x282828) : ImGui::ColorConvertFloat4ToU32(Style.Colors[ImGuiCol_Border]);
    const ImU32 HoverBackground = DarkHmi ? KitInkHex(0x202020) : ImGui::ColorConvertFloat4ToU32(Style.Colors[ImGuiCol_FrameBgHovered]);
    const ImU32 EdgeColour      = DarkHmi ? KitInkHex(0x2a2a2a) : ImGui::ColorConvertFloat4ToU32(Style.Colors[ImGuiCol_Border]);
    const ImU32 TextColour      = DarkHmi ? KitInkHex(0xdcdcdc) : ImGui::ColorConvertFloat4ToU32(Style.Colors[ImGuiCol_Text]);
    const ImU32 SoftColour      = DarkHmi ? KitInkHex(0xb4b4b4) : ImGui::ColorConvertFloat4ToU32(Style.Colors[ImGuiCol_Text]);
    const ImU32 MutedColour     = DarkHmi ? KitInkHex(0x7d7d7d) : ImGui::ColorConvertFloat4ToU32(Style.Colors[ImGuiCol_TextDisabled]);
    const ImU32 FaintColour     = DarkHmi ? KitInkHex(0x5c5c5c) : ImGui::ColorConvertFloat4ToU32(Style.Colors[ImGuiCol_TextDisabled]);
    const ImU32 GreenColour     = KitInkHex(0x3ec46d);
    const ImU32 WarmColour      = KitInkHex(0xe0a24a);

    Ink->AddRectFilled(ImVec2(WindowPosition.x, CursorTop.y - 4.0f),
                       ImVec2(WindowPosition.x + WindowSize.x, WindowPosition.y + WindowSize.y), PanelBackground);

    // Brand row (EditorShell.js .brand): 24 px Layers mark, 23 px DM Sans, right-aligned ENGINE / 01.
    const float BrandTop = BodyTop;
    const float BrandLeft = PanelLeft + 8.0f;
    DrawVehicleHmiGlyph(Ink, "Layers", ImVec2(BrandLeft + 12.0f, BrandTop + 15.5f), 24.0f,
                        KitInkHex(0xc2c2c2), 1.5f, 1.0f, -10.0f);
    const float WordX = BrandLeft + 24.0f + 10.0f;
    KitDrawText(Ink, Faces.Medium, 23.0f, ImVec2(WordX, BrandTop + 1.0f), TextColour, "frontier");
    const float WordWidth = KitTextWidth(Faces.Medium, 23.0f, "frontier");
    KitDrawText(Ink, Faces.Medium, 23.0f, ImVec2(WordX + WordWidth, BrandTop + 1.0f),
                DarkHmi ? KitInkHex(0xa8a8a8) : SoftColour, ".");
    const float VersionWidth = KitTextWidth(Faces.Medium, 9.0f, "ENGINE / 01") + 10.0f * 1.4f;
    DrawTrackedText(Ink, Faces.Medium, 9.0f, ImVec2(PanelRight - 8.0f - VersionWidth, BrandTop + 10.0f),
                    DarkHmi ? KitInkHex(0x6f6f6f) : MutedColour, "ENGINE / 01", 1.4f);

    // WORKSPACE label and project-aware .frontier title.
    const float WorkspaceLabelTop = BrandTop + 57.0f; // .brand line box plus 26 px lower padding
    DrawTrackedText(Ink, Faces.SemiBold, 9.0f, ImVec2(PanelLeft + 10.0f, WorkspaceLabelTop),
                    DarkHmi ? KitInkHex(0x717171) : MutedColour, "WORKSPACE", 1.6f);
    Ink->AddCircleFilled(ImVec2(PanelRight - 10.0f, WorkspaceLabelTop + 5.0f), 4.0f,
                         IM_COL32(62, 196, 109, 24), 12);
    Ink->AddCircleFilled(ImVec2(PanelRight - 10.0f, WorkspaceLabelTop + 5.0f), 2.5f, GreenColour, 10);

    const float ProjectTitleTop = WorkspaceLabelTop + 22.0f;
    const char* ProjectName = QueryWorkspaceName().empty() ? "Project workspace" : QueryWorkspaceName().c_str();
    KitDrawText(Ink, Faces.Regular, 16.0f, ImVec2(PanelLeft + 10.0f, ProjectTitleTop), TextColour, ProjectName);
    const float ProjectNameWidth = KitTextWidth(Faces.Regular, 16.0f, ProjectName);
    KitDrawText(Ink, Faces.Regular, 16.0f, ImVec2(PanelLeft + 10.0f + ProjectNameWidth, ProjectTitleTop),
                DarkHmi ? KitInkHex(0x6f6f6f) : MutedColour, ".frontier");

    // Enabled / Disabled metric tiles: CSS .count-tiles and .count-tile, including the 62 px card height.
    const float TileTop = WorkspaceLabelTop + 64.0f;
    const float TileHeight = 62.0f;
    const float TileGap = 9.0f;
    const float TileWidth = (PanelWidth - TileGap - 4.0f) * 0.5f;
    const auto DrawCountTile = [&](float X, const char* Caption, uint32_t Value, bool EnabledTile) {
        const ImVec2 Minimum(X, TileTop);
        const ImVec2 Maximum(X + TileWidth, TileTop + TileHeight);
        DrawRoundSurface(Ink, Minimum, Maximum, TileBackground, TileBorder, 14.0f);
        const ImVec2 IconCentre(X + 12.0f + 11.0f, TileTop + 10.0f + 11.0f);
        const ImU32 IconDisc = EnabledTile ? KitInkHex(0x3ec46d) : KitInkHex(0x2c2c2c);
        const ImU32 IconInk = EnabledTile ? KitInkHex(0x0b2414) : KitInkHex(0x8a8a8a);
        Ink->AddCircleFilled(IconCentre, 11.0f, IconDisc, 24);
        DrawVehicleHmiGlyph(Ink, EnabledTile ? "Check" : "EyeOff", IconCentre, 12.0f, IconInk, 2.4f);
        KitDrawText(Ink, Faces.Regular, 10.0f, ImVec2(X + 12.0f, TileTop + TileHeight - 21.0f),
                    DarkHmi ? KitInkHex(0x9a9a9a) : MutedColour, Caption);
        char Text[24] = {};
        std::snprintf(Text, sizeof(Text), "%u", Value);
        const float ValueWidth = KitTextWidth(Faces.Light, 27.0f, Text);
        KitDrawText(Ink, Faces.Light, 27.0f, ImVec2(X + TileWidth - 13.0f - ValueWidth, TileTop + 13.0f),
                    DarkHmi ? KitInkHex(0xe6e6e6) : TextColour, Text);
    };
    DrawCountTile(PanelLeft + 2.0f, "Enabled", EnabledCounts.Enabled, true);
    DrawCountTile(PanelLeft + 2.0f + TileWidth + TileGap, "Disabled", EnabledCounts.Disabled, false);

    // Outliner heading and compact entity count. The reference heading has no add/spawn button.
    const float HeadingTop = TileTop + TileHeight + 16.0f;
    KitDrawText(Ink, Faces.Medium, 12.0f, ImVec2(PanelLeft + 10.0f, HeadingTop), TextColour, "Outliner");
    char CountText[16] = {};
    std::snprintf(CountText, sizeof(CountText), "%02u", Total);
    const float HeadingWidth = KitTextWidth(Faces.Medium, 12.0f, "Outliner");
    const float CountWidth = KitTextWidth(Faces.Regular, 10.0f, CountText);
    const float CountX = PanelLeft + 10.0f + HeadingWidth + 6.0f;
    DrawRoundSurface(Ink, ImVec2(CountX, HeadingTop - 1.0f),
                     ImVec2(CountX + CountWidth + 10.0f, HeadingTop + 15.0f),
                     0, DarkHmi ? KitInkHex(0x313131) : EdgeColour, 5.0f);
    KitDrawText(Ink, Faces.Regular, 10.0f, ImVec2(CountX + 5.0f, HeadingTop + 1.0f),
                DarkHmi ? KitInkHex(0x7e7e7e) : MutedColour, CountText);

    // Find an object… search input: 34 px control, 18 px bottom margin, exact source Lucide magnifier.
    const float SearchTop = HeadingTop + 26.0f;
    const float SearchHeight = 34.0f;
    const float SearchLeft = PanelLeft + 4.0f;
    const float SearchRight = PanelRight - 4.0f;
    if (FilterSynchronised != TextFilter)
    {
        std::snprintf(FilterBuffer.data(), FilterBuffer.size(), "%s", TextFilter.c_str());
        FilterSynchronised = TextFilter;
    }
    DrawRoundSurface(Ink, ImVec2(SearchLeft, SearchTop), ImVec2(SearchRight, SearchTop + SearchHeight),
                     SearchBackground, SearchBorder, 8.0f);
    DrawVehicleHmiGlyph(Ink, "Search", ImVec2(SearchLeft + 18.0f, SearchTop + 17.0f), 14.0f,
                        DarkHmi ? KitInkHex(0x707070) : MutedColour, 1.7f);

    ImGui::SetCursorScreenPos(ImVec2(SearchLeft + 35.0f, SearchTop + 5.0f));
    ImGui::PushItemWidth(std::max(40.0f, SearchRight - SearchLeft - 44.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    if (ImGui::InputTextWithHint("##OutlinerSearchInput", "Find an object…", FilterBuffer.data(), FilterBuffer.size()))
    {
        ConfigureTextFilter(FilterBuffer.data());
        FilterSynchronised = TextFilter;
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::PopItemWidth();

    // Scrollable grouped tree and bottom status row; measurements follow CSS .tree / .group / .tree-row.
    constexpr float FooterExtent = 67.0f; // border + 16 px top + 34 px icon + 2 px lower + 14 px body padding
    const float FooterTop = WindowPosition.y + WindowSize.y - FooterExtent;
    const float TreeTop = SearchTop + SearchHeight + 18.0f;
    const float TreeHeight = std::max(48.0f, FooterTop - TreeTop);
    const float TreeLeft = WindowPosition.x + 16.0f;
    const float TreeRight = WindowPosition.x + WindowSize.x - 16.0f;

    ImGui::SetCursorScreenPos(ImVec2(WindowPosition.x, TreeTop));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::BeginChild("##OutlinerGroupedList", ImVec2(WindowSize.x, TreeHeight), false,
                          ImGuiWindowFlags_NoBackground))
    {
        ImDrawList* TreeInk = ImGui::GetWindowDrawList();
        for (size_t GroupIndex = 0; GroupIndex < Groups.size(); ++GroupIndex)
        {
            const SceneDirectoryGroupSection& Section = Groups[GroupIndex];
            if (Section.Rows.empty())
            {
                continue;
            }

            const bool GroupCollapsed = QueryGroupCollapsed(Section.Group);
            const bool GroupOpen = !GroupCollapsed || !TextFilter.empty();
            const ImVec2 GroupStart = ImGui::GetCursorScreenPos();
            constexpr float GroupHeaderHeight = 22.0f; // 14 px label + 8 px source padding
            const float GroupButtonLeft = PanelLeft + 8.0f;
            const float GroupButtonWidth = std::max(1.0f, TreeRight - GroupButtonLeft - 4.0f);
            ImGui::PushID(static_cast<int>(GroupIndex));
            ImGui::SetCursorScreenPos(ImVec2(GroupButtonLeft, GroupStart.y));
            ImGui::InvisibleButton("##OutlinerGroup", ImVec2(GroupButtonWidth, GroupHeaderHeight));
            if (ImGui::IsItemClicked())
            {
                ConfigureGroupCollapsed(Section.Group, !GroupCollapsed);
            }

            const bool HasEnvironmentGlyph = Section.Group == "Environment" || Section.Group == "Forcefield";
            const float LabelLeft = PanelLeft + 10.0f;
            const float GlyphY = GroupStart.y + 8.0f;
            float ChevronX = LabelLeft;
            if (HasEnvironmentGlyph)
            {
                DrawVehicleHmiGlyph(TreeInk, "NativeEnvironment", ImVec2(LabelLeft + 9.0f, GlyphY), 18.0f,
                                    SoftColour, 1.5f, 0.92f);
                ChevronX = LabelLeft + 18.0f + 7.0f;
            }
            DrawVehicleHmiGlyph(TreeInk, GroupOpen ? "ChevronDown" : "ChevronRight",
                                ImVec2(ChevronX + 6.0f, GlyphY), 12.0f,
                                DarkHmi ? KitInkHex(0x818181) : MutedColour, 1.8f);
            const float GroupTextX = ChevronX + 12.0f + 7.0f;
            KitDrawText(TreeInk, Faces.Medium, 10.0f, ImVec2(GroupTextX, GroupStart.y + 1.0f),
                        DarkHmi ? KitInkHex(0x818181) : MutedColour, Section.Group.c_str());
            char GroupCount[16] = {};
            std::snprintf(GroupCount, sizeof(GroupCount), "%u", Section.Count);
            const float GroupCountWidth = KitTextWidth(Faces.Regular, 9.0f, GroupCount);
            KitDrawText(TreeInk, Faces.Regular, 9.0f,
                        ImVec2(TreeRight - 10.0f - GroupCountWidth, GroupStart.y + 2.0f),
                        DarkHmi ? KitInkHex(0x575757) : FaintColour, GroupCount);
            ImGui::PopID();

            if (!GroupOpen)
            {
                ImGui::SetCursorScreenPos(ImVec2(TreeLeft, GroupStart.y + GroupHeaderHeight + 16.0f));
                ImGui::Dummy(ImVec2(1.0f, 0.0f));
                continue;
            }

            ImGui::SetCursorScreenPos(ImVec2(TreeLeft, GroupStart.y + GroupHeaderHeight + 2.0f));
            for (const SceneDirectoryRow& Row : Section.Rows)
            {
                const ImVec2 RowStart = ImGui::GetCursorScreenPos();
                constexpr float RowHeight = 38.0f;
                const float RowLeft = TreeLeft + 2.0f;
                const float RowRight = TreeRight - 2.0f;
                const float RowWidth = std::max(40.0f, RowRight - RowLeft);
                const bool HasEyeToggle = Row.OutlinerToggle != nullptr;
                const float EyeWidth = HasEyeToggle ? 30.0f : 0.0f;
                const ImU32 AccentColour = ParseHexColourOrFallback(Row.Colour, Row.Classification);
                const bool IsSelected = Row.Token == Selected;
                const float LeftPadding = Row.DepthTier > 0 ? 38.0f : 16.0f;
                const float GlyphLeft = RowLeft + LeftPadding;
                const float GlyphSize = 16.0f;
                const float NameLeft = GlyphLeft + GlyphSize + 11.0f;
                const float StateCentreX = RowRight - EyeWidth - 7.5f;
                const float MainButtonWidth = std::max(16.0f, RowWidth - EyeWidth);

                ImGui::PushID(static_cast<int>(Row.Token.Value));
                ImGui::SetCursorScreenPos(RowStart);
                ImGui::InvisibleButton("##OutlinerRowSelect", ImVec2(MainButtonWidth, RowHeight));
                const bool RowHovered = ImGui::IsItemHovered();
                const bool RowClicked = ImGui::IsItemClicked();

                bool EyeHovered = false;
                bool EyeClicked = false;
                if (HasEyeToggle)
                {
                    ImGui::SetCursorScreenPos(ImVec2(RowRight - EyeWidth, RowStart.y));
                    ImGui::InvisibleButton("##OutlinerRowVisibility", ImVec2(EyeWidth, RowHeight));
                    EyeHovered = ImGui::IsItemHovered();
                    EyeClicked = ImGui::IsItemClicked();
                    if (EyeClicked)
                    {
                        *Row.OutlinerToggle = !*Row.OutlinerToggle;
#if defined(FRONTIER_EDITOR_PROFILE)
                        if (const RecordEditorDescription* Description = Directory.QueryEditorDescription(Row.Token);
                            Description != nullptr && Description->Revision != nullptr)
                        {
                            ++(*Description->Revision);
                        }
#endif
                    }
                }
                if (RowClicked)
                {
                    Select(Row.Token);
                }

                const ImVec2 RowMinimum(RowLeft, RowStart.y + 2.0f);
                const ImVec2 RowMaximum(RowRight, RowStart.y + RowHeight - 2.0f);
                if (IsSelected)
                {
                    const ImU32 LeftFill = DarkHmi ? KitInkHex(0x333333) : ImGui::ColorConvertFloat4ToU32(Style.Colors[ImGuiCol_HeaderActive]);
                    const ImU32 RightFill = DarkHmi ? KitInkHex(0x292929) : LeftFill;
                    TreeInk->AddRectFilledMultiColor(RowMinimum, RowMaximum, LeftFill, RightFill, RightFill, LeftFill);
                    TreeInk->AddRect(RowMinimum, RowMaximum, DarkHmi ? KitInkHex(0x434343) : EdgeColour,
                                     8.0f, ImDrawFlags_None, 1.0f);
                    TreeInk->AddRectFilled(ImVec2(RowLeft - 2.0f, RowStart.y + 11.0f),
                                           ImVec2(RowLeft + 0.5f, RowStart.y + 27.0f), AccentColour, 1.0f);
                }
                else if (RowHovered || EyeHovered)
                {
                    TreeInk->AddRectFilled(RowMinimum, RowMaximum,
                                           DarkHmi ? KitInkHex(0x202020) : HoverBackground, 8.0f);
                }

                const float RowOpacity = Row.Enabled ? 0.85f : 0.45f;
                DrawVehicleHmiGlyph(TreeInk, Row.Glyph.empty() ? "Box" : Row.Glyph,
                                    ImVec2(GlyphLeft + GlyphSize * 0.5f, RowStart.y + RowHeight * 0.5f),
                                    GlyphSize, AccentColour, 1.6f, RowOpacity);

                const ImU32 RowNameColour = Row.Enabled
                    ? (IsSelected ? (DarkHmi ? KitInkHex(0xf1f1f1) : TextColour)
                                  : (DarkHmi ? KitInkHex(0xa3a3a3) : TextColour))
                    : KitInkWithAlpha(DarkHmi ? KitInkHex(0xa3a3a3) : TextColour, 0.45f);
                const float MetaWidth = Row.Meta.empty() ? 0.0f : KitTextWidth(Faces.Regular, 9.0f, Row.Meta.c_str());
                const float NameRight = StateCentreX - 10.0f - MetaWidth;
                TreeInk->PushClipRect(ImVec2(NameLeft, RowStart.y), ImVec2(std::max(NameLeft, NameRight), RowStart.y + RowHeight), true);
                KitDrawText(TreeInk, IsSelected ? Faces.Medium : Faces.Regular, 12.0f,
                            ImVec2(NameLeft, RowStart.y + 12.0f), RowNameColour, Row.Name.c_str());
                TreeInk->PopClipRect();
                if (MetaWidth > 0.0f && NameRight > NameLeft + 44.0f)
                {
                    KitDrawText(TreeInk, Faces.Regular, 9.0f,
                                ImVec2(StateCentreX - 10.0f - MetaWidth, RowStart.y + 14.0f),
                                KitInkWithAlpha(DarkHmi ? KitInkHex(0x666666) : MutedColour,
                                                Row.Enabled ? 1.0f : 0.45f), Row.Meta.c_str());
                }

                ImU32 StateColour = DarkHmi ? KitInkHex(0x3a3a3a) : FaintColour;
                if (Row.Enabled && Row.State == "live") StateColour = GreenColour;
                else if (Row.Enabled && Row.State == "warm") StateColour = WarmColour;
                TreeInk->AddCircleFilled(ImVec2(StateCentreX, RowStart.y + RowHeight * 0.5f), 3.5f, StateColour, 12);

                if (HasEyeToggle)
                {
                    const float IconOpacity = (EyeHovered || IsSelected || !Row.Enabled) ? 1.0f : 0.0f;
                    DrawVehicleHmiGlyph(TreeInk, Row.Enabled ? "Eye" : "EyeOff",
                                        ImVec2(RowRight - EyeWidth * 0.5f, RowStart.y + RowHeight * 0.5f),
                                        13.0f, DarkHmi ? KitInkHex(0x6a6a6a) : MutedColour, 1.7f, IconOpacity);
                }

                ImGui::SetCursorScreenPos(ImVec2(TreeLeft, RowStart.y + RowHeight + 4.0f));
                ImGui::Dummy(ImVec2(1.0f, 0.0f));
                ImGui::PopID();
            }
            ImGui::SetCursorScreenPos(ImVec2(TreeLeft, ImGui::GetCursorScreenPos().y + 16.0f));
            ImGui::Dummy(ImVec2(1.0f, 0.0f));
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    // Source footer uses a hairline, 34 px Box tile, project identity and a live status dot; no faux card.
    Ink->AddLine(ImVec2(WindowPosition.x, FooterTop),
                 ImVec2(WindowPosition.x + WindowSize.x, FooterTop), EdgeColour, 1.0f);
    const float FooterIconLeft = PanelLeft + 10.0f;
    const float FooterIconTop = FooterTop + 17.0f;
    Ink->AddRectFilled(ImVec2(FooterIconLeft, FooterIconTop), ImVec2(FooterIconLeft + 34.0f, FooterIconTop + 34.0f),
                       DarkHmi ? KitInkHex(0x252525) : TileBackground, 8.0f);
    DrawVehicleHmiGlyph(Ink, "Box", ImVec2(FooterIconLeft + 17.0f, FooterIconTop + 17.0f), 18.0f,
                        DarkHmi ? KitInkHex(0x9a9a9a) : MutedColour, 1.5f);
    KitDrawText(Ink, Faces.Medium, 11.0f, ImVec2(FooterIconLeft + 45.0f, FooterTop + 16.0f),
                DarkHmi ? KitInkHex(0xbbbbbb) : TextColour,
                QueryWorkspaceName().empty() ? "Project workspace" : QueryWorkspaceName().c_str());
    KitDrawText(Ink, Faces.Regular, 10.0f, ImVec2(FooterIconLeft + 45.0f, FooterTop + 34.0f),
                DarkHmi ? KitInkHex(0x6e6e6e) : MutedColour, QueryWorkspaceStatus().c_str());
    Ink->AddCircleFilled(ImVec2(PanelRight - 10.0f, FooterIconTop + 17.0f), 4.5f,
                         IM_COL32(62, 196, 109, 24), 12);
    Ink->AddCircleFilled(ImVec2(PanelRight - 10.0f, FooterIconTop + 17.0f), 2.5f, GreenColour, 10);
}

} // namespace Frontier

#else

namespace Frontier
{
void SceneDirectoryPanel::ConstructInterface() {}
} // namespace Frontier

#endif
