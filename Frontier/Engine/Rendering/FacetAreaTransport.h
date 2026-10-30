/*============================================================================================================================================
                                                       FACETAREATRANSPORT.H
============================================================================================================================================*/
// 🧩 Query packed float32 triangle visibility and trace diffuse facet suffix/receiver connections without GPU inputs.
#pragma once
#include "FacetAreaScore.h"
#include "TriangleVisibilitySpace.h"

namespace Frontier
{
struct FacetAreaShadingTriangle
{
    FacetAreaTriangle Triangle;
    std::array<std::array<float, 3>, 3> Normals{};
    std::array<float, 3> Albedo{};
};

struct FacetAreaStrike
{
    bool Accepted = false;
    uint32_t Index = UINT32_MAX;
    std::array<float, 3> Coordinates{}; // [m,-,-] - Distance,U,V;Index sentinel denotes a valid miss.
};

struct FacetAreaSurface
{
    bool Accepted = false;
    uint32_t Index = UINT32_MAX;
    std::array<float, 3> Position{}, Normal{}, Albedo{};
    std::array<float, 3> Strike{};
};

struct FacetAreaPath
{
    bool Accepted = false, SuffixVisible = false, ConnectionVisible = false;
    std::array<float, 3> Origin{}, Direction{}, Rgb{}, SuffixOnly{}, ConnectionOnly{};
    float Maximum = 0;
};

class FacetAreaTransport
{
    std::vector<FacetAreaShadingTriangle> Surfaces;
    TriangleVisibilitySpace::FloatAtlas Atlas;
    bool Prepared = false;
    FacetAreaSurface Resolve(uint32_t Index, float U, float V, const std::array<float, 3>& Position,
                             const std::array<float, 3>& Direction) const noexcept;
public:
    // Surfaces must already follow the packed BVH corner order;no implicit permutation or geometry repair.
    bool Prepare(const std::vector<FacetAreaShadingTriangle>& Triangles, const TriangleVisibilitySpace::FloatAtlas& Packed);
    [[nodiscard]] FacetAreaStrike Trace(const std::array<float, 3>& Origin, const std::array<float, 3>& Direction,
                                        float Minimum, float Maximum) const noexcept;
    [[nodiscard]] FacetAreaSurface Primary(const std::array<float, 3>& Origin,
                                           const std::array<float, 3>& Direction) const noexcept;
    [[nodiscard]] FacetAreaSurface Source(const FacetAreaProposal& Proposal) const noexcept;
    [[nodiscard]] FacetAreaPath Connect(const FacetAreaProposal& Proposal, const FacetAreaSurface& Receiver) const noexcept;
};
}
