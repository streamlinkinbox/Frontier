/*==========================================================================================================================================
                                                NATIVE PRESENTATION OF THE OUTLINER PANEL
                                     ------------------------------------------------------------------
             SceneDirectoryPanel drawn with StackRail + FolderRow + TreeRow + VehicleHMI Enabled/Disabled count tiles.
==========================================================================================================================================*/

// 🧩 Native presentation of the Outliner panel: Folder enclosures, count tiles, filter chips, and live row telemetry.

#include "SceneDirectoryPanel.h"

#if defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/DropdownControls.h"
#include "Engine/Interface/StackRailControls.h"
#include "Engine/Scene/SceneDirectory.h"
#include "SceneDirectoryPanelRegistration.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

namespace Frontier
{

namespace
{

ImU32 ClassificationColour(RecordClassification Classification) noexcept
{
    switch (Classification)
    {
        case RecordClassification::Folder:       return KitInkHex(0x7ad9a3);
        case RecordClassification::Vehicle:      return KitInkHex(0x2fe6a7);
        case RecordClassification::Tyre:         return KitInkHex(0x63a8ff);
        case RecordClassification::Environment:  return KitInkHex(0x8fd14f);
        case RecordClassification::Geometry:     return KitInkHex(0xb18cff);
        case RecordClassification::Illumination: return KitInkHex(0xffb454);
        case RecordClassification::Camera:       return KitInkHex(0xff7ad9);
        case RecordClassification::Physics:      return KitInkHex(0xff6d6d);
        case RecordClassification::Interface:    return KitInkHex(0x9fd8e8);
        case RecordClassification::Content:      return KitInkHex(0xd8cf9f);
        case RecordClassification::Unclassified: return KitInkHex(0x5f6b64);
        case RecordClassification::Count:        break;
    }
    return KitInkHex(0x5f6b64);
}

void MenuCaption(const KitFaces& Faces, const char* Text)
{
    const KitTheme Theme = ResolveKitTheme();
    ImDrawList*    Ink   = ImGui::GetWindowDrawList();
    const ImVec2   At    = ImGui::GetCursorScreenPos();
    KitDrawTracked(Ink, Faces.Medium, 10.5f, ImVec2(At.x + 10.0f, At.y + 8.0f), Theme.Faint, Text, 10.5f * 0.13f);
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 27.0f));
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PANEL REGISTRATION
//------------------------------------------------------------------------------------------------------------------------

DockPanelSpecification MakeSceneDirectoryPanelSpecification(SceneDirectoryPanel& Panel)
{
    DockPanelSpecification Specification;
    Specification.Key              = "Outliner";
    Specification.Caption          = "Outliner";
    Specification.ConstructContent = [&Panel]() { Panel.ConstructInterface(); };
    Specification.Open             = true;
    return Specification;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERFACE RECORDING
//------------------------------------------------------------------------------------------------------------------------

void SceneDirectoryPanel::ConstructInterface()
{
    if (ImGui::GetCurrentContext() == nullptr)
    {
        return;
    }
    SceneDirectory&              Directory = SceneDirectory::Instance();
    const KitFaces               Faces     = ResolveKitFaces();
    const KitTheme               Theme     = ResolveKitTheme();
    static std::array<char, 128> FilterBuffer{};
    static std::string           FilterSynchronised;

    const ImVec2 WindowPosition = ImGui::GetWindowPos();
    const ImVec2 WindowSize     = ImGui::GetWindowSize();
    const float  PL             = WindowPosition.x;
    const float  PT             = WindowPosition.y;
    const float  PR             = PL + WindowSize.x;
    const float  PB             = PT + WindowSize.y;
    const float  CL             = PL + 14.0f;
    const float  CR             = PR - 14.0f;
    const float  ContentW       = std::max(80.0f, CR - CL);
    ImDrawList*  Ink            = ImGui::GetWindowDrawList();

    Ink->AddRectFilled(WindowPosition, ImVec2(PR, PB), Theme.PanelBg);
    Ink->AddRect(WindowPosition, ImVec2(PR, PB), Theme.PanelEdge);

    // -- StackRail head (title · HeadCount · + Folder · + Add) --------------------------------------------------------
    char CountText[48] = {};
    std::snprintf(CountText, sizeof(CountText), "%u", QueryTotal());
    const StackRailHeadInteraction HeadHit =
        DrawOutlinerHead(Faces, ImVec2(CL, PT + 12.0f), ContentW, "Outliner", CountText);
    if (HeadHit.FolderClicked)
    {
        RequestSpawn(RecordClassification::Folder);
    }
    if (HeadHit.AddClicked)
    {
        ImGui::OpenPopup("##OutlinerSpawnPopup");
    }
    float CursorY = PT + 12.0f + 36.0f + 8.0f;

    // Spawn menu (reusable dropdown surface).
    {
        ImGui::SetNextWindowPos(ImVec2(CR, PT + 46.0f), ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(224.0f, 0.0f));
        if (DropdownControlSurface::BeginMenu("##OutlinerSpawnPopup", 1.0f))
        {
            MenuCaption(Faces, "CREATE IN SCENE");
            for (uint32_t Index = 0; Index < static_cast<uint32_t>(RecordClassification::Count); ++Index)
            {
                const auto Classification = static_cast<RecordClassification>(Index);
                if (Classification == RecordClassification::Unclassified)
                {
                    continue;
                }
                const char* Name = RecordClassificationName(Classification);
                if (DropdownControlSurface::Option(Name, false, 1.0f))
                {
                    RequestSpawn(Classification);
                    ImGui::CloseCurrentPopup();
                }
            }
            DropdownControlSurface::EndMenu();
        }
    }

    // -- VehicleHMI Enabled / Disabled count tiles (ported from EditorShell.js .count-tiles) --------------------------
    {
        const float TileGap = 8.0f;
        const float TileW   = std::max(40.0f, (ContentW - TileGap) * 0.5f);
        const float TileH   = 30.0f;

        auto DrawCountTile = [&](ImVec2 Origin, const char* Caption, uint32_t Count, ImU32 Accent, bool CheckGlyph) {
            KitPillSurface(Ink, Origin, ImVec2(Origin.x + TileW, Origin.y + TileH), Theme.Surface, Theme.LineSoft,
                           8.0f);
            const ImVec2 IconCentre(Origin.x + 15.0f, Origin.y + TileH * 0.5f);
            if (CheckGlyph)
            {
                Ink->AddCircleFilled(IconCentre, 6.5f, KitInkWithAlpha(Accent, 0.16f));
                Ink->AddLine(ImVec2(IconCentre.x - 3.2f, IconCentre.y + 0.2f),
                             ImVec2(IconCentre.x - 0.8f, IconCentre.y + 2.6f), Accent, 1.6f);
                Ink->AddLine(ImVec2(IconCentre.x - 0.8f, IconCentre.y + 2.6f),
                             ImVec2(IconCentre.x + 3.6f, IconCentre.y - 2.4f), Accent, 1.6f);
            }
            else
            {
                Ink->AddCircle(IconCentre, 5.5f, Accent, 20, 1.3f);
                Ink->AddLine(ImVec2(IconCentre.x - 5.5f, IconCentre.y + 4.5f),
                             ImVec2(IconCentre.x + 5.5f, IconCentre.y - 4.5f), Accent, 1.3f);
            }
            KitDrawText(Ink, Faces.Regular, 11.5f, ImVec2(Origin.x + 28.0f, Origin.y + 9.0f), Theme.Muted, Caption);
            char ValueText[24] = {};
            std::snprintf(ValueText, sizeof(ValueText), "%u", Count);
            const float ValueW = KitTextWidth(Faces.SemiBold, 12.5f, ValueText);
            KitDrawText(Ink, Faces.SemiBold, 12.5f, ImVec2(Origin.x + TileW - 10.0f - ValueW, Origin.y + 8.5f),
                        Theme.Text, ValueText);
        };

        DrawCountTile(ImVec2(CL, CursorY), "Enabled", QueryEnabledCount(), Theme.Mint, true);
        DrawCountTile(ImVec2(CL + TileW + TileGap, CursorY), "Disabled", QueryDisabledCount(), Theme.Faint, false);
        CursorY += TileH + 8.0f;
    }

    // -- Category tabs (flowing chip strip, All + populated classifications) -----------------------------------------
    {
        float X    = CL;
        auto  Chip = [&](const char* Label, const char* IdText, uint32_t Count, bool Active, ImU32 Dot,
                        const std::optional<RecordClassification>& FilterTarget) {
            char Text[64] = {};
            std::snprintf(Text, sizeof(Text), "%s %u", Label, Count);
            const float W = 10.0f + 7.0f + 6.0f + KitTextWidth(Faces.Regular, 11.5f, Text) + 10.0f;
            if (X + W > CR && X > CL)
            {
                X = CL;
                CursorY += 24.0f + 5.0f;
            }
            bool Hovered = false;
            char Id[80]  = {};
            std::snprintf(Id, sizeof(Id), "##OutlinerChip%s", IdText);
            if (KitTarget(Id, ImVec2(X, CursorY), ImVec2(W, 24.0f), Hovered))
            {
                ConfigureClassificationFilter(FilterTarget);
            }
            KitPillSurface(Ink, ImVec2(X, CursorY), ImVec2(X + W, CursorY + 24.0f),
                           Active ? Theme.ChipOnBg : (Hovered ? Theme.Surface3 : Theme.Surface2),
                           Active ? 0 : Theme.Line, 999.0f);
            Ink->AddCircleFilled(ImVec2(X + 13.5f, CursorY + 12.0f), 3.2f, Active ? Theme.ChipOnInk : Dot);
            KitDrawText(Ink, Faces.Regular, 11.5f, ImVec2(X + 23.0f, CursorY + 6.2f),
                        Active ? Theme.ChipOnInk : Theme.Muted, Text);
            X += W + 5.0f;
        };
        Chip("All", "All", QueryTotal(), !QueryClassificationFilter().has_value(), Theme.Mint, std::nullopt);
        for (const SceneClassificationTab& Tab : Tabs)
        {
            if (Tab.Count == 0 && QueryClassificationFilter() != Tab.Classification)
            {
                continue;
            }
            Chip(RecordClassificationName(Tab.Classification), RecordClassificationName(Tab.Classification),
                 Tab.Count, QueryClassificationFilter() == Tab.Classification,
                 ClassificationColour(Tab.Classification), Tab.Classification);
        }
        CursorY += 24.0f + 8.0f;
    }

    // -- Search field ("Find an object…" matching VehicleHMI EditorShell.js) -----------------------------------------
    {
        const float RowH  = 32.0f;
        const float RowCY = CursorY + RowH * 0.5f;
        KitPillSurface(Ink, ImVec2(CL, CursorY), ImVec2(CR, CursorY + RowH), Theme.Surface2, Theme.Line, 999.0f);
        Ink->AddCircle(ImVec2(CL + 18.0f, RowCY - 1.5f), 5.2f, Theme.Faint, 24, 1.4f);
        Ink->AddLine(ImVec2(CL + 22.0f, RowCY + 2.8f), ImVec2(CL + 26.0f, RowCY + 6.8f), Theme.Faint, 1.6f);

        if (QueryTextFilter() != FilterSynchronised)
        {
            std::snprintf(FilterBuffer.data(), FilterBuffer.size(), "%s", QueryTextFilter().c_str());
            FilterSynchronised = QueryTextFilter();
        }
        const bool  HasText   = !QueryTextFilter().empty();
        char        Shown[32] = {};
        float       TrailingW = 0.0f;
        if (HasText)
        {
            std::snprintf(Shown, sizeof(Shown), "%zu shown", QueryVisible().size());
            TrailingW = KitTextWidth(Faces.Regular, 11.0f, Shown) + 8.0f + 22.0f + 8.0f;
        }
        const ImVec4 InputTextCol = ImGui::ColorConvertU32ToFloat4(Theme.Text);
        ImGui::SetCursorScreenPos(ImVec2(CL + 34.0f, CursorY + (RowH - 13.0f) * 0.5f));
        ImGui::PushFont(Faces.Regular, 13.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, InputTextCol);
        ImGui::SetNextItemWidth(std::max(30.0f, ContentW - 34.0f - TrailingW));
        if (ImGui::InputTextWithHint("##OutlinerFilter", "Find an object\xE2\x80\xA6", FilterBuffer.data(),
                                     FilterBuffer.size()))
        {
            ConfigureTextFilter(FilterBuffer.data());
            FilterSynchronised = QueryTextFilter();
        }
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);
        ImGui::PopFont();

        if (HasText)
        {
            const float ShownW = KitTextWidth(Faces.Regular, 11.0f, Shown);
            KitDrawText(Ink, Faces.Regular, 11.0f, ImVec2(CR - 8.0f - 22.0f - 6.0f - ShownW, RowCY - 5.5f),
                        Theme.Faint, Shown);
            const ImVec2 DiscCentre(CR - 8.0f - 11.0f, RowCY);
            bool         ClearHover = false;
            if (KitTarget("##OutlinerFilterClear", ImVec2(DiscCentre.x - 11.0f, DiscCentre.y - 11.0f),
                          ImVec2(22.0f, 22.0f), ClearHover, "Clear filter"))
            {
                FilterBuffer[0] = '\0';
                ConfigureTextFilter("");
                FilterSynchronised = QueryTextFilter();
            }
            Ink->AddCircleFilled(DiscCentre, 11.0f, Theme.Surface3);
            const ImU32 Cross = ClearHover ? Theme.Text : Theme.Muted;
            Ink->AddLine(ImVec2(DiscCentre.x - 3.5f, DiscCentre.y - 3.5f),
                         ImVec2(DiscCentre.x + 3.5f, DiscCentre.y + 3.5f), Cross, 1.4f);
            Ink->AddLine(ImVec2(DiscCentre.x - 3.5f, DiscCentre.y + 3.5f),
                         ImVec2(DiscCentre.x + 3.5f, DiscCentre.y - 3.5f), Cross, 1.4f);
        }
        CursorY += RowH + 8.0f;
    }

    Ink->AddRectFilled(ImVec2(CL, CursorY), ImVec2(CR, CursorY + 1.0f), Theme.LineSoft);
    CursorY += 6.0f;

    // -- Tree (Folder enclosures + nested record rows) ---------------------------------------------------------------
    const float FooterH    = 26.0f;
    const float TreeBottom = PB - 10.0f - FooterH;
    const float TreeH      = std::max(40.0f, TreeBottom - CursorY);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::SetCursorScreenPos(ImVec2(CL, CursorY));
    if (ImGui::BeginChild("##OutlinerRows", ImVec2(ContentW, TreeH), false))
    {
        const auto& VisibleRows = QueryVisible();
        if (VisibleRows.empty())
        {
            const char* Message = QueryTotal() == 0
                                      ? "No scene objects yet — records appear here as they are constructed."
                                      : "No objects match the current filter.";
            const float W = KitTextWidth(Faces.Regular, 12.0f, Message);
            KitDrawText(Ink, Faces.Regular, 12.0f,
                        ImVec2(CL + std::max(0.0f, (ContentW - W) * 0.5f), CursorY + 24.0f), Theme.Faint, Message);
        }
        bool RefreshNeeded = false;
        for (const SceneDirectoryRow& Row : VisibleRows)
        {
            const ImVec2 At     = ImGui::GetCursorScreenPos();
            char         Id[48] = {};
            std::snprintf(Id, sizeof(Id), "OutlinerRow%llu", static_cast<unsigned long long>(Row.Token.Value));
            const ImU32 RowAccent = Row.AccentColour != 0 ? static_cast<ImU32>(Row.AccentColour)
                                                          : ClassificationColour(Row.Classification);

            if (Row.Classification == RecordClassification::Folder)
            {
                const TreeRowInteraction Hit = DrawFolderRow(
                    Faces, Id, ImVec2(CL, At.y), ContentW, static_cast<float>(Row.DepthTier) * 16.0f, Row.Collapsed,
                    RowAccent, Row.Name.c_str(), Row.LiveBadge.c_str(), Row.ChildCount, Row.Visible,
                    QuerySelected() == Row.Token);
                if (Hit.ChevronClicked)
                {
                    ConfigureCollapsed(Row.Token, !Row.Collapsed);
                }
                else if (Hit.EyeClicked)
                {
                    (void)Directory.ConfigureCascadeVisible(Row.Token, !Row.Visible);
                    RefreshNeeded = true;
                }
                else if (Hit.Clicked)
                {
                    Select(Row.Token);
                    RefreshNeeded = true;
                }
                ImGui::SetCursorScreenPos(ImVec2(CL, At.y + 36.0f));
                ImGui::Dummy(ImVec2(ContentW, 0.0f));
            }
            else
            {
                const char* BadgeText = !Row.LiveBadge.empty() ? Row.LiveBadge.c_str()
                                                               : RecordClassificationName(Row.Classification);
                const TreeRowInteraction Hit = DrawTreeRow(
                    Faces, Id, ImVec2(CL, At.y), ContentW, static_cast<float>(Row.DepthTier) * 16.0f,
                    Row.ChildCount > 0, Row.Collapsed, RowAccent, Row.Name.c_str(), BadgeText, Row.Visible,
                    QuerySelected() == Row.Token);
                if (Hit.ChevronClicked)
                {
                    ConfigureCollapsed(Row.Token, !Row.Collapsed);
                }
                else if (Hit.EyeClicked)
                {
                    (void)Directory.ConfigureVisible(Row.Token, !Row.Visible);
                    RefreshNeeded = true;
                }
                else if (Hit.Clicked)
                {
                    Select(Row.Token);
                    RefreshNeeded = true;
                }
                ImGui::SetCursorScreenPos(ImVec2(CL, At.y + 30.0f));
                ImGui::Dummy(ImVec2(ContentW, 0.0f));
            }
        }
        if (RefreshNeeded)
        {
            Synchronize(Directory);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    // -- StackFoot readout --------------------------------------------------------------------------------------------
    char Left[64] = {};
    std::snprintf(Left, sizeof(Left), "%zu shown \xC2\xB7 %u total", QueryVisible().size(), QueryTotal());
    char Right[80] = {};
    if (QuerySelected().IsValid())
    {
        std::snprintf(Right, sizeof(Right), "%s #%llu", QueryInspector().Name.c_str(),
                      static_cast<unsigned long long>(QuerySelected().Value));
    }
    else
    {
        std::snprintf(Right, sizeof(Right), "%u hidden", QueryDisabledCount());
    }
    DrawStackRailFoot(Faces, ImVec2(CL, PB - 10.0f - FooterH), ContentW, Left, Right);
}

} // namespace Frontier

#else // FRONTIER_WITH_INTERFACE_SDK

namespace Frontier
{

void SceneDirectoryPanel::ConstructInterface()
{
}

} // namespace Frontier

#endif // FRONTIER_WITH_INTERFACE_SDK
