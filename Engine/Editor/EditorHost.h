//============================================================================================================================================
//                                                       EDITORHOST.H
//============================================================================================================================================
// 🧩 Development editor host — seats the theme, builds the dock columns, and records the three panels over the
//    project's instance feed. The host owns the controls the panels draw with and the four faces they draw in.

#pragma once

#include "ControlPanel.h"
#include "OutlinerPanel.h"
#include "ViewportPanel.h"
#include "InspectorPanel.h"
#include "ControlCentrePanel.h"

#include <cstdint>

namespace Frontier {

class EditorHost
{
public:
    EditorHost() noexcept;
    ~EditorHost() noexcept = default;

    EditorHost(const EditorHost&)            = delete;
    EditorHost& operator=(const EditorHost&) = delete;

    // Call once after the ImGui context exists — seats the sheet's tab figures, the full token theme, and the
    //    four faces. Idempotent: the faces seat once (a second seating would duplicate the glyph sheet), the style
    //    re-seats freely. Runs last, over the scheduler's own seating, so the editor's tokens win everywhere.
    void ApplyTheme() noexcept;

    // Call every tick between ImGui::NewFrame() and ImGui::Render() — records the fullscreen dock host, the
    //    dockspace, and the three panels over the project's feed. The panels borrow the feed and edit it in
    //    place; the sheet must already describe the currently picked instance (see QueryPickedInstance).
    void Record(EditorInstance* Instances, uint32_t InstanceCount, EditorSheet* PickedSheet) noexcept;

    // Seats the viewport's scene view (see ViewportPanel::AssignView) and reads back the view rect.
    void AssignView(const unsigned char* Rgba, uint32_t Width, uint32_t Height) noexcept;
    [[nodiscard]] float QueryViewWidth() const noexcept;
    [[nodiscard]] float QueryViewHeight() const noexcept;

    // The Control Centre shade's figures: GI seats the render path, exposure trims the view.
    [[nodiscard]] bool  QueryGiEnabled() const noexcept;
    [[nodiscard]] float QueryExposure() const noexcept;
    [[nodiscard]] float QueryGiSwitchX() const noexcept;
    [[nodiscard]] float QueryGiSwitchY() const noexcept;
    [[nodiscard]] float QueryNotchX() const noexcept;
    [[nodiscard]] float QueryNotchY() const noexcept;

    // The primary pick — the instance the sheet must describe. kNoEditorInstance when nothing is picked.
    [[nodiscard]] uint32_t QueryPickedInstance() const noexcept;

    // Faces seated by ApplyTheme (four when both archives resolve, zero without the define).
    [[nodiscard]] int QueryFontCount() const noexcept;

    // The test seam; the proof drives the pick through it.
    void PickInstance(uint32_t Index) noexcept;

private:
    // Splits the dockspace into the outliner / viewport / inspector columns on the first tick, then rests.
    //    Runs with the host window open: the builder addresses the host, and without it there is nothing to
    //    build against.
    void ConstructLayout() noexcept;

    ControlPanel      Controls_;          // first: the panels borrow it
    OutlinerPanel  Outliner_;
    ViewportPanel  Viewport_;
    InspectorPanel Inspector_;
    ControlCentrePanel ControlCentre_;   // last: the shade draws above the dock columns

    bool ShadeOpen_ = false;             // shut at boot, Android-style; shared with the shade and the gear

    bool FontsSeated_ = false;
    int  FontCount_   = 0;
};

} // namespace Frontier
