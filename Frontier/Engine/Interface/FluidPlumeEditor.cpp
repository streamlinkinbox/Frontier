/*==========================================================================================================================================
                                                       FLUIDPLUMEEDITOR.CPP
==========================================================================================================================================*/
// 🧩 The Smoke & fire editor window: a title bar with the live readout and Close, and the six cards below in columns (four on a wide window, three
//    and two on narrower ones); each column is one ControlColumn page that scrolls with the wheel when its cards outgrow it.

#include "Engine/Interface/FluidPlumeEditor.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/ControlColumn.h"
#include "Engine/Interface/FluidPlumeEditor/FluidPlumePages.h"
#include "Engine/Interface/InspectorControls.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/SceneDirectory.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <optional>

namespace Frontier
{

namespace
{

// 📝 What the window keeps between refreshes: the host's readout and the verbs waiting for it.
struct EditorState
{
    std::optional<FluidPlumeReadout> Readout;
    std::vector<std::string>         Requests;
};

EditorState& ResolveState()
{
    static EditorState State;
    return State;
}

using CardBuilder = void (*)(ControlColumn&, FluidPlumeContext&);

constexpr float SmallestFit = 0.82f;   // [-] how far the four-column page shrinks to fit a short window

constexpr ImU32 ScrollThumbInk = IM_COL32(255, 255, 255, 56);   // [-] the thumb of a column that scrolls

struct ColumnPlan
{
    std::array<CardBuilder, 3> Cards{};   // [-] top to bottom; null ends the column
};

// 📝 The prototype's order (Domain, Source, Simulation, Appearance, Fire, Presets) flowed into as many columns as the window is wide enough for,
//    so that every card is in view on the capture's 1920 px window.
std::array<ColumnPlan, 4> PlanColumns(int Count)
{
    using namespace FluidPlumePages;
    std::array<ColumnPlan, 4> Plan{};
    if (Count >= 4)
    {
        Plan[0].Cards = { ConstructDomainCard, ConstructSourceCard, nullptr };
        Plan[1].Cards = { ConstructSimulationCard, nullptr, nullptr };
        Plan[2].Cards = { ConstructAppearanceCard, nullptr, nullptr };
        Plan[3].Cards = { ConstructFireCard, ConstructPresetsCard, nullptr };
    }
    else if (Count == 3)
    {
        Plan[0].Cards = { ConstructDomainCard, ConstructSourceCard, ConstructPresetsCard };
        Plan[1].Cards = { ConstructSimulationCard, nullptr, nullptr };
        Plan[2].Cards = { ConstructAppearanceCard, ConstructFireCard, nullptr };
    }
    else
    {
        Plan[0].Cards = { ConstructDomainCard, ConstructSourceCard, ConstructAppearanceCard };
        Plan[1].Cards = { ConstructSimulationCard, ConstructFireCard, ConstructPresetsCard };
    }
    return Plan;
}

} // namespace

void FluidPlumeEditor::AssignReadout(const FluidPlumeReadout& Readout)
{
    ResolveState().Readout = Readout;
}

void FluidPlumeEditor::RevokeReadout() noexcept
{
    ResolveState().Readout.reset();
}

const FluidPlumeReadout* FluidPlumeEditor::QueryReadout() noexcept
{
    const EditorState& State = ResolveState();
    return State.Readout ? &*State.Readout : nullptr;
}

std::vector<std::string> FluidPlumeEditor::TakeRequests()
{
    std::vector<std::string> Taken;
    Taken.swap(ResolveState().Requests);
    return Taken;
}

void FluidPlumeEditor::ConstructInterface(RecordToken Token, FluidPlumeConfiguration& Parameters)
{
    EditorState&           State   = ResolveState();
    InspectorRuntime&      Runtime = InspectorRuntime::Instance();
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();
    const float            Scale   = Runtime.ResolveInterfaceScale();
    const ImGuiViewport*   Main    = ImGui::GetMainViewport();
    const ImVec2           Margin(28.0f * Scale, 24.0f * Scale);
    const ImVec2           Position(Main->WorkPos.x + Margin.x, Main->WorkPos.y + Margin.y);
    const ImVec2           Size(std::max(1100.0f, Main->WorkSize.x - Margin.x * 2.0f), std::max(640.0f, Main->WorkSize.y - Margin.y * 2.0f));

    // 📝 A modal popup: it stays above the workspace, dims it and keeps the pointer from the panels behind it, as long as the record's editor is the
    //    active one.
    constexpr const char* WindowName = "##FluidPlumeEditorWindow";
    if (!ImGui::IsPopupOpen(WindowName)) ImGui::OpenPopup(WindowName);
    ImGui::SetNextWindowPos(Position, ImGuiCond_Always);
    ImGui::SetNextWindowSize(Size, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertU32ToFloat4(Palette.ViewportBg));
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.02f, 0.02f, 0.03f, 0.55f));
    const bool Shown = ImGui::BeginPopupModal(WindowName, nullptr,
                                              ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                                  ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    if (!Shown) return;

    ImDrawList* Ink    = ImGui::GetWindowDrawList();
    const float Header = 50.0f * Scale;
    Ink->AddRect(Position, ImVec2(Position.x + Size.x, Position.y + Size.y), Palette.EdgeBorder, 16.0f * Scale, 0, 1.5f);
    Ink->AddRectFilled(Position, ImVec2(Position.x + Size.x, Position.y + Header), Palette.InspectorTopBg, 16.0f * Scale, ImDrawFlags_RoundCornersTop);
    Ink->AddLine(ImVec2(Position.x, Position.y + Header), ImVec2(Position.x + Size.x, Position.y + Header), Palette.EdgeBorder, 1.0f);
    ConstructEditorGlyph(Ink, ImVec2(Position.x + 24.0f * Scale, Position.y + 25.0f * Scale), 8.0f * Scale, Palette.ThemeAccent, "Flame", 1.5f);
    Ink->AddText(Faces.Medium, 14.0f * Scale, ImVec2(Position.x + 42.0f * Scale, Position.y + 16.5f * Scale), Palette.TextPrimary, "Frontier \xC2\xB7 Smoke & fire editor");

    // 📝 The live readout: the record, its lattice and what the host's solver is doing.
    const FluidPlumeReadout* Readout = State.Readout ? &*State.Readout : nullptr;
    FluidPlumeContext        Context{ Parameters, Readout, State.Requests };
    char                     Line[256];
    const std::string        Lattice = FluidPlumePages::FormatLatticeExtent(FluidPlumePages::ResolveGrade(Context));
    std::snprintf(Line, sizeof(Line), "%s \xC2\xB7 %s \xC2\xB7 %s \xC2\xB7 %s", Readout == nullptr ? "NO SOLVER" : "LIVE", SceneDirectory::Instance().QueryName(Token).c_str(),
                  Lattice.c_str(), Parameters.Enabled ? "ENABLED" : "DISABLED");
    Ink->AddText(Faces.Monospace, 10.5f * Scale, ImVec2(Position.x + 300.0f * Scale, Position.y + 19.5f * Scale), Readout == nullptr ? Palette.TextSecondary : Palette.LiveGreen, Line);

    const float  CloseW = 108.0f * Scale;
    const ImVec2 CloseMin(Position.x + Size.x - CloseW - 14.0f * Scale, Position.y + 10.0f * Scale);
    ImGui::SetCursorScreenPos(CloseMin);
    const bool Closing = ImGui::InvisibleButton("##CloseFluidPlumeEditor", ImVec2(CloseW, 30.0f * Scale));
    const bool Hovered = ImGui::IsItemHovered();
    Ink->AddRectFilled(CloseMin, ImVec2(CloseMin.x + CloseW, CloseMin.y + 30.0f * Scale), Hovered ? Palette.SegmentedActiveBg : Palette.CountCardBg, 9.0f * Scale);
    Ink->AddRect(CloseMin, ImVec2(CloseMin.x + CloseW, CloseMin.y + 30.0f * Scale), Palette.InsetFieldBorder, 9.0f * Scale);
    Ink->AddText(Faces.Medium, 11.0f * Scale, ImVec2(CloseMin.x + 18.0f * Scale, CloseMin.y + 8.5f * Scale), Palette.TextPrimary, "Close Editor");

    // 📝 The columns: one ControlColumn page each, in a child that clips and scrolls.
    const ImVec2 BodyMin(Position.x + 12.0f * Scale, Position.y + Header + 12.0f * Scale);
    const ImVec2 BodyMax(Position.x + Size.x - 12.0f * Scale, Position.y + Size.y - 12.0f * Scale);
    const float  Gap     = 12.0f * Scale;
    const float  BodyW   = BodyMax.x - BodyMin.x;
    const int    Count   = BodyW >= 1500.0f * Scale ? 4 : BodyW >= 1000.0f * Scale ? 3 : 2;
    const float  ColumnW = (BodyW - Gap * static_cast<float>(Count - 1)) / static_cast<float>(Count);
    const auto   Plan    = PlanColumns(Count);
    // 📝 Four columns hold every card in view if the window is tall enough for the Simulation column; a little shorter and the page shrinks to fit (down
    //    to 0.82), shorter still and the columns scroll. Three and two columns always scroll.
    const float Fit = Count >= 4 ? std::clamp((BodyMax.y - BodyMin.y - 2.0f) / (FluidPlumePages::TallestColumnHeight * Scale), SmallestFit, 1.0f) : 1.0f;
    for (int Column = 0; Column < Count; ++Column)
    {
        const ImVec2 Minimum(BodyMin.x + (ColumnW + Gap) * static_cast<float>(Column), BodyMin.y);
        const ImVec2 Maximum(Minimum.x + ColumnW, BodyMax.y);
        Ink->AddRectFilled(Minimum, Maximum, Palette.ColumnBg, 14.0f * Scale);
        Ink->AddRect(Minimum, Maximum, Palette.EdgeBorder, 14.0f * Scale);
        ImGui::SetCursorScreenPos(ImVec2(Minimum.x + 1.0f, Minimum.y + 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        char Name[40];
        std::snprintf(Name, sizeof(Name), "##FluidPlumeColumn%d", Column);
        if (ImGui::BeginChild(Name, ImVec2(ColumnW - 2.0f, Maximum.y - Minimum.y - 2.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground))
        {
            const ControlTheme Theme = ControlTheme::ResolveNative();
            ControlColumn      Page(Theme, Faces, Scale * Fit);
            for (const CardBuilder Card : Plan[static_cast<size_t>(Column)].Cards)
            {
                if (Card != nullptr) Card(Page, Context);
            }
            Page.Finalize();
            // 📝 A column that scrolls shows where it is: a slim thumb at its right edge (the scrollbar itself is hidden).
            const float Scrollable = ImGui::GetScrollMaxY();
            if (Scrollable > 0.0f)
            {
                const ImVec2 Origin  = ImGui::GetWindowPos();
                const ImVec2 Extent  = ImGui::GetWindowSize();
                const float  Thumb   = std::max(24.0f * Scale, Extent.y * Extent.y / (Extent.y + Scrollable));
                const float  Top     = Origin.y + (Extent.y - Thumb) * (ImGui::GetScrollY() / Scrollable);
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(Origin.x + Extent.x - 7.0f * Scale, Top), ImVec2(Origin.x + Extent.x - 4.0f * Scale, Top + Thumb),
                                                          ScrollThumbInk, 2.0f * Scale);
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    if (Closing)
    {
        ImGui::CloseCurrentPopup();
        Runtime.CloseActiveEditor();
    }
    ImGui::EndPopup();
}

} // namespace Frontier

#elif FRONTIER_EDITOR

namespace Frontier
{
void FluidPlumeEditor::ConstructInterface(RecordToken, FluidPlumeConfiguration&) {}
void FluidPlumeEditor::AssignReadout(const FluidPlumeReadout&) {}
void FluidPlumeEditor::RevokeReadout() noexcept {}
const FluidPlumeReadout* FluidPlumeEditor::QueryReadout() noexcept { return nullptr; }
std::vector<std::string> FluidPlumeEditor::TakeRequests() { return {}; }
} // namespace Frontier

#endif
