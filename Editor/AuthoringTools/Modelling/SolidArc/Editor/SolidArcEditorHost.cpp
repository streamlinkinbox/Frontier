//=============================================================================================================================================
// SolidArcEditorHost.cpp
//=============================================================================================================================================

#include "SolidArcEditorHost.h"
#include "../../../../../Engine/Editor/SunInspectorPanel.h"
#include "../../../../../Engine/DisplayPresentation/IconPresentation.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>

namespace Frontier {

SolidArcEditorHost::SolidArcEditorHost() noexcept
{
    Outliner_.AssignControls(&Controls_);
    Viewport_.AssignControls(&Controls_);
    Inspector_.AssignControls(&Controls_);
    Outliner_.AssignTabOpen(&OutlinerTabOpen_);
    Viewport_.AssignTabOpen(&ViewportTabOpen_);
    Inspector_.AssignTabOpen(&InspectorTabOpen_);
    Outliner_.AssignWindowTitle("SolidArc Outliner");
    Viewport_.AssignWindowTitle("SolidArc Viewport");
    Inspector_.AssignWindowTitle("SolidArc Inspector");
    // SolidArc uses its CAD filter catalogue, not Project-Zero's game narrowing labels.
    // Colours match the web editor KINDS palette: curve/sketch #4fd8e0, body #ffb454,
    // surface #4da3ff, construction plane #b48cff and dimensions #e5d33a.
    const OutlinerFilterEntry SolidArcFilters[] =
    {
        { "Lines",        IM_COL32(79, 216, 224, 255), SolidArcOutlinerFilter::Lines },
        { "Profiles",     IM_COL32(79, 216, 224, 255), SolidArcOutlinerFilter::Profiles },
        { "Bodies",       IM_COL32(255, 180, 84, 255), SolidArcOutlinerFilter::Bodies },
        { "Surfaces",     IM_COL32(77, 163, 255, 255), SolidArcOutlinerFilter::Surfaces },
        { "Construction", IM_COL32(180, 140, 255, 255), SolidArcOutlinerFilter::Construction },
        { "Dimensions",   IM_COL32(229, 211, 58, 255), SolidArcOutlinerFilter::Dimensions },
    };
    Outliner_.AssignFilterCatalog(SolidArcFilters, static_cast<uint32_t>(sizeof(SolidArcFilters) / sizeof(SolidArcFilters[0])));
    Viewport_.AssignChrome(ViewportPanelChrome::SolidArcCad);
    Outliner_.AssignReadout(&Readout_);
    Viewport_.AssignReadout(&Readout_);
    Inspector_.AssignReadout(&Readout_);
}

void SolidArcEditorHost::ApplyTheme() noexcept
{
    PrepareSunInspectorFonts();
    IconPresentation::Attach();
    ImGuiStyle& Style = ImGui::GetStyle();
#ifdef FRONTIER_DEVELOPMENT
    Style.TabSlant                  = 14.0f;
    Style.TabOverlap                = 24.0f;
    Style.TabHeight                 = 24.0f;
    Style.TabStripPadTop            = 4.0f;
    Style.TabMinWidthBase           = 110.0f;
    Style.TabMinWidthShrink         = 110.0f;
    Style.DockingNodeHasCloseButton = false;
    Style.TabRounding               = 0.0f;
    Style.TabBorderSize             = 0.0f;
    Style.TabBarBorderSize          = 0.0f;
    Style.TabButtonRounding         = 1.0f;
#endif
    Style.WindowPadding    = ImVec2(14.0f, 12.0f);
    Style.WindowRounding   = 8.0f;
    Style.ChildRounding    = 12.0f;
    Style.FrameRounding    = 16.0f;
    Style.WindowBorderSize = 1.0f;
    Style.FrameBorderSize  = 1.0f;
    Style.ScrollbarSize    = 8.0f;

    ImVec4* Colours = Style.Colors;
    Colours[ImGuiCol_WindowBg]       = ImVec4(0.071f, 0.071f, 0.071f, 1.0f);
    Colours[ImGuiCol_ChildBg]        = ImVec4(0.000f, 0.000f, 0.000f, 0.0f);
    Colours[ImGuiCol_Border]         = ImVec4(1.000f, 1.000f, 1.000f, 0.05f);
    Colours[ImGuiCol_FrameBg]        = ImVec4(0.000f, 0.000f, 0.000f, 1.0f);
    Colours[ImGuiCol_TitleBg]        = ImVec4(0.039f, 0.039f, 0.039f, 1.0f);
    Colours[ImGuiCol_TitleBgActive]  = ImVec4(0.039f, 0.039f, 0.039f, 1.0f);
    Colours[ImGuiCol_TitleBgCollapsed] = ImVec4(0.039f, 0.039f, 0.039f, 1.0f);
    Colours[ImGuiCol_Button]         = ImVec4(0.133f, 0.133f, 0.133f, 1.0f);
    Colours[ImGuiCol_ButtonHovered]  = ImVec4(0.180f, 0.180f, 0.180f, 1.0f);
    Colours[ImGuiCol_Header]         = ImVec4(0.165f, 0.165f, 0.165f, 1.0f);
    Colours[ImGuiCol_Tab]            = ImVec4(0.149f, 0.149f, 0.173f, 1.0f);
    Colours[ImGuiCol_TabHovered]     = ImVec4(0.196f, 0.196f, 0.227f, 1.0f);
    Colours[ImGuiCol_TabActive]      = ImVec4(0.071f, 0.071f, 0.071f, 1.0f);
    Colours[ImGuiCol_DockingPreview] = ImVec4(1.000f, 1.000f, 1.000f, 0.12f);
}

void SolidArcEditorHost::ConstructLayout() noexcept
{
    const ImGuiID DockId = ImGui::GetID("SolidArcEditorDockSpace");
    if (LayoutSeated_)
        return;
    LayoutSeated_ = true;

    ImGuiViewport* Main = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(DockId);
    ImGui::DockBuilderAddNode(DockId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(DockId, Main->Size);

    ImGuiID Left = 0u, CentreAndRight = 0u, Centre = 0u, Right = 0u;
    const float LeftShare = Main->Size.x > 0.0f ? std::clamp(236.0f / Main->Size.x, 0.15f, 0.26f) : 0.18f;
    ImGui::DockBuilderSplitNode(DockId, ImGuiDir_Left, LeftShare, &Left, &CentreAndRight);
    const float RestWidth = std::max(1.0f, Main->Size.x * (1.0f - LeftShare));
    const float RightShare = std::clamp(236.0f / RestWidth, 0.16f, 0.28f);
    ImGui::DockBuilderSplitNode(CentreAndRight, ImGuiDir_Right, RightShare, &Right, &Centre);
    ImGui::DockBuilderDockWindow("SolidArc Outliner", Left);
    ImGui::DockBuilderDockWindow("SolidArc Viewport", Centre);
    ImGui::DockBuilderDockWindow("SolidArc Inspector", Right);
    LeftColumn_ = Left;
    CentreColumn_ = Centre;
    RightColumn_ = Right;
    ImGui::DockBuilderFinish(DockId);
}

void SolidArcEditorHost::Record(ConsoleHost& Host) noexcept
{
    Host.Render();
    ViewImage_ = Host.Raster().Readback();
    RowCount_ = BuildSolidArcOutliner(Host, Rows_.data(), Bindings_.data(), kMaxEditorInstances, &Readout_);
    if (!ViewImage_.Pixels.empty())
        Viewport_.AssignView(ViewImage_.Pixels.data(), ViewImage_.Width, ViewImage_.Height);
    else
        Viewport_.AssignView(nullptr, 0u, 0u);

    ImGuiViewport* Main = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(Main->Pos.x, Main->Pos.y));
    ImGui::SetNextWindowSize(ImVec2(Main->Size.x, Main->Size.y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    constexpr ImGuiWindowFlags HostFlags = ImGuiWindowFlags_NoTitleBar
                                         | ImGuiWindowFlags_NoResize
                                         | ImGuiWindowFlags_NoMove
                                         | ImGuiWindowFlags_NoScrollbar
                                         | ImGuiWindowFlags_NoScrollWithMouse
                                         | ImGuiWindowFlags_NoSavedSettings
                                         | ImGuiWindowFlags_NoBringToFrontOnFocus
                                         | ImGuiWindowFlags_NoNavFocus;
    if (ImGui::Begin("SolidArcDockHost", nullptr, HostFlags))
    {
        const ImGuiDockNodeFlags DockFlags =
            static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_NoWindowMenuButton);
        ConstructLayout();
        ImGui::DockSpace(ImGui::GetID("SolidArcEditorDockSpace"), ImVec2(0.0f, 0.0f), DockFlags);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);

    Outliner_.Record(Rows_.data(), RowCount_);
    Viewport_.Record(Rows_.data(), RowCount_);

    const uint32_t Picked = Outliner_.QueryPicked();
    EditorInstance* PickedRow = (Picked < RowCount_) ? &Rows_[Picked] : nullptr;
    if (PickedRow != nullptr)
        BuildSolidArcInspectorSheet(Host, Bindings_[Picked], &PickedSheet_);
    else
        BuildSolidArcInspectorSheet(Host, SolidArcOutlinerBinding{}, &PickedSheet_);
    Inspector_.Record(PickedRow, Picked, &PickedSheet_);

    ApplySolidArcOutlinerVisibility(Host, Rows_.data(), Bindings_.data(), RowCount_);
    if (Picked < RowCount_)
        ApplySolidArcInspectorSheet(Host, Bindings_[Picked], PickedSheet_);
}

uint32_t SolidArcEditorHost::QueryPickedFigureIdentity() const noexcept
{
    const uint32_t Picked = Outliner_.QueryPicked();
    if (Picked >= RowCount_ || Bindings_[Picked].RowRole != SolidArcOutlinerBinding::Role::Figure)
        return 0u;
    return Bindings_[Picked].FigureIdentity;
}

} // namespace Frontier
