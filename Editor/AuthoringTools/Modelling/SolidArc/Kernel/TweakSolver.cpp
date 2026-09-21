//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Kernel/TweakSolver.cpp — Translate one face, edge or vertex of a solid on fixed topology
//============================================================================================================================================

#include "Kernel/TweakSolver.h"
#include <algorithm>
#include <cmath>
#include <optional>

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

    struct NativeCylinderCap
    {
        Vec3   Base, Axis;
        double Radius = 0.0, Height = 0.0;
        bool   Upper = false;
    };

    [[nodiscard]] std::optional<NativeCylinderCap> NativeCylinderCapForFace(const BrepBody& Body, int Face) noexcept
    {
        if (Face < 0 || Face >= static_cast<int>(Body.Faces.size()) || Body.Faces.size() != 3 ||
            Body.Edges.size() != 3 || Body.Vertices.size() != 2 || Body.Loops.size() != 3 || Body.Coedges.size() != 6 ||
            Body.Faces[Face].Surface.Classification != SurfaceClassification::Plane) return std::nullopt;
        int CylinderFace = -1;
        for (int I = 0; I < static_cast<int>(Body.Faces.size()); ++I)
            if (Body.Faces[I].Surface.Classification == SurfaceClassification::Cylinder)
            {
                if (CylinderFace >= 0) return std::nullopt;
                CylinderFace = I;
            }
        if (CylinderFace < 0 || Body.Faces[Face].Loops.size() != 1) return std::nullopt;
        int CircularEdge = -1;
        for (int Coedge : Body.Loops[Body.Faces[Face].Loops[0]].Coedges)
        {
            const BrepEdge& Edge = Body.Edges[Body.Coedges[Coedge].Edge];
            if (Edge.Closed() && Edge.Curve.Classification == CurveClassification::Circle && Edge.Curve.Rational() && Edge.Coedges.size() == 2)
            {
                if (CircularEdge >= 0) return std::nullopt;
                CircularEdge = Body.Coedges[Coedge].Edge;
            }
        }
        if (CircularEdge < 0) return std::nullopt;
        const NurbsSurface& Cylinder = Body.Faces[CylinderFace].Surface;
        Vec3 Centres[2]; int Count = 0;
        for (int I = 0; I < static_cast<int>(Body.Faces.size()); ++I)
            if (Body.Faces[I].Surface.Classification == SurfaceClassification::Plane)
            {
                const NurbsSurface& Plane = Body.Faces[I].Surface;
                Centres[Count++] = Plane.Sample(0.5 * (Plane.DomainStartU() + Plane.DomainEndU()), 0.5 * (Plane.DomainStartV() + Plane.DomainEndV()));
            }
        if (Count != 2 || Cylinder.Axis.Length() <= Tol || Cylinder.RadiusMajor <= Tol) return std::nullopt;
        const Vec3 Axis = Cylinder.Axis.Normalised();
        double T[2] = { (Centres[0] - Cylinder.Origin).Dot(Axis), (Centres[1] - Cylinder.Origin).Dot(Axis) };
        const double Low = std::min(T[0], T[1]), High = std::max(T[0], T[1]);
        const double Height = High - Low;
        const double Reach = std::max({ 1.0, Cylinder.RadiusMajor, Height });
        if (Height <= ScalarCriteria::GeometricTolerance * Reach) return std::nullopt;
        const Vec3 SelectedCentre = Body.Faces[Face].Surface.Sample(0.5 * (Body.Faces[Face].Surface.DomainStartU() + Body.Faces[Face].Surface.DomainEndU()),
                                                                     0.5 * (Body.Faces[Face].Surface.DomainStartV() + Body.Faces[Face].Surface.DomainEndV()));
        const double SelectedT = (SelectedCentre - Cylinder.Origin).Dot(Axis);
        if (std::fabs(SelectedT - Low) > ScalarCriteria::GeometricTolerance * Reach && std::fabs(SelectedT - High) > ScalarCriteria::GeometricTolerance * Reach)
            return std::nullopt;
        return NativeCylinderCap{ Cylinder.Origin + Axis * Low, Axis, Cylinder.RadiusMajor, Height,
                                  std::fabs(SelectedT - High) <= ScalarCriteria::GeometricTolerance * Reach };
    }

    [[nodiscard]] Deliver<BrepBody> TranslateNativeCylinderCap(const BrepBody& Body, int Face, Vec3 Delta, bool& Recognized) noexcept
    {
        Recognized = false;
        const std::optional<NativeCylinderCap> Cap = NativeCylinderCapForFace(Body, Face);
        if (!Cap) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "not a native cylinder cap");
        Recognized = true;
        const double Along = Delta.Dot(Cap->Axis);
        const Vec3 Lateral = Delta - Cap->Axis * Along;
        if (Lateral.Length() > ScalarCriteria::GeometricTolerance * std::max({ 1.0, Cap->Radius, Cap->Height }))
            return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "native circular-cap tweak only supports translation along the cylinder axis");
        const double Height = Cap->Height + Along;
        if (Height <= ScalarCriteria::GeometricTolerance * std::max(1.0, Cap->Height))
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "native circular-cap tweak collapses the cylinder");
        const Vec3 Base = Cap->Upper ? Cap->Base : Cap->Base - Cap->Axis * Along;
        Deliver<BrepBody> Result = BrepBody::Cylinder(Base, Cap->Axis, Cap->Radius, Height);
        if (!Result || !Result.Payload.Validate().Solid()) return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "native circular-cap tweak did not rebuild a solid");
        return Result;
    }

    [[nodiscard]] int NativeCylinderCapFaceForEdge(const BrepBody& Body, int Edge) noexcept
    {
        if (Edge < 0 || Edge >= static_cast<int>(Body.Edges.size()) || !Body.Edges[Edge].Closed() ||
            Body.Edges[Edge].Curve.Classification != CurveClassification::Circle || Body.Edges[Edge].Coedges.size() != 2) return -1;
        for (int Coedge : Body.Edges[Edge].Coedges)
        {
            const int Face = Body.Coedges[Coedge].Face;
            if (Face >= 0 && NativeCylinderCapForFace(Body, Face)) return Face;
        }
        return -1;
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

    Deliver<BrepBody> TransformFaceTargets(const BrepBody& Body, int Face, const Mat4& Transform,
                                            bool AllowWarp, const char* Operation) noexcept
    {
        const BodyReport Before = Body.Validate();
        if (!Before.Solid()) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "transform tweak needs a closed solid");
        if (Face < 0 || Face >= static_cast<int>(Body.Faces.size()))
            return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "face index out of range");

        const std::vector<int> Moved = TweakSolver::FaceVertices(Body, Face);
        if (Moved.empty()) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "face has no vertices to transform");
        const double Reach = Scale(Body);
        std::vector<char> IsMoved(Body.Vertices.size(), 0);
        std::vector<Vec3> Targets(Body.Vertices.size());
        for (size_t I = 0; I < Body.Vertices.size(); ++I) Targets[I] = Body.Vertices[I].Point;
        for (int Vertex : Moved)
        {
            if (Vertex < 0 || Vertex >= static_cast<int>(Body.Vertices.size()))
                return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "face vertex index out of range");
            IsMoved[Vertex] = 1;
            Targets[Vertex] = Transform.TransformPoint(Body.Vertices[Vertex].Point);
        }

        std::vector<int> AffectedEdges;
        std::vector<AffectedFace> Affected;
        std::vector<char> FaceSeen(Body.Faces.size(), 0);
        for (size_t E = 0; E < Body.Edges.size(); ++E)
        {
            const BrepEdge& Edge = Body.Edges[E];
            if (Edge.VertexStart < 0 || Edge.VertexEnd < 0 ||
                (!IsMoved[Edge.VertexStart] && !IsMoved[Edge.VertexEnd])) continue;
            if (!Straight(Edge))
                return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "transform tweak touches a curved edge");
            AffectedEdges.push_back(static_cast<int>(E));
            for (int Coedge : Edge.Coedges)
            {
                int Adjacent = Body.Coedges[Coedge].Face;
                if (Adjacent < 0 || Adjacent >= static_cast<int>(Body.Faces.size()))
                    return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "coedge without a face");
                if (!FaceSeen[Adjacent]) { FaceSeen[Adjacent] = 1; Affected.push_back({ Adjacent, FaceRefit::Rigid, false }); }
            }
        }

        for (AffectedFace& Entry : Affected)
        {
            const BrepFace& SourceFace = Body.Faces[Entry.Face];
            const std::vector<int> Corners = TweakSolver::FaceVertices(Body, Entry.Face);
            bool AllMoved = true;
            for (int Vertex : Corners) if (!IsMoved[Vertex]) { AllMoved = false; break; }
            if (AllMoved) { Entry.Refit = FaceRefit::Rigid; continue; }

            if (NaturalQuad(Body, SourceFace))
            {
                Entry.Refit = FaceRefit::NaturalQuad;
                Vec3 CornersAfter[4]; int K = 0;
                for (int I = 0; I < 2; ++I)
                    for (int J = 0; J < 2; ++J)
                    {
                        int Vertex = CornerVertex(Body, SourceFace, Corners, I, J, Reach);
                        if (Vertex < 0) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "natural quad corner has no matching vertex");
                        CornersAfter[K++] = Targets[Vertex];
                    }
                Entry.Warps = !Coplanar(CornersAfter, 4, Reach);
                if (Entry.Warps && !AllowWarp)
                    return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "transform tweak would warp an adjacent planar face; allow warping to accept bilinear faces");
                continue;
            }
            if (PlanarTrimmed(SourceFace))
            {
                Entry.Refit = FaceRefit::PlanarTrimmed;
                const PlaneFrame Frame = FrameOf(SourceFace.Surface);
                for (int Vertex : Corners)
                    if (IsMoved[Vertex] && std::fabs(Frame.Height(Targets[Vertex])) > ScalarCriteria::GeometricTolerance * Reach)
                        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "transform tweak would move a trimmed face out of its plane");
                continue;
            }
            return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "transform tweak supports planar faces and natural quads only");
        }

        BrepBody Out = Body;
        for (int Vertex : Moved) Out.Vertices[Vertex].Point = Targets[Vertex];
        for (int EdgeIndex : AffectedEdges)
        {
            BrepEdge& Edge = Out.Edges[EdgeIndex];
            Deliver<NurbsCurve> Line = NurbsCurve::Line(Out.Vertices[Edge.VertexStart].Point, Out.Vertices[Edge.VertexEnd].Point);
            if (!Line) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "transform tweak collapses an edge");
            Edge.Curve = std::move(Line.Payload);
        }

        for (const AffectedFace& Entry : Affected)
        {
            BrepFace& Destination = Out.Faces[Entry.Face];
            if (Entry.Refit == FaceRefit::Rigid)
            {
                Destination.Surface = Destination.Surface.Transformed(Transform);
            }
            else if (Entry.Refit == FaceRefit::NaturalQuad)
            {
                const std::vector<int> Corners = TweakSolver::FaceVertices(Body, Entry.Face);
                for (int I = 0; I < 2; ++I)
                    for (int J = 0; J < 2; ++J)
                    {
                        int Vertex = CornerVertex(Body, Body.Faces[Entry.Face], Corners, I, J, Reach);
                        Destination.Surface.Pole(I, J) = Vec4(Targets[Vertex], 1.0);
                    }
                const PlaneFrame Frame = FrameOf(Destination.Surface);
                if (Frame.Du.Cross(Frame.Dv).Length() <= Tol * Reach * Reach)
                    return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "transform tweak collapses a face");
                Destination.Surface.Origin = Frame.Origin;
                Destination.Surface.Axis = Frame.Normal;
                Destination.Surface.Classification = Entry.Warps ? SurfaceClassification::Freeform : SurfaceClassification::Plane;
            }
            else
            {
                const PlaneFrame Frame = FrameOf(Destination.Surface);
                for (int Loop : Destination.Loops)
                    for (int Coedge : Out.Loops[Loop].Coedges)
                    {
                        const BrepEdge& Edge = Out.Edges[Out.Coedges[Coedge].Edge];
                        if (Edge.VertexStart < 0 || Edge.VertexEnd < 0) continue;
                        Out.Coedges[Coedge].Trace = { Frame.Uv(Out.CoedgeStart(Coedge)), Frame.Uv(Out.CoedgeEnd(Coedge)) };
                    }
            }
        }

        const BodyReport After = Out.Validate();
        if (!After.Solid()) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "transform tweak did not leave a closed solid");
        if (After.Volume <= ScalarCriteria::VolumeTolerance * std::max(1.0, Before.Volume))
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "transform tweak inverts or collapses the solid");
        (void)Operation;
        return Deliver<BrepBody>::Accept(std::move(Out));
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
    bool Recognized = false;
    Deliver<BrepBody> Curved = TranslateNativeCylinderCap(Body, Face, Delta, Recognized);
    if (Recognized) return Curved;
    return TranslateVertices(Body, FaceVertices(Body, Face), Delta, AllowWarp);
}

Deliver<BrepBody> TweakSolver::TranslateEdge(const BrepBody& Body, int Edge, Vec3 Delta, bool AllowWarp) noexcept
{
    if (Edge < 0 || Edge >= static_cast<int>(Body.Edges.size())) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "edge index out of range");
    const int CapFace = NativeCylinderCapFaceForEdge(Body, Edge);
    if (CapFace >= 0) return TranslateFace(Body, CapFace, Delta, AllowWarp);
    if (Body.Edges[Edge].Closed()) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "a closed curved edge is unsupported outside the native circular-cylinder cap route");
    return TranslateVertices(Body, EdgeVertices(Body, Edge), Delta, AllowWarp);
}

Deliver<BrepBody> TweakSolver::TranslateVertex(const BrepBody& Body, int Vertex, Vec3 Delta, bool AllowWarp) noexcept
{
    if (Vertex < 0 || Vertex >= static_cast<int>(Body.Vertices.size())) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "vertex index out of range");
    return TranslateVertices(Body, { Vertex }, Delta, AllowWarp);
}

Deliver<BrepBody> TweakSolver::RotateFace(const BrepBody& Body, int Face, Vec3 Axis, double Angle, bool AllowWarp) noexcept
{
    if (!std::isfinite(Angle) || std::fabs(Angle) <= ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "rotation angle is zero or invalid");
    if (Axis.Length() <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "rotation axis is zero");
    const std::vector<int> Vertices = FaceVertices(Body, Face);
    if (Vertices.empty()) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "face index out of range or has no vertices");
    Vec3 Pivot{};
    for (int Vertex : Vertices) Pivot = Pivot + Body.Vertices[Vertex].Point;
    Pivot = Pivot * (1.0 / static_cast<double>(Vertices.size()));
    const Mat4 Transform = Mat4::Translation(Pivot) * Mat4::Rotation(Axis.Normalised(), Angle) * Mat4::Translation(-Pivot);
    return TransformFaceTargets(Body, Face, Transform, AllowWarp, "rotation");
}

Deliver<BrepBody> TweakSolver::ScaleFace(const BrepBody& Body, int Face, double Factor, bool AllowWarp) noexcept
{
    if (!std::isfinite(Factor) || Factor <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "scale factor must be positive");
    const std::vector<int> Vertices = FaceVertices(Body, Face);
    if (Vertices.empty()) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "face index out of range or has no vertices");
    Vec3 Pivot{};
    for (int Vertex : Vertices) Pivot = Pivot + Body.Vertices[Vertex].Point;
    Pivot = Pivot * (1.0 / static_cast<double>(Vertices.size()));
    const Mat4 Transform = Mat4::Translation(Pivot) * Mat4::Scaling({ Factor, Factor, Factor }) * Mat4::Translation(-Pivot);
    return TransformFaceTargets(Body, Face, Transform, AllowWarp, "scale");
}

} // namespace Frontier
