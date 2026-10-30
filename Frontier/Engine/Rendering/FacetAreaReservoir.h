/*============================================================================================================================================
                                                        FACETAREARESERVOIR.H
============================================================================================================================================*/
// 🧩 Select a fresh two-proposal facet-area reservoir with explicit target/density weights.

#pragma once

#include <array>
#include <cstdint>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                   PROPOSAL MEASURES
//------------------------------------------------------------------------------------------------------------------------

struct FacetAreaMeasure
{
    double Target  = 0; // [-] - Nonnegative unnormalized target at this proposal.
    double Density = 0; // [1/area] - Proposal density in the same area measure as the integrand.
};

struct FacetAreaSelection
{
    bool     Accepted       = false;       // [-] - False means invalid/unrepresentable input,not an empty sample.
    uint32_t Selected       = UINT32_MAX;  // [idx] - Ordinal0/1;sentinel for a valid all-zero pair or rejection.
    uint32_t CandidateCount = 2;           // [candidates] - Includes zero-target/null proposals.
    double   WeightSum      = 0;           // [area] - Sum of target/density weights.
    double   Normalizer     = 0;           // [area] - WeightSum/(2*selected target).
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    FRESH SELECTION
//------------------------------------------------------------------------------------------------------------------------

// Uniform must be finite and in[0,1). Choose first iff Uniform<firstWeight/sumWeight.
// A zero target permits zero density and contributes zero weight,but still counts toward M2.
// The caller must establish proposal support for its integrand. This API does not sample geometry,
// generate randomness,trace visibility,combine histories or establish temporal/spatial unbiasedness.
[[nodiscard]] FacetAreaSelection SelectFacetAreaPair(
    const std::array<FacetAreaMeasure, 2>& Proposals,
    double                                Uniform) noexcept;
}
