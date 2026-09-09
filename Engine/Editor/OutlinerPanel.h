//============================================================================================================================================
//                                                      OUTLINERPANEL.H
//============================================================================================================================================
// 🧩 Development editor, left column — the record register: query line plus grouped rows. Static records this step.

#pragma once

#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                      OUTLINER PANEL
//------------------------------------------------------------------------------------------------------------------------

// The left dock column. A query line narrows the grouped rows below it; picking a row remembers its index so
//    the inspector has something to describe once the scene feed lands. Nothing here allocates: the register
//    is a fixed table compiled into the translation unit.
class OutlinerPanel
{
public:
    OutlinerPanel() noexcept = default;
    ~OutlinerPanel() noexcept = default;

    OutlinerPanel(const OutlinerPanel&)            = delete;
    OutlinerPanel& operator=(const OutlinerPanel&) = delete;

    // Records the panel into its dock column. Inert unless FRONTIER_DEVELOPMENT is defined.
    void Record() noexcept;

private:
    static constexpr uint32_t kNothingPicked = 0xFFFFFFFFu;

    char     QueryText[64] = {};           // [-]  the query line, as typed
    uint32_t Picked        = 5u;           // [-]  index of the picked row, or kNothingPicked
};

} // namespace Frontier
