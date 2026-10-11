//============================================================================================================================================
//                                                            TYREMESHSTRUCTURE.H
//============================================================================================================================================
// 📦 Indexed tyre mesh: one welded position pool carrying topology, per-corner attributes carrying shading.

#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                        RECORDS
//------------------------------------------------------------------------------------------------------------------------

/// 📦 One welded point in space. Topology is expressed entirely in terms of these.
/// tag   schema
struct TyrePositionRecord
{
    float    X = 0.0f;   // [mm]  - lateral, along the axis
    float    Y = 0.0f;   // [mm]  - vertical
    float    Z = 0.0f;   // [mm]  - longitudinal
};

/// 📦 Shading attributes at one triangle corner. Three of these accompany every triangle.
/// note  Corners are never welded. Two triangles meeting along a groove wall share positions and therefore
///       share an edge, while keeping the opposed normals that make the wall read as a hard edge.
/// tag   schema
struct TyreCornerRecord
{
    float    NormalX = 0.0f;   // [-]   - surface normal
    float    NormalY = 1.0f;   // [-]   - surface normal
    float    NormalZ = 0.0f;   // [-]   - surface normal
    float    U       = 0.0f;   // [-]   - chart coordinate, circumferential
    float    V       = 0.0f;   // [-]   - chart coordinate, lateral
    uint16_t Group   = 0u;     // [-]   - material group, splits the draw not the topology
};

/// 📦 What the watertightness gate measures.
/// note  ⚠️ BoundaryEdge counts every edge used once, including the legitimate rim opening where the tread
///       meets the sidewall. Acceptance compares against the expected opening, never against zero.
/// tag   schema, diagnostic
struct TyreMeshMetrics
{
    uint32_t PositionCount    = 0u;   // [-]  - welded points
    uint32_t TriangleCount    = 0u;   // [-]  - triangles recorded
    uint32_t BoundaryEdge     = 0u;   // [-]  - edges incident to exactly one triangle
    uint32_t NonManifoldEdge  = 0u;   // [-]  - edges incident to three or more triangles
    uint32_t DegenerateCount  = 0u;   // [-]  - triangles with a repeated index or zero area
    uint32_t DuplicateCount   = 0u;   // [-]  - triangles repeating an existing corner triple
    uint32_t QuadCount        = 0u;   // [-]  - faces recorded as quads rather than loose triangles
    uint32_t LooseTriangle    = 0u;   // [-]  - faces recorded as triangles in their own right
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     MESH STRUCTURE
//------------------------------------------------------------------------------------------------------------------------

/// 📦 Accumulates triangles against a welded position pool, so a shared edge is shared by construction.
/// note  💡 This is the whole reason the port exists. The browser prototype emits every floor piece and
///       every groove wall as an independent triangle soup and relies on separately triangulated polygons
///       agreeing numerically on the vertices they share. They do not: the audit measures 13 167 interior
///       cracks on the off-road preset. Welding on the way in makes agreement structural instead of lucky.
/// note  💡 Positions weld, corners do not. Watertightness is a property of position topology; hard
///       shading edges are a property of corners. Conflating them is what forces a builder to choose between
///       a closed mesh and a crisp groove wall, and it never had to be a choice.
/// cost  🚩
/// tag   api
class TyreMeshStructure
{
public:
    TyreMeshStructure() noexcept = default;

    /// 📦 Returns the index of the welded point at this position, creating it when none is within tolerance.
    /// in    X, Y, Z   [mm]  position in tyre-local space
    /// out   uint32_t  [-]   index into the position pool
    /// cost  ✔️
    /// note  ⚠️ Probes the 27 cells around the quantised key, not just the one it lands in. A plain hash on a
    ///       quantised key silently fails for two points a hair apart that straddle a cell edge, which is the
    ///       exact case a groove wall produces and the exact case that must not leak.
    /// tag   api, allocating, nonthrowing
    uint32_t WeldPosition(float X, float Y, float Z) noexcept;

    /// 📦 Records one triangle over three already-welded positions.
    /// in    A, B, C        [-]  position indices, counter-clockwise seen from outside
    /// in    CornerA/B/C    [-]  shading attributes for each corner, never welded
    /// err   a triangle with a repeated index is still recorded and is reported as degenerate by the gate
    /// cost  ✔️
    /// tag   api, allocating, nonthrowing
    void AddTriangle(uint32_t                A,
                     uint32_t                B,
                     uint32_t                C,
                     const TyreCornerRecord& CornerA,
                     const TyreCornerRecord& CornerB,
                     const TyreCornerRecord& CornerC) noexcept;

    /// 📦 Records one quad as two triangles, and remembers that it was a quad.
    /// in    A, B, C, D     [-]  position indices in ring order, counter-clockwise seen from outside
    /// in    CornerA…D     [-]  shading attributes, one per ring position
    /// note  💡 The engine draws triangles, so a quad is split here rather than carried. What is kept is the
    ///       count: quad fraction is the measure of whether the tread really has the topology it claims,
    ///       and it cannot be recovered from an index buffer after the fact.
    /// cost  ✔️
    /// tag   api, allocating, nonthrowing
    void AddQuad(uint32_t                A,
                 uint32_t                B,
                 uint32_t                C,
                 uint32_t                D,
                 const TyreCornerRecord& CornerA,
                 const TyreCornerRecord& CornerB,
                 const TyreCornerRecord& CornerC,
                 const TyreCornerRecord& CornerD) noexcept;

/// 📦 Splits every edge that another welded position happens to lie on.
    /// out   uint32_t  [-]  edges split; zero means the mesh had no T-junctions to begin with
    /// cost  🔴
    /// note  💡 A T-junction is the crack welding cannot close. Two faces can share a span of space without
    ///       sharing an edge: one spans it with a single edge while its neighbour, cut by something the
    ///       first never saw, spans it with two. Every vertex is welded, every position agrees, and the
    ///       edge ledger still reports three one-sided edges. The only repair is to give the longer edge
    ///       the vertex it is missing, which is what this does.
    /// note  ⚠️ Run once after the mesh is complete and before any audit. Faces added afterwards can
    ///       reintroduce junctions, and nothing re-checks.
    /// tag   api, allocating, nonthrowing
    uint32_t RepairJunctions() noexcept;

        /// 📦 Measures watertightness over the position topology.
    /// out   TyreMeshMetrics  [-]  counts; zero cracks means every interior edge has exactly two triangles
    /// cost  🚩
    /// note  Builds an edge ledger over the index buffer each call. Linear in triangles, not cached, because
    ///       it is a gate rather than a hot path.
    /// tag   api, allocating, nonthrowing, diagnostic
    [[nodiscard]] TyreMeshMetrics QueryMetrics() const noexcept;

    void Reserve(uint32_t Positions, uint32_t Triangles) noexcept;
    void Clear() noexcept;

    [[nodiscard]] const std::vector<TyrePositionRecord>& QueryPositions() const noexcept { return Positions; }
    [[nodiscard]] const std::vector<TyreCornerRecord>&   QueryCorners()   const noexcept { return Corners; }
    [[nodiscard]] const std::vector<uint32_t>&           QueryIndices()   const noexcept { return Indices; }

    /// 📦 Distance below which two points are the same point.
    /// note  📐 1 µm. The pattern is authored in integer hundredths of a millimetre upstream, so genuinely
    ///       distinct features are never closer than 10 µm, and float32 at a 500 mm radius resolves ~30 nm.
    ///       The gap between those two bounds is where this tolerance has to sit.
    static constexpr float WeldTolerance = 1.0e-3f;   // [mm]  - below this two points weld

private:
    [[nodiscard]] static uint64_t CellKey(int64_t Cx, int64_t Cy, int64_t Cz) noexcept;

    std::vector<TyrePositionRecord>                 Positions;
    std::vector<TyreCornerRecord>                   Corners;
    std::vector<uint32_t>                           Indices;
    std::unordered_map<uint64_t, std::vector<uint32_t>> CellIndex;   // quantised cell → candidate positions
    uint32_t                                        QuadTally = 0u;
};

}   // namespace Frontier
