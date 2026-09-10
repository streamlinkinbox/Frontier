//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Kernel/IntersectionSolver.cpp — marching SSI across face boundaries, (u,v) arrangements, booleans
//============================================================================================================================================
#include "IntersectionSolver.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <cstdio>

namespace Frontier
{

const char* Describe(BodyOperation Operation) noexcept
{
    switch (Operation) { case BodyOperation::Union: return "union"; case BodyOperation::Subtract: return "subtract"; default: return "intersect"; }
}

namespace
{
    //------------------------------------------------------------------------------------------------------------------------
    //                                                  FACE DOMAINS
    //------------------------------------------------------------------------------------------------------------------------
    // The trimming loops of one face in its own (u,v) plane, oriented so that material is always on the LEFT (reversed faces
    //    have their traces walked backwards). Each ring point remembers the coedge and the coedge-curve parameter it came from.
    struct DomainPoint
    {
        Vec2   P;                                                                       // [-] (u,v)
        int    Coedge = -1;                                                             // [-] coedge owning the segment that STARTS here; −1 = degenerate side (no edge)
        double T = 0.0;                                                                 // [-] coedge-curve parameter at P
        double TNext = 0.0;                                                             // [-] coedge-curve parameter at the segment's end
    };
    struct DomainRing
    {
        std::vector<DomainPoint> Points;                                                // [-] closed implicitly (last → first)
    };
    struct FaceDomain
    {
        std::vector<DomainRing> Rings;                                                  // [-] outer first
        std::vector<std::vector<int>> CoedgeOrder;                                      // [-] per ring: coedges in walk order (normalised)
        double ScaleU = 1, ScaleV = 1;                                                  // [-] domain extents
        bool Reversed = false;                                                          // [-]

        [[nodiscard]] bool Inside(Vec2 Q) const noexcept
        {
            if (Rings.empty()) return false;
            std::vector<Vec2> R;
            for (size_t K = 0; K < Rings.size(); ++K)
            {
                R.clear(); for (const DomainPoint& D : Rings[K].Points) R.push_back(D.P);
                bool In = InsideRing(R, Q);
                if (K == 0 && !In) return false;
                if (K > 0 && In) return false;
            }
            return true;
        }
    };

    FaceDomain BuildDomain(const BrepBody& B, int Face) noexcept
    {
        const BrepFace& F = B.Faces[Face];
        const NurbsSurface& S = F.Surface;
        FaceDomain D; D.Reversed = F.Reversed;
        D.ScaleU = S.DomainEndU() - S.DomainStartU(); D.ScaleV = S.DomainEndV() - S.DomainStartV();
        for (int L : F.Loops)
        {
            DomainRing R; std::vector<int> Order;
            std::vector<int> Cs = B.Loops[L].Coedges;
            if (F.Reversed) std::reverse(Cs.begin(), Cs.end());
            struct Run { std::vector<Vec2> Tr; std::vector<double> T; int C; };
            std::vector<Run> Runs;
            for (int C : Cs)
            {
                Run Rn; Rn.C = C; Rn.Tr = B.CoedgeTrace(C, &Rn.T);
                if (Rn.T.size() != Rn.Tr.size()) { Rn.T.assign(Rn.Tr.size(), 0.0); for (size_t I = 0; I < Rn.Tr.size(); ++I) Rn.T[I] = static_cast<double>(I); }
                if (F.Reversed) { std::reverse(Rn.Tr.begin(), Rn.Tr.end()); std::reverse(Rn.T.begin(), Rn.T.end()); }
                if (Rn.Tr.size() >= 2) Runs.push_back(std::move(Rn));
                Order.push_back(C);
            }
            const double Join = 1e-6 * (D.ScaleU + D.ScaleV);
            for (size_t K = 0; K < Runs.size(); ++K)
            {
                const Run& Rn = Runs[K]; const Run& Nx = Runs[(K + 1) % Runs.size()];
                for (size_t I = 0; I + 1 < Rn.Tr.size(); ++I) R.Points.push_back({ Rn.Tr[I], Rn.C, Rn.T[I], Rn.T[I + 1] });
                // the run's last point: dropped when the next run starts there, else kept as the start of a degenerate (edge-less) side
                if (Rn.Tr.back().Distance(Nx.Tr.front()) > Join) R.Points.push_back({ Rn.Tr.back(), -1, Rn.T.back(), Rn.T.back() });
            }
            if (R.Points.size() >= 3) { D.Rings.push_back(std::move(R)); D.CoedgeOrder.push_back(std::move(Order)); }
        }
        return D;
    }

    // (u,v) of a point of coedge C (given by edge parameter) on its face: interpolated along the coedge's trace and refined.
    Vec2 ParameterOnCoedge(const BrepBody& B, int C, double EdgeParameter, Vec3 Point) noexcept
    {
        const BrepCoedge& Ce = B.Coedges[C];
        const NurbsCurve& E = B.Edges[Ce.Edge].Curve;
        double Tc = Ce.Reversed ? E.DomainStart() + E.DomainEnd() - EdgeParameter : EdgeParameter;
        std::vector<double> T; std::vector<Vec2> Tr = B.CoedgeTrace(C, &T);
        Vec2 Seed = Tr.front();
        if (T.size() == Tr.size() && Tr.size() >= 2)
        {
            size_t I = 0; while (I + 2 < T.size() && T[I + 1] < Tc) ++I;
            double Span = T[I + 1] - T[I]; double F = Span > 0 ? ScalarCriteria::Clamp((Tc - T[I]) / Span, 0.0, 1.0) : 0.0;
            Seed = Tr[I] + (Tr[I + 1] - Tr[I]) * F;
        }
        const NurbsSurface& S = B.Faces[Ce.Face].Surface;
        if (B.Faces[Ce.Face].Natural && S.Sample(Seed.X, Seed.Y).Distance(Point) <= ScalarCriteria::MergeTolerance) return Seed;   // iso side: exact
        double U = Seed.X, V = Seed.Y;
        (void)SeededParameter(S, Point, U, V);
        Vec2 R{ U, V };
        double SpanU = S.DomainEndU() - S.DomainStartU(), SpanV = S.DomainEndV() - S.DomainStartV();
        if (std::fabs(R.X - Seed.X) > 0.25 * SpanU || std::fabs(R.Y - Seed.Y) > 0.25 * SpanV) return Seed;   // jumped a seam: keep the side we came from
        return R;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  NUMERICS
    //------------------------------------------------------------------------------------------------------------------------
    bool Solve3(const Vec3& R0, const Vec3& R1, const Vec3& R2, Vec3 Rhs, Vec3& X) noexcept
    {
        // rows R0..R2, solution from the reciprocal basis: X = (b0·(R1×R2) + b1·(R2×R0) + b2·(R0×R1)) / det
        double Det = R0.Dot(R1.Cross(R2));
        if (std::fabs(Det) < 1e-18) return false;
        X = (R1.Cross(R2) * Rhs.X + R2.Cross(R0) * Rhs.Y + R0.Cross(R1) * Rhs.Z) / Det;
        return true;
    }

    // Surface evaluation that continues linearly past the domain (Derivatives itself clamps): lets the march step over a
    //    face boundary so the crossing can be detected and then solved exactly on the edge.
    void Extended(const NurbsSurface& S, double U, double V, Vec3& P, Vec3& DU, Vec3& DV) noexcept
    {
        double Uc = ScalarCriteria::Clamp(U, S.DomainStartU(), S.DomainEndU()), Vc = ScalarCriteria::Clamp(V, S.DomainStartV(), S.DomainEndV());
        S.Derivatives(Uc, Vc, P, DU, DV);
        if (Uc != U) P += DU * (U - Uc);
        if (Vc != V) P += DV * (V - Vc);
    }
    Vec3 ExtendedPoint(const NurbsSurface& S, double U, double V) noexcept { Vec3 P, A, B; Extended(S, U, V, P, A, B); return P; }

    // Unbounded Newton projection: parameters may leave the domain a little (up to 10 %) so seams are crossed, not wrapped.
    double FreeParameter(const NurbsSurface& S, Vec3 Target, double& U, double& V) noexcept
    {
        const double U0 = S.DomainStartU(), U1 = S.DomainEndU(), V0 = S.DomainStartV(), V1 = S.DomainEndV();
        const double Mu = 0.1 * (U1 - U0), Mv = 0.1 * (V1 - V0);
        for (int It = 0; It < 30; ++It)
        {
            Vec3 P, DU, DV; Extended(S, U, V, P, DU, DV);
            Vec3 R = Target - P;
            if (R.Length() <= ScalarCriteria::KernelTolerance * 0.1) break;
            double A = DU.Dot(DU), Bb = DU.Dot(DV), D = DV.Dot(DV), Fu = DU.Dot(R), Fv = DV.Dot(R);
            double Det = A * D - Bb * Bb; if (std::fabs(Det) < 1e-300) break;
            double Du = (Fu * D - Fv * Bb) / Det, Dv = (A * Fv - Bb * Fu) / Det;
            double Nu = ScalarCriteria::Clamp(U + Du, U0 - Mu, U1 + Mu), Nv = ScalarCriteria::Clamp(V + Dv, V0 - Mv, V1 + Mv);
            bool Done = std::fabs(Nu - U) < 1e-14 && std::fabs(Nv - V) < 1e-14;
            U = Nu; V = Nv;
            if (Done) break;
        }
        return ExtendedPoint(S, U, V).Distance(Target);
    }

    struct Side
    {
        const BrepBody* Body = nullptr;
        int    Face = -1;
        double U = 0, V = 0;
    };

    // Pull a point onto both surfaces: intersect the two tangent planes with the plane through Predicted normal to Direction,
    //    re-project, repeat. Converges quadratically for transversal contact.
    bool RefineOnBoth(Side& A, Side& B, Vec3 Predicted, Vec3 Direction, Vec3& X, double* CrossOut = nullptr) noexcept
    {
        const NurbsSurface& Sa = A.Body->Faces[A.Face].Surface; const NurbsSurface& Sb = B.Body->Faces[B.Face].Surface;
        double Gap = ScalarCriteria::Infinity;
        X = Predicted;
        for (int It = 0; It < 25; ++It)
        {
            Vec3 Pa, Ua, Va, Pb, Ub, Vb;
            Extended(Sa, A.U, A.V, Pa, Ua, Va); Extended(Sb, B.U, B.V, Pb, Ub, Vb);
            Vec3 Na = Ua.Cross(Va), Nb = Ub.Cross(Vb);
            Na = Na.LengthSquared() > 1e-24 ? Na.Normalised() : Sa.Normal(A.U, A.V);
            Nb = Nb.LengthSquared() > 1e-24 ? Nb.Normalised() : Sb.Normal(B.U, B.V);
            Vec3 Cr = Na.Cross(Nb);
            if (CrossOut) *CrossOut = Cr.Length();
            if (Cr.Length() < 1e-6) return false;
            Vec3 D = Direction.LengthSquared() > 0 ? Direction : Cr.Normalised();
            Vec3 Sol;
            if (!Solve3(Na, Nb, D, Vec3{ Na.Dot(Pa), Nb.Dot(Pb), D.Dot(Predicted) }, Sol)) return false;
            double Ga = FreeParameter(Sa, Sol, A.U, A.V), Gb = FreeParameter(Sb, Sol, B.U, B.V);
            Gap = std::max(Ga, Gb);
            X = Sol;
            if (Gap <= ScalarCriteria::KernelTolerance) return true;
        }
        if (IntersectionSolver::Verbose && Gap > ScalarCriteria::MergeTolerance) std::fprintf(stderr, "[ssi] refine gap %.3e at uv (%.3f %.3f)/(%.3f %.3f)\n", Gap, A.U, A.V, B.U, B.V);
        return Gap <= ScalarCriteria::MergeTolerance;
    }

    // Edge curve ∩ surface: Newton on (t, u, v) from a seed.
    bool SolveEdgeSurface(const NurbsCurve& E, const NurbsSurface& S, double& T, double& U, double& V, Vec3& X) noexcept
    {
        for (int It = 0; It < 40; ++It)
        {
            Vec3 D[2]; E.Derivatives(T, 1, D);
            Vec3 P, Su, Sv; Extended(S, U, V, P, Su, Sv);
            Vec3 R = D[0] - P;
            if (R.Length() <= ScalarCriteria::KernelTolerance * 0.1) { X = D[0]; return true; }
            // J·δ = −R with J columns (E', −Su, −Sv): assemble the rows and solve.
            Vec3 C0 = D[1], C1 = Su * -1.0, C2 = Sv * -1.0;
            Vec3 Row0{ C0.X, C1.X, C2.X }, Row1{ C0.Y, C1.Y, C2.Y }, Row2{ C0.Z, C1.Z, C2.Z };
            Vec3 Delta;
            if (!Solve3(Row0, Row1, Row2, R * -1.0, Delta)) return false;
            T += Delta.X; U += Delta.Y; V += Delta.Z;
            T = ScalarCriteria::Clamp(T, E.DomainStart(), E.DomainEnd());
            if (Delta.Length() < 1e-15) break;
        }
        Vec3 Pe = E.Sample(T); X = Pe;
        return Pe.Distance(ExtendedPoint(S, U, V)) <= ScalarCriteria::MergeTolerance;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  TRIANGLE SEEDS
    //------------------------------------------------------------------------------------------------------------------------
    struct Triangles
    {
        std::vector<Vec3> P; std::vector<uint32_t> I; std::vector<int> Face; Box3 Bounds;   // flat list over all faces
    };
    Triangles TriangulateBody(const BrepBody& B, double Chord) noexcept
    {
        Triangles T;
        for (size_t F = 0; F < B.Faces.size(); ++F)
        {
            BrepBody::FaceTriangles Ft = B.TessellateFace(static_cast<int>(F), Chord);
            uint32_t Offset = static_cast<uint32_t>(T.P.size());
            for (Vec3 P : Ft.Positions) { T.P.push_back(P); T.Bounds.Include(P); }
            for (size_t K = 0; K + 2 < Ft.Triangles.size(); K += 3) { T.I.push_back(Offset + Ft.Triangles[K]); T.I.push_back(Offset + Ft.Triangles[K + 1]); T.I.push_back(Offset + Ft.Triangles[K + 2]); T.Face.push_back(static_cast<int>(F)); }
        }
        return T;
    }
    bool SegmentPlane(Vec3 A, Vec3 B, Vec3 N, double D, Vec3& X) noexcept
    {
        double Da = N.Dot(A) - D, Db = N.Dot(B) - D;
        if ((Da > 0) == (Db > 0)) return false;
        X = A + (B - A) * (Da / (Da - Db)); return true;
    }
    bool PointInTri(Vec3 P, Vec3 A, Vec3 B, Vec3 C, Vec3 N) noexcept
    {
        double S1 = (B - A).Cross(P - A).Dot(N), S2 = (C - B).Cross(P - B).Dot(N), S3 = (A - C).Cross(P - C).Dot(N);
        double Eps = -1e-9 * (std::fabs(S1) + std::fabs(S2) + std::fabs(S3) + 1e-300);
        return S1 >= Eps && S2 >= Eps && S3 >= Eps;
    }
    // Intersection segment of two triangles (transversal case only): the points where edges of one pierce the other.
    bool TriTri(const Vec3* A, const Vec3* B, Vec3& S0, Vec3& S1) noexcept
    {
        Vec3 Na = (A[1] - A[0]).Cross(A[2] - A[0]), Nb = (B[1] - B[0]).Cross(B[2] - B[0]);
        if (Na.LengthSquared() < 1e-30 || Nb.LengthSquared() < 1e-30) return false;
        double Da = Na.Dot(A[0]), Db = Nb.Dot(B[0]);
        Vec3 Pts[6]; int N = 0; Vec3 X;
        for (int E = 0; E < 3 && N < 6; ++E) if (SegmentPlane(A[E], A[(E + 1) % 3], Nb, Db, X) && PointInTri(X, B[0], B[1], B[2], Nb)) Pts[N++] = X;
        for (int E = 0; E < 3 && N < 6; ++E) if (SegmentPlane(B[E], B[(E + 1) % 3], Na, Da, X) && PointInTri(X, A[0], A[1], A[2], Na)) Pts[N++] = X;
        if (N < 2) return false;
        // the two farthest apart
        double Best = -1; for (int I = 0; I < N; ++I) for (int J = I + 1; J < N; ++J) { double D = Pts[I].Distance(Pts[J]); if (D > Best) { Best = D; S0 = Pts[I]; S1 = Pts[J]; } }
        return Best > 1e-9;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  TRACING
    //------------------------------------------------------------------------------------------------------------------------
    struct Exit                                                                         // a point where the intersection curve crosses an edge
    {
        int    Body = 0;                                                                // [-] 0 = A, 1 = B: whose edge
        int    Edge = -1;                                                               // [-]
        double T = 0.0;                                                                 // [-] edge parameter
        Vec3   Point;                                                                   // [m]
    };
    struct PieceEnd
    {
        int Exit = -1;                                                                  // [-] −1 = open seed end (only transiently)
        int Coedge[2] = { -1, -1 };                                                     // [-] per side: the coedge the curve crossed on that side's face (−1 = interior)
    };
    struct Piece                                                                        // run of the curve inside one face pair
    {
        int Face[2] = { -1, -1 };                                                       // [-]
        std::vector<Vec3> Points;                                                       // [m]
        std::vector<Vec2> Trace[2];                                                     // [-]
        PieceEnd Start, End;
        bool Closed = false;                                                            // [-]
        NurbsCurve Curve;                                                               // [-] interpolant
        double Deviation = 0.0;                                                         // [m]
    };

    struct Tracer
    {
        const BrepBody* Body[2];
        std::vector<std::vector<FaceDomain>> Domains;                                   // [body][face]
        std::vector<Exit>  Exits;
        std::vector<Piece> Pieces;
        double StepMax = 0.0, StepMin = 0.0;
        const char* Failure = nullptr;

        // Where the (u,v) segment P→Q leaves the face: ring segment index and fraction along P→Q.
        bool Crossing(const FaceDomain& D, Vec2 P, Vec2 Q, int& RingOut, int& SegOut, double& FractionOut, double& RingFractionOut) const noexcept
        {
            bool Found = false; FractionOut = ScalarCriteria::Infinity;
            for (size_t R = 0; R < D.Rings.size(); ++R)
            {
                const std::vector<DomainPoint>& Pts = D.Rings[R].Points;
                for (size_t I = 0; I < Pts.size(); ++I)
                {
                    Vec2 A = Pts[I].P, B = Pts[(I + 1) % Pts.size()].P;
                    Vec2 Rr = Q - P, Ss = B - A; double Den = Rr.Cross(Ss);
                    if (std::fabs(Den) < 1e-300) continue;
                    double Tt = (A - P).Cross(Ss) / Den, Uu = (A - P).Cross(Rr) / Den;
                    if (Tt < 1e-9 || Tt > 1.0 + 1e-9 || Uu < -1e-9 || Uu > 1.0 + 1e-9) continue;
                    if (Tt < FractionOut) { FractionOut = Tt; RingOut = static_cast<int>(R); SegOut = static_cast<int>(I); RingFractionOut = ScalarCriteria::Clamp(Uu, 0.0, 1.0); Found = true; }
                }
            }
            return Found;
        }

        bool Trace(Side A, Side B, Vec3 X0) noexcept
        {
            Side S[2] = { A, B };
            Vec3 X = X0;
            Piece Cur; Cur.Face[0] = S[0].Face; Cur.Face[1] = S[1].Face;
            auto Push = [&](Vec3 P) { Cur.Points.push_back(P); Cur.Trace[0].emplace_back(S[0].U, S[0].V); Cur.Trace[1].emplace_back(S[1].U, S[1].V); };
            Push(X);
            size_t FirstPiece = Pieces.size();
            double H = StepMax * 0.25;
            Vec3 Tangent; Vec3 T0;
            {
                Vec3 Na = Body[0]->Faces[S[0].Face].Surface.Normal(S[0].U, S[0].V), Nb = Body[1]->Faces[S[1].Face].Surface.Normal(S[1].U, S[1].V);
                Tangent = Na.Cross(Nb).Normalised(); T0 = Tangent;
                if (Tangent.LengthSquared() < 0.5) { Failure = "tangent or coincident faces (not transversal)"; return false; }
            }
            int Steps = 0;
            for (int Guard = 0; Guard < 20000; ++Guard)
            {
                // ---- predict, refine, control the turning angle
                Side N[2] = { S[0], S[1] }; Vec3 Xn; double Cross = 1;
                bool Ok = RefineOnBoth(N[0], N[1], X + Tangent * H, Tangent, Xn, &Cross);
                if (Ok && Cross < 1e-4) { Failure = "tangent or coincident faces (not transversal)"; return false; }
                Vec3 Tn;
                if (Ok)
                {
                    Vec3 Na = Body[0]->Faces[N[0].Face].Surface.Normal(N[0].U, N[0].V), Nb = Body[1]->Faces[N[1].Face].Surface.Normal(N[1].U, N[1].V);
                    Tn = Na.Cross(Nb).Normalised(); if (Tn.Dot(Tangent) < 0) Tn = Tn * -1.0;
                    double Turn = std::acos(ScalarCriteria::Clamp(Tn.Dot(Tangent), -1.0, 1.0));
                    double Chord = Xn.Distance(X);
                    if ((Turn > ScalarCriteria::Radians(8.0) || Chord > 1.6 * H) && H > StepMin) { H *= 0.5; continue; }
                    if (Chord < 0.25 * H && H > StepMin) { H *= 0.5; continue; }
                    if (Turn < ScalarCriteria::Radians(2.0)) H = std::min(H * 1.5, StepMax);
                }
                else
                {
                    if (H > StepMin) { H *= 0.5; continue; }
                    if (IntersectionSolver::Verbose) std::fprintf(stderr, "[ssi] lost at (%.4f %.4f %.4f) faces %d/%d uv (%.3f %.3f)/(%.3f %.3f) H %.2e steps %d\n", X.X, X.Y, X.Z, S[0].Face, S[1].Face, S[0].U, S[0].V, S[1].U, S[1].V, H, Steps);
                    Failure = "marching lost the intersection curve"; return false;
                }
                // ---- did we leave a face on either side?
                int ExitSide = -1, Ring = 0, Seg = 0; double Frac = ScalarCriteria::Infinity, RingFrac = 0;
                for (int K = 0; K < 2; ++K)
                {
                    const FaceDomain& D = Domains[K][S[K].Face];
                    Vec2 P{ S[K].U, S[K].V }, Q{ N[K].U, N[K].V };
                    if (D.Inside(Q)) continue;
                    int R, Sg; double F, Rf;
                    if (!Crossing(D, P, Q, R, Sg, F, Rf)) { if (H > StepMin) { F = -1; } else { Failure = "intersection runs into a surface singularity or seam corner (non-generic: nudge or rotate one body)"; return false; } }
                    if (F < 0) { ExitSide = -2; break; }
                    if (F < Frac) { Frac = F; ExitSide = K; Ring = R; Seg = Sg; RingFrac = Rf; }
                }
                if (ExitSide == -2) { H *= 0.5; continue; }
                if (ExitSide >= 0)
                {
                    const int K = ExitSide, O = 1 - K;
                    const BrepBody& Bk = *Body[K];
                    const FaceDomain& D = Domains[K][S[K].Face];
                    const std::vector<DomainPoint>& Pts = D.Rings[Ring].Points;
                    const DomainPoint& Da = Pts[Seg]; const DomainPoint& Db = Pts[(Seg + 1) % Pts.size()];
                    (void)Db;
                    if (Da.Coedge < 0) { Failure = "intersection runs through a surface singularity (sphere pole / cone apex): rotate the body so the pole clears the cut"; return false; }
                    int C = Da.Coedge;
                    const BrepCoedge& Ce = Bk.Coedges[C];
                    const NurbsCurve& E = Bk.Edges[Ce.Edge].Curve;
                    double Tc = Da.T + (Da.TNext - Da.T) * RingFrac;
                    // coedge-curve parameter → edge parameter (walk direction already normalised in the domain)
                    double Te = Ce.Reversed ? E.DomainStart() + E.DomainEnd() - Tc : Tc;
                    Side Other = S[O]; Other.U += (N[O].U - S[O].U) * Frac; Other.V += (N[O].V - S[O].V) * Frac;
                    Vec3 Xe;
                    double Tsolve = Te, Us = Other.U, Vs = Other.V;
                    if (!SolveEdgeSurface(E, Body[O]->Faces[Other.Face].Surface, Tsolve, Us, Vs, Xe))
                    {
                        if (H > StepMin) { H *= 0.5; continue; }
                        Failure = "edge–surface refinement did not converge at a face boundary"; return false;
                    }
                    Other.U = Us; Other.V = Vs;
                    // if the solved point sits on an edge interior we are fine; at a vertex the topology is non-generic
                    if (Tsolve <= E.DomainStart() + 1e-9 || Tsolve >= E.DomainEnd() - 1e-9) { Failure = "intersection passes exactly through a vertex (non-generic: nudge one body)"; return false; }
                    Exit Ex; Ex.Body = K; Ex.Edge = Ce.Edge; Ex.T = Tsolve; Ex.Point = Xe;
                    if (IntersectionSolver::Verbose) std::fprintf(stderr, "[ssi]   exit body %d edge %d t %.4f at (%.4f %.4f %.4f) after %d steps\n", K, Ce.Edge, Tsolve, Xe.X, Xe.Y, Xe.Z, Steps);
                    // reuse an existing identical exit (the curve may be re-traced from another seed only in failure cases; still, be safe)
                    int ExitId = -1;
                    for (size_t I = 0; I < Exits.size(); ++I) if (Exits[I].Body == K && Exits[I].Edge == Ce.Edge && std::fabs(Exits[I].T - Tsolve) < 1e-9) ExitId = static_cast<int>(I);
                    if (ExitId < 0) { Exits.push_back(Ex); ExitId = static_cast<int>(Exits.size() - 1); }
                    // close the current piece at the exit
                    Side Here[2]; Here[K] = S[K]; Here[O] = Other;
                    Vec2 UvHere = ParameterOnCoedge(Bk, C, Tsolve, Xe); Here[K].U = UvHere.X; Here[K].V = UvHere.Y;
                    S[0] = Here[0]; S[1] = Here[1]; X = Xe; Push(X);
                    Cur.End.Exit = ExitId; Cur.End.Coedge[K] = C; Cur.End.Coedge[O] = -1;
                    Pieces.push_back(Cur);
                    // continue on the neighbouring face across the edge (the other coedge of the same edge)
                    const BrepEdge& Ed = Bk.Edges[Ce.Edge];
                    if (Ed.Coedges.size() != 2) { Failure = "intersection crosses an open or non-manifold edge"; return false; }
                    int Cn = Ed.Coedges[0] == C ? Ed.Coedges[1] : Ed.Coedges[0];
                    Vec2 UvNext = ParameterOnCoedge(Bk, Cn, Tsolve, Xe);
                    S[K].Face = Bk.Coedges[Cn].Face; S[K].U = UvNext.X; S[K].V = UvNext.Y;
                    Cur = Piece(); Cur.Face[0] = S[0].Face; Cur.Face[1] = S[1].Face;
                    Cur.Start.Exit = ExitId; Cur.Start.Coedge[K] = Cn; Cur.Start.Coedge[O] = -1;
                    Push(X);
                    ++Steps;
                    H = std::max(H * 0.5, StepMin);
                    // new tangent: the face pair changed; its sign must lead into the new face's domain
                    {
                        Vec3 Na = Body[0]->Faces[S[0].Face].Surface.Normal(S[0].U, S[0].V), Nb = Body[1]->Faces[S[1].Face].Surface.Normal(S[1].U, S[1].V);
                        Vec3 Tt = Na.Cross(Nb).Normalised();
                        if (Tt.LengthSquared() < 0.5) { Failure = "tangent or coincident faces (not transversal)"; return false; }
                        const FaceDomain& Dn = Domains[K][S[K].Face];
                        Side Ahead[2] = { S[0], S[1] }; Vec3 Xp;
                        bool Fwd = RefineOnBoth(Ahead[0], Ahead[1], X + Tt * (H * 0.5), Tt, Xp) && Dn.Inside({ Ahead[K].U, Ahead[K].V });
                        if (!Fwd)
                        {
                            Side Ahead2[2] = { S[0], S[1] };
                            bool Bwd = RefineOnBoth(Ahead2[0], Ahead2[1], X - Tt * (H * 0.5), Tt * -1.0, Xp) && Dn.Inside({ Ahead2[K].U, Ahead2[K].V });
                            if (Bwd || Tt.Dot(Tangent) < 0) Tt = Tt * -1.0;
                        }
                        Tangent = Tt;
                    }
                    continue;
                }
                // ---- closure: the segment X→Xn passes the start point on the starting face pair
                if (Steps >= 3)
                {
                    Vec3 Dd = Xn - X; double L2 = Dd.LengthSquared();
                    double Tt = L2 > 0 ? ScalarCriteria::Clamp((X0 - X).Dot(Dd) / L2, 0.0, 1.0) : 0.0;
                    Vec3 Near = X + Dd * Tt;
                    bool SamePair = (Pieces.size() > FirstPiece ? (Pieces[FirstPiece].Face[0] == Cur.Face[0] && Pieces[FirstPiece].Face[1] == Cur.Face[1]) : true);
                    if (SamePair && Near.Distance(X0) < 0.75 * H && Tn.Dot(T0) > 0.5)
                    {
                        if (Pieces.size() == FirstPiece)
                        {
                            Cur.Closed = true; Cur.Points.push_back(X0); Cur.Trace[0].push_back(Cur.Trace[0].front()); Cur.Trace[1].push_back(Cur.Trace[1].front());
                            Pieces.push_back(Cur);
                        }
                        else
                        {
                            // splice: Cur (…→X0) + first piece (X0→…)
                            Piece& First = Pieces[FirstPiece];
                            Piece Joined = Cur;
                            for (size_t I = 1; I < First.Points.size(); ++I) { Joined.Points.push_back(First.Points[I]); Joined.Trace[0].push_back(First.Trace[0][I]); Joined.Trace[1].push_back(First.Trace[1][I]); }
                            Joined.End = First.End;
                            First = Joined;
                        }
                        return true;
                    }
                }
                // ---- accept the step
                S[0] = N[0]; S[1] = N[1]; X = Xn; Tangent = Tn; Push(X); ++Steps;
            }
            if (IntersectionSolver::Verbose) std::fprintf(stderr, "[ssi] did not close: steps %d pieces %zu at (%.4f %.4f %.4f)\n", Steps, Pieces.size() - FirstPiece, X.X, X.Y, X.Z);
            Failure = "marching did not close"; return false;
        }
    };

    // Cubic interpolant through the piece points, then densify where it strays from the surfaces.
    void FitPiece(Piece& P, const BrepBody& A, const BrepBody& B) noexcept
    {
        const NurbsSurface& Sa = A.Faces[P.Face[0]].Surface; const NurbsSurface& Sb = B.Faces[P.Face[1]].Surface;
        for (int Round = 0; Round < 4; ++Round)
        {
            std::vector<Vec3> Pts = P.Points;
            if (P.Closed) Pts.pop_back();
            int Degree = std::min(3, static_cast<int>(Pts.size()) - 1);
            if (Degree < 1) return;
            Deliver<NurbsCurve> C = P.Closed ? NurbsCurve::Interpolate(Pts, Degree, true) : NurbsCurve::Interpolate(Pts, Degree, false);
            if (!C) { Deliver<NurbsCurve> L = NurbsCurve::Polyline(P.Points, false); if (L) P.Curve = L.Payload; return; }
            P.Curve = C.Payload;
            // measure at chord midpoints (parameters are chord-length based: midpoints of consecutive knots)
            std::vector<double> Knots;                                                  // interpolation parameters ≈ Greville
            { double Total = 0; Knots.push_back(0); for (size_t I = 1; I < P.Points.size(); ++I) { Total += P.Points[I].Distance(P.Points[I - 1]); Knots.push_back(Total); } for (double& K : Knots) K = Total > 0 ? K / Total : 0; }
            double T0 = P.Curve.DomainStart(), T1 = P.Curve.DomainEnd();
            std::vector<Vec3> NewPts; std::vector<Vec2> NewA, NewB; bool Split = false; double Worst = 0;
            for (size_t I = 0; I + 1 < P.Points.size(); ++I)
            {
                NewPts.push_back(P.Points[I]); NewA.push_back(P.Trace[0][I]); NewB.push_back(P.Trace[1][I]);
                double Tm = T0 + (T1 - T0) * 0.5 * (Knots[I] + Knots[I + 1]);
                Vec3 M = P.Curve.Sample(Tm);
                Side Ra{ &A, P.Face[0], 0.5 * (P.Trace[0][I].X + P.Trace[0][I + 1].X), 0.5 * (P.Trace[0][I].Y + P.Trace[0][I + 1].Y) };
                Side Rb{ &B, P.Face[1], 0.5 * (P.Trace[1][I].X + P.Trace[1][I + 1].X), 0.5 * (P.Trace[1][I].Y + P.Trace[1][I + 1].Y) };
                double Ua = Ra.U, Va = Ra.V, Ub = Rb.U, Vb = Rb.V;
                double Da = FreeParameter(Sa, M, Ua, Va), Db = FreeParameter(Sb, M, Ub, Vb);
                double Dev = std::max(Da, Db); Worst = std::max(Worst, Dev);
                if (Dev > 2e-6 && Round < 3)
                {
                    Vec3 X; Vec3 Dir = (P.Points[I + 1] - P.Points[I]).Normalised();
                    Ra.U = Ua; Ra.V = Va; Rb.U = Ub; Rb.V = Vb;
                    if (RefineOnBoth(Ra, Rb, M, Dir, X)) { NewPts.push_back(X); NewA.emplace_back(Ra.U, Ra.V); NewB.emplace_back(Rb.U, Rb.V); Split = true; }
                }
            }
            NewPts.push_back(P.Points.back()); NewA.push_back(P.Trace[0].back()); NewB.push_back(P.Trace[1].back());
            P.Deviation = Worst;
            if (!Split) return;
            P.Points = NewPts; P.Trace[0] = NewA; P.Trace[1] = NewB;
        }
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  ARRANGEMENT AND SPLITTING
    //------------------------------------------------------------------------------------------------------------------------
    enum class SpanForm : uint8_t { Loop, Cut, None };
    struct Span
    {
        SpanForm Form = SpanForm::None;
        int    Coedge = -1;                                                             // Loop: body coedge
        double T0 = 0, T1 = 0;                                                          // Loop: coedge-curve parameters in walk direction
        int    Piece = -1;                                                              // Cut
        bool   Backward = false;                                                        // Cut: walked end → start
        std::vector<Vec2> Trace;                                                        // [-] (u,v) polyline in walk direction
    };
    struct Cell
    {
        std::vector<std::vector<Span>> Rings;                                           // outer first
        int  Inside = -1;                                                               // [-] −1 unknown, 0 outside the other body, 1 inside
        bool Whole = false;                                                             // [-] the untouched original face
    };

    struct Graph
    {
        struct DEdge { int From, To; SpanForm Form; int Coedge; double T0, T1; int Piece; int I0; bool Backward; };
        std::vector<Vec2> P;
        std::vector<bool> Exit;                                                         // [-] vertex sits on an intersection exit
        std::vector<DEdge> E;
        std::map<std::pair<long long, long long>, int> Keys;                          // vertex identity → index

        int Vertex(long long Ka, long long Kb, Vec2 Pt) noexcept
        {
            auto It = Keys.find({ Ka, Kb }); if (It != Keys.end()) return It->second;
            P.push_back(Pt); Exit.push_back(Ka < 0); int Id = static_cast<int>(P.size() - 1); Keys[{ Ka, Kb }] = Id; return Id;
        }
    };

    struct FaceSplit
    {
        std::vector<Cell> Cells;
    };

    FaceSplit SplitFace(const BrepBody& B, int Body, int Face, const FaceDomain& D, const std::vector<Exit>& Exits, const std::vector<Piece>& Pieces, bool Touched) noexcept
    {
        FaceSplit Out;
        Graph G;
        // ---- loop edges (one direction, material on the left)
        for (size_t R = 0; R < D.Rings.size(); ++R)
        {
            const std::vector<DomainPoint>& Pts = D.Rings[R].Points;
            // expand each segment with the exits that fall on it
            struct Knot { Vec2 P; int Coedge; double T, TNext; long long Ka, Kb; };
            std::vector<Knot> Nodes;
            for (size_t I = 0; I < Pts.size(); ++I)
            {
                const DomainPoint& A = Pts[I];
                Nodes.push_back({ A.P, A.Coedge, A.T, A.TNext, 1000000LL * static_cast<long long>(R) + static_cast<long long>(I), -1 });
                if (A.Coedge < 0) continue;
                const BrepCoedge& Ce = B.Coedges[A.Coedge]; const NurbsCurve& Ec = B.Edges[Ce.Edge].Curve;
                std::vector<Knot> Ins;
                for (size_t X = 0; X < Exits.size(); ++X)
                {
                    if (Exits[X].Body != Body || Exits[X].Edge != Ce.Edge) continue;
                    double Tc = Ce.Reversed ? Ec.DomainStart() + Ec.DomainEnd() - Exits[X].T : Exits[X].T;
                    double Lo = std::min(A.T, A.TNext), Hi = std::max(A.T, A.TNext);
                    if (Tc <= Lo || Tc >= Hi) continue;
                    Vec2 Uv = ParameterOnCoedge(B, A.Coedge, Exits[X].T, Exits[X].Point);
                    Ins.push_back({ Uv, A.Coedge, Tc, A.TNext, -static_cast<long long>(X) - 1, static_cast<long long>(A.Coedge) });
                }
                bool Ascending = A.TNext > A.T;
                std::sort(Ins.begin(), Ins.end(), [&](const Knot& L, const Knot& Rn) { return Ascending ? L.T < Rn.T : L.T > Rn.T; });
                for (Knot& N : Ins) { Nodes.back().TNext = N.T; Nodes.push_back(N); }
            }
            std::vector<int> Ids;
            for (Knot& N : Nodes) Ids.push_back(G.Vertex(N.Ka, N.Kb, N.P));
            for (size_t I = 0; I < Nodes.size(); ++I)
            {
                size_t J = (I + 1) % Nodes.size();
                if (Ids[I] == Ids[J]) continue;
                Graph::DEdge E{ Ids[I], Ids[J], Nodes[I].Coedge >= 0 ? SpanForm::Loop : SpanForm::None, Nodes[I].Coedge, Nodes[I].T, Nodes[I].TNext, -1, 0, false };
                G.E.push_back(E);
            }
        }
        // ---- cut edges (both directions)
        for (size_t Pi = 0; Pi < Pieces.size(); ++Pi)
        {
            const Piece& P = Pieces[Pi];
            int K = P.Face[Body] == Face ? Body : -1;
            if (K < 0 || P.Face[Body] != Face) continue;
            const std::vector<Vec2>& Tr = P.Trace[Body];
            std::vector<int> Ids;
            for (size_t I = 0; I < Tr.size(); ++I)
            {
                bool First = I == 0, Last = I + 1 == Tr.size();
                if (P.Closed && Last) { Ids.push_back(Ids.front()); break; }
                if (First && P.Start.Exit >= 0) Ids.push_back(G.Vertex(-static_cast<long long>(P.Start.Exit) - 1, P.Start.Coedge[Body], Tr[I]));
                else if (Last && P.End.Exit >= 0) Ids.push_back(G.Vertex(-static_cast<long long>(P.End.Exit) - 1, P.End.Coedge[Body], Tr[I]));
                else Ids.push_back(G.Vertex(2000000000LL + static_cast<long long>(Pi), static_cast<long long>(I), Tr[I]));
            }
            for (size_t I = 0; I + 1 < Ids.size(); ++I)
            {
                if (Ids[I] == Ids[I + 1]) continue;
                G.E.push_back({ Ids[I], Ids[I + 1], SpanForm::Cut, -1, 0, 0, static_cast<int>(Pi), static_cast<int>(I), false });
                G.E.push_back({ Ids[I + 1], Ids[I], SpanForm::Cut, -1, 0, 0, static_cast<int>(Pi), static_cast<int>(I), true });
            }
        }
                // ---- face walk (left face of every directed edge)
        std::vector<PlanarEdge> Pe; Pe.reserve(G.E.size());
        for (const Graph::DEdge& E : G.E) Pe.push_back({ E.From, E.To });
        std::vector<std::vector<std::vector<int>>> Walked = PlanarCells(G.P, Pe);
        struct Walk { std::vector<int> Edges; };
        // ---- spans from walks
        auto Spans = [&](const Walk& W) -> std::vector<Span>
        {
            std::vector<Span> Out2;
            auto Same = [&](const Graph::DEdge& A, const Graph::DEdge& Bb) { return A.To == Bb.From && !G.Exit[A.To] && A.Form == Bb.Form && A.Form != SpanForm::None && (A.Form == SpanForm::Loop ? A.Coedge == Bb.Coedge : (A.Piece == Bb.Piece && A.Backward == Bb.Backward)); };
            // rotate to a provenance change
            size_t N = W.Edges.size(), Start = 0;
            for (size_t I = 0; I < N; ++I) if (!Same(G.E[W.Edges[(I + N - 1) % N]], G.E[W.Edges[I]])) { Start = I; break; }
            for (size_t K = 0; K < N; ++K)
            {
                const Graph::DEdge& E = G.E[W.Edges[(Start + K) % N]];
                if (!Out2.empty() && Same(G.E[W.Edges[(Start + K + N - 1) % N]], E))
                {
                    Span& Sp = Out2.back(); Sp.T1 = E.T1; Sp.Trace.push_back(G.P[E.To]); continue;
                }
                Span Sp; Sp.Form = E.Form; Sp.Coedge = E.Coedge; Sp.T0 = E.T0; Sp.T1 = E.T1; Sp.Piece = E.Piece; Sp.Backward = E.Backward;
                Sp.Trace = { G.P[E.From], G.P[E.To] };
                Out2.push_back(std::move(Sp));
            }
            return Out2;
        };
        std::vector<Cell> Cells;
        for (const std::vector<std::vector<int>>& Wc : Walked)
        {
            Cell C;
            for (const std::vector<int>& Ring : Wc) { Walk W; W.Edges = Ring; C.Rings.push_back(Spans(W)); }
            Cells.push_back(std::move(C));
        }
        if (!Touched && Cells.size() == 1) Cells[0].Whole = true;
        Out.Cells = std::move(Cells);
        return Out;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  RAY PARITY
    //------------------------------------------------------------------------------------------------------------------------
    bool RayTriangle(Vec3 O, Vec3 Dir, Vec3 A, Vec3 B, Vec3 C, double& T) noexcept
    {
        Vec3 E1 = B - A, E2 = C - A, P = Dir.Cross(E2); double Det = E1.Dot(P);
        if (std::fabs(Det) < 1e-14) return false;
        double Inv = 1.0 / Det; Vec3 Tv = O - A; double U = Tv.Dot(P) * Inv; if (U < 0 || U > 1) return false;
        Vec3 Q = Tv.Cross(E1); double V = Dir.Dot(Q) * Inv; if (V < 0 || U + V > 1) return false;
        T = E2.Dot(Q) * Inv; return T > 1e-9;
    }
    bool EnclosesTriangles(const Triangles& T, Vec3 P) noexcept
    {
        if (!T.Bounds.Inflated(1e-6).Contains(P)) return false;
        const Vec3 Dirs[3] = { Vec3{ 0.3127, 0.5261, 0.7907 }.Normalised(), Vec3{ -0.6183, 0.2214, 0.7541 }.Normalised(), Vec3{ 0.1521, -0.8134, 0.5613 }.Normalised() };
        int Votes = 0;
        for (Vec3 D : Dirs)
        {
            int Hits = 0; double Tt;
            for (size_t I = 0; I + 2 < T.I.size(); I += 3) if (RayTriangle(P, D, T.P[T.I[I]], T.P[T.I[I + 1]], T.P[T.I[I + 2]], Tt)) ++Hits;
            Votes += Hits % 2;
        }
        return Votes >= 2;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  PUBLIC: INTERSECT
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    bool TraceAll(const BrepBody& A, const BrepBody& B, Tracer& Tr) noexcept
    {
        Tr.Body[0] = &A; Tr.Body[1] = &B;
        Tr.Domains.resize(2);
        for (size_t F = 0; F < A.Faces.size(); ++F) Tr.Domains[0].push_back(BuildDomain(A, static_cast<int>(F)));
        for (size_t F = 0; F < B.Faces.size(); ++F) Tr.Domains[1].push_back(BuildDomain(B, static_cast<int>(F)));
        double Diag = std::min(A.Bounds().Diagonal(), B.Bounds().Diagonal());
        Tr.StepMax = 0.04 * Diag; Tr.StepMin = Tr.StepMax / 512.0;
        if (!A.Bounds().Inflated(1e-9).Overlaps(B.Bounds())) return true;
        // seeds from triangle pairs
        Triangles Ta = TriangulateBody(A, 2e-3), Tb = TriangulateBody(B, 2e-3);
        struct Seed { Vec3 P; int Fa, Fb; };
        std::vector<Seed> Seeds;
        std::vector<Box3> Bb; for (size_t J = 0; J + 2 < Tb.I.size(); J += 3) { Box3 X; X.Include(Tb.P[Tb.I[J]]); X.Include(Tb.P[Tb.I[J + 1]]); X.Include(Tb.P[Tb.I[J + 2]]); Bb.push_back(X); }
        for (size_t I = 0; I + 2 < Ta.I.size(); I += 3)
        {
            Vec3 Pa[3] = { Ta.P[Ta.I[I]], Ta.P[Ta.I[I + 1]], Ta.P[Ta.I[I + 2]] };
            Box3 Xa; Xa.Include(Pa[0]); Xa.Include(Pa[1]); Xa.Include(Pa[2]); Xa = Xa.Inflated(1e-9);
            for (size_t J = 0; J + 2 < Tb.I.size(); J += 3)
            {
                if (!Xa.Overlaps(Bb[J / 3])) continue;
                Vec3 Pb[3] = { Tb.P[Tb.I[J]], Tb.P[Tb.I[J + 1]], Tb.P[Tb.I[J + 2]] };
                Vec3 S0, S1;
                if (TriTri(Pa, Pb, S0, S1)) Seeds.push_back({ (S0 + S1) * 0.5, Ta.Face[I / 3], Tb.Face[J / 3] });
            }
        }
        if (IntersectionSolver::Verbose) std::fprintf(stderr, "[ssi] %zu triangle seeds, step %.3e..%.3e\n", Seeds.size(), Tr.StepMin, Tr.StepMax);
        // march from unconsumed seeds
        auto Consumed = [&](const Seed& Sd)
        {
            double Tol = std::max(0.5 * Tr.StepMax, 4e-3 * Diag);
            for (const Piece& P : Tr.Pieces)
            {
                if (P.Face[0] != Sd.Fa || P.Face[1] != Sd.Fb) continue;
                for (size_t I = 0; I + 1 < P.Points.size(); ++I)
                {
                    Vec3 D = P.Points[I + 1] - P.Points[I]; double L2 = D.LengthSquared();
                    double T = L2 > 0 ? ScalarCriteria::Clamp((Sd.P - P.Points[I]).Dot(D) / L2, 0.0, 1.0) : 0.0;
                    if ((P.Points[I] + D * T).Distance(Sd.P) < Tol) return true;
                }
            }
            return false;
        };
        for (const Seed& Sd : Seeds)
        {
            if (Consumed(Sd)) continue;
            Side Sa{ &A, Sd.Fa, 0, 0 }, Sb{ &B, Sd.Fb, 0, 0 };
            A.Faces[Sd.Fa].Surface.ClosestParameter(Sd.P, Sa.U, Sa.V); B.Faces[Sd.Fb].Surface.ClosestParameter(Sd.P, Sb.U, Sb.V);
            Vec3 X; double Cross = 1;
            if (!RefineOnBoth(Sa, Sb, Sd.P, Vec3{}, X, &Cross)) { if (Cross < 1e-4) { Tr.Failure = "tangent or coincident faces (not transversal)"; return false; } if (IntersectionSolver::Verbose) std::fprintf(stderr, "[ssi] seed %d/%d did not refine (cross %.2e)\n", Sd.Fa, Sd.Fb, Cross); continue; }
            if (!Tr.Domains[0][Sd.Fa].Inside({ Sa.U, Sa.V }) || !Tr.Domains[1][Sd.Fb].Inside({ Sb.U, Sb.V })) { if (IntersectionSolver::Verbose) std::fprintf(stderr, "[ssi] seed %d/%d outside a domain uv (%.3f %.3f)/(%.3f %.3f)\n", Sd.Fa, Sd.Fb, Sa.U, Sa.V, Sb.U, Sb.V); continue; }
            if (IntersectionSolver::Verbose) std::fprintf(stderr, "[ssi] seed faces %d/%d at (%.4f %.4f %.4f)\n", Sd.Fa, Sd.Fb, X.X, X.Y, X.Z);
            if (!Tr.Trace(Sa, Sb, X)) return false;
            if (IntersectionSolver::Verbose) std::fprintf(stderr, "[ssi]   pieces now %zu exits %zu\n", Tr.Pieces.size(), Tr.Exits.size());
        }
        for (Piece& P : Tr.Pieces) FitPiece(P, A, B);
        return true;
    }
}

std::vector<IntersectionCurve> IntersectionSolver::Intersect(const BrepBody& A, const BrepBody& B) noexcept
{
    Tracer Tr;
    std::vector<IntersectionCurve> Out;
    if (!TraceAll(A, B, Tr)) return Out;
    for (const Piece& P : Tr.Pieces)
    {
        IntersectionCurve C; C.Curve = P.Curve; C.Points = P.Points; C.TraceA = P.Trace[0]; C.TraceB = P.Trace[1]; C.FaceA = P.Face[0]; C.FaceB = P.Face[1]; C.Closed = P.Closed; C.Deviation = P.Deviation;
        Out.push_back(std::move(C));
    }
    return Out;
}

bool IntersectionSolver::Encloses(const BrepBody& Body, Vec3 P) noexcept
{
    return EnclosesTriangles(TriangulateBody(Body, 2e-3), P);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  PUBLIC: COMBINE
//------------------------------------------------------------------------------------------------------------------------

Deliver<BrepBody> IntersectionSolver::Combine(const BrepBody& A, const BrepBody& B, BodyOperation Operation, BooleanReport* Report) noexcept
{
    if (A.Classification() != BodyClassification::Solid || B.Classification() != BodyClassification::Solid) return Deliver<BrepBody>::Reject(RefusalReason::OpenWire, "booleans need two closed solids");
    Tracer Tr;
    if (!TraceAll(A, B, Tr)) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, Tr.Failure ? Tr.Failure : "intersection failed");
    const BrepBody* Bodies[2] = { &A, &B };
    BooleanReport Rep; Rep.Curves = static_cast<int>(Tr.Pieces.size());

    // ---- split every face
    struct FacePieces { int Body, Face; std::vector<Cell> Cells; };
    std::vector<FacePieces> All;
    for (int K = 0; K < 2; ++K)
        for (size_t F = 0; F < Bodies[K]->Faces.size(); ++F)
        {
            bool Touched = false;
            for (const Piece& P : Tr.Pieces) if (P.Face[K] == static_cast<int>(F)) { Touched = true; break; }
            if (!Touched) for (int L : Bodies[K]->Faces[F].Loops) for (int C : Bodies[K]->Loops[L].Coedges) for (const Exit& X : Tr.Exits) if (X.Body == K && X.Edge == Bodies[K]->Coedges[C].Edge) Touched = true;
            FaceSplit Sp = SplitFace(*Bodies[K], K, static_cast<int>(F), Tr.Domains[K][F], Tr.Exits, Tr.Pieces, Touched);
            (K == 0 ? Rep.PiecesA : Rep.PiecesB) += static_cast<int>(Sp.Cells.size());
            if (IntersectionSolver::Verbose)
            {
                std::fprintf(stderr, "[bool] body %d face %zu touched %d cells %zu\n", K, F, Touched, Sp.Cells.size());
                for (const Cell& C : Sp.Cells) for (size_t R = 0; R < C.Rings.size(); ++R) { std::fprintf(stderr, "        ring %zu:", R); for (const Span& Sp2 : C.Rings[R]) std::fprintf(stderr, " %s%d[%zu]", Sp2.Form == SpanForm::Loop ? "L" : Sp2.Form == SpanForm::Cut ? "C" : "N", Sp2.Form == SpanForm::Loop ? Sp2.Coedge : Sp2.Piece, Sp2.Trace.size()); std::fprintf(stderr, "\n"); }
            }
            All.push_back({ K, static_cast<int>(F), std::move(Sp.Cells) });
        }

    // ---- classify: cut adjacency first (exact, local)
    Triangles Tri[2] = { TriangulateBody(A, 2e-3), TriangulateBody(B, 2e-3) };
    for (FacePieces& Fp : All)
    {
        const BrepBody& Bk = *Bodies[Fp.Body]; const BrepBody& Bo = *Bodies[1 - Fp.Body];
        const NurbsSurface& S = Bk.Faces[Fp.Face].Surface;
        for (Cell& C : Fp.Cells)
        {
            for (const std::vector<Span>& Ring : C.Rings)
            {
                for (const Span& Sp : Ring)
                {
                    if (Sp.Form != SpanForm::Cut || Sp.Trace.size() < 2) continue;
                    const Piece& P = Tr.Pieces[Sp.Piece];
                    size_t Mid = (Sp.Trace.size() - 1) / 2;
                    Vec2 Q0 = Sp.Trace[Mid], Q1 = Sp.Trace[Mid + 1];
                    Vec2 Dir = (Q1 - Q0); Vec2 Left = Dir.Perpendicular();
                    Vec2 M = (Q0 + Q1) * 0.5;
                    Vec3 Pp, Su, Sv; S.Derivatives(M.X, M.Y, Pp, Su, Sv);
                    Vec3 W = (Su * Left.X + Sv * Left.Y).Normalised();
                    // the other face's normal at the matching point of the piece
                    size_t Idx = Sp.Backward ? (P.Points.size() - 2 - Mid) : Mid;
                    Idx = std::min(Idx, P.Points.size() - 2);
                    Vec2 Ov = (P.Trace[1 - Fp.Body][Idx] + P.Trace[1 - Fp.Body][Idx + 1]) * 0.5;
                    Vec3 No = Bo.FaceNormal(P.Face[1 - Fp.Body], Ov.X, Ov.Y);
                    double Dot = W.Dot(No);
                    if (std::fabs(Dot) < 1e-6) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "coincident faces along the intersection (not transversal)");
                    C.Inside = Dot < 0 ? 1 : 0;
                    break;
                }
                if (C.Inside >= 0) break;
            }
        }
    }
    // ---- flood across shared original edge spans within each body
    auto EdgeKey = [&](int Body, const Span& Sp) -> long long
    {
        const BrepBody& Bk = *Bodies[Body];
        const BrepCoedge& Ce = Bk.Coedges[Sp.Coedge]; const NurbsCurve& E = Bk.Edges[Ce.Edge].Curve;
        double Mid = 0.5 * (Sp.T0 + Sp.T1); if (Ce.Reversed) Mid = E.DomainStart() + E.DomainEnd() - Mid;   // edge parameter of the span midpoint
        // interval index among the exits of this edge
        std::vector<double> Ts; for (const Exit& X : Tr.Exits) if (X.Body == Body && X.Edge == Ce.Edge) Ts.push_back(X.T);
        std::sort(Ts.begin(), Ts.end());
        int Interval = 0; for (double T : Ts) if (Mid > T) ++Interval;
        return (static_cast<long long>(Body) << 40) | (static_cast<long long>(Ce.Edge) << 16) | Interval;
    };
    for (int Round = 0; Round < 64; ++Round)
    {
        bool Changed = false;
        std::map<long long, int> Known;
        for (FacePieces& Fp : All) for (Cell& C : Fp.Cells) if (C.Inside >= 0) for (auto& Ring : C.Rings) for (const Span& Sp : Ring) if (Sp.Form == SpanForm::Loop) Known[EdgeKey(Fp.Body, Sp)] = C.Inside;
        for (FacePieces& Fp : All) for (Cell& C : Fp.Cells)
        {
            if (C.Inside >= 0) continue;
            for (auto& Ring : C.Rings) for (const Span& Sp : Ring)
            {
                if (Sp.Form != SpanForm::Loop) continue;
                auto It = Known.find(EdgeKey(Fp.Body, Sp));
                if (It != Known.end()) { C.Inside = It->second; Changed = true; break; }
            }
        }
        if (!Changed) break;
    }
    // ---- leftovers by parity
    for (FacePieces& Fp : All)
    {
        const NurbsSurface& S = Bodies[Fp.Body]->Faces[Fp.Face].Surface;
        for (Cell& C : Fp.Cells)
        {
            if (C.Inside >= 0) continue;
            const Span& Sp = C.Rings[0][0];
            Vec2 M = (Sp.Trace[0] + Sp.Trace[1]) * 0.5, Left = (Sp.Trace[1] - Sp.Trace[0]).Perpendicular().Normalised();
            double Eps = 1e-3 * (S.DomainEndU() - S.DomainStartU() + S.DomainEndV() - S.DomainStartV());
            Vec2 Q = M + Left * Eps;
            C.Inside = EnclosesTriangles(Tri[1 - Fp.Body], S.Sample(Q.X, Q.Y)) ? 1 : 0;
        }
    }
    for (FacePieces& Fp : All) for (Cell& C : Fp.Cells) if (C.Inside == 1) (Fp.Body == 0 ? Rep.InsideA : Rep.InsideB)++;
    if (IntersectionSolver::Verbose) for (FacePieces& Fp : All) for (size_t I = 0; I < Fp.Cells.size(); ++I) std::fprintf(stderr, "[bool] body %d face %d cell %zu inside %d\n", Fp.Body, Fp.Face, I, Fp.Cells[I].Inside);

    // ---- assemble
    BrepBody Out;
    std::map<long long, int> EdgeOf;                                                    // span key → result edge
    std::map<int, int> CutEdgeOf;                                                       // piece → result edge
    for (FacePieces& Fp : All)
    {
        const BrepBody& Bk = *Bodies[Fp.Body];
        bool Keep0 = Operation == BodyOperation::Intersect;                             // keep inside pieces?
        bool KeepInside = Keep0 || (Operation == BodyOperation::Subtract && Fp.Body == 1);
        for (Cell& C : Fp.Cells)
        {
            bool Keep = (C.Inside == 1) == KeepInside;
            if (!Keep) continue;
            (Fp.Body == 0 ? Rep.KeptA : Rep.KeptB)++;
            const BrepFace& Src = Bk.Faces[Fp.Face];
            int F = Out.AddFace(Src.Surface);
            Out.Faces[F].Natural = C.Whole && Src.Natural;
            for (size_t R = 0; R < C.Rings.size(); ++R)
            {
                int L = Out.AddLoop(F, R == 0);
                for (const Span& Sp : C.Rings[R])
                {
                    if (Sp.Form == SpanForm::None) continue;
                    int E = -1; bool Sense = false;
                    if (Sp.Form == SpanForm::Loop)
                    {
                        const BrepCoedge& Ce = Bk.Coedges[Sp.Coedge]; const NurbsCurve& Ec = Bk.Edges[Ce.Edge].Curve;
                        long long Key = EdgeKey(Fp.Body, Sp);
                        auto It = EdgeOf.find(Key);
                        if (It != EdgeOf.end()) E = It->second;
                        else
                        {
                            // curve of the span in edge parameters
                            double Ta = Ce.Reversed ? Ec.DomainStart() + Ec.DomainEnd() - Sp.T0 : Sp.T0;
                            double Tb = Ce.Reversed ? Ec.DomainStart() + Ec.DomainEnd() - Sp.T1 : Sp.T1;
                            if (Ta > Tb) std::swap(Ta, Tb);
                            bool Whole = Ta <= Ec.DomainStart() + 1e-9 && Tb >= Ec.DomainEnd() - 1e-9;
                            NurbsCurve Curve;
                            std::vector<double> Ts; for (const Exit& X : Tr.Exits) if (X.Body == Fp.Body && X.Edge == Ce.Edge) Ts.push_back(X.T);
                            if (Whole || Ts.empty()) Curve = Ec;
                            else Curve = Ec.Trimmed(Ta, Tb);                             // a closed edge's seam point is a real vertex: never wrap past it
                            E = Out.AddEdge(Curve, ScalarCriteria::MergeTolerance);
                            EdgeOf[Key] = E;
                        }
                        // sense: walk direction vs the result edge's own direction
                        Vec3 WalkStart = Bk.Faces[Fp.Face].Surface.Sample(Sp.Trace.front().X, Sp.Trace.front().Y);
                        Sense = Out.Edges[E].Curve.StartPoint().Distance(WalkStart) > Out.Edges[E].Curve.EndPoint().Distance(WalkStart);
                        if (Out.Edges[E].Curve.Closed())
                        {
                            Vec3 WalkDir = Bk.Faces[Fp.Face].Surface.Sample(Sp.Trace[1].X, Sp.Trace[1].Y) - WalkStart;
                            Sense = Out.Edges[E].Curve.Tangent(Out.Edges[E].Curve.DomainStart()).Dot(WalkDir) < 0;
                            if (Ce.Reversed != Src.Reversed) { /* closed edge: sense from geometry above */ }
                        }
                    }
                    else
                    {
                        auto It = CutEdgeOf.find(Sp.Piece);
                        if (It != CutEdgeOf.end()) E = It->second;
                        else { E = Out.AddEdge(Tr.Pieces[Sp.Piece].Curve, ScalarCriteria::MergeTolerance); CutEdgeOf[Sp.Piece] = E; }
                        Sense = Sp.Backward;
                        if (Out.Edges[E].Curve.Closed())
                        {
                            const NurbsSurface& S = Bk.Faces[Fp.Face].Surface;
                            Vec3 WalkDir = S.Sample(Sp.Trace[1].X, Sp.Trace[1].Y) - S.Sample(Sp.Trace[0].X, Sp.Trace[0].Y);
                            Sense = Out.Edges[E].Curve.Tangent(Out.Edges[E].Curve.DomainStart()).Dot(WalkDir) < 0;
                        }
                    }
                    int Co = Out.AddCoedge(E, Sense, F, L);
                    Out.Coedges[Co].Trace = Sp.Trace;
                }
            }
            if (Src.Reversed) Out.FlipFace(F);
            if (Operation == BodyOperation::Subtract && Fp.Body == 1) Out.FlipFace(F);
        }
    }
    if (Out.Faces.empty()) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "the result is empty");
    BodyReport R = Out.Validate();
    if (R.MisorientedEdges > 0) Out.Orient();
    if (Report) *Report = Rep;
    return Deliver<BrepBody>::Accept(std::move(Out));
}

} // namespace Frontier
