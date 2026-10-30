/*============================================================================================================================================
                                                       FACETAREARESERVOIR.CPP
============================================================================================================================================*/
// 🧩 Resolve fresh facet-area pair weights and bounded finite normalizers without renderer activation.

#include "FacetAreaReservoir.h"

#include <cmath>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                    FRESH SELECTION
//------------------------------------------------------------------------------------------------------------------------

FacetAreaSelection SelectFacetAreaPair(const std::array<FacetAreaMeasure, 2>& Proposals, double Uniform) noexcept
{
    FacetAreaSelection Selection;
    if (!std::isfinite(Uniform) || Uniform < 0 || Uniform >= 1) return Selection;

    std::array<double, 2> Weights{};
    for (uint32_t Index = 0; Index < 2; ++Index)
    {
        const auto& Proposal = Proposals[Index];
        if (!std::isfinite(Proposal.Target) || Proposal.Target < 0 ||
            !std::isfinite(Proposal.Density) || Proposal.Density < 0) return Selection;
        if (Proposal.Target == 0) continue;
        if (Proposal.Density == 0) return Selection;
        Weights[Index] = Proposal.Target / Proposal.Density;
        if (!std::isfinite(Weights[Index]) || Weights[Index] <= 0) return Selection;
    }

    const double Sum = Weights[0] + Weights[1];
    if (!std::isfinite(Sum)) return Selection;
    if (Sum == 0)
    {
        Selection.Accepted = true;
        return Selection;
    }

    const uint32_t Selected = (Weights[0] > 0 && Uniform == 0) || Uniform < Weights[0] / Sum ? 0 : 1;
    // 📝 Exponent decomposition avoids overflowing2*target or underflowing sum/2 before division.
    int SumExponent = 0, TargetExponent = 0;
    const double SumFraction = std::frexp(Sum, &SumExponent);
    const double TargetFraction = std::frexp(Proposals[Selected].Target, &TargetExponent);
    const double Normalizer = std::ldexp(SumFraction / TargetFraction, SumExponent - TargetExponent - 1);
    if (!std::isfinite(Normalizer) || Normalizer <= 0) return Selection;

    Selection.Accepted = true;
    Selection.Selected = Selected;
    Selection.WeightSum = Sum;
    Selection.Normalizer = Normalizer;
    return Selection;
}
}
