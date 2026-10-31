/*==========================================================================================================================================
                                                NATIVE PRESENTATION OF THE OUTLINER PANEL
==========================================================================================================================================*/
// 🧩 1:1 C++ Dark OLED port of Experimental/SlateReference/A/Exhibits/VehicleProvingGrounds/Source/EditorShell.js +
//    Style/FrontierEditor.css: frontier. brand header, project-aware WORKSPACE title, twin Enabled/Disabled count
//    tiles, Find an object... search bar, grouped entity rows with live meta, state dot, Dedicated Editor popup
//    launcher (double-click or [↗] button), and Eye visibility toggle.

#include "SceneDirectoryPanel.h"

#if defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/DropdownControls.h"
#include "Engine/Interface/StackRailControls.h"
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
        const unsigned long V = std::strtoul(Hex.c_str() + 1, &End, 16);
        if (End == Hex.c_str() + 7)
        {
            return KitInkHex(static_cast<int>(V));
        }
    }
    switch (Classification)
    {
        case RecordClassification::Vehicle:      return KitInkHex(0xe1d34a);
        case RecordClassification::Tyre:         return KitInkHex(0xe1d34a);
        case RecordClassification::Environment:  return KitInkHex(0xe8c27a);
        case RecordClassification::Geometry:     return KitInkHex(0x8fb3a4);
        case RecordClassification::Illumination: return KitInkHex(0x9fb4d6);
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
    const ImVec4 Bg = Style.Colors[ImGuiCol_WindowBg];
    return (0.2126f * Bg.x + 0.7152f * Bg.y + 0.0722f * Bg.z) < 0.35f;
}

void KitGlyphLayers(ImDrawList* Ink, ImVec2 C, float R, ImU32 Col)
{
    const ImVec2 Top[4] = {ImVec2(C.x, C.y - R * 0.85f), ImVec2(C.x + R * 0.92f, C.y - R * 0.35f),
                           ImVec2(C.x, C.y + R * 0.15f), ImVec2(C.x - R * 0.92f, C.y - R * 0.35f)};
    Ink->AddPolyline(Top, 4, Col, ImDrawFlags_Closed, 1.35f);
    const ImVec2 Mid[3] = {ImVec2(C.x - R * 0.92f, C.y + R * 0.08f), ImVec2(C.x, C.y + R * 0.55f), ImVec2(C.x + R * 0.92f, C.y + R * 0.08f)};
    Ink->AddPolyline(Mid, 3, Col, 0, 1.3f);
    const ImVec2 Bot[3] = {ImVec2(C.x - R * 0.92f, C.y + R * 0.45f), ImVec2(C.x, C.y + R * 0.92f), ImVec2(C.x + R * 0.92f, C.y + R * 0.45f)};
    Ink->AddPolyline(Bot, 3, Col, 0, 1.3f);
}

void KitGlyphCheck(ImDrawList* Ink, ImVec2 C, float R, ImU32 Col, float Thick)
{
    const ImVec2 Pts[3] = {ImVec2(C.x - R * 0.72f, C.y + R * 0.05f),
                           ImVec2(C.x - R * 0.16f, C.y + R * 0.62f),
                           ImVec2(C.x + R * 0.78f, C.y - R * 0.56f)};
    Ink->AddPolyline(Pts, 3, Col, 0, Thick);
}

void KitGlyphMagnifier(ImDrawList* Ink, ImVec2 C, float R, ImU32 Col, float Thick)
{
    Ink->AddCircle(ImVec2(C.x - R * 0.18f, C.y - R * 0.18f), R * 0.68f, Col, 16, Thick);
    Ink->AddLine(ImVec2(C.x + R * 0.30f, C.y + R * 0.30f),
                 ImVec2(C.x + R * 0.86f, C.y + R * 0.86f), Col, Thick);
}

void KitGlyphChevron(ImDrawList* Ink, ImVec2 C, float R, ImU32 Col, bool Expanded)
{
    if (Expanded)
    {
        const ImVec2 Pts[3] = {ImVec2(C.x - R, C.y - R * 0.45f), ImVec2(C.x, C.y + R * 0.55f), ImVec2(C.x + R, C.y - R * 0.45f)};
        Ink->AddPolyline(Pts, 3, Col, 0, 1.4f);
    }
    else
    {
        const ImVec2 Pts[3] = {ImVec2(C.x - R * 0.45f, C.y - R), ImVec2(C.x + R * 0.55f, C.y), ImVec2(C.x - R * 0.45f, C.y + R)};
        Ink->AddPolyline(Pts, 3, Col, 0, 1.4f);
    }
}

void KitGlyphEye(ImDrawList* Ink, ImVec2 C, float R, ImU32 Col, bool Open)
{
    Ink->AddCircle(C, R * 0.76f, Col, 16, 1.25f);
    if (Open)
    {
        Ink->AddCircleFilled(C, R * 0.32f, Col, 12);
    }
    else
    {
        Ink->AddLine(ImVec2(C.x - R * 0.85f, C.y + R * 0.85f),
                     ImVec2(C.x + R * 0.85f, C.y - R * 0.85f), Col, 1.3f);
    }
}

void KitGlyphPopupWindow(ImDrawList* Ink, ImVec2 C, float R, ImU32 Col)
{
    Ink->AddRect(ImVec2(C.x - R * 0.82f, C.y - R * 0.55f), ImVec2(C.x + R * 0.48f, C.y + R * 0.82f), Col, 1.5f, 0, 1.2f);
    Ink->AddLine(ImVec2(C.x - R * 0.05f, C.y + R * 0.05f), ImVec2(C.x + R * 0.85f, C.y - R * 0.85f), Col, 1.3f);
    Ink->AddLine(ImVec2(C.x + R * 0.30f, C.y - R * 0.85f), ImVec2(C.x + R * 0.85f, C.y - R * 0.85f), Col, 1.3f);
    Ink->AddLine(ImVec2(C.x + R * 0.85f, C.y - R * 0.85f), ImVec2(C.x + R * 0.85f, C.y - R * 0.30f), Col, 1.3f);
}

void KitGlyphEntity(ImDrawList* Ink, ImVec2 C, float R, ImU32 Col, const std::string& Group)
{
    if (Group == "Render")
    {
        Ink->AddRect(ImVec2(C.x - R * 0.85f, C.y - R * 0.65f), ImVec2(C.x + R * 0.85f, C.y + R * 0.45f), Col, 1.8f, 0, 1.35f);
        Ink->AddLine(ImVec2(C.x - R * 0.45f, C.y + R * 0.82f), ImVec2(C.x + R * 0.45f, C.y + R * 0.82f), Col, 1.35f);
    }
    else if (Group == "Environment")
    {
        Ink->AddCircle(C, R * 0.48f, Col, 14, 1.35f);
        for (int K = 0; K < 4; ++K)
        {
            const float A = static_cast<float>(K) * 1.570796f;
            Ink->AddLine(ImVec2(C.x + std::cos(A) * R * 0.68f, C.y + std::sin(A) * R * 0.68f),
                         ImVec2(C.x + std::cos(A) * R * 0.92f, C.y + std::sin(A) * R * 0.92f), Col, 1.25f);
        }
    }
    else if (Group == "Vehicle")
    {
        Ink->AddRect(ImVec2(C.x - R * 0.88f, C.y - R * 0.25f), ImVec2(C.x + R * 0.88f, C.y + R * 0.42f), Col, 2.0f, 0, 1.35f);
        Ink->AddCircleFilled(ImVec2(C.x - R * 0.50f, C.y + R * 0.58f), R * 0.24f, Col, 10);
        Ink->AddCircleFilled(ImVec2(C.x + R * 0.50f, C.y + R * 0.58f), R * 0.24f, Col, 10);
    }
    else if (Group == "Effects")
    {
        Ink->AddTriangle(ImVec2(C.x, C.y - R * 0.88f), ImVec2(C.x + R * 0.68f, C.y + R * 0.55f),
                         ImVec2(C.x - R * 0.68f, C.y + R * 0.55f), Col, 1.35f);
        Ink->AddCircleFilled(ImVec2(C.x, C.y + R * 0.22f), R * 0.24f, Col, 10);
    }
    else if (Group == "Destruction")
    {
        Ink->AddQuad(ImVec2(C.x, C.y - R * 0.86f), ImVec2(C.x + R * 0.82f, C.y),
                     ImVec2(C.x, C.y + R * 0.86f), ImVec2(C.x - R * 0.82f, C.y), Col, 1.35f);
        Ink->AddLine(ImVec2(C.x - R * 0.35f, C.y - R * 0.35f), ImVec2(C.x + R * 0.35f, C.y + R * 0.35f), Col, 1.2f);
    }
    else
    {
        Ink->AddRect(ImVec2(C.x - R * 0.72f, C.y - R * 0.72f), ImVec2(C.x + R * 0.72f, C.y + R * 0.72f), Col, 2.0f, 0, 1.35f);
        Ink->AddCircleFilled(C, R * 0.22f, Col, 8);
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
    SceneDirectory&              Directory = SceneDirectory::Instance();
    const KitFaces               Faces     = ResolveKitFaces();
    const ImGuiStyle&            Style     = ImGui::GetStyle();
    const bool                   DarkOled  = IsDarkWorkspaceTheme(Style);
    static std::array<char, 128> FilterBuffer{};
    static std::string           FilterSynchronised;

    const ImVec2 WinPos  = ImGui::GetWindowPos();
    const ImVec2 WinSize = ImGui::GetWindowSize();
    const ImVec2 CurTop  = ImGui::GetCursorScreenPos();
    const float  PL      = WinPos.x + 14.0f;
    const float  PR      = WinPos.x + WinSize.x - 14.0f;
    const float  PT      = CurTop.y + 6.0f;
    const float  Width   = std::max(120.0f, PR - PL);
    ImDrawList*  Ink     = ImGui::GetWindowDrawList();

    // Exact FrontierEditor.css Dark OLED tokens (--ground:#0a0a0a, --column:#121212, --edge:#262626, --text:#dcdcdc, --on:#3ec46d)
    const ImU32 ColBg    = DarkOled ? KitInkHex(0x111111) : ImGui::ColorConvertFloat4ToU32(Style.Colors[ImGuiCol_WindowBg]);
    const ImU32 TileBg   = DarkOled ? KitInkHex(0x191919) : ImGui::ColorConvertFloat4ToU32(Style.Colors[ImGuiCol_FrameBg]);
    const ImU32 InputBg  = DarkOled ? KitInkHex(0x0c0c0c) : ImGui::ColorConvertFloat4ToU32(Style.Colors[ImGuiCol_FrameBg]);
    const ImU32 HoverBg  = DarkOled ? KitInkHex(0x1f1f1f) : ImGui::ColorConvertFloat4ToU32(Style.Colors[ImGuiCol_FrameBgHovered]);
    const ImU32 SelBg    = DarkOled ? KitInkHex(0x232323) : ImGui::ColorConvertFloat4ToU32(Style.Colors[ImGuiCol_HeaderActive]);
    const ImU32 EdgeCol  = DarkOled ? KitInkHex(0x262626) : ImGui::ColorConvertFloat4ToU32(Style.Colors[ImGuiCol_Border]);
    const ImU32 TextCol  = DarkOled ? KitInkHex(0xdcdcdc) : ImGui::ColorConvertFloat4ToU32(Style.Colors[ImGuiCol_Text]);
    const ImU32 SoftCol  = DarkOled ? KitInkHex(0xb4b4b4) : ImGui::ColorConvertFloat4ToU32(Style.Colors[ImGuiCol_Text]);
    const ImU32 MuteCol  = DarkOled ? KitInkHex(0x7d7d7d) : ImGui::ColorConvertFloat4ToU32(Style.Colors[ImGuiCol_TextDisabled]);
    const ImU32 FaintCol = DarkOled ? KitInkHex(0x5c5c5c) : ImGui::ColorConvertFloat4ToU32(Style.Colors[ImGuiCol_TextDisabled]);
    const ImU32 OnGreen  = KitInkHex(0x3ec46d);

    Ink->AddRectFilled(ImVec2(WinPos.x, CurTop.y - 4.0f), ImVec2(WinPos.x + WinSize.x, WinPos.y + WinSize.y), ColBg);

    // 1. EditorShell.js Brand Row ("frontier." + "ENGINE / 01")
    KitGlyphLayers(Ink, ImVec2(PL + 10.0f, PT + 11.0f), 8.5f, SoftCol);
    KitDrawText(Ink, Faces.SemiBold, 17.5f, ImVec2(PL + 25.0f, PT + 1.0f), TextCol, "frontier");
    const float BrandW = KitTextWidth(Faces.SemiBold, 17.5f, "frontier");
    KitDrawText(Ink, Faces.SemiBold, 17.5f, ImVec2(PL + 25.0f + BrandW, PT + 1.0f), MuteCol, ".");
    const float VerW = KitTextWidth(Faces.Medium, 9.5f, "ENGINE / 01");
    KitDrawTracked(Ink, Faces.Medium, 9.0f, ImVec2(PR - VerW - 6.0f, PT + 6.5f), FaintCol, "ENGINE / 01", 0.8f);

    // 2. WORKSPACE + active .frontier project title
    const float WsY = PT + 28.0f;
    KitDrawTracked(Ink, Faces.Medium, 8.5f, ImVec2(PL + 2.0f, WsY), FaintCol, "WORKSPACE", 1.5f);
    Ink->AddCircleFilled(ImVec2(PR - 6.0f, WsY + 4.5f), 3.0f, OnGreen, 10);
    KitDrawText(Ink, Faces.Medium, 15.0f, ImVec2(PL + 2.0f, WsY + 13.0f), TextCol,
                QueryWorkspaceName().c_str());

    // 3. Twin Count Tiles (Enabled / Disabled) — 1:1 EditorShell.js .count-tiles
    const float TileTop = WsY + 38.0f;
    const float TileH   = 40.0f;
    const float TileGap = 8.0f;
    const float TileW   = (Width - TileGap) * 0.5f;

    auto DrawCountTile = [&](float X, const char* Caption, uint32_t CountVal, bool IsEnabledTile) {
        KitPillSurface(Ink, ImVec2(X, TileTop), ImVec2(X + TileW, TileTop + TileH), TileBg, EdgeCol, 12.0f);
        const ImVec2 IcoC(X + 14.0f, TileTop + 13.0f);
        if (IsEnabledTile)
        {
            Ink->AddCircleFilled(IcoC, 6.5f, IM_COL32(62, 196, 109, 36), 12);
            KitGlyphCheck(Ink, IcoC, 4.2f, OnGreen, 1.5f);
        }
        else
        {
            Ink->AddCircleFilled(IcoC, 6.5f, IM_COL32(140, 140, 140, 28), 12);
            KitGlyphEye(Ink, IcoC, 4.4f, MuteCol, false);
        }
        KitDrawText(Ink, Faces.Regular, 10.0f, ImVec2(X + 10.0f, TileTop + 23.5f), MuteCol, Caption);
        char NumBuf[24] = {};
        std::snprintf(NumBuf, sizeof(NumBuf), "%u", CountVal);
        const float NW = KitTextWidth(Faces.Regular, 17.0f, NumBuf);
        KitDrawText(Ink, Faces.Regular, 17.0f, ImVec2(X + TileW - NW - 11.0f, TileTop + 11.0f), TextCol, NumBuf);
    };
    DrawCountTile(PL, "Enabled", EnabledCounts.Enabled, true);
    DrawCountTile(PL + TileW + TileGap, "Disabled", EnabledCounts.Disabled, false);

    // 4. Outliner Heading + Zero-Padded Entity Count + "+" Spawn Menu
    const float HdgY = TileTop + TileH + 12.0f;
    KitDrawText(Ink, Faces.Medium, 13.0f, ImVec2(PL + 2.0f, HdgY), TextCol, "Outliner");
    char CountPad[16] = {};
    std::snprintf(CountPad, sizeof(CountPad), "%02u", Total);
    KitDrawText(Ink, Faces.Regular, 11.5f, ImVec2(PL + 56.0f, HdgY + 1.0f), FaintCol, CountPad);

    ImGui::SetCursorScreenPos(ImVec2(PR - 22.0f, HdgY - 2.0f));
    ImGui::InvisibleButton("##OutlinerSpawnAdd", ImVec2(22.0f, 20.0f));
    KitPillSurface(Ink, ImVec2(PR - 22.0f, HdgY - 2.0f), ImVec2(PR, HdgY + 18.0f),
                   ImGui::IsItemHovered() ? HoverBg : TileBg, EdgeCol, 6.0f);
    KitDrawText(Ink, Faces.Medium, 12.5f, ImVec2(PR - 14.5f, HdgY), SoftCol, "+");
    if (ImGui::IsItemClicked())
    {
        ImGui::OpenPopup("##OutlinerSpawnMenu");
    }
    if (ImGui::BeginPopup("##OutlinerSpawnMenu"))
    {
        for (uint32_t Idx = 0; Idx < static_cast<uint32_t>(RecordClassification::Count); ++Idx)
        {
            const auto ClassVal = static_cast<RecordClassification>(Idx);
            if (ImGui::MenuItem(RecordClassificationName(ClassVal)))
            {
                RequestSpawn(ClassVal);
            }
        }
        ImGui::EndPopup();
    }

    // 5. "Find an object..." Search Input
    const float SearchTop = HdgY + 23.0f;
    const float SearchH   = 30.0f;

    if (FilterSynchronised != TextFilter)
    {
        std::snprintf(FilterBuffer.data(), FilterBuffer.size(), "%s", TextFilter.c_str());
        FilterSynchronised = TextFilter;
    }
    KitPillSurface(Ink, ImVec2(PL, SearchTop), ImVec2(PR, SearchTop + SearchH), InputBg, EdgeCol, 10.0f);
    KitGlyphMagnifier(Ink, ImVec2(PL + 14.0f, SearchTop + SearchH * 0.5f), 5.0f, MuteCol, 1.35f);

    ImGui::SetCursorScreenPos(ImVec2(PL + 25.0f, SearchTop + 5.0f));
    ImGui::PushItemWidth(Width - 32.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 3.0f));
    if (ImGui::InputTextWithHint("##OutlinerSearchInput", "Find an object...", FilterBuffer.data(), FilterBuffer.size()))
    {
        ConfigureTextFilter(FilterBuffer.data());
        FilterSynchronised = TextFilter;
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::PopItemWidth();

    // 6. Scrollable Grouped Entity Tree (Double-click or [↗] opens Dedicated Entity Editor popup!)
    const float TreeTop    = SearchTop + SearchH + 10.0f;
    const float FooterH    = 46.0f;
    const float TreeBottom = WinPos.y + WinSize.y - FooterH;
    const float TreeH      = std::max(48.0f, TreeBottom - TreeTop);

    ImGui::SetCursorScreenPos(ImVec2(WinPos.x, TreeTop));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    if (ImGui::BeginChild("##OutlinerGroupedList", ImVec2(WinSize.x, TreeH), false))
    {
        ImDrawList* TInk       = ImGui::GetWindowDrawList();
        const float InnerRight = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - 10.0f;

        for (size_t GIdx = 0; GIdx < Groups.size(); ++GIdx)
        {
            const SceneDirectoryGroupSection& Sec = Groups[GIdx];
            if (Sec.Rows.empty())
            {
                continue;
            }

            const bool   GroupFolded = QueryGroupCollapsed(Sec.Group);
            const ImVec2 GStart      = ImGui::GetCursorScreenPos();
            const float  GH          = 24.0f;
            ImGui::PushID(static_cast<int>(GIdx));
            ImGui::SetCursorScreenPos(ImVec2(PL, GStart.y));
            ImGui::InvisibleButton("##GrpHdr", ImVec2(InnerRight - PL, GH));
            if (ImGui::IsItemClicked())
            {
                ConfigureGroupCollapsed(Sec.Group, !GroupFolded);
            }
            KitGlyphChevron(TInk, ImVec2(PL + 6.0f, GStart.y + GH * 0.5f), 3.4f, MuteCol, !GroupFolded);
            KitDrawText(TInk, Faces.Medium, 10.5f, ImVec2(PL + 16.0f, GStart.y + 5.0f), MuteCol, Sec.Group.c_str());
            char GrpCnt[16] = {};
            std::snprintf(GrpCnt, sizeof(GrpCnt), "%u", Sec.Count);
            const float GCW = KitTextWidth(Faces.Regular, 9.5f, GrpCnt);
            KitDrawText(TInk, Faces.Regular, 9.5f, ImVec2(InnerRight - GCW - 4.0f, GStart.y + 6.0f), FaintCol, GrpCnt);
            ImGui::PopID();

            if (GroupFolded && TextFilter.empty())
            {
                continue;
            }

            for (const SceneDirectoryRow& Row : Sec.Rows)
            {
                if (Row.DepthTier > 0 && TextFilter.empty())
                {
                    bool Skip = false;
                    for (const SceneDirectoryRow& Cand : Sec.Rows)
                    {
                        if (Cand.DepthTier < Row.DepthTier && Cand.Collapsed && Cand.ChildCount > 0 &&
                            Directory.QueryParent(Row.Token) == Cand.Token)
                        {
                            Skip = true;
                            break;
                        }
                    }
                    if (Skip)
                    {
                        continue;
                    }
                }

                const ImVec2 RStart = ImGui::GetCursorScreenPos();
                const float  RH     = 34.0f;
                const float  Indent = PL + static_cast<float>(Row.DepthTier) * 14.0f;
                const bool   IsSel  = (Row.Token == Selected);
                const ImU32  AccCol = ParseHexColourOrFallback(Row.Colour, Row.Classification);

                ImGui::PushID(static_cast<int>(Row.Token.Value));

                // Right-edge Eye toggle
                const float EyeX = InnerRight - 20.0f;
                ImGui::SetCursorScreenPos(ImVec2(EyeX, RStart.y + 3.0f));
                ImGui::InvisibleButton("##RowEye", ImVec2(20.0f, RH - 6.0f));
                const bool EyeClicked = ImGui::IsItemClicked();
                if (EyeClicked)
                {
#if defined(FRONTIER_EDITOR_PROFILE)
                    if (const RecordEditorDescription* Desc = Directory.QueryEditorDescription(Row.Token);
                        Desc != nullptr && Desc->Enabled != nullptr)
                    {
                        *Desc->Enabled = !(*Desc->Enabled);
                        if (Desc->Revision != nullptr)
                        {
                            ++(*Desc->Revision);
                        }
                    }
                    else
#endif
                    {
                        Directory.ConfigureVisible(Row.Token, !Row.Visible);
                    }
                }

                // Dedicated Editor Popup button [↗] right beside Eye
                const float PopX = EyeX - 21.0f;
                ImGui::SetCursorScreenPos(ImVec2(PopX, RStart.y + 3.0f));
                ImGui::InvisibleButton("##RowOpenEditor", ImVec2(20.0f, RH - 6.0f));
                const bool PopHov     = ImGui::IsItemHovered();
                const bool PopClicked = ImGui::IsItemClicked();
                if (PopClicked)
                {
                    Select(Row.Token);
#if defined(FRONTIER_EDITOR_PROFILE)
                    if (const RecordEditorDescription* Desc = Directory.QueryEditorDescription(Row.Token);
                        Desc != nullptr && Desc->OpenDedicatedEditor)
                    {
                        Desc->OpenDedicatedEditor();
                    }
#endif
                    RequestOpenDedicatedEditor(Row.StableId);
                }

                // Subtree chevron if parent row
                bool ChevClicked = false;
                if (Row.ChildCount > 0)
                {
                    ImGui::SetCursorScreenPos(ImVec2(Indent, RStart.y + 3.0f));
                    ImGui::InvisibleButton("##RowChev", ImVec2(14.0f, RH - 6.0f));
                    ChevClicked = ImGui::IsItemClicked();
                    if (ChevClicked)
                    {
                        ConfigureCollapsed(Row.Token, !Row.Collapsed);
                    }
                }

                // Main row click (select) + double-click (open Dedicated Entity Editor popup!)
                const float MainX = Indent + (Row.ChildCount > 0 ? 14.0f : 0.0f);
                ImGui::SetCursorScreenPos(ImVec2(MainX, RStart.y));
                ImGui::InvisibleButton("##RowSelect", ImVec2(std::max(32.0f, PopX - MainX - 2.0f), RH));
                const bool RowHov = ImGui::IsItemHovered();
                if (!EyeClicked && !PopClicked && !ChevClicked && ImGui::IsItemClicked())
                {
                    Select(Row.Token);
                }
                if (!EyeClicked && !ChevClicked && RowHov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    Select(Row.Token);
#if defined(FRONTIER_EDITOR_PROFILE)
                    if (const RecordEditorDescription* Desc = Directory.QueryEditorDescription(Row.Token);
                        Desc != nullptr && Desc->OpenDedicatedEditor)
                    {
                        Desc->OpenDedicatedEditor();
                    }
#endif
                    RequestOpenDedicatedEditor(Row.StableId);
                }

                if (IsSel)
                {
                    KitPillSurface(TInk, ImVec2(PL, RStart.y + 1.5f), ImVec2(InnerRight, RStart.y + RH - 1.5f),
                                   SelBg, EdgeCol, 10.0f);
                    TInk->AddRectFilled(ImVec2(PL, RStart.y + 7.0f), ImVec2(PL + 2.5f, RStart.y + RH - 7.0f), AccCol, 1.5f);
                }
                else if (RowHov || PopHov)
                {
                    KitPillSurface(TInk, ImVec2(PL, RStart.y + 1.5f), ImVec2(InnerRight, RStart.y + RH - 1.5f),
                                   HoverBg, 0, 10.0f);
                }

                if (Row.ChildCount > 0)
                {
                    KitGlyphChevron(TInk, ImVec2(Indent + 6.5f, RStart.y + RH * 0.5f), 3.3f, MuteCol, !Row.Collapsed);
                }

                const float GlyX = Indent + (Row.ChildCount > 0 ? 22.0f : 12.0f);
                KitGlyphEntity(TInk, ImVec2(GlyX, RStart.y + RH * 0.5f), 6.2f, Row.Enabled ? AccCol : FaintCol, Sec.Group);

                const ImU32 LabelCol = Row.Enabled ? (IsSel ? KitInkHex(0xf2f2f2) : TextCol) : FaintCol;
                KitDrawText(TInk, IsSel ? Faces.Medium : Faces.Regular, 12.0f,
                            ImVec2(GlyX + 12.0f, RStart.y + 9.0f), LabelCol, Row.Name.c_str());

                // Live status dot + right-aligned Meta
                const float StateDotX = PopX - 8.0f;
                if (Row.Enabled)
                {
                    TInk->AddCircleFilled(ImVec2(StateDotX, RStart.y + RH * 0.5f), 2.5f, OnGreen, 8);
                }
                if (!Row.Meta.empty())
                {
                    const float MW = KitTextWidth(Faces.Regular, 9.5f, Row.Meta.c_str());
                    if (StateDotX - MW - 7.0f > GlyX + 92.0f)
                    {
                        KitDrawText(TInk, Faces.Regular, 9.5f, ImVec2(StateDotX - MW - 7.0f, RStart.y + 10.5f),
                                    MuteCol, Row.Meta.c_str());
                    }
                }

                KitGlyphPopupWindow(TInk, ImVec2(PopX + 10.0f, RStart.y + RH * 0.5f), 5.0f,
                                    PopHov ? KitInkHex(0xf0f0f0) : (IsSel ? SoftCol : FaintCol));
                KitGlyphEye(TInk, ImVec2(EyeX + 10.0f, RStart.y + RH * 0.5f), 5.0f,
                            Row.Enabled ? MuteCol : FaintCol, Row.Enabled);

                ImGui::SetCursorScreenPos(ImVec2(PL, RStart.y + RH));
                ImGui::Dummy(ImVec2(1.0f, 0.0f));
                ImGui::PopID();
            }
            ImGui::Dummy(ImVec2(1.0f, 4.0f));
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // 7. Project status card: the host supplies truthful session state, not VehicleProvingGrounds copy.
    Ink->AddLine(ImVec2(WinPos.x, TreeBottom), ImVec2(WinPos.x + WinSize.x, TreeBottom), EdgeCol, 1.0f);
    KitPillSurface(Ink, ImVec2(PL, TreeBottom + 6.0f), ImVec2(PR, WinPos.y + WinSize.y - 6.0f), TileBg, EdgeCol, 10.0f);
    KitGlyphEntity(Ink, ImVec2(PL + 16.0f, TreeBottom + 23.0f), 6.5f, SoftCol, "Level");
    KitDrawText(Ink, Faces.Medium, 11.0f, ImVec2(PL + 30.0f, TreeBottom + 11.0f), TextCol, "Project workspace");
    KitDrawText(Ink, Faces.Regular, 9.5f, ImVec2(PL + 30.0f, TreeBottom + 24.0f), MuteCol,
                QueryWorkspaceStatus().c_str());
    Ink->AddCircleFilled(ImVec2(PR - 12.0f, TreeBottom + 23.0f), 3.0f, OnGreen, 10);
}

} // namespace Frontier

#else

namespace Frontier
{
void SceneDirectoryPanel::ConstructInterface() {}
} // namespace Frontier

#endif
