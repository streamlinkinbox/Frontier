//============================================================================================================================================
//                                                    VIEWPORTPANEL.H
//============================================================================================================================================
// 🧩 Development editor viewport — the stage column. Header bar with the brand, the dock toggles, the view
//    cycler and the transport; the dark view with its axis orb; the command line; the stats footer. Every
//    control here is local state: the transport runs, the clock scrubs, the command line answers honestly.

#pragma once

#include <cstdint>

namespace Frontier {

class EditorKit;

class ViewportPanel final
{
public:
    void AssignKit(EditorKit* Kit) noexcept;

    void Record(uint32_t RecordCount) noexcept;

private:
    void RecordBar() noexcept;
    void RecordView() noexcept;
    void RecordCommand() noexcept;
    void RecordFooter(uint32_t RecordCount) noexcept;

    EditorKit* Kit_ = nullptr;

    bool Playing_ = false;
    bool Paused_  = false;
    bool SimOn_   = false;

    bool     MarkersOn_ = true;
    uint32_t ViewPick_  = 0u;
    bool     DockLeft_  = true;
    bool     DockRight_ = true;

    char  CommandText_[128] = {};
    char  CommandEcho_[128] = {};
    bool  FocusCommand_     = false;
    float ClockHours_       = 19.15f;
};

} // namespace Frontier
