/*==========================================================================================================================================
                                                         NESTINGCONSTRUCTION.H
==========================================================================================================================================*/
// 🧩 1:1 port of the demo's virtualized geometry build (GeometryHierarchy.js): UV- and sharp-edge-aware weld, adjacency
//    clusters, boundary/seam/crease-locked quadric simplification into a nesting of cluster tiers, its validation and tier soups.

// 📝 Anything arriving as a triangle soup is accepted. Positions weld by exact 3D coordinates so the full manifold adjacency is
//    visible across UV seams and hard creases, while per-corner UVs and per-triangle sharp-edge masks travel with the triangles
//    through every tier. During simplification group borders, open borders and family borders lock position and UVs
//    bit-exact; UV seams and marked sharp edges add line-alignment quadrics, feature corners pin, and degree-2 feature vertices
//    collapse only along their curve. Every cluster records its group-coherent coarser error and sphere so the runtime cut
//    (finer error <= threshold < coarser error) is evaluated per cluster.
//    Demo vocabulary: Level → Tier, Parents → Coarser, Children → Finer, ParentError/ParentSphere → CoarserError/CoarserSphere,
//    GroupId → GroupIdentity, MaxLevels → TierLimit, meshlet → cluster. Arithmetic follows NestingNumerics.h.
#pragma once
#include "NestingNumerics.h"
#include <optional>
#include <string>
#include <vector>
namespace Frontier::VirtualizedGeometry
{
// 📝 One soup triangle (a facet of GeometryHierarchy.js).
struct SoupFacet
{
    std::array<Point3, 3> Corners{};               // [m]
    bool                  CarriesUv      = false;  // [-] UVs / Coordinates present
    std::array<Point2, 3> CornerUv{};              // [-]
    bool                  CarriesNormals = false;  // [-] authored corner normals present
    std::array<Point3, 3> Normals{};               // [-]
    uint32_t              SharpEdges     = 0;      // [-] bit e marks the edge from corner e to corner e + 1
    uint32_t              Family         = 0;      // [-] material family
};
struct SurfaceSoup
{
    std::vector<SoupFacet>               Facets{};
    std::vector<std::array<uint32_t, 2>> SharpVertexPairs{};   // [-] mesh-level SharpEdges given as welded vertex indices
    std::vector<std::array<Point3, 2>>   SharpPointPairs{};    // [m] mesh-level SharpEdges given as position pairs
};
struct NestingOptions
{
    uint32_t              TriangleLimit = 128;    // [-] triangles per cluster
    uint32_t              VertexLimit   = 256;    // [-] unique vertices per cluster
    uint32_t              GroupLimit    = 8;      // [-] finer clusters per simplification group (doubles per tier, up to 32)
    double                TargetRatio   = 0.5;    // [-] triangles kept per simplification pass
    uint32_t              TierLimit     = 32;     // [-]
    std::vector<uint32_t> TransmissiveFamilies{}; // [-] families that form the transmissive (glass) class
    std::optional<double> SharpAngleDegrees{};    // [deg] optional dihedral crease angle (the editor never sets it)
    std::optional<double> SharpNormalCosine{};    // [-] corner-normal crease threshold, cos 45° when absent
    // 📝 Refinements over the demo (Documentation/Plans/Ongoing/VirtualizedGeometry.md I2-I5). All off: the demo bit for bit.
    bool                  MeasuredDeviation   = false;   // [-] I3: group error measured against the source leaves, not summed
    bool                  NeedleGuard         = false;   // [-] I4: aspect-ratio term against needle triangles
    bool                  ClassBorderCollapse = false;   // [-] I5: glass/opaque borders collapse along their curve, not locked
    uint32_t              Threads             = 1;       // [-] I2: groups simplified in parallel (0: every hardware thread)
};
// 📝 The editor's build: every refinement on, every hardware thread.
[[nodiscard]] NestingOptions RefinedNestingOptions();

// 📝 Indexed geometry of one tier: positions per vertex, three vertices, a family, six corner UVs and a sharp mask per triangle.
struct TierGeometry
{
    std::vector<double>   Positions{};
    std::vector<uint32_t> Triangles{};
    std::vector<uint32_t> Families{};
    std::vector<double>   CornerUvs{};
    std::vector<uint8_t>  SharpMask{};
    [[nodiscard]] uint32_t TriangleCount() const noexcept { return static_cast<uint32_t>(Triangles.size() / 3); }
    [[nodiscard]] uint32_t VertexCount() const noexcept { return static_cast<uint32_t>(Positions.size() / 3); }
    [[nodiscard]] Point3   ReadPosition(uint32_t Vertex) const noexcept
    {
        return {Positions[Vertex * 3], Positions[Vertex * 3 + 1], Positions[Vertex * 3 + 2]};
    }
};
struct WeldedSoup : TierGeometry
{
    bool HasUvs = false;
};
[[nodiscard]] WeldedSoup WeldFacetSoup(const SurfaceSoup& Source, const NestingOptions& Options = {});

struct NestingSphere
{
    Point3 Center{};        // [m]
    double Radius = 0.0;    // [m]
    bool operator==(const NestingSphere&) const = default;
};
struct NestingCone
{
    Point3 Axis{};          // [-]
    double Cutoff = 1.0;    // [-] cosine of the half angle
};
struct NestedCluster
{
    uint32_t              Identity      = 0;
    uint32_t              Tier          = 0;
    std::vector<uint32_t> Finer{};                 // [-] clusters of the simplification groups this one stands in for
    std::vector<uint32_t> Coarser{};               // [-] clusters simplified from this one's group
    int32_t               GroupIdentity = -1;
    uint32_t              FirstTriangle = 0;
    std::vector<uint32_t> Triangles{};             // [-] tier triangles, ascending
    uint32_t              TriangleCount = 0;
    Point3                Minimum{}, Maximum{};    // [m] conservative over every finer cluster
    NestingSphere         Sphere{};
    NestingCone           Cone{};
    Point3                TightMinimum{}, TightMaximum{};
    NestingSphere         TightSphere{};
    double                Error         = 0.0;           // [m]
    double                CoarserError  = Unbounded;     // [m]
    NestingSphere         CoarserSphere{};
    uint32_t              ClassMask     = 0;             // [-] bit 0 opaque, bit 1 transmissive
    uint32_t              SharpEdgesInCluster = 0;
    int32_t               PageIndex     = -1;            // [-] set by PackNestingPages
};
struct NestingTier : TierGeometry
{
    uint32_t              Tier = 0;
    std::vector<uint32_t> Clusters{};
    uint32_t              SharpEdgeCount  = 0;
    uint32_t              UvSeamEdgeCount = 0;
};
struct GeometryNesting
{
    std::vector<NestedCluster> Clusters{};
    std::vector<NestingTier>   Tiers{};
    uint32_t                   SourceTriangles = 0;
    uint32_t                   WeldedVertices  = 0;
    bool                       HasUvs          = false;
};
// 📝 BuildGeometryHierarchy. Throws std::invalid_argument on invalid limits, like the demo.
[[nodiscard]] GeometryNesting ConstructGeometryNesting(const SurfaceSoup& Source, const NestingOptions& Options = {});

// 📝 ValidateGeometryHierarchy: independent re-checks that trust none of the builder's bookkeeping.
struct NestingValidation
{
    struct TierSummary
    {
        uint32_t Tier = 0, Clusters = 0, Triangles = 0, Vertices = 0, SharpEdges = 0, UvSeams = 0;
        double   MaximumError = 0.0;
    };
    bool                     Passed = true;
    std::vector<std::string> Failures{};
    std::vector<TierSummary> Tiers{};
};
[[nodiscard]] NestingValidation ValidateGeometryNesting(const SurfaceSoup& Source, const GeometryNesting& Nesting,
                                                        const NestingOptions& Options = {});

// 📝 CollectHierarchyLevel: one tier as a renderable soup owned by its clusters.
struct TierSoup
{
    std::vector<double>   Positions{};    // [m] nine per triangle
    std::vector<double>   CornerUvs{};    // [-] six per triangle
    std::vector<uint8_t>  SharpMask{};
    std::vector<uint8_t>  UvSeamMask{};
    std::vector<uint32_t> Families{};
    std::vector<uint32_t> Owners{};
    std::vector<double>   Errors{};
    uint32_t              TriangleCount = 0;
};
[[nodiscard]] TierSoup CollectNestingTier(const GeometryNesting& Nesting, uint32_t Tier);
} // namespace Frontier::VirtualizedGeometry
