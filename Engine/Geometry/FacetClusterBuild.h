/*==========================================================================================================================================
                                                        FACETCLUSTERBUILD.H
==========================================================================================================================================*/
// 🧩 Virtualized-geometry tier build, step K1: weld a facet soup into a shared-position polygon complex, classify edge incidence
//    (marked sharp, UV seam, dihedral crease, open boundary), partition scored facet clusters under triangle and vertex caps,
//    measure each cluster's enclosure sphere and normal cone, and validate the finished partition.
//
// 📝 Behavioural reference: Experimental/VehicleHmiPrototype/Source/GeometryHierarchy.js — WeldFacetSoup, BuildEdgeIncidence,
//    BuildMeshlets, MeasureCluster. This is a port of the mechanism, not a transliteration: containers, hashing and the
//    adjacency expansion are C++ structures, while the scoring terms and their constants stay numerically identical so the
//    C++ build reproduces the reference cluster counts on the reference corpora.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

//--------------------------------------------------------------------------------------------------------------------------
//                                                        STRUCTS
//--------------------------------------------------------------------------------------------------------------------------

struct SpatialPosition
{
    double X = 0.0;   // [m] - X component
    double Y = 0.0;   // [m] - Y component
    double Z = 0.0;   // [m] - Z component
};

// 📝 Unwelded input: three corners per facet, exactly as a heightmap, a meshed distance field or a DCC export presents them.
//    Family is the authoring material index (JS reference: Family); the reference partitions on the transmissive class
//    derived from it, not on the family itself, so two opaque families may share a cluster and a glass band may not.
struct FacetField
{
    std::vector<SpatialPosition>               Corners;        // [m]   - 3 per facet, facet-major; empty facets are rejected
    std::vector<std::array<double, 2>>         CornerUVs;      // [-]   - 3 per facet; empty when the source carries no UVs
    std::vector<uint32_t>                      Family;         // [idx] - 1 per facet
    std::vector<uint8_t>                       SharpMask;      // [-]   - 1 per facet; bit c marks edge (c, c+1 mod 3) sharp
};

// 📝 Welded indexed form: positions merge on exact coordinate bits, so connectivity is visible across UV seams and creases.
//    UVs stay per corner — a seam is one welded vertex carrying two different corner UVs, which no per-vertex UV can hold.
struct PolygonComplex
{
    std::vector<SpatialPosition>               Positions;      // [m]   - welded vertices
    std::vector<uint32_t>                      Triangles;      // [idx] - 3 welded vertex indices per facet
    std::vector<std::array<double, 2>>         CornerUVs;      // [-]   - 3 per facet, aligned with Triangles
    std::vector<uint32_t>                      Family;         // [idx] - 1 per facet
    std::vector<uint8_t>                       SharpMask;      // [-]   - 1 per facet, propagated from incidence
    bool                                       HasUVs = false; // [-]   - the source carried UVs
};

struct WeldSpecification
{
    bool   HasCreaseAngle = false;   // [-] - classify two-facet edges sharp by dihedral angle
    double CreaseCosine   = 0.0;     // [-] - cos(crease angle); the edge is sharp when dot(n0, n1) < CreaseCosine
};

struct WeldOutcome
{
    PolygonComplex Complex;              // [-] - welded complex with propagated sharp masks
    uint32_t       SharpEdgeCount    = 0;   // [idx] - undirected edges classified sharp
    uint32_t       UvSeamEdgeCount   = 0;   // [idx] - undirected edges whose corner UVs disagree
    uint32_t       BoundaryEdgeCount = 0;   // [idx] - undirected edges with exactly one incident facet
};

struct ClusterSpecification
{
    uint32_t              TriangleLimit = 128;   // [idx] - facet cap per cluster (JS reference: TriangleLimit)
    uint32_t              VertexLimit   = 256;   // [idx] - distinct welded-vertex cap per cluster (JS reference: VertexLimit)
    std::vector<uint32_t> TransmissiveFamily;    // [idx] - families that partition as their own transmissive class
};

// 📝 One bounded cluster of facets (JS reference: meshlet). Sphere and cone are the runtime cull inputs; Minimum/Maximum are the
//    axis-aligned enclosure, which later tiers union to prove a coarser cluster contains every cluster it replaces.
struct FacetCluster
{
    std::vector<uint32_t> Triangles;              // [idx] - member facets, ascending
    SpatialPosition       SphereCentre;           // [m]   - corner-mean centre
    double                SphereRadius   = 0.0;   // [m]   - farthest corner distance from the centre
    SpatialPosition       ConeAxis;               // [-]   - normalised summed facet normal
    double                ConeCutoff     = -1.0;  // [-]   - min dot(axis, unit facet normal); 1 = flat, -1 = no culling
    std::array<double, 3> Minimum{};              // [m]   - enclosure minimum
    std::array<double, 3> Maximum{};              // [m]   - enclosure maximum
    uint32_t              PartitionKey   = 0;     // [idx] - the single partition class of every member
};

//--------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//--------------------------------------------------------------------------------------------------------------------------

// Weld corners on exact position bits, build edge incidence, propagate authored sharp masks, then classify remaining
// two-facet edges by the requested crease angle. Imported edges inherit their counts in WeldOutcome.
WeldOutcome WeldFacetField(const FacetField& Source, const WeldSpecification& Specification = {});

// Grow connected clusters from the lowest unassigned facet, choosing each next member by normal alignment with the running
// cone, scale-normalised centroid distance and a penalty for joining only across a seam or crease. Members stay class-pure.
// Returns clusters in seed order; every facet appears in exactly one cluster.
std::vector<FacetCluster> PartitionFacetClusters(const PolygonComplex& Complex,
                                                 const ClusterSpecification& Specification = {});

// Measure a cluster's enclosure sphere, enclosure box and normal cone from a member list.
FacetCluster MeasureFacetCluster(const PolygonComplex& Complex, const std::vector<uint32_t>& Members,
                                 uint32_t PartitionKey);

// Validate a partition against the K1 invariants; returns one string per violation (empty = valid):
// coverage exactly once, ascending members, triangle cap, welded-vertex cap, connectivity, class purity, sphere containment,
// enclosure containment and a usable normal cone.
std::vector<std::string> ValidateClusterBuild(const PolygonComplex& Complex,
                                             const std::vector<FacetCluster>& Clusters,
                                             const ClusterSpecification& Specification);

// The transmissive class the reference partitions on: 1 when the family is listed as transmissive, otherwise 0.
uint32_t QueryPartitionClass(uint32_t Family, const ClusterSpecification& Specification);

}   // namespace Frontier
