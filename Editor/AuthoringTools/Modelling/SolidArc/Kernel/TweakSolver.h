//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Kernel/TweakSolver.h — Translate one face, edge or vertex of a solid on fixed topology
//============================================================================================================================================
// A tweak moves a set of body vertices by one vector and re-fits only the geometry that touches them; no vertex, edge,
//    loop or face is created or destroyed, so closure and manifoldness are structural rather than something a Boolean
//    has to rediscover. The bounded domain is the polyhedral B-rep SolidArc already builds exactly:
//
//      • every edge touching a moved vertex is a straight line and is rebuilt between its (moved) vertices;
//      • a face whose vertices all move is translated rigidly (any surface type);
//      • a natural four-sided face (degree 1×1, four poles — box faces, extrusion sides, loft rectangles) has its poles
//        set to its corners: it stays a Plane when the corners remain coplanar, otherwise it becomes a bilinear patch,
//        which is exact but only accepted when the caller opts into warping;
//      • a trimmed planar face (extrusion and prism caps, n-gons) keeps its plane and its coedge traces are refitted,
//        provided every moved vertex stays in that plane — an n-gon cannot warp, so otherwise the tweak refuses.
//
// What that means for the questions a modeller asks: translating a box face by ANY vector keeps all six faces planar
//    (the neighbours become parallelograms); translating a box edge keeps the two faces along it planar and warps the
//    two end faces unless the vector has no component along the edge; translating a box vertex warps its three faces.
//    The tweak never detects a face passing through another; it refuses only an inverted or degenerate result.
#pragma once

#include "Kernel/TopologySpecification.h"
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  TWEAK SOLVER
//------------------------------------------------------------------------------------------------------------------------

class TweakSolver
{
public:
    // The vertices a face / an edge / a vertex selection moves (unique, loop order for a face).
    [[nodiscard]] static std::vector<int> FaceVertices(const BrepBody& Body, int Face) noexcept;
    [[nodiscard]] static std::vector<int> EdgeVertices(const BrepBody& Body, int Edge) noexcept;

    // Natural four-sided faces that would leave their plane if the vertices moved by Delta. Empty ⇒ the tweak keeps
    //    every face planar. (Trimmed planar faces that would leave their plane are refusals, not warps.)
    [[nodiscard]] static std::vector<int> WarpedFaces(const BrepBody& Body, const std::vector<int>& Vertices, Vec3 Delta) noexcept;

    // Translate the vertex set by Delta on a closed solid. AllowWarp admits bilinear (non-planar) four-sided faces.
    [[nodiscard]] static Deliver<BrepBody> TranslateVertices(const BrepBody& Body, const std::vector<int>& Vertices, Vec3 Delta,
                                                            bool AllowWarp) noexcept;
    [[nodiscard]] static Deliver<BrepBody> TranslateFace(const BrepBody& Body, int Face, Vec3 Delta, bool AllowWarp) noexcept;
    [[nodiscard]] static Deliver<BrepBody> TranslateEdge(const BrepBody& Body, int Edge, Vec3 Delta, bool AllowWarp) noexcept;
    [[nodiscard]] static Deliver<BrepBody> TranslateVertex(const BrepBody& Body, int Vertex, Vec3 Delta, bool AllowWarp) noexcept;

    // Uniformly rotate or scale one planar face about its vertex centroid while retaining the original topology. Adjacent
    // natural quads are re-fitted; a resulting bilinear warp requires AllowWarp. Curved faces and non-planar trim edits
    // refuse rather than silently approximate their surfaces.
    [[nodiscard]] static Deliver<BrepBody> RotateFace(const BrepBody& Body, int Face, Vec3 Axis, double Angle,
                                                      bool AllowWarp = false) noexcept;
    [[nodiscard]] static Deliver<BrepBody> ScaleFace(const BrepBody& Body, int Face, double Factor,
                                                     bool AllowWarp = false) noexcept;
};

} // namespace Frontier
