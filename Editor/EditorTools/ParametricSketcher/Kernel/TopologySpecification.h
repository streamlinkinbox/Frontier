//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Kernel/TopologySpecification.h — B-rep: vertices, edges, coedges, loops, faces and the body that owns them
//============================================================================================================================================
// A body is a set of faces (NURBS surface + trimming loops) glued along shared edges. Every edge is used by exactly two
//    coedges with opposite sense in a closed manifold body; that rule, plus a positive signed volume, is what "correct
//    winding" means here and it is what the booleans of later phases preserve.
//
//    Building is generic rather than per-primitive: Sew() merges coincident boundary curves of independent faces,
//    Capped() closes remaining planar boundary loops with planar faces, Orient() flips faces until neighbours disagree
//    across each edge and the volume is positive. Box, cylinder, cone, extrusion and revolution all go through the same
//    three calls, so a bug shows up everywhere at once — and so does a fix.
//
//    Indices are plain ints into the body's vectors (no pointers): bodies copy freely, which the undo timeline relies on.
#pragma once

#include "CurveSpecification.h"
#include "SurfaceSpecification.h"
#include <vector>

namespace Frontier
{

struct BrepVertex
{
    Vec3 Point;                                                                         // [m]
};

struct BrepEdge
{
    NurbsCurve Curve;                                                                   // [-] 3D geometry, parameter increases start → end
    int        VertexStart = -1;                                                        // [-]
    int        VertexEnd = -1;                                                          // [-] equals VertexStart for a closed edge
    std::vector<int> Coedges;                                                           // [-] users; 2 with opposite sense ⇒ manifold interior edge

    [[nodiscard]] bool Closed() const noexcept { return VertexStart == VertexEnd; }
};

struct BrepCoedge
{
    int  Edge = -1;                                                                     // [-]
    bool Reversed = false;                                                              // [-] traverse the edge end → start
    int  Face = -1;                                                                     // [-]
    int  Loop = -1;                                                                     // [-]
    std::vector<Vec2> Trace;                                                            // [-] (u,v) polyline of the edge on the face, traversal order; empty ⇒ derive on demand
};

struct BrepLoop
{
    std::vector<int> Coedges;                                                           // [-] ordered, head to tail
    int  Face = -1;                                                                     // [-]
    bool Outer = true;                                                                  // [-] false = hole
};

struct BrepFace
{
    NurbsSurface     Surface;                                                           // [-]
    std::vector<int> Loops;                                                             // [-] outer first
    bool             Reversed = false;                                                  // [-] face normal = −surface normal
    bool             Natural = true;                                                    // [-] loops are the surface's own boundary (no trimming)
};

enum class BodyClassification : uint8_t { Wire, Sheet, Solid };
[[nodiscard]] const char* Describe(BodyClassification Classification) noexcept;

struct BodyReport                                                                       // result of Validate()
{
    int    Vertices = 0, Edges = 0, Faces = 0, Loops = 0;                               // [-]
    int    OpenEdges = 0;                                                               // [-] edges with one coedge
    int    NonManifoldEdges = 0;                                                        // [-] edges with > 2 coedges
    int    MisorientedEdges = 0;                                                        // [-] two coedges with the same sense
    int    EulerCharacteristic = 0;                                                     // [-] V − E + F − (inner loops)
    int    Hulls = 0;                                                                  // [-] edge-connected face groups
    int    Genus = -1;                                                                  // [-] closed body: total grips = Hulls − χ/2
    double Volume = 0.0;                                                                // [m³] signed
    double Area = 0.0;                                                                  // [m²]
    bool   Closed = false;                                                              // [-]
    bool   Manifold = false;                                                            // [-]
    bool   Oriented = false;                                                            // [-]
    [[nodiscard]] bool Solid() const noexcept { return Closed && Manifold && Oriented && Volume > 0.0; }
};

class BrepBody
{
public:
    std::vector<BrepVertex> Vertices;
    std::vector<BrepEdge>   Edges;
    std::vector<BrepCoedge> Coedges;
    std::vector<BrepLoop>   Loops;
    std::vector<BrepFace>   Faces;

    //---------------------------------------------- construction ----------------------------------------------
    // One face from a surface: its natural boundary becomes edges (seams for closed directions, no edge for degenerate sides).
    [[nodiscard]] static BrepBody FromSurface(const NurbsSurface& Surface) noexcept;
    // Sew independent faces along coincident boundary edges, then cap and orient. The generic solid builder.
    [[nodiscard]] static Deliver<BrepBody> Sew(const std::vector<NurbsSurface>& Surfaces, double Tolerance = ScalarCriteria::MergeTolerance, bool Cap = true) noexcept;
    [[nodiscard]] static Deliver<BrepBody> Box(Vec3 CornerA, Vec3 CornerB) noexcept;
    [[nodiscard]] static Deliver<BrepBody> Cylinder(Vec3 FootCentre, Vec3 Axis, double Radius, double Height) noexcept;
    [[nodiscard]] static Deliver<BrepBody> Cone(Vec3 FootCentre, Vec3 Axis, double RadiusFoot, double RadiusTop, double Height) noexcept;
    [[nodiscard]] static Deliver<BrepBody> Sphere(Vec3 Centre, double Radius) noexcept;
    [[nodiscard]] static Deliver<BrepBody> Torus(Vec3 Centre, Vec3 Axis, double RadiusMajor, double RadiusMinor) noexcept;
    // Closed planar profile → solid. The profile is split at tangent kinks so each straight run becomes its own face (Plasticity style).
    [[nodiscard]] static Deliver<BrepBody> Extrude(const NurbsCurve& Profile, Vec3 Direction, double Length) noexcept;
    [[nodiscard]] static Deliver<BrepBody> Extrude(const std::vector<NurbsCurve>& Loops, Vec3 Direction, double Length) noexcept;   // outer + holes → through-holes
    // Profile revolved by a full turn (closed profile → torus-like, open profile touching the axis → sphere-like) or a partial angle with caps.
    [[nodiscard]] static Deliver<BrepBody> Revolve(const NurbsCurve& Profile, Vec3 AxisOrigin, Vec3 AxisDirection, double Angle) noexcept;
    [[nodiscard]] static Deliver<BrepBody> Revolve(const std::vector<NurbsCurve>& Loops, Vec3 AxisOrigin, Vec3 AxisDirection, double Angle) noexcept;
    // Thicken a closed surface body into a solid shell of half-thickness t (or full thickness 2t when --both). Each face is
    //    duplicated, the original moved −t·n and the copy +t·n, then the duplicate's natural boundary is re-stitched to the
    //    original's. For a single NurbsSurface the body becomes a slab. A planar face can use --plane to give the normal.
    [[nodiscard]] static Deliver<BrepBody> Solidify(const BrepBody& Shell, double HalfThickness) noexcept;
    // Planar-setback chamfer along one body edge. The two adjacent faces are walked, the edge is replaced by a chamfer
    //    plane that cuts the edge by SetBack perpendicular to the edge on each side, the two faces are trimmed by the
    //    parallel set-back lines, and a new planar face is added between them. Refuses non-manifold edges, edges whose
    //    adjacent faces are not both planar, and edges where the chamfer would self-intersect (the two set-back lines
    //    cross before reaching the next vertex). The new face's normal points outward.
    [[nodiscard]] Deliver<BrepBody> ChamferEdge(int Edge, double SetBack, double Tolerance = ScalarCriteria::MergeTolerance) const noexcept;

    //---------------------------------------------- editing ----------------------------------------------
    // Add planar faces on every open boundary loop that lies in a plane. Returns the number of caps added.
    int  Capped(double Tolerance = ScalarCriteria::MergeTolerance) noexcept;
    // Propagate a consistent orientation across shared edges, then flip everything if the volume is negative.
    bool Orient() noexcept;
    void FlipFace(int Face) noexcept;
    [[nodiscard]] BrepBody Transformed(const Mat4& M) const noexcept;

    //---------------------------------------------- queries ----------------------------------------------
    [[nodiscard]] BodyClassification    Classification() const noexcept;
    [[nodiscard]] BodyReport  Validate() const noexcept;
    [[nodiscard]] Box3        Bounds() const noexcept;
    [[nodiscard]] Vec3        FaceNormal(int Face, double U, double V) const noexcept;  // honours Reversed
    [[nodiscard]] double      SignedVolume() const noexcept;                            // divergence theorem over the face tessellations
    [[nodiscard]] double      Area() const noexcept;
    // Coedge geometry in traversal order (start → end as the loop walks it).
    [[nodiscard]] Vec3        CoedgeStart(int Coedge) const noexcept;
    [[nodiscard]] Vec3        CoedgeEnd(int Coedge) const noexcept;
    [[nodiscard]] NurbsCurve  CoedgeCurve(int Coedge) const noexcept;
    // Open boundary loops (edges with a single coedge) chained head to tail; each entry is a list of coedge indices.
    [[nodiscard]] std::vector<std::vector<int>> OpenLoops(double Tolerance = ScalarCriteria::MergeTolerance) const noexcept;

    // (u,v) polyline of one coedge on its face in traversal order: the stored Trace, else the iso-side of a natural face,
    //    else a projection of the edge samples onto the surface. Parameters (edge curve parameters) are returned alongside.
    [[nodiscard]] std::vector<Vec2> CoedgeTrace(int Coedge, std::vector<double>* Parameters = nullptr, int Samples = 24) const noexcept;
    // Triangles of one face: lattice tessellation for natural faces, ear-clipped loop polygon for trimmed planar faces,
    //    ear-clipped (u,v) polygon refined onto the surface for trimmed curved faces.
    struct FaceTriangles
    {
        std::vector<Vec3>     Positions;                                                // [m]
        std::vector<Vec3>     Normals;                                                  // [-] face normals (Reversed applied)
        std::vector<Vec2>     Parameters;                                               // [-]
        std::vector<uint32_t> Triangles;                                                // [-] CCW seen from +normal
    };
    [[nodiscard]] FaceTriangles TessellateFace(int Face, double ChordTolerance = ScalarCriteria::ChordTolerance) const noexcept;
    // Polyline of an edge for drawing / picking.
    [[nodiscard]] std::vector<Vec3> EdgePolyline(int Edge, double ChordTolerance = 2e-3) const noexcept;

    // Building blocks (public so verification can exercise them).
    int  AddVertex(Vec3 P, double Tolerance) noexcept;                                  // merges with an existing vertex within tolerance
    int  AddEdge(NurbsCurve Curve, double Tolerance) noexcept;                          // merges with a coincident existing edge (either sense)
    int  AddCoedge(int Edge, bool Reversed, int Face, int Loop) noexcept;
    int  AddFace(NurbsSurface Surface) noexcept;                                        // face with no loops yet
    int  AddLoop(int Face, bool Outer) noexcept;
    // Appends the natural boundary loop(s) of a face's surface; merges edges/vertices within tolerance.
    void AddNaturalBoundary(int Face, double Tolerance) noexcept;

private:
    [[nodiscard]] int FindCoincidentEdge(const NurbsCurve& Curve, double Tolerance, bool& ReversedOut) const noexcept;
};

// Ear clipping of a simple polygon (with optional holes) lying in a plane; returns index triples into the input
//    points, CCW with respect to Normal. Exposed for verification and for the 2D boolean phase.
[[nodiscard]] std::vector<uint32_t> TriangulatePlanarPolygon(const std::vector<Vec3>& Points, const std::vector<std::vector<uint32_t>>& Rings, Vec3 Normal) noexcept;
// The same on 2D points (first ring outer, the rest holes; any input sense). Triangles come out CCW in the plane.
[[nodiscard]] std::vector<uint32_t> TriangulatePolygon(const std::vector<Vec2>& Points, const std::vector<std::vector<uint32_t>>& Rings) noexcept;
// Cells of a planar arrangement: directed edges (From, To) whose left side is material; interior edges must be given in
//    both directions. Returns cells as rings of edge indices (outer ring first, CCW; hole rings CW).
struct PlanarEdge { int From = -1, To = -1; };
[[nodiscard]] std::vector<std::vector<std::vector<int>>> PlanarCells(const std::vector<Vec2>& Points, const std::vector<PlanarEdge>& Edges) noexcept;
// Point-in-polygon on a (u,v) ring (even-odd rule).
[[nodiscard]] bool InsideRing(const std::vector<Vec2>& Ring, Vec2 P) noexcept;
// Newton projection of a point onto a surface from a (u,v) seed; returns the distance reached. Seams and wraps are honoured.
double SeededParameter(const NurbsSurface& S, Vec3 P, double& U, double& V, int Iterations = 30) noexcept;

// Split a curve at tangent discontinuities (interior knots of full multiplicity with a bent tangent).
[[nodiscard]] std::vector<NurbsCurve> SplitAtKinks(const NurbsCurve& Curve, double AngleTolerance = ScalarCriteria::Radians(1.0)) noexcept;

} // namespace Frontier
