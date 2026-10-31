/*==========================================================================================================================================
                                                NATIVE PRESENTATION OF THE OUTLINER PANEL
                                     ------------------------------------------------------------------
             1:1 VehicleHMI Outliner: frontier. brand, Proving Grounds.scene, 54px count tiles, + Folder, search & IlluminationHost.
==========================================================================================================================================*/

// 🧩 1:1 VehicleHMI Outliner presentation (ported from EditorShell.js + FrontierEditor.css) over SceneDirectoryPanel.

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
        case RecordClassification::Folder:       return KitInkHex(0x9fb4d6);
        case RecordClassification::Vehicle:      return KitInkHex(0xe1d34a);
        case RecordClassification::Tyre:         return KitInkHex(0xa8c5eb);
        case RecordClassification::Environment:  return KitInkHex(0xe8c27a);
        case RecordClassification::Geometry:     return KitInkHex(0xa8bbeb);
        case RecordClassification::Illumination: return KitInkHex(0x9fb4d6);
        case RecordClassification::Camera:       return KitInkHex(0xa8bbeb);
        case RecordClassification::Physics:      return KitInkHex(0x8fb3a4);
        case RecordClassification::Interface:    return KitInkHex(0x9fd8e8);
        case RecordClassification::Content:      return KitInkHex(0xe8833a);
        case RecordClassification::Unclassified: return KitInkHex(0x6d7680);
        case RecordClassification::Count:        break;
    }
    return KitInkHex(0x6d7680);
}

void DrawLayersBrandGlyph(ImDrawList* Ink, ImVec2 Centre, ImU32 Accent)
{
    for (int Layer = 2; Layer >= 0; --Layer)
    {
        const float  OffsetY = static_cast<float>(Layer - 1) * 3.8f;
        const ImU32  Stroke  = Layer == 0 ? Accent : KitInkWithAlpha(Accent, Layer == 1 ? 0.68f : 0.42f);
        const ImVec2 Pts[4]  = {
            ImVec2(Centre.x,        Centre.y - 4.6f + OffsetY),
            ImVec2(Centre.x + 8.0f, Centre.y + OffsetY),
            ImVec2(Centre.x,        Centre.y + 4.6f + OffsetY),
            ImVec2(Centre.x - 8.0f, Centre.y + OffsetY)
        };
        Ink->AddPolyline(Pts, 4, Stroke, ImDrawFlags_Closed, 1.45f);
    }
}

void DrawRecordGlyph(ImDrawList* Ink, ImVec2 Centre, RecordClassification Classification, ImU32 Accent)
{
    switch (Classification)
    {
        case RecordClassification::Environment:
            Ink->AddCircle(Centre, 3.6f, Accent, 16, 1.4f);
            for (int R = 0; R < 8; ++R)
            {
                const float A = static_cast<float>(R) * 0.785398f;
                Ink->AddLine(ImVec2(Centre.x + std::cos(A) * 5.2f, Centre.y + std::sin(A) * 5.2f),
                             ImVec2(Centre.x + std::cos(A) * 6.8f, Centre.y + std::sin(A) * 6.8f), Accent, 1.2f);
            }
            break;
        case RecordClassification::Vehicle:
            Ink->AddRect(ImVec2(Centre.x - 6.5f, Centre.y - 3.5f), ImVec2(Centre.x + 6.5f, Centre.y + 3.5f), Accent, 2.0f, 0, 1.4f);
            Ink->AddCircleFilled(ImVec2(Centre.x - 4.0f, Centre.y + 4.2f), 1.8f, Accent);
            Ink->AddCircleFilled(ImVec2(Centre.x + 4.0f, Centre.y + 4.2f), 1.8f, Accent);
            break;
        case RecordClassification::Tyre:
            Ink->AddCircle(Centre, 5.5f, Accent, 18, 1.5f);
            Ink->AddCircle(Centre, 2.2f, Accent, 12, 1.1f);
            break;
        case RecordClassification::Illumination:
            Ink->AddRect(ImVec2(Centre.x - 6.0f, Centre.y - 4.5f), ImVec2(Centre.x + 6.0f, Centre.y + 3.5f), Accent, 1.5f, 0, 1.4f);
            Ink->AddLine(ImVec2(Centre.x - 3.0f, Centre.y + 6.0f), ImVec2(Centre.x + 3.0f, Centre.y + 6.0f), Accent, 1.4f);
            break;
        case RecordClassification::Physics:
            Ink->AddQuad(ImVec2(Centre.x, Centre.y - 6.0f), ImVec2(Centre.x + 6.0f, Centre.y),
                         ImVec2(Centre.x, Centre.y + 6.0f), ImVec2(Centre.x - 6.0f, Centre.y), Accent, 1.4f);
            break;
        case RecordClassification::Content:
            Ink->AddCircleFilled(ImVec2(Centre.x, Centre.y - 4.0f), 1.6f, Accent);
            Ink->AddCircleFilled(ImVec2(Centre.x - 3.8f, Centre.y + 2.5f), 2.0f, Accent);
            Ink->AddCircleFilled(ImVec2(Centre.x + 3.8f, Centre.y + 2.5f), 1.5f, Accent);
            break;
        default:
            Ink->AddRect(ImVec2(Centre.x - 5.2f, Centre.y - 5.2f), ImVec2(Centre.x + 5.2f, Centre.y + 5.2f), Accent, 2.0f, 0, 1.35f);
            break;
    }
}

void DrawEyeIcon(ImDrawList* Ink, ImVec2 Centre, bool Visible, ImU32 Colour)
{
    Ink->AddBezierCubic(ImVec2(Centre.x - 6.2f, Centre.y),
                        ImVec2(Centre.x - 2.8f, Centre.y - 4.4f),
                        ImVec2(Centre.x + 2.8f, Centre.y - 4.4f),
                        ImVec2(Centre.x + 6.2f, Centre.y), Colour, 1.25f);
    Ink->AddBezierCubic(ImVec2(Centre.x - 6.2f, Centre.y),
                        ImVec2(Centre.x - 2.8f, Centre.y + 4.4f),
                        ImVec2(Centre.x + 2.8f, Centre.y + 4.4f),
                        ImVec2(Centre.x + 6.2f, Centre.y), Colour, 1.25f);
    if (Visible)
    {
        Ink->AddCircleFilled(Centre, 1.8f, Colour);
    }
    else
    {
        Ink->AddLine(ImVec2(Centre.x - 5.5f, Centre.y + 4.8f), ImVec2(Centre.x + 5.5f, Centre.y - 4.8f), Colour, 1.35f);
    }
}

void MenuCaption(const KitFaces& Faces, const char* Text)
{
    const KitTheme Theme = ResolveKitTheme();
    ImDrawList*    Ink   = ImGui::GetWindowDrawList();
    const ImVec2   At    = ImGui::GetCursorScreenPos();
    KitDrawTracked(Ink, Faces.Medium, 10.0f, ImVec2(At.x + 10.0f, At.y + 8.0f), Theme.Faint, Text, 1.1f);
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 26.0f));
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

    const ImU32 BgCol   = Theme.LightCanvas ? Theme.PanelBg : KitInkHex(0x111214);
    const ImU32 CardCol = Theme.LightCanvas ? Theme.Surface : KitInkHex(0x17191c);
    const ImU32 EdgeCol = Theme.LightCanvas ? Theme.PanelEdge : KitInkHex(0x23262b);

    Ink->AddRectFilled(WindowPosition, ImVec2(PR, PB), BgCol);
    Ink->AddLine(ImVec2(PR - 1.0f, PT), ImVec2(PR - 1.0f, PB), EdgeCol, 1.0f);

    float CursorY = PT + 12.0f;

    // 1. VehicleHMI Brand Bar: Layers glyph + "frontier." + "ENGINE / 01"
    {
        DrawLayersBrandGlyph(Ink, ImVec2(CL + 10.0f, CursorY + 11.0f), Theme.Mint);
        const float WordX = CL + 26.0f;
        KitDrawText(Ink, Faces.SemiBold, 17.5f, ImVec2(WordX, CursorY + 1.0f), Theme.Text, "frontier");
        const float WordW = KitTextWidth(Faces.SemiBold, 17.5f, "frontier");
        KitDrawText(Ink, Faces.SemiBold, 17.5f, ImVec2(WordX + WordW + 0.5f, CursorY + 1.0f), Theme.Mint, ".");

        const char* Ver = "ENGINE / 01";
        const float VerW = KitTextWidth(Faces.Medium, 9.2f, Ver) + 10.0f;
        KitDrawTracked(Ink, Faces.Medium, 9.2f, ImVec2(CR - VerW, CursorY + 6.5f), Theme.Faint, Ver, 0.9f);
        CursorY += 28.0f;
    }

    // 2. WORKSPACE Eyebrow + Proving Grounds.scene Title
    {
        const float TrackW = KitDrawTracked(Ink, Faces.Medium, 9.2f, ImVec2(CL, CursorY), Theme.Muted, "WORKSPACE", 1.15f);
        Ink->AddCircleFilled(ImVec2(CL + TrackW + 8.0f, CursorY + 4.8f), 2.8f, Theme.Mint);
        CursorY += 15.0f;

        KitDrawText(Ink, Faces.SemiBold, 16.0f, ImVec2(CL, CursorY), Theme.Text, "Proving Grounds");
        const float TitleW = KitTextWidth(Faces.SemiBold, 16.0f, "Proving Grounds");
        KitDrawText(Ink, Faces.Regular, 13.5f, ImVec2(CL + TitleW + 1.5f, CursorY + 2.0f), Theme.Faint, ".scene");
        CursorY += 25.0f;
    }

    // 3. Enabled / Disabled 52px Vertical Count Tiles (.count-tiles in FrontierEditor.css)
    {
        const float TileGap = 8.0f;
        const float TileW   = std::max(40.0f, (ContentW - TileGap) * 0.5f);
        const float TileH   = 50.0f;

        auto DrawCountTile = [&](ImVec2 Origin, const char* Caption, uint32_t Count, ImU32 Accent, bool EnabledTile) {
            KitPillSurface(Ink, Origin, ImVec2(Origin.x + TileW, Origin.y + TileH), CardCol, EdgeCol, 7.0f);
            const ImVec2 IconC(Origin.x + 15.0f, Origin.y + 15.0f);
            if (EnabledTile)
            {
                Ink->AddCircleFilled(IconC, 6.2f, KitInkWithAlpha(Accent, 0.16f));
                Ink->AddLine(ImVec2(IconC.x - 3.0f, IconC.y + 0.2f), ImVec2(IconC.x - 0.7f, IconC.y + 2.5f), Accent, 1.5f);
                Ink->AddLine(ImVec2(IconC.x - 0.7f, IconC.y + 2.5f), ImVec2(IconC.x + 3.4f, IconC.y - 2.2f), Accent, 1.5f);
            }
            else
            {
                DrawEyeIcon(Ink, IconC, false, Accent);
            }
            KitDrawTracked(Ink, Faces.Medium, 9.2f, ImVec2(Origin.x + 26.0f, Origin.y + 10.5f), Theme.Muted, Caption, 0.8f);
            char ValueText[24] = {};
            std::snprintf(ValueText, sizeof(ValueText), "%u", Count);
            KitDrawText(Ink, Faces.SemiBold, 18.0f, ImVec2(Origin.x + 11.0f, Origin.y + 25.5f),
                        EnabledTile ? Theme.Text : Theme.Muted, ValueText);
        };

        DrawCountTile(ImVec2(CL, CursorY), "ENABLED", QueryEnabledCount(), Theme.Mint, true);
        DrawCountTile(ImVec2(CL + TileW + TileGap, CursorY), "DISABLED", QueryDisabledCount(), Theme.Faint, false);
        CursorY += TileH + 10.0f;
    }

    // 4. Outliner Heading + Count Badge + "+ Folder" & "+ Add"
    {
        KitDrawText(Ink, Faces.SemiBold, 12.8f, ImVec2(CL, CursorY + 3.5f), Theme.Text, "Outliner");
        const float Hw = KitTextWidth(Faces.SemiBold, 12.8f, "Outliner");

        char CountBuf[16] = {};
        std::snprintf(CountBuf, sizeof(CountBuf), "%02u", QueryTotal());
        const float BadgeW = KitTextWidth(Faces.Medium, 10.0f, CountBuf) + 12.0f;
        KitPillSurface(Ink, ImVec2(CL + Hw + 7.0f, CursorY + 2.5f), ImVec2(CL + Hw + 7.0f + BadgeW, CursorY + 20.5f),
                       CardCol, EdgeCol, 999.0f);
        KitDrawText(Ink, Faces.Medium, 10.0f, ImVec2(CL + Hw + 13.0f, CursorY + 5.0f), Theme.Muted, CountBuf);

        // "+ Add" & "+ Folder" compact buttons on right
        const float AddW    = 44.0f;
        const float FolderW = 58.0f;
        const float BtnH    = 22.0f;
        bool        AddHov  = false;
        bool        FldHov  = false;
        const ImVec2 AddMin(CR - AddW, CursorY + 1.0f);
        const ImVec2 FldMin(CR - AddW - 5.0f - FolderW, CursorY + 1.0f);
        if (KitTarget("##OutlinerNewFolderBtn", FldMin, ImVec2(FolderW, BtnH), FldHov, "Create new Folder"))
        {
            RequestSpawn(RecordClassification::Folder);
        }
        if (KitTarget("##OutlinerSpawnAddBtn", AddMin, ImVec2(AddW, BtnH), AddHov, "Create scene item"))
        {
            ImGui::OpenPopup("##OutlinerSpawnPopup");
        }
        KitPillSurface(Ink, FldMin, ImVec2(FldMin.x + FolderW, FldMin.y + BtnH),
                       FldHov ? Theme.Surface3 : CardCol, EdgeCol, 5.5f);
        KitDrawText(Ink, Faces.Medium, 10.4f, ImVec2(FldMin.x + 8.0f, FldMin.y + 4.8f), Theme.Muted, "+ Folder");

        KitPillSurface(Ink, AddMin, ImVec2(AddMin.x + AddW, AddMin.y + BtnH),
                       AddHov ? Theme.Surface3 : CardCol, EdgeCol, 5.5f);
        KitDrawText(Ink, Faces.Medium, 10.4f, ImVec2(AddMin.x + 8.0f, AddMin.y + 4.8f), Theme.Muted, "+ Add");

        CursorY += 28.0f;
    }

    // Spawn menu & Category Filter menu popups
    {
        ImGui::SetNextWindowPos(ImVec2(CR, CursorY), ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(216.0f, 0.0f));
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
                if (DropdownControlSurface::Option(RecordClassificationName(Classification), false, 1.0f))
                {
                    RequestSpawn(Classification);
                    ImGui::CloseCurrentPopup();
                }
            }
            DropdownControlSurface::EndMenu();
        }
    }

    // 5. Search Bar ("Find an object…" + compact Filter button)
    {
        const float RowH    = 32.0f;
        const float FilterW = 28.0f;
        const float SearchW = ContentW - FilterW - 5.0f;
        const float RowCY   = CursorY + RowH * 0.5f;

        KitPillSurface(Ink, ImVec2(CL, CursorY), ImVec2(CL + SearchW, CursorY + RowH), CardCol, EdgeCol, 6.5f);
        Ink->AddCircle(ImVec2(CL + 14.5f, RowCY - 1.2f), 4.5f, Theme.Faint, 20, 1.35f);
        Ink->AddLine(ImVec2(CL + 18.0f, RowCY + 2.2f), ImVec2(CL + 21.5f, RowCY + 5.8f), Theme.Faint, 1.45f);

        if (QueryTextFilter() != FilterSynchronised)
        {
            std::snprintf(FilterBuffer.data(), FilterBuffer.size(), "%s", QueryTextFilter().c_str());
            FilterSynchronised = QueryTextFilter();
        }
        const ImVec4 InputTextCol = ImGui::ColorConvertU32ToFloat4(Theme.Text);
        ImGui::SetCursorScreenPos(ImVec2(CL + 28.0f, CursorY + (RowH - 13.0f) * 0.5f));
        ImGui::PushFont(Faces.Regular, 12.2f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, InputTextCol);
        ImGui::SetNextItemWidth(std::max(30.0f, SearchW - 34.0f));
        if (ImGui::InputTextWithHint("##OutlinerFilter", "Find an object\xE2\x80\xA6", FilterBuffer.data(),
                                     FilterBuffer.size()))
        {
            ConfigureTextFilter(FilterBuffer.data());
            FilterSynchronised = QueryTextFilter();
        }
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);
        ImGui::PopFont();

        // Compact Classification Filter Button
        const ImVec2 FltMin(CR - FilterW, CursorY);
        const bool   HasClassFilter = QueryClassificationFilter().has_value();
        bool         FltHov         = false;
        if (KitTarget("##OutlinerClassFilterBtn", FltMin, ImVec2(FilterW, RowH), FltHov, "Filter by classification"))
        {
            ImGui::OpenPopup("##OutlinerFilterPopup");
        }
        KitPillSurface(Ink, FltMin, ImVec2(FltMin.x + FilterW, FltMin.y + RowH),
                       HasClassFilter ? Theme.MintDim : (FltHov ? Theme.Surface3 : CardCol),
                       HasClassFilter ? Theme.Mint : EdgeCol, 6.5f);
        const ImU32 FunnelCol = HasClassFilter ? Theme.Mint : Theme.Muted;
        Ink->AddLine(ImVec2(FltMin.x + 8.0f,  RowCY - 4.0f), ImVec2(FltMin.x + 20.0f, RowCY - 4.0f), FunnelCol, 1.4f);
        Ink->AddLine(ImVec2(FltMin.x + 10.5f, RowCY),        ImVec2(FltMin.x + 17.5f, RowCY),        FunnelCol, 1.4f);
        Ink->AddLine(ImVec2(FltMin.x + 12.5f, RowCY + 4.0f), ImVec2(FltMin.x + 15.5f, RowCY + 4.0f), FunnelCol, 1.4f);

        ImGui::SetNextWindowPos(ImVec2(CR, CursorY + RowH + 4.0f), ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(210.0f, 0.0f));
        if (DropdownControlSurface::BeginMenu("##OutlinerFilterPopup", 1.0f))
        {
            MenuCaption(Faces, "FILTER CLASSIFICATION");
            if (DropdownControlSurface::Option("All objects", !HasClassFilter, 1.0f))
            {
                ConfigureClassificationFilter(std::nullopt);
                ImGui::CloseCurrentPopup();
            }
            for (const SceneClassificationTab& Tab : Tabs)
            {
                if (Tab.Count == 0 && QueryClassificationFilter() != Tab.Classification)
                {
                    continue;
                }
                char Label[64] = {};
                std::snprintf(Label, sizeof(Label), "%s (%u)", RecordClassificationName(Tab.Classification), Tab.Count);
                if (DropdownControlSurface::Option(Label, QueryClassificationFilter() == Tab.Classification, 1.0f))
                {
                    ConfigureClassificationFilter(Tab.Classification);
                    ImGui::CloseCurrentPopup();
                }
            }
            DropdownControlSurface::EndMenu();
        }

        CursorY += RowH + 8.0f;
    }

    // 6. Scrollable Scene Tree (.tree in FrontierEditor.css)
    const float BottomCardH = 42.0f;
    const float TreeBottom  = PB - 10.0f - BottomCardH - 8.0f;
    const float TreeH       = std::max(40.0f, TreeBottom - CursorY);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::SetCursorScreenPos(ImVec2(CL, CursorY));
    if (ImGui::BeginChild("##OutlinerRows", ImVec2(ContentW, TreeH), false))
    {
        ImDrawList* TreeInk     = ImGui::GetWindowDrawList();
        const auto& VisibleRows = QueryVisible();
        bool        RefreshNeeded = false;

        for (const SceneDirectoryRow& Row : VisibleRows)
        {
            const ImVec2 At       = ImGui::GetCursorScreenPos();
            const bool   Selected = (QuerySelected() == Row.Token);
            const ImU32  Accent   = Row.AccentColour != 0 ? static_cast<ImU32>(Row.AccentColour)
                                                          : ClassificationColour(Row.Classification);

            if (Row.Classification == RecordClassification::Folder)
            {
                const float RowH   = 28.0f;
                const float Indent = static_cast<float>(Row.DepthTier) * 12.0f;
                const float RX     = CL + Indent;
                const float RW     = std::max(40.0f, ContentW - Indent);
                const float CY     = At.y + RowH * 0.5f;

                bool ChevHov = false, EyeHov = false, MainHov = false;
                char CId[48] = {}, EId[48] = {}, MId[48] = {};
                std::snprintf(CId, sizeof(CId), "##FldChev%llu", static_cast<unsigned long long>(Row.Token.Value));
                std::snprintf(EId, sizeof(EId), "##FldEye%llu", static_cast<unsigned long long>(Row.Token.Value));
                std::snprintf(MId, sizeof(MId), "##FldMain%llu", static_cast<unsigned long long>(Row.Token.Value));

                const bool ChevClicked = KitTarget(CId, ImVec2(RX, At.y), ImVec2(18.0f, RowH), ChevHov);
                const bool EyeClicked  = KitTarget(EId, ImVec2(CL + ContentW - 22.0f, At.y), ImVec2(22.0f, RowH), EyeHov);
                const bool MainClicked = KitTarget(MId, ImVec2(RX + 18.0f, At.y), ImVec2(std::max(10.0f, RW - 40.0f), RowH), MainHov);

                if (Selected || MainHov)
                {
                    KitPillSurface(TreeInk, ImVec2(RX, At.y + 1.0f), ImVec2(CL + ContentW, At.y + RowH - 1.0f),
                                   Selected ? Theme.MintDim : Theme.RowHover,
                                   Selected ? KitInkWithAlpha(Theme.Mint, 0.45f) : 0u, 5.0f);
                }

                // Chevron
                const ImVec2 ChevC(RX + 8.0f, CY);
                if (Row.Collapsed)
                {
                    TreeInk->AddLine(ImVec2(ChevC.x - 2.0f, ChevC.y - 3.5f), ImVec2(ChevC.x + 2.0f, ChevC.y), Theme.Muted, 1.35f);
                    TreeInk->AddLine(ImVec2(ChevC.x + 2.0f, ChevC.y), ImVec2(ChevC.x - 2.0f, ChevC.y + 3.5f), Theme.Muted, 1.35f);
                }
                else
                {
                    TreeInk->AddLine(ImVec2(ChevC.x - 3.5f, ChevC.y - 1.8f), ImVec2(ChevC.x, ChevC.y + 2.2f), Theme.Muted, 1.35f);
                    TreeInk->AddLine(ImVec2(ChevC.x, ChevC.y + 2.2f), ImVec2(ChevC.x + 3.5f, ChevC.y - 1.8f), Theme.Muted, 1.35f);
                }

                // Uppercase Group Label + Child Count matching EditorShell.js .tree-group
                std::string UpperName = Row.Name;
                for (char& Ch : UpperName)
                {
                    if (Ch >= 'a' && Ch <= 'z') Ch = static_cast<char>(Ch - 'a' + 'A');
                }
                const float LabelW = KitDrawTracked(TreeInk, Faces.SemiBold, 9.6f, ImVec2(RX + 19.0f, CY - 5.0f),
                                                    Row.Visible ? Theme.Muted : Theme.Faint, UpperName.c_str(), 1.15f);
                char SubCount[16] = {};
                std::snprintf(SubCount, sizeof(SubCount), "%u", Row.ChildCount);
                KitDrawText(TreeInk, Faces.Regular, 9.8f, ImVec2(RX + 25.0f + LabelW, CY - 5.0f), Theme.Faint, SubCount);

                DrawEyeIcon(TreeInk, ImVec2(CL + ContentW - 11.0f, CY), Row.Visible,
                            EyeHov ? Theme.Text : (Row.Visible ? Theme.Faint : KitInkWithAlpha(Theme.Faint, 0.55f)));

                if (ChevClicked)
                {
                    ConfigureCollapsed(Row.Token, !Row.Collapsed);
                }
                else if (EyeClicked)
                {
                    (void)Directory.ConfigureCascadeVisible(Row.Token, !Row.Visible);
                    RefreshNeeded = true;
                }
                else if (MainClicked)
                {
                    Select(Row.Token);
                    RefreshNeeded = true;
                }

                ImGui::SetCursorScreenPos(ImVec2(CL, At.y + RowH + 2.0f));
                ImGui::Dummy(ImVec2(ContentW, 0.0f));
            }
            else
            {
                const float RowH      = 34.0f;
                const int   NestDepth = std::max(0, static_cast<int>(Row.DepthTier) - 1);
                const float Indent    = static_cast<float>(NestDepth) * 15.0f;
                const float RX        = CL + Indent;
                const float CY        = At.y + RowH * 0.5f;

                bool ChevHov = false, EyeHov = false, MainHov = false;
                char CId[48] = {}, EId[48] = {}, MId[48] = {};
                std::snprintf(CId, sizeof(CId), "##RowChev%llu", static_cast<unsigned long long>(Row.Token.Value));
                std::snprintf(EId, sizeof(EId), "##RowEye%llu", static_cast<unsigned long long>(Row.Token.Value));
                std::snprintf(MId, sizeof(MId), "##RowMain%llu", static_cast<unsigned long long>(Row.Token.Value));

                const bool HasKids     = Row.ChildCount > 0u;
                const bool ChevClicked = HasKids ? KitTarget(CId, ImVec2(RX, At.y), ImVec2(14.0f, RowH), ChevHov) : false;
                const bool EyeClicked  = KitTarget(EId, ImVec2(CL + ContentW - 22.0f, At.y), ImVec2(22.0f, RowH), EyeHov);
                const float MainLeft   = RX + (HasKids ? 14.0f : 2.0f);
                const bool MainClicked = KitTarget(MId, ImVec2(MainLeft, At.y),
                                                   ImVec2(std::max(10.0f, (CL + ContentW - 22.0f) - MainLeft), RowH), MainHov);

                if (Selected || MainHov)
                {
                    KitPillSurface(TreeInk, ImVec2(RX, At.y + 1.0f), ImVec2(CL + ContentW, At.y + RowH - 1.0f),
                                   Selected ? (Theme.LightCanvas ? Theme.MintDim : KitInkRgba(47, 230, 167, 22)) : Theme.RowHover,
                                   Selected ? KitInkWithAlpha(Theme.Mint, 0.40f) : 0u, 6.0f);
                    if (Selected)
                    {
                        TreeInk->AddRectFilled(ImVec2(RX, At.y + 6.0f), ImVec2(RX + 2.5f, At.y + RowH - 6.0f), Theme.Mint, 2.0f);
                    }
                }

                if (HasKids)
                {
                    const ImVec2 ChevC(RX + 7.0f, CY);
                    if (Row.Collapsed)
                    {
                        TreeInk->AddLine(ImVec2(ChevC.x - 1.8f, ChevC.y - 3.0f), ImVec2(ChevC.x + 1.8f, ChevC.y), Theme.Muted, 1.3f);
                        TreeInk->AddLine(ImVec2(ChevC.x + 1.8f, ChevC.y), ImVec2(ChevC.x - 1.8f, ChevC.y + 3.0f), Theme.Muted, 1.3f);
                    }
                    else
                    {
                        TreeInk->AddLine(ImVec2(ChevC.x - 3.0f, ChevC.y - 1.5f), ImVec2(ChevC.x, ChevC.y + 2.0f), Theme.Muted, 1.3f);
                        TreeInk->AddLine(ImVec2(ChevC.x, ChevC.y + 2.0f), ImVec2(ChevC.x + 3.0f, ChevC.y - 1.5f), Theme.Muted, 1.3f);
                    }
                }

                const float IconX = RX + (HasKids ? 22.0f : 14.0f);
                DrawRecordGlyph(TreeInk, ImVec2(IconX, CY), Row.Classification,
                                Row.Visible ? Accent : KitInkWithAlpha(Accent, 0.38f));

                const float TextX = IconX + 13.0f;
                KitDrawText(TreeInk, Selected ? Faces.SemiBold : Faces.Medium, 12.2f, ImVec2(TextX, CY - 6.5f),
                            Row.Visible ? Theme.Text : Theme.Muted, Row.Name.c_str());

                // Right-aligned live telemetry meta + live dot + eye toggle
                const float EyeX = CL + ContentW - 11.0f;
                DrawEyeIcon(TreeInk, ImVec2(EyeX, CY), Row.Visible,
                            EyeHov ? Theme.Text : (Row.Visible ? Theme.Faint : KitInkWithAlpha(Theme.Faint, 0.45f)));

                float RightCursor = CL + ContentW - 25.0f;
                if (Row.Visible)
                {
                    TreeInk->AddCircleFilled(ImVec2(RightCursor - 3.0f, CY), 2.5f, Theme.Mint);
                    RightCursor -= 10.0f;
                }
                if (!Row.LiveBadge.empty())
                {
                    const float MetaW = KitTextWidth(Faces.Regular, 10.2f, Row.LiveBadge.c_str());
                    if (RightCursor - MetaW > TextX + KitTextWidth(Faces.Medium, 12.2f, Row.Name.c_str()) + 8.0f)
                    {
                        KitDrawText(TreeInk, Faces.Regular, 10.2f, ImVec2(RightCursor - MetaW, CY - 5.2f),
                                    Theme.Muted, Row.LiveBadge.c_str());
                    }
                }

                if (ChevClicked)
                {
                    ConfigureCollapsed(Row.Token, !Row.Collapsed);
                }
                else if (EyeClicked)
                {
                    (void)Directory.ConfigureVisible(Row.Token, !Row.Visible);
                    RefreshNeeded = true;
                }
                else if (MainClicked)
                {
                    Select(Row.Token);
                    RefreshNeeded = true;
                }

                ImGui::SetCursorScreenPos(ImVec2(CL, At.y + RowH + 1.0f));
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

    // 7. Bottom IlluminationHost Status Card (.outliner-bottom in EditorShell.js)
    {
        const ImVec2 CardMin(CL, PB - 10.0f - BottomCardH);
        const ImVec2 CardMax(CR, PB - 10.0f);
        KitPillSurface(Ink, CardMin, CardMax, CardCol, EdgeCol, 7.0f);

        const ImVec2 BoxC(CardMin.x + 19.0f, (CardMin.y + CardMax.y) * 0.5f);
        Ink->AddQuad(ImVec2(BoxC.x, BoxC.y - 7.0f), ImVec2(BoxC.x + 6.5f, BoxC.y - 3.2f),
                     ImVec2(BoxC.x, BoxC.y + 0.5f), ImVec2(BoxC.x - 6.5f, BoxC.y - 3.2f), Theme.Mint, 1.3f);
        Ink->AddLine(ImVec2(BoxC.x - 6.5f, BoxC.y - 3.2f), ImVec2(BoxC.x - 6.5f, BoxC.y + 3.8f), Theme.Mint, 1.3f);
        Ink->AddLine(ImVec2(BoxC.x + 6.5f, BoxC.y - 3.2f), ImVec2(BoxC.x + 6.5f, BoxC.y + 3.8f), Theme.Mint, 1.3f);
        Ink->AddLine(ImVec2(BoxC.x, BoxC.y + 0.5f),        ImVec2(BoxC.x, BoxC.y + 7.2f),        Theme.Mint, 1.3f);
        Ink->AddLine(ImVec2(BoxC.x - 6.5f, BoxC.y + 3.8f), ImVec2(BoxC.x, BoxC.y + 7.2f),        Theme.Mint, 1.3f);
        Ink->AddLine(ImVec2(BoxC.x + 6.5f, BoxC.y + 3.8f), ImVec2(BoxC.x, BoxC.y + 7.2f),        Theme.Mint, 1.3f);

        KitDrawText(Ink, Faces.SemiBold, 11.6f, ImVec2(CardMin.x + 36.0f, CardMin.y + 7.5f), Theme.Text, "IlluminationHost");
        KitDrawText(Ink, Faces.Regular, 10.0f, ImVec2(CardMin.x + 36.0f, CardMin.y + 22.5f), Theme.Muted,
                    "Dual-tier RTX / SDF GI");
        Ink->AddCircleFilled(ImVec2(CardMax.x - 14.0f, (CardMin.y + CardMax.y) * 0.5f), 3.2f, Theme.Mint);
    }
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
