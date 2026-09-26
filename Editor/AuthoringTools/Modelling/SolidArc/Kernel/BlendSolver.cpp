//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Kernel/BlendSolver.cpp — chamfer / fillet / face push as regularised set operations
//============================================================================================================================================
#include "BlendSolver.h"
#include "IntersectionSolver.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace Frontier
{
namespace
{
    constexpr double Tol = ScalarCriteria::MergeTolerance;

    // A rectangular solid given by an origin corner and three edge vectors, built from six planar faces and sewn.
    //    Used as the half-space cutter: it is finite, so it must be made comfortably larger than the target body.
    Deliver<BrepBody> OrientedBox(Vec3 Corner, Vec3 U, Vec3 V, Vec3 W, double LengthU, double LengthV, double LengthW) noexcept
    {
        std::vector<NurbsSurface> Faces;
        auto Put = [&](Deliver<NurbsSurface> S) { if (S) Faces.push_back(std::move(S.Payload)); };
        Put(NurbsSurface::Plane(Corner, U, V, LengthU, LengthV));
        Put(NurbsSurface::Plane(Corner + W * LengthW, U, V, LengthU, LengthV));
        Put(NurbsSurface::Plane(Corner, U, W, LengthU, LengthW));
        Put(NurbsSurface::Plane(Corner + V * LengthV, U, W, LengthU, LengthW));
        Put(NurbsSurface::Plane(Corner, V, W, LengthV, LengthW));
        Put(NurbsSurface::Plane(Corner + U * LengthU, V, W, LengthV, LengthW));
        if (Faces.size() != 6) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "cutter face is degenerate");
        return BrepBody::Sew(Faces);
    }

    // The tool that removes a corner. Two things make this harder than "cut with a half-space":
    //
    //    1. It must be LOCAL across the corner. An unbounded half-space also lops off every other part of the body
    //       lying beyond the set-back plane — on the spanner it cut the whole head off.
    //    2. Its END CAPS must not land on a neighbouring face or vertex. The boolean is exact, not tolerant: it
    //       refuses a non-transversal contact ("surface singularity or seam corner", "passes exactly through a
    //       vertex") rather than guessing. A cutter stopping exactly at the edge's endpoints is precisely that case,
    //       and one running well past them slices into whatever is around the corner.
    //
    //    There is no single margin that satisfies both for every edge — measured across the 30 edges of the pushed
    //    spanner, every fixed choice either refuses or over-cuts somewhere. So the cutter is parameterised and
    //    ChamferEdge tries a ladder of them, keeping the first that both closes and matches the closed-form volume.
    //    HalfWidth is measured across the corner, Margin along the edge (negative = stop short of the endpoints).
    Deliver<BrepBody> CornerCutter(const EdgeCornerFrame& F, double Offset, double HalfWidth, double Margin, double Outward) noexcept
    {
        Vec3 W = F.Bisector;                                                             // outward: the side cut away
        Vec3 U = F.Tangent.Cross(W);
        if (U.Length() <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "cutter frame is degenerate");
        U = U.Normalised();
        double Span = F.Length + 2.0 * Margin;
        if (Span <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "cutter is shorter than the edge allows");
        Vec3 Corner = F.Start + W * Offset - U * HalfWidth - F.Tangent * Margin;
        return OrientedBox(Corner, U, F.Tangent, W, 2.0 * HalfWidth, Span, Outward);
    }

    // Outward normal of a planar face, and whether it really is planar.
    bool PlanarNormal(const BrepBody& Body, int Face, Vec3& Out) noexcept
    {
        Vec3 N = Body.FaceNormal(Face, 0.5, 0.5);
        if (N.Length() <= Tol) return false;
        N = N.Normalised();
        const double Samples[4][2] = { { 0.25, 0.25 }, { 0.75, 0.25 }, { 0.25, 0.75 }, { 0.75, 0.75 } };
        for (const auto& S : Samples)
        {
            Vec3 M = Body.FaceNormal(Face, S[0], S[1]);
            if (M.Length() <= Tol) return false;
            if (M.Normalised().Dot(N) < 0.999999) return false;
        }
        Out = N;
        return true;
    }

    // A full circular cylinder cap has an exact chamfer construction: retain the cylindrical run and sew a conical
    // frustum at the selected cap. It avoids asking a planar prism cutter to approximate a curved edge.
    struct CylinderCap
    {
        Vec3   Base, Axis;
        double Radius = 0.0, Height = 0.0;
        bool   Upper = false;
    };

    std::optional<CylinderCap> NativeCylinderCap(const BrepBody& Body, int Edge) noexcept
    {
        if (Edge < 0 || Edge >= static_cast<int>(Body.Edges.size()) || !Body.Validate().Solid() ||
            Body.Vertices.size() != 2 || Body.Edges.size() != 3 || Body.Coedges.size() != 6 || Body.Loops.size() != 3 || Body.Faces.size() != 3) return std::nullopt;
        const BrepEdge& Boundary = Body.Edges[Edge];
        if (!Boundary.Closed() || Boundary.Curve.Classification != CurveClassification::Circle || Boundary.Curve.Degree != 2 || !Boundary.Curve.Rational() || Boundary.Coedges.size() != 2) return std::nullopt;
        int Rims = 0, Seams = 0;
        for (const BrepEdge& Candidate : Body.Edges)
        {
            if (Candidate.Closed() && Candidate.Curve.Classification == CurveClassification::Circle && Candidate.Curve.Degree == 2 && Candidate.Curve.Rational() && Candidate.Coedges.size() == 2) ++Rims;
            else if (!Candidate.Closed() && Candidate.Curve.Classification == CurveClassification::Line && Candidate.Curve.Degree == 1 && Candidate.Coedges.size() == 2) ++Seams;
            else return std::nullopt;
        }
        if (Rims != 2 || Seams != 1) return std::nullopt;
        int Side = -1, Caps[2] = { -1, -1 }, CapCount = 0;
        for (size_t F = 0; F < Body.Faces.size(); ++F)
        {
            const BrepFace& Face = Body.Faces[F];
            if (Face.Loops.size() != 1) return std::nullopt;
            if (Face.Surface.Classification == SurfaceClassification::Cylinder)
            {
                if (Side >= 0) return std::nullopt;
                Side = static_cast<int>(F);
            }
            else if (Face.Surface.Classification == SurfaceClassification::Plane)
            {
                if (CapCount == 2) return std::nullopt;
                Caps[CapCount++] = static_cast<int>(F);
            }
            else return std::nullopt;
        }
        if (Side < 0 || CapCount != 2) return std::nullopt;
        int SelectedCap = -1;
        for (int Coedge : Boundary.Coedges)
        {
            if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return std::nullopt;
            int Face = Body.Coedges[Coedge].Face;
            if (Face == Side) continue;
            if (Face == Caps[0] || Face == Caps[1]) { if (SelectedCap >= 0) return std::nullopt; SelectedCap = Face; }
            else return std::nullopt;
        }
        if (SelectedCap < 0) return std::nullopt;
        const NurbsSurface& Cylinder = Body.Faces[Side].Surface;
        Vec3 Axis = Cylinder.Axis.Normalised();
        if (Axis.Length() <= Tol || Cylinder.RadiusMajor <= Tol || std::fabs(Cylinder.RadiusMajor - Cylinder.RadiusMinor) > Tol) return std::nullopt;
        const double RadiusTolerance = ScalarCriteria::GeometricTolerance * std::max(1.0, Cylinder.RadiusMajor);
        for (const BrepEdge& Rim : Body.Edges)
            if (Rim.Closed())
            {
                Vec3 Point = Rim.Curve.Sample(0.5 * (Rim.Curve.DomainStart() + Rim.Curve.DomainEnd()));
                double Along = (Point - Cylinder.Origin).Dot(Axis);
                Vec3 Radial = Point - (Cylinder.Origin + Axis * Along);
                if (std::fabs(Radial.Length() - Cylinder.RadiusMajor) > RadiusTolerance) return std::nullopt;
                for (int I = 0; I < 5; ++I)
                {
                    Vec3 Sample = Rim.Curve.Sample(Rim.Curve.DomainStart() + (Rim.Curve.DomainEnd() - Rim.Curve.DomainStart()) * (static_cast<double>(I) / 4.0));
                    if (std::fabs((Sample - Cylinder.Origin).Dot(Axis) - Along) > RadiusTolerance) return std::nullopt;
                }
            }
        for (int Cap : Caps)
        {
            Vec3 Normal;
            if (!PlanarNormal(Body, Cap, Normal) || std::fabs(Normal.Dot(Axis)) < 1.0 - ScalarCriteria::GeometricTolerance) return std::nullopt;
        }
        const auto HeightAt = [&](int Face)
        {
            const NurbsSurface& Surface = Body.Faces[Face].Surface;
            Vec3 Point = Surface.Sample(0.5 * (Surface.DomainStartU() + Surface.DomainEndU()), 0.5 * (Surface.DomainStartV() + Surface.DomainEndV()));
            return (Point - Cylinder.Origin).Dot(Axis);
        };
        double T0 = HeightAt(Caps[0]), T1 = HeightAt(Caps[1]);
        double Low = std::min(T0, T1), High = std::max(T0, T1), Height = High - Low;
        const double Epsilon = ScalarCriteria::GeometricTolerance * std::max({ 1.0, Cylinder.RadiusMajor, Height });
        if (Height <= Epsilon) return std::nullopt;
        double Chosen = HeightAt(SelectedCap);
        bool Upper = std::fabs(Chosen - High) <= Epsilon;
        if (!Upper && std::fabs(Chosen - Low) > Epsilon) return std::nullopt;
        Vec3 SeamPoint = Boundary.Curve.Sample(0.5 * (Boundary.Curve.DomainStart() + Boundary.Curve.DomainEnd()));
        Vec3 Radial = SeamPoint - (Cylinder.Origin + Axis * Chosen);
        if (std::fabs(Radial.Dot(Axis)) > Epsilon || std::fabs(Radial.Length() - Cylinder.RadiusMajor) > Epsilon) return std::nullopt;
        return CylinderCap{ Cylinder.Origin + Axis * Low, Axis, Cylinder.RadiusMajor, Height, Upper };
    }

    Deliver<BrepBody> ChamferCylinderCap(const CylinderCap& Cap, double SetBack) noexcept
    {
        if (SetBack <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "set-back is zero or negative");
        if (SetBack >= Cap.Radius - Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "set-back reaches the cylinder axis");
        if (SetBack >= Cap.Height - Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "set-back consumes the entire cylinder height");
        std::vector<NurbsSurface> Faces;
        Deliver<NurbsSurface> Cylinder = Cap.Upper
            ? NurbsSurface::Cylinder(Cap.Base, Cap.Axis, Cap.Radius, Cap.Height - SetBack)
            : NurbsSurface::Cylinder(Cap.Base + Cap.Axis * SetBack, Cap.Axis, Cap.Radius, Cap.Height - SetBack);
        Deliver<NurbsSurface> Cone = Cap.Upper
            ? NurbsSurface::Cone(Cap.Base + Cap.Axis * (Cap.Height - SetBack), Cap.Axis, Cap.Radius, Cap.Radius - SetBack, SetBack)
            : NurbsSurface::Cone(Cap.Base, Cap.Axis, Cap.Radius - SetBack, Cap.Radius, SetBack);
        if (!Cylinder || !Cone) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "cylindrical chamfer support is degenerate");
        Faces.push_back(std::move(Cylinder.Payload)); Faces.push_back(std::move(Cone.Payload));
        Deliver<BrepBody> Result = BrepBody::Sew(Faces);
        if (!Result || !Result.Payload.Validate().Solid()) return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "cylindrical chamfer could not be sewn into a valid solid");
        return Result;
    }

    std::optional<CylinderCap> NativeCylinderCapFace(const BrepBody& Body, int Face) noexcept
    {
        if (Face < 0 || Face >= static_cast<int>(Body.Faces.size()) || Body.Faces[Face].Surface.Classification != SurfaceClassification::Plane) return std::nullopt;
        for (size_t E = 0; E < Body.Edges.size(); ++E)
        {
            const BrepEdge& Boundary = Body.Edges[E];
            if (!Boundary.Closed()) continue;
            bool OnFace = false;
            for (int Coedge : Boundary.Coedges)
                if (Coedge >= 0 && Coedge < static_cast<int>(Body.Coedges.size()) && Body.Coedges[Coedge].Face == Face) { OnFace = true; break; }
            if (OnFace)
                if (std::optional<CylinderCap> Cap = NativeCylinderCap(Body, static_cast<int>(E))) return Cap;
        }
        return std::nullopt;
    }

    std::optional<CylinderCap> NativeCylinderSideFace(const BrepBody& Body, int Face) noexcept
    {
        if (Face < 0 || Face >= static_cast<int>(Body.Faces.size()) || Body.Faces[Face].Surface.Classification != SurfaceClassification::Cylinder) return std::nullopt;
        for (size_t E = 0; E < Body.Edges.size(); ++E)
            if (Body.Edges[E].Closed())
                if (std::optional<CylinderCap> Cap = NativeCylinderCap(Body, static_cast<int>(E))) return Cap;
        return std::nullopt;
    }

    // Phase 31's first general smooth-support pair is the circular root where a cylindrical boss leaves a planar
    // annular shoulder. Unlike a three-face cylinder cap, the unsplit edge belongs to a five-face stepped solid and the
    // fillet ADDS its rolling-ball wedge. Phase 32a follows a complete circular chain across angular representation
    // seams; Phase 32b admits a finite semicircular chain on one diameter cap, and Phase 32d admits a general-angle
    // sector whose two radial caps share the rotation-axis edge. The classifier derives every patch and dimension from
    // topology/support geometry, never face-table order or a remembered primitive recipe.
    struct PlaneCylinderRoot
    {
        Vec3   Base, Axis, RadialStart;
        double OuterRadius = 0.0, ShoulderHeight = 0.0;
        double BossRadius = 0.0, BossHeight = 0.0;
        double SweepAngle = ScalarCriteria::TwoPi;
    };

    bool CircularFrame(const NurbsCurve& Curve, Vec3& Centre, Vec3& Normal, double& Radius) noexcept
    {
        if ((Curve.Classification != CurveClassification::Arc && Curve.Classification != CurveClassification::Circle) ||
            Curve.Degree != 2 || !Curve.Rational()) return false;
        const double T0 = Curve.DomainStart(), Span = Curve.DomainEnd() - T0;
        if (Span <= ScalarCriteria::ParametricEpsilon) return false;
        const double MiddleFraction = Curve.Closed() ? 0.25 : 0.5;
        const double EndFraction = Curve.Closed() ? 0.5 : 1.0;
        Vec3 P0 = Curve.Sample(T0), P1 = Curve.Sample(T0 + Span * MiddleFraction), P2 = Curve.Sample(T0 + Span * EndFraction);
        Vec3 U = P1 - P0, V = P2 - P0, Cross = U.Cross(V);
        const double Denominator = 2.0 * Cross.LengthSquared();
        if (Denominator <= ScalarCriteria::KernelTolerance) return false;
        Centre = P0 + (Cross.Cross(U) * V.LengthSquared() + V.Cross(Cross) * U.LengthSquared()) / Denominator;
        Radius = Centre.Distance(P0);
        if (Radius <= Tol) return false;
        Normal = Cross.Normalised();
        const double Epsilon = ScalarCriteria::GeometricTolerance * std::max(1.0, Radius);
        for (int I = 0; I <= 8; ++I)
        {
            Vec3 Radial = Curve.Sample(T0 + Span * (static_cast<double>(I) / 8.0)) - Centre;
            if (std::fabs(Radial.Length() - Radius) > Epsilon || std::fabs(Radial.Dot(Normal)) > Epsilon) return false;
        }
        return true;
    }

    bool CylinderEndCentres(const NurbsSurface& Cylinder, Vec3& Start, Vec3& End, double& Radius) noexcept
    {
        if (Cylinder.Classification != SurfaceClassification::Cylinder || Cylinder.RadiusMajor <= Tol ||
            std::fabs(Cylinder.RadiusMajor - Cylinder.RadiusMinor) > Tol) return false;
        const double U0 = Cylinder.DomainStartU(), U1 = Cylinder.DomainEndU();
        const double V0 = Cylinder.DomainStartV(), V1 = Cylinder.DomainEndV();
        const double MiddleU = 0.5 * (U0 + U1);
        Vec3 Axis = Cylinder.Axis.Normalised();
        if (Axis.Length() <= Tol) return false;
        Vec3 PointStart = Cylinder.Sample(MiddleU, V0), PointEnd = Cylinder.Sample(MiddleU, V1);
        Start = Cylinder.Origin + Axis * (PointStart - Cylinder.Origin).Dot(Axis);
        End = Cylinder.Origin + Axis * (PointEnd - Cylinder.Origin).Dot(Axis);
        Radius = Cylinder.RadiusMajor;
        const double Height = Start.Distance(End);
        const double Epsilon = ScalarCriteria::GeometricTolerance * std::max({ 1.0, Radius, Height });
        if (Height <= Epsilon || (End - Start).Normalised().Cross(Axis).Length() > ScalarCriteria::GeometricTolerance) return false;
        for (double V : { V0, V1 })
        {
            Vec3 Centre = V == V0 ? Start : End;
            for (int I = 0; I <= 8; ++I)
            {
                Vec3 Point = Cylinder.Sample(U0 + (U1 - U0) * (static_cast<double>(I) / 8.0), V);
                Vec3 Radial = Point - Centre;
                if (std::fabs(Radial.Length() - Radius) > Epsilon || std::fabs(Radial.Dot(Axis)) > Epsilon) return false;
            }
        }
        return true;
    }

    bool CircularChain(const BrepBody& Body, const std::vector<int>& Chain, Vec3 Centre,
                       Vec3 Axis, double Radius, bool Closed, double* Span = nullptr) noexcept
    {
        if (Chain.empty()) return false;
        if (Closed && Chain.size() == 1 && Body.Edges[Chain.front()].Closed()) return true;
        std::vector<int> Degrees(Body.Vertices.size(), 0);
        double Angle = 0.0;
        for (int Edge : Chain)
        {
            if (Edge < 0 || Edge >= static_cast<int>(Body.Edges.size())) return false;
            const BrepEdge& Candidate = Body.Edges[Edge];
            if (Candidate.Closed() || Candidate.VertexStart < 0 || Candidate.VertexEnd < 0) return false;
            Vec3 CandidateCentre, CandidateNormal; double CandidateRadius = 0.0;
            if (!CircularFrame(Candidate.Curve, CandidateCentre, CandidateNormal, CandidateRadius) ||
                CandidateCentre.Distance(Centre) > ScalarCriteria::GeometricTolerance * std::max(1.0, Radius) ||
                std::fabs(CandidateRadius - Radius) > ScalarCriteria::GeometricTolerance * std::max(1.0, Radius) ||
                std::fabs(CandidateNormal.Dot(Axis)) < 1.0 - ScalarCriteria::GeometricTolerance) return false;
            ++Degrees[Candidate.VertexStart]; ++Degrees[Candidate.VertexEnd];
            const double T0 = Candidate.Curve.DomainStart(), T1 = Candidate.Curve.DomainEnd(), TM = 0.5 * (T0 + T1);
            Vec3 R0 = (Candidate.Curve.Sample(T0) - Centre).Normalised();
            Vec3 RM = (Candidate.Curve.Sample(TM) - Centre).Normalised();
            Vec3 R1 = (Candidate.Curve.Sample(T1) - Centre).Normalised();
            Angle += std::acos(ScalarCriteria::Clamp(R0.Dot(RM), -1.0, 1.0));
            Angle += std::acos(ScalarCriteria::Clamp(RM.Dot(R1), -1.0, 1.0));
        }
        int Endpoints = 0;
        for (int Degree : Degrees)
        {
            if (Degree == 1) ++Endpoints;
            else if (Degree != 0 && Degree != 2) return false;
        }
        if (Span) *Span = Angle;
        if (Closed) return Endpoints == 0 && ScalarCriteria::WithinAngularTolerance(Angle, ScalarCriteria::TwoPi);
        return Endpoints == 2 && Angle > ScalarCriteria::AngularTolerance && Angle < ScalarCriteria::TwoPi - ScalarCriteria::AngularTolerance;
    }

    bool OpenChainSweep(const BrepBody& Body, const std::vector<int>& Chain, Vec3 Centre,
                        Vec3 Axis, Vec3& RadialStart, double& SweepAngle) noexcept
    {
        std::vector<int> Degrees(Body.Vertices.size(), 0);
        for (int Edge : Chain)
        {
            const BrepEdge& Candidate = Body.Edges[Edge];
            if (Candidate.VertexStart < 0 || Candidate.VertexEnd < 0) return false;
            ++Degrees[Candidate.VertexStart]; ++Degrees[Candidate.VertexEnd];
        }
        int StartVertex = -1;
        for (size_t Vertex = 0; Vertex < Degrees.size(); ++Vertex)
            if (Degrees[Vertex] == 1) { StartVertex = static_cast<int>(Vertex); break; }
        if (StartVertex < 0) return false;
        RadialStart = (Body.Vertices[StartVertex].Point - Centre).Normalised();
        if (RadialStart.Length() <= Tol || std::fabs(RadialStart.Dot(Axis)) > ScalarCriteria::GeometricTolerance) return false;

        std::vector<int> Remaining = Chain;
        int CurrentVertex = StartVertex;
        SweepAngle = 0.0;
        auto SignedTurn = [&](Vec3 A, Vec3 B) noexcept
        {
            A = A.Normalised(); B = B.Normalised();
            return std::atan2(Axis.Dot(A.Cross(B)), ScalarCriteria::Clamp(A.Dot(B), -1.0, 1.0));
        };
        while (!Remaining.empty())
        {
            auto It = std::find_if(Remaining.begin(), Remaining.end(), [&](int Edge)
            {
                const BrepEdge& Candidate = Body.Edges[Edge];
                return Candidate.VertexStart == CurrentVertex || Candidate.VertexEnd == CurrentVertex;
            });
            if (It == Remaining.end()) return false;
            const BrepEdge& Candidate = Body.Edges[*It];
            const bool Reverse = Candidate.VertexEnd == CurrentVertex;
            const NurbsCurve& Curve = Candidate.Curve;
            const double T0 = Curve.DomainStart(), T1 = Curve.DomainEnd(), TM = 0.5 * (T0 + T1);
            Vec3 Start = Curve.Sample(Reverse ? T1 : T0) - Centre;
            Vec3 Middle = Curve.Sample(TM) - Centre;
            Vec3 End = Curve.Sample(Reverse ? T0 : T1) - Centre;
            SweepAngle += SignedTurn(Start, Middle) + SignedTurn(Middle, End);
            CurrentVertex = Reverse ? Candidate.VertexStart : Candidate.VertexEnd;
            Remaining.erase(It);
        }
        if (ScalarCriteria::WithinAngularTolerance(std::fabs(SweepAngle), ScalarCriteria::Pi))
            SweepAngle = std::copysign(ScalarCriteria::Pi, SweepAngle);
        return Degrees[CurrentVertex] == 1 && CurrentVertex != StartVertex &&
            std::fabs(SweepAngle) > ScalarCriteria::SweepTolerance && std::fabs(SweepAngle) < ScalarCriteria::TwoPi - ScalarCriteria::SweepTolerance;
    }

    bool CapRadialSector(BrepBody& Body, Vec3 AxisStart, Vec3 AxisEnd, Vec3 RadialStart,
                         double SweepAngle, double RadialExtent) noexcept
    {
        Vec3 Axis = (AxisEnd - AxisStart).Normalised();
        RadialStart = (RadialStart - Axis * RadialStart.Dot(Axis)).Normalised();
        Vec3 RadialEnd = RadialStart * std::cos(SweepAngle) + Axis.Cross(RadialStart) * std::sin(SweepAngle);
        if (Axis.Length() <= Tol || RadialStart.Length() <= Tol || RadialEnd.Length() <= Tol || RadialExtent <= Tol)
            return false;

        std::vector<int> StartEdges, EndEdges;
        for (size_t Edge = 0; Edge < Body.Edges.size(); ++Edge)
        {
            if (Body.Edges[Edge].Coedges.size() != 1) continue;
            const NurbsCurve& Curve = Body.Edges[Edge].Curve;
            Vec3 Middle = Curve.Sample(0.5 * (Curve.DomainStart() + Curve.DomainEnd()));
            Vec3 Radial = Middle - (AxisStart + Axis * (Middle - AxisStart).Dot(Axis));
            if (Radial.Length() <= Tol) return false;
            Radial = Radial.Normalised();
            double AtStart = std::fabs(Radial.Dot(RadialStart));
            double AtEnd = std::fabs(Radial.Dot(RadialEnd));
            (AtStart >= AtEnd ? StartEdges : EndEdges).push_back(static_cast<int>(Edge));
        }
        if (StartEdges.empty() || EndEdges.empty()) return false;

        Deliver<NurbsCurve> AxisCurve = NurbsCurve::Line(AxisStart, AxisEnd);
        if (!AxisCurve) return false;
        int AxisEdge = Body.AddEdge(AxisCurve.Payload, ScalarCriteria::MergeTolerance);
        int StartVertex = Body.AddVertex(AxisStart, ScalarCriteria::MergeTolerance);
        int EndVertex = Body.AddVertex(AxisEnd, ScalarCriteria::MergeTolerance);
        auto AddCap = [&](std::vector<int> Edges, Vec3 Radial) noexcept
        {
            std::vector<std::pair<int, bool>> Path;
            int Current = StartVertex;
            while (Current != EndVertex)
            {
                auto It = std::find_if(Edges.begin(), Edges.end(), [&](int Edge)
                {
                    return Body.Edges[Edge].VertexStart == Current || Body.Edges[Edge].VertexEnd == Current;
                });
                if (It == Edges.end()) return false;
                int Edge = *It;
                bool Reversed = Body.Edges[Edge].VertexEnd == Current;
                Current = Reversed ? Body.Edges[Edge].VertexStart : Body.Edges[Edge].VertexEnd;
                Path.push_back({ Edge, Reversed });
                Edges.erase(It);
            }
            if (!Edges.empty()) return false;
            const double Pad = 0.01 * std::max(RadialExtent, AxisStart.Distance(AxisEnd)) + Tol;
            Deliver<NurbsSurface> Plane = NurbsSurface::Plane(AxisStart - Radial * Pad - Axis * Pad,
                Radial, Axis, RadialExtent + 2.0 * Pad, AxisStart.Distance(AxisEnd) + 2.0 * Pad);
            if (!Plane) return false;
            int Face = Body.AddFace(std::move(Plane.Payload));
            Body.Faces[Face].Natural = false;
            int Loop = Body.AddLoop(Face, true);
            for (const auto& [Edge, Reversed] : Path) Body.AddCoedge(Edge, Reversed, Face, Loop);
            bool ReverseAxis = Body.Edges[AxisEdge].VertexStart == EndVertex;
            Body.AddCoedge(AxisEdge, ReverseAxis, Face, Loop);
            return true;
        };
        if (!AddCap(StartEdges, RadialStart) || !AddCap(EndEdges, RadialEnd)) return false;
        Body.Orient();
        return Body.Validate().Solid();
    }

    struct OrthogonalBoxCorner
    {
        Vec3 Corner, X, Y, Z;
        double LX = 0.0, LY = 0.0, LZ = 0.0;
    };

    std::optional<OrthogonalBoxCorner> ClassifyOrthogonalBoxCorner(const BrepBody& Body,
                                                                   const std::vector<int>& Edges) noexcept
    {
        if (Edges.size() != 3 || !Body.Validate().Solid() || Body.Vertices.size() != 8 || Body.Edges.size() != 12 ||
            Body.Coedges.size() != 24 || Body.Loops.size() != 6 || Body.Faces.size() != 6) return std::nullopt;
        for (const BrepFace& Face : Body.Faces)
            if (Face.Surface.Classification != SurfaceClassification::Plane || Face.Loops.size() != 1) return std::nullopt;
        int Common = -1;
        for (size_t Vertex = 0; Vertex < Body.Vertices.size(); ++Vertex)
        {
            int Incidence = 0;
            for (int Edge : Edges)
            {
                if (Edge < 0 || Edge >= static_cast<int>(Body.Edges.size())) return std::nullopt;
                const BrepEdge& Candidate = Body.Edges[Edge];
                if (Candidate.Curve.Classification != CurveClassification::Line || Candidate.Coedges.size() != 2 ||
                    Candidate.VertexStart < 0 || Candidate.VertexEnd < 0) return std::nullopt;
                if (Candidate.VertexStart == static_cast<int>(Vertex) || Candidate.VertexEnd == static_cast<int>(Vertex)) ++Incidence;
            }
            if (Incidence == 3) { if (Common >= 0) return std::nullopt; Common = static_cast<int>(Vertex); }
        }
        if (Common < 0) return std::nullopt;
        OrthogonalBoxCorner Result; Result.Corner = Body.Vertices[Common].Point;
        Vec3 Direction[3]; double Length[3]{};
        for (size_t I = 0; I < Edges.size(); ++I)
        {
            const BrepEdge& Edge = Body.Edges[Edges[I]];
            int Other = Edge.VertexStart == Common ? Edge.VertexEnd : Edge.VertexStart;
            Vec3 Span = Body.Vertices[Other].Point - Result.Corner;
            Length[I] = Span.Length(); if (Length[I] <= Tol) return std::nullopt;
            Direction[I] = Span / Length[I];
        }
        if (std::fabs(Direction[0].Dot(Direction[1])) > ScalarCriteria::GeometricTolerance || std::fabs(Direction[0].Dot(Direction[2])) > ScalarCriteria::GeometricTolerance ||
            std::fabs(Direction[1].Dot(Direction[2])) > ScalarCriteria::GeometricTolerance) return std::nullopt;
        if (Direction[0].Cross(Direction[1]).Dot(Direction[2]) < 0.0)
        { std::swap(Direction[1], Direction[2]); std::swap(Length[1], Length[2]); }
        Result.X = Direction[0]; Result.Y = Direction[1]; Result.Z = Direction[2];
        Result.LX = Length[0]; Result.LY = Length[1]; Result.LZ = Length[2];
        const double Scale = std::max({ 1.0, Result.LX, Result.LY, Result.LZ });
        for (const BrepVertex& Vertex : Body.Vertices)
        {
            Vec3 Offset = Vertex.Point - Result.Corner;
            double C[3]{ Offset.Dot(Result.X), Offset.Dot(Result.Y), Offset.Dot(Result.Z) };
            const double L[3]{ Result.LX, Result.LY, Result.LZ };
            for (int I = 0; I < 3; ++I)
                if (std::min(std::fabs(C[I]), std::fabs(C[I] - L[I])) > ScalarCriteria::GeometricTolerance * Scale) return std::nullopt;
        }
        const double Volume = Result.LX * Result.LY * Result.LZ;
        if (!ScalarCriteria::WithinScaledTolerance(Body.Validate().Volume, Volume, ScalarCriteria::GeometricTolerance)) return std::nullopt;
        return Result;
    }

    std::optional<OrthogonalBoxCorner> PrismFrameFromRails(const BrepBody& Body,const std::vector<int>& Edges) noexcept
    {
        if(Edges.size()!=4)return std::nullopt;
        Vec3 A;double LA=0;std::vector<Vec3> Low;
        for(int Index:Edges){if(Index<0||Index>=static_cast<int>(Body.Edges.size()))return std::nullopt;const BrepEdge&E=Body.Edges[Index];
            if(E.Curve.Classification!=CurveClassification::Line||E.VertexStart<0||E.VertexEnd<0)return std::nullopt;
            Vec3 P0=Body.Vertices[E.VertexStart].Point,P1=Body.Vertices[E.VertexEnd].Point,D=P1-P0;double L=D.Length();if(L<=Tol)return std::nullopt;
            if(LA==0){LA=L;A=D/L;}else if(std::fabs(L-LA)>ScalarCriteria::GeometricTolerance*LA||std::fabs(D.Normalised().Dot(A))<1.0-ScalarCriteria::GeometricTolerance)return std::nullopt;
            Low.push_back(P0.Dot(A)<=P1.Dot(A)?P0:P1);}
        std::sort(Low.begin(),Low.end(),[](Vec3 X,Vec3 Y){if(X.X!=Y.X)return X.X<Y.X;if(X.Y!=Y.Y)return X.Y<Y.Y;return X.Z<Y.Z;});
        Vec3 O=Low.front();std::vector<Vec3>D;for(size_t I=1;I<Low.size();++I)D.push_back(Low[I]-O);
        std::sort(D.begin(),D.end(),[](Vec3 X,Vec3 Y){return X.LengthSquared()<Y.LengthSquared();});
        if(D.size()!=3||D[0].Length()<=Tol||D[1].Length()<=Tol)return std::nullopt;
        Vec3 B=D[0].Normalised(),C=D[1].Normalised();double LB=D[0].Length(),LC=D[1].Length();
        if(std::fabs(B.Dot(C))>ScalarCriteria::GeometricTolerance||D[2].Distance(D[0]+D[1])>ScalarCriteria::GeometricTolerance*std::max(LB,LC))return std::nullopt;
        if(A.Cross(B).Dot(C)<0){std::swap(B,C);std::swap(LB,LC);}
        return OrthogonalBoxCorner{O,A,B,C,LA,LB,LC};
    }

    constexpr size_t MaxPrismBores=8;
    struct PrismHole { double B=0.0,C=0.0,Radius=0.0; };
    struct PerforatedBoxPrism { OrthogonalBoxCorner Box; std::vector<PrismHole> Holes; };

    std::optional<PerforatedBoxPrism> ClassifyPerforatedBoxPrism(const BrepBody& Body,const std::vector<int>& Edges) noexcept
    {
        auto Report=Body.Validate();int HoleCount=Report.Genus;
        if(Edges.size()!=4||!Report.Solid()||Report.Hulls!=1||HoleCount<1||HoleCount>static_cast<int>(MaxPrismBores)||
           Body.Vertices.size()!=static_cast<size_t>(8+2*HoleCount)||Body.Edges.size()!=static_cast<size_t>(12+3*HoleCount)||
           Body.Coedges.size()!=static_cast<size_t>(24+6*HoleCount)||Body.Loops.size()!=static_cast<size_t>(6+3*HoleCount)||
           Body.Faces.size()!=static_cast<size_t>(6+HoleCount))return std::nullopt;
        int Planes=0,Extrusions=0;
        for(const BrepFace& Face:Body.Faces){Planes+=Face.Surface.Classification==SurfaceClassification::Plane;
            Extrusions+=Face.Surface.Classification==SurfaceClassification::Extrusion;}
        if(Planes!=2||Extrusions!=4+HoleCount)return std::nullopt;
        auto Frame=PrismFrameFromRails(Body,Edges);if(!Frame)return std::nullopt;
        Vec3 O=Frame->Corner,A=Frame->X,B=Frame->Y,C=Frame->Z;double LA=Frame->LX,LB=Frame->LY,LC=Frame->LZ;
        struct Ring{Vec3 Centre,Normal;double Radius=0.0;};std::vector<Ring>Lower,Upper;
        const double Epsilon=ScalarCriteria::GeometricTolerance*std::max(1.0,LA);
        for(const BrepEdge&E:Body.Edges)if(E.Closed())
        {
            Ring R;if(!CircularFrame(E.Curve,R.Centre,R.Normal,R.Radius)||std::fabs(R.Normal.Dot(A))<1.0-ScalarCriteria::GeometricTolerance)return std::nullopt;
            double T=(R.Centre-O).Dot(A);if(std::fabs(T)<=Epsilon)Lower.push_back(R);else if(std::fabs(T-LA)<=Epsilon)Upper.push_back(R);else return std::nullopt;
        }
        if(Lower.size()!=static_cast<size_t>(HoleCount)||Upper.size()!=Lower.size())return std::nullopt;
        std::sort(Lower.begin(),Lower.end(),[&](const Ring& X,const Ring& Y){Vec3 DX=X.Centre-O,DY=Y.Centre-O;
            if(DX.Dot(B)!=DY.Dot(B))return DX.Dot(B)<DY.Dot(B);
            return DX.Dot(C)<DY.Dot(C);});
        std::vector<bool>Used(Upper.size(),false);std::vector<PrismHole>Holes;double RemovedArea=0.0;
        for(const Ring& L:Lower)
        {
            int Match=-1;for(size_t I=0;I<Upper.size();++I)if(!Used[I])
            {
                Vec3 Span=Upper[I].Centre-L.Centre;double Axial=Span.Dot(A);
                if(std::fabs(std::fabs(Axial)-LA)<=Epsilon&&(Span-A*Axial).Length()<=Epsilon&&
                   std::fabs(Upper[I].Radius-L.Radius)<=ScalarCriteria::GeometricTolerance*std::max(1.0,L.Radius)){if(Match>=0)return std::nullopt;Match=static_cast<int>(I);}
            }
            if(Match<0)return std::nullopt;
            Used[Match]=true;Vec3 Offset=L.Centre-O;
            if(std::fabs(Offset.Dot(A))>Epsilon)return std::nullopt;
            Holes.push_back({Offset.Dot(B),Offset.Dot(C),L.Radius});RemovedArea+=ScalarCriteria::Pi*L.Radius*L.Radius;
        }
        bool Intersecting=false;
        for(size_t I=0;I<Holes.size();++I)for(size_t J=I+1;J<Holes.size();++J)
            Intersecting|=Vec2{Holes[I].B-Holes[J].B,Holes[I].C-Holes[J].C}.Length()<=Holes[I].Radius+Holes[J].Radius+Tol;
        OrthogonalBoxCorner Box{O,A,B,C,LA,LB,LC};double Exact=LA*(LB*LC-RemovedArea);
        if(!Intersecting&&!ScalarCriteria::WithinVolumeTolerance(Report.Volume, Exact))return std::nullopt;
        return PerforatedBoxPrism{Box,std::move(Holes)};
    }

    struct BlindCavity { PrismHole Hole; bool FromLow=true; double Depth=0.0; };
    struct BlindBoxPrism { OrthogonalBoxCorner Box; std::vector<BlindCavity> Cavities; };

    std::optional<BlindBoxPrism> ClassifyBlindBoxPrism(const BrepBody& Body,const std::vector<int>& Edges) noexcept
    {
        auto Report=Body.Validate();
        if(!Report.Solid()||Report.Hulls!=1||Report.Genus!=0||Body.Faces.size()<8||(Body.Faces.size()-6)%2!=0)return std::nullopt;
        size_t Count=(Body.Faces.size()-6)/2;
        if(Count<1||Count>MaxPrismBores||Body.Vertices.size()!=8+2*Count||Body.Edges.size()!=12+3*Count||
           Body.Coedges.size()!=24+6*Count||Body.Loops.size()!=6+3*Count)return std::nullopt;
        auto Frame=PrismFrameFromRails(Body,Edges);if(!Frame)return std::nullopt;
        int Planes=0,Cylinders=0,EndLoops=0;std::vector<int>CylinderFaces;
        for(size_t I=0;I<Body.Faces.size();++I)
        {
            const BrepFace& Face=Body.Faces[I];Planes+=Face.Surface.Classification==SurfaceClassification::Plane;
            if(Face.Surface.Classification==SurfaceClassification::Plane&&Face.Loops.size()>1)EndLoops+=static_cast<int>(Face.Loops.size()-1);
            if(Face.Surface.Classification==SurfaceClassification::Cylinder){++Cylinders;CylinderFaces.push_back(static_cast<int>(I));}
        }
        if(Planes!=static_cast<int>(6+Count)||Cylinders!=static_cast<int>(Count)||EndLoops!=static_cast<int>(Count))return std::nullopt;
        Vec3 A=Frame->X;double LA=Frame->LX,Scale=std::max(1.0,LA),Removed=0.0;std::vector<BlindCavity>Cavities;
        for(int CylinderFace:CylinderFaces)
        {
            const BrepFace& Face=Body.Faces[CylinderFace];const NurbsSurface& Cylinder=Face.Surface;
            if(!Cylinder.Rational()||Cylinder.RadiusMajor<=Tol||std::fabs(Cylinder.Axis.Normalised().Dot(A))<1.0-ScalarCriteria::GeometricTolerance)return std::nullopt;
            std::vector<double>RingT;
            for(int Loop:Face.Loops){if(Loop<0||Loop>=static_cast<int>(Body.Loops.size()))return std::nullopt;
                for(int Coedge:Body.Loops[Loop].Coedges){if(Coedge<0||Coedge>=static_cast<int>(Body.Coedges.size()))return std::nullopt;
                    int Edge=Body.Coedges[Coedge].Edge;if(Edge<0||Edge>=static_cast<int>(Body.Edges.size()))return std::nullopt;
                    const BrepEdge& E=Body.Edges[Edge];if(!E.Closed())continue;double T=(E.Curve.Sample(E.Curve.DomainStart())-Frame->Corner).Dot(A);
                    for(int K=1;K<=4;++K)if(std::fabs((E.Curve.Sample(E.Curve.DomainStart()+(E.Curve.DomainEnd()-E.Curve.DomainStart())*K/4.0)-Frame->Corner).Dot(A)-T)>ScalarCriteria::GeometricTolerance*Scale)return std::nullopt;
                    RingT.push_back(T);}}
            if(RingT.size()!=2)return std::nullopt;
            std::sort(RingT.begin(),RingT.end());
            bool FromLow=std::fabs(RingT[0])<=ScalarCriteria::GeometricTolerance*Scale&&RingT[1]>Tol&&RingT[1]<LA-Tol;
            bool FromHigh=std::fabs(RingT[1]-LA)<=ScalarCriteria::GeometricTolerance*Scale&&RingT[0]>Tol&&RingT[0]<LA-Tol;
            if(FromLow==FromHigh)return std::nullopt;
            double Depth=FromLow?RingT[1]:LA-RingT[0];
            Vec3 Offset=Cylinder.Origin-Frame->Corner;PrismHole Hole{Offset.Dot(Frame->Y),Offset.Dot(Frame->Z),Cylinder.RadiusMajor};
            Cavities.push_back({Hole,FromLow,Depth});Removed+=ScalarCriteria::Pi*Hole.Radius*Hole.Radius*Depth;
        }
        std::sort(Cavities.begin(),Cavities.end(),[](const BlindCavity& X,const BlindCavity& Y){if(X.FromLow!=Y.FromLow)return X.FromLow>Y.FromLow;
            if(X.Hole.B!=Y.Hole.B)return X.Hole.B<Y.Hole.B;
            return X.Hole.C<Y.Hole.C;});
        double Exact=Frame->LX*(Frame->LY*Frame->LZ)-Removed;
        if(!ScalarCriteria::WithinVolumeTolerance(Report.Volume, Exact))return std::nullopt;
        return BlindBoxPrism{*Frame,std::move(Cavities)};
    }

    struct SideBlindCavity
    {
        int Along=1;double X=0.0,Cross=0.0,Radius=0.0;bool FromLow=true;double Depth=0.0;
    };
    struct SideBlindPrism { OrthogonalBoxCorner Box;std::vector<SideBlindCavity> Cavities; };

    std::optional<SideBlindPrism> ClassifySideBlindPrism(const BrepBody& Body,const std::vector<int>& Edges) noexcept
    {
        auto Report=Body.Validate();
        if(!Report.Solid()||Report.Hulls!=1||Report.Genus!=0||Body.Faces.size()<8||(Body.Faces.size()-6)%2!=0)return std::nullopt;
        size_t Count=(Body.Faces.size()-6)/2;
        if(Count<1||Count>MaxPrismBores||Body.Vertices.size()!=8+2*Count||Body.Edges.size()!=12+3*Count||
           Body.Coedges.size()!=24+6*Count||Body.Loops.size()!=6+3*Count)return std::nullopt;
        auto Frame=PrismFrameFromRails(Body,Edges);if(!Frame)return std::nullopt;
        int Planes=0,Cylinders=0,InnerLoops=0;std::vector<int>CylinderFaces;
        for(size_t I=0;I<Body.Faces.size();++I)
        {
            const BrepFace& Face=Body.Faces[I];Planes+=Face.Surface.Classification==SurfaceClassification::Plane;
            if(Face.Surface.Classification==SurfaceClassification::Plane&&Face.Loops.size()>1)InnerLoops+=static_cast<int>(Face.Loops.size()-1);
            if(Face.Surface.Classification==SurfaceClassification::Cylinder){++Cylinders;CylinderFaces.push_back(static_cast<int>(I));}
        }
        if(Planes!=static_cast<int>(6+Count)||Cylinders!=static_cast<int>(Count)||InnerLoops!=static_cast<int>(Count))return std::nullopt;
        double Removed=0.0;std::vector<SideBlindCavity>Cavities;Cavities.reserve(Count);
        for(int CylinderFace:CylinderFaces)
        {
            const BrepFace& Face=Body.Faces[CylinderFace];const NurbsSurface& Cylinder=Face.Surface;
            if(!Cylinder.Rational()||Cylinder.RadiusMajor<=Tol)return std::nullopt;
            Vec3 Axis=Cylinder.Axis.Normalised();int Along=0;
            if(std::fabs(Axis.Dot(Frame->Y))>1.0-ScalarCriteria::GeometricTolerance)Along=1;
            else if(std::fabs(Axis.Dot(Frame->Z))>1.0-ScalarCriteria::GeometricTolerance)Along=2;
            else return std::nullopt;
            Vec3 Direction=Along==1?Frame->Y:Frame->Z;double Length=Along==1?Frame->LY:Frame->LZ;
            double Scale=std::max(1.0,Length);std::vector<double>RingT;
            for(int Loop:Face.Loops){if(Loop<0||Loop>=static_cast<int>(Body.Loops.size()))return std::nullopt;
                for(int Coedge:Body.Loops[Loop].Coedges){if(Coedge<0||Coedge>=static_cast<int>(Body.Coedges.size()))return std::nullopt;
                    int Edge=Body.Coedges[Coedge].Edge;if(Edge<0||Edge>=static_cast<int>(Body.Edges.size()))return std::nullopt;
                    const BrepEdge& E=Body.Edges[Edge];if(!E.Closed())continue;double T=(E.Curve.Sample(E.Curve.DomainStart())-Frame->Corner).Dot(Direction);
                    for(int K=1;K<=4;++K)if(std::fabs((E.Curve.Sample(E.Curve.DomainStart()+(E.Curve.DomainEnd()-E.Curve.DomainStart())*K/4.0)-Frame->Corner).Dot(Direction)-T)>ScalarCriteria::GeometricTolerance*Scale)return std::nullopt;
                    RingT.push_back(T);}}
            if(RingT.size()!=2)return std::nullopt;
            std::sort(RingT.begin(),RingT.end());
            bool FromLow=std::fabs(RingT[0])<=ScalarCriteria::GeometricTolerance*Scale&&RingT[1]>Tol&&RingT[1]<Length-Tol;
            bool FromHigh=std::fabs(RingT[1]-Length)<=ScalarCriteria::GeometricTolerance*Scale&&RingT[0]>Tol&&RingT[0]<Length-Tol;
            if(FromLow==FromHigh)return std::nullopt;
            double Depth=FromLow?RingT[1]:Length-RingT[0];Vec3 Offset=Cylinder.Origin-Frame->Corner;
            Cavities.push_back({Along,Offset.Dot(Frame->X),Offset.Dot(Along==1?Frame->Z:Frame->Y),Cylinder.RadiusMajor,FromLow,Depth});
            Removed+=ScalarCriteria::Pi*Cylinder.RadiusMajor*Cylinder.RadiusMajor*Depth;
        }
        for(size_t I=1;I<Cavities.size();++I)if(Cavities[I].Along!=Cavities[0].Along)return std::nullopt;
        std::sort(Cavities.begin(),Cavities.end(),[](const SideBlindCavity& A,const SideBlindCavity& B)
        {
            if(A.Along!=B.Along)return A.Along<B.Along;
            if(A.FromLow!=B.FromLow)return A.FromLow>B.FromLow;
            if(A.X!=B.X)return A.X<B.X;
            return A.Cross<B.Cross;
        });
        double Exact=Frame->LX*Frame->LY*Frame->LZ-Removed;
        if(!ScalarCriteria::WithinVolumeTolerance(Report.Volume, Exact))return std::nullopt;
        return SideBlindPrism{*Frame,std::move(Cavities)};
    }

    struct SideSteppedBlindStage { double Radius=0.0,Depth=0.0; };
    struct SideSteppedBlindPrism
    {
        OrthogonalBoxCorner Box;int Along=1;double X=0.0,Cross=0.0;std::vector<SideSteppedBlindStage> Stages;bool FromLow=true;
    };

    std::optional<SideSteppedBlindPrism> ClassifySideSteppedBlindPrism(const BrepBody& Body,const std::vector<int>& Edges) noexcept
    {
        auto Report=Body.Validate();
        if(!Report.Solid()||Report.Hulls!=1||Report.Genus!=0||Body.Faces.size()<10||(Body.Faces.size()-6)%2!=0)return std::nullopt;
        size_t Count=(Body.Faces.size()-6)/2;
        if(Count<2||Count>MaxPrismBores||Body.Vertices.size()!=8+2*Count||Body.Edges.size()!=12+3*Count||
           Body.Coedges.size()!=24+6*Count||Body.Loops.size()!=6+3*Count)return std::nullopt;
        auto Frame=PrismFrameFromRails(Body,Edges);if(!Frame)return std::nullopt;
        int Planes=0,Cylinders=0,InnerLoops=0;std::vector<int>CylinderFaces;
        for(size_t I=0;I<Body.Faces.size();++I)
        {
            const BrepFace& Face=Body.Faces[I];Planes+=Face.Surface.Classification==SurfaceClassification::Plane;
            if(Face.Surface.Classification==SurfaceClassification::Plane&&Face.Loops.size()>1)InnerLoops+=static_cast<int>(Face.Loops.size()-1);
            if(Face.Surface.Classification==SurfaceClassification::Cylinder){++Cylinders;CylinderFaces.push_back(static_cast<int>(I));}
        }
        if(Planes!=static_cast<int>(6+Count)||Cylinders!=static_cast<int>(Count)||InnerLoops!=static_cast<int>(Count))return std::nullopt;
        struct Span{int Along=0;double X=0.0,Cross=0.0,Radius=0.0,Low=0.0,High=0.0;};std::vector<Span>Spans;
        for(int CylinderFace:CylinderFaces)
        {
            const BrepFace& Face=Body.Faces[CylinderFace];const NurbsSurface& Cylinder=Face.Surface;
            if(!Cylinder.Rational()||Cylinder.RadiusMajor<=Tol)return std::nullopt;
            Vec3 Axis=Cylinder.Axis.Normalised();int Along=0;
            if(std::fabs(Axis.Dot(Frame->Y))>1.0-ScalarCriteria::GeometricTolerance)Along=1;
            else if(std::fabs(Axis.Dot(Frame->Z))>1.0-ScalarCriteria::GeometricTolerance)Along=2;
            else return std::nullopt;
            Vec3 Direction=Along==1?Frame->Y:Frame->Z;double Length=Along==1?Frame->LY:Frame->LZ;
            double PositionTolerance=ScalarCriteria::GeometricTolerance*std::max(1.0,Length);std::vector<double>RingT;
            for(int Loop:Face.Loops){if(Loop<0||Loop>=static_cast<int>(Body.Loops.size()))return std::nullopt;
                for(int Coedge:Body.Loops[Loop].Coedges){if(Coedge<0||Coedge>=static_cast<int>(Body.Coedges.size()))return std::nullopt;
                    int Edge=Body.Coedges[Coedge].Edge;if(Edge<0||Edge>=static_cast<int>(Body.Edges.size()))return std::nullopt;
                    const BrepEdge& E=Body.Edges[Edge];if(!E.Closed())continue;double T=(E.Curve.Sample(E.Curve.DomainStart())-Frame->Corner).Dot(Direction);
                    for(int K=1;K<=4;++K)if(std::fabs((E.Curve.Sample(E.Curve.DomainStart()+(E.Curve.DomainEnd()-E.Curve.DomainStart())*K/4.0)-Frame->Corner).Dot(Direction)-T)>PositionTolerance)return std::nullopt;
                    RingT.push_back(T);}}
            if(RingT.size()!=2)return std::nullopt;
            std::sort(RingT.begin(),RingT.end());Vec3 Offset=Cylinder.Origin-Frame->Corner;
            Spans.push_back({Along,Offset.Dot(Frame->X),Offset.Dot(Along==1?Frame->Z:Frame->Y),Cylinder.RadiusMajor,RingT[0],RingT[1]});
        }
        int Along=Spans.front().Along;for(const Span& SpanValue:Spans)if(SpanValue.Along!=Along)return std::nullopt;
        double Length=Along==1?Frame->LY:Frame->LZ,PositionTolerance=ScalarCriteria::GeometricTolerance*std::max(1.0,Length);
        int Entry=-1;bool FromLow=false;
        for(size_t I=0;I<Spans.size();++I)
        {
            bool Low=std::fabs(Spans[I].Low)<=PositionTolerance,High=std::fabs(Spans[I].High-Length)<=PositionTolerance;
            if(Low==High)continue;
            if(Entry>=0)return std::nullopt;
            Entry=static_cast<int>(I);FromLow=Low;
        }
        if(Entry<0)return std::nullopt;
        const Span& Outer=Spans[Entry];std::vector<bool>Used(Count,false);std::vector<SideSteppedBlindStage>Stages;Stages.reserve(Count);
        int Current=Entry;double PreviousDepth=0.0,PreviousRadius=std::numeric_limits<double>::infinity(),Removed=0.0;
        for(size_t StageIndex=0;StageIndex<Count;++StageIndex)
        {
            if(Current<0||Used[Current])return std::nullopt;
            const Span& CurrentSpan=Spans[Current];Used[Current]=true;
            if(std::hypot(CurrentSpan.X-Outer.X,CurrentSpan.Cross-Outer.Cross)>PositionTolerance||CurrentSpan.Radius>=PreviousRadius-Tol)
                return std::nullopt;
            double Depth=FromLow?CurrentSpan.High:Length-CurrentSpan.Low;
            if(Depth<=PreviousDepth+Tol||Depth>=Length-Tol)return std::nullopt;
            Stages.push_back({CurrentSpan.Radius,Depth});
            Removed+=ScalarCriteria::Pi*CurrentSpan.Radius*CurrentSpan.Radius*(Depth-PreviousDepth);
            PreviousDepth=Depth;PreviousRadius=CurrentSpan.Radius;
            if(StageIndex+1==Count)break;
            double Boundary=FromLow?CurrentSpan.High:CurrentSpan.Low;int Next=-1;
            for(size_t I=0;I<Count;++I)if(!Used[I])
            {
                double Candidate=FromLow?Spans[I].Low:Spans[I].High;
                if(std::fabs(Candidate-Boundary)>PositionTolerance)continue;
                if(Next>=0)return std::nullopt;
                Next=static_cast<int>(I);
            }
            if(Next<0)return std::nullopt;
            Current=Next;
        }
        double Exact=Frame->LX*Frame->LY*Frame->LZ-Removed;
        if(!ScalarCriteria::WithinVolumeTolerance(Report.Volume, Exact))return std::nullopt;
        return SideSteppedBlindPrism{*Frame,Along,Outer.X,Outer.Cross,std::move(Stages),FromLow};
    }

    struct SideSteppedBlindCavity
    {
        int Along=1;double X=0.0,Cross=0.0;std::vector<SideSteppedBlindStage> Stages;bool FromLow=true;
    };
    struct SideSteppedBlindSet
    {
        OrthogonalBoxCorner Box;std::vector<SideSteppedBlindCavity> Cavities;
    };

    std::optional<SideSteppedBlindSet> ClassifySideSteppedBlindSet(const BrepBody& Body,const std::vector<int>& Edges) noexcept
    {
        auto Report=Body.Validate();
        if(!Report.Solid()||Report.Hulls!=1||Report.Genus!=0||Body.Faces.size()<14||(Body.Faces.size()-6)%2!=0)return std::nullopt;
        size_t StageCount=(Body.Faces.size()-6)/2;
        if(StageCount<4||StageCount>2*MaxPrismBores||Body.Vertices.size()!=8+2*StageCount||Body.Edges.size()!=12+3*StageCount||
           Body.Coedges.size()!=24+6*StageCount||Body.Loops.size()!=6+3*StageCount)return std::nullopt;
        auto Frame=PrismFrameFromRails(Body,Edges);if(!Frame)return std::nullopt;
        int Planes=0,Cylinders=0,InnerLoops=0;std::vector<int>CylinderFaces;
        for(size_t I=0;I<Body.Faces.size();++I)
        {
            const BrepFace& Face=Body.Faces[I];Planes+=Face.Surface.Classification==SurfaceClassification::Plane;
            if(Face.Surface.Classification==SurfaceClassification::Plane&&Face.Loops.size()>1)InnerLoops+=static_cast<int>(Face.Loops.size()-1);
            if(Face.Surface.Classification==SurfaceClassification::Cylinder){++Cylinders;CylinderFaces.push_back(static_cast<int>(I));}
        }
        if(Planes!=static_cast<int>(6+StageCount)||Cylinders!=static_cast<int>(StageCount)||InnerLoops!=static_cast<int>(StageCount))return std::nullopt;
        struct Span{int Along=0;double X=0.0,Cross=0.0,Radius=0.0,Low=0.0,High=0.0;};std::vector<Span>Spans;
        for(int CylinderFace:CylinderFaces)
        {
            const BrepFace& Face=Body.Faces[CylinderFace];const NurbsSurface& Cylinder=Face.Surface;
            if(!Cylinder.Rational()||Cylinder.RadiusMajor<=Tol)return std::nullopt;
            Vec3 Axis=Cylinder.Axis.Normalised();int Along=0;
            if(std::fabs(Axis.Dot(Frame->Y))>1.0-ScalarCriteria::GeometricTolerance)Along=1;
            else if(std::fabs(Axis.Dot(Frame->Z))>1.0-ScalarCriteria::GeometricTolerance)Along=2;
            else return std::nullopt;
            Vec3 Direction=Along==1?Frame->Y:Frame->Z;double Length=Along==1?Frame->LY:Frame->LZ;
            double PositionTolerance=ScalarCriteria::GeometricTolerance*std::max(1.0,Length);std::vector<double>RingT;
            for(int Loop:Face.Loops){if(Loop<0||Loop>=static_cast<int>(Body.Loops.size()))return std::nullopt;
                for(int Coedge:Body.Loops[Loop].Coedges){if(Coedge<0||Coedge>=static_cast<int>(Body.Coedges.size()))return std::nullopt;
                    int Edge=Body.Coedges[Coedge].Edge;if(Edge<0||Edge>=static_cast<int>(Body.Edges.size()))return std::nullopt;
                    const BrepEdge& E=Body.Edges[Edge];if(!E.Closed())continue;double T=(E.Curve.Sample(E.Curve.DomainStart())-Frame->Corner).Dot(Direction);
                    for(int K=1;K<=4;++K)if(std::fabs((E.Curve.Sample(E.Curve.DomainStart()+(E.Curve.DomainEnd()-E.Curve.DomainStart())*K/4.0)-Frame->Corner).Dot(Direction)-T)>PositionTolerance)return std::nullopt;
                    RingT.push_back(T);}}
            if(RingT.size()!=2)return std::nullopt;
            std::sort(RingT.begin(),RingT.end());Vec3 Offset=Cylinder.Origin-Frame->Corner;
            Spans.push_back({Along,Offset.Dot(Frame->X),Offset.Dot(Along==1?Frame->Z:Frame->Y),Cylinder.RadiusMajor,RingT[0],RingT[1]});
        }
        int Along=Spans.front().Along;for(const Span& SpanValue:Spans)if(SpanValue.Along!=Along)return std::nullopt;
        double Length=Along==1?Frame->LY:Frame->LZ,PositionTolerance=ScalarCriteria::GeometricTolerance*std::max(1.0,Length);
        std::vector<int>Entries;
        for(size_t I=0;I<Spans.size();++I)
        {
            bool Low=std::fabs(Spans[I].Low)<=PositionTolerance,High=std::fabs(Spans[I].High-Length)<=PositionTolerance;
            if(Low!=High)Entries.push_back(static_cast<int>(I));
        }
        if(Entries.size()<2||Entries.size()>MaxPrismBores)return std::nullopt;
        std::vector<bool>Used(Spans.size(),false);std::vector<SideSteppedBlindCavity>Cavities;Cavities.reserve(Entries.size());double Removed=0.0;
        for(int Entry:Entries)
        {
            if(Used[Entry])return std::nullopt;
            const Span& Outer=Spans[Entry];bool FromLow=std::fabs(Outer.Low)<=PositionTolerance;int Current=Entry;
            double PreviousDepth=0.0,PreviousRadius=std::numeric_limits<double>::infinity();std::vector<SideSteppedBlindStage>Stages;
            while(Current>=0)
            {
                if(Used[Current]||Stages.size()>=MaxPrismBores)return std::nullopt;
                const Span& CurrentSpan=Spans[Current];Used[Current]=true;
                if(std::hypot(CurrentSpan.X-Outer.X,CurrentSpan.Cross-Outer.Cross)>PositionTolerance||CurrentSpan.Radius>=PreviousRadius-Tol)
                    return std::nullopt;
                double Depth=FromLow?CurrentSpan.High:Length-CurrentSpan.Low;
                if(Depth<=PreviousDepth+Tol||Depth>=Length-Tol)return std::nullopt;
                Stages.push_back({CurrentSpan.Radius,Depth});
                Removed+=ScalarCriteria::Pi*CurrentSpan.Radius*CurrentSpan.Radius*(Depth-PreviousDepth);
                PreviousDepth=Depth;PreviousRadius=CurrentSpan.Radius;
                double Boundary=FromLow?CurrentSpan.High:CurrentSpan.Low;int Next=-1;
                for(size_t I=0;I<Spans.size();++I)if(!Used[I])
                {
                    double CandidateBoundary=FromLow?Spans[I].Low:Spans[I].High;
                    if(std::fabs(CandidateBoundary-Boundary)>PositionTolerance||
                       std::hypot(Spans[I].X-Outer.X,Spans[I].Cross-Outer.Cross)>PositionTolerance)continue;
                    if(Next>=0)return std::nullopt;
                    Next=static_cast<int>(I);
                }
                Current=Next;
            }
            if(Stages.size()<2)return std::nullopt;
            Cavities.push_back({Along,Outer.X,Outer.Cross,std::move(Stages),FromLow});
        }
        if(std::find(Used.begin(),Used.end(),false)!=Used.end())return std::nullopt;
        std::sort(Cavities.begin(),Cavities.end(),[](const SideSteppedBlindCavity& A,const SideSteppedBlindCavity& B)
        {
            if(A.FromLow!=B.FromLow)return A.FromLow>B.FromLow;
            if(A.X!=B.X)return A.X<B.X;
            return A.Cross<B.Cross;
        });
        double Exact=Frame->LX*Frame->LY*Frame->LZ-Removed;
        if(!ScalarCriteria::WithinVolumeTolerance(Report.Volume, Exact))return std::nullopt;
        return SideSteppedBlindSet{*Frame,std::move(Cavities)};
    }

    struct SteppedBlindStage { PrismHole Hole;double Depth=0.0; };
    struct SteppedBlindPrism
    {
        OrthogonalBoxCorner Box;std::vector<SteppedBlindStage> Stages;bool FromLow=true;
    };

    std::optional<SteppedBlindPrism> ClassifySteppedBlindPrism(const BrepBody& Body,const std::vector<int>& Edges) noexcept
    {
        auto Report=Body.Validate();
        if(!Report.Solid()||Report.Hulls!=1||Report.Genus!=0||Body.Faces.size()<10||(Body.Faces.size()-6)%2!=0)return std::nullopt;
        size_t Count=(Body.Faces.size()-6)/2;
        if(Count<2||Count>MaxPrismBores||Body.Vertices.size()!=8+2*Count||Body.Edges.size()!=12+3*Count||
           Body.Coedges.size()!=24+6*Count||Body.Loops.size()!=6+3*Count)return std::nullopt;
        auto Frame=PrismFrameFromRails(Body,Edges);if(!Frame)return std::nullopt;
        int Planes=0,Cylinders=0,InnerLoops=0;std::vector<int>CylinderFaces;
        for(size_t I=0;I<Body.Faces.size();++I)
        {
            const BrepFace& Face=Body.Faces[I];Planes+=Face.Surface.Classification==SurfaceClassification::Plane;
            if(Face.Surface.Classification==SurfaceClassification::Plane&&Face.Loops.size()>1)InnerLoops+=static_cast<int>(Face.Loops.size()-1);
            if(Face.Surface.Classification==SurfaceClassification::Cylinder){++Cylinders;CylinderFaces.push_back(static_cast<int>(I));}
        }
        if(Planes!=static_cast<int>(6+Count)||Cylinders!=static_cast<int>(Count)||InnerLoops!=static_cast<int>(Count))return std::nullopt;
        struct Span{PrismHole Hole;double Low=0.0,High=0.0;};std::vector<Span>Spans;
        Vec3 A=Frame->X;double LA=Frame->LX,Scale=std::max(1.0,LA),PositionTolerance=ScalarCriteria::GeometricTolerance*Scale;
        for(int CylinderFace:CylinderFaces)
        {
            const BrepFace& Face=Body.Faces[CylinderFace];const NurbsSurface& Cylinder=Face.Surface;
            if(!Cylinder.Rational()||Cylinder.RadiusMajor<=Tol||std::fabs(Cylinder.Axis.Normalised().Dot(A))<1.0-ScalarCriteria::GeometricTolerance)return std::nullopt;
            std::vector<double>RingT;
            for(int Loop:Face.Loops){if(Loop<0||Loop>=static_cast<int>(Body.Loops.size()))return std::nullopt;
                for(int Coedge:Body.Loops[Loop].Coedges){if(Coedge<0||Coedge>=static_cast<int>(Body.Coedges.size()))return std::nullopt;
                    int Edge=Body.Coedges[Coedge].Edge;if(Edge<0||Edge>=static_cast<int>(Body.Edges.size()))return std::nullopt;
                    const BrepEdge& E=Body.Edges[Edge];if(!E.Closed())continue;double T=(E.Curve.Sample(E.Curve.DomainStart())-Frame->Corner).Dot(A);
                    for(int K=1;K<=4;++K)if(std::fabs((E.Curve.Sample(E.Curve.DomainStart()+(E.Curve.DomainEnd()-E.Curve.DomainStart())*K/4.0)-Frame->Corner).Dot(A)-T)>PositionTolerance)return std::nullopt;
                    RingT.push_back(T);}}
            if(RingT.size()!=2)return std::nullopt;
            std::sort(RingT.begin(),RingT.end());Vec3 Offset=Cylinder.Origin-Frame->Corner;
            Spans.push_back({{Offset.Dot(Frame->Y),Offset.Dot(Frame->Z),Cylinder.RadiusMajor},RingT[0],RingT[1]});
        }
        int Entry=-1;bool FromLow=false;
        for(size_t I=0;I<Count;++I)
        {
            bool Low=std::fabs(Spans[I].Low)<=PositionTolerance,High=std::fabs(Spans[I].High-LA)<=PositionTolerance;
            if(Low==High)continue;
            if(Entry>=0)return std::nullopt;
            Entry=static_cast<int>(I);FromLow=Low;
        }
        if(Entry<0)return std::nullopt;
        std::vector<bool>Used(Count,false);std::vector<SteppedBlindStage>Stages;Stages.reserve(Count);
        int Current=Entry;double PreviousDepth=0.0,PreviousRadius=std::numeric_limits<double>::infinity(),Removed=0.0;
        const PrismHole& Centre=Spans[Entry].Hole;
        for(size_t StageIndex=0;StageIndex<Count;++StageIndex)
        {
            if(Current<0||Used[Current])return std::nullopt;
            const Span& CurrentSpan=Spans[Current];Used[Current]=true;
            if(std::hypot(CurrentSpan.Hole.B-Centre.B,CurrentSpan.Hole.C-Centre.C)>PositionTolerance||
               CurrentSpan.Hole.Radius>=PreviousRadius-Tol)return std::nullopt;
            double Depth=FromLow?CurrentSpan.High:LA-CurrentSpan.Low;
            if(Depth<=PreviousDepth+Tol||Depth>=LA-Tol)return std::nullopt;
            Stages.push_back({CurrentSpan.Hole,Depth});
            Removed+=ScalarCriteria::Pi*CurrentSpan.Hole.Radius*CurrentSpan.Hole.Radius*(Depth-PreviousDepth);
            PreviousDepth=Depth;PreviousRadius=CurrentSpan.Hole.Radius;
            if(StageIndex+1==Count)break;
            double Boundary=FromLow?CurrentSpan.High:CurrentSpan.Low;int Next=-1;
            for(size_t I=0;I<Count;++I)if(!Used[I])
            {
                double Candidate=FromLow?Spans[I].Low:Spans[I].High;
                if(std::fabs(Candidate-Boundary)>PositionTolerance)continue;
                if(Next>=0)return std::nullopt;
                Next=static_cast<int>(I);
            }
            if(Next<0)return std::nullopt;
            Current=Next;
        }
        double Exact=LA*Frame->LY*Frame->LZ-Removed;
        if(!ScalarCriteria::WithinVolumeTolerance(Report.Volume, Exact))return std::nullopt;
        return SteppedBlindPrism{*Frame,std::move(Stages),FromLow};
    }

    struct TwoStageBlindCavity
    {
        std::vector<SteppedBlindStage> Stages;bool FromLow=true;
    };
    struct DualSteppedBlindPrism
    {
        OrthogonalBoxCorner Box;std::vector<TwoStageBlindCavity> Cavities;
    };

    std::optional<DualSteppedBlindPrism> ClassifyDualSteppedBlindPrism(const BrepBody& Body,const std::vector<int>& Edges) noexcept
    {
        auto Report=Body.Validate();
        if(!Report.Solid()||Report.Hulls!=1||Report.Genus!=0||Body.Vertices.size()!=16||Body.Edges.size()!=24||
           Body.Coedges.size()!=48||Body.Loops.size()!=18||Body.Faces.size()!=14)return std::nullopt;
        auto Frame=PrismFrameFromRails(Body,Edges);if(!Frame)return std::nullopt;
        int Planes=0,Cylinders=0,InnerLoops=0;std::vector<int>CylinderFaces;
        for(size_t I=0;I<Body.Faces.size();++I)
        {
            const BrepFace& Face=Body.Faces[I];Planes+=Face.Surface.Classification==SurfaceClassification::Plane;
            if(Face.Surface.Classification==SurfaceClassification::Plane&&Face.Loops.size()>1)InnerLoops+=static_cast<int>(Face.Loops.size()-1);
            if(Face.Surface.Classification==SurfaceClassification::Cylinder){++Cylinders;CylinderFaces.push_back(static_cast<int>(I));}
        }
        if(Planes!=10||Cylinders!=4||InnerLoops!=4)return std::nullopt;
        struct Span{PrismHole Hole;double Low=0.0,High=0.0;};std::vector<Span>Spans;
        Vec3 A=Frame->X;double LA=Frame->LX,Scale=std::max(1.0,LA),PositionTolerance=ScalarCriteria::GeometricTolerance*Scale;
        for(int CylinderFace:CylinderFaces)
        {
            const BrepFace& Face=Body.Faces[CylinderFace];const NurbsSurface& Cylinder=Face.Surface;
            if(!Cylinder.Rational()||Cylinder.RadiusMajor<=Tol||std::fabs(Cylinder.Axis.Normalised().Dot(A))<1.0-ScalarCriteria::GeometricTolerance)return std::nullopt;
            std::vector<double>RingT;
            for(int Loop:Face.Loops){if(Loop<0||Loop>=static_cast<int>(Body.Loops.size()))return std::nullopt;
                for(int Coedge:Body.Loops[Loop].Coedges){if(Coedge<0||Coedge>=static_cast<int>(Body.Coedges.size()))return std::nullopt;
                    int Edge=Body.Coedges[Coedge].Edge;if(Edge<0||Edge>=static_cast<int>(Body.Edges.size()))return std::nullopt;
                    const BrepEdge& E=Body.Edges[Edge];if(!E.Closed())continue;double T=(E.Curve.Sample(E.Curve.DomainStart())-Frame->Corner).Dot(A);
                    for(int K=1;K<=4;++K)if(std::fabs((E.Curve.Sample(E.Curve.DomainStart()+(E.Curve.DomainEnd()-E.Curve.DomainStart())*K/4.0)-Frame->Corner).Dot(A)-T)>PositionTolerance)return std::nullopt;
                    RingT.push_back(T);}}
            if(RingT.size()!=2)return std::nullopt;
            std::sort(RingT.begin(),RingT.end());Vec3 Offset=Cylinder.Origin-Frame->Corner;
            Spans.push_back({{Offset.Dot(Frame->Y),Offset.Dot(Frame->Z),Cylinder.RadiusMajor},RingT[0],RingT[1]});
        }
        std::vector<int>Entries;
        for(size_t I=0;I<Spans.size();++I)
        {
            bool Low=std::fabs(Spans[I].Low)<=PositionTolerance,High=std::fabs(Spans[I].High-LA)<=PositionTolerance;
            if(Low!=High)Entries.push_back(static_cast<int>(I));
        }
        if(Entries.size()!=2)return std::nullopt;
        std::vector<bool>Used(Spans.size(),false);std::vector<TwoStageBlindCavity>Cavities;double Removed=0.0;
        for(int Entry:Entries)
        {
            if(Used[Entry])return std::nullopt;
            const Span& Outer=Spans[Entry];
            bool FromLow=std::fabs(Outer.Low)<=PositionTolerance;double Boundary=FromLow?Outer.High:Outer.Low;int InnerIndex=-1;
            for(size_t I=0;I<Spans.size();++I)if(!Used[I]&&static_cast<int>(I)!=Entry)
            {
                const Span& Candidate=Spans[I];double CandidateBoundary=FromLow?Candidate.Low:Candidate.High;
                if(std::fabs(CandidateBoundary-Boundary)>PositionTolerance||
                   std::hypot(Candidate.Hole.B-Outer.Hole.B,Candidate.Hole.C-Outer.Hole.C)>PositionTolerance)continue;
                if(InnerIndex>=0)return std::nullopt;
                InnerIndex=static_cast<int>(I);
            }
            if(InnerIndex<0)return std::nullopt;
            const Span& Inner=Spans[InnerIndex];
            double ShoulderDepth=FromLow?Outer.High:LA-Outer.Low,TotalDepth=FromLow?Inner.High:LA-Inner.Low;
            if(Outer.Hole.Radius<=Inner.Hole.Radius+Tol||ShoulderDepth<=Tol||TotalDepth<=ShoulderDepth+Tol||TotalDepth>=LA-Tol)
                return std::nullopt;
            Used[Entry]=true;Used[InnerIndex]=true;
            Cavities.push_back({{{Outer.Hole,ShoulderDepth},{Inner.Hole,TotalDepth}},FromLow});
            Removed+=ScalarCriteria::Pi*(Outer.Hole.Radius*Outer.Hole.Radius*ShoulderDepth+
                Inner.Hole.Radius*Inner.Hole.Radius*(TotalDepth-ShoulderDepth));
        }
        if(std::find(Used.begin(),Used.end(),false)!=Used.end())return std::nullopt;
        std::sort(Cavities.begin(),Cavities.end(),[](const TwoStageBlindCavity& X,const TwoStageBlindCavity& Y)
        {
            if(X.FromLow!=Y.FromLow)return X.FromLow>Y.FromLow;
            if(X.Stages[0].Hole.B!=Y.Stages[0].Hole.B)return X.Stages[0].Hole.B<Y.Stages[0].Hole.B;
            return X.Stages[0].Hole.C<Y.Stages[0].Hole.C;
        });
        double Exact=LA*Frame->LY*Frame->LZ-Removed;
        if(!ScalarCriteria::WithinVolumeTolerance(Report.Volume, Exact))return std::nullopt;
        return DualSteppedBlindPrism{*Frame,std::move(Cavities)};
    }

    Deliver<BrepBody> BuildOrthogonalBoxCorner(const OrthogonalBoxCorner& Box, double Radius) noexcept
    {
        if (Radius <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "radius is zero or negative");
        if (Radius >= std::min({ Box.LX, Box.LY, Box.LZ }) - Tol)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                "corner fillet radius consumes one of the three selected box edges");
        auto P = [&](double X, double Y, double Z) noexcept
        { return Box.Corner + Box.X * X + Box.Y * Y + Box.Z * Z; };
        auto Arc = [&](Vec3 A, Vec3 M, Vec3 B) noexcept { return NurbsCurve::ArcThreePoints(A, M, B); };
        const double D = Radius / std::sqrt(2.0);
        const Vec3 SX=P(0,Radius,Radius), SY=P(Radius,0,Radius), SZ=P(Radius,Radius,0);
        const Vec3 XY=P(Box.LX,0,Radius), XZ=P(Box.LX,Radius,0), YX=P(0,Box.LY,Radius);
        const Vec3 YZ=P(Radius,Box.LY,0), ZX=P(0,Radius,Box.LZ), ZY=P(Radius,0,Box.LZ);
        const Vec3 CXY=P(Box.LX,Box.LY,0), CXZ=P(Box.LX,0,Box.LZ), CYZ=P(0,Box.LY,Box.LZ);
        const Vec3 CXYZ=P(Box.LX,Box.LY,Box.LZ);
        std::vector<Deliver<NurbsCurve>> C{
            Arc(SY,P(Radius,Radius-D,Radius-D),SZ), Arc(SX,P(Radius-D,Radius,Radius-D),SZ),
            Arc(SX,P(Radius-D,Radius-D,Radius),SY), NurbsCurve::Line(SY,XY), NurbsCurve::Line(SZ,XZ),
            Arc(XY,P(Box.LX,Radius-D,Radius-D),XZ), NurbsCurve::Line(SX,YX), NurbsCurve::Line(SZ,YZ),
            Arc(YX,P(Radius-D,Box.LY,Radius-D),YZ), NurbsCurve::Line(SX,ZX), NurbsCurve::Line(SY,ZY),
            Arc(ZX,P(Radius-D,Radius-D,Box.LZ),ZY), NurbsCurve::Line(XZ,CXY), NurbsCurve::Line(YZ,CXY),
            NurbsCurve::Line(XY,CXZ), NurbsCurve::Line(ZY,CXZ), NurbsCurve::Line(YX,CYZ), NurbsCurve::Line(ZX,CYZ),
            NurbsCurve::Line(CXY,CXYZ), NurbsCurve::Line(CXZ,CXYZ), NurbsCurve::Line(CYZ,CXYZ)
        };
        for (const auto& Curve : C) if (!Curve) return Deliver<BrepBody>::Reject(Curve.Denial.Reason, Curve.Denial.Detail);
        BrepBody Result; std::vector<int> E;
        for (auto& Curve : C) E.push_back(Result.AddEdge(std::move(Curve.Payload), ScalarCriteria::MergeTolerance));
        auto Face = [&](NurbsSurface Surface, std::initializer_list<std::pair<int,bool>> Boundary)
        {
            int F=Result.AddFace(std::move(Surface)); Result.Faces[F].Natural=false; int L=Result.AddLoop(F,true);
            for (auto [Edge,Reverse] : Boundary) Result.AddCoedge(E[Edge],Reverse,F,L);
        };
        auto Plane = [&](Vec3 O, Vec3 U, Vec3 V, double A, double B)
        { return NurbsSurface::Plane(O,U,V,A,B); };
        std::vector<Deliver<NurbsSurface>> Q{
            Plane(Box.Corner,Box.Y,Box.Z,Box.LY,Box.LZ), Plane(Box.Corner,Box.X,Box.Z,Box.LX,Box.LZ),
            Plane(Box.Corner,Box.X,Box.Y,Box.LX,Box.LY), Plane(P(Box.LX,0,0),Box.Y,Box.Z,Box.LY,Box.LZ),
            Plane(P(0,Box.LY,0),Box.X,Box.Z,Box.LX,Box.LZ), Plane(P(0,0,Box.LZ),Box.X,Box.Y,Box.LX,Box.LY)
        };
        for (const auto& Surface : Q) if (!Surface) return Deliver<BrepBody>::Reject(Surface.Denial.Reason,Surface.Denial.Detail);
        Face(std::move(Q[0].Payload),{{6,false},{16,false},{17,true},{9,true}});
        Face(std::move(Q[1].Payload),{{3,false},{14,false},{15,true},{10,true}});
        Face(std::move(Q[2].Payload),{{4,false},{12,false},{13,true},{7,true}});
        Face(std::move(Q[3].Payload),{{5,false},{12,false},{18,false},{19,true},{14,true}});
        Face(std::move(Q[4].Payload),{{8,false},{13,false},{18,false},{20,true},{16,true}});
        Face(std::move(Q[5].Payload),{{11,false},{15,false},{19,false},{20,true},{17,true}});
        auto SectionX=Arc(SY,P(Radius,Radius-D,Radius-D),SZ);
        auto SectionY=Arc(SX,P(Radius-D,Radius,Radius-D),SZ);
        auto SectionZ=Arc(SX,P(Radius-D,Radius-D,Radius),SY);
        auto RX=SectionX?NurbsSurface::Extrusion(SectionX.Payload,Box.X,Box.LX-Radius):Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput,"corner section");
        auto RY=SectionY?NurbsSurface::Extrusion(SectionY.Payload,Box.Y,Box.LY-Radius):Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput,"corner section");
        auto RZ=SectionZ?NurbsSurface::Extrusion(SectionZ.Payload,Box.Z,Box.LZ-Radius):Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput,"corner section");
        if(!RX||!RY||!RZ) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,"orthogonal corner roll is degenerate");
        auto Cylinder=[&](NurbsSurface& S,Vec3 Axis){S.Classification=SurfaceClassification::Cylinder;S.Origin=P(Radius,Radius,Radius);S.Axis=Axis;S.RadiusMajor=S.RadiusMinor=Radius;};
        Cylinder(RX.Payload,Box.X); Cylinder(RY.Payload,Box.Y); Cylinder(RZ.Payload,Box.Z);
        Face(std::move(RX.Payload),{{0,false},{4,false},{5,true},{3,true}});
        Face(std::move(RY.Payload),{{1,false},{7,false},{8,true},{6,true}});
        Face(std::move(RZ.Payload),{{2,false},{10,false},{11,true},{9,true}});
        auto Meridian=Arc(SX,P(Radius-D,Radius,Radius-D),SZ);
        auto Sphere=Meridian?NurbsSurface::Revolution(Meridian.Payload,P(Radius,Radius,Radius),Box.Z,ScalarCriteria::Pi*0.5):Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput,"corner sphere");
        if(!Sphere) return Deliver<BrepBody>::Reject(Sphere.Denial.Reason,Sphere.Denial.Detail);
        Sphere.Payload.Classification=SurfaceClassification::Sphere; Sphere.Payload.Origin=P(Radius,Radius,Radius);
        Sphere.Payload.Axis=Box.Z; Sphere.Payload.RadiusMajor=Sphere.Payload.RadiusMinor=Radius;
        Face(std::move(Sphere.Payload),{{2,false},{0,false},{1,true}});
        Result.Orient(); BodyReport Report=Result.Validate();
        if(!Report.Solid()||Report.Hulls!=1||Report.Genus!=0||Result.Vertices.size()!=13||Result.Edges.size()!=21||
           Result.Coedges.size()!=42||Result.Loops.size()!=10||Result.Faces.size()!=10)
            return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,"orthogonal three-face corner fillet did not reach exact manifold topology");
        return Deliver<BrepBody>::Accept(std::move(Result));
    }

    Deliver<BrepBody> BuildRoundedOrthogonalBox(const OrthogonalBoxCorner& Box, double Radius) noexcept
    {
        if (Radius <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "radius is zero or negative");
        if (2.0 * Radius >= std::min({ Box.LX, Box.LY, Box.LZ }) - Tol)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                "all-edge box fillet radius consumes an inset planar support");
        auto P = [&](double X, double Y, double Z) noexcept
        { return Box.Corner + Box.X * X + Box.Y * Y + Box.Z * Z; };
        std::vector<NurbsSurface> Surfaces;
        auto AddPlane = [&](Vec3 O, Vec3 U, Vec3 V, double A, double B)
        {
            Deliver<NurbsSurface> S = NurbsSurface::Plane(O, U, V, A, B);
            if (S) Surfaces.push_back(std::move(S.Payload));
            return static_cast<bool>(S);
        };
        if (!AddPlane(P(0,Radius,Radius),Box.Y,Box.Z,Box.LY-2*Radius,Box.LZ-2*Radius) ||
            !AddPlane(P(Box.LX,Radius,Radius),Box.Y,Box.Z,Box.LY-2*Radius,Box.LZ-2*Radius) ||
            !AddPlane(P(Radius,0,Radius),Box.X,Box.Z,Box.LX-2*Radius,Box.LZ-2*Radius) ||
            !AddPlane(P(Radius,Box.LY,Radius),Box.X,Box.Z,Box.LX-2*Radius,Box.LZ-2*Radius) ||
            !AddPlane(P(Radius,Radius,0),Box.X,Box.Y,Box.LX-2*Radius,Box.LY-2*Radius) ||
            !AddPlane(P(Radius,Radius,Box.LZ),Box.X,Box.Y,Box.LX-2*Radius,Box.LY-2*Radius))
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "rounded-box planar support is degenerate");

        const double D = Radius / std::sqrt(2.0);
        auto AddCylinder = [&](Vec3 StartCentre, Vec3 Axis, double Length, Vec3 A, Vec3 B)
        {
            Deliver<NurbsCurve> Section = NurbsCurve::ArcThreePoints(StartCentre + A * Radius,
                StartCentre + (A + B) * D, StartCentre + B * Radius);
            Deliver<NurbsSurface> Roll = Section
                ? NurbsSurface::Extrusion(Section.Payload, Axis, Length)
                : Deliver<NurbsSurface>::Reject(Section.Denial.Reason, Section.Denial.Detail);
            if (!Roll) return false;
            Roll.Payload.Classification=SurfaceClassification::Cylinder; Roll.Payload.Origin=StartCentre;
            Roll.Payload.Axis=Axis; Roll.Payload.RadiusMajor=Roll.Payload.RadiusMinor=Radius;
            Surfaces.push_back(std::move(Roll.Payload)); return true;
        };
        for (int Y=0;Y<2;++Y) for (int Z=0;Z<2;++Z)
        {
            double CY=Y?Box.LY-Radius:Radius, CZ=Z?Box.LZ-Radius:Radius;
            if(!AddCylinder(P(Radius,CY,CZ),Box.X,Box.LX-2*Radius,Y?Box.Y:Box.Y*-1.0,Z?Box.Z:Box.Z*-1.0))
                return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,"rounded-box X roll is degenerate");
        }
        for (int X=0;X<2;++X) for (int Z=0;Z<2;++Z)
        {
            double CX=X?Box.LX-Radius:Radius, CZ=Z?Box.LZ-Radius:Radius;
            if(!AddCylinder(P(CX,Radius,CZ),Box.Y,Box.LY-2*Radius,X?Box.X:Box.X*-1.0,Z?Box.Z:Box.Z*-1.0))
                return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,"rounded-box Y roll is degenerate");
        }
        for (int X=0;X<2;++X) for (int Y=0;Y<2;++Y)
        {
            double CX=X?Box.LX-Radius:Radius, CY=Y?Box.LY-Radius:Radius;
            if(!AddCylinder(P(CX,CY,Radius),Box.Z,Box.LZ-2*Radius,X?Box.X:Box.X*-1.0,Y?Box.Y:Box.Y*-1.0))
                return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,"rounded-box Z roll is degenerate");
        }
        for(int X=0;X<2;++X)for(int Y=0;Y<2;++Y)for(int Z=0;Z<2;++Z)
        {
            Vec3 SX=X?Box.X:Box.X*-1.0, SZ=Z?Box.Z:Box.Z*-1.0;
            Vec3 Centre=P(X?Box.LX-Radius:Radius,Y?Box.LY-Radius:Radius,Z?Box.LZ-Radius:Radius);
            Deliver<NurbsCurve> Meridian=NurbsCurve::ArcThreePoints(Centre+SX*Radius,Centre+(SX+SZ)*D,Centre+SZ*Radius);
            double Sweep=(X==Y?1.0:-1.0)*ScalarCriteria::Pi*0.5;
            Deliver<NurbsSurface> Patch=Meridian
                ? NurbsSurface::Revolution(Meridian.Payload,Centre,Box.Z,Sweep)
                : Deliver<NurbsSurface>::Reject(Meridian.Denial.Reason,Meridian.Denial.Detail);
            if(!Patch) return Deliver<BrepBody>::Reject(Patch.Denial.Reason,Patch.Denial.Detail);
            Patch.Payload.Classification=SurfaceClassification::Sphere; Patch.Payload.Origin=Centre; Patch.Payload.Axis=Box.Z;
            Patch.Payload.RadiusMajor=Patch.Payload.RadiusMinor=Radius; Surfaces.push_back(std::move(Patch.Payload));
        }
        Deliver<BrepBody> Result=BrepBody::Sew(Surfaces);
        if(!Result) return Result;
        BodyReport Report=Result.Payload.Validate();
        if(!Report.Solid()||Report.Hulls!=1||Report.Genus!=0||Result.Payload.Vertices.size()!=24||Result.Payload.Edges.size()!=48||
           Result.Payload.Coedges.size()!=96||Result.Payload.Loops.size()!=26||Result.Payload.Faces.size()!=26)
            return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,"all-edge rounded box did not reach exact manifold topology");
        return Result;
    }

    const char* PrismHoleWallRefusal(const PrismHole& Hole,double LB,double LC,double Radius) noexcept
    {
        if(Hole.Radius<=Tol)return "rounded-prism hole radius is not positive";
        if(Hole.Radius>=0.5*std::min(LB,LC)-Tol||Hole.B<=Hole.Radius+Tol||Hole.B>=LB-Hole.Radius-Tol||
           Hole.C<=Hole.Radius+Tol||Hole.C>=LC-Hole.Radius-Tol)return "rounded-prism hole consumes the radial wall";
        if(Hole.Radius<Radius)
        {
            double ClosestB=std::clamp(Hole.B,Radius,LB-Radius),ClosestC=std::clamp(Hole.C,Radius,LC-Radius);
            double OffsetDistance=Vec2{Hole.B-ClosestB,Hole.C-ClosestC}.Length();
            if(OffsetDistance>Tol&&OffsetDistance>=Radius-Hole.Radius-Tol)
                return "rounded-prism hole intersects the inward-offset wall";
        }
        return nullptr;
    }

    Deliver<BrepBody> BuildRoundedBoxPrism(const OrthogonalBoxCorner& Box, int Along, double Radius,
                                            const std::vector<PrismHole>& Holes={}) noexcept
    {
        Vec3 A=Along==0?Box.X:(Along==1?Box.Y:Box.Z);
        Vec3 B=Along==0?Box.Y:(Along==1?Box.X:Box.X);
        Vec3 C=Along==0?Box.Z:(Along==1?Box.Z:Box.Y);
        double LA=Along==0?Box.LX:(Along==1?Box.LY:Box.LZ);
        double LB=Along==0?Box.LY:Box.LX;
        double LC=Along==0?Box.LZ:(Along==1?Box.LZ:Box.LY);
        if(Radius<=Tol)return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,"radius is zero or negative");
        if(2.0*Radius>=std::min(LB,LC)-Tol)return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
            "parallel-edge fillet radius consumes the rounded-prism cross-section");
        if(Holes.size()>MaxPrismBores)return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
            "rounded-prism route supports at most eight through-holes");
        for(const PrismHole& Hole:Holes)if(const char* Denial=PrismHoleWallRefusal(Hole,LB,LC,Radius))
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,Denial);
        for(size_t I=0;I<Holes.size();++I)for(size_t J=I+1;J<Holes.size();++J)
            if(Vec2{Holes[I].B-Holes[J].B,Holes[I].C-Holes[J].C}.Length()<=Holes[I].Radius+Holes[J].Radius+Tol)
                return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                    "rounded-prism holes consume the inter-hole ligament");
        auto P=[&](double X,double Y,double Z){return Box.Corner+A*X+B*Y+C*Z;};
        std::vector<NurbsSurface>S;
        auto Plane=[&](Vec3 O,Vec3 U,Vec3 V,double X,double Y){auto Q=NurbsSurface::Plane(O,U,V,X,Y);if(Q)S.push_back(std::move(Q.Payload));return(bool)Q;};
        if(!Plane(P(0,0,Radius),A,C,LA,LC-2*Radius)||!Plane(P(0,LB,Radius),A,C,LA,LC-2*Radius)||
           !Plane(P(0,Radius,0),A,B,LA,LB-2*Radius)||!Plane(P(0,Radius,LC),A,B,LA,LB-2*Radius))
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,"rounded-prism planar support is degenerate");
        const double D=Radius/std::sqrt(2.0);
        for(int Y=0;Y<2;++Y)for(int Z=0;Z<2;++Z)
        {
            Vec3 Centre=P(0,Y?LB-Radius:Radius,Z?LC-Radius:Radius);
            Vec3 U=Y?B:B*-1.0,V=Z?C:C*-1.0;
            auto Arc=NurbsCurve::ArcThreePoints(Centre+U*Radius,Centre+(U+V)*D,Centre+V*Radius);
            auto Roll=Arc?NurbsSurface::Extrusion(Arc.Payload,A,LA):Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput,"prism arc");
            if(!Roll)return Deliver<BrepBody>::Reject(Roll.Denial.Reason,Roll.Denial.Detail);
            Roll.Payload.Classification=SurfaceClassification::Cylinder;Roll.Payload.Origin=Centre;Roll.Payload.Axis=A;
            Roll.Payload.RadiusMajor=Roll.Payload.RadiusMinor=Radius;S.push_back(std::move(Roll.Payload));
        }
        for(const PrismHole& Hole:Holes)
        {
            auto Bore=NurbsSurface::Cylinder(P(0,Hole.B,Hole.C),A,Hole.Radius,LA);
            if(!Bore)return Deliver<BrepBody>::Reject(Bore.Denial.Reason,Bore.Denial.Detail);
            S.push_back(Bore.Payload.Reversed());
        }
        auto Result=BrepBody::Sew(S);if(!Result)return Result;auto Report=Result.Payload.Validate();
        const size_t N=Holes.size();
        const bool Topology=Report.Genus==static_cast<int>(N)&&Result.Payload.Vertices.size()==16+2*N&&
            Result.Payload.Edges.size()==24+3*N&&Result.Payload.Coedges.size()==48+6*N&&
            Result.Payload.Loops.size()==10+3*N&&Result.Payload.Faces.size()==10+N;
        if(!Report.Solid()||Report.Hulls!=1||!Topology)
            return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,"parallel-edge rounded prism did not reach exact manifold topology");
        return Result;
    }

    Deliver<BrepBody> BuildRoundedBlindPrism(const BlindBoxPrism& Blind,double Radius) noexcept
    {
        size_t N=Blind.Cavities.size();
        if(N<1||N>MaxPrismBores)return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,"rounded-prism route supports at most eight blind cavities");
        for(const BlindCavity& Cavity:Blind.Cavities)
        {
            if(const char* Denial=PrismHoleWallRefusal(Cavity.Hole,Blind.Box.LY,Blind.Box.LZ,Radius))
                return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,Denial);
            if(Cavity.Depth<=Tol||Cavity.Depth>=Blind.Box.LX-Tol)
                return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,"blind bore depth reaches a prism end");
        }
        for(size_t I=0;I<N;++I)for(size_t J=I+1;J<N;++J)
        {
            const BlindCavity&A=Blind.Cavities[I],&B=Blind.Cavities[J];
            double ALow=A.FromLow?0.0:Blind.Box.LX-A.Depth,AHigh=A.FromLow?A.Depth:Blind.Box.LX;
            double BLow=B.FromLow?0.0:Blind.Box.LX-B.Depth,BHigh=B.FromLow?B.Depth:Blind.Box.LX;
            double AxialGap=std::max({0.0,ALow-BHigh,BLow-AHigh});
            double RadialGap=std::max(0.0,Vec2{A.Hole.B-B.Hole.B,A.Hole.C-B.Hole.C}.Length()-A.Hole.Radius-B.Hole.Radius);
            if(std::hypot(AxialGap,RadialGap)<=Tol)
                return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,"blind cavities consume the inter-cavity ligament");
        }
        auto Result=BuildRoundedBoxPrism(Blind.Box,0,Radius);if(!Result)return Result;
        auto P=[&](double X,double Y,double Z){return Blind.Box.Corner+Blind.Box.X*X+Blind.Box.Y*Y+Blind.Box.Z*Z;};
        double Margin=std::max(1.0,Blind.Box.LX*0.1);int EntranceEdges=0;
        for(const BlindCavity& Cavity:Blind.Cavities)
        {
            double Entry=Cavity.FromLow?0.0:Blind.Box.LX;Vec3 Direction=Cavity.FromLow?Blind.Box.X:Blind.Box.X*-1.0;
            auto Cutter=BrepBody::Cylinder(P(Cavity.FromLow?-Margin:Blind.Box.LX+Margin,Cavity.Hole.B,Cavity.Hole.C),
                                          Direction,Cavity.Hole.Radius,Margin+Cavity.Depth);
            if(!Cutter)return Deliver<BrepBody>::Reject(Cutter.Denial.Reason,Cutter.Denial.Detail);
            auto Next=IntersectionSolver::Combine(Result.Payload,Cutter.Payload,BodyOperation::Subtract);if(!Next)return Next;
            int Restored=0;Vec3 Centre=P(Entry,Cavity.Hole.B,Cavity.Hole.C);
            for(BrepEdge& Edge:Next.Payload.Edges)if(Edge.Closed())
            {
                Vec3 Sample=Edge.Curve.Sample(Edge.Curve.DomainStart());double T=(Sample-Blind.Box.Corner).Dot(Blind.Box.X);
                Vec3 Radial=Sample-Centre-Blind.Box.X*(Sample-Centre).Dot(Blind.Box.X);
                if(std::fabs(T-Entry)>ScalarCriteria::ScaledPositionTolerance*std::max(1.0,Blind.Box.LX)||!ScalarCriteria::WithinCircularTolerance(Radial.Length(), Cavity.Hole.Radius))continue;
                auto Circle=NurbsCurve::Circle(Centre,Direction,Cavity.Hole.Radius);
                if(!Circle||Edge.VertexStart<0||Circle.Payload.Sample(Circle.Payload.DomainStart()).Distance(Next.Payload.Vertices[Edge.VertexStart].Point)>ScalarCriteria::CircularTolerance)
                    return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,"blind bore entrance seam could not be restored exactly");
                Edge.Curve=std::move(Circle.Payload);++Restored;
            }
            if(Restored!=1)return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,"blind bore entrance could not be identified uniquely");
            EntranceEdges+=Restored;Result=std::move(Next);
        }
        auto Report=Result.Payload.Validate();int Planes=0,Cylinders=0,Circles=0,EndLoops=0;
        for(const BrepFace& Face:Result.Payload.Faces){Planes+=Face.Surface.Classification==SurfaceClassification::Plane;
            Cylinders+=Face.Surface.Classification==SurfaceClassification::Cylinder&&Face.Surface.Rational();
            if(Face.Surface.Classification==SurfaceClassification::Plane&&Face.Loops.size()>1)EndLoops+=static_cast<int>(Face.Loops.size()-1);}
        for(const BrepEdge& Edge:Result.Payload.Edges)Circles+=Edge.Closed()&&Edge.Curve.Classification==CurveClassification::Circle&&Edge.Curve.Rational();
        if(EntranceEdges!=static_cast<int>(N)||!Report.Solid()||Report.Hulls!=1||Report.Genus!=0||
           Planes!=static_cast<int>(6+N)||Cylinders!=static_cast<int>(4+N)||Circles!=static_cast<int>(2*N)||EndLoops!=static_cast<int>(N)||
           Result.Payload.Vertices.size()!=16+2*N||Result.Payload.Edges.size()!=24+3*N||Result.Payload.Coedges.size()!=48+6*N||
           Result.Payload.Loops.size()!=10+3*N||Result.Payload.Faces.size()!=10+2*N)
            return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,"blind-bore rounded prism did not reach exact manifold topology");
        return Result;
    }

    Deliver<BrepBody> BuildRoundedSideBlindPrism(const SideBlindPrism& Side,double Radius) noexcept
    {
        const size_t N=Side.Cavities.size();
        if(N<1||N>MaxPrismBores)return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,"rounded-prism route supports at most eight parallel side blind cavities");
        const int Along=Side.Cavities.front().Along;
        const double SideLength=Along==1?Side.Box.LY:Side.Box.LZ,StripLength=Along==1?Side.Box.LZ:Side.Box.LY;
        for(const SideBlindCavity& Cavity:Side.Cavities)
        {
            if(Cavity.Along!=Along)return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,"side blind cavities do not share one cross-section direction");
            if(Cavity.Radius<=Tol||Cavity.Depth<=Tol||Cavity.Depth>=SideLength-Tol)
                return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,"side blind-bore radius or depth consumes its support");
            if(Cavity.X<=Cavity.Radius+Tol||Cavity.X>=Side.Box.LX-Cavity.Radius-Tol||
               Cavity.Cross<=Radius+Cavity.Radius+Tol||Cavity.Cross>=StripLength-Radius-Cavity.Radius-Tol)
                return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,"side blind bore leaves the retained rounded-prism wall");
        }
        for(size_t I=0;I<N;++I)for(size_t J=I+1;J<N;++J)
        {
            const SideBlindCavity&A=Side.Cavities[I],&B=Side.Cavities[J];
            double ALow=A.FromLow?0.0:SideLength-A.Depth,AHigh=A.FromLow?A.Depth:SideLength;
            double BLow=B.FromLow?0.0:SideLength-B.Depth,BHigh=B.FromLow?B.Depth:SideLength;
            double AxialGap=std::max({0.0,ALow-BHigh,BLow-AHigh});
            double RadialGap=std::max(0.0,Vec2{A.X-B.X,A.Cross-B.Cross}.Length()-A.Radius-B.Radius);
            if(std::hypot(AxialGap,RadialGap)<=Tol)
                return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,"side blind cavities consume the inter-cavity ligament");
        }
        auto Result=BuildRoundedBoxPrism(Side.Box,0,Radius);if(!Result)return Result;
        auto P=[&](double X,double Y,double Z){return Side.Box.Corner+Side.Box.X*X+Side.Box.Y*Y+Side.Box.Z*Z;};
        Vec3 Axis=Along==1?Side.Box.Y:Side.Box.Z;double Margin=std::max(1.0,SideLength*0.1);int RestoredEdges=0;
        for(const SideBlindCavity& Cavity:Side.Cavities)
        {
            Vec3 FloorDirection=Cavity.FromLow?Axis:Axis*-1.0;double Entry=Cavity.FromLow?0.0:SideLength;
            double CutterStart=Cavity.FromLow?-Margin:SideLength-Cavity.Depth;
            Vec3 Origin=Along==1?P(Cavity.X,CutterStart,Cavity.Cross):P(Cavity.X,Cavity.Cross,CutterStart);
            auto Cutter=BrepBody::Cylinder(Origin,Axis,Cavity.Radius,Margin+Cavity.Depth);
            if(!Cutter)return Deliver<BrepBody>::Reject(Cutter.Denial.Reason,Cutter.Denial.Detail);
            auto Next=IntersectionSolver::Combine(Result.Payload,Cutter.Payload,BodyOperation::Subtract);if(!Next)return Next;
            Vec3 Centre=Along==1?P(Cavity.X,Entry,Cavity.Cross):P(Cavity.X,Cavity.Cross,Entry),FloorCentre=Centre+FloorDirection*Cavity.Depth;
            int Restored=0;
            for(BrepEdge& Edge:Next.Payload.Edges)if(Edge.Closed())
            {
                Vec3 Sample=Edge.Curve.Sample(Edge.Curve.DomainStart());double T=(Sample-Side.Box.Corner).Dot(Axis);
                bool AtEntry=std::fabs(T-Entry)<=ScalarCriteria::ScaledPositionTolerance*std::max(1.0,SideLength);
                Vec3 Target=AtEntry?Centre:FloorCentre,Radial=Sample-Target-Axis*(Sample-Target).Dot(Axis);
                if((!AtEntry&&std::fabs((Sample-FloorCentre).Dot(Axis))>ScalarCriteria::ScaledPositionTolerance*std::max(1.0,SideLength))||
                   !ScalarCriteria::WithinCircularTolerance(Radial.Length(), Cavity.Radius))continue;
                auto Circle=NurbsCurve::Circle(Target,Axis,Cavity.Radius);
                if(Edge.VertexStart<0)return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,"side blind-bore rim has no start vertex");
                if(!Circle||Circle.Payload.Sample(Circle.Payload.DomainStart()).Distance(Next.Payload.Vertices[Edge.VertexStart].Point)>ScalarCriteria::CircularTolerance)
                    Circle=NurbsCurve::Circle(Target,Axis*-1.0,Cavity.Radius);
                if(!Circle||Circle.Payload.Sample(Circle.Payload.DomainStart()).Distance(Next.Payload.Vertices[Edge.VertexStart].Point)>ScalarCriteria::CircularTolerance)
                    return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,"side blind-bore rim could not be restored exactly");
                Edge.Curve=std::move(Circle.Payload);++Restored;
            }
            if(Restored!=2)return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,"side blind-bore rims could not be identified uniquely");
            RestoredEdges+=Restored;Result=std::move(Next);
        }
        auto Report=Result.Payload.Validate();int Planes=0,Cylinders=0,Circles=0,InnerLoops=0;
        for(const BrepFace& Face:Result.Payload.Faces){Planes+=Face.Surface.Classification==SurfaceClassification::Plane;
            Cylinders+=Face.Surface.Classification==SurfaceClassification::Cylinder&&Face.Surface.Rational();
            if(Face.Surface.Classification==SurfaceClassification::Plane&&Face.Loops.size()>1)InnerLoops+=static_cast<int>(Face.Loops.size()-1);}
        for(const BrepEdge& Edge:Result.Payload.Edges)Circles+=Edge.Closed()&&Edge.Curve.Classification==CurveClassification::Circle&&Edge.Curve.Rational();
        if(RestoredEdges!=static_cast<int>(2*N)||!Report.Solid()||Report.Hulls!=1||Report.Genus!=0||
           Planes!=static_cast<int>(6+N)||Cylinders!=static_cast<int>(4+N)||Circles!=static_cast<int>(2*N)||InnerLoops!=static_cast<int>(N)||
           Result.Payload.Vertices.size()!=16+2*N||Result.Payload.Edges.size()!=24+3*N||Result.Payload.Coedges.size()!=48+6*N||
           Result.Payload.Loops.size()!=10+3*N||Result.Payload.Faces.size()!=10+2*N)
            return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,"side blind-bore rounded prism did not reach exact manifold topology");
        return Result;
    }

    Deliver<BrepBody> BuildRoundedSideSteppedBlindPrism(const SideSteppedBlindPrism& Step,double Radius) noexcept
    {
        size_t N=Step.Stages.size();
        if(N<2||N>MaxPrismBores)return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,"side stepped route supports two through eight diameters");
        double SideLength=Step.Along==1?Step.Box.LY:Step.Box.LZ,StripLength=Step.Along==1?Step.Box.LZ:Step.Box.LY;
        const SideSteppedBlindStage& Outer=Step.Stages.front();double PreviousRadius=std::numeric_limits<double>::infinity(),PreviousDepth=0.0;
        for(const SideSteppedBlindStage& Stage:Step.Stages)
        {
            if(Stage.Radius<=Tol||Stage.Radius>=PreviousRadius-Tol||Stage.Depth<=PreviousDepth+Tol||Stage.Depth>=SideLength-Tol)
                return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,"side stepped blind bore consumes a radial or axial shoulder");
            PreviousRadius=Stage.Radius;PreviousDepth=Stage.Depth;
        }
        if(Step.X<=Outer.Radius+Tol||Step.X>=Step.Box.LX-Outer.Radius-Tol||
           Step.Cross<=Radius+Outer.Radius+Tol||Step.Cross>=StripLength-Radius-Outer.Radius-Tol)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,"side stepped blind bore leaves the retained rounded-prism wall");
        auto Result=BuildRoundedBoxPrism(Step.Box,0,Radius);if(!Result)return Result;
        auto P=[&](double X,double Y,double Z){return Step.Box.Corner+Step.Box.X*X+Step.Box.Y*Y+Step.Box.Z*Z;};
        Vec3 Axis=Step.Along==1?Step.Box.Y:Step.Box.Z;double Margin=std::max(1.0,SideLength*0.1),Entry=Step.FromLow?0.0:SideLength;
        for(const SideSteppedBlindStage& Stage:Step.Stages)
        {
            double CutterStart=Step.FromLow?-Margin:SideLength-Stage.Depth;
            Vec3 Origin=Step.Along==1?P(Step.X,CutterStart,Step.Cross):P(Step.X,Step.Cross,CutterStart);
            auto Cutter=BrepBody::Cylinder(Origin,Axis,Stage.Radius,Margin+Stage.Depth);
            if(!Cutter)return Deliver<BrepBody>::Reject(Cutter.Denial.Reason,Cutter.Denial.Detail);
            auto Next=IntersectionSolver::Combine(Result.Payload,Cutter.Payload,BodyOperation::Subtract);if(!Next)return Next;
            Result=std::move(Next);
        }
        struct Ring{double T=0.0,Radius=0.0;};std::vector<Ring>Rings{{Entry,Outer.Radius}};Rings.reserve(2*N);
        for(size_t I=0;I<N;++I)
        {
            double T=Step.FromLow?Step.Stages[I].Depth:SideLength-Step.Stages[I].Depth;
            Rings.push_back({T,Step.Stages[I].Radius});
            if(I+1<N)Rings.push_back({T,Step.Stages[I+1].Radius});
        }
        int Restored=0;double PositionTolerance=ScalarCriteria::ScaledPositionTolerance*std::max(1.0,SideLength);
        for(const Ring& Expected:Rings)
        {
            int Matches=0;Vec3 Centre=Step.Along==1?P(Step.X,Expected.T,Step.Cross):P(Step.X,Step.Cross,Expected.T);
            for(BrepEdge& Edge:Result.Payload.Edges)if(Edge.Closed())
            {
                Vec3 Sample=Edge.Curve.Sample(Edge.Curve.DomainStart());double T=(Sample-Step.Box.Corner).Dot(Axis);
                Vec3 Radial=Sample-Centre-Axis*(Sample-Centre).Dot(Axis);
                if(std::fabs(T-Expected.T)>PositionTolerance||!ScalarCriteria::WithinCircularTolerance(Radial.Length(), Expected.Radius))continue;
                if(Edge.VertexStart<0)return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,"side stepped rim has no start vertex");
                auto Circle=NurbsCurve::Circle(Centre,Axis,Expected.Radius);
                if(!Circle||Circle.Payload.Sample(Circle.Payload.DomainStart()).Distance(Result.Payload.Vertices[Edge.VertexStart].Point)>ScalarCriteria::CircularTolerance)
                    Circle=NurbsCurve::Circle(Centre,Axis*-1.0,Expected.Radius);
                if(!Circle||Circle.Payload.Sample(Circle.Payload.DomainStart()).Distance(Result.Payload.Vertices[Edge.VertexStart].Point)>ScalarCriteria::CircularTolerance)
                    return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,"side stepped blind-bore rim could not be restored exactly");
                Edge.Curve=std::move(Circle.Payload);++Matches;
            }
            if(Matches!=1)return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,"side stepped blind-bore rim could not be identified uniquely");
            Restored+=Matches;
        }
        auto Report=Result.Payload.Validate();int Planes=0,Cylinders=0,Circles=0,InnerLoops=0;
        for(const BrepFace& Face:Result.Payload.Faces){Planes+=Face.Surface.Classification==SurfaceClassification::Plane;
            Cylinders+=Face.Surface.Classification==SurfaceClassification::Cylinder&&Face.Surface.Rational();
            if(Face.Surface.Classification==SurfaceClassification::Plane&&Face.Loops.size()>1)InnerLoops+=static_cast<int>(Face.Loops.size()-1);}
        for(const BrepEdge& Edge:Result.Payload.Edges)Circles+=Edge.Closed()&&Edge.Curve.Classification==CurveClassification::Circle&&Edge.Curve.Rational();
        if(Restored!=static_cast<int>(2*N)||!Report.Solid()||Report.Hulls!=1||Report.Genus!=0||
           Planes!=static_cast<int>(6+N)||Cylinders!=static_cast<int>(4+N)||Circles!=static_cast<int>(2*N)||InnerLoops!=static_cast<int>(N)||
           Result.Payload.Vertices.size()!=16+2*N||Result.Payload.Edges.size()!=24+3*N||Result.Payload.Coedges.size()!=48+6*N||
           Result.Payload.Loops.size()!=10+3*N||Result.Payload.Faces.size()!=10+2*N)
            return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,"side stepped rounded prism did not reach exact manifold topology");
        return Result;
    }

    Deliver<BrepBody> BuildRoundedSideSteppedBlindSet(const SideSteppedBlindSet& Set,double Radius) noexcept
    {
        size_t N=Set.Cavities.size(),TotalStages=0;
        if(N<2||N>MaxPrismBores)
            return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,"side stepped route supports two through eight cavities");
        int Along=Set.Cavities.front().Along;double SideLength=Along==1?Set.Box.LY:Set.Box.LZ;
        double StripLength=Along==1?Set.Box.LZ:Set.Box.LY;
        for(const SideSteppedBlindCavity& Cavity:Set.Cavities)
        {
            if(Cavity.Along!=Along||Cavity.Stages.size()<2||Cavity.Stages.size()>MaxPrismBores)
                return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,"side stepped cavities must share one direction and contain two through eight stages each");
            TotalStages+=Cavity.Stages.size();double PreviousRadius=std::numeric_limits<double>::infinity(),PreviousDepth=0.0;
            for(const SideSteppedBlindStage& Stage:Cavity.Stages)
            {
                if(Stage.Radius<=Tol||Stage.Radius>=PreviousRadius-Tol||Stage.Depth<=PreviousDepth+Tol||Stage.Depth>=SideLength-Tol)
                    return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,"side stepped cavity consumes a radial or axial shoulder");
                PreviousRadius=Stage.Radius;PreviousDepth=Stage.Depth;
            }
            const SideSteppedBlindStage& Outer=Cavity.Stages.front();
            if(Cavity.X<=Outer.Radius+Tol||Cavity.X>=Set.Box.LX-Outer.Radius-Tol||
               Cavity.Cross<=Radius+Outer.Radius+Tol||Cavity.Cross>=StripLength-Radius-Outer.Radius-Tol)
                return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,"side stepped cavity leaves the retained rounded-prism wall");
        }
        if(TotalStages>2*MaxPrismBores)
            return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,"side stepped set exceeds the sixteen-stage construction budget");
        struct Band{double Low=0.0,High=0.0,Radius=0.0,X=0.0,Cross=0.0;};std::vector<std::vector<Band>>Bands(N);
        for(size_t C=0;C<N;++C)
        {
            double Previous=0.0;
            for(const SideSteppedBlindStage& Stage:Set.Cavities[C].Stages)
            {
                double Low=Set.Cavities[C].FromLow?Previous:SideLength-Stage.Depth;
                double High=Set.Cavities[C].FromLow?Stage.Depth:SideLength-Previous;
                Bands[C].push_back({Low,High,Stage.Radius,Set.Cavities[C].X,Set.Cavities[C].Cross});Previous=Stage.Depth;
            }
        }
        for(size_t I=0;I<N;++I)for(size_t J=I+1;J<N;++J)for(const Band& A:Bands[I])for(const Band& B:Bands[J])
        {
            double AxialGap=std::max({0.0,A.Low-B.High,B.Low-A.High});
            double RadialGap=std::max(0.0,std::hypot(A.X-B.X,A.Cross-B.Cross)-A.Radius-B.Radius);
            if(std::hypot(AxialGap,RadialGap)<=Tol)
                return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,"side stepped cavities consume the inter-cavity ligament");
        }
        auto Result=BuildRoundedBoxPrism(Set.Box,0,Radius);if(!Result)return Result;
        auto P=[&](double X,double Y,double Z){return Set.Box.Corner+Set.Box.X*X+Set.Box.Y*Y+Set.Box.Z*Z;};
        Vec3 Axis=Along==1?Set.Box.Y:Set.Box.Z;double Margin=std::max(1.0,SideLength*0.1);
        for(const SideSteppedBlindCavity& Cavity:Set.Cavities)for(const SideSteppedBlindStage& Stage:Cavity.Stages)
        {
            double CutterStart=Cavity.FromLow?-Margin:SideLength-Stage.Depth;
            Vec3 Origin=Along==1?P(Cavity.X,CutterStart,Cavity.Cross):P(Cavity.X,Cavity.Cross,CutterStart);
            auto Cutter=BrepBody::Cylinder(Origin,Axis,Stage.Radius,Margin+Stage.Depth);
            if(!Cutter)return Deliver<BrepBody>::Reject(Cutter.Denial.Reason,Cutter.Denial.Detail);
            auto Next=IntersectionSolver::Combine(Result.Payload,Cutter.Payload,BodyOperation::Subtract);if(!Next)return Next;
            Result=std::move(Next);
        }
        struct Ring{double T=0.0,Radius=0.0,X=0.0,Cross=0.0;};std::vector<Ring>Rings;Rings.reserve(2*TotalStages);
        for(const SideSteppedBlindCavity& Cavity:Set.Cavities)
        {
            const SideSteppedBlindStage& Outer=Cavity.Stages.front();Rings.push_back({Cavity.FromLow?0.0:SideLength,Outer.Radius,Cavity.X,Cavity.Cross});
            for(size_t I=0;I<Cavity.Stages.size();++I)
            {
                double T=Cavity.FromLow?Cavity.Stages[I].Depth:SideLength-Cavity.Stages[I].Depth;
                Rings.push_back({T,Cavity.Stages[I].Radius,Cavity.X,Cavity.Cross});
                if(I+1<Cavity.Stages.size())Rings.push_back({T,Cavity.Stages[I+1].Radius,Cavity.X,Cavity.Cross});
            }
        }
        int Restored=0;double PositionTolerance=ScalarCriteria::ScaledPositionTolerance*std::max(1.0,SideLength);
        for(const Ring& Expected:Rings)
        {
            int Matches=0;Vec3 Centre=Along==1?P(Expected.X,Expected.T,Expected.Cross):P(Expected.X,Expected.Cross,Expected.T);
            for(BrepEdge& Edge:Result.Payload.Edges)if(Edge.Closed())
            {
                Vec3 Sample=Edge.Curve.Sample(Edge.Curve.DomainStart());double T=(Sample-Set.Box.Corner).Dot(Axis);
                Vec3 Radial=Sample-Centre-Axis*(Sample-Centre).Dot(Axis);
                if(std::fabs(T-Expected.T)>PositionTolerance||!ScalarCriteria::WithinCircularTolerance(Radial.Length(), Expected.Radius))continue;
                if(Edge.VertexStart<0)return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,"side stepped set rim has no start vertex");
                auto Circle=NurbsCurve::Circle(Centre,Axis,Expected.Radius);
                if(!Circle||Circle.Payload.Sample(Circle.Payload.DomainStart()).Distance(Result.Payload.Vertices[Edge.VertexStart].Point)>ScalarCriteria::CircularTolerance)
                    Circle=NurbsCurve::Circle(Centre,Axis*-1.0,Expected.Radius);
                if(!Circle||Circle.Payload.Sample(Circle.Payload.DomainStart()).Distance(Result.Payload.Vertices[Edge.VertexStart].Point)>ScalarCriteria::CircularTolerance)
                    return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,"side stepped set rim could not be restored exactly");
                Edge.Curve=std::move(Circle.Payload);++Matches;
            }
            if(Matches!=1)return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,"side stepped set rim could not be identified uniquely");
            Restored+=Matches;
        }
        auto Report=Result.Payload.Validate();int Planes=0,Cylinders=0,Circles=0,InnerLoops=0;
        for(const BrepFace& Face:Result.Payload.Faces){Planes+=Face.Surface.Classification==SurfaceClassification::Plane;
            Cylinders+=Face.Surface.Classification==SurfaceClassification::Cylinder&&Face.Surface.Rational();
            if(Face.Surface.Classification==SurfaceClassification::Plane&&Face.Loops.size()>1)InnerLoops+=static_cast<int>(Face.Loops.size()-1);}
        for(const BrepEdge& Edge:Result.Payload.Edges)Circles+=Edge.Closed()&&Edge.Curve.Classification==CurveClassification::Circle&&Edge.Curve.Rational();
        if(Restored!=static_cast<int>(2*TotalStages)||!Report.Solid()||Report.Hulls!=1||Report.Genus!=0||
           Planes!=static_cast<int>(6+TotalStages)||Cylinders!=static_cast<int>(4+TotalStages)||
           Circles!=static_cast<int>(2*TotalStages)||InnerLoops!=static_cast<int>(TotalStages)||
           Result.Payload.Vertices.size()!=16+2*TotalStages||Result.Payload.Edges.size()!=24+3*TotalStages||
           Result.Payload.Coedges.size()!=48+6*TotalStages||Result.Payload.Loops.size()!=10+3*TotalStages||
           Result.Payload.Faces.size()!=10+2*TotalStages)
            return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,"side stepped set did not reach exact rounded-prism topology");
        return Result;
    }

    Deliver<BrepBody> BuildRoundedSteppedBlindPrism(const SteppedBlindPrism& Step,double Radius) noexcept
    {
        size_t N=Step.Stages.size();
        if(N<2||N>MaxPrismBores)return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
            "rounded-prism route supports at most eight coaxial blind-bore stages");
        const PrismHole& Outer=Step.Stages.front().Hole;
        if(const char* Denial=PrismHoleWallRefusal(Outer,Step.Box.LY,Step.Box.LZ,Radius))
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,Denial);
        double PreviousRadius=std::numeric_limits<double>::infinity(),PreviousDepth=0.0,Scale=std::max(1.0,Step.Box.LX);
        for(const SteppedBlindStage& Stage:Step.Stages)
        {
            if(Stage.Hole.Radius<=Tol||Stage.Hole.Radius>=PreviousRadius-Tol||Stage.Depth<=PreviousDepth+Tol||
               Stage.Depth>=Step.Box.LX-Tol||std::hypot(Stage.Hole.B-Outer.B,Stage.Hole.C-Outer.C)>ScalarCriteria::GeometricTolerance*Scale)
                return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,"stepped blind bore consumes or misaligns a radial or axial shoulder");
            PreviousRadius=Stage.Hole.Radius;PreviousDepth=Stage.Depth;
        }
        auto Result=BuildRoundedBoxPrism(Step.Box,0,Radius);if(!Result)return Result;
        auto P=[&](double X,double Y,double Z){return Step.Box.Corner+Step.Box.X*X+Step.Box.Y*Y+Step.Box.Z*Z;};
        double Margin=std::max(1.0,Step.Box.LX*0.1),Entry=Step.FromLow?0.0:Step.Box.LX;
        Vec3 Direction=Step.FromLow?Step.Box.X:Step.Box.X*-1.0;
        for(const SteppedBlindStage& Stage:Step.Stages)
        {
            auto Cutter=BrepBody::Cylinder(P(Step.FromLow?-Margin:Step.Box.LX+Margin,Outer.B,Outer.C),
                                           Direction,Stage.Hole.Radius,Margin+Stage.Depth);
            if(!Cutter)return Deliver<BrepBody>::Reject(Cutter.Denial.Reason,Cutter.Denial.Detail);
            auto Next=IntersectionSolver::Combine(Result.Payload,Cutter.Payload,BodyOperation::Subtract);if(!Next)return Next;
            Result=std::move(Next);
        }
        struct Ring{double T=0.0,Radius=0.0;};std::vector<Ring>Rings;Rings.reserve(2*N);
        Rings.push_back({Entry,Outer.Radius});
        for(size_t I=0;I<N;++I)
        {
            double T=Step.FromLow?Step.Stages[I].Depth:Step.Box.LX-Step.Stages[I].Depth;
            Rings.push_back({T,Step.Stages[I].Hole.Radius});
            if(I+1<N)Rings.push_back({T,Step.Stages[I+1].Hole.Radius});
        }
        int Restored=0;
        for(const Ring& Expected:Rings)
        {
            int Matches=0;Vec3 Centre=P(Expected.T,Outer.B,Outer.C);
            for(BrepEdge& Edge:Result.Payload.Edges)if(Edge.Closed())
            {
                Vec3 Sample=Edge.Curve.Sample(Edge.Curve.DomainStart());double T=(Sample-Step.Box.Corner).Dot(Step.Box.X);
                Vec3 Radial=Sample-Centre-Step.Box.X*(Sample-Centre).Dot(Step.Box.X);
                if(std::fabs(T-Expected.T)>ScalarCriteria::ScaledPositionTolerance*Scale||!ScalarCriteria::WithinCircularTolerance(Radial.Length(), Expected.Radius))continue;
                auto Circle=NurbsCurve::Circle(Centre,Direction,Expected.Radius);
                if(!Circle||Edge.VertexStart<0||Circle.Payload.Sample(Circle.Payload.DomainStart()).Distance(Result.Payload.Vertices[Edge.VertexStart].Point)>ScalarCriteria::CircularTolerance)
                    return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,"stepped blind-bore rim could not be restored exactly");
                Edge.Curve=std::move(Circle.Payload);++Matches;
            }
            if(Matches!=1)return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,"stepped blind-bore rim could not be identified uniquely");
            Restored+=Matches;
        }
        auto Report=Result.Payload.Validate();int Planes=0,Cylinders=0,Circles=0,InnerLoops=0;
        for(const BrepFace& Face:Result.Payload.Faces){Planes+=Face.Surface.Classification==SurfaceClassification::Plane;
            Cylinders+=Face.Surface.Classification==SurfaceClassification::Cylinder&&Face.Surface.Rational();
            if(Face.Surface.Classification==SurfaceClassification::Plane&&Face.Loops.size()>1)InnerLoops+=static_cast<int>(Face.Loops.size()-1);}
        for(const BrepEdge& Edge:Result.Payload.Edges)Circles+=Edge.Closed()&&Edge.Curve.Classification==CurveClassification::Circle&&Edge.Curve.Rational();
        if(Restored!=static_cast<int>(2*N)||!Report.Solid()||Report.Hulls!=1||Report.Genus!=0||
           Planes!=static_cast<int>(6+N)||Cylinders!=static_cast<int>(4+N)||Circles!=static_cast<int>(2*N)||InnerLoops!=static_cast<int>(N)||
           Result.Payload.Vertices.size()!=16+2*N||Result.Payload.Edges.size()!=24+3*N||Result.Payload.Coedges.size()!=48+6*N||
           Result.Payload.Loops.size()!=10+3*N||Result.Payload.Faces.size()!=10+2*N)
            return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,"stepped blind-bore rounded prism did not reach exact manifold topology");
        return Result;
    }

    Deliver<BrepBody> BuildRoundedDualSteppedBlindPrism(const DualSteppedBlindPrism& Dual,double Radius) noexcept
    {
        if(Dual.Cavities.size()!=2||Dual.Cavities[0].Stages.size()!=2||Dual.Cavities[1].Stages.size()!=2)
            return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,"rounded-prism route requires exactly two two-stage blind cavities");
        for(const TwoStageBlindCavity& Cavity:Dual.Cavities)
        {
            const PrismHole& Outer=Cavity.Stages.front().Hole;
            if(const char* Denial=PrismHoleWallRefusal(Outer,Dual.Box.LY,Dual.Box.LZ,Radius))
                return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,Denial);
            if(Cavity.Stages[1].Hole.Radius<=Tol||Outer.Radius<=Cavity.Stages[1].Hole.Radius+Tol||
               Cavity.Stages[0].Depth<=Tol||Cavity.Stages[1].Depth<=Cavity.Stages[0].Depth+Tol||
               Cavity.Stages[1].Depth>=Dual.Box.LX-Tol)
                return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,"two-stage blind cavity consumes a radial or axial shoulder");
        }
        struct Band{double Low=0.0,High=0.0,Radius=0.0,B=0.0,C=0.0;};std::vector<std::vector<Band>>Bands(2);
        for(size_t C=0;C<2;++C)
        {
            double Previous=0.0;
            for(const SteppedBlindStage& Stage:Dual.Cavities[C].Stages)
            {
                double Low=Dual.Cavities[C].FromLow?Previous:Dual.Box.LX-Stage.Depth;
                double High=Dual.Cavities[C].FromLow?Stage.Depth:Dual.Box.LX-Previous;
                Bands[C].push_back({Low,High,Stage.Hole.Radius,Stage.Hole.B,Stage.Hole.C});Previous=Stage.Depth;
            }
        }
        for(const Band& A:Bands[0])for(const Band& B:Bands[1])
        {
            double AxialGap=std::max({0.0,A.Low-B.High,B.Low-A.High});
            double RadialGap=std::max(0.0,std::hypot(A.B-B.B,A.C-B.C)-A.Radius-B.Radius);
            if(std::hypot(AxialGap,RadialGap)<=Tol)
                return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,"stepped blind cavities consume the inter-cavity ligament");
        }
        auto Result=BuildRoundedBoxPrism(Dual.Box,0,Radius);if(!Result)return Result;
        auto P=[&](double X,double Y,double Z){return Dual.Box.Corner+Dual.Box.X*X+Dual.Box.Y*Y+Dual.Box.Z*Z;};
        double Margin=std::max(1.0,Dual.Box.LX*0.1);
        for(const TwoStageBlindCavity& Cavity:Dual.Cavities)
        {
            const PrismHole& Outer=Cavity.Stages.front().Hole;Vec3 Direction=Cavity.FromLow?Dual.Box.X:Dual.Box.X*-1.0;
            for(const SteppedBlindStage& Stage:Cavity.Stages)
            {
                auto Cutter=BrepBody::Cylinder(P(Cavity.FromLow?-Margin:Dual.Box.LX+Margin,Outer.B,Outer.C),
                                               Direction,Stage.Hole.Radius,Margin+Stage.Depth);
                if(!Cutter)return Deliver<BrepBody>::Reject(Cutter.Denial.Reason,Cutter.Denial.Detail);
                auto Next=IntersectionSolver::Combine(Result.Payload,Cutter.Payload,BodyOperation::Subtract);if(!Next)return Next;
                Result=std::move(Next);
            }
        }
        struct Ring{double T=0.0,Radius=0.0,B=0.0,C=0.0;bool FromLow=true;};std::vector<Ring>Rings;Rings.reserve(8);
        for(const TwoStageBlindCavity& Cavity:Dual.Cavities)
        {
            const PrismHole& Outer=Cavity.Stages.front().Hole;Rings.push_back({Cavity.FromLow?0.0:Dual.Box.LX,Outer.Radius,Outer.B,Outer.C,Cavity.FromLow});
            for(size_t I=0;I<2;++I)
            {
                double T=Cavity.FromLow?Cavity.Stages[I].Depth:Dual.Box.LX-Cavity.Stages[I].Depth;
                Rings.push_back({T,Cavity.Stages[I].Hole.Radius,Outer.B,Outer.C,Cavity.FromLow});
                if(I==0)Rings.push_back({T,Cavity.Stages[1].Hole.Radius,Outer.B,Outer.C,Cavity.FromLow});
            }
        }
        int Restored=0;double Scale=std::max(1.0,Dual.Box.LX);
        for(const Ring& Expected:Rings)
        {
            int Matches=0;Vec3 Centre=P(Expected.T,Expected.B,Expected.C);Vec3 Direction=Expected.FromLow?Dual.Box.X:Dual.Box.X*-1.0;
            for(BrepEdge& Edge:Result.Payload.Edges)if(Edge.Closed())
            {
                Vec3 Sample=Edge.Curve.Sample(Edge.Curve.DomainStart());double T=(Sample-Dual.Box.Corner).Dot(Dual.Box.X);
                Vec3 Radial=Sample-Centre-Dual.Box.X*(Sample-Centre).Dot(Dual.Box.X);
                if(std::fabs(T-Expected.T)>ScalarCriteria::ScaledPositionTolerance*Scale||!ScalarCriteria::WithinCircularTolerance(Radial.Length(), Expected.Radius))continue;
                auto Circle=NurbsCurve::Circle(Centre,Direction,Expected.Radius);
                if(!Circle||Edge.VertexStart<0||Circle.Payload.Sample(Circle.Payload.DomainStart()).Distance(Result.Payload.Vertices[Edge.VertexStart].Point)>ScalarCriteria::CircularTolerance)
                    return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,"dual stepped blind-bore rim could not be restored exactly");
                Edge.Curve=std::move(Circle.Payload);++Matches;
            }
            if(Matches!=1)return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,"dual stepped blind-bore rim could not be identified uniquely");
            Restored+=Matches;
        }
        auto Report=Result.Payload.Validate();int Planes=0,Cylinders=0,Circles=0,InnerLoops=0;
        for(const BrepFace& Face:Result.Payload.Faces){Planes+=Face.Surface.Classification==SurfaceClassification::Plane;
            Cylinders+=Face.Surface.Classification==SurfaceClassification::Cylinder&&Face.Surface.Rational();
            if(Face.Surface.Classification==SurfaceClassification::Plane&&Face.Loops.size()>1)InnerLoops+=static_cast<int>(Face.Loops.size()-1);}
        for(const BrepEdge& Edge:Result.Payload.Edges)Circles+=Edge.Closed()&&Edge.Curve.Classification==CurveClassification::Circle&&Edge.Curve.Rational();
        if(Restored!=8||!Report.Solid()||Report.Hulls!=1||Report.Genus!=0||Planes!=10||Cylinders!=8||Circles!=8||InnerLoops!=4||
           Result.Payload.Vertices.size()!=24||Result.Payload.Edges.size()!=36||Result.Payload.Coedges.size()!=72||
           Result.Payload.Loops.size()!=22||Result.Payload.Faces.size()!=18)
            return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,"dual stepped blind-bore rounded prism did not reach exact manifold topology");
        return Result;
    }

    std::optional<PlaneCylinderRoot> PlaneCylinderBossRoot(const BrepBody& Body, int Edge) noexcept
    {
        if (Edge < 0 || Edge >= static_cast<int>(Body.Edges.size()) || !Body.Validate().Solid()) return std::nullopt;
        Deliver<std::vector<int>> ChainResult = BlendSolver::TangentChain(Body, Edge);
        if (!ChainResult || ChainResult.Payload.empty()) return std::nullopt;
        const std::vector<int>& RootEdges = ChainResult.Payload;
        const size_t SegmentCount = RootEdges.size();

        // Closed rings have three angular support-patch bands plus two end caps. Finite roots add bottom/top sectors.
        // A half-turn closes through one planar diameter face; a general-angle sector has two radial caps sharing one
        // physical axis edge.
        const bool ClosedTopology = Body.Vertices.size() == 4 * SegmentCount &&
            Body.Edges.size() == 7 * SegmentCount && Body.Coedges.size() == 14 * SegmentCount &&
            Body.Loops.size() == 3 * SegmentCount + 2 && Body.Faces.size() == 3 * SegmentCount + 2;
        const bool SemicircleTopology = Body.Vertices.size() == 4 * SegmentCount + 6 &&
            Body.Edges.size() == 9 * SegmentCount + 5 && Body.Coedges.size() == 18 * SegmentCount + 10 &&
            Body.Loops.size() == 5 * SegmentCount + 1 && Body.Faces.size() == 5 * SegmentCount + 1;
        const bool SectorTopology = Body.Vertices.size() == 4 * SegmentCount + 6 &&
            Body.Edges.size() == 9 * SegmentCount + 6 && Body.Coedges.size() == 18 * SegmentCount + 12 &&
            Body.Loops.size() == 5 * SegmentCount + 2 && Body.Faces.size() == 5 * SegmentCount + 2;
        const bool OpenTopology = SemicircleTopology || SectorTopology;
        if (!ClosedTopology && !OpenTopology) return std::nullopt;

        auto Contains = [](const std::vector<int>& Values, int Value) noexcept
        { return std::find(Values.begin(), Values.end(), Value) != Values.end(); };
        auto AppendUnique = [&](std::vector<int>& Values, int Value) noexcept
        { if (!Contains(Values, Value)) Values.push_back(Value); };

        Vec3 RootCentre, Axis; double BossRadius = 0.0, BossHeight = 0.0;
        bool FirstRoot = true;
        std::vector<int> ShoulderFaces, BossFaces;
        for (int RootIndex : RootEdges)
        {
            const BrepEdge& RootEdge = Body.Edges[RootIndex];
            if (RootEdge.Coedges.size() != 2) return std::nullopt;
            Vec3 CandidateCentre, CandidateCircleNormal; double CandidateRadius = 0.0;
            if (!CircularFrame(RootEdge.Curve, CandidateCentre, CandidateCircleNormal, CandidateRadius)) return std::nullopt;

            int ShoulderFace = -1, BossFace = -1; Vec3 CandidateAxis;
            for (int Coedge : RootEdge.Coedges)
            {
                if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return std::nullopt;
                int Face = Body.Coedges[Coedge].Face;
                if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return std::nullopt;
                if (Body.Faces[Face].Surface.Classification == SurfaceClassification::Cylinder)
                {
                    if (BossFace >= 0) return std::nullopt;
                    BossFace = Face;
                }
                else
                {
                    Vec3 Normal;
                    if (ShoulderFace >= 0 || !PlanarNormal(Body, Face, Normal)) return std::nullopt;
                    ShoulderFace = Face; CandidateAxis = Normal.Normalised();
                }
            }
            if (ShoulderFace < 0 || BossFace < 0 || CandidateAxis.Length() <= Tol ||
                std::fabs(CandidateCircleNormal.Dot(CandidateAxis)) < 1.0 - ScalarCriteria::GeometricTolerance) return std::nullopt;

            Vec3 BossStart, BossEnd; double ClassifiedBossRadius = 0.0;
            if (!CylinderEndCentres(Body.Faces[BossFace].Surface, BossStart, BossEnd, ClassifiedBossRadius)) return std::nullopt;
            const double Scale = std::max({ 1.0, CandidateRadius, ClassifiedBossRadius, BossStart.Distance(BossEnd) });
            const double Epsilon = ScalarCriteria::GeometricTolerance * Scale;
            if (std::fabs(CandidateRadius - ClassifiedBossRadius) > Epsilon) return std::nullopt;
            Vec3 BossOther;
            if (BossStart.Distance(CandidateCentre) <= Epsilon) BossOther = BossEnd;
            else if (BossEnd.Distance(CandidateCentre) <= Epsilon) BossOther = BossStart;
            else return std::nullopt;
            double CandidateBossHeight = BossOther.Distance(CandidateCentre);
            if (CandidateBossHeight <= Epsilon ||
                (BossOther - CandidateCentre).Normalised().Dot(CandidateAxis) < 1.0 - ScalarCriteria::GeometricTolerance) return std::nullopt;

            if (FirstRoot)
            {
                RootCentre = CandidateCentre; Axis = CandidateAxis;
                BossRadius = CandidateRadius; BossHeight = CandidateBossHeight;
                FirstRoot = false;
            }
            else if (CandidateCentre.Distance(RootCentre) > Epsilon || CandidateAxis.Dot(Axis) < 1.0 - ScalarCriteria::GeometricTolerance ||
                     std::fabs(CandidateRadius - BossRadius) > Epsilon ||
                     std::fabs(CandidateBossHeight - BossHeight) > Epsilon) return std::nullopt;
            AppendUnique(ShoulderFaces, ShoulderFace);
            AppendUnique(BossFaces, BossFace);
        }
        double RootSpan = 0.0;
        if (FirstRoot || ShoulderFaces.size() != SegmentCount || BossFaces.size() != SegmentCount ||
            !CircularChain(Body, RootEdges, RootCentre, Axis, BossRadius, ClosedTopology, &RootSpan)) return std::nullopt;

        // Every shoulder patch has one concentric outer circular arc. Follow those arcs through their adjacent cylindrical
        // wall patches, just as the selected tangent chain was followed through the boss/shoulder patches.
        std::vector<int> OuterEdges, OuterFaces;
        double OuterRadius = 0.0;
        for (int ShoulderFace : ShoulderFaces)
        {
            int FoundOuter = -1;
            for (int Loop : Body.Faces[ShoulderFace].Loops)
            {
                if (Loop < 0 || Loop >= static_cast<int>(Body.Loops.size())) return std::nullopt;
                for (int Coedge : Body.Loops[Loop].Coedges)
                {
                    if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return std::nullopt;
                    int Candidate = Body.Coedges[Coedge].Edge;
                    if (Candidate < 0 || Candidate >= static_cast<int>(Body.Edges.size()) || Contains(RootEdges, Candidate)) continue;
                    Vec3 CandidateCentre, CandidateNormal; double CandidateRadius = 0.0;
                    const double Epsilon = ScalarCriteria::GeometricTolerance * std::max(1.0, BossRadius);
                    if (!CircularFrame(Body.Edges[Candidate].Curve, CandidateCentre, CandidateNormal, CandidateRadius) ||
                        CandidateCentre.Distance(RootCentre) > Epsilon ||
                        std::fabs(CandidateNormal.Dot(Axis)) < 1.0 - ScalarCriteria::GeometricTolerance || CandidateRadius <= BossRadius + Epsilon) continue;
                    if (FoundOuter >= 0 && FoundOuter != Candidate) return std::nullopt;
                    FoundOuter = Candidate;
                }
            }
            if (FoundOuter < 0) return std::nullopt;
            Vec3 CandidateCentre, CandidateNormal; double CandidateRadius = 0.0;
            if (!CircularFrame(Body.Edges[FoundOuter].Curve, CandidateCentre, CandidateNormal, CandidateRadius)) return std::nullopt;
            if (OuterRadius <= Tol) OuterRadius = CandidateRadius;
            else if (std::fabs(CandidateRadius - OuterRadius) > ScalarCriteria::GeometricTolerance * std::max(1.0, OuterRadius)) return std::nullopt;
            AppendUnique(OuterEdges, FoundOuter);

            int OuterFace = -1;
            for (int Coedge : Body.Edges[FoundOuter].Coedges)
            {
                if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return std::nullopt;
                int Face = Body.Coedges[Coedge].Face;
                if (!Contains(ShoulderFaces, Face))
                {
                    if (OuterFace >= 0 || Face < 0 || Face >= static_cast<int>(Body.Faces.size()) ||
                        Body.Faces[Face].Surface.Classification != SurfaceClassification::Cylinder) return std::nullopt;
                    OuterFace = Face;
                }
            }
            if (OuterFace < 0) return std::nullopt;
            AppendUnique(OuterFaces, OuterFace);
        }
        double OuterSpan = 0.0;
        if (OuterEdges.size() != SegmentCount || OuterFaces.size() != SegmentCount || OuterRadius <= BossRadius ||
            !CircularChain(Body, OuterEdges, RootCentre, Axis, OuterRadius, ClosedTopology, &OuterSpan) ||
            !ScalarCriteria::WithinAngularTolerance(OuterSpan, RootSpan)) return std::nullopt;

        Vec3 Base; double ShoulderHeight = 0.0; bool FirstOuter = true;
        for (int OuterFace : OuterFaces)
        {
            Vec3 OuterStart, OuterEnd; double ClassifiedOuterRadius = 0.0;
            if (!CylinderEndCentres(Body.Faces[OuterFace].Surface, OuterStart, OuterEnd, ClassifiedOuterRadius)) return std::nullopt;
            const double Epsilon = ScalarCriteria::GeometricTolerance * std::max({ 1.0, OuterRadius, OuterStart.Distance(OuterEnd) });
            if (std::fabs(OuterRadius - ClassifiedOuterRadius) > Epsilon) return std::nullopt;
            Vec3 CandidateBase;
            if (OuterStart.Distance(RootCentre) <= Epsilon) CandidateBase = OuterEnd;
            else if (OuterEnd.Distance(RootCentre) <= Epsilon) CandidateBase = OuterStart;
            else return std::nullopt;
            double CandidateHeight = CandidateBase.Distance(RootCentre);
            if (CandidateHeight <= Epsilon ||
                (CandidateBase - RootCentre).Normalised().Dot(Axis) > -1.0 + ScalarCriteria::GeometricTolerance) return std::nullopt;
            if (FirstOuter) { Base = CandidateBase; ShoulderHeight = CandidateHeight; FirstOuter = false; }
            else if (CandidateBase.Distance(Base) > Epsilon || std::fabs(CandidateHeight - ShoulderHeight) > Epsilon)
                return std::nullopt;
        }
        if (FirstOuter) return std::nullopt;

        Vec3 BossTop = RootCentre + Axis * BossHeight;
        if (ClosedTopology)
        {
            // The only remaining faces are the planar end caps at the derived outer base and boss top.
            int BottomCaps = 0, TopCaps = 0;
            for (size_t Face = 0; Face < Body.Faces.size(); ++Face)
            {
                int FaceIndex = static_cast<int>(Face);
                if (Contains(ShoulderFaces, FaceIndex) || Contains(BossFaces, FaceIndex) || Contains(OuterFaces, FaceIndex)) continue;
                Vec3 Normal;
                if (!PlanarNormal(Body, FaceIndex, Normal) || std::fabs(Normal.Dot(Axis)) < 1.0 - ScalarCriteria::GeometricTolerance) return std::nullopt;
                const NurbsSurface& Cap = Body.Faces[Face].Surface;
                Vec3 Point = Cap.Sample(0.5 * (Cap.DomainStartU() + Cap.DomainEndU()),
                                        0.5 * (Cap.DomainStartV() + Cap.DomainEndV()));
                const double Epsilon = ScalarCriteria::GeometricTolerance * std::max({ 1.0, OuterRadius, ShoulderHeight, BossHeight });
                if (std::fabs((Point - Base).Dot(Axis)) <= Epsilon) ++BottomCaps;
                else if (std::fabs((Point - BossTop).Dot(Axis)) <= Epsilon) ++TopCaps;
                else return std::nullopt;
            }
            if (BottomCaps != 1 || TopCaps != 1) return std::nullopt;
            return PlaneCylinderRoot{ Base, Axis, {}, OuterRadius, ShoulderHeight,
                                      BossRadius, BossHeight, ScalarCriteria::TwoPi };
        }

        // Finite roots retain N planar bottom patches, N top patches, and either one diameter cap for a half-turn or two
        // radial caps meeting at the axis for a general-angle sector.
        struct RadialCap { Vec3 Normal, Point; };
        int BottomPatches = 0, TopPatches = 0;
        std::vector<RadialCap> RadialCaps;
        const double Epsilon = ScalarCriteria::GeometricTolerance * std::max({ 1.0, OuterRadius, ShoulderHeight, BossHeight });
        for (size_t Face = 0; Face < Body.Faces.size(); ++Face)
        {
            int FaceIndex = static_cast<int>(Face);
            if (Contains(ShoulderFaces, FaceIndex) || Contains(BossFaces, FaceIndex) || Contains(OuterFaces, FaceIndex)) continue;
            const NurbsSurface& Surface = Body.Faces[Face].Surface;
            Vec3 Normal;
            if (!PlanarNormal(Body, FaceIndex, Normal)) return std::nullopt;
            Vec3 Point = Surface.Sample(0.5 * (Surface.DomainStartU() + Surface.DomainEndU()),
                                        0.5 * (Surface.DomainStartV() + Surface.DomainEndV()));
            double Alignment = std::fabs(Normal.Dot(Axis));
            if (Alignment > 1.0 - ScalarCriteria::GeometricTolerance)
            {
                if (std::fabs((Point - Base).Dot(Axis)) <= Epsilon) ++BottomPatches;
                else if (std::fabs((Point - BossTop).Dot(Axis)) <= Epsilon) ++TopPatches;
                else return std::nullopt;
            }
            else if (Alignment < ScalarCriteria::GeometricTolerance && Surface.Classification == SurfaceClassification::Plane)
                RadialCaps.push_back({ Normal.Normalised(), Point });
            else return std::nullopt;
        }
        const size_t ExpectedRadialCaps = SemicircleTopology ? 1u : 2u;
        if (BottomPatches != static_cast<int>(SegmentCount) || TopPatches != static_cast<int>(SegmentCount) ||
            RadialCaps.size() != ExpectedRadialCaps) return std::nullopt;
        Vec3 RadialStart; double SweepAngle = 0.0;
        if (!OpenChainSweep(Body, RootEdges, RootCentre, Axis, RadialStart, SweepAngle) ||
            !ScalarCriteria::WithinAngularTolerance(std::fabs(SweepAngle), RootSpan)) return std::nullopt;
        const bool HalfTurn = ScalarCriteria::WithinAngularTolerance(std::fabs(SweepAngle), ScalarCriteria::Pi);
        if (HalfTurn != SemicircleTopology) return std::nullopt;
        Vec3 RadialEnd = RadialStart * std::cos(SweepAngle) + Axis.Cross(RadialStart) * std::sin(SweepAngle);
        auto MatchesCap = [&](const RadialCap& Cap, Vec3 Radial) noexcept
        {
            Vec3 ExpectedNormal = Axis.Cross(Radial).Normalised();
            return std::fabs(Cap.Normal.Dot(ExpectedNormal)) > 1.0 - ScalarCriteria::GeometricTolerance &&
                std::fabs((Cap.Point - Base).Dot(ExpectedNormal)) <= Epsilon;
        };
        int StartCaps = 0, EndCaps = 0;
        for (const RadialCap& Cap : RadialCaps)
        {
            if (MatchesCap(Cap, RadialStart)) ++StartCaps;
            if (MatchesCap(Cap, RadialEnd)) ++EndCaps;
        }
        if (StartCaps != 1 || EndCaps != 1) return std::nullopt;
        return PlaneCylinderRoot{ Base, Axis, RadialStart, OuterRadius, ShoulderHeight,
                                  BossRadius, BossHeight, SweepAngle };
    }

    Deliver<BrepBody> FilletPlaneCylinderBossRoot(const PlaneCylinderRoot& Root, double Radius) noexcept
    {
        if (Radius <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "radius is zero or negative");
        if (Radius >= Root.BossHeight - Tol)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "fillet radius consumes the cylindrical boss height");
        if (Radius >= Root.OuterRadius - Root.BossRadius - Tol)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "fillet radius consumes the planar shoulder");

        const Vec3 ShoulderCentre = Root.Base + Root.Axis * Root.ShoulderHeight;
        const bool Closed = std::fabs(std::fabs(Root.SweepAngle) - ScalarCriteria::TwoPi) <= ScalarCriteria::GeometricTolerance;
        const Vec3 Radial = Closed ? Workplane::FromNormal(Root.Base, Root.Axis).AxisX : Root.RadialStart.Normalised();
        if (Radial.Length() <= Tol || std::fabs(Radial.Dot(Root.Axis)) > ScalarCriteria::GeometricTolerance)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "plane-cylinder fillet radial frame is degenerate");

        auto RevolveLine = [&](Vec3 Start, Vec3 End) -> Deliver<NurbsSurface>
        {
            Deliver<NurbsCurve> Line = NurbsCurve::Line(Start, End);
            return Line ? NurbsSurface::Revolution(Line.Payload, Root.Base, Root.Axis, Root.SweepAngle)
                        : Deliver<NurbsSurface>::Reject(Line.Denial.Reason, Line.Denial.Detail);
        };

        Deliver<NurbsSurface> Outer;
        if (Closed) Outer = NurbsSurface::Cylinder(Root.Base, Root.Axis, Root.OuterRadius, Root.ShoulderHeight);
        else
        {
            Outer = RevolveLine(Root.Base + Radial * Root.OuterRadius,
                                ShoulderCentre + Radial * Root.OuterRadius);
            if (Outer)
            {
                Outer.Payload.Classification = SurfaceClassification::Cylinder;
                Outer.Payload.Origin = Root.Base; Outer.Payload.Axis = Root.Axis;
                Outer.Payload.RadiusMajor = Outer.Payload.RadiusMinor = Root.OuterRadius;
            }
        }

        Deliver<NurbsCurve> ShoulderLine = NurbsCurve::Line(
            ShoulderCentre + Radial * Root.OuterRadius,
            ShoulderCentre + Radial * (Root.BossRadius + Radius));
        Deliver<NurbsSurface> Shoulder = ShoulderLine
            ? NurbsSurface::Revolution(ShoulderLine.Payload, Root.Base, Root.Axis, Root.SweepAngle)
            : Deliver<NurbsSurface>::Reject(ShoulderLine.Denial.Reason, ShoulderLine.Denial.Detail);

        Vec3 MeridianCentre = ShoulderCentre + Root.Axis * Radius + Radial * (Root.BossRadius + Radius);
        Vec3 ShoulderContact = MeridianCentre - Root.Axis * Radius;
        Vec3 BossContact = MeridianCentre - Radial * Radius;
        Vec3 MeridianMiddle = MeridianCentre - (Root.Axis + Radial) * (Radius / std::sqrt(2.0));
        Deliver<NurbsCurve> Meridian = NurbsCurve::ArcThreePoints(ShoulderContact, MeridianMiddle, BossContact);
        Deliver<NurbsSurface> Roll = Meridian
            ? NurbsSurface::Revolution(Meridian.Payload, Root.Base, Root.Axis, Root.SweepAngle)
            : Deliver<NurbsSurface>::Reject(Meridian.Denial.Reason, Meridian.Denial.Detail);

        Deliver<NurbsSurface> Boss;
        if (Closed)
            Boss = NurbsSurface::Cylinder(ShoulderCentre + Root.Axis * Radius, Root.Axis,
                                          Root.BossRadius, Root.BossHeight - Radius);
        else
        {
            Boss = RevolveLine(ShoulderCentre + Root.Axis * Radius + Radial * Root.BossRadius,
                               ShoulderCentre + Root.Axis * Root.BossHeight + Radial * Root.BossRadius);
            if (Boss)
            {
                Boss.Payload.Classification = SurfaceClassification::Cylinder;
                Boss.Payload.Origin = ShoulderCentre + Root.Axis * Radius; Boss.Payload.Axis = Root.Axis;
                Boss.Payload.RadiusMajor = Boss.Payload.RadiusMinor = Root.BossRadius;
            }
        }
        if (!Outer || !Shoulder || !Roll || !Boss)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "plane-cylinder fillet support is degenerate");

        // Preserve exact partial-torus identity for checking, selection, and later support correspondence.
        Roll.Payload.Classification = SurfaceClassification::Torus;
        Roll.Payload.Origin = ShoulderCentre + Root.Axis * Radius;
        Roll.Payload.Axis = Root.Axis;
        Roll.Payload.RadiusMajor = Root.BossRadius + Radius;
        Roll.Payload.RadiusMinor = Radius;

        std::vector<NurbsSurface> Surfaces{ Outer.Payload, Shoulder.Payload, Roll.Payload, Boss.Payload };
        if (!Closed)
        {
            // Bottom/top sectors meet at the axis endpoints; Sew adds the single planar diameter face containing both
            // finite chain endpoints. Internal angular representation seams intentionally vanish.
            Deliver<NurbsSurface> Bottom = RevolveLine(Root.Base, Root.Base + Radial * Root.OuterRadius);
            Vec3 BossTop = ShoulderCentre + Root.Axis * Root.BossHeight;
            Deliver<NurbsSurface> Top = RevolveLine(BossTop + Radial * Root.BossRadius, BossTop);
            if (!Bottom || !Top)
                return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "open-chain fillet end support is degenerate");
            Surfaces.push_back(std::move(Bottom.Payload));
            Surfaces.push_back(std::move(Top.Payload));
        }
        Deliver<BrepBody> Result = BrepBody::Sew(Surfaces);
        if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, Result.Denial.Detail);
        const bool HalfTurn = !Closed && ScalarCriteria::WithinAngularTolerance(std::fabs(Root.SweepAngle), ScalarCriteria::Pi);
        if (!Closed && !HalfTurn)
        {
            Vec3 BossTop = ShoulderCentre + Root.Axis * Root.BossHeight;
            if (!CapRadialSector(Result.Payload, Root.Base, BossTop, Radial, Root.SweepAngle, Root.OuterRadius))
                return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                    "general-angle plane-cylinder fillet could not close its radial endpoint caps");
        }
        BodyReport Report = Result.Payload.Validate();
        const bool ExpectedTopology = Closed
            ? Result.Payload.Vertices.size() == 5 && Result.Payload.Edges.size() == 9 &&
              Result.Payload.Coedges.size() == 18 && Result.Payload.Loops.size() == 6 && Result.Payload.Faces.size() == 6
            : HalfTurn
                ? Report.Hulls == 1 && Report.Genus == 0 && Result.Payload.Vertices.size() == 12 &&
                  Result.Payload.Edges.size() == 17 && Result.Payload.Coedges.size() == 34 &&
                  Result.Payload.Loops.size() == 7 && Result.Payload.Faces.size() == 7
                : Report.Hulls == 1 && Report.Genus == 0 && Result.Payload.Vertices.size() == 12 &&
                  Result.Payload.Edges.size() == 18 && Result.Payload.Coedges.size() == 36 &&
                  Result.Payload.Loops.size() == 8 && Result.Payload.Faces.size() == 8;
        if (!Report.Solid() || !ExpectedTopology)
            return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "plane-cylinder fillet did not reach its exact manifold topology");
        return Result;
    }

    //------------------------------------------------------------------------------------------------------------------------
    // Phase 32z: the first unequal-radius (asymmetric) support pair. A native conical frustum boss leaves a planar
    // annular shoulder; its foot circle (radius R_f, on the shoulder) and top circle (radius R_t ≠ R_f) are the two
    // coaxial supports. Plane and coaxial cone are both surfaces of revolution about one axis, so their r-offsets meet
    // in an exact circular spine and the rolling ball sweeps an exact rational torus band. With tan α = (R_f − R_t)/H
    // (α > 0 narrows upward, α < 0 is an undercut flare):
    //
    //      cone contact height          z_t = r (1 − sin α)
    //      cone contact radius          ρ_t = R_f − z_t tan α
    //      spine = shoulder contact     ρ_c = R_f + r (1 − sin α) / cos α          (α = 0 recovers Phase 31's R_f + r)
    //      meridian arc                 from angle π + α (cone contact) to 3π/2 (shoulder contact), span π/2 − α
    //
    // The wedge the roll adds is the meridian region bounded by the shoulder, the generator and the arc, revolved
    // about the axis; Pappus gives its exact volume from the region's first moment, and the route refuses a result
    // whose tessellated volume disagrees with that closed form.
    struct PlaneConeRoot
    {
        Vec3   Base, Axis;                                                               // [m] outer-wall foot centre, [-] unit axis
        double OuterRadius = 0.0, ShoulderHeight = 0.0;                                  // [m]
        double FootRadius = 0.0, TopRadius = 0.0, BossHeight = 0.0;                      // [m] conical boss
        [[nodiscard]] double HalfAngle() const noexcept { return std::atan2(FootRadius - TopRadius, BossHeight); }   // [rad]
    };

    // Analytic identity of a complete native conical frustum face, verified by sampling both end rows and the middle
    // of the generators rather than trusted from the tag alone.
    bool ConeEndCentres(const NurbsSurface& Cone, Vec3& Start, Vec3& End, double& RadiusStart, double& RadiusEnd) noexcept
    {
        if (Cone.Classification != SurfaceClassification::Cone || Cone.RadiusMajor <= Tol || Cone.RadiusMinor <= Tol) return false;
        const double U0 = Cone.DomainStartU(), U1 = Cone.DomainEndU();
        const double V0 = Cone.DomainStartV(), V1 = Cone.DomainEndV();
        Vec3 Axis = Cone.Axis.Normalised();
        if (Axis.Length() <= Tol) return false;
        auto CentreAt = [&](double V) { Vec3 P = Cone.Sample(0.5 * (U0 + U1), V); return Cone.Origin + Axis * (P - Cone.Origin).Dot(Axis); };
        Start = CentreAt(V0); End = CentreAt(V1);
        const double Height = Start.Distance(End);
        const double Scale = std::max({ 1.0, Cone.RadiusMajor, Cone.RadiusMinor, Height });
        const double Epsilon = ScalarCriteria::GeometricTolerance * Scale;
        if (Height <= Epsilon || (End - Start).Normalised().Cross(Axis).Length() > ScalarCriteria::GeometricTolerance) return false;
        auto RowRadius = [&](double V, Vec3 Centre, double& Radius)
        {
            Radius = (Cone.Sample(U0, V) - Centre).Length();
            for (int I = 0; I <= 8; ++I)
            {
                Vec3 Radial = Cone.Sample(U0 + (U1 - U0) * (static_cast<double>(I) / 8.0), V) - Centre;
                if (std::fabs(Radial.Length() - Radius) > Epsilon || std::fabs(Radial.Dot(Axis)) > Epsilon) return false;
            }
            return Radius > Tol;
        };
        if (!RowRadius(V0, Start, RadiusStart) || !RowRadius(V1, End, RadiusEnd)) return false;
        // Straight generators: the middle row sits exactly halfway in height and radius.
        Vec3 MiddleCentre = (Start + End) * 0.5; double MiddleRadius = 0.0;
        if (!RowRadius(0.5 * (V0 + V1), MiddleCentre, MiddleRadius) ||
            std::fabs(MiddleRadius - 0.5 * (RadiusStart + RadiusEnd)) > Epsilon) return false;
        // The tagged identity must agree with the measured one.
        const bool FootFirst = Start.Distance(Cone.Origin) <= Epsilon;
        const double TaggedStart = FootFirst ? Cone.RadiusMajor : Cone.RadiusMinor;
        const double TaggedEnd = FootFirst ? Cone.RadiusMinor : Cone.RadiusMajor;
        if (!FootFirst && End.Distance(Cone.Origin) > Epsilon) return false;
        return std::fabs(TaggedStart - RadiusStart) <= Epsilon && std::fabs(TaggedEnd - RadiusEnd) <= Epsilon;
    }

    std::optional<PlaneConeRoot> PlaneConeBossRoot(const BrepBody& Body, int Edge) noexcept
    {
        // Bounded to the complete five-face stepped solid with one closed root rim (the Phase 31 closed topology).
        if (Edge < 0 || Edge >= static_cast<int>(Body.Edges.size()) || !Body.Validate().Solid() ||
            Body.Vertices.size() != 4 || Body.Edges.size() != 7 || Body.Coedges.size() != 14 ||
            Body.Loops.size() != 5 || Body.Faces.size() != 5) return std::nullopt;
        const BrepEdge& RootEdge = Body.Edges[Edge];
        if (!RootEdge.Closed() || RootEdge.Coedges.size() != 2) return std::nullopt;
        Vec3 RootCentre, RootNormal; double FootRadius = 0.0;
        if (!CircularFrame(RootEdge.Curve, RootCentre, RootNormal, FootRadius)) return std::nullopt;

        int ShoulderFace = -1, BossFace = -1; Vec3 Axis;
        for (int Coedge : RootEdge.Coedges)
        {
            if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return std::nullopt;
            int Face = Body.Coedges[Coedge].Face;
            if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return std::nullopt;
            if (Body.Faces[Face].Surface.Classification == SurfaceClassification::Cone)
            {
                if (BossFace >= 0) return std::nullopt;
                BossFace = Face;
            }
            else
            {
                Vec3 Normal;
                if (ShoulderFace >= 0 || !PlanarNormal(Body, Face, Normal)) return std::nullopt;
                ShoulderFace = Face; Axis = Normal.Normalised();
            }
        }
        if (ShoulderFace < 0 || BossFace < 0 || Axis.Length() <= Tol ||
            std::fabs(RootNormal.Dot(Axis)) < 1.0 - ScalarCriteria::GeometricTolerance) return std::nullopt;

        // The conical boss stands on the root circle and rises along the shoulder's outward normal.
        Vec3 ConeStart, ConeEnd; double RadiusStart = 0.0, RadiusEnd = 0.0;
        if (!ConeEndCentres(Body.Faces[BossFace].Surface, ConeStart, ConeEnd, RadiusStart, RadiusEnd)) return std::nullopt;
        const double BossScale = std::max({ 1.0, FootRadius, RadiusStart, RadiusEnd, ConeStart.Distance(ConeEnd) });
        const double BossEpsilon = ScalarCriteria::GeometricTolerance * BossScale;
        Vec3 BossTop; double TopRadius = 0.0, ClassifiedFoot = 0.0;
        if (ConeStart.Distance(RootCentre) <= BossEpsilon) { BossTop = ConeEnd; TopRadius = RadiusEnd; ClassifiedFoot = RadiusStart; }
        else if (ConeEnd.Distance(RootCentre) <= BossEpsilon) { BossTop = ConeStart; TopRadius = RadiusStart; ClassifiedFoot = RadiusEnd; }
        else return std::nullopt;
        const double BossHeight = BossTop.Distance(RootCentre);
        if (std::fabs(ClassifiedFoot - FootRadius) > BossEpsilon || TopRadius <= Tol || BossHeight <= BossEpsilon ||
            (BossTop - RootCentre).Normalised().Dot(Axis) < 1.0 - ScalarCriteria::GeometricTolerance) return std::nullopt;

        // The shoulder's other rim is the one concentric circle wider than the root (its revolution seam is skipped
        // geometrically, as Phase 31 does); that rim's second face is the retained outer cylinder.
        int OuterEdge = -1; Vec3 OuterCentre, OuterNormal; double OuterRadius = 0.0;
        for (int Loop : Body.Faces[ShoulderFace].Loops)
        {
            if (Loop < 0 || Loop >= static_cast<int>(Body.Loops.size())) return std::nullopt;
            for (int Coedge : Body.Loops[Loop].Coedges)
            {
                if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return std::nullopt;
                int Candidate = Body.Coedges[Coedge].Edge;
                if (Candidate < 0 || Candidate >= static_cast<int>(Body.Edges.size()) || Candidate == Edge) continue;
                Vec3 CandidateCentre, CandidateNormal; double CandidateRadius = 0.0;
                const double Epsilon = ScalarCriteria::GeometricTolerance * std::max(1.0, FootRadius);
                if (!CircularFrame(Body.Edges[Candidate].Curve, CandidateCentre, CandidateNormal, CandidateRadius) ||
                    CandidateCentre.Distance(RootCentre) > Epsilon ||
                    std::fabs(CandidateNormal.Dot(Axis)) < 1.0 - ScalarCriteria::GeometricTolerance ||
                    CandidateRadius <= FootRadius + Epsilon) continue;
                if (OuterEdge >= 0 && OuterEdge != Candidate) return std::nullopt;
                OuterEdge = Candidate; OuterCentre = CandidateCentre; OuterNormal = CandidateNormal; OuterRadius = CandidateRadius;
            }
        }
        if (OuterEdge < 0 || !Body.Edges[OuterEdge].Closed() || Body.Edges[OuterEdge].Coedges.size() != 2) return std::nullopt;
        int OuterFace = -1;
        for (int Coedge : Body.Edges[OuterEdge].Coedges)
        {
            int Face = Body.Coedges[Coedge].Face;
            if (Face == ShoulderFace) continue;
            if (OuterFace >= 0 || Face < 0 || Face >= static_cast<int>(Body.Faces.size()) ||
                Body.Faces[Face].Surface.Classification != SurfaceClassification::Cylinder) return std::nullopt;
            OuterFace = Face;
        }
        if (OuterFace < 0) return std::nullopt;
        Vec3 OuterStart, OuterEnd; double ClassifiedOuterRadius = 0.0;
        if (!CylinderEndCentres(Body.Faces[OuterFace].Surface, OuterStart, OuterEnd, ClassifiedOuterRadius)) return std::nullopt;
        const double WallEpsilon = ScalarCriteria::GeometricTolerance * std::max({ 1.0, OuterRadius, OuterStart.Distance(OuterEnd) });
        if (std::fabs(OuterRadius - ClassifiedOuterRadius) > WallEpsilon) return std::nullopt;
        Vec3 Base;
        if (OuterStart.Distance(RootCentre) <= WallEpsilon) Base = OuterEnd;
        else if (OuterEnd.Distance(RootCentre) <= WallEpsilon) Base = OuterStart;
        else return std::nullopt;
        const double ShoulderHeight = Base.Distance(RootCentre);
        if (ShoulderHeight <= WallEpsilon ||
            (Base - RootCentre).Normalised().Dot(Axis) > -1.0 + ScalarCriteria::GeometricTolerance) return std::nullopt;

        // The two remaining faces are the planar end caps: one at the outer base, one at the boss top with the top rim.
        int BottomCaps = 0, TopCaps = 0;
        const double CapEpsilon = ScalarCriteria::GeometricTolerance * std::max({ 1.0, OuterRadius, ShoulderHeight, BossHeight });
        for (size_t Face = 0; Face < Body.Faces.size(); ++Face)
        {
            const int FaceIndex = static_cast<int>(Face);
            if (FaceIndex == ShoulderFace || FaceIndex == BossFace || FaceIndex == OuterFace) continue;
            Vec3 Normal;
            if (!PlanarNormal(Body, FaceIndex, Normal) || std::fabs(Normal.Dot(Axis)) < 1.0 - ScalarCriteria::GeometricTolerance ||
                Body.Faces[Face].Loops.size() != 1) return std::nullopt;
            const NurbsSurface& Cap = Body.Faces[Face].Surface;
            Vec3 Point = Cap.Sample(0.5 * (Cap.DomainStartU() + Cap.DomainEndU()), 0.5 * (Cap.DomainStartV() + Cap.DomainEndV()));
            if (std::fabs((Point - Base).Dot(Axis)) <= CapEpsilon) ++BottomCaps;
            else if (std::fabs((Point - BossTop).Dot(Axis)) <= CapEpsilon)
            {
                const std::vector<int>& Rim = Body.Loops[Body.Faces[Face].Loops.front()].Coedges;
                if (Rim.size() != 1) return std::nullopt;
                Vec3 RimCentre, RimNormal; double RimRadius = 0.0;
                if (!CircularFrame(Body.Edges[Body.Coedges[Rim.front()].Edge].Curve, RimCentre, RimNormal, RimRadius) ||
                    RimCentre.Distance(BossTop) > CapEpsilon || std::fabs(RimRadius - TopRadius) > CapEpsilon) return std::nullopt;
                ++TopCaps;
            }
            else return std::nullopt;
        }
        if (BottomCaps != 1 || TopCaps != 1) return std::nullopt;
        return PlaneConeRoot{ Base, Axis, OuterRadius, ShoulderHeight, FootRadius, TopRadius, BossHeight };
    }

    // First moment about the axis (∫ρ dA) of the meridian wedge: quadrilateral (root corner, shoulder contact, arc
    // centre, cone contact) minus the circular sector the arc cuts from it. 2π times this is the added volume.
    double PlaneConeWedgeMoment(double FootRadius, double HalfAngle, double Radius) noexcept
    {
        const double SinA = std::sin(HalfAngle), CosA = std::cos(HalfAngle);
        const double ContactHeight = Radius * (1.0 - SinA);
        const double SpineRadius = FootRadius + Radius * (1.0 - SinA) / CosA;
        const double ContactRadius = FootRadius - ContactHeight * std::tan(HalfAngle);
        const double Quad[4][2] = { { FootRadius, 0.0 }, { SpineRadius, 0.0 }, { SpineRadius, Radius }, { ContactRadius, ContactHeight } };
        double QuadMoment = 0.0;
        for (int I = 0; I < 4; ++I)
        {
            const double* P = Quad[I]; const double* Q = Quad[(I + 1) % 4];
            const double Cross = P[0] * Q[1] - Q[0] * P[1];
            QuadMoment += (P[0] + Q[0]) * Cross;                                       // Σ (ρᵢ + ρᵢ₊₁)(ρᵢ zᵢ₊₁ − ρᵢ₊₁ zᵢ) / 6
        }
        QuadMoment = std::fabs(QuadMoment) / 6.0;
        // Sector centred at (ρ_c, r) from angle π + α to 3π/2: ∫∫(ρ_c + s cos θ) s ds dθ.
        const double Theta0 = ScalarCriteria::Pi + HalfAngle, Theta1 = 1.5 * ScalarCriteria::Pi;
        const double SectorMoment = SpineRadius * Radius * Radius * (Theta1 - Theta0) / 2.0 +
                                    Radius * Radius * Radius * (std::sin(Theta1) - std::sin(Theta0)) / 3.0;
        return QuadMoment - SectorMoment;
    }

    double PlaneConeSourceVolume(const PlaneConeRoot& Root) noexcept
    {
        return ScalarCriteria::Pi * Root.OuterRadius * Root.OuterRadius * Root.ShoulderHeight +
               ScalarCriteria::Pi * Root.BossHeight *
               (Root.FootRadius * Root.FootRadius + Root.FootRadius * Root.TopRadius + Root.TopRadius * Root.TopRadius) / 3.0;
    }

    Deliver<BrepBody> FilletPlaneConeBossRoot(const PlaneConeRoot& Root, double Radius) noexcept
    {
        if (Radius <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "radius is zero or negative");
        const double HalfAngle = Root.HalfAngle();
        const double SinA = std::sin(HalfAngle), CosA = std::cos(HalfAngle);
        const double ContactHeight = Radius * (1.0 - SinA);
        const double SpineRadius = Root.FootRadius + Radius * (1.0 - SinA) / CosA;
        const double ContactRadius = Root.FootRadius - ContactHeight * std::tan(HalfAngle);
        if (ContactHeight >= Root.BossHeight - Tol)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "fillet radius consumes the conical boss height");
        if (SpineRadius >= Root.OuterRadius - Tol)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "fillet radius consumes the planar shoulder");
        if (ContactRadius <= Tol)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "fillet contact leaves no conical boss");

        const Vec3 ShoulderCentre = Root.Base + Root.Axis * Root.ShoulderHeight;
        const Vec3 Radial = Workplane::FromNormal(Root.Base, Root.Axis).AxisX;
        if (Radial.Length() <= Tol || std::fabs(Radial.Dot(Root.Axis)) > ScalarCriteria::GeometricTolerance)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "plane-cone fillet radial frame is degenerate");

        Deliver<NurbsSurface> Outer = NurbsSurface::Cylinder(Root.Base, Root.Axis, Root.OuterRadius, Root.ShoulderHeight);
        Deliver<NurbsCurve> ShoulderLine = NurbsCurve::Line(ShoulderCentre + Radial * Root.OuterRadius,
                                                            ShoulderCentre + Radial * SpineRadius);
        Deliver<NurbsSurface> Shoulder = ShoulderLine
            ? NurbsSurface::Revolution(ShoulderLine.Payload, Root.Base, Root.Axis, ScalarCriteria::TwoPi)
            : Deliver<NurbsSurface>::Reject(ShoulderLine.Denial.Reason, ShoulderLine.Denial.Detail);

        // Meridian: shoulder contact (angle 3π/2) → arc middle → cone contact (angle π + α), about the spine centre.
        const Vec3 MeridianCentre = ShoulderCentre + Root.Axis * Radius + Radial * SpineRadius;
        auto OnMeridian = [&](double Angle) { return MeridianCentre + (Radial * std::cos(Angle) + Root.Axis * std::sin(Angle)) * Radius; };
        const Vec3 ShoulderContact = OnMeridian(1.5 * ScalarCriteria::Pi);
        const Vec3 ConeContact = OnMeridian(ScalarCriteria::Pi + HalfAngle);
        const Vec3 MeridianMiddle = OnMeridian(1.25 * ScalarCriteria::Pi + 0.5 * HalfAngle);
        Deliver<NurbsCurve> Meridian = NurbsCurve::ArcThreePoints(ShoulderContact, MeridianMiddle, ConeContact);
        Deliver<NurbsSurface> Roll = Meridian
            ? NurbsSurface::Revolution(Meridian.Payload, Root.Base, Root.Axis, ScalarCriteria::TwoPi)
            : Deliver<NurbsSurface>::Reject(Meridian.Denial.Reason, Meridian.Denial.Detail);

        Deliver<NurbsSurface> Boss = NurbsSurface::Cone(ShoulderCentre + Root.Axis * ContactHeight, Root.Axis,
                                                        ContactRadius, Root.TopRadius, Root.BossHeight - ContactHeight);
        if (!Outer || !Shoulder || !Roll || !Boss)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "plane-cone fillet support is degenerate");

        // Preserve exact partial-torus identity for checking, selection, and later support correspondence.
        Roll.Payload.Classification = SurfaceClassification::Torus;
        Roll.Payload.Origin = ShoulderCentre + Root.Axis * Radius;
        Roll.Payload.Axis = Root.Axis;
        Roll.Payload.RadiusMajor = SpineRadius;
        Roll.Payload.RadiusMinor = Radius;

        Deliver<BrepBody> Result = BrepBody::Sew({ Outer.Payload, Shoulder.Payload, Roll.Payload, Boss.Payload });
        if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, Result.Denial.Detail);
        const BodyReport Report = Result.Payload.Validate();
        const bool ExpectedTopology = Result.Payload.Vertices.size() == 5 && Result.Payload.Edges.size() == 9 &&
            Result.Payload.Coedges.size() == 18 && Result.Payload.Loops.size() == 6 && Result.Payload.Faces.size() == 6;
        if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || !ExpectedTopology)
            return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "plane-cone fillet did not reach its exact manifold topology");
        const double ExactVolume = PlaneConeSourceVolume(Root) +
                                   ScalarCriteria::TwoPi * PlaneConeWedgeMoment(Root.FootRadius, HalfAngle, Radius);
        if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExactVolume))
            return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence, "plane-cone fillet volume failed analytic acceptance");
        return Result;
    }

    struct ConeSide
    {
        Vec3 Base, Axis;
        double RadiusFoot = 0.0, RadiusTop = 0.0, Height = 0.0;
    };

    std::optional<ConeSide> NativeConeSideFace(const BrepBody& Body, int Face) noexcept
    {
        if (Face < 0 || Face >= static_cast<int>(Body.Faces.size()) ||
            Body.Faces[Face].Surface.Classification != SurfaceClassification::Cone || !Body.Validate().Solid() ||
            Body.Vertices.size() != 2 || Body.Edges.size() != 3 || Body.Coedges.size() != 6 ||
            Body.Loops.size() != 3 || Body.Faces.size() != 3) return std::nullopt;

        const NurbsSurface& Side = Body.Faces[Face].Surface;
        Vec3 Axis = Side.Axis.Normalised();
        if (Axis.Length() <= Tol || Side.RadiusMajor <= Tol || Side.RadiusMinor <= Tol) return std::nullopt;

        int Caps = 0, Rims = 0, Seams = 0;
        double Low = ScalarCriteria::Infinity, High = -ScalarCriteria::Infinity;
        for (size_t CandidateIndex = 0; CandidateIndex < Body.Faces.size(); ++CandidateIndex)
        {
            const BrepFace& Candidate = Body.Faces[CandidateIndex];
            if (Candidate.Loops.size() != 1) return std::nullopt;
            if (Candidate.Surface.Classification == SurfaceClassification::Plane)
            {
                Vec3 Normal;
                if (!PlanarNormal(Body, static_cast<int>(CandidateIndex), Normal) ||
                    std::fabs(Normal.Dot(Axis)) < 1.0 - ScalarCriteria::GeometricTolerance) return std::nullopt;
                const NurbsSurface& Plane = Candidate.Surface;
                Vec3 Point = Plane.Sample(0.5 * (Plane.DomainStartU() + Plane.DomainEndU()),
                                          0.5 * (Plane.DomainStartV() + Plane.DomainEndV()));
                double Along = (Point - Side.Origin).Dot(Axis);
                Low = std::min(Low, Along); High = std::max(High, Along); ++Caps;
            }
            else if (static_cast<int>(CandidateIndex) != Face) return std::nullopt;
        }
        for (const BrepEdge& Edge : Body.Edges)
        {
            if (Edge.Closed() && Edge.Curve.Classification == CurveClassification::Circle &&
                Edge.Curve.Degree == 2 && Edge.Curve.Rational() && Edge.Coedges.size() == 2) ++Rims;
            else if (!Edge.Closed() && Edge.Curve.Classification == CurveClassification::Line &&
                     Edge.Curve.Degree == 1 && Edge.Coedges.size() == 2) ++Seams;
            else return std::nullopt;
        }

        const double Height = High - Low;
        const double Scale = std::max({ 1.0, Side.RadiusMajor, Side.RadiusMinor, std::fabs(Low), std::fabs(High), Height });
        const double Epsilon = ScalarCriteria::GeometricTolerance * Scale;
        if (Caps != 2 || Rims != 2 || Seams != 1 || Height <= Epsilon) return std::nullopt;

        // Verify the classified support against its actual NURBS. In particular, derive the two axial endpoints instead
        // of assuming the construction height was positive: Cone(..., -H) is a valid primitive whose first ring is the
        // geometrically upper ring. Canonicalising to low → high makes outward face motion independent of construction
        // direction while preserving the same exact support.
        const double U0 = Side.DomainStartU(), U1 = Side.DomainEndU();
        const double V0 = Side.DomainStartV(), V1 = Side.DomainEndV();
        auto Ring = [&](double V, double& Along, double& Radius) -> bool
        {
            Vec3 First = Side.Sample(U0, V);
            Along = (First - Side.Origin).Dot(Axis);
            Radius = (First - (Side.Origin + Axis * Along)).Length();
            if (Radius <= Tol) return false;
            for (int I = 1; I <= 8; ++I)
            {
                Vec3 Point = Side.Sample(U0 + (U1 - U0) * (static_cast<double>(I) / 8.0), V);
                double T = (Point - Side.Origin).Dot(Axis);
                double R = (Point - (Side.Origin + Axis * T)).Length();
                if (std::fabs(T - Along) > Epsilon || std::fabs(R - Radius) > Epsilon) return false;
            }
            return true;
        };

        double AlongStart = 0.0, AlongEnd = 0.0, RadiusStart = 0.0, RadiusEnd = 0.0;
        if (!Ring(V0, AlongStart, RadiusStart) || !Ring(V1, AlongEnd, RadiusEnd)) return std::nullopt;
        if (std::fabs(std::min(AlongStart, AlongEnd) - Low) > Epsilon ||
            std::fabs(std::max(AlongStart, AlongEnd) - High) > Epsilon ||
            std::fabs(RadiusStart - Side.RadiusMajor) > Epsilon ||
            std::fabs(RadiusEnd - Side.RadiusMinor) > Epsilon) return std::nullopt;

        for (int J = 1; J < 4; ++J)
        {
            const double Fraction = static_cast<double>(J) / 4.0;
            double Along = 0.0, Radius = 0.0;
            if (!Ring(V0 + (V1 - V0) * Fraction, Along, Radius) ||
                std::fabs(Along - ScalarCriteria::Lerp(AlongStart, AlongEnd, Fraction)) > Epsilon ||
                std::fabs(Radius - ScalarCriteria::Lerp(RadiusStart, RadiusEnd, Fraction)) > Epsilon) return std::nullopt;
        }

        bool LowRim = false, HighRim = false;
        for (const BrepEdge& Edge : Body.Edges)
        {
            if (!Edge.Closed()) continue;
            double RimAlong = 0.0, RimRadius = 0.0;
            for (int I = 0; I < 8; ++I)
            {
                Vec3 Point = Edge.Curve.Sample(Edge.Curve.DomainStart() +
                    (Edge.Curve.DomainEnd() - Edge.Curve.DomainStart()) * (static_cast<double>(I) / 8.0));
                double Along = (Point - Side.Origin).Dot(Axis);
                double Radius = (Point - (Side.Origin + Axis * Along)).Length();
                if (I == 0) { RimAlong = Along; RimRadius = Radius; }
                else if (std::fabs(Along - RimAlong) > Epsilon || std::fabs(Radius - RimRadius) > Epsilon) return std::nullopt;
            }
            if (std::fabs(RimAlong - Low) <= Epsilon)
            {
                if (LowRim || std::fabs(RimRadius - (AlongStart < AlongEnd ? RadiusStart : RadiusEnd)) > Epsilon) return std::nullopt;
                LowRim = true;
            }
            else if (std::fabs(RimAlong - High) <= Epsilon)
            {
                if (HighRim || std::fabs(RimRadius - (AlongStart > AlongEnd ? RadiusStart : RadiusEnd)) > Epsilon) return std::nullopt;
                HighRim = true;
            }
            else return std::nullopt;
        }
        if (!LowRim || !HighRim) return std::nullopt;

        const bool StartIsLow = AlongStart < AlongEnd;
        return ConeSide{ Side.Origin + Axis * Low, Axis,
                         StartIsLow ? RadiusStart : RadiusEnd,
                         StartIsLow ? RadiusEnd : RadiusStart, Height };
    }

    struct ConeCap { ConeSide Shape; bool Upper = false; };
    std::optional<ConeCap> NativeConeCapFace(const BrepBody& Body, int Face) noexcept
    {
        if (Face < 0 || Face >= static_cast<int>(Body.Faces.size()) || Body.Faces[Face].Surface.Classification != SurfaceClassification::Plane) return std::nullopt;
        for (size_t F = 0; F < Body.Faces.size(); ++F) if (Body.Faces[F].Surface.Classification == SurfaceClassification::Cone)
            if (std::optional<ConeSide> Side = NativeConeSideFace(Body, static_cast<int>(F)))
            {
                const NurbsSurface& Cap = Body.Faces[Face].Surface;
                Vec3 Point = Cap.Sample(0.5 * (Cap.DomainStartU() + Cap.DomainEndU()), 0.5 * (Cap.DomainStartV() + Cap.DomainEndV()));
                double T = (Point - Side->Base).Dot(Side->Axis), Epsilon = ScalarCriteria::GeometricTolerance * std::max({ 1.0, Side->Height, Side->RadiusFoot, Side->RadiusTop });
                if (std::fabs(T - Side->Height) <= Epsilon) return ConeCap{ *Side, true };
                if (std::fabs(T) <= Epsilon) return ConeCap{ *Side, false };
            }
        return std::nullopt;
    }
    Deliver<BrepBody> PushConeCap(const ConeCap& Cap, double Distance) noexcept
    {
        const double Slope = (Cap.Shape.RadiusTop - Cap.Shape.RadiusFoot) / Cap.Shape.Height, Height = Cap.Shape.Height + Distance;
        if (Height <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "push would consume the entire cone height");
        Vec3 Base = Cap.Shape.Base; double Foot = Cap.Shape.RadiusFoot, Top = Cap.Shape.RadiusTop;
        if (Cap.Upper) Top += Slope * Distance; else { Base = Base - Cap.Shape.Axis * Distance; Foot -= Slope * Distance; }
        if (Foot <= Tol || Top <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "push would collapse a conical cap radius");
        Deliver<BrepBody> Result = BrepBody::Cone(Base, Cap.Shape.Axis, Foot, Top, Height);
        if (!Result || !Result.Payload.Validate().Solid()) return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "conical cap push could not be rebuilt into a valid solid");
        return Result;
    }

    Deliver<BrepBody> PushConeSide(const ConeSide& Cone, double Distance) noexcept
    {
        const double OffsetScale = std::sqrt(1.0 + std::pow((Cone.RadiusTop - Cone.RadiusFoot) / Cone.Height, 2.0));
        const double Foot = Cone.RadiusFoot + Distance * OffsetScale, Top = Cone.RadiusTop + Distance * OffsetScale;
        if (Foot <= Tol || Top <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "push would collapse a conical cap radius");
        Deliver<BrepBody> Result = BrepBody::Cone(Cone.Base, Cone.Axis, Foot, Top, Cone.Height);
        if (!Result || !Result.Payload.Validate().Solid()) return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "conical side push could not be rebuilt into a valid solid");
        return Result;
    }

    Deliver<BrepBody> PushCylinderCap(const CylinderCap& Cap, double Distance) noexcept
    {
        const double NewHeight = Cap.Height + Distance;
        if (NewHeight <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "push would consume the entire cylinder height");
        Vec3 NewBase = Cap.Upper ? Cap.Base : Cap.Base - Cap.Axis * Distance;
        Deliver<BrepBody> Result = BrepBody::Cylinder(NewBase, Cap.Axis, Cap.Radius, NewHeight);
        if (!Result || !Result.Payload.Validate().Solid()) return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "cylindrical cap push could not be rebuilt into a valid solid");
        return Result;
    }

    Deliver<BrepBody> PushCylinderSide(const CylinderCap& Cylinder, double Distance) noexcept
    {
        const double NewRadius = Cylinder.Radius + Distance;
        if (NewRadius <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "push would collapse the cylinder radius");
        Deliver<BrepBody> Result = BrepBody::Cylinder(Cylinder.Base, Cylinder.Axis, NewRadius, Cylinder.Height);
        if (!Result || !Result.Payload.Validate().Solid()) return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "cylindrical side push could not be rebuilt into a valid solid");
        return Result;
    }

    Deliver<BrepBody> FilletCylinderCap(const CylinderCap& Cap, double Radius) noexcept
    {
        if (Radius <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "radius is zero or negative");
        if (Radius >= Cap.Radius - Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "fillet radius reaches the cylinder axis");
        if (Radius >= Cap.Height - Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "fillet radius consumes the entire cylinder height");

        // The meridian is an exact rational quarter arc. Revolving it forms the constant-radius rolling-ball surface;
        // the remaining cylinder touches its outer endpoint and the automatically capped plane touches its inner one.
        Workplane Frame = Workplane::FromNormal(Cap.Base, Cap.Axis);
        Vec3 Centre = Cap.Upper
            ? Cap.Base + Cap.Axis * (Cap.Height - Radius) + Frame.AxisX * (Cap.Radius - Radius)
            : Cap.Base + Cap.Axis * Radius + Frame.AxisX * (Cap.Radius - Radius);
        Vec3 Outer = Centre + Frame.AxisX * Radius;
        Vec3 Mid = Cap.Upper
            ? Centre + (Frame.AxisX + Cap.Axis) * (Radius / std::sqrt(2.0))
            : Centre + (Frame.AxisX - Cap.Axis) * (Radius / std::sqrt(2.0));
        Vec3 Inner = Cap.Upper ? Centre + Cap.Axis * Radius : Centre - Cap.Axis * Radius;
        Deliver<NurbsCurve> Meridian = NurbsCurve::ArcThreePoints(Outer, Mid, Inner);
        if (!Meridian) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "circular-cap fillet meridian is degenerate");
        Deliver<NurbsSurface> Roll = NurbsSurface::Revolution(Meridian.Payload, Cap.Base, Cap.Axis, ScalarCriteria::TwoPi);
        Deliver<NurbsSurface> Cylinder = Cap.Upper
            ? NurbsSurface::Cylinder(Cap.Base, Cap.Axis, Cap.Radius, Cap.Height - Radius)
            : NurbsSurface::Cylinder(Cap.Base + Cap.Axis * Radius, Cap.Axis, Cap.Radius, Cap.Height - Radius);
        if (!Roll || !Cylinder) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "circular-cap fillet support is degenerate");
        // It is a partial torus, not a generic revolution: preserve exact analytic identity for downstream selection,
        // checking and future blend correspondence while retaining its quarter-domain NURBS control net.
        Roll.Payload.Classification = SurfaceClassification::Torus;
        Roll.Payload.Origin = Centre - Frame.AxisX * (Cap.Radius - Radius);
        Roll.Payload.Axis = Cap.Axis; Roll.Payload.RadiusMajor = Cap.Radius - Radius; Roll.Payload.RadiusMinor = Radius;
        std::vector<NurbsSurface> Faces;
        Faces.push_back(std::move(Cylinder.Payload)); Faces.push_back(std::move(Roll.Payload));
        Deliver<BrepBody> Result = BrepBody::Sew(Faces);
        if (!Result || !Result.Payload.Validate().Solid()) return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "circular-cap fillet could not be sewn into a valid solid");
        return Result;
    }

    // Move a face by rebuilding its boundary as a ring of ruled faces instead of unioning a nearly coincident
    // extrusion.  A Boolean needs the footprint pulled in by a small epsilon to avoid coincident side faces; that
    // epsilon leaves a very thin, but real, rim around the old face.  On a full-face push the rim is not design
    // geometry: its four inner edges are only a few microns long across, yet they were presented as blend targets.
    //
    // Replacing the face directly has the exact intended topology.  Existing boundary edges remain at the root and
    // become shared by their old neighbour and one new ruled wall; translated copies become the moved cap's edges.
    // It also means a subsequent blend sees the actual arm perimeter, rather than an epsilon-wide Boolean artefact.
    Deliver<BrepBody> DirectPlanarPush(const BrepBody& Body, int Face, Vec3 Normal, double Distance) noexcept
    {
        if (Body.Faces[Face].Loops.empty())
            return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "face has no boundary loops to push");

        struct Boundary
        {
            int        Coedge = -1;
            int        RootEdge = -1;
            int        CapEdge = -1;
            NurbsCurve Root;
            NurbsCurve Cap;
        };

        BrepBody Result = Body;
        const Mat4 Move = Mat4::Translation(Normal * Distance);
        std::vector<Boundary> BoundaryEdges;
        for (int Loop : Result.Faces[Face].Loops)
            for (int Coedge : Result.Loops[Loop].Coedges)
            {
                int RootEdge = Result.Coedges[Coedge].Edge;
                if (RootEdge < 0 || RootEdge >= (int)Result.Edges.size())
                    return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "face has an invalid boundary edge");
                NurbsCurve Root = Result.Edges[RootEdge].Curve;
                NurbsCurve Cap = Root.Transformed(Move);
                int CapEdge = Result.AddEdge(Cap, Tol);
                BoundaryEdges.push_back({ Coedge, RootEdge, CapEdge, std::move(Root), std::move(Cap) });
            }
        if (BoundaryEdges.size() < 3)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "face boundary has fewer than three edges");

        // Lift the selected face and rewire its existing coedges to the translated rim.  The same coedge/loop order
        // is retained, so holes move with the cap as expected.
        Result.Faces[Face].Surface = Result.Faces[Face].Surface.Transformed(Move);
        for (const Boundary& B : BoundaryEdges)
        {
            std::vector<int>& Users = Result.Edges[B.RootEdge].Coedges;
            Users.erase(std::remove(Users.begin(), Users.end(), B.Coedge), Users.end());
            Result.Edges[B.CapEdge].Coedges.push_back(B.Coedge);
            Result.Coedges[B.Coedge].Edge = B.CapEdge;
            Result.Coedges[B.Coedge].Trace.clear();
        }

        // Every ruled wall naturally reuses its root and cap edge, and automatically shares the vertical joins with
        // the adjacent ruled walls.  There is no coincident-face Boolean seam to leave behind.
        for (const Boundary& B : BoundaryEdges)
        {
            Deliver<NurbsSurface> Wall = NurbsSurface::Ruled(B.Root, B.Cap);
            if (!Wall) return Deliver<BrepBody>::Reject(Wall.Denial.Reason, Wall.Denial.Detail);
            int WallFace = Result.AddFace(std::move(Wall.Payload));
            Result.AddNaturalBoundary(WallFace, Tol);
        }
        Result.Orient();
        BodyReport Report = Result.Validate();
        if (!Report.Solid())
            return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "direct face push did not close into a manifold solid");
        return Deliver<BrepBody>::Accept(std::move(Result));
    }

    // A root edge of a prismatic handle can be reflex: its material angle is greater than π even though the two
    // adjacent face normals have the smaller supplementary angle.  Treating it as a convex corner and subtracting a
    // box cutter makes the cutter re-enter the head (the broken, pinched pictures this case originally produced).
    //
    // For the regular and very common prismatic case, edit the cross-section itself instead.  The complete outline is
    // rebuilt once, then extruded through the selected edge.  This leaves one clean bevel face, or a consistently
    // sampled concave round, rather than a Boolean-generated hole and four accidental end faces.
    std::optional<BrepBody> PrismaticReflexBlend(const BrepBody& Body, const EdgeCornerFrame& F, double Amount, bool Round) noexcept
    {
        // A Boolean/PullFace result can partition an otherwise planar cap into several coplanar faces.  Do not pick
        // one face loop: that was the subtle source of the old broken root.  Instead trace the boundary of the whole
        // cross-section.  A perimeter edge has one cap-face user and one non-cap user; seams between cap patches have
        // two cap users and are deliberately ignored.
        const double CapLevel = F.Start.Dot(F.Tangent);
        std::vector<std::vector<std::pair<int, int>>> Neighbours(Body.Vertices.size());
        for (size_t E = 0; E < Body.Edges.size(); ++E)
        {
            const BrepEdge& Edge = Body.Edges[E];
            if (Edge.VertexStart < 0 || Edge.VertexEnd < 0) return std::nullopt;
            if (std::fabs(Body.Vertices[Edge.VertexStart].Point.Dot(F.Tangent) - CapLevel) > Tol ||
                std::fabs(Body.Vertices[Edge.VertexEnd].Point.Dot(F.Tangent) - CapLevel) > Tol) continue;
            int CapUsers = 0;
            for (int C : Edge.Coedges)
            {
                Vec3 N;
                if (PlanarNormal(Body, Body.Coedges[C].Face, N) && std::fabs(N.Dot(F.Tangent)) > 0.999999) ++CapUsers;
            }
            if (CapUsers != 1) continue;
            if (Edge.Curve.Degree > 1) return std::nullopt;                            // the rebuilt cap contour must stay linear except for our roll
            Neighbours[Edge.VertexStart].push_back({ (int)E, Edge.VertexEnd });
            Neighbours[Edge.VertexEnd].push_back({ (int)E, Edge.VertexStart });
        }

        int Start = -1;
        for (size_t V = 0; V < Body.Vertices.size(); ++V)
            if (Body.Vertices[V].Point.Coincident(F.Start, Tol)) { Start = (int)V; break; }
        if (Start < 0 || Neighbours[Start].size() != 2) return std::nullopt;             // not a simple, capped prism

        // Begin at the selected root.  The trace has no duplicate endpoint; it is P, next, ..., previous.
        std::vector<Vec3> Points;
        int Current = Start, PreviousEdge = -1;
        for (size_t Guard = 0; Guard <= Body.Vertices.size(); ++Guard)
        {
            Points.push_back(Body.Vertices[Current].Point);
            const std::vector<std::pair<int, int>>& Options = Neighbours[Current];
            if (Options.size() != 2) return std::nullopt;
            const std::pair<int, int>& Step = Options[Options[0].first == PreviousEdge ? 1 : 0];
            PreviousEdge = Step.first; Current = Step.second;
            if (Current == Start) break;
            if (Guard == Body.Vertices.size()) return std::nullopt;
        }
        const size_t Count = Points.size();
        if (Count < 3 || Current != Start) return std::nullopt;
        Vec3 P = Points[0], Previous = Points.back(), Next = Points[1];
        Vec3 ToPrevious = Previous - P, ToNext = Next - P;
        double PreviousLength = ToPrevious.Length(), NextLength = ToNext.Length();
        if (PreviousLength <= Tol || NextLength <= Tol) return std::nullopt;
        ToPrevious = ToPrevious * (1.0 / PreviousLength);
        ToNext = ToNext * (1.0 / NextLength);

        // The sign of a local turn relative to the loop's area normal identifies a reflex vertex independent of
        // whether we started from the upper or lower cap (and hence independent of loop orientation).
        Vec3 AreaNormal;
        for (size_t I = 0; I < Count; ++I) AreaNormal = AreaNormal + Points[I].Cross(Points[(I + 1) % Count]);
        if (AreaNormal.Length() <= Tol || (P - Previous).Cross(Next - P).Dot(AreaNormal) >= -Tol) return std::nullopt;

        double SetBack = Amount;
        Vec3 Centre, Mid;
        Deliver<NurbsCurve> Arc;
        if (Round)
        {
            // The angle between the two rays is the small *void* angle at a reflex root.  Its exact tangent distance
            // is R/tan(void/2), and the circle centre sits on its bisector.  This is not the convex formula used by
            // the Boolean fallback's `F.Bisector`.
            double VoidAngle = std::acos(ScalarCriteria::Clamp(ToPrevious.Dot(ToNext), -1.0, 1.0));
            if (VoidAngle <= Tol || VoidAngle >= ScalarCriteria::Pi - Tol) return std::nullopt;
            SetBack = Amount / std::tan(VoidAngle * 0.5);
            Vec3 VoidBisector = (ToPrevious + ToNext).Normalised();
            Centre = P + VoidBisector * (Amount / std::sin(VoidAngle * 0.5));
            Mid = Centre - VoidBisector * Amount;
        }
        if (SetBack <= Tol || SetBack >= PreviousLength - Tol || SetBack >= NextLength - Tol) return std::nullopt;

        Vec3 A = P + ToPrevious * SetBack, B = P + ToNext * SetBack;
        if (!Round)
        {
            // P is replaced by a single, planar bevel edge.  This adds the proper triangular wedge to a re-entrant
            // corner rather than subtracting a convex cutter from it.
            std::vector<Vec3> Outline;
            Outline.reserve(Count + 1);
            Outline.push_back(B);
            Outline.insert(Outline.end(), Points.begin() + 1, Points.end());
            Outline.push_back(A);
            Deliver<NurbsCurve> Profile = NurbsCurve::Polyline(Outline, true);
            if (!Profile) return std::nullopt;
            Deliver<BrepBody> Rebuilt = BrepBody::Extrude(Profile.Payload, F.Tangent, F.Length);
            if (!Rebuilt || !Rebuilt.Payload.Validate().Solid()) return std::nullopt;
            return std::move(Rebuilt.Payload);
        }

        // Keep the circular piece separate from the tangent straight walls.  A NURBS Join is geometrically valid,
        // but it intentionally hides a G1 seam from SplitAtKinks; sewing these sheets explicitly records the
        // cylindrical fillet as its own face while retaining exact (rational) circle geometry.
        Arc = NurbsCurve::ArcThreePoints(A, Mid, B);
        if (!Arc) return std::nullopt;
        std::vector<NurbsCurve> Pieces;
        Pieces.reserve(Count + 1);
        auto AppendLine = [&](Vec3 From, Vec3 To) -> bool
        {
            Deliver<NurbsCurve> Line = NurbsCurve::Line(From, To);
            if (!Line) return false;
            Pieces.push_back(std::move(Line.Payload));
            return true;
        };
        if (!AppendLine(B, Points[1])) return std::nullopt;
        for (size_t I = 1; I + 1 < Count; ++I)
            if (!AppendLine(Points[I], Points[I + 1])) return std::nullopt;
        if (!AppendLine(Points.back(), A)) return std::nullopt;
        Pieces.push_back(std::move(Arc.Payload));                                      // A → B closes the profile

        std::vector<NurbsSurface> Sides;
        Sides.reserve(Pieces.size());
        for (const NurbsCurve& Piece : Pieces)
        {
            Deliver<NurbsSurface> Side = NurbsSurface::Extrusion(Piece, F.Tangent, F.Length);
            if (!Side) return std::nullopt;
            Sides.push_back(std::move(Side.Payload));
        }
        Deliver<BrepBody> Rebuilt = BrepBody::Sew(Sides);
        if (!Rebuilt || !Rebuilt.Payload.Validate().Solid()) return std::nullopt;
        return std::move(Rebuilt.Payload);
    }
}

Deliver<std::vector<int>> BlendSolver::TangentChain(const BrepBody& Body, int SeedEdge) noexcept
{
    if (SeedEdge < 0 || SeedEdge >= static_cast<int>(Body.Edges.size()))
        return Deliver<std::vector<int>>::Reject(RefusalReason::DegenerateInput, "seed edge index is out of range");
    if (Body.Edges[SeedEdge].Coedges.size() != 2)
        return Deliver<std::vector<int>>::Reject(RefusalReason::Unsupported, "seed edge is not a manifold body edge");

    std::vector<int> Chain{ SeedEdge };
    for (size_t Cursor = 0; Cursor < Chain.size(); ++Cursor)
    {
        int CurrentIndex = Chain[Cursor];
        const BrepEdge& Current = Body.Edges[CurrentIndex];
        if (Current.Closed()) continue;
        for (int Vertex : { Current.VertexStart, Current.VertexEnd })
        {
            if (Vertex < 0 || Vertex >= static_cast<int>(Body.Vertices.size()))
                return Deliver<std::vector<int>>::Reject(RefusalReason::Unsupported, "chain edge has no valid endpoint vertex");
            const double CurrentParameter = Vertex == Current.VertexStart
                ? Current.Curve.DomainStart() : Current.Curve.DomainEnd();
            Vec3 CurrentTangent = Current.Curve.Tangent(CurrentParameter).Normalised();
            if (CurrentTangent.Length() <= Tol)
                return Deliver<std::vector<int>>::Reject(RefusalReason::DegenerateInput, "chain edge has a zero endpoint tangent");

            int Continuation = -1;
            for (size_t CandidateIndex = 0; CandidateIndex < Body.Edges.size(); ++CandidateIndex)
            {
                if (static_cast<int>(CandidateIndex) == CurrentIndex) continue;
                const BrepEdge& Candidate = Body.Edges[CandidateIndex];
                if (Candidate.Coedges.size() != 2 || Candidate.Closed() ||
                    (Candidate.VertexStart != Vertex && Candidate.VertexEnd != Vertex)) continue;
                const double CandidateParameter = Vertex == Candidate.VertexStart
                    ? Candidate.Curve.DomainStart() : Candidate.Curve.DomainEnd();
                Vec3 CandidateTangent = Candidate.Curve.Tangent(CandidateParameter).Normalised();
                if (CandidateTangent.Length() <= Tol ||
                    std::fabs(CurrentTangent.Dot(CandidateTangent)) < 1.0 - ScalarCriteria::GeometricTolerance) continue;
                if (Continuation >= 0 && Continuation != static_cast<int>(CandidateIndex))
                    return Deliver<std::vector<int>>::Reject(RefusalReason::Unsupported, "tangent chain branches ambiguously at a vertex");
                Continuation = static_cast<int>(CandidateIndex);
            }
            if (Continuation >= 0 && std::find(Chain.begin(), Chain.end(), Continuation) == Chain.end())
                Chain.push_back(Continuation);
        }
    }
    return Deliver<std::vector<int>>::Accept(std::move(Chain));
}

// The refusal texts below are literals with static storage, so a Refusal may carry them directly; the public Frame
//    copies the text into the caller's string. Returns nullptr when Out is filled.
static const char* CornerFrameRefusal(const BrepBody& Body, int Edge, EdgeCornerFrame& Out) noexcept
{
    if (Edge < 0 || Edge >= (int)Body.Edges.size()) return "edge index out of range";
    const BrepEdge& E = Body.Edges[Edge];
    if (E.Coedges.size() != 2) return "edge is not a manifold interior edge (a blend needs exactly two adjacent faces)";
    if (E.VertexStart < 0 || E.VertexEnd < 0) return "edge has no stored vertices";

    Vec3 P0 = Body.Vertices[E.VertexStart].Point, P1 = Body.Vertices[E.VertexEnd].Point;
    Vec3 Along = P1 - P0;
    double Length = Along.Length();
    if (Length <= Tol) return "edge is degenerate (zero length)";
    Vec3 Tangent = Along * (1.0 / Length);

    // The edge must be straight: the tool solids are prisms, so a curved edge would not be cut exactly.
    {
        std::vector<Vec3> Samples = Body.EdgePolyline(Edge, 1e-4);
        for (const Vec3& S : Samples)
        {
            Vec3 D = S - P0;
            double Deviation = (D - Tangent * D.Dot(Tangent)).Length();
            if (Deviation > 1e-6 * std::max(1.0, Length)) return "edge is not straight (blend of a curved edge is not supported)";
        }
    }

    int FaceA = Body.Coedges[E.Coedges[0]].Face, FaceB = Body.Coedges[E.Coedges[1]].Face;
    if (FaceA < 0 || FaceB < 0 || FaceA == FaceB) return "edge has invalid adjacent faces";

    Vec3 NA, NB;
    if (!PlanarNormal(Body, FaceA, NA) || !PlanarNormal(Body, FaceB, NB)) return "blend requires both adjacent faces to be planar (curvature detected)";
    if (std::fabs(Tangent.Dot(NA)) > 1e-6 || std::fabs(Tangent.Dot(NB)) > ScalarCriteria::DirectionTolerance) return "edge does not lie in both face planes";

    Vec3 Bisector = NA + NB;
    if (Bisector.Length() <= Tol) return "adjacent faces are opposite (degenerate corner)";
    Bisector = Bisector.Normalised();

    // In-face directions: perpendicular to the edge, lying in each face, pointing away from the edge across the face.
    //    Taking them as ±(Tangent × N) and choosing the sign that leads to the face's interior keeps this correct for
    //    both convex and concave edges.
    auto InFace = [&](int Face, Vec3 N) -> Vec3
    {
        Vec3 Direction = Tangent.Cross(N);
        if (Direction.Length() <= Tol) return Vec3{};
        Direction = Direction.Normalised();
        BrepBody::FaceTriangles Triangles = Body.TessellateFace(Face);
        Vec3 Centroid{}; double Weight = 0.0;
        for (size_t I = 0; I + 2 < Triangles.Triangles.size(); I += 3)
        {
            Vec3 A = Triangles.Positions[Triangles.Triangles[I]], B = Triangles.Positions[Triangles.Triangles[I + 1]], C = Triangles.Positions[Triangles.Triangles[I + 2]];
            double Area = (B - A).Cross(C - A).Length() * 0.5;
            Centroid = Centroid + (A + B + C) * (Area / 3.0);
            Weight += Area;
        }
        if (Weight <= Tol) return Vec3{};
        Centroid = Centroid * (1.0 / Weight);
        double Side = (Centroid - P0).Dot(Direction);
        return Side >= 0.0 ? Direction : Direction * -1.0;
    };
    Vec3 InA = InFace(FaceA, NA), InB = InFace(FaceB, NB);
    if (InA.Length() <= Tol || InB.Length() <= Tol) return "cannot resolve the in-face direction of the edge";

    // Interior dihedral: the angle the material subtends at the edge, measured between the two in-face directions.
    double Dihedral = std::acos(ScalarCriteria::Clamp(InA.Dot(InB), -1.0, 1.0));
    if (Dihedral <= 1e-6 || Dihedral >= ScalarCriteria::Pi - 1e-6) return "faces are tangent or folded at this edge (no corner to blend)";

    Out.Start = P0; Out.End = P1; Out.Tangent = Tangent;
    Out.NormalA = NA; Out.NormalB = NB; Out.InA = InA; Out.InB = InB;
    Out.Bisector = Bisector; Out.Length = Length; Out.Dihedral = Dihedral;
    Out.FaceA = FaceA; Out.FaceB = FaceB;
    return nullptr;
}

bool BlendSolver::Frame(const BrepBody& Body, int Edge, EdgeCornerFrame& Out, std::string& Refusal) noexcept
{
    if (const char* Text = CornerFrameRefusal(Body, Edge, Out)) { Refusal = Text; return false; }
    return true;
}

double BlendSolver::TangentSetBack(const EdgeCornerFrame& F, double Radius) noexcept
{
    return Radius / std::tan(F.Dihedral * 0.5);
}

double BlendSolver::ChamferRemoval(const EdgeCornerFrame& F, double SetBack) noexcept
{
    // Triangle of sides SetBack, SetBack with the included interior angle, swept along the edge.
    return F.Length * 0.5 * SetBack * SetBack * std::sin(F.Dihedral);
}

double BlendSolver::FilletRemoval(const EdgeCornerFrame& F, double Radius) noexcept
{
    // Corner kite (two tangent lengths) minus the circular sector the roll leaves behind, swept along the edge.
    double T = TangentSetBack(F, Radius);
    return F.Length * (Radius * T - Radius * Radius * (ScalarCriteria::Pi - F.Dihedral) * 0.5);
}

Deliver<BrepBody> BlendSolver::ChamferEdge(const BrepBody& Body, int Edge, double SetBack) noexcept
{
    if (std::optional<CylinderCap> Cap = NativeCylinderCap(Body, Edge)) return ChamferCylinderCap(*Cap, SetBack);
    EdgeCornerFrame F;
    if (const char* Why = CornerFrameRefusal(Body, Edge, F)) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, Why);
    if (SetBack <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "set-back is zero or negative");

    // A re-entrant edge of a capped prism is a profile operation, not the exterior wedge removal below.
    if (std::optional<BrepBody> Reflex = PrismaticReflexBlend(Body, F, SetBack, false))
        return Deliver<BrepBody>::Accept(std::move(*Reflex));

    // The cut plane passes through the two set-back points, square to the outward bisector.
    Vec3 A = F.Start + F.InA * SetBack, B = F.Start + F.InB * SetBack;
    double Offset = ((A + B) * 0.5 - F.Start).Dot(F.Bisector);
    const double Target = Body.Validate().Volume - ChamferRemoval(F, SetBack);
    // A plane chamfer is exact geometry. The B-rep volume reporter itself is tessellated, so three cubic microns is
    // its observed integration floor on this model — it is 2e-10 of the part, not an approximation allowance. An
    // appreciably different result is refused rather than silently shipping a mis-cut part.
    constexpr double Accept = 3e-6;

    // Ladder of cutter shapes, coarsest-fitting first. Negative margins stop the cutter just short of the edge's
    // endpoints (relative to the edge length); positive ones run it past (relative to the set-back). Each candidate
    // must be both closed and within the numerical-integration floor of the exact local wedge.
    static const double Margins[] = { -1e-8, -1e-7, -1e-6, -1e-5, -1e-4, -1e-3,
                                           ScalarCriteria::GeometricTolerance, 1e-7, 1e-6, 1e-5, 1e-4, 1e-3,
                                           0.01, 0.05, 0.13, 0.29, 0.53, 1.0, 2.0, 3.0 };
    static const double HalfWidths[] = { 0.55, 0.8, 1.2, 2.0, 3.0 };

    Deliver<BrepBody> Best = Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no cutter placement produced a valid solid");
    double BestError = ScalarCriteria::Infinity;
    for (double Width : HalfWidths)
        for (double Margin : Margins)
        {
            double Along = Margin < 0.0 ? F.Length * Margin : SetBack * Margin;
            Deliver<BrepBody> Cutter = CornerCutter(F, Offset, SetBack * Width, Along, SetBack * 3.0);
            if (!Cutter) continue;
            Deliver<BrepBody> Cut = IntersectionSolver::Combine(Body, Cutter.Payload, BodyOperation::Subtract);
            if (!Cut) continue;
            BodyReport Report = Cut.Payload.Validate();
            if (!Report.Solid()) continue;
            double Error = std::fabs(Report.Volume - Target);
            if (Error < BestError) { BestError = Error; Best = std::move(Cut); }
        }
    if (Best && BestError <= Accept) return Best;
    return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no cutter placement reached the exact chamfer tolerance");
}

Deliver<BrepBody> BlendSolver::FilletEdge(const BrepBody& Body, int Edge, double Radius) noexcept
{
    if (std::optional<CylinderCap> Cap = NativeCylinderCap(Body, Edge)) return FilletCylinderCap(*Cap, Radius);
    if (std::optional<PlaneCylinderRoot> Root = PlaneCylinderBossRoot(Body, Edge))
        return FilletPlaneCylinderBossRoot(*Root, Radius);
    if (std::optional<PlaneConeRoot> Root = PlaneConeBossRoot(Body, Edge))
        return FilletPlaneConeBossRoot(*Root, Radius);
    EdgeCornerFrame F;
    if (const char* Why = CornerFrameRefusal(Body, Edge, F)) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, Why);
    if (Radius <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "radius is zero or negative");

    // A prismatic reflex root is rebuilt from its 2D profile so the roll stays on the material side of the corner.
    if (std::optional<BrepBody> Reflex = PrismaticReflexBlend(Body, F, Radius, true))
        return Deliver<BrepBody>::Accept(std::move(*Reflex));

    // 1. Cut the corner back to where the rolling ball touches each face.
    double T = TangentSetBack(F, Radius);
    Vec3 TangentA = F.Start + F.InA * T, TangentB = F.Start + F.InB * T;
    Deliver<BrepBody> Wedged = ChamferEdge(Body, Edge, T);
    if (!Wedged) return Deliver<BrepBody>::Reject(Wedged.Denial.Reason, Wedged.Denial.Detail);

    Vec3 Centre = F.Start - F.Bisector * (Radius / std::sin(F.Dihedral * 0.5));
    Vec3 AxisU = F.Bisector, AxisV = F.Tangent.Cross(F.Bisector).Normalised();
    const double Target = Body.Validate().Volume - FilletRemoval(F, Radius);

    // 2. Re-seat the flat the cut left onto the tangent cylinder. The two cap edges of that flat are still straight
    //    chords; on a simple corner rebuilding them as arcs on the same cylinder is what makes the volume exact, but
    //    where the surrounding topology is more involved the rebuild can over-correct. Rather than guess, build the
    //    body both ways and keep whichever lands closer to the closed-form volume — the operation checks its own work.
    auto Build = [&](bool RebuildCaps) -> Deliver<BrepBody>
    {
        BrepBody Result = Wedged.Payload;
        int Flat = -1; double Closest = 0.999;
        for (size_t Face = 0; Face < Result.Faces.size(); ++Face)
        {
            Vec3 N = Result.FaceNormal((int)Face, 0.5, 0.5);
            if (N.Length() <= Tol) continue;
            double Alignment = N.Normalised().Dot(F.Bisector);
            if (Alignment > Closest) { Closest = Alignment; Flat = (int)Face; }
        }
        if (Flat < 0) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "the set-back cut did not produce a chamfer face to roll");

        Deliver<NurbsCurve> Profile = NurbsCurve::ArcThreePoints(TangentA, Centre + F.Bisector * Radius, TangentB);
        if (!Profile) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "fillet arc section is degenerate");
        Deliver<NurbsSurface> Roll = NurbsSurface::Extrusion(Profile.Payload, F.Tangent, F.Length);
        if (!Roll) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "fillet surface is degenerate");
        Result.Faces[Flat].Surface = std::move(Roll.Payload);

        // The swept arc's natural normal may agree or disagree with the flat it replaces; decide from the geometry.
        Vec3 Facing = Result.FaceNormal(Flat, 0.5, 0.5);
        if (Facing.Length() > Tol && Facing.Normalised().Dot(F.Bisector) < 0.0) Result.Faces[Flat].Reversed = !Result.Faces[Flat].Reversed;

        if (RebuildCaps)
            for (int Loop : Result.Faces[Flat].Loops)
                for (int Coedge : Result.Loops[Loop].Coedges)
                {
                    int EdgeIndex = Result.Coedges[Coedge].Edge;
                    Vec3 S = Result.CoedgeStart(Coedge), E = Result.CoedgeEnd(Coedge);
                    if ((E - S).Length() <= Tol) continue;
                    int Other = -1;
                    for (int Ce : Result.Edges[EdgeIndex].Coedges) if (Result.Coedges[Ce].Face != Flat) { Other = Result.Coedges[Ce].Face; break; }
                    if (Other < 0) continue;
                    Vec3 PlaneNormal;
                    if (!PlanarNormal(Result, Other, PlaneNormal)) continue;
                    double Facing2 = PlaneNormal.Dot(F.Tangent);
                    if (std::fabs(Facing2) <= 1e-9) continue;                              // a rail of the roll: already exact
                    if (std::fabs(std::fabs(Facing2) - 1.0) > ScalarCriteria::DirectionTolerance) continue;              // a mitred end sections in an ellipse, not an arc

                    auto AngleOf = [&](Vec3 P)
                    {
                        Vec3 Radial = P - (Centre + F.Tangent * (P - Centre).Dot(F.Tangent));
                        return std::atan2(Radial.Dot(AxisV), Radial.Dot(AxisU));
                    };
                    double Sweep = AngleOf(E) - AngleOf(S);
                    while (Sweep >  ScalarCriteria::Pi) Sweep -= 2.0 * ScalarCriteria::Pi;
                    while (Sweep < -ScalarCriteria::Pi) Sweep += 2.0 * ScalarCriteria::Pi;
                    double Middle = AngleOf(S) + Sweep * 0.5;
                    Vec3 Radial = AxisU * std::cos(Middle) + AxisV * std::sin(Middle);
                    double Slide = (PlaneNormal.Dot(S - Centre) - Radius * PlaneNormal.Dot(Radial)) / Facing2;
                    Deliver<NurbsCurve> Section = NurbsCurve::ArcThreePoints(S, Centre + F.Tangent * Slide + Radial * Radius, E);
                    if (Section) Result.Edges[EdgeIndex].Curve = std::move(Section.Payload);
                }

        for (BrepCoedge& C : Result.Coedges) C.Trace.clear();
        Result.Orient();
        return Deliver<BrepBody>::Accept(std::move(Result));
    };

    // Score both cap treatments against the closed form. A fillet must also leave MORE material than the flat cut it
    //    replaces — the roll is added back into the corner — so a candidate that comes out below the wedge volume is
    //    geometrically wrong however close its number looks, and is rejected outright.
    const double WedgeVolume = Wedged.Payload.Validate().Volume;
    Deliver<BrepBody> Plain = Build(false), Arced = Build(true);
    auto Score = [&](const Deliver<BrepBody>& D) -> double
    {
        if (!D) return ScalarCriteria::Infinity;
        BodyReport R = D.Payload.Validate();
        if (!R.Solid()) return ScalarCriteria::Infinity;
        if (R.Volume < WedgeVolume - std::max(std::fabs(WedgeVolume), 1.0) * 1e-9) return ScalarCriteria::Infinity;
        return std::fabs(R.Volume - Target);
    };
    double ScorePlain = Score(Plain), ScoreArced = Score(Arced);
    if (std::isfinite(ScoreArced) || std::isfinite(ScorePlain))
        return ScoreArced <= ScorePlain ? std::move(Arced) : std::move(Plain);
    // Neither candidate is admissible: fall back to the tangent-set-back flat, which is a valid solid and is what a
    //    chamfer at the fillet's own set-back would have produced.
    return Wedged;
}

Deliver<BrepBody> BlendSolver::FilletEdges(const BrepBody& Body, const std::vector<int>& SeedEdges,
                                           double Radius, int* AppliedChains) noexcept
{
    if (AppliedChains) *AppliedChains = 0;
    if (!Body.Validate().Solid())
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "multi-edge fillet requires a valid solid body");
    if (Radius <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "radius is zero or negative");
    if (SeedEdges.empty())
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "multi-edge fillet seed set is empty");

    struct Signature
    {
        Vec3 Start, Middle, End, Centre;
    };
    struct Target
    {
        std::vector<int> Chain;
        Signature Geometry;
    };
    auto EdgeSignature = [](const BrepBody& Source, int Edge) noexcept -> Signature
    {
        const NurbsCurve& Curve = Source.Edges[Edge].Curve;
        double T0 = Curve.DomainStart(), T1 = Curve.DomainEnd();
        Signature Result{ Curve.Sample(T0), Curve.Sample(0.5 * (T0 + T1)), Curve.Sample(T1), {} };
        Result.Centre = (Result.Start + Result.Middle + Result.End) / 3.0;
        return Result;
    };
    auto Intersects = [](const std::vector<int>& A, const std::vector<int>& B) noexcept
    {
        for (int Value : A) if (std::find(B.begin(), B.end(), Value) != B.end()) return true;
        return false;
    };

    std::vector<int> OrderedSeeds = SeedEdges;
    std::sort(OrderedSeeds.begin(), OrderedSeeds.end());
    OrderedSeeds.erase(std::unique(OrderedSeeds.begin(), OrderedSeeds.end()), OrderedSeeds.end());
    std::vector<Target> Targets;
    for (int Seed : OrderedSeeds)
    {
        if (Seed < 0 || Seed >= static_cast<int>(Body.Edges.size()))
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "multi-edge fillet seed is out of range");
        Deliver<std::vector<int>> FoundChain = TangentChain(Body, Seed);
        if (!FoundChain) return Deliver<BrepBody>::Reject(FoundChain.Denial.Reason, FoundChain.Denial.Detail);
        std::vector<int> Effective{ Seed };
        const bool CurvedPropagation = FoundChain.Payload.size() > 1 &&
            std::all_of(FoundChain.Payload.begin(), FoundChain.Payload.end(), [&](int Edge)
            {
                CurveClassification Classification = Body.Edges[Edge].Curve.Classification;
                return Classification == CurveClassification::Arc || Classification == CurveClassification::Circle;
            });
        if (CurvedPropagation) Effective = FoundChain.Payload;
        std::sort(Effective.begin(), Effective.end());

        bool Duplicate = false;
        for (const Target& Existing : Targets)
        {
            if (Existing.Chain == Effective) { Duplicate = true; break; }
            if (Intersects(Existing.Chain, Effective))
                return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "multi-edge fillet seeds overlap inconsistent tangent chains");
        }
        if (!Duplicate) Targets.push_back({ Effective, EdgeSignature(Body, Effective.front()) });
    }

    // Four mutually parallel edges form a complete cross-section family. Rebuild them together as one exact rounded
    // prism so opposite rolls receive a global 2r clearance check instead of four unrelated local operations.
    if(Targets.size()==4&&std::all_of(Targets.begin(),Targets.end(),[](const Target&T){return T.Chain.size()==1;}))
    {
        std::vector<int> Selected;for(const Target&T:Targets)Selected.push_back(T.Chain.front());
        if(auto Blind=ClassifyBlindBoxPrism(Body,Selected))
        {
            auto Result=BuildRoundedBlindPrism(*Blind,Radius);
            if(Result&&AppliedChains)*AppliedChains=4;
            return Result;
        }
        if(auto SideBlind=ClassifySideBlindPrism(Body,Selected))
        {
            auto Result=BuildRoundedSideBlindPrism(*SideBlind,Radius);
            if(Result&&AppliedChains)*AppliedChains=4;
            return Result;
        }
        if(auto SideStepped=ClassifySideSteppedBlindPrism(Body,Selected))
        {
            auto Result=BuildRoundedSideSteppedBlindPrism(*SideStepped,Radius);
            if(Result&&AppliedChains)*AppliedChains=4;
            return Result;
        }
        if(auto SideSteppedSet=ClassifySideSteppedBlindSet(Body,Selected))
        {
            auto Result=BuildRoundedSideSteppedBlindSet(*SideSteppedSet,Radius);
            if(Result&&AppliedChains)*AppliedChains=4;
            return Result;
        }
        if(auto Stepped=ClassifySteppedBlindPrism(Body,Selected))
        {
            auto Result=BuildRoundedSteppedBlindPrism(*Stepped,Radius);
            if(Result&&AppliedChains)*AppliedChains=4;
            return Result;
        }
        if(auto DualStepped=ClassifyDualSteppedBlindPrism(Body,Selected))
        {
            auto Result=BuildRoundedDualSteppedBlindPrism(*DualStepped,Radius);
            if(Result&&AppliedChains)*AppliedChains=4;
            return Result;
        }
        auto ShapeReport=Body.Validate();
        size_t BlindCount=Body.Faces.size()>=8&&(Body.Faces.size()-6)%2==0?(Body.Faces.size()-6)/2:0;
        if(ShapeReport.Genus==0&&BlindCount>=1&&Body.Vertices.size()==8+2*BlindCount&&Body.Edges.size()==12+3*BlindCount&&
           Body.Coedges.size()==24+6*BlindCount&&Body.Loops.size()==6+3*BlindCount)
            return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                "blind-bore prism is outside the exact route for one to eight separated cavities");
        if(auto Perforated=ClassifyPerforatedBoxPrism(Body,Selected))
        {
            auto Result=BuildRoundedBoxPrism(Perforated->Box,0,Radius,Perforated->Holes);
            if(Result&&AppliedChains)*AppliedChains=4;
            return Result;
        }
        if(ShapeReport.Genus!=0)
            return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                "perforated parallel-edge family is outside the bounded axis-parallel bore route");
        std::vector<int> FirstCorner;for(size_t E=0;E<Body.Edges.size();++E)
            if(Body.Edges[E].VertexStart==0||Body.Edges[E].VertexEnd==0)FirstCorner.push_back(static_cast<int>(E));
        if(auto Box=ClassifyOrthogonalBoxCorner(Body,FirstCorner))
        {
            int Family=-1;bool Same=true;
            for(const Target&T:Targets){const BrepEdge&E=Body.Edges[T.Chain.front()];Vec3 D=(Body.Vertices[E.VertexEnd].Point-Body.Vertices[E.VertexStart].Point).Normalised();
                int F=std::fabs(D.Dot(Box->X))>1.0-ScalarCriteria::GeometricTolerance?0:(std::fabs(D.Dot(Box->Y))>1.0-ScalarCriteria::GeometricTolerance?1:(std::fabs(D.Dot(Box->Z))>1.0-ScalarCriteria::GeometricTolerance?2:-1));
                if(Family<0)Family=F;else if(F!=Family)Same=false;}
            if(Same&&Family>=0){auto Result=BuildRoundedBoxPrism(*Box,Family,Radius);if(Result&&AppliedChains)*AppliedChains=4;return Result;}
        }
    }

    // A complete twelve-edge rectangular network is the first bounded blend/blend composition: inset planes, twelve
    // exact cylinders, and eight copies of the same spherical octant corner transition.
    if (Targets.size() == 12 && std::all_of(Targets.begin(), Targets.end(), [](const Target& T) { return T.Chain.size() == 1; }))
    {
        std::vector<int> FirstCorner;
        for (size_t Edge=0; Edge<Body.Edges.size(); ++Edge)
            if (Body.Edges[Edge].VertexStart==0 || Body.Edges[Edge].VertexEnd==0) FirstCorner.push_back(static_cast<int>(Edge));
        if (std::optional<OrthogonalBoxCorner> Box = ClassifyOrthogonalBoxCorner(Body, FirstCorner))
        {
            Deliver<BrepBody> Result=BuildRoundedOrthogonalBox(*Box,Radius);
            if(Result&&AppliedChains)*AppliedChains=12;
            return Result;
        }
    }

    // The first actual corner patch is deliberately exact and bounded: three straight orthogonal edges of a six-face
    // rectangular solid meeting at one vertex receive equal-radius cylinders and one rational spherical octant.
    if (Targets.size() == 3 && std::all_of(Targets.begin(), Targets.end(), [](const Target& T) { return T.Chain.size() == 1; }))
    {
        std::vector<int> CornerEdges;
        for (const Target& T : Targets) CornerEdges.push_back(T.Chain.front());
        if (std::optional<OrthogonalBoxCorner> Corner = ClassifyOrthogonalBoxCorner(Body, CornerEdges))
        {
            Deliver<BrepBody> Result = BuildOrthogonalBoxCorner(*Corner, Radius);
            if (Result && AppliedChains) *AppliedChains = 3;
            return Result;
        }
    }

    // Other shared-vertex sets are still corner-resolution requests, not independent rolls; reject them transactionally
    // before either operation changes the working copy.
    for (size_t A = 0; A < Targets.size(); ++A)
        for (size_t B = A + 1; B < Targets.size(); ++B)
            for (int EdgeA : Targets[A].Chain)
                for (int EdgeB : Targets[B].Chain)
                {
                    const BrepEdge& EA = Body.Edges[EdgeA];
                    const BrepEdge& EB = Body.Edges[EdgeB];
                    for (int VA : { EA.VertexStart, EA.VertexEnd })
                        for (int VB : { EB.VertexStart, EB.VertexEnd })
                            if (VA >= 0 && VA == VB)
                                return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                    "multi-edge fillet chains share a vertex (corner resolution is not supported)");
                }

    auto VecLess = [](Vec3 A, Vec3 B) noexcept
    {
        if (A.X != B.X) return A.X < B.X;
        if (A.Y != B.Y) return A.Y < B.Y;
        return A.Z < B.Z;
    };
    auto SamePoint = [](Vec3 A, Vec3 B) noexcept { return A.X == B.X && A.Y == B.Y && A.Z == B.Z; };
    auto Less = [&](const Target& A, const Target& B) noexcept
    {
        if (!SamePoint(A.Geometry.Centre, B.Geometry.Centre)) return VecLess(A.Geometry.Centre, B.Geometry.Centre);
        if (!SamePoint(A.Geometry.Middle, B.Geometry.Middle)) return VecLess(A.Geometry.Middle, B.Geometry.Middle);
        Vec3 ALow = VecLess(A.Geometry.Start, A.Geometry.End) ? A.Geometry.Start : A.Geometry.End;
        Vec3 AHigh = VecLess(A.Geometry.Start, A.Geometry.End) ? A.Geometry.End : A.Geometry.Start;
        Vec3 BLow = VecLess(B.Geometry.Start, B.Geometry.End) ? B.Geometry.Start : B.Geometry.End;
        Vec3 BHigh = VecLess(B.Geometry.Start, B.Geometry.End) ? B.Geometry.End : B.Geometry.Start;
        if (!SamePoint(ALow, BLow)) return VecLess(ALow, BLow);
        if (!SamePoint(AHigh, BHigh)) return VecLess(AHigh, BHigh);
        return A.Chain.front() < B.Chain.front();
    };
    std::sort(Targets.begin(), Targets.end(), Less);

    const double MatchTolerance = ScalarCriteria::ScaledPositionTolerance * std::max(1.0, Body.Bounds().Diagonal());
    auto Resolve = [&](const BrepBody& Working, const Signature& Wanted) noexcept -> int
    {
        int Found = -1; double Best = ScalarCriteria::Infinity; bool Ambiguous = false;
        const double TieTolerance = MatchTolerance * 1e-6;
        for (size_t Edge = 0; Edge < Working.Edges.size(); ++Edge)
        {
            if (Working.Edges[Edge].Coedges.size() != 2) continue;
            Signature Candidate = EdgeSignature(Working, static_cast<int>(Edge));
            double Direct = std::max({ Candidate.Start.Distance(Wanted.Start), Candidate.Middle.Distance(Wanted.Middle),
                                       Candidate.End.Distance(Wanted.End) });
            double Reverse = std::max({ Candidate.Start.Distance(Wanted.End), Candidate.Middle.Distance(Wanted.Middle),
                                        Candidate.End.Distance(Wanted.Start) });
            double Error = std::min(Direct, Reverse);
            if (Error < Best - TieTolerance)
            {
                Best = Error; Found = static_cast<int>(Edge); Ambiguous = false;
            }
            else if (Error <= MatchTolerance && std::fabs(Error - Best) <= TieTolerance) Ambiguous = true;
        }
        return Best <= MatchTolerance && !Ambiguous ? Found : -1;
    };

    BrepBody Working = Body;
    for (const Target& TargetEdge : Targets)
    {
        int Current = Resolve(Working, TargetEdge.Geometry);
        if (Current < 0)
            return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                "multi-edge fillet target is missing or geometrically ambiguous after an earlier independent roll");
        Deliver<BrepBody> Rolled = FilletEdge(Working, Current, Radius);
        if (!Rolled) return Deliver<BrepBody>::Reject(Rolled.Denial.Reason, Rolled.Denial.Detail);
        Working = std::move(Rolled.Payload);
    }
    if (!Working.Validate().Solid())
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "multi-edge fillet did not return a valid solid");
    if (AppliedChains) *AppliedChains = static_cast<int>(Targets.size());
    return Deliver<BrepBody>::Accept(std::move(Working));
}

Deliver<BrepBody> BlendSolver::PushFace(const BrepBody& Body, int Face, double Distance) noexcept
{
    if (Face < 0 || Face >= (int)Body.Faces.size()) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "face index out of range");
    if (std::fabs(Distance) <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "push distance is zero");
    if (std::optional<ConeSide> Cone = NativeConeSideFace(Body, Face)) return PushConeSide(*Cone, Distance);
    if (std::optional<ConeCap> Cap = NativeConeCapFace(Body, Face)) return PushConeCap(*Cap, Distance);
    if (std::optional<CylinderCap> Cylinder = NativeCylinderSideFace(Body, Face)) return PushCylinderSide(*Cylinder, Distance);
    if (std::optional<CylinderCap> Cap = NativeCylinderCapFace(Body, Face)) return PushCylinderCap(*Cap, Distance);
    Vec3 Normal;
    if (!PlanarNormal(Body, Face, Normal)) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "push requires a planar face");

    // Prefer the exact topological construction.  Keep the Boolean path below as a conservative fallback for any
    // future face type that the direct construction cannot sew into a closed manifold body.
    Deliver<BrepBody> Direct = DirectPlanarPush(Body, Face, Normal, Distance);
    if (Direct) return Direct;

    // The tool is the face's own outline swept along the normal. It is started *behind* the face, inside the material,
    //    so the tool's side walls are never coincident with the body's — the boolean refuses tangent contact, and a
    //    tool that merely touches the face would be exactly that case.
    BrepBody::FaceTriangles Triangles = Body.TessellateFace(Face);
    if (Triangles.Triangles.empty()) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "face has no area to push");

    // How far behind the face the tool starts. It only has to clear the face plane so the two solids overlap rather
    //    than merely touch (a touching tool is the coincident-face case the boolean refuses); it must NOT reach the
    //    far side of the body, or the sweep punches out through the opposite wall and the "push" becomes a slot. A
    //    small fraction of the body is both, and the seat cancels exactly in the union.
    Box3 Bounds = Body.Bounds();
    Vec3 Span = Bounds.Extent();
    double Reach = std::max({ Span.X, Span.Y, Span.Z, 1.0 });
    double Thickness = ScalarCriteria::Infinity;
    for (size_t Other = 0; Other < Body.Faces.size(); ++Other)
    {
        if ((int)Other == Face) continue;
        Vec3 OtherNormal;
        if (!PlanarNormal(Body, (int)Other, OtherNormal)) continue;
        if (OtherNormal.Dot(Normal) > -0.5) continue;                                    // only walls facing back at us
        BrepBody::FaceTriangles Far = Body.TessellateFace((int)Other);
        for (const Vec3& P : Far.Positions)
        {
            double Behind = (Triangles.Positions.empty() ? 0.0 : (Triangles.Positions.front() - P).Dot(Normal));
            if (Behind > Tol) Thickness = std::min(Thickness, Behind);
        }
    }
    double Depth = std::isfinite(Thickness) ? std::min(Reach, Thickness * 0.5) : Reach * 0.25;
    Depth = std::max(Depth, Reach * 1e-3);                                               // still a real overlap

    // Outline of the face: its outer loop, sampled in order.
    if (Body.Faces[Face].Loops.empty()) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "face has no outer loop");
    int Outer = Body.Faces[Face].Loops.front();
    std::vector<Vec3> Outline;
    for (int Coedge : Body.Loops[Outer].Coedges)
    {
        NurbsCurve C = Body.CoedgeCurve(Coedge);
        std::vector<Vec3> Points; C.Tessellate(Points, nullptr, 1e-4);
        for (size_t I = 0; I + 1 < Points.size(); ++I)
            if (Outline.empty() || !Outline.back().Coincident(Points[I], Tol)) Outline.push_back(Points[I]);
    }
    if (Outline.size() < 3) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "face outline is degenerate");
    if (Outline.front().Coincident(Outline.back(), Tol)) Outline.pop_back();

    // Slide the outline back inside the body, then sweep it past the target depth.
    //    The outline is also pulled in by a hair: a tool whose walls lie exactly on the body's walls is the coincident-
    //    face case the boolean refuses outright ("surface singularity or seam corner"), because the two boundaries meet
    //    along a whole face rather than crossing. The inset is a few parts per million of the body, far below the
    //    tessellation tolerance the volumes are checked at, and it makes every contact transversal.
    Vec3 Centroid{};
    for (const Vec3& P : Outline) Centroid = Centroid + P;
    Centroid = Centroid * (1.0 / double(Outline.size()));
    const double Inset = std::max(Span.Length(), 1.0) * 1e-6;

    Vec3 Base = Normal * -Depth;
    std::vector<Vec3> Shifted;
    Shifted.reserve(Outline.size() + 1);
    for (const Vec3& P : Outline)
    {
        Vec3 Radial = P - Centroid;
        Radial = Radial - Normal * Radial.Dot(Normal);                                   // stay in the face's plane
        double Reach = Radial.Length();
        Vec3 Pulled = Reach > Inset ? P - Radial * (Inset / Reach) : P;
        Shifted.push_back(Pulled + Base);
    }
    Shifted.push_back(Shifted.front());

    Deliver<NurbsCurve> Loop = NurbsCurve::Polyline(Shifted, true);
    if (!Loop) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "face outline does not close");
    Deliver<BrepBody> Tool = BrepBody::Extrude(Loop.Payload, Normal, Depth + std::fabs(Distance));
    if (!Tool) return Deliver<BrepBody>::Reject(Tool.Denial.Reason, Tool.Denial.Detail);

    Deliver<BrepBody> Result = Distance > 0.0
        ? IntersectionSolver::Combine(Body, Tool.Payload, BodyOperation::Union)
        : IntersectionSolver::Combine(Body, Tool.Payload, BodyOperation::Subtract);
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, Result.Denial.Detail);

    // The seam where the tool met the face comes back as a degree-3 interpolation of the intersection polyline even
    //    where it is dead straight (arc length == chord to 1e-9). Those splines are geometrically right but they make
    //    the next boolean non-generic: a later chamfer of an edge touching this seam splits the body into two hulls.
    //    Snap any provably straight seam back to an exact line so pushed bodies stay blendable.
    BrepBody Clean = std::move(Result.Payload);
    for (BrepEdge& E : Clean.Edges)
    {
        if (E.Curve.Degree <= 1) continue;
        if (E.VertexStart < 0 || E.VertexEnd < 0 || E.Closed()) continue;
        Vec3 A = Clean.Vertices[E.VertexStart].Point, B = Clean.Vertices[E.VertexEnd].Point;
        double Chord = (B - A).Length();
        if (Chord <= Tol) continue;
        if (std::fabs(E.Curve.Length() - Chord) > ScalarCriteria::ScaledPositionTolerance * std::max(1.0, Chord)) continue;  // genuinely curved
        Deliver<NurbsCurve> Straight = NurbsCurve::Line(A, B);
        if (Straight) E.Curve = std::move(Straight.Payload);
    }
    for (BrepCoedge& C : Clean.Coedges) C.Trace.clear();
    return Deliver<BrepBody>::Accept(std::move(Clean));
}

bool BlendSolver::ValidateAsymmetricSpecification(const AsymmetricBlendSpecification& Specification,
                                                    std::string& Refusal) noexcept
{
    if (Specification.MinimumClearance < 0.0 || !std::isfinite(Specification.MinimumClearance))
    { Refusal = "minimum clearance is invalid"; return false; }
    if (Specification.Classification == AsymmetricSupportClassification::EqualRadiusAsymmetricPlanes)
    {
        if (!std::isfinite(Specification.Low.Radius) || !std::isfinite(Specification.High.Radius) ||
            Specification.Low.Radius <= ScalarCriteria::MergeTolerance ||
            std::fabs(Specification.Low.Radius - Specification.High.Radius) > ScalarCriteria::CircularTolerance)
        { Refusal = "equal-radius plane mode requires equal positive radii"; return false; }
        if (Specification.Low.Normal.Length() <= ScalarCriteria::GeometricTolerance ||
            Specification.High.Normal.Length() <= ScalarCriteria::GeometricTolerance ||
            std::fabs(std::fabs(Specification.Low.Normal.Normalised().Dot(Specification.High.Normal.Normalised())) - 1.0) > ScalarCriteria::AngularTolerance)
        { Refusal = "equal-radius plane supports must be parallel and non-degenerate"; return false; }
        if ((Specification.High.Centre - Specification.Low.Centre).Length() <=
            2.0 * Specification.Low.Radius + std::max(Specification.MinimumClearance, ScalarCriteria::GeometricTolerance))
        { Refusal = "equal-radius plane supports have no positive ligament"; return false; }
    }
    else if (!ValidateAsymmetricEndpointPair(Specification.Low, Specification.High,
                                               Specification.MinimumClearance, Refusal)) return false;
    if (Specification.BlendRadius < 0.0 || !std::isfinite(Specification.BlendRadius))
    { Refusal = "blend radius is invalid"; return false; }
    const bool HasAngles = std::fabs(Specification.Low.EndpointAngle) > ScalarCriteria::SweepTolerance ||
                           std::fabs(Specification.High.EndpointAngle) > ScalarCriteria::SweepTolerance;
    if (HasAngles && (Specification.Low.EndpointAngle <= ScalarCriteria::SweepTolerance ||
                      Specification.High.EndpointAngle <= ScalarCriteria::SweepTolerance ||
                      Specification.Low.EndpointAngle >= ScalarCriteria::TwoPi - ScalarCriteria::SweepTolerance ||
                      Specification.High.EndpointAngle >= ScalarCriteria::TwoPi - ScalarCriteria::SweepTolerance))
    { Refusal = "endpoint angles must be finite partial sweeps"; return false; }
    if (Specification.Classification == AsymmetricSupportClassification::VariableRadiusRoll)
    {
        if (Specification.BlendRadius <= ScalarCriteria::MergeTolerance)
        { Refusal = "variable-radius roll requires a positive blend radius"; return false; }
        if (!Specification.RadiusLaw.Positive() ||
            Specification.RadiusLaw.Start <= ScalarCriteria::MergeTolerance ||
            std::fabs(Specification.RadiusLaw.Start - Specification.Low.Radius) > ScalarCriteria::CircularTolerance ||
            std::fabs(Specification.RadiusLaw.End - Specification.High.Radius) > ScalarCriteria::CircularTolerance)
        { Refusal = "variable-radius law does not match endpoint radii"; return false; }
    }
    return true;
}

Deliver<BrepBody> BlendSolver::ReconstructAsymmetricFrustum(const AsymmetricBlendSpecification& Specification) noexcept
{
    std::string Refusal;
    if (Specification.Classification != AsymmetricSupportClassification::TaperedFrustum &&
        Specification.Classification != AsymmetricSupportClassification::UnequalRadialCaps &&
        Specification.Classification != AsymmetricSupportClassification::EqualRadiusAsymmetricPlanes)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "frustum reconstruction requires a supported endpoint mode");
    if (!ValidateAsymmetricSpecification(Specification, Refusal))
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "invalid asymmetric frustum specification");
    Vec3 Delta = Specification.High.Centre - Specification.Low.Centre;
    double Height = Delta.Length();
    if (Height <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "endpoint supports have zero axial span");
    Vec3 Axis = Delta / Height;
    if (std::fabs(std::fabs(Axis.Dot(Specification.Low.Normal.Normalised())) - 1.0) > ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "endpoint centres are not on the support axis");
    if (std::fabs(std::fabs(Axis.Dot(Specification.High.Normal.Normalised())) - 1.0) > ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "endpoint centres are not on the support axis");
    Deliver<BrepBody> Result = BrepBody::Cone(Specification.Low.Centre, Axis,
                                               Specification.Low.Radius, Specification.High.Radius, Height);
    if (!Result) return Result;
    const double R0 = Specification.Low.Radius;
    const double R1 = Specification.High.Radius;
    const VariableRadiusLaw Law{ R0, R1 };
    const double ExactVolume = Law.SweptVolume(Height);
    if (!ScalarCriteria::WithinVolumeTolerance(Result.Payload.Validate().Volume, ExactVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "asymmetric frustum volume failed analytic acceptance");
    return Result;
}

Deliver<BrepBody> BlendSolver::ReconstructAsymmetricSupport(const AsymmetricBlendSpecification& Specification) noexcept
{
    switch (Specification.Classification)
    {
        case AsymmetricSupportClassification::TaperedFrustum:
        case AsymmetricSupportClassification::UnequalRadialCaps:
        case AsymmetricSupportClassification::EqualRadiusAsymmetricPlanes:
            return ReconstructAsymmetricFrustum(Specification);
        default:
            return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                             "asymmetric support mode has no bounded reconstruction route");
    }
}

bool BlendSolver::ValidateG1EndpointMatch(Vec3 SurfaceNormal, Vec3 SupportNormal,
                                               std::string& Refusal) noexcept
{
    if (SurfaceNormal.Length() <= ScalarCriteria::GeometricTolerance ||
        SupportNormal.Length() <= ScalarCriteria::GeometricTolerance)
    { Refusal = "G1 endpoint normal is degenerate"; return false; }
    const double Alignment = SurfaceNormal.Normalised().Dot(SupportNormal.Normalised());
    if (std::fabs(std::fabs(Alignment) - 1.0) > ScalarCriteria::AngularTolerance)
    { Refusal = "surface and support normals are not G1 aligned"; return false; }
    return true;
}

Deliver<VariableRadiusSurface> BlendSolver::BuildVariableRadiusSurface(const AsymmetricBlendSpecification& Specification) noexcept
{
    if (Specification.Classification != AsymmetricSupportClassification::VariableRadiusRoll)
        return Deliver<VariableRadiusSurface>::Reject(RefusalReason::Unsupported, "variable surface requires variable-radius mode");
    std::string Refusal;
    if (!ValidateAsymmetricSpecification(Specification, Refusal))
        return Deliver<VariableRadiusSurface>::Reject(RefusalReason::DegenerateInput, "invalid variable-radius surface specification");
    Vec3 Delta = Specification.High.Centre - Specification.Low.Centre;
    double Length = Delta.Length();
    if (Length <= ScalarCriteria::MergeTolerance)
        return Deliver<VariableRadiusSurface>::Reject(RefusalReason::DegenerateInput, "variable surface has zero axial length");
    Vec3 Axis = Delta / Length;
    Vec3 Radial = Axis.Cross(Vec3::UnitX());
    if (Radial.Length() <= ScalarCriteria::GeometricTolerance) Radial = Axis.Cross(Vec3::UnitY());
    if (Radial.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<VariableRadiusSurface>::Reject(RefusalReason::DegenerateInput, "variable surface frame is degenerate");
    return Deliver<VariableRadiusSurface>::Accept(VariableRadiusSurface{ Specification.Low.Centre, Axis, Radial, Length, Specification.RadiusLaw });
}

Deliver<BrepBody> BlendSolver::ReconstructVariableRadiusRuledSolid(const AsymmetricBlendSpecification& Specification) noexcept
{
    auto Surface = BuildVariableRadiusSurface(Specification);
    if (!Surface) return Deliver<BrepBody>::Reject(Surface.Denial.Reason, "invalid variable-radius ruled surface");
    return BrepBody::Cone(Surface.Payload.Origin, Surface.Payload.Axis, Surface.Payload.Law.Start,
                          Surface.Payload.Law.End, Surface.Payload.Length);
}

bool BlendSolver::ValidateVariableSurfaceCurvature(const VariableRadiusSurface& Surface,
                                                       double MaximumCircumferentialCurvature,
                                                       std::string& Refusal) noexcept
{
    if (!std::isfinite(MaximumCircumferentialCurvature) || MaximumCircumferentialCurvature <= 0.0)
    { Refusal = "curvature bound is invalid"; return false; }
    for (int I = 0; I <= 16; ++I)
    {
        const double K = Surface.CircumferentialCurvature(static_cast<double>(I) / 16.0);
        if (!std::isfinite(K) || K > MaximumCircumferentialCurvature + ScalarCriteria::GeometricTolerance)
        { Refusal = "variable surface curvature exceeds bound"; return false; }
    }
    return true;
}

bool BlendSolver::ValidateVariableSurfaceG1(const VariableRadiusSurface& Surface,
                                              Vec3 LowSupportNormal, Vec3 HighSupportNormal,
                                              double Angle, std::string& Refusal) noexcept
{
    if (Surface.Length <= ScalarCriteria::GeometricTolerance || !Surface.Law.Positive())
    { Refusal = "variable-radius surface is degenerate"; return false; }
    const Vec3 SurfaceNormal = Surface.Normal(Angle);
    if (!ValidateG1EndpointMatch(SurfaceNormal, LowSupportNormal, Refusal)) return false;
    if (!ValidateG1EndpointMatch(SurfaceNormal, HighSupportNormal, Refusal)) return false;
    return true;
}

bool BlendSolver::ValidateAsymmetricEndpointChain(const AsymmetricEndpointChain& Chain,
                                                    double MinimumClearance, std::string& Refusal) noexcept
{
    if (Chain.Supports.size() < 2) { Refusal = "endpoint chain requires at least two supports"; return false; }
    for (size_t I = 0; I + 1 < Chain.Supports.size(); ++I)
    {
        if (!ValidateAsymmetricEndpointPair(Chain.Supports[I], Chain.Supports[I + 1], MinimumClearance, Refusal)) return false;
        Vec3 Axis = (Chain.Supports[I + 1].Centre - Chain.Supports[I].Centre).Normalised();
        if (I + 2 < Chain.Supports.size())
        {
            Vec3 Next = (Chain.Supports[I + 2].Centre - Chain.Supports[I + 1].Centre).Normalised();
            if (std::fabs(std::fabs(Axis.Dot(Next)) - 1.0) > ScalarCriteria::AngularTolerance)
            { Refusal = "endpoint chain changes axis direction"; return false; }
        }
    }
    return true;
}

bool BlendSolver::ValidateAsymmetricEndpointPair(const EndpointSupport& Low, const EndpointSupport& High,
                                                   double MinimumClearance, std::string& Refusal) noexcept
{
    const double Scale = std::max({ 1.0, Low.Radius, High.Radius, MinimumClearance });
    const double PositionTolerance = ScalarCriteria::GeometricTolerance * Scale;
    if (!std::isfinite(Low.Radius) || !std::isfinite(High.Radius) || Low.Radius <= ScalarCriteria::MergeTolerance ||
        High.Radius <= ScalarCriteria::MergeTolerance)
    { Refusal = "endpoint support radius is not positive"; return false; }
    if (std::fabs(Low.Radius - High.Radius) <= ScalarCriteria::CircularTolerance)
    { Refusal = "endpoint supports are not asymmetric"; return false; }
    if (Low.Normal.Length() <= ScalarCriteria::GeometricTolerance || High.Normal.Length() <= ScalarCriteria::GeometricTolerance)
    { Refusal = "endpoint support normal is degenerate"; return false; }
    if (std::fabs(std::fabs(Low.Normal.Normalised().Dot(High.Normal.Normalised())) - 1.0) > ScalarCriteria::AngularTolerance)
    { Refusal = "endpoint support normals are not parallel"; return false; }
    if ((High.Centre - Low.Centre).Length() <= Low.Radius + High.Radius + std::max(MinimumClearance, PositionTolerance))
    { Refusal = "endpoint support intervals have no positive ligament"; return false; }
    return true;
}

} // namespace Frontier
