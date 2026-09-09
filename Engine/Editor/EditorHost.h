//============================================================================================================================================
//                                                       EDITORHOST.H
//============================================================================================================================================
// 🧩 Development editor host — seats the trapezoid tab sheet, builds the dock columns, and records the three panels.

#pragma once

#include "OutlinerPanel.h"
#include "ViewportPanel.h"
#include "InspectorPanel.h"

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                       EDITOR HOST
//------------------------------------------------------------------------------------------------------------------------

// Owns the development editor's place in the tick: the tab sheet, the first-seat dock columns, and the three
//    docked panels. Every method below is inert unless FRONTIER_DEVELOPMENT is defined, so a build without the
//    define keeps the dockspace but draws no editor.
class EditorHost
{
public:
    EditorHost() noexcept = default;
    ~EditorHost() noexcept = default;

    EditorHost(const EditorHost&)            = delete;
    EditorHost& operator=(const EditorHost&) = delete;

    // Call once after the ImGui context exists — seats the sheet's tab figures and tints. Idempotent: the
    //    four geometry figures repeat the SwapchainExchange seating so the headless proof, which never runs
    //    the swapchain, draws the identical strip from this call alone.
    void ApplyTheme() noexcept;

    // Call every tick between ImGui::NewFrame() and ImGui::Render() — records the fullscreen dock host,
    //    the dockspace, and the three panels.
    void Record() noexcept;

private:
    // Splits the dockspace into the outliner / viewport / inspector columns on the first tick, then rests.
    //    Runs with the host window open: the builder addresses the host, and without it there is nothing to
    //    build against.
    void ConstructLayout() noexcept;

    OutlinerPanel  Outliner_;
    ViewportPanel  Viewport_;
    InspectorPanel Inspector_;
};

} // namespace Frontier
