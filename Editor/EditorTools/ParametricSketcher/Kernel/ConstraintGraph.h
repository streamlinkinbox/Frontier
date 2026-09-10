//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Kernel/ConstraintGraph.h — Phase 18: persistent constraint graph state
//============================================================================================================================================
// ConstraintGraph is the persistent state that lives in the ConsoleHost (alongside Dimensions). It holds the
//    user's constraint network: which 2D points on the workplane are unknowns, which constraints relate them,
//    and a mapping back to the figure Blueprints that own the (x, y) values. When the user invokes
//    `constraint solve` (or implicitly via a `dim edit` on a constrained figure), the graph is materialised
//    into a ConstraintSolver, solved, and the results written back into the figures' Blueprints, which
//    triggers a rebuild.
//
// The graph is purely 2D. Projecting a 3D figure point to the workplane's 2D basis is the host's job
//    (ToLocal); we trust the host to give us consistent 2D values. The graph never sees Z.
#pragma once

#include "ConstraintSolver.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

// A single entry in the graph. We store the constraint and a human-readable note. The unknowns are
//    keyed by PointRef, which is part of the ConstraintSolver model.
struct ConstraintEntry
{
    uint32_t                       Id = 0;                                        // [-] unique within the graph
    Constraint                     C;                                             // [-] the underlying constraint
    std::string                    Note;                                          // [-] "distance L1.start L1.end = 3.0"
};

// A 2D anchor: a named point on the workplane. We track the figure + Blueprint slot so we can
//    write the solved (x, y) back into the figure. Slot names the Blueprint field:
//      0 = A (line start / polyline first / rect corner0 / circle centre)
//      1 = B (line end / polyline second / rect corner2 / circle radius point)
//      2 = C (arc 3rd point / polyline 3rd / etc.)
//      3 = PolylinePoints[K] (sentinel: slot = 3 + K*3, sub-index = 0/1/2 for X/Y/Z)
//      4 = Rectangle corner index (separate field)
struct ConstraintAnchor
{
    PointRef        Ref;                                                           // [-] which point in the solver
    std::string     Figure;                                                        // [-] figure name in the scene
    int             Slot          = 0;                                            // [-] blueprint slot index
    int             SubIndex      = 0;                                            // [-] for polyline: vertex index; for rect: corner index
    int             Component     = 0;                                            // [-] 0 = X, 1 = Y
    bool            Fixed         = false;                                        // [-] pinned (does not move)
};

class ConstraintGraph
{
public:
    // ---- constraint management ---------------------------------------------------------
    uint32_t AddConstraint(const Constraint& C, const std::string& Note) noexcept;
    void     RemoveConstraint(uint32_t Id) noexcept;
    void     Clear() noexcept { Entries.clear(); Anchors.clear(); }
    [[nodiscard]] size_t ConstraintCount() const noexcept { return Entries.size(); }
    [[nodiscard]] const std::vector<ConstraintEntry>& AllConstraints() const noexcept { return Entries; }
    [[nodiscard]] const std::vector<ConstraintAnchor>& AllAnchors() const noexcept { return Anchors; }

    // ---- anchor management -------------------------------------------------------------
    // Add a 2D anchor if one doesn't already exist for the given PointRef. Returns the
    //    index into Anchors. The figure name + slot fields are filled in by the caller.
    size_t AddAnchor(const PointRef& Ref, const std::string& Figure, int Slot, int SubIndex, int Component) noexcept;
    [[nodiscard]] size_t AnchorIndex(const PointRef& Ref) const noexcept;

    // ---- fixing (pinning) --------------------------------------------------------------
    void SetFixed(const PointRef& Ref, bool Fixed) noexcept;

    // ---- query -------------------------------------------------------------------------
    [[nodiscard]] bool Empty() const noexcept { return Entries.empty(); }
    [[nodiscard]] int  NextId() const noexcept { return int(NextId_); }

private:
    std::vector<ConstraintEntry> Entries;                                          // [-] the constraints
    std::vector<ConstraintAnchor> Anchors;                                          // [-] the 2D anchors (point refs that are constrained)
    uint32_t                     NextId_ = 1;                                       // [-] monotonic id allocator
};

} // namespace Frontier
