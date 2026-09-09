//============================================================================================================================================
//                                                  CONTROLCENTREPANEL.H
//============================================================================================================================================
// 🧩 Development editor Control Centre — the top notch, Android-style. At rest it is a 30-pixel strip the
//    dock host always leaves it; a downward drag (or a tap, or the viewport's gear) slides the settings sheet
//    down over the columns, and the sheet slides back up the same way. GI seats the render path (full ReSTIR
//    while on, the visibility raster while off) and exposure trims the view for lookdev. It records last
//    and stays undockable, so the shade always draws above the dock columns.

#pragma once

#include <cstdint>

#include <imgui.h>

namespace Frontier {

class ControlPanel;

class ControlCentrePanel final
{
public:
    static constexpr float kCollapsedH = 30.0f;    // the strip; the dock host offsets by this
    static constexpr float kExpandedH  = 160.0f;   // the sheet the drag opens

    void AssignControls(ControlPanel* Controls) noexcept;

    // Shares the open figure with the viewport's gear: either side toggles, both agree.
    void AssignOpen(bool* Open) noexcept;

    void Record() noexcept;

    [[nodiscard]] bool  QueryGiEnabled() const noexcept { return GiOn_; }
    [[nodiscard]] float QueryExposure() const noexcept { return Exposure_; }

    // The strip handle centre (always valid) and the GI switch centre (valid while open), so a harness
    //    can click the shade it sees.
    [[nodiscard]] float QueryNotchX() const noexcept { return NotchX_; }
    [[nodiscard]] float QueryNotchY() const noexcept { return NotchY_; }
    [[nodiscard]] float QueryGiSwitchX() const noexcept { return GiX_; }
    [[nodiscard]] float QueryGiSwitchY() const noexcept { return GiY_; }

private:
    ControlPanel* Controls_ = nullptr;
    bool*         Open_     = nullptr;

    bool  GiOn_     = true;    // the render path: full ReSTIR while on, the visibility raster while off
    float Exposure_ = 0.0f;    // [ev] lookdev trim, applied before the tonemap

    float Height_ = kCollapsedH;   // the animated sheet height, chasing Target_
    float Target_ = kCollapsedH;

    bool  Dragging_        = false;
    float DragStartY_      = 0.0f;
    float DragStartTarget_ = kCollapsedH;
    float DragMoved_       = 0.0f;

    float NotchX_ = -1.0f;
    float NotchY_ = -1.0f;
    float GiX_    = -1.0f;
    float GiY_    = -1.0f;
};

} // namespace Frontier
