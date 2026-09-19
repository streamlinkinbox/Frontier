//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Kernel/ConstraintSolver.h — Phase 18: 2D constraint solver (doF analysis + Newton)
//============================================================================================================================================
// A constraint solver for 2D sketch geometry. The unknowns are 2D point coordinates on the workplane; each
//    constraint is a scalar equation f(unknowns) = 0 (or = prescribed value for distance / angle). The system is
//    over-determined when #constraints > 2N, well-determined when = 2N, under-determined when < 2N, where N is
//    the number of distinct 2D points mentioned by any constraint. The Newton step is J^T J dx = -J^T r, with
//    line-search backtracking for convergence robustness.
//
// Constraint types (9 total):
//    Distance (P1, P2) = d       — distance between two named points equals d
//    Angle (L1, L2) = θ°         — angle between two lines equals θ
//    Coincident (P1, P2)         — x1 = x2, y1 = y2 (two scalar rows)
//    Horizontal (P1, P2)         — y1 = y2
//    Vertical (P1, P2)           — x1 = x2
//    Parallel (L1, L2)           — cross(direction(L1), direction(L2)) = 0
//    Perpendicular (L1, L2)      — dot(direction(L1), direction(L2)) = 0
//    EqualLength (L1, L2)        — length(L1) - length(L2) = 0
//    EqualRadius (C1, C2)        — radius(C1) - radius(C2) = 0
//
// Each constraint has an analytic Jacobian row (∂f/∂x_i, ∂f/∂y_i) for the unknowns it references. There is no
//    automatic differentiation — every gradient is hand-derived (see ConstraintSolver.cpp).
//
// The solver is intentionally simple: dense matrix, no sparsity, no incrementality. For sketch-scale problems
//    (< 50 unknowns) the solve is O(N^3) per iteration, taking microseconds. Production CAD solvers use sparse
//    Levenberg-Marquardt; we don't need that here.
#pragma once

#include "VectorSpecification.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

// A 2D point is a Vec2 (x, y). We use this throughout the constraint solver.
using SketchPoint = Vec2;

// A named reference to a point in a sketch: it identifies which point of which figure. The kind tells us where
//    the point lives in the figure's Blueprint; the index disambiguates when a figure has multiple points.
enum class PointRefKind : uint8_t
{
    // Curve endpoints
    LineStart,             // [-] first endpoint of a Line curve
    LineEnd,               // [-] second endpoint of a Line curve
    // Polyline vertices (index into the polyline)
    PolylineVertex,        // [-] vertex K of a polyline (set Index)
    // Circle / arc / ellipse
    CircleCentre,          // [-] centre of a Circle or Arc
    CircleRadiusPoint,     // [-] any point on the circle's circumference (the first pole)
    // Rectangle corners
    RectCorner,            // [-] corner K (0..3) of a Rectangle
};
struct PointRef
{
    std::string  Figure;                                                       // [-] figure name
    PointRefKind Kind = PointRefKind::LineStart;                              // [-] which point on the figure
    int          Index = 0;                                                   // [-] index (for PolylineVertex, RectCorner)
    [[nodiscard]] bool operator==(const PointRef& Other) const noexcept
    {
        return Figure == Other.Figure && Kind == Other.Kind && Index == Other.Index;
    }
};

// A named line reference: takes two points (start, end) of the figure.
struct LineRef
{
    std::string Figure;                                                       // [-] figure name
    bool        UseCentreAndPoint = false;                                    // [-] for circles/arcs: the line is from the centre to a point on the circumference
    [[nodiscard]] PointRef Start() const noexcept { PointRef P; P.Figure = Figure; P.Kind = UseCentreAndPoint ? PointRefKind::CircleCentre : PointRefKind::LineStart; return P; }
    [[nodiscard]] PointRef End() const noexcept { PointRef P; P.Figure = Figure; P.Kind = UseCentreAndPoint ? PointRefKind::CircleRadiusPoint : PointRefKind::LineEnd; return P; }
};

// A named circle reference (for EqualRadius).
struct CircleRef
{
    std::string Figure;                                                       // [-] figure name (must be a Circle or Arc)
};

// 2D constraint type.
enum class ConstraintType : uint8_t
{
    Distance = 0, Angle, Coincident, Horizontal, Vertical, Parallel, Perpendicular, EqualLength, EqualRadius
};

// A single constraint: one row in the residual vector, one (or two for Coincident) row(s) in the Jacobian.
struct Constraint
{
    uint32_t        Id        = 0;                                            // [-] unique within the host
    ConstraintType  Type      = ConstraintType::Distance;                     // [-] which equation
    PointRef        P1{}, P2{};                                               // [-] primary references (used by most types)
    PointRef        P3{}, P4{};                                               // [-] secondary references (used by Angle, Parallel, Perpendicular, EqualLength)
    CircleRef       C1{}, C2{};                                               // [-] circle references (used by EqualRadius)
    double          Prescribed = 0.0;                                          // [m] or [rad] the prescribed value (Distance, Angle)
    bool            Active    = true;                                         // [-] soft-disable a constraint without deleting it
    std::string     Note;                                                      // [-] human-readable description (e.g. "distance P1 P2 = 5.0")
};

// Result of a solve: the new unknown values + statistics.
struct SolveReport
{
    bool        Converged   = false;                                          // [-] did the Newton step reach the tolerance?
    int         Iterations  = 0;                                              // [-] Newton iterations
    double      ResidualL2  = 0.0;                                            // [-] |r|_2 at the end (smaller is better)
    int         DoF         = 0;                                              // [-] degrees of freedom after the solve (2*N - rank(J))
    std::string Refusal;                                                      // [-] if not converged, why
    int         SingularRank = -1;                                            // [-] if a singular Jacobian was detected, the rank we found
};

// One unknown in the system: a 2D point on the workplane. The solver owns the (x, y) values; the caller writes
//    them back into the figure's Blueprint at the end.
struct SketchUnknown
{
    PointRef Ref;                                                             // [-] which point this is (kind + figure + index)
    double   X = 0.0;                                                          // [m]
    double   Y = 0.0;                                                          // [m]
    bool     Fixed = false;                                                    // [-] pinned — never moves during the solve
};

// The solver state.
class ConstraintSolver
{
public:
    // Build the unknown list from a list of figure names. The caller pre-populates the unknowns' (X, Y) from
    //    the figures' current Blueprint values. We then add constraints and call Solve.
    void SetUnknown(const PointRef& Ref, double X, double Y, bool Fixed = false) noexcept;
    void SetUnknown(const SketchUnknown& U) noexcept { Unknowns.push_back(U); }
    [[nodiscard]] size_t UnknownCount() const noexcept { return Unknowns.size(); }
    [[nodiscard]] const std::vector<SketchUnknown>& AllUnknowns() const noexcept { return Unknowns; }
    [[nodiscard]] std::vector<SketchUnknown>& AllUnknowns() noexcept { return Unknowns; }

    // Add a constraint. The Id is assigned by the host (not by the solver). Returns a human-readable note.
    void AddConstraint(const Constraint& C) noexcept { Constraints.push_back(C); }
    [[nodiscard]] size_t ConstraintCount() const noexcept { return Constraints.size(); }
    [[nodiscard]] const std::vector<Constraint>& AllConstraints() const noexcept { return Constraints; }
    void RemoveConstraint(uint32_t Id) noexcept;
    void Clear() noexcept { Unknowns.clear(); Constraints.clear(); }

    // Find the index of an unknown by ref, or SIZE_MAX.
    [[nodiscard]] size_t IndexOf(const PointRef& Ref) const noexcept;

    // Compute the residual + Jacobian. Returns the number of constraint rows (a Coincident contributes 2).
    //    `Residuals` and `Jacobian` must be pre-allocated (rows = constraint row count, cols = 2 * unknowns).
    int  Evaluate(std::vector<double>& Residuals, std::vector<std::vector<double>>& Jacobian) const noexcept;

    // Run the Newton solve. Updates Unknowns[*].X, Y in place. Returns a SolveReport.
    SolveReport Solve(int MaxIterations = 50, double Tolerance = 1e-9, double LineSearchStep = 0.5) noexcept;

    // Degrees of freedom: 2N - rank(J). Evaluates the Jacobian at the current state, does an SVD-free rank
    //    estimate (Gaussian elimination with a tiny pivot), returns the dof. Negative means over-constrained.
    [[nodiscard]] int DoF() const noexcept;

private:
    std::vector<SketchUnknown> Unknowns;                                       // [-] the unknown list
    std::vector<Constraint>    Constraints;                                    // [-] the constraint list

    // Solve a small dense linear system in place (Gaussian elimination with partial pivoting). On entry
    //    A is M×N, b is M. On exit A is the row-echelon form, b is the solution if M = N + 0.
    //    For over-determined systems (more equations than unknowns) we use least-squares via the normal
    //    equations A^T A x = A^T b. We return the rank of A^T A in *OutRank.
    void SolveLinearSystem(std::vector<std::vector<double>>& A, std::vector<double>& B, std::vector<double>& OutX, int* OutRank) noexcept;
};

} // namespace Frontier
