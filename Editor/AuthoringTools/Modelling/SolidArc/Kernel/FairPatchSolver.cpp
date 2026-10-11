//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Kernel/FairPatchSolver.cpp — Energy-fair fills with G0 / G1 / G2 rims and guides
//============================================================================================================================================
#include "Kernel/FairPatchSolver.h"
#include <algorithm>
#include <cmath>

namespace Frontier
{

const char* Describe(RimContinuity Continuity) noexcept
{
    switch (Continuity)
    {
        case RimContinuity::Tangent:   return "G1";
        case RimContinuity::Curvature: return "G2";
        default:                       return "G0";
    }
}

namespace
{
    //------------------------------------------------------------------------------------------------------------------------
    //                                                  SMALL LINEAR ALGEBRA
    //------------------------------------------------------------------------------------------------------------------------
    // Weighted least squares accumulated straight into the normal equations: (AᵀA) X = Aᵀ B, X and B are N × 3.
    struct LeastSquares
    {
        int N = 0;
        std::vector<double> AtA;                                                        // N × N, row-major
        std::vector<Vec3>   Atb;                                                        // N
        explicit LeastSquares(int Count) : N(Count), AtA(size_t(Count) * Count, 0.0), Atb(size_t(Count)) {}
        struct Term { int Index; double Coefficient; };
        void Add(const std::vector<Term>& Row, Vec3 Rhs, double Weight) noexcept
        {
            double W2 = Weight * Weight;
            for (const Term& A : Row)
            {
                for (const Term& B : Row) AtA[size_t(A.Index) * N + B.Index] += W2 * A.Coefficient * B.Coefficient;
                Atb[size_t(A.Index)] = Atb[size_t(A.Index)] + Rhs * (W2 * A.Coefficient);
            }
        }
        // Cholesky (AᵀA is SPD once every unknown appears in a fairness row). Returns false when it is not.
        [[nodiscard]] bool Solve(std::vector<Vec3>& X) noexcept
        {
            std::vector<double> L(size_t(N) * N, 0.0);
            for (int I = 0; I < N; ++I)
            {
                for (int J = 0; J <= I; ++J)
                {
                    double S = AtA[size_t(I) * N + J];
                    for (int K = 0; K < J; ++K) S -= L[size_t(I) * N + K] * L[size_t(J) * N + K];
                    if (I == J) { if (S <= 1e-18) return false; L[size_t(I) * N + I] = std::sqrt(S); }
                    else L[size_t(I) * N + J] = S / L[size_t(J) * N + J];
                }
            }
            std::vector<Vec3> Y(static_cast<size_t>(N));
            for (int I = 0; I < N; ++I) { Vec3 S = Atb[size_t(I)]; for (int K = 0; K < I; ++K) S = S - Y[size_t(K)] * L[size_t(I) * N + K]; Y[size_t(I)] = S / L[size_t(I) * N + I]; }
            X.assign(size_t(N), Vec3{});
            for (int I = N - 1; I >= 0; --I) { Vec3 S = Y[size_t(I)]; for (int K = I + 1; K < N; ++K) S = S - X[size_t(K)] * L[size_t(K) * N + I]; X[size_t(I)] = S / L[size_t(I) * N + I]; }
            return true;
        }
    };

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  BASIS QUERIES ON A KNOT VECTOR
    //------------------------------------------------------------------------------------------------------------------------
    struct Basis
    {
        NurbsCurve Shape;                                                               // dummy curve carrying degree + knots
        Basis(int Degree, const std::vector<double>& Knots, int Count) { Shape.Degree = Degree; Shape.Knots = Knots; Shape.Poles.assign(size_t(Count), Vec4{}); }
        [[nodiscard]] double Greville(int I) const noexcept { double S = 0; for (int K = 1; K <= Shape.Degree; ++K) S += Shape.Knots[size_t(I + K)]; return S / Shape.Degree; }
        // Values of every basis function at T (dense over Count).
        [[nodiscard]] std::vector<double> Values(double T) const noexcept
        {
            std::vector<double> Out(Shape.Poles.size(), 0.0); double N[16]; int Span = Shape.FindSpan(T); Shape.BasisFunctions(Span, T, N);
            for (int K = 0; K <= Shape.Degree; ++K) Out[size_t(Span - Shape.Degree + K)] = N[K];
            return Out;
        }
        // First and second derivatives of every basis function at T.
        void Slopes(double T, std::vector<double>& D1, std::vector<double>& D2) const noexcept
        {
            D1.assign(Shape.Poles.size(), 0.0); D2.assign(Shape.Poles.size(), 0.0); double N[3 * 16]; int Span = Shape.FindSpan(T); Shape.BasisDerivatives(Span, T, 2, N);
            for (int K = 0; K <= Shape.Degree; ++K) { D1[size_t(Span - Shape.Degree + K)] = N[(Shape.Degree + 1) + K]; D2[size_t(Span - Shape.Degree + K)] = N[2 * (Shape.Degree + 1) + K]; }
        }
    };

    // Insert knots until every direction has at least `Spans` spans, spreading the new knots by gap size.
    NurbsSurface Refine(NurbsSurface S, int Spans) noexcept
    {
        for (int Direction = 0; Direction < 2; ++Direction)
        {
            const std::vector<double>& Knots = Direction == 0 ? S.KnotsU : S.KnotsV;
            std::vector<double> Distinct; for (double K : Knots) if (Distinct.empty() || K > Distinct.back() + ScalarCriteria::ParametricEpsilon) Distinct.push_back(K);
            double Total = Distinct.back() - Distinct.front();
            std::vector<double> Fresh;
            for (size_t I = 0; I + 1 < Distinct.size(); ++I)
            {
                double Gap = Distinct[I + 1] - Distinct[I];
                int Pieces = std::max(1, int(std::ceil(Spans * Gap / Total - 1e-9)));
                for (int K = 1; K < Pieces; ++K) Fresh.push_back(Distinct[I] + Gap * K / Pieces);
            }
            for (double K : Fresh) S = Direction == 0 ? S.InsertKnotU(K) : S.InsertKnotV(K);
        }
        return S;
    }

    Vec3 Slerp(Vec3 A, Vec3 B, double T) noexcept
    {
        Vec3 M = A * (1.0 - T) + B * T;
        if (M.LengthSquared() < 1e-12) return A;
        return M.Normalised();
    }

    // Unit direction in which a rim's support continues past the rim: perpendicular to the rim tangent inside the support
    //    plane, pointing away from the support's own interior (probed on the support); zero for an unsupported rim.
    Vec3 Continuation(const FairRim& R, double F) noexcept
    {
        if (!R.Supported()) return Vec3{};
        double T = R.Curve.DomainStart() + (R.Curve.DomainEnd() - R.Curve.DomainStart()) * F;
        Vec3 P = R.Curve.Sample(T), Tan = R.Curve.Tangent(T);
        Vec3 N = FairPatchSolver::SupportNormal(R, F, P);
        Vec3 Side = N.Cross(Tan).Normalised();
        if (Side.LengthSquared() < 0.5) return Vec3{};
        if (R.Support) { double D = 0, U = 0, V = 0; R.Support->ClosestParameter(P + Side * 1e-2, U, V, &D); if (D < 1e-4) Side = -Side; }
        return Side;
    }

    // Reverse a rim: curve sense and its normal field.
    FairRim Reversed(FairRim R) noexcept { R.Curve = R.Curve.Reversed(); std::reverse(R.NormalField.begin(), R.NormalField.end()); return R; }

    // Chain rims end to start into a closed ring (reordering / reversing), like SkinSolver::RingOrder but keeping the supports.
    std::vector<FairRim> RingOrderRims(std::vector<FairRim> Pieces, double Tolerance) noexcept
    {
        std::vector<FairRim> Ring;
        if (Pieces.empty()) return Ring;
        Ring.push_back(Pieces.front()); Pieces.erase(Pieces.begin());
        while (!Pieces.empty())
        {
            Vec3 End = Ring.back().Curve.EndPoint(); bool Found = false;
            for (size_t I = 0; I < Pieces.size(); ++I)
            {
                if (Pieces[I].Curve.StartPoint().Coincident(End, Tolerance)) { Ring.push_back(Pieces[I]); Pieces.erase(Pieces.begin() + long(I)); Found = true; break; }
                if (Pieces[I].Curve.EndPoint().Coincident(End, Tolerance)) { Ring.push_back(Reversed(Pieces[I])); Pieces.erase(Pieces.begin() + long(I)); Found = true; break; }
            }
            if (!Found) return {};
        }
        if (!Ring.back().Curve.EndPoint().Coincident(Ring.front().Curve.StartPoint(), Tolerance)) return {};
        return Ring;
    }

    // Split a rim at parameter T into two rims that keep the support.
    std::pair<FairRim, FairRim> SplitRim(const FairRim& R, double T) noexcept
    {
        auto [A, B] = R.Curve.Split(T);
        FairRim Ra = R, Rb = R; Ra.Curve = A; Rb.Curve = B;
        if (R.NormalField.size() >= 2)
        {
            double F = (T - R.Curve.DomainStart()) / (R.Curve.DomainEnd() - R.Curve.DomainStart());
            int Count = int(R.NormalField.size());
            Ra.NormalField.clear(); Rb.NormalField.clear();
            for (int I = 0; I < Count; ++I) { Ra.NormalField.push_back(FairPatchSolver::SupportNormal(R, F * I / (Count - 1), Vec3{})); Rb.NormalField.push_back(FairPatchSolver::SupportNormal(R, F + (1 - F) * I / (Count - 1), Vec3{})); }
        }
        return { Ra, Rb };
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SUPPORT QUERIES
//------------------------------------------------------------------------------------------------------------------------

double FairPatchSolver::SupportTouch::SecondForm(Vec3 Direction) const noexcept
{
    // Direction = a·Du + b·Dv (least squares in the tangent plane), II = a²(Duu·n) + 2ab(Duv·n) + b²(Dvv·n)
    double E = Du.Dot(Du), F = Du.Dot(Dv), G = Dv.Dot(Dv), Det = E * G - F * F;
    if (std::fabs(Det) < 1e-24) return 0.0;
    double P = Direction.Dot(Du), Q = Direction.Dot(Dv);
    double A = (G * P - F * Q) / Det, B = (E * Q - F * P) / Det;
    return A * A * Duu.Dot(Normal) + 2 * A * B * Duv.Dot(Normal) + B * B * Dvv.Dot(Normal);
}

FairPatchSolver::SupportTouch FairPatchSolver::Touch(const NurbsSurface& Support, Vec3 P) noexcept
{
    SupportTouch Out;
    double U = 0, V = 0; Support.ClosestParameter(P, U, V);
    Vec3 Point; Support.Derivatives(U, V, Point, Out.Du, Out.Dv);
    Out.Normal = Support.Normal(U, V);
    // second derivatives by central differences of the first (the surface offers analytic first derivatives only)
    double Hu = (Support.DomainEndU() - Support.DomainStartU()) * 1e-4, Hv = (Support.DomainEndV() - Support.DomainStartV()) * 1e-4;
    auto Clamp = [](double T, double A, double B) { return std::min(std::max(T, A), B); };
    double U0 = Clamp(U - Hu, Support.DomainStartU(), Support.DomainEndU()), U1 = Clamp(U + Hu, Support.DomainStartU(), Support.DomainEndU());
    double V0 = Clamp(V - Hv, Support.DomainStartV(), Support.DomainEndV()), V1 = Clamp(V + Hv, Support.DomainStartV(), Support.DomainEndV());
    Vec3 Pp, DuA, DvA, DuB, DvB;
    Support.Derivatives(U0, V, Pp, DuA, DvA); Support.Derivatives(U1, V, Pp, DuB, DvB);
    Out.Duu = (DuB - DuA) / (U1 - U0); Out.Duv = (DvB - DvA) / (U1 - U0);
    Support.Derivatives(U, V0, Pp, DuA, DvA); Support.Derivatives(U, V1, Pp, DuB, DvB);
    Out.Dvv = (DvB - DvA) / (V1 - V0);
    Out.Live = true;
    return Out;
}

Vec3 FairPatchSolver::SupportNormal(const FairRim& Rim, double F, Vec3 P, SupportTouch* Full) noexcept
{
    if (Rim.Support)
    {
        SupportTouch Pr = Touch(*Rim.Support, P);
        if (Full) *Full = Pr;
        return Pr.Normal;
    }
    if (Rim.NormalField.size() >= 2)
    {
        double S = std::min(std::max(F, 0.0), 1.0) * double(Rim.NormalField.size() - 1);
        size_t I = std::min(size_t(S), Rim.NormalField.size() - 2);
        Vec3 N = Slerp(Rim.NormalField[I], Rim.NormalField[I + 1], S - double(I));
        if (Full) { *Full = SupportTouch{}; Full->Normal = N; Full->Live = false; }
        return N;
    }
    if (Full) *Full = SupportTouch{};
    return Vec3{};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  MEASUREMENT
//------------------------------------------------------------------------------------------------------------------------

double FairPatchSolver::TangentBreak(const NurbsSurface& S, const FairRim& Rim, int Samples) noexcept
{
    if (!Rim.Supported()) return 0.0;
    double Worst = 0.0;
    for (int I = 0; I <= Samples; ++I)
    {
        double F = 0.05 + 0.9 * I / Samples;                                            // corners excluded: two supports meeting at an angle cannot both be honoured there
        Vec3 P = Rim.Curve.Sample(Rim.Curve.DomainStart() + (Rim.Curve.DomainEnd() - Rim.Curve.DomainStart()) * F);
        Vec3 Ns = SupportNormal(Rim, F, P);
        double U = 0, V = 0; S.ClosestParameter(P, U, V);
        Vec3 N = S.Normal(U, V);
        double Sine = std::min(1.0, N.Cross(Ns).Length());
        Worst = std::max(Worst, std::asin(Sine));
    }
    return Worst;
}

double FairPatchSolver::CurvatureBreak(const NurbsSurface& S, const FairRim& Rim, int Samples) noexcept
{
    if (!Rim.Support) return 0.0;
    double Worst = 0.0;
    for (int I = 0; I <= Samples; ++I)
    {
        double F = 0.2 + 0.6 * I / Samples;                                             // the outer fifths belong to the corners: curvature there is decided by the neighbouring rim
        double T = Rim.Curve.DomainStart() + (Rim.Curve.DomainEnd() - Rim.Curve.DomainStart()) * F;
        Vec3 P = Rim.Curve.Sample(T), Tan = Rim.Curve.Tangent(T);
        SupportTouch Sup = Touch(*Rim.Support, P), Own = Touch(S, P);
        if (Own.Normal.Dot(Sup.Normal) < 0) { Own.Normal = -Own.Normal; }
        Vec3 D = Own.Normal.Cross(Tan).Normalised();                                    // across the rim, in the shared tangent plane
        double Ks = Own.SecondForm(D), Kp = Sup.SecondForm(D);
        Worst = std::max(Worst, std::fabs(Ks - Kp));
    }
    return Worst;
}

double FairPatchSolver::BendingEnergy(const NurbsSurface& S, int Lattice) noexcept
{
    double U0 = S.DomainStartU(), U1 = S.DomainEndU(), V0 = S.DomainStartV(), V1 = S.DomainEndV();
    double Du = (U1 - U0) / Lattice, Dv = (V1 - V0) / Lattice;
    std::vector<Vec3> P(size_t(Lattice + 1) * (Lattice + 1));
    for (int I = 0; I <= Lattice; ++I) for (int J = 0; J <= Lattice; ++J) P[size_t(I) * (Lattice + 1) + J] = S.Sample(U0 + Du * I, V0 + Dv * J);
    auto At = [&](int I, int J) { return P[size_t(I) * (Lattice + 1) + J]; };
    double E = 0.0;
    for (int I = 1; I < Lattice; ++I) for (int J = 1; J < Lattice; ++J)
    {
        Vec3 Suu = (At(I + 1, J) - At(I, J) * 2.0 + At(I - 1, J)) / (Du * Du);
        Vec3 Svv = (At(I, J + 1) - At(I, J) * 2.0 + At(I, J - 1)) / (Dv * Dv);
        Vec3 Suv = (At(I + 1, J + 1) - At(I + 1, J - 1) - At(I - 1, J + 1) + At(I - 1, J - 1)) / (4 * Du * Dv);
        E += (Suu.LengthSquared() + 2 * Suv.LengthSquared() + Svv.LengthSquared()) * Du * Dv;
    }
    return E;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  QUAD SOLVE
//------------------------------------------------------------------------------------------------------------------------

Deliver<NurbsSurface> FairPatchSolver::Quad(std::vector<FairRim> Rims, const std::vector<NurbsCurve>& Guides, const FairPatchOptions& Options, FairPatchReport* Report) noexcept
{
    if (Rims.size() != 4) return Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput, "a quad fill needs exactly four rims");
    std::vector<FairRim> Ring = RingOrderRims(std::move(Rims), Options.Tolerance);
    if (Ring.empty()) return Deliver<NurbsSurface>::Reject(RefusalReason::OpenWire, "fill rims do not form a closed ring");
    // 1. Coons fill on the exact rims — gives the boundary rows, the lattice and the starting interior.
    Deliver<NurbsSurface> Coons = SkinSolver::CoonsPatch({ Ring[0].Curve, Ring[1].Curve, Ring[2].Curve, Ring[3].Curve }, Options.Tolerance);
    if (!Coons) return Coons;
    NurbsSurface S = Refine(Coons.Payload, std::max(2, Options.Spans));
    const int NU = S.CountU, NV = S.CountV;
    if (NU < 4 || NV < 4) return Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput, "fill lattice too small");
    Basis BU(S.DegreeU, S.KnotsU, NU), BV(S.DegreeV, S.KnotsV, NV);
    std::vector<Vec3> Start(S.Poles.size()); for (size_t K = 0; K < S.Poles.size(); ++K) Start[K] = S.Poles[K].Divide();
    auto Index = [&](int I, int J) { return I * NV + J; };
    auto Interior = [&](int I, int J) { return I > 0 && I < NU - 1 && J > 0 && J < NV - 1; };
    auto Unknown = [&](int I, int J) { return (I - 1) * (NV - 2) + (J - 1); };
    const int Count = (NU - 2) * (NV - 2);
    std::vector<Vec3> Poles = Start;

    // Side s of the ring, expressed as a lattice walk: pole (I,J) = Origin + a·Along + b·Inward; a ∈ [0, Na), b ∈ [0, Nb).
    struct Side { int Na, Nb; int A0, B0; int Ai, Aj, Bi, Bj; bool ForwardA; bool ForwardB; const Basis* Along; const Basis* Across; };
    // ring[0] bottom (v=0, u→), ring[1] right (u=1, v↑), ring[2] top (v=1, u←), ring[3] left (u=0, v↓)
    Side Sides[4] =
    {
        { NU, NV, 0,      0,      1, 0, 0,  1, true,  true,  &BU, &BV },
        { NV, NU, NU - 1, 0,      0, 1, -1, 0, true,  false, &BV, &BU },
        { NU, NV, NU - 1, NV - 1, -1, 0, 0, -1, false, false, &BU, &BV },
        { NV, NU, 0,      NV - 1, 0, -1, 1, 0, false, true,  &BV, &BU },
    };
    auto Cell = [&](const Side& Sd, int A, int B) { int I = Sd.A0 + A * Sd.Ai + B * Sd.Bi, J = Sd.B0 + A * Sd.Aj + B * Sd.Bj; return std::pair<int, int>{ I, J }; };
    // along-rim parameter of lattice index A on side Sd, and the rim fraction F (ring direction)
    auto AlongParameter = [&](const Side& Sd, int A) { int K = Sd.ForwardA ? A : Sd.Na - 1 - A; return Sd.Along->Greville(K); };
    auto Fraction = [&](const Side& Sd, double T) { const Basis* B = Sd.Along; double T0 = B->Shape.Knots[size_t(B->Shape.Degree)], T1 = B->Shape.Knots[B->Shape.Poles.size()]; double F = (T - T0) / (T1 - T0); return Sd.ForwardA ? F : 1.0 - F; };
    // cross-direction basis derivative coefficients at the rim end (b = 0 is the rim): c1[b], c2[b]
    struct Across { std::vector<double> C1, C2; };
    auto AcrossSlopes = [&](const Side& Sd)
    {
        Across Out; const Basis* B = Sd.Across; std::vector<double> D1, D2;
        double T = Sd.ForwardB ? B->Shape.Knots[size_t(B->Shape.Degree)] : B->Shape.Knots[B->Shape.Poles.size()];
        B->Slopes(T, D1, D2);
        Out.C1.assign(size_t(Sd.Nb), 0.0); Out.C2.assign(size_t(Sd.Nb), 0.0);
        for (int Bk = 0; Bk < Sd.Nb; ++Bk) { int K = Sd.ForwardB ? Bk : Sd.Nb - 1 - Bk; Out.C1[size_t(Bk)] = (Sd.ForwardB ? 1.0 : -1.0) * D1[size_t(K)]; Out.C2[size_t(Bk)] = D2[size_t(K)]; }
        return Out;
    };
    // Coons cross derivative (inward) at along-parameter T on side Sd, from the starting lattice
    auto CoonsInward = [&](const Side& Sd, const Across& Ac, double T)
    {
        std::vector<double> N = Sd.Along->Values(T); Vec3 D;
        for (int A = 0; A < Sd.Na; ++A) { int K = Sd.ForwardA ? A : Sd.Na - 1 - A; if (N[size_t(K)] == 0) continue; for (int B = 0; B < Sd.Nb; ++B) if (Ac.C1[size_t(B)] != 0) { auto [I, J] = Cell(Sd, A, B); D = D + Start[size_t(Index(I, J))] * (N[size_t(K)] * Ac.C1[size_t(B)]); } }
        return D;
    };
    auto Combine = [&](const std::vector<Vec3>& Lattice, const Side& Sd, const std::vector<double>& C, double T)
    {
        std::vector<double> N = Sd.Along->Values(T); Vec3 D;
        for (int A = 0; A < Sd.Na; ++A) { int K = Sd.ForwardA ? A : Sd.Na - 1 - A; if (N[size_t(K)] == 0) continue; for (int B = 0; B < Sd.Nb; ++B) if (C[size_t(B)] != 0) { auto [I, J] = Cell(Sd, A, B); D = D + Lattice[size_t(Index(I, J))] * (N[size_t(K)] * C[size_t(B)]); } }
        return D;
    };

    const double WeightFair = Options.Fairness, WeightG1 = 40.0, WeightG2 = 10.0, WeightGuide = 10.0;
    int Unsupported = 0;
    for (const FairRim& R : Ring) if (R.Continuity != RimContinuity::Position && !R.Supported()) ++Unsupported;
    double Hu = (S.DomainEndU() - S.DomainStartU()) / (NU - 1), Hv = (S.DomainEndV() - S.DomainStartV()) / (NV - 1);
    std::vector<double> Gu(static_cast<size_t>(NU)), Gv(static_cast<size_t>(NV));
    for (int I = 0; I < NU; ++I) Gu[size_t(I)] = BU.Greville(I);
    for (int J = 0; J < NV; ++J) Gv[size_t(J)] = BV.Greville(J);

    for (int Round = 0; Round < std::max(1, Options.Rounds); ++Round)
    {
        LeastSquares System(Count);
        std::vector<LeastSquares::Term> Row;
        auto Emit = [&](const std::vector<std::pair<int, double>>& Terms, Vec3 Rhs, double Weight)
        {
            Row.clear();
            for (auto [K, C] : Terms) { int I = K / NV, J = K % NV; if (Interior(I, J)) Row.push_back({ Unknown(I, J), C }); else Rhs = Rhs - Poles[size_t(K)] * C; }
            if (!Row.empty()) System.Add(Row, Rhs, Weight);
        };
        // 2. Fairness: second divided differences of the control net over the Greville abscissae (zero for a plane with
        //    any knot spacing), scaled by the mean spacing so the rows are O(1) against the rim rows.
        for (int I = 1; I < NU - 1; ++I)
        {
            double A = Gu[size_t(I)] - Gu[size_t(I - 1)], B = Gu[size_t(I + 1)] - Gu[size_t(I)], Sc = Hu * Hu * 2.0 / (A + B);
            for (int J = 0; J < NV; ++J) Emit({ { Index(I - 1, J), Sc / A }, { Index(I, J), -Sc * (1 / A + 1 / B) }, { Index(I + 1, J), Sc / B } }, Vec3{}, WeightFair);
        }
        for (int J = 1; J < NV - 1; ++J)
        {
            double A = Gv[size_t(J)] - Gv[size_t(J - 1)], B = Gv[size_t(J + 1)] - Gv[size_t(J)], Sc = Hv * Hv * 2.0 / (A + B);
            for (int I = 0; I < NU; ++I) Emit({ { Index(I, J - 1), Sc / A }, { Index(I, J), -Sc * (1 / A + 1 / B) }, { Index(I, J + 1), Sc / B } }, Vec3{}, WeightFair);
        }
        for (int I = 0; I < NU - 1; ++I) for (int J = 0; J < NV - 1; ++J)
        {
            double Sc = Hu * Hv / ((Gu[size_t(I + 1)] - Gu[size_t(I)]) * (Gv[size_t(J + 1)] - Gv[size_t(J)]));
            Emit({ { Index(I + 1, J + 1), Sc }, { Index(I + 1, J), -Sc }, { Index(I, J + 1), -Sc }, { Index(I, J), Sc } }, Vec3{}, WeightFair * std::sqrt(2.0));
        }
        // 3. Rim conditions, one equation per Greville abscissa along each supported rim.
        for (int Sn = 0; Sn < 4; ++Sn)
        {
            const FairRim& R = Ring[size_t(Sn)]; const Side& Sd = Sides[Sn];
            if (R.Continuity == RimContinuity::Position || !R.Supported()) continue;
            Across Ac = AcrossSlopes(Sd);
            for (int A = 1; A < Sd.Na - 1; ++A)
            {
                double T = AlongParameter(Sd, A); double F = Fraction(Sd, T);
                // Where two supports meet at an angle the corner cannot honour both; ease the rows next to the corners so
                //    the conflict does not ring along the rim.
                double Taper = (A == 1 || A == Sd.Na - 2) ? 0.35 : (A == 2 || A == Sd.Na - 3) ? 0.7 : 1.0;
                double Taper2 = (A <= 3 || A >= Sd.Na - 4) ? 0.0 : Taper;               // G2 rows stay clear of the corners altogether
                Vec3 P = Combine(Poles, Sd, [&] { std::vector<double> C0(size_t(Sd.Nb), 0.0); C0[0] = 1.0; return C0; }(), T);
                SupportTouch Pr; Vec3 N = SupportNormal(R, F, P, &Pr);
                if (N.LengthSquared() < 0.5) continue;
                std::vector<double> Nb = Sd.Along->Values(T);
                // G1: inward derivative · n = 0
                std::vector<std::pair<int, double>> Terms;
                for (int Aa = 0; Aa < Sd.Na; ++Aa) { int K = Sd.ForwardA ? Aa : Sd.Na - 1 - Aa; if (Nb[size_t(K)] == 0) continue; for (int B = 0; B < Sd.Nb; ++B) if (Ac.C1[size_t(B)] != 0) { auto [I, J] = Cell(Sd, Aa, B); Terms.push_back({ Index(I, J), Nb[size_t(K)] * Ac.C1[size_t(B)] }); } }
                {
                    // x, y, z share one matrix, so the scalar condition D·n = 0 is imposed as the vector condition
                    //    D = (previous D projected into the tangent plane), rescaled by the tension — a fixed point that is
                    //    exactly G1 once reached and settles within the rounds.
                    Vec3 D = Combine(Poles, Sd, Ac.C1, T);
                    Vec3 Dc = CoonsInward(Sd, Ac, T);
                    double Wanted = Dc.Length() * R.Tension;                            // magnitude: the Coons cross derivative × tension
                    Vec3 Target = D - N * D.Dot(N);                                     // direction: the current derivative, laid into the support plane
                    if (Target.LengthSquared() < 1e-4 * Dc.LengthSquared())
                    {
                        // the current derivative is (nearly) normal to the support — a 90° corner such as a hole in a box
                        //    top: continue the support past the rim, perpendicular to the rim tangent, away from its interior
                        Vec3 Side = Continuation(R, F);
                        if (Side.LengthSquared() < 0.5) Side = Dc.Normalised();
                        else if (!R.Support && Side.Dot(Dc) < 0) Side = -Side;
                        Target = Side;
                    }
                    Target = Target.Normalised() * Wanted;
                    Emit(Terms, Target, WeightG1 * Taper);
                }
                // G2: inward second derivative · n = II_support(inward first derivative) — via the same projection trick
                if (R.Continuity == RimContinuity::Curvature && Pr.Live)
                {
                    Vec3 D1 = Combine(Poles, Sd, Ac.C1, T);
                    Vec3 D2 = Combine(Poles, Sd, Ac.C2, T);
                    double Wanted = Pr.SecondForm(D1);
                    Vec3 Target = D2 - N * D2.Dot(N) + N * Wanted;
                    std::vector<std::pair<int, double>> Terms2;
                    for (int Aa = 0; Aa < Sd.Na; ++Aa) { int K = Sd.ForwardA ? Aa : Sd.Na - 1 - Aa; if (Nb[size_t(K)] == 0) continue; for (int B = 0; B < Sd.Nb; ++B) if (Ac.C2[size_t(B)] != 0) { auto [I, J] = Cell(Sd, Aa, B); Terms2.push_back({ Index(I, J), Nb[size_t(K)] * Ac.C2[size_t(B)] }); } }
                    if (Taper2 > 0) Emit(Terms2, Target, WeightG2 * Taper2);
                }
            }
        }
        // 4. Guides: interpolate samples at their current closest (u,v).
        for (const NurbsCurve& G : Guides)
        {
            int Samples = std::max(8, 2 * Options.Spans);
            for (int K = 0; K <= Samples; ++K)
            {
                Vec3 Q = G.Sample(G.DomainStart() + (G.DomainEnd() - G.DomainStart()) * K / Samples);
                double U = 0, V = 0; S.ClosestParameter(Q, U, V);
                std::vector<double> Nu = BU.Values(U), Nv = BV.Values(V);
                std::vector<std::pair<int, double>> Terms;
                bool Touches = false;
                for (int I = 0; I < NU; ++I) for (int J = 0; J < NV; ++J) { double C = Nu[size_t(I)] * Nv[size_t(J)]; if (C != 0) { Terms.push_back({ Index(I, J), C }); Touches |= Interior(I, J); } }
                if (Touches) Emit(Terms, Q, WeightGuide);
            }
        }
        std::vector<Vec3> X;
        if (!System.Solve(X)) return Deliver<NurbsSurface>::Reject(RefusalReason::NoConvergence, "fill system is singular");
        for (int I = 1; I < NU - 1; ++I) for (int J = 1; J < NV - 1; ++J) Poles[size_t(Index(I, J))] = X[size_t(Unknown(I, J))];
        for (size_t K = 0; K < S.Poles.size(); ++K) S.Poles[K] = Vec4(Poles[K], 1.0);
    }
    S.Classification = SurfaceClassification::FairPatch;
    if (Report)
    {
        Report->Quads += 1; Report->Unknowns = std::max(Report->Unknowns, Count); Report->UnsupportedRims += Unsupported;
        for (const FairRim& R : Ring)
        {
            if (R.Continuity == RimContinuity::Position || !R.Supported()) continue;
            if (R.Support) Report->TangentBreak = std::max(Report->TangentBreak, TangentBreak(S, R));
            else Report->SeamBreak = std::max(Report->SeamBreak, TangentBreak(S, R));
            if (R.Continuity == RimContinuity::Curvature) Report->CurvatureBreak = std::max(Report->CurvatureBreak, CurvatureBreak(S, R));
        }
        for (const NurbsCurve& G : Guides) for (int K = 0; K <= 32; ++K) { double D = 0, U = 0, V = 0; S.ClosestParameter(G.Sample(G.DomainStart() + (G.DomainEnd() - G.DomainStart()) * K / 32.0), U, V, &D); Report->GuideDeviation = std::max(Report->GuideDeviation, D); }
        Report->Energy += BendingEnergy(S); Report->CoonsEnergy += BendingEnergy(Coons.Payload);
    }
    return Deliver<NurbsSurface>::Accept(std::move(S));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  N-SIDED BUILD
//------------------------------------------------------------------------------------------------------------------------

Deliver<SkinSolver::Skin> FairPatchSolver::Build(std::vector<FairRim> Rims, const std::vector<NurbsCurve>& Guides, const FairPatchOptions& Options, FairPatchReport* Report) noexcept
{
    using Skin = SkinSolver::Skin;
    if (Rims.size() == 1 && Rims.front().Curve.Closed())
    {
        FairRim Whole = Rims.front(); double L = Whole.Curve.Length();
        std::vector<FairRim> Quarters; FairRim Rest = Whole;
        for (int Q = 1; Q < 4; ++Q) { double T = Rest.Curve.ParameterAtLength(L * 0.25); auto [A, B] = SplitRim(Rest, T); Quarters.push_back(A); Rest = B; }
        Quarters.push_back(Rest); Rims = Quarters;
    }
    if (Rims.size() < 3) return Deliver<Skin>::Reject(RefusalReason::DegenerateInput, "a fill needs at least three rims (or one closed curve)");
    if (Report) *Report = FairPatchReport{};
    if (Rims.size() == 4 && !Options.Star)
    {
        Deliver<NurbsSurface> Q = Quad(std::move(Rims), Guides, Options, Report);
        if (!Q) return Deliver<Skin>::Reject(Q.Denial.Reason, Q.Denial.Detail);
        Skin Out; Out.Sheet = std::move(Q.Payload);
        return Deliver<Skin>::Accept(std::move(Out));
    }
    // N-sided: split each rim at mid-length; centre = mean of the midpoints; spokes are cubic Béziers that leave each rim
    //    perpendicular to it (within the rim's support plane) and meet at the centre in a common plane. Each spoke carries
    //    a normal field shared by the two quads on either side, so they are G1 across it.
    std::vector<FairRim> Ring = RingOrderRims(std::move(Rims), Options.Tolerance);
    if (Ring.empty()) return Deliver<Skin>::Reject(RefusalReason::OpenWire, "fill rims do not form a closed ring");
    const size_t N = Ring.size();
    std::vector<FairRim> First(N), Second(N); std::vector<Vec3> Mid(N), MidNormal(N), MidTangent(N);
    Vec3 Centre;
    for (size_t I = 0; I < N; ++I)
    {
        double T = Ring[I].Curve.ParameterAtLength(Ring[I].Curve.Length() * 0.5);
        auto [A, B] = SplitRim(Ring[I], T); First[I] = A; Second[I] = B; Mid[I] = A.Curve.EndPoint(); Centre = Centre + Mid[I];
        MidTangent[I] = Ring[I].Curve.Tangent(T);
        MidNormal[I] = Ring[I].Supported() ? SupportNormal(Ring[I], 0.5, Mid[I]) : Vec3{};
    }
    Centre = Centre * (1.0 / double(N));
    // Orientation: mid normals are flipped to agree with the ring's winding normal where they are not perpendicular
    //    to it, else to face away from the centre. The centre normal is their mean when that is well defined (a smooth
    //    window in a curved face) else the winding normal signed by the supports' continuation (a pillow over a box top).
    Vec3 Winding; for (size_t I = 0; I < N; ++I) Winding = Winding + (Mid[I] - Centre).Cross(Mid[(I + 1) % N] - Centre);
    Winding = Winding.Normalised();
    Vec3 CentreNormal, Lift; int Supported = 0;
    for (size_t I = 0; I < N; ++I)
    {
        if (MidNormal[I].LengthSquared() < 0.5) { MidNormal[I] = Winding; continue; }
        double Agree = MidNormal[I].Dot(Winding);
        if (std::fabs(Agree) > 0.3) { if (Agree < 0) MidNormal[I] = -MidNormal[I]; }
        else if (MidNormal[I].Dot(Mid[I] - Centre) < 0) MidNormal[I] = -MidNormal[I];
        CentreNormal = CentreNormal + MidNormal[I]; Lift = Lift + Continuation(Ring[I], 0.5); ++Supported;
    }
    if (CentreNormal.Length() > 0.5 * Supported && Supported) CentreNormal = CentreNormal.Normalised();
    else CentreNormal = Lift.Dot(Winding) < 0 ? -Winding : Winding;
    // A window in a smooth skin: supports whose normal runs along the winding normal continue under the fill, so the
    //    centre is placed on them (mean of the closest points). Otherwise (walls meeting the fill at an angle) the
    //    centre is lifted along the supports' continuation so the spokes meet it with a fair blend.
    Vec3 Onto; int Landed = 0;
    for (size_t I = 0; I < N; ++I)
    {
        if (!Ring[I].Support) continue;
        double U = 0, V = 0; Ring[I].Support->ClosestParameter(Centre, U, V);
        if (std::fabs(Ring[I].Support->Normal(U, V).Dot(Winding)) < 0.7) continue;
        Onto = Onto + Ring[I].Support->Sample(U, V); ++Landed;
    }
    if (Landed) Centre = Onto / double(Landed);
    else if (Supported && Lift.LengthSquared() > 1e-6)
    {
        double Reach = 0; for (size_t I = 0; I < N; ++I) Reach += (Mid[I] - Centre).Length(); Reach /= double(N);
        Centre = Centre + Lift * (Reach * 0.35 / double(Supported));
    }
    std::vector<FairRim> Spoke(N);
    for (size_t I = 0; I < N; ++I)
    {
        Vec3 P = Mid[I], Toward = Centre - P;
        Vec3 In = Toward - MidTangent[I] * Toward.Dot(MidTangent[I]);
        In = In - MidNormal[I] * In.Dot(MidNormal[I]);                                  // perpendicular to the rim, inside the support plane
        if (In.LengthSquared() < 1e-18) In = Toward;
        In = In.Normalised() * (Toward.Length() / 3.0);
        Vec3 Arrive = Toward - CentreNormal * Toward.Dot(CentreNormal);                 // arrive in the centre's tangent plane
        if (Arrive.LengthSquared() < 1e-18) Arrive = Toward;
        Arrive = Arrive.Normalised() * (Toward.Length() / 3.0);
        Deliver<NurbsCurve> B = NurbsCurve::Bezier({ P, P + In, Centre - Arrive, Centre });
        Spoke[I].Curve = B ? B.Payload : NurbsCurve::Line(P, Centre).Payload;
        Spoke[I].Continuity = RimContinuity::Tangent;
        Spoke[I].Tension = 1.0;
        for (int K = 0; K <= 16; ++K)
        {
            // blend of the two end normals, made perpendicular to the spoke's tangent so both neighbouring quads can honour it
            double T = Spoke[I].Curve.DomainStart() + (Spoke[I].Curve.DomainEnd() - Spoke[I].Curve.DomainStart()) * K / 16.0;
            Vec3 Tan = Spoke[I].Curve.Tangent(T), Nn = Slerp(MidNormal[I], CentreNormal, K / 16.0);
            Nn = Nn - Tan * Nn.Dot(Tan);
            Spoke[I].NormalField.push_back(Nn.LengthSquared() > 1e-12 ? Nn.Normalised() : CentreNormal);
        }
    }
    std::vector<NurbsSurface> Quads;
    for (size_t I = 0; I < N; ++I)
    {
        size_t J = (I + 1) % N;
        Deliver<NurbsSurface> Q = Quad({ Second[I], First[J], Reversed(Spoke[J]), Spoke[I] }, Guides, Options, Report);
        if (!Q) return Deliver<Skin>::Reject(Q.Denial.Reason, Q.Denial.Detail);
        Quads.push_back(std::move(Q.Payload));
    }
    Deliver<BrepBody> Body = BrepBody::Sew(Quads, ScalarCriteria::MergeTolerance, false);
    if (!Body) return Deliver<Skin>::Reject(Body.Denial.Reason, Body.Denial.Detail);
    Skin Out; Out.Body = std::move(Body.Payload); Out.IsBody = true;
    return Deliver<Skin>::Accept(std::move(Out));
}

} // namespace Frontier
