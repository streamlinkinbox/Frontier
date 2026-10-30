/*============================================================================================================================================
                                                        FACETAREAPROPOSALS.CPP
============================================================================================================================================*/
// 🧩 Retain shader float32 area-CDF and square-root triangle sampling without renderer activation.

#include "FacetAreaProposals.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace Frontier
{
uint32_t HashFacetAreaBits(uint32_t Bits) noexcept
{
    Bits ^= Bits >> 16;
    Bits *= 0x7feb352du;
    Bits ^= Bits >> 15;
    Bits *= 0x846ca68bu;
    return Bits ^ (Bits >> 16);
}

std::array<float, 3> DrawFacetAreaUniforms(uint32_t Seed, uint32_t Draw) noexcept
{
    std::array<float, 3> Uniforms{};
    for (uint32_t Coordinate = 0; Coordinate < 3; ++Coordinate)
        Uniforms[Coordinate] = float(HashFacetAreaBits(Seed ^ ((3 * Draw + Coordinate + 1) * 0x9e3779b9u)) >> 8) /
                               16777216.f;
    return Uniforms;
}

float DrawFacetAreaSelectionUniform(uint32_t Seed, uint32_t Lane) noexcept
{
    return float(HashFacetAreaBits(Seed ^ (0x7f4a7c15u + Lane * 0x9e3779b9u)) >> 8) / 16777216.f;
}

bool FacetAreaProposals::Prepare(const std::vector<FacetAreaTriangle>& Corners)
{
    Triangles.clear(); Areas.clear(); Cumulative.clear(); UniformDensity.clear(); AreaDensity = 0;
    if (Corners.empty() || Corners.size() > 16777216u) return false;
    std::vector<float> PreparedAreas, PreparedCumulative, PreparedDensity;
    float Total = 0;
    for (const auto& Triangle : Corners)
    {
        std::array<float, 3> First{}, Second{};
        for (uint32_t Axis = 0; Axis < 3; ++Axis)
        {
            if (!std::isfinite(Triangle.A[Axis]) || !std::isfinite(Triangle.B[Axis]) ||
                !std::isfinite(Triangle.C[Axis])) return false;
            First[Axis]  = Triangle.B[Axis] - Triangle.A[Axis];
            Second[Axis] = Triangle.C[Axis] - Triangle.A[Axis];
        }
        const std::array<float, 3> Cross{First[1] * Second[2] - First[2] * Second[1],
                                         First[2] * Second[0] - First[0] * Second[2],
                                         First[0] * Second[1] - First[1] * Second[0]};
        const float Area = .5f * std::sqrt((Cross[0] * Cross[0] + Cross[1] * Cross[1]) + Cross[2] * Cross[2]);
        if (!std::isfinite(Area) || Area <= 0) return false;
        Total += Area;
        PreparedAreas.push_back(Area);
    }
    if (!std::isfinite(Total) || Total <= 0 || !std::isfinite(1.f / Total) || 1.f / Total <= 0) return false;
    float Boundary = 0;
    for (float Area : PreparedAreas)
    {
        const float Next = Boundary + Area / Total;
        const float Density = 1.f / float(Corners.size()) / Area;
        if (!std::isfinite(Next) || Next <= Boundary || !std::isfinite(Density) || Density <= 0) return false;
        if (PreparedCumulative.size() + 1 < PreparedAreas.size() && Next >= 1.f) return false;
        Boundary = Next;
        PreparedCumulative.push_back(Next);
        PreparedDensity.push_back(Density);
    }
    // 📝 Final float CDF need not be exactly1;the shader assigns the remainder to the last triangle.
    Triangles      = Corners;
    Areas          = std::move(PreparedAreas);
    Cumulative     = std::move(PreparedCumulative);
    UniformDensity = std::move(PreparedDensity);
    AreaDensity    = 1.f / Total;
    return true;
}

FacetAreaProposal FacetAreaProposals::Draw(uint32_t Seed, uint32_t Ordinal, FacetAreaMeasureCategory Measure) const noexcept
{
    FacetAreaProposal Proposal;
    if (Triangles.empty() || (Measure != FacetAreaMeasureCategory::UniformFacet &&
                             Measure != FacetAreaMeasureCategory::ProportionalArea)) return Proposal;
    const auto Uniforms = DrawFacetAreaUniforms(Seed, Ordinal);
    const uint32_t Count = uint32_t(Triangles.size());
    uint32_t Index = std::min(uint32_t(Uniforms[0] * float(Count)), Count - 1);
    if (Measure == FacetAreaMeasureCategory::ProportionalArea)
    {
        Index = Count - 1;
        for (uint32_t Facet = 0; Facet < Count; ++Facet)
            if (Uniforms[0] < Cumulative[Facet]) { Index = Facet; break; }
    }
    const float Root = std::sqrt(Uniforms[1]);
    Proposal.Index   = Index;
    Proposal.U       = 1.f - Root;
    Proposal.V       = Root * Uniforms[2];
    Proposal.Density = Measure == FacetAreaMeasureCategory::UniformFacet ? UniformDensity[Index] : AreaDensity;
    for (uint32_t Axis = 0; Axis < 3; ++Axis)
    {
        const auto& Triangle = Triangles[Index];
        Proposal.Point[Axis] = (Triangle.A[Axis] + Proposal.U * (Triangle.B[Axis] - Triangle.A[Axis])) +
                               Proposal.V * (Triangle.C[Axis] - Triangle.A[Axis]);
        if (!std::isfinite(Proposal.Point[Axis])) return FacetAreaProposal{};
    }
    Proposal.Accepted = true;
    return Proposal;
}
}
