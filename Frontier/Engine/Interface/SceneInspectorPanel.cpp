/*====================================================================================================================================
                                                    SCENEINSPECTORPANEL.CPP
====================================================================================================================================*/
// 🧩 1:1 C++ Dark OLED port of Experimental/SlateReference/A/Exhibits/VehicleProvingGrounds/Source/EditorShell.js +
//    EditorControls.js + Style/FrontierEditor.css: Dark OLED #141414 column, Inspector > Group crumb bar, object-header
//    with [Editor ↗] Dedicated Entity Editor launcher + Enabled pill, .property-switches glowing green icon tiles,
//    numbered #1f1f1f .card containers (16px radius, #2f2f2f border), 2-line platinum 3px .control sliders with
//    right-aligned #141414 .number-field boxes, monochrome #111111 .segmented pills, and .inspector-footer.

#include "SceneInspectorPanel.h"

#include "Engine/Interface/SceneDirectoryPanel.h"

#if defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/StackRailControls.h"
#include "Engine/Scene/SceneDirectory.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace Frontier
{

namespace
{

ImU32 ParseInspectorAccent(const std::string& Hex) noexcept
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
    return KitInkHex(0xc5c5c5);
}

bool IsDarkOledTheme(const ImGuiStyle& Style) noexcept
{
    const ImVec4 Bg = Style.Colors[ImGuiCol_WindowBg];
    return (0.2126f * Bg.x + 0.7152f * Bg.y + 0.0722f * Bg.z) < 0.35f;
}

std::string UpperCaseString(std::string S)
{
    for (char& C : S)
    {
        C = static_cast<char>(std::toupper(static_cast<unsigned char>(C)));
    }
    return S;
}

void BumpModelRevision(const SceneInspectorModel& Model) noexcept
{
    if (Model.RevisionPtr != nullptr)
    {
        ++(*Model.RevisionPtr);
    }
}

void DrawTinyCheck(ImDrawList* Ink, ImVec2 C, float R, ImU32 Col)
{
    const ImVec2 Pts[3] = {ImVec2(C.x - R * 0.72f, C.y + R * 0.05f),
                           ImVec2(C.x - R * 0.16f, C.y + R * 0.62f),
                           ImVec2(C.x + R * 0.78f, C.y - R * 0.56f)};
    Ink->AddPolyline(Pts, 3, Col, 0, 1.5f);
}

void DrawPopupLaunchGlyph(ImDrawList* Ink, ImVec2 C, float R, ImU32 Col)
{
    Ink->AddRect(ImVec2(C.x - R * 0.80f, C.y - R * 0.52f), ImVec2(C.x + R * 0.48f, C.y + R * 0.80f), Col, 1.5f, 0, 1.25f);
    Ink->AddLine(ImVec2(C.x - R * 0.05f, C.y + R * 0.05f), ImVec2(C.x + R * 0.84f, C.y - R * 0.84f), Col, 1.35f);
    Ink->AddLine(ImVec2(C.x + R * 0.28f, C.y - R * 0.84f), ImVec2(C.x + R * 0.84f, C.y - R * 0.84f), Col, 1.35f);
    Ink->AddLine(ImVec2(C.x + R * 0.84f, C.y - R * 0.84f), ImVec2(C.x + R * 0.84f, C.y - R * 0.28f), Col, 1.35f);
}

#if defined(FRONTIER_EDITOR_PROFILE)
double ReadScalarField(const RecordEditorScalarField& Field) noexcept
{
    if (Field.Target != nullptr)
    {
        return *Field.Target;
    }
    if (Field.FloatTarget != nullptr)
    {
        return static_cast<double>(*Field.FloatTarget);
    }
    return 0.0;
}

bool WriteScalarField(const RecordEditorScalarField& Field, double Next) noexcept
{
    Next = std::clamp(Next, Field.Minimum, Field.Maximum);
    if (Field.Target != nullptr)
    {
        if (std::abs(*Field.Target - Next) > 1e-12)
        {
            *Field.Target = Next;
            return true;
        }
        return false;
    }
    if (Field.FloatTarget != nullptr)
    {
        const float NextF = static_cast<float>(Next);
        if (std::abs(*Field.FloatTarget - NextF) > 1e-7f)
        {
            *Field.FloatTarget = NextF;
            return true;
        }
    }
    return false;
}
#endif

} // namespace

void ConstructSceneInspectorInterface(SceneDirectoryPanel& Panel)
{
    const SceneInspectorModel& Model    = Panel.QueryInspectorModel();
    const KitFaces             Faces    = ResolveKitFaces();
    const ImGuiStyle&          Style    = ImGui::GetStyle();
    const bool                 DarkOled = IsDarkOledTheme(Style);
    const ImVec2               WinPos   = ImGui::GetWindowPos();
    const ImVec2               CurTop   = ImGui::GetCursorScreenPos();
    const ImVec2               Origin(WinPos.x, CurTop.y);
    const ImVec2               Size(ImGui::GetWindowSize().x,
                                    std::max(64.0f, WinPos.y + ImGui::GetWindowSize().y - CurTop.y));
    ImDrawList*                Ink      = ImGui::GetWindowDrawList();

    // Exact FrontierEditor.css Dark OLED palette
    const ImU32 ColBg    = DarkOled ? KitInkHex(0x131313) : ImGui::ColorConvertFloat4ToU32(Style.Colors[ImGuiCol_WindowBg]);
    const ImU32 CardBg   = DarkOled ? KitInkHex(0x1d1d1d) : ImGui::ColorConvertFloat4ToU32(Style.Colors[ImGuiCol_FrameBg]);
    const ImU32 CardEdge = DarkOled ? KitInkHex(0x2f2f2f) : ImGui::ColorConvertFloat4ToU32(Style.Colors[ImGuiCol_Border]);
    const ImU32 EdgeCol  = DarkOled ? KitInkHex(0x262626) : ImGui::ColorConvertFloat4ToU32(Style.Colors[ImGuiCol_Border]);
    const ImU32 SegBg    = DarkOled ? KitInkHex(0x111111) : ImGui::ColorConvertFloat4ToU32(Style.Colors[ImGuiCol_WindowBg]);
    const ImU32 SegAct   = DarkOled ? KitInkHex(0x2e2e2e) : ImGui::ColorConvertFloat4ToU32(Style.Colors[ImGuiCol_HeaderActive]);
    const ImU32 FieldBg  = DarkOled ? KitInkHex(0x131313) : ImGui::ColorConvertFloat4ToU32(Style.Colors[ImGuiCol_FrameBg]);
    const ImU32 HoverBg  = DarkOled ? KitInkHex(0x292929) : ImGui::ColorConvertFloat4ToU32(Style.Colors[ImGuiCol_FrameBgHovered]);
    const ImU32 TextCol  = DarkOled ? KitInkHex(0xdfdfdf) : ImGui::ColorConvertFloat4ToU32(Style.Colors[ImGuiCol_Text]);
    const ImU32 SoftCol  = DarkOled ? KitInkHex(0xa8a8a8) : ImGui::ColorConvertFloat4ToU32(Style.Colors[ImGuiCol_Text]);
    const ImU32 MuteCol  = DarkOled ? KitInkHex(0x7d7d7d) : ImGui::ColorConvertFloat4ToU32(Style.Colors[ImGuiCol_TextDisabled]);
    const ImU32 FaintCol = DarkOled ? KitInkHex(0x565656) : ImGui::ColorConvertFloat4ToU32(Style.Colors[ImGuiCol_TextDisabled]);
    const ImU32 RailFill = DarkOled ? KitInkHex(0xc8c8c8) : KitInkHex(0x475569);
    const ImU32 RailTrk  = DarkOled ? KitInkHex(0x404040) : IM_COL32(120, 130, 145, 90);
    const ImU32 OnGreen  = KitInkHex(0x3ec46d);
    const ImU32 CompCol  = ParseInspectorAccent(Model.Colour);

    Ink->AddRectFilled(ImVec2(Origin.x, Origin.y - 4.0f), ImVec2(Origin.x + Size.x, Origin.y + Size.y), ColBg);

    // 1. .inspector-top (40 px): "Inspector > Group" on left + "✓ Changes apply live" on right
    constexpr float HeadH = 40.0f;
    Ink->AddLine(ImVec2(Origin.x, Origin.y + HeadH), ImVec2(Origin.x + Size.x, Origin.y + HeadH), EdgeCol, 1.0f);

    if (!Model.HasSelection)
    {
        KitDrawText(Ink, Faces.Regular, 11.5f, ImVec2(Origin.x + 16.0f, Origin.y + 12.0f), MuteCol, "Inspector");
        KitDrawText(Ink, Faces.Regular, 12.0f, ImVec2(Origin.x + 16.0f, Origin.y + 62.0f), MuteCol,
                    "Select an entity in the Outliner (or double-click for its Dedicated Editor).");
        return;
    }

    KitDrawText(Ink, Faces.Regular, 11.0f, ImVec2(Origin.x + 16.0f, Origin.y + 12.5f), SoftCol, "Inspector");
    const float InsW = KitTextWidth(Faces.Regular, 11.0f, "Inspector");
    KitDrawText(Ink, Faces.Regular, 11.0f, ImVec2(Origin.x + 22.0f + InsW, Origin.y + 12.5f), FaintCol, "\xE2\x80\xBA");
    KitDrawText(Ink, Faces.Regular, 11.0f, ImVec2(Origin.x + 32.0f + InsW, Origin.y + 12.5f), MuteCol, Model.Group.c_str());

    const float LiveW = KitTextWidth(Faces.Regular, 10.0f, "Changes apply live");
    const float LiveX = Origin.x + Size.x - LiveW - 16.0f;
    DrawTinyCheck(Ink, ImVec2(LiveX - 9.0f, Origin.y + HeadH * 0.5f), 4.0f, MuteCol);
    KitDrawText(Ink, Faces.Regular, 10.0f, ImVec2(LiveX, Origin.y + 13.0f), MuteCol, "Changes apply live");

    // 2. Scrollable .inspector-content
    const float ScrollTop = Origin.y + HeadH + 1.0f;
    const float ScrollH   = std::max(48.0f, Size.y - HeadH - 2.0f);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 16.0f));
    ImGui::SetCursorScreenPos(ImVec2(Origin.x, ScrollTop));
    if (ImGui::BeginChild("##InspectorScroll", ImVec2(Size.x, ScrollH), false))
    {
        ImDrawList* CInk  = ImGui::GetWindowDrawList();
        const float Left  = Origin.x + 16.0f;
        const float Right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - 16.0f;
        const float Width = std::max(140.0f, Right - Left);

        // .object-header: eyebrow + h1 on left, [Editor ↗] + .enabled-pill on right
        ImVec2 Cur = ImGui::GetCursorScreenPos();
        Cur.x      = Left;
        const std::string Eyebrow = UpperCaseString(Model.Category);
        KitDrawTracked(CInk, Faces.Medium, 8.5f, ImVec2(Left, Cur.y + 2.0f), MuteCol, Eyebrow.c_str(), 1.5f);
        KitDrawText(CInk, Faces.Regular, 18.0f, ImVec2(Left, Cur.y + 16.0f), TextCol, Model.Name.c_str());

        float RightEdgeX = Right;
        if (Model.HasEnabled && Model.EnabledPtr != nullptr)
        {
            const bool  IsOn  = *Model.EnabledPtr;
            const char* Cap   = IsOn ? "Enabled" : "Disabled";
            const float PillW = 74.0f;
            const float PillX = RightEdgeX - PillW;
            const float PillY = Cur.y + 8.0f;
            ImGui::SetCursorScreenPos(ImVec2(PillX, PillY));
            ImGui::InvisibleButton("##InspectorEnableToggle", ImVec2(PillW, 25.0f));
            if (ImGui::IsItemClicked())
            {
                *Model.EnabledPtr = !IsOn;
                BumpModelRevision(Model);
            }
            KitPillSurface(CInk, ImVec2(PillX, PillY), ImVec2(PillX + PillW, PillY + 25.0f),
                           DarkOled ? KitInkHex(0x232323) : CardBg, DarkOled ? KitInkHex(0x3a3a3a) : CardEdge, 12.5f);
            CInk->AddCircleFilled(ImVec2(PillX + 12.0f, PillY + 12.5f), 2.8f, IsOn ? OnGreen : MuteCol, 10);
            KitDrawText(CInk, Faces.Regular, 10.0f, ImVec2(PillX + 20.0f, PillY + 6.5f), SoftCol, Cap);
            RightEdgeX = PillX - 6.0f;
        }

#if defined(FRONTIER_EDITOR_PROFILE)
        // [Editor ↗] Dedicated Entity Editor Popup Launcher Button!
        if (!Model.StableId.empty() || Model.OpenDedicatedEditor)
        {
            const float EdW = 68.0f;
            const float EdX = RightEdgeX - EdW;
            const float EdY = Cur.y + 8.0f;
            ImGui::SetCursorScreenPos(ImVec2(EdX, EdY));
            ImGui::InvisibleButton("##OpenDedicatedEditorBtn", ImVec2(EdW, 25.0f));
            const bool EdHov = ImGui::IsItemHovered();
            KitPillSurface(CInk, ImVec2(EdX, EdY), ImVec2(EdX + EdW, EdY + 25.0f),
                           EdHov ? HoverBg : (DarkOled ? KitInkHex(0x202020) : CardBg),
                           EdHov ? KitInkHex(0x606060) : (DarkOled ? KitInkHex(0x383838) : CardEdge), 12.5f);
            DrawPopupLaunchGlyph(CInk, ImVec2(EdX + 13.0f, EdY + 12.5f), 4.6f, EdHov ? TextCol : SoftCol);
            KitDrawText(CInk, Faces.Medium, 10.0f, ImVec2(EdX + 22.0f, EdY + 6.5f), EdHov ? TextCol : SoftCol, "Editor");
            if (ImGui::IsItemClicked())
            {
                if (Model.OpenDedicatedEditor)
                {
                    Model.OpenDedicatedEditor();
                }
                Panel.RequestOpenDedicatedEditor(Model.StableId);
            }
        }
#endif

        ImGui::SetCursorScreenPos(ImVec2(Left, Cur.y + 46.0f));

        // .section-label: PROPERTIES on left, short description on right/below
        Cur = ImGui::GetCursorScreenPos();
        KitDrawTracked(CInk, Faces.Medium, 8.5f, ImVec2(Left, Cur.y), MuteCol, "PROPERTIES", 1.4f);
        ImGui::SetCursorScreenPos(ImVec2(Left, Cur.y + 15.0f));
        if (!Model.Description.empty())
        {
            ImGui::PushFont(Faces.Regular, 10.5f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(MuteCol));
            ImGui::PushTextWrapPos(Right);
            ImGui::SetCursorScreenPos(ImVec2(Left, ImGui::GetCursorScreenPos().y));
            ImGui::TextUnformatted(Model.Description.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(Width, 8.0f));
        }

#if defined(FRONTIER_EDITOR_PROFILE)
        uint32_t CardCounter = 1u;

        // .property-switches: FrontierEditor.css glowing green icon-tile quick switches (up to 4 tiles)
        const size_t TileSwitchCount = std::min<size_t>(Model.Switches.size(), 4u);
        if (TileSwitchCount > 0)
        {
            Cur = ImGui::GetCursorScreenPos();
            const float TGap = 7.0f;
            const float TW   = (Width - TGap * static_cast<float>(TileSwitchCount - 1)) / static_cast<float>(TileSwitchCount);
            const float TH   = 68.0f;

            for (size_t SIdx = 0; SIdx < TileSwitchCount; ++SIdx)
            {
                const RecordEditorSwitch& Sw = Model.Switches[SIdx];
                if (Sw.Target == nullptr) continue;
                const float TX = Left + static_cast<float>(SIdx) * (TW + TGap);
                const bool  On = *Sw.Target;

                ImGui::PushID(static_cast<int>(SIdx + 2800));
                ImGui::SetCursorScreenPos(ImVec2(TX, Cur.y));
                ImGui::InvisibleButton("##PropSwTile", ImVec2(TW, TH));
                const bool Hov = ImGui::IsItemHovered();
                if (ImGui::IsItemClicked())
                {
                    *Sw.Target = !On;
                    BumpModelRevision(Model);
                }

                const ImU32 TBg   = Hov ? HoverBg : (DarkOled ? KitInkHex(0x1f1f1f) : CardBg);
                const ImU32 TEdge = On ? (DarkOled ? KitInkHex(0x323232) : CardEdge) : KitInkHex(0x493232);
                KitPillSurface(CInk, ImVec2(TX, Cur.y), ImVec2(TX + TW, Cur.y + TH), TBg, TEdge, 14.0f);

                const ImVec2 BadgeMin(TX + (TW - 26.0f) * 0.5f, Cur.y + 8.0f);
                const ImVec2 BadgeMax(BadgeMin.x + 26.0f, BadgeMin.y + 26.0f);
                KitPillSurface(CInk, BadgeMin, BadgeMax, On ? KitInkHex(0x2f6d45) : KitInkHex(0x7c4543), 0, 8.0f);
                if (On)
                {
                    DrawTinyCheck(CInk, ImVec2((BadgeMin.x + BadgeMax.x) * 0.5f, (BadgeMin.y + BadgeMax.y) * 0.5f),
                                  4.8f, KitInkHex(0xd9f5e3));
                }
                else
                {
                    CInk->AddCircle(ImVec2((BadgeMin.x + BadgeMax.x) * 0.5f, (BadgeMin.y + BadgeMax.y) * 0.5f),
                                    4.2f, KitInkHex(0xf2d1cf), 12, 1.4f);
                }

                // Truncate switch label neatly to fit tile width
                std::string Cap = Sw.Label != nullptr ? Sw.Label : "";
                while (Cap.size() > 4 && KitTextWidth(Faces.Regular, 9.5f, Cap.c_str()) > TW - 8.0f)
                {
                    Cap.pop_back();
                }
                const float CW = KitTextWidth(Faces.Regular, 9.5f, Cap.c_str());
                KitDrawText(CInk, Faces.Regular, 9.5f, ImVec2(TX + (TW - CW) * 0.5f, Cur.y + 38.0f), SoftCol, Cap.c_str());

                const char* StTxt = On ? "ON" : "OFF";
                const float SW    = KitTextWidth(Faces.Medium, 8.0f, StTxt);
                KitDrawTracked(CInk, Faces.Medium, 8.0f, ImVec2(TX + (TW - SW) * 0.5f, Cur.y + 52.5f),
                               On ? KitInkHex(0x7fd39d) : KitInkHex(0xcd8580), StTxt, 0.9f);
                ImGui::PopID();
            }
            ImGui::SetCursorScreenPos(ImVec2(Left, Cur.y + TH + 10.0f));
        }

        // 1:1 VehicleProvingGrounds / General UI Live Metric & Instrument Card
        if (Model.ConstructInfographic)
        {
            ImGui::SetCursorScreenPos(ImVec2(Left, ImGui::GetCursorScreenPos().y));
            Model.ConstructInfographic();
            ++CardCounter;
        }

        // Action Buttons Row (.action-row / .action-button)
        if (!Model.Actions.empty())
        {
            Cur = ImGui::GetCursorScreenPos();
            const float Gap  = 7.0f;
            const size_t Cnt = Model.Actions.size();
            const float  BW  = Cnt <= 3 ? (Width - Gap * static_cast<float>(Cnt - 1)) / static_cast<float>(Cnt) : 0.0f;
            float X = Left;
            float Y = Cur.y;
            for (size_t Idx = 0; Idx < Cnt; ++Idx)
            {
                const RecordEditorAction& Act = Model.Actions[Idx];
                if (Act.Label == nullptr || !Act.Invoke) continue;
                const float ThisW = BW > 0.0f ? BW : (KitTextWidth(Faces.Medium, 10.0f, Act.Label) + 22.0f);
                if (BW <= 0.0f && X > Left && X + ThisW > Right)
                {
                    X = Left;
                    Y += 34.0f;
                }
                ImGui::PushID(static_cast<int>(Idx + 1000));
                ImGui::SetCursorScreenPos(ImVec2(X, Y));
                ImGui::InvisibleButton("##ActBtn", ImVec2(ThisW, 28.0f));
                const bool  Hov  = ImGui::IsItemHovered();
                const ImU32 Fill = Act.Tone == 1 ? (Hov ? KitInkHex(0xe2efe7) : KitInkHex(0xcfe0d6))
                                 : (Act.Tone == 2 ? (Hov ? KitInkHex(0x3a2222) : KitInkHex(0x261a1a))
                                                  : (Hov ? HoverBg : (DarkOled ? KitInkHex(0x252525) : CardBg)));
                const ImU32 Brd  = Act.Tone == 1 ? KitInkHex(0xcfe0d6)
                                 : (Act.Tone == 2 ? KitInkHex(0x5a3434) : (DarkOled ? KitInkHex(0x383838) : CardEdge));
                const ImU32 Txt  = Act.Tone == 1 ? KitInkHex(0x1a2a20)
                                 : (Act.Tone == 2 ? KitInkHex(0xf0b0ab) : TextCol);
                KitPillSurface(CInk, ImVec2(X, Y), ImVec2(X + ThisW, Y + 28.0f), Fill, Brd, 9.5f);
                const float TW = KitTextWidth(Faces.Medium, 10.0f, Act.Label);
                KitDrawText(CInk, Faces.Medium, 10.0f, ImVec2(X + (ThisW - TW) * 0.5f, Y + 7.5f), Txt, Act.Label);
                if (ImGui::IsItemClicked())
                {
                    Act.Invoke();
                    BumpModelRevision(Model);
                }
                ImGui::PopID();
                X += ThisW + Gap;
            }
            ImGui::SetCursorScreenPos(ImVec2(Left, Y + 38.0f));
        }

        // Segmented Choice Cards (.card + .segmented: #111111 container, #2e2e2e active monochrome pill)
        if (!Model.ChoiceFields.empty())
        {
            Cur = ImGui::GetCursorScreenPos();
            const float CardH = 38.0f + static_cast<float>(Model.ChoiceFields.size()) * 54.0f;
            KitPillSurface(CInk, ImVec2(Left, Cur.y), ImVec2(Right, Cur.y + CardH), CardBg, CardEdge, 16.0f);

            CInk->AddCircleFilled(ImVec2(Left + 18.0f, Cur.y + 19.0f), 3.2f, CompCol, 10);
            KitDrawText(CInk, Faces.Medium, 11.5f, ImVec2(Left + 28.0f, Cur.y + 11.5f), TextCol, "Configuration & Modes");
            char IdxBuf[8] = {};
            std::snprintf(IdxBuf, sizeof(IdxBuf), "%02u", CardCounter++);
            KitDrawText(CInk, Faces.Regular, 9.0f, ImVec2(Right - 24.0f, Cur.y + 13.0f), FaintCol, IdxBuf);

            for (size_t CIdx = 0; CIdx < Model.ChoiceFields.size(); ++CIdx)
            {
                const RecordEditorChoiceField& CF = Model.ChoiceFields[CIdx];
                if (CF.Target == nullptr || CF.Options.empty()) continue;
                const float BaseY = Cur.y + 34.0f + static_cast<float>(CIdx) * 54.0f;
                KitDrawText(CInk, Faces.Regular, 10.0f, ImVec2(Left + 16.0f, BaseY), SoftCol,
                            CF.Label != nullptr ? CF.Label : "Mode");

                const float BarY = BaseY + 17.0f;
                const float BarH = 30.0f;
                KitPillSurface(CInk, ImVec2(Left + 16.0f, BarY), ImVec2(Right - 16.0f, BarY + BarH),
                               SegBg, EdgeCol, 10.0f);

                const float InnerW = (Width - 32.0f) - 8.0f;
                const float Gap    = 4.0f;
                const float OptW   = (InnerW - Gap * static_cast<float>(CF.Options.size() - 1)) /
                                     static_cast<float>(CF.Options.size());
                ImGui::PushID(static_cast<int>(CIdx + 2000));
                for (size_t OIdx = 0; OIdx < CF.Options.size(); ++OIdx)
                {
                    const RecordEditorChoiceOption& Opt = CF.Options[OIdx];
                    const float OX    = Left + 20.0f + static_cast<float>(OIdx) * (OptW + Gap);
                    const bool  IsAct = (*CF.Target == Opt.Value);
                    ImGui::SetCursorScreenPos(ImVec2(OX, BarY + 3.0f));
                    ImGui::InvisibleButton("##ChoiceOpt", ImVec2(OptW, BarH - 6.0f));
                    if (ImGui::IsItemClicked() && *CF.Target != Opt.Value)
                    {
                        *CF.Target = Opt.Value;
                        BumpModelRevision(Model);
                    }
                    if (IsAct)
                    {
                        KitPillSurface(CInk, ImVec2(OX, BarY + 3.0f), ImVec2(OX + OptW, BarY + BarH - 3.0f),
                                       SegAct, DarkOled ? KitInkHex(0x3e3e3e) : CardEdge, 7.5f);
                    }
                    else if (ImGui::IsItemHovered())
                    {
                        KitPillSurface(CInk, ImVec2(OX, BarY + 3.0f), ImVec2(OX + OptW, BarY + BarH - 3.0f),
                                       HoverBg, 0, 7.5f);
                    }
                    const float TW = KitTextWidth(IsAct ? Faces.Medium : Faces.Regular, 10.0f, Opt.Label);
                    KitDrawText(CInk, IsAct ? Faces.Medium : Faces.Regular, 10.0f,
                                ImVec2(OX + (OptW - TW) * 0.5f, BarY + 8.0f),
                                IsAct ? KitInkHex(0xf2f2f2) : MuteCol, Opt.Label);
                }
                ImGui::PopID();
            }
            ImGui::SetCursorScreenPos(ImVec2(Left, Cur.y + CardH + 10.0f));
        }

        // Remaining Switches (> 4) rendered as .switch-row with green .toggle
        if (Model.Switches.size() > TileSwitchCount)
        {
            Cur = ImGui::GetCursorScreenPos();
            const size_t Rem   = Model.Switches.size() - TileSwitchCount;
            const float  RowH  = 32.0f;
            const float  CardH = 38.0f + static_cast<float>(Rem) * RowH;
            KitPillSurface(CInk, ImVec2(Left, Cur.y), ImVec2(Right, Cur.y + CardH), CardBg, CardEdge, 16.0f);
            CInk->AddCircleFilled(ImVec2(Left + 18.0f, Cur.y + 19.0f), 3.2f, CompCol, 10);
            KitDrawText(CInk, Faces.Medium, 11.5f, ImVec2(Left + 28.0f, Cur.y + 11.5f), TextCol, "Features");
            char IdxBuf[8] = {};
            std::snprintf(IdxBuf, sizeof(IdxBuf), "%02u", CardCounter++);
            KitDrawText(CInk, Faces.Regular, 9.0f, ImVec2(Right - 24.0f, Cur.y + 13.0f), FaintCol, IdxBuf);

            for (size_t SIdx = TileSwitchCount; SIdx < Model.Switches.size(); ++SIdx)
            {
                const RecordEditorSwitch& Sw = Model.Switches[SIdx];
                if (Sw.Target == nullptr) continue;
                const float SY = Cur.y + 34.0f + static_cast<float>(SIdx - TileSwitchCount) * RowH;
                ImGui::PushID(static_cast<int>(SIdx + 3000));
                ImGui::SetCursorScreenPos(ImVec2(Left + 14.0f, SY));
                ImGui::InvisibleButton("##SwRow", ImVec2(Width - 28.0f, RowH - 2.0f));
                if (ImGui::IsItemClicked())
                {
                    *Sw.Target = !(*Sw.Target);
                    BumpModelRevision(Model);
                }
                KitDrawText(CInk, Faces.Regular, 11.0f, ImVec2(Left + 16.0f, SY + 7.0f), TextCol, Sw.Label);

                const float TrackW = 38.0f;
                const float TrackH = 21.0f;
                const float TrackX = Right - TrackW - 16.0f;
                const float TrackY = SY + 4.5f;
                const bool  On     = *Sw.Target;
                KitPillSurface(CInk, ImVec2(TrackX, TrackY), ImVec2(TrackX + TrackW, TrackY + TrackH),
                               On ? OnGreen : KitInkHex(0x383838),
                               On ? KitInkHex(0x53d684) : KitInkHex(0x484848), 10.5f);
                const float KnobX = On ? (TrackX + TrackW - 10.5f) : (TrackX + 10.5f);
                CInk->AddCircleFilled(ImVec2(KnobX, TrackY + TrackH * 0.5f), 7.5f,
                                      On ? KitInkHex(0xf4f4f4) : KitInkHex(0x9c9c9c), 14);
                ImGui::PopID();
            }
            ImGui::SetCursorScreenPos(ImVec2(Left, Cur.y + CardH + 10.0f));
        }

        // Scalar Parameter Cards (.card + EditorControls.js 2-line .control with 3px #c5c5c5 platinum rail!)
        size_t FIdx = 0;
        while (FIdx < Model.ScalarFields.size())
        {
            const std::string SecName = Model.ScalarFields[FIdx].Section != nullptr && Model.ScalarFields[FIdx].Section[0] != '\0'
                                      ? Model.ScalarFields[FIdx].Section
                                      : "Parameters";
            size_t EndIdx = FIdx + 1;
            while (EndIdx < Model.ScalarFields.size())
            {
                const std::string NextSec = Model.ScalarFields[EndIdx].Section != nullptr &&
                                            Model.ScalarFields[EndIdx].Section[0] != '\0'
                                          ? Model.ScalarFields[EndIdx].Section
                                          : "Parameters";
                if (NextSec != SecName) break;
                ++EndIdx;
            }

            Cur = ImGui::GetCursorScreenPos();
            const float RowH  = 46.0f;
            const float CardH = 42.0f + static_cast<float>(EndIdx - FIdx) * RowH;
            KitPillSurface(CInk, ImVec2(Left, Cur.y), ImVec2(Right, Cur.y + CardH), CardBg, CardEdge, 16.0f);

            CInk->AddCircleFilled(ImVec2(Left + 18.0f, Cur.y + 19.0f), 3.2f, CompCol, 10);
            KitDrawText(CInk, Faces.Medium, 11.5f, ImVec2(Left + 28.0f, Cur.y + 11.5f), TextCol, SecName.c_str());
            char IdxBuf[8] = {};
            std::snprintf(IdxBuf, sizeof(IdxBuf), "%02u", CardCounter++);
            KitDrawText(CInk, Faces.Regular, 9.0f, ImVec2(Right - 24.0f, Cur.y + 13.0f), FaintCol, IdxBuf);

            for (size_t K = FIdx; K < EndIdx; ++K)
            {
                const RecordEditorScalarField& Field = Model.ScalarFields[K];
                if (Field.Target == nullptr && Field.FloatTarget == nullptr) continue;

                const float RY  = Cur.y + 36.0f + static_cast<float>(K - FIdx) * RowH;
                double      Val = ReadScalarField(Field);

                // Line 1: Label on left + .number-field (value + unit) on right
                KitDrawText(CInk, Faces.Regular, 10.5f, ImVec2(Left + 16.0f, RY + 2.0f), SoftCol,
                            Field.Label != nullptr ? Field.Label : "");

                const float NumW = 76.0f;
                const float NumX = Right - NumW - 16.0f;
                KitPillSurface(CInk, ImVec2(NumX, RY - 1.0f), ImVec2(NumX + NumW, RY + 20.0f),
                               FieldBg, DarkOled ? KitInkHex(0x333333) : CardEdge, 7.0f);

                char ValStr[32] = {};
                std::snprintf(ValStr, sizeof(ValStr), "%.*f", std::clamp(Field.Digits, 0, 4), Val);
                const bool HasUnit = Field.Unit != nullptr && Field.Unit[0] != '\0' && std::string(Field.Unit) != "-";
                const float UnitW  = HasUnit ? (KitTextWidth(Faces.Regular, 9.0f, Field.Unit) + 4.0f) : 0.0f;
                const float ValW   = KitTextWidth(Faces.Medium, 10.5f, ValStr);
                KitDrawText(CInk, Faces.Medium, 10.5f, ImVec2(NumX + NumW - UnitW - ValW - 7.0f, RY + 2.5f), TextCol, ValStr);
                if (HasUnit)
                {
                    KitDrawText(CInk, Faces.Regular, 9.0f, ImVec2(NumX + NumW - UnitW - 4.0f, RY + 4.0f), MuteCol, Field.Unit);
                }

                // Line 2: Full-width 3px platinum slider rail (#c5c5c5 over #404040) + 11px thumb ringed in #1d1d1d
                const float SliderX = Left + 16.0f;
                const float SliderW = Width - 32.0f;
                const float RailY   = RY + 30.0f;

                ImGui::PushID(static_cast<int>(K + 4000));
                ImGui::SetCursorScreenPos(ImVec2(SliderX, RailY - 9.0f));
                ImGui::InvisibleButton("##ScalarRail", ImVec2(SliderW, 18.0f));
                if (ImGui::IsItemActive())
                {
                    const float T = std::clamp((ImGui::GetIO().MousePos.x - SliderX) / SliderW, 0.0f, 1.0f);
                    double Next   = Field.Minimum + static_cast<double>(T) * (Field.Maximum - Field.Minimum);
                    if (Field.Step > 0.0)
                    {
                        Next = std::round(Next / Field.Step) * Field.Step;
                    }
                    if (WriteScalarField(Field, Next))
                    {
                        Val = ReadScalarField(Field);
                        BumpModelRevision(Model);
                    }
                }

                const float Norm = (Field.Maximum > Field.Minimum)
                    ? static_cast<float>(std::clamp((Val - Field.Minimum) / (Field.Maximum - Field.Minimum), 0.0, 1.0))
                    : 0.0f;
                CInk->AddRectFilled(ImVec2(SliderX, RailY - 1.5f), ImVec2(SliderX + SliderW, RailY + 1.5f), RailTrk, 2.0f);
                if (Norm > 0.004f)
                {
                    CInk->AddRectFilled(ImVec2(SliderX, RailY - 1.5f),
                                        ImVec2(SliderX + SliderW * Norm, RailY + 1.5f), RailFill, 2.0f);
                }
                const ImVec2 ThumbC(SliderX + SliderW * Norm, RailY);
                CInk->AddCircleFilled(ThumbC, 8.0f, CardBg, 16);
                CInk->AddCircleFilled(ThumbC, 5.2f, KitInkHex(0xe8e8e8), 16);
                ImGui::PopID();
            }
            ImGui::SetCursorScreenPos(ImVec2(Left, Cur.y + CardH + 10.0f));
            FIdx = EndIdx;
        }
#endif

        // .inspector-footer
        Cur = ImGui::GetCursorScreenPos();
        CInk->AddCircleFilled(ImVec2(Left + 4.0f, Cur.y + 8.0f), 2.2f, MuteCol, 8);
        KitDrawText(CInk, Faces.Regular, 9.5f, ImVec2(Left + 12.0f, Cur.y + 2.0f), MuteCol, "Changes apply in real time");
        const std::string RightFoot = Model.Name + " / " + Model.Category;
        const float RFW = KitTextWidth(Faces.Regular, 9.5f, RightFoot.c_str());
        if (Right - RFW > Left + 150.0f)
        {
            KitDrawText(CInk, Faces.Regular, 9.5f, ImVec2(Right - RFW, Cur.y + 2.0f), MuteCol, RightFoot.c_str());
        }
        ImGui::Dummy(ImVec2(Width, 20.0f));
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

} // namespace Frontier

#else

namespace Frontier
{
void ConstructSceneInspectorInterface(SceneDirectoryPanel&) {}
} // namespace Frontier

#endif
