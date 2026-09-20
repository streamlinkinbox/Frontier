//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Kernel/ChamferSolver.cpp — Exact planar chamfer of one straight edge or of a planar face's rim, by topology
//============================================================================================================================================
// Both routes edit the body in place and reuse the slots the chamfer consumes, so no entity is renumbered:
//
//    single edge e = v0→v1 between faces A and B, third faces C0 and C1 at the ends
//      v0 slot → the cut on A's other edge at v0 (Qa0); a new vertex holds the cut on B's edge (Qb0); same at v1
//      e  slot → the set-back line in A (Qa0→Qa1); a new edge holds the set-back line in B (Qb0→Qb1); B's coedge moves
//      new edges Qa0→Qb0 in C0 and Qa1→Qb1 in C1; one new planar face with one loop over the four
//      ΔV = +2, ΔE = +3, ΔF = +1 — the Euler characteristic is unchanged by construction
//
//    rim of face F with vertices w_i and side edges s_i leaving each w_i
//      w_i slot → the inset vertex W_i (the rim edges follow it, so F's loop is already right)
//      new vertex Q_i on s_i where the two neighbouring chamfer planes cut it (they must agree)
//      rim edge slot → inset edge W_i→W_{i+1}; new lowered edge Q_i→Q_{i+1} takes the side face's coedge; new mitre
//      edge Q_i→W_i; one new planar face per rim edge
//      ΔV = +n, ΔE = +2n, ΔF = +n
//
// The removed material is, per edge, the prism over the triangle (w, W, Q) at each end — three tetrahedra — and the
//    result must agree with source − Σ pieces within the kernel's volume gate, on top of closed/manifold/oriented.

#include "Kernel/ChamferSolver.h"
#include "Kernel/SurfaceSpecification.h"
#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{
    constexpr double Tol = ScalarCriteria::KernelTolerance;                             // [m]

    [[nodiscard]] double Reach(const BrepBody& B) noexcept { return std::max(1.0, B.Bounds().Diagonal()); }

    [[nodiscard]] bool Straight(const BrepEdge& E) noexcept
    {
        return !E.Closed() && E.Curve.Degree == 1 && E.Curve.Poles.size() == 2;
    }

    // Affine basis of a four-pole planar patch (the twin of TweakSolver's): P(u, v) = O + Du·û + Dv·v̂ on the domain.
    struct PlaneBasis
    {
        Vec3   Origin, Du, Dv, Normal;
        double U0 = 0.0, U1 = 1.0, V0 = 0.0, V1 = 1.0;                                  // [-] parameter domain
        [[nodiscard]] Vec2 Uv(Vec3 P) const noexcept
        {
            const Vec3 D = P - Origin;
            const double Guu = Du.Dot(Du), Guv = Du.Dot(Dv), Gvv = Dv.Dot(Dv), Bu = D.Dot(Du), Bv = D.Dot(Dv);
            const double Det = Guu * Gvv - Guv * Guv;
            if (std::fabs(Det) <= Tol) return { U0, V0 };
            const double A = (Bu * Gvv - Bv * Guv) / Det, B = (Bv * Guu - Bu * Guv) / Det;
            return { U0 + A * (U1 - U0), V0 + B * (V1 - V0) };
        }
        [[nodiscard]] double Height(Vec3 P) const noexcept { return (P - Origin).Dot(Normal); }
    };

    [[nodiscard]] bool PlanarBasisOf(const NurbsSurface& S, double Scale, PlaneBasis& F) noexcept
    {
        if (S.DegreeU != 1 || S.DegreeV != 1 || S.CountU != 2 || S.CountV != 2 || S.Poles.size() != 4) return false;
        for (const Vec4& P : S.Poles) if (std::fabs(P.W - 1.0) > ScalarCriteria::ParametricEpsilon) return false;
        F.Origin = S.Pole(0, 0).Divide();
        F.Du = S.Pole(1, 0).Divide() - F.Origin;
        F.Dv = S.Pole(0, 1).Divide() - F.Origin;
        const Vec3 N = F.Du.Cross(F.Dv);
        if (N.Length() <= Tol * Scale * Scale) return false;
        F.Normal = N.Normalised();
        F.U0 = S.DomainStartU(); F.U1 = S.DomainEndU(); F.V0 = S.DomainStartV(); F.V1 = S.DomainEndV();
        return std::fabs(F.Height(S.Pole(1, 1).Divide())) <= ScalarCriteria::GeometricTolerance * Scale;   // the fourth pole in plane
    }

    [[nodiscard]] int OtherVertex(const BrepEdge& E, int V) noexcept { return E.VertexStart == V ? E.VertexEnd : E.VertexStart; }

    [[nodiscard]] int CoedgeStartVertex(const BrepBody& B, int C) noexcept
    {
        const BrepCoedge& K = B.Coedges[C]; const BrepEdge& E = B.Edges[K.Edge];
        return K.Reversed ? E.VertexEnd : E.VertexStart;
    }
    [[nodiscard]] int CoedgeEndVertex(const BrepBody& B, int C) noexcept
    {
        const BrepCoedge& K = B.Coedges[C]; const BrepEdge& E = B.Edges[K.Edge];
        return K.Reversed ? E.VertexStart : E.VertexEnd;
    }

    [[nodiscard]] std::vector<int> EdgesAtVertex(const BrepBody& B, int V) noexcept
    {
        std::vector<int> Out;
        for (size_t E = 0; E < B.Edges.size(); ++E)
            if (B.Edges[E].VertexStart == V || B.Edges[E].VertexEnd == V) Out.push_back(static_cast<int>(E));
        return Out;
    }

    [[nodiscard]] int CoedgeOfEdgeOnFace(const BrepBody& B, int Edge, int Face) noexcept
    {
        for (int C : B.Edges[Edge].Coedges) if (B.Coedges[C].Face == Face) return C;
        return -1;
    }

    // Unit direction of edge E on face F in F's own traversal sense, and the in-face inward normal (left of travel
    //    seen from outside: Outward × Travel), given the face's outward normal.
    [[nodiscard]] bool TravelAndInward(const BrepBody& B, int Edge, int Face, Vec3 Outward, Vec3& Travel, Vec3& Inward) noexcept
    {
        const int C = CoedgeOfEdgeOnFace(B, Edge, Face);
        if (C < 0) return false;
        const BrepEdge& E = B.Edges[Edge];
        Vec3 D = B.Vertices[E.VertexEnd].Point - B.Vertices[E.VertexStart].Point;
        if (D.Length() <= Tol) return false;
        Travel = D.Normalised() * (B.Coedges[C].Reversed ? -1.0 : 1.0);
        Inward = Outward.Cross(Travel).Normalised();
        return Inward.Length() > 0.5;
    }

    [[nodiscard]] Vec3 OutwardNormal(const BrepBody& B, int Face) noexcept
    {
        const NurbsSurface& S = B.Faces[Face].Surface;
        return B.FaceNormal(Face, 0.5 * (S.DomainStartU() + S.DomainEndU()), 0.5 * (S.DomainStartV() + S.DomainEndV())).Normalised();
    }

    void RebuildLine(BrepBody& B, int Edge) noexcept
    {
        BrepEdge& E = B.Edges[Edge];
        Deliver<NurbsCurve> L = NurbsCurve::Line(B.Vertices[E.VertexStart].Point, B.Vertices[E.VertexEnd].Point);
        if (L) E.Curve = std::move(L.Payload);
    }

    int NewVertex(BrepBody& B, Vec3 P) noexcept { B.Vertices.push_back({ P }); return static_cast<int>(B.Vertices.size() - 1); }

    int NewLineEdge(BrepBody& B, int V0, int V1) noexcept
    {
        BrepEdge E; E.VertexStart = V0; E.VertexEnd = V1;
        Deliver<NurbsCurve> L = NurbsCurve::Line(B.Vertices[V0].Point, B.Vertices[V1].Point);
        if (L) E.Curve = std::move(L.Payload);
        B.Edges.push_back(std::move(E));
        return static_cast<int>(B.Edges.size() - 1);
    }

    // A coedge inserted into Loop right after position After (or appended), registered on its edge.
    int InsertCoedge(BrepBody& B, int Edge, bool Reversed, int Face, int Loop, int After) noexcept
    {
        B.Coedges.push_back({ Edge, Reversed, Face, Loop, {} });
        const int C = static_cast<int>(B.Coedges.size() - 1);
        B.Edges[Edge].Coedges.push_back(C);
        std::vector<int>& L = B.Loops[Loop].Coedges;
        if (After < 0 || After + 1 >= static_cast<int>(L.size())) L.push_back(C);
        else L.insert(L.begin() + After + 1, C);
        return C;
    }

    // Materialise every trace of a face (natural faces derive theirs on demand) so the face can become a trimmed plane
    //    whose loop no longer coincides with the patch boundary; then retrace the straight coedges listed as changed.
    void RetracePlanarFace(const BrepBody& Before, BrepBody& B, int Face, const PlaneBasis& Basis, const std::vector<int>& Changed) noexcept
    {
        BrepFace& F = B.Faces[Face];
        if (F.Natural)
        {
            for (int Loop : F.Loops)
                for (int C : B.Loops[Loop].Coedges)
                    if (C < static_cast<int>(Before.Coedges.size()) && B.Coedges[C].Trace.empty())
                        B.Coedges[C].Trace = Before.CoedgeTrace(C);
            F.Natural = false;
        }
        for (int C : Changed) B.Coedges[C].Trace = { Basis.Uv(B.CoedgeStart(C)), Basis.Uv(B.CoedgeEnd(C)) };
    }

    // Planar patch through four coplanar corners with a margin, its basis, and the traces of a fresh one-loop face.
    [[nodiscard]] bool PlanarFaceOver(BrepBody& B, const Vec3* Corner, Vec3 Outward, int& FaceOut, PlaneBasis& BasisOut) noexcept
    {
        const Vec3 AxisU = (Corner[1] - Corner[0]).Normalised();
        if (AxisU.Length() < 0.5 || Outward.Length() < 0.5) return false;
        const Vec3 AxisV = Outward.Cross(AxisU).Normalised();
        double UMin = 0, UMax = 0, VMin = 0, VMax = 0;
        for (int I = 0; I < 4; ++I)
        {
            const Vec3 D = Corner[I] - Corner[0];
            const double U = D.Dot(AxisU), V = D.Dot(AxisV);
            UMin = std::min(UMin, U); UMax = std::max(UMax, U); VMin = std::min(VMin, V); VMax = std::max(VMax, V);
        }
        const double Margin = 0.1 * std::max(UMax - UMin, VMax - VMin);
        Deliver<NurbsSurface> Plane = NurbsSurface::Plane(Corner[0] + AxisU * (UMin - Margin) + AxisV * (VMin - Margin), AxisU, AxisV,
                                                          UMax - UMin + 2.0 * Margin, VMax - VMin + 2.0 * Margin);
        if (!Plane) return false;
        FaceOut = B.AddFace(std::move(Plane.Payload));
        BrepFace& F = B.Faces[FaceOut];
        F.Natural = false;
        if (!PlanarBasisOf(F.Surface, 1.0, BasisOut)) return false;
        F.Reversed = BasisOut.Normal.Dot(Outward) < 0.0;
        return true;
    }

    // Loop of a fresh face over four edges: the first is walked against its one existing coedge (manifold orientation),
    //    each next edge is whichever remaining one continues from the current vertex, and the walk must return home.
    [[nodiscard]] bool CloseQuadLoop(BrepBody& B, int Face, const int* Edge, const PlaneBasis& Basis) noexcept
    {
        const int Loop = B.AddLoop(Face, true);
        const BrepEdge& First = B.Edges[Edge[0]];
        if (First.Coedges.size() != 1) return false;
        bool Reversed = !B.Coedges[First.Coedges[0]].Reversed;
        const int Home = Reversed ? First.VertexEnd : First.VertexStart;
        int At = Reversed ? First.VertexStart : First.VertexEnd;
        std::vector<int> Made;
        Made.push_back(InsertCoedge(B, Edge[0], Reversed, Face, Loop, -1));
        bool Used[4] = { true, false, false, false };
        for (int Step = 1; Step < 4; ++Step)
        {
            int Next = -1;
            for (int I = 1; I < 4; ++I)
                if (!Used[I] && (B.Edges[Edge[I]].VertexStart == At || B.Edges[Edge[I]].VertexEnd == At)) { Next = I; break; }
            if (Next < 0) return false;
            const BrepEdge& E = B.Edges[Edge[Next]];
            Reversed = E.VertexStart != At;
            At = Reversed ? E.VertexStart : E.VertexEnd;
            Used[Next] = true;
            Made.push_back(InsertCoedge(B, Edge[Next], Reversed, Face, Loop, -1));
        }
        if (At != Home) return false;
        for (int C : Made) B.Coedges[C].Trace = { Basis.Uv(B.CoedgeStart(C)), Basis.Uv(B.CoedgeEnd(C)) };
        return true;
    }

    // Volume of the prism over the triangles (W0, X0, Q0) and (W1, X1, Q1) as three tetrahedra; exact for planar quads.
    [[nodiscard]] double PrismVolume(Vec3 W0, Vec3 X0, Vec3 Q0, Vec3 W1, Vec3 X1, Vec3 Q1) noexcept
    {
        auto Tet = [](Vec3 A, Vec3 B, Vec3 C, Vec3 D) { return std::fabs((B - A).Cross(C - A).Dot(D - A)) / 6.0; };
        return Tet(W0, X0, Q0, W1) + Tet(X0, Q0, W1, X1) + Tet(Q0, W1, X1, Q1);
    }

    [[nodiscard]] Deliver<BrepBody> Finish(BrepBody&& Out, double Before, double Removed) noexcept
    {
        const BodyReport After = Out.Validate();
        if (!After.Closed || !After.Manifold || !After.Oriented)
            return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "chamfer left the body open; the selection is not supported");
        if (!ScalarCriteria::WithinVolumeTolerance(After.Volume, Before - Removed))
            return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "chamfer volume disagrees with the exact removed wedge");
        return Deliver<BrepBody>::Accept(std::move(Out));
    }

    // Everything the two routes need to know about one rim/edge end: the corner vertex, the two edges leaving it in
    //    faces A and B, and the third face they share.
    struct CornerFan
    {
        int Vertex = -1, EdgeA = -1, EdgeB = -1, FaceC = -1;
    };

    [[nodiscard]] const char* ResolveCorner(const BrepBody& B, int Vertex, int Edge, int FaceA, int FaceB, CornerFan& Out) noexcept
    {
        std::vector<int> Spokes = EdgesAtVertex(B, Vertex);
        if (Spokes.size() != 3) return "an end vertex of the edge is not three-valent";
        Out.Vertex = Vertex;
        for (int S : Spokes)
        {
            if (S == Edge) continue;
            if (!Straight(B.Edges[S])) return "an edge meeting the chamfered edge is curved";
            if (CoedgeOfEdgeOnFace(B, S, FaceA) >= 0) Out.EdgeA = S;
            else if (CoedgeOfEdgeOnFace(B, S, FaceB) >= 0) Out.EdgeB = S;
        }
        if (Out.EdgeA < 0 || Out.EdgeB < 0 || Out.EdgeA == Out.EdgeB) return "the edges around an end vertex do not pair with its two faces";
        int FaceOfA = -1, FaceOfB = -1;
        for (int C : B.Edges[Out.EdgeA].Coedges) if (B.Coedges[C].Face != FaceA) FaceOfA = B.Coedges[C].Face;
        for (int C : B.Edges[Out.EdgeB].Coedges) if (B.Coedges[C].Face != FaceB) FaceOfB = B.Coedges[C].Face;
        if (FaceOfA < 0 || FaceOfA != FaceOfB) return "the two edges around an end vertex do not share one third face";
        Out.FaceC = FaceOfA;
        return nullptr;
    }

    // Where a plane (point P, normal N) cuts the straight edge leaving Vertex; strictly inside the edge or refused.
    [[nodiscard]] const char* CutEdgeFromVertex(const BrepBody& B, int Edge, int Vertex, Vec3 P, Vec3 N, double Scale, Vec3& Cut) noexcept
    {
        const Vec3 From = B.Vertices[Vertex].Point, To = B.Vertices[OtherVertex(B.Edges[Edge], Vertex)].Point;
        const Vec3 D = To - From;
        const double Denominator = D.Dot(N);
        if (std::fabs(Denominator) <= Tol * Scale) return "the chamfer plane runs along an edge at an end vertex";
        const double S = (P - From).Dot(N) / Denominator;
        if (S <= ScalarCriteria::GeometricTolerance || S >= 1.0 - ScalarCriteria::GeometricTolerance)
            return "set-back reaches the far end of an adjacent edge";
        Cut = From + D * S;
        return nullptr;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SINGLE EDGE
//------------------------------------------------------------------------------------------------------------------------

Deliver<BrepBody> ChamferSolver::ChamferEdge(const BrepBody& Body, int Edge, double SetBack) noexcept
{
    using R = Deliver<BrepBody>;
    if (SetBack <= Tol) return R::Reject(RefusalReason::DegenerateInput, "set-back is zero or negative");
    if (Edge < 0 || Edge >= static_cast<int>(Body.Edges.size())) return R::Reject(RefusalReason::Unsupported, "edge index out of range");
    const BodyReport Before = Body.Validate();
    if (!Before.Solid()) return R::Reject(RefusalReason::OpenWire, "chamfer needs a closed solid");
    const BrepEdge& E = Body.Edges[Edge];
    if (!Straight(E)) return R::Reject(RefusalReason::Unsupported, "the planar chamfer takes a straight edge");
    if (E.Coedges.size() != 2) return R::Reject(RefusalReason::Unsupported, "edge is not a manifold interior edge");
    const double Scale = Reach(Body);
    const int FaceA = Body.Coedges[E.Coedges[0]].Face, FaceB = Body.Coedges[E.Coedges[1]].Face;
    PlaneBasis BasisA, BasisB;
    if (FaceA == FaceB || !PlanarBasisOf(Body.Faces[FaceA].Surface, Scale, BasisA) || !PlanarBasisOf(Body.Faces[FaceB].Surface, Scale, BasisB))
        return R::Reject(RefusalReason::Unsupported, "both faces along the edge must be planar");
    if (Body.Faces[FaceA].Loops.size() != 1 || Body.Faces[FaceB].Loops.size() != 1)
        return R::Reject(RefusalReason::Unsupported, "a face along the edge has a hole");

    const Vec3 OutA = OutwardNormal(Body, FaceA), OutB = OutwardNormal(Body, FaceB);
    Vec3 TravelA, InwardA, TravelB, InwardB;
    if (!TravelAndInward(Body, Edge, FaceA, OutA, TravelA, InwardA) || !TravelAndInward(Body, Edge, FaceB, OutB, TravelB, InwardB))
        return R::Reject(RefusalReason::DegenerateInput, "edge is degenerate");
    if (InwardA.Dot(OutB) > -ScalarCriteria::GeometricTolerance)
        return R::Reject(RefusalReason::Unsupported, InwardA.Dot(InwardB) < -1.0 + ScalarCriteria::GeometricTolerance
                         ? "the faces are coplanar at this edge; there is no corner to chamfer"
                         : "concave edge: a chamfer there adds material and is not supported");

    // Chamfer plane through the two set-back lines, normal toward the outside.
    const Vec3 Axis = TravelA;
    Vec3 Normal = Axis.Cross(InwardB - InwardA).Normalised();
    if (Normal.Length() < 0.5) return R::Reject(RefusalReason::DegenerateInput, "the two faces meet at a knife edge");
    if (Normal.Dot(OutA + OutB) < 0.0) Normal = Normal * -1.0;
    const Vec3 OnPlane = Body.Vertices[E.VertexStart].Point + InwardA * SetBack;

    CornerFan Fan0, Fan1;
    if (const char* Why = ResolveCorner(Body, E.VertexStart, Edge, FaceA, FaceB, Fan0)) return R::Reject(RefusalReason::Unsupported, Why);
    if (const char* Why = ResolveCorner(Body, E.VertexEnd, Edge, FaceA, FaceB, Fan1)) return R::Reject(RefusalReason::Unsupported, Why);
    PlaneBasis BasisC0, BasisC1;
    if (!PlanarBasisOf(Body.Faces[Fan0.FaceC].Surface, Scale, BasisC0) || !PlanarBasisOf(Body.Faces[Fan1.FaceC].Surface, Scale, BasisC1))
        return R::Reject(RefusalReason::Unsupported, "the face at an end of the edge must be planar");
    if (Body.Faces[Fan0.FaceC].Loops.size() != 1 || Body.Faces[Fan1.FaceC].Loops.size() != 1)
        return R::Reject(RefusalReason::Unsupported, "the face at an end of the edge has a hole");

    Vec3 Qa0, Qb0, Qa1, Qb1;
    if (const char* Why = CutEdgeFromVertex(Body, Fan0.EdgeA, Fan0.Vertex, OnPlane, Normal, Scale, Qa0)) return R::Reject(RefusalReason::Unsupported, Why);
    if (const char* Why = CutEdgeFromVertex(Body, Fan0.EdgeB, Fan0.Vertex, OnPlane, Normal, Scale, Qb0)) return R::Reject(RefusalReason::Unsupported, Why);
    if (const char* Why = CutEdgeFromVertex(Body, Fan1.EdgeA, Fan1.Vertex, OnPlane, Normal, Scale, Qa1)) return R::Reject(RefusalReason::Unsupported, Why);
    if (const char* Why = CutEdgeFromVertex(Body, Fan1.EdgeB, Fan1.Vertex, OnPlane, Normal, Scale, Qb1)) return R::Reject(RefusalReason::Unsupported, Why);
    const Vec3 W0 = Body.Vertices[E.VertexStart].Point, W1 = Body.Vertices[E.VertexEnd].Point;
    const double Removed = PrismVolume(W0, Qa0, Qb0, W1, Qa1, Qb1);

    BrepBody Out = Body;
    // Vertices: the v slots take the A-side cuts, new vertices take the B-side cuts; B's spokes re-attach to them.
    const int Va0 = E.VertexStart, Va1 = E.VertexEnd;
    Out.Vertices[Va0].Point = Qa0; Out.Vertices[Va1].Point = Qa1;
    const int Vb0 = NewVertex(Out, Qb0), Vb1 = NewVertex(Out, Qb1);
    auto Reattach = [&](int SpokeEdge, int OldVertex, int NewV)
    {
        BrepEdge& S = Out.Edges[SpokeEdge];
        if (S.VertexStart == OldVertex) S.VertexStart = NewV; else S.VertexEnd = NewV;
    };
    Reattach(Fan0.EdgeB, Va0, Vb0); Reattach(Fan1.EdgeB, Va1, Vb1);
    for (int Spoke : { Fan0.EdgeA, Fan0.EdgeB, Fan1.EdgeA, Fan1.EdgeB }) RebuildLine(Out, Spoke);

    // Edges: the e slot is the A-side line; the B-side line is new and takes B's coedge.
    RebuildLine(Out, Edge);
    const int CoedgeA = CoedgeOfEdgeOnFace(Body, Edge, FaceA), CoedgeB = CoedgeOfEdgeOnFace(Body, Edge, FaceB);
    const int EdgeB = NewLineEdge(Out, Vb0, Vb1);
    Out.Coedges[CoedgeB].Edge = EdgeB;
    Out.Edges[EdgeB].Coedges = { CoedgeB };
    Out.Edges[Edge].Coedges = { CoedgeA };

    // End faces: a short edge between the two cuts, inserted between the coedges of the two spokes.
    int EndEdge[2] = { -1, -1 }; int EndCoedge[2] = { -1, -1 };
    const CornerFan* Fans[2] = { &Fan0, &Fan1 };
    const int VaOf[2] = { Va0, Va1 }, VbOf[2] = { Vb0, Vb1 };
    for (int End = 0; End < 2; ++End)
    {
        const CornerFan& Fan = *Fans[End];
        EndEdge[End] = NewLineEdge(Out, VaOf[End], VbOf[End]);
        const int Loop = Out.Faces[Fan.FaceC].Loops[0];
        std::vector<int>& L = Out.Loops[Loop].Coedges;
        const int Ca = CoedgeOfEdgeOnFace(Out, Fan.EdgeA, Fan.FaceC), Cb = CoedgeOfEdgeOnFace(Out, Fan.EdgeB, Fan.FaceC);
        int Pa = -1, Pb = -1;
        for (int I = 0; I < static_cast<int>(L.size()); ++I) { if (L[I] == Ca) Pa = I; if (L[I] == Cb) Pb = I; }
        if (Pa < 0 || Pb < 0) return R::Reject(RefusalReason::Unsupported, "the end face does not walk both spokes");
        const int N = static_cast<int>(L.size());
        // The spoke that arrives at the corner comes first in the loop; the short edge is walked from its cut to the other's.
        if ((Pa + 1) % N == Pb) EndCoedge[End] = InsertCoedge(Out, EndEdge[End], false, Fan.FaceC, Loop, Pa);
        else if ((Pb + 1) % N == Pa) EndCoedge[End] = InsertCoedge(Out, EndEdge[End], true, Fan.FaceC, Loop, Pb);
        else return R::Reject(RefusalReason::Unsupported, "the two spokes are not consecutive in the end face");
    }

    // The chamfer face itself.
    const Vec3 Corners[4] = { Qa0, Qa1, Qb1, Qb0 };
    int FaceChamfer = -1; PlaneBasis BasisChamfer;
    if (!PlanarFaceOver(Out, Corners, Normal, FaceChamfer, BasisChamfer)) return R::Reject(RefusalReason::DegenerateInput, "the chamfer face is degenerate");
    const int Ring[4] = { Edge, EndEdge[1], EdgeB, EndEdge[0] };
    if (!CloseQuadLoop(Out, FaceChamfer, Ring, BasisChamfer)) return R::Reject(RefusalReason::Unsupported, "the chamfer face does not close");

    // Re-trim the four neighbours on their unchanged planes.
    RetracePlanarFace(Body, Out, FaceA, BasisA, { CoedgeA, CoedgeOfEdgeOnFace(Out, Fan0.EdgeA, FaceA), CoedgeOfEdgeOnFace(Out, Fan1.EdgeA, FaceA) });
    RetracePlanarFace(Body, Out, FaceB, BasisB, { CoedgeB, CoedgeOfEdgeOnFace(Out, Fan0.EdgeB, FaceB), CoedgeOfEdgeOnFace(Out, Fan1.EdgeB, FaceB) });
    RetracePlanarFace(Body, Out, Fan0.FaceC, BasisC0, { EndCoedge[0], CoedgeOfEdgeOnFace(Out, Fan0.EdgeA, Fan0.FaceC), CoedgeOfEdgeOnFace(Out, Fan0.EdgeB, Fan0.FaceC) });
    RetracePlanarFace(Body, Out, Fan1.FaceC, BasisC1, { EndCoedge[1], CoedgeOfEdgeOnFace(Out, Fan1.EdgeA, Fan1.FaceC), CoedgeOfEdgeOnFace(Out, Fan1.EdgeB, Fan1.FaceC) });
    return Finish(std::move(Out), Before.Volume, Removed);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  FACE RIM
//------------------------------------------------------------------------------------------------------------------------

Deliver<BrepBody> ChamferSolver::ChamferFaceRim(const BrepBody& Body, int Face, double SetBack) noexcept
{
    using R = Deliver<BrepBody>;
    if (SetBack <= Tol) return R::Reject(RefusalReason::DegenerateInput, "set-back is zero or negative");
    if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return R::Reject(RefusalReason::Unsupported, "face index out of range");
    const BodyReport Before = Body.Validate();
    if (!Before.Solid()) return R::Reject(RefusalReason::OpenWire, "chamfer needs a closed solid");
    const BrepFace& F = Body.Faces[Face];
    const double Scale = Reach(Body);
    PlaneBasis BasisF;
    if (!PlanarBasisOf(F.Surface, Scale, BasisF)) return R::Reject(RefusalReason::Unsupported, "the rim chamfer takes a planar face");
    if (F.Loops.size() != 1) return R::Reject(RefusalReason::Unsupported, "the face has a hole; its rim is not one loop");
    const std::vector<int>& Rim = Body.Loops[F.Loops[0]].Coedges;
    const int N = static_cast<int>(Rim.size());
    if (N < 3) return R::Reject(RefusalReason::Unsupported, "the rim has fewer than three edges");
    const Vec3 OutF = OutwardNormal(Body, Face);

    // Per rim edge: side face, travel and inward directions in both faces, the chamfer plane.
    struct RimEdge { int Edge, Coedge, Side; Vec3 InwardF, InwardS, Normal, OnPlane; PlaneBasis BasisS; };
    std::vector<RimEdge> Edges(N);
    for (int I = 0; I < N; ++I)
    {
        RimEdge& K = Edges[I];
        K.Coedge = Rim[I]; K.Edge = Body.Coedges[K.Coedge].Edge;
        const BrepEdge& E = Body.Edges[K.Edge];
        if (!Straight(E)) return R::Reject(RefusalReason::Unsupported, "a rim edge is curved");
        if (E.Coedges.size() != 2) return R::Reject(RefusalReason::Unsupported, "a rim edge is not a manifold interior edge");
        K.Side = Body.Coedges[E.Coedges[0]].Face == Face ? Body.Coedges[E.Coedges[1]].Face : Body.Coedges[E.Coedges[0]].Face;
        if (K.Side == Face || !PlanarBasisOf(Body.Faces[K.Side].Surface, Scale, K.BasisS)) return R::Reject(RefusalReason::Unsupported, "a side face along the rim is not planar");
        if (Body.Faces[K.Side].Loops.size() != 1) return R::Reject(RefusalReason::Unsupported, "a side face along the rim has a hole");
        Vec3 TravelF, TravelS;
        const Vec3 OutS = OutwardNormal(Body, K.Side);
        if (!TravelAndInward(Body, K.Edge, Face, OutF, TravelF, K.InwardF) || !TravelAndInward(Body, K.Edge, K.Side, OutS, TravelS, K.InwardS))
            return R::Reject(RefusalReason::DegenerateInput, "a rim edge is degenerate");
        if (K.InwardF.Dot(OutS) > -ScalarCriteria::GeometricTolerance) return R::Reject(RefusalReason::Unsupported, "a rim edge is concave or its faces are coplanar");
        K.Normal = TravelF.Cross(K.InwardS - K.InwardF).Normalised();
        if (K.Normal.Length() < 0.5) return R::Reject(RefusalReason::DegenerateInput, "a rim edge is a knife edge");
        if (K.Normal.Dot(OutF + OutS) < 0.0) K.Normal = K.Normal * -1.0;
        K.OnPlane = Body.Vertices[E.VertexStart].Point + K.InwardF * SetBack;
    }

    // Per rim vertex (the head of rim edge I−1 = the tail of rim edge I): the side edge leaving it, the inset vertex as
    //    the meet of the two set-back lines in F, and the one cut both chamfer planes must make on the side edge.
    struct RimVertex { int Vertex, SideEdge; Vec3 Inset, Cut; };
    std::vector<RimVertex> Vertices(N);
    for (int I = 0; I < N; ++I)
    {
        const int Previous = (I + N - 1) % N;
        RimVertex& V = Vertices[I];
        V.Vertex = CoedgeStartVertex(Body, Rim[I]);
        if (CoedgeEndVertex(Body, Rim[Previous]) != V.Vertex) return R::Reject(RefusalReason::Unsupported, "the rim loop is not head to tail");
        std::vector<int> Spokes = EdgesAtVertex(Body, V.Vertex);
        if (Spokes.size() != 3) return R::Reject(RefusalReason::Unsupported, "a rim vertex is not three-valent");
        V.SideEdge = -1;
        for (int S : Spokes) if (S != Edges[I].Edge && S != Edges[Previous].Edge) V.SideEdge = S;
        if (V.SideEdge < 0 || !Straight(Body.Edges[V.SideEdge])) return R::Reject(RefusalReason::Unsupported, "the side edge at a rim vertex is missing or curved");
        // Inset: intersect the two set-back lines (both in F's plane) — line I−1: P + s·T, line I: Q + t·U.
        const BrepEdge& Ep = Body.Edges[Edges[Previous].Edge]; const BrepEdge& Ei = Body.Edges[Edges[I].Edge];
        const Vec3 Tp = (Body.Vertices[Ep.VertexEnd].Point - Body.Vertices[Ep.VertexStart].Point).Normalised();
        const Vec3 Ti = (Body.Vertices[Ei.VertexEnd].Point - Body.Vertices[Ei.VertexStart].Point).Normalised();
        const Vec3 P = Body.Vertices[Ep.VertexStart].Point + Edges[Previous].InwardF * SetBack;
        const Vec3 Q = Body.Vertices[Ei.VertexStart].Point + Edges[I].InwardF * SetBack;
        const Vec3 Cross = Tp.Cross(Ti);
        if (Cross.Length() <= ScalarCriteria::GeometricTolerance) return R::Reject(RefusalReason::Unsupported, "two consecutive rim edges are collinear");
        const double S = (Q - P).Cross(Ti).Dot(Cross) / Cross.Dot(Cross);
        V.Inset = P + Tp * S;
        if (std::fabs(BasisF.Height(V.Inset)) > ScalarCriteria::GeometricTolerance * Scale) return R::Reject(RefusalReason::Unsupported, "the inset corner leaves the face plane");
        Vec3 CutPrevious, CutNext;
        if (const char* Why = CutEdgeFromVertex(Body, V.SideEdge, V.Vertex, Edges[Previous].OnPlane, Edges[Previous].Normal, Scale, CutPrevious)) return R::Reject(RefusalReason::Unsupported, Why);
        if (const char* Why = CutEdgeFromVertex(Body, V.SideEdge, V.Vertex, Edges[I].OnPlane, Edges[I].Normal, Scale, CutNext)) return R::Reject(RefusalReason::Unsupported, Why);
        if (CutPrevious.Distance(CutNext) > ScalarCriteria::GeometricTolerance * Scale)
            return R::Reject(RefusalReason::Unsupported, "the two chamfers meeting at a rim corner cut its side edge at different points; that corner needs a vertex face");
        V.Cut = CutPrevious;
    }
    // The inset polygon must still turn the same way as the rim (a set-back larger than the inradius would flip it).
    for (int I = 0; I < N; ++I)
    {
        const Vec3 A = Vertices[I].Inset, B = Vertices[(I + 1) % N].Inset, C = Vertices[(I + 2) % N].Inset;
        const Vec3 Wa = Body.Vertices[Vertices[I].Vertex].Point, Wb = Body.Vertices[Vertices[(I + 1) % N].Vertex].Point, Wc = Body.Vertices[Vertices[(I + 2) % N].Vertex].Point;
        if ((B - A).Cross(C - B).Dot(OutF) * (Wb - Wa).Cross(Wc - Wb).Dot(OutF) <= 0.0)
            return R::Reject(RefusalReason::Unsupported, "set-back consumes the face; the inset rim folds over");
    }

    double Removed = 0.0;
    for (int I = 0; I < N; ++I)
    {
        const int Next = (I + 1) % N;
        Removed += PrismVolume(Body.Vertices[Vertices[I].Vertex].Point, Vertices[I].Inset, Vertices[I].Cut,
                               Body.Vertices[Vertices[Next].Vertex].Point, Vertices[Next].Inset, Vertices[Next].Cut);
    }

    BrepBody Out = Body;
    // Vertices: rim slots become the inset corners (F's loop follows); side edges re-attach to new cut vertices.
    std::vector<int> CutVertex(N);
    for (int I = 0; I < N; ++I)
    {
        Out.Vertices[Vertices[I].Vertex].Point = Vertices[I].Inset;
        CutVertex[I] = NewVertex(Out, Vertices[I].Cut);
        BrepEdge& S = Out.Edges[Vertices[I].SideEdge];
        if (S.VertexStart == Vertices[I].Vertex) S.VertexStart = CutVertex[I]; else S.VertexEnd = CutVertex[I];
    }
    for (int I = 0; I < N; ++I) { RebuildLine(Out, Vertices[I].SideEdge); RebuildLine(Out, Edges[I].Edge); }

    // Edges: lowered lines take the side coedges; mitre lines join each cut to its inset corner.
    std::vector<int> Lowered(N), Mitre(N), SideCoedge(N);
    for (int I = 0; I < N; ++I)
    {
        const int Next = (I + 1) % N;
        const BrepEdge& E = Body.Edges[Edges[I].Edge];
        // The lowered edge runs in the rim edge's own direction so the side coedge keeps its sense.
        const bool Forward = E.VertexStart == Vertices[I].Vertex;
        Lowered[I] = Forward ? NewLineEdge(Out, CutVertex[I], CutVertex[Next]) : NewLineEdge(Out, CutVertex[Next], CutVertex[I]);
        SideCoedge[I] = CoedgeOfEdgeOnFace(Body, Edges[I].Edge, Edges[I].Side);
        Out.Coedges[SideCoedge[I]].Edge = Lowered[I];
        Out.Edges[Lowered[I]].Coedges = { SideCoedge[I] };
        Out.Edges[Edges[I].Edge].Coedges = { Edges[I].Coedge };
        Mitre[I] = NewLineEdge(Out, CutVertex[I], Vertices[I].Vertex);
    }
    for (int I = 0; I < N; ++I)
    {
        const int Next = (I + 1) % N;
        const Vec3 Corners[4] = { Vertices[I].Inset, Vertices[Next].Inset, Vertices[Next].Cut, Vertices[I].Cut };
        int FaceChamfer = -1; PlaneBasis BasisChamfer;
        if (!PlanarFaceOver(Out, Corners, Edges[I].Normal, FaceChamfer, BasisChamfer)) return R::Reject(RefusalReason::DegenerateInput, "a chamfer face is degenerate");
        const int Ring[4] = { Edges[I].Edge, Mitre[Next], Lowered[I], Mitre[I] };
        if (!CloseQuadLoop(Out, FaceChamfer, Ring, BasisChamfer)) return R::Reject(RefusalReason::Unsupported, "a chamfer face does not close");
    }
    // Re-trim F on its plane (every rim coedge moved) and each side face along its lowered edge and two side edges.
    std::vector<int> AllRim; for (int I = 0; I < N; ++I) AllRim.push_back(Edges[I].Coedge);
    RetracePlanarFace(Body, Out, Face, BasisF, AllRim);
    for (int I = 0; I < N; ++I)
    {
        const int Next = (I + 1) % N;
        RetracePlanarFace(Body, Out, Edges[I].Side, Edges[I].BasisS,
                          { SideCoedge[I], CoedgeOfEdgeOnFace(Out, Vertices[I].SideEdge, Edges[I].Side), CoedgeOfEdgeOnFace(Out, Vertices[Next].SideEdge, Edges[I].Side) });
    }
    return Finish(std::move(Out), Before.Volume, Removed);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  DISPATCH
//------------------------------------------------------------------------------------------------------------------------

int ChamferSolver::RimFace(const BrepBody& Body, const std::vector<int>& Edges) noexcept
{
    std::vector<int> Wanted = Edges;
    std::sort(Wanted.begin(), Wanted.end());
    Wanted.erase(std::unique(Wanted.begin(), Wanted.end()), Wanted.end());
    int Found = -1;
    for (size_t F = 0; F < Body.Faces.size(); ++F)
    {
        if (Body.Faces[F].Loops.size() != 1) continue;
        std::vector<int> Have;
        for (int C : Body.Loops[Body.Faces[F].Loops[0]].Coedges) Have.push_back(Body.Coedges[C].Edge);
        std::sort(Have.begin(), Have.end());
        if (Have != Wanted) continue;
        if (Found >= 0) return -2;
        Found = static_cast<int>(F);
    }
    return Found;
}

Deliver<BrepBody> ChamferSolver::ChamferEdges(const BrepBody& Body, const std::vector<int>& Edges, double SetBack) noexcept
{
    if (Edges.empty()) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "no edge selected");
    if (Edges.size() == 1) return ChamferEdge(Body, Edges.front(), SetBack);
    for (int E : Edges)
        if (E < 0 || E >= static_cast<int>(Body.Edges.size())) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "edge index out of range");
    const int Face = RimFace(Body, Edges);
    if (Face == -2) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "the edge set is the rim of two faces; name the face instead");
    if (Face < 0) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "an edge set must be one edge or the complete rim of one planar face");
    return ChamferFaceRim(Body, Face, SetBack);
}

} // namespace Frontier
