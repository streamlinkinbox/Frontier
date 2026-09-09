//============================================================================================================================================
//                                                     VIEWPORTPANEL.CPP
//============================================================================================================================================
// 🧩 Development editor, central column — hosts the ray-traced image. Placeholder chrome until the scene feed lands.

#include "ViewportPanel.h"

#include <imgui.h>
#include <imgui_internal.h>   // the two button silencers live in the vendor's private flags

namespace Frontier {

//============================================================================================================================================
//                                                          RECORD
//============================================================================================================================================

void ViewportPanel::Record() noexcept
{
#ifdef FRONTIER_DEVELOPMENT
    // 🔴 NoTitleBar is deliberately NOT set: the vendor refuses a tab strip to any window carrying it, so
    //    hiding the caption that way would also deny a torn-off panel the trapezoid. Docked, the column's
    //    strip replaces the caption; floating, the panel keeps a strip of its own. The two silencers remove
    //    the caret and close mark the sheet has none of.
    ImGuiWindowClass Declared;
    Declared.DockingAlwaysTabBar      = true;
    Declared.DockNodeFlagsOverrideSet = static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_NoWindowMenuButton)
                                      | static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_NoCloseButton);
    ImGui::SetNextWindowClass(&Declared);

    if (!ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("The ray-traced image attaches here.");
    ImGui::Separator();
    ImGui::TextWrapped("This column is UI chrome until the scene feed lands: the transport row below records "
                       "its shape now and earns its behaviour then.");
    ImGui::Spacing();
    if (ImGui::Button("Play"))
    {
    }
    ImGui::SameLine();
    if (ImGui::Button("Pause"))
    {
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop"))
    {
    }
    ImGui::Separator();
    ImGui::TextUnformatted("1280 x 720   |   60 Hz   |   no scene bound");

    ImGui::End();
#endif
}

} // namespace Frontier
