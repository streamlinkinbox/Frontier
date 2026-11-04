/*============================================================================================================================================
                                           VECTORCOVERAGETIERHYSTERESISVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Headless CPU-only regression test for the reported "Baking decal.../Baking pattern..." badge that never
//    clears, even on an idle, empty document. Drives the real (non-static) pooled-tier decision that backs
//    VectorAuthoringCoverageRendering::RefitSlot — VectorAuthoringCoverageRendering::SelectPooledTier — across
//    many simulated frames with a DisplayExtent (the live canvas's device-pixel size hint) that jitters by a
//    small amount frame to frame, exactly the kind of sub-pixel noise real layout/DPI rounding produces on an
//    otherwise perfectly idle canvas. If SelectPooledTier ever keeps requesting a different pooled tier on
//    every single frame instead of settling onto a fixed tier, RefitSlot returns true forever, the slot's
//    Pending flag never clears, and VectorAuthoringCoverageRendering::QuerySelectedReady() never reports
//    ready — exactly the permanently-stuck badge symptom, independent of document content (Segments=0 here,
//    matching the reported empty-document repro) and independent of any real GPU (this function touches no
//    Vulkan state at all).

#include "Engine/Render/VectorAuthoringCoverageRendering.h"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <cstdint>

namespace
{
void Require(bool Condition, const std::string &Message)
{
    if (!Condition) throw std::runtime_error(Message);
}
}

int main()
{
    using namespace Frontier;
    try
    {
        uint32_t Scenarios = 0, FramesSimulated = 0;
        // 📝 Sweep nominal device-pixel extents across the whole practical canvas range (a small
        // letterboxed board up through a heavily zoomed-in one) and small jitter amplitudes that model
        // sub-pixel layout/DPI noise on an otherwise untouched canvas — including amplitudes right at
        // and just past the documented hysteresis tolerance (max(4, ActiveExtent/32)).
        for (size_t Segments : {size_t(0), size_t(50), size_t(5000)})
        {
            for (uint32_t Nominal = 32; Nominal <= 3000; Nominal += 17)
            {
                for (uint32_t Jitter : {0u, 1u, 2u, 3u, 5u, 8u, 16u, 24u})
                {
                    ++Scenarios;
                    size_t ActiveTier = 5; // Unfitted, as a freshly constructed slot starts.
                    size_t StableRun = 0;
                    bool Settled = false;
                    for (int Frame = 0; Frame < 64; ++Frame)
                    {
                        ++FramesSimulated;
                        // Alternates +Jitter / -Jitter every frame — the worst-case oscillation a
                        // sub-pixel-rounding source could produce on an otherwise fixed layout.
                        const int32_t Signed = (Frame % 2 == 0) ? int32_t(Jitter) : -int32_t(Jitter);
                        const uint32_t DisplayExtent = uint32_t(std::max<int32_t>(1, int32_t(Nominal) + Signed));
                        const size_t Next = VectorAuthoringCoverageRendering::SelectPooledTier(ActiveTier, Segments, DisplayExtent);
                        if (Next == ActiveTier) ++StableRun; else StableRun = 0;
                        ActiveTier = Next;
                        // 📝 Once it has held the same tier for 4 consecutive frames under ongoing jitter,
                        // consider it settled — real frame cadence would have long since cleared the badge.
                        if (StableRun >= 4) { Settled = true; break; }
                    }
                    Require(Settled, "Pooled coverage tier never settled for Segments=" + std::to_string(Segments) +
                        " Nominal=" + std::to_string(Nominal) + " Jitter=" + std::to_string(Jitter) +
                        " — this is the stuck-forever 'Baking...' badge reproduced on real production logic.");
                }
            }
        }
        std::cout << "Checked " << Scenarios << " jitter scenarios (" << FramesSimulated << " simulated frames); "
            "VectorAuthoringCoverageRendering::SelectPooledTier always settled.\n";
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "VectorCoverageTierHysteresisValidation failed: " << Failure.what() << "\n";
        return 1;
    }
    std::cout << "VectorCoverageTierHysteresisValidation passed.\n";
    return 0;
}
