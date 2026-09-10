//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Kernel/IntersectionSolver.h — surface–surface intersection (SSI) and B-rep booleans
//============================================================================================================================================
// Two solids meet along curves that lie on both boundaries. The solver finds them on the true NURBS surfaces:
//    1. seed   — coarse tessellations of the trimmed faces are intersected triangle against triangle; the segments are
//                chained per face pair into polylines that carry (u,v) on both faces,
//    2. refine — every chain point is pulled onto both surfaces by tangent-plane Newton iteration; points where a chain
//                leaves a face are solved as edge-curve ∩ surface so both neighbouring faces agree to KernelTolerance,
//    3. densify — chords are subdivided (again refined) until the interpolating cubic follows the true curve,
//    4. split  — every face becomes a planar arrangement in its own (u,v) domain: trimming loops (edges split at exits)
//                plus cut curves; the arrangement's cells are the face pieces,
//    5. classify — a piece touching a cut curve is inside the other body iff its inward direction across the curve points
//                against the other face's normal (exact, local); the rest follow by flooding across shared edges, and
//                untouched hulls by a parity ray against the other body,
//    6. assemble — union / subtract / intersect choose pieces (subtraction flips the tool's) and share edges and vertices
//                so the result is again a closed, consistently wound B-rep with (u,v) trims on every coedge.
//    Limits: intersections must be transversal — coincident or tangent faces are refused rather than guessed.
#pragma once

#include "TopologySpecification.h"

namespace Frontier
{

enum class BodyOperation : uint8_t { Union, Subtract, Intersect };
[[nodiscard]] const char* Describe(BodyOperation Operation) noexcept;

struct IntersectionCurve
{
    NurbsCurve        Curve;                                                            // [-] cubic interpolant through Points
    std::vector<Vec3> Points;                                                           // [m] on both surfaces to KernelTolerance
    std::vector<Vec2> TraceA;                                                           // [-] (u,v) on FaceA, same order as Points
    std::vector<Vec2> TraceB;                                                           // [-] (u,v) on FaceB
    int    FaceA = -1;                                                                  // [-]
    int    FaceB = -1;                                                                  // [-]
    bool   Closed = false;                                                              // [-]
    double Deviation = 0.0;                                                             // [m] largest |Sa − Sb| over the points (should be ~1e-12)
};

struct BooleanReport
{
    int Curves = 0;                                                                     // [-] intersection curve pieces
    int PiecesA = 0, PiecesB = 0;                                                       // [-] face pieces after splitting
    int KeptA = 0, KeptB = 0;                                                           // [-] pieces that survived the operation
    int InsideA = 0, InsideB = 0;                                                       // [-] pieces of A inside B / of B inside A
};

class IntersectionSolver
{
public:
    // Intersection curves between the boundaries of two bodies (each piece runs between face boundaries or closes on itself).
    [[nodiscard]] static std::vector<IntersectionCurve> Intersect(const BrepBody& A, const BrepBody& B) noexcept;
    // A ∪ B, A − B, A ∩ B of two closed solids. Refuses coincident faces and empty results.
    [[nodiscard]] static Deliver<BrepBody> Combine(const BrepBody& A, const BrepBody& B, BodyOperation Operation, BooleanReport* Report = nullptr) noexcept;
    // Parity point-in-solid test against the body's tessellation (used for untouched hulls and exposed for verification).
    [[nodiscard]] static bool Encloses(const BrepBody& Body, Vec3 P) noexcept;
    // Trace the marching to stderr (console `--verbose`, verification triage).
    static inline bool Verbose = false;
};

} // namespace Frontier
