/*====================================================================================================================================
                                                        FACETTIERBUILD.H
====================================================================================================================================*/
// 🧩 Virtualized-geometry build, first tier: facet soup weld, edge adjacency, UV-seam / sharp-edge classification,
//    bounded facet-cluster partition with bounds spheres and normal cones, and build validation (C++ port, step K1a).

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct FacetPoint3D
{
    double X = 0.0;   // [m] - X component
    double Y = 0.0;   // [m] - Y component
    double Z = 0.0;   // [m] - Z component
};

// 📝 Unwelded input: three corners per facet, exactly as heightmap / SDF-meshing / DCC export presents them.
//    Classification is the material-class index (JS reference: family); values also drive class-pure partition.
struct FacetField
{
    std::vector<FacetPoint3D>               Corners;              // [m] - 3 per facet, facet-major
    std::vector<std::array<double, 2>> CornerUVs;            // [-] - 3 per facet; empty when the source has no UVs
    std::vector<uint32_t>              Classification;       // [idx] - 1 per facet
    std::vector<uint8_t>               SharpMask;            // [-] - 1 per facet; bit c set = edge corner c → corner (c+1)%3 is sharp
};

// 📝 Welded indexed form: positions coincident by exact position, UVs stay per-corner (a UV seam is two corners
//    at one welded vertex carrying different UVs, so UVs can never be per-vertex here).
struct PolygonComplex
{
    std::vector<FacetPoint3D>               Positions;            // [m] - welded vertices
    std::vector<uint32_t>              Triangles;            // [idx] - 3 welded vertex indices per triangle
    std::vector<std::array<double, 2>> CornerUVs;            // [-] - 3 per triangle, aligned with Triangles
    std::vector<uint32_t>              Classification;       // [idx] - 1 per triangle
    std::vector<uint8_t>               SharpMask;            // [-] - 1 per triangle; propagated after incidence analysis
    bool                               CarriesUVs = false;       // [-] - source carried UVs
};

struct WeldSpecification
{
    bool   UseCreaseAngle = false;   // [-] - classify edges sharp by dihedral angle
    double CreaseCosine   = 0.0;     // [-] - cos(crease angle); edge sharp when dot(n0, n1) < this
};

struct WeldOutcome
{
    PolygonComplex Complex;
    uint32_t       SharpEdgeCount    = 0;   // [idx] - undirected edges classified sharp
    uint32_t       UvSeamEdgeCount   = 0;   // [idx] - undirected edges whose corner UVs disagree
    uint32_t       BoundaryEdgeCount = 0;   // [idx] - undirected edges with exactly one incident triangle
};

// 📝 One bounded cluster of facets (JS reference: meshlet). Triangles of one classification only.
struct FacetCluster
{
    std::vector<uint32_t> Triangles;       // [idx] - member triangles (into PolygonComplex::Triangles / 3)
    FacetPoint3D               SphereCentre;    // [m] - bounds sphere centre
    double                SphereRadius = 0.0;   // [m] - bounds sphere radius
    FacetPoint3D               ConeAxis;        // [-] - mean face-normal axis
    double                ConeCutoff = -1.0;    // [-] - min dot(face normal, axis); -1 = no backface culling
    uint32_t              Classification = 0;   // [idx] - the single classification of every member
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Weld a facet field by exact position; propagate sharp classification from per-facet masks and,
// optionally, a dihedral crease angle; count sharp / UV-seam / boundary edges from edge incidence.
WeldOutcome WeldFacetField(const FacetField& Source, const WeldSpecification& Specification);

// Partition a welded complex into connected, class-pure clusters of at most TriangleLimit triangles
// (greedy adjacency growth from the lowest unassigned triangle), each with bounds sphere + normal cone.
std::vector<FacetCluster> PartitionFacetClusters(const PolygonComplex& Complex, uint32_t TriangleLimit = 128);

// Validate a partition against the build invariants; returns one string per violation (empty = valid):
// triangle coverage exactly once, cluster size cap, connectivity, class purity, sphere containment.
std::vector<std::string> ValidateClusterBuild(const PolygonComplex& Complex,
                                              const std::vector<FacetCluster>& Clusters,
                                              uint32_t TriangleLimit);

}   // namespace Frontier
