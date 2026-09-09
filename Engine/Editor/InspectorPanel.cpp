//============================================================================================================================================
//                                                     INSPECTORPANEL.CPP
//============================================================================================================================================
// 🧩 Development editor, right column — describes the picked record. Static sliders this step; nothing is bound yet.

#include "InspectorPanel.h"

#include <imgui.h>
#include <imgui_internal.h>   // the two button silencers live in the vendor's private flags

namespace Frontier {

//============================================================================================================================================
//                                                          RECORD
//============================================================================================================================================

void InspectorPanel::Record() noexcept
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

    if (!ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    // The sliders move and hold; they bind to the picked record once the scene feed lands.
    ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
    if (ImGui::CollapsingHeader("Transform"))
    {
        ImGui::DragFloat3("Position", Position, 0.01f, 0.0f, 0.0f, "%.2f m");
        ImGui::DragFloat3("Rotation", Rotation, 0.10f, 0.0f, 0.0f, "%.1f °");
        ImGui::DragFloat3("Scale", Scale, 0.005f, 0.001f, 1000.0f, "%.3f");
    }
    ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
    if (ImGui::CollapsingHeader("Surface"))
    {
        ImGui::SliderFloat("Metallic", &Metallic, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Roughness", &Roughness, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Exposure", &ExposureBias, -4.0f, 4.0f, "%+.2f EV");
    }

    ImGui::End();
#endif
}

} // namespace Frontier
