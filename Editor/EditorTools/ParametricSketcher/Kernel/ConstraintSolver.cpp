//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Kernel/ConstraintSolver.cpp — Phase 18: 2D constraint solver
//============================================================================================================================================
// See ConstraintSolver.h for the design. This file is hand-rolled math — no Eigen, no third-party deps — because
//    the problem is small (< 50 unknowns) and the dense Gaussian elimination is microseconds. Every gradient is
//    hand-derived; the residual for each constraint type is a small analytic function of the unknowns.
#include "ConstraintSolver.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{
    // A small dense matrix: 2D vector of doubles, contiguous storage, A[i][j] with i < rows, j < cols.
    using Matrix = std::vector<std::vector<double>>;

    // Multiply A^T * b where A is M×N, b is M; result is N. Used to form the normal equations A^T A x = A^T b.
    std::vector<double> MatVecAT(const Matrix& A, const std::vector<double>& B) noexcept
    {
        const size_t M = A.size();
        const size_t N = M ? A[0].size() : 0;
        std::vector<double> Out(N, 0.0);
        for (size_t I = 0; I < M; ++I)
        {
            const auto& Row = A[I];
            for (size_t J = 0; J < N; ++J) Out[J] += Row[J] * B[I];
        }
        return Out;
    }

    // Multiply A^T * A where A is M×N; result is N×N. Used to form the normal equations.
    Matrix MatMulATA(const Matrix& A) noexcept
    {
        const size_t M = A.size();
        const size_t N = M ? A[0].size() : 0;
        Matrix Out(N, std::vector<double>(N, 0.0));
        for (size_t I = 0; I < M; ++I)
        {
            const auto& Row = A[I];
            for (size_t J = 0; J < N; ++J)
            {
                const double Rij = Row[J];
                for (size_t K = 0; K < N; ++K) Out[J][K] += Rij * Row[K];
            }
        }
        return Out;
    }

    // Solve N×N linear system via Gaussian elimination with partial pivoting. In-place: A and b are
    //    destroyed; x is the solution. Returns the rank (number of pivots above the pivot tolerance).
    int SolveNxN(Matrix& A, std::vector<double>& B, std::vector<double>& X) noexcept
    {
        const size_t N = A.size();
        if (N == 0) { X.clear(); return 0; }
        X.assign(N, 0.0);
        // In-place LU with partial pivoting on A.
        const double PivotTol = 1e-12;
        int Rank = 0;
        for (size_t K = 0; K < N; ++K)
        {
            // Find the row with the largest |A[i][k]| for i ≥ k.
            size_t Pivot = K;
            double MaxAbs = std::fabs(A[K][K]);
            for (size_t I = K + 1; I < N; ++I)
            {
                const double V = std::fabs(A[I][K]);
                if (V > MaxAbs) { MaxAbs = V; Pivot = I; }
            }
            if (MaxAbs < PivotTol) continue;                                     // singular; leave the row, count rank
            if (Pivot != K) { std::swap(A[Pivot], A[K]); std::swap(B[Pivot], B[K]); }
            ++Rank;
            // Eliminate below.
            const double Akk = A[K][K];
            for (size_t I = K + 1; I < N; ++I)
            {
                const double Factor = A[I][K] / Akk;
                if (Factor == 0.0) continue;
                for (size_t J = K + 1; J < N; ++J) A[I][J] -= Factor * A[K][J];
                B[I] -= Factor * B[K];
                A[I][K] = 0.0;
            }
        }
        // Back-substitution.
        for (int I = int(N) - 1; I >= 0; --I)
        {
            double Sum = B[I];
            for (size_t J = size_t(I) + 1; J < N; ++J) Sum -= A[I][J] * X[J];
            if (std::fabs(A[I][I]) < PivotTol) { X[I] = 0.0; continue; }        // singular; leave at 0
            X[I] = Sum / A[I][I];
        }
        return Rank;
    }
}

void ConstraintSolver::SetUnknown(const PointRef& Ref, double X, double Y, bool Fixed) noexcept
{
    SketchUnknown U; U.Ref = Ref; U.X = X; U.Y = Y; U.Fixed = Fixed;
    Unknowns.push_back(U);
}

size_t ConstraintSolver::IndexOf(const PointRef& Ref) const noexcept
{
    for (size_t I = 0; I < Unknowns.size(); ++I)
    {
        const auto& U = Unknowns[I];
        if (U.Ref.Figure == Ref.Figure && U.Ref.Kind == Ref.Kind && U.Ref.Index == Ref.Index) return I;
    }
    return SIZE_MAX;
}

void ConstraintSolver::RemoveConstraint(uint32_t Id) noexcept
{
    Constraints.erase(std::remove_if(Constraints.begin(), Constraints.end(), [Id](const Constraint& C) { return C.Id == Id; }), Constraints.end());
}

int ConstraintSolver::Evaluate(std::vector<double>& Residuals, std::vector<std::vector<double>>& Jacobian) const noexcept
{
    const size_t N = Unknowns.size();
    const size_t Cols = 2 * N;
    // First pass: count rows. Coincident contributes 2 rows; everything else 1.
    size_t Rows = 0;
    for (const Constraint& C : Constraints) if (C.Active) Rows += (C.Type == ConstraintType::Coincident ? 2 : 1);
    Residuals.assign(Rows, 0.0);
    Jacobian.assign(Rows, std::vector<double>(Cols, 0.0));
    size_t Row = 0;
    auto X = [&](const PointRef& P, size_t Idx) -> double { return Idx == SIZE_MAX ? 0.0 : Unknowns[Idx].X; };
    auto Y = [&](const PointRef& P, size_t Idx) -> double { return Idx == SIZE_MAX ? 0.0 : Unknowns[Idx].Y; };
    auto Fix = [&](const PointRef& P, size_t Idx) -> bool { return Idx == SIZE_MAX ? false : Unknowns[Idx].Fixed; };
    auto ColX = [&](size_t Idx) { return Idx == SIZE_MAX ? SIZE_MAX : 2 * Idx; };
    auto ColY = [&](size_t Idx) { return Idx == SIZE_MAX ? SIZE_MAX : 2 * Idx + 1; };
    for (const Constraint& C : Constraints)
    {
        if (!C.Active) continue;
        const size_t I1 = IndexOf(C.P1), I2 = IndexOf(C.P2);
        const size_t I3 = IndexOf(C.P3), I4 = IndexOf(C.P4);
        const double X1 = X(C.P1, I1), Y1 = Y(C.P1, I1);
        const double X2 = X(C.P2, I2), Y2 = Y(C.P2, I2);
        const double X3 = X(C.P3, I3), Y3 = Y(C.P3, I3);
        const double X4 = X(C.P4, I4), Y4 = Y(C.P4, I4);
        const size_t C1X = ColX(I1), C1Y = ColY(I1);
        const size_t C2X = ColX(I2), C2Y = ColY(I2);
        const size_t C3X = ColX(I3), C3Y = ColY(I3);
        const size_t C4X = ColX(I4), C4Y = ColY(I4);
        switch (C.Type)
        {
            case ConstraintType::Distance:
            {
                const double DX = X2 - X1, DY = Y2 - Y1;
                const double D = std::sqrt(DX * DX + DY * DY);
                if (D < 1e-12) { Residuals[Row] = 0.0; break; }                  // degenerate; report zero residual, leave Jacobian zero
                Residuals[Row] = D - C.Prescribed;
                if (C1X != SIZE_MAX)
                {
                    Jacobian[Row][C1X] += -DX / D;
                    Jacobian[Row][C1Y] += -DY / D;
                }
                if (C2X != SIZE_MAX)
                {
                    Jacobian[Row][C2X] += +DX / D;
                    Jacobian[Row][C2Y] += +DY / D;
                }
                break;
            }
            case ConstraintType::Coincident:
            {
                // Two rows: x1 - x2 = 0, y1 - y2 = 0.
                Residuals[Row]     = X1 - X2;
                Residuals[Row + 1] = Y1 - Y2;
                if (C1X != SIZE_MAX) Jacobian[Row][C1X]     += 1.0;
                if (C2X != SIZE_MAX) Jacobian[Row][C2X]     += -1.0;
                if (C1Y != SIZE_MAX) Jacobian[Row + 1][C1Y] += 1.0;
                if (C2Y != SIZE_MAX) Jacobian[Row + 1][C2Y] += -1.0;
                break;
            }
            case ConstraintType::Horizontal:
            {
                Residuals[Row] = Y1 - Y2;
                if (C1Y != SIZE_MAX) Jacobian[Row][C1Y] += 1.0;
                if (C2Y != SIZE_MAX) Jacobian[Row][C2Y] += -1.0;
                break;
            }
            case ConstraintType::Vertical:
            {
                Residuals[Row] = X1 - X2;
                if (C1X != SIZE_MAX) Jacobian[Row][C1X] += 1.0;
                if (C2X != SIZE_MAX) Jacobian[Row][C2X] += -1.0;
                break;
            }
            case ConstraintType::Angle:
            {
                // Angle from line P1→P2 to line P3→P4, measured in radians. We use the atan2-of-cross/dot
                //    formulation so the result is continuous over the full -π..π range. The Jacobian is the
                //    derivative of atan2(c, d) w.r.t. its arguments, applied via the chain rule to the
                //    endpoint coordinates. The result is the angle, in radians, *minus* the prescribed value.
                const double DX1 = X2 - X1, DY1 = Y2 - Y1;
                const double DX2 = X4 - X3, DY2 = Y4 - Y3;
                const double Cross = DX1 * DY2 - DY1 * DX2;
                const double Dot   = DX1 * DX2 + DY1 * DY2;
                const double Theta = std::atan2(Cross, Dot);
                Residuals[Row] = Theta - C.Prescribed;
                // d(atan2(c, d)) / dθ = (d * dc - c * dd) / (c^2 + d^2) = (Cross' * Dot - Cross * Dot') / (Denom)
                //    where Denom = c^2 + d^2.
                // We then chain through (c, d) w.r.t. (x, y) of each endpoint.
                const double Denom = Cross * Cross + Dot * Dot;
                if (Denom < 1e-12) break;                                          // parallel lines, undefined angle gradient — leave zero
                const double InvDenom = 1.0 / Denom;
                // Partial derivatives of (Cross, Dot) w.r.t. each endpoint coordinate. d(Cross)/dx1 = 0
                //    (Cross is a function of direction (dx1, dy1, dx2, dy2), not of x1 alone — wait, no,
                //    Cross = (x2-x1) * (y4-y3) - (y2-y1) * (x4-x3), so dCross/dx1 = -(y4-y3) = -DY2.
                //    dCross/dy1 = +(x4-x3) = +DX2. dCross/dx2 = +(y4-y3) = +DY2. dCross/dy2 = -(x4-x3) = -DX2.
                //    Similarly dDot/dx1 = (x4-x3) = +DX2, dDot/dy1 = (y4-y3) = +DY2, dDot/dx2 = (x2-x1) = +DX1, dDot/dy2 = (y2-y1) = +DY1.
                // dTheta/d(any unknown) = (dDot/d(...) * Cross - Dot * dCross/d(...)) * InvDenom
                //    Wait, actually dTheta/dx = d(atan2)/dCross * dCross/dx + d(atan2)/dDot * dDot/dx
                //                                  = (+Dot * InvDenom) * dCross/dx + (-Cross * InvDenom) * dDot/dx
                //    Let A = Dot * InvDenom, B = -Cross * InvDenom. So dTheta/dx = A * dCross/dx + B * dDot/dx.
                const double A = Dot * InvDenom;
                const double B = -Cross * InvDenom;
                // P1: dCross/dx1 = -DY2, dCross/dy1 = +DX2, dDot/dx1 = +DX2, dDot/dy1 = +DY2.
                if (C1X != SIZE_MAX) Jacobian[Row][C1X] += A * (-DY2) + B * (+DX2);
                if (C1Y != SIZE_MAX) Jacobian[Row][C1Y] += A * (+DX2) + B * (+DY2);
                // P2: dCross/dx2 = +DY2, dCross/dy2 = -DX2, dDot/dx2 = +DX1, dDot/dy2 = +DY1.
                if (C2X != SIZE_MAX) Jacobian[Row][C2X] += A * (+DY2) + B * (+DX1);
                if (C2Y != SIZE_MAX) Jacobian[Row][C2Y] += A * (-DX2) + B * (+DY1);
                // P3: dCross/dx3 = +DY1, dCross/dy3 = -DX1, dDot/dx3 = +DX1, dDot/dy3 = +DY1.
                if (C3X != SIZE_MAX) Jacobian[Row][C3X] += A * (+DY1) + B * (+DX1);
                if (C3Y != SIZE_MAX) Jacobian[Row][C3Y] += A * (-DX1) + B * (+DY1);
                // P4: dCross/dx4 = -DY1, dCross/dy4 = +DX1, dDot/dx4 = +DX1, dDot/dy4 = +DY1.
                if (C4X != SIZE_MAX) Jacobian[Row][C4X] += A * (-DY1) + B * (+DX1);
                if (C4Y != SIZE_MAX) Jacobian[Row][C4Y] += A * (+DX1) + B * (+DY1);
                break;
            }
            case ConstraintType::Parallel:
            {
                // Cross product of the two direction vectors = 0.
                const double DX1 = X2 - X1, DY1 = Y2 - Y1;
                const double DX2 = X4 - X3, DY2 = Y4 - Y3;
                Residuals[Row] = DX1 * DY2 - DY1 * DX2;
                // d(Cross)/d endpoint coordinates.
                if (C1X != SIZE_MAX) Jacobian[Row][C1X] += -DY2;
                if (C1Y != SIZE_MAX) Jacobian[Row][C1Y] += +DX2;
                if (C2X != SIZE_MAX) Jacobian[Row][C2X] += +DY2;
                if (C2Y != SIZE_MAX) Jacobian[Row][C2Y] += -DX2;
                if (C3X != SIZE_MAX) Jacobian[Row][C3X] += +DY1;
                if (C3Y != SIZE_MAX) Jacobian[Row][C3Y] += -DX1;
                if (C4X != SIZE_MAX) Jacobian[Row][C4X] += -DY1;
                if (C4Y != SIZE_MAX) Jacobian[Row][C4Y] += +DX1;
                break;
            }
            case ConstraintType::Perpendicular:
            {
                // Dot product of the two direction vectors = 0.
                const double DX1 = X2 - X1, DY1 = Y2 - Y1;
                const double DX2 = X4 - X3, DY2 = Y4 - Y3;
                Residuals[Row] = DX1 * DX2 + DY1 * DY2;
                if (C1X != SIZE_MAX) Jacobian[Row][C1X] += +DX2;
                if (C1Y != SIZE_MAX) Jacobian[Row][C1Y] += +DY2;
                if (C2X != SIZE_MAX) Jacobian[Row][C2X] += +DX1;
                if (C2Y != SIZE_MAX) Jacobian[Row][C2Y] += +DY1;
                if (C3X != SIZE_MAX) Jacobian[Row][C3X] += +DX1;
                if (C3Y != SIZE_MAX) Jacobian[Row][C3Y] += +DY1;
                if (C4X != SIZE_MAX) Jacobian[Row][C4X] += +DX1;
                if (C4Y != SIZE_MAX) Jacobian[Row][C4Y] += +DY1;
                break;
            }
            case ConstraintType::EqualLength:
            {
                // length(P1, P2) - length(P3, P4) = 0.
                const double DX1 = X2 - X1, DY1 = Y2 - Y1;
                const double DX2 = X4 - X3, DY2 = Y4 - Y3;
                const double L1 = std::sqrt(DX1 * DX1 + DY1 * DY1);
                const double L2 = std::sqrt(DX2 * DX2 + DY2 * DY2);
                Residuals[Row] = L1 - L2;
                if (L1 > 1e-12)
                {
                    if (C1X != SIZE_MAX) Jacobian[Row][C1X] += -DX1 / L1;
                    if (C1Y != SIZE_MAX) Jacobian[Row][C1Y] += -DY1 / L1;
                    if (C2X != SIZE_MAX) Jacobian[Row][C2X] += +DX1 / L1;
                    if (C2Y != SIZE_MAX) Jacobian[Row][C2Y] += +DY1 / L1;
                }
                if (L2 > 1e-12)
                {
                    if (C3X != SIZE_MAX) Jacobian[Row][C3X] -= -DX2 / L2;          // d(-L2)/dx3 = -dL2/dx3 = -(-DX2/L2) = +DX2/L2
                    if (C3Y != SIZE_MAX) Jacobian[Row][C3Y] -= -DY2 / L2;
                    if (C4X != SIZE_MAX) Jacobian[Row][C4X] -= +DX2 / L2;
                    if (C4Y != SIZE_MAX) Jacobian[Row][C4Y] -= +DY2 / L2;
                }
                break;
            }
            case ConstraintType::EqualRadius:
            {
                // We treat EqualRadius as: the Blueprint.R0 of C1 and C2 are equal. The unknowns don't include
                //    a "radius" variable directly — instead, we use the distance from C1.centre to C1.radiusPoint
                //    and from C2.centre to C2.radiusPoint. C1 and C2 are encoded in P1, P2 (centre) and P3, P4 (point).
                //    Wait, the user passes CircleRef C1 and C2; we use the LineRef Start/End of those circles to
                //    get (centre, point). For simplicity, the caller must pass C1 and C2 as the P1 (centre) and
                //    P3 (point-on-circle) refs. EqualRadius then reads as: |P2 - P1| - |P4 - P3| = 0. The caller
                //    is responsible for setting up the point refs to be the centre and a circumference point.
                //    We use a slightly different signature: the residual is "distance(P1, P2) - distance(P3, P4)".
                //    This way, both unknowns participate naturally and the Jacobian is the same as EqualLength.
                const double DX1 = X2 - X1, DY1 = Y2 - Y1;
                const double DX2 = X4 - X3, DY2 = Y4 - Y3;
                const double L1 = std::sqrt(DX1 * DX1 + DY1 * DY1);
                const double L2 = std::sqrt(DX2 * DX2 + DY2 * DY2);
                Residuals[Row] = L1 - L2;
                if (L1 > 1e-12)
                {
                    if (C1X != SIZE_MAX) Jacobian[Row][C1X] += -DX1 / L1;
                    if (C1Y != SIZE_MAX) Jacobian[Row][C1Y] += -DY1 / L1;
                    if (C2X != SIZE_MAX) Jacobian[Row][C2X] += +DX1 / L1;
                    if (C2Y != SIZE_MAX) Jacobian[Row][C2Y] += +DY1 / L1;
                }
                if (L2 > 1e-12)
                {
                    if (C3X != SIZE_MAX) Jacobian[Row][C3X] += +DX2 / L2;
                    if (C3Y != SIZE_MAX) Jacobian[Row][C3Y] += +DY2 / L2;
                    if (C4X != SIZE_MAX) Jacobian[Row][C4X] += -DX2 / L2;
                    if (C4Y != SIZE_MAX) Jacobian[Row][C4Y] += -DY2 / L2;
                }
                break;
            }
        }
        // Fixed unknowns: zero out their Jacobian column. This effectively pins them — they don't move.
        for (size_t I = 0; I < N; ++I)
        {
            if (Unknowns[I].Fixed)
            {
                Jacobian[Row][2 * I]     = 0.0;
                Jacobian[Row][2 * I + 1] = 0.0;
            }
        }
        Row += (C.Type == ConstraintType::Coincident ? 2 : 1);
    }
    return int(Rows);
}

SolveReport ConstraintSolver::Solve(int MaxIterations, double Tolerance, double LineSearchStep) noexcept
{
    SolveReport R;
    const size_t N = Unknowns.size();
    if (N == 0) { R.Refusal = "no unknowns"; return R; }
    if (Constraints.empty()) { R.Refusal = "no constraints"; return R; }
    std::vector<double> Residuals;
    std::vector<std::vector<double>> Jacobian;
    int Rows = Evaluate(Residuals, Jacobian);
    // Compute the initial residual L2 norm.
    double LastL2 = 0;
    for (double V : Residuals) LastL2 += V * V;
    LastL2 = std::sqrt(LastL2);
    R.ResidualL2 = LastL2;
    if (Rows == 0) { R.Converged = true; R.Iterations = 0; R.DoF = 2 * int(N); return R; }
    // If the residual is already zero at the initial state, we're done. No Newton needed.
    if (LastL2 < 1e-12) { R.Converged = true; R.Iterations = 0; return R; }
    // Build the normal equations A^T A dx = -A^T r. For under-determined systems (rank < 2N), this still
    //    gives a minimum-norm Newton step — the system moves toward satisfying the constraints in the
    //    direction that doesn't grow the unknowns unnecessarily. We track the dof and report it; the
    //    user can pin the remaining degrees of freedom with more constraints.
    Matrix ATA = MatMulATA(Jacobian);
    std::vector<double> ATr = MatVecAT(Jacobian, Residuals);
    for (double& V : ATr) V = -V;                                              // RHS = -A^T r
    std::vector<double> Dx;
    int Rank = 0;
    SolveLinearSystem(ATA, ATr, Dx, &Rank);
    R.SingularRank = Rank;
    R.DoF = 2 * int(N) - Rank;                                                 // updated below if we get a better rank
    // Take a Newton step with line search.
    for (int Iter = 0; Iter < MaxIterations; ++Iter)
    {
        // Try a full step first.
        std::vector<SketchUnknown> Trial = Unknowns;
        for (size_t I = 0; I < N; ++I)
        {
            if (Unknowns[I].Fixed) continue;
            Trial[I].X = Unknowns[I].X + Dx[2 * I];
            Trial[I].Y = Unknowns[I].Y + Dx[2 * I + 1];
        }
        // Evaluate at the trial point. We swap so Evaluate reads the trial; then swap back so Unknowns holds
        //    the original, and we move Trial into Unknowns only if the step is good.
        std::swap(Unknowns, Trial);
        int TrialRows = Evaluate(Residuals, Jacobian);
        double TrialL2 = 0;
        for (double V : Residuals) TrialL2 += V * V;
        TrialL2 = std::sqrt(TrialL2);
        std::swap(Unknowns, Trial);                                             // Unknowns = original, Trial = stepped
        if (TrialL2 < LastL2)
        {
            // Step is good: adopt the trial.
            Unknowns = std::move(Trial);
            LastL2 = TrialL2;
            R.Iterations = Iter + 1;
            R.ResidualL2 = LastL2;
            if (LastL2 < Tolerance) { R.Converged = true; return R; }
        }
        else
        {
            // Step made it worse. Try a smaller step (backtracking).
            double Step = LineSearchStep;
            bool Found = false;
            for (int B = 0; B < 12; ++B)
            {
                Trial = Unknowns;
                for (size_t I = 0; I < N; ++I)
                {
                    if (Unknowns[I].Fixed) continue;
                    Trial[I].X = Unknowns[I].X + Dx[2 * I] * Step;
                    Trial[I].Y = Unknowns[I].Y + Dx[2 * I + 1] * Step;
                }
                std::swap(Unknowns, Trial);
                TrialRows = Evaluate(Residuals, Jacobian);
                TrialL2 = 0;
                for (double V : Residuals) TrialL2 += V * V;
                TrialL2 = std::sqrt(TrialL2);
                std::swap(Unknowns, Trial);
                if (TrialL2 < LastL2) { Unknowns = std::move(Trial); Found = true; break; }
                Step *= 0.5;
            }
            if (!Found)
            {
                R.Refusal = "Newton did not converge: residual stuck at " + std::to_string(LastL2);
                R.ResidualL2 = LastL2;
                return R;
            }
            LastL2 = TrialL2;
            R.Iterations = Iter + 1;
            R.ResidualL2 = LastL2;
            if (LastL2 < Tolerance) { R.Converged = true; return R; }
        }
        // Recompute the Newton step.
        ATA = MatMulATA(Jacobian);
        ATr = MatVecAT(Jacobian, Residuals);
        for (double& V : ATr) V = -V;
        Dx.assign(2 * N, 0.0);
        Rank = SolveNxN(ATA, ATr, Dx);
        R.DoF = 2 * int(N) - Rank;
        // Under-determined: keep going. The minimum-norm step is still useful.
    }
    R.Refusal = "Newton did not converge in " + std::to_string(MaxIterations) + " iterations (residual " + std::to_string(LastL2) + ")";
    R.ResidualL2 = LastL2;
    return R;
}

int ConstraintSolver::DoF() const noexcept
{
    const size_t N = Unknowns.size();
    if (N == 0) return 0;
    std::vector<double> Residuals;
    std::vector<std::vector<double>> Jacobian;
    int Rows = Evaluate(Residuals, Jacobian);
    (void)Rows;
    // Form A^T A and find its rank via Gaussian elimination.
    Matrix ATA = MatMulATA(Jacobian);
    std::vector<double> B(2 * N, 0.0);
    std::vector<double> X;
    int Rank = SolveNxN(ATA, B, X);
    return 2 * int(N) - Rank;
}

void ConstraintSolver::SolveLinearSystem(std::vector<std::vector<double>>& A, std::vector<double>& B, std::vector<double>& OutX, int* OutRank) noexcept
{
    const size_t N = A.size();
    if (N == 0) { OutX.clear(); if (OutRank) *OutRank = 0; return; }
    OutX.assign(N, 0.0);
    const double PivotTol = 1e-12;
    int Rank = 0;
    for (size_t K = 0; K < N; ++K)
    {
        size_t Pivot = K;
        double MaxAbs = std::fabs(A[K][K]);
        for (size_t I = K + 1; I < N; ++I)
        {
            const double V = std::fabs(A[I][K]);
            if (V > MaxAbs) { MaxAbs = V; Pivot = I; }
        }
        if (MaxAbs < PivotTol) continue;
        if (Pivot != K) { std::swap(A[Pivot], A[K]); std::swap(B[Pivot], B[K]); }
        ++Rank;
        const double Akk = A[K][K];
        for (size_t I = K + 1; I < N; ++I)
        {
            const double Factor = A[I][K] / Akk;
            if (Factor == 0.0) continue;
            for (size_t J = K + 1; J < N; ++J) A[I][J] -= Factor * A[K][J];
            B[I] -= Factor * B[K];
            A[I][K] = 0.0;
        }
    }
    for (int I = int(N) - 1; I >= 0; --I)
    {
        double Sum = B[I];
        for (size_t J = size_t(I) + 1; J < N; ++J) Sum -= A[I][J] * OutX[J];
        if (std::fabs(A[I][I]) < PivotTol) { OutX[I] = 0.0; continue; }
        OutX[I] = Sum / A[I][I];
    }
    if (OutRank) *OutRank = Rank;
}

} // namespace Frontier
