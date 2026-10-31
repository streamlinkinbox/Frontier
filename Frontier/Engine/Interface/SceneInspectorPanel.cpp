/*==========================================================================================================================================
                                                        SCENE INSPECTOR PANEL
                                     ------------------------------------------------------------------
           Global Engine Inspector host: renders Folder, Sun, Lens Flare, Atmosphere, Clouds, Wind, and custom inspectors.
==========================================================================================================================================*/

// 🧩 Global Engine Inspector host: renders Folder, Sun, Lens Flare, Atmosphere, Clouds, Wind, and custom inspectors.

#include "SceneInspectorPanel.h"

#if defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/SceneDirectoryPanel.h"
#include "Engine/Interface/StackRailControls.h"
#include "Engine/Rendering/AtmosphereModel.h"
#include "Engine/Scene/EnvironmentRecords.h"
#include "Engine/Scene/RecordStore.h"
#include "Engine/Scene/SceneDirectory.h"
#include "SceneInspectorPanelRegistration.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace Frontier
{

DockPanelSpecification MakeSceneInspectorPanelSpecification(SceneInspectorPanel& InspectorPanel,
                                                            SceneDirectoryPanel& DirectoryPanel)
{
    DockPanelSpecification Specification;
    Specification.Key              = "Inspector";
    Specification.Caption          = "Inspector";
    Specification.ConstructContent = [&InspectorPanel, &DirectoryPanel]() {
        InspectorPanel.ConstructInterface(DirectoryPanel);
    };
    Specification.Open = true;
    return Specification;
}

#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)

namespace
{

constexpr float PiFloat = 3.14159265f;

ImU32 ToneMappedLinearRgb(SpectrumRgb Radiance, float Exposure = 1.0f) noexcept
{
    auto MapChannel = [Exposure](double Linear) noexcept -> int {
        const double Scaled = std::max(0.0, Linear * static_cast<double>(Exposure));
        const double Mapped = Scaled / (1.0 + Scaled);
        const double Srgb   = std::pow(Mapped, 1.0 / 2.2);
        return std::clamp(static_cast<int>(Srgb * 255.0 + 0.5), 0, 255);
    };
    return KitInkRgba(MapChannel(Radiance.R), MapChannel(Radiance.G), MapChannel(Radiance.B), 255);
}

ImU32 SrgbSwatch(double LinearRed, double LinearGreen, double LinearBlue) noexcept
{
    auto Convert = [](double Channel) noexcept -> int {
        const double Clamped = std::clamp(Channel, 0.0, 1.0);
        return std::clamp(static_cast<int>(std::pow(Clamped, 1.0 / 2.2) * 255.0 + 0.5), 0, 255);
    };
    return KitInkRgba(Convert(LinearRed), Convert(LinearGreen), Convert(LinearBlue), 255);
}

std::string FormatSolarClock(double Hours)
{
    int TotalMinutes = static_cast<int>(std::lround(Hours * 60.0)) % 1440;
    if (TotalMinutes < 0)
    {
        TotalMinutes += 1440;
    }
    char Buffer[16] = {};
    std::snprintf(Buffer, sizeof(Buffer), "%02d:%02d", TotalMinutes / 60, TotalMinutes % 60);
    return std::string(Buffer);
}

struct ObservatoryFrame
{
    KitFaces    Faces;
    KitTheme    Theme;
    ImDrawList* Ink    = nullptr;
    float       Left   = 0.0f;
    float       Width  = 0.0f;
    ImU32       CanvasBg   = 0;
    ImU32       CanvasGrid = 0;
};

ObservatoryFrame MakeObservatoryFrame(float Left, float Width) noexcept
{
    ObservatoryFrame Frame;
    Frame.Faces      = ResolveKitFaces();
    Frame.Theme      = ResolveKitTheme();
    Frame.Ink        = ImGui::GetWindowDrawList();
    Frame.Left       = Left;
    Frame.Width      = std::max(120.0f, Width);
    Frame.CanvasBg   = KitInkHex(0x0b1012);
    Frame.CanvasGrid = KitInkRgba(205, 224, 212, 26);
    return Frame;
}

void AdvanceLayout(float Left, float VerticalAdvance, float Width)
{
    const ImVec2 Current = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(Left, Current.y + VerticalAdvance));
    ImGui::Dummy(ImVec2(Width, 0.0f));
}

bool DrawObservatoryPowerPill(const ObservatoryFrame& Frame, const char* Id, ImVec2 TopRight,
                              const char* OnCaption, const char* OffCaption, bool Active, ImU32 Accent)
{
    const char* Label  = Active ? OnCaption : OffCaption;
    const float TextW  = KitTextWidth(Frame.Faces.Medium, 11.5f, Label);
    const float PillW  = 12.0f + 8.0f + 6.0f + TextW + 12.0f;
    const ImVec2 Min(TopRight.x - PillW, TopRight.y);
    const ImVec2 Max(TopRight.x, TopRight.y + 26.0f);
    bool         Hovered = false;
    const bool   Clicked = KitTarget(Id, Min, ImVec2(PillW, 26.0f), Hovered);
    KitPillSurface(Frame.Ink, Min, Max,
                   Active ? KitInkWithAlpha(Accent, 0.16f)
                          : (Hovered ? Frame.Theme.Surface3 : Frame.Theme.Surface2),
                   Active ? KitInkWithAlpha(Accent, 0.55f) : Frame.Theme.Line, 999.0f);
    Frame.Ink->AddCircleFilled(ImVec2(Min.x + 14.0f, Min.y + 13.0f), 3.6f, Active ? Accent : Frame.Theme.Faint);
    KitDrawText(Frame.Ink, Frame.Faces.Medium, 11.5f, ImVec2(Min.x + 24.0f, Min.y + 6.8f),
                Active ? Frame.Theme.Text : Frame.Theme.Muted, Label);
    return Clicked;
}

bool DrawObservatoryActionButton(const ObservatoryFrame& Frame, const char* Id, const char* Caption, ImU32 Accent)
{
    const ImVec2 Origin  = ImGui::GetCursorScreenPos();
    const float  Height  = 30.0f;
    bool         Hovered = false;
    const bool   Clicked = KitTarget(Id, ImVec2(Frame.Left, Origin.y), ImVec2(Frame.Width, Height), Hovered);
    KitPillSurface(Frame.Ink, ImVec2(Frame.Left, Origin.y), ImVec2(Frame.Left + Frame.Width, Origin.y + Height),
                   Hovered ? Frame.Theme.Surface3 : Frame.Theme.Surface2,
                   Hovered ? KitInkWithAlpha(Accent, 0.55f) : Frame.Theme.Line, 8.5f);
    KitDrawText(Frame.Ink, Frame.Faces.Medium, 12.0f, ImVec2(Frame.Left + 12.0f, Origin.y + 8.5f),
                Hovered ? Frame.Theme.Text : Accent, Caption);
    ImGui::SetCursorScreenPos(ImVec2(Frame.Left, Origin.y + Height + 8.0f));
    ImGui::Dummy(ImVec2(Frame.Width, 0.0f));
    return Clicked;
}

void DrawObservatoryHeader(const ObservatoryFrame& Frame, const char* Eyebrow, const char* Title,
                           const char* Subtitle, const char* PowerId, bool* EnabledTarget, ImU32 Accent,
                           const char* OnText = "Live", const char* OffText = "Off")
{
    const ImVec2 Origin = ImGui::GetCursorScreenPos();
    KitDrawTracked(Frame.Ink, Frame.Faces.Medium, 9.8f, ImVec2(Frame.Left, Origin.y), Accent, Eyebrow, 1.2f);

    const float TitleY = Origin.y + 16.0f;
    KitDrawText(Frame.Ink, Frame.Faces.SemiBold, 22.0f, ImVec2(Frame.Left, TitleY), Frame.Theme.Text, Title);

    if (EnabledTarget != nullptr && PowerId != nullptr)
    {
        if (DrawObservatoryPowerPill(Frame, PowerId, ImVec2(Frame.Left + Frame.Width, TitleY + 1.0f), OnText,
                                     OffText, *EnabledTarget, Accent))
        {
            *EnabledTarget = !(*EnabledTarget);
            SceneDirectory::Instance().SynchronizeEnabledFromDescriptions();
        }
    }

    const float SubY = TitleY + 28.0f;
    KitDrawText(Frame.Ink, Frame.Faces.Regular, 12.0f, ImVec2(Frame.Left, SubY), Frame.Theme.Muted, Subtitle);
    ImGui::SetCursorScreenPos(ImVec2(Frame.Left, SubY + 22.0f));
    ImGui::Dummy(ImVec2(Frame.Width, 0.0f));
}

void DrawObservatoryTabs(const ObservatoryFrame& Frame, const char* IdPrefix,
                         const std::array<const char*, 4>& Labels, uint32_t& ActiveTab, ImU32 Accent)
{
    const ImVec2 Origin = ImGui::GetCursorScreenPos();
    const float  Gap    = 5.0f;
    const float  TabW   = (Frame.Width - Gap * 3.0f) * 0.25f;
    const float  TabH   = 28.0f;

    for (uint32_t Index = 0; Index < 4; ++Index)
    {
        const float X        = Frame.Left + static_cast<float>(Index) * (TabW + Gap);
        const bool  Selected = (ActiveTab == Index);
        bool        Hovered  = false;
        char        Id[64]   = {};
        std::snprintf(Id, sizeof(Id), "##%sTab%u", IdPrefix, Index);
        if (KitTarget(Id, ImVec2(X, Origin.y), ImVec2(TabW, TabH), Hovered))
        {
            ActiveTab = Index;
        }
        KitPillSurface(Frame.Ink, ImVec2(X, Origin.y), ImVec2(X + TabW, Origin.y + TabH),
                       Selected ? KitInkWithAlpha(Accent, 0.20f)
                                : (Hovered ? Frame.Theme.Surface3 : Frame.Theme.Surface2),
                       Selected ? KitInkWithAlpha(Accent, 0.65f) : Frame.Theme.LineSoft, 7.5f);
        const float TextW = KitTextWidth(Frame.Faces.Medium, 11.2f, Labels[Index]);
        KitDrawText(Frame.Ink, Frame.Faces.Medium, 11.2f,
                    ImVec2(X + std::max(4.0f, (TabW - TextW) * 0.5f), Origin.y + 8.0f),
                    Selected ? Frame.Theme.Text : Frame.Theme.Muted, Labels[Index]);
    }
    ImGui::SetCursorScreenPos(ImVec2(Frame.Left, Origin.y + TabH + 10.0f));
    ImGui::Dummy(ImVec2(Frame.Width, 0.0f));
}

void DrawSectionCardHeader(const ObservatoryFrame& Frame, const char* Title, const char* IndexCode, ImU32 Accent)
{
    const ImVec2 Origin = ImGui::GetCursorScreenPos();
    Frame.Ink->AddRectFilled(ImVec2(Frame.Left, Origin.y + 2.0f), ImVec2(Frame.Left + Frame.Width, Origin.y + 3.0f),
                               Frame.Theme.LineSoft);
    const float RowY = Origin.y + 9.0f;
    KitDrawText(Frame.Ink, Frame.Faces.SemiBold, 13.5f, ImVec2(Frame.Left, RowY), Frame.Theme.Text, Title);
    if (IndexCode != nullptr && IndexCode[0] != '\0')
    {
        const float CodeW = KitTextWidth(Frame.Faces.Medium, 9.5f, IndexCode) + 8.0f;
        KitDrawTracked(Frame.Ink, Frame.Faces.Medium, 9.5f,
                       ImVec2(Frame.Left + Frame.Width - CodeW, RowY + 2.0f), Accent, IndexCode, 0.9f);
    }
    ImGui::SetCursorScreenPos(ImVec2(Frame.Left, RowY + 21.0f));
    ImGui::Dummy(ImVec2(Frame.Width, 0.0f));
}

void DrawKpiTriple(const ObservatoryFrame& Frame, const char* Label0, const char* Reading0, const char* Unit0,
                   const char* Label1, const char* Reading1, const char* Unit1, const char* Label2,
                   const char* Reading2, const char* Unit2)
{
    const ImVec2 Origin = ImGui::GetCursorScreenPos();
    const float  Gap    = 6.0f;
    const float  CellW  = (Frame.Width - Gap * 2.0f) / 3.0f;
    const float  CellH  = 50.0f;

    const std::array<const char*, 3> Labels{Label0, Label1, Label2};
    const std::array<const char*, 3> Readings{Reading0, Reading1, Reading2};
    const std::array<const char*, 3> Units{Unit0, Unit1, Unit2};

    for (size_t Index = 0; Index < 3; ++Index)
    {
        const float X = Frame.Left + static_cast<float>(Index) * (CellW + Gap);
        KitPillSurface(Frame.Ink, ImVec2(X, Origin.y), ImVec2(X + CellW, Origin.y + CellH), Frame.Theme.Surface,
                       Frame.Theme.LineSoft, 8.5f);
        KitDrawText(Frame.Ink, Frame.Faces.Regular, 10.2f, ImVec2(X + 9.0f, Origin.y + 7.0f), Frame.Theme.Faint,
                    Labels[Index]);
        KitDrawText(Frame.Ink, Frame.Faces.SemiBold, 14.0f, ImVec2(X + 9.0f, Origin.y + 21.5f), Frame.Theme.Text,
                    Readings[Index]);
        const float ValW = KitTextWidth(Frame.Faces.SemiBold, 14.0f, Readings[Index]);
        if (Units[Index] != nullptr && Units[Index][0] != '\0')
        {
            KitDrawText(Frame.Ink, Frame.Faces.Regular, 10.0f, ImVec2(X + 12.0f + ValW, Origin.y + 24.5f),
                        Frame.Theme.Muted, Units[Index]);
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(Frame.Left, Origin.y + CellH + 8.0f));
    ImGui::Dummy(ImVec2(Frame.Width, 0.0f));
}

// 📝 Native C++ port of ObservatorySlider.js: label + formatted numeric readout above a wide interactive track.
bool DrawObservatorySlider(const ObservatoryFrame& Frame, const char* Id, const char* Label, double& Target,
                           double Minimum, double Maximum, double Step, const char* Unit, int32_t Digits, ImU32 Accent)
{
    const ImVec2 Origin  = ImGui::GetCursorScreenPos();
    const float  Height  = 38.0f;
    const float  TrackY  = Origin.y + 26.0f;
    const float  TrackL  = Frame.Left + 2.0f;
    const float  TrackR  = Frame.Left + Frame.Width - 2.0f;
    const float  TrackW  = std::max(24.0f, TrackR - TrackL);
    bool         Changed = false;

    KitDrawText(Frame.Ink, Frame.Faces.Regular, 11.8f, ImVec2(Frame.Left, Origin.y + 2.0f), Frame.Theme.Muted,
                Label);

    char Readout[64] = {};
    std::snprintf(Readout, sizeof(Readout), "%.*f%s%s", std::clamp(Digits, 0, 4), Target,
                  (Unit != nullptr && Unit[0] != '\0') ? " " : "", Unit != nullptr ? Unit : "");
    const float ReadoutW = KitTextWidth(Frame.Faces.Medium, 11.8f, Readout);
    KitDrawText(Frame.Ink, Frame.Faces.Medium, 11.8f, ImVec2(Frame.Left + Frame.Width - ReadoutW, Origin.y + 2.0f),
                Frame.Theme.Text, Readout);

    ImGui::SetCursorScreenPos(ImVec2(TrackL, TrackY - 8.0f));
    ImGui::InvisibleButton(Id, ImVec2(TrackW, 16.0f));
    const bool Active  = ImGui::IsItemActive();
    const bool Hovered = ImGui::IsItemHovered();
    if (Active && Maximum > Minimum)
    {
        const float Normalized = std::clamp((ImGui::GetIO().MousePos.x - TrackL) / TrackW, 0.0f, 1.0f);
        double      Proposed   = Minimum + static_cast<double>(Normalized) * (Maximum - Minimum);
        if (Step > 0.0)
        {
            Proposed = std::round(Proposed / Step) * Step;
        }
        Proposed = std::clamp(Proposed, Minimum, Maximum);
        if (std::abs(Proposed - Target) > 1e-9)
        {
            Target  = Proposed;
            Changed = true;
        }
    }

    const float Fraction = Maximum > Minimum
                               ? static_cast<float>(std::clamp((Target - Minimum) / (Maximum - Minimum), 0.0, 1.0))
                               : 0.0f;
    const float FillX    = TrackL + Fraction * TrackW;
    Frame.Ink->AddRectFilled(ImVec2(TrackL, TrackY - 2.2f), ImVec2(TrackR, TrackY + 2.2f), Frame.Theme.Surface2, 2.2f);
    if (FillX > TrackL + 1.0f)
    {
        Frame.Ink->AddRectFilled(ImVec2(TrackL, TrackY - 2.2f), ImVec2(FillX, TrackY + 2.2f),
                                   KitInkWithAlpha(Accent, 0.85f), 2.2f);
    }
    Frame.Ink->AddCircleFilled(ImVec2(FillX, TrackY), Active || Hovered ? 6.2f : 5.0f, Accent);
    Frame.Ink->AddCircle(ImVec2(FillX, TrackY), Active || Hovered ? 6.2f : 5.0f, Frame.Theme.PanelBg, 16, 1.3f);

    ImGui::SetCursorScreenPos(ImVec2(Frame.Left, Origin.y + Height));
    ImGui::Dummy(ImVec2(Frame.Width, 0.0f));
    return Changed;
}

bool DrawObservatorySwitchRow(const ObservatoryFrame& Frame, const char* Id, const char* Label, bool& Target,
                              ImU32 Accent)
{
    const ImVec2 Origin  = ImGui::GetCursorScreenPos();
    const float  Height  = 28.0f;
    bool         Hovered = false;
    const bool   Clicked = KitTarget(Id, ImVec2(Frame.Left, Origin.y), ImVec2(Frame.Width, Height), Hovered);
    if (Clicked)
    {
        Target = !Target;
    }
    KitPillSurface(Frame.Ink, ImVec2(Frame.Left, Origin.y), ImVec2(Frame.Left + Frame.Width, Origin.y + Height),
                   Hovered ? Frame.Theme.Surface3 : Frame.Theme.Surface, Frame.Theme.LineSoft, 7.5f);
    KitDrawText(Frame.Ink, Frame.Faces.Regular, 11.8f, ImVec2(Frame.Left + 10.0f, Origin.y + 7.5f),
                Frame.Theme.Text, Label);

    const float TrackR = Frame.Left + Frame.Width - 10.0f;
    const float TrackL = TrackR - 32.0f;
    const float Cy     = Origin.y + Height * 0.5f;
    KitPillSurface(Frame.Ink, ImVec2(TrackL, Cy - 7.5f), ImVec2(TrackR, Cy + 7.5f),
                   Target ? KitInkWithAlpha(Accent, 0.30f) : Frame.Theme.Surface2,
                   Target ? KitInkWithAlpha(Accent, 0.70f) : Frame.Theme.Line, 999.0f);
    Frame.Ink->AddCircleFilled(ImVec2(Target ? TrackR - 7.5f : TrackL + 7.5f, Cy), 5.2f,
                                 Target ? Accent : Frame.Theme.Faint);

    ImGui::SetCursorScreenPos(ImVec2(Frame.Left, Origin.y + Height + 5.0f));
    ImGui::Dummy(ImVec2(Frame.Width, 0.0f));
    return Clicked;
}

bool DrawObservatoryOptionStrip(const ObservatoryFrame& Frame, const char* IdPrefix, const char* Label,
                                const std::vector<const char*>& Options, int32_t& SelectedIndex, ImU32 Accent)
{
    if (Options.empty())
    {
        return false;
    }
    const ImVec2 LabelPos = ImGui::GetCursorScreenPos();
    KitDrawText(Frame.Ink, Frame.Faces.Regular, 11.5f, ImVec2(Frame.Left, LabelPos.y), Frame.Theme.Muted, Label);
    ImGui::SetCursorScreenPos(ImVec2(Frame.Left, LabelPos.y + 17.0f));

    bool  Changed = false;
    float CursorX = Frame.Left;
    float CursorY = LabelPos.y + 17.0f;
    for (size_t Index = 0; Index < Options.size(); ++Index)
    {
        const float PillW = KitTextWidth(Frame.Faces.Medium, 11.0f, Options[Index]) + 18.0f;
        if (CursorX + PillW > Frame.Left + Frame.Width && CursorX > Frame.Left)
        {
            CursorX = Frame.Left;
            CursorY += 27.0f;
        }
        const bool Active  = (SelectedIndex == static_cast<int32_t>(Index));
        bool       Hovered = false;
        char       Id[80]  = {};
        std::snprintf(Id, sizeof(Id), "##%sOpt%zu", IdPrefix, Index);
        if (KitTarget(Id, ImVec2(CursorX, CursorY), ImVec2(PillW, 23.0f), Hovered))
        {
            SelectedIndex = static_cast<int32_t>(Index);
            Changed       = true;
        }
        KitPillSurface(Frame.Ink, ImVec2(CursorX, CursorY), ImVec2(CursorX + PillW, CursorY + 23.0f),
                       Active ? KitInkWithAlpha(Accent, 0.22f)
                              : (Hovered ? Frame.Theme.Surface3 : Frame.Theme.Surface2),
                       Active ? KitInkWithAlpha(Accent, 0.65f) : Frame.Theme.LineSoft, 999.0f);
        KitDrawText(Frame.Ink, Frame.Faces.Medium, 11.0f, ImVec2(CursorX + 9.0f, CursorY + 5.5f),
                    Active ? Frame.Theme.Text : Frame.Theme.Muted, Options[Index]);
        CursorX += PillW + 5.0f;
    }
    ImGui::SetCursorScreenPos(ImVec2(Frame.Left, CursorY + 29.0f));
    ImGui::Dummy(ImVec2(Frame.Width, 0.0f));
    return Changed;
}

void DrawObservatoryNote(const ObservatoryFrame& Frame, const char* Text)
{
    ImGui::SetCursorScreenPos(ImVec2(Frame.Left, ImGui::GetCursorScreenPos().y));
    ImGui::PushFont(Frame.Faces.Regular, 11.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Frame.Theme.Faint));
    ImGui::PushTextWrapPos(Frame.Left + Frame.Width);
    ImGui::TextUnformatted(Text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::PopFont();
    AdvanceLayout(Frame.Left, 6.0f, Frame.Width);
}

void DrawObservatoryFooter(const ObservatoryFrame& Frame, const char* LeftLabel)
{
    AdvanceLayout(Frame.Left, 6.0f, Frame.Width);
    const ImVec2 Origin = ImGui::GetCursorScreenPos();
    Frame.Ink->AddRectFilled(ImVec2(Frame.Left, Origin.y), ImVec2(Frame.Left + Frame.Width, Origin.y + 1.0f),
                               Frame.Theme.LineSoft);
    KitDrawTracked(Frame.Ink, Frame.Faces.Medium, 9.5f, ImVec2(Frame.Left, Origin.y + 7.0f), Frame.Theme.Faint,
                   LeftLabel, 0.9f);
    const char* RightLabel = "Settings apply live";
    const float RightW     = KitTextWidth(Frame.Faces.Regular, 10.5f, RightLabel);
    KitDrawText(Frame.Ink, Frame.Faces.Regular, 10.5f,
                ImVec2(Frame.Left + Frame.Width - RightW, Origin.y + 6.0f), Frame.Theme.Muted, RightLabel);
    ImGui::SetCursorScreenPos(ImVec2(Frame.Left, Origin.y + 24.0f));
    ImGui::Dummy(ImVec2(Frame.Width, 0.0f));
}

//========================================================================================================================
//                                            1. FOLDER OBSERVATORY
//========================================================================================================================

void ConstructFolderObservatory(const ObservatoryFrame& Frame, FolderRecord& Folder, SceneDirectory& Directory,
                                SceneDirectoryPanel& DirectoryPanel)
{
    const ImU32 Accent = Folder.AccentRgba != 0 ? static_cast<ImU32>(Folder.AccentRgba) : Frame.Theme.Mint;
    const bool  WasOn  = Folder.Enabled;
    DrawObservatoryHeader(Frame, "FOLDER / HIERARCHY ENCLOSURE", Folder.Title.c_str(),
                          "Group scene records into collapsible folders with one-click cascade visibility.",
                          "##FolderPower", &Folder.Enabled, Accent, "Visible", "Hidden");
    if (Folder.Enabled != WasOn)
    {
        (void)Directory.ConfigureCascadeVisible(Folder.Token(), Folder.Enabled);
        DirectoryPanel.Synchronize(Directory);
    }

    // Tabbed Folder Hero Card (inspired by SourceImages/Folder UI).
    {
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        const float  CardH  = 86.0f;
        KitPillSurface(Frame.Ink, ImVec2(Frame.Left, Origin.y), ImVec2(Frame.Left + Frame.Width, Origin.y + CardH),
                       Frame.Theme.Surface, KitInkWithAlpha(Accent, 0.45f), 12.0f);
        DrawFolderTabGlyph(Frame.Ink, ImVec2(Frame.Left + 34.0f, Origin.y + 44.0f), 1.85f, Accent,
                           !DirectoryPanel.QueryCollapsed(Folder.Token()));
        KitDrawTracked(Frame.Ink, Frame.Faces.Medium, 9.8f, ImVec2(Frame.Left + 68.0f, Origin.y + 18.0f), Accent,
                       Folder.Tag.c_str(), 1.1f);
        KitDrawText(Frame.Ink, Frame.Faces.SemiBold, 17.5f, ImVec2(Frame.Left + 68.0f, Origin.y + 34.0f),
                    Frame.Theme.Text, Folder.Title.c_str());

        uint32_t DirectVisible = 0;
        uint32_t DirectHidden  = 0;
        std::vector<RecordToken> Order;
        Directory.CopyRegistrationOrder(Order);
        for (const RecordToken Candidate : Order)
        {
            if (Directory.QueryParent(Candidate) == Folder.Token())
            {
                if (Directory.QueryVisible(Candidate))
                    ++DirectVisible;
                else
                    ++DirectHidden;
            }
        }
        char SubLine[96] = {};
        std::snprintf(SubLine, sizeof(SubLine), "%u enclosed  \xC2\xB7  %u visible  \xC2\xB7  %u hidden",
                      DirectVisible + DirectHidden, DirectVisible, DirectHidden);
        KitDrawText(Frame.Ink, Frame.Faces.Regular, 11.5f, ImVec2(Frame.Left + 68.0f, Origin.y + 58.0f),
                    Frame.Theme.Muted, SubLine);
        ImGui::SetCursorScreenPos(ImVec2(Frame.Left, Origin.y + CardH + 10.0f));
        ImGui::Dummy(ImVec2(Frame.Width, 0.0f));
    }

    // Editable Folder Name & Category Tag.
    DrawSectionCardHeader(Frame, "Folder identity", "01 / LABEL & SWATCH", Accent);
    {
        static RecordToken           EditedFolder{};
        static std::array<char, 96>  NameBuffer{};
        static std::array<char, 48>  TagBuffer{};
        if (EditedFolder != Folder.Token())
        {
            EditedFolder = Folder.Token();
            std::snprintf(NameBuffer.data(), NameBuffer.size(), "%s", Folder.Title.c_str());
            std::snprintf(TagBuffer.data(), TagBuffer.size(), "%s", Folder.Tag.c_str());
        }
        ImGui::PushFont(Frame.Faces.Regular, 12.5f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(Frame.Theme.Surface2));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Frame.Theme.Text));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 6.0f));

        ImGui::SetCursorScreenPos(ImVec2(Frame.Left, ImGui::GetCursorScreenPos().y));
        ImGui::SetNextItemWidth(Frame.Width);
        if (ImGui::InputTextWithHint("##FolderNameEdit", "Folder title\xE2\x80\xA6", NameBuffer.data(), NameBuffer.size()))
        {
            Folder.Title = NameBuffer.data();
            (void)Directory.ConfigureName(Folder.Token(), Folder.Title);
            DirectoryPanel.Synchronize(Directory);
        }
        AdvanceLayout(Frame.Left, 5.0f, Frame.Width);
        ImGui::SetCursorScreenPos(ImVec2(Frame.Left, ImGui::GetCursorScreenPos().y));
        ImGui::SetNextItemWidth(Frame.Width);
        if (ImGui::InputTextWithHint("##FolderTagEdit", "Group tag (e.g. ENVIRONMENT)\xE2\x80\xA6", TagBuffer.data(),
                                     TagBuffer.size()))
        {
            Folder.Tag = TagBuffer.data();
            DirectoryPanel.Synchronize(Directory);
        }
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
        ImGui::PopFont();
        AdvanceLayout(Frame.Left, 8.0f, Frame.Width);
    }

    // 8-Swatch Accent Palette.
    {
        static constexpr std::array<ImU32, 8> Swatches{
            KitInkHex(0x7ad9a3), KitInkHex(0xe8c78b), KitInkHex(0x8fd14f), KitInkHex(0x63a8ff),
            KitInkHex(0xc3afd2), KitInkHex(0xffb454), KitInkHex(0xff7ad9), KitInkHex(0x9fd8e8),
        };
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        const float  StepX  = Frame.Width / static_cast<float>(Swatches.size());
        for (size_t Index = 0; Index < Swatches.size(); ++Index)
        {
            const ImVec2 Centre(Frame.Left + StepX * (static_cast<float>(Index) + 0.5f), Origin.y + 13.0f);
            bool         Hovered = false;
            char         Id[48]  = {};
            std::snprintf(Id, sizeof(Id), "##FolderSwatch%zu", Index);
            if (KitTarget(Id, ImVec2(Centre.x - 12.0f, Centre.y - 12.0f), ImVec2(24.0f, 24.0f), Hovered))
            {
                Folder.AccentRgba = Swatches[Index];
                (void)Directory.ConfigureAccentColour(Folder.Token(), Folder.AccentRgba);
                DirectoryPanel.Synchronize(Directory);
            }
            Frame.Ink->AddCircleFilled(Centre, Hovered ? 9.5f : 8.0f, Swatches[Index]);
            if (Folder.AccentRgba == Swatches[Index])
            {
                Frame.Ink->AddCircle(Centre, 11.5f, Frame.Theme.Text, 24, 1.6f);
            }
        }
        ImGui::SetCursorScreenPos(ImVec2(Frame.Left, Origin.y + 32.0f));
        ImGui::Dummy(ImVec2(Frame.Width, 0.0f));
    }

    // Cascade & Subfolder Actions.
    DrawSectionCardHeader(Frame, "Hierarchy controls", "02 / ENCLOSURE", Accent);
    if (DrawObservatoryActionButton(Frame, "##FolderShowAll", "Show all enclosed records", Accent))
    {
        Folder.Enabled = true;
        (void)Directory.ConfigureCascadeVisible(Folder.Token(), true);
        DirectoryPanel.Synchronize(Directory);
    }
    if (DrawObservatoryActionButton(Frame, "##FolderHideAll", "Hide all enclosed records", Frame.Theme.Muted))
    {
        Folder.Enabled = false;
        (void)Directory.ConfigureCascadeVisible(Folder.Token(), false);
        DirectoryPanel.Synchronize(Directory);
    }
    if (DrawObservatoryActionButton(Frame, "##FolderSpawnSub", "+ Create nested subfolder", Accent))
    {
        DirectoryPanel.RequestSpawn(RecordClassification::Folder);
    }

    // Enclosed items list + quick reparent from root.
    DrawSectionCardHeader(Frame, "Enclosed records", "03 / CONTENTS", Accent);
    std::vector<RecordToken> Order;
    Directory.CopyRegistrationOrder(Order);
    uint32_t ShownEnclosed = 0;
    for (const RecordToken Candidate : Order)
    {
        if (Directory.QueryParent(Candidate) != Folder.Token())
        {
            continue;
        }
        ++ShownEnclosed;
        const std::string RecordName = Directory.QueryName(Candidate);
        const ImVec2      RowPos     = ImGui::GetCursorScreenPos();
        KitPillSurface(Frame.Ink, ImVec2(Frame.Left, RowPos.y), ImVec2(Frame.Left + Frame.Width, RowPos.y + 26.0f),
                       Frame.Theme.Surface, Frame.Theme.LineSoft, 6.5f);
        bool JumpHover = false;
        char JumpId[64] = {};
        std::snprintf(JumpId, sizeof(JumpId), "##FolderJump%llu", static_cast<unsigned long long>(Candidate.Value));
        if (KitTarget(JumpId, ImVec2(Frame.Left, RowPos.y), ImVec2(Frame.Width - 64.0f, 26.0f), JumpHover,
                      "Inspect record"))
        {
            DirectoryPanel.Select(Candidate);
            DirectoryPanel.Synchronize(Directory);
        }
        KitDrawText(Frame.Ink, Frame.Faces.Regular, 11.8f, ImVec2(Frame.Left + 10.0f, RowPos.y + 6.5f),
                    JumpHover ? Accent : Frame.Theme.Text, RecordName.c_str());

        bool UnpackHover = false;
        char UnpackId[64] = {};
        std::snprintf(UnpackId, sizeof(UnpackId), "##FolderUnpack%llu", static_cast<unsigned long long>(Candidate.Value));
        if (KitTarget(UnpackId, ImVec2(Frame.Left + Frame.Width - 58.0f, RowPos.y + 3.0f), ImVec2(54.0f, 20.0f),
                      UnpackHover, "Move to root"))
        {
            (void)Directory.ConfigureParent(Candidate, RecordToken{});
            DirectoryPanel.Synchronize(Directory);
        }
        KitDrawText(Frame.Ink, Frame.Faces.Medium, 10.5f,
                    ImVec2(Frame.Left + Frame.Width - 52.0f, RowPos.y + 7.0f),
                    UnpackHover ? Frame.Theme.Text : Frame.Theme.Faint, "Unpack");
        ImGui::SetCursorScreenPos(ImVec2(Frame.Left, RowPos.y + 30.0f));
        ImGui::Dummy(ImVec2(Frame.Width, 0.0f));
    }
    if (ShownEnclosed == 0)
    {
        DrawObservatoryNote(Frame, "Folder is empty. Click Move here below on any root record to enclose it.");
    }

    // Root records available to enclose.
    uint32_t RootCandidates = 0;
    for (const RecordToken Candidate : Order)
    {
        if (Candidate == Folder.Token() || Directory.QueryParent(Candidate).IsValid() ||
            Directory.QueryClassification(Candidate) == RecordClassification::Folder)
        {
            continue;
        }
        if (RootCandidates == 0)
        {
            DrawSectionCardHeader(Frame, "Available root records", "04 / ENCLOSE", Accent);
        }
        ++RootCandidates;
        const std::string RecordName = Directory.QueryName(Candidate);
        const ImVec2      RowPos     = ImGui::GetCursorScreenPos();
        bool              MoveHover  = false;
        char              MoveId[64] = {};
        std::snprintf(MoveId, sizeof(MoveId), "##FolderClaim%llu", static_cast<unsigned long long>(Candidate.Value));
        if (KitTarget(MoveId, ImVec2(Frame.Left, RowPos.y), ImVec2(Frame.Width, 25.0f), MoveHover))
        {
            (void)Directory.ConfigureParent(Candidate, Folder.Token());
            DirectoryPanel.Synchronize(Directory);
        }
        KitPillSurface(Frame.Ink, ImVec2(Frame.Left, RowPos.y), ImVec2(Frame.Left + Frame.Width, RowPos.y + 25.0f),
                       MoveHover ? Frame.Theme.Surface3 : Frame.Theme.Surface2, Frame.Theme.LineSoft, 6.5f);
        KitDrawText(Frame.Ink, Frame.Faces.Regular, 11.5f, ImVec2(Frame.Left + 10.0f, RowPos.y + 6.0f),
                    Frame.Theme.Muted, RecordName.c_str());
        KitDrawText(Frame.Ink, Frame.Faces.Medium, 10.5f,
                    ImVec2(Frame.Left + Frame.Width - 66.0f, RowPos.y + 6.5f), Accent, "+ Enclose");
        ImGui::SetCursorScreenPos(ImVec2(Frame.Left, RowPos.y + 29.0f));
        ImGui::Dummy(ImVec2(Frame.Width, 0.0f));
    }

    DrawObservatoryFooter(Frame, "OUTLINER FOLDER ENCLOSURE");
}

//========================================================================================================================
//                                              2. SUN OBSERVATORY
//========================================================================================================================

void ConstructSunObservatory(const ObservatoryFrame& Frame, SunRecord& Sun, uint32_t& ActiveTab,
                             SceneDirectory& Directory, SceneDirectoryPanel& DirectoryPanel)
{
    const ImU32 Accent = KitInkHex(0xe8c78b);
    DrawObservatoryHeader(Frame, "ENVIRONMENT  /  02", "Sun", "Directional light / position & colour.",
                          "##SunPower", &Sun.Enabled, Accent, "Sunlight", "Off");
    DrawObservatoryTabs(Frame, "Sun", {"Overview", "Daylight", "Light", "Disc & optics"}, ActiveTab, Accent);

    if (ActiveTab == 0)
    {
        DrawSectionCardHeader(Frame, "Solar vector", "01 / DIRECTION", Accent);
        // Interactive Polar Sky-Dome Instrument (ported from SunInspectorDashboard.js).
        const ImVec2 CanvasMin = ImGui::GetCursorScreenPos();
        const float  CanvasH   = 206.0f;
        const ImVec2 CanvasMax(Frame.Left + Frame.Width, CanvasMin.y + CanvasH);
        KitPillSurface(Frame.Ink, CanvasMin, CanvasMax, Frame.CanvasBg, Frame.Theme.Line, 11.0f);

        ImGui::SetCursorScreenPos(CanvasMin);
        ImGui::InvisibleButton("##SunSkyDome", ImVec2(Frame.Width, CanvasH));
        const ImVec2 DomeCentre(Frame.Left + Frame.Width * 0.5f, CanvasMin.y + CanvasH * 0.50f);
        const float  BoundaryRadius = std::min(Frame.Width * 0.34f, 72.0f);
        const float  OuterRadius    = BoundaryRadius * 1.28f;

        if (ImGui::IsItemActive())
        {
            const float Dx = ImGui::GetIO().MousePos.x - DomeCentre.x;
            const float Dy = DomeCentre.y - ImGui::GetIO().MousePos.y;
            const float Dist = std::hypot(Dx, Dy);
            if (Dist > 1.0f)
            {
                Sun.AzimuthDegrees = std::clamp(static_cast<double>(std::atan2(Dy, Dx) * (180.0f / PiFloat)), -180.0, 180.0);
            }
            Sun.ElevationDegrees = std::clamp(90.0 - static_cast<double>(Dist / BoundaryRadius) * 90.0, -90.0, 90.0);
            Sun.TimeLinked       = false;
            Sun.DayCycleActive   = false;
        }

        for (float Fraction : {0.5f, 1.0f, 1.28f})
        {
            Frame.Ink->AddCircle(DomeCentre, BoundaryRadius * Fraction,
                                   std::abs(Fraction - 1.0f) < 0.01f ? KitInkRgba(232, 199, 139, 112)
                                                                     : KitInkRgba(232, 199, 139, 32),
                                   48, 1.15f);
        }
        for (int Tick = 0; Tick < 24; ++Tick)
        {
            const float Angle = static_cast<float>(Tick) / 24.0f * PiFloat * 2.0f;
            Frame.Ink->AddLine(
                ImVec2(DomeCentre.x + std::cos(Angle) * (OuterRadius - 4.0f),
                       DomeCentre.y + std::sin(Angle) * (OuterRadius - 4.0f)),
                ImVec2(DomeCentre.x + std::cos(Angle) * OuterRadius, DomeCentre.y + std::sin(Angle) * OuterRadius),
                KitInkRgba(232, 199, 139, 80), 1.0f);
        }
        Frame.Ink->AddLine(ImVec2(DomeCentre.x - OuterRadius - 8.0f, DomeCentre.y),
                             ImVec2(DomeCentre.x + OuterRadius + 8.0f, DomeCentre.y), KitInkRgba(232, 199, 139, 34));
        Frame.Ink->AddLine(ImVec2(DomeCentre.x, CanvasMin.y + 12.0f), ImVec2(DomeCentre.x, CanvasMax.y - 18.0f),
                             KitInkRgba(232, 199, 139, 34));

        const float AzimuthRad = static_cast<float>(Sun.AzimuthDegrees) * (PiFloat / 180.0f);
        const float Radial     = std::clamp(static_cast<float>((90.0 - Sun.ElevationDegrees) / 90.0), 0.0f, 1.32f) *
                             BoundaryRadius;
        const ImVec2 SunHandle(DomeCentre.x + std::cos(AzimuthRad) * Radial,
                               DomeCentre.y - std::sin(AzimuthRad) * Radial);
        const bool   SunLit = Sun.Enabled && Sun.ElevationDegrees >= 0.0;
        const ImU32  VectorCol = SunLit ? Accent : KitInkHex(0x87938b);
        Frame.Ink->AddLine(DomeCentre, SunHandle, VectorCol, 1.8f);
        Frame.Ink->AddCircleFilled(DomeCentre, 3.2f, KitInkHex(0xd9cab0));
        if (SunLit)
        {
            Frame.Ink->AddCircleFilled(SunHandle, 14.0f, KitInkRgba(232, 199, 139, 38));
        }
        Frame.Ink->AddCircle(SunHandle, 10.0f, KitInkWithAlpha(VectorCol, 0.55f), 24, 1.3f);
        Frame.Ink->AddCircleFilled(SunHandle, 4.8f, SunLit ? KitInkHex(0xf0d69f) : KitInkHex(0x8d9b91));

        KitDrawText(Frame.Ink, Frame.Faces.Medium, 10.0f, ImVec2(DomeCentre.x - 16.0f, CanvasMin.y + 6.0f),
                    KitInkHex(0xa49886), "+Y / N");
        KitDrawText(Frame.Ink, Frame.Faces.Medium, 10.0f,
                    ImVec2(DomeCentre.x + OuterRadius + 10.0f, DomeCentre.y - 5.0f), KitInkHex(0xa49886), "+X / E");
        KitDrawText(Frame.Ink, Frame.Faces.Medium, 10.0f,
                    ImVec2(DomeCentre.x - OuterRadius - 20.0f, DomeCentre.y - 5.0f), KitInkHex(0xa49886), "W");
        KitDrawText(Frame.Ink, Frame.Faces.Medium, 10.0f, ImVec2(DomeCentre.x - 3.0f, CanvasMax.y - 16.0f),
                    KitInkHex(0xa49886), "S");

        const SpatialVector Dir = Sun.QuerySunDirection();
        char                XyzText[64] = {};
        std::snprintf(XyzText, sizeof(XyzText), "XYZ  %.2f / %.2f / %.2f", Dir.X, Dir.Y, Dir.Z);
        KitDrawText(Frame.Ink, Frame.Faces.Regular, 10.0f, ImVec2(CanvasMin.x + 10.0f, CanvasMax.y - 16.0f),
                    KitInkHex(0xab9d88), XyzText);
        ImGui::SetCursorScreenPos(ImVec2(Frame.Left, CanvasMax.y + 8.0f));
        ImGui::Dummy(ImVec2(Frame.Width, 0.0f));

        char AzBuf[24] = {}, TempBuf[24] = {}, IllumBuf[24] = {};
        std::snprintf(AzBuf, sizeof(AzBuf), "%.1f", Sun.AzimuthDegrees);
        std::snprintf(TempBuf, sizeof(TempBuf), "%d", static_cast<int>(std::lround(Sun.TemperatureKelvin)));
        std::snprintf(IllumBuf, sizeof(IllumBuf), "%.1f", Sun.SolarIlluminance);
        DrawKpiTriple(Frame, "Azimuth", AzBuf, "\xC2\xB0", "Source tint",
                      Sun.ColourSourceIndex == 1 ? "RGB" : TempBuf, Sun.ColourSourceIndex == 1 ? "" : "K",
                      "Sunlight", IllumBuf, "\xC3\x97");

        DrawObservatorySwitchRow(Frame, "##SunDiscQuick", "Finite solar disc", Sun.DiscEnabled, Accent);
        DrawObservatorySwitchRow(Frame, "##SunCycleQuick", "Animate day cycle", Sun.DayCycleActive, Accent);

        DrawSectionCardHeader(Frame, "Direction controls", "02 / AIM", Accent);
        if (DrawObservatorySlider(Frame, "##SunAzimuth", "Azimuth", Sun.AzimuthDegrees, -180.0, 180.0, 0.5,
                                  "\xC2\xB0", 1, Accent))
        {
            Sun.TimeLinked = false;
        }
        if (DrawObservatorySlider(Frame, "##SunElevation", "Elevation", Sun.ElevationDegrees, -90.0, 90.0, 0.5,
                                  "\xC2\xB0", 1, Accent))
        {
            Sun.TimeLinked = false;
        }

        if (DrawObservatoryOptionStrip(Frame, "SunPreset", "Lighting presets", {"Noon", "Golden", "Dusk", "Night"},
                                       Sun.PresetIndex, Accent))
        {
            Sun.ApplyPreset(static_cast<SolarLightingPreset>(std::clamp(Sun.PresetIndex, 0, 3)));
        }
        if (DrawObservatoryActionButton(Frame, "##SunResetBtn", "Reset Sun defaults", Frame.Theme.Muted))
        {
            Sun.ResetToDefaults();
        }
    }
    else if (ActiveTab == 1)
    {
        DrawSectionCardHeader(Frame, "Solar day", "03 / EPHEMERIS", Accent);
        const SolarDayTrajectory Trajectory = Sun.EvaluateDayTrajectory();
        const ImVec2             PlotMin    = ImGui::GetCursorScreenPos();
        const float              PlotH      = 150.0f;
        const ImVec2             PlotMax(Frame.Left + Frame.Width, PlotMin.y + PlotH);
        KitPillSurface(Frame.Ink, PlotMin, PlotMax, Frame.CanvasBg, Frame.Theme.Line, 10.0f);

        const float InnerL = PlotMin.x + 28.0f;
        const float InnerR = PlotMax.x - 12.0f;
        const float MidY   = PlotMin.y + PlotH * 0.52f;
        auto        MapElevY = [&](double Degrees) -> float {
            return MidY - static_cast<float>(Degrees / 90.0) * (PlotH * 0.38f);
        };
        for (double Elev : {-60.0, 0.0, 60.0})
        {
            const float Y = MapElevY(Elev);
            Frame.Ink->AddLine(ImVec2(InnerL, Y), ImVec2(InnerR, Y),
                                 Elev == 0.0 ? KitInkRgba(227, 202, 153, 96) : KitInkRgba(227, 202, 153, 28));
        }

        for (size_t Index = 1; Index < Trajectory.ElevationDegrees.size(); ++Index)
        {
            const float X0 = InnerL + static_cast<float>(Index - 1) / 96.0f * (InnerR - InnerL);
            const float X1 = InnerL + static_cast<float>(Index) / 96.0f * (InnerR - InnerL);
            const float Y0 = MapElevY(Trajectory.ElevationDegrees[Index - 1]);
            const float Y1 = MapElevY(Trajectory.ElevationDegrees[Index]);
            const bool  Day = Trajectory.ElevationDegrees[Index - 1] >= 0.0 || Trajectory.ElevationDegrees[Index] >= 0.0;
            Frame.Ink->AddLine(ImVec2(X0, Y0), ImVec2(X1, Y1),
                                 Day ? KitInkHex(0xefd397) : KitInkRgba(159, 153, 122, 140), Day ? 2.2f : 1.3f);
        }
        const float CursorX = InnerL + static_cast<float>(Sun.SolarTimeHours / 24.0) * (InnerR - InnerL);
        Frame.Ink->AddLine(ImVec2(CursorX, PlotMin.y + 10.0f), ImVec2(CursorX, PlotMax.y - 18.0f),
                             KitInkRgba(232, 199, 139, 130), 1.2f);
        for (int Hour : {0, 6, 12, 18, 24})
        {
            char HLabel[12] = {};
            std::snprintf(HLabel, sizeof(HLabel), "%02d:00", Hour);
            const float HX = InnerL + static_cast<float>(Hour) / 24.0f * (InnerR - InnerL) - 12.0f;
            KitDrawText(Frame.Ink, Frame.Faces.Regular, 9.5f, ImVec2(HX, PlotMax.y - 14.0f), KitInkHex(0xa39982),
                        HLabel);
        }
        ImGui::SetCursorScreenPos(ImVec2(Frame.Left, PlotMax.y + 8.0f));
        ImGui::Dummy(ImVec2(Frame.Width, 0.0f));

        const std::string RiseStr = Trajectory.RiseHour < 0.0 ? "--:--" : FormatSolarClock(Trajectory.RiseHour);
        const std::string SetStr  = Trajectory.SetHour < 0.0 ? "--:--" : FormatSolarClock(Trajectory.SetHour);
        char              DayHrs[24] = {};
        std::snprintf(DayHrs, sizeof(DayHrs), "%.1f", Trajectory.DaylightHours);
        DrawKpiTriple(Frame, "Solar rise", RiseStr.c_str(), "", "Solar set", SetStr.c_str(), "", "Above 0\xC2\xB0",
                      DayHrs, "h");

        if (DrawObservatorySlider(Frame, "##SunLocalTime", "Local solar control time", Sun.SolarTimeHours, 0.0, 24.0,
                                  0.05, "h", 2, Accent))
        {
            Sun.TimeLinked = true;
            Sun.SynchronizeFromEphemeris();
        }
        DrawObservatorySlider(Frame, "##SunDayDur", "Full day duration", Sun.DayDurationHours, 0.1, 24.0, 0.1,
                              "real h", 1, Accent);

        DrawSectionCardHeader(Frame, "Time & location", "04 / COORDINATES", Accent);
        if (DrawObservatorySlider(Frame, "##SunLat", "Latitude", Sun.LatitudeDegrees, -90.0, 90.0, 0.1, "\xC2\xB0", 2,
                                  Accent) |
            DrawObservatorySlider(Frame, "##SunLon", "Longitude", Sun.LongitudeDegrees, -180.0, 180.0, 0.1, "\xC2\xB0",
                                  2, Accent) |
            DrawObservatorySlider(Frame, "##SunTz", "UTC offset", Sun.UtcOffsetHours, -12.0, 14.0, 0.5, "h", 1, Accent))
        {
            Sun.TimeLinked = true;
            Sun.SynchronizeFromEphemeris();
        }
    }
    else if (ActiveTab == 2)
    {
        DrawSectionCardHeader(Frame, "Light source", "05 / RADIANCE", Accent);
        DrawObservatorySlider(Frame, "##SunIllum", "Sunlight multiplier", Sun.SolarIlluminance, 0.0, 60.0, 0.1,
                              "\xC3\x97", 1, Accent);
        DrawObservatoryOptionStrip(Frame, "SunColSrc", "Colour source", {"Temperature", "RGB"}, Sun.ColourSourceIndex,
                                   Accent);
        if (DrawObservatorySlider(Frame, "##SunKelvin", "Colour temperature", Sun.TemperatureKelvin, 2000.0, 10000.0,
                                  50.0, "K", 0, Accent))
        {
            Sun.ColourSourceIndex = 0;
        }

        DrawSectionCardHeader(Frame, "Source spectrum", "06 / REFERENCE", Accent);
        const ImVec2 SpecMin = ImGui::GetCursorScreenPos();
        const float  SpecH   = 136.0f;
        const ImVec2 SpecMax(Frame.Left + Frame.Width, SpecMin.y + SpecH);
        KitPillSurface(Frame.Ink, SpecMin, SpecMax, Frame.CanvasBg, Frame.Theme.Line, 10.0f);

        const float LeftX  = SpecMin.x + 14.0f;
        const float RightX = SpecMax.x - 14.0f;
        const float FloorY = SpecMax.y - 28.0f;
        for (int Sample = 1; Sample <= 64; ++Sample)
        {
            const double Nm0 = 380.0 + static_cast<double>(Sample - 1) / 64.0 * 400.0;
            const double Nm1 = 380.0 + static_cast<double>(Sample) / 64.0 * 400.0;
            const float  X0  = LeftX + static_cast<float>(Sample - 1) / 64.0f * (RightX - LeftX);
            const float  X1  = LeftX + static_cast<float>(Sample) / 64.0f * (RightX - LeftX);
            const float  Y0  = FloorY - static_cast<float>(SunRecord::EvaluateNormalizedPlanck(Nm0, Sun.TemperatureKelvin)) * 90.0f;
            const float  Y1  = FloorY - static_cast<float>(SunRecord::EvaluateNormalizedPlanck(Nm1, Sun.TemperatureKelvin)) * 90.0f;
            Frame.Ink->AddLine(ImVec2(X0, Y0), ImVec2(X1, Y1), KitInkHex(0xf0ddba), 2.0f);

            // Spectral rainbow bar along bottom.
            const float T = static_cast<float>(Sample - 1) / 64.0f;
            const ImU32 BarCol = KitInkRgba(
                static_cast<int>(130 + 115 * std::clamp(T * 1.6f - 0.3f, 0.0f, 1.0f)),
                static_cast<int>(125 + 85 * std::sin(T * PiFloat)),
                static_cast<int>(215 * (1.0f - T * 0.7f)), 220);
            Frame.Ink->AddRectFilled(ImVec2(X0, FloorY + 6.0f), ImVec2(X1 + 1.0f, FloorY + 11.0f), BarCol);
        }
        const double WienPeakNm = 2897771.955 / Sun.TemperatureKelvin;
        if (WienPeakNm >= 380.0 && WienPeakNm <= 780.0)
        {
            const float PeakX = LeftX + static_cast<float>((WienPeakNm - 380.0) / 400.0) * (RightX - LeftX);
            Frame.Ink->AddLine(ImVec2(PeakX, SpecMin.y + 12.0f), ImVec2(PeakX, FloorY), KitInkRgba(238, 214, 166, 120));
            Frame.Ink->AddCircleFilled(ImVec2(PeakX, SpecMin.y + 18.0f), 4.0f, Accent);
        }
        KitDrawText(Frame.Ink, Frame.Faces.Regular, 9.5f, ImVec2(LeftX, SpecMax.y - 13.0f), KitInkHex(0xac9f86),
                    "380 nm");
        KitDrawText(Frame.Ink, Frame.Faces.Regular, 9.5f, ImVec2(RightX - 38.0f, SpecMax.y - 13.0f),
                    KitInkHex(0xac9f86), "780 nm");
        ImGui::SetCursorScreenPos(ImVec2(Frame.Left, SpecMax.y + 8.0f));
        ImGui::Dummy(ImVec2(Frame.Width, 0.0f));
    }
    else
    {
        DrawSectionCardHeader(Frame, "Apparent solar disc", "07 / ANGULAR SIZE", Accent);
        const ImVec2 DiscMin = ImGui::GetCursorScreenPos();
        const float  DiscH   = 136.0f;
        const ImVec2 DiscMax(Frame.Left + Frame.Width, DiscMin.y + DiscH);
        KitPillSurface(Frame.Ink, DiscMin, DiscMax, Frame.CanvasBg, Frame.Theme.Line, 10.0f);
        const ImVec2 Centre(Frame.Left + Frame.Width * 0.5f, DiscMin.y + DiscH * 0.5f);
        for (float R : {24.0f, 44.0f, 60.0f})
        {
            Frame.Ink->AddCircle(Centre, R, KitInkRgba(232, 199, 139, 36));
        }
        const float ApparentR = 8.0f + static_cast<float>(Sun.AngularRadiusDegrees / 2.0) * 42.0f;
        Frame.Ink->AddCircleFilled(Centre, ApparentR + 18.0f * static_cast<float>(Sun.OpticalGlow * 0.5),
                                     KitInkRgba(232, 199, 139, 34));
        Frame.Ink->AddCircleFilled(Centre, ApparentR,
                                     Sun.DiscEnabled ? KitInkHex(0xf1d7a6) : KitInkHex(0x776e5b));
        ImGui::SetCursorScreenPos(ImVec2(Frame.Left, DiscMax.y + 8.0f));
        ImGui::Dummy(ImVec2(Frame.Width, 0.0f));

        double DiameterDegrees = Sun.AngularRadiusDegrees * 2.0;
        if (DrawObservatorySlider(Frame, "##SunDiam", "Angular diameter", DiameterDegrees, 0.10, 4.00, 0.02, "\xC2\xB0",
                                  2, Accent))
        {
            Sun.AngularRadiusDegrees = DiameterDegrees * 0.5;
        }
        DrawObservatorySlider(Frame, "##SunGlow", "Optical aureole", Sun.OpticalGlow, 0.0, 3.0, 0.02, "\xC3\x97", 2,
                              Accent);

        DrawSectionCardHeader(Frame, "Connected optics", "08 / SUBCOMPONENT", Accent);
        DrawObservatoryNote(Frame, "Lens flare is a separate camera-optics component driven by solar visibility.");
        if (DrawObservatoryActionButton(Frame, "##OpenLensFlareLink", "Open Lens Flare  ->", Accent))
        {
            const RecordToken FlareToken = Directory.FindTokenByIdentifier("flare");
            if (FlareToken.IsValid())
            {
                DirectoryPanel.Select(FlareToken);
                DirectoryPanel.Synchronize(Directory);
            }
        }
    }

    DrawObservatoryFooter(Frame, "DIRECTIONAL LIGHT / Z UP");
}

//========================================================================================================================
//                                          3. LENS FLARE OBSERVATORY
//========================================================================================================================

void ConstructLensFlareObservatory(const ObservatoryFrame& Frame, LensFlareRecord& Flare, uint32_t& ActiveTab)
{
    const ImU32 Accent = KitInkHex(0xc3afd2);
    DrawObservatoryHeader(Frame, "ENVIRONMENT / CAMERA OPTICS", "Lens flare",
                          "Reflections, diffraction, and light across the sensor.", "##FlarePower", &Flare.Enabled,
                          Accent, "Enabled", "Disabled");

    // 01 / LIVE PROCEDURAL REFERENCE — Optical composite (ported from LensFlare optical controls).
    DrawSectionCardHeader(Frame, "Optical composite", "01 / LIVE PROCEDURAL", Accent);
    {
        const ImVec2 CompMin = ImGui::GetCursorScreenPos();
        const float  CompH   = 134.0f;
        const ImVec2 CompMax(Frame.Left + Frame.Width, CompMin.y + CompH);
        KitPillSurface(Frame.Ink, CompMin, CompMax, KitInkHex(0x07080a), Frame.Theme.Line, 10.0f);

        ImGui::SetCursorScreenPos(CompMin);
        ImGui::InvisibleButton("##FlareCompositeDrag", ImVec2(Frame.Width, CompH));
        if (ImGui::IsItemActive())
        {
            Flare.PreviewSourceU = std::clamp(
                static_cast<double>((ImGui::GetIO().MousePos.x - CompMin.x) / Frame.Width), 0.06, 0.94);
            Flare.PreviewSourceV = std::clamp(
                static_cast<double>((ImGui::GetIO().MousePos.y - CompMin.y) / CompH), 0.08, 0.92);
        }

        constexpr int   Cols        = 96;
        constexpr int   Rows        = 40;
        const float     CellW       = (Frame.Width - 6.0f) / static_cast<float>(Cols);
        const float     CellH       = (CompH - 6.0f) / static_cast<float>(Rows);
        const double    AspectRatio = static_cast<double>(Frame.Width / CompH);
        std::vector<ImU32> FlareGrid(static_cast<size_t>((Cols + 1) * (Rows + 1)));
        for (int Row = 0; Row <= Rows; ++Row)
        {
            const double V = static_cast<double>(Row) / static_cast<double>(Rows);
            for (int Col = 0; Col <= Cols; ++Col)
            {
                const double      U      = static_cast<double>(Col) / static_cast<double>(Cols);
                const SpectrumRgb Sample = Flare.EvaluateOpticalSample(U, V, AspectRatio);
                FlareGrid[static_cast<size_t>(Row * (Cols + 1) + Col)] = ToneMappedLinearRgb(Sample, 1.35f);
            }
        }
        for (int Row = 0; Row < Rows; ++Row)
        {
            for (int Col = 0; Col < Cols; ++Col)
            {
                const ImVec2 PMin(CompMin.x + 3.0f + static_cast<float>(Col) * CellW,
                                  CompMin.y + 3.0f + static_cast<float>(Row) * CellH);
                const ImVec2 PMax(PMin.x + CellW, PMin.y + CellH);
                const ImU32  C00 = FlareGrid[static_cast<size_t>(Row * (Cols + 1) + Col)];
                const ImU32  C10 = FlareGrid[static_cast<size_t>(Row * (Cols + 1) + Col + 1)];
                const ImU32  C11 = FlareGrid[static_cast<size_t>((Row + 1) * (Cols + 1) + Col + 1)];
                const ImU32  C01 = FlareGrid[static_cast<size_t>((Row + 1) * (Cols + 1) + Col)];
                Frame.Ink->AddRectFilledMultiColor(PMin, PMax, C00, C10, C11, C01);
            }
        }
        const ImVec2 SourcePos(CompMin.x + static_cast<float>(Flare.PreviewSourceU) * Frame.Width,
                               CompMin.y + static_cast<float>(Flare.PreviewSourceV) * CompH);
        Frame.Ink->AddCircle(SourcePos, 6.5f, KitInkRgba(243, 222, 178, 180), 20, 1.2f);

        char SourcePose[64] = {};
        std::snprintf(SourcePose, sizeof(SourcePose), "SOURCE %.2f / %.2f  (drag light)", Flare.PreviewSourceU,
                      Flare.PreviewSourceV);
        KitDrawText(Frame.Ink, Frame.Faces.Medium, 10.0f, ImVec2(CompMin.x + 10.0f, CompMax.y - 16.0f),
                    KitInkRgba(235, 232, 244, 210), SourcePose);

        ImGui::SetCursorScreenPos(ImVec2(Frame.Left, CompMax.y + 8.0f));
        ImGui::Dummy(ImVec2(Frame.Width, 0.0f));
    }

    DrawObservatoryTabs(Frame, "Flare", {"Composite", "Ghosts", "Halo", "Rays"}, ActiveTab, Accent);

    if (ActiveTab == 0)
    {
        DrawSectionCardHeader(Frame, "Light stack", "02 / SOURCE & PRESET", Accent);
        if (DrawObservatoryOptionStrip(
                Frame, "FlarePreset", "Optical preset",
                {"Cinematic", "Clean", "Anamorphic", "Subtle", "Halo", "Custom layers"}, Flare.PresetIndex, Accent))
        {
            Flare.ApplyPreset(static_cast<LensFlarePresetCategory>(std::clamp(Flare.PresetIndex, 0, 5)));
        }
        DrawObservatorySlider(Frame, "##FlareInt", "Intensity", Flare.Intensity, 0.0, 5.0, 0.02, "\xC3\x97", 2, Accent);
        DrawObservatorySlider(Frame, "##FlareSpread", "Spread", Flare.Spread, 0.25, 2.0, 0.02, "\xC3\x97", 2, Accent);
        DrawObservatorySlider(Frame, "##FlareChrom", "Chromatic separation", Flare.ChromaticSeparation, 0.0, 1.0, 0.01,
                              "", 2, Accent);
        if (DrawObservatoryActionButton(Frame, "##FlareResetSource", "Reset preview source UV", Frame.Theme.Muted))
        {
            Flare.PreviewSourceU = 0.28;
            Flare.PreviewSourceV = 0.43;
        }
    }
    else if (ActiveTab == 1)
    {
        DrawSectionCardHeader(Frame, "Internal reflections", "03 / SENSOR AXIS", Accent);
        const ImVec2 AxisMin = ImGui::GetCursorScreenPos();
        const float  AxisH   = 130.0f;
        const ImVec2 AxisMax(Frame.Left + Frame.Width, AxisMin.y + AxisH);
        KitPillSurface(Frame.Ink, AxisMin, AxisMax, Frame.CanvasBg, Frame.Theme.Line, 10.0f);

        const ImVec2 SensorCentre(Frame.Left + Frame.Width * 0.5f, AxisMin.y + AxisH * 0.5f);
        Frame.Ink->AddLine(ImVec2(AxisMin.x + 12.0f, SensorCentre.y), ImVec2(AxisMax.x - 12.0f, SensorCentre.y),
                             Frame.CanvasGrid);
        Frame.Ink->AddLine(ImVec2(SensorCentre.x, AxisMin.y + 10.0f), ImVec2(SensorCentre.x, AxisMax.y - 18.0f),
                             Frame.CanvasGrid);
        const ImVec2 SrcPt(AxisMin.x + 16.0f + static_cast<float>(Flare.PreviewSourceU) * (Frame.Width - 32.0f),
                           AxisMin.y + 12.0f + static_cast<float>(Flare.PreviewSourceV) * (AxisH - 30.0f));
        Frame.Ink->AddLine(SrcPt,
                             ImVec2(SensorCentre.x - (SrcPt.x - SensorCentre.x) * 2.2f,
                                    SensorCentre.y - (SrcPt.y - SensorCentre.y) * 2.2f),
                             KitInkRgba(195, 175, 210, 88), 1.1f);

        const int32_t Count = static_cast<int32_t>(std::lround(Flare.GhostCount));
        const int32_t Sides = Flare.QueryGhostSides();
        for (int32_t Index = 0; Index < Count; ++Index)
        {
            const double K  = 1.0 - (0.6 + static_cast<double>(Index + 1) / std::max(1, Count) * (1.7 + Flare.GhostSpacing));
            const float  GX = AxisMin.x + 16.0f +
                             static_cast<float>(0.5 + (Flare.PreviewSourceU - 0.5) * K) * (Frame.Width - 32.0f);
            const float  GY = AxisMin.y + 12.0f +
                             static_cast<float>(0.5 + (Flare.PreviewSourceV - 0.5) * K) * (AxisH - 30.0f);
            if (GX < AxisMin.x + 14.0f || GX > AxisMax.x - 14.0f || GY < AxisMin.y + 12.0f || GY > AxisMax.y - 20.0f)
            {
                continue;
            }
            const float R = 6.0f + static_cast<float>(Index % 4) * 2.8f;
            Frame.Ink->AddCircle(ImVec2(GX, GY), R, KitInkRgba(195, 175, 210, 180), Sides >= 3 ? Sides : 24, 1.3f);
        }
        Frame.Ink->AddCircleFilled(SrcPt, 4.2f, KitInkHex(0xf3deb2));
        Frame.Ink->AddCircleFilled(SensorCentre, 3.2f, KitInkHex(0xc1c9d5));
        KitDrawText(Frame.Ink, Frame.Faces.Regular, 9.5f, ImVec2(AxisMin.x + 12.0f, AxisMax.y - 14.0f),
                    KitInkHex(0x9b9dab), "SOURCE -> SENSOR CENTRE -> REFLECTIONS");
        ImGui::SetCursorScreenPos(ImVec2(Frame.Left, AxisMax.y + 8.0f));
        ImGui::Dummy(ImVec2(Frame.Width, 0.0f));

        if (DrawObservatoryOptionStrip(Frame, "GhostShape", "Aperture ghost shape", {"Round", "Hexagon", "Octagon"},
                                       Flare.GhostShapeIndex, Accent) |
            DrawObservatorySlider(Frame, "##GhostCnt", "Ghost count", Flare.GhostCount, 0.0, 24.0, 1.0, "", 0, Accent) |
            DrawObservatorySlider(Frame, "##GhostBri", "Ghost brightness", Flare.GhostBrightness, 0.0, 2.0, 0.02,
                                  "\xC3\x97", 2, Accent) |
            DrawObservatorySlider(Frame, "##GhostSpc", "Ghost spacing", Flare.GhostSpacing, 0.0, 3.0, 0.02, "\xC3\x97",
                                  2, Accent))
        {
            Flare.PresetIndex = static_cast<int32_t>(LensFlarePresetCategory::CustomLayers);
        }
    }
    else if (ActiveTab == 2)
    {
        DrawSectionCardHeader(Frame, "Annular response", "04 / RADIAL SECTION", Accent);
        const ImVec2 HaloMin = ImGui::GetCursorScreenPos();
        const float  HaloH   = 122.0f;
        const ImVec2 HaloMax(Frame.Left + Frame.Width, HaloMin.y + HaloH);
        KitPillSurface(Frame.Ink, HaloMin, HaloMax, Frame.CanvasBg, Frame.Theme.Line, 10.0f);
        const ImVec2 RingCentre(HaloMin.x + Frame.Width * 0.34f, HaloMin.y + HaloH * 0.5f);
        const float  Scale = 52.0f / 0.6f;
        const float  R     = static_cast<float>(Flare.HaloRadius) * Scale;
        const float  W     = static_cast<float>(Flare.HaloWidth) * Scale;
        Frame.Ink->AddCircle(RingCentre, 54.0f, KitInkRgba(189, 190, 169, 34), 36);
        Frame.Ink->AddCircle(RingCentre, R, KitInkRgba(229, 213, 171, 150), 36, 1.6f);
        Frame.Ink->AddCircle(RingCentre, std::max(1.0f, R - W), KitInkRgba(229, 213, 171, 55), 36);
        Frame.Ink->AddCircle(RingCentre, R + W, KitInkRgba(229, 213, 171, 55), 36);
        char HaloRead[48] = {};
        std::snprintf(HaloRead, sizeof(HaloRead), "R %.2f  +/- %.3f UV", Flare.HaloRadius, Flare.HaloWidth);
        KitDrawText(Frame.Ink, Frame.Faces.SemiBold, 12.5f,
                    ImVec2(HaloMin.x + Frame.Width * 0.58f, HaloMin.y + 46.0f), KitInkHex(0xe5d5ab), HaloRead);
        KitDrawText(Frame.Ink, Frame.Faces.Regular, 10.0f,
                    ImVec2(HaloMin.x + Frame.Width * 0.58f, HaloMin.y + 66.0f), KitInkHex(0xa3a393),
                    "RESPONSE SUPPORT");
        ImGui::SetCursorScreenPos(ImVec2(Frame.Left, HaloMax.y + 8.0f));
        ImGui::Dummy(ImVec2(Frame.Width, 0.0f));

        if (DrawObservatorySlider(Frame, "##HaloRad", "Halo radius", Flare.HaloRadius, 0.01, 0.60, 0.01, "UV", 2,
                                  Accent) |
            DrawObservatorySlider(Frame, "##HaloWid", "Halo width", Flare.HaloWidth, 0.005, 0.20, 0.002, "UV", 3,
                                  Accent) |
            DrawObservatorySlider(Frame, "##HaloBri", "Halo brightness", Flare.HaloBrightness, 0.0, 2.0, 0.02,
                                  "\xC3\x97", 2, Accent))
        {
            Flare.PresetIndex = static_cast<int32_t>(LensFlarePresetCategory::CustomLayers);
        }
    }
    else
    {
        DrawSectionCardHeader(Frame, "Diffraction & streaks", "05 / LAYER MIXER", Accent);
        if (DrawObservatorySwitchRow(Frame, "##FlareAnamorphic", "Anamorphic cool streak", Flare.AnamorphicEnabled,
                                     Accent) |
            DrawObservatorySwitchRow(Frame, "##FlareWarm", "Rotatable warm streak", Flare.WarmStreakEnabled, Accent) |
            DrawObservatorySwitchRow(Frame, "##FlareBurst", "Polygonal starburst", Flare.StarburstEnabled, Accent))
        {
            Flare.PresetIndex = static_cast<int32_t>(LensFlarePresetCategory::CustomLayers);
        }
        char PairCaption[96] = {};
        std::snprintf(PairCaption, sizeof(PairCaption), "%d ray pairs active  (%s)", Flare.QueryActiveRayPairs(),
                      Flare.PresetIndex == static_cast<int32_t>(LensFlarePresetCategory::CustomLayers)
                          ? "Custom ray pairs"
                          : "Preset aperture blades");
        DrawObservatoryNote(Frame, PairCaption);

        DrawObservatorySlider(Frame, "##FlareRot", "Rotation", Flare.RotationDegrees, 0.0, 360.0, 1.0, "\xC2\xB0", 0,
                              Accent);
        if (DrawObservatorySlider(Frame, "##FlareRays", "Ray pairs", Flare.RayPairs, 2.0, 12.0, 1.0, "", 0, Accent) |
            DrawObservatorySlider(Frame, "##FlareGain", "Streak gain", Flare.StreakGain, 0.0, 3.0, 0.02, "\xC3\x97",
                                  2, Accent))
        {
            Flare.PresetIndex = static_cast<int32_t>(LensFlarePresetCategory::CustomLayers);
        }
        DrawObservatorySlider(Frame, "##FlareBlades", "Aperture blades", Flare.ApertureBlades, 2.0, 12.0, 1.0, "", 0,
                              Accent);
    }

    DrawObservatoryFooter(Frame, "CAMERA SENSOR OPTICS");
}

//========================================================================================================================
//                                          4. ATMOSPHERE OBSERVATORY
//========================================================================================================================

void ConstructAtmosphereObservatory(const ObservatoryFrame& Frame, AtmosphereRecord& Atmosphere, uint32_t& ActiveTab,
                                    SceneDirectory& Directory)
{
    const ImU32 Accent = KitInkHex(0xb8d3bd);
    DrawObservatoryHeader(Frame, "ENVIRONMENT  /  03", "Atmosphere", "Scattering, density & the visible sky.",
                          "##AtmoPower", &Atmosphere.Enabled, Accent, "Live", "Off");
    DrawObservatoryTabs(Frame, "Atmo", {"Overview", "Medium", "Light", "Baking"}, ActiveTab, Accent);

    // Query live Sun elevation if a SunRecord is present in SceneDirectory.
    double      SunElevationDeg = 38.0;
    double      SunAzimuthDeg   = 145.0;
    RecordToken SunToken        = Directory.FindTokenByIdentifier("sun");
    if (SunToken.IsValid())
    {
        if (RecordEntry* Entry = RecordStore::Instance().Find(SunToken))
        {
            const auto* Sun = static_cast<const SunRecord*>(Entry);
            SunElevationDeg = Sun->ElevationDegrees;
            SunAzimuthDeg   = Sun->AzimuthDegrees;
        }
    }

    if (ActiveTab == 0)
    {
        // 01 / DENSITY SCAN — Atmospheric column (ported from AtmosphereColumnInstrument.js).
        DrawSectionCardHeader(Frame, "Atmospheric column", "01 / DENSITY SCAN", Accent);
        const ImVec2 ScanMin = ImGui::GetCursorScreenPos();
        const float  ScanH   = 168.0f;
        const ImVec2 ScanMax(Frame.Left + Frame.Width, ScanMin.y + ScanH);
        KitPillSurface(Frame.Ink, ScanMin, ScanMax, Frame.CanvasBg, Frame.Theme.Line, 10.0f);

        const ImVec2 PlanetCentre(Frame.Left + Frame.Width * 0.44f, ScanMax.y + 74.0f);
        const float  LimbRadius = 112.0f;
        for (int Km = 30; Km >= 0; Km -= 2)
        {
            const double RelR  = Atmosphere.QueryRelativeRayleighDensity(static_cast<double>(Km) * 1000.0);
            const double RelM  = Atmosphere.QueryRelativeMieDensity(static_cast<double>(Km) * 1000.0);
            const float  Alpha = std::clamp(
                static_cast<float>(0.04 + RelR * Atmosphere.RayleighMultiplier * 0.22 +
                                   RelM * Atmosphere.MieMultiplier * 0.10),
                0.02f, 0.55f);
            const float LayerRadius = LimbRadius + static_cast<float>(Km) * 3.1f;
            Frame.Ink->PathArcTo(PlanetCentre, LayerRadius, -PiFloat * 0.82f, -PiFloat * 0.18f, 36);
            Frame.Ink->PathStroke(KitInkWithAlpha(Accent, Alpha), 0, 2.2f);
        }
        Frame.Ink->PathArcTo(PlanetCentre, LimbRadius, -PiFloat * 0.85f, -PiFloat * 0.15f, 40);
        Frame.Ink->PathStroke(KitInkRgba(180, 197, 153, 140), 0, 2.0f);

        const float  ProbeR = LimbRadius + static_cast<float>(Atmosphere.ProbeAltitudeMetres / 1000.0) * 3.1f;
        const ImVec2 ProbePt(PlanetCentre.x + std::cos(-1.35f) * ProbeR, PlanetCentre.y + std::sin(-1.35f) * ProbeR);
        Frame.Ink->AddCircle(ProbePt, 8.0f, KitInkHex(0xd4e99b), 20, 1.3f);
        Frame.Ink->AddCircleFilled(ProbePt, 3.2f, KitInkHex(0xe3efc5));
        Frame.Ink->AddLine(ProbePt, ImVec2(ScanMax.x - 96.0f, ProbePt.y - 12.0f), KitInkRgba(212, 233, 155, 160));

        const double ProbeRayleighPct = Atmosphere.QueryRelativeRayleighDensity(Atmosphere.ProbeAltitudeMetres) * 100.0;
        const double ProbeMiePct      = Atmosphere.QueryRelativeMieDensity(Atmosphere.ProbeAltitudeMetres) * 100.0;
        char         ProbeRead[64]    = {};
        std::snprintf(ProbeRead, sizeof(ProbeRead), "%.1f km  \xC2\xB7  %.0f%% air",
                      Atmosphere.ProbeAltitudeMetres / 1000.0, ProbeRayleighPct);
        KitDrawText(Frame.Ink, Frame.Faces.SemiBold, 11.0f,
                    ImVec2(ScanMax.x - 94.0f, std::max(ScanMin.y + 8.0f, ProbePt.y - 20.0f)), KitInkHex(0xe3efc5),
                    ProbeRead);
        char AerosolRead[64] = {};
        std::snprintf(AerosolRead, sizeof(AerosolRead), "Aerosols %.0f%% of sea level", ProbeMiePct);
        KitDrawText(Frame.Ink, Frame.Faces.Regular, 10.0f, ImVec2(ScanMin.x + 12.0f, ScanMax.y - 16.0f),
                    KitInkHex(0xa9b59b), AerosolRead);

        ImGui::SetCursorScreenPos(ImVec2(Frame.Left, ScanMax.y + 6.0f));
        ImGui::Dummy(ImVec2(Frame.Width, 0.0f));
        DrawObservatorySlider(Frame, "##AtmoProbe", "Column probe altitude", Atmosphere.ProbeAltitudeMetres, 0.0,
                              30000.0, 500.0, "m", 0, Accent);

        char RStr[24] = {}, MStr[24] = {}, OStr[24] = {};
        std::snprintf(RStr, sizeof(RStr), "%.2f", Atmosphere.RayleighMultiplier);
        std::snprintf(MStr, sizeof(MStr), "%.2f", Atmosphere.MieMultiplier);
        std::snprintf(OStr, sizeof(OStr), "%.2f", Atmosphere.OzoneMultiplier);
        DrawKpiTriple(Frame, "Rayleigh", RStr, "\xC3\x97", "Mie", MStr, "\xC3\x97", "Ozone", OStr, "\xC3\x97");

        // Sky-view capture panorama (evaluated from Frontier::SingleScattering).
        DrawSectionCardHeader(Frame, "Sky-view capture", "GPU / SCATTERING", Accent);
        const ImVec2 SkyMin = ImGui::GetCursorScreenPos();
        const float  SkyH   = 96.0f;
        const ImVec2 SkyMax(Frame.Left + Frame.Width, SkyMin.y + SkyH);
        KitPillSurface(Frame.Ink, SkyMin, SkyMax, Frame.CanvasBg, Frame.Theme.Line, 10.0f);

        const double        ElRad = SunElevationDeg * (3.14159265358979323846 / 180.0);
        const double        AzRad = SunAzimuthDeg * (3.14159265358979323846 / 180.0);
        const SpatialVector SunDir{std::cos(ElRad) * std::cos(AzRad), std::cos(ElRad) * std::sin(AzRad),
                                   std::sin(ElRad)};
        const SpatialVector ObserverKm{0.0, 0.0, Atmosphere.Parameters.GroundRadiusKm + 0.1};
        const SpectrumRgb   Irradiance{Atmosphere.SkyBrightnessScale * 14.0, Atmosphere.SkyBrightnessScale * 14.0,
                                       Atmosphere.SkyBrightnessScale * 14.0};
        constexpr int       Cols  = 64;
        constexpr int       Rows  = 24;
        const float         CellW = (Frame.Width - 6.0f) / static_cast<float>(Cols);
        const float         CellH = (SkyH - 6.0f) / static_cast<float>(Rows);
        std::vector<ImU32>  SkyGrid(static_cast<size_t>((Cols + 1) * (Rows + 1)));
        for (int Row = 0; Row <= Rows; ++Row)
        {
            const double ViewElev = (1.0 - static_cast<double>(Row) / static_cast<double>(Rows)) * 1.35 - 0.05;
            for (int Col = 0; Col <= Cols; ++Col)
            {
                const double        ViewAz = (static_cast<double>(Col) / static_cast<double>(Cols) - 0.5) * 6.2831853;
                const SpatialVector ViewDir{std::cos(ViewElev) * std::cos(ViewAz),
                                            std::cos(ViewElev) * std::sin(ViewAz), std::sin(ViewElev)};
                const SpectrumRgb   SkyRad =
                    Atmosphere.Enabled
                        ? SingleScattering(Atmosphere.Parameters, ObserverKm, ViewDir, SunDir, Irradiance,
                                           Atmosphere.UseBakedLookup ? &Atmosphere.LookupTable : nullptr, 12)
                        : SpectrumRgb{};
                SkyGrid[static_cast<size_t>(Row * (Cols + 1) + Col)] = ToneMappedLinearRgb(SkyRad, 1.4f);
            }
        }
        for (int Row = 0; Row < Rows; ++Row)
        {
            for (int Col = 0; Col < Cols; ++Col)
            {
                const ImVec2 PMin(SkyMin.x + 3.0f + static_cast<float>(Col) * CellW,
                                  SkyMin.y + 3.0f + static_cast<float>(Row) * CellH);
                const ImVec2 PMax(PMin.x + CellW, PMin.y + CellH);
                const ImU32  C00 = SkyGrid[static_cast<size_t>(Row * (Cols + 1) + Col)];
                const ImU32  C10 = SkyGrid[static_cast<size_t>(Row * (Cols + 1) + Col + 1)];
                const ImU32  C11 = SkyGrid[static_cast<size_t>((Row + 1) * (Cols + 1) + Col + 1)];
                const ImU32  C01 = SkyGrid[static_cast<size_t>((Row + 1) * (Cols + 1) + Col)];
                Frame.Ink->AddRectFilledMultiColor(PMin, PMax, C00, C10, C11, C01);
            }
        }
        char LutTag[64] = {};
        std::snprintf(LutTag, sizeof(LutTag), "LUT rev %u  \xC2\xB7  RGBA16F diagnostic", Atmosphere.LookupRevision);
        KitDrawText(Frame.Ink, Frame.Faces.Medium, 9.8f, ImVec2(SkyMin.x + 10.0f, SkyMax.y - 15.0f),
                    KitInkRgba(235, 244, 238, 215), LutTag);
        ImGui::SetCursorScreenPos(ImVec2(Frame.Left, SkyMax.y + 8.0f));
        ImGui::Dummy(ImVec2(Frame.Width, 0.0f));

        DrawSectionCardHeader(Frame, "Medium recipe", "LIVE COEFFICIENTS", Accent);
        if (DrawObservatorySlider(Frame, "##AtmoRay", "R  Molecular scattering", Atmosphere.RayleighMultiplier, 0.0,
                                  3.0, 0.02, "\xC3\x97", 2, Accent) |
            DrawObservatorySlider(Frame, "##AtmoMie", "M  Aerosol haze", Atmosphere.MieMultiplier, 0.0, 6.0, 0.05,
                                  "\xC3\x97", 2, Accent) |
            DrawObservatorySlider(Frame, "##AtmoO3", "O3 Ozone absorption", Atmosphere.OzoneMultiplier, 0.0, 4.0,
                                  0.02, "\xC3\x97", 2, Accent))
        {
            Atmosphere.RebakeLookupTable();
        }
    }
    else if (ActiveTab == 1)
    {
        DrawSectionCardHeader(Frame, "How the air thins", "SCALE-HEIGHT COMPARATOR", Accent);
        const ImVec2 RibMin = ImGui::GetCursorScreenPos();
        const float  RibH   = 140.0f;
        const ImVec2 RibMax(Frame.Left + Frame.Width, RibMin.y + RibH);
        KitPillSurface(Frame.Ink, RibMin, RibMax, Frame.CanvasBg, Frame.Theme.Line, 10.0f);

        const float MidX   = Frame.Left + Frame.Width * 0.5f;
        const float TopY   = RibMin.y + 14.0f;
        const float BotY   = RibMax.y - 20.0f;
        const float MaxSpan = Frame.Width * 0.38f;
        Frame.Ink->AddLine(ImVec2(MidX - 16.0f, TopY), ImVec2(MidX - 16.0f, BotY), Frame.CanvasGrid);
        Frame.Ink->AddLine(ImVec2(MidX + 16.0f, TopY), ImVec2(MidX + 16.0f, BotY), Frame.CanvasGrid);
        for (int Step = 1; Step <= 40; ++Step)
        {
            const double Alt0 = static_cast<double>(Step - 1) / 40.0 * 20000.0;
            const double Alt1 = static_cast<double>(Step) / 40.0 * 20000.0;
            const float  Y0   = BotY - static_cast<float>(Alt0 / 20000.0) * (BotY - TopY);
            const float  Y1   = BotY - static_cast<float>(Alt1 / 20000.0) * (BotY - TopY);
            Frame.Ink->AddLine(
                ImVec2(MidX - 16.0f - static_cast<float>(Atmosphere.QueryRelativeRayleighDensity(Alt0)) * MaxSpan, Y0),
                ImVec2(MidX - 16.0f - static_cast<float>(Atmosphere.QueryRelativeRayleighDensity(Alt1)) * MaxSpan, Y1),
                KitInkHex(0xb8d3bd), 2.0f);
            Frame.Ink->AddLine(
                ImVec2(MidX + 16.0f + static_cast<float>(Atmosphere.QueryRelativeMieDensity(Alt0)) * MaxSpan, Y0),
                ImVec2(MidX + 16.0f + static_cast<float>(Atmosphere.QueryRelativeMieDensity(Alt1)) * MaxSpan, Y1),
                KitInkHex(0xdecfa8), 2.0f);
        }
        KitDrawText(Frame.Ink, Frame.Faces.Regular, 9.5f, ImVec2(RibMin.x + 10.0f, RibMax.y - 15.0f),
                    KitInkHex(0xb8d3bd), "Rayleigh / molecules");
        KitDrawText(Frame.Ink, Frame.Faces.Regular, 9.5f, ImVec2(RibMax.x - 94.0f, RibMax.y - 15.0f),
                    KitInkHex(0xdecfa8), "Mie / aerosols");
        ImGui::SetCursorScreenPos(ImVec2(Frame.Left, RibMax.y + 8.0f));
        ImGui::Dummy(ImVec2(Frame.Width, 0.0f));

        if (DrawObservatorySlider(Frame, "##AtmoRayH", "Rayleigh scale height", Atmosphere.RayleighScaleMetres,
                                  1000.0, 16000.0, 100.0, "m", 0, Accent) |
            DrawObservatorySlider(Frame, "##AtmoMieH", "Mie scale height", Atmosphere.MieScaleMetres, 500.0, 8000.0,
                                  50.0, "m", 0, Accent) |
            DrawObservatorySlider(Frame, "##AtmoMieG", "Mie anisotropy", Atmosphere.MieAnisotropyG, -0.95, 0.95, 0.01,
                                  "g", 2, Accent))
        {
            Atmosphere.RebakeLookupTable();
        }
    }
    else if (ActiveTab == 2)
    {
        DrawSectionCardHeader(Frame, "Light through air", "TRANSMISSION", Accent);
        const SpectrumRgb Trans   = Atmosphere.EvaluateSolarTransmission(SunElevationDeg);
        const double      Neutral = Trans.R * 0.2126 + Trans.G * 0.7152 + Trans.B * 0.0722;

        const ImVec2 DialMin = ImGui::GetCursorScreenPos();
        const float  DialH   = 126.0f;
        const ImVec2 DialMax(Frame.Left + Frame.Width, DialMin.y + DialH);
        KitPillSurface(Frame.Ink, DialMin, DialMax, Frame.CanvasBg, Frame.Theme.Line, 10.0f);

        const ImVec2 ArcCentre(Frame.Left + Frame.Width * 0.5f, DialMax.y - 22.0f);
        const std::array<double, 3> Fractions{std::clamp(Trans.R, 0.0, 1.0), std::clamp(Trans.G, 0.0, 1.0),
                                              std::clamp(Trans.B, 0.0, 1.0)};
        const std::array<ImU32, 3>  Colours{KitInkHex(0xd2a49b), KitInkHex(0xb6cc9c), KitInkHex(0xa4c6d7)};
        for (size_t Ch = 0; Ch < 3; ++Ch)
        {
            const float Radius = 84.0f - static_cast<float>(Ch) * 14.0f;
            Frame.Ink->PathArcTo(ArcCentre, Radius, PiFloat, PiFloat * 2.0f, 36);
            Frame.Ink->PathStroke(KitInkRgba(197, 207, 185, 26), 0, 5.0f);
            if (Fractions[Ch] > 0.005)
            {
                Frame.Ink->PathArcTo(ArcCentre, Radius, PiFloat,
                                       PiFloat + static_cast<float>(Fractions[Ch]) * PiFloat, 36);
                Frame.Ink->PathStroke(Colours[Ch], 0, 5.0f);
            }
        }
        char NeutralText[32] = {};
        std::snprintf(NeutralText, sizeof(NeutralText), "%.1f%%", Neutral * 100.0);
        const float NW = KitTextWidth(Frame.Faces.SemiBold, 16.0f, NeutralText);
        KitDrawText(Frame.Ink, Frame.Faces.SemiBold, 16.0f, ImVec2(ArcCentre.x - NW * 0.5f, ArcCentre.y - 26.0f),
                    Frame.Theme.Text, NeutralText);
        ImGui::SetCursorScreenPos(ImVec2(Frame.Left, DialMax.y + 8.0f));
        ImGui::Dummy(ImVec2(Frame.Width, 0.0f));

        char RTr[24] = {}, GTr[24] = {}, BTr[24] = {};
        std::snprintf(RTr, sizeof(RTr), "%.1f", Trans.R * 100.0);
        std::snprintf(GTr, sizeof(GTr), "%.1f", Trans.G * 100.0);
        std::snprintf(BTr, sizeof(BTr), "%.1f", Trans.B * 100.0);
        DrawKpiTriple(Frame, "Red", RTr, "%", "Green", GTr, "%", "Blue", BTr, "%");

        DrawObservatorySwitchRow(Frame, "##AtmoAirMass", "Apply air-mass attenuation", Atmosphere.AirMassAttenuation,
                                 Accent);
        DrawObservatorySwitchRow(Frame, "##AtmoMulti", "Multiple scattering", Atmosphere.MultipleScattering, Accent);
        DrawObservatorySlider(Frame, "##AtmoSkyScale", "Sky brightness", Atmosphere.SkyBrightnessScale, 0.0, 3.0,
                              0.02, "\xC3\x97", 2, Accent);
        DrawObservatorySlider(Frame, "##AtmoLimb", "Limb glow", Atmosphere.LimbGlow, 0.0, 3.0, 0.02, "\xC3\x97",
                              2, Accent);
        DrawObservatorySlider(Frame, "##AtmoWhite", "White line", Atmosphere.BoundaryLine, 0.0, 3.0, 0.02, "\xC3\x97",
                              2, Accent);
    }
    else
    {
        DrawSectionCardHeader(Frame, "Transmittance baking", "04 / GPU LOOKUP", Accent);
        DrawObservatorySwitchRow(Frame, "##AtmoUseBake", "Use baked 128x32 lookup", Atmosphere.UseBakedLookup, Accent);
        if (DrawObservatorySlider(Frame, "##AtmoSamples", "Integration samples", Atmosphere.LookupSamples, 8.0, 64.0,
                                  1.0, "samples", 0, Accent))
        {
            Atmosphere.RebakeLookupTable();
        }
        if (DrawObservatoryActionButton(Frame, "##AtmoRebakeBtn", "Rebake Transmittance LUT", Accent))
        {
            Atmosphere.RebakeLookupTable();
        }
    }

    DrawObservatoryFooter(Frame, "ATMOSPHERIC SCATTERING");
}

//========================================================================================================================
//                                            5. CLOUDS OBSERVATORY
//========================================================================================================================

void ConstructCloudObservatory(const ObservatoryFrame& Frame, CloudRecord& Clouds, uint32_t& ActiveTab,
                               SceneDirectory& Directory, SceneDirectoryPanel& DirectoryPanel)
{
    const ImU32 Accent = KitInkHex(0xd4e5a8);
    DrawObservatoryHeader(Frame, "ENVIRONMENT  /  01", "Clouds", "Density field / shape & illumination.",
                          "##CloudPower", &Clouds.Enabled, Accent, "Live", "Off");
    DrawObservatoryTabs(Frame, "Cloud", {"Overview", "Formation", "Rendering", "Weather"}, ActiveTab, Accent);

    if (ActiveTab == 0)
    {
        // 01 / FIELD — Top-down FBM density atlas (sampled from CloudRecord::SampleTopDownDensity).
        DrawSectionCardHeader(Frame, "Density atlas", "01 / FIELD", Accent);
        const ImVec2 AtlasMin = ImGui::GetCursorScreenPos();
        const float  AtlasH   = 136.0f;
        const ImVec2 AtlasMax(Frame.Left + Frame.Width, AtlasMin.y + AtlasH);
        KitPillSurface(Frame.Ink, AtlasMin, AtlasMax, KitInkHex(0x111a20), Frame.Theme.Line, 10.0f);

        constexpr int      Cols  = 76;
        constexpr int      Rows  = 38;
        const float        CellW = (Frame.Width - 6.0f) / static_cast<float>(Cols);
        const float        CellH = (AtlasH - 6.0f) / static_cast<float>(Rows);
        std::vector<ImU32> AtlasGrid(static_cast<size_t>((Cols + 1) * (Rows + 1)));
        for (int Row = 0; Row <= Rows; ++Row)
        {
            const double V = static_cast<double>(Row) / static_cast<double>(Rows);
            for (int Col = 0; Col <= Cols; ++Col)
            {
                const double U       = static_cast<double>(Col) / static_cast<double>(Cols);
                const double Density = Clouds.SampleTopDownDensity(U, V);
                const int    R       = std::clamp(static_cast<int>(18.0 + Density * 196.0), 0, 255);
                const int    G       = std::clamp(static_cast<int>(28.0 + Density * 212.0), 0, 255);
                const int    B       = std::clamp(static_cast<int>(36.0 + Density * 198.0), 0, 255);
                AtlasGrid[static_cast<size_t>(Row * (Cols + 1) + Col)] = KitInkRgba(R, G, B, 255);
            }
        }
        for (int Row = 0; Row < Rows; ++Row)
        {
            for (int Col = 0; Col < Cols; ++Col)
            {
                const ImVec2 PMin(AtlasMin.x + 3.0f + static_cast<float>(Col) * CellW,
                                  AtlasMin.y + 3.0f + static_cast<float>(Row) * CellH);
                const ImVec2 PMax(PMin.x + CellW, PMin.y + CellH);
                const ImU32  C00 = AtlasGrid[static_cast<size_t>(Row * (Cols + 1) + Col)];
                const ImU32  C10 = AtlasGrid[static_cast<size_t>(Row * (Cols + 1) + Col + 1)];
                const ImU32  C11 = AtlasGrid[static_cast<size_t>((Row + 1) * (Cols + 1) + Col + 1)];
                const ImU32  C01 = AtlasGrid[static_cast<size_t>((Row + 1) * (Cols + 1) + Col)];
                Frame.Ink->AddRectFilledMultiColor(PMin, PMax, C00, C10, C11, C01);
            }
        }
        const ImVec2 Reticle(Frame.Left + Frame.Width * 0.5f, AtlasMin.y + AtlasH * 0.5f);
        Frame.Ink->AddCircle(Reticle, 15.0f, KitInkRgba(212, 230, 168, 130), 24);
        Frame.Ink->AddCircleFilled(Reticle, 2.5f, Accent);
        KitDrawText(Frame.Ink, Frame.Faces.Medium, 9.8f, ImVec2(AtlasMin.x + 10.0f, AtlasMax.y - 15.0f),
                    KitInkHex(0xd8e4db), "XY / WORLD FIELD");
        KitDrawText(Frame.Ink, Frame.Faces.Medium, 9.8f, ImVec2(AtlasMax.x - 84.0f, AtlasMin.y + 8.0f), Accent,
                    Clouds.QueryFormationName());
        ImGui::SetCursorScreenPos(ImVec2(Frame.Left, AtlasMax.y + 8.0f));
        ImGui::Dummy(ImVec2(Frame.Width, 0.0f));

        char FloorStr[24] = {}, TopStr[24] = {}, RayStr[24] = {};
        std::snprintf(FloorStr, sizeof(FloorStr), "%.2f", Clouds.FloorAltitudeMetres / 1000.0);
        std::snprintf(TopStr, sizeof(TopStr), "%.2f", Clouds.QueryEffectiveTopMetres() / 1000.0);
        std::snprintf(RayStr, sizeof(RayStr), "%u", Clouds.QueryCompleteLayerRaySamples());
        DrawKpiTriple(Frame, "Layer floor", FloorStr, "km", "Effective top", TopStr, "km", "Layer ray", RayStr,
                      "samples");

        // 02 / MIX — Interactive 2D Coverage x Density Pad.
        DrawSectionCardHeader(Frame, "Shape the field", "02 / MIX", Accent);
        const ImVec2 MixMin = ImGui::GetCursorScreenPos();
        const float  MixH   = 106.0f;
        const ImVec2 MixMax(Frame.Left + Frame.Width, MixMin.y + MixH);
        KitPillSurface(Frame.Ink, MixMin, MixMax, Frame.CanvasBg, Frame.Theme.Line, 10.0f);

        ImGui::SetCursorScreenPos(MixMin);
        ImGui::InvisibleButton("##CloudMixPad", ImVec2(Frame.Width, MixH));
        if (ImGui::IsItemActive())
        {
            Clouds.Coverage = std::clamp(
                static_cast<double>((ImGui::GetIO().MousePos.x - (MixMin.x + 16.0f)) / (Frame.Width - 32.0f)), 0.0, 1.0);
            Clouds.DensityMultiplier = std::clamp(
                static_cast<double>((MixMax.y - 18.0f - ImGui::GetIO().MousePos.y) / (MixH - 30.0f)) * 4.0, 0.0, 4.0);
            Clouds.SynchronizeParameters();
        }
        const float PX = MixMin.x + 16.0f + static_cast<float>(Clouds.Coverage) * (Frame.Width - 32.0f);
        const float PY = (MixMax.y - 18.0f) - static_cast<float>(Clouds.DensityMultiplier / 4.0) * (MixH - 30.0f);
        Frame.Ink->AddRectFilled(ImVec2(MixMin.x + 16.0f, PY), ImVec2(PX, MixMax.y - 18.0f),
                                   KitInkRgba(199, 221, 150, 36), 3.0f);
        Frame.Ink->AddCircle(ImVec2(PX, PY), 8.5f, Accent, 20, 1.3f);
        Frame.Ink->AddCircleFilled(ImVec2(PX, PY), 3.5f, Accent);
        KitDrawText(Frame.Ink, Frame.Faces.Regular, 9.2f, ImVec2(MixMin.x + 14.0f, MixMax.y - 14.0f),
                    KitInkHex(0x8d9a8c), "CLEAR");
        KitDrawText(Frame.Ink, Frame.Faces.Regular, 9.2f, ImVec2(MixMax.x - 62.0f, MixMax.y - 14.0f),
                    KitInkHex(0x8d9a8c), "OVERCAST");
        ImGui::SetCursorScreenPos(ImVec2(Frame.Left, MixMax.y + 8.0f));
        ImGui::Dummy(ImVec2(Frame.Width, 0.0f));

        double CoveragePercent = Clouds.Coverage * 100.0;
        if (DrawObservatorySlider(Frame, "##CloudCov", "Coverage", CoveragePercent, 0.0, 100.0, 1.0, "%", 0, Accent))
        {
            Clouds.Coverage = CoveragePercent / 100.0;
            Clouds.SynchronizeParameters();
        }
        if (DrawObservatorySlider(Frame, "##CloudDens", "Density multiplier", Clouds.DensityMultiplier, 0.0, 4.0,
                                  0.05, "\xC3\x97", 2, Accent))
        {
            Clouds.SynchronizeParameters();
        }
    }
    else if (ActiveTab == 1)
    {
        DrawSectionCardHeader(Frame, "Layer anatomy", "03 / PROFILE & STACK", Accent);
        DrawObservatoryOptionStrip(
            Frame, "CloudForm", "Formation",
            {"Stratus", "Stratocumulus", "Cumulus", "Cumulonimbus", "Altostratus", "Cirrus"}, Clouds.FormationIndex,
            Accent);

        // Axonometric Cloud Layer Stack (ported from CloudLayerCard.js).
        const ImVec2 StackMin = ImGui::GetCursorScreenPos();
        const float  StackH   = 126.0f;
        const ImVec2 StackMax(Frame.Left + Frame.Width, StackMin.y + StackH);
        KitPillSurface(Frame.Ink, StackMin, StackMax, Frame.CanvasBg, Frame.Theme.Line, 10.0f);

        const float BottomY = StackMax.y - 22.0f;
        const float TopY    = BottomY - 82.0f * static_cast<float>(std::sqrt(Clouds.QueryRenderedDepthMetres() / 6000.0));
        const float StrataX = StackMin.x + Frame.Width * 0.60f;
        for (int Layer = 0; Layer <= 14; ++Layer)
        {
            const double H       = (static_cast<double>(Layer) + 0.5) / 15.0;
            const double Profile = CloudRecord::EvaluateFormationProfile(H, Clouds.FormationIndex, Clouds.AnvilShaping);
            const float  Y       = BottomY - (BottomY - TopY) * static_cast<float>(H);
            const float  RX      = 38.0f + static_cast<float>(Profile) * 24.0f;
            Frame.Ink->AddEllipseFilled(ImVec2(StrataX, Y), ImVec2(RX, 9.5f), KitInkRgba(189, 213, 223, 26), 0.0f, 24);
            Frame.Ink->AddEllipse(ImVec2(StrataX, Y), ImVec2(RX, 9.5f), KitInkRgba(184, 209, 222, 48), 0.0f, 24);
        }
        Frame.Ink->AddLine(ImVec2(StackMin.x + 42.0f, TopY), ImVec2(StackMin.x + 42.0f, BottomY), KitInkHex(0xa8c7d7),
                             1.6f);
        Frame.Ink->AddLine(ImVec2(StackMin.x + 34.0f, TopY), ImVec2(StackMin.x + 52.0f, TopY), KitInkHex(0xb7d8e9), 2.0f);
        Frame.Ink->AddLine(ImVec2(StackMin.x + 34.0f, BottomY), ImVec2(StackMin.x + 52.0f, BottomY),
                             KitInkHex(0xb7d8e9), 2.0f);
        KitDrawText(Frame.Ink, Frame.Faces.Medium, 9.5f, ImVec2(StackMin.x + 10.0f, TopY - 5.0f),
                    KitInkHex(0xb9d4e0), "TOP");
        KitDrawText(Frame.Ink, Frame.Faces.Medium, 9.5f, ImVec2(StackMin.x + 10.0f, BottomY - 5.0f),
                    KitInkHex(0x899ba6), "FLOOR");
        if (Clouds.QueryCeilingClipped())
        {
            KitDrawText(Frame.Ink, Frame.Faces.Medium, 9.5f, ImVec2(StackMax.x - 98.0f, StackMin.y + 8.0f), Accent,
                        "CEILING LIMITED");
        }
        ImGui::SetCursorScreenPos(ImVec2(Frame.Left, StackMax.y + 8.0f));
        ImGui::Dummy(ImVec2(Frame.Width, 0.0f));

        if (DrawObservatorySlider(Frame, "##CloudBase", "Base altitude", Clouds.FloorAltitudeMetres, 100.0, 20000.0,
                                  100.0, "m", 0, Accent) |
            DrawObservatorySlider(Frame, "##CloudThick", "Layer thickness", Clouds.ThicknessMetres, 100.0, 6000.0,
                                  100.0, "m", 0, Accent) |
            DrawObservatorySlider(Frame, "##CloudCeil", "Ceiling limit", Clouds.CeilingLimitMetres, 4000.0, 20000.0,
                                  100.0, "m", 0, Accent) |
            DrawObservatorySlider(Frame, "##CloudScale", "Feature scale", Clouds.FeatureScale, 0.2, 3.0, 0.05,
                                  "\xC3\x97", 2, Accent) |
            DrawObservatorySlider(Frame, "##CloudAnvil", "Anvil shaping", Clouds.AnvilShaping, 0.0, 1.0, 0.02, "", 2,
                                  Accent) |
            DrawObservatorySlider(Frame, "##CloudDetail", "Edge erosion", Clouds.DetailErosion, 0.0, 1.0, 0.02, "", 2,
                                  Accent))
        {
            Clouds.SynchronizeParameters();
        }

        DrawSectionCardHeader(Frame, "Light response", "04 / SCATTER", Accent);
        DrawObservatorySlider(Frame, "##CloudAniso", "Forward scattering", Clouds.ForwardAnisotropyG, -0.8, 0.8, 0.02,
                              "g", 2, Accent);
        DrawObservatorySlider(Frame, "##CloudAbs", "Light absorption", Clouds.LightAbsorption, 0.0, 1.0, 0.02, "", 2,
                              Accent);
        DrawObservatorySlider(Frame, "##CloudPow", "Powder response", Clouds.PowderResponse, 0.0, 1.0, 0.02, "", 2,
                              Accent);
    }
    else if (ActiveTab == 2)
    {
        DrawSectionCardHeader(Frame, "Resolution & trace", "05 / BUDGET", Accent);
        DrawObservatorySlider(Frame, "##CloudRes", "Cloud resolution", Clouds.ResolutionPercent, 25.0, 100.0, 1.0,
                              "% / axis", 0, Accent);
        DrawObservatorySlider(Frame, "##CloudSteps", "Sky reference samples", Clouds.SkyReferenceSamples, 8.0, 64.0,
                              1.0, "samples", 0, Accent);

        DrawSectionCardHeader(Frame, "Ground shadow field", "06 / OCCLUSION", Accent);
        DrawObservatorySwitchRow(Frame, "##CloudShadOn", "Cast cloud shadows", Clouds.CastShadows, Accent);
        DrawObservatorySlider(Frame, "##CloudShadStr", "Shadow strength", Clouds.ShadowStrength, 0.0, 1.0, 0.02, "",
                              2, Accent);
        DrawObservatorySlider(Frame, "##CloudShadSoft", "Shadow softness", Clouds.ShadowSoftnessMetres, 0.0, 200.0,
                              1.0, "m", 0, Accent);
        DrawObservatorySlider(Frame, "##CloudShadSpan", "Field span", Clouds.ShadowSpanMetres, 1000.0, 16000.0, 100.0,
                              "m", 0, Accent);
    }
    else
    {
        DrawSectionCardHeader(Frame, "Connected atmosphere", "07 / FORCEFIELD", Accent);
        DrawObservatorySwitchRow(Frame, "##CloudFollowWind", "Follow shared wind", Clouds.FollowSharedWind, Accent);
        DrawObservatoryNote(Frame, "Wind is shared with local media and precipitation via Forcefield -> Wind.");
        if (DrawObservatoryActionButton(Frame, "##OpenWindForceField", "Open Wind ForceField  ->", Accent))
        {
            const RecordToken WindToken = Directory.FindTokenByIdentifier("wind");
            if (WindToken.IsValid())
            {
                DirectoryPanel.Select(WindToken);
                DirectoryPanel.Synchronize(Directory);
            }
        }
    }

    DrawObservatoryFooter(Frame, "GPU VOLUMETRIC FIELD");
}

//========================================================================================================================
//                                             6. WIND OBSERVATORY
//========================================================================================================================

void ConstructWindObservatory(const ObservatoryFrame& Frame, WindRecord& Wind, uint32_t& ActiveTab)
{
    const ImU32 Accent = KitInkHex(0xc7d8ac);
    DrawObservatoryHeader(Frame, "FORCEFIELD / SHARED WEATHER", "Wind",
                          "A direction, a pulse, a changing column of air.", "##WindPower", &Wind.Enabled, Accent,
                          "On", "Off");
    DrawObservatoryTabs(Frame, "Wind", {"Flow", "Variability", "Altitude", "Receivers"}, ActiveTab, Accent);

    if (ActiveTab == 0)
    {
        DrawSectionCardHeader(Frame, "Direction of travel", "01 / BASE FLOW", Accent);
        // Hero readout: "7.0 m/s  FROM 270 deg"
        {
            const ImVec2 SumPos = ImGui::GetCursorScreenPos();
            char         Spd[32] = {}, From[32] = {};
            std::snprintf(Spd, sizeof(Spd), "%.1f m/s", Wind.ReferenceSpeedMps);
            std::snprintf(From, sizeof(From), "FROM %.0f\xC2\xB0", Wind.QueryFromBearingDegrees());
            KitDrawText(Frame.Ink, Frame.Faces.SemiBold, 18.0f, ImVec2(Frame.Left, SumPos.y), Frame.Theme.Text,
                        Spd);
            const float FromW = KitTextWidth(Frame.Faces.Medium, 12.0f, From);
            KitDrawText(Frame.Ink, Frame.Faces.Medium, 12.0f,
                        ImVec2(Frame.Left + Frame.Width - FromW, SumPos.y + 4.0f), Accent, From);
            ImGui::SetCursorScreenPos(ImVec2(Frame.Left, SumPos.y + 26.0f));
            ImGui::Dummy(ImVec2(Frame.Width, 0.0f));
        }

        // Interactive Meteorological Wind-Flow Compass (ported from NativeWindPanel.js).
        const ImVec2 CompMin = ImGui::GetCursorScreenPos();
        const float  CompH   = 196.0f;
        const ImVec2 CompMax(Frame.Left + Frame.Width, CompMin.y + CompH);
        KitPillSurface(Frame.Ink, CompMin, CompMax, Frame.CanvasBg, Frame.Theme.Line, 10.0f);

        const ImVec2 Centre(Frame.Left + Frame.Width * 0.5f, CompMin.y + CompH * 0.48f);
        const float  CompassR = std::min(Frame.Width * 0.34f, 68.0f);

        ImGui::SetCursorScreenPos(CompMin);
        ImGui::InvisibleButton("##WindCompassDrag", ImVec2(Frame.Width, CompH));
        if (ImGui::IsItemActive())
        {
            const float Dx = ImGui::GetIO().MousePos.x - Centre.x;
            const float Dy = Centre.y - ImGui::GetIO().MousePos.y;
            double      NewTravel = std::atan2(Dx, Dy) * (180.0 / 3.14159265358979323846) + 180.0;
            NewTravel             = std::fmod(NewTravel, 360.0);
            if (NewTravel < 0.0)
            {
                NewTravel += 360.0;
            }
            Wind.TravelBearingDegrees = NewTravel;
            Wind.ReferenceSpeedMps =
                std::clamp(static_cast<double>(std::hypot(Dx, Dy) / CompassR) * 40.0, 0.0, 40.0);
        }

        Frame.Ink->AddCircle(Centre, CompassR, KitInkRgba(193, 214, 184, 52), 48);
        Frame.Ink->AddCircle(Centre, CompassR * 0.62f, KitInkRgba(193, 214, 184, 24), 48);

        const float  TravelRad = static_cast<float>(Wind.TravelBearingDegrees) * (PiFloat / 180.0f);
        const ImVec2 U(std::sin(TravelRad), -std::cos(TravelRad));
        const ImVec2 V(-U.y, U.x);
        auto         FlowPoint = [&](float Along, float Across) -> ImVec2 {
            return ImVec2(Centre.x + U.x * Along + V.x * Across, Centre.y + U.y * Along + V.y * Across);
        };

        for (int Stream = -2; Stream <= 2; ++Stream)
        {
            const float Across = static_cast<float>(Stream) * (CompassR * 0.25f);
            const float HalfL  = std::sqrt(std::max(0.0f, CompassR * CompassR * 0.78f - Across * Across));
            for (int Seg = 1; Seg <= 18; ++Seg)
            {
                const float T0 = static_cast<float>(Seg - 1) / 18.0f;
                const float T1 = static_cast<float>(Seg) / 18.0f;
                Frame.Ink->AddLine(
                    FlowPoint(-HalfL + 2.0f * HalfL * T0, Across + std::sin(T0 * PiFloat) * Stream * 1.6f),
                    FlowPoint(-HalfL + 2.0f * HalfL * T1, Across + std::sin(T1 * PiFloat) * Stream * 1.6f),
                    KitInkRgba(185, 201, 161, 48), 1.3f);
            }
            if (Wind.ReferenceSpeedMps > 0.1)
            {
                const ImVec2 Tip  = FlowPoint(HalfL * 0.50f, Across);
                const ImVec2 Tail = FlowPoint(HalfL * 0.50f - 8.0f, Across);
                Frame.Ink->AddLine(Tail, Tip, Accent, 1.8f);
                Frame.Ink->AddLine(FlowPoint(HalfL * 0.50f - 5.5f, Across - 3.5f), Tip, Accent, 1.6f);
                Frame.Ink->AddLine(FlowPoint(HalfL * 0.50f - 5.5f, Across + 3.5f), Tip, Accent, 1.6f);
            }
        }
        const ImVec2 SourceDot = FlowPoint(-CompassR * static_cast<float>(Wind.ReferenceSpeedMps / 40.0), 0.0f);
        Frame.Ink->AddLine(Centre, SourceDot, KitInkRgba(223, 228, 186, 120), 1.3f);
        Frame.Ink->AddCircleFilled(SourceDot, 5.2f, KitInkHex(0xe1e5bc));
        Frame.Ink->AddCircleFilled(Centre, 2.8f, KitInkHex(0x94ab8a));

        KitDrawText(Frame.Ink, Frame.Faces.Medium, 10.0f, ImVec2(Centre.x - 16.0f, CompMin.y + 6.0f),
                    KitInkHex(0xa7b69b), "N / +Y");
        KitDrawText(Frame.Ink, Frame.Faces.Medium, 10.0f, ImVec2(Centre.x + CompassR + 9.0f, Centre.y - 5.0f),
                    KitInkHex(0xa7b69b), "E");
        KitDrawText(Frame.Ink, Frame.Faces.Medium, 10.0f, ImVec2(Centre.x - CompassR - 16.0f, Centre.y - 5.0f),
                    KitInkHex(0xa7b69b), "W");
        KitDrawText(Frame.Ink, Frame.Faces.Medium, 10.0f, ImVec2(Centre.x - 3.0f, Centre.y + CompassR + 5.0f),
                    KitInkHex(0xa7b69b), "S");
        KitDrawText(Frame.Ink, Frame.Faces.Regular, 9.2f, ImVec2(CompMin.x + 12.0f, CompMax.y - 14.0f),
                    KitInkHex(0x91a38c), "DOT = WIND FROM  /  ARROWS = TRAVEL TO");
        ImGui::SetCursorScreenPos(ImVec2(Frame.Left, CompMax.y + 8.0f));
        ImGui::Dummy(ImVec2(Frame.Width, 0.0f));

        DrawObservatorySlider(Frame, "##WindSpeed", "Reference speed", Wind.ReferenceSpeedMps, 0.0, 40.0, 0.2, "m/s",
                              1, Accent);
        double FromDeg = Wind.QueryFromBearingDegrees();
        if (DrawObservatorySlider(Frame, "##WindFrom", "Bearing from", FromDeg, 0.0, 360.0, 1.0, "\xC2\xB0", 0,
                                  Accent))
        {
            Wind.ConfigureFromBearingDegrees(FromDeg);
        }
    }
    else if (ActiveTab == 1)
    {
        DrawSectionCardHeader(Frame, "The gust envelope", "02 / SPEED MODULATION", Accent);
        const ImVec2 GustMin = ImGui::GetCursorScreenPos();
        const float  GustH   = 118.0f;
        const ImVec2 GustMax(Frame.Left + Frame.Width, GustMin.y + GustH);
        KitPillSurface(Frame.Ink, GustMin, GustMax, Frame.CanvasBg, Frame.Theme.Line, 10.0f);

        const double PeakBound = std::max(1.0, Wind.ReferenceSpeedMps * (1.0 + Wind.GustStrength));
        auto         MapSpeedY = [&](double Speed) -> float {
            return (GustMax.y - 16.0f) - static_cast<float>(Speed / PeakBound) * (GustH - 32.0f);
        };
        Frame.Ink->AddRectFilled(
            ImVec2(GustMin.x + 12.0f, MapSpeedY(Wind.ReferenceSpeedMps * (1.0 + Wind.GustStrength))),
            ImVec2(GustMax.x - 12.0f, MapSpeedY(Wind.ReferenceSpeedMps * (1.0 - Wind.GustStrength))),
            KitInkRgba(185, 209, 166, 26), 4.0f);
        for (int Step = 1; Step <= 96; ++Step)
        {
            const double T0 = static_cast<double>(Step - 1) / 96.0 * 40.0 * (0.35 + Wind.GustStrength * 0.4);
            const double T1 = static_cast<double>(Step) / 96.0 * 40.0 * (0.35 + Wind.GustStrength * 0.4);
            const float  X0 = GustMin.x + 12.0f + static_cast<float>(Step - 1) / 96.0f * (Frame.Width - 24.0f);
            const float  X1 = GustMin.x + 12.0f + static_cast<float>(Step) / 96.0f * (Frame.Width - 24.0f);
            Frame.Ink->AddLine(
                ImVec2(X0, MapSpeedY(Wind.ReferenceSpeedMps * WindRecord::EvaluateGustFactor(Wind.GustStrength, T0))),
                ImVec2(X1, MapSpeedY(Wind.ReferenceSpeedMps * WindRecord::EvaluateGustFactor(Wind.GustStrength, T1))),
                KitInkHex(0xcedab4), 1.8f);
        }
        ImGui::SetCursorScreenPos(ImVec2(Frame.Left, GustMax.y + 8.0f));
        ImGui::Dummy(ImVec2(Frame.Width, 0.0f));

        DrawObservatorySlider(Frame, "##WindGust", "Gust strength", Wind.GustStrength, 0.0, 1.0, 0.01, "", 2, Accent);

        DrawSectionCardHeader(Frame, "Cross-flow perturbation", "03 / VELOCITY AXES", Accent);
        DrawObservatorySlider(Frame, "##WindTurb", "Turbulence", Wind.Turbulence, 0.0, 1.0, 0.01, "", 2, Accent);
    }
    else if (ActiveTab == 2)
    {
        DrawSectionCardHeader(Frame, "Wind through altitude", "04 / VECTOR LADDER", Accent);
        const ImVec2 LadMin = ImGui::GetCursorScreenPos();
        const float  LadH   = 150.0f;
        const ImVec2 LadMax(Frame.Left + Frame.Width, LadMin.y + LadH);
        KitPillSurface(Frame.Ink, LadMin, LadMax, Frame.CanvasBg, Frame.Theme.Line, 10.0f);

        const double MaxSpeed = std::max(1.0, Wind.ReferenceSpeedMps * (1.0 + Wind.ShearPerKilometre * 3.0));
        const float  SpineX   = LadMin.x + Frame.Width * 0.42f;
        Frame.Ink->AddLine(ImVec2(SpineX, LadMin.y + 12.0f), ImVec2(SpineX, LadMax.y - 12.0f), Frame.CanvasGrid);
        for (int Rung = 0; Rung < 6; ++Rung)
        {
            const double AltM  = static_cast<double>(Rung) * 600.0;
            const float  Y     = LadMax.y - 20.0f - static_cast<float>(Rung) * 22.0f;
            const double Speed = Wind.ReferenceSpeedMps * (1.0 + Wind.ShearPerKilometre * (AltM / 1000.0));
            const float  A     = static_cast<float>(Wind.TravelBearingDegrees +
                                               Wind.VeerDegreesPerKilometre * (AltM / 1000.0)) *
                            (PiFloat / 180.0f);
            const float  Len   = static_cast<float>(Speed / MaxSpeed) * 56.0f;
            const ImVec2 End(SpineX + std::sin(A) * Len, Y - std::cos(A) * Len * 0.28f);
            Frame.Ink->AddLine(ImVec2(SpineX, Y), End, Accent, 2.0f);
            Frame.Ink->AddCircleFilled(End, 2.8f, KitInkHex(0xdce3be));

            char AltLbl[24] = {}, SpdLbl[24] = {};
            std::snprintf(AltLbl, sizeof(AltLbl), Rung == 0 ? "0 m" : "%.1f km", AltM / 1000.0);
            std::snprintf(SpdLbl, sizeof(SpdLbl), "%.1f m/s", Speed);
            KitDrawText(Frame.Ink, Frame.Faces.Regular, 10.0f, ImVec2(LadMin.x + 12.0f, Y - 5.0f),
                        KitInkHex(0x9aad94), AltLbl);
            const float SW = KitTextWidth(Frame.Faces.Medium, 10.0f, SpdLbl);
            KitDrawText(Frame.Ink, Frame.Faces.Medium, 10.0f, ImVec2(LadMax.x - 12.0f - SW, Y - 5.0f),
                        KitInkHex(0xc8d3b6), SpdLbl);
        }
        ImGui::SetCursorScreenPos(ImVec2(Frame.Left, LadMax.y + 8.0f));
        ImGui::Dummy(ImVec2(Frame.Width, 0.0f));

        DrawObservatorySlider(Frame, "##WindShear", "Speed gain per km", Wind.ShearPerKilometre, 0.0, 2.0, 0.02,
                              "\xC3\x97/km", 2, Accent);
        DrawObservatorySlider(Frame, "##WindVeer", "Direction veer per km", Wind.VeerDegreesPerKilometre, -60.0, 60.0,
                              1.0, "\xC2\xB0/km", 0, Accent);
    }
    else
    {
        DrawSectionCardHeader(Frame, "Where the field goes", "05 / ADVECTION ROUTING", Accent);
        DrawObservatorySwitchRow(Frame, "##WindRouteClouds", "Clouds  (density travel at cloud-base)",
                                 Wind.RouteClouds, Accent);
        DrawObservatorySwitchRow(Frame, "##WindRouteLocalCloud", "Local cloud  (bounded volume advection)",
                                 Wind.RouteLocalCloud, Accent);
        DrawObservatorySwitchRow(Frame, "##WindRouteLocalFog", "Local fog  (authored volume drift)",
                                 Wind.RouteLocalFog, Accent);
        DrawObservatorySwitchRow(Frame, "##WindRoutePrecip", "Precipitation  (GPU falling particles)",
                                 Wind.RoutePrecipitation, Accent);
        DrawObservatorySlider(Frame, "##WindPrecipDrift", "Precipitation wind drift", Wind.PrecipitationDrift, 0.0,
                              1.0, 0.02, "", 2, Accent);
    }

    DrawObservatoryFooter(Frame, "SHARED ADVECTION FORCEFIELD");
}

//========================================================================================================================
//                                    7. DECLARATIVE + CUSTOM RECORD INSPECTOR
//========================================================================================================================

void ConstructStandardOrCustomInspector(const ObservatoryFrame& Frame, const RecordEditorDescription& Description,
                                        const SceneInspectorSummary& Summary, SceneDirectory& Directory,
                                        SceneDirectoryPanel& DirectoryPanel)
{
    const ImU32 Accent = Description.AccentColour != 0 ? static_cast<ImU32>(Description.AccentColour)
                                                       : Frame.Theme.Mint;
    const char* Eyebrow = !Description.Eyebrow.empty() ? Description.Eyebrow.c_str()
                                                       : RecordClassificationName(Summary.Classification);
    const char* Title   = !Summary.Name.empty() ? Summary.Name.c_str() : Description.DisplayName.c_str();
    const char* Sub     = !Description.Subtitle.empty() ? Description.Subtitle.c_str() : "Changes apply live.";

    DrawObservatoryHeader(Frame, Eyebrow, Title, Sub, "##StdRecordPower", Description.EnabledTarget, Accent,
                          "Enabled", "Disabled");

    if (Description.ConstructCustomInspector)
    {
        Description.ConstructCustomInspector(Directory, DirectoryPanel);
    }

    std::string CurrentSection;
    for (size_t Index = 0; Index < Description.OptionFields.size(); ++Index)
    {
        const RecordEditorOptionField& Opt = Description.OptionFields[Index];
        if (Opt.Target == nullptr)
        {
            continue;
        }
        if (CurrentSection != Opt.Section)
        {
            CurrentSection = Opt.Section;
            DrawSectionCardHeader(Frame, CurrentSection.c_str(), "OPTIONS", Accent);
        }
        char Id[48] = {};
        std::snprintf(Id, sizeof(Id), "StdOpt%zu", Index);
        DrawObservatoryOptionStrip(Frame, Id, Opt.Label, Opt.Options, *Opt.Target, Accent);
    }

    for (size_t Index = 0; Index < Description.Switches.size(); ++Index)
    {
        const RecordEditorSwitchField& Sw = Description.Switches[Index];
        if (Sw.Target == nullptr)
        {
            continue;
        }
        if (CurrentSection != Sw.Section)
        {
            CurrentSection = Sw.Section;
            DrawSectionCardHeader(Frame, CurrentSection.c_str(), "SWITCHES", Accent);
        }
        char Id[48] = {};
        std::snprintf(Id, sizeof(Id), "##StdSw%zu", Index);
        DrawObservatorySwitchRow(Frame, Id, Sw.Label, *Sw.Target, Accent);
    }

    for (size_t Index = 0; Index < Description.ScalarFields.size(); ++Index)
    {
        const RecordEditorScalarField& Field = Description.ScalarFields[Index];
        if (Field.Target == nullptr)
        {
            continue;
        }
        if (CurrentSection != Field.Section)
        {
            CurrentSection = Field.Section;
            DrawSectionCardHeader(Frame, CurrentSection.c_str(), "PARAMETERS", Accent);
        }
        char Id[48] = {};
        std::snprintf(Id, sizeof(Id), "##StdScl%zu", Index);
        DrawObservatorySlider(Frame, Id, Field.Label, *Field.Target, Field.Minimum, Field.Maximum, Field.Step,
                              Field.Unit, Field.FractionalDigits, Accent);
    }

    for (size_t Index = 0; Index < Description.ColourFields.size(); ++Index)
    {
        const RecordEditorColourField& Col = Description.ColourFields[Index];
        if (Col.TargetRed == nullptr || Col.TargetGreen == nullptr || Col.TargetBlue == nullptr)
        {
            continue;
        }
        if (CurrentSection != Col.Section)
        {
            CurrentSection = Col.Section;
            DrawSectionCardHeader(Frame, CurrentSection.c_str(), "COLOUR", Accent);
        }
        const ImVec2 WellPos = ImGui::GetCursorScreenPos();
        KitPillSurface(Frame.Ink, ImVec2(Frame.Left, WellPos.y),
                       ImVec2(Frame.Left + Frame.Width, WellPos.y + 30.0f), Frame.Theme.Surface,
                       Frame.Theme.LineSoft, 7.5f);
        Frame.Ink->AddCircleFilled(ImVec2(Frame.Left + 16.0f, WellPos.y + 15.0f), 7.5f,
                                     SrgbSwatch(*Col.TargetRed, *Col.TargetGreen, *Col.TargetBlue));
        KitDrawText(Frame.Ink, Frame.Faces.Regular, 11.8f, ImVec2(Frame.Left + 32.0f, WellPos.y + 8.0f),
                    Frame.Theme.Text, Col.Label);
        ImGui::SetCursorScreenPos(ImVec2(Frame.Left, WellPos.y + 35.0f));
        ImGui::Dummy(ImVec2(Frame.Width, 0.0f));
    }

    DrawObservatoryFooter(Frame, RecordClassificationName(Summary.Classification));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FOG OBSERVATORY
//------------------------------------------------------------------------------------------------------------------------

void ConstructFogObservatory(const ObservatoryFrame& Frame, FogRecord& Fog, uint32_t& ActiveTab)
{
    const ImU32 Accent = KitInkHex(0x9ac4d9);
    DrawObservatoryHeader(Frame, "ENVIRONMENT / AERIAL & HEIGHT FOG", "Fog",
                          "Koschmieder split-target contrast decay, exponential height bank, and solar aureole.",
                          "##FogPower", &Fog.Enabled, Accent, "Fog on", "Clear");

    // 6 receding split-luminance (0.80 / 0.08) Koschmieder contrast targets + interactive distance probe!
    const ImVec2 CanvasMin = ImGui::GetCursorScreenPos();
    const float  CanvasH   = 134.0f;
    const ImVec2 CanvasMax(Frame.Left + Frame.Width, CanvasMin.y + CanvasH);
    KitPillSurface(Frame.Ink, CanvasMin, CanvasMax, Frame.CanvasBg, Frame.Theme.Line, 10.0f);

    const ImU32 FogTint = SrgbSwatch(Fog.InscatterRed, Fog.InscatterGreen, Fog.InscatterBlue);
    Frame.Ink->AddRectFilledMultiColor(
        ImVec2(CanvasMin.x + 2.0f, CanvasMin.y + 2.0f), ImVec2(CanvasMax.x - 2.0f, CanvasMax.y - 26.0f),
        KitInkWithAlpha(FogTint, 0.24f), KitInkWithAlpha(FogTint, 0.42f),
        KitInkWithAlpha(FogTint, 0.62f), KitInkWithAlpha(FogTint, 0.30f));

    static constexpr std::array<double, 6> TargetDistancesMetres{{15.0, 40.0, 90.0, 180.0, 320.0, 520.0}};
    const float PadL  = CanvasMin.x + 14.0f;
    const float SpanW = (Frame.Width - 28.0f) / 6.0f;
    for (size_t Idx = 0; Idx < TargetDistancesMetres.size(); ++Idx)
    {
        const double               Dist   = TargetDistancesMetres[Idx];
        const FogSplitTargetSample Sample = Fog.EvaluateSplitTarget(Dist);
        const float                CX     = PadL + SpanW * (static_cast<float>(Idx) + 0.5f);
        const float                CardW  = std::max(14.0f, SpanW * 0.62f - static_cast<float>(Idx) * 1.6f);
        const float                CardH  = 66.0f - static_cast<float>(Idx) * 6.5f;
        const float                BaseY  = CanvasMax.y - 28.0f;

        const ImU32 TopCol = SrgbSwatch(Sample.BrightLuminance, Sample.BrightLuminance, Sample.BrightLuminance * 1.04);
        const ImU32 BotCol = SrgbSwatch(Sample.DarkLuminance, Sample.DarkLuminance, Sample.DarkLuminance * 1.04);
        Frame.Ink->AddRectFilled(ImVec2(CX - CardW * 0.5f, BaseY - CardH), ImVec2(CX + CardW * 0.5f, BaseY - CardH * 0.5f),
                                 TopCol, 2.5f);
        Frame.Ink->AddRectFilled(ImVec2(CX - CardW * 0.5f, BaseY - CardH * 0.5f), ImVec2(CX + CardW * 0.5f, BaseY),
                                 BotCol, 2.5f);
        Frame.Ink->AddRect(ImVec2(CX - CardW * 0.5f, BaseY - CardH), ImVec2(CX + CardW * 0.5f, BaseY),
                           KitInkRgba(255, 255, 255, 42), 2.5f);

        char DistLbl[24] = {};
        std::snprintf(DistLbl, sizeof(DistLbl), "%.0fm", Dist);
        KitDrawText(Frame.Ink, Frame.Faces.Regular, 9.2f, ImVec2(CX - 11.0f, CanvasMax.y - 19.0f), Frame.Theme.Muted,
                    DistLbl);
    }

    const FogSplitTargetSample ProbeSample = Fog.EvaluateSplitTarget(Fog.ProbeDistanceMetres);
    char                       Caption[96] = {};
    std::snprintf(Caption, sizeof(Caption), "KOSCHMIEDER %.2f km  \xC2\xB7  PROBE %.0f m CONTRAST %.0f%%",
                  Fog.QueryKoschmiederVisibilityMetres() / 1000.0, Fog.ProbeDistanceMetres,
                  ProbeSample.ApparentContrast * 100.0);
    KitDrawText(Frame.Ink, Frame.Faces.Medium, 9.8f, ImVec2(CanvasMin.x + 12.0f, CanvasMin.y + 8.0f), Accent, Caption);
    ImGui::SetCursorScreenPos(ImVec2(Frame.Left, CanvasMax.y + 8.0f));
    ImGui::Dummy(ImVec2(Frame.Width, 0.0f));

    char VisBuf[24] = {}, TransBuf[24] = {}, ContBuf[24] = {};
    std::snprintf(VisBuf, sizeof(VisBuf), "%.2f", Fog.QueryKoschmiederVisibilityMetres() / 1000.0);
    std::snprintf(TransBuf, sizeof(TransBuf), "%.0f", ProbeSample.Transmittance * 100.0);
    std::snprintf(ContBuf, sizeof(ContBuf), "%.1f", ProbeSample.ApparentContrast * 100.0);
    DrawKpiTriple(Frame, "VISIBILITY", VisBuf, "km", "TRANSMIT", TransBuf, "%", "CONTRAST", ContBuf, "%");

    DrawObservatoryTabs(Frame, "Fog", {"Visibility", "Height bank", "Aerial gate", "Sun phase"}, ActiveTab, Accent);

    bool Changed = false;
    if (ActiveTab == 0)
    {
        DrawSectionCardHeader(Frame, "Atmospheric presets & probe", "01 / KOSCHMIEDER", Accent);
        if (DrawObservatoryOptionStrip(Frame, "FogPreset", "Proving grounds profile",
                                       {"Clear", "Morning Mist", "Valley Bank", "Alpine Whiteout"},
                                       Fog.PresetIndex, Accent))
        {
            Fog.ApplyPreset(Fog.PresetIndex);
        }
        Changed |= DrawObservatorySlider(Frame, "##FogProbe", "Contrast target probe", Fog.ProbeDistanceMetres,
                                         5.0, 600.0, 5.0, "m", 0, Accent);
        Changed |= DrawObservatorySlider(Frame, "##FogDens", "Floor extinction density", Fog.DensityAtFloor,
                                         0.0, 0.15, 0.001, "1/m", 3, Accent);
    }
    else if (ActiveTab == 1)
    {
        DrawSectionCardHeader(Frame, "Exponential altitude terrace", "02 / SCALE HEIGHT", Accent);
        Changed |= DrawObservatorySlider(Frame, "##FogFloor", "Reference floor altitude", Fog.FloorAltitudeMetres,
                                         -50.0, 500.0, 1.0, "m", 0, Accent);
        Changed |= DrawObservatorySlider(Frame, "##FogFall", "Exponential scale height", Fog.FalloffHeightMetres,
                                         2.0, 300.0, 1.0, "m", 0, Accent);
        Changed |= DrawObservatorySwitchRow(Frame, "##FogAtmLink", "Couple atmospheric sky limb tint",
                                            Fog.CoupleAtmosphereTint, Accent);
    }
    else if (ActiveTab == 2)
    {
        DrawSectionCardHeader(Frame, "Clear-start aerial perspective", "03 / NEAR GATE", Accent);
        Changed |= DrawObservatorySlider(Frame, "##FogGate", "Clear-start foreground gate", Fog.AerialStartMetres,
                                         0.0, 400.0, 2.0, "m", 0, Accent);
        Changed |= DrawObservatorySlider(Frame, "##FogAerD", "Long-range aerial extinction", Fog.AerialDensity,
                                         0.0, 0.05, 0.0005, "1/m", 4, Accent);
        Changed |= DrawObservatorySlider(Frame, "##FogMie", "Rayleigh-to-Mie aerial blend", Fog.AerialMieBlend,
                                         0.0, 1.0, 0.02, "", 2, Accent);
    }
    else
    {
        DrawSectionCardHeader(Frame, "Forward solar aureole", "04 / PHASE LOBE", Accent);
        Changed |= DrawObservatorySlider(Frame, "##FogPow", "Forward scattering power", Fog.SolarForwardPower,
                                         1.0, 32.0, 0.5, "exp", 1, Accent);
        Changed |= DrawObservatorySlider(Frame, "##FogGain", "Solar in-scatter gain", Fog.SolarScatterGain,
                                         0.0, 3.0, 0.05, "\xC3\x97", 2, Accent);
        Changed |= DrawObservatorySlider(Frame, "##FogR", "Inscatter red", Fog.InscatterRed, 0.0, 1.2, 0.01, "", 2, Accent);
        Changed |= DrawObservatorySlider(Frame, "##FogG", "Inscatter green", Fog.InscatterGreen, 0.0, 1.2, 0.01, "", 2, Accent);
        Changed |= DrawObservatorySlider(Frame, "##FogB", "Inscatter blue", Fog.InscatterBlue, 0.0, 1.2, 0.01, "", 2, Accent);
    }

    if (Changed)
    {
        Fog.SynchronizeParameters();
    }
    DrawObservatoryFooter(Frame, "ENVIRONMENT / FOG MEDIUM");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   STARS OBSERVATORY
//------------------------------------------------------------------------------------------------------------------------

void ConstructStarsObservatory(const ObservatoryFrame& Frame, StarsRecord& Stars, uint32_t& ActiveTab)
{
    const ImU32 Accent = KitInkHex(0x9cb8f2);
    DrawObservatoryHeader(Frame, "CELESTIAL / SIDEREAL PLANISPHERE", "Stars",
                          "Lambert equal-area celestial dome, Hipparcos magnitude histogram, and scintillation.",
                          "##StarsPower", &Stars.Enabled, Accent, "Dome live", "Off");

    // North-pole Lambert equal-area celestial planisphere!
    const ImVec2 DomeMin = ImGui::GetCursorScreenPos();
    const float  DomeH   = 152.0f;
    const ImVec2 DomeMax(Frame.Left + Frame.Width, DomeMin.y + DomeH);
    KitPillSurface(Frame.Ink, DomeMin, DomeMax, KitInkHex(0x0b0f17), Frame.Theme.Line, 10.0f);

    const ImVec2 DomeCentre(Frame.Left + Frame.Width * 0.34f, DomeMin.y + DomeH * 0.50f);
    const float  DomeRadius = 60.0f;
    Frame.Ink->AddCircleFilled(DomeCentre, DomeRadius, KitInkHex(0x070a11), 48);
    Frame.Ink->AddCircle(DomeCentre, DomeRadius, KitInkRgba(156, 184, 242, 95), 48, 1.2f);
    Frame.Ink->AddCircle(DomeCentre, DomeRadius * 0.58f, KitInkRgba(156, 184, 242, 36), 36, 1.0f);
    Frame.Ink->AddLine(ImVec2(DomeCentre.x - DomeRadius, DomeCentre.y), ImVec2(DomeCentre.x + DomeRadius, DomeCentre.y),
                       KitInkRgba(156, 184, 242, 32));
    Frame.Ink->AddLine(ImVec2(DomeCentre.x, DomeCentre.y - DomeRadius), ImVec2(DomeCentre.x, DomeCentre.y + DomeRadius),
                       KitInkRgba(156, 184, 242, 32));

    const double RotRad = Stars.RotationDegrees * (3.14159265358979323846 / 180.0);
    const double CosR   = std::cos(RotRad);
    const double SinR   = std::sin(RotRad);
    for (const StarRecord& Star : Stars.Catalogue)
    {
        if (Star.Direction.Z < 0.0 || Star.Magnitude > Stars.LimitingMagnitude)
        {
            continue;
        }
        // Lambert azimuthal equal-area projection from Z-apex
        const double Radial = std::sqrt(std::max(0.0, 1.0 - Star.Direction.Z));
        const double RX     = Star.Direction.X * CosR - Star.Direction.Y * SinR;
        const double RY     = Star.Direction.X * SinR + Star.Direction.Y * CosR;
        const double Horiz  = std::hypot(RX, RY);
        const float  PX     = DomeCentre.x + (Horiz > 1e-6 ? static_cast<float>((RX / Horiz) * Radial) * DomeRadius : 0.0f);
        const float  PY     = DomeCentre.y - (Horiz > 1e-6 ? static_cast<float>((RY / Horiz) * Radial) * DomeRadius : 0.0f);
        const float  Size   = std::clamp(static_cast<float>((6.8 - Star.Magnitude) * 0.45), 0.9f, 2.8f);
        const ImU32  Tint   = Star.ColourIndex < 0.0 ? KitInkRgba(185, 212, 255, 230) : KitInkRgba(255, 224, 184, 220);
        Frame.Ink->AddCircleFilled(ImVec2(PX, PY), Size, Tint, 8);
    }

    // Right side of hero card: live 10-bin Hipparcos magnitude histogram (-2..+8)
    const auto  Bins   = Stars.QueryMagnitudeHistogram();
    uint32_t    MaxBin = 1u;
    for (uint32_t C : Bins)
    {
        MaxBin = std::max(MaxBin, C);
    }
    const float HistL = Frame.Left + Frame.Width * 0.62f;
    const float HistW = Frame.Width * 0.33f;
    const float HistB = DomeMax.y - 24.0f;
    KitDrawText(Frame.Ink, Frame.Faces.Medium, 9.5f, ImVec2(HistL, DomeMin.y + 12.0f), Accent, "MAGNITUDE BINS");
    for (size_t B = 0; B < Bins.size(); ++B)
    {
        const float BX   = HistL + static_cast<float>(B) * (HistW / 10.0f);
        const float BarH = (static_cast<float>(Bins[B]) / static_cast<float>(MaxBin)) * 88.0f;
        const bool  Lit  = (static_cast<double>(B) - 2.0) <= Stars.LimitingMagnitude;
        Frame.Ink->AddRectFilled(ImVec2(BX + 1.0f, HistB - BarH), ImVec2(BX + (HistW / 10.0f) - 1.5f, HistB),
                                 Lit ? KitInkWithAlpha(Accent, 0.80f) : Frame.Theme.Surface3, 2.0f);
    }
    KitDrawText(Frame.Ink, Frame.Faces.Regular, 9.0f, ImVec2(HistL, HistB + 4.0f), Frame.Theme.Faint, "-2 mag -> +8 mag");
    ImGui::SetCursorScreenPos(ImVec2(Frame.Left, DomeMax.y + 8.0f));
    ImGui::Dummy(ImVec2(Frame.Width, 0.0f));

    char VisStars[24] = {}, MagCut[24] = {}, RotStr[24] = {};
    std::snprintf(VisStars, sizeof(VisStars), "%u", Stars.QueryVisibleStarCount());
    std::snprintf(MagCut, sizeof(MagCut), "%.1f", Stars.LimitingMagnitude);
    std::snprintf(RotStr, sizeof(RotStr), "%.0f", Stars.RotationDegrees);
    DrawKpiTriple(Frame, "VISIBLE", VisStars, "stars", "MAG LIMIT", MagCut, "mag", "SIDEREAL", RotStr, "deg");

    DrawObservatoryTabs(Frame, "Stars", {"Planisphere", "Magnitudes", "Scintillation", "Galactic"}, ActiveTab, Accent);
    if (ActiveTab == 0)
    {
        DrawSectionCardHeader(Frame, "Sidereal orientation", "01 / DOME ROTATION", Accent);
        DrawObservatorySlider(Frame, "##StarRot", "Sidereal rotation", Stars.RotationDegrees, 0.0, 360.0, 1.0, "deg", 0, Accent);
        DrawObservatorySwitchRow(Frame, "##StarDrift", "Continuous sidereal drift", Stars.SiderealDriftActive, Accent);
    }
    else if (ActiveTab == 1)
    {
        DrawSectionCardHeader(Frame, "Photometric cutoff", "02 / APERTURE GAIN", Accent);
        DrawObservatorySlider(Frame, "##StarMag", "Limiting visual magnitude", Stars.LimitingMagnitude, 1.0, 8.0, 0.1, "mag", 1, Accent);
        DrawObservatorySlider(Frame, "##StarBri", "Catalogue flux multiplier", Stars.BrightnessMultiplier, 0.0, 4.0, 0.05, "\xC3\x97", 2, Accent);
    }
    else if (ActiveTab == 2)
    {
        DrawSectionCardHeader(Frame, "Atmospheric scintillation (4 s trace)", "03 / TURBULENCE", Accent);
        const ImVec2 TraceMin = ImGui::GetCursorScreenPos();
        const float  TraceH   = 68.0f;
        const ImVec2 TraceMax(Frame.Left + Frame.Width, TraceMin.y + TraceH);
        KitPillSurface(Frame.Ink, TraceMin, TraceMax, Frame.CanvasBg, Frame.Theme.LineSoft, 8.0f);
        ImVec2 PrevPt{};
        for (int Step = 0; Step <= 64; ++Step)
        {
            const double T   = (4.0 * Step) / 64.0;
            const double Env = Stars.EvaluateScintillationTrace(T);
            const ImVec2 Pt(TraceMin.x + 8.0f + (Frame.Width - 16.0f) * (static_cast<float>(Step) / 64.0f),
                            TraceMax.y - 10.0f - static_cast<float>((Env - 0.15) / 1.70) * (TraceH - 20.0f));
            if (Step > 0)
            {
                Frame.Ink->AddLine(PrevPt, Pt, Accent, 1.5f);
            }
            PrevPt = Pt;
        }
        ImGui::SetCursorScreenPos(ImVec2(Frame.Left, TraceMax.y + 8.0f));
        ImGui::Dummy(ImVec2(Frame.Width, 0.0f));
        DrawObservatorySlider(Frame, "##StarScin", "Scintillation depth", Stars.ScintillationStrength, 0.0, 1.0, 0.02, "", 2, Accent);
        DrawObservatorySlider(Frame, "##StarHz", "Scintillation frequency", Stars.ScintillationRateHz, 0.2, 8.0, 0.1, "Hz", 1, Accent);
    }
    else
    {
        DrawSectionCardHeader(Frame, "Galactic band radiance", "04 / MILKY WAY", Accent);
        DrawObservatorySlider(Frame, "##StarMw", "Galactic plane luminance", Stars.MilkyWayGain, 0.0, 2.0, 0.05, "\xC3\x97", 2, Accent);
    }
    DrawObservatoryFooter(Frame, "CELESTIAL / STAR CATALOGUE");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MOON OBSERVATORY
//------------------------------------------------------------------------------------------------------------------------

void ConstructMoonObservatory(const ObservatoryFrame& Frame, MoonRecord& Moon, uint32_t& ActiveTab)
{
    const ImU32 Accent = KitInkHex(0xc4d5e8);
    DrawObservatoryHeader(Frame, "CELESTIAL / SELENOGRAPHIC DISC", "Moon",
                          "29.53-day synodic phase rail, selenographic terminator, and log angular diameter tape.",
                          "##MoonPower", &Moon.Enabled, Accent, "Lunar live", "Off");

    const LunarPhase Lunar  = Moon.QueryLunarPhase();
    const bool       Waxing = Moon.QueryWaxing();

    // Selenographic terminator disc + logarithmic angular diameter tape!
    const ImVec2 DiscMin = ImGui::GetCursorScreenPos();
    const float  DiscH   = 136.0f;
    const ImVec2 DiscMax(Frame.Left + Frame.Width, DiscMin.y + DiscH);
    KitPillSurface(Frame.Ink, DiscMin, DiscMax, KitInkHex(0x0b0f16), Frame.Theme.Line, 10.0f);

    const ImVec2 Centre(Frame.Left + 74.0f, DiscMin.y + DiscH * 0.5f);
    const float  Rad = 46.0f;
    // Scanline terminator shading across selenographic sphere
    for (int Dy = -44; Dy <= 44; Dy += 2)
    {
        const float HalfSpan = std::sqrt(std::max(0.0f, Rad * Rad - static_cast<float>(Dy * Dy)));
        const double TermCos = 1.0 - 2.0 * Lunar.Illumination;
        const float  TermX   = static_cast<float>(TermCos) * HalfSpan * (Waxing ? 1.0f : -1.0f);
        const float  LitL    = Waxing ? TermX : -HalfSpan;
        const float  LitR    = Waxing ? HalfSpan : TermX;
        Frame.Ink->AddLine(ImVec2(Centre.x - HalfSpan, Centre.y + static_cast<float>(Dy)),
                           ImVec2(Centre.x + HalfSpan, Centre.y + static_cast<float>(Dy)),
                           KitInkRgba(34, 42, 56, 220), 2.2f);
        if (LitR > LitL)
        {
            Frame.Ink->AddLine(ImVec2(Centre.x + LitL, Centre.y + static_cast<float>(Dy)),
                               ImVec2(Centre.x + LitR, Centre.y + static_cast<float>(Dy)),
                               SrgbSwatch(Moon.TintRed * 0.88, Moon.TintGreen * 0.88, Moon.TintBlue * 0.88), 2.2f);
        }
    }
    Frame.Ink->AddCircle(Centre, Rad, KitInkRgba(196, 213, 232, 130), 40, 1.3f);

    // Right side: body title + log 0.1..180 deg angular diameter tape with 0.52 deg Earth Moon tick!
    const float TapeL = Frame.Left + 140.0f;
    const float TapeR = Frame.Left + Frame.Width - 14.0f;
    const float TapeW = std::max(40.0f, TapeR - TapeL);
    KitDrawText(Frame.Ink, Frame.Faces.SemiBold, 14.0f, ImVec2(TapeL, DiscMin.y + 18.0f), Frame.Theme.Text,
                Moon.QueryBodyPresetName());
    char PhaseSub[64] = {};
    std::snprintf(PhaseSub, sizeof(PhaseSub), "Age %.1f d  \xC2\xB7  %.0f%% %s",
                  Lunar.AgeDays, Lunar.Illumination * 100.0, Waxing ? "Waxing" : "Waning");
    KitDrawText(Frame.Ink, Frame.Faces.Regular, 11.0f, ImVec2(TapeL, DiscMin.y + 38.0f), Frame.Theme.Muted, PhaseSub);

    const float TapeY = DiscMin.y + 84.0f;
    Frame.Ink->AddRectFilled(ImVec2(TapeL, TapeY - 3.0f), ImVec2(TapeR, TapeY + 3.0f), Frame.Theme.Surface2, 3.0f);
    const auto LogNorm = [](double Deg) {
        return static_cast<float>(std::clamp((std::log10(std::max(0.1, Deg)) - std::log10(0.1)) /
                                                 (std::log10(180.0) - std::log10(0.1)),
                                             0.0, 1.0));
    };
    const float EarthTickX = TapeL + LogNorm(0.52) * TapeW;
    const float CurTickX   = TapeL + LogNorm(Moon.AngularDiameterDegrees) * TapeW;
    Frame.Ink->AddLine(ImVec2(EarthTickX, TapeY - 9.0f), ImVec2(EarthTickX, TapeY + 9.0f), KitInkHex(0xebb35e), 1.4f);
    Frame.Ink->AddCircleFilled(ImVec2(CurTickX, TapeY), 5.5f, Accent);
    KitDrawText(Frame.Ink, Frame.Faces.Regular, 9.2f, ImVec2(TapeL, TapeY + 12.0f), Frame.Theme.Faint,
                "LOG ANGULAR TAPE: 0.1\xC2\xB0 .. 0.52\xC2\xB0 (Luna) .. 180\xC2\xB0");

    ImGui::SetCursorScreenPos(ImVec2(Frame.Left, DiscMax.y + 8.0f));
    ImGui::Dummy(ImVec2(Frame.Width, 0.0f));

    DrawObservatoryTabs(Frame, "Moon", {"Selenograph", "Synodic", "Coordinates", "Presets"}, ActiveTab, Accent);
    if (ActiveTab == 0)
    {
        DrawSectionCardHeader(Frame, "Surface albedo & earthshine", "01 / OPTICS", Accent);
        DrawObservatorySlider(Frame, "##MoonStr", "Moonlight illuminance", Moon.MoonlightStrength, 0.0, 3.0, 0.02, "\xC3\x97", 2, Accent);
        DrawObservatorySlider(Frame, "##MoonEarth", "Ashen earthshine", Moon.EarthshineStrength, 0.0, 0.5, 0.01, "", 2, Accent);
        DrawObservatorySlider(Frame, "##MoonMare", "Mare / highland contrast", Moon.MareContrast, 0.0, 1.0, 0.02, "", 2, Accent);
    }
    else if (ActiveTab == 1)
    {
        DrawSectionCardHeader(Frame, "Synodic cycle (29.53 days)", "02 / PHASE RAIL", Accent);
        DrawObservatorySlider(Frame, "##MoonAge", "Synodic age", Moon.SynodicAgeDays, 0.0, SynodicMonthDays, 0.1, "d", 1, Accent);
        DrawObservatorySwitchRow(Frame, "##MoonLock", "Lock phase to calendar ephemeris", Moon.LockToEphemeris, Accent);
    }
    else if (ActiveTab == 2)
    {
        DrawSectionCardHeader(Frame, "Sky dome coordinates & scale", "03 / EPHEMERIS", Accent);
        DrawObservatorySlider(Frame, "##MoonEl", "Sky elevation", Moon.ElevationDegrees, -90.0, 90.0, 0.5, "deg", 1, Accent);
        DrawObservatorySlider(Frame, "##MoonAz", "Azimuth bearing", Moon.AzimuthDegrees, -180.0, 180.0, 1.0, "deg", 0, Accent);
        DrawObservatorySlider(Frame, "##MoonDia", "Angular diameter", Moon.AngularDiameterDegrees, 0.1, 60.0, 0.05, "deg", 2, Accent);
    }
    else
    {
        DrawSectionCardHeader(Frame, "Planetary satellite presets", "04 / BODY CATALOGUE", Accent);
        if (DrawObservatoryOptionStrip(Frame, "MoonPreset", "Celestial body",
                                       {"Earth Moon", "Europa", "Titan", "Enceladus", "Phobos", "Gas Giant"},
                                       Moon.PresetBodyIndex, Accent))
        {
            Moon.ApplyBodyPreset(Moon.PresetBodyIndex);
        }
    }
    DrawObservatoryFooter(Frame, "CELESTIAL / SELENOGRAPHIC MODEL");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  WEATHER OBSERVATORY
//------------------------------------------------------------------------------------------------------------------------

void ConstructWeatherObservatory(const ObservatoryFrame& Frame, WeatherRecord& Weather, uint32_t& ActiveTab)
{
    const ImU32 Accent = KitInkHex(0x789efa);
    DrawObservatoryHeader(Frame, "WEATHER / STORM & SURFACE DEPOSIT", "Storm & Deposit",
                          "Procedural forked lightning, 343 m/s thunder acoustics, and 0..4 mm apron retention.",
                          "##WeatherPower", &Weather.Enabled, Accent, "Storm live", "Dry");

    // Hero canvas: either Forked Lightning Bolt + Flash Envelope (tabs 0..1) or 16x10 Surface Retention Grid (tabs 2..3)!
    const ImVec2 HeroMin = ImGui::GetCursorScreenPos();
    const float  HeroH   = 138.0f;
    const ImVec2 HeroMax(Frame.Left + Frame.Width, HeroMin.y + HeroH);
    KitPillSurface(Frame.Ink, HeroMin, HeroMax, KitInkHex(0x0b1019), Frame.Theme.Line, 10.0f);

    if (ActiveTab <= 1)
    {
        // Project 3D ActiveBolt X/Z into left half of storm canvas + stroke/restrike envelope on right!
        const auto ProjectBoltPt = [&](SpatialVector P) -> ImVec2 {
            const float U = std::clamp(static_cast<float>((P.X + 4.5) / 9.0), 0.04f, 0.96f);
            const float V = 1.0f - std::clamp(static_cast<float>(P.Z / 15.0), 0.04f, 0.96f);
            return ImVec2(HeroMin.x + 12.0f + U * (Frame.Width * 0.48f), HeroMin.y + 10.0f + V * (HeroH - 24.0f));
        };
        for (size_t I = 1; I < Weather.ActiveBolt.MainChannel.size(); ++I)
        {
            const ImVec2 A = ProjectBoltPt(Weather.ActiveBolt.MainChannel[I - 1]);
            const ImVec2 B = ProjectBoltPt(Weather.ActiveBolt.MainChannel[I]);
            Frame.Ink->AddLine(A, B, KitInkRgba(140, 185, 255, 95), 4.2f);
            Frame.Ink->AddLine(A, B, KitInkHex(0xeef5ff), 1.6f);
        }
        for (const auto& Branch : Weather.ActiveBolt.Branches)
        {
            for (size_t I = 1; I < Branch.size(); ++I)
            {
                Frame.Ink->AddLine(ProjectBoltPt(Branch[I - 1]), ProjectBoltPt(Branch[I]),
                                   KitInkRgba(165, 200, 255, 175), 1.0f);
            }
        }
        // Right half: multi-stroke flash envelope + 343 m/s acoustic arrival marker!
        const float EnvL = HeroMin.x + Frame.Width * 0.54f;
        const float EnvW = Frame.Width * 0.42f;
        const float EnvB = HeroMax.y - 24.0f;
        KitDrawText(Frame.Ink, Frame.Faces.Medium, 9.5f, ImVec2(EnvL, HeroMin.y + 10.0f), Accent,
                    "RETURN STROKE + THUNDER DELAY");
        ImVec2 Prev{};
        for (int Step = 0; Step <= 48; ++Step)
        {
            const double T   = (0.65 * Step) / 48.0;
            const double Val = LightningIntensity(T, Weather.StrikeCounter);
            const ImVec2 Pt(EnvL + EnvW * (static_cast<float>(Step) / 48.0f),
                            EnvB - static_cast<float>(Val) * 76.0f);
            if (Step > 0)
            {
                Frame.Ink->AddLine(Prev, Pt, KitInkHex(0x9dc2ff), 1.5f);
            }
            Prev = Pt;
        }
        char ThunderCaption[64] = {};
        std::snprintf(ThunderCaption, sizeof(ThunderCaption), "Thunder +%.2f s (%.0f m @ 343 m/s)",
                      Weather.QueryThunderArrivalSeconds(), Weather.StrikeDistanceMetres);
        KitDrawText(Frame.Ink, Frame.Faces.Regular, 9.4f, ImVec2(EnvL, HeroMax.y - 16.0f), KitInkHex(0xebb35e),
                    ThunderCaption);
    }
    else
    {
        // 16 x 10 Proving Grounds Surface Deposit Retention Grid (shows dry shadow under ControlVehicle!)
        const float GridL = HeroMin.x + 14.0f;
        const float GridT = HeroMin.y + 24.0f;
        const float CellW = (Frame.Width - 28.0f) / 16.0f;
        const float CellH = (HeroH - 36.0f) / 10.0f;
        KitDrawText(Frame.Ink, Frame.Faces.Medium, 9.5f, ImVec2(GridL, HeroMin.y + 8.0f), Accent,
                    "CHUNKED APRON RETENTION GRID (0..4 mm  \xC2\xB7  VEHICLE CHASSIS SHADOW)");
        for (int32_t GY = 0; GY < 10; ++GY)
        {
            for (int32_t GX = 0; GX < 16; ++GX)
            {
                const double Mm   = Weather.SampleSurfaceRetentionMm(GX, GY);
                const float  Frac = std::clamp(static_cast<float>(Mm / 4.0), 0.0f, 1.0f);
                const ImU32  Fill = Weather.PrecipitationModeIndex == 3
                                        ? KitInkRgba(static_cast<int>(30 + 195 * Frac),
                                                     static_cast<int>(38 + 195 * Frac),
                                                     static_cast<int>(52 + 195 * Frac), 235)
                                        : KitInkRgba(static_cast<int>(22 + 58 * Frac),
                                                     static_cast<int>(34 + 128 * Frac),
                                                     static_cast<int>(52 + 188 * Frac), 235);
                const ImVec2 CMin(GridL + GX * CellW + 1.0f, GridT + GY * CellH + 1.0f);
                const ImVec2 CMax(GridL + (GX + 1) * CellW - 1.0f, GridT + (GY + 1) * CellH - 1.0f);
                Frame.Ink->AddRectFilled(CMin, CMax, Fill, 2.0f);
            }
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(Frame.Left, HeroMax.y + 8.0f));
    ImGui::Dummy(ImVec2(Frame.Width, 0.0f));

    if (DrawObservatoryActionButton(Frame, "##StrikeAheadBtn", "Strike ahead (discharge return stroke + thunder)", Accent))
    {
        Weather.TriggerStrikeAhead();
    }

    DrawObservatoryTabs(Frame, "Weather", {"Lightning", "Thunder", "Precipitation", "Deposit grid"}, ActiveTab, Accent);
    if (ActiveTab == 0)
    {
        DrawSectionCardHeader(Frame, "Midpoint-displacement leader", "01 / BOLT GEOMETRY", Accent);
        DrawObservatorySlider(Frame, "##BoltForks", "Secondary fork branches", Weather.BoltForkCount, 0.0, 6.0, 1.0, "", 0, Accent);
        DrawObservatorySlider(Frame, "##BoltJag", "Channel jaggedness", Weather.BoltJaggedness, 0.05, 0.60, 0.02, "", 2, Accent);
        DrawObservatorySlider(Frame, "##BoltExp", "Peak flash illuminance", Weather.FlashExposureMultiplier, 0.0, 4.0, 0.1, "\xC3\x97", 1, Accent);
    }
    else if (ActiveTab == 1)
    {
        DrawSectionCardHeader(Frame, "Acoustic propagation (343 m/s)", "02 / THUNDER", Accent);
        DrawObservatorySlider(Frame, "##StrikeDist", "Strike range ahead", Weather.StrikeDistanceMetres, 50.0, 5000.0, 25.0, "m", 0, Accent);
        DrawObservatorySlider(Frame, "##StrikeClock", "Elapsed stroke clock", Weather.TimeSinceStrikeSeconds, 0.0, 1.0, 0.01, "s", 2, Accent);
        DrawObservatorySwitchRow(Frame, "##AutoStorm", "Periodic storm cell discharges", Weather.AutoStormCells, Accent);
    }
    else if (ActiveTab == 2)
    {
        DrawSectionCardHeader(Frame, "Advected particle field", "03 / PRECIPITATION", Accent);
        DrawObservatoryOptionStrip(Frame, "PrecipMode", "Phase state", {"Clear", "Rain", "Sleet", "Snow"},
                                   Weather.PrecipitationModeIndex, Accent);
        DrawObservatorySlider(Frame, "##PrecipRate", "Precipitation rate", Weather.PrecipitationRate, 0.0, 1.0, 0.02, "", 2, Accent);
        DrawObservatorySlider(Frame, "##PrecipSize", "Drop / crystal diameter", Weather.ParticleSizeMillimetres, 0.5, 8.0, 0.1, "mm", 1, Accent);
    }
    else
    {
        DrawSectionCardHeader(Frame, "Chunked surface retention", "04 / APRON DEPOSIT", Accent);
        DrawObservatorySlider(Frame, "##SurfDep", "Accumulated surface depth", Weather.SurfaceDepositMm, 0.0, 4.0, 0.1, "mm", 1, Accent);
        DrawObservatorySlider(Frame, "##SurfDrain", "Runoff / melt rate", Weather.DrainOrMeltRate, 0.0, 1.0, 0.02, "", 2, Accent);
    }
    DrawObservatoryFooter(Frame, "WEATHER / LIGHTNING & RETENTION");
}

} // namespace

#endif // Editor/Development profiles

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERFACE RECORDING
//------------------------------------------------------------------------------------------------------------------------

void SceneInspectorPanel::ConstructInterface(SceneDirectoryPanel& DirectoryPanel)
{
    if (ImGui::GetCurrentContext() == nullptr)
    {
        return;
    }
    const KitFaces Faces          = ResolveKitFaces();
    const KitTheme Theme          = ResolveKitTheme();
    const ImVec2   WindowPosition = ImGui::GetWindowPos();
    const ImVec2   WindowSize     = ImGui::GetWindowSize();
    const float    PL             = WindowPosition.x;
    const float    PT             = std::max(WindowPosition.y, ImGui::GetCursorScreenPos().y - ImGui::GetStyle().WindowPadding.y);
    const float    PR             = PL + WindowSize.x;
    const float    PB             = WindowPosition.y + WindowSize.y;
    ImDrawList*    OuterInk       = ImGui::GetWindowDrawList();

    OuterInk->AddRectFilled(ImVec2(PL, PT), ImVec2(PR, PB), Theme.PanelBg);
    OuterInk->AddRect(ImVec2(PL, PT), ImVec2(PR, PB), Theme.PanelEdge);

#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    SceneDirectory&              Directory = SceneDirectory::Instance();
    const SceneInspectorSummary& Summary   = DirectoryPanel.QueryInspector();

    // Top crumb bar (40 px, ported from EditorShell.js #inspector-top).
    const float CrumbH = 38.0f;
    OuterInk->AddRectFilled(ImVec2(PL + 1.0f, PT + 1.0f), ImVec2(PR - 1.0f, PT + CrumbH), Theme.Surface);
    OuterInk->AddLine(ImVec2(PL, PT + CrumbH), ImVec2(PR, PT + CrumbH), Theme.LineSoft);
    std::string CrumbText = "Inspector";
    if (Summary.HasSelection)
    {
        CrumbText += "  /  ";
        CrumbText += !Summary.ParentName.empty() ? Summary.ParentName
                                                 : RecordClassificationName(Summary.Classification);
    }
    KitDrawText(OuterInk, Faces.Medium, 11.5f, ImVec2(PL + 16.0f, PT + 12.5f), Theme.Muted, CrumbText.c_str());

    const char* LiveCaption = "Changes apply live";
    const float LiveW       = KitTextWidth(Faces.Regular, 11.0f, LiveCaption);
    OuterInk->AddCircleFilled(ImVec2(PR - 16.0f - LiveW - 10.0f, PT + 19.0f), 3.0f, Theme.Mint);
    KitDrawText(OuterInk, Faces.Regular, 11.0f, ImVec2(PR - 16.0f - LiveW, PT + 13.0f), Theme.Faint, LiveCaption);

    const float BodyTop = PT + CrumbH + 6.0f;
    const float BodyH   = std::max(40.0f, PB - BodyTop - 8.0f);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::SetCursorScreenPos(ImVec2(PL + 4.0f, BodyTop));
    if (ImGui::BeginChild("##InspectorScrollRegion", ImVec2(std::max(80.0f, WindowSize.x - 8.0f), BodyH), false))
    {
        const ImVec2       ChildPos = ImGui::GetWindowPos();
        const float        InnerL   = ChildPos.x + 12.0f;
        const float        InnerW   = std::max(100.0f, ImGui::GetContentRegionAvail().x - 24.0f);
        ObservatoryFrame Frame  = MakeObservatoryFrame(InnerL, InnerW);
        ImGui::SetCursorScreenPos(ImVec2(InnerL, ChildPos.y + 8.0f));

        if (!Summary.HasSelection)
        {
            DrawObservatoryNote(Frame, "Select a folder or scene record in the Outliner to inspect its live settings.");
        }
        else
        {
            const RecordEditorDescription* Description = Directory.QueryEditorDescription(Summary.Token);
            RecordEntry*                   LiveEntry   = RecordStore::Instance().Find(Summary.Token);
            if (Description == nullptr || LiveEntry == nullptr)
            {
                char Sub[128] = {};
                std::snprintf(Sub, sizeof(Sub), "%s  \xC2\xB7  %u enclosed  \xC2\xB7  token #%llu",
                              RecordClassificationName(Summary.Classification), Summary.ChildCount,
                              static_cast<unsigned long long>(Summary.Token.Value));
                DrawIdentityHeader(Faces, ImVec2(InnerL, ChildPos.y + 8.0f), InnerW, Theme.Mint, Summary.Name.c_str(),
                                   Sub);
            }
            else
            {
                switch (Description->InstrumentCategory)
                {
                    case InspectorInstrumentCategory::FolderObservatory:
                        ConstructFolderObservatory(Frame, *static_cast<FolderRecord*>(LiveEntry), Directory,
                                                   DirectoryPanel);
                        break;
                    case InspectorInstrumentCategory::SunObservatory:
                        ConstructSunObservatory(Frame, *static_cast<SunRecord*>(LiveEntry), SunTabIndex, Directory,
                                                DirectoryPanel);
                        break;
                    case InspectorInstrumentCategory::LensFlareObservatory:
                        ConstructLensFlareObservatory(Frame, *static_cast<LensFlareRecord*>(LiveEntry),
                                                      FlareTabIndex);
                        break;
                    case InspectorInstrumentCategory::AtmosphereObservatory:
                        ConstructAtmosphereObservatory(Frame, *static_cast<AtmosphereRecord*>(LiveEntry),
                                                       AtmosphereTabIndex, Directory);
                        break;
                    case InspectorInstrumentCategory::CloudObservatory:
                        ConstructCloudObservatory(Frame, *static_cast<CloudRecord*>(LiveEntry), CloudTabIndex,
                                                  Directory, DirectoryPanel);
                        break;
                    case InspectorInstrumentCategory::WindObservatory:
                        ConstructWindObservatory(Frame, *static_cast<WindRecord*>(LiveEntry), WindTabIndex);
                        break;
                    case InspectorInstrumentCategory::FogObservatory:
                        ConstructFogObservatory(Frame, *static_cast<FogRecord*>(LiveEntry), FogTabIndex);
                        break;
                    case InspectorInstrumentCategory::StarsObservatory:
                        ConstructStarsObservatory(Frame, *static_cast<StarsRecord*>(LiveEntry), StarsTabIndex);
                        break;
                    case InspectorInstrumentCategory::MoonObservatory:
                        ConstructMoonObservatory(Frame, *static_cast<MoonRecord*>(LiveEntry), MoonTabIndex);
                        break;
                    case InspectorInstrumentCategory::WeatherObservatory:
                        ConstructWeatherObservatory(Frame, *static_cast<WeatherRecord*>(LiveEntry), WeatherTabIndex);
                        break;
                    case InspectorInstrumentCategory::Standard:
                    case InspectorInstrumentCategory::Custom:
                        ConstructStandardOrCustomInspector(Frame, *Description, Summary, Directory, DirectoryPanel);
                        break;
                }
            }
        }
        Directory.SynchronizeEnabledFromDescriptions();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
#else
    (void)DirectoryPanel;
#endif
}

} // namespace Frontier

#else // FRONTIER_WITH_INTERFACE_SDK

namespace Frontier
{

void SceneInspectorPanel::ConstructInterface(SceneDirectoryPanel& DirectoryPanel)
{
    (void)DirectoryPanel;
}

} // namespace Frontier

#endif // FRONTIER_WITH_INTERFACE_SDK
