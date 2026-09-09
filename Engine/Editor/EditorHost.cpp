//============================================================================================================================================
//                                                      EDITORHOST.CPP
//============================================================================================================================================
// 🧩 Development editor host — seats the trapezoid tab sheet, builds the dock columns, and records the three panels.

#include "EditorHost.h"

#include <imgui.h>
#include <imgui_internal.h>   // DockBuilder*: the first-seat columns are built, not dragged

namespace Frontier {

//============================================================================================================================================
//                                                       APPLY THEME
//============================================================================================================================================

void EditorHost::ApplyTheme() noexcept
{
#ifdef FRONTIER_DEVELOPMENT
    ImGuiStyle& Applied = ImGui::GetStyle();

    // Trapezoid sheet (Patches A/B/C; the figures are References/DockWorkspace.html's). The four geometry
    //    figures repeat the SwapchainExchange seating on purpose: the headless proof never runs the swapchain,
    //    and this call alone must draw the identical strip there.
    Applied.TabSlant            = 14.0f;   // [px] inset of a tab's two upper corners
    Applied.TabOverlap           = 24.0f;   // [px] neighbour interlock, so slanted edges overlap
    Applied.TabHeight            = 24.0f;   // [px] strip height
    Applied.TabStripPadTop       = 4.0f;    // [px] strip showing above the tabs
    Applied.FramePadding.x       = 38.0f;   // [px] tab breathing room, so the slant reads
    Applied.TabMinWidthBase      = 170.0f;  // [px] tab width floor
    Applied.TabMinWidthShrink    = 170.0f;  // [px] tab width floor while shrinking
    Applied.TabRounding          = 0.0f;    // [px] the sheet's corners are cut, not rounded
    Applied.TabBorderSize        = 0.0f;    // [px] no tab outline
    Applied.TabBarBorderSize     = 0.0f;    // [px] no strip outline
    Applied.TabButtonRounding    = 1.0f;    // [-] tab buttons are full discs

    // Tints. Every seated-tab variant carries one tint, so each column's lone tab renders the same colour
    //    whether or not its column holds the keyboard.
    const ImVec4 Quiet  = ImVec4(0.10f, 0.10f, 0.11f, 1.0f);
    const ImVec4 Seated = ImVec4(0.16f, 0.17f, 0.19f, 1.0f);
    Applied.Colors[ImGuiCol_Tab]                   = Quiet;
    Applied.Colors[ImGuiCol_TabHovered]            = ImVec4(0.14f, 0.14f, 0.15f, 1.0f);
    Applied.Colors[ImGuiCol_TabActive]             = Seated;
    Applied.Colors[ImGuiCol_TabUnfocused]          = Quiet;
    Applied.Colors[ImGuiCol_TabUnfocusedActive]    = Seated;
    Applied.Colors[ImGuiCol_TabDimmed]             = ImVec4(0.07f, 0.07f, 0.08f, 1.0f);
    Applied.Colors[ImGuiCol_TabDimmedSelected]     = Seated;
    Applied.Colors[ImGuiCol_TabSelectedOverline]   = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    Applied.Colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
#else
    // Without the define the editor draws nothing: the dockspace stays, the panels stay away.
#endif
}

//============================================================================================================================================
//                                                     CONSTRUCT LAYOUT
//============================================================================================================================================

void EditorHost::ConstructLayout() noexcept
{
#ifdef FRONTIER_DEVELOPMENT
    const ImGuiID DockId = ImGui::GetID("FrontierDockSpace");

    // Once the columns exist this rests, so a dragged rearrangement survives; with no .ini (the headless
    //    proof) the same seating rebuilds every run.
    if (ImGui::DockBuilderGetNode(DockId) != nullptr)
        return;

    ImGuiViewport* Main = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(DockId);
    ImGui::DockBuilderAddNode(DockId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(DockId, Main->Size);

    ImGuiID Left = 0u, Centre = 0u, Right = 0u;
    ImGui::DockBuilderSplitNode(DockId, ImGuiDir_Left, 0.22f, &Left, &Centre);
    ImGui::DockBuilderSplitNode(Centre, ImGuiDir_Right, 0.27f, &Right, &Centre);

    ImGui::DockBuilderDockWindow("Outliner", Left);
    ImGui::DockBuilderDockWindow("Viewport", Centre);
    ImGui::DockBuilderDockWindow("Inspector", Right);
    ImGui::DockBuilderFinish(DockId);
#endif
}

//============================================================================================================================================
//                                                          RECORD
//============================================================================================================================================

void EditorHost::Record() noexcept
{
#ifdef FRONTIER_DEVELOPMENT
    ImGuiViewport* Main = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(Main->Pos);
    ImGui::SetNextWindowSize(Main->Size);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    constexpr ImGuiWindowFlags Bare = ImGuiWindowFlags_NoTitleBar
                                    | ImGuiWindowFlags_NoResize
                                    | ImGuiWindowFlags_NoMove
                                    | ImGuiWindowFlags_NoScrollbar
                                    | ImGuiWindowFlags_NoScrollWithMouse
                                    | ImGuiWindowFlags_NoSavedSettings
                                    | ImGuiWindowFlags_NoBringToFrontOnFocus
                                    | ImGuiWindowFlags_NoNavFocus
                                    | ImGuiWindowFlags_NoBackground;

    if (ImGui::Begin("FrontierDockHost", nullptr, Bare))
    {
        // 🔴 PassthruCentralNode so the render shows through where nothing is docked — without it the vendor
        //    fills the whole column with its own colour. The two silencers remove the caret and close mark
        //    the sheet has none of. Docking over the centre stays allowed: the central column HOLDS the
        //    Viewport tab, so covering it means tabbing with the view, not losing it.
        const ImGuiDockNodeFlags NodeFlags =
              static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_PassthruCentralNode)
            | static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_NoWindowMenuButton)
            | static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_NoCloseButton);

        ConstructLayout();
        ImGui::DockSpace(ImGui::GetID("FrontierDockSpace"), ImVec2(0.0f, 0.0f), NodeFlags);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);

    Outliner_.Record();
    Viewport_.Record();
    Inspector_.Record();
#endif
}

} // namespace Frontier
