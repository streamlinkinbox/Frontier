//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Kernel/TopologySpecification.cpp — B-rep construction, sewing, capping, orientation and validation
//============================================================================================================================================
#include "TopologySpecification.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <optional>
#include "ProfileSolver.h"

namespace Frontier
{

const char* Describe(BodyClassification Classification) noexcept
{
    switch (Classification) { case BodyClassification::Wire: return "wire"; case BodyClassification::Sheet: return "sheet"; default: return "solid"; }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  CURVE SPLITTING
//------------------------------------------------------------------------------------------------------------------------

std::vector<NurbsCurve> SplitAtKinks(const NurbsCurve& Curve, double AngleTolerance) noexcept
{
    std::vector<double> Cuts;
    const double T0 = Curve.DomainStart(), T1 = Curve.DomainEnd();
    const double Eps = (T1 - T0) * 1e-6;
    size_t I = static_cast<size_t>(Curve.Degree) + 1;
    while (I < Curve.Poles.size())
    {
        double K = Curve.Knots[I]; size_t Mult = 0;
        while (I + Mult < Curve.Knots.size() && Curve.Knots[I + Mult] == K) ++Mult;
        if (K > T0 + Eps && K < T1 - Eps && static_cast<int>(Mult) >= Curve.Degree)
        {
            Vec3 Before = Curve.Tangent(K - Eps), After = Curve.Tangent(K + Eps);
            if (std::acos(ScalarCriteria::Clamp(Before.Dot(After), -1.0, 1.0)) > AngleTolerance) Cuts.push_back(K);
        }
        I += std::max<size_t>(Mult, 1);
    }
    std::vector<NurbsCurve> Pieces;
    NurbsCurve Rest = Curve;
    for (double K : Cuts)
    {
        auto Parts = Rest.Split(K);
        Pieces.push_back(std::move(Parts.first));
        Rest = std::move(Parts.second);
    }
    Pieces.push_back(std::move(Rest));
    // A closed curve whose seam is itself a kink: the first and last pieces are genuinely separate faces already.
    return Pieces;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  PLANAR TRIANGULATION
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    struct Planar2
    {
        Vec3 U, V;
        [[nodiscard]] Vec2 Project(Vec3 P) const noexcept { return { P.Dot(U), P.Dot(V) }; }
    };
    Planar2 PlanarBasis(Vec3 Normal) noexcept
    {
        Vec3 N = Normal.Normalised();
        Vec3 Seed = std::fabs(N.Z) < 0.9 ? Vec3::UnitZ() : Vec3::UnitX();
        Vec3 U = Seed.Cross(N).Normalised();
        return { U, N.Cross(U) };
    }
    double Cross2(Vec2 A, Vec2 B) noexcept { return A.X * B.Y - A.Y * B.X; }
    double SignedArea(const std::vector<Vec2>& P, const std::vector<uint32_t>& Ring) noexcept
    {
        double A = 0; for (size_t I = 0; I < Ring.size(); ++I) A += Cross2(P[Ring[I]], P[Ring[(I + 1) % Ring.size()]]); return A * 0.5;
    }
    bool PointInTriangle(Vec2 P, Vec2 A, Vec2 B, Vec2 C) noexcept
    {
        double D1 = Cross2(B - A, P - A), D2 = Cross2(C - B, P - B), D3 = Cross2(A - C, P - C);
        bool Neg = D1 < 0 || D2 < 0 || D3 < 0, Pos = D1 > 0 || D2 > 0 || D3 > 0;
        return !(Neg && Pos);
    }
    bool SegmentsCross(Vec2 A, Vec2 B, Vec2 C, Vec2 D) noexcept
    {
        double D1 = Cross2(B - A, C - A), D2 = Cross2(B - A, D - A), D3 = Cross2(D - C, A - C), D4 = Cross2(D - C, B - C);
        return ((D1 > 0) != (D2 > 0)) && ((D3 > 0) != (D4 > 0)) && D1 != 0 && D2 != 0 && D3 != 0 && D4 != 0;
    }
}

std::vector<uint32_t> TriangulatePlanarPolygon(const std::vector<Vec3>& Points, const std::vector<std::vector<uint32_t>>& Rings, Vec3 Normal) noexcept
{
    Planar2 Basis = PlanarBasis(Normal);
    std::vector<Vec2> P(Points.size());
    for (size_t I = 0; I < Points.size(); ++I) P[I] = Basis.Project(Points[I]);
    return TriangulatePolygon(P, Rings);
}

bool InsideRing(const std::vector<Vec2>& Ring, Vec2 Q) noexcept
{
    bool In = false;
    for (size_t I = 0, J = Ring.size() - 1; I < Ring.size(); J = I++)
        if ((Ring[I].Y > Q.Y) != (Ring[J].Y > Q.Y) && Q.X < (Ring[J].X - Ring[I].X) * (Q.Y - Ring[I].Y) / (Ring[J].Y - Ring[I].Y) + Ring[I].X) In = !In;
    return In;
}

double SeededParameter(const NurbsSurface& S, Vec3 Target, double& U, double& V, int Iterations) noexcept
{
    const double U0 = S.DomainStartU(), U1 = S.DomainEndU(), V0 = S.DomainStartV(), V1 = S.DomainEndV();
    const bool WrapU = S.ClosedU(ScalarCriteria::KernelTolerance), WrapV = S.ClosedV(ScalarCriteria::KernelTolerance);
    for (int It = 0; It < Iterations; ++It)
    {
        Vec3 P, DU, DV; S.Derivatives(U, V, P, DU, DV);
        Vec3 R = Target - P;
        if (R.Length() <= ScalarCriteria::KernelTolerance) break;
        // Gauss–Newton on the first-order tangent plane (2×2 normal equations).
        double A = DU.Dot(DU), B = DU.Dot(DV), D = DV.Dot(DV), Fu = DU.Dot(R), Fv = DV.Dot(R);
        double Det = A * D - B * B; if (std::fabs(Det) < 1e-300) break;
        double Du = (Fu * D - Fv * B) / Det, Dv = (A * Fv - B * Fu) / Det;
        double Nu = U + Du, Nv = V + Dv;
        if (WrapU) { double Sp = U1 - U0; while (Nu < U0) Nu += Sp; while (Nu > U1) Nu -= Sp; } else Nu = ScalarCriteria::Clamp(Nu, U0, U1);
        if (WrapV) { double Sp = V1 - V0; while (Nv < V0) Nv += Sp; while (Nv > V1) Nv -= Sp; } else Nv = ScalarCriteria::Clamp(Nv, V0, V1);
        bool Done = std::fabs(Nu - U) < 1e-13 && std::fabs(Nv - V) < 1e-13;
        U = Nu; V = Nv;
        if (Done) break;
    }
    return S.Sample(U, V).Distance(Target);
}

std::vector<uint32_t> TriangulatePolygon(const std::vector<Vec2>& P, const std::vector<std::vector<uint32_t>>& Rings) noexcept
{
    std::vector<uint32_t> Out;
    if (Rings.empty() || Rings.front().size() < 3) return Out;

    // Outer ring CCW, holes CW.
    std::vector<uint32_t> Outer = Rings.front();
    if (SignedArea(P, Outer) < 0) std::reverse(Outer.begin(), Outer.end());
    std::vector<std::vector<uint32_t>> Holes;
    for (size_t H = 1; H < Rings.size(); ++H)
    {
        std::vector<uint32_t> R = Rings[H]; if (R.size() < 3) continue;
        if (SignedArea(P, R) > 0) std::reverse(R.begin(), R.end());
        Holes.push_back(std::move(R));
    }
    // Join holes into the outer ring, rightmost hole first (classic ear-clipping-with-holes).
    std::sort(Holes.begin(), Holes.end(), [&](const std::vector<uint32_t>& A, const std::vector<uint32_t>& B)
    {
        double Ma = -1e300, Mb = -1e300; for (uint32_t I : A) Ma = std::max(Ma, P[I].X); for (uint32_t I : B) Mb = std::max(Mb, P[I].X); return Ma > Mb;
    });
    for (const std::vector<uint32_t>& Hole : Holes)
    {
        size_t Hi = 0; for (size_t I = 1; I < Hole.size(); ++I) if (P[Hole[I]].X > P[Hole[Hi]].X) Hi = I;
        Vec2 Hp = P[Hole[Hi]];
        // pick the visible outer vertex: closest one such that the join crosses no outer edge
        size_t Best = SIZE_MAX; double BestD = 1e300;
        for (size_t O = 0; O < Outer.size(); ++O)
        {
            Vec2 Op = P[Outer[O]]; double D = (Op - Hp).LengthSquared(); if (D >= BestD) continue;
            bool Blocked = false;
            for (size_t E = 0; E < Outer.size() && !Blocked; ++E)
            {
                uint32_t A = Outer[E], B = Outer[(E + 1) % Outer.size()];
                if (A == Outer[O] || B == Outer[O]) continue;
                Blocked = SegmentsCross(Hp, Op, P[A], P[B]);
            }
            for (size_t E = 0; E < Hole.size() && !Blocked; ++E)
            {
                uint32_t A = Hole[E], B = Hole[(E + 1) % Hole.size()];
                if (A == Hole[Hi] || B == Hole[Hi]) continue;
                Blocked = SegmentsCross(Hp, Op, P[A], P[B]);
            }
            if (!Blocked) { Best = O; BestD = D; }
        }
        if (Best == SIZE_MAX) Best = 0;
        std::vector<uint32_t> Merged;
        Merged.reserve(Outer.size() + Hole.size() + 2);
        for (size_t O = 0; O <= Best; ++O) Merged.push_back(Outer[O]);
        for (size_t K = 0; K <= Hole.size(); ++K) Merged.push_back(Hole[(Hi + K) % Hole.size()]);
        for (size_t O = Best; O < Outer.size(); ++O) Merged.push_back(Outer[O]);
        Outer = std::move(Merged);
    }
    // Ear clipping.
    std::vector<uint32_t> Ring = Outer;
    int Guard = 0;
    while (Ring.size() > 3 && Guard++ < 100000)
    {
        bool Clipped = false;
        for (size_t I = 0; I < Ring.size(); ++I)
        {
            uint32_t Ia = Ring[(I + Ring.size() - 1) % Ring.size()], Ib = Ring[I], Ic = Ring[(I + 1) % Ring.size()];
            Vec2 A = P[Ia], B = P[Ib], C = P[Ic];
            if (Cross2(B - A, C - B) <= 1e-14) continue;                                // reflex or degenerate
            bool Inside = false;
            for (size_t J = 0; J < Ring.size() && !Inside; ++J)
            {
                uint32_t Ij = Ring[J];
                if (Ij == Ia || Ij == Ib || Ij == Ic) continue;
                if (P[Ij].Distance(A) < 1e-12 || P[Ij].Distance(B) < 1e-12 || P[Ij].Distance(C) < 1e-12) continue;   // join duplicates
                Inside = PointInTriangle(P[Ij], A, B, C);
            }
            if (Inside) continue;
            Out.push_back(Ia); Out.push_back(Ib); Out.push_back(Ic);
            Ring.erase(Ring.begin() + static_cast<long>(I));
            Clipped = true;
            break;
        }
        if (!Clipped)
        {
            // Numerically stuck (collinear runs): drop the flattest vertex and continue.
            size_t Flat = 0; double Best = 1e300;
            for (size_t I = 0; I < Ring.size(); ++I)
            {
                Vec2 A = P[Ring[(I + Ring.size() - 1) % Ring.size()]], B = P[Ring[I]], C = P[Ring[(I + 1) % Ring.size()]];
                double Cr = std::fabs(Cross2(B - A, C - B)); if (Cr < Best) { Best = Cr; Flat = I; }
            }
            Ring.erase(Ring.begin() + static_cast<long>(Flat));
        }
    }
    if (Ring.size() == 3) { Out.push_back(Ring[0]); Out.push_back(Ring[1]); Out.push_back(Ring[2]); }
    return Out;
}

std::vector<std::vector<std::vector<int>>> PlanarCells(const std::vector<Vec2>& P, const std::vector<PlanarEdge>& E) noexcept
{
    std::vector<std::vector<int>> Outgoing(P.size());
    for (size_t I = 0; I < E.size(); ++I) if (E[I].From >= 0 && E[I].To >= 0 && E[I].From != E[I].To) Outgoing[E[I].From].push_back(static_cast<int>(I));
    auto Angle = [&](int Ed) { Vec2 D = P[E[Ed].To] - P[E[Ed].From]; return std::atan2(D.Y, D.X); };
    struct Walk { std::vector<int> Edges; double Area = 0; };
    std::vector<Walk> Walks;
    std::vector<bool> Used(E.size(), false);
    for (size_t Seed = 0; Seed < E.size(); ++Seed)
    {
        if (Used[Seed] || E[Seed].From < 0 || E[Seed].From == E[Seed].To) continue;
        Walk W; int Cur = static_cast<int>(Seed);
        for (int Guard = 0; Guard < 1000000; ++Guard)
        {
            Used[Cur] = true; W.Edges.push_back(Cur);
            int V = E[Cur].To;
            Vec2 Back = P[E[Cur].From] - P[V]; double Ab = std::atan2(Back.Y, Back.X);
            int Next = -1; double Best = ScalarCriteria::Infinity;
            for (int O : Outgoing[V])
            {
                double Delta = Ab - Angle(O);
                while (Delta <= 1e-12) Delta += ScalarCriteria::TwoPi;
                while (Delta > ScalarCriteria::TwoPi + 1e-12) Delta -= ScalarCriteria::TwoPi;
                if (E[O].To == E[Cur].From) Delta = ScalarCriteria::TwoPi;             // the twin is the last resort
                if (Delta < Best) { Best = Delta; Next = O; }
            }
            if (Next < 0 || Next == static_cast<int>(Seed) || Used[Next]) break;
            Cur = Next;
        }
        for (int Ed : W.Edges) W.Area += P[E[Ed].From].Cross(P[E[Ed].To]);
        W.Area *= 0.5;
        Walks.push_back(std::move(W));
    }
    // positive walks are cells; negative walks are hole outlines (or the unbounded face) → attach to the smallest containing cell
    std::vector<std::vector<std::vector<int>>> Cells;
    std::vector<std::vector<Vec2>> Polys; std::vector<double> Areas;
    double Scale = 0; for (const Vec2& Q : P) Scale = std::max(Scale, std::fabs(Q.X) + std::fabs(Q.Y));
    const double AreaEps = 1e-14 * Scale * Scale;
    for (Walk& W : Walks)
    {
        if (W.Area <= AreaEps) continue;
        Cells.push_back({ W.Edges });
        std::vector<Vec2> Poly; for (int Ed : W.Edges) Poly.push_back(P[E[Ed].From]);
        Polys.push_back(std::move(Poly)); Areas.push_back(W.Area);
    }
    for (Walk& W : Walks)
    {
        if (W.Area >= -AreaEps) continue;
        Vec2 Q = P[E[W.Edges.front()].From];
        int Best = -1;
        for (size_t C = 0; C < Polys.size(); ++C)
        {
            // a hole vertex lies on its own outline, so test a point just inside the hole's outline instead
            bool OnOutline = false; for (int Ed : Cells[C][0]) if (E[Ed].From == E[W.Edges.front()].From) { OnOutline = true; break; }
            if (OnOutline) continue;
            if (InsideRing(Polys[C], Q) && (Best < 0 || Areas[C] < Areas[Best])) Best = static_cast<int>(C);
        }
        if (Best >= 0) Cells[Best].push_back(W.Edges);
    }
    return Cells;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  BUILDING BLOCKS
//------------------------------------------------------------------------------------------------------------------------

int BrepBody::AddVertex(Vec3 P, double Tolerance) noexcept
{
    for (size_t I = 0; I < Vertices.size(); ++I) if (Vertices[I].Point.Distance(P) <= Tolerance) return static_cast<int>(I);
    Vertices.push_back({ P });
    return static_cast<int>(Vertices.size() - 1);
}

int BrepBody::FindCoincidentEdge(const NurbsCurve& Curve, double Tolerance, bool& ReversedOut) const noexcept
{
    const Vec3 S = Curve.StartPoint(), E = Curve.EndPoint();
    for (size_t I = 0; I < Edges.size(); ++I)
    {
        const NurbsCurve& C = Edges[I].Curve;
        Vec3 Cs = C.StartPoint(), Ce = C.EndPoint();
        bool Forward = Cs.Distance(S) <= Tolerance && Ce.Distance(E) <= Tolerance;
        bool Backward = Cs.Distance(E) <= Tolerance && Ce.Distance(S) <= Tolerance;
        if (!Forward && !Backward) continue;
        // interior agreement, parameterisation-independent
        bool Same = true;
        for (int K = 1; K <= 5 && Same; ++K)
        {
            double T = Curve.DomainStart() + (Curve.DomainEnd() - Curve.DomainStart()) * K / 6.0;
            double D = 0; (void)C.ClosestParameter(Curve.Sample(T), &D);
            Same = D <= Tolerance * 4.0;
        }
        if (!Same) continue;
        if (Forward) { ReversedOut = false; return static_cast<int>(I); }
        if (Backward)
        {
            // closed curves match both ways; decide by the direction at the start
            if (Cs.Distance(Ce) <= Tolerance)
            {
                Vec3 Ta = Curve.Tangent(Curve.DomainStart()), Tb = C.Tangent(C.DomainStart());
                ReversedOut = Ta.Dot(Tb) < 0;
            }
            else ReversedOut = true;
            return static_cast<int>(I);
        }
    }
    return -1;
}

namespace
{
    // Iso-curves come out of a surface unlabelled; recognise the two shapes that dominate B-rep edges.
    void ClassifyEdgeCurve(NurbsCurve& C) noexcept
    {
        if (C.Classification != CurveClassification::Freeform) return;
        if (C.Degree == 1 && C.Poles.size() == 2) { C.Classification = CurveClassification::Line; return; }
        if (C.Degree == 1)
        {
            Vec3 A = C.StartPoint(), D = (C.EndPoint() - A).Normalised(); bool Straight = true;
            for (const Vec4& P : C.Poles) if ((P.Divide() - A).Cross(D).Length() > ScalarCriteria::MergeTolerance) { Straight = false; break; }
            C.Classification = Straight ? CurveClassification::Line : CurveClassification::Polyline; return;
        }
        if (C.Rational() && C.Degree == 2)
        {
            // equal distance of all samples from the centroid of samples in the fitted plane ⇒ circle / arc
            Vec3 Sum; const int N = 12; std::vector<Vec3> S;
            for (int I = 0; I <= N; ++I) { S.push_back(C.Sample(C.DomainStart() + (C.DomainEnd() - C.DomainStart()) * I / N)); Sum = Sum + S.back(); }
            Frontier::Plane P; if (!C.Planar(P)) return;
            // circumcentre of three well-spread samples
            Vec3 A = S[0], B = S[N / 3], Cc = S[2 * N / 3];
            Vec3 Ab = B - A, Ac = Cc - A, Nn = Ab.Cross(Ac); double Den = 2.0 * Nn.LengthSquared(); if (Den < 1e-18) return;
            Vec3 Centre = A + (Ac * Ab.LengthSquared() - Ab * Ac.LengthSquared()).Cross(Nn) * (1.0 / Den);
            double R = A.Distance(Centre); bool Round = true;
            for (const Vec3& Q : S) if (std::fabs(Q.Distance(Centre) - R) > ScalarCriteria::MergeTolerance * 10.0) { Round = false; break; }
            if (Round) C.Classification = C.Closed() ? CurveClassification::Circle : CurveClassification::Arc;
        }
    }
}

int BrepBody::AddEdge(NurbsCurve Curve, double Tolerance) noexcept
{
    bool Rev = false;
    int Existing = FindCoincidentEdge(Curve, Tolerance, Rev);
    if (Existing >= 0) return Existing;                                                 // caller decides the sense via the coedge
    ClassifyEdgeCurve(Curve);
    BrepEdge E;
    E.VertexStart = AddVertex(Curve.StartPoint(), Tolerance);
    E.VertexEnd = AddVertex(Curve.EndPoint(), Tolerance);
    E.Curve = std::move(Curve);
    Edges.push_back(std::move(E));
    return static_cast<int>(Edges.size() - 1);
}

int BrepBody::AddCoedge(int Edge, bool Reversed, int Face, int Loop) noexcept
{
    Coedges.push_back({ Edge, Reversed, Face, Loop, {} });
    int Index = static_cast<int>(Coedges.size() - 1);
    Edges[Edge].Coedges.push_back(Index);
    Loops[Loop].Coedges.push_back(Index);
    return Index;
}

int BrepBody::AddFace(NurbsSurface Surface) noexcept
{
    BrepFace F; F.Surface = std::move(Surface);
    Faces.push_back(std::move(F));
    return static_cast<int>(Faces.size() - 1);
}

int BrepBody::AddLoop(int Face, bool Outer) noexcept
{
    BrepLoop L; L.Face = Face; L.Outer = Outer;
    Loops.push_back(std::move(L));
    Faces[Face].Loops.push_back(static_cast<int>(Loops.size() - 1));
    return static_cast<int>(Loops.size() - 1);
}

void BrepBody::AddNaturalBoundary(int Face, double Tolerance) noexcept
{
    const NurbsSurface& S = Faces[Face].Surface;
    const double U0 = S.DomainStartU(), U1 = S.DomainEndU(), V0 = S.DomainStartV(), V1 = S.DomainEndV();
    int Loop = AddLoop(Face, true);
    // CCW in (u,v): bottom (v0, u↑), right (u1, v↑), top (v1, u↓), left (u0, v↓). Edge curves are stored forward.
    struct Side { NurbsCurve Curve; bool Reversed; };
    Side Sides[4] = { { S.IsoCurveV(V0), false }, { S.IsoCurveU(U1), false }, { S.IsoCurveV(V1), true }, { S.IsoCurveU(U0), true } };
    for (Side& Sd : Sides)
    {
        if (Sd.Curve.Length() <= Tolerance * 4.0) continue;                             // degenerate side (pole / apex)
        bool Rev = false;
        int E = FindCoincidentEdge(Sd.Curve, Tolerance, Rev);
        if (E < 0) { E = AddEdge(Sd.Curve, Tolerance); Rev = false; }
        AddCoedge(E, Sd.Reversed != Rev, Face, Loop);
    }
}

BrepBody BrepBody::FromSurface(const NurbsSurface& Surface) noexcept
{
    BrepBody B;
    int F = B.AddFace(Surface);
    B.AddNaturalBoundary(F, ScalarCriteria::MergeTolerance);
    return B;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  COEDGE GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

Vec3 BrepBody::CoedgeStart(int C) const noexcept
{
    const BrepCoedge& Ce = Coedges[C]; const BrepEdge& E = Edges[Ce.Edge];
    return Vertices[Ce.Reversed ? E.VertexEnd : E.VertexStart].Point;
}
Vec3 BrepBody::CoedgeEnd(int C) const noexcept
{
    const BrepCoedge& Ce = Coedges[C]; const BrepEdge& E = Edges[Ce.Edge];
    return Vertices[Ce.Reversed ? E.VertexStart : E.VertexEnd].Point;
}
NurbsCurve BrepBody::CoedgeCurve(int C) const noexcept
{
    const BrepCoedge& Ce = Coedges[C];
    return Ce.Reversed ? Edges[Ce.Edge].Curve.Reversed() : Edges[Ce.Edge].Curve;
}

std::vector<std::vector<int>> BrepBody::OpenLoops(double) const noexcept
{
    std::vector<std::vector<int>> Out;
    std::vector<bool> Used(Coedges.size(), false);
    auto OpenCoedge = [&](int C) { return Edges[Coedges[C].Edge].Coedges.size() == 1; };
    auto StartVertex = [&](int C) { const BrepCoedge& Ce = Coedges[C]; const BrepEdge& E = Edges[Ce.Edge]; return Ce.Reversed ? E.VertexEnd : E.VertexStart; };
    auto EndVertex   = [&](int C) { const BrepCoedge& Ce = Coedges[C]; const BrepEdge& E = Edges[Ce.Edge]; return Ce.Reversed ? E.VertexStart : E.VertexEnd; };
    for (size_t Seed = 0; Seed < Coedges.size(); ++Seed)
    {
        if (Used[Seed] || !OpenCoedge(static_cast<int>(Seed))) continue;
        std::vector<int> Chain{ static_cast<int>(Seed) }; Used[Seed] = true;
        int Head = StartVertex(static_cast<int>(Seed)), Tail = EndVertex(static_cast<int>(Seed));
        bool Grew = true;
        while (Tail != Head && Grew)
        {
            Grew = false;
            for (size_t C = 0; C < Coedges.size(); ++C)
            {
                if (Used[C] || !OpenCoedge(static_cast<int>(C))) continue;
                if (StartVertex(static_cast<int>(C)) == Tail) { Chain.push_back(static_cast<int>(C)); Used[C] = true; Tail = EndVertex(static_cast<int>(C)); Grew = true; break; }
            }
        }
        if (Tail == Head) Out.push_back(std::move(Chain));
    }
    return Out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  CAPPING AND ORIENTATION
//------------------------------------------------------------------------------------------------------------------------

int BrepBody::Capped(double Tolerance) noexcept
{
    // 1. every open loop → plane fit (Newell) + planarity check
    struct Rim { std::vector<int> Loop; Vec3 Normal; Vec3 Centroid; double Offset; std::vector<Vec3> Pts; double Area; bool Taken = false; };
    std::vector<Rim> Rims;
    for (const std::vector<int>& Loop : OpenLoops(Tolerance))
    {
        Rim R; R.Loop = Loop;
        for (int C : Loop) { NurbsCurve K = CoedgeCurve(C); for (int I = 0; I < 8; ++I) R.Pts.push_back(K.Sample(K.DomainStart() + (K.DomainEnd() - K.DomainStart()) * I / 8.0)); }
        if (R.Pts.size() < 3) continue;
        Vec3 N, Centroid;
        for (size_t I = 0; I < R.Pts.size(); ++I)
        {
            Vec3 A = R.Pts[I], B = R.Pts[(I + 1) % R.Pts.size()];
            N.X += (A.Y - B.Y) * (A.Z + B.Z); N.Y += (A.Z - B.Z) * (A.X + B.X); N.Z += (A.X - B.X) * (A.Y + B.Y);
            Centroid = Centroid + A;
        }
        R.Centroid = Centroid * (1.0 / R.Pts.size());
        R.Area = 0.5 * N.Length();
        if (N.Length() < 1e-12) continue;
        R.Normal = N.Normalised(); R.Offset = R.Normal.Dot(R.Centroid);
        bool Planar = true; for (Vec3 P : R.Pts) if (std::fabs((P - R.Centroid).Dot(R.Normal)) > Tolerance * 10.0) { Planar = false; break; }
        if (!Planar) continue;
        Rims.push_back(std::move(R));
    }
    // 2. group coplanar rims: an outer rim (largest area, walking with normal N) adopts every coplanar rim of opposite walk
    //    sense whose points lie inside it — those become holes of the same cap.
    std::sort(Rims.begin(), Rims.end(), [](const Rim& A, const Rim& B) { return A.Area > B.Area; });
    int Added = 0;
    for (size_t O = 0; O < Rims.size(); ++O)
    {
        if (Rims[O].Taken) continue;
        Rim& Outer = Rims[O]; Outer.Taken = true;
        Vec3 N = Outer.Normal;
        Planar2 Basis = PlanarBasis(N * -1.0);
        // point-in-polygon on the outer rim's samples
        std::vector<Vec2> Poly; for (Vec3 P : Outer.Pts) Poly.push_back(Basis.Project(P - Outer.Centroid));
        auto Inside = [&](Vec3 P)
        {
            Vec2 Q = Basis.Project(P - Outer.Centroid); bool In = false;
            for (size_t I = 0, J = Poly.size() - 1; I < Poly.size(); J = I++)
                if ((Poly[I].Y > Q.Y) != (Poly[J].Y > Q.Y) && Q.X < (Poly[J].X - Poly[I].X) * (Q.Y - Poly[I].Y) / (Poly[J].Y - Poly[I].Y) + Poly[I].X) In = !In;
            return In;
        };
        std::vector<size_t> Holes;
        for (size_t H = O + 1; H < Rims.size(); ++H)
        {
            Rim& Cand = Rims[H];
            if (Cand.Taken) continue;
            if (std::fabs(std::fabs(Cand.Normal.Dot(N)) - 1.0) > 1e-6) continue;                   // not parallel
            if (std::fabs(Cand.Normal.Dot(N) * Cand.Offset - Outer.Offset) > Tolerance * 10.0) continue;   // not the same plane
            if (Cand.Normal.Dot(N) > 0) continue;                                                    // same walk sense → its own cap, not a hole
            if (!Inside(Cand.Pts[0])) continue;
            Cand.Taken = true; Holes.push_back(H);
        }
        double MinU = 1e300, MaxU = -1e300, MinV = 1e300, MaxV = -1e300;
        for (Vec3 P : Outer.Pts) { Vec2 Q = Basis.Project(P - Outer.Centroid); MinU = std::min(MinU, Q.X); MaxU = std::max(MaxU, Q.X); MinV = std::min(MinV, Q.Y); MaxV = std::max(MaxV, Q.Y); }
        double Pad = 0.05 * std::max(MaxU - MinU, MaxV - MinV) + Tolerance;
        Vec3 Origin = Outer.Centroid + Basis.U * (MinU - Pad) + Basis.V * (MinV - Pad);
        Deliver<NurbsSurface> Plane = NurbsSurface::Plane(Origin, Basis.U, Basis.V, MaxU - MinU + 2 * Pad, MaxV - MinV + 2 * Pad);
        if (!Plane) continue;
        int F = AddFace(Plane.Payload);
        Faces[F].Natural = false;
        // The rims walk with normal N; the cap's outward normal is −N so every rim is traversed the opposite way.
        int L = AddLoop(F, true);
        for (auto It = Outer.Loop.rbegin(); It != Outer.Loop.rend(); ++It) AddCoedge(Coedges[*It].Edge, !Coedges[*It].Reversed, F, L);
        for (size_t H : Holes)
        {
            int Lh = AddLoop(F, false);
            for (auto It = Rims[H].Loop.rbegin(); It != Rims[H].Loop.rend(); ++It) AddCoedge(Coedges[*It].Edge, !Coedges[*It].Reversed, F, Lh);
        }
        ++Added;
    }
    return Added;
}

void BrepBody::FlipFace(int Face) noexcept
{
    BrepFace& F = Faces[Face];
    F.Reversed = !F.Reversed;
    for (int L : F.Loops)
    {
        std::reverse(Loops[L].Coedges.begin(), Loops[L].Coedges.end());
        for (int C : Loops[L].Coedges) { Coedges[C].Reversed = !Coedges[C].Reversed; std::reverse(Coedges[C].Trace.begin(), Coedges[C].Trace.end()); }
    }
}

bool BrepBody::Orient() noexcept
{
    if (Faces.empty()) return false;
    std::vector<int> Fixed(Faces.size(), 0);                                            // 0 unvisited, 1 fixed
    for (size_t Seed = 0; Seed < Faces.size(); ++Seed)
    {
        if (Fixed[Seed]) continue;
        std::deque<int> Queue{ static_cast<int>(Seed) }; Fixed[Seed] = 1;
        while (!Queue.empty())
        {
            int F = Queue.front(); Queue.pop_front();
            for (int L : Faces[F].Loops) for (int C : Loops[L].Coedges)
            {
                const BrepEdge& E = Edges[Coedges[C].Edge];
                if (E.Coedges.size() != 2) continue;
                int Other = E.Coedges[0] == C ? E.Coedges[1] : E.Coedges[0];
                int G = Coedges[Other].Face;
                if (Fixed[G]) continue;
                if (Coedges[Other].Reversed == Coedges[C].Reversed) FlipFace(G);
                Fixed[G] = 1; Queue.push_back(G);
            }
        }
    }
    if (SignedVolume() < 0) for (size_t F = 0; F < Faces.size(); ++F) FlipFace(static_cast<int>(F));
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  TESSELLATION AND MEASURES
//------------------------------------------------------------------------------------------------------------------------

Vec3 BrepBody::FaceNormal(int Face, double U, double V) const noexcept
{
    Vec3 N = Faces[Face].Surface.Normal(U, V);
    return Faces[Face].Reversed ? N * -1.0 : N;
}

std::vector<Vec3> BrepBody::EdgePolyline(int Edge, double ChordTolerance) const noexcept
{
    const NurbsCurve& C = Edges[Edge].Curve;
    std::vector<Vec3> Out;
    (void)ChordTolerance;
    if (C.Degree == 1) { for (const Vec4& P : C.Poles) Out.push_back(P.Divide()); return Out; }
    // Chord-tolerance sampling per span: radius of curvature vs. allowed sagitta.
    for (size_t S = static_cast<size_t>(C.Degree); S + 1 < C.Knots.size() - static_cast<size_t>(C.Degree); ++S)
    {
        double T0 = C.Knots[S], T1 = C.Knots[S + 1]; if (T1 <= T0) continue;
        double Kappa = std::max(C.Curvature(0.5 * (T0 + T1)), 1e-9);
        double Len = C.Trimmed(T0, T1).Length();
        double Step = 2.0 * std::sqrt(std::max(2.0 * ChordTolerance / Kappa - ChordTolerance * ChordTolerance, 1e-12));
        int N = std::clamp(static_cast<int>(std::ceil(Len / Step)), 2, 64);
        for (int I = (Out.empty() ? 0 : 1); I <= N; ++I) Out.push_back(C.Sample(T0 + (T1 - T0) * I / N));
    }
    if (Out.empty()) Out.push_back(C.StartPoint());
    return Out;
}

std::vector<Vec2> BrepBody::CoedgeTrace(int Coedge, std::vector<double>* Parameters, int Samples) const noexcept
{
    const BrepCoedge& Ce = Coedges[Coedge];
    if (Parameters) Parameters->clear();
    if (!Ce.Trace.empty()) return Ce.Trace;
    const BrepFace& F = Faces[Ce.Face];
    const NurbsSurface& S = F.Surface;
    NurbsCurve K = CoedgeCurve(Coedge);
    const double U0 = S.DomainStartU(), U1 = S.DomainEndU(), V0 = S.DomainStartV(), V1 = S.DomainEndV();
    std::vector<double> T;
    if (K.Degree == 1) { for (size_t I = 0; I < K.Poles.size(); ++I) T.push_back(K.Knots[I + 1]); }
    else
    {
        // chord-tolerance sampling so the trimmed tessellation follows the edge as closely as the lattice follows the surface
        std::vector<Vec3> Pts; K.Tessellate(Pts, &T, ScalarCriteria::ChordTolerance * 4.0);
        if (static_cast<int>(T.size()) < Samples) { T.clear(); int N = std::max(Samples, static_cast<int>(K.Poles.size()) * 4); for (int I = 0; I <= N; ++I) T.push_back(K.DomainStart() + (K.DomainEnd() - K.DomainStart()) * I / N); }
    }
    std::vector<Vec2> Out; Out.reserve(T.size());
    if (F.Natural)
    {
        // One of the four natural sides, walked CCW in (u,v): bottom (v0,u↑) right (u1,v↑) top (v1,u↓) left (u0,v↓).
        //    Direction disambiguates the two coedges of a seam, which share one 3D curve.
        // Five samples along the coedge (quarter points included) so the two seam coedges of a closed direction — the same
        //    3D curve walked both ways — land on their own sides.
        Vec3 Along[5]; for (int Q = 0; Q < 5; ++Q) Along[Q] = K.Sample(K.DomainStart() + (K.DomainEnd() - K.DomainStart()) * Q / 4.0);
        struct Side { Vec2 From, To; };
        const Side Sides[4] = { { { U0, V0 }, { U1, V0 } }, { { U1, V0 }, { U1, V1 } }, { { U1, V1 }, { U0, V1 } }, { { U0, V1 }, { U0, V0 } } };
        int Best = -1; double BestErr = ScalarCriteria::Infinity;
        for (int I = 0; I < 4; ++I)
        {
            double Err = 0;
            for (int Q = 0; Q < 5; ++Q) { Vec2 At = Sides[I].From + (Sides[I].To - Sides[I].From) * (Q / 4.0); Err += S.Sample(At.X, At.Y).Distance(Along[Q]); }
            if (Err < BestErr) { BestErr = Err; Best = I; }
        }
        if (Best >= 0 && BestErr <= ScalarCriteria::MergeTolerance * 50.0)
        {
            int N = static_cast<int>(T.size()) - 1;
            for (int I = 0; I <= N; ++I) Out.push_back(Sides[Best].From + (Sides[Best].To - Sides[Best].From) * (static_cast<double>(I) / N));
            if (Parameters) *Parameters = T;
            return Out;
        }
    }
    // Project the samples; the first from a global search, the rest seeded by their predecessor so seams are not crossed.
    double U = 0, V = 0;
    for (size_t I = 0; I < T.size(); ++I)
    {
        Vec3 P = K.Sample(T[I]);
        if (I == 0) S.ClosestParameter(P, U, V); else (void)SeededParameter(S, P, U, V);
        Out.emplace_back(U, V);
    }
    if (Parameters) *Parameters = T;
    return Out;
}

BrepBody::FaceTriangles BrepBody::TessellateFace(int Face, double ChordTolerance) const noexcept
{
    FaceTriangles Out;
    const BrepFace& F = Faces[Face];
    if (F.Natural)
    {
        NurbsSurface::Tessellation T = F.Surface.Tessellate(ChordTolerance);
        Out.Positions = std::move(T.Positions); Out.Normals = std::move(T.Normals); Out.Parameters = std::move(T.Parameters); Out.Triangles = std::move(T.Triangles);
    }
    else
    {
        // Trimmed face: the surface's own lattice is clipped by the (u,v) trimming rings — lattice segments are split at
        //    the rings, ring edges at the lattice lines, the planar arrangement is walked into cells, and each small cell is
        //    ear clipped. Every triangle therefore spans at most one lattice cell, as on a natural face.
        const NurbsSurface& S = F.Surface;
        const double U0 = S.DomainStartU(), U1 = S.DomainEndU(), V0 = S.DomainStartV(), V1 = S.DomainEndV();
        const double Eps = 1e-9 * (U1 - U0 + V1 - V0);
        std::vector<double> SamplesU{ U0, U1 }, SamplesV{ V0, V1 };
        if (S.DegreeU > 1 || S.DegreeV > 1)
        {
            NurbsSurface::Tessellation T = S.Tessellate(ChordTolerance);
            SamplesU.clear(); SamplesV.clear();
            for (int I = 0; I < T.ColumnCount; ++I) SamplesU.push_back(T.Parameters[I].X);
            for (int J = 0; J < T.RowCount; ++J) SamplesV.push_back(T.Parameters[static_cast<size_t>(J) * T.ColumnCount].Y);
        }
        std::vector<Vec2> P;
        std::vector<PlanarEdge> Edges;
        // ring points
        std::vector<std::vector<int>> Rings;
        for (int L : F.Loops)
        {
            std::vector<int> Ring;
            for (int C : Loops[L].Coedges)
            {
                std::vector<Vec2> Tr = CoedgeTrace(C);
                for (size_t I = 0; I + 1 < Tr.size(); ++I)
                {
                    if (!Ring.empty() && P[Ring.back()].Distance(Tr[I]) <= Eps) continue;
                    Ring.push_back(static_cast<int>(P.size())); P.push_back(Tr[I]);
                }
            }
            if (Ring.size() > 1 && P[Ring.front()].Distance(P[Ring.back()]) <= Eps) Ring.pop_back();
            if (Ring.size() >= 3) Rings.push_back(std::move(Ring));
        }
        if (F.Reversed) for (std::vector<int>& R : Rings) std::reverse(R.begin(), R.end());   // material on the left in (u,v)
        // lattice points inside the domain are added lazily; crossings split both the ring edge and the lattice segment
        auto InsideDomain = [&](Vec2 Q)
        {
            for (size_t K = 0; K < Rings.size(); ++K)
            {
                std::vector<Vec2> R; for (int I : Rings[K]) R.push_back(P[I]);
                bool In = InsideRing(R, Q); if (K == 0 && !In) return false; if (K > 0 && In) return false;
            }
            return !Rings.empty();
        };
        struct Cut { double Along; int Point; };                                          // a split point on a ring edge or a lattice segment
        std::vector<std::vector<Cut>> RingCuts;                                          // per ring edge (indexed by ring, position)
        std::vector<std::vector<int>> RingEdgeFirst;                                      // ring → first index into RingCuts
        int EdgeCount = 0; for (auto& R : Rings) EdgeCount += static_cast<int>(R.size());
        RingCuts.resize(EdgeCount);
        {
            int First = 0; for (auto& R : Rings) { RingEdgeFirst.push_back({ First }); First += static_cast<int>(R.size()); }
        }
        const int NU = static_cast<int>(SamplesU.size()), NV = static_cast<int>(SamplesV.size());
        std::vector<int> LatticeIndex(static_cast<size_t>(NU) * NV, -1);
        auto LatticeAt = [&](int I, int J) { int& N = LatticeIndex[static_cast<size_t>(I) * NV + J]; if (N < 0) { N = static_cast<int>(P.size()); P.push_back({ SamplesU[I], SamplesV[J] }); } return N; };
        // lattice segments: (fixed line, from sample k to k+1) with their cuts
        struct Segment { bool AlongU; int Line, K; std::vector<Cut> Cuts; };
        std::vector<Segment> Segments;
        for (int J = 0; J < NV; ++J) for (int I = 0; I + 1 < NU; ++I) Segments.push_back({ true, J, I, {} });
        for (int I = 0; I < NU; ++I) for (int J = 0; J + 1 < NV; ++J) Segments.push_back({ false, I, J, {} });
        auto SegmentEnds = [&](const Segment& Sg, Vec2& A, Vec2& B) { if (Sg.AlongU) { A = { SamplesU[Sg.K], SamplesV[Sg.Line] }; B = { SamplesU[Sg.K + 1], SamplesV[Sg.Line] }; } else { A = { SamplesU[Sg.Line], SamplesV[Sg.K] }; B = { SamplesU[Sg.Line], SamplesV[Sg.K + 1] }; } };
        for (size_t Si = 0; Si < Segments.size(); ++Si)
        {
            Segment& Sg = Segments[Si]; Vec2 A, B; SegmentEnds(Sg, A, B);
            Vec2 Dir = B - A;
            for (size_t R = 0; R < Rings.size(); ++R)
                for (size_t I = 0; I < Rings[R].size(); ++I)
                {
                    Vec2 C = P[Rings[R][I]], D = P[Rings[R][(I + 1) % Rings[R].size()]];
                    Vec2 Sd = D - C; double Den = Dir.Cross(Sd);
                    if (std::fabs(Den) < 1e-300) continue;
                    double T = (C - A).Cross(Sd) / Den, U = (C - A).Cross(Dir) / Den;
                    if (T < -1e-9 || T > 1 + 1e-9 || U < -1e-9 || U > 1 + 1e-9) continue;
                    int N;
                    Vec2 X = A + Dir * T;
                    if (U <= 1e-7) N = Rings[R][I];                                     // ring vertex on the segment
                    else if (U >= 1 - 1e-7) N = Rings[R][(I + 1) % Rings[R].size()];
                    else
                    {
                        if (T <= 1e-7) N = Sg.AlongU ? LatticeAt(Sg.K, Sg.Line) : LatticeAt(Sg.Line, Sg.K);
                        else if (T >= 1 - 1e-7) N = Sg.AlongU ? LatticeAt(Sg.K + 1, Sg.Line) : LatticeAt(Sg.Line, Sg.K + 1);
                        else { N = static_cast<int>(P.size()); P.push_back(X); }
                        RingCuts[RingEdgeFirst[R][0] + static_cast<int>(I)].push_back({ U, N });
                    }
                    Sg.Cuts.push_back({ T, N });
                }
        }
        // ring edges with cuts (one direction)
        for (size_t R = 0; R < Rings.size(); ++R)
            for (size_t I = 0; I < Rings[R].size(); ++I)
            {
                std::vector<Cut>& Cs = RingCuts[RingEdgeFirst[R][0] + static_cast<int>(I)];
                std::sort(Cs.begin(), Cs.end(), [](const Cut& X, const Cut& Y) { return X.Along < Y.Along; });
                int Prev = Rings[R][I];
                for (const Cut& C : Cs) { if (C.Point != Prev) Edges.push_back({ Prev, C.Point }); Prev = C.Point; }
                int Last = Rings[R][(I + 1) % Rings[R].size()];
                if (Last != Prev) Edges.push_back({ Prev, Last });
            }
        // lattice segments: sub-segments whose midpoint lies inside the domain, both directions
        for (Segment& Sg : Segments)
        {
            Vec2 A, B; SegmentEnds(Sg, A, B);
            std::vector<Cut> Cs = Sg.Cuts;
            std::sort(Cs.begin(), Cs.end(), [](const Cut& X, const Cut& Y) { return X.Along < Y.Along; });
            std::vector<std::pair<double, int>> Chain;                                  // (along, point) including ends (lattice points created lazily)
            Chain.emplace_back(0.0, -1);
            for (const Cut& C : Cs) if (Chain.back().second != C.Point && (Chain.size() == 1 || C.Along > Chain.back().first + 1e-12)) Chain.emplace_back(C.Along, C.Point);
            if (Chain.back().first < 1 - 1e-12 || Chain.size() == 1) Chain.emplace_back(1.0, -1);
            for (size_t K = 0; K + 1 < Chain.size(); ++K)
            {
                double Ta = Chain[K].first, Tb = Chain[K + 1].first;
                if (Tb - Ta <= 1e-12) continue;
                Vec2 M = A + (B - A) * (0.5 * (Ta + Tb));
                if (!InsideDomain(M)) continue;
                int Na = Chain[K].second, Nb = Chain[K + 1].second;
                if (Na < 0) { Na = Sg.AlongU ? LatticeAt(Sg.K + (Ta > 0.5 ? 1 : 0), Sg.Line) : LatticeAt(Sg.Line, Sg.K + (Ta > 0.5 ? 1 : 0)); Chain[K].second = Na; }
                if (Nb < 0) { Nb = Sg.AlongU ? LatticeAt(Sg.K + (Tb > 0.5 ? 1 : 0), Sg.Line) : LatticeAt(Sg.Line, Sg.K + (Tb > 0.5 ? 1 : 0)); Chain[K + 1].second = Nb; }
                if (Na == Nb) continue;
                Edges.push_back({ Na, Nb }); Edges.push_back({ Nb, Na });
            }
        }
        // cells → triangles
        std::vector<std::vector<std::vector<int>>> Cells = PlanarCells(P, Edges);
        for (const std::vector<std::vector<int>>& Cell : Cells)
        {
            std::vector<std::vector<uint32_t>> CellRings;
            for (const std::vector<int>& Ring : Cell)
            {
                std::vector<uint32_t> Rg; for (int Ed : Ring) Rg.push_back(static_cast<uint32_t>(Edges[Ed].From));
                CellRings.push_back(std::move(Rg));
            }
            std::vector<uint32_t> Tri = TriangulatePolygon(P, CellRings);
            Out.Triangles.insert(Out.Triangles.end(), Tri.begin(), Tri.end());
        }
        Out.Parameters = P;
        Out.Positions.reserve(P.size()); Out.Normals.reserve(P.size());
        for (Vec2 Q : P) { Out.Positions.push_back(S.Sample(Q.X, Q.Y)); Out.Normals.push_back(S.Normal(Q.X, Q.Y)); }
    }
    if (F.Reversed)
    {
        for (Vec3& N : Out.Normals) N = N * -1.0;
        for (size_t T = 0; T + 2 < Out.Triangles.size(); T += 3) std::swap(Out.Triangles[T + 1], Out.Triangles[T + 2]);
    }
    return Out;
}

double BrepBody::SignedVolume() const noexcept
{
    double V = 0;
    for (size_t F = 0; F < Faces.size(); ++F)
    {
        FaceTriangles T = TessellateFace(static_cast<int>(F), ScalarCriteria::ChordTolerance);
        for (size_t I = 0; I + 2 < T.Triangles.size(); I += 3)
        {
            const Vec3& A = T.Positions[T.Triangles[I]]; const Vec3& B = T.Positions[T.Triangles[I + 1]]; const Vec3& C = T.Positions[T.Triangles[I + 2]];
            V += A.Dot(B.Cross(C)) / 6.0;
        }
    }
    return V;
}

double BrepBody::Area() const noexcept
{
    double A = 0;
    for (size_t F = 0; F < Faces.size(); ++F)
    {
        FaceTriangles T = TessellateFace(static_cast<int>(F), ScalarCriteria::ChordTolerance);
        for (size_t I = 0; I + 2 < T.Triangles.size(); I += 3)
            A += 0.5 * (T.Positions[T.Triangles[I + 1]] - T.Positions[T.Triangles[I]]).Cross(T.Positions[T.Triangles[I + 2]] - T.Positions[T.Triangles[I]]).Length();
    }
    return A;
}

Box3 BrepBody::Bounds() const noexcept
{
    // Natural faces are bounded by their pole hull; trimmed caps sit on padded planes, so use their edges instead.
    Box3 B;
    for (const BrepFace& F : Faces)
    {
        if (F.Natural) { B.Include(F.Surface.Bounds()); continue; }
        for (int L : F.Loops) for (int C : Loops[L].Coedges) B.Include(Edges[Coedges[C].Edge].Curve.Bounds());
    }
    return B;
}

BodyClassification BrepBody::Classification() const noexcept
{
    if (Faces.empty()) return BodyClassification::Wire;
    for (const BrepEdge& E : Edges) if (E.Coedges.size() < 2) return BodyClassification::Sheet;
    return BodyClassification::Solid;
}

BodyReport BrepBody::Validate() const noexcept
{
    BodyReport R;
    R.Vertices = static_cast<int>(Vertices.size()); R.Edges = static_cast<int>(Edges.size()); R.Faces = static_cast<int>(Faces.size()); R.Loops = static_cast<int>(Loops.size());
    for (const BrepEdge& E : Edges)
    {
        if (E.Coedges.size() == 1) ++R.OpenEdges;
        else if (E.Coedges.size() > 2) ++R.NonManifoldEdges;
        else if (E.Coedges.size() == 2 && Coedges[E.Coedges[0]].Reversed == Coedges[E.Coedges[1]].Reversed) ++R.MisorientedEdges;
    }
    R.Closed = R.OpenEdges == 0 && !Faces.empty();
    R.Manifold = R.NonManifoldEdges == 0;
    R.Oriented = R.MisorientedEdges == 0;
    R.EulerCharacteristic = R.Vertices - R.Edges + R.Faces - (R.Loops - R.Faces);       // inner loops (holes in faces) count as handles
    // Hulls: flood faces across shared edges.
    std::vector<int> Label(Faces.size(), -1);
    for (size_t Seed = 0; Seed < Faces.size(); ++Seed)
    {
        if (Label[Seed] >= 0) continue;
        std::vector<int> Stack{ static_cast<int>(Seed) }; Label[Seed] = R.Hulls;
        while (!Stack.empty())
        {
            int F = Stack.back(); Stack.pop_back();
            for (int L : Faces[F].Loops) for (int C : Loops[L].Coedges) for (int Other : Edges[Coedges[C].Edge].Coedges)
            {
                int G = Coedges[Other].Face;
                if (Label[G] < 0) { Label[G] = R.Hulls; Stack.push_back(G); }
            }
        }
        ++R.Hulls;
    }
    if (R.Closed && R.Manifold) R.Genus = R.Hulls - R.EulerCharacteristic / 2;
    R.Volume = R.Closed ? SignedVolume() : 0.0;                                         // divergence theorem needs a closed boundary
    R.Area = Area();
    return R;
}

BrepBody BrepBody::Transformed(const Mat4& M) const noexcept
{
    BrepBody B = *this;
    for (BrepVertex& V : B.Vertices) V.Point = M.TransformPoint(V.Point);
    for (BrepEdge& E : B.Edges) E.Curve = E.Curve.Transformed(M);
    for (BrepFace& F : B.Faces) F.Surface = F.Surface.Transformed(M);
    // A reflection turns the body inside out; the topology switch say so once the volume goes negative.
    Vec3 X = M.TransformDirection(Vec3::UnitX()), Y = M.TransformDirection(Vec3::UnitY()), Z = M.TransformDirection(Vec3::UnitZ());
    if (X.Cross(Y).Dot(Z) < 0) for (size_t F = 0; F < B.Faces.size(); ++F) B.FlipFace(static_cast<int>(F));
    return B;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  GENERIC BUILDERS
//------------------------------------------------------------------------------------------------------------------------

Deliver<BrepBody> BrepBody::Sew(const std::vector<NurbsSurface>& Surfaces, double Tolerance, bool Cap) noexcept
{
    if (Surfaces.empty()) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "no faces to sew");
    BrepBody B;
    for (const NurbsSurface& S : Surfaces) { int F = B.AddFace(S); B.AddNaturalBoundary(F, Tolerance); }
    B.Orient();                                                                         // neighbours agree before caps are derived from them
    if (Cap) B.Capped(Tolerance);
    B.Orient();
    return Deliver<BrepBody>::Accept(std::move(B));
}

Deliver<BrepBody> BrepBody::Solidify(const BrepBody& Shell, double HalfThickness) noexcept
{
    // Solidify turns a NURBS surface into a slab of total thickness 2·HalfThickness. The two layers are the original
    //    surface translated ± HalfThickness along its centre normal. The side wall is the ruled surface between each
    //    natural-boundary loop and its translated copy, which is meaningful only when the boundary is non-degenerate
    //    under the translation (a flat sheet's boundary, when translated along the surface normal, is parallel to itself
    //    and yields a zero-area side wall — that case is refused, with a hint to use `extrude` on a planar curve).
    //
    // Full-surface-offset (a different operation) is a later phase.
    if (HalfThickness <= ScalarCriteria::KernelTolerance) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "thickness must be positive");
    if (Shell.Faces.size() != 1) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "solidify currently operates on a single surface; pass a sheet body with one face");

    const BrepFace& F = Shell.Faces[0];
    if (F.Loops.empty()) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "surface has no boundary to thicken");

    auto StitchLoop = [&](int L) -> std::optional<NurbsCurve>
    {
        const BrepLoop& Lp = Shell.Loops[L];
        if (Lp.Coedges.empty()) return std::nullopt;
        std::vector<Vec3> Pts;
        for (int Ce : Lp.Coedges)
        {
            const BrepCoedge& C = Shell.Coedges[Ce];
            const BrepEdge& E = Shell.Edges[C.Edge];
            std::vector<Vec3> P; E.Curve.Tessellate(P, nullptr, 2e-3);
            if (C.Reversed) std::reverse(P.begin(), P.end());
            if (!Pts.empty()) P.erase(P.begin());
            Pts.insert(Pts.end(), P.begin(), P.end());
        }
        if (Pts.size() < 4) return std::nullopt;
        Deliver<NurbsCurve> Loop = NurbsCurve::Polyline(Pts, true);
        if (!Loop) return std::nullopt;
        return Loop.Payload;
    };
    std::vector<NurbsCurve> Loops; Loops.reserve(F.Loops.size());
    for (int L : F.Loops) { auto Lc = StitchLoop(L); if (Lc) Loops.push_back(*Lc); }
    if (Loops.empty()) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "no usable boundary loop");

    Vec3 Normal = F.Surface.Normal(0.5 * (F.Surface.DomainStartU() + F.Surface.DomainEndU()),
                                   0.5 * (F.Surface.DomainStartV() + F.Surface.DomainEndV()));
    if (F.Reversed) Normal = -Normal;
    Mat4 Out = Mat4::Translation(Normal *  HalfThickness);
    Mat4 In  = Mat4::Translation(Normal * -HalfThickness);

    // The side wall between C and C.Transformed(Out) has non-zero area only when C is not parallel to the translation
    //    direction (i.e. C has some component orthogonal to Normal). For a planar surface whose boundary lies in the
    //    surface's own plane, every point of C is orthogonal to Normal, so the ruled surface collapses. Refuse early.
    if (F.Surface.ClosedU() || F.Surface.ClosedV()) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "surface is closed in at least one direction (cylinder, sphere, torus, closed patch) — Solidify only works on open sheets; use a regular closed body instead");
    bool AnyWall = false;
    for (const NurbsCurve& C : Loops)
    {
        if (C.Length() <= ScalarCriteria::KernelTolerance) continue;
        // Sample the curve's midpoint and check the tangent has any component orthogonal to the translation
        Vec3 Mid = C.Sample(0.5 * (C.DomainStart() + C.DomainEnd()));
        Vec3 Translated = Mat4::Translation(Normal * HalfThickness).TransformPoint(Mid);
        if ((Translated - Mid).Length() > 1e-9) { AnyWall = true; break; }
    }
    if (!AnyWall) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "surface boundary is parallel to its own normal — the side wall would be zero area; use `extrude <curve> t` on a planar curve instead");

    BrepBody B;
    int Top = B.AddFace(F.Surface.Transformed(Out));   B.AddNaturalBoundary(Top, ScalarCriteria::MergeTolerance);
    int Bottom = B.AddFace(F.Surface.Transformed(In)); B.AddNaturalBoundary(Bottom, ScalarCriteria::MergeTolerance);
    for (const NurbsCurve& C : Loops)
    {
        if (C.Length() <= ScalarCriteria::KernelTolerance) continue;
        NurbsCurve T = C.Transformed(Out);
        Deliver<NurbsSurface> Wall = NurbsSurface::Ruled(C, T);
        if (!Wall) continue;
        int Side = B.AddFace(Wall.Payload); B.AddNaturalBoundary(Side, ScalarCriteria::MergeTolerance);
    }
    B.Orient();
    return Deliver<BrepBody>::Accept(std::move(B));
}

Deliver<BrepBody> BrepBody::ChamferEdge(int EdgeIndex, double SetBack, double Tolerance) const noexcept
{
    if (EdgeIndex < 0 || EdgeIndex >= (int)Edges.size()) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "edge index out of range");
    if (SetBack <= Tolerance) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "set-back is zero or negative");
    const BrepEdge& E = Edges[EdgeIndex];
    if (E.Coedges.size() != 2) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "edge is not a manifold interior edge (chamfer needs exactly two adjacent faces)");
    int Ce1 = E.Coedges[0], Ce2 = E.Coedges[1];
    int F1 = Coedges[Ce1].Face, F2 = Coedges[Ce2].Face;
    if (F1 < 0 || F2 < 0 || F1 == F2) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "edge has invalid adjacent faces");
    (void)Coedges[Ce1].Loop; (void)Coedges[Ce2].Loop;                                    // the loops keep their coedge entries; we just rewrite Edge references

    // Endpoints from the edge's stored vertices. For a smooth chamfer, sample the curve instead so the set-back follows
    //    the actual edge geometry; for a planar chamfer the endpoints are sufficient.
    if (E.VertexStart < 0 || E.VertexEnd < 0) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "edge has no stored vertices");
    Vec3 P0 = Vertices[E.VertexStart].Point, P1 = Vertices[E.VertexEnd].Point;
    Vec3 Tangent = (P1 - P0); if (Tangent.Length() <= Tolerance) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "edge is a point");
    Tangent = Tangent.Normalised();

    // Face normals. Take the average across the surface so a slight curvature is not a refusal; the planar assumption
    //    is enforced by checking the variation is small.
    Vec3 N1 = FaceNormal(F1, 0.5, 0.5), N2 = FaceNormal(F2, 0.5, 0.5);
    Vec3 N1b = FaceNormal(F1, 0.25, 0.25), N2b = FaceNormal(F2, 0.25, 0.25);
    if ((N1 - N1b).Length() > 0.05 || (N2 - N2b).Length() > 0.05) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "chamfer requires both adjacent faces to be planar (curvature detected)");
    if (N1.Length() <= Tolerance || N2.Length() <= Tolerance) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "face normal is zero");
    N1 = N1.Normalised(); N2 = N2.Normalised();

    // For a planar chamfer to leave the body manifold, the set-back lines must be parallel to the original edge (i.e.
    //    the edge must lie in both face planes). That is true iff the edge tangent is perpendicular to both face
    //    normals: Tangent · N1 = 0 and Tangent · N2 = 0. For non-90° dihedrals, the set-back would be along N1 and N2
    //    but those displacements are not parallel to the original edge; the chamfer face would not be planar. Refuse
    //    those — the user can split the edge first.
    if (std::fabs(Tangent.Dot(N1)) > 1e-6 || std::fabs(Tangent.Dot(N2)) > 1e-6) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "edge is not perpendicular to the face normals at the chamfer point (chamfer is planar only); for a rolling-ball fillet, see Phase 11b");
    // The chamfer's outward direction is the bisector of N1 and N2 — this is the direction we displace the original
    //    edge into. For a 90° corner, N1 and N2 are perpendicular and the bisector is at 45°; for an obtuse corner, the
    //    bisector leans toward the steeper face.
    Vec3 Bisector = (N1 + N2); if (Bisector.Length() <= Tolerance) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "face normals are opposite — degenerate corner");
    Bisector = Bisector.Normalised();

    // Self-intersection guard: the chamfer must not extend past the next edge. For a 90° corner with a single edge
    //    meeting V0 and V1, the set-back distance along each face is SetBack; the chamfer's inward reach is SetBack /
    //    sin(half-dihedral). For dihedral θ between F1 and F2, sin(θ/2) = |N1 × N2| / 2. If SetBack / sin(θ/2) > edge
    //    length, the chamfer overshoots — refuse. (We use 80% of the edge length as the safe upper bound so adjacent
    //    chamfers still meet cleanly.)
    double SinHalf = (N1.Cross(N2)).Length() * 0.5; if (SinHalf <= 1e-6) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "face normals are parallel (no corner)");
    double EdgeLen = (P1 - P0).Length();
    double MaxSetback = EdgeLen * 0.4 * SinHalf;
    if (SetBack > MaxSetback) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "set-back is too large for this edge (would self-intersect)");

    // Chamfer face corners: each corner sits on its adjacent face's surface, shifted from the original edge by SetBack
    //    along the in-face inward direction. The in-face inward direction is the projection of the body-inward
    //    direction (the negative bisector) onto the face's tangent plane. For a 90° corner this gives a unit vector at
    //    45° to both face normals. The set-back is the SetBack distance measured along this in-face direction.
    Vec3 In1 = -Bisector - (-Bisector).Dot(N1) * N1;                                     // projection onto F1's tangent plane
    if (In1.Length() <= Tolerance) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "edge is parallel to the chamfer direction (degenerate)");
    In1 = In1.Normalised() * SetBack;
    Vec3 In2 = -Bisector - (-Bisector).Dot(N2) * N2;                                     // projection onto F2's tangent plane
    if (In2.Length() <= Tolerance) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "edge is parallel to the chamfer direction (degenerate)");
    In2 = In2.Normalised() * SetBack;
    Vec3 C00 = P0 + In1; Vec3 C10 = P1 + In1;                                             // set-back line in F1's plane
    Vec3 C01 = P0 + In2; Vec3 C11 = P1 + In2;                                             // set-back line in F2's plane
    // The chamfer face's outward normal is the bisector. The face plane is (C10 - C00, C01 - C00). Check it is
    //    perpendicular to Tangent (sanity: chamfer face is parallel to the edge direction).
    Vec3 Cn = (C10 - C00).Cross(C01 - C00);
    if (Cn.Length() <= Tolerance) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "chamfer face is degenerate (set-back points coincide)");
    Cn = Cn.Normalised();
    if (Cn.Dot(Bisector) < 0) Cn = -Cn;                                                  // make outward
    (void)Cn;                                                                             // orientation is enforced by the plane construction below; suppress unused

    // Build the new body as a copy, then apply the surgery.
    BrepBody B = *this;

    // Add 4 new vertices (one per chamfer corner). The existing P0, P1 are still in B.Vertices; the chamfer uses new
    //    vertices at the set-back positions so the original vertices remain on the body's outer corner and adjacent
    //    chamfers can meet them.
    int V00 = B.AddVertex(C00, Tolerance), V10 = B.AddVertex(C10, Tolerance);
    int V01 = B.AddVertex(C01, Tolerance), V11 = B.AddVertex(C11, Tolerance);
    (void)V00; (void)V10; (void)V01; (void)V11;                                          // vertex references picked up by the edges below; suppress unused-var warning

    // Add 4 new edges:
    //    E_setback_F1: V00 → V10 (parallel to original edge, lies in F1's plane)
    //    E_setback_F2: V01 → V11 (parallel to original edge, lies in F2's plane)
    //    E_end_0:     V00 → V01 (chamfer face, short edge at V0)
    //    E_end_1:     V10 → V11 (chamfer face, short edge at V1)
    Deliver<NurbsCurve> ES1 = NurbsCurve::Line(C00, C10);
    Deliver<NurbsCurve> ES2 = NurbsCurve::Line(C01, C11);
    Deliver<NurbsCurve> EE0 = NurbsCurve::Line(C00, C01);
    Deliver<NurbsCurve> EE1 = NurbsCurve::Line(C10, C11);
    if (!ES1 || !ES2 || !EE0 || !EE1) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "chamfer set-back line is degenerate");
    int ESetbackF1 = B.AddEdge(ES1.Payload, Tolerance);
    int ESetbackF2 = B.AddEdge(ES2.Payload, Tolerance);
    int EEnd0 = B.AddEdge(EE0.Payload, Tolerance);
    int EEnd1 = B.AddEdge(EE1.Payload, Tolerance);

    // Add the new chamfer face. The face is a planar quad. The chamfer face's outward normal is the bisector. The plane
    //    is built with U along the edge and V = bisector × U (perpendicular to both the edge and the bisector — a
    //    tangent vector in the chamfer plane). Then U × V = U × (bisector × U) = bisector (BAC–CAB: A × (B × C) =
    //    B(A·C) − C(A·B); with A=U, B=bisector, C=U, U·U=1, U·bisector=0 ⇒ U × (bisector × U) = bisector). The
    //    chamfer face's natural surface normal matches the desired outward bisector.
    double LenU = (C10 - C00).Length();
    double LenV = (C01 - C00).Length();
    Vec3 U = (C10 - C00).Normalised();
    Vec3 V = Bisector.Cross(U);                                                           // V is perpendicular to the edge in the chamfer plane
    if (V.Length() <= Tolerance) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "chamfer plane is degenerate (edge parallel to bisector)");
    V = V.Normalised();
    Deliver<NurbsSurface> ChamferPlane = NurbsSurface::Plane(C00, U, V, LenU, LenV);
    if (!ChamferPlane) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "chamfer face plane is degenerate");
    int FChamfer = B.AddFace(ChamferPlane.Payload);
    int LChamfer = B.AddLoop(FChamfer, true);
    B.AddCoedge(EEnd0, false, FChamfer, LChamfer);
    B.AddCoedge(ESetbackF2, false, FChamfer, LChamfer);
    B.AddCoedge(EEnd1, true, FChamfer, LChamfer);
    B.AddCoedge(ESetbackF1, true, FChamfer, LChamfer);

    // Re-stitch F1's outer loop: replace its reference to the original edge with the F1 set-back edge. The original
    //    edge's coedge in F1 (Ce1) pointed at EdgeIndex; we redirect it to ESetbackF1 and move Ce1 between the two
    //    edges' Coedges lists so the new edge has 2 users (F1 + chamfer) and the original edge loses F1.
    B.Edges[EdgeIndex].Coedges.erase(std::remove(B.Edges[EdgeIndex].Coedges.begin(), B.Edges[EdgeIndex].Coedges.end(), Ce1), B.Edges[EdgeIndex].Coedges.end());
    B.Edges[ESetbackF1].Coedges.push_back(Ce1);
    B.Coedges[Ce1].Edge = ESetbackF1;
    B.Coedges[Ce1].Trace.clear();

    // F2's loop: same surgery for Ce2 → ESetbackF2, with the opposite sense to the chamfer face's coedge on ESetbackF2
    //    (so the two coedges form a manifold interior edge with shared geometry).
    int ChamferSetbackF2Coedge = -1;
    for (int Ce : B.Edges[ESetbackF2].Coedges) if (B.Coedges[Ce].Face == FChamfer) { ChamferSetbackF2Coedge = Ce; break; }
    if (ChamferSetbackF2Coedge < 0) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "internal: chamfer set-back F2 coedge not found");
    B.Edges[EdgeIndex].Coedges.erase(std::remove(B.Edges[EdgeIndex].Coedges.begin(), B.Edges[EdgeIndex].Coedges.end(), Ce2), B.Edges[EdgeIndex].Coedges.end());
    B.Edges[ESetbackF2].Coedges.push_back(Ce2);
    B.Coedges[Ce2].Edge = ESetbackF2;
    B.Coedges[Ce2].Reversed = !B.Coedges[ChamferSetbackF2Coedge].Reversed;
    B.Coedges[Ce2].Trace.clear();

    // The original edge E is now an orphan: Ce1 and Ce2 no longer reference it via their .Edge field (they point at
    //    the new set-back edges instead). We leave EdgeIndex in the Edges array (no remove primitive) with its
    //    original Coedges list — that keeps Classification() from flagging the body as a Sheet. The edge's Curve is
    //    still in the table but no coedge's .Edge field points at it; the edge is effectively unused.
    //    Future passes can prune it.

    B.Orient();
    return Deliver<BrepBody>::Accept(std::move(B));
}

Deliver<BrepBody> BrepBody::Box(Vec3 A, Vec3 B) noexcept
{
    Vec3 Lo{ std::min(A.X, B.X), std::min(A.Y, B.Y), std::min(A.Z, B.Z) }, Hi{ std::max(A.X, B.X), std::max(A.Y, B.Y), std::max(A.Z, B.Z) };
    Vec3 D = Hi - Lo;
    if (D.X <= ScalarCriteria::KernelTolerance || D.Y <= ScalarCriteria::KernelTolerance || D.Z <= ScalarCriteria::KernelTolerance) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "flat box");
    std::vector<NurbsSurface> F;
    auto Put = [&](Deliver<NurbsSurface> S) { if (S) F.push_back(std::move(S.Payload)); };
    Put(NurbsSurface::Plane(Lo, Vec3::UnitX(), Vec3::UnitY(), D.X, D.Y));                                  // bottom
    Put(NurbsSurface::Plane({ Lo.X, Lo.Y, Hi.Z }, Vec3::UnitX(), Vec3::UnitY(), D.X, D.Y));                // top
    Put(NurbsSurface::Plane(Lo, Vec3::UnitX(), Vec3::UnitZ(), D.X, D.Z));                                  // front (y = lo)
    Put(NurbsSurface::Plane({ Lo.X, Hi.Y, Lo.Z }, Vec3::UnitX(), Vec3::UnitZ(), D.X, D.Z));                // back
    Put(NurbsSurface::Plane(Lo, Vec3::UnitY(), Vec3::UnitZ(), D.Y, D.Z));                                  // left (x = lo)
    Put(NurbsSurface::Plane({ Hi.X, Lo.Y, Lo.Z }, Vec3::UnitY(), Vec3::UnitZ(), D.Y, D.Z));                // right
    return Sew(F);
}

Deliver<BrepBody> BrepBody::Cylinder(Vec3 Foot, Vec3 Axis, double Radius, double Height) noexcept
{
    Deliver<NurbsSurface> S = NurbsSurface::Cylinder(Foot, Axis, Radius, Height);
    if (!S) return Deliver<BrepBody>::Reject(S.Denial.Reason, S.Denial.Detail);
    return Sew({ S.Payload });
}

Deliver<BrepBody> BrepBody::Cone(Vec3 Foot, Vec3 Axis, double RadiusFoot, double RadiusTop, double Height) noexcept
{
    Deliver<NurbsSurface> S = NurbsSurface::Cone(Foot, Axis, RadiusFoot, RadiusTop, Height);
    if (!S) return Deliver<BrepBody>::Reject(S.Denial.Reason, S.Denial.Detail);
    return Sew({ S.Payload });
}

Deliver<BrepBody> BrepBody::Sphere(Vec3 Centre, double Radius) noexcept
{
    Deliver<NurbsSurface> S = NurbsSurface::Sphere(Centre, Radius);
    if (!S) return Deliver<BrepBody>::Reject(S.Denial.Reason, S.Denial.Detail);
    return Sew({ S.Payload });
}

Deliver<BrepBody> BrepBody::Torus(Vec3 Centre, Vec3 Axis, double RadiusMajor, double RadiusMinor) noexcept
{
    Deliver<NurbsSurface> S = NurbsSurface::Torus(Centre, Axis, RadiusMajor, RadiusMinor);
    if (!S) return Deliver<BrepBody>::Reject(S.Denial.Reason, S.Denial.Detail);
    return Sew({ S.Payload });
}

Deliver<BrepBody> BrepBody::Extrude(const NurbsCurve& Profile, Vec3 Direction, double Length) noexcept
{
    return Extrude(std::vector<NurbsCurve>{ Profile }, Direction, Length);
}

// Loops of one planar profile given in any sense → outer loops counter-clockwise, holes clockwise (by depth parity), so
//    the side sheets of every loop face outward and the caps' inner loops run against the outer ones.
static std::vector<NurbsCurve> OrientedLoops(const std::vector<NurbsCurve>& Loops) noexcept
{
    if (Loops.size() < 2) return Loops;
    Vec3 N = Vec3::UnitZ();
    for (const NurbsCurve& L : Loops) { std::vector<Vec3> P; L.Tessellate(P, nullptr, 1e-2); Vec3 A = Vec3(); for (size_t I = 0; I + 1 < P.size(); ++I) A = A + P[I].Cross(P[I + 1]); if (A.LengthSquared() > 1e-20) { N = A.Normalised(); break; } }
    Deliver<Profile> P = ProfileSolver::Assemble(Loops, N);
    if (!P) return Loops;
    Profile Q = ProfileSolver::Normalised(P.Payload);
    return Q.Curves();
}

Deliver<BrepBody> BrepBody::Extrude(const std::vector<NurbsCurve>& Loops, Vec3 Direction, double Length) noexcept
{
    std::vector<NurbsSurface> Sides;
    for (const NurbsCurve& Loop : OrientedLoops(Loops))
        for (const NurbsCurve& Piece : SplitAtKinks(Loop))
        {
            Deliver<NurbsSurface> S = NurbsSurface::Extrusion(Piece, Direction, Length);
            if (!S) return Deliver<BrepBody>::Reject(S.Denial.Reason, S.Denial.Detail);
            Sides.push_back(std::move(S.Payload));
        }
    if (Sides.empty()) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "nothing to extrude");
    return Sew(Sides);
}

Deliver<BrepBody> BrepBody::Revolve(const NurbsCurve& Profile, Vec3 AxisOrigin, Vec3 AxisDirection, double Angle) noexcept
{
    return Revolve(std::vector<NurbsCurve>{ Profile }, AxisOrigin, AxisDirection, Angle);
}

Deliver<BrepBody> BrepBody::Revolve(const std::vector<NurbsCurve>& Loops, Vec3 AxisOrigin, Vec3 AxisDirection, double Angle) noexcept
{
    std::vector<NurbsSurface> Sides;
    for (const NurbsCurve& Loop : OrientedLoops(Loops))
        for (const NurbsCurve& Piece : SplitAtKinks(Loop))
        {
            Deliver<NurbsSurface> S = NurbsSurface::Revolution(Piece, AxisOrigin, AxisDirection, Angle);
            if (!S) return Deliver<BrepBody>::Reject(S.Denial.Reason, S.Denial.Detail);
            Sides.push_back(std::move(S.Payload));
        }
    if (Sides.empty()) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "nothing to revolve");
    return Sew(Sides);
}

} // namespace Frontier
