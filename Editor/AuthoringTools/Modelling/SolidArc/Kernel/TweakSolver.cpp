//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Kernel/TweakSolver.cpp — Translate one face, edge or vertex of a solid on fixed topology
//============================================================================================================================================

#include "Kernel/TweakSolver.h"
#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{
    constexpr double Tol = ScalarCriteria::KernelTolerance;                             // [m]

    // How a face touching the moved vertices is re-fitted.
    enum class FaceRefit : uint8_t { Rigid, NaturalQuad, PlanarTrimmed };

    struct AffectedFace
    {
        int       Face = -1;                                                            // [-]
        FaceRefit Refit = FaceRefit::Rigid;                                             // [-]
        bool      Warps = false;                                                        // [-] NaturalQuad leaving its plane
    };

    [[nodiscard]] bool Straight(const BrepEdge& E) noexcept
    {
        return !E.Closed() && E.Curve.Degree == 1 && E.Curve.Poles.size() == 2;
    }

    [[nodiscard]] bool FourPoleBilinear(const NurbsSurface& S) noexcept
    {
        if (S.DegreeU != 1 || S.DegreeV != 1 || S.CountU != 2 || S.CountV != 2 || S.Poles.size() != 4) return false;
        for (const Vec4& P : S.Poles) if (std::fabs(P.W - 1.0) > ScalarCriteria::ParametricEpsilon) return false;
        return true;
    }

    [[nodiscard]] bool NaturalQuad(const BrepBody& B, const BrepFace& F) noexcept
    {
        return F.Natural && F.Loops.size() == 1 && B.Loops[F.Loops[0]].Coedges.size() == 4 && FourPoleBilinear(F.Surface);
    }

    [[nodiscard]] bool PlanarTrimmed(const BrepFace& F) noexcept
    {
        return !F.Natural && F.Surface.Classification == SurfaceClassification::Plane && FourPoleBilinear(F.Surface);
    }

    [[nodiscard]] double Scale(const BrepBody& B) noexcept
    {
        return std::max(1.0, B.Bounds().Diagonal());
    }

    [[nodiscard]] bool Coplanar(const Vec3* P, int Count, double Reach) noexcept
    {
        // Plane through the first corner and the two most independent directions from it; every other corner within tolerance.
        Vec3 Normal{};
        double Best = 0.0;
        for (int I = 1; I < Count; ++I)
            for (int J = I + 1; J < Count; ++J)
            {
                Vec3 N = (P[I] - P[0]).Cross(P[J] - P[0]);
                if (N.Length() > Best) { Best = N.Length(); Normal = N; }
            }
        if (Best <= Tol * Reach * Reach) return true;                                   // degenerate: collinear corners are trivially coplanar
        Normal = Normal.Normalised();
        for (int I = 1; I < Count; ++I) if (std::fabs((P[I] - P[0]).Dot(Normal)) > ScalarCriteria::GeometricTolerance * Reach) return false;
        return true;
    }

    // Affine frame of a four-pole planar patch: P(u, v) = O + Du·(u − u0)/(u1 − u0) + Dv·(v − v0)/(v1 − v0). The inverse
    //    solves the 2×2 Gram system, so it is exact whatever the pole spacing and even for non-orthogonal axes.
    struct PlaneFrame
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

    [[nodiscard]] PlaneFrame FrameOf(const NurbsSurface& S) noexcept
    {
        PlaneFrame F;
        F.Origin = S.Pole(0, 0).Divide();
        F.Du = S.Pole(1, 0).Divide() - F.Origin;
        F.Dv = S.Pole(0, 1).Divide() - F.Origin;
        F.Normal = F.Du.Cross(F.Dv).Normalised();
        F.U0 = S.DomainStartU(); F.U1 = S.DomainEndU(); F.V0 = S.DomainStartV(); F.V1 = S.DomainEndV();
        return F;
    }

    // Corner vertex of a natural quad at pole (I, J), matched by coincidence with the sampled surface corner.
    [[nodiscard]] int CornerVertex(const BrepBody& B, const BrepFace& F, const std::vector<int>& Vertices, int I, int J, double Reach) noexcept
    {
        const NurbsSurface& S = F.Surface;
        Vec3 C = S.Sample(I == 0 ? S.DomainStartU() : S.DomainEndU(), J == 0 ? S.DomainStartV() : S.DomainEndV());
        int Best = -1; double BestDistance = ScalarCriteria::MergeTolerance * Reach;
        for (int V : Vertices)
        {
            double D = B.Vertices[V].Point.Distance(C);
            if (D < BestDistance) { BestDistance = D; Best = V; }
        }
        return Best;
    }

    [[nodiscard]] bool Analyse(const BrepBody& B, const std::vector<int>& Moved, Vec3 Delta,
                               std::vector<int>& Edges, std::vector<AffectedFace>& Faces, const char*& Why) noexcept
    {
        const double Reach = Scale(B);
        std::vector<char> IsMoved(B.Vertices.size(), 0);
        for (int V : Moved)
        {
            if (V < 0 || V >= static_cast<int>(B.Vertices.size())) { Why = "vertex index out of range"; return false; }
            if (IsMoved[V]) { Why = "a vertex is listed twice"; return false; }
            IsMoved[V] = 1;
        }
        if (Moved.empty()) { Why = "nothing selected to move"; return false; }
        if (Delta.Length() <= Tol) { Why = "translation is zero"; return false; }

        std::vector<char> FaceSeen(B.Faces.size(), 0);
        for (size_t E = 0; E < B.Edges.size(); ++E)
        {
            const BrepEdge& Edge = B.Edges[E];
            if (Edge.VertexStart < 0 || Edge.VertexEnd < 0) continue;
            if (!IsMoved[Edge.VertexStart] && !IsMoved[Edge.VertexEnd]) continue;
            if (!Straight(Edge)) { Why = "an edge touching the moved vertices is curved; tweak moves straight edges only"; return false; }
            Edges.push_back(static_cast<int>(E));
            for (int Coedge : Edge.Coedges)
            {
                int Face = B.Coedges[Coedge].Face;
                if (Face < 0 || Face >= static_cast<int>(B.Faces.size())) { Why = "coedge without a face"; return false; }
                if (!FaceSeen[Face]) { FaceSeen[Face] = 1; Faces.push_back({ Face, FaceRefit::Rigid, false }); }
            }
        }

        for (AffectedFace& A : Faces)
        {
            const BrepFace& F = B.Faces[A.Face];
            std::vector<int> Corners = TweakSolver::FaceVertices(B, A.Face);
            bool All = true;
            for (int V : Corners) if (!IsMoved[V]) { All = false; break; }
            if (All) { A.Refit = FaceRefit::Rigid; continue; }

            if (NaturalQuad(B, F))
            {
                A.Refit = FaceRefit::NaturalQuad;
                Vec3 P[4]; int K = 0;
                for (int I = 0; I < 2; ++I)
                    for (int J = 0; J < 2; ++J)
                    {
                        int V = CornerVertex(B, F, Corners, I, J, Reach);
                        if (V < 0) { Why = "a four-sided face's corner does not coincide with a vertex"; return false; }
                        P[K++] = B.Vertices[V].Point + (IsMoved[V] ? Delta : Vec3{});
                    }
                A.Warps = !Coplanar(P, 4, Reach);
                continue;
            }
            if (PlanarTrimmed(F))
            {
                A.Refit = FaceRefit::PlanarTrimmed;
                const PlaneFrame Frame = FrameOf(F.Surface);
                for (int V : Corners)
                    if (IsMoved[V] && std::fabs(Frame.Height(B.Vertices[V].Point + Delta)) > ScalarCriteria::GeometricTolerance * Reach)
                    {
                        Why = "a trimmed planar face would leave its plane; only four-sided natural faces may warp";
                        return false;
                    }
                continue;
            }
            Why = "a face touching the moved vertices is neither a four-sided natural face nor a trimmed plane";
            return false;
        }
        return true;
    }
}

std::vector<int> TweakSolver::FaceVertices(const BrepBody& Body, int Face) noexcept
{
    std::vector<int> Out;
    if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return Out;
    for (int Loop : Body.Faces[Face].Loops)
        for (int Coedge : Body.Loops[Loop].Coedges)
        {
            const BrepEdge& E = Body.Edges[Body.Coedges[Coedge].Edge];
            for (int V : { E.VertexStart, E.VertexEnd })
                if (V >= 0 && std::find(Out.begin(), Out.end(), V) == Out.end()) Out.push_back(V);
        }
    return Out;
}

std::vector<int> TweakSolver::EdgeVertices(const BrepBody& Body, int Edge) noexcept
{
    std::vector<int> Out;
    if (Edge < 0 || Edge >= static_cast<int>(Body.Edges.size())) return Out;
    const BrepEdge& E = Body.Edges[Edge];
    if (E.VertexStart >= 0) Out.push_back(E.VertexStart);
    if (E.VertexEnd >= 0 && E.VertexEnd != E.VertexStart) Out.push_back(E.VertexEnd);
    return Out;
}

std::vector<int> TweakSolver::WarpedFaces(const BrepBody& Body, const std::vector<int>& Vertices, Vec3 Delta) noexcept
{
    std::vector<int> Edges; std::vector<AffectedFace> Faces; const char* Why = nullptr;
    std::vector<int> Out;
    if (!Analyse(Body, Vertices, Delta, Edges, Faces, Why)) return Out;
    for (const AffectedFace& A : Faces) if (A.Warps) Out.push_back(A.Face);
    return Out;
}

Deliver<BrepBody> TweakSolver::TranslateVertices(const BrepBody& Body, const std::vector<int>& Vertices, Vec3 Delta, bool AllowWarp) noexcept
{
    const BodyReport Before = Body.Validate();
    if (!Before.Solid()) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "tweak needs a closed solid");

    std::vector<int> Edges; std::vector<AffectedFace> Faces; const char* Why = nullptr;
    if (!Analyse(Body, Vertices, Delta, Edges, Faces, Why)) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, Why);
    for (const AffectedFace& A : Faces)
        if (A.Warps && !AllowWarp)
            return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "translation would warp a planar face; allow warping to accept bilinear faces");

    const double Reach = Scale(Body);
    BrepBody Out = Body;
    std::vector<char> IsMoved(Out.Vertices.size(), 0);
    for (int V : Vertices) { IsMoved[V] = 1; Out.Vertices[V].Point = Out.Vertices[V].Point + Delta; }

    for (int E : Edges)
    {
        BrepEdge& Edge = Out.Edges[E];
        Deliver<NurbsCurve> Line = NurbsCurve::Line(Out.Vertices[Edge.VertexStart].Point, Out.Vertices[Edge.VertexEnd].Point);
        if (!Line) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "translation collapses an edge to a point");
        Edge.Curve = std::move(Line.Payload);
    }

    for (const AffectedFace& A : Faces)
    {
        BrepFace& F = Out.Faces[A.Face];
        switch (A.Refit)
        {
            case FaceRefit::Rigid:
                F.Surface = F.Surface.Transformed(Mat4::Translation(Delta));
                break;
            case FaceRefit::NaturalQuad:
            {
                const std::vector<int> Corners = FaceVertices(Body, A.Face);
                for (int I = 0; I < 2; ++I)
                    for (int J = 0; J < 2; ++J)
                    {
                        int V = CornerVertex(Body, Body.Faces[A.Face], Corners, I, J, Reach);
                        F.Surface.Pole(I, J) = Vec4(Out.Vertices[V].Point, 1.0);
                    }
                const PlaneFrame Frame = FrameOf(F.Surface);
                if (Frame.Du.Cross(Frame.Dv).Length() <= Tol * Reach * Reach)
                    return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "translation collapses a face");
                F.Surface.Origin = Frame.Origin;
                F.Surface.Axis = Frame.Normal;
                F.Surface.Classification = A.Warps ? SurfaceClassification::Freeform : SurfaceClassification::Plane;
                break;
            }
            case FaceRefit::PlanarTrimmed:
            {
                // The underlying plane patch only has to contain the trim loop; Extrude builds it a small margin larger
                //    than the profile, so a vertex pushed outward can leave it and the tessellator would clip the face.
                //    Grow the patch to the moved loop (same axes, same normal) and retrace every edge of the face.
                PlaneFrame Frame = FrameOf(F.Surface);
                double UMin = Frame.U0, UMax = Frame.U1, VMin = Frame.V0, VMax = Frame.V1;
                for (int V : FaceVertices(Out, A.Face))
                {
                    const Vec2 Uv = Frame.Uv(Out.Vertices[V].Point);
                    UMin = std::min(UMin, Uv.X); UMax = std::max(UMax, Uv.X); VMin = std::min(VMin, Uv.Y); VMax = std::max(VMax, Uv.Y);
                }
                const bool Grow = UMin < Frame.U0 || UMax > Frame.U1 || VMin < Frame.V0 || VMax > Frame.V1;
                if (Grow)
                {
                    for (int Loop : F.Loops)
                        for (int C : Out.Loops[Loop].Coedges)
                            if (!Straight(Out.Edges[Out.Coedges[C].Edge]))
                                return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "a trimmed plane must grow to hold the moved loop but has curved edges");
                    const double Margin = 0.1 * std::max(UMax - UMin, VMax - VMin);
                    const double SpanU = Frame.U1 - Frame.U0, SpanV = Frame.V1 - Frame.V0;
                    const Vec3 AxisU = Frame.Du * (1.0 / SpanU), AxisV = Frame.Dv * (1.0 / SpanV);   // unit per parameter step
                    const Vec3 Origin = Frame.Origin + AxisU * (UMin - Margin - Frame.U0) + AxisV * (VMin - Margin - Frame.V0);
                    Deliver<NurbsSurface> Grown = NurbsSurface::Plane(Origin, AxisU, AxisV, (UMax - UMin + 2.0 * Margin) * AxisU.Length(),
                                                                      (VMax - VMin + 2.0 * Margin) * AxisV.Length());
                    if (!Grown) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "the moved loop's plane is degenerate");
                    F.Surface = std::move(Grown.Payload);
                    Frame = FrameOf(F.Surface);
                }
                for (int Loop : F.Loops)
                    for (int C : Out.Loops[Loop].Coedges)
                    {
                        BrepCoedge& Coedge = Out.Coedges[C];
                        const BrepEdge& Edge = Out.Edges[Coedge.Edge];
                        if (!Grow && !IsMoved[Edge.VertexStart] && !IsMoved[Edge.VertexEnd]) continue;
                        Coedge.Trace = { Frame.Uv(Out.CoedgeStart(C)), Frame.Uv(Out.CoedgeEnd(C)) };
                    }
                break;
            }
        }
    }

    const BodyReport After = Out.Validate();
    if (!After.Closed || !After.Manifold || !After.Oriented)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "tweak left the body open; the selection is not supported");
    if (After.Volume <= ScalarCriteria::VolumeTolerance * std::max(1.0, Before.Volume))
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "translation inverts or collapses the solid");
    return Deliver<BrepBody>::Accept(std::move(Out));
}

Deliver<BrepBody> TweakSolver::TranslateFace(const BrepBody& Body, int Face, Vec3 Delta, bool AllowWarp) noexcept
{
    if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "face index out of range");
    return TranslateVertices(Body, FaceVertices(Body, Face), Delta, AllowWarp);
}

Deliver<BrepBody> TweakSolver::TranslateEdge(const BrepBody& Body, int Edge, Vec3 Delta, bool AllowWarp) noexcept
{
    if (Edge < 0 || Edge >= static_cast<int>(Body.Edges.size())) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "edge index out of range");
    if (Body.Edges[Edge].Closed()) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "a closed edge has no vertices to move");
    return TranslateVertices(Body, EdgeVertices(Body, Edge), Delta, AllowWarp);
}

Deliver<BrepBody> TweakSolver::TranslateVertex(const BrepBody& Body, int Vertex, Vec3 Delta, bool AllowWarp) noexcept
{
    if (Vertex < 0 || Vertex >= static_cast<int>(Body.Vertices.size())) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "vertex index out of range");
    return TranslateVertices(Body, { Vertex }, Delta, AllowWarp);
}

} // namespace Frontier
