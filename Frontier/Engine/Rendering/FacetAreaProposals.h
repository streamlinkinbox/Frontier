/*============================================================================================================================================
                                                         FACETAREAPROPOSALS.H
============================================================================================================================================*/
// 🧩 Generate float32 continuous-area proposals with shader-compatible draw ordinals and area densities.

#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace Frontier
{
struct FacetAreaTriangle
{
    std::array<float, 3> A{}, B{}, C{}; // [m] - Finite ordered triangle corners.
};

enum class FacetAreaMeasureCategory : uint32_t
{
    UniformFacet = 0,
    ProportionalArea = 1
};

struct FacetAreaProposal
{
    bool                 Accepted = false;       // [-] - Explicit validity,not a null transport result.
    uint32_t             Index    = UINT32_MAX;  // [idx] - Caller triangle ordinal;no renumbering.
    float                U        = 0;           // [-] - Weight of corner B.
    float                V        = 0;           // [-] - Weight of corner C.
    float                Density  = 0;           // [1/m²] - Proposal density in area measure.
    std::array<float, 3>  Point{};                // [m] - A+U*(B-A)+V*(C-A),float32 order.
};

uint32_t HashFacetAreaBits(uint32_t Bits) noexcept;
std::array<float, 3> DrawFacetAreaUniforms(uint32_t Seed, uint32_t Draw) noexcept;
float DrawFacetAreaSelectionUniform(uint32_t Seed, uint32_t Lane) noexcept;

class FacetAreaProposals
{
    std::vector<FacetAreaTriangle> Triangles;
    std::vector<float> Areas, Cumulative, UniformDensity;
    float AreaDensity = 0;

public:
    // Prepare is cold-path allocating;Draw allocates nothing. Failure clears any previous distribution.
    // Only positive-area finite triangles with representable PDFs and increasing float CDF are accepted.
    bool Prepare(const std::vector<FacetAreaTriangle>& Corners);
    [[nodiscard]] FacetAreaProposal Draw(uint32_t Seed, uint32_t Ordinal, FacetAreaMeasureCategory Measure) const noexcept;
};
}
