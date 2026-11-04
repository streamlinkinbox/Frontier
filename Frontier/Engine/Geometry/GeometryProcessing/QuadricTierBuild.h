/*====================================================================================================================================
                                                        QUADRICTIERBUILD.H
====================================================================================================================================*/
// 🧩 Virtualized-geometry tier build: quadric edge-contraction simplification with locked borders, cluster grouping,
//    and the coarsening sequence (leaf clusters → one bounded root cluster) with monotonic deviation and enclosure.

#pragma once

#include "Engine/Geometry/GeometryProcessing/FacetTierBuild.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One depth tier of the cluster sequence: the level geometry and its bounded partition.
//    Links run one way only — a coarser cluster encloses a set of finer clusters.
struct ClusterTier
{
    uint32_t                           DepthTier = 0;        // [-] - 0 = leaf
    PolygonComplex                     Complex;              // [-] - level geometry
    std::vector<FacetCluster>          Clusters;             // [-] - partition of Complex
    std::vector<double>                ClusterDeviation;     // [m] - geometric deviation of each cluster
    std::vector<double>                VertexDeviation;      // [m] - per-vertex deviation of Complex
    std::vector<std::vector<uint32_t>> EnclosedClusters;    // [idx] - finer-tier clusters each cluster encloses
    double                             MaximumDeviation = 0.0; // [m] - largest cluster deviation at this tier
};

// 📝 The whole coarsening sequence, leaf tier first. Every tier covers the same surface at a lower facet count.
struct ClusterTierSequence
{
    std::vector<ClusterTier> Tiers;                  // [-] - leaf first, root last
    uint32_t                 SourceTriangleCount = 0; // [idx]
    uint32_t                 SharpEdgeCount      = 0; // [idx]
    uint32_t                 UvSeamEdgeCount     = 0; // [idx]
    uint32_t                 BoundaryEdgeCount   = 0; // [idx]
};

struct TierBuildSpecification
{
    uint32_t TriangleLimit      = 128;   // [idx] - leaf cluster cap; the build stops here
    uint32_t GroupLimit         = 8;     // [idx] - finer clusters grouped at the leaf tier
    uint32_t GroupLimitCeiling  = 32;    // [idx] - grouping cap after doubling per tier
    double   CoarseningFraction = 0.5;   // [-] - fraction of facets a group keeps per tier
    bool     UseCreaseAngle     = true;  // [-] - classify edges sharp by dihedral angle
    double   CreaseCosine       = 0.70710678118654752; // [-] - cos(45°)
    double   UvPenalty          = 4.0;   // [-] - weight of UV-space edge length in the collapse cost
    uint32_t MaximumTiers       = 24;    // [idx] - guard against a non-terminating build
};

struct SimplificationSpecification
{
    uint32_t TargetTriangles       = 0;    // [idx] - 0 = derive from TargetFraction
    double   TargetFraction        = 0.5;  // [-] - used when TargetTriangles is 0
    double   UvPenalty             = 4.0;  // [-] - weight of UV-space edge length in the collapse cost
    double   NormalCosineThreshold = 0.0;  // [-] - reject a collapse that turns a facet below this dot product
};

struct SimplificationOutcome
{
    PolygonComplex      Complex;            // [-] - simplified geometry (per-corner UVs preserved)
    std::vector<double> VertexDeviation;    // [m] - per output vertex
    std::vector<double> TriangleDeviation;  // [m] - per output triangle (max of its corners)
    uint32_t            CollapseCount   = 0; // [idx]
    uint32_t            BlockedCollapses = 0; // [idx]
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Simplify a welded complex by quadric edge contraction. LockedVertices (empty = none) pins a vertex to its input
// position; sharp edges, UV seams and open boundaries are never collapsed across. Returns the simplified geometry with
// a conservative per-vertex deviation accumulated from every merged input position.
SimplificationOutcome SimplifyPolygonComplex(const PolygonComplex& Source,
                                              const SimplificationSpecification& Specification,
                                              const std::vector<uint8_t>& LockedVertices = {});

// Build the whole coarsening sequence: weld → leaf partition → (group → simplify → recluster) until a tier fits the
// cluster cap. Coarser clusters record the finer clusters they enclose; deviation never decreases with depth.
ClusterTierSequence BuildClusterTierSequence(const PolygonComplex& Source,
                                             const TierBuildSpecification& Specification);

// Validate a built sequence; returns one string per violation (empty = valid).
std::vector<std::string> ValidateTierSequence(const ClusterTierSequence& Sequence,
                                              const TierBuildSpecification& Specification);

}   // namespace Frontier
