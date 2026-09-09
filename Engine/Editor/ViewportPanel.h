//============================================================================================================================================
//                                                      VIEWPORTPANEL.H
//============================================================================================================================================
// 🧩 Development editor, central column — hosts the ray-traced image. Placeholder chrome until the scene feed lands.

#pragma once

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                      VIEWPORT PANEL
//------------------------------------------------------------------------------------------------------------------------

// The central dock column. This step records the column's chrome — heading, transport row, standing line — so
//    the three-tab strip and the layout prove out before the scene feed is bound underneath it.
class ViewportPanel
{
public:
    ViewportPanel() noexcept = default;
    ~ViewportPanel() noexcept = default;

    ViewportPanel(const ViewportPanel&)            = delete;
    ViewportPanel& operator=(const ViewportPanel&) = delete;

    // Records the panel into its dock column. Inert unless FRONTIER_DEVELOPMENT is defined.
    void Record() noexcept;
};

} // namespace Frontier
