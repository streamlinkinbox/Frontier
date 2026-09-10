//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Kernel/ProfileSolver.cpp — Planar profile algebra: exact curve crossings, winding, booleans, fillet,
//    chamfer, offset, trim, join.
//============================================================================================================================================
#include "Kernel/ProfileSolver.h"
#include "Kernel/TopologySpecification.h"
#include <algorithm>
#include <cmath>

namespace Frontier
{

const char* Describe(ProfileOperation Op) noexcept
{
    switch (Op) { case ProfileOperation::Union: return "union"; case ProfileOperation::Subtract: return "subtract"; default: return "intersect"; }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  LOCAL SUPPORT
//------------------------------------------------------------------------------------------------------------------------
namespace
{
    struct PlanarAxes
    {
        Vec3 U, V, N;
        [[nodiscard]] Vec2 Project(Vec3 P) const noexcept { return { P.Dot(U), P.Dot(V) }; }
    };
    PlanarAxes AxesOf(Vec3 Normal) noexcept
    {
        Vec3 N = Normal.Normalised();
        Vec3 Seed = std::fabs(N.Z) < 0.9 ? Vec3::UnitZ() : Vec3::UnitX();
        Vec3 U = Seed.Cross(N).Normalised();
        return { U, N.Cross(U), N };
    }

    double Cross2(Vec2 A, Vec2 B) noexcept { return A.X * B.Y - A.Y * B.X; }

    // Bounding box of a Bézier piece from its poles (convex hull property).
    Box3 PoleBox(const NurbsCurve& C) noexcept { Box3 B; for (const Vec4& P : C.Poles) B.Include(P.Divide()); return B; }

    bool Flat(const NurbsCurve& C, double Tolerance) noexcept
    {
        Vec3 A = C.StartPoint(), D = C.EndPoint() - A; double L2 = D.LengthSquared();
        for (const Vec4& H : C.Poles)
        {
            Vec3 P = H.Divide() - A;
            double Dist = L2 < 1e-24 ? P.Length() : (P - D * (P.Dot(D) / L2)).Length();
            if (Dist > Tolerance) return false;
        }
        return true;
    }

    // Segment–segment crossing in 3D (coplanar assumption): returns parameters along both chords.
    bool ChordCrossing(Vec3 A0, Vec3 A1, Vec3 B0, Vec3 B1, double& S, double& T) noexcept
    {
        Vec3 Da = A1 - A0, Db = B1 - B0, W = B0 - A0;
        Vec3 N = Da.Cross(Db); double N2 = N.LengthSquared();
        if (N2 < 1e-30) return false;
        S = W.Cross(Db).Dot(N) / N2; T = W.Cross(Da).Dot(N) / N2;
        return S >= -1e-9 && S <= 1 + 1e-9 && T >= -1e-9 && T <= 1 + 1e-9;
    }

    struct Candidate { double S, T; };

    // Recursive subdivision of two Bézier pieces; parameters are real curve parameters carried through the recursion.
    void Subdivide(const NurbsCurve& A, double A0, double A1, const NurbsCurve& B, double B0, double B1, int Depth,
                   double Tolerance, std::vector<Candidate>& Out) noexcept
    {
        if (!PoleBox(A).Inflated(Tolerance).Overlaps(PoleBox(B).Inflated(Tolerance))) return;
        bool FlatA = Flat(A, Tolerance), FlatB = Flat(B, Tolerance);
        if ((FlatA && FlatB) || Depth > 40)
        {
            double S = 0, T = 0;
            if (ChordCrossing(A.StartPoint(), A.EndPoint(), B.StartPoint(), B.EndPoint(), S, T))
                Out.push_back({ A0 + (A1 - A0) * ScalarCriteria::Clamp(S, 0.0, 1.0), B0 + (B1 - B0) * ScalarCriteria::Clamp(T, 0.0, 1.0) });
            else if (Depth > 40 && A.EndPoint().Distance(B.EndPoint()) < Tolerance * 4) Out.push_back({ A1, B1 });
            return;
        }
        if (!FlatA && (FlatB || PoleBox(A).Diagonal() >= PoleBox(B).Diagonal()))
        {
            double Am = 0.5 * (A0 + A1);
            auto [L, R] = A.Split(0.5 * (A.DomainStart() + A.DomainEnd()));
            Subdivide(L, A0, Am, B, B0, B1, Depth + 1, Tolerance, Out);
            Subdivide(R, Am, A1, B, B0, B1, Depth + 1, Tolerance, Out);
        }
        else
        {
            double Bm = 0.5 * (B0 + B1);
            auto [L, R] = B.Split(0.5 * (B.DomainStart() + B.DomainEnd()));
            Subdivide(A, A0, A1, L, B0, Bm, Depth + 1, Tolerance, Out);
            Subdivide(A, A0, A1, R, Bm, B1, Depth + 1, Tolerance, Out);
        }
    }

    // Newton on F(s,t) = A(s) − B(t) projected on the two tangents.
    void Polish(const NurbsCurve& A, const NurbsCurve& B, double& S, double& T) noexcept
    {
        for (int It = 0; It < 12; ++It)
        {
            Vec3 Da[2], Db[2]; A.Derivatives(S, 1, Da); B.Derivatives(T, 1, Db);
            Vec3 F = Da[0] - Db[0];
            if (F.LengthSquared() < 1e-26) return;
            // solve [Da' −Db'] [ds dt]ᵀ = −F in least squares (2 unknowns, 3 equations)
            double A11 = Da[1].Dot(Da[1]), A12 = -Da[1].Dot(Db[1]), A22 = Db[1].Dot(Db[1]);
            double R1 = -Da[1].Dot(F), R2 = Db[1].Dot(F);
            double Det = A11 * A22 - A12 * A12; if (std::fabs(Det) < 1e-30) return;
            double Ds = (R1 * A22 - A12 * R2) / Det, Dt = (A11 * R2 - A12 * R1) / Det;
            S = ScalarCriteria::Clamp(S + Ds, A.DomainStart(), A.DomainEnd());
            T = ScalarCriteria::Clamp(T + Dt, B.DomainStart(), B.DomainEnd());
            if (std::fabs(Ds) + std::fabs(Dt) < 1e-15) return;
        }
    }

    // Piece of a split loop with its classification.
    struct Piece
    {
        NurbsCurve Curve;
        int        Owner = 0;                                                           // 0 from A, 1 from B
        bool       Keep = false;
        bool       Used = false;
    };

    // Angle of the tangent at the start / end of a curve inside the plane; used to pick the tightest turn when chaining.
    double HeadingAt(const NurbsCurve& C, double T, const PlanarAxes& Ax) noexcept
    {
        Vec2 D = Ax.Project(C.Tangent(T)); return std::atan2(D.Y, D.X);
    }

    NurbsCurve ChainPieces(std::vector<NurbsCurve> Parts) noexcept
    {
        NurbsCurve R = Parts.front();
        for (size_t I = 1; I < Parts.size(); ++I) { Deliver<NurbsCurve> J = NurbsCurve::Join(R, Parts[I]); if (J) R = std::move(J.Payload); }
        return R;
    }

    // Corner list of a degree-1 polyline / polygon (or C0 breaks of a higher-degree chain): parameters where tangents kink.
    struct Corner { double T; Vec3 P; Vec3 In, Out; };
    std::vector<Corner> CornersOf(const NurbsCurve& C) noexcept
    {
        std::vector<Corner> Out;
        const bool Closed = C.Closed();
        std::vector<double> Breaks;
        for (size_t I = C.Degree + 1; I < C.Poles.size(); ++I)
        {
            int Mult = 1; while (I + Mult < C.Knots.size() && ScalarCriteria::Coincident(C.Knots[I + Mult], C.Knots[I])) ++Mult;
            if (Mult >= C.Degree && C.Knots[I] > C.Knots[I - 1] + ScalarCriteria::ParametricEpsilon) Breaks.push_back(C.Knots[I]);
            I += Mult - 1;
        }
        auto Push = [&](double T, Vec3 In, Vec3 Outg) { if (In.Cross(Outg).Length() > 1e-6 || In.Dot(Outg) < 0) Out.push_back({ T, C.Sample(T), In, Outg }); };
        for (double T : Breaks) Push(T, C.Tangent(T - 1e-9 * (C.DomainEnd() - C.DomainStart())), C.Tangent(T + 1e-9 * (C.DomainEnd() - C.DomainStart())));
        if (Closed) Push(C.DomainStart(), C.Tangent(C.DomainEnd()), C.Tangent(C.DomainStart()));
        std::sort(Out.begin(), Out.end(), [](const Corner& A, const Corner& B) { return A.T < B.T; });
        return Out;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  CURVE LEVEL
//------------------------------------------------------------------------------------------------------------------------
std::vector<CurveCrossing> ProfileSolver::Intersect(const NurbsCurve& A, const NurbsCurve& B, double Tolerance) noexcept
{
    std::vector<Candidate> Raw;
    std::vector<NurbsCurve> Pa = A.BezierSegments(), Pb = B.BezierSegments();
    for (const NurbsCurve& Sa : Pa)
        for (const NurbsCurve& Sb : Pb)
            Subdivide(Sa, Sa.DomainStart(), Sa.DomainEnd(), Sb, Sb.DomainStart(), Sb.DomainEnd(), 0, std::max(Tolerance, 1e-7), Raw);
    std::vector<CurveCrossing> Out;
    for (Candidate& K : Raw)
    {
        Polish(A, B, K.S, K.T);
        Vec3 P = A.Sample(K.S), Q = B.Sample(K.T);
        if (P.Distance(Q) > ScalarCriteria::MergeTolerance) continue;
        bool Dup = false;
        for (const CurveCrossing& X : Out) if (X.Point.Distance(P) < ScalarCriteria::MergeTolerance) { Dup = true; break; }
        if (Dup) continue;
        CurveCrossing X; X.ParameterA = K.S; X.ParameterB = K.T; X.Point = (P + Q) * 0.5;
        X.Tangent = A.Tangent(K.S).Cross(B.Tangent(K.T)).Length() < 1e-4;
        Out.push_back(X);
    }
    std::sort(Out.begin(), Out.end(), [](const CurveCrossing& L, const CurveCrossing& R) { return L.ParameterA < R.ParameterA; });
    return Out;
}

std::vector<CurveCrossing> ProfileSolver::SelfIntersections(const NurbsCurve& A) noexcept
{
    std::vector<CurveCrossing> Out;
    std::vector<NurbsCurve> P = A.BezierSegments();
    // spans carry their own knots so parameters are global
    const bool Closed = A.Closed();
    for (size_t I = 0; I < P.size(); ++I)
        for (size_t J = I + 1; J < P.size(); ++J)
        {
            std::vector<Candidate> Raw;
            Subdivide(P[I], P[I].DomainStart(), P[I].DomainEnd(), P[J], P[J].DomainStart(), P[J].DomainEnd(), 0, 1e-7, Raw);
            for (Candidate& K : Raw)
            {
                Polish(A, A, K.S, K.T);
                if (std::fabs(K.S - K.T) < 1e-6) continue;                                         // shared knot between neighbours
                if (Closed && std::fabs(std::fabs(K.S - K.T) - (A.DomainEnd() - A.DomainStart())) < 1e-6) continue;   // seam
                Vec3 Pt = A.Sample(K.S); if (Pt.Distance(A.Sample(K.T)) > ScalarCriteria::MergeTolerance) continue;
                bool Dup = false; for (const CurveCrossing& X : Out) if (X.Point.Distance(Pt) < ScalarCriteria::MergeTolerance) Dup = true;
                if (!Dup) Out.push_back({ K.S, K.T, Pt, false });
            }
        }
    return Out;
}

std::vector<NurbsCurve> ProfileSolver::SplitAt(const NurbsCurve& C, std::vector<double> Parameters) noexcept
{
    const double T0 = C.DomainStart(), T1 = C.DomainEnd();
    std::sort(Parameters.begin(), Parameters.end());
    std::vector<double> Cuts;
    for (double T : Parameters) if (T > T0 + 1e-9 && T < T1 - 1e-9 && (Cuts.empty() || T - Cuts.back() > 1e-9)) Cuts.push_back(T);
    std::vector<NurbsCurve> Out;
    if (Cuts.empty()) { Out.push_back(C); return Out; }
    double Prev = T0;
    for (double T : Cuts) { Out.push_back(C.Trimmed(Prev, T)); Prev = T; }
    Out.push_back(C.Trimmed(Prev, T1));
    // closed curve not cut at the seam: merge last and first so a piece does not start at an arbitrary seam
    if (C.Closed() && std::find_if(Parameters.begin(), Parameters.end(), [&](double T) { return std::fabs(T - T0) < 1e-9 || std::fabs(T - T1) < 1e-9; }) == Parameters.end() && Out.size() > 1)
    {
        Deliver<NurbsCurve> J = NurbsCurve::Join(Out.back(), Out.front());
        if (J) { Out.front() = std::move(J.Payload); Out.pop_back(); }
    }
    return Out;
}

double ProfileSolver::SignedArea(const NurbsCurve& Closed, Vec3 Normal) noexcept
{
    // Green's theorem on a fine tessellation; exact for polylines, 1e-4-sagitta accurate for arcs.
    PlanarAxes Ax = AxesOf(Normal);
    std::vector<Vec3> Pts; Closed.Tessellate(Pts, nullptr, 1e-5);
    double A = 0;
    for (size_t I = 0; I + 1 < Pts.size(); ++I) A += Cross2(Ax.Project(Pts[I]), Ax.Project(Pts[I + 1]));
    A += Cross2(Ax.Project(Pts.back()), Ax.Project(Pts.front()));
    return 0.5 * A;
}

int ProfileSolver::Winding(const NurbsCurve& Closed, Vec3 Normal, Vec3 P) noexcept
{
    PlanarAxes Ax = AxesOf(Normal);
    std::vector<Vec3> Pts; Closed.Tessellate(Pts, nullptr, 1e-5);
    Vec2 Q = Ax.Project(P);
    int W = 0;
    for (size_t I = 0; I < Pts.size(); ++I)
    {
        Vec2 A = Ax.Project(Pts[I]), B = Ax.Project(Pts[(I + 1) % Pts.size()]);
        if (A.Y <= Q.Y) { if (B.Y > Q.Y && Cross2(B - A, Q - A) > 0) ++W; }
        else            { if (B.Y <= Q.Y && Cross2(B - A, Q - A) < 0) --W; }
    }
    return W;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  PROFILE LEVEL
//------------------------------------------------------------------------------------------------------------------------
double Profile::Area() const noexcept { double A = 0; for (const ProfileLoop& L : Loops) A += L.SignedArea; return A; }
int Profile::Winding(Vec3 P) const noexcept { int W = 0; for (const ProfileLoop& L : Loops) W += ProfileSolver::Winding(L.Curve, Normal, P); return W; }
Profile Profile::Reversed() const noexcept { Profile R = *this; for (ProfileLoop& L : R.Loops) { L.Curve = L.Curve.Reversed(); L.SignedArea = -L.SignedArea; } return R; }
std::vector<NurbsCurve> Profile::Curves() const noexcept { std::vector<NurbsCurve> C; for (const ProfileLoop& L : Loops) C.push_back(L.Curve); return C; }

Deliver<Profile> ProfileSolver::Assemble(const std::vector<NurbsCurve>& Loops, Vec3 Normal) noexcept
{
    Profile P; P.Normal = Normal.Normalised();
    Frontier::Plane Ref; bool HaveRef = false;
    for (const NurbsCurve& C : Loops)
    {
        if (!C.Closed()) return Deliver<Profile>::Reject(RefusalReason::OpenWire, "profile loop is not closed");
        if (!SelfIntersections(C).empty()) return Deliver<Profile>::Reject(RefusalReason::SelfIntersecting, "profile loop crosses itself");
        Frontier::Plane Pl; if (!C.Planar(Pl) && C.Poles.size() > 2) return Deliver<Profile>::Reject(RefusalReason::NonPlanar, "profile loop is not planar");
        if (C.Poles.size() > 2)
        {
            if (std::fabs(std::fabs(Pl.Normal.Dot(P.Normal)) - 1.0) > 1e-6) return Deliver<Profile>::Reject(RefusalReason::NonPlanar, "loop normal differs from the profile normal");
            if (HaveRef && std::fabs(Pl.Normal.Dot(P.Normal) * Pl.Offset - Ref.Offset) > ScalarCriteria::MergeTolerance) return Deliver<Profile>::Reject(RefusalReason::NonPlanar, "loops lie in different planes");
            if (!HaveRef) { Ref = Frontier::Plane::FromPointNormal(C.StartPoint(), P.Normal); HaveRef = true; }
        }
        ProfileLoop L; L.Curve = C; L.SignedArea = SignedArea(C, P.Normal);
        if (std::fabs(L.SignedArea) < 1e-12) return Deliver<Profile>::Reject(RefusalReason::DegenerateInput, "profile loop has no area");
        P.Loops.push_back(std::move(L));
    }
    // enclosure depth: count how many *other* loops contain a sample point of this one
    for (size_t I = 0; I < P.Loops.size(); ++I)
    {
        Vec3 S = P.Loops[I].Curve.Sample(P.Loops[I].Curve.DomainStart() + 0.37 * (P.Loops[I].Curve.DomainEnd() - P.Loops[I].Curve.DomainStart()));
        int D = 0;
        for (size_t J = 0; J < P.Loops.size(); ++J) if (J != I && Winding(P.Loops[J].Curve, P.Normal, S) != 0) ++D;
        P.Loops[I].Depth = D;
    }
    return Deliver<Profile>::Accept(std::move(P));
}

Profile ProfileSolver::Normalised(Profile P) noexcept
{
    for (ProfileLoop& L : P.Loops)
    {
        const bool WantCcw = (L.Depth % 2) == 0;
        if ((L.SignedArea > 0) != WantCcw) { L.Curve = L.Curve.Reversed(); L.SignedArea = -L.SignedArea; }
    }
    return P;
}

Deliver<Profile> ProfileSolver::Combine(const Profile& Ain, const Profile& Bin, ProfileOperation Op) noexcept
{
    if (std::fabs(std::fabs(Ain.Normal.Dot(Bin.Normal)) - 1.0) > 1e-6) return Deliver<Profile>::Reject(RefusalReason::NonPlanar, "profiles are not coplanar");
    Profile A = Normalised(Ain), B = Normalised(Bin);
    if (Bin.Normal.Dot(A.Normal) < 0) B = B.Reversed();
    B.Normal = A.Normal;
    PlanarAxes Ax = AxesOf(A.Normal);

    // 1. split every loop of A against every loop of B (and vice versa) at true crossings
    auto SplitAll = [&](const Profile& From, const Profile& Against, int Owner, std::vector<Piece>& Out)
    {
        for (const ProfileLoop& L : From.Loops)
        {
            std::vector<double> Cuts;
            for (const ProfileLoop& M : Against.Loops)
                for (const CurveCrossing& X : Intersect(L.Curve, M.Curve)) Cuts.push_back(X.ParameterA);
            for (NurbsCurve& Pc : SplitAt(L.Curve, Cuts)) { Piece P; P.Curve = std::move(Pc); P.Owner = Owner; Out.push_back(std::move(P)); }
        }
    };
    std::vector<Piece> Pieces;
    SplitAll(A, B, 0, Pieces);
    SplitAll(B, A, 1, Pieces);

    // 2. classify each piece by the winding of the *other* profile just left and just right of its midpoint.
    //    Union keeps what lies outside the other, intersect what lies inside, subtract keeps A outside B plus B inside A
    //    with the B pieces reversed (the complement's boundary runs the other way). Coincident boundary pieces are kept
    //    once, from A, only when the result has material on exactly one side of them.
    for (Piece& P : Pieces)
    {
        const Profile& Other = P.Owner == 0 ? B : A;
        double Tm = 0.5 * (P.Curve.DomainStart() + P.Curve.DomainEnd());
        Vec3 M = P.Curve.Sample(Tm);
        Vec3 Tn = P.Curve.Tangent(Tm), Left = A.Normal.Cross(Tn).Normalised();
        const double Nudge = 1e-5;
        bool Lin = Other.Winding(M + Left * Nudge) != 0, Rin = Other.Winding(M - Left * Nudge) != 0;
        bool Inside = Lin && Rin, Outside = !Lin && !Rin, OnBoundary = Lin != Rin;
        bool FromA = P.Owner == 0;
        switch (Op)
        {
        case ProfileOperation::Union:     P.Keep = Outside || (OnBoundary && FromA && Lin);  break;   // same sense: material left of both
        case ProfileOperation::Intersect: P.Keep = Inside  || (OnBoundary && FromA && Lin);  break;
        case ProfileOperation::Subtract:
            if (FromA) P.Keep = Outside || (OnBoundary && !Lin);                                   // B's material on the right: edge survives
            else { P.Keep = Inside; if (P.Keep) P.Curve = P.Curve.Reversed(); }
            break;
        }
    }

    // 3. chain kept pieces into loops, preferring the sharpest left turn (keeps holes separate from outers at touching vertices)
    Profile Out; Out.Normal = A.Normal;
    for (size_t Seed = 0; Seed < Pieces.size(); ++Seed)
    {
        if (!Pieces[Seed].Keep || Pieces[Seed].Used) continue;
        std::vector<NurbsCurve> Chain; Chain.push_back(Pieces[Seed].Curve); Pieces[Seed].Used = true;
        Vec3 Start = Pieces[Seed].Curve.StartPoint(); Vec3 Head = Pieces[Seed].Curve.EndPoint();
        int Guard = 0;
        while (!Head.Coincident(Start, ScalarCriteria::MergeTolerance) && Guard++ < 4096)
        {
            double InHeading = HeadingAt(Chain.back(), Chain.back().DomainEnd(), Ax);
            int Best = -1; double BestTurn = 1e300;
            for (size_t K = 0; K < Pieces.size(); ++K)
            {
                if (!Pieces[K].Keep || Pieces[K].Used || !Pieces[K].Curve.StartPoint().Coincident(Head, ScalarCriteria::MergeTolerance)) continue;
                double Turn = HeadingAt(Pieces[K].Curve, Pieces[K].Curve.DomainStart(), Ax) - InHeading;
                while (Turn <= -ScalarCriteria::Pi) Turn += ScalarCriteria::TwoPi;
                while (Turn > ScalarCriteria::Pi) Turn -= ScalarCriteria::TwoPi;
                double Rank = ScalarCriteria::Pi - Turn;                                            // smallest rank = sharpest left turn
                if (Rank < BestTurn) { BestTurn = Rank; Best = static_cast<int>(K); }
            }
            if (Best < 0) break;
            Pieces[Best].Used = true; Chain.push_back(Pieces[Best].Curve); Head = Pieces[Best].Curve.EndPoint();
        }
        if (!Head.Coincident(Start, ScalarCriteria::MergeTolerance)) continue;                     // dangling — dropped (tangent touch)
        ProfileLoop L; L.Curve = ChainPieces(std::move(Chain));
        if (!L.Curve.Closed()) continue;
        L.SignedArea = SignedArea(L.Curve, Out.Normal);
        if (std::fabs(L.SignedArea) < 1e-12) continue;
        Out.Loops.push_back(std::move(L));
    }
    for (size_t I = 0; I < Out.Loops.size(); ++I)
    {
        Vec3 S = Out.Loops[I].Curve.Sample(Out.Loops[I].Curve.DomainStart() + 0.37 * (Out.Loops[I].Curve.DomainEnd() - Out.Loops[I].Curve.DomainStart()));
        int D = 0; for (size_t J = 0; J < Out.Loops.size(); ++J) if (J != I && Winding(Out.Loops[J].Curve, Out.Normal, S) != 0) ++D;
        Out.Loops[I].Depth = D;
    }
    return Deliver<Profile>::Accept(std::move(Out));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  PLANAR ARRANGEMENT
//------------------------------------------------------------------------------------------------------------------------
std::vector<PlanarCell> ProfileSolver::Cells(const std::vector<NurbsCurve>& Curves, Vec3 Normal) noexcept
{
    PlanarAxes Ax = AxesOf(Normal);
    // 1. split every curve at every crossing with every other curve (and with itself)
    struct Arc { NurbsCurve Curve; uint32_t Origin; };
    std::vector<Arc> Arcs;
    for (size_t I = 0; I < Curves.size(); ++I)
    {
        std::vector<double> Cuts;
        for (const CurveCrossing& X : SelfIntersections(Curves[I])) { Cuts.push_back(X.ParameterA); Cuts.push_back(X.ParameterB); }
        for (size_t J = 0; J < Curves.size(); ++J)
        {
            if (J == I) continue;
            for (const CurveCrossing& X : Intersect(Curves[I], Curves[J])) Cuts.push_back(X.ParameterA);
            // end points of open curves touching this curve also split it (T-junctions)
            if (!Curves[J].Closed())
                for (Vec3 E : { Curves[J].StartPoint(), Curves[J].EndPoint() })
                {
                    double D = 0; double T = Curves[I].ClosestParameter(E, &D);
                    if (D < ScalarCriteria::MergeTolerance) Cuts.push_back(T);
                }
        }
        // a closed curve with no cuts still needs a seam so it is one arc with distinct ends → split at its start
        for (NurbsCurve& P : SplitAt(Curves[I], Cuts)) Arcs.push_back({ std::move(P), static_cast<uint32_t>(I) });
    }
    // 2. half-edges: each arc both ways, keyed by their end vertices (merged by tolerance)
    std::vector<Vec3> Vertices;
    auto VertexOf = [&](Vec3 P)
    {
        for (size_t V = 0; V < Vertices.size(); ++V) if (Vertices[V].Coincident(P, ScalarCriteria::MergeTolerance)) return static_cast<int>(V);
        Vertices.push_back(P); return static_cast<int>(Vertices.size()) - 1;
    };
    struct Half { int Arc; bool Forward; int From, To; double HeadingOut, HeadingIn; bool Used = false; };
    std::vector<Half> Halves;
    for (size_t A = 0; A < Arcs.size(); ++A)
    {
        const NurbsCurve& C = Arcs[A].Curve;
        int V0 = VertexOf(C.StartPoint()), V1 = VertexOf(C.EndPoint());
        if (V0 == V1 && !C.Closed()) continue;
        double H0 = HeadingAt(C, C.DomainStart(), Ax), H1 = HeadingAt(C, C.DomainEnd(), Ax);
        // heading arriving at a vertex is the reverse of the tangent there
        auto Flip = [](double H) { H += ScalarCriteria::Pi; while (H > ScalarCriteria::Pi) H -= ScalarCriteria::TwoPi; return H; };
        Halves.push_back({ static_cast<int>(A), true,  V0, V1, H0, Flip(H1) });
        Halves.push_back({ static_cast<int>(A), false, V1, V0, Flip(H1), H0 });
    }
    // prune dangling half-edges (vertices of degree 1) repeatedly: they cannot bound a cell
    for (bool Changed = true; Changed;)
    {
        Changed = false;
        std::vector<int> Degree(Vertices.size(), 0);
        for (const Half& H : Halves) if (!H.Used) ++Degree[H.From];
        for (Half& H : Halves) if (!H.Used && (Degree[H.From] <= 1 || Degree[H.To] <= 1)) { H.Used = true; Changed = true; }
    }
    // 3. trace cycles: from each unused half-edge, at every vertex take the next half-edge counter-clockwise from the
    //    reverse of the arrival direction (leftmost turn) → each bounded face once as a ccw loop, the outer face once cw.
    std::vector<PlanarCell> Cells;
    std::vector<std::vector<int>> Outgoing(Vertices.size());
    for (size_t H = 0; H < Halves.size(); ++H) if (!Halves[H].Used) Outgoing[Halves[H].From].push_back(static_cast<int>(H));
    for (size_t Seed = 0; Seed < Halves.size(); ++Seed)
    {
        if (Halves[Seed].Used) continue;
        std::vector<int> Cycle; int Cur = static_cast<int>(Seed); int Guard = 0;
        while (Guard++ < 100000)
        {
            Halves[Cur].Used = true; Cycle.push_back(Cur);
            const Half& H = Halves[Cur];
            // candidates leaving H.To; pick the smallest clockwise turn from the reversed arrival heading (leftmost)
            // Leftmost turn: among half-edges leaving H.To, the one whose outgoing heading is the first encountered
            //    rotating clockwise from the twin's heading (the direction back along the arc). Walking a face keeps it on
            //    the left, so every bounded face is traced counter-clockwise exactly once.
            double Back = H.HeadingIn;
            int Best = -1; double BestTurn = 1e300;
            for (int Cand : Outgoing[H.To])
            {
                const Half& K = Halves[Cand];
                bool Twin = K.Arc == H.Arc && K.Forward != H.Forward;
                double Turn = Back - K.HeadingOut;                                                   // clockwise angle from Back to K
                while (Turn <= 1e-9) Turn += ScalarCriteria::TwoPi;
                while (Turn > ScalarCriteria::TwoPi + 1e-9) Turn -= ScalarCriteria::TwoPi;
                if (Twin) Turn = ScalarCriteria::TwoPi;                                              // U-turn only when nothing else leaves
                if (Turn < BestTurn) { BestTurn = Turn; Best = Cand; }
            }
            if (Best < 0) break;
            if (Best == static_cast<int>(Seed)) break;
            if (Halves[Best].Used) { Cycle.clear(); break; }                                         // merged into a traced cycle: not a face
            Cur = Best;
        }
        if (Cycle.empty() || Halves[Cycle.back()].To != Halves[Seed].From) continue;
        std::vector<NurbsCurve> Parts; std::vector<uint32_t> Src;
        for (int Hx : Cycle) { const Half& H = Halves[Hx]; Parts.push_back(H.Forward ? Arcs[H.Arc].Curve : Arcs[H.Arc].Curve.Reversed()); Src.push_back(Arcs[H.Arc].Origin); }
        NurbsCurve Loop = ChainPieces(std::move(Parts));
        if (!Loop.Closed()) continue;
        double Area = SignedArea(Loop, Normal);
        if (Area <= 1e-12) continue;                                                                 // the unbounded face runs clockwise
        PlanarCell Cell; Cell.Outer = std::move(Loop); Cell.Area = Area;
        std::sort(Src.begin(), Src.end()); Src.erase(std::unique(Src.begin(), Src.end()), Src.end()); Cell.Origins = std::move(Src);
        Cells.push_back(std::move(Cell));
    }
    // 4. containment: a cell nested directly inside another becomes that cell's hole (largest first so parents come first)
    std::sort(Cells.begin(), Cells.end(), [](const PlanarCell& A, const PlanarCell& B) { return A.Area > B.Area; });
    std::vector<int> Parent(Cells.size(), -1);
    for (size_t I = 0; I < Cells.size(); ++I)
    {
        // Sample points strictly inside cell I: boundary samples nudged inward; cells sharing an edge with I must not be
        //    fooled by a sample on that shared edge, so several samples vote and a container needs a majority.
        std::vector<Vec3> Samples;
        for (double F : { 0.11, 0.29, 0.43, 0.61, 0.77, 0.93 })
        {
            double T0 = Cells[I].Outer.DomainStart() + F * (Cells[I].Outer.DomainEnd() - Cells[I].Outer.DomainStart());
            Vec3 Tan = Cells[I].Outer.Tangent(T0); if (Tan.LengthSquared() < 1e-30) continue;
            Vec3 S = Cells[I].Outer.Sample(T0) + Normal.Normalised().Cross(Tan).Normalised() * 1e-5;
            if (Winding(Cells[I].Outer, Normal, S) != 0) Samples.push_back(S);
        }
        for (size_t J = 0; J < I; ++J)                                                               // smaller index = larger area
        {
            size_t Votes = 0; for (const Vec3& S : Samples) if (Winding(Cells[J].Outer, Normal, S) != 0) ++Votes;
            if (!Samples.empty() && Votes * 2 > Samples.size()) Parent[I] = static_cast<int>(J);        // the last (smallest) container wins
        }
    }
    for (size_t I = 0; I < Cells.size(); ++I)
    {
        int D = 0; for (int P = Parent[I]; P >= 0; P = Parent[P]) ++D;
        Cells[I].Depth = D;
        if (Parent[I] >= 0) { Cells[Parent[I]].Holes.push_back(Cells[I].Outer.Reversed()); Cells[Parent[I]].Area -= Cells[I].Area; }
    }
    return Cells;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SKETCH EDITS
//------------------------------------------------------------------------------------------------------------------------
namespace
{
    // Replace each chosen corner of a piecewise curve with a connector built by `Connect(P0, Corner, P1) → curve`.
    template<typename Connector>
    Deliver<NurbsCurve> ReplaceCorners(const NurbsCurve& C, double Distance, const std::vector<int>* Corners, Connector Connect) noexcept
    {
        if (Distance <= ScalarCriteria::KernelTolerance) return Deliver<NurbsCurve>::Reject(RefusalReason::DegenerateInput, "zero distance");
        std::vector<Corner> All = CornersOf(C);
        if (All.empty()) return Deliver<NurbsCurve>::Reject(RefusalReason::DegenerateInput, "curve has no corners");
        std::vector<bool> Pick(All.size(), Corners == nullptr);
        if (Corners) for (int I : *Corners) if (I >= 0 && I < static_cast<int>(All.size())) Pick[I] = true;
        // cut parameters: setback along each side of the corner, measured by arc length
        const bool Closed = C.Closed();
        const double Span = C.DomainEnd() - C.DomainStart();
        struct Cut { double Before, After; Vec3 P0, P1; Corner K; bool On; double Setback; };
        std::vector<Cut> Cuts;
        for (size_t I = 0; I < All.size(); ++I)
        {
            const Corner& K = All[I];
            double HalfAngle = 0.5 * std::acos(ScalarCriteria::Clamp(K.In.Dot(K.Out), -1.0, 1.0));
            double Setback = Connect.Setback(Distance, HalfAngle);
            if (!std::isfinite(Setback) || Setback <= 0) return Deliver<NurbsCurve>::Reject(RefusalReason::DegenerateInput, "corner too flat for that distance");
            double Lk = C.Trimmed(C.DomainStart(), K.T).Length();
            double Total = C.Length();
            double Lb = Lk - Setback, La = Lk + Setback;
            if (Closed) { if (Lb < 0) Lb += Total; if (La > Total) La -= Total; }
            else if (Lb < -1e-9 || La > Total + 1e-9) return Deliver<NurbsCurve>::Reject(RefusalReason::DegenerateInput, "setback exceeds the neighbouring segment");
            Cut Q; Q.Before = C.ParameterAtLength(std::max(Lb, 0.0)); Q.After = C.ParameterAtLength(std::min(La, Total));
            Q.P0 = C.Sample(Q.Before); Q.P1 = C.Sample(Q.After); Q.K = K; Q.On = Pick[I]; Q.Setback = Setback;
            Cuts.push_back(Q);
        }
        // adjacent setbacks must not overlap: a corner whose setback collides with a neighbour is left sharp
        for (size_t I = 0; I < Cuts.size(); ++I)
        {
            size_t J = I + 1; if (J == Cuts.size()) { if (!Closed) break; J = 0; }
            if (!Cuts[I].On || !Cuts[J].On) continue;
            double AtI = C.Trimmed(C.DomainStart(), Cuts[I].K.T).Length(), AtJ = C.Trimmed(C.DomainStart(), Cuts[J].K.T).Length();
            double Gap = J == 0 ? (C.Length() - AtI) + AtJ : AtJ - AtI;
            double Need = Cuts[I].Setback + Cuts[J].Setback;
            if (Need > Gap + 1e-9) { Cuts[I].On = false; Cuts[J].On = false; }
        }
        if (std::none_of(Cuts.begin(), Cuts.end(), [](const Cut& Q) { return Q.On; })) return Deliver<NurbsCurve>::Reject(RefusalReason::DegenerateInput, "every corner's setback collides with its neighbour — use a smaller distance");
        // rebuild: walk the curve, splicing connectors in
        std::vector<NurbsCurve> Parts;
        auto FirstOn = std::find_if(Cuts.begin(), Cuts.end(), [](const Cut& Q) { return Q.On; });
        if (FirstOn == Cuts.end()) return Deliver<NurbsCurve>::Accept(C);
        // start point: for a closed curve start just after the first corner's connector; for open, at the curve start
        double Cursor = Closed ? FirstOn->After : C.DomainStart();
        size_t Begin = static_cast<size_t>(FirstOn - Cuts.begin());
        size_t N = Cuts.size();
        for (size_t Step = 0; Step < N; ++Step)
        {
            size_t Idx = (Begin + (Closed ? 1 : 0) + Step) % N;
            if (Closed && Step == N - 1 && Idx != Begin) { /* fallthrough handled by ordering */ }
            const Cut& Q = Cuts[Idx];
            if (!Q.On) continue;
            // segment from Cursor to Q.Before (wrap-aware)
            if (Q.Before < Cursor - 1e-9) { Parts.push_back(C.Trimmed(Cursor, C.DomainEnd())); Parts.push_back(C.Trimmed(C.DomainStart(), Q.Before)); }
            else if (Q.Before > Cursor + 1e-9) Parts.push_back(C.Trimmed(Cursor, Q.Before));
            Parts.push_back(Connect(Q.P0, Q.K.P, Q.P1, Q.K.In, Q.K.Out, Distance));
            Cursor = Q.After;
            (void)Span;
        }
        if (Closed)
        {
            // close back to the start of the walk (FirstOn->After)
            double End = FirstOn->After;
            if (End < Cursor - 1e-9) { Parts.push_back(C.Trimmed(Cursor, C.DomainEnd())); if (End > C.DomainStart() + 1e-9) Parts.push_back(C.Trimmed(C.DomainStart(), End)); }
            else if (End > Cursor + 1e-9) Parts.push_back(C.Trimmed(Cursor, End));
        }
        else if (Cursor < C.DomainEnd() - 1e-9) Parts.push_back(C.Trimmed(Cursor, C.DomainEnd()));
        // drop empty pieces then chain
        std::vector<NurbsCurve> Live; for (NurbsCurve& P : Parts) if (P.Poles.size() >= 2 && P.Length() > ScalarCriteria::KernelTolerance) Live.push_back(std::move(P));
        NurbsCurve R = ChainPieces(std::move(Live));
        R.Classification = CurveClassification::Freeform;
        return Deliver<NurbsCurve>::Accept(std::move(R));
    }

    struct FilletConnector
    {
        double Setback(double Radius, double HalfAngle) const noexcept { return Radius / std::tan(HalfAngle); }   // HalfAngle = half the turn's supplement
        NurbsCurve operator()(Vec3 P0, Vec3 Corner, Vec3 P1, Vec3 In, Vec3 Out, double Radius) const noexcept
        {
            // exact rational arc tangent to both sides: circle centre along the bisector
            Vec3 Bis = (Out - In).Normalised();                                          // points into the turn
            double Half = 0.5 * (ScalarCriteria::Pi - std::acos(ScalarCriteria::Clamp(In.Dot(Out), -1.0, 1.0)));
            Vec3 Centre = Corner + Bis * (Radius / std::sin(Half));
            Vec3 N = In.Cross(Out).Normalised();
            Vec3 R0 = P0 - Centre;
            double Sweep = std::acos(ScalarCriteria::Clamp((P0 - Centre).Normalised().Dot((P1 - Centre).Normalised()), -1.0, 1.0));
            Vec3 U = R0.Normalised(), V = N.Cross(U);
            // sign: from P0 towards P1 about N
            if ((P1 - Centre).Dot(V) < 0) { N = N * -1.0; V = V * -1.0; }
            (void)Sweep;
            double Ang = std::atan2((P1 - Centre).Dot(V), (P1 - Centre).Dot(U));
            Deliver<NurbsCurve> Arc = NurbsCurve::Arc(Centre, N, Radius, 0.0, Ang);
            // Arc() measures angle 0 from its own basis; rebuild so 0 lands on P0 by rotating: build from the three points instead
            Deliver<NurbsCurve> Three = NurbsCurve::ArcThreePoints(P0, Centre + (U * std::cos(0.5 * Ang) + V * std::sin(0.5 * Ang)) * Radius, P1);
            return Three ? Three.Payload : Arc.Payload;
        }
    };
    struct ChamferConnector
    {
        double Setback(double Distance, double) const noexcept { return Distance; }
        NurbsCurve operator()(Vec3 P0, Vec3, Vec3 P1, Vec3, Vec3, double) const noexcept { return NurbsCurve::Line(P0, P1).Payload; }
    };
}

Deliver<NurbsCurve> ProfileSolver::Filleted(const NurbsCurve& C, double Radius, const std::vector<int>* Corners) noexcept
{
    return ReplaceCorners(C, Radius, Corners, FilletConnector{});
}

Deliver<NurbsCurve> ProfileSolver::Chamfered(const NurbsCurve& C, double Setback, const std::vector<int>* Corners) noexcept
{
    return ReplaceCorners(C, Setback, Corners, ChamferConnector{});
}

Deliver<NurbsCurve> ProfileSolver::Offset(const NurbsCurve& C, double Distance, Vec3 Normal) noexcept
{
    if (std::fabs(Distance) <= ScalarCriteria::KernelTolerance) return Deliver<NurbsCurve>::Accept(C);
    Vec3 N = Normal.Normalised();
    // Piecewise: lines and arcs offset exactly (arc radius ± d), free-form pieces by pole-wise normal offset of a fine
    //    interpolation. Pieces are re-joined by arcs at convex corners and trimmed at concave ones.
    // Work span by span (Bézier pieces): every line and every rational-quadratic arc offsets exactly; anything else is
    //    sampled, shifted along the left normal and re-interpolated. Tangent joints stay coincident, corners get a bridge.
    std::vector<NurbsCurve> Pieces;
    for (const NurbsCurve& K : SplitAtKinks(C)) for (NurbsCurve& S : K.BezierSegments()) if (S.Length() > ScalarCriteria::KernelTolerance) Pieces.push_back(std::move(S));
    std::vector<NurbsCurve> Shifted;
    for (const NurbsCurve& P : Pieces)
    {
        const double Tm = 0.5 * (P.DomainStart() + P.DomainEnd());
        if (Flat(P, ScalarCriteria::KernelTolerance))
        {
            Vec3 T = (P.EndPoint() - P.StartPoint()).Normalised(), L = N.Cross(T);
            Shifted.push_back(NurbsCurve::Line(P.StartPoint() + L * Distance, P.EndPoint() + L * Distance).Payload);
            continue;
        }
        if (P.Rational() && P.Degree == 2)
        {
            double K = P.Curvature(Tm);
            Vec3 D[3]; P.Derivatives(Tm, 2, D);
            if (K > 1e-9)
            {
                double R = 1.0 / K; Vec3 LeftN = N.Cross(D[1]).Normalised();
                bool TurnsLeft = D[2].Dot(LeftN) > 0;
                Vec3 Centre = D[0] + (TurnsLeft ? LeftN : LeftN * -1.0) * R;
                double Rn = TurnsLeft ? R - Distance : R + Distance;
                if (Rn <= ScalarCriteria::KernelTolerance) return Deliver<NurbsCurve>::Reject(RefusalReason::DegenerateInput, "offset collapses an arc");
                Vec3 A = Centre + (P.StartPoint() - Centre).Normalised() * Rn, B = Centre + (P.EndPoint() - Centre).Normalised() * Rn;
                Vec3 Mid = Centre + (P.Sample(Tm) - Centre).Normalised() * Rn;
                Deliver<NurbsCurve> Arc = NurbsCurve::ArcThreePoints(A, Mid, B); if (Arc) { Shifted.push_back(Arc.Payload); continue; }
            }
        }
        std::vector<Vec3> Pts, Src; std::vector<double> Prm; P.Tessellate(Src, &Prm, 1e-5);
        for (size_t I = 0; I < Src.size(); ++I) { Vec3 T = P.Tangent(Prm[I]); Pts.push_back(Src[I] + N.Cross(T).Normalised() * Distance); }
        std::vector<Vec3> Thin; for (size_t I = 0; I < Pts.size(); I += std::max<size_t>(1, Pts.size() / 24)) Thin.push_back(Pts[I]);
        if (!Thin.back().Coincident(Pts.back(), 1e-12)) Thin.push_back(Pts.back());
        Deliver<NurbsCurve> S = NurbsCurve::Interpolate(Thin, 3, false); if (S) Shifted.push_back(S.Payload);
    }
    if (Shifted.empty()) return Deliver<NurbsCurve>::Reject(RefusalReason::DegenerateInput, "nothing to offset");
    if (Shifted.size() == 1 && !C.Closed()) { NurbsCurve R = Shifted.front(); return Deliver<NurbsCurve>::Accept(std::move(R)); }
    // connect consecutive pieces: intersect if they cross (concave), else bridge with an arc about the original corner (convex)
    const bool Closed = C.Closed();
    size_t Count = Shifted.size();
    std::vector<NurbsCurve> Out;
    auto Connect = [&](NurbsCurve& Prev, NurbsCurve& Next, Vec3 Corner)
    {
        std::vector<CurveCrossing> X = Intersect(Prev, Next, 1e-7);
        if (!X.empty())
        {
            const CurveCrossing& K = X.back();
            Prev = Prev.Trimmed(Prev.DomainStart(), K.ParameterA); Next = Next.Trimmed(K.ParameterB, Next.DomainEnd());
            return NurbsCurve{};
        }
        Vec3 A = Prev.EndPoint(), B = Next.StartPoint();
        if (A.Coincident(B, ScalarCriteria::MergeTolerance)) return NurbsCurve{};
        Vec3 Mid = Corner + ((A - Corner).Normalised() + (B - Corner).Normalised()).Normalised() * std::fabs(Distance);
        Deliver<NurbsCurve> Arc = NurbsCurve::ArcThreePoints(A, Mid, B);
        return Arc ? Arc.Payload : NurbsCurve::Line(A, B).Payload;
    };
    std::vector<NurbsCurve> Bridges(Count);
    for (size_t I = 0; I + 1 < Count || (Closed && I < Count); ++I)
    {
        size_t J = (I + 1) % Count; if (!Closed && J == 0) break;
        Bridges[I] = Connect(Shifted[I], Shifted[J], Pieces[I].EndPoint());
    }
    for (size_t I = 0; I < Count; ++I) { Out.push_back(Shifted[I]); if (Bridges[I].Poles.size() >= 2) Out.push_back(Bridges[I]); }
    NurbsCurve R = ChainPieces(std::move(Out));
    return Deliver<NurbsCurve>::Accept(std::move(R));
}

Deliver<std::vector<NurbsCurve>> ProfileSolver::Trimmed(const NurbsCurve& C, const std::vector<NurbsCurve>& Cutters, Vec3 Near) noexcept
{
    std::vector<double> Cuts;
    for (const NurbsCurve& K : Cutters) for (const CurveCrossing& X : Intersect(C, K)) Cuts.push_back(X.ParameterA);
    if (Cuts.empty()) return Deliver<std::vector<NurbsCurve>>::Reject(RefusalReason::DegenerateInput, "cutters do not cross the curve");
    std::vector<NurbsCurve> Pieces = SplitAt(C, Cuts);
    if (Pieces.size() < 2) return Deliver<std::vector<NurbsCurve>>::Reject(RefusalReason::DegenerateInput, "only one piece after cutting");
    // remove the piece nearest to Near
    size_t Drop = 0; double Best = 1e300;
    for (size_t I = 0; I < Pieces.size(); ++I) { double D = 0; (void)Pieces[I].ClosestParameter(Near, &D); if (D < Best) { Best = D; Drop = I; } }
    Pieces.erase(Pieces.begin() + static_cast<std::ptrdiff_t>(Drop));
    return Deliver<std::vector<NurbsCurve>>::Accept(std::move(Pieces));
}

Deliver<NurbsCurve> ProfileSolver::Joined(std::vector<NurbsCurve> Pieces) noexcept
{
    if (Pieces.empty()) return Deliver<NurbsCurve>::Reject(RefusalReason::DegenerateInput, "nothing to join");
    std::vector<bool> Used(Pieces.size(), false);
    NurbsCurve R = Pieces.front(); Used[0] = true;
    for (size_t Round = 1; Round < Pieces.size(); ++Round)
    {
        bool Grew = false;
        for (size_t I = 0; I < Pieces.size() && !Grew; ++I)
        {
            if (Used[I]) continue;
            const NurbsCurve& P = Pieces[I];
            if (P.StartPoint().Coincident(R.EndPoint(), ScalarCriteria::MergeTolerance))          { Deliver<NurbsCurve> J = NurbsCurve::Join(R, P); if (J) { R = J.Payload; Used[I] = Grew = true; } }
            else if (P.EndPoint().Coincident(R.EndPoint(), ScalarCriteria::MergeTolerance))       { Deliver<NurbsCurve> J = NurbsCurve::Join(R, P.Reversed()); if (J) { R = J.Payload; Used[I] = Grew = true; } }
            else if (P.EndPoint().Coincident(R.StartPoint(), ScalarCriteria::MergeTolerance))     { Deliver<NurbsCurve> J = NurbsCurve::Join(P, R); if (J) { R = J.Payload; Used[I] = Grew = true; } }
            else if (P.StartPoint().Coincident(R.StartPoint(), ScalarCriteria::MergeTolerance))   { Deliver<NurbsCurve> J = NurbsCurve::Join(P.Reversed(), R); if (J) { R = J.Payload; Used[I] = Grew = true; } }
        }
        if (!Grew) return Deliver<NurbsCurve>::Reject(RefusalReason::OpenWire, "pieces do not connect end to end");
    }
    if (R.Closed()) R.Classification = CurveClassification::Freeform;
    return Deliver<NurbsCurve>::Accept(std::move(R));
}

} // namespace Frontier
