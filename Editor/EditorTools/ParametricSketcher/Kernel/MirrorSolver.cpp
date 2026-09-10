//--------------------------------------------------------------------------------------------------------------------------------------//
// 📦 Editor/EditorTools/ParametricSketcher/Kernel/MirrorSolver.cpp — Phase 19: pure 3D reflection + radial rotation math                                     //
//--------------------------------------------------------------------------------------------------------------------------------------//
// See MirrorSolver.h. Hand-rolled, no third-party deps. The math is direct: each function is a 3D vector op with no branching beyond the
//    zero-direction guard.
#include "MirrorSolver.h"
#include <cmath>

namespace Frontier
{

Vec3 ReflectAcrossPlane(Vec3 P, Vec3 PlaneOrigin, Vec3 PlaneNormal) noexcept
{
    Vec3 N = PlaneNormal.Normalised();
    if (N.LengthSquared() < 1e-30) return P;                                    // degenerate plane; return input
    Vec3 D = P - PlaneOrigin;
    double Dist = D.Dot(N);
    return P - N * (2.0 * Dist);
}

Vec3 ReflectAcrossAxis(Vec3 P, MirrorAxis Axis) noexcept
{
    Vec3 D = Axis.Direction.Normalised();
    if (D.LengthSquared() < 1e-30) return P;                                    // degenerate axis; return input
    Vec3 V = P - Axis.Origin;
    double ParallelLen = V.Dot(D);
    Vec3 Parallel = D * ParallelLen;
    Vec3 Perp = V - Parallel;
    return Axis.Origin + Parallel - Perp;                                      // P' = O + parallel - perpendicular
}

Vec3 RotateAroundAxis(Vec3 P, MirrorAxis Axis, double ThetaRadians) noexcept
{
    Vec3 D = Axis.Direction.Normalised();
    if (D.LengthSquared() < 1e-30) return P;                                    // degenerate axis
    Vec3 V = P - Axis.Origin;
    double ParallelLen = V.Dot(D);
    Vec3 Parallel = D * ParallelLen;
    Vec3 Perp = V - Parallel;
    // Rodrigues: P' = O + parallel + cos(θ)·perp + sin(θ)·(D × perp)
    Vec3 Cross = D.Cross(Perp);
    double C = std::cos(ThetaRadians);
    double S = std::sin(ThetaRadians);
    return Axis.Origin + Parallel + Perp * C + Cross * S;
}

Vec3 ComposeMirrors(Vec3 P, const MirrorOp* Ops, size_t Count) noexcept
{
    Vec3 Q = P;
    for (size_t I = 0; I < Count; ++I)
    {
        const MirrorOp& Op = Ops[I];
        if (Op.Kind == MirrorOp::Kind::Plane) Q = ReflectAcrossPlane(Q, Op.Origin, Op.NormalOrDir);
        else                                    Q = ReflectAcrossAxis(Q, { Op.Origin, Op.NormalOrDir });
    }
    return Q;
}

} // namespace Frontier
