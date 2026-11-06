/*==========================================================================================================================================
                                                       FLUIDPLUMEEDITOR.CPP
==========================================================================================================================================*/
// 🧩 The Smoke & fire editor window: a title bar with the live readout and Close, and the editor's workspace below — the Outliner, the viewport,
//    the Log Stream, the Inspector and the libraries (FluidPlumeEditorWorkspace), as the Particle editor's window lays them out.

#include "Engine/Interface/FluidPlumeEditor.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/FluidPlumeEditor/FluidPlumeEditorWorkspace.h"
#include "Engine/Interface/FluidPlumeEditor/FluidPlumePages.h"
#include "Engine/Interface/InspectorControls.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/SceneDirectory.h"

#include <algorithm>
#include <cstdio>
#include <memory>

namespace Frontier
{

namespace
{

std::unique_ptr<FluidPlumeEditorWorkspace>& ResolveWorkspace()
{
    static std::unique_ptr<FluidPlumeEditorWorkspace> Workspace;
    if (!Workspace) Workspace = std::make_unique<FluidPlumeEditorWorkspace>();
    return Workspace;
}

} // namespace

FluidPlumeEditorWorkspace* FluidPlumeEditor::AccessWorkspace()
{
    return ResolveWorkspace().get();
}

void FluidPlumeEditor::AssignReadout(const FluidPlumeReadout& Readout)
{
    ResolveWorkspace()->AssignReadout(Readout);
}

void FluidPlumeEditor::RevokeReadout() noexcept
{
    ResolveWorkspace()->RevokeReadout();
}

const FluidPlumeReadout* FluidPlumeEditor::QueryReadout() noexcept
{
    return ResolveWorkspace()->QueryReadout();
}

std::vector<std::string> FluidPlumeEditor::TakeRequests()
{
    return ResolveWorkspace()->TakeHostRequests();
}

void FluidPlumeEditor::ConstructInterface(RecordToken Token, FluidPlumeConfiguration& Parameters)
{
    FluidPlumeEditorWorkspace& Workspace = *ResolveWorkspace();
    InspectorRuntime&          Runtime   = InspectorRuntime::Instance();
    Workspace.Presenter                  = Runtime.QueryPlumeViewportPresenter();
    const std::string Name               = SceneDirectory::Instance().QueryName(Token);
    Workspace.AttachRecord(Parameters, Name);

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

    // 📝 The live readout: the record, its lattice and whether a solver stands behind the viewport.
    const FluidPlumeReadout* Readout = Workspace.QueryReadout();
    char                     Line[256];
    const std::string        Lattice = FluidPlumePages::FormatLatticeExtent(Readout != nullptr ? Readout->Grade : 2u);
    std::snprintf(Line, sizeof(Line), "%s \xC2\xB7 %s \xC2\xB7 %s \xC2\xB7 %s", Readout == nullptr ? "NO SOLVER" : "LIVE", Name.c_str(), Lattice.c_str(),
                  Parameters.Enabled ? "ENABLED" : "DISABLED");
    Ink->AddText(Faces.Monospace, 10.5f * Scale, ImVec2(Position.x + 300.0f * Scale, Position.y + 19.5f * Scale), Readout == nullptr ? Palette.TextSecondary : Palette.LiveGreen, Line);

    const float  CloseW = 108.0f * Scale;
    const ImVec2 CloseMin(Position.x + Size.x - CloseW - 14.0f * Scale, Position.y + 10.0f * Scale);
    ImGui::SetCursorScreenPos(CloseMin);
    const bool Closing = ImGui::InvisibleButton("##CloseFluidPlumeEditor", ImVec2(CloseW, 30.0f * Scale));
    const bool Hovered = ImGui::IsItemHovered();
    Ink->AddRectFilled(CloseMin, ImVec2(CloseMin.x + CloseW, CloseMin.y + 30.0f * Scale), Hovered ? Palette.SegmentedActiveBg : Palette.CountCardBg, 9.0f * Scale);
    Ink->AddRect(CloseMin, ImVec2(CloseMin.x + CloseW, CloseMin.y + 30.0f * Scale), Palette.InsetFieldBorder, 9.0f * Scale);
    Ink->AddText(Faces.Medium, 11.0f * Scale, ImVec2(CloseMin.x + 18.0f * Scale, CloseMin.y + 8.5f * Scale), Palette.TextPrimary, "Close Editor");

    const ImVec2 BodyMin(Position.x + 12.0f * Scale, Position.y + Header + 12.0f * Scale);
    const ImVec2 BodyMax(Position.x + Size.x - 12.0f * Scale, Position.y + Size.y - 12.0f * Scale);
    Workspace.Construct(BodyMin, BodyMax, Scale, ImGui::GetIO().DeltaTime);
    if (Closing)
    {
        // 📝 The test emitter is the editor's own: it stops with the window, so the record leaves as it came.
        Workspace.DetachRecord();
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
FluidPlumeEditorWorkspace* FluidPlumeEditor::AccessWorkspace() { return nullptr; }
} // namespace Frontier

#endif
