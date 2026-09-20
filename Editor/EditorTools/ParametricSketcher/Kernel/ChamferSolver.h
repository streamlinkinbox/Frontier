//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Kernel/ChamferSolver.h — Exact planar chamfer of one straight edge or of a planar face's rim, by topology
//============================================================================================================================================
// A chamfer here is an Euler operation, not a Boolean: the edge (or every edge of one face's rim) is replaced by a
//    planar face whose two long sides are the set-back lines in the adjacent faces, and the neighbours are re-trimmed
//    along those lines. Nothing is intersected numerically, so the result is exact for any dihedral angle — the case
//    that defeated the cutter route (a 120° prism edge came back genus 1 with a 90° wedge volume, unrefused).
//
//    The bounded domain is the polyhedral B-rep SolidArc builds exactly:
//      • the chamfered edge is straight, convex, and shared by two planar faces;
//      • each of its end vertices is three-valent, its third face planar: the chamfer plane cuts the two other edges
//        there strictly inside their length and the short edge between the cuts lies in that third face;
//      • for a rim, every rim vertex's outgoing side edge must be cut at ONE point by both neighbouring chamfer
//        planes (true for right prisms and boxes, where the mitre is a single edge); otherwise the corner would need
//        its own vertex face and the route refuses rather than approximating.
//    Concave edges (a chamfer there adds material), curved edges and faces, faces with holes and higher-valence
//    vertices refuse with the source untouched. Every result is validated closed/manifold/oriented and its volume is
//    checked against the exact removed wedge (three tetrahedra per edge).
#pragma once

#include "Kernel/TopologySpecification.h"
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  CHAMFER SOLVER
//------------------------------------------------------------------------------------------------------------------------

class ChamferSolver
{
public:
    // One straight convex edge between two planar faces; equal set-back measured in each face perpendicular to the edge.
    [[nodiscard]] static Deliver<BrepBody> ChamferEdge(const BrepBody& Body, int Edge, double SetBack) noexcept;
    // Every edge of a planar face's single rim at once — the bevel of a cap — with mitred corners.
    [[nodiscard]] static Deliver<BrepBody> ChamferFaceRim(const BrepBody& Body, int Face, double SetBack) noexcept;
    // Dispatch: one edge → ChamferEdge; exactly the rim of one planar face → ChamferFaceRim; anything else refuses.
    [[nodiscard]] static Deliver<BrepBody> ChamferEdges(const BrepBody& Body, const std::vector<int>& Edges, double SetBack) noexcept;
    // The face whose single loop uses exactly this edge set, or −1 (−2 when two faces match).
    [[nodiscard]] static int RimFace(const BrepBody& Body, const std::vector<int>& Edges) noexcept;
};

} // namespace Frontier
