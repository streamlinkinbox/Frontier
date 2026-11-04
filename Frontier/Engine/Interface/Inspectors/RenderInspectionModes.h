/*==========================================================================================================================================
                                                          RENDERINSPECTIONMODES.H
==========================================================================================================================================*/
// 🧩 The renderer's debug views by the names the Render settings page and the viewport bar list: the demo's inspection views 0–14
//    (RefreshToggles::Inspection) and, as view 15, the editor's analytic illumination; plus what view 6 reads.

#pragma once

#include <cstddef>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    RENDER INSPECTION MODES
//------------------------------------------------------------------------------------------------------------------------

struct RenderInspectionView
{
    const char* Group            = "";
    const char* Chip             = "";
    const char* Name             = "";
    const char* Note             = "";
    bool        NeedsVirtualized = false;   // [-] the demo's cluster views that read the virtualized geometry's targets
};

struct RenderInspectionModes
{
    static constexpr size_t ModeCount    = 15;   // [-] the demo's inspection views 0–14
    static constexpr size_t ViewCount    = 16;   // [-] + the analytic illumination
    static constexpr int    AnalyticView = 15;
    static const RenderInspectionView Views[ViewCount];
    static constexpr size_t DistanceCount = 3;   // [-] view 6 reads: DistanceInspection 1–3
    static const char* const DistanceNames[DistanceCount];
};

} // namespace Frontier
