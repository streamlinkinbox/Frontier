/*============================================================================================================================================
                                             VECTORCOVERAGECURVEFLATTENINGVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Pure-CPU proof that the adaptive cubic-flatten segment-count formula (Wang's formula) is sound and
// bounded: see Engine/Render/VectorCoverage.h for exactly why this cannot, by itself, prove the hand-copied
// Slang shader text in VectorCoverage.slang/VectorSurface.slang matches -- that still needs a real GPU run.

#include "Engine/Render/VectorCoverage.h"
#include <iostream>

using namespace Frontier;

namespace
{
uint32_t Assertions = 0;
void Confirm(bool Condition, const char *Caption)
{
    if (!Condition) throw std::runtime_error(Caption);
    ++Assertions;
}
}

int main()
{
    try
    {
        // --- A perfectly straight "curve" (all four control points collinear) needs the fewest chords ----
        {
            const uint32_t Count = EstimateCubicFlattenSegments({0, 0}, {10, 0}, {20, 0}, {30, 0}, .25f);
            Confirm(Count == 2, "A collinear control polygon should flatten to the minimum two chords.");
        }
        // --- A sharply bulging curve under a tight tolerance needs many chords, capped at 32 -------------
        {
            const uint32_t Count = EstimateCubicFlattenSegments({0, 0}, {0, 500}, {500, 500}, {500, 0}, .01f);
            Confirm(Count == 32, "An extreme bulge under a tight tolerance should saturate at the 32-chord ceiling.");
        }
        // --- Never below the floor of two chords, even for a degenerate zero-length curve -----------------
        {
            const uint32_t Count = EstimateCubicFlattenSegments({5, 5}, {5, 5}, {5, 5}, {5, 5}, 1e6f);
            Confirm(Count == 2, "A degenerate zero-extent curve should still floor at two chords.");
        }
        // --- Monotonic in control-point deviation: a bigger bulge never needs fewer chords ----------------
        {
            uint32_t Previous = 0;
            for (float Bulge : {1.f, 5.f, 20.f, 80.f, 300.f})
            {
                const uint32_t Count = EstimateCubicFlattenSegments({0, 0}, {0, Bulge}, {Bulge, Bulge}, {Bulge, 0}, .25f);
                Confirm(Count >= Previous, "Flatten segment count regressed as the curve's bulge grew.");
                Previous = Count;
            }
        }
        // --- Monotonic in tolerance: a coarser (larger) tolerance never needs more chords ------------------
        {
            uint32_t Previous = 1000;
            for (float Tolerance : {.01f, .1f, 1.f, 10.f, 100.f})
            {
                const uint32_t Count = EstimateCubicFlattenSegments({0, 0}, {0, 40}, {40, 40}, {40, 0}, Tolerance);
                Confirm(Count <= Previous, "Flatten segment count regressed as the tolerance grew coarser.");
                Previous = Count;
            }
        }
        // --- A non-finite or zero tolerance must never divide by zero or return out-of-range -------------
        {
            const uint32_t Zero = EstimateCubicFlattenSegments({0, 0}, {0, 40}, {40, 40}, {40, 0}, 0.f);
            Confirm(Zero >= 2 && Zero <= 32, "A zero tolerance must still clamp into the valid chord-count range.");
            const uint32_t Negative = EstimateCubicFlattenSegments({0, 0}, {0, 40}, {40, 40}, {40, 0}, -5.f);
            Confirm(Negative >= 2 && Negative <= 32, "A negative tolerance must still clamp into the valid chord-count range.");
        }
        // --- A small ornamental curve relative to a typical quarter-pixel tolerance collapses well below the
        // former fixed 32-chord flatten, which is the whole point of adaptive flattening -------------------
        {
            const float Antialias = 2.f; // a typical 256px bake of a 512-unit document
            const uint32_t Count = EstimateCubicFlattenSegments({0, 0}, {0, 3}, {3, 3}, {3, 0}, Antialias * .25f);
            Confirm(Count < 32, "A small ornamental curve did not collapse below the legacy fixed chord count.");
        }
        std::cout << "VectorCoverageCurveFlatteningValidation: " << Assertions << " checks passed\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "VectorCoverageCurveFlatteningValidation failed: " << Failure.what() << "\n";
        return 1;
    }
}
