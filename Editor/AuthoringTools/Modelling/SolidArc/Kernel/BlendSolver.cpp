//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Kernel/BlendSolver.cpp — chamfer / fillet / face push as regularised set operations
//============================================================================================================================================
#include "BlendSolver.h"
#include "IntersectionSolver.h"
#include "SkinSolver.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <array>

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

    // Forward declaration: the same local feasibility gate is used by one-edge and edge-set chamfers.
    bool ChamferSetbackFits(const BrepBody& Body, int SelectedEdge, const EdgeCornerFrame& Frame,
                            double SetBack, std::string& Refusal) noexcept;

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
            const bool HalfTurn = std::fabs(std::fabs(SweepAngle) - ScalarCriteria::Pi) <= ScalarCriteria::SweepTolerance;
            double AtStart = HalfTurn ? Radial.Dot(RadialStart) : std::fabs(Radial.Dot(RadialStart));
            double AtEnd = HalfTurn ? Radial.Dot(RadialEnd) : std::fabs(Radial.Dot(RadialEnd));
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
            // The radial boundary already walks from the lower axial centre to the upper axial
            // centre; the shared axis edge must close it in the opposite direction.
            const bool ReverseAxis = Body.Edges[AxisEdge].VertexStart == StartVertex;
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

    Deliver<BrepBody> ChamferPlaneCylinderBossRoot(const PlaneCylinderRoot& Root, double SetBack) noexcept
    {
        if (SetBack <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "set-back is zero or negative");
        const bool Closed = ScalarCriteria::WithinAngularTolerance(std::fabs(Root.SweepAngle), ScalarCriteria::TwoPi);
        if (!Closed)
        {
            // Partial-loop reconstruction first covers a physical half-turn, then uses the existing radial-cap
            // healer for one bounded general-angle sector. Arbitrary/branched chains still fail classification.
            const bool HalfTurn = ScalarCriteria::WithinAngularTolerance(std::fabs(Root.SweepAngle), ScalarCriteria::Pi);
            if (std::fabs(Root.SweepAngle) > ScalarCriteria::Pi + ScalarCriteria::AngularTolerance)
                return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "reflex partial curved chamfers remain unsupported");
            if (SetBack >= Root.BossHeight - Tol || Root.BossRadius + SetBack >= Root.OuterRadius - Tol)
                return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "set-back consumes the boss height or shoulder wall");
            const Vec3 ShoulderCentre = Root.Base + Root.Axis * Root.ShoulderHeight;
            const Vec3 BossTop = ShoulderCentre + Root.Axis * Root.BossHeight;
            const Vec3 Radial = Root.RadialStart.Normalised();
            if (Radial.Length() <= Tol || std::fabs(Radial.Dot(Root.Axis)) > ScalarCriteria::GeometricTolerance)
                return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial curved chamfer radial frame is degenerate");
            auto RevolveLine = [&](Vec3 Start, Vec3 End) -> Deliver<NurbsSurface>
            {
                Deliver<NurbsCurve> Line = NurbsCurve::Line(Start, End);
                return Line ? NurbsSurface::Revolution(Line.Payload, Root.Base, Root.Axis, Root.SweepAngle)
                            : Deliver<NurbsSurface>::Reject(Line.Denial.Reason, Line.Denial.Detail);
            };
            Deliver<NurbsSurface> Outer = RevolveLine(Root.Base + Radial * Root.OuterRadius,
                                                       ShoulderCentre + Radial * Root.OuterRadius);
            Deliver<NurbsSurface> Shoulder = RevolveLine(ShoulderCentre + Radial * Root.OuterRadius,
                                                          ShoulderCentre + Radial * (Root.BossRadius + SetBack));
            Deliver<NurbsSurface> Chamfer = RevolveLine(ShoulderCentre + Radial * (Root.BossRadius + SetBack),
                                                         ShoulderCentre + Root.Axis * SetBack + Radial * Root.BossRadius);
            Deliver<NurbsSurface> Boss = RevolveLine(ShoulderCentre + Root.Axis * SetBack + Radial * Root.BossRadius,
                                                      BossTop + Radial * Root.BossRadius);
            Deliver<NurbsSurface> Bottom = RevolveLine(Root.Base, Root.Base + Radial * Root.OuterRadius);
            Deliver<NurbsSurface> Top = RevolveLine(BossTop + Radial * Root.BossRadius, BossTop);
            if (!Outer || !Shoulder || !Chamfer || !Boss || !Bottom || !Top)
                return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial curved chamfer support is degenerate");
            Outer.Payload.Classification = SurfaceClassification::Cylinder;
            Outer.Payload.Origin = Root.Base; Outer.Payload.Axis = Root.Axis;
            Outer.Payload.RadiusMajor = Outer.Payload.RadiusMinor = Root.OuterRadius;
            Chamfer.Payload.Classification = SurfaceClassification::Cone;
            Chamfer.Payload.Origin = ShoulderCentre; Chamfer.Payload.Axis = Root.Axis;
            Chamfer.Payload.RadiusMajor = Root.BossRadius + SetBack; Chamfer.Payload.RadiusMinor = Root.BossRadius;
            Boss.Payload.Classification = SurfaceClassification::Cylinder;
            Boss.Payload.Origin = ShoulderCentre + Root.Axis * SetBack; Boss.Payload.Axis = Root.Axis;
            Boss.Payload.RadiusMajor = Boss.Payload.RadiusMinor = Root.BossRadius;
            Deliver<BrepBody> Result = BrepBody::Sew({ Outer.Payload, Shoulder.Payload, Chamfer.Payload,
                                                       Boss.Payload, Bottom.Payload, Top.Payload });
            if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, Result.Denial.Detail);
            if (!HalfTurn)
            {
                const Vec3 BossTopPoint = ShoulderCentre + Root.Axis * Root.BossHeight;
                if (!CapRadialSector(Result.Payload, Root.Base, BossTopPoint, Radial, Root.SweepAngle, Root.OuterRadius))
                    return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                        "general-angle partial curved chamfer could not close its radial endpoint caps");
            }
            const BodyReport Report = Result.Payload.Validate();
            const bool ExactTopology = HalfTurn
                ? Result.Payload.Vertices.size() == 12 && Result.Payload.Edges.size() == 17 &&
                  Result.Payload.Coedges.size() == 34 && Result.Payload.Loops.size() == 7 && Result.Payload.Faces.size() == 7
                : Result.Payload.Vertices.size() == 12 && Result.Payload.Edges.size() == 18 &&
                  Result.Payload.Coedges.size() == 36 && Result.Payload.Loops.size() == 8 && Result.Payload.Faces.size() == 8;
            if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 ||
                Report.OpenEdges != 0 || Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || !ExactTopology)
                return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "partial curved chamfer did not reach exact manifold topology");
            return Result;
        }
        if (SetBack >= Root.BossHeight - Tol || Root.BossRadius + SetBack >= Root.OuterRadius - Tol)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "set-back consumes the boss height or shoulder wall");

        Workplane Frame = Workplane::FromNormal(Root.Base, Root.Axis);
        const Vec3 Radial = Frame.AxisX;
        const Vec3 ShoulderCentre = Root.Base + Root.Axis * Root.ShoulderHeight;
        Deliver<NurbsSurface> Outer = NurbsSurface::Cylinder(Root.Base, Root.Axis, Root.OuterRadius, Root.ShoulderHeight);
        Deliver<NurbsCurve> ShoulderLine = NurbsCurve::Line(ShoulderCentre + Radial * Root.OuterRadius,
                                                            ShoulderCentre + Radial * (Root.BossRadius + SetBack));
        Deliver<NurbsSurface> Shoulder = ShoulderLine
            ? NurbsSurface::Revolution(ShoulderLine.Payload, Root.Base, Root.Axis, ScalarCriteria::TwoPi)
            : Deliver<NurbsSurface>::Reject(ShoulderLine.Denial.Reason, ShoulderLine.Denial.Detail);
        Deliver<NurbsSurface> Chamfer = NurbsSurface::Cone(ShoulderCentre, Root.Axis,
                                                           Root.BossRadius + SetBack, Root.BossRadius, SetBack);
        Deliver<NurbsSurface> Boss = NurbsSurface::Cylinder(ShoulderCentre + Root.Axis * SetBack, Root.Axis,
                                                            Root.BossRadius, Root.BossHeight - SetBack);
        if (!Outer || !Shoulder || !Chamfer || !Boss)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "curved root chamfer support is degenerate");
        Deliver<BrepBody> Result = BrepBody::Sew({ Outer.Payload, Shoulder.Payload, Chamfer.Payload, Boss.Payload });
        if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, Result.Denial.Detail);
        const BodyReport Report = Result.Payload.Validate();
        if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
            Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0)
            return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "curved root chamfer did not heal to one manifold solid");
        return Result;
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

    bool ConeEndCentresAllowApex(const NurbsSurface& Cone, Vec3& Start, Vec3& End,
                                 double& RadiusStart, double& RadiusEnd) noexcept
    {
        if (Cone.Classification != SurfaceClassification::Cone || Cone.RadiusMajor <= Tol ||
            Cone.RadiusMinor < -ScalarCriteria::GeometricTolerance) return false;
        const double U0 = Cone.DomainStartU(), U1 = Cone.DomainEndU();
        const double V0 = Cone.DomainStartV(), V1 = Cone.DomainEndV();
        const Vec3 Axis = Cone.Axis.Normalised();
        if (Axis.Length() <= Tol) return false;
        auto CentreAt = [&](double V)
        { Vec3 P = Cone.Sample(0.5 * (U0 + U1), V); return Cone.Origin + Axis * (P - Cone.Origin).Dot(Axis); };
        Start = CentreAt(V0); End = CentreAt(V1);
        const double Height = Start.Distance(End);
        const double Scale = std::max({ 1.0, Cone.RadiusMajor, Cone.RadiusMinor, Height });
        const double Epsilon = ScalarCriteria::GeometricTolerance * Scale;
        if (Height <= Epsilon || (End - Start).Normalised().Cross(Axis).Length() > ScalarCriteria::GeometricTolerance) return false;
        auto RowRadius = [&](double V, Vec3 Centre, double Expected, double& Radius)
        {
            Radius = (Cone.Sample(U0, V) - Centre).Length();
            if (Expected <= ScalarCriteria::GeometricTolerance)
            {
                for (int I = 0; I <= 8; ++I)
                    if (Cone.Sample(U0 + (U1 - U0) * (static_cast<double>(I) / 8.0), V).Distance(Centre) > Epsilon) return false;
                Radius = 0.0; return true;
            }
            for (int I = 0; I <= 8; ++I)
            {
                Vec3 Radial = Cone.Sample(U0 + (U1 - U0) * (static_cast<double>(I) / 8.0), V) - Centre;
                if (std::fabs(Radial.Length() - Radius) > Epsilon || std::fabs(Radial.Dot(Axis)) > Epsilon) return false;
            }
            return Radius > Tol;
        };
        const bool StartAtOrigin = Start.Distance(Cone.Origin) <= Epsilon;
        const double TaggedStart = StartAtOrigin ? Cone.RadiusMajor : Cone.RadiusMinor;
        const double TaggedEnd = StartAtOrigin ? Cone.RadiusMinor : Cone.RadiusMajor;
        if (!StartAtOrigin && End.Distance(Cone.Origin) > Epsilon) return false;
        if (!RowRadius(V0, Start, TaggedStart, RadiusStart) || !RowRadius(V1, End, TaggedEnd, RadiusEnd)) return false;
        return true;
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

    struct PlaneConeApexRoot
    {
        Vec3   Base, Axis;
        double OuterRadius = 0.0, ShoulderHeight = 0.0;
        double FootRadius = 0.0, BossHeight = 0.0;
    };

    std::optional<PlaneConeApexRoot> PlaneConeApexBossRoot(const BrepBody& Body, int Edge) noexcept
    {
        if (Edge < 0 || Edge >= static_cast<int>(Body.Edges.size()) || !Body.Validate().Solid() ||
            Body.Vertices.size() != 4 || Body.Edges.size() != 6 || Body.Coedges.size() != 12 ||
            Body.Loops.size() != 4 || Body.Faces.size() != 4) return std::nullopt;
        const BrepEdge& RootEdge = Body.Edges[Edge];
        if (!RootEdge.Closed() || RootEdge.Coedges.size() != 2) return std::nullopt;
        Vec3 RootCentre, RootNormal; double FootRadius = 0.0;
        if (!CircularFrame(RootEdge.Curve, RootCentre, RootNormal, FootRadius)) return std::nullopt;
        int ShoulderFace = -1, ConeFace = -1;
        for (int Coedge : RootEdge.Coedges)
        {
            if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return std::nullopt;
            const int Face = Body.Coedges[Coedge].Face;
            if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return std::nullopt;
            const SurfaceClassification Class = Body.Faces[Face].Surface.Classification;
            if (Class == SurfaceClassification::Cone)
            {
                if (ConeFace >= 0) return std::nullopt;
                ConeFace = Face;
            }
            else if (Class == SurfaceClassification::Revolution)
            {
                if (ShoulderFace >= 0) return std::nullopt;
                ShoulderFace = Face;
            }
            else return std::nullopt;
        }
        if (ShoulderFace < 0 || ConeFace < 0) return std::nullopt;
        const Vec3 Axis = Body.Faces[ConeFace].Surface.Axis.Normalised();
        if (Axis.Length() <= Tol || std::fabs(RootNormal.Dot(Axis)) < 1.0 - ScalarCriteria::GeometricTolerance) return std::nullopt;
        Vec3 ConeStart, ConeEnd; double ConeStartRadius = 0.0, ConeEndRadius = 0.0;
        if (!ConeEndCentresAllowApex(Body.Faces[ConeFace].Surface, ConeStart, ConeEnd, ConeStartRadius, ConeEndRadius)) return std::nullopt;
        const double Epsilon = ScalarCriteria::GeometricTolerance * std::max({ 1.0, FootRadius, ConeStart.Distance(ConeEnd) });
        Vec3 Apex; double ClassifiedFoot = 0.0;
        if (ConeStart.Distance(RootCentre) <= Epsilon) { Apex = ConeEnd; ClassifiedFoot = ConeStartRadius; }
        else if (ConeEnd.Distance(RootCentre) <= Epsilon) { Apex = ConeStart; ClassifiedFoot = ConeEndRadius; }
        else return std::nullopt;
        if (std::fabs(ClassifiedFoot - FootRadius) > Epsilon ||
            (Apex - RootCentre).Normalised().Dot(Axis) < 1.0 - ScalarCriteria::GeometricTolerance ||
            Apex.Distance(RootCentre) <= Epsilon || (ConeStartRadius > Tol && ConeEndRadius > Tol)) return std::nullopt;
        int OuterEdge = -1; double OuterRadius = 0.0;
        for (int Loop : Body.Faces[ShoulderFace].Loops)
        {
            if (Loop < 0 || Loop >= static_cast<int>(Body.Loops.size())) return std::nullopt;
            for (int Coedge : Body.Loops[Loop].Coedges)
            {
                if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return std::nullopt;
                const int Candidate = Body.Coedges[Coedge].Edge;
                if (Candidate < 0 || Candidate >= static_cast<int>(Body.Edges.size()) || Candidate == Edge) continue;
                Vec3 Centre, Normal; double Radius = 0.0;
                if (!CircularFrame(Body.Edges[Candidate].Curve, Centre, Normal, Radius) ||
                    Centre.Distance(RootCentre) > Epsilon || std::fabs(Normal.Dot(Axis)) < 1.0 - ScalarCriteria::GeometricTolerance ||
                    Radius <= FootRadius + Epsilon) continue;
                if (OuterEdge >= 0 && OuterEdge != Candidate) return std::nullopt;
                OuterEdge = Candidate; OuterRadius = Radius;
            }
        }
        if (OuterEdge < 0 || !Body.Edges[OuterEdge].Closed() || Body.Edges[OuterEdge].Coedges.size() != 2) return std::nullopt;
        int OuterFace = -1;
        for (int Coedge : Body.Edges[OuterEdge].Coedges)
        {
            const int Face = Body.Coedges[Coedge].Face;
            if (Face == ShoulderFace) continue;
            if (OuterFace >= 0 || Face < 0 || Face >= static_cast<int>(Body.Faces.size()) ||
                Body.Faces[Face].Surface.Classification != SurfaceClassification::Cylinder) return std::nullopt;
            OuterFace = Face;
        }
        if (OuterFace < 0) return std::nullopt;
        Vec3 OuterStart, OuterEnd; double ClassifiedOuterRadius = 0.0;
        if (!CylinderEndCentres(Body.Faces[OuterFace].Surface, OuterStart, OuterEnd, ClassifiedOuterRadius)) return std::nullopt;
        if (std::fabs(ClassifiedOuterRadius - OuterRadius) > Epsilon) return std::nullopt;
        Vec3 Base;
        if (OuterStart.Distance(RootCentre) <= Epsilon) Base = OuterEnd;
        else if (OuterEnd.Distance(RootCentre) <= Epsilon) Base = OuterStart;
        else return std::nullopt;
        const double ShoulderHeight = RootCentre.Distance(Base);
        const double BossHeight = Apex.Distance(RootCentre);
        if (ShoulderHeight <= Epsilon || BossHeight <= Epsilon ||
            (RootCentre - Base).Normalised().Dot(Axis) < 1.0 - ScalarCriteria::GeometricTolerance) return std::nullopt;
        int BottomCaps = 0;
        for (size_t Face = 0; Face < Body.Faces.size(); ++Face)
        {
            const int FaceIndex = static_cast<int>(Face);
            if (FaceIndex == ShoulderFace || FaceIndex == ConeFace || FaceIndex == OuterFace) continue;
            Vec3 Normal;
            if (!PlanarNormal(Body, FaceIndex, Normal) || std::fabs(Normal.Dot(Axis)) < 1.0 - ScalarCriteria::GeometricTolerance ||
                Body.Faces[Face].Loops.size() != 1) return std::nullopt;
            const NurbsSurface& Cap = Body.Faces[Face].Surface;
            const Vec3 Point = Cap.Sample(0.5 * (Cap.DomainStartU() + Cap.DomainEndU()),
                                          0.5 * (Cap.DomainStartV() + Cap.DomainEndV()));
            if (std::fabs((Point - Base).Dot(Axis)) > Epsilon) return std::nullopt;
            ++BottomCaps;
        }
        if (BottomCaps != 1) return std::nullopt;
        return PlaneConeApexRoot{ Base, Axis, OuterRadius, ShoulderHeight, FootRadius, BossHeight };
    }

    Deliver<BrepBody> ChamferPlaneConeApexBossRoot(const PlaneConeApexRoot& Root, double SetBack) noexcept
    {
        if (SetBack <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "set-back is zero or negative");
        if (SetBack >= Root.BossHeight - Tol || Root.FootRadius + SetBack >= Root.OuterRadius - Tol)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "set-back consumes the apex cone or shoulder wall");
        const double Slant = std::hypot(Root.BossHeight, Root.FootRadius);
        if (SetBack >= Slant - Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "set-back consumes the apex cone slant");
        const double Axial = SetBack * Root.BossHeight / Slant;
        const double ContactRadius = Root.FootRadius * (1.0 - Axial / Root.BossHeight);
        if (Axial <= Tol || Axial >= Root.BossHeight - Tol || ContactRadius <= Tol)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "apex contact is degenerate");
        const Vec3 ShoulderCentre = Root.Base + Root.Axis * Root.ShoulderHeight;
        const Vec3 Apex = ShoulderCentre + Root.Axis * Root.BossHeight;
        const Vec3 Radial = Workplane::FromNormal(Root.Base, Root.Axis).AxisX;
        auto RevolveLine = [&](Vec3 Start, Vec3 End) -> Deliver<NurbsSurface>
        {
            Deliver<NurbsCurve> Line = NurbsCurve::Line(Start, End);
            return Line ? NurbsSurface::Revolution(Line.Payload, Root.Base, Root.Axis, ScalarCriteria::TwoPi)
                        : Deliver<NurbsSurface>::Reject(Line.Denial.Reason, Line.Denial.Detail);
        };
        Deliver<NurbsSurface> Outer = NurbsSurface::Cylinder(Root.Base, Root.Axis, Root.OuterRadius, Root.ShoulderHeight);
        Deliver<NurbsCurve> ShoulderLine = NurbsCurve::Line(ShoulderCentre + Radial * Root.OuterRadius,
                                                             ShoulderCentre + Radial * (Root.FootRadius + SetBack));
        Deliver<NurbsSurface> Shoulder = ShoulderLine
            ? NurbsSurface::Revolution(ShoulderLine.Payload, Root.Base, Root.Axis, ScalarCriteria::TwoPi)
            : Deliver<NurbsSurface>::Reject(ShoulderLine.Denial.Reason, ShoulderLine.Denial.Detail);
        Deliver<NurbsSurface> Chamfer = RevolveLine(ShoulderCentre + Radial * (Root.FootRadius + SetBack),
                                                     ShoulderCentre + Root.Axis * Axial + Radial * ContactRadius);
        Deliver<NurbsSurface> Boss = RevolveLine(ShoulderCentre + Root.Axis * Axial + Radial * ContactRadius, Apex);
        Deliver<NurbsSurface> Bottom = RevolveLine(Root.Base, Root.Base + Radial * Root.OuterRadius);
        if (!Outer || !Shoulder || !Chamfer || !Boss || !Bottom)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "apex plane-cone support is degenerate");
        Chamfer.Payload.Classification = SurfaceClassification::Cone;
        Chamfer.Payload.Origin = ShoulderCentre; Chamfer.Payload.Axis = Root.Axis;
        Chamfer.Payload.RadiusMajor = Root.FootRadius + SetBack; Chamfer.Payload.RadiusMinor = ContactRadius;
        Boss.Payload.Classification = SurfaceClassification::Cone;
        Boss.Payload.Origin = ShoulderCentre + Root.Axis * Axial; Boss.Payload.Axis = Root.Axis;
        Boss.Payload.RadiusMajor = ContactRadius; Boss.Payload.RadiusMinor = 0.0;
        Deliver<BrepBody> Result = BrepBody::Sew({ Outer.Payload, Shoulder.Payload, Chamfer.Payload, Boss.Payload, Bottom.Payload });
        if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, Result.Denial.Detail);
        const BodyReport Report = Result.Payload.Validate();
        if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
            Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Result.Payload.Vertices.size() != 6 ||
            Result.Payload.Edges.size() != 9 || Result.Payload.Coedges.size() != 18 || Result.Payload.Loops.size() != 5 ||
            Result.Payload.Faces.size() != 5)
            return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "apex plane-cone chamfer did not heal to exact topology");
        return Result;
    }

    struct PlaneConeApexPartialRoot
    {
        Vec3   Base, Axis, RadialStart;
        double OuterRadius = 0.0, ShoulderHeight = 0.0, FootRadius = 0.0, BossHeight = 0.0;
        double SweepAngle = 0.0;
    };

    std::optional<PlaneConeApexPartialRoot> PlaneConeApexPartialBossRoot(const BrepBody& Body, int Edge) noexcept
    {
        if (Edge < 0 || Edge >= static_cast<int>(Body.Edges.size()) || !Body.Validate().Solid()) return std::nullopt;
        Deliver<std::vector<int>> ChainResult = BlendSolver::TangentChain(Body, Edge);
        if (!ChainResult || ChainResult.Payload.empty()) return std::nullopt;
        const std::vector<int>& RootEdges = ChainResult.Payload;
        const size_t SegmentCount = RootEdges.size();
        const bool SemicircleTopology = SegmentCount == 2 && Body.Vertices.size() == 11 && Body.Edges.size() == 18 &&
            Body.Coedges.size() == 36 && Body.Loops.size() == 9 && Body.Faces.size() == 9;
        const bool SectorTopology = SegmentCount == 2 && Body.Vertices.size() == 11 && Body.Edges.size() == 19 &&
            Body.Coedges.size() == 38 && Body.Loops.size() == 10 && Body.Faces.size() == 10;
        if (!SemicircleTopology && !SectorTopology) return std::nullopt;
        Vec3 RootCentre, Axis; double FootRadius = 0.0; bool FirstRoot = true;
        std::vector<int> ShoulderFaces, ConeFaces;
        for (int RootIndex : RootEdges)
        {
            const BrepEdge& RootEdge = Body.Edges[RootIndex];
            if (RootEdge.Coedges.size() != 2) return std::nullopt;
            Vec3 Centre, Normal; double Radius = 0.0;
            if (!CircularFrame(RootEdge.Curve, Centre, Normal, Radius)) return std::nullopt;
            int ShoulderFace = -1, ConeFace = -1;
            for (int Coedge : RootEdge.Coedges)
            {
                if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return std::nullopt;
                const int Face = Body.Coedges[Coedge].Face;
                if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return std::nullopt;
                const SurfaceClassification Class = Body.Faces[Face].Surface.Classification;
                if (Class == SurfaceClassification::Revolution) { if (ShoulderFace >= 0) return std::nullopt; ShoulderFace = Face; }
                else if (Class == SurfaceClassification::Cone) { if (ConeFace >= 0) return std::nullopt; ConeFace = Face; }
                else return std::nullopt;
            }
            if (ShoulderFace < 0 || ConeFace < 0) return std::nullopt;
            const Vec3 CandidateAxis = Body.Faces[ConeFace].Surface.Axis.Normalised();
            if (CandidateAxis.Length() <= Tol || std::fabs(Normal.Dot(CandidateAxis)) < 1.0 - ScalarCriteria::GeometricTolerance) return std::nullopt;
            Vec3 Start, End; double StartRadius = 0.0, EndRadius = 0.0;
            if (!ConeEndCentresAllowApex(Body.Faces[ConeFace].Surface, Start, End, StartRadius, EndRadius)) return std::nullopt;
            const double Epsilon = ScalarCriteria::GeometricTolerance * std::max({ 1.0, Radius, Start.Distance(End) });
            const bool RootAtStart = Start.Distance(Centre) <= Epsilon;
            const double MeasuredFoot = RootAtStart ? StartRadius : EndRadius;
            const double ApexRadius = RootAtStart ? EndRadius : StartRadius;
            const Vec3 Apex = RootAtStart ? End : Start;
            if ((RootAtStart ? End : Start).Distance(Centre) <= Epsilon || std::fabs(MeasuredFoot - Radius) > Epsilon ||
                ApexRadius > Epsilon || (Apex - Centre).Normalised().Dot(CandidateAxis) < 1.0 - ScalarCriteria::GeometricTolerance) return std::nullopt;
            if (FirstRoot) { RootCentre = Centre; Axis = CandidateAxis; FootRadius = Radius; FirstRoot = false; }
            else if (Centre.Distance(RootCentre) > Epsilon || CandidateAxis.Dot(Axis) < 1.0 - ScalarCriteria::GeometricTolerance ||
                     std::fabs(Radius - FootRadius) > Epsilon) return std::nullopt;
            ShoulderFaces.push_back(ShoulderFace); ConeFaces.push_back(ConeFace);
        }
        double RootSpan = 0.0;
        if (FirstRoot || ShoulderFaces.size() != SegmentCount || ConeFaces.size() != SegmentCount ||
            !CircularChain(Body, RootEdges, RootCentre, Axis, FootRadius, false, &RootSpan) ||
            std::fabs(RootSpan) > ScalarCriteria::Pi + ScalarCriteria::AngularTolerance ||
            (SemicircleTopology && !ScalarCriteria::WithinAngularTolerance(std::fabs(RootSpan), ScalarCriteria::Pi)) ||
            (!SemicircleTopology && ScalarCriteria::WithinAngularTolerance(std::fabs(RootSpan), ScalarCriteria::Pi))) return std::nullopt;
        std::vector<int> OuterEdges, OuterFaces; double OuterRadius = 0.0;
        for (int ShoulderFace : ShoulderFaces)
        {
            int Found = -1;
            for (int Loop : Body.Faces[ShoulderFace].Loops) for (int Coedge : Body.Loops[Loop].Coedges)
            {
                const int Candidate = Body.Coedges[Coedge].Edge;
                if (Candidate < 0 || Candidate >= static_cast<int>(Body.Edges.size()) || std::find(RootEdges.begin(), RootEdges.end(), Candidate) != RootEdges.end()) continue;
                Vec3 C, N; double R = 0.0;
                if (!CircularFrame(Body.Edges[Candidate].Curve, C, N, R) || C.Distance(RootCentre) > ScalarCriteria::GeometricTolerance * std::max(1.0, FootRadius) ||
                    std::fabs(N.Dot(Axis)) < 1.0 - ScalarCriteria::GeometricTolerance || R <= FootRadius) continue;
                if (Found >= 0 && Found != Candidate) return std::nullopt;
                Found = Candidate; OuterRadius = R;
            }
            if (Found < 0) return std::nullopt;
            OuterEdges.push_back(Found);
            int OuterFace = -1;
            for (int Coedge : Body.Edges[Found].Coedges)
            {
                const int Face = Body.Coedges[Coedge].Face;
                if (Face == ShoulderFace) continue;
                if (OuterFace >= 0 || Face < 0 || Face >= static_cast<int>(Body.Faces.size()) || Body.Faces[Face].Surface.Classification != SurfaceClassification::Cylinder) return std::nullopt;
                OuterFace = Face;
            }
            if (OuterFace < 0) return std::nullopt;
            OuterFaces.push_back(OuterFace);
        }
        if (OuterEdges.size() != SegmentCount || OuterFaces.size() != SegmentCount ||
            !CircularChain(Body, OuterEdges, RootCentre, Axis, OuterRadius, false, nullptr)) return std::nullopt;
        Vec3 Base, Apex; double ShoulderHeight = 0.0, BossHeight = 0.0; bool FirstOuter = true;
        for (int OuterFace : OuterFaces)
        {
            Vec3 A, B; double R = 0.0;
            if (!CylinderEndCentres(Body.Faces[OuterFace].Surface, A, B, R)) return std::nullopt;
            const double Epsilon = ScalarCriteria::GeometricTolerance * std::max({ 1.0, OuterRadius, A.Distance(B) });
            Vec3 Shoulder, CandidateBase;
            if (A.Distance(RootCentre) < B.Distance(RootCentre)) { Shoulder = A; CandidateBase = B; }
            else { Shoulder = B; CandidateBase = A; }
            const double H = Shoulder.Distance(CandidateBase);
            if (H <= Epsilon || (Shoulder - CandidateBase).Normalised().Dot(Axis) < 1.0 - ScalarCriteria::GeometricTolerance) return std::nullopt;
            if (FirstOuter) { Base = CandidateBase; ShoulderHeight = H; FirstOuter = false; }
            else if (Shoulder.Distance(RootCentre) > Epsilon || CandidateBase.Distance(Base) > Epsilon || std::fabs(H - ShoulderHeight) > Epsilon) return std::nullopt;
        }
        if (FirstOuter) return std::nullopt;
        int BottomPatches = 0; std::vector<Vec3> RadialNormals;
        const double CapEpsilon = ScalarCriteria::GeometricTolerance * std::max({ 1.0, OuterRadius, ShoulderHeight });
        for (size_t Face = 0; Face < Body.Faces.size(); ++Face)
        {
            const int F = static_cast<int>(Face);
            if (std::find(ShoulderFaces.begin(), ShoulderFaces.end(), F) != ShoulderFaces.end() ||
                std::find(ConeFaces.begin(), ConeFaces.end(), F) != ConeFaces.end() ||
                std::find(OuterFaces.begin(), OuterFaces.end(), F) != OuterFaces.end()) continue;
            Vec3 N; if (!PlanarNormal(Body, F, N)) return std::nullopt;
            const NurbsSurface& S = Body.Faces[F].Surface;
            const Vec3 P = S.Sample(.5*(S.DomainStartU()+S.DomainEndU()), .5*(S.DomainStartV()+S.DomainEndV()));
            if (std::fabs(N.Dot(Axis)) > 1.0 - ScalarCriteria::GeometricTolerance)
            {
                if (std::fabs((P - Base).Dot(Axis)) > CapEpsilon) return std::nullopt;
                ++BottomPatches;
            }
            else if (std::fabs(N.Dot(Axis)) < ScalarCriteria::GeometricTolerance) RadialNormals.push_back(N);
            else return std::nullopt;
        }
        if (BottomPatches != static_cast<int>(SegmentCount) || RadialNormals.size() != (SemicircleTopology ? 1u : 2u)) return std::nullopt;
        Vec3 Radial; double Sweep = 0.0;
        if (!OpenChainSweep(Body, RootEdges, RootCentre, Axis, Radial, Sweep) || !ScalarCriteria::WithinAngularTolerance(std::fabs(Sweep), RootSpan)) return std::nullopt;
        Apex = RootCentre + Axis * 1.0; // the height is recovered from the apex endpoint below
        Vec3 ConeStart, ConeEnd; double RS = 0.0, RE = 0.0;
        if (!ConeEndCentresAllowApex(Body.Faces[ConeFaces.front()].Surface, ConeStart, ConeEnd, RS, RE)) return std::nullopt;
        Apex = ConeStart.Distance(RootCentre) <= ScalarCriteria::GeometricTolerance ? ConeEnd : ConeStart;
        BossHeight = Apex.Distance(RootCentre);
        if (BossHeight <= Tol) return std::nullopt;
        return PlaneConeApexPartialRoot{ Base, Axis, Radial, OuterRadius, ShoulderHeight, FootRadius, BossHeight, Sweep };
    }

    Deliver<BrepBody> ChamferPlaneConeApexPartialBossRoot(const PlaneConeApexPartialRoot& Root, double SetBack) noexcept
    {
        if (SetBack <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "set-back is zero or negative");
        if (std::fabs(Root.SweepAngle) >= ScalarCriteria::TwoPi - ScalarCriteria::AngularTolerance)
            return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "partial apex chamfer requires an open root");
        if (SetBack >= Root.BossHeight - Tol || Root.FootRadius + SetBack >= Root.OuterRadius - Tol)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "set-back consumes the apex cone or wall");
        const double Slant = std::hypot(Root.BossHeight, Root.FootRadius);
        if (SetBack >= Slant - Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "set-back consumes the apex slant");
        const double Axial = SetBack * Root.BossHeight / Slant;
        const double Contact = Root.FootRadius * (1.0 - Axial / Root.BossHeight);
        if (Axial <= Tol || Axial >= Root.BossHeight - Tol || Contact <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "apex partial contact is degenerate");
        const Vec3 Shoulder = Root.Base + Root.Axis * Root.ShoulderHeight;
        const Vec3 Apex = Shoulder + Root.Axis * Root.BossHeight;
        const Vec3 Radial = Root.RadialStart.Normalised();
        auto Revolve = [&](Vec3 A, Vec3 B) -> Deliver<NurbsSurface>
        { Deliver<NurbsCurve> L = NurbsCurve::Line(A, B); return L ? NurbsSurface::Revolution(L.Payload, Root.Base, Root.Axis, Root.SweepAngle) : Deliver<NurbsSurface>::Reject(L.Denial.Reason, L.Denial.Detail); };
        Deliver<NurbsSurface> Outer = Revolve(Root.Base + Radial*Root.OuterRadius, Shoulder + Radial*Root.OuterRadius);
        Deliver<NurbsSurface> ShoulderSurface = Revolve(Shoulder + Radial*Root.OuterRadius, Shoulder + Radial*(Root.FootRadius+SetBack));
        Deliver<NurbsSurface> Chamfer = Revolve(Shoulder + Radial*(Root.FootRadius+SetBack), Shoulder + Root.Axis*Axial + Radial*Contact);
        Deliver<NurbsSurface> Boss = Revolve(Shoulder + Root.Axis*Axial + Radial*Contact, Apex);
        Deliver<NurbsSurface> Bottom = Revolve(Root.Base, Root.Base + Radial*Root.OuterRadius);
        if (!Outer || !ShoulderSurface || !Chamfer || !Boss || !Bottom) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial apex support is degenerate");
        Outer.Payload.Classification=SurfaceClassification::Cylinder; Outer.Payload.Origin=Root.Base; Outer.Payload.Axis=Root.Axis; Outer.Payload.RadiusMajor=Outer.Payload.RadiusMinor=Root.OuterRadius;
        Chamfer.Payload.Classification=SurfaceClassification::Cone; Chamfer.Payload.Origin=Shoulder; Chamfer.Payload.Axis=Root.Axis; Chamfer.Payload.RadiusMajor=Root.FootRadius+SetBack; Chamfer.Payload.RadiusMinor=Contact;
        Boss.Payload.Classification=SurfaceClassification::Cone; Boss.Payload.Origin=Shoulder+Root.Axis*Axial; Boss.Payload.Axis=Root.Axis; Boss.Payload.RadiusMajor=Contact; Boss.Payload.RadiusMinor=0.0;
        Deliver<BrepBody> Result = BrepBody::Sew({Outer.Payload,ShoulderSurface.Payload,Chamfer.Payload,Boss.Payload,Bottom.Payload});
        if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, Result.Denial.Detail);
        const bool Half = ScalarCriteria::WithinAngularTolerance(std::fabs(Root.SweepAngle), ScalarCriteria::Pi);
        if (!Half && !CapRadialSector(Result.Payload, Root.Base, Apex, Radial, Root.SweepAngle, Root.OuterRadius)) return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "partial apex radial caps could not heal");
        const BodyReport Report = Result.Payload.Validate();
        const bool Exact = Half ? Result.Payload.Vertices.size()==10 && Result.Payload.Edges.size()==14 && Result.Payload.Coedges.size()==28 && Result.Payload.Loops.size()==6 && Result.Payload.Faces.size()==6
                                : Result.Payload.Vertices.size()==10 && Result.Payload.Edges.size()==15 && Result.Payload.Coedges.size()==30 && Result.Payload.Loops.size()==7 && Result.Payload.Faces.size()==7;
        if (!Report.Solid() || Report.Hulls!=1 || Report.Genus!=0 || Report.OpenEdges!=0 || Report.NonManifoldEdges!=0 || Report.MisorientedEdges!=0 || !Exact) return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,"partial apex chamfer did not reach exact topology");
        return Result;
    }

    struct CylinderConeRoot
    {
        Vec3   Base, Axis;
        double OuterRadius = 0.0, ShoulderHeight = 0.0;
        double BossRadius = 0.0, CylinderHeight = 0.0;
        double TopRadius = 0.0, ConeHeight = 0.0;
    };

    std::optional<CylinderConeRoot> CylinderConeBossRoot(const BrepBody& Body, int Edge) noexcept
    {
        // This is intentionally the canonical V5/E9/C18/L6/F6 stepped source: outer cylinder, annular shoulder,
        // cylindrical boss, conical frustum, and two caps. A cylinder/cone edge that merely looks circular but does
        // not have this measured support set remains outside the route.
        if (Edge < 0 || Edge >= static_cast<int>(Body.Edges.size()) || !Body.Validate().Solid() ||
            Body.Vertices.size() != 5 || Body.Edges.size() != 9 || Body.Coedges.size() != 18 ||
            Body.Loops.size() != 6 || Body.Faces.size() != 6) return std::nullopt;
        const BrepEdge& RootEdge = Body.Edges[Edge];
        if (!RootEdge.Closed() || RootEdge.Coedges.size() != 2) return std::nullopt;
        Vec3 RootCentre, RootNormal; double RootRadius = 0.0;
        if (!CircularFrame(RootEdge.Curve, RootCentre, RootNormal, RootRadius)) return std::nullopt;

        int BossFace = -1, ConeFace = -1;
        for (int Coedge : RootEdge.Coedges)
        {
            if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return std::nullopt;
            const int Face = Body.Coedges[Coedge].Face;
            if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return std::nullopt;
            const SurfaceClassification Class = Body.Faces[Face].Surface.Classification;
            if (Class == SurfaceClassification::Cylinder)
            {
                if (BossFace >= 0) return std::nullopt;
                BossFace = Face;
            }
            else if (Class == SurfaceClassification::Cone)
            {
                if (ConeFace >= 0) return std::nullopt;
                ConeFace = Face;
            }
            else return std::nullopt;
        }
        if (BossFace < 0 || ConeFace < 0) return std::nullopt;
        Vec3 Axis = Body.Faces[BossFace].Surface.Axis.Normalised();
        if (Axis.Length() <= Tol || std::fabs(RootNormal.Dot(Axis)) < 1.0 - ScalarCriteria::GeometricTolerance) return std::nullopt;

        Vec3 CylinderStart, CylinderEnd; double CylinderRadius = 0.0;
        if (!CylinderEndCentres(Body.Faces[BossFace].Surface, CylinderStart, CylinderEnd, CylinderRadius)) return std::nullopt;
        const double Scale = std::max({ 1.0, RootRadius, CylinderRadius, CylinderStart.Distance(CylinderEnd) });
        const double Epsilon = ScalarCriteria::GeometricTolerance * Scale;
        if (std::fabs(CylinderRadius - RootRadius) > Epsilon) return std::nullopt;
        Vec3 ShoulderCentre;
        if (CylinderStart.Distance(RootCentre) <= Epsilon) ShoulderCentre = CylinderEnd;
        else if (CylinderEnd.Distance(RootCentre) <= Epsilon) ShoulderCentre = CylinderStart;
        else return std::nullopt;
        const Vec3 CylinderDirection = (RootCentre - ShoulderCentre).Normalised();
        if (CylinderDirection.Length() <= Tol || CylinderDirection.Dot(Axis) < 1.0 - ScalarCriteria::GeometricTolerance) return std::nullopt;
        const double CylinderHeight = ShoulderCentre.Distance(RootCentre);
        if (CylinderHeight <= Epsilon) return std::nullopt;

        Vec3 ConeStart, ConeEnd; double ConeStartRadius = 0.0, TopRadius = 0.0;
        if (!ConeEndCentres(Body.Faces[ConeFace].Surface, ConeStart, ConeEnd, ConeStartRadius, TopRadius)) return std::nullopt;
        if (ConeStart.Distance(RootCentre) <= Epsilon)
        {
            if (std::fabs(ConeStartRadius - RootRadius) > Epsilon) return std::nullopt;
        }
        else if (ConeEnd.Distance(RootCentre) <= Epsilon)
        {
            if (std::fabs(TopRadius - RootRadius) > Epsilon) return std::nullopt;
            std::swap(ConeStart, ConeEnd); std::swap(ConeStartRadius, TopRadius);
        }
        else return std::nullopt;
        const Vec3 ConeDirection = (ConeEnd - RootCentre).Normalised();
        const double ConeHeight = RootCentre.Distance(ConeEnd);
        if (ConeHeight <= Epsilon || ConeDirection.Dot(Axis) < 1.0 - ScalarCriteria::GeometricTolerance || TopRadius <= Tol)
            return std::nullopt;

        int ShoulderFace = -1;
        for (size_t Face = 0; Face < Body.Faces.size(); ++Face)
            if (Body.Faces[Face].Surface.Classification == SurfaceClassification::Revolution)
            {
                if (ShoulderFace >= 0) return std::nullopt;
                ShoulderFace = static_cast<int>(Face);
            }
        if (ShoulderFace < 0) return std::nullopt;

        int OuterEdge = -1; double OuterRadius = 0.0;
        for (int Loop : Body.Faces[ShoulderFace].Loops)
        {
            if (Loop < 0 || Loop >= static_cast<int>(Body.Loops.size())) return std::nullopt;
            for (int Coedge : Body.Loops[Loop].Coedges)
            {
                if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return std::nullopt;
                const int Candidate = Body.Coedges[Coedge].Edge;
                if (Candidate < 0 || Candidate >= static_cast<int>(Body.Edges.size()) || Candidate == Edge) continue;
                Vec3 Centre, Normal; double Radius = 0.0;
                if (!CircularFrame(Body.Edges[Candidate].Curve, Centre, Normal, Radius) ||
                    Centre.Distance(ShoulderCentre) > Epsilon || std::fabs(Normal.Dot(Axis)) < 1.0 - ScalarCriteria::GeometricTolerance)
                    continue;
                if (std::fabs(Radius - RootRadius) <= Epsilon) continue;
                if (Radius <= RootRadius + Epsilon || (OuterEdge >= 0 && OuterEdge != Candidate)) return std::nullopt;
                OuterEdge = Candidate; OuterRadius = Radius;
            }
        }
        if (OuterEdge < 0 || !Body.Edges[OuterEdge].Closed() || Body.Edges[OuterEdge].Coedges.size() != 2) return std::nullopt;
        int OuterFace = -1;
        for (int Coedge : Body.Edges[OuterEdge].Coedges)
        {
            const int Face = Body.Coedges[Coedge].Face;
            if (Face == ShoulderFace) continue;
            if (OuterFace >= 0 || Face < 0 || Face >= static_cast<int>(Body.Faces.size()) ||
                Body.Faces[Face].Surface.Classification != SurfaceClassification::Cylinder) return std::nullopt;
            OuterFace = Face;
        }
        if (OuterFace < 0) return std::nullopt;
        Vec3 OuterStart, OuterEnd; double ClassifiedOuterRadius = 0.0;
        if (!CylinderEndCentres(Body.Faces[OuterFace].Surface, OuterStart, OuterEnd, ClassifiedOuterRadius) ||
            std::fabs(ClassifiedOuterRadius - OuterRadius) > Epsilon) return std::nullopt;
        Vec3 Base;
        if (OuterStart.Distance(ShoulderCentre) <= Epsilon) Base = OuterEnd;
        else if (OuterEnd.Distance(ShoulderCentre) <= Epsilon) Base = OuterStart;
        else return std::nullopt;
        const double ShoulderHeight = Base.Distance(ShoulderCentre);
        if (ShoulderHeight <= Epsilon || (ShoulderCentre - Base).Normalised().Dot(Axis) < 1.0 - ScalarCriteria::GeometricTolerance)
            return std::nullopt;

        int BottomCaps = 0, TopCaps = 0;
        const Vec3 BossTop = RootCentre + Axis * ConeHeight;
        const double CapEpsilon = ScalarCriteria::GeometricTolerance * std::max({ 1.0, OuterRadius, ShoulderHeight, ConeHeight });
        for (size_t Face = 0; Face < Body.Faces.size(); ++Face)
        {
            const int FaceIndex = static_cast<int>(Face);
            if (FaceIndex == ShoulderFace || FaceIndex == BossFace || FaceIndex == ConeFace || FaceIndex == OuterFace) continue;
            Vec3 Normal;
            if (!PlanarNormal(Body, FaceIndex, Normal) || std::fabs(Normal.Dot(Axis)) < 1.0 - ScalarCriteria::GeometricTolerance ||
                Body.Faces[Face].Loops.size() != 1) return std::nullopt;
            const Vec3 Point = Body.Faces[Face].Surface.Sample(
                0.5 * (Body.Faces[Face].Surface.DomainStartU() + Body.Faces[Face].Surface.DomainEndU()),
                0.5 * (Body.Faces[Face].Surface.DomainStartV() + Body.Faces[Face].Surface.DomainEndV()));
            if (std::fabs((Point - Base).Dot(Axis)) <= CapEpsilon) ++BottomCaps;
            else if (std::fabs((Point - BossTop).Dot(Axis)) <= CapEpsilon) ++TopCaps;
            else return std::nullopt;
        }
        if (BottomCaps != 1 || TopCaps != 1) return std::nullopt;
        return CylinderConeRoot{ Base, Axis, OuterRadius, ShoulderHeight, RootRadius, CylinderHeight, TopRadius, ConeHeight };
    }

    Deliver<BrepBody> ChamferCylinderConeBossRoot(const CylinderConeRoot& Root, double SetBack) noexcept
    {
        if (SetBack <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "set-back is zero or negative");
        if (SetBack >= Root.CylinderHeight - Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "cylinder-cone root setback consumes the cylindrical support");
        const double Slant = std::hypot(Root.ConeHeight, Root.TopRadius - Root.BossRadius);
        if (SetBack >= Slant - Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "cylinder-cone root setback consumes the conical support");
        const double Axial = SetBack * Root.ConeHeight / Slant;
        const double ContactRadius = Root.BossRadius + (Root.TopRadius - Root.BossRadius) * Axial / Root.ConeHeight;
        const double BandHeight = SetBack + Axial;
        if (Axial <= Tol || BandHeight <= Tol || Axial >= Root.ConeHeight - Tol || ContactRadius <= Tol)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "cylinder-cone root contact is degenerate");
        const Vec3 ShoulderCentre = Root.Base + Root.Axis * Root.ShoulderHeight;
        const Vec3 RootCentre = ShoulderCentre + Root.Axis * Root.CylinderHeight;
        const Vec3 Radial = Workplane::FromNormal(Root.Base, Root.Axis).AxisX;
        Deliver<NurbsSurface> Outer = NurbsSurface::Cylinder(Root.Base, Root.Axis, Root.OuterRadius, Root.ShoulderHeight);
        Deliver<NurbsCurve> ShoulderLine = NurbsCurve::Line(ShoulderCentre + Radial * Root.OuterRadius,
                                                            ShoulderCentre + Radial * Root.BossRadius);
        Deliver<NurbsSurface> Shoulder = ShoulderLine
            ? NurbsSurface::Revolution(ShoulderLine.Payload, Root.Base, Root.Axis, ScalarCriteria::TwoPi)
            : Deliver<NurbsSurface>::Reject(ShoulderLine.Denial.Reason, ShoulderLine.Denial.Detail);
        Deliver<NurbsSurface> Boss = NurbsSurface::Cylinder(ShoulderCentre, Root.Axis, Root.BossRadius,
                                                            Root.CylinderHeight - SetBack);
        Deliver<NurbsSurface> Chamfer = NurbsSurface::Cone(RootCentre - Root.Axis * SetBack, Root.Axis,
                                                           Root.BossRadius, ContactRadius, BandHeight);
        Deliver<NurbsSurface> Cone = NurbsSurface::Cone(RootCentre + Root.Axis * Axial, Root.Axis,
                                                        ContactRadius, Root.TopRadius, Root.ConeHeight - Axial);
        if (!Outer || !Shoulder || !Boss || !Chamfer || !Cone)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "cylinder-cone root chamfer support is degenerate");
        Deliver<BrepBody> Result = BrepBody::Sew({ Outer.Payload, Shoulder.Payload, Boss.Payload, Chamfer.Payload, Cone.Payload });
        if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, Result.Denial.Detail);
        const BodyReport Report = Result.Payload.Validate();
        if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
            Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Result.Payload.Vertices.size() != 6 ||
            Result.Payload.Edges.size() != 11 || Result.Payload.Loops.size() != 7 || Result.Payload.Faces.size() != 7)
            return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "cylinder-cone root chamfer did not heal to one exact solid");
        return Result;
    }

    double CylinderConeSourceVolume(const CylinderConeRoot& Root) noexcept
    {
        return ScalarCriteria::Pi * Root.OuterRadius * Root.OuterRadius * Root.ShoulderHeight +
               ScalarCriteria::Pi * Root.BossRadius * Root.BossRadius * Root.CylinderHeight +
               ScalarCriteria::Pi * Root.ConeHeight *
                   (Root.BossRadius * Root.BossRadius + Root.BossRadius * Root.TopRadius + Root.TopRadius * Root.TopRadius) / 3.0;
    }

    double CylinderConeFilletRemoval(const CylinderConeRoot& Root, double Radius) noexcept
    {
        const double HalfAngle = std::atan2(Root.BossRadius - Root.TopRadius, Root.ConeHeight);
        const double CentreZ = -Radius * std::tan(0.5 * HalfAngle);
        const double CentreR = Root.BossRadius - Radius;
        const double ContactR = CentreR + Radius * std::cos(HalfAngle);
        const double ContactZ = CentreZ + Radius * std::sin(HalfAngle);
        auto SegmentMoment = [](double R0, double Z0, double R1, double Z1) noexcept
        {
            return (Z1 - Z0) * (R0 * R0 + R0 * R1 + R1 * R1) / 6.0;
        };
        auto ArcMoment = [&](double Start, double End) noexcept
        {
            const double S0 = std::sin(Start), S1 = std::sin(End);
            const double S20 = std::sin(2.0 * Start), S21 = std::sin(2.0 * End);
            return Radius / 2.0 * (CentreR * CentreR * (S1 - S0) +
                2.0 * CentreR * Radius * ((End - Start) / 2.0 + (S21 - S20) / 4.0) +
                Radius * Radius * ((S1 - S0) - (S1 * S1 * S1 - S0 * S0 * S0) / 3.0));
        };
        const double Moment = SegmentMoment(Root.BossRadius, CentreZ, Root.BossRadius, 0.0) +
            SegmentMoment(Root.BossRadius, 0.0, ContactR, ContactZ) + ArcMoment(HalfAngle, 0.0);
        return ScalarCriteria::TwoPi * std::fabs(Moment);
    }

    Deliver<BrepBody> FilletCylinderConeBossRoot(const CylinderConeRoot& Root, double Radius) noexcept
    {
        if (Radius <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "radius is zero or negative");
        if (Root.TopRadius <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "cylinder-cone apex fillets remain unsupported");
        const double HalfAngle = std::atan2(Root.BossRadius - Root.TopRadius, Root.ConeHeight);
        if (HalfAngle <= ScalarCriteria::AngularTolerance)
            return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "cylinder-cone fillet requires a narrowing cone");
        const double CentreZ = -Radius * std::tan(0.5 * HalfAngle);
        const double CentreR = Root.BossRadius - Radius;
        const double ContactR = CentreR + Radius * std::cos(HalfAngle);
        const double ContactZ = CentreZ + Radius * std::sin(HalfAngle);
        if (CentreR <= Tol || ContactR <= Tol || -CentreZ >= Root.CylinderHeight - Tol ||
            ContactZ <= Tol || ContactZ >= Root.ConeHeight - Tol)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "cylinder-cone fillet consumes its supports");
        const Vec3 ShoulderCentre = Root.Base + Root.Axis * Root.ShoulderHeight;
        const Vec3 RootCentre = ShoulderCentre + Root.Axis * Root.CylinderHeight;
        const Vec3 ConeTop = RootCentre + Root.Axis * Root.ConeHeight;
        const Vec3 Radial = Workplane::FromNormal(Root.Base, Root.Axis).AxisX;
        auto RevolveLine = [&](Vec3 Start, Vec3 End) -> Deliver<NurbsSurface>
        {
            Deliver<NurbsCurve> Line = NurbsCurve::Line(Start, End);
            return Line ? NurbsSurface::Revolution(Line.Payload, Root.Base, Root.Axis, ScalarCriteria::TwoPi)
                        : Deliver<NurbsSurface>::Reject(Line.Denial.Reason, Line.Denial.Detail);
        };
        Deliver<NurbsSurface> Outer = NurbsSurface::Cylinder(Root.Base, Root.Axis, Root.OuterRadius, Root.ShoulderHeight);
        Deliver<NurbsCurve> ShoulderLine = NurbsCurve::Line(ShoulderCentre + Radial * Root.OuterRadius,
                                                            ShoulderCentre + Radial * Root.BossRadius);
        Deliver<NurbsSurface> Shoulder = ShoulderLine
            ? NurbsSurface::Revolution(ShoulderLine.Payload, Root.Base, Root.Axis, ScalarCriteria::TwoPi)
            : Deliver<NurbsSurface>::Reject(ShoulderLine.Denial.Reason, ShoulderLine.Denial.Detail);
        const Vec3 CylinderContact = RootCentre + Root.Axis * CentreZ + Radial * Root.BossRadius;
        const Vec3 ConeContact = RootCentre + Root.Axis * ContactZ + Radial * ContactR;
        Deliver<NurbsSurface> Boss = RevolveLine(ShoulderCentre + Radial * Root.BossRadius, CylinderContact);
        const Vec3 MeridianCentre = RootCentre + Root.Axis * CentreZ + Radial * CentreR;
        auto OnMeridian = [&](double Angle) noexcept
        {
            return MeridianCentre + (Radial * std::cos(Angle) + Root.Axis * std::sin(Angle)) * Radius;
        };
        Deliver<NurbsCurve> Meridian = NurbsCurve::ArcThreePoints(
            OnMeridian(0.0), OnMeridian(0.5 * HalfAngle), OnMeridian(HalfAngle));
        Deliver<NurbsSurface> Roll = Meridian
            ? NurbsSurface::Revolution(Meridian.Payload, Root.Base, Root.Axis, ScalarCriteria::TwoPi)
            : Deliver<NurbsSurface>::Reject(Meridian.Denial.Reason, Meridian.Denial.Detail);
        Deliver<NurbsSurface> Cone = RevolveLine(ConeContact, ConeTop + Radial * Root.TopRadius);
        if (!Outer || !Shoulder || !Boss || !Roll || !Cone)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "cylinder-cone fillet support is degenerate");
        Boss.Payload.Classification = SurfaceClassification::Cylinder;
        Boss.Payload.Origin = ShoulderCentre; Boss.Payload.Axis = Root.Axis;
        Boss.Payload.RadiusMajor = Boss.Payload.RadiusMinor = Root.BossRadius;
        Roll.Payload.Classification = SurfaceClassification::Torus;
        Roll.Payload.Origin = RootCentre + Root.Axis * CentreZ;
        Roll.Payload.Axis = Root.Axis;
        Roll.Payload.RadiusMajor = CentreR; Roll.Payload.RadiusMinor = Radius;
        Cone.Payload.Classification = SurfaceClassification::Cone;
        Cone.Payload.Origin = RootCentre + Root.Axis * ContactZ;
        Cone.Payload.Axis = Root.Axis;
        Cone.Payload.RadiusMajor = ContactR; Cone.Payload.RadiusMinor = Root.TopRadius;
        Deliver<BrepBody> Result = BrepBody::Sew({ Outer.Payload, Shoulder.Payload, Boss.Payload, Roll.Payload, Cone.Payload });
        if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, Result.Denial.Detail);
        const BodyReport Report = Result.Payload.Validate();
        const bool ExactTopology = Report.Hulls == 1 && Report.Genus == 0 && Result.Payload.Vertices.size() == 6 &&
            Result.Payload.Edges.size() == 11 && Result.Payload.Coedges.size() == 22 &&
            Result.Payload.Loops.size() == 7 && Result.Payload.Faces.size() == 7;
        const double TargetVolume = CylinderConeSourceVolume(Root) - CylinderConeFilletRemoval(Root, Radius);
        if (!Report.Solid() || Report.OpenEdges != 0 || Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 ||
            !ExactTopology || !ScalarCriteria::WithinVolumeTolerance(Report.Volume, TargetVolume))
            return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "cylinder-cone fillet did not reach exact analytic topology");
        return Result;
    }

    struct ConeConeRoot
    {
        Vec3 Base, Axis;
        double BaseRadius = 0.0, RootRadius = 0.0, TopRadius = 0.0;
        double LowerHeight = 0.0, UpperHeight = 0.0;
    };

    std::optional<ConeConeRoot> ConeConeBossRoot(const BrepBody& Body, int Edge) noexcept
    {
        if (Edge < 0 || Edge >= static_cast<int>(Body.Edges.size()) || !Body.Validate().Solid() ||
            Body.Vertices.size() != 5 || Body.Edges.size() != 7 || Body.Coedges.size() != 14 ||
            Body.Loops.size() != 4 || Body.Faces.size() != 4) return std::nullopt;
        const BrepEdge& RootEdge = Body.Edges[Edge];
        if (!RootEdge.Closed() || RootEdge.Coedges.size() != 2) return std::nullopt;
        int LowerFace = -1, UpperFace = -1;
        Vec3 RootCentre, Axis; double RootRadius = 0.0; bool First = true;
        Vec3 Base, Top; double BaseRadius = 0.0, TopRadius = 0.0, LowerHeight = 0.0, UpperHeight = 0.0;
        for (int Coedge : RootEdge.Coedges)
        {
            if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return std::nullopt;
            const int Face = Body.Coedges[Coedge].Face;
            if (Face < 0 || Face >= static_cast<int>(Body.Faces.size()) ||
                Body.Faces[Face].Surface.Classification != SurfaceClassification::Cone) return std::nullopt;
            Vec3 Centre, Normal; double Radius = 0.0;
            if (!CircularFrame(RootEdge.Curve, Centre, Normal, Radius)) return std::nullopt;
            Vec3 Start, End; double StartRadius = 0.0, EndRadius = 0.0;
            if (!ConeEndCentres(Body.Faces[Face].Surface, Start, End, StartRadius, EndRadius)) return std::nullopt;
            Vec3 Far; double FarRadius = 0.0;
            const double Epsilon = ScalarCriteria::GeometricTolerance * std::max({ 1.0, Radius, Start.Distance(End) });
            if (Start.Distance(Centre) <= Epsilon) { Far = End; FarRadius = EndRadius; }
            else if (End.Distance(Centre) <= Epsilon) { Far = Start; FarRadius = StartRadius; }
            else return std::nullopt;
            if (std::fabs(FarRadius - Radius) <= Epsilon || FarRadius <= Tol) return std::nullopt;
            if (First)
            {
                RootCentre = Centre; RootRadius = Radius; Axis = Body.Faces[Face].Surface.Axis.Normalised(); First = false;
            }
            else if (Centre.Distance(RootCentre) > Epsilon || std::fabs(Radius - RootRadius) > Epsilon ||
                     Body.Faces[Face].Surface.Axis.Normalised().Dot(Axis) < 1.0 - ScalarCriteria::GeometricTolerance) return std::nullopt;
            const double Along = (Far - RootCentre).Dot(Axis);
            if (std::fabs(Along) <= Epsilon) return std::nullopt;
            if (Along < 0.0)
            {
                if (LowerFace >= 0) return std::nullopt;
                LowerFace = Face; Base = Far; BaseRadius = FarRadius; LowerHeight = -Along;
            }
            else
            {
                if (UpperFace >= 0) return std::nullopt;
                UpperFace = Face; Top = Far; TopRadius = FarRadius; UpperHeight = Along;
            }
        }
        if (First || LowerFace < 0 || UpperFace < 0 || LowerHeight <= Tol || UpperHeight <= Tol ||
            Axis.Length() <= Tol || BaseRadius <= Tol || TopRadius <= Tol) return std::nullopt;
        int Caps = 0;
        for (size_t Face = 0; Face < Body.Faces.size(); ++Face)
        {
            const int FaceIndex = static_cast<int>(Face);
            if (FaceIndex == LowerFace || FaceIndex == UpperFace) continue;
            Vec3 Normal;
            if (!PlanarNormal(Body, FaceIndex, Normal) || std::fabs(Normal.Dot(Axis)) < 1.0 - ScalarCriteria::GeometricTolerance ||
                Body.Faces[FaceIndex].Loops.size() != 1) return std::nullopt;
            const NurbsSurface& Surface = Body.Faces[FaceIndex].Surface;
            const Vec3 Point = Surface.Sample(0.5 * (Surface.DomainStartU() + Surface.DomainEndU()),
                                              0.5 * (Surface.DomainStartV() + Surface.DomainEndV()));
            const double CapEpsilon = ScalarCriteria::GeometricTolerance * std::max(1.0, LowerHeight + UpperHeight);
            if (std::fabs((Point - Base).Dot(Axis)) <= CapEpsilon ||
                std::fabs((Point - Top).Dot(Axis)) <= CapEpsilon) ++Caps;
            else return std::nullopt;
        }
        if (Caps != 2 || std::fabs(BaseRadius - RootRadius) <= Tol || std::fabs(TopRadius - RootRadius) <= Tol ||
            std::fabs(BaseRadius - TopRadius) <= Tol) return std::nullopt;
        return ConeConeRoot{ Base, Axis, BaseRadius, RootRadius, TopRadius, LowerHeight, UpperHeight };
    }

    double ConeConeSourceVolume(const ConeConeRoot& Root) noexcept
    {
        return ScalarCriteria::Pi * Root.LowerHeight *
                   (Root.BaseRadius * Root.BaseRadius + Root.BaseRadius * Root.RootRadius + Root.RootRadius * Root.RootRadius) / 3.0 +
               ScalarCriteria::Pi * Root.UpperHeight *
                   (Root.RootRadius * Root.RootRadius + Root.RootRadius * Root.TopRadius + Root.TopRadius * Root.TopRadius) / 3.0;
    }

    double ConeConeFilletRemoval(const ConeConeRoot& Root, double Radius) noexcept
    {
        const double LowerAngle = std::atan2(Root.BaseRadius - Root.RootRadius, Root.LowerHeight);
        const double UpperAngle = std::atan2(Root.RootRadius - Root.TopRadius, Root.UpperHeight);
        const double Det = std::sin(UpperAngle - LowerAngle);
        const double LowerCos = std::cos(LowerAngle), LowerSin = std::sin(LowerAngle);
        const double UpperCos = std::cos(UpperAngle), UpperSin = std::sin(UpperAngle);
        const double CentreR = Root.RootRadius + Radius * (LowerSin - UpperSin) / Det;
        const double CentreZ = Radius * (UpperCos - LowerCos) / Det;
        const double LowerContactZ = CentreZ + Radius * LowerSin;
        const double UpperContactZ = CentreZ + Radius * UpperSin;
        const double LowerContactR = CentreR + Radius * LowerCos;
        const double UpperContactR = CentreR + Radius * UpperCos;
        auto SegmentMoment = [](double R0, double Z0, double R1, double Z1) noexcept
        { return (Z1 - Z0) * (R0 * R0 + R0 * R1 + R1 * R1) / 6.0; };
        auto ArcMoment = [&](double Start, double End) noexcept
        {
            const double S0 = std::sin(Start), S1 = std::sin(End);
            const double S20 = std::sin(2.0 * Start), S21 = std::sin(2.0 * End);
            return Radius / 2.0 * (CentreR * CentreR * (S1 - S0) +
                2.0 * CentreR * Radius * ((End - Start) / 2.0 + (S21 - S20) / 4.0) +
                Radius * Radius * ((S1 - S0) - (S1 * S1 * S1 - S0 * S0 * S0) / 3.0));
        };
        const double Moment = SegmentMoment(LowerContactR, LowerContactZ, Root.RootRadius, 0.0) +
            SegmentMoment(Root.RootRadius, 0.0, UpperContactR, UpperContactZ) + ArcMoment(UpperAngle, LowerAngle);
        return ScalarCriteria::TwoPi * std::fabs(Moment);
    }

    Deliver<BrepBody> FilletConeConeBossRoot(const ConeConeRoot& Root, double Radius) noexcept
    {
        if (Radius <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "radius is zero or negative");
        const double LowerAngle = std::atan2(Root.BaseRadius - Root.RootRadius, Root.LowerHeight);
        const double UpperAngle = std::atan2(Root.RootRadius - Root.TopRadius, Root.UpperHeight);
        if (LowerAngle <= ScalarCriteria::AngularTolerance || UpperAngle <= LowerAngle + ScalarCriteria::AngularTolerance)
            return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "cone-cone fillet requires an increasing narrowing slope");
        const double Det = std::sin(UpperAngle - LowerAngle);
        const double LowerCos = std::cos(LowerAngle), LowerSin = std::sin(LowerAngle);
        const double UpperCos = std::cos(UpperAngle), UpperSin = std::sin(UpperAngle);
        const double CentreR = Root.RootRadius + Radius * (LowerSin - UpperSin) / Det;
        const double CentreZ = Radius * (UpperCos - LowerCos) / Det;
        const double LowerContactZ = CentreZ + Radius * LowerSin;
        const double UpperContactZ = CentreZ + Radius * UpperSin;
        const double LowerContactR = CentreR + Radius * LowerCos;
        const double UpperContactR = CentreR + Radius * UpperCos;
        if (-LowerContactZ >= Root.LowerHeight - Tol || UpperContactZ >= Root.UpperHeight - Tol ||
            LowerContactR <= Tol || UpperContactR <= Tol || CentreR <= Tol)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "cone-cone fillet consumes its supports");
        const Vec3 RootCentre = Root.Base + Root.Axis * Root.LowerHeight;
        const Vec3 Top = RootCentre + Root.Axis * Root.UpperHeight;
        const Vec3 Radial = Workplane::FromNormal(Root.Base, Root.Axis).AxisX;
        auto RevolveLine = [&](Vec3 Start, Vec3 End) -> Deliver<NurbsSurface>
        {
            Deliver<NurbsCurve> Line = NurbsCurve::Line(Start, End);
            return Line ? NurbsSurface::Revolution(Line.Payload, Root.Base, Root.Axis, ScalarCriteria::TwoPi)
                        : Deliver<NurbsSurface>::Reject(Line.Denial.Reason, Line.Denial.Detail);
        };
        Deliver<NurbsSurface> Lower = RevolveLine(Root.Base + Radial * Root.BaseRadius,
                                                   RootCentre + Root.Axis * LowerContactZ + Radial * LowerContactR);
        Deliver<NurbsSurface> Upper = RevolveLine(RootCentre + Root.Axis * UpperContactZ + Radial * UpperContactR,
                                                   Top + Radial * Root.TopRadius);
        const Vec3 MeridianCentre = RootCentre + Root.Axis * CentreZ + Radial * CentreR;
        auto OnMeridian = [&](double Angle) noexcept
        {
            return MeridianCentre + (Radial * std::cos(Angle) + Root.Axis * std::sin(Angle)) * Radius;
        };
        Deliver<NurbsCurve> Meridian = NurbsCurve::ArcThreePoints(
            OnMeridian(LowerAngle), OnMeridian(0.5 * (LowerAngle + UpperAngle)), OnMeridian(UpperAngle));
        Deliver<NurbsSurface> Roll = Meridian
            ? NurbsSurface::Revolution(Meridian.Payload, Root.Base, Root.Axis, ScalarCriteria::TwoPi)
            : Deliver<NurbsSurface>::Reject(Meridian.Denial.Reason, Meridian.Denial.Detail);
        if (!Lower || !Roll || !Upper)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "cone-cone fillet support is degenerate");
        Lower.Payload.Classification = SurfaceClassification::Cone;
        Lower.Payload.Origin = Root.Base; Lower.Payload.Axis = Root.Axis;
        Lower.Payload.RadiusMajor = Root.BaseRadius; Lower.Payload.RadiusMinor = LowerContactR;
        Roll.Payload.Classification = SurfaceClassification::Torus;
        Roll.Payload.Origin = RootCentre + Root.Axis * CentreZ; Roll.Payload.Axis = Root.Axis;
        Roll.Payload.RadiusMajor = CentreR; Roll.Payload.RadiusMinor = Radius;
        Upper.Payload.Classification = SurfaceClassification::Cone;
        Upper.Payload.Origin = RootCentre + Root.Axis * UpperContactZ; Upper.Payload.Axis = Root.Axis;
        Upper.Payload.RadiusMajor = UpperContactR; Upper.Payload.RadiusMinor = Root.TopRadius;
        Deliver<BrepBody> Result = BrepBody::Sew({ Lower.Payload, Roll.Payload, Upper.Payload });
        if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, Result.Denial.Detail);
        const BodyReport Report = Result.Payload.Validate();
        const bool ExactTopology = Report.Hulls == 1 && Report.Genus == 0 && Result.Payload.Vertices.size() == 4 &&
            Result.Payload.Edges.size() == 7 && Result.Payload.Coedges.size() == 14 &&
            Result.Payload.Loops.size() == 5 && Result.Payload.Faces.size() == 5;
        const double TargetVolume = ConeConeSourceVolume(Root) - ConeConeFilletRemoval(Root, Radius);
        if (!Report.Solid() || Report.OpenEdges != 0 || Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 ||
            !ExactTopology || !ScalarCriteria::WithinVolumeTolerance(Report.Volume, TargetVolume))
            return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "cone-cone fillet did not reach exact analytic topology");
        return Result;
    }

    struct PlaneConePartialRoot
    {
        Vec3   Base, Axis, RadialStart;
        double OuterRadius = 0.0, ShoulderHeight = 0.0;
        double FootRadius = 0.0, TopRadius = 0.0, BossHeight = 0.0;
        double SweepAngle = 0.0;
    };

    std::optional<PlaneConePartialRoot> PlaneConePartialBossRoot(const BrepBody& Body, int Edge) noexcept
    {
        if (Edge < 0 || Edge >= static_cast<int>(Body.Edges.size()) || !Body.Validate().Solid()) return std::nullopt;
        Deliver<std::vector<int>> ChainResult = BlendSolver::TangentChain(Body, Edge);
        if (!ChainResult || ChainResult.Payload.empty()) return std::nullopt;
        const std::vector<int>& RootEdges = ChainResult.Payload;
        const size_t SegmentCount = RootEdges.size();
        const bool SemicircleTopology = Body.Vertices.size() == 4 * SegmentCount + 6 &&
            Body.Edges.size() == 9 * SegmentCount + 5 && Body.Coedges.size() == 18 * SegmentCount + 10 &&
            Body.Loops.size() == 5 * SegmentCount + 1 && Body.Faces.size() == 5 * SegmentCount + 1;
        const bool SectorTopology = Body.Vertices.size() == 4 * SegmentCount + 6 &&
            Body.Edges.size() == 9 * SegmentCount + 6 && Body.Coedges.size() == 18 * SegmentCount + 12 &&
            Body.Loops.size() == 5 * SegmentCount + 2 && Body.Faces.size() == 5 * SegmentCount + 2;
        if (!SemicircleTopology && !SectorTopology) return std::nullopt;

        Vec3 RootCentre, Axis; double FootRadius = 0.0; bool FirstRoot = true;
        std::vector<int> ShoulderFaces, ConeFaces;
        for (int RootIndex : RootEdges)
        {
            const BrepEdge& RootEdge = Body.Edges[RootIndex];
            if (RootEdge.Coedges.size() != 2) return std::nullopt;
            Vec3 CandidateCentre, CandidateNormal; double CandidateRadius = 0.0;
            if (!CircularFrame(RootEdge.Curve, CandidateCentre, CandidateNormal, CandidateRadius)) return std::nullopt;
            int ShoulderFace = -1, ConeFace = -1;
            for (int Coedge : RootEdge.Coedges)
            {
                if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return std::nullopt;
                const int Face = Body.Coedges[Coedge].Face;
                if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return std::nullopt;
                const SurfaceClassification Class = Body.Faces[Face].Surface.Classification;
                if (Class == SurfaceClassification::Cone)
                {
                    if (ConeFace >= 0) return std::nullopt;
                    ConeFace = Face;
                }
                else if (Class == SurfaceClassification::Revolution)
                {
                    if (ShoulderFace >= 0) return std::nullopt;
                    ShoulderFace = Face;
                }
                else return std::nullopt;
            }
            if (ShoulderFace < 0 || ConeFace < 0) return std::nullopt;
            Vec3 CandidateAxis = Body.Faces[ConeFace].Surface.Axis.Normalised();
            if (CandidateAxis.Length() <= Tol || std::fabs(CandidateNormal.Dot(CandidateAxis)) < 1.0 - ScalarCriteria::GeometricTolerance)
                return std::nullopt;
            Vec3 ConeStart, ConeEnd; double ConeStartRadius = 0.0, ConeEndRadius = 0.0;
            if (!ConeEndCentres(Body.Faces[ConeFace].Surface, ConeStart, ConeEnd, ConeStartRadius, ConeEndRadius)) return std::nullopt;
            if (ConeStart.Distance(CandidateCentre) > ScalarCriteria::GeometricTolerance * std::max(1.0, CandidateRadius) &&
                ConeEnd.Distance(CandidateCentre) > ScalarCriteria::GeometricTolerance * std::max(1.0, CandidateRadius)) return std::nullopt;
            const double Epsilon = ScalarCriteria::GeometricTolerance * std::max({ 1.0, CandidateRadius,
                                                                                       ConeStart.Distance(ConeEnd) });
            const bool RootAtStart = ConeStart.Distance(CandidateCentre) <= Epsilon;
            const double MeasuredFoot = RootAtStart ? ConeStartRadius : ConeEndRadius;
            if (std::fabs(MeasuredFoot - CandidateRadius) > Epsilon) return std::nullopt;
            if (FirstRoot)
            {
                RootCentre = CandidateCentre; Axis = CandidateAxis; FootRadius = CandidateRadius; FirstRoot = false;
            }
            else if (CandidateCentre.Distance(RootCentre) > Epsilon || CandidateAxis.Dot(Axis) < 1.0 - ScalarCriteria::GeometricTolerance ||
                     std::fabs(CandidateRadius - FootRadius) > Epsilon) return std::nullopt;
            ShoulderFaces.push_back(ShoulderFace); ConeFaces.push_back(ConeFace);
        }
        double RootSpan = 0.0;
        if (FirstRoot || ShoulderFaces.size() != SegmentCount || ConeFaces.size() != SegmentCount ||
            !CircularChain(Body, RootEdges, RootCentre, Axis, FootRadius, false, &RootSpan)) return std::nullopt;
        if (std::fabs(RootSpan) > ScalarCriteria::Pi + ScalarCriteria::AngularTolerance ||
            (SemicircleTopology && !ScalarCriteria::WithinAngularTolerance(std::fabs(RootSpan), ScalarCriteria::Pi)) ||
            (!SemicircleTopology && ScalarCriteria::WithinAngularTolerance(std::fabs(RootSpan), ScalarCriteria::Pi))) return std::nullopt;

        std::vector<int> OuterEdges, OuterFaces; double OuterRadius = 0.0;
        for (int ShoulderFace : ShoulderFaces)
        {
            int FoundOuter = -1;
            for (int Loop : Body.Faces[ShoulderFace].Loops)
            {
                if (Loop < 0 || Loop >= static_cast<int>(Body.Loops.size())) return std::nullopt;
                for (int Coedge : Body.Loops[Loop].Coedges)
                {
                    if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return std::nullopt;
                    const int Candidate = Body.Coedges[Coedge].Edge;
                    if (Candidate < 0 || Candidate >= static_cast<int>(Body.Edges.size()) ||
                        std::find(RootEdges.begin(), RootEdges.end(), Candidate) != RootEdges.end()) continue;
                    Vec3 Centre, Normal; double Radius = 0.0;
                    if (!CircularFrame(Body.Edges[Candidate].Curve, Centre, Normal, Radius) ||
                        Centre.Distance(RootCentre) > ScalarCriteria::GeometricTolerance * std::max(1.0, FootRadius) ||
                        std::fabs(Normal.Dot(Axis)) < 1.0 - ScalarCriteria::GeometricTolerance || Radius <= FootRadius) continue;
                    if (FoundOuter >= 0 && FoundOuter != Candidate) return std::nullopt;
                    FoundOuter = Candidate; OuterRadius = Radius;
                }
            }
            if (FoundOuter < 0) return std::nullopt;
            OuterEdges.push_back(FoundOuter);
            int OuterFace = -1;
            for (int Coedge : Body.Edges[FoundOuter].Coedges)
            {
                const int Face = Body.Coedges[Coedge].Face;
                if (Face == ShoulderFace) continue;
                if (OuterFace >= 0 || Face < 0 || Face >= static_cast<int>(Body.Faces.size()) ||
                    Body.Faces[Face].Surface.Classification != SurfaceClassification::Cylinder) return std::nullopt;
                OuterFace = Face;
            }
            if (OuterFace < 0) return std::nullopt;
            OuterFaces.push_back(OuterFace);
        }
        if (OuterEdges.size() != SegmentCount || OuterFaces.size() != SegmentCount || OuterRadius <= FootRadius ||
            !CircularChain(Body, OuterEdges, RootCentre, Axis, OuterRadius, false, nullptr)) return std::nullopt;

        Vec3 ShoulderCentre, Base; double ShoulderHeight = 0.0; bool FirstOuter = true;
        for (int OuterFace : OuterFaces)
        {
            Vec3 OuterStart, OuterEnd; double ClassifiedRadius = 0.0;
            if (!CylinderEndCentres(Body.Faces[OuterFace].Surface, OuterStart, OuterEnd, ClassifiedRadius)) return std::nullopt;
            const double Epsilon = ScalarCriteria::GeometricTolerance * std::max({ 1.0, OuterRadius, OuterStart.Distance(OuterEnd) });
            if (std::fabs(ClassifiedRadius - OuterRadius) > Epsilon) return std::nullopt;
            Vec3 CandidateShoulder, CandidateBase;
            if (OuterStart.Distance(RootCentre) < OuterEnd.Distance(RootCentre))
            {
                CandidateShoulder = OuterStart; CandidateBase = OuterEnd;
            }
            else { CandidateShoulder = OuterEnd; CandidateBase = OuterStart; }
            const double CandidateHeight = CandidateShoulder.Distance(CandidateBase);
            if ((CandidateShoulder - CandidateBase).Normalised().Dot(Axis) < 1.0 - ScalarCriteria::GeometricTolerance || CandidateHeight <= Epsilon)
                return std::nullopt;
            if (FirstOuter)
            {
                ShoulderCentre = CandidateShoulder; Base = CandidateBase; ShoulderHeight = CandidateHeight; FirstOuter = false;
            }
            else if (CandidateShoulder.Distance(ShoulderCentre) > Epsilon || CandidateBase.Distance(Base) > Epsilon ||
                     std::fabs(CandidateHeight - ShoulderHeight) > Epsilon) return std::nullopt;
        }
        if (FirstOuter) return std::nullopt;
        Vec3 ConeTop; double TopRadius = 0.0;
        {
            Vec3 Start, End; double StartRadius = 0.0, EndRadius = 0.0;
            if (!ConeEndCentres(Body.Faces[ConeFaces.front()].Surface, Start, End, StartRadius, EndRadius)) return std::nullopt;
            const double Epsilon = ScalarCriteria::GeometricTolerance * std::max({ 1.0, FootRadius, Start.Distance(End) });
            if (Start.Distance(RootCentre) <= Epsilon) { ConeTop = End; TopRadius = EndRadius; }
            else if (End.Distance(RootCentre) <= Epsilon) { ConeTop = Start; TopRadius = StartRadius; }
            else return std::nullopt;
        }
        const double BossHeight = ConeTop.Distance(RootCentre);
        if (BossHeight <= Tol || TopRadius <= Tol || (ConeTop - RootCentre).Normalised().Dot(Axis) < 1.0 - ScalarCriteria::GeometricTolerance)
            return std::nullopt;

        int BottomPatches = 0, TopPatches = 0; std::vector<Vec3> RadialNormals;
        const double CapEpsilon = ScalarCriteria::GeometricTolerance * std::max({ 1.0, OuterRadius, ShoulderHeight, BossHeight });
        for (size_t Face = 0; Face < Body.Faces.size(); ++Face)
        {
            const int FaceIndex = static_cast<int>(Face);
            if (std::find(ShoulderFaces.begin(), ShoulderFaces.end(), FaceIndex) != ShoulderFaces.end() ||
                std::find(ConeFaces.begin(), ConeFaces.end(), FaceIndex) != ConeFaces.end() ||
                std::find(OuterFaces.begin(), OuterFaces.end(), FaceIndex) != OuterFaces.end()) continue;
            Vec3 Normal;
            if (!PlanarNormal(Body, FaceIndex, Normal)) return std::nullopt;
            const NurbsSurface& Surface = Body.Faces[FaceIndex].Surface;
            const Vec3 Point = Surface.Sample(0.5 * (Surface.DomainStartU() + Surface.DomainEndU()),
                                              0.5 * (Surface.DomainStartV() + Surface.DomainEndV()));
            const double Alignment = std::fabs(Normal.Dot(Axis));
            if (Alignment > 1.0 - ScalarCriteria::GeometricTolerance)
            {
                if (std::fabs((Point - Base).Dot(Axis)) <= CapEpsilon) ++BottomPatches;
                else if (std::fabs((Point - ConeTop).Dot(Axis)) <= CapEpsilon) ++TopPatches;
                else return std::nullopt;
            }
            else if (Alignment < ScalarCriteria::GeometricTolerance) RadialNormals.push_back(Normal);
            else return std::nullopt;
        }
        const size_t ExpectedRadial = SemicircleTopology ? 1u : 2u;
        if (BottomPatches != static_cast<int>(SegmentCount) || TopPatches != static_cast<int>(SegmentCount) ||
            RadialNormals.size() != ExpectedRadial) return std::nullopt;
        Vec3 RadialStart; double SweepAngle = 0.0;
        if (!OpenChainSweep(Body, RootEdges, RootCentre, Axis, RadialStart, SweepAngle) ||
            !ScalarCriteria::WithinAngularTolerance(std::fabs(SweepAngle), RootSpan)) return std::nullopt;
        return PlaneConePartialRoot{ Base, Axis, RadialStart, OuterRadius, ShoulderHeight, FootRadius, TopRadius,
                                     BossHeight, SweepAngle };
    }

    Deliver<BrepBody> ChamferPlaneConePartialBossRoot(const PlaneConePartialRoot& Root, double SetBack) noexcept
    {
        if (SetBack <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "set-back is zero or negative");
        if (std::fabs(Root.SweepAngle) >= ScalarCriteria::TwoPi - ScalarCriteria::AngularTolerance)
            return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "partial plane-cone chamfer requires an open curved root");
        if (SetBack >= Root.BossHeight - Tol || Root.FootRadius + SetBack >= Root.OuterRadius - Tol)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "set-back consumes the cone height or shoulder wall");
        const double Slant = std::hypot(Root.BossHeight, Root.TopRadius - Root.FootRadius);
        if (SetBack >= Slant - Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "set-back consumes the conical support");
        const double Axial = SetBack * Root.BossHeight / Slant;
        const double ContactRadius = Root.FootRadius + (Root.TopRadius - Root.FootRadius) * Axial / Root.BossHeight;
        if (Axial <= Tol || Axial >= Root.BossHeight - Tol || ContactRadius <= Tol)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial plane-cone contact is degenerate");
        const Vec3 ShoulderCentre = Root.Base + Root.Axis * Root.ShoulderHeight;
        const Vec3 BossTop = ShoulderCentre + Root.Axis * Root.BossHeight;
        const Vec3 Radial = Root.RadialStart.Normalised();
        if (Radial.Length() <= Tol || std::fabs(Radial.Dot(Root.Axis)) > ScalarCriteria::GeometricTolerance)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial plane-cone radial frame is degenerate");
        auto RevolveLine = [&](Vec3 Start, Vec3 End) -> Deliver<NurbsSurface>
        {
            Deliver<NurbsCurve> Line = NurbsCurve::Line(Start, End);
            return Line ? NurbsSurface::Revolution(Line.Payload, Root.Base, Root.Axis, Root.SweepAngle)
                        : Deliver<NurbsSurface>::Reject(Line.Denial.Reason, Line.Denial.Detail);
        };
        Deliver<NurbsSurface> Outer = RevolveLine(Root.Base + Radial * Root.OuterRadius,
                                                   ShoulderCentre + Radial * Root.OuterRadius);
        Deliver<NurbsSurface> Shoulder = RevolveLine(ShoulderCentre + Radial * Root.OuterRadius,
                                                      ShoulderCentre + Radial * (Root.FootRadius + SetBack));
        Deliver<NurbsSurface> Chamfer = RevolveLine(ShoulderCentre + Radial * (Root.FootRadius + SetBack),
                                                     ShoulderCentre + Root.Axis * Axial + Radial * ContactRadius);
        Deliver<NurbsSurface> Boss = RevolveLine(ShoulderCentre + Root.Axis * Axial + Radial * ContactRadius,
                                                  BossTop + Radial * Root.TopRadius);
        Deliver<NurbsSurface> Bottom = RevolveLine(Root.Base, Root.Base + Radial * Root.OuterRadius);
        Deliver<NurbsSurface> Top = RevolveLine(BossTop + Radial * Root.TopRadius, BossTop);
        if (!Outer || !Shoulder || !Chamfer || !Boss || !Bottom || !Top)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial plane-cone support is degenerate");
        Outer.Payload.Classification = SurfaceClassification::Cylinder;
        Outer.Payload.Origin = Root.Base; Outer.Payload.Axis = Root.Axis;
        Outer.Payload.RadiusMajor = Outer.Payload.RadiusMinor = Root.OuterRadius;
        Chamfer.Payload.Classification = SurfaceClassification::Cone;
        Chamfer.Payload.Origin = ShoulderCentre; Chamfer.Payload.Axis = Root.Axis;
        Chamfer.Payload.RadiusMajor = Root.FootRadius + SetBack; Chamfer.Payload.RadiusMinor = ContactRadius;
        Boss.Payload.Classification = SurfaceClassification::Cone;
        Boss.Payload.Origin = ShoulderCentre + Root.Axis * Axial; Boss.Payload.Axis = Root.Axis;
        Boss.Payload.RadiusMajor = ContactRadius; Boss.Payload.RadiusMinor = Root.TopRadius;
        Deliver<BrepBody> Result = BrepBody::Sew({ Outer.Payload, Shoulder.Payload, Chamfer.Payload,
                                                   Boss.Payload, Bottom.Payload, Top.Payload });
        if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, Result.Denial.Detail);
        const bool HalfTurn = ScalarCriteria::WithinAngularTolerance(std::fabs(Root.SweepAngle), ScalarCriteria::Pi);
        if (!HalfTurn && !CapRadialSector(Result.Payload, Root.Base, BossTop, Radial, Root.SweepAngle, Root.OuterRadius))
            return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "partial plane-cone radial caps could not heal");
        const BodyReport Report = Result.Payload.Validate();
        const bool ExactTopology = HalfTurn
            ? Result.Payload.Vertices.size() == 12 && Result.Payload.Edges.size() == 17 && Result.Payload.Coedges.size() == 34 &&
              Result.Payload.Loops.size() == 7 && Result.Payload.Faces.size() == 7
            : Result.Payload.Vertices.size() == 12 && Result.Payload.Edges.size() == 18 && Result.Payload.Coedges.size() == 36 &&
              Result.Payload.Loops.size() == 8 && Result.Payload.Faces.size() == 8;
        if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
            Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || !ExactTopology)
            return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "partial plane-cone chamfer did not reach exact topology");
        return Result;
    }

    Deliver<BrepBody> FilletPlaneConePartialBossRoot(const PlaneConePartialRoot& Root, double Radius) noexcept
    {
        if (Radius <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "radius is zero or negative");
        if (std::fabs(Root.SweepAngle) >= ScalarCriteria::TwoPi - ScalarCriteria::AngularTolerance)
            return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "partial plane-cone fillet requires an open curved root");
        const double HalfAngle = std::atan2(Root.FootRadius - Root.TopRadius, Root.BossHeight);
        const double SinA = std::sin(HalfAngle), CosA = std::cos(HalfAngle);
        if (CosA <= ScalarCriteria::GeometricTolerance) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "partial plane-cone fillet angle is unsupported");
        const double ContactHeight = Radius * (1.0 - SinA);
        const double SpineRadius = Root.FootRadius + Radius * (1.0 - SinA) / CosA;
        const double ContactRadius = Root.FootRadius - ContactHeight * std::tan(HalfAngle);
        if (ContactHeight >= Root.BossHeight - Tol || SpineRadius >= Root.OuterRadius - Tol || ContactRadius <= Tol)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial plane-cone fillet consumes its supports");
        const Vec3 ShoulderCentre = Root.Base + Root.Axis * Root.ShoulderHeight;
        const Vec3 BossTop = ShoulderCentre + Root.Axis * Root.BossHeight;
        const Vec3 Radial = Root.RadialStart.Normalised();
        if (Radial.Length() <= Tol || std::fabs(Radial.Dot(Root.Axis)) > ScalarCriteria::GeometricTolerance)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial plane-cone fillet radial frame is degenerate");
        auto RevolveLine = [&](Vec3 Start, Vec3 End) -> Deliver<NurbsSurface>
        {
            Deliver<NurbsCurve> Line = NurbsCurve::Line(Start, End);
            return Line ? NurbsSurface::Revolution(Line.Payload, Root.Base, Root.Axis, Root.SweepAngle)
                        : Deliver<NurbsSurface>::Reject(Line.Denial.Reason, Line.Denial.Detail);
        };
        Deliver<NurbsSurface> Outer = RevolveLine(Root.Base + Radial * Root.OuterRadius,
                                                   ShoulderCentre + Radial * Root.OuterRadius);
        Deliver<NurbsSurface> Shoulder = RevolveLine(ShoulderCentre + Radial * Root.OuterRadius,
                                                      ShoulderCentre + Radial * SpineRadius);
        const Vec3 MeridianCentre = ShoulderCentre + Root.Axis * Radius + Radial * SpineRadius;
        auto OnMeridian = [&](double Angle) noexcept
        {
            return MeridianCentre + (Radial * std::cos(Angle) + Root.Axis * std::sin(Angle)) * Radius;
        };
        const Vec3 ShoulderContact = OnMeridian(1.5 * ScalarCriteria::Pi);
        const Vec3 ConeContact = OnMeridian(ScalarCriteria::Pi + HalfAngle);
        const Vec3 MeridianMiddle = OnMeridian(1.25 * ScalarCriteria::Pi + 0.5 * HalfAngle);
        Deliver<NurbsCurve> Meridian = NurbsCurve::ArcThreePoints(ShoulderContact, MeridianMiddle, ConeContact);
        Deliver<NurbsSurface> Roll = Meridian
            ? NurbsSurface::Revolution(Meridian.Payload, Root.Base, Root.Axis, Root.SweepAngle)
            : Deliver<NurbsSurface>::Reject(Meridian.Denial.Reason, Meridian.Denial.Detail);
        Deliver<NurbsSurface> Boss = RevolveLine(ShoulderCentre + Root.Axis * ContactHeight + Radial * ContactRadius,
                                                  BossTop + Radial * Root.TopRadius);
        Deliver<NurbsSurface> Bottom = RevolveLine(Root.Base, Root.Base + Radial * Root.OuterRadius);
        Deliver<NurbsSurface> Top = RevolveLine(BossTop + Radial * Root.TopRadius, BossTop);
        if (!Outer || !Shoulder || !Roll || !Boss || !Bottom || !Top)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial plane-cone fillet support is degenerate");
        Roll.Payload.Classification = SurfaceClassification::Torus;
        Roll.Payload.Origin = ShoulderCentre + Root.Axis * Radius;
        Roll.Payload.Axis = Root.Axis;
        Roll.Payload.RadiusMajor = SpineRadius;
        Roll.Payload.RadiusMinor = Radius;
        Deliver<BrepBody> Result = BrepBody::Sew({ Outer.Payload, Shoulder.Payload, Roll.Payload,
                                                   Boss.Payload, Bottom.Payload, Top.Payload });
        if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, Result.Denial.Detail);
        const bool HalfTurn = ScalarCriteria::WithinAngularTolerance(std::fabs(Root.SweepAngle), ScalarCriteria::Pi);
        if (!HalfTurn && !CapRadialSector(Result.Payload, Root.Base, BossTop, Radial, Root.SweepAngle, Root.OuterRadius))
            return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "partial plane-cone fillet radial caps could not heal");
        const BodyReport Report = Result.Payload.Validate();
        const bool ExactTopology = HalfTurn
            ? Result.Payload.Vertices.size() == 12 && Result.Payload.Edges.size() == 17 && Result.Payload.Coedges.size() == 34 &&
              Result.Payload.Loops.size() == 7 && Result.Payload.Faces.size() == 7
            : Result.Payload.Vertices.size() == 12 && Result.Payload.Edges.size() == 18 && Result.Payload.Coedges.size() == 36 &&
              Result.Payload.Loops.size() == 8 && Result.Payload.Faces.size() == 8;
        if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
            Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || !ExactTopology)
            return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "partial plane-cone fillet did not reach exact topology");
        return Result;
    }

    struct ConeCylinderPartialRoot
    {
        Vec3   Base, Axis, RadialStart;
        double BaseRadius = 0.0, RootRadius = 0.0;
        double ConeHeight = 0.0, CylinderHeight = 0.0;
        double SweepAngle = 0.0;
    };

    std::optional<ConeCylinderPartialRoot> ConeCylinderPartialBossRoot(const BrepBody& Body, int Edge) noexcept
    {
        if (Edge < 0 || Edge >= static_cast<int>(Body.Edges.size()) || !Body.Validate().Solid()) return std::nullopt;
        Deliver<std::vector<int>> ChainResult = BlendSolver::TangentChain(Body, Edge);
        if (!ChainResult || ChainResult.Payload.empty()) return std::nullopt;
        const std::vector<int>& RootEdges = ChainResult.Payload;
        const size_t SegmentCount = RootEdges.size();
        const bool SemicircleTopology = SegmentCount == 2 && Body.Vertices.size() == 11 &&
            Body.Edges.size() == 18 && Body.Coedges.size() == 36 && Body.Loops.size() == 9 && Body.Faces.size() == 9;
        const bool SectorTopology = SegmentCount == 2 && Body.Vertices.size() == 11 &&
            Body.Edges.size() == 19 && Body.Coedges.size() == 38 && Body.Loops.size() == 10 && Body.Faces.size() == 10;
        if (!SemicircleTopology && !SectorTopology) return std::nullopt;

        Vec3 RootCentre, Axis; double RootRadius = 0.0; bool FirstRoot = true;
        std::vector<int> ConeFaces, CylinderFaces;
        for (int RootIndex : RootEdges)
        {
            const BrepEdge& RootEdge = Body.Edges[RootIndex];
            if (RootEdge.Coedges.size() != 2) return std::nullopt;
            Vec3 CandidateCentre, CandidateNormal; double CandidateRadius = 0.0;
            if (!CircularFrame(RootEdge.Curve, CandidateCentre, CandidateNormal, CandidateRadius)) return std::nullopt;
            int ConeFace = -1, CylinderFace = -1;
            for (int Coedge : RootEdge.Coedges)
            {
                if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return std::nullopt;
                const int Face = Body.Coedges[Coedge].Face;
                if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return std::nullopt;
                const SurfaceClassification Class = Body.Faces[Face].Surface.Classification;
                if (Class == SurfaceClassification::Cone)
                {
                    if (ConeFace >= 0) return std::nullopt;
                    ConeFace = Face;
                }
                else if (Class == SurfaceClassification::Cylinder)
                {
                    if (CylinderFace >= 0) return std::nullopt;
                    CylinderFace = Face;
                }
                else return std::nullopt;
            }
            if (ConeFace < 0 || CylinderFace < 0) return std::nullopt;
            Vec3 CandidateAxis = Body.Faces[CylinderFace].Surface.Axis.Normalised();
            if (CandidateAxis.Length() <= Tol || std::fabs(CandidateNormal.Dot(CandidateAxis)) < 1.0 - ScalarCriteria::GeometricTolerance)
                return std::nullopt;
            if (FirstRoot)
            {
                RootCentre = CandidateCentre; Axis = CandidateAxis; RootRadius = CandidateRadius; FirstRoot = false;
            }
            else if (CandidateCentre.Distance(RootCentre) > ScalarCriteria::GeometricTolerance * std::max(1.0, CandidateRadius) ||
                     CandidateAxis.Dot(Axis) < 1.0 - ScalarCriteria::GeometricTolerance ||
                     std::fabs(CandidateRadius - RootRadius) > ScalarCriteria::GeometricTolerance * std::max(1.0, RootRadius)) return std::nullopt;
            ConeFaces.push_back(ConeFace); CylinderFaces.push_back(CylinderFace);
        }

        double RootSpan = 0.0;
        if (FirstRoot || ConeFaces.size() != SegmentCount || CylinderFaces.size() != SegmentCount ||
            !CircularChain(Body, RootEdges, RootCentre, Axis, RootRadius, false, &RootSpan) ||
            std::fabs(RootSpan) > ScalarCriteria::Pi + ScalarCriteria::AngularTolerance ||
            (SemicircleTopology && !ScalarCriteria::WithinAngularTolerance(std::fabs(RootSpan), ScalarCriteria::Pi)) ||
            (!SemicircleTopology && ScalarCriteria::WithinAngularTolerance(std::fabs(RootSpan), ScalarCriteria::Pi))) return std::nullopt;

        Vec3 Base, Top; double BaseRadius = 0.0, CylinderHeight = 0.0, ConeHeight = 0.0; bool FirstSupport = true;
        for (size_t I = 0; I < SegmentCount; ++I)
        {
            Vec3 ConeStart, ConeEnd; double ConeStartRadius = 0.0, ConeEndRadius = 0.0;
            if (!ConeEndCentres(Body.Faces[ConeFaces[I]].Surface, ConeStart, ConeEnd, ConeStartRadius, ConeEndRadius)) return std::nullopt;
            const double ConeEpsilon = ScalarCriteria::GeometricTolerance * std::max({ 1.0, RootRadius, ConeStart.Distance(ConeEnd) });
            Vec3 CandidateBase; double CandidateBaseRadius = 0.0;
            if (ConeStart.Distance(RootCentre) <= ConeEpsilon)
            {
                CandidateBase = ConeEnd; CandidateBaseRadius = ConeEndRadius;
            }
            else if (ConeEnd.Distance(RootCentre) <= ConeEpsilon)
            {
                CandidateBase = ConeStart; CandidateBaseRadius = ConeStartRadius;
            }
            else return std::nullopt;
            if (std::fabs((CandidateBase - RootCentre).Normalised().Dot(Axis)) < 1.0 - ScalarCriteria::GeometricTolerance ||
                CandidateBase.Distance(RootCentre) <= ConeEpsilon || CandidateBaseRadius <= Tol ||
                std::fabs(CandidateBaseRadius - RootRadius) <= ConeEpsilon) return std::nullopt;

            Vec3 CylinderStart, CylinderEnd; double ClassifiedRadius = 0.0;
            if (!CylinderEndCentres(Body.Faces[CylinderFaces[I]].Surface, CylinderStart, CylinderEnd, ClassifiedRadius)) return std::nullopt;
            if (std::fabs(ClassifiedRadius - RootRadius) > ConeEpsilon) return std::nullopt;
            Vec3 CandidateTop;
            if (CylinderStart.Distance(RootCentre) <= ConeEpsilon) CandidateTop = CylinderEnd;
            else if (CylinderEnd.Distance(RootCentre) <= ConeEpsilon) CandidateTop = CylinderStart;
            else return std::nullopt;
            const double CandidateConeHeight = CandidateBase.Distance(RootCentre);
            const double CandidateCylinderHeight = CandidateTop.Distance(RootCentre);
            if (CandidateCylinderHeight <= ConeEpsilon ||
                (CandidateTop - RootCentre).Normalised().Dot(Axis) < 1.0 - ScalarCriteria::GeometricTolerance ||
                (RootCentre - CandidateBase).Normalised().Dot(Axis) < 1.0 - ScalarCriteria::GeometricTolerance) return std::nullopt;
            if (FirstSupport)
            {
                Base = CandidateBase; Top = CandidateTop; BaseRadius = CandidateBaseRadius;
                ConeHeight = CandidateConeHeight; CylinderHeight = CandidateCylinderHeight; FirstSupport = false;
            }
            else if (CandidateBase.Distance(Base) > ConeEpsilon || CandidateTop.Distance(Top) > ConeEpsilon ||
                     std::fabs(CandidateBaseRadius - BaseRadius) > ConeEpsilon ||
                     std::fabs(CandidateConeHeight - ConeHeight) > ConeEpsilon ||
                     std::fabs(CandidateCylinderHeight - CylinderHeight) > ConeEpsilon) return std::nullopt;
        }
        if (FirstSupport) return std::nullopt;

        int BottomPatches = 0, TopPatches = 0; std::vector<Vec3> RadialNormals;
        const double CapEpsilon = ScalarCriteria::GeometricTolerance * std::max({ 1.0, BaseRadius, ConeHeight, CylinderHeight });
        for (size_t Face = 0; Face < Body.Faces.size(); ++Face)
        {
            const int FaceIndex = static_cast<int>(Face);
            if (std::find(ConeFaces.begin(), ConeFaces.end(), FaceIndex) != ConeFaces.end() ||
                std::find(CylinderFaces.begin(), CylinderFaces.end(), FaceIndex) != CylinderFaces.end()) continue;
            Vec3 Normal;
            if (!PlanarNormal(Body, FaceIndex, Normal)) return std::nullopt;
            const NurbsSurface& Surface = Body.Faces[FaceIndex].Surface;
            const Vec3 Point = Surface.Sample(0.5 * (Surface.DomainStartU() + Surface.DomainEndU()),
                                              0.5 * (Surface.DomainStartV() + Surface.DomainEndV()));
            const double Alignment = std::fabs(Normal.Dot(Axis));
            if (Alignment > 1.0 - ScalarCriteria::GeometricTolerance)
            {
                if (std::fabs((Point - Base).Dot(Axis)) <= CapEpsilon) ++BottomPatches;
                else if (std::fabs((Point - Top).Dot(Axis)) <= CapEpsilon) ++TopPatches;
                else return std::nullopt;
            }
            else if (Alignment < ScalarCriteria::GeometricTolerance) RadialNormals.push_back(Normal);
            else return std::nullopt;
        }
        const size_t ExpectedRadial = SemicircleTopology ? 1u : 2u;
        if (BottomPatches != static_cast<int>(SegmentCount) || TopPatches != static_cast<int>(SegmentCount) ||
            RadialNormals.size() != ExpectedRadial) return std::nullopt;
        Vec3 RadialStart; double SweepAngle = 0.0;
        if (!OpenChainSweep(Body, RootEdges, RootCentre, Axis, RadialStart, SweepAngle) ||
            !ScalarCriteria::WithinAngularTolerance(std::fabs(SweepAngle), RootSpan)) return std::nullopt;
        return ConeCylinderPartialRoot{ Base, Axis, RadialStart, BaseRadius, RootRadius,
                                        ConeHeight, CylinderHeight, SweepAngle };
    }

    Deliver<BrepBody> ChamferConeCylinderPartialBossRoot(const ConeCylinderPartialRoot& Root, double SetBack) noexcept
    {
        if (SetBack <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "set-back is zero or negative");
        if (std::fabs(Root.SweepAngle) >= ScalarCriteria::TwoPi - ScalarCriteria::AngularTolerance)
            return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "partial cone-cylinder chamfer requires an open curved root");
        if (SetBack >= Root.CylinderHeight - Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "set-back consumes the cylindrical support");
        const double Slant = std::hypot(Root.ConeHeight, Root.BaseRadius - Root.RootRadius);
        if (SetBack >= Slant - Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "set-back consumes the conical support");
        const double Axial = SetBack * Root.ConeHeight / Slant;
        const double ContactRadius = Root.RootRadius + (Root.BaseRadius - Root.RootRadius) * Axial / Root.ConeHeight;
        if (Axial <= Tol || Axial >= Root.ConeHeight - Tol || ContactRadius <= Tol)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "cone-cylinder contact is degenerate");
        const Vec3 RootCentre = Root.Base + Root.Axis * Root.ConeHeight;
        const Vec3 Top = RootCentre + Root.Axis * Root.CylinderHeight;
        const Vec3 Radial = Root.RadialStart.Normalised();
        if (Radial.Length() <= Tol || std::fabs(Radial.Dot(Root.Axis)) > ScalarCriteria::GeometricTolerance)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial cone-cylinder radial frame is degenerate");
        auto RevolveLine = [&](Vec3 Start, Vec3 End) -> Deliver<NurbsSurface>
        {
            Deliver<NurbsCurve> Line = NurbsCurve::Line(Start, End);
            return Line ? NurbsSurface::Revolution(Line.Payload, Root.Base, Root.Axis, Root.SweepAngle)
                        : Deliver<NurbsSurface>::Reject(Line.Denial.Reason, Line.Denial.Detail);
        };
        Deliver<NurbsSurface> Cone = RevolveLine(Root.Base + Radial * Root.BaseRadius,
                                                  RootCentre - Root.Axis * Axial + Radial * ContactRadius);
        Deliver<NurbsSurface> Chamfer = RevolveLine(RootCentre - Root.Axis * Axial + Radial * ContactRadius,
                                                     RootCentre + Root.Axis * SetBack + Radial * Root.RootRadius);
        Deliver<NurbsSurface> Cylinder = RevolveLine(RootCentre + Root.Axis * SetBack + Radial * Root.RootRadius,
                                                      Top + Radial * Root.RootRadius);
        Deliver<NurbsSurface> Bottom = RevolveLine(Root.Base, Root.Base + Radial * Root.BaseRadius);
        Deliver<NurbsSurface> TopCap = RevolveLine(Top + Radial * Root.RootRadius, Top);
        if (!Cone || !Chamfer || !Cylinder || !Bottom || !TopCap)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial cone-cylinder support is degenerate");
        Cone.Payload.Classification = SurfaceClassification::Cone;
        Cone.Payload.Origin = Root.Base; Cone.Payload.Axis = Root.Axis;
        Cone.Payload.RadiusMajor = Root.BaseRadius; Cone.Payload.RadiusMinor = ContactRadius;
        Chamfer.Payload.Classification = SurfaceClassification::Cone;
        Chamfer.Payload.Origin = Root.Base; Chamfer.Payload.Axis = Root.Axis;
        Chamfer.Payload.RadiusMajor = ContactRadius; Chamfer.Payload.RadiusMinor = Root.RootRadius;
        Cylinder.Payload.Classification = SurfaceClassification::Cylinder;
        Cylinder.Payload.Origin = RootCentre + Root.Axis * SetBack; Cylinder.Payload.Axis = Root.Axis;
        Cylinder.Payload.RadiusMajor = Cylinder.Payload.RadiusMinor = Root.RootRadius;
        Deliver<BrepBody> Result = BrepBody::Sew({ Cone.Payload, Chamfer.Payload, Cylinder.Payload,
                                                   Bottom.Payload, TopCap.Payload });
        if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, Result.Denial.Detail);
        const bool HalfTurn = ScalarCriteria::WithinAngularTolerance(std::fabs(Root.SweepAngle), ScalarCriteria::Pi);
        if (!HalfTurn && !CapRadialSector(Result.Payload, Root.Base, Top, Radial, Root.SweepAngle,
                                          std::max(Root.BaseRadius, Root.RootRadius)))
            return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "partial cone-cylinder radial caps could not heal");
        const BodyReport Report = Result.Payload.Validate();
        const bool ExactTopology = HalfTurn
            ? Report.Hulls == 1 && Report.Genus == 0 && Result.Payload.Vertices.size() == 10 &&
              Result.Payload.Edges.size() == 14 && Result.Payload.Coedges.size() == 28 &&
              Result.Payload.Loops.size() == 6 && Result.Payload.Faces.size() == 6
            : Report.Hulls == 1 && Report.Genus == 0 && Result.Payload.Vertices.size() == 10 &&
              Result.Payload.Edges.size() == 15 && Result.Payload.Coedges.size() == 30 &&
              Result.Payload.Loops.size() == 7 && Result.Payload.Faces.size() == 7;
        if (!Report.Solid() || Report.OpenEdges != 0 || Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || !ExactTopology)
            return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "partial cone-cylinder chamfer did not reach exact topology");
        return Result;
    }

    Deliver<BrepBody> FilletConeCylinderPartialBossRoot(const ConeCylinderPartialRoot& Root, double Radius) noexcept
    {
        if (Radius <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "radius is zero or negative");
        if (std::fabs(Root.SweepAngle) >= ScalarCriteria::TwoPi - ScalarCriteria::AngularTolerance)
            return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "partial cone-cylinder fillet requires an open root");
        const double HalfAngle = std::atan2(Root.RootRadius - Root.BaseRadius, Root.ConeHeight);
        if (HalfAngle <= ScalarCriteria::AngularTolerance)
            return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "partial cone-cylinder fillet requires a cone that widens toward the cylinder");
        const double CentreR = Root.RootRadius - Radius;
        const double CentreZ = Radius * std::tan(0.5 * HalfAngle);
        const double ConeContactZ = CentreZ - Radius * std::sin(HalfAngle);
        const double CylinderContactZ = CentreZ;
        const double ContactR = CentreR + Radius * std::cos(HalfAngle);
        if (CentreR <= Tol || ContactR <= Tol || -ConeContactZ >= Root.ConeHeight - Tol ||
            CylinderContactZ >= Root.CylinderHeight - Tol)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial cone-cylinder fillet consumes its supports");
        const Vec3 RootCentre = Root.Base + Root.Axis * Root.ConeHeight;
        const Vec3 Top = RootCentre + Root.Axis * Root.CylinderHeight;
        const Vec3 Radial = Root.RadialStart.Normalised();
        if (Radial.Length() <= Tol || std::fabs(Radial.Dot(Root.Axis)) > ScalarCriteria::GeometricTolerance)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial cone-cylinder fillet radial frame is degenerate");
        auto RevolveLine = [&](Vec3 Start, Vec3 End) -> Deliver<NurbsSurface>
        {
            Deliver<NurbsCurve> Line = NurbsCurve::Line(Start, End);
            return Line ? NurbsSurface::Revolution(Line.Payload, Root.Base, Root.Axis, Root.SweepAngle)
                        : Deliver<NurbsSurface>::Reject(Line.Denial.Reason, Line.Denial.Detail);
        };
        Deliver<NurbsSurface> Cone = RevolveLine(Root.Base + Radial * Root.BaseRadius,
                                                  RootCentre + Root.Axis * ConeContactZ + Radial * ContactR);
        Deliver<NurbsSurface> Cylinder = RevolveLine(RootCentre + Root.Axis * CylinderContactZ + Radial * Root.RootRadius,
                                                      Top + Radial * Root.RootRadius);
        const Vec3 MeridianCentre = RootCentre + Root.Axis * CentreZ + Radial * CentreR;
        auto OnMeridian = [&](double Angle) noexcept
        {
            return MeridianCentre + (Radial * std::cos(Angle) + Root.Axis * std::sin(Angle)) * Radius;
        };
        Deliver<NurbsCurve> Meridian = NurbsCurve::ArcThreePoints(
            OnMeridian(-HalfAngle), OnMeridian(-0.5 * HalfAngle), OnMeridian(0.0));
        Deliver<NurbsSurface> Roll = Meridian
            ? NurbsSurface::Revolution(Meridian.Payload, Root.Base, Root.Axis, Root.SweepAngle)
            : Deliver<NurbsSurface>::Reject(Meridian.Denial.Reason, Meridian.Denial.Detail);
        Deliver<NurbsSurface> Bottom = RevolveLine(Root.Base, Root.Base + Radial * Root.BaseRadius);
        Deliver<NurbsSurface> TopCap = RevolveLine(Top + Radial * Root.RootRadius, Top);
        if (!Cone || !Roll || !Cylinder || !Bottom || !TopCap)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial cone-cylinder fillet support is degenerate");
        Cone.Payload.Classification = SurfaceClassification::Cone;
        Cone.Payload.Origin = Root.Base; Cone.Payload.Axis = Root.Axis;
        Cone.Payload.RadiusMajor = Root.BaseRadius; Cone.Payload.RadiusMinor = ContactR;
        Roll.Payload.Classification = SurfaceClassification::Torus;
        Roll.Payload.Origin = RootCentre + Root.Axis * CentreZ; Roll.Payload.Axis = Root.Axis;
        Roll.Payload.RadiusMajor = CentreR; Roll.Payload.RadiusMinor = Radius;
        Cylinder.Payload.Classification = SurfaceClassification::Cylinder;
        Cylinder.Payload.Origin = RootCentre + Root.Axis * CylinderContactZ; Cylinder.Payload.Axis = Root.Axis;
        Cylinder.Payload.RadiusMajor = Cylinder.Payload.RadiusMinor = Root.RootRadius;
        Deliver<BrepBody> Result = BrepBody::Sew({ Cone.Payload, Roll.Payload, Cylinder.Payload,
                                                   Bottom.Payload, TopCap.Payload });
        if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, Result.Denial.Detail);
        const bool HalfTurn = ScalarCriteria::WithinAngularTolerance(std::fabs(Root.SweepAngle), ScalarCriteria::Pi);
        if (!HalfTurn && !CapRadialSector(Result.Payload, Root.Base, Top, Radial, Root.SweepAngle,
                                          std::max(Root.BaseRadius, Root.RootRadius)))
            return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "partial cone-cylinder fillet radial caps could not heal");

        const double Fraction = std::fabs(Root.SweepAngle) / ScalarCriteria::TwoPi;
        const double SourceVolume = Fraction * ScalarCriteria::Pi *
            (Root.ConeHeight * (Root.BaseRadius * Root.BaseRadius + Root.BaseRadius * Root.RootRadius + Root.RootRadius * Root.RootRadius) / 3.0 +
             Root.CylinderHeight * Root.RootRadius * Root.RootRadius);
        const double MirrorCentreZ = -Radius * std::tan(0.5 * HalfAngle);
        const double MirrorContactR = CentreR + Radius * std::cos(HalfAngle);
        const double MirrorContactZ = MirrorCentreZ + Radius * std::sin(HalfAngle);
        const auto SegmentMoment = [](double R0, double Z0, double R1, double Z1) noexcept
        {
            return (Z1 - Z0) * (R0 * R0 + R0 * R1 + R1 * R1) / 6.0;
        };
        const double S0 = std::sin(HalfAngle), S1 = 0.0;
        const double S20 = std::sin(2.0 * HalfAngle), S21 = 0.0;
        const double ArcMoment = Radius / 2.0 * (CentreR * CentreR * (S1 - S0) +
            2.0 * CentreR * Radius * ((0.0 - HalfAngle) / 2.0 + (S21 - S20) / 4.0) +
            Radius * Radius * ((S1 - S0) - (S1 * S1 * S1 - S0 * S0 * S0) / 3.0));
        const double Removal = ScalarCriteria::TwoPi * std::fabs(
            SegmentMoment(Root.RootRadius, MirrorCentreZ, Root.RootRadius, 0.0) +
            SegmentMoment(Root.RootRadius, 0.0, MirrorContactR, MirrorContactZ) + ArcMoment);
        const double TargetVolume = SourceVolume - Fraction * Removal;
        const BodyReport Report = Result.Payload.Validate();
        const bool ExactTopology = HalfTurn
            ? Report.Hulls == 1 && Report.Genus == 0 && Result.Payload.Vertices.size() == 10 &&
              Result.Payload.Edges.size() == 14 && Result.Payload.Coedges.size() == 28 &&
              Result.Payload.Loops.size() == 6 && Result.Payload.Faces.size() == 6
            : Report.Hulls == 1 && Report.Genus == 0 && Result.Payload.Vertices.size() == 10 &&
              Result.Payload.Edges.size() == 15 && Result.Payload.Coedges.size() == 30 &&
              Result.Payload.Loops.size() == 7 && Result.Payload.Faces.size() == 7;
        if (!Report.Solid() || Report.OpenEdges != 0 || Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 ||
            !ExactTopology || !ScalarCriteria::WithinVolumeTolerance(Report.Volume, TargetVolume))
            return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "partial cone-cylinder fillet did not reach exact analytic topology");
        return Result;
    }

    struct ConeConePartialRoot
    {
        Vec3   Base, Axis, RadialStart;
        double BaseRadius = 0.0, RootRadius = 0.0, TopRadius = 0.0;
        double LowerHeight = 0.0, UpperHeight = 0.0;
        double SweepAngle = 0.0;
    };

    std::optional<ConeConePartialRoot> ConeConePartialBossRoot(const BrepBody& Body, int Edge) noexcept
    {
        if (Edge < 0 || Edge >= static_cast<int>(Body.Edges.size()) || !Body.Validate().Solid()) return std::nullopt;
        Deliver<std::vector<int>> ChainResult = BlendSolver::TangentChain(Body, Edge);
        if (!ChainResult || ChainResult.Payload.empty()) return std::nullopt;
        const std::vector<int>& RootEdges = ChainResult.Payload;
        const size_t SegmentCount = RootEdges.size();
        const bool SemicircleTopology = SegmentCount == 2 && Body.Vertices.size() == 11 &&
            Body.Edges.size() == 18 && Body.Coedges.size() == 36 && Body.Loops.size() == 9 && Body.Faces.size() == 9;
        const bool SectorTopology = SegmentCount == 2 && Body.Vertices.size() == 11 &&
            Body.Edges.size() == 19 && Body.Coedges.size() == 38 && Body.Loops.size() == 10 && Body.Faces.size() == 10;
        if (!SemicircleTopology && !SectorTopology) return std::nullopt;

        Vec3 RootCentre, Axis; double RootRadius = 0.0; bool FirstRoot = true;
        std::vector<std::pair<int, int>> ConeFacePairs;
        for (int RootIndex : RootEdges)
        {
            const BrepEdge& RootEdge = Body.Edges[RootIndex];
            if (RootEdge.Coedges.size() != 2) return std::nullopt;
            Vec3 CandidateCentre, CandidateNormal; double CandidateRadius = 0.0;
            if (!CircularFrame(RootEdge.Curve, CandidateCentre, CandidateNormal, CandidateRadius)) return std::nullopt;
            std::pair<int, int> Pair{ -1, -1 };
            for (int Coedge : RootEdge.Coedges)
            {
                if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return std::nullopt;
                const int Face = Body.Coedges[Coedge].Face;
                if (Face < 0 || Face >= static_cast<int>(Body.Faces.size()) ||
                    Body.Faces[Face].Surface.Classification != SurfaceClassification::Cone) return std::nullopt;
                if (Pair.first < 0) Pair.first = Face;
                else if (Pair.second < 0) Pair.second = Face;
                else return std::nullopt;
            }
            if (Pair.first < 0 || Pair.second < 0) return std::nullopt;
            Vec3 CandidateAxis = Body.Faces[Pair.first].Surface.Axis.Normalised();
            if (CandidateAxis.Length() <= Tol || std::fabs(CandidateNormal.Dot(CandidateAxis)) < 1.0 - ScalarCriteria::GeometricTolerance)
                return std::nullopt;
            if (FirstRoot)
            {
                RootCentre = CandidateCentre; Axis = CandidateAxis; RootRadius = CandidateRadius; FirstRoot = false;
            }
            else if (CandidateCentre.Distance(RootCentre) > ScalarCriteria::GeometricTolerance * std::max(1.0, CandidateRadius) ||
                     CandidateAxis.Dot(Axis) < 1.0 - ScalarCriteria::GeometricTolerance ||
                     std::fabs(CandidateRadius - RootRadius) > ScalarCriteria::GeometricTolerance * std::max(1.0, RootRadius)) return std::nullopt;
            ConeFacePairs.push_back(Pair);
        }
        double RootSpan = 0.0;
        if (FirstRoot || ConeFacePairs.size() != SegmentCount ||
            !CircularChain(Body, RootEdges, RootCentre, Axis, RootRadius, false, &RootSpan) ||
            std::fabs(RootSpan) > ScalarCriteria::Pi + ScalarCriteria::AngularTolerance ||
            (SemicircleTopology && !ScalarCriteria::WithinAngularTolerance(std::fabs(RootSpan), ScalarCriteria::Pi)) ||
            (!SemicircleTopology && ScalarCriteria::WithinAngularTolerance(std::fabs(RootSpan), ScalarCriteria::Pi))) return std::nullopt;

        Vec3 Base, Top; double BaseRadius = 0.0, TopRadius = 0.0, LowerHeight = 0.0, UpperHeight = 0.0; bool FirstSupport = true;
        for (const auto& Pair : ConeFacePairs)
        {
            Vec3 LowerBase, UpperBase; double LowerBaseRadius = 0.0, UpperBaseRadius = 0.0;
            bool HasLower = false, HasUpper = false;
            for (int ConeFace : { Pair.first, Pair.second })
            {
                Vec3 Start, End; double StartRadius = 0.0, EndRadius = 0.0;
                if (!ConeEndCentres(Body.Faces[ConeFace].Surface, Start, End, StartRadius, EndRadius)) return std::nullopt;
                const double Epsilon = ScalarCriteria::GeometricTolerance * std::max({ 1.0, RootRadius, Start.Distance(End) });
                Vec3 Far; double FarRadius = 0.0;
                if (Start.Distance(RootCentre) <= Epsilon) { Far = End; FarRadius = EndRadius; }
                else if (End.Distance(RootCentre) <= Epsilon) { Far = Start; FarRadius = StartRadius; }
                else return std::nullopt;
                const double Along = (Far - RootCentre).Dot(Axis);
                if (std::fabs(Along) <= Epsilon || FarRadius <= Tol || std::fabs(FarRadius - RootRadius) <= Epsilon) return std::nullopt;
                if (Along < 0.0)
                {
                    if (HasLower) return std::nullopt;
                    LowerBase = Far; LowerBaseRadius = FarRadius; HasLower = true;
                }
                else
                {
                    if (HasUpper) return std::nullopt;
                    UpperBase = Far; UpperBaseRadius = FarRadius; HasUpper = true;
                }
            }
            if (!HasLower || !HasUpper) return std::nullopt;
            const double CandidateLowerHeight = RootCentre.Distance(LowerBase);
            const double CandidateUpperHeight = RootCentre.Distance(UpperBase);
            if ((RootCentre - LowerBase).Normalised().Dot(Axis) < 1.0 - ScalarCriteria::GeometricTolerance ||
                (UpperBase - RootCentre).Normalised().Dot(Axis) < 1.0 - ScalarCriteria::GeometricTolerance ||
                CandidateLowerHeight <= Tol || CandidateUpperHeight <= Tol) return std::nullopt;
            if (FirstSupport)
            {
                Base = LowerBase; Top = UpperBase; BaseRadius = LowerBaseRadius; TopRadius = UpperBaseRadius;
                LowerHeight = CandidateLowerHeight; UpperHeight = CandidateUpperHeight; FirstSupport = false;
            }
            else if (LowerBase.Distance(Base) > ScalarCriteria::GeometricTolerance * std::max(1.0, LowerHeight) ||
                     UpperBase.Distance(Top) > ScalarCriteria::GeometricTolerance * std::max(1.0, UpperHeight) ||
                     std::fabs(LowerBaseRadius - BaseRadius) > ScalarCriteria::GeometricTolerance * std::max(1.0, BaseRadius) ||
                     std::fabs(UpperBaseRadius - TopRadius) > ScalarCriteria::GeometricTolerance * std::max(1.0, TopRadius) ||
                     std::fabs(CandidateLowerHeight - LowerHeight) > ScalarCriteria::GeometricTolerance * std::max(1.0, LowerHeight) ||
                     std::fabs(CandidateUpperHeight - UpperHeight) > ScalarCriteria::GeometricTolerance * std::max(1.0, UpperHeight)) return std::nullopt;
        }
        if (FirstSupport) return std::nullopt;

        int BottomPatches = 0, TopPatches = 0; std::vector<Vec3> RadialNormals;
        const double CapEpsilon = ScalarCriteria::GeometricTolerance * std::max({ 1.0, BaseRadius, TopRadius, LowerHeight, UpperHeight });
        for (size_t Face = 0; Face < Body.Faces.size(); ++Face)
        {
            const int FaceIndex = static_cast<int>(Face);
            bool RootSupport = false;
            for (const auto& Pair : ConeFacePairs)
                RootSupport = RootSupport || Pair.first == FaceIndex || Pair.second == FaceIndex;
            if (RootSupport) continue;
            Vec3 Normal;
            if (!PlanarNormal(Body, FaceIndex, Normal)) return std::nullopt;
            const NurbsSurface& Surface = Body.Faces[FaceIndex].Surface;
            const Vec3 Point = Surface.Sample(0.5 * (Surface.DomainStartU() + Surface.DomainEndU()),
                                              0.5 * (Surface.DomainStartV() + Surface.DomainEndV()));
            const double Alignment = std::fabs(Normal.Dot(Axis));
            if (Alignment > 1.0 - ScalarCriteria::GeometricTolerance)
            {
                if (std::fabs((Point - Base).Dot(Axis)) <= CapEpsilon) ++BottomPatches;
                else if (std::fabs((Point - Top).Dot(Axis)) <= CapEpsilon) ++TopPatches;
                else return std::nullopt;
            }
            else if (Alignment < ScalarCriteria::GeometricTolerance) RadialNormals.push_back(Normal);
            else return std::nullopt;
        }
        const size_t ExpectedRadial = SemicircleTopology ? 1u : 2u;
        if (BottomPatches != static_cast<int>(SegmentCount) || TopPatches != static_cast<int>(SegmentCount) ||
            RadialNormals.size() != ExpectedRadial) return std::nullopt;
        Vec3 RadialStart; double SweepAngle = 0.0;
        if (!OpenChainSweep(Body, RootEdges, RootCentre, Axis, RadialStart, SweepAngle) ||
            !ScalarCriteria::WithinAngularTolerance(std::fabs(SweepAngle), RootSpan)) return std::nullopt;
        return ConeConePartialRoot{ Base, Axis, RadialStart, BaseRadius, RootRadius, TopRadius,
                                    LowerHeight, UpperHeight, SweepAngle };
    }

    Deliver<BrepBody> ChamferConeConePartialBossRoot(const ConeConePartialRoot& Root, double SetBack) noexcept
    {
        if (SetBack <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "set-back is zero or negative");
        if (std::fabs(Root.SweepAngle) >= ScalarCriteria::TwoPi - ScalarCriteria::AngularTolerance)
            return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "partial cone-cone chamfer requires an open curved root");
        const double LowerSlant = std::hypot(Root.LowerHeight, Root.BaseRadius - Root.RootRadius);
        const double UpperSlant = std::hypot(Root.UpperHeight, Root.TopRadius - Root.RootRadius);
        if (SetBack >= LowerSlant - Tol || SetBack >= UpperSlant - Tol)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "set-back consumes a conical support");
        const double LowerAxial = SetBack * Root.LowerHeight / LowerSlant;
        const double UpperAxial = SetBack * Root.UpperHeight / UpperSlant;
        const double LowerContact = Root.RootRadius + (Root.BaseRadius - Root.RootRadius) * LowerAxial / Root.LowerHeight;
        const double UpperContact = Root.RootRadius + (Root.TopRadius - Root.RootRadius) * UpperAxial / Root.UpperHeight;
        if (LowerAxial <= Tol || UpperAxial <= Tol || LowerAxial >= Root.LowerHeight - Tol ||
            UpperAxial >= Root.UpperHeight - Tol || LowerContact <= Tol || UpperContact <= Tol)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "cone-cone contact is degenerate");
        const Vec3 RootCentre = Root.Base + Root.Axis * Root.LowerHeight;
        const Vec3 Top = RootCentre + Root.Axis * Root.UpperHeight;
        const Vec3 Radial = Root.RadialStart.Normalised();
        if (Radial.Length() <= Tol || std::fabs(Radial.Dot(Root.Axis)) > ScalarCriteria::GeometricTolerance)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial cone-cone radial frame is degenerate");
        auto RevolveLine = [&](Vec3 Start, Vec3 End) -> Deliver<NurbsSurface>
        {
            Deliver<NurbsCurve> Line = NurbsCurve::Line(Start, End);
            return Line ? NurbsSurface::Revolution(Line.Payload, Root.Base, Root.Axis, Root.SweepAngle)
                        : Deliver<NurbsSurface>::Reject(Line.Denial.Reason, Line.Denial.Detail);
        };
        Deliver<NurbsSurface> Lower = RevolveLine(Root.Base + Radial * Root.BaseRadius,
                                                   RootCentre - Root.Axis * LowerAxial + Radial * LowerContact);
        Deliver<NurbsSurface> Chamfer = RevolveLine(RootCentre - Root.Axis * LowerAxial + Radial * LowerContact,
                                                     RootCentre + Root.Axis * UpperAxial + Radial * UpperContact);
        Deliver<NurbsSurface> Upper = RevolveLine(RootCentre + Root.Axis * UpperAxial + Radial * UpperContact,
                                                   Top + Radial * Root.TopRadius);
        Deliver<NurbsSurface> Bottom = RevolveLine(Root.Base, Root.Base + Radial * Root.BaseRadius);
        Deliver<NurbsSurface> TopCap = RevolveLine(Top + Radial * Root.TopRadius, Top);
        if (!Lower || !Chamfer || !Upper || !Bottom || !TopCap)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial cone-cone support is degenerate");
        Lower.Payload.Classification = SurfaceClassification::Cone;
        Lower.Payload.Origin = Root.Base; Lower.Payload.Axis = Root.Axis;
        Lower.Payload.RadiusMajor = Root.BaseRadius; Lower.Payload.RadiusMinor = LowerContact;
        Chamfer.Payload.Classification = SurfaceClassification::Cone;
        Chamfer.Payload.Origin = Root.Base; Chamfer.Payload.Axis = Root.Axis;
        Chamfer.Payload.RadiusMajor = LowerContact; Chamfer.Payload.RadiusMinor = UpperContact;
        Upper.Payload.Classification = SurfaceClassification::Cone;
        Upper.Payload.Origin = Root.Base; Upper.Payload.Axis = Root.Axis;
        Upper.Payload.RadiusMajor = UpperContact; Upper.Payload.RadiusMinor = Root.TopRadius;
        Deliver<BrepBody> Result = BrepBody::Sew({ Lower.Payload, Chamfer.Payload, Upper.Payload,
                                                   Bottom.Payload, TopCap.Payload });
        if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, Result.Denial.Detail);
        const bool HalfTurn = ScalarCriteria::WithinAngularTolerance(std::fabs(Root.SweepAngle), ScalarCriteria::Pi);
        if (!HalfTurn && !CapRadialSector(Result.Payload, Root.Base, Top, Radial, Root.SweepAngle,
                                          std::max({ Root.BaseRadius, Root.RootRadius, Root.TopRadius })))
            return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "partial cone-cone radial caps could not heal");
        const BodyReport Report = Result.Payload.Validate();
        const bool ExactTopology = HalfTurn
            ? Report.Hulls == 1 && Report.Genus == 0 && Result.Payload.Vertices.size() == 10 &&
              Result.Payload.Edges.size() == 14 && Result.Payload.Coedges.size() == 28 &&
              Result.Payload.Loops.size() == 6 && Result.Payload.Faces.size() == 6
            : Report.Hulls == 1 && Report.Genus == 0 && Result.Payload.Vertices.size() == 10 &&
              Result.Payload.Edges.size() == 15 && Result.Payload.Coedges.size() == 30 &&
              Result.Payload.Loops.size() == 7 && Result.Payload.Faces.size() == 7;
        if (!Report.Solid() || Report.OpenEdges != 0 || Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || !ExactTopology)
            return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "partial cone-cone chamfer did not reach exact topology");
        return Result;
    }

    Deliver<BrepBody> FilletConeConePartialBossRoot(const ConeConePartialRoot& Root, double Radius) noexcept
    {
        if (Radius <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "radius is zero or negative");
        if (std::fabs(Root.SweepAngle) >= ScalarCriteria::TwoPi - ScalarCriteria::AngularTolerance)
            return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "partial cone-cone fillet requires an open root");
        const double LowerAngle = std::atan2(Root.BaseRadius - Root.RootRadius, Root.LowerHeight);
        const double UpperAngle = std::atan2(Root.RootRadius - Root.TopRadius, Root.UpperHeight);
        if (LowerAngle <= ScalarCriteria::AngularTolerance || UpperAngle <= LowerAngle + ScalarCriteria::AngularTolerance)
            return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "partial cone-cone fillet requires an increasing narrowing slope");
        const double Det = std::sin(UpperAngle - LowerAngle);
        const double LowerCos = std::cos(LowerAngle), LowerSin = std::sin(LowerAngle);
        const double UpperCos = std::cos(UpperAngle), UpperSin = std::sin(UpperAngle);
        const double CentreR = Root.RootRadius + Radius * (LowerSin - UpperSin) / Det;
        const double CentreZ = Radius * (UpperCos - LowerCos) / Det;
        const double LowerContactZ = CentreZ + Radius * LowerSin;
        const double UpperContactZ = CentreZ + Radius * UpperSin;
        const double LowerContactR = CentreR + Radius * LowerCos;
        const double UpperContactR = CentreR + Radius * UpperCos;
        if (-LowerContactZ >= Root.LowerHeight - Tol || UpperContactZ >= Root.UpperHeight - Tol ||
            LowerContactR <= Tol || UpperContactR <= Tol || CentreR <= Tol)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial cone-cone fillet consumes its supports");
        const Vec3 RootCentre = Root.Base + Root.Axis * Root.LowerHeight;
        const Vec3 Top = RootCentre + Root.Axis * Root.UpperHeight;
        const Vec3 Radial = Root.RadialStart.Normalised();
        if (Radial.Length() <= Tol || std::fabs(Radial.Dot(Root.Axis)) > ScalarCriteria::GeometricTolerance)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial cone-cone fillet radial frame is degenerate");
        auto RevolveLine = [&](Vec3 Start, Vec3 End) -> Deliver<NurbsSurface>
        {
            Deliver<NurbsCurve> Line = NurbsCurve::Line(Start, End);
            return Line ? NurbsSurface::Revolution(Line.Payload, Root.Base, Root.Axis, Root.SweepAngle)
                        : Deliver<NurbsSurface>::Reject(Line.Denial.Reason, Line.Denial.Detail);
        };
        Deliver<NurbsSurface> Lower = RevolveLine(Root.Base + Radial * Root.BaseRadius,
                                                   RootCentre + Root.Axis * LowerContactZ + Radial * LowerContactR);
        Deliver<NurbsSurface> Upper = RevolveLine(RootCentre + Root.Axis * UpperContactZ + Radial * UpperContactR,
                                                   Top + Radial * Root.TopRadius);
        const Vec3 MeridianCentre = RootCentre + Root.Axis * CentreZ + Radial * CentreR;
        auto OnMeridian = [&](double Angle) noexcept
        {
            return MeridianCentre + (Radial * std::cos(Angle) + Root.Axis * std::sin(Angle)) * Radius;
        };
        Deliver<NurbsCurve> Meridian = NurbsCurve::ArcThreePoints(
            OnMeridian(LowerAngle), OnMeridian(0.5 * (LowerAngle + UpperAngle)), OnMeridian(UpperAngle));
        Deliver<NurbsSurface> Roll = Meridian
            ? NurbsSurface::Revolution(Meridian.Payload, Root.Base, Root.Axis, Root.SweepAngle)
            : Deliver<NurbsSurface>::Reject(Meridian.Denial.Reason, Meridian.Denial.Detail);
        Deliver<NurbsSurface> Bottom = RevolveLine(Root.Base, Root.Base + Radial * Root.BaseRadius);
        Deliver<NurbsSurface> TopCap = RevolveLine(Top + Radial * Root.TopRadius, Top);
        if (!Lower || !Roll || !Upper || !Bottom || !TopCap)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial cone-cone fillet support is degenerate");
        Lower.Payload.Classification = SurfaceClassification::Cone;
        Lower.Payload.Origin = Root.Base; Lower.Payload.Axis = Root.Axis;
        Lower.Payload.RadiusMajor = Root.BaseRadius; Lower.Payload.RadiusMinor = LowerContactR;
        Roll.Payload.Classification = SurfaceClassification::Torus;
        Roll.Payload.Origin = RootCentre + Root.Axis * CentreZ; Roll.Payload.Axis = Root.Axis;
        Roll.Payload.RadiusMajor = CentreR; Roll.Payload.RadiusMinor = Radius;
        Upper.Payload.Classification = SurfaceClassification::Cone;
        Upper.Payload.Origin = RootCentre + Root.Axis * UpperContactZ; Upper.Payload.Axis = Root.Axis;
        Upper.Payload.RadiusMajor = UpperContactR; Upper.Payload.RadiusMinor = Root.TopRadius;
        Deliver<BrepBody> Result = BrepBody::Sew({ Lower.Payload, Roll.Payload, Upper.Payload,
                                                   Bottom.Payload, TopCap.Payload });
        if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, Result.Denial.Detail);
        const bool HalfTurn = ScalarCriteria::WithinAngularTolerance(std::fabs(Root.SweepAngle), ScalarCriteria::Pi);
        if (!HalfTurn && !CapRadialSector(Result.Payload, Root.Base, Top, Radial, Root.SweepAngle,
                                          std::max({ Root.BaseRadius, Root.RootRadius, Root.TopRadius })))
            return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "partial cone-cone fillet radial caps could not heal");
        const ConeConeRoot Complete{ Root.Base, Root.Axis, Root.BaseRadius, Root.RootRadius, Root.TopRadius,
                                     Root.LowerHeight, Root.UpperHeight };
        const double Fraction = std::fabs(Root.SweepAngle) / ScalarCriteria::TwoPi;
        const double TargetVolume = Fraction * (ConeConeSourceVolume(Complete) - ConeConeFilletRemoval(Complete, Radius));
        const BodyReport Report = Result.Payload.Validate();
        const bool ExactTopology = HalfTurn
            ? Report.Hulls == 1 && Report.Genus == 0 && Result.Payload.Vertices.size() == 10 &&
              Result.Payload.Edges.size() == 14 && Result.Payload.Coedges.size() == 28 &&
              Result.Payload.Loops.size() == 6 && Result.Payload.Faces.size() == 6
            : Report.Hulls == 1 && Report.Genus == 0 && Result.Payload.Vertices.size() == 10 &&
              Result.Payload.Edges.size() == 15 && Result.Payload.Coedges.size() == 30 &&
              Result.Payload.Loops.size() == 7 && Result.Payload.Faces.size() == 7;
        if (!Report.Solid() || Report.OpenEdges != 0 || Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 ||
            !ExactTopology || !ScalarCriteria::WithinVolumeTolerance(Report.Volume, TargetVolume))
            return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "partial cone-cone fillet did not reach exact analytic topology");
        return Result;
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

    Deliver<BrepBody> ChamferPlaneConeBossRoot(const PlaneConeRoot& Root, double SetBack) noexcept
    {
        if (SetBack <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "set-back is zero or negative");
        if (Root.FootRadius + SetBack >= Root.OuterRadius - Tol || SetBack >= Root.BossHeight - Tol)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "plane-cone root setback consumes the shoulder or boss");
        const double Slant = std::hypot(Root.BossHeight, Root.TopRadius - Root.FootRadius);
        if (SetBack >= Slant - Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "plane-cone root setback consumes the conical support");
        const double Axial = SetBack * Root.BossHeight / Slant;
        const double ContactRadius = Root.FootRadius + (Root.TopRadius - Root.FootRadius) * Axial / Root.BossHeight;
        if (Axial <= Tol || Axial >= Root.BossHeight - Tol || ContactRadius <= Tol)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "plane-cone root contact is degenerate");
        const Vec3 ShoulderCentre = Root.Base + Root.Axis * Root.ShoulderHeight;
        const Vec3 Radial = Workplane::FromNormal(Root.Base, Root.Axis).AxisX;
        Deliver<NurbsSurface> Outer = NurbsSurface::Cylinder(Root.Base, Root.Axis, Root.OuterRadius, Root.ShoulderHeight);
        Deliver<NurbsCurve> ShoulderLine = NurbsCurve::Line(ShoulderCentre + Radial * Root.OuterRadius,
                                                            ShoulderCentre + Radial * (Root.FootRadius + SetBack));
        Deliver<NurbsSurface> Shoulder = ShoulderLine
            ? NurbsSurface::Revolution(ShoulderLine.Payload, Root.Base, Root.Axis, ScalarCriteria::TwoPi)
            : Deliver<NurbsSurface>::Reject(ShoulderLine.Denial.Reason, ShoulderLine.Denial.Detail);
        Deliver<NurbsSurface> Bevel = NurbsSurface::Cone(ShoulderCentre, Root.Axis, Root.FootRadius + SetBack, ContactRadius, Axial);
        Deliver<NurbsSurface> Boss = NurbsSurface::Cone(ShoulderCentre + Root.Axis * Axial, Root.Axis,
                                                        ContactRadius, Root.TopRadius, Root.BossHeight - Axial);
        if (!Outer || !Shoulder || !Bevel || !Boss)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "plane-cone root chamfer support is degenerate");
        Deliver<BrepBody> Result = BrepBody::Sew({ Outer.Payload, Shoulder.Payload, Bevel.Payload, Boss.Payload });
        if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, Result.Denial.Detail);
        const BodyReport Report = Result.Payload.Validate();
        if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
            Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0)
            return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "plane-cone root chamfer did not heal to one solid");
        return Result;
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

    std::optional<ConeCap> NativeConeCapEdge(const BrepBody& Body, int Edge) noexcept
    {
        if (Edge < 0 || Edge >= static_cast<int>(Body.Edges.size()) || !Body.Edges[Edge].Closed() ||
            Body.Edges[Edge].Curve.Classification != CurveClassification::Circle || Body.Edges[Edge].Coedges.size() != 2) return std::nullopt;
        for (int Coedge : Body.Edges[Edge].Coedges)
        {
            const int Face = Body.Coedges[Coedge].Face;
            if (std::optional<ConeCap> Cap = NativeConeCapFace(Body, Face)) return Cap;
        }
        return std::nullopt;
    }

    Deliver<BrepBody> ChamferConeCap(const ConeCap& Cap, double SetBack) noexcept
    {
        const ConeSide& Shape = Cap.Shape;
        const double DeltaRadius = Shape.RadiusTop - Shape.RadiusFoot;
        const double Slant = std::hypot(Shape.Height, DeltaRadius);
        if (SetBack <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "set-back is zero or negative");
        if (SetBack >= Shape.Height || SetBack >= std::min(Shape.RadiusFoot, Shape.RadiusTop) - Tol || SetBack >= Slant - Tol)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "conical cap set-back collapses the cone");

        const double JoinHeight = SetBack * Shape.Height / Slant;
        const double JoinRadius = Cap.Upper
            ? Shape.RadiusTop - SetBack * DeltaRadius / Slant
            : Shape.RadiusFoot + SetBack * DeltaRadius / Slant;
        const double CapRadius = (Cap.Upper ? Shape.RadiusTop : Shape.RadiusFoot) - SetBack;
        if (JoinHeight <= Tol || JoinHeight >= Shape.Height - Tol || JoinRadius <= Tol || CapRadius <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "conical cap set-back is infeasible");

        Deliver<NurbsSurface> Retained = Cap.Upper
            ? NurbsSurface::Cone(Shape.Base, Shape.Axis, Shape.RadiusFoot, JoinRadius, JoinHeight)
            : NurbsSurface::Cone(Shape.Base + Shape.Axis * JoinHeight, Shape.Axis, JoinRadius, Shape.RadiusTop, Shape.Height - JoinHeight);
        Deliver<NurbsSurface> Bevel = Cap.Upper
            ? NurbsSurface::Cone(Shape.Base + Shape.Axis * JoinHeight, Shape.Axis, JoinRadius, CapRadius, Shape.Height - JoinHeight)
            : NurbsSurface::Cone(Shape.Base, Shape.Axis, CapRadius, JoinRadius, JoinHeight);
        if (!Retained || !Bevel) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "conical cap chamfer support is degenerate");
        Deliver<BrepBody> Result = BrepBody::Sew({ std::move(Retained.Payload), std::move(Bevel.Payload) });
        if (!Result || !Result.Payload.Validate().Solid()) return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "conical cap chamfer did not sew into a solid");
        return Result;
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
    // Exact planar-prism network route. A simple extrusion is reconstructed from its complete cap perimeter, replacing
    //    every selected parallel edge corner in one 2D pass. Unlike sequential cutter/Boolean attempts this is stable for
    //    multiple reflex corners and for concave U/L profiles; any non-prismatic or ambiguous network returns nullopt so
    //    the normal convex/refusal routes remain authoritative.
    std::optional<BrepBody> PrismaticChamferNetwork(const BrepBody& Body, const std::vector<EdgeCornerFrame>& Frames, double Amount) noexcept
    {
        if (Frames.size() < 2) return std::nullopt;
        const EdgeCornerFrame& Seed = Frames.front();
        if (Seed.Length <= Tol || Seed.Tangent.Length() <= Tol) return std::nullopt;
        const Vec3 Axis = Seed.Tangent.Normalised();
        const double CapLevel = Seed.Start.Dot(Axis);
        const double Reach = std::max(1.0, Body.Bounds().Diagonal());
        const double Epsilon = ScalarCriteria::GeometricTolerance * Reach * 10.0;
        for (const EdgeCornerFrame& F : Frames)
        {
            if (F.Length <= Tol || std::fabs(std::fabs(F.Tangent.Normalised().Dot(Axis)) - 1.0) > 1e-6) return std::nullopt;
            if (std::fabs(F.Start.Dot(Axis) - CapLevel) > Epsilon && std::fabs(F.End.Dot(Axis) - CapLevel) > Epsilon) return std::nullopt;
        }

        std::vector<std::vector<std::pair<int, int>>> Neighbours(Body.Vertices.size());
        for (size_t E = 0; E < Body.Edges.size(); ++E)
        {
            const BrepEdge& Edge = Body.Edges[E];
            if (Edge.VertexStart < 0 || Edge.VertexEnd < 0 || Edge.Closed() || Edge.Curve.Degree > 1) return std::nullopt;
            if (std::fabs(Body.Vertices[Edge.VertexStart].Point.Dot(Axis) - CapLevel) > Epsilon ||
                std::fabs(Body.Vertices[Edge.VertexEnd].Point.Dot(Axis) - CapLevel) > Epsilon) continue;
            int CapUsers = 0;
            for (int Coedge : Edge.Coedges)
            {
                Vec3 Normal;
                if (PlanarNormal(Body, Body.Coedges[Coedge].Face, Normal) && std::fabs(Normal.Dot(Axis)) > 0.999999) ++CapUsers;
            }
            if (CapUsers != 1) continue;
            Neighbours[Edge.VertexStart].push_back({ static_cast<int>(E), Edge.VertexEnd });
            Neighbours[Edge.VertexEnd].push_back({ static_cast<int>(E), Edge.VertexStart });
        }

        int Start = -1;
        for (size_t V = 0; V < Body.Vertices.size() && Start < 0; ++V)
        {
            for (const EdgeCornerFrame& F : Frames)
            {
                if (Body.Vertices[V].Point.Distance(F.Start) <= Epsilon || Body.Vertices[V].Point.Distance(F.End) <= Epsilon)
                {
                    if (std::fabs(Body.Vertices[V].Point.Dot(Axis) - CapLevel) <= Epsilon) { Start = static_cast<int>(V); break; }
                }
            }
        }
        if (Start < 0 || Neighbours[Start].size() != 2) return std::nullopt;

        std::vector<Vec3> Points;
        int Current = Start, PreviousEdge = -1;
        for (size_t Guard = 0; Guard <= Body.Vertices.size(); ++Guard)
        {
            Points.push_back(Body.Vertices[Current].Point);
            if (Neighbours[Current].size() != 2) return std::nullopt;
            const auto& Step = Neighbours[Current][Neighbours[Current][0].first == PreviousEdge ? 1 : 0];
            PreviousEdge = Step.first; Current = Step.second;
            if (Current == Start) break;
            if (Guard == Body.Vertices.size()) return std::nullopt;
        }
        if (Points.size() < 3 || Current != Start) return std::nullopt;

        std::vector<int> Selected;
        for (const EdgeCornerFrame& F : Frames)
        {
            Vec3 Anchor = std::fabs(F.Start.Dot(Axis) - CapLevel) <= Epsilon ? F.Start : F.End;
            int Found = -1;
            for (size_t I = 0; I < Points.size(); ++I)
                if (Points[I].Distance(Anchor) <= Epsilon) { Found = static_cast<int>(I); break; }
            if (Found < 0 || std::find(Selected.begin(), Selected.end(), Found) != Selected.end()) return std::nullopt;
            Selected.push_back(Found);
        }

        std::vector<Vec3> Outline;
        Outline.reserve(Points.size() + Selected.size());
        for (size_t I = 0; I < Points.size(); ++I)
        {
            const bool IsSelected = std::find(Selected.begin(), Selected.end(), static_cast<int>(I)) != Selected.end();
            if (!IsSelected) { Outline.push_back(Points[I]); continue; }
            const Vec3 Previous = Points[(I + Points.size() - 1) % Points.size()];
            const Vec3 Next = Points[(I + 1) % Points.size()];
            const double ALength = Points[I].Distance(Previous), BLength = Points[I].Distance(Next);
            if (Amount <= Tol || Amount >= ALength - Tol || Amount >= BLength - Tol) return std::nullopt;
            Outline.push_back(Points[I] + (Previous - Points[I]) * (Amount / ALength));
            Outline.push_back(Points[I] + (Next - Points[I]) * (Amount / BLength));
        }
        Deliver<NurbsCurve> Profile = NurbsCurve::Polyline(Outline, true);
        if (!Profile) return std::nullopt;
        Deliver<BrepBody> Rebuilt = BrepBody::Extrude(Profile.Payload, Axis, Seed.Length);
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
    if (std::optional<ConeCap> Cap = NativeConeCapEdge(Body, Edge)) return ChamferConeCap(*Cap, SetBack);
    if (std::optional<PlaneCylinderRoot> Root = PlaneCylinderBossRoot(Body, Edge))
        return ChamferPlaneCylinderBossRoot(*Root, SetBack);
    if (std::optional<PlaneConeRoot> Root = PlaneConeBossRoot(Body, Edge))
        return ChamferPlaneConeBossRoot(*Root, SetBack);
    if (std::optional<PlaneConeApexRoot> Root = PlaneConeApexBossRoot(Body, Edge))
        return ChamferPlaneConeApexBossRoot(*Root, SetBack);
    if (std::optional<PlaneConeApexPartialRoot> Root = PlaneConeApexPartialBossRoot(Body, Edge))
        return ChamferPlaneConeApexPartialBossRoot(*Root, SetBack);
    if (std::optional<PlaneConePartialRoot> Root = PlaneConePartialBossRoot(Body, Edge))
        return ChamferPlaneConePartialBossRoot(*Root, SetBack);
    if (std::optional<ConeCylinderPartialRoot> Root = ConeCylinderPartialBossRoot(Body, Edge))
        return ChamferConeCylinderPartialBossRoot(*Root, SetBack);
    if (std::optional<ConeConePartialRoot> Root = ConeConePartialBossRoot(Body, Edge))
        return ChamferConeConePartialBossRoot(*Root, SetBack);
    if (std::optional<CylinderConeRoot> Root = CylinderConeBossRoot(Body, Edge))
        return ChamferCylinderConeBossRoot(*Root, SetBack);
    EdgeCornerFrame F;
    if (const char* Why = CornerFrameRefusal(Body, Edge, F)) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, Why);
    if (SetBack <= Tol) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "set-back is zero or negative");
    std::string Feasibility;
    if (!ChamferSetbackFits(Body, Edge, F, SetBack, Feasibility))
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "set-back would self-intersect the next planar boundary");

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
                                           0.01, 0.05, 0.13, 0.29, 0.53, 1.0, 2.0, 3.0, 5.0, 8.0, 12.0 };
    static const double HalfWidths[] = { 0.55, 0.8, 1.2, 2.0, 3.0, 5.0, 8.0 };

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

namespace
{
    // A conservative local feasibility test for a planar setback. The displacement is measured in the adjacent face,
    // so the first non-selected vertex on either face is the earliest place at which the setback line can leave that
    // face. Refusing at that boundary is preferable to producing a self-crossing trim that a later Boolean might heal
    // differently on another tessellation.
    bool ChamferSetbackFits(const BrepBody& Body, int SelectedEdge, const EdgeCornerFrame& Frame, double SetBack, std::string& Refusal) noexcept
    {
        if (!std::isfinite(SetBack) || SetBack <= Tol)
        {
            Refusal = "set-back is zero or negative";
            return false;
        }
        const double Scale = std::max(1.0, Body.Bounds().Diagonal());
        const double Epsilon = ScalarCriteria::ScaledPositionTolerance * Scale;
        for (int Face : { Frame.FaceA, Frame.FaceB })
        {
            if (Face < 0 || Face >= static_cast<int>(Body.Faces.size()))
            {
                Refusal = "edge has an invalid adjacent face";
                return false;
            }
            const Vec3 Direction = Face == Frame.FaceA ? Frame.InA : Frame.InB;
            const Vec3 Along = Frame.Tangent;
            const auto Cross2 = [](double AX, double AY, double BX, double BY) noexcept { return AX * BY - AY * BX; };
            for (const int Loop : Body.Faces[Face].Loops)
                for (const int Coedge : Body.Loops[Loop].Coedges)
                {
                    int OtherEdge = Body.Coedges[Coedge].Edge;
                    if (OtherEdge < 0 || OtherEdge >= static_cast<int>(Body.Edges.size()) || OtherEdge == SelectedEdge) continue;
                    const BrepEdge& Boundary = Body.Edges[OtherEdge];
                    // Intersect each face-boundary segment with the ray that carries the setback from either selected
                    // endpoint. This catches an acute/oblique polygon where the ray reaches a side before it reaches
                    // that side's endpoint; a vertex-distance-only test would miss exactly that self-crossing.
                    const std::vector<Vec3> Samples = Body.EdgePolyline(OtherEdge, 1e-5);
                    for (const Vec3& Origin : { Frame.Start, Frame.End })
                        for (size_t I = 0; I + 1 < Samples.size(); ++I)
                        {
                            Vec3 Q = Samples[I] - Origin, S = Samples[I + 1] - Samples[I];
                            double QX = Q.Dot(Direction), QY = Q.Dot(Along);
                            double SX = S.Dot(Direction), SY = S.Dot(Along);
                            double Denominator = Cross2(1.0, 0.0, SX, SY);
                            if (std::fabs(Denominator) <= ScalarCriteria::GeometricTolerance) continue;
                            double T = Cross2(QX, QY, SX, SY) / Denominator;
                            double U = Cross2(QX, QY, 1.0, 0.0) / Denominator;
                            if (T > Epsilon && U >= -Epsilon && U <= 1.0 + Epsilon && T <= SetBack + Epsilon)
                            {
                                Refusal = "set-back would self-intersect the next planar boundary";
                                return false;
                            }
                        }
                    for (int Vertex : { Boundary.VertexStart, Boundary.VertexEnd })
                    {
                        if (Vertex < 0 || Vertex >= static_cast<int>(Body.Vertices.size())) continue;
                        Vec3 P = Body.Vertices[Vertex].Point;
                        if (P.Distance(Frame.Start) <= Epsilon || P.Distance(Frame.End) <= Epsilon) continue;
                        if (P.Distance(Frame.Start) <= SetBack + Epsilon || P.Distance(Frame.End) <= SetBack + Epsilon)
                        {
                            Refusal = "set-back would self-intersect the next planar boundary";
                            return false;
                        }
                    }
                }
        }
        return true;
    }

    Deliver<BrepBody> ChamferToolUnion(const std::vector<EdgeCornerFrame>& Frames,
                                       double SetBack, double WidthFactor) noexcept
    {
        Deliver<BrepBody> Accumulated = Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no chamfer cutter");
        // Extend each prism by one setback at both ends. Adjacent members then overlap across their common vertex,
        // which lets the union create one mitred removal instead of a zero-measure point contact.
        const double Margin = 1.0;
        for (const EdgeCornerFrame& Frame : Frames)
        {
            Vec3 A = Frame.Start + Frame.InA * SetBack, B = Frame.Start + Frame.InB * SetBack;
            double Offset = ((A + B) * 0.5 - Frame.Start).Dot(Frame.Bisector);
            Deliver<BrepBody> Cutter = CornerCutter(Frame, Offset, SetBack * WidthFactor,
                                                     Margin, SetBack * (WidthFactor + 1.0));
            if (!Cutter) return Cutter;
            if (!Accumulated)
            {
                Accumulated = std::move(Cutter);
                continue;
            }
            Deliver<BrepBody> Joined = IntersectionSolver::Combine(Accumulated.Payload, Cutter.Payload, BodyOperation::Union);
            if (!Joined) return Joined;
            Accumulated = std::move(Joined);
        }
        return Accumulated;
    }

    struct ChamferPlane
    {
        Vec3 Normal{};
        double Offset = 0.0;
    };

    bool SolveThreePlanes(const ChamferPlane& A, const ChamferPlane& B, const ChamferPlane& C, Vec3& Point) noexcept
    {
        double Determinant = A.Normal.Dot(B.Normal.Cross(C.Normal));
        if (std::fabs(Determinant) <= ScalarCriteria::GeometricTolerance) return false;
        Point = (B.Normal.Cross(C.Normal) * A.Offset + C.Normal.Cross(A.Normal) * B.Offset +
                 A.Normal.Cross(B.Normal) * C.Offset) / Determinant;
        return Point.Length() < ScalarCriteria::Infinity;
    }

    [[nodiscard]] bool IsNonPlanarClosedEdgeLoop(const BrepBody& Body, const std::vector<int>& Edges) noexcept
    {
        if (Edges.size() < 4) return false;
        std::vector<int> Vertices;
        for (int Edge : Edges)
        {
            if (Edge < 0 || Edge >= static_cast<int>(Body.Edges.size())) return false;
            const BrepEdge& E = Body.Edges[Edge];
            if (E.VertexStart < 0 || E.VertexEnd < 0) return false;
            Vertices.push_back(E.VertexStart); Vertices.push_back(E.VertexEnd);
        }
        std::sort(Vertices.begin(), Vertices.end());
        Vertices.erase(std::unique(Vertices.begin(), Vertices.end()), Vertices.end());
        if (Vertices.size() != Edges.size()) return false;
        for (int Vertex : Vertices)
        {
            int Degree = 0;
            for (int Edge : Edges)
                Degree += Body.Edges[Edge].VertexStart == Vertex || Body.Edges[Edge].VertexEnd == Vertex;
            if (Degree != 2) return false;
        }
        std::vector<int> Component{ Vertices.front() };
        for (size_t Cursor = 0; Cursor < Component.size(); ++Cursor)
        {
            const int Vertex = Component[Cursor];
            for (int Edge : Edges)
            {
                const BrepEdge& E = Body.Edges[Edge];
                if (E.VertexStart != Vertex && E.VertexEnd != Vertex) continue;
                const int Other = E.VertexStart == Vertex ? E.VertexEnd : E.VertexStart;
                if (std::find(Component.begin(), Component.end(), Other) == Component.end()) Component.push_back(Other);
            }
        }
        if (Component.size() != Vertices.size()) return false;

        const double Scale = std::max(1.0, Body.Bounds().Diagonal());
        const double Epsilon = ScalarCriteria::ScaledPositionTolerance * Scale * 10.0;
        Vec3 A = Body.Vertices[Vertices[0]].Point, B{}, C{};
        bool FoundB = false, FoundC = false;
        for (size_t I = 1; I < Vertices.size() && !FoundB; ++I)
            if (Body.Vertices[Vertices[I]].Point.Distance(A) > Epsilon) { B = Body.Vertices[Vertices[I]].Point; FoundB = true; }
        if (!FoundB) return false;
        for (size_t I = 1; I < Vertices.size() && !FoundC; ++I)
            if ((Body.Vertices[Vertices[I]].Point - A).Cross(B - A).Length() > Epsilon * Epsilon) { C = Body.Vertices[Vertices[I]].Point; FoundC = true; }
        if (!FoundC) return false;
        const Vec3 Normal = (B - A).Cross(C - A).Normalised();
        for (int Vertex : Vertices)
            if (std::fabs((Body.Vertices[Vertex].Point - A).Dot(Normal)) > Epsilon) return true;
        return false;
    }

    [[nodiscard]] bool IsNonPlanarEdgeSelection(const BrepBody& Body, const std::vector<int>& Edges) noexcept
    {
        std::vector<int> Vertices;
        for (int Edge : Edges)
        {
            if (Edge < 0 || Edge >= static_cast<int>(Body.Edges.size())) return false;
            Vertices.push_back(Body.Edges[Edge].VertexStart);
            Vertices.push_back(Body.Edges[Edge].VertexEnd);
        }
        std::sort(Vertices.begin(), Vertices.end());
        Vertices.erase(std::unique(Vertices.begin(), Vertices.end()), Vertices.end());
        if (Vertices.size() < 4) return false;
        const double Scale = std::max(1.0, Body.Bounds().Diagonal());
        const double Epsilon = ScalarCriteria::ScaledPositionTolerance * Scale * 10.0;
        const Vec3 A = Body.Vertices[Vertices[0]].Point;
        int BIndex = -1, CIndex = -1;
        for (size_t I = 1; I < Vertices.size() && BIndex < 0; ++I)
            if (Body.Vertices[Vertices[I]].Point.Distance(A) > Epsilon) BIndex = static_cast<int>(I);
        if (BIndex < 0) return false;
        for (size_t I = 1; I < Vertices.size() && CIndex < 0; ++I)
            if ((Body.Vertices[Vertices[I]].Point - A).Cross(Body.Vertices[Vertices[BIndex]].Point - A).Length() > Epsilon * Epsilon) CIndex = static_cast<int>(I);
        if (CIndex < 0) return false;
        const Vec3 Normal = (Body.Vertices[Vertices[BIndex]].Point - A).Cross(Body.Vertices[Vertices[CIndex]].Point - A).Normalised();
        for (int Vertex : Vertices)
            if (std::fabs((Body.Vertices[Vertex].Point - A).Dot(Normal)) > Epsilon) return true;
        return false;
    }

    // Reconstruct a convex planar polyhedron from its supporting planes. This is the deterministic topology route for
    // connected edge loops: all original faces and all new chamfer planes are solved together, so shared vertices become
    // real mitres instead of coincident Boolean end caps. The convexity guard is deliberate; concave planar networks still
    // use the common-tool/fallback path and refuse if that path cannot prove a closed result.
    Deliver<BrepBody> ConvexPlanarChamfer(const BrepBody& Body, const std::vector<EdgeCornerFrame>& Frames,
                                           double SetBack) noexcept
    {
        std::vector<ChamferPlane> Planes;
        Planes.reserve(Body.Faces.size() + Frames.size());
        const double Scale = std::max(1.0, Body.Bounds().Diagonal());
        const double Epsilon = ScalarCriteria::ScaledPositionTolerance * Scale * 10.0;
        for (size_t Face = 0; Face < Body.Faces.size(); ++Face)
        {
            Vec3 Normal;
            if (!PlanarNormal(Body, static_cast<int>(Face), Normal))
                return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "convex planar chamfer route requires planar faces");
            Vec3 Sample = Body.Faces[Face].Surface.Sample(
                0.5 * (Body.Faces[Face].Surface.DomainStartU() + Body.Faces[Face].Surface.DomainEndU()),
                0.5 * (Body.Faces[Face].Surface.DomainStartV() + Body.Faces[Face].Surface.DomainEndV()));
            Planes.push_back({ Normal, Normal.Dot(Sample) });
        }
        for (const EdgeCornerFrame& Frame : Frames)
        {
            Vec3 A = Frame.Start + Frame.InA * SetBack;
            Vec3 B = Frame.Start + Frame.InB * SetBack;
            Vec3 Normal = Frame.Bisector.Normalised();
            Planes.push_back({ Normal, Normal.Dot((A + B) * 0.5) });
        }

        // Every source vertex must be inside every source plane. This rejects concave bodies and wrong normal senses
        // before any new topology is assembled.
        for (const BrepVertex& Vertex : Body.Vertices)
            for (size_t Plane = 0; Plane < Body.Faces.size(); ++Plane)
                if (Planes[Plane].Normal.Dot(Vertex.Point) > Planes[Plane].Offset + Epsilon)
                    return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "planar chamfer route requires a convex source solid");

        std::vector<Vec3> Points;
        auto AddPoint = [&](Vec3 Point)
        {
            for (Vec3 Existing : Points) if (Existing.Distance(Point) <= ScalarCriteria::MergeTolerance * 2.0) return;
            Points.push_back(Point);
        };
        for (size_t A = 0; A < Planes.size(); ++A)
            for (size_t B = A + 1; B < Planes.size(); ++B)
                for (size_t C = B + 1; C < Planes.size(); ++C)
                {
                    Vec3 Point;
                    if (!SolveThreePlanes(Planes[A], Planes[B], Planes[C], Point)) continue;
                    bool Inside = true;
                    for (const ChamferPlane& Plane : Planes)
                        if (Plane.Normal.Dot(Point) > Plane.Offset + Epsilon) { Inside = false; break; }
                    if (Inside) AddPoint(Point);
                }
        if (Points.size() < 4)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "chamfer planes do not enclose a solid");

        struct Polygon { int Plane = -1; std::vector<int> Vertices; };
        std::vector<Polygon> Polygons;
        for (size_t PlaneIndex = 0; PlaneIndex < Planes.size(); ++PlaneIndex)
        {
            const ChamferPlane& Plane = Planes[PlaneIndex];
            std::vector<int> OnPlane;
            for (size_t Point = 0; Point < Points.size(); ++Point)
                if (std::fabs(Plane.Normal.Dot(Points[Point]) - Plane.Offset) <= Epsilon * 4.0) OnPlane.push_back(static_cast<int>(Point));
            if (OnPlane.size() < 3) continue;                                            // a redundant chamfer plane
            Vec3 Centre{};
            for (int Point : OnPlane) Centre = Centre + Points[Point];
            Centre = Centre * (1.0 / static_cast<double>(OnPlane.size()));
            Vec3 U = (Points[OnPlane.front()] - Centre).Normalised();
            if (U.Length() <= Tol) U = Plane.Normal.AnyPerpendicular();
            Vec3 V = Plane.Normal.Cross(U).Normalised();
            std::sort(OnPlane.begin(), OnPlane.end(), [&](int Left, int Right)
            {
                Vec3 A = Points[Left] - Centre, B = Points[Right] - Centre;
                return std::atan2(A.Dot(V), A.Dot(U)) < std::atan2(B.Dot(V), B.Dot(U));
            });
            // Remove any duplicate from a near-degenerate plane intersection before sewing.
            OnPlane.erase(std::unique(OnPlane.begin(), OnPlane.end()), OnPlane.end());
            if (OnPlane.size() >= 3) Polygons.push_back({ static_cast<int>(PlaneIndex), std::move(OnPlane) });
        }
        if (Polygons.size() < 4)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "chamfer reconstruction has too few planar faces");

        BrepBody Result;
        std::vector<int> VertexMap(Points.size(), -1);
        for (size_t I = 0; I < Points.size(); ++I) VertexMap[I] = Result.AddVertex(Points[I], ScalarCriteria::MergeTolerance * 2.0);
        for (const Polygon& Polygon : Polygons)
        {
            const ChamferPlane& Plane = Planes[Polygon.Plane];
            Vec3 Centre{};
            for (int Point : Polygon.Vertices) Centre = Centre + Points[Point];
            Centre = Centre * (1.0 / static_cast<double>(Polygon.Vertices.size()));
            Vec3 U = (Points[Polygon.Vertices.front()] - Centre).Normalised();
            if (U.Length() <= Tol) U = Plane.Normal.AnyPerpendicular();
            Vec3 V = Plane.Normal.Cross(U).Normalised();
            double MinU = ScalarCriteria::Infinity, MaxU = -ScalarCriteria::Infinity;
            double MinV = ScalarCriteria::Infinity, MaxV = -ScalarCriteria::Infinity;
            for (int Point : Polygon.Vertices)
            {
                Vec3 D = Points[Point] - Centre;
                MinU = std::min(MinU, D.Dot(U)); MaxU = std::max(MaxU, D.Dot(U));
                MinV = std::min(MinV, D.Dot(V)); MaxV = std::max(MaxV, D.Dot(V));
            }
            double Pad = std::max(ScalarCriteria::MergeTolerance, (MaxU - MinU + MaxV - MinV) * 1e-6);
            Deliver<NurbsSurface> Surface = NurbsSurface::Plane(Centre + U * (MinU - Pad) + V * (MinV - Pad), U, V,
                                                                  MaxU - MinU + 2.0 * Pad, MaxV - MinV + 2.0 * Pad);
            if (!Surface) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "chamfer reconstruction produced a degenerate face");
            int Face = Result.AddFace(std::move(Surface.Payload));
            Result.Faces[Face].Natural = false;
            int Loop = Result.AddLoop(Face, true);
            for (size_t I = 0; I < Polygon.Vertices.size(); ++I)
            {
                Vec3 A = Points[Polygon.Vertices[I]], B = Points[Polygon.Vertices[(I + 1) % Polygon.Vertices.size()]];
                Deliver<NurbsCurve> Curve = NurbsCurve::Line(A, B);
                if (!Curve) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "chamfer reconstruction produced a zero-length boundary");
                int Edge = Result.AddEdge(std::move(Curve.Payload), ScalarCriteria::MergeTolerance * 2.0);
                bool Reversed = Result.Edges[Edge].Curve.StartPoint().Distance(A) > ScalarCriteria::MergeTolerance;
                Result.AddCoedge(Edge, Reversed, Face, Loop);
            }
        }
        Result.Orient();
        if (!Result.Validate().Solid())
            return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "chamfer reconstruction did not close as a manifold solid");
        return Deliver<BrepBody>::Accept(std::move(Result));
    }
}

Deliver<BrepBody> BlendSolver::ChamferEdges(const BrepBody& Body, const std::vector<int>& SeedEdges,
                                             double SetBack, int* AppliedEdges) noexcept
{
    if (AppliedEdges) *AppliedEdges = 0;
    if (SeedEdges.empty())
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "chamfer edge set is empty");
    if (!Body.Validate().Solid())
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "chamfer edge set requires a valid solid body");
    if (!std::isfinite(SetBack) || SetBack <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "set-back is zero or negative");

    // Native circular rims and the bounded plane–cylinder / plane–cone roots have no straight planar edge frame;
    // preserve their exact curved-support routes before the planar-setback classifier.
    if (SeedEdges.size() == 1 && (NativeCylinderCap(Body, SeedEdges.front()) || NativeConeCapEdge(Body, SeedEdges.front()) ||
                                  PlaneCylinderBossRoot(Body, SeedEdges.front()) || PlaneConeBossRoot(Body, SeedEdges.front()) ||
                                  PlaneConeApexBossRoot(Body, SeedEdges.front()) || PlaneConeApexPartialBossRoot(Body, SeedEdges.front()) ||
                                  PlaneConePartialBossRoot(Body, SeedEdges.front()) || ConeCylinderPartialBossRoot(Body, SeedEdges.front()) ||
                                  ConeConePartialBossRoot(Body, SeedEdges.front()) || CylinderConeBossRoot(Body, SeedEdges.front())))
    {
        Deliver<BrepBody> Result = ChamferEdge(Body, SeedEdges.front(), SetBack);
        if (Result && AppliedEdges) *AppliedEdges = 1;
        return Result;
    }

    std::vector<int> Unique;
    std::vector<EdgeCornerFrame> Frames;
    Unique.reserve(SeedEdges.size());
    Frames.reserve(SeedEdges.size());
    for (int Edge : SeedEdges)
    {
        if (std::find(Unique.begin(), Unique.end(), Edge) != Unique.end())
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "chamfer edge set contains a duplicate edge");
        EdgeCornerFrame Frame;
        std::string Refusal;
        if (!BlendSolver::Frame(Body, Edge, Frame, Refusal))
            return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "selected edge requires two adjacent planar faces");
        if (!ChamferSetbackFits(Body, Edge, Frame, SetBack, Refusal))
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "set-back would self-intersect the next planar boundary");
        Unique.push_back(Edge);
        Frames.push_back(Frame);
    }

    const double SourceVolume = Body.Validate().Volume;
    double SumRemoval = 0.0;
    for (const EdgeCornerFrame& Frame : Frames) SumRemoval += ChamferRemoval(Frame, SetBack);
    const double VolumeTolerance = std::max(1e-5, std::fabs(SourceVolume) * 2e-7);

    // A non-planar selection is accepted only when it is one closed loop. An open or branched
    // selection has no unambiguous cyclic boundary pairing, so refuse before any reconstruction.
    if (IsNonPlanarEdgeSelection(Body, Unique) && !IsNonPlanarClosedEdgeLoop(Body, Unique))
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
            "non-planar edge selections must form one closed loop with an unambiguous boundary");

    // A closed non-planar edge loop is a distinct selection semantic: its vertices do not lie on one
    // cutting profile, but every adjacent support is still planar. Solve the full convex half-space
    // system in one transaction; never fall back to sequential edge-table chamfers, which can twist
    // the loop or publish a partially mitred result.
    if (IsNonPlanarClosedEdgeLoop(Body, Unique))
    {
        if (Deliver<BrepBody> Exact = ConvexPlanarChamfer(Body, Frames, SetBack))
        {
            const BodyReport Report = Exact.Payload.Validate();
            const double Removed = SourceVolume - Report.Volume;
            if (Report.Solid() && Report.Hulls == 1 && Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 &&
                Report.MisorientedEdges == 0 && Removed > VolumeTolerance && Removed <= SumRemoval + VolumeTolerance)
            {
                if (AppliedEdges) *AppliedEdges = static_cast<int>(Unique.size());
                return Exact;
            }
        }
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
            "non-planar edge loop needs one convex planar support system with a closed manifold result");
    }

    if (Unique.size() > 1)
        if (std::optional<BrepBody> Prism = PrismaticChamferNetwork(Body, Frames, SetBack))
        {
            const BodyReport Report = Prism->Validate();
            const double Removed = SourceVolume - Report.Volume;
            if (Report.Solid() && Removed > VolumeTolerance)
            {
                if (AppliedEdges) *AppliedEdges = static_cast<int>(Unique.size());
                return Deliver<BrepBody>::Accept(std::move(*Prism));
            }
        }

    if (Unique.size() == 1)
    {
        if (Deliver<BrepBody> Exact = ConvexPlanarChamfer(Body, Frames, SetBack))
        {
            double Removed = SourceVolume - Exact.Payload.Validate().Volume;
            if (Removed > VolumeTolerance && Removed <= SumRemoval + VolumeTolerance)
            {
                if (AppliedEdges) *AppliedEdges = 1;
                return Exact;
            }
        }
        Deliver<BrepBody> Result = ChamferEdge(Body, Unique.front(), SetBack);
        if (Result && AppliedEdges) *AppliedEdges = 1;
        return Result;
    }

    // Convex planar solids get the exact half-space reconstruction first. It handles adjacent loops and arbitrary
    // dihedral angles without Boolean end-cap contacts; the volume guard rejects a numerically valid but over-cut result.
    if (Deliver<BrepBody> Exact = ConvexPlanarChamfer(Body, Frames, SetBack))
    {
        double Removed = SourceVolume - Exact.Payload.Validate().Volume;
        if (Removed > VolumeTolerance && Removed <= SumRemoval + VolumeTolerance)
        {
            if (AppliedEdges) *AppliedEdges = static_cast<int>(Unique.size());
            return Exact;
        }
    }

    // A common cutter is important at a connected vertex: subtracting two finite prisms independently leaves an
    // order-dependent seam or asks the second operation to blend against the first chamfer face. Try a small geometric
    // ladder because the prism's transverse caps must clear different local face widths on different planar parts.
    for (double WidthFactor : { 0.8, 1.2, 2.0, 3.0, 5.0 })
    {
        Deliver<BrepBody> Tools = ChamferToolUnion(Frames, SetBack, WidthFactor);
        if (!Tools) continue;
        Deliver<BrepBody> Cut = IntersectionSolver::Combine(Body, Tools.Payload, BodyOperation::Subtract);
        if (!Cut) continue;
        BodyReport Report = Cut.Payload.Validate();
        if (!Report.Solid()) continue;
        double Removed = SourceVolume - Report.Volume;
        if (Removed <= VolumeTolerance || Removed > SumRemoval + VolumeTolerance) continue;
        if (AppliedEdges) *AppliedEdges = static_cast<int>(Unique.size());
        return Cut;
    }

    // If the common tool is refused by a non-transversal vertex contact, retry on a private copy in geometric order.
    // The resolver follows the original edge line and interval, not an edge-table index: the first chamfer shortens a
    // neighbouring member at a shared vertex and necessarily renumbers it. This fallback is still transactional because
    // `Working` is never published until every member succeeds.
    BrepBody Working = Body;
    for (const EdgeCornerFrame& Wanted : Frames)
    {
        Vec3 Direction = (Wanted.End - Wanted.Start).Normalised();
        double WantedLength = Wanted.Start.Distance(Wanted.End);
        int Found = -1;
        double Best = ScalarCriteria::Infinity;
        const double Epsilon = ScalarCriteria::ScaledPositionTolerance * std::max(1.0, Body.Bounds().Diagonal());
        for (size_t E = 0; E < Working.Edges.size(); ++E)
        {
            const BrepEdge& CandidateEdge = Working.Edges[E];
            if (CandidateEdge.Coedges.size() != 2 || CandidateEdge.VertexStart < 0 || CandidateEdge.VertexEnd < 0) continue;
            Vec3 A = Working.Vertices[CandidateEdge.VertexStart].Point;
            Vec3 B = Working.Vertices[CandidateEdge.VertexEnd].Point;
            Vec3 CandidateDirection = (B - A).Normalised();
            if (CandidateDirection.Length() <= Tol || std::fabs(std::fabs(CandidateDirection.Dot(Direction)) - 1.0) > 1e-6) continue;
            double AAlong = (A - Wanted.Start).Dot(Direction), BAlong = (B - Wanted.Start).Dot(Direction);
            double Low = std::min(AAlong, BAlong), High = std::max(AAlong, BAlong);
            if (High < -Epsilon || Low > WantedLength + Epsilon) continue;
            Vec3 ALine = Wanted.Start + Direction * AAlong, BLine = Wanted.Start + Direction * BAlong;
            double LineError = std::max(A.Distance(ALine), B.Distance(BLine));
            if (LineError > Epsilon) continue;
            Vec3 Mid = (A + B) * 0.5;
            double Score = std::fabs((Mid - Wanted.Start).Dot(Direction) - WantedLength * 0.5) + LineError * 10.0;
            if (Score < Best) { Best = Score; Found = static_cast<int>(E); }
        }
        if (Found < 0) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "selected edge disappeared during the transactional chamfer");
        Deliver<BrepBody> Result = ChamferEdge(Working, Found, SetBack);
        if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, Result.Denial.Detail);
        Working = std::move(Result.Payload);
    }
    if (Working.Validate().Solid())
    {
        if (AppliedEdges) *AppliedEdges = static_cast<int>(Unique.size());
        return Deliver<BrepBody>::Accept(std::move(Working));
    }
    return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
        "no common planar cutter produced a closed solid without self-intersection");
}

Deliver<BrepBody> BlendSolver::FilletEdge(const BrepBody& Body, int Edge, double Radius) noexcept
{
    if (std::optional<CylinderCap> Cap = NativeCylinderCap(Body, Edge)) return FilletCylinderCap(*Cap, Radius);
    if (std::optional<PlaneCylinderRoot> Root = PlaneCylinderBossRoot(Body, Edge))
        return FilletPlaneCylinderBossRoot(*Root, Radius);
    if (std::optional<PlaneConePartialRoot> Root = PlaneConePartialBossRoot(Body, Edge))
        return FilletPlaneConePartialBossRoot(*Root, Radius);
    if (std::optional<CylinderConeRoot> Root = CylinderConeBossRoot(Body, Edge))
        return FilletCylinderConeBossRoot(*Root, Radius);
    if (std::optional<ConeConeRoot> Root = ConeConeBossRoot(Body, Edge))
        return FilletConeConeBossRoot(*Root, Radius);
    if (std::optional<ConeCylinderPartialRoot> Root = ConeCylinderPartialBossRoot(Body, Edge))
        return FilletConeCylinderPartialBossRoot(*Root, Radius);
    if (std::optional<ConeConePartialRoot> Root = ConeConePartialBossRoot(Body, Edge))
        return FilletConeConePartialBossRoot(*Root, Radius);
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
    if (std::fabs(std::fabs(Axis.Dot(Specification.Low.Normal.Normalised())) - 1.0) > ScalarCriteria::AngularTolerance ||
        std::fabs(std::fabs(Axis.Dot(Specification.High.Normal.Normalised())) - 1.0) > ScalarCriteria::AngularTolerance)
        return Deliver<VariableRadiusSurface>::Reject(RefusalReason::Unsupported, "variable-radius supports must be normal to the axial spine");
    Vec3 Radial = Axis.Cross(Vec3::UnitX());
    if (Radial.Length() <= ScalarCriteria::GeometricTolerance) Radial = Axis.Cross(Vec3::UnitY());
    if (Radial.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<VariableRadiusSurface>::Reject(RefusalReason::DegenerateInput, "variable surface frame is degenerate");
    return Deliver<VariableRadiusSurface>::Accept(VariableRadiusSurface{ Specification.Low.Centre, Axis, Radial, Length, Specification.RadiusLaw });
}

Deliver<BrepBody> BlendSolver::ReconstructVariableRadiusRuledSolid(const AsymmetricBlendSpecification& Specification) noexcept
{
    if (std::fabs(Specification.Low.EndpointAngle) > ScalarCriteria::SweepTolerance ||
        std::fabs(Specification.High.EndpointAngle) > ScalarCriteria::SweepTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "variable-radius ruled reconstruction requires complete circular supports");
    auto Surface = BuildVariableRadiusSurface(Specification);
    if (!Surface) return Deliver<BrepBody>::Reject(Surface.Denial.Reason, "invalid variable-radius ruled surface");
    Deliver<BrepBody> Result = BrepBody::Cone(Surface.Payload.Origin, Surface.Payload.Axis, Surface.Payload.Law.Start,
                                               Surface.Payload.Law.End, Surface.Payload.Length);
    if (!Result) return Result;
    const double ExactVolume = Surface.Payload.Law.SweptVolume(Surface.Payload.Length);
    if (!ScalarCriteria::WithinVolumeTolerance(Result.Payload.Validate().Volume, ExactVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence, "variable-radius ruled volume failed analytic acceptance");
    return Result;
}

Deliver<BrepBody> BlendSolver::ReconstructVariableRadiusCornerBlend(const VariableRadiusCornerSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Length) || !std::isfinite(Specification.Width) ||
        Specification.Length <= ScalarCriteria::MergeTolerance || Specification.Width <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "variable corner dimensions must be positive");
    if (!Specification.RadiusLaw.Positive())
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "variable corner radii must be finite and positive");
    const Vec3 Axis = Specification.EdgeAxis.Normalised();
    if (Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "variable corner edge axis is degenerate");
    const double MaximumRadius = std::max(Specification.RadiusLaw.Start, Specification.RadiusLaw.End);
    if (MaximumRadius >= Specification.Width - ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "variable corner radius consumes the planar supports");

    Vec3 U = Axis.Cross(Vec3::UnitX());
    if (U.Length() <= ScalarCriteria::GeometricTolerance) U = Axis.Cross(Vec3::UnitY());
    if (U.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "variable corner frame is degenerate");
    U = U.Normalised();
    const Vec3 V = Axis.Cross(U).Normalised();
    auto Point = [&](double T, double AlongU, double AlongV)
    {
        return Specification.Origin + Axis * (Specification.Length * T) + U * AlongU + V * AlongV;
    };
    auto SectionLine = [&](double T, Vec3 A, Vec3 B) -> Deliver<NurbsCurve>
    {
        return NurbsCurve::Line(Point(T, A.X, A.Y), Point(T, B.X, B.Y));
    };
    auto SectionArc = [&](double T) -> Deliver<NurbsCurve>
    {
        const double Radius = Specification.RadiusLaw.Radius(T);
        const Vec3 A{ Radius, 0, 0 }, E{ 0, Radius, 0 };
        const Vec3 Middle{ Radius - Radius / std::sqrt(2.0), Radius - Radius / std::sqrt(2.0), 0 };
        return NurbsCurve::ArcThreePoints(Point(T, E.X, E.Y), Point(T, Middle.X, Middle.Y), Point(T, A.X, A.Y));
    };
    auto LoftTwo = [&](const Deliver<NurbsCurve>& A, const Deliver<NurbsCurve>& B) -> Deliver<NurbsSurface>
    {
        if (!A || !B) return Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput, "variable corner section is degenerate");
        return NurbsSurface::Loft({ A.Payload, B.Payload }, 1);
    };
    const double Width = Specification.Width;
    const Vec3 A0{ Specification.RadiusLaw.Start, 0, 0 }, B0{ Width, 0, 0 }, C0{ Width, Width, 0 }, D0{ 0, Width, 0 }, E0{ 0, Specification.RadiusLaw.Start, 0 };
    const Vec3 A1{ Specification.RadiusLaw.End, 0, 0 }, B1{ Width, 0, 0 }, C1{ Width, Width, 0 }, D1{ 0, Width, 0 }, E1{ 0, Specification.RadiusLaw.End, 0 };
    std::vector<NurbsSurface> Surfaces;
    auto Add = [&](const Deliver<NurbsCurve>& Low, const Deliver<NurbsCurve>& High) -> bool
    {
        Deliver<NurbsSurface> Surface = LoftTwo(Low, High);
        if (!Surface) return false;
        Surfaces.push_back(std::move(Surface.Payload));
        return true;
    };
    if (!Add(SectionLine(0.0, A0, B0), SectionLine(1.0, A1, B1)) ||
        !Add(SectionLine(0.0, B0, C0), SectionLine(1.0, B1, C1)) ||
        !Add(SectionLine(0.0, C0, D0), SectionLine(1.0, C1, D1)) ||
        !Add(SectionLine(0.0, D0, E0), SectionLine(1.0, D1, E1)) ||
        !Add(SectionArc(0.0), SectionArc(1.0)))
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "variable corner surfaces could not be constructed");

    Deliver<BrepBody> Result = BrepBody::Sew(Surfaces, ScalarCriteria::MergeTolerance, true);
    if (!Result) return Result;
    const BodyReport Report = Result.Payload.Validate();
    const double R0 = Specification.RadiusLaw.Start, R1 = Specification.RadiusLaw.End;
    const double Removed = (1.0 - ScalarCriteria::Pi / 4.0) * Specification.Length * (R0 * R0 + R0 * R1 + R1 * R1) / 3.0;
    const double ExpectedVolume = Specification.Length * Width * Width - Removed;
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 ||
        Result.Payload.Vertices.size() != 10 || Result.Payload.Edges.size() != 15 ||
        Result.Payload.Coedges.size() != 30 || Result.Payload.Loops.size() != 7 || Result.Payload.Faces.size() != 7)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "variable corner blend did not reach exact capped topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence, "variable corner blend volume failed analytic acceptance");
    return Result;
}

Deliver<BrepBody> BlendSolver::ReconstructVariableSetbackCornerBlend(const VariableSetbackCornerSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Length) ||
        Specification.Length <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "variable setback corner length must be positive");
    if (!Specification.RadiusLaw.Positive())
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "variable setback corner radii must be finite and positive");
    if (!Specification.SetbackLaw.Positive())
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "variable support setbacks must be finite and positive");

    const Vec3 Axis = Specification.EdgeAxis.Normalised();
    if (Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "variable setback corner edge axis is degenerate");

    const double R0 = Specification.RadiusLaw.Start;
    const double R1 = Specification.RadiusLaw.End;
    const double S0 = Specification.SetbackLaw.Start;
    const double S1 = Specification.SetbackLaw.End;
    const double D0 = R0 + S0;
    const double D1 = R1 + S1;
    if (!std::isfinite(D0) || !std::isfinite(D1) ||
        D0 <= ScalarCriteria::MergeTolerance || D1 <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "variable support extent is degenerate");

    Vec3 U = Axis.Cross(Vec3::UnitX());
    if (U.Length() <= ScalarCriteria::GeometricTolerance) U = Axis.Cross(Vec3::UnitY());
    if (U.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "variable setback corner frame is degenerate");
    U = U.Normalised();
    const Vec3 V = Axis.Cross(U).Normalised();
    auto Point = [&](double T, double AlongU, double AlongV)
    {
        return Specification.Origin + Axis * (Specification.Length * T) + U * AlongU + V * AlongV;
    };
    auto SectionLine = [&](double T, Vec3 A, Vec3 B) -> Deliver<NurbsCurve>
    {
        return NurbsCurve::Line(Point(T, A.X, A.Y), Point(T, B.X, B.Y));
    };
    auto SectionArc = [&](double T) -> Deliver<NurbsCurve>
    {
        const double Radius = Specification.RadiusLaw.Radius(T);
        const Vec3 A{ Radius, 0, 0 }, E{ 0, Radius, 0 };
        const double Offset = Radius - Radius / std::sqrt(2.0);
        const Vec3 Middle{ Offset, Offset, 0 };
        return NurbsCurve::ArcThreePoints(Point(T, E.X, E.Y), Point(T, Middle.X, Middle.Y), Point(T, A.X, A.Y));
    };
    auto LoftTwo = [&](const Deliver<NurbsCurve>& A, const Deliver<NurbsCurve>& B) -> Deliver<NurbsSurface>
    {
        if (!A || !B)
            return Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput, "variable setback corner section is degenerate");
        return NurbsSurface::Loft({ A.Payload, B.Payload }, 1);
    };

    const Vec3 A0{ R0, 0, 0 }, B0{ D0, 0, 0 }, C0{ D0, D0, 0 }, D0Point{ 0, D0, 0 }, E0{ 0, R0, 0 };
    const Vec3 A1{ R1, 0, 0 }, B1{ D1, 0, 0 }, C1{ D1, D1, 0 }, D1Point{ 0, D1, 0 }, E1{ 0, R1, 0 };
    std::vector<NurbsSurface> Surfaces;
    auto Add = [&](const Deliver<NurbsCurve>& Low, const Deliver<NurbsCurve>& High) -> bool
    {
        Deliver<NurbsSurface> Surface = LoftTwo(Low, High);
        if (!Surface) return false;
        Surfaces.push_back(std::move(Surface.Payload));
        return true;
    };
    if (!Add(SectionLine(0.0, A0, B0), SectionLine(1.0, A1, B1)) ||
        !Add(SectionLine(0.0, B0, C0), SectionLine(1.0, B1, C1)) ||
        !Add(SectionLine(0.0, C0, D0Point), SectionLine(1.0, C1, D1Point)) ||
        !Add(SectionLine(0.0, D0Point, E0), SectionLine(1.0, D1Point, E1)) ||
        !Add(SectionArc(0.0), SectionArc(1.0)))
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "variable setback corner surfaces could not be constructed");

    Deliver<BrepBody> Result = BrepBody::Sew(Surfaces, ScalarCriteria::MergeTolerance, true);
    if (!Result) return Result;
    const BodyReport Report = Result.Payload.Validate();
    const double ExpectedOuterVolume = Specification.Length * (D0 * D0 + D0 * D1 + D1 * D1) / 3.0;
    const double RemovedCornerVolume = (1.0 - ScalarCriteria::Pi / 4.0) * Specification.Length *
        (R0 * R0 + R0 * R1 + R1 * R1) / 3.0;
    const double ExpectedVolume = ExpectedOuterVolume - RemovedCornerVolume;
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 ||
        Result.Payload.Vertices.size() != 10 || Result.Payload.Edges.size() != 15 ||
        Result.Payload.Coedges.size() != 30 || Result.Payload.Loops.size() != 7 || Result.Payload.Faces.size() != 7)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "variable setback corner blend did not reach exact capped topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence, "variable setback corner volume failed analytic acceptance");
    return Result;
}

Deliver<BrepBody> BlendSolver::ReconstructNonlinearUnequalSetbackCornerBlend(
    const NonlinearUnequalSetbackCornerSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Length) || Specification.Length <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "nonlinear unequal setback corner length must be positive");
    if (!Specification.RadiusLaw.Positive() || !Specification.SetbackALaw.Positive() ||
        !Specification.SetbackBLaw.Positive())
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "nonlinear unequal setback laws must be finite and positive");
    if (!Specification.SetbackALaw.Nonlinear() || !Specification.SetbackBLaw.Nonlinear())
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "both nonlinear unequal support setbacks must be genuinely quadratic");
    const auto UnequalAt = [&](double T) noexcept
    {
        return std::fabs(Specification.SetbackALaw.Radius(T) - Specification.SetbackBLaw.Radius(T)) >
               ScalarCriteria::GeometricTolerance;
    };
    if (!UnequalAt(0.0) && !UnequalAt(0.5) && !UnequalAt(1.0))
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "equal nonlinear support setbacks belong to the common-setback route");
    const Vec3 Axis = Specification.EdgeAxis.Normalised();
    if (Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "nonlinear unequal setback corner edge axis is degenerate");
    Vec3 U = Axis.Cross(Vec3::UnitX());
    if (U.Length() <= ScalarCriteria::GeometricTolerance) U = Axis.Cross(Vec3::UnitY());
    if (U.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "nonlinear unequal setback corner frame is degenerate");
    U = U.Normalised();
    const Vec3 V = Axis.Cross(U).Normalised();
    auto Point = [&](double T, double AlongU, double AlongV)
    {
        return Specification.Origin + Axis * (Specification.Length * T) + U * AlongU + V * AlongV;
    };
    auto LineSection = [&](double T, Vec3 A, Vec3 B) -> Deliver<NurbsCurve>
    {
        return NurbsCurve::Line(Point(T, A.X, A.Y), Point(T, B.X, B.Y));
    };
    auto ArcSection = [&](double T) -> Deliver<NurbsCurve>
    {
        const double R = Specification.RadiusLaw.Radius(T);
        const double Offset = R - R / std::sqrt(2.0);
        return NurbsCurve::ArcThreePoints(Point(T, 0.0, R), Point(T, Offset, Offset), Point(T, R, 0.0));
    };
    auto LoftThree = [&](const std::vector<Deliver<NurbsCurve>>& Sections) -> Deliver<NurbsSurface>
    {
        std::vector<NurbsCurve> Curves;
        Curves.reserve(Sections.size());
        for (const Deliver<NurbsCurve>& Section : Sections)
        {
            if (!Section) return Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput,
                                                                "nonlinear unequal setback section is degenerate");
            Curves.push_back(Section.Payload);
        }
        int Degree = 1;
        for (const NurbsCurve& Curve : Curves) Degree = std::max(Degree, Curve.Degree);
        std::vector<NurbsCurve> Rows;
        for (const NurbsCurve& Curve : Curves)
            Rows.push_back((Curve.Degree < Degree ? Curve.Elevated(Degree) : Curve).Reparameterised(0.0, 1.0));
        std::vector<double> Distinct;
        for (const NurbsCurve& Curve : Rows) for (double Knot : Curve.Knots) Distinct.push_back(Knot);
        std::sort(Distinct.begin(), Distinct.end());
        Distinct.erase(std::unique(Distinct.begin(), Distinct.end(),
                                   [](double A, double B) { return ScalarCriteria::Coincident(A, B, ScalarCriteria::ParametricEpsilon); }),
                       Distinct.end());
        const auto Multiplicity = [](const std::vector<double>& Knots, double T)
        {
            int Count = 0; for (double Knot : Knots) if (ScalarCriteria::Coincident(Knot, T, ScalarCriteria::ParametricEpsilon)) ++Count; return Count;
        };
        for (double Knot : Distinct)
        {
            int Target = 0; for (const NurbsCurve& Curve : Rows) Target = std::max(Target, Multiplicity(Curve.Knots, Knot));
            for (NurbsCurve& Curve : Rows)
            {
                const int Need = Target - Multiplicity(Curve.Knots, Knot);
                if (Need > 0) Curve = Curve.InsertKnot(Knot, Need);
            }
        }
        const int CountU = Rows.front().PoleCount();
        const std::vector<double> StationParameters{ 0.0, 0.5, 1.0 };
        std::vector<NurbsCurve> Columns;
        std::vector<double> KnotsV;
        for (int I = 0; I < CountU; ++I)
        {
            std::vector<Vec4> Through;
            for (const NurbsCurve& Curve : Rows) Through.push_back(Curve.Poles[I]);
            Deliver<NurbsCurve> Column = NurbsCurve::InterpolateHomogeneous(Through, 2, &StationParameters);
            if (!Column) return Deliver<NurbsSurface>::Reject(Column.Denial.Reason, Column.Denial.Detail);
            if (Columns.empty()) KnotsV = Column.Payload.Knots;
            Columns.push_back(std::move(Column.Payload));
        }
        NurbsSurface Surface;
        Surface.DegreeU = Degree; Surface.DegreeV = 2; Surface.CountU = CountU;
        Surface.CountV = Columns.front().PoleCount(); Surface.KnotsU = Rows.front().Knots; Surface.KnotsV = KnotsV;
        Surface.Poles.resize(static_cast<size_t>(Surface.CountU) * Surface.CountV);
        for (int I = 0; I < Surface.CountU; ++I)
            for (int J = 0; J < Surface.CountV; ++J) Surface.Pole(I, J) = Columns[I].Poles[J];
        Surface.Classification = SurfaceClassification::Loft;
        return Deliver<NurbsSurface>::Accept(std::move(Surface));
    };
    auto Add = [&](const std::vector<Deliver<NurbsCurve>>& Sections,
                   std::vector<NurbsSurface>& Surfaces) -> bool
    {
        Deliver<NurbsSurface> Surface = LoftThree(Sections);
        if (!Surface) return false;
        Surfaces.push_back(std::move(Surface.Payload));
        return true;
    };
    const double R0 = Specification.RadiusLaw.Radius(0.0);
    const double Rm = Specification.RadiusLaw.Radius(0.5);
    const double R1 = Specification.RadiusLaw.Radius(1.0);
    const double A0 = R0 + Specification.SetbackALaw.Radius(0.0);
    const double Am = Rm + Specification.SetbackALaw.Radius(0.5);
    const double A1 = R1 + Specification.SetbackALaw.Radius(1.0);
    const double B0 = R0 + Specification.SetbackBLaw.Radius(0.0);
    const double Bm = Rm + Specification.SetbackBLaw.Radius(0.5);
    const double B1 = R1 + Specification.SetbackBLaw.Radius(1.0);
    if (A0 <= R0 || Am <= Rm || A1 <= R1 || B0 <= R0 || Bm <= Rm || B1 <= R1)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "nonlinear unequal setback extents do not leave a positive corner");
    const Vec3 P00{ R0, 0, 0 }, Q00{ A0, 0, 0 }, R00{ A0, B0, 0 }, S00{ 0, B0, 0 }, T00{ 0, R0, 0 };
    const Vec3 Pm{ Rm, 0, 0 }, Qm{ Am, 0, 0 }, RmPoint{ Am, Bm, 0 }, Sm{ 0, Bm, 0 }, Tm{ 0, Rm, 0 };
    const Vec3 P11{ R1, 0, 0 }, Q11{ A1, 0, 0 }, R11{ A1, B1, 0 }, S11{ 0, B1, 0 }, T11{ 0, R1, 0 };
    std::vector<NurbsSurface> Surfaces;
    if (!Add({ LineSection(0.0, P00, Q00), LineSection(0.5, Pm, Qm), LineSection(1.0, P11, Q11) }, Surfaces) ||
        !Add({ LineSection(0.0, Q00, R00), LineSection(0.5, Qm, RmPoint), LineSection(1.0, Q11, R11) }, Surfaces) ||
        !Add({ LineSection(0.0, R00, S00), LineSection(0.5, RmPoint, Sm), LineSection(1.0, R11, S11) }, Surfaces) ||
        !Add({ LineSection(0.0, S00, T00), LineSection(0.5, Sm, Tm), LineSection(1.0, S11, T11) }, Surfaces) ||
        !Add({ ArcSection(0.0), ArcSection(0.5), ArcSection(1.0) }, Surfaces))
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "nonlinear unequal setback corner surfaces could not be constructed");
    Deliver<BrepBody> Result = BrepBody::Sew(Surfaces, ScalarCriteria::MergeTolerance, true);
    if (!Result) return Result;
    const auto Coefficients = [](const QuadraticRadiusLaw& Law)
    {
        return std::array<double, 3>{ Law.Start, -3.0 * Law.Start + 4.0 * Law.Middle - Law.End,
                                      2.0 * Law.Start - 4.0 * Law.Middle + 2.0 * Law.End };
    };
    const auto IntegralProduct = [&](const QuadraticRadiusLaw& First, const QuadraticRadiusLaw& Second)
    {
        const auto A = Coefficients(First); const auto B = Coefficients(Second);
        double Integral = 0.0;
        for (int I = 0; I <= 2; ++I) for (int J = 0; J <= 2; ++J)
            Integral += A[I] * B[J] * Specification.Length / static_cast<double>(I + J + 1);
        return Integral;
    };
    const QuadraticRadiusLaw OuterALaw{
        Specification.RadiusLaw.Start + Specification.SetbackALaw.Start,
        Specification.RadiusLaw.Middle + Specification.SetbackALaw.Middle,
        Specification.RadiusLaw.End + Specification.SetbackALaw.End };
    const QuadraticRadiusLaw OuterBLaw{
        Specification.RadiusLaw.Start + Specification.SetbackBLaw.Start,
        Specification.RadiusLaw.Middle + Specification.SetbackBLaw.Middle,
        Specification.RadiusLaw.End + Specification.SetbackBLaw.End };
    const double ExpectedVolume = IntegralProduct(OuterALaw, OuterBLaw) -
        (1.0 - ScalarCriteria::Pi / 4.0) * Specification.RadiusLaw.IntegratedSquare(Specification.Length);
    const BodyReport Report = Result.Payload.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 ||
        Result.Payload.Vertices.size() != 10 || Result.Payload.Edges.size() != 15 ||
        Result.Payload.Coedges.size() != 30 || Result.Payload.Loops.size() != 7 || Result.Payload.Faces.size() != 7)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                                          "nonlinear unequal setback corner did not reach exact capped topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence,
                                          "nonlinear unequal setback corner volume failed analytic acceptance");
    return Result;
}

Deliver<BrepBody> BlendSolver::ReconstructUnequalSetbackCornerBlend(
    const UnequalSetbackCornerSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Length) || Specification.Length <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "unequal setback corner length must be positive");
    if (!Specification.RadiusLaw.Positive() || !Specification.SetbackALaw.Positive() ||
        !Specification.SetbackBLaw.Positive())
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "unequal setback laws must be finite and positive");
    const bool Unequal = std::fabs(Specification.SetbackALaw.Start - Specification.SetbackBLaw.Start) > ScalarCriteria::GeometricTolerance ||
                         std::fabs(Specification.SetbackALaw.End - Specification.SetbackBLaw.End) > ScalarCriteria::GeometricTolerance;
    if (!Unequal)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "equal support setbacks belong to the common-setback route");
    const Vec3 Axis = Specification.EdgeAxis.Normalised();
    if (Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "unequal setback corner edge axis is degenerate");
    Vec3 U = Axis.Cross(Vec3::UnitX());
    if (U.Length() <= ScalarCriteria::GeometricTolerance) U = Axis.Cross(Vec3::UnitY());
    if (U.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "unequal setback corner frame is degenerate");
    U = U.Normalised();
    const Vec3 V = Axis.Cross(U).Normalised();
    auto Point = [&](double T, double AlongU, double AlongV)
    {
        return Specification.Origin + Axis * (Specification.Length * T) + U * AlongU + V * AlongV;
    };
    auto LineSection = [&](double T, Vec3 A, Vec3 B) -> Deliver<NurbsCurve>
    {
        return NurbsCurve::Line(Point(T, A.X, A.Y), Point(T, B.X, B.Y));
    };
    auto ArcSection = [&](double T) -> Deliver<NurbsCurve>
    {
        const double R = Specification.RadiusLaw.Radius(T);
        const double Offset = R - R / std::sqrt(2.0);
        return NurbsCurve::ArcThreePoints(Point(T, 0.0, R), Point(T, Offset, Offset), Point(T, R, 0.0));
    };
    auto LoftTwo = [&](const Deliver<NurbsCurve>& A, const Deliver<NurbsCurve>& B) -> Deliver<NurbsSurface>
    {
        if (!A || !B) return Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput, "unequal setback section is degenerate");
        return NurbsSurface::Loft({ A.Payload, B.Payload }, 1);
    };
    const double R0 = Specification.RadiusLaw.Start, R1 = Specification.RadiusLaw.End;
    const double SA0 = Specification.SetbackALaw.Start, SA1 = Specification.SetbackALaw.End;
    const double SB0 = Specification.SetbackBLaw.Start, SB1 = Specification.SetbackBLaw.End;
    const double UA0 = R0 + SA0, UA1 = R1 + SA1;
    const double VB0 = R0 + SB0, VB1 = R1 + SB1;
    const Vec3 A0{ R0, 0, 0 }, B0{ UA0, 0, 0 }, C0{ UA0, VB0, 0 }, D0{ 0, VB0, 0 }, E0{ 0, R0, 0 };
    const Vec3 A1{ R1, 0, 0 }, B1{ UA1, 0, 0 }, C1{ UA1, VB1, 0 }, D1{ 0, VB1, 0 }, E1{ 0, R1, 0 };
    std::vector<NurbsSurface> Surfaces;
    auto Add = [&](const Deliver<NurbsCurve>& Low, const Deliver<NurbsCurve>& High) -> bool
    {
        Deliver<NurbsSurface> Surface = LoftTwo(Low, High);
        if (!Surface) return false;
        Surfaces.push_back(std::move(Surface.Payload));
        return true;
    };
    if (!Add(LineSection(0.0, A0, B0), LineSection(1.0, A1, B1)) ||
        !Add(LineSection(0.0, B0, C0), LineSection(1.0, B1, C1)) ||
        !Add(LineSection(0.0, C0, D0), LineSection(1.0, C1, D1)) ||
        !Add(LineSection(0.0, D0, E0), LineSection(1.0, D1, E1)) ||
        !Add(ArcSection(0.0), ArcSection(1.0)))
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "unequal setback corner surfaces could not be constructed");
    Deliver<BrepBody> Result = BrepBody::Sew(Surfaces, ScalarCriteria::MergeTolerance, true);
    if (!Result) return Result;
    const double OuterVolume = Specification.Length * (2.0 * UA0 * VB0 + UA0 * VB1 + UA1 * VB0 + 2.0 * UA1 * VB1) / 6.0;
    const double RemovedVolume = (1.0 - ScalarCriteria::Pi / 4.0) * Specification.Length *
        (R0 * R0 + R0 * R1 + R1 * R1) / 3.0;
    const double ExpectedVolume = OuterVolume - RemovedVolume;
    const BodyReport Report = Result.Payload.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 ||
        Result.Payload.Vertices.size() != 10 || Result.Payload.Edges.size() != 15 ||
        Result.Payload.Coedges.size() != 30 || Result.Payload.Loops.size() != 7 || Result.Payload.Faces.size() != 7)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "unequal setback corner did not reach exact capped topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence, "unequal setback corner volume failed analytic acceptance");
    return Result;
}

Deliver<QuadraticVariableRadiusSurface> BlendSolver::BuildQuadraticVariableRadiusSurface(
    const NonlinearVariableRadiusCornerSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Length) || Specification.Length <= ScalarCriteria::MergeTolerance)
        return Deliver<QuadraticVariableRadiusSurface>::Reject(RefusalReason::DegenerateInput,
                                                                 "nonlinear variable-radius length must be positive");
    if (!Specification.RadiusLaw.Positive() || !Specification.RadiusLaw.Nonlinear())
        return Deliver<QuadraticVariableRadiusSurface>::Reject(RefusalReason::Unsupported,
                                                                 "nonlinear surface requires a positive non-linear quadratic law");
    const Vec3 Axis = Specification.EdgeAxis.Normalised();
    if (Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<QuadraticVariableRadiusSurface>::Reject(RefusalReason::DegenerateInput,
                                                                 "nonlinear variable-radius edge axis is degenerate");
    Vec3 Radial = Axis.Cross(Vec3::UnitX());
    if (Radial.Length() <= ScalarCriteria::GeometricTolerance) Radial = Axis.Cross(Vec3::UnitY());
    if (Radial.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<QuadraticVariableRadiusSurface>::Reject(RefusalReason::DegenerateInput,
                                                                 "nonlinear variable-radius frame is degenerate");
    return Deliver<QuadraticVariableRadiusSurface>::Accept(
        QuadraticVariableRadiusSurface{ Specification.Origin, Axis, Radial, Specification.Length, Specification.RadiusLaw });
}

bool BlendSolver::ValidateQuadraticSurfaceCurvature(const QuadraticVariableRadiusSurface& Surface,
                                                      double MaximumCircumferentialCurvature,
                                                      double MaximumMeridionalCurvature,
                                                      std::string& Refusal) noexcept
{
    if (!std::isfinite(MaximumCircumferentialCurvature) || MaximumCircumferentialCurvature <= 0.0 ||
        !std::isfinite(MaximumMeridionalCurvature) || MaximumMeridionalCurvature < 0.0)
    { Refusal = "quadratic surface curvature bounds are invalid"; return false; }
    if (Surface.Length <= ScalarCriteria::GeometricTolerance || !Surface.Law.Positive())
    { Refusal = "quadratic variable-radius surface is degenerate"; return false; }
    for (int I = 0; I <= 32; ++I)
    {
        const double T = static_cast<double>(I) / 32.0;
        const double Circumferential = Surface.CircumferentialCurvature(T);
        const double Meridional = std::fabs(Surface.MeridionalCurvature(T));
        if (!std::isfinite(Circumferential) || !std::isfinite(Meridional) ||
            Circumferential > MaximumCircumferentialCurvature + ScalarCriteria::GeometricTolerance ||
            Meridional > MaximumMeridionalCurvature + ScalarCriteria::GeometricTolerance)
        { Refusal = "quadratic variable-radius curvature exceeds the declared bounds"; return false; }
    }
    return true;
}

Deliver<BrepBody> BlendSolver::ReconstructPartialEdgeFillet(const PartialEdgeFilletSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Length) || !std::isfinite(Specification.Start) ||
        !std::isfinite(Specification.End) || !std::isfinite(Specification.Width) ||
        !std::isfinite(Specification.Radius) || Specification.Length <= Tol || Specification.Width <= Tol ||
        Specification.Radius <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial edge dimensions and radius must be finite and positive");
    if (Specification.Start <= Tol || Specification.End >= Specification.Length - Tol ||
        Specification.End <= Specification.Start + Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "partial edge interval must be a strict interior segment");
    if (Specification.Radius >= Specification.Width - Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial edge radius consumes the planar supports");
    if (!std::isfinite(Specification.EdgeAxis.X) || !std::isfinite(Specification.EdgeAxis.Y) ||
        !std::isfinite(Specification.EdgeAxis.Z) || Specification.EdgeAxis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial edge axis is degenerate");
    const Vec3 Axis = Specification.EdgeAxis.Normalised();
    Vec3 U = Axis.Cross(Vec3::UnitX());
    if (U.Length() <= ScalarCriteria::GeometricTolerance) U = Axis.Cross(Vec3::UnitY());
    if (U.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial edge frame is degenerate");
    U = U.Normalised();
    const Vec3 V = Axis.Cross(U).Normalised();
    const double R = Specification.Radius;
    const double W = Specification.Width;
    const double S = Specification.Start;
    const double E = Specification.End;
    const Vec3 O = Specification.Origin;
    auto At = [&](double T, double AlongU, double AlongV) noexcept
    {
        return O + Axis * T + U * AlongU + V * AlongV;
    };
    auto Line = [](Vec3 A, Vec3 B) -> Deliver<NurbsCurve> { return NurbsCurve::Line(A, B); };
    auto Arc = [&](double T) -> Deliver<NurbsCurve>
    {
        const double Offset = R - R / std::sqrt(2.0);
        return NurbsCurve::ArcThreePoints(At(T, R, 0.0), At(T, Offset, Offset), At(T, 0.0, R));
    };
    std::vector<NurbsSurface> Faces;
    auto AddPlane = [&](Vec3 Corner, Vec3 UAxis, Vec3 VAxis, double LU, double LV) -> bool
    {
        Deliver<NurbsSurface> Face = NurbsSurface::Plane(Corner, UAxis, VAxis, LU, LV);
        if (!Face) return false;
        Faces.push_back(std::move(Face.Payload));
        return true;
    };
    auto AddWallInterval = [&](double A, double B) -> bool
    {
        const double D = B - A;
        return AddPlane(At(A, 0.0, 0.0), Axis, U, D, R) &&
               AddPlane(At(A, R, 0.0), Axis, U, D, W - R) &&
               AddPlane(At(A, W, 0.0), Axis, V, D, R) &&
               AddPlane(At(A, W, R), Axis, V, D, W - R) &&
               AddPlane(At(A, 0.0, W), Axis, U, D, R) &&
               AddPlane(At(A, R, W), Axis, U, D, W - R) &&
               AddPlane(At(A, 0.0, 0.0), Axis, V, D, R) &&
               AddPlane(At(A, 0.0, R), Axis, V, D, W - R);
    };
    auto AddBlendInterval = [&](double A, double B) -> bool
    {
        const double D = B - A;
        // Inside the selected interval the two support strips nearest the corner are removed;
        // the remaining four-sided boundary is the two shortened walls plus split outer walls.
        return AddPlane(At(A, R, 0.0), Axis, U, D, W - R) &&
               AddPlane(At(A, W, 0.0), Axis, V, D, R) &&
               AddPlane(At(A, W, R), Axis, V, D, W - R) &&
               AddPlane(At(A, 0.0, W), Axis, U, D, R) &&
               AddPlane(At(A, R, W), Axis, U, D, W - R) &&
               AddPlane(At(A, 0.0, R), Axis, V, D, W - R);
    };
    if (!AddWallInterval(0.0, S) || !AddBlendInterval(S, E) || !AddWallInterval(E, Specification.Length))
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial edge support patches are degenerate");
    Deliver<NurbsCurve> StartArc = Arc(S), EndArc = Arc(E);
    if (!StartArc || !EndArc)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial edge arc is degenerate");
    Deliver<NurbsSurface> Blend = NurbsSurface::Loft({ StartArc.Payload, EndArc.Payload }, 1);
    if (!Blend) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial edge blend surface is degenerate");
    Faces.push_back(std::move(Blend.Payload));
    auto AddTransition = [&](double T) -> bool
    {
        Deliver<NurbsCurve> OA = Line(At(T, 0.0, 0.0), At(T, R, 0.0));
        Deliver<NurbsCurve> AE = Arc(T);
        Deliver<NurbsCurve> EO = Line(At(T, 0.0, R), At(T, 0.0, 0.0));
        if (!OA || !AE || !EO) return false;
        Deliver<NurbsSurface> Cap = SkinSolver::CoonsPatch({ OA.Payload, AE.Payload, EO.Payload });
        if (!Cap) return false;
        Faces.push_back(std::move(Cap.Payload));
        return true;
    };
    if (!AddTransition(S) || !AddTransition(E))
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial edge transition cap is degenerate");
    auto AddEndCap = [&](double T) -> bool
    {
        return AddPlane(At(T, 0.0, 0.0), U, V, R, R) &&
               AddPlane(At(T, R, 0.0), U, V, W - R, R) &&
               AddPlane(At(T, R, R), U, V, W - R, W - R) &&
               AddPlane(At(T, 0.0, R), U, V, R, W - R);
    };
    if (!AddEndCap(0.0) || !AddEndCap(Specification.Length))
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial edge end cap is degenerate");
    Deliver<BrepBody> Result = BrepBody::Sew(Faces, Tol, true);
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, "partial edge support patches could not be sewn");
    const BodyReport Report = Result.Payload.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "partial edge blend did not reach closed manifold topology");
    return Result;
}

Deliver<BrepBody> BlendSolver::ReconstructObliquePlanarCornerFillet(
    const ObliquePlanarCornerFilletSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Origin.X) || !std::isfinite(Specification.Origin.Y) ||
        !std::isfinite(Specification.Origin.Z) || !std::isfinite(Specification.Length) ||
        !std::isfinite(Specification.WidthA) || !std::isfinite(Specification.WidthB) ||
        !std::isfinite(Specification.Radius) || Specification.Length <= ScalarCriteria::MergeTolerance ||
        Specification.WidthA <= ScalarCriteria::MergeTolerance || Specification.WidthB <= ScalarCriteria::MergeTolerance ||
        Specification.Radius <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "oblique corner dimensions and radius must be finite and positive");
    if (Specification.EdgeAxis.Length() <= ScalarCriteria::GeometricTolerance ||
        Specification.SupportA.Length() <= ScalarCriteria::GeometricTolerance ||
        Specification.SupportB.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "oblique corner frame contains a degenerate direction");
    const Vec3 Axis = Specification.EdgeAxis.Normalised();
    const Vec3 SupportA = Specification.SupportA.Normalised();
    const Vec3 SupportB = Specification.SupportB.Normalised();
    if (std::fabs(Axis.Dot(SupportA)) > ScalarCriteria::AngularTolerance ||
        std::fabs(Axis.Dot(SupportB)) > ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "oblique corner supports must be planar and perpendicular to the edge");
    const double CosTheta = ScalarCriteria::Clamp(SupportA.Dot(SupportB), -1.0, 1.0);
    const double Theta = std::acos(CosTheta);
    if (Theta <= ScalarCriteria::AngularTolerance || Theta >= ScalarCriteria::Pi - ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "oblique corner support angle must be strictly between zero and pi");
    const double HalfTheta = 0.5 * Theta;
    const double SinHalfTheta = std::sin(HalfTheta);
    const double TangentDistance = Specification.Radius * std::cos(HalfTheta) / SinHalfTheta;
    if (TangentDistance >= Specification.WidthA - ScalarCriteria::MergeTolerance ||
        TangentDistance >= Specification.WidthB - ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "oblique corner radius consumes a finite support extent");
    const Vec3 Origin = Specification.Origin;
    const Vec3 TangentA = Origin + SupportA * TangentDistance;
    const Vec3 TangentB = Origin + SupportB * TangentDistance;
    const Vec3 OuterA = Origin + SupportA * Specification.WidthA;
    const Vec3 OuterB = Origin + SupportB * Specification.WidthB;
    const Vec3 Centre = Origin + (SupportA + SupportB).Normalised() * (Specification.Radius / SinHalfTheta);
    const Vec3 RadialA = (TangentA - Centre).Normalised();
    const Vec3 RadialB = (TangentB - Centre).Normalised();
    if ((RadialA + RadialB).Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "oblique corner arc midpoint is degenerate");
    const Vec3 ArcMiddle = Centre + (RadialA + RadialB).Normalised() * Specification.Radius;
    Deliver<NurbsCurve> WallA = NurbsCurve::Line(TangentA, OuterA);
    Deliver<NurbsCurve> Outer = NurbsCurve::Line(OuterA, OuterB);
    Deliver<NurbsCurve> WallB = NurbsCurve::Line(OuterB, TangentB);
    Deliver<NurbsCurve> Fillet = NurbsCurve::ArcThreePoints(TangentB, ArcMiddle, TangentA);
    if (!WallA || !Outer || !WallB || !Fillet)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "oblique corner profile is degenerate");
    Deliver<NurbsSurface> FaceA = NurbsSurface::Extrusion(WallA.Payload, Axis, Specification.Length);
    Deliver<NurbsSurface> OuterFace = NurbsSurface::Extrusion(Outer.Payload, Axis, Specification.Length);
    Deliver<NurbsSurface> FaceB = NurbsSurface::Extrusion(WallB.Payload, Axis, Specification.Length);
    Deliver<NurbsSurface> FilletFace = NurbsSurface::Extrusion(Fillet.Payload, Axis, Specification.Length);
    if (!FaceA || !OuterFace || !FaceB || !FilletFace)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "oblique corner extrusion surfaces are degenerate");
    Deliver<BrepBody> Result = BrepBody::Sew({ FaceA.Payload, OuterFace.Payload, FaceB.Payload, FilletFace.Payload },
                                              ScalarCriteria::MergeTolerance, true);
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason,
                                                   "oblique corner surfaces could not be sewn");
    const double SharpArea = 0.5 * Specification.WidthA * Specification.WidthB * std::sin(Theta);
    const double RemovedArea = 0.5 * std::sin(Theta) *
        (TangentDistance * TangentDistance + Specification.Radius * Specification.Radius) -
        0.5 * Specification.Radius * Specification.Radius * (ScalarCriteria::Pi - Theta);
    const double ExpectedVolume = Specification.Length * (SharpArea - RemovedArea);
    const BodyReport Report = Result.Payload.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 ||
        Result.Payload.Vertices.size() != 8 || Result.Payload.Edges.size() != 12 ||
        Result.Payload.Coedges.size() != 24 || Result.Payload.Loops.size() != 6 || Result.Payload.Faces.size() != 6)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                                          "oblique corner did not reach exact capped topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence,
                                          "oblique corner volume failed analytic acceptance");
    return Result;
}

Deliver<BrepBody> BlendSolver::ReconstructObliquePartialEdgeFillet(
    const ObliquePartialEdgeFilletSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Origin.X) || !std::isfinite(Specification.Origin.Y) ||
        !std::isfinite(Specification.Origin.Z) || !std::isfinite(Specification.Length) ||
        !std::isfinite(Specification.Start) || !std::isfinite(Specification.End) ||
        !std::isfinite(Specification.WidthA) || !std::isfinite(Specification.WidthB) ||
        !std::isfinite(Specification.Radius) || Specification.Length <= ScalarCriteria::MergeTolerance ||
        Specification.WidthA <= ScalarCriteria::MergeTolerance || Specification.WidthB <= ScalarCriteria::MergeTolerance ||
        Specification.Radius <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "oblique partial-edge dimensions and radius must be finite and positive");
    if (Specification.Start <= ScalarCriteria::MergeTolerance ||
        Specification.End >= Specification.Length - ScalarCriteria::MergeTolerance ||
        Specification.End <= Specification.Start + ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "oblique partial-edge interval must be strict interior");
    if (Specification.EdgeAxis.Length() <= ScalarCriteria::GeometricTolerance ||
        Specification.SupportA.Length() <= ScalarCriteria::GeometricTolerance ||
        Specification.SupportB.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "oblique partial-edge frame contains a degenerate direction");
    const Vec3 Axis = Specification.EdgeAxis.Normalised();
    const Vec3 SupportA = Specification.SupportA.Normalised();
    const Vec3 SupportB = Specification.SupportB.Normalised();
    if (std::fabs(Axis.Dot(SupportA)) > ScalarCriteria::AngularTolerance ||
        std::fabs(Axis.Dot(SupportB)) > ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "oblique partial-edge supports must be planar and perpendicular to the edge");
    const double Theta = std::acos(ScalarCriteria::Clamp(SupportA.Dot(SupportB), -1.0, 1.0));
    if (Theta <= ScalarCriteria::AngularTolerance || Theta >= ScalarCriteria::Pi - ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "oblique partial-edge support angle must be strictly between zero and pi");
    const double HalfTheta = 0.5 * Theta;
    const double TangentDistance = Specification.Radius * std::cos(HalfTheta) / std::sin(HalfTheta);
    if (TangentDistance >= Specification.WidthA - ScalarCriteria::MergeTolerance ||
        TangentDistance >= Specification.WidthB - ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "oblique partial-edge radius consumes a finite support extent");
    const Vec3 Origin = Specification.Origin;
    const Vec3 TangentA = Origin + SupportA * TangentDistance;
    const Vec3 TangentB = Origin + SupportB * TangentDistance;
    const Vec3 OuterA = Origin + SupportA * Specification.WidthA;
    const Vec3 OuterB = Origin + SupportB * Specification.WidthB;
    const Vec3 Centre = Origin + (SupportA + SupportB).Normalised() * (Specification.Radius / std::sin(HalfTheta));
    const Vec3 RadialA = (TangentA - Centre).Normalised();
    const Vec3 RadialB = (TangentB - Centre).Normalised();
    if ((RadialA + RadialB).Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "oblique partial-edge arc midpoint is degenerate");
    const Vec3 ArcMiddle = Centre + (RadialA + RadialB).Normalised() * Specification.Radius;
    Deliver<NurbsCurve> CornerA = NurbsCurve::Line(Origin, TangentA);
    Deliver<NurbsCurve> WallA = NurbsCurve::Line(TangentA, OuterA);
    Deliver<NurbsCurve> Outer = NurbsCurve::Line(OuterA, OuterB);
    Deliver<NurbsCurve> WallB = NurbsCurve::Line(OuterB, TangentB);
    Deliver<NurbsCurve> CornerB = NurbsCurve::Line(TangentB, Origin);
    Deliver<NurbsCurve> Arc = NurbsCurve::ArcThreePoints(TangentA, ArcMiddle, TangentB);
    if (!CornerA || !WallA || !Outer || !WallB || !CornerB || !Arc)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "oblique partial-edge profile is degenerate");
    const auto TranslateAlongEdge = [&](const NurbsCurve& Curve, double Distance)
    {
        return Curve.Transformed(Mat4::Translation(Axis * Distance));
    };
    const auto AddLineFace = [&](const NurbsCurve& Curve, double Distance, double Span,
                                  std::vector<NurbsSurface>& Surfaces) -> bool
    {
        const NurbsCurve Section = TranslateAlongEdge(Curve, Distance);
        const Vec3 Direction = (Section.EndPoint() - Section.StartPoint()).Normalised();
        Deliver<NurbsSurface> Surface = NurbsSurface::Plane(Section.StartPoint(), Axis, Direction,
                                                             Span, Section.Length());
        if (!Surface) return false;
        Surfaces.push_back(std::move(Surface.Payload));
        return true;
    };
    const auto AddArcFace = [&](const NurbsCurve& Curve, double Distance, double Span,
                                std::vector<NurbsSurface>& Surfaces) -> bool
    {
        Deliver<NurbsSurface> Surface = NurbsSurface::Extrusion(TranslateAlongEdge(Curve, Distance), Axis, Span);
        if (!Surface) return false;
        Surfaces.push_back(std::move(Surface.Payload));
        return true;
    };
    std::vector<NurbsSurface> Surfaces;
    if (!AddLineFace(WallA.Payload, 0.0, Specification.Start, Surfaces) ||
        !AddLineFace(WallA.Payload, Specification.Start, Specification.End - Specification.Start, Surfaces) ||
        !AddLineFace(WallA.Payload, Specification.End, Specification.Length - Specification.End, Surfaces) ||
        !AddLineFace(Outer.Payload, 0.0, Specification.Start, Surfaces) ||
        !AddLineFace(Outer.Payload, Specification.Start, Specification.End - Specification.Start, Surfaces) ||
        !AddLineFace(Outer.Payload, Specification.End, Specification.Length - Specification.End, Surfaces) ||
        !AddLineFace(WallB.Payload, 0.0, Specification.Start, Surfaces) ||
        !AddLineFace(WallB.Payload, Specification.Start, Specification.End - Specification.Start, Surfaces) ||
        !AddLineFace(WallB.Payload, Specification.End, Specification.Length - Specification.End, Surfaces) ||
        !AddLineFace(CornerA.Payload, 0.0, Specification.Start, Surfaces) ||
        !AddLineFace(CornerA.Payload, Specification.End, Specification.Length - Specification.End, Surfaces) ||
        !AddLineFace(CornerB.Payload, 0.0, Specification.Start, Surfaces) ||
        !AddLineFace(CornerB.Payload, Specification.End, Specification.Length - Specification.End, Surfaces) ||
        !AddArcFace(Arc.Payload, Specification.Start, Specification.End - Specification.Start, Surfaces))
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "oblique partial-edge support surfaces are degenerate");
    const auto AddTransition = [&](double Distance) -> bool
    {
        const Mat4 Transform = Mat4::Translation(Axis * Distance);
        Deliver<NurbsSurface> Cap = SkinSolver::CoonsPatch({ CornerA.Payload.Transformed(Transform),
                                                              Arc.Payload.Transformed(Transform),
                                                              CornerB.Payload.Transformed(Transform) });
        if (!Cap) return false;
        Surfaces.push_back(std::move(Cap.Payload));
        return true;
    };
    if (!AddTransition(Specification.Start) || !AddTransition(Specification.End))
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "oblique partial-edge transition cap is degenerate");
    const auto AddEndCap = [&](double Distance) -> bool
    {
        const Mat4 Transform = Mat4::Translation(Axis * Distance);
        Deliver<NurbsCurve> Chord = NurbsCurve::Line(TangentA, TangentB);
        if (!Chord) return false;
        Deliver<NurbsSurface> CornerFill = SkinSolver::CoonsPatch({ CornerA.Payload.Transformed(Transform),
                                                                      Chord.Payload.Transformed(Transform),
                                                                      CornerB.Payload.Transformed(Transform) });
        Deliver<NurbsSurface> OuterFill = SkinSolver::CoonsPatch({ WallA.Payload.Transformed(Transform),
                                                                    Outer.Payload.Transformed(Transform),
                                                                    WallB.Payload.Transformed(Transform),
                                                                    Chord.Payload.Reversed().Transformed(Transform) });
        if (!CornerFill || !OuterFill) return false;
        Surfaces.push_back(std::move(CornerFill.Payload));
        Surfaces.push_back(std::move(OuterFill.Payload));
        return true;
    };
    if (!AddEndCap(0.0) || !AddEndCap(Specification.Length))
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "oblique partial-edge finite cap is degenerate");
    Deliver<BrepBody> Result = BrepBody::Sew(Surfaces, ScalarCriteria::MergeTolerance, false);
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason,
                                                   "oblique partial-edge surfaces could not be sewn");
    const double SharpArea = 0.5 * Specification.WidthA * Specification.WidthB * std::sin(Theta);
    const double RemovedArea = 0.5 * std::sin(Theta) *
        (TangentDistance * TangentDistance + Specification.Radius * Specification.Radius) -
        0.5 * Specification.Radius * Specification.Radius * (ScalarCriteria::Pi - Theta);
    const double ExpectedVolume = Specification.Length * SharpArea -
        (Specification.End - Specification.Start) * RemovedArea;
    const BodyReport Report = Result.Payload.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                                          "oblique partial-edge blend did not reach closed manifold topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence,
                                          "oblique partial-edge volume failed analytic acceptance");
    return Result;
}

Deliver<BrepBody> BlendSolver::ReconstructObliqueQuadraticPartialEdgeFillet(
    const ObliqueQuadraticPartialEdgeFilletSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Origin.X) || !std::isfinite(Specification.Origin.Y) ||
        !std::isfinite(Specification.Origin.Z) || !std::isfinite(Specification.Length) ||
        !std::isfinite(Specification.Start) || !std::isfinite(Specification.End) ||
        !std::isfinite(Specification.WidthA) || !std::isfinite(Specification.WidthB) ||
        Specification.Length <= ScalarCriteria::MergeTolerance ||
        Specification.WidthA <= ScalarCriteria::MergeTolerance ||
        Specification.WidthB <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "oblique quadratic partial-edge dimensions must be finite and positive");
    if (Specification.Start <= ScalarCriteria::MergeTolerance ||
        Specification.End >= Specification.Length - ScalarCriteria::MergeTolerance ||
        Specification.End <= Specification.Start + ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "oblique quadratic partial-edge interval must be strict interior");
    if (!Specification.RadiusLaw.Positive() || !Specification.RadiusLaw.Nonlinear())
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "oblique quadratic partial-edge route requires a genuinely nonlinear positive radius law");
    if (Specification.EdgeAxis.Length() <= ScalarCriteria::GeometricTolerance ||
        Specification.SupportA.Length() <= ScalarCriteria::GeometricTolerance ||
        Specification.SupportB.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "oblique quadratic partial-edge frame contains a degenerate direction");
    const Vec3 Axis = Specification.EdgeAxis.Normalised();
    const Vec3 SupportA = Specification.SupportA.Normalised();
    const Vec3 SupportB = Specification.SupportB.Normalised();
    if (std::fabs(Axis.Dot(SupportA)) > ScalarCriteria::AngularTolerance ||
        std::fabs(Axis.Dot(SupportB)) > ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "oblique quadratic partial-edge supports must be perpendicular to the edge");
    const double Theta = std::acos(ScalarCriteria::Clamp(SupportA.Dot(SupportB), -1.0, 1.0));
    if (Theta <= ScalarCriteria::AngularTolerance || Theta >= ScalarCriteria::Pi - ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "oblique quadratic partial-edge support angle must be strictly between zero and pi");
    const double HalfTheta = 0.5 * Theta;
    const double CotHalf = std::cos(HalfTheta) / std::sin(HalfTheta);
    const Vec3 Origin = Specification.Origin;
    const Vec3 OuterA = Origin + SupportA * Specification.WidthA;
    const Vec3 OuterB = Origin + SupportB * Specification.WidthB;
    const auto RadiusAt = [&](double T) noexcept { return Specification.RadiusLaw.Radius(T); };
    const auto TangentDistanceAt = [&](double T) noexcept { return RadiusAt(T) * CotHalf; };
    for (int I = 0; I <= 64; ++I)
    {
        const double T = static_cast<double>(I) / 64.0;
        const double Radius = RadiusAt(T);
        const double TangentDistance = TangentDistanceAt(T);
        if (!std::isfinite(Radius) || !std::isfinite(TangentDistance) ||
            Radius <= ScalarCriteria::MergeTolerance ||
            TangentDistance >= Specification.WidthA - ScalarCriteria::MergeTolerance ||
            TangentDistance >= Specification.WidthB - ScalarCriteria::MergeTolerance)
            return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                              "oblique quadratic partial-edge radius consumes a finite support extent");
    }
    const double S = Specification.Start;
    const double E = Specification.End;
    const double M = 0.5 * (S + E);
    const double R0 = RadiusAt(0.0);
    const double Rm = RadiusAt(0.5);
    const double R1 = RadiusAt(1.0);
    const auto Point = [&](double Along, Vec3 Planar) noexcept { return Origin + Axis * Along + Planar; };
    const auto TangentA = [&](double Along, double Radius) noexcept
    {
        return Point(Along, SupportA * (Radius * CotHalf));
    };
    const auto TangentB = [&](double Along, double Radius) noexcept
    {
        return Point(Along, SupportB * (Radius * CotHalf));
    };
    const auto ArcMiddle = [&](double Along, double Radius) noexcept
    {
        const Vec3 Centre = Origin + Axis * Along + (SupportA + SupportB).Normalised() * (Radius / std::sin(HalfTheta));
        const Vec3 RadialA = (TangentA(Along, Radius) - Centre).Normalised();
        const Vec3 RadialB = (TangentB(Along, Radius) - Centre).Normalised();
        return Centre + (RadialA + RadialB).Normalised() * Radius;
    };
    const auto Line = [](Vec3 A, Vec3 B) -> Deliver<NurbsCurve> { return NurbsCurve::Line(A, B); };
    const auto CornerAAt = [&](double Along, double Radius) { return Line(Point(Along, { 0, 0, 0 }), TangentA(Along, Radius)); };
    const auto WallAAt = [&](double Along, double Radius) { return Line(TangentA(Along, Radius), Point(Along, OuterA - Origin)); };
    const auto OuterAt = [&](double Along) { return Line(Point(Along, OuterA - Origin), Point(Along, OuterB - Origin)); };
    const auto WallBAt = [&](double Along, double Radius) { return Line(Point(Along, OuterB - Origin), TangentB(Along, Radius)); };
    const auto CornerBAt = [&](double Along, double Radius) { return Line(TangentB(Along, Radius), Point(Along, { 0, 0, 0 })); };
    const auto ArcAt = [&](double Along, double Radius)
    {
        return NurbsCurve::ArcThreePoints(TangentA(Along, Radius), ArcMiddle(Along, Radius), TangentB(Along, Radius));
    };
    std::vector<NurbsSurface> Surfaces;
    const auto AddLineStrip = [&](const Deliver<NurbsCurve>& Profile, double Begin, double Span) -> bool
    {
        if (!Profile) return false;
        const NurbsCurve Section = Profile.Payload.Transformed(Mat4::Translation(Axis * Begin));
        const Vec3 Direction = (Section.EndPoint() - Section.StartPoint()).Normalised();
        Deliver<NurbsSurface> Surface = NurbsSurface::Plane(Section.StartPoint(), Axis, Direction,
                                                             Span, Section.Length());
        if (!Surface) return false;
        Surfaces.push_back(std::move(Surface.Payload));
        return true;
    };
    const auto LoftFixedStations = [&](std::vector<NurbsCurve> Sections) -> Deliver<NurbsSurface>
    {
        if (Sections.size() != 3) return Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput, "oblique quadratic loft requires three stations");
        std::vector<NurbsCurve> Rows;
        Rows.reserve(Sections.size());
        for (NurbsCurve& Section : Sections)
            Rows.push_back((Section.Degree < 2 ? Section.Elevated(2) : Section).Reparameterised(0.0, 1.0));
        const int CountU = Rows.front().PoleCount();
        for (const NurbsCurve& Row : Rows)
            if (Row.PoleCount() != CountU || Row.Knots != Rows.front().Knots)
                return Deliver<NurbsSurface>::Reject(RefusalReason::NoConvergence, "oblique quadratic loft stations are incompatible");
        const std::vector<double> ParametersV{ 0.0, 0.5, 1.0 };
        std::vector<NurbsCurve> Columns;
        Columns.reserve(static_cast<size_t>(CountU));
        for (int I = 0; I < CountU; ++I)
        {
            std::vector<Vec4> Through;
            Through.reserve(Rows.size());
            for (const NurbsCurve& Row : Rows) Through.push_back(Row.Poles[I]);
            Deliver<NurbsCurve> Column = NurbsCurve::InterpolateHomogeneous(Through, 2, &ParametersV);
            if (!Column) return Deliver<NurbsSurface>::Reject(Column.Denial.Reason, Column.Denial.Detail);
            Columns.push_back(std::move(Column.Payload));
        }
        std::vector<Vec4> Poles(static_cast<size_t>(CountU) * Columns.front().PoleCount());
        for (int I = 0; I < CountU; ++I)
            for (int J = 0; J < Columns[I].PoleCount(); ++J)
                Poles[static_cast<size_t>(I) * Columns[I].PoleCount() + J] = Columns[I].Poles[J];
        return NurbsSurface::Build(2, 2, CountU, Columns.front().PoleCount(), std::move(Poles),
                                    Rows.front().Knots, Columns.front().Knots);
    };
    const auto AddLoft = [&](std::vector<Deliver<NurbsCurve>> Sections) -> bool
    {
        std::vector<NurbsCurve> Curves;
        Curves.reserve(Sections.size());
        for (const Deliver<NurbsCurve>& Section : Sections)
        {
            if (!Section) return false;
            Curves.push_back(Section.Payload);
        }
        Deliver<NurbsSurface> Surface = LoftFixedStations(std::move(Curves));
        if (!Surface) return false;
        Surface.Payload.Classification = SurfaceClassification::Loft;
        Surfaces.push_back(std::move(Surface.Payload));
        return true;
    };
    if (!AddLineStrip(CornerAAt(0.0, R0), 0.0, S) ||
        !AddLineStrip(WallAAt(0.0, R0), 0.0, S) ||
        !AddLineStrip(OuterAt(0.0), 0.0, S) ||
        !AddLineStrip(WallBAt(0.0, R0), 0.0, S) ||
        !AddLineStrip(CornerBAt(0.0, R0), 0.0, S) ||
        !AddLoft({ WallAAt(S, R0), WallAAt(M, Rm), WallAAt(E, R1) }) ||
        !AddLoft({ OuterAt(S), OuterAt(M), OuterAt(E) }) ||
        !AddLoft({ WallBAt(S, R0), WallBAt(M, Rm), WallBAt(E, R1) }) ||
        !AddLoft({ ArcAt(S, R0), ArcAt(M, Rm), ArcAt(E, R1) }) ||
        !AddLineStrip(CornerAAt(0.0, R1), E, Specification.Length - E) ||
        !AddLineStrip(WallAAt(0.0, R1), E, Specification.Length - E) ||
        !AddLineStrip(OuterAt(0.0), E, Specification.Length - E) ||
        !AddLineStrip(WallBAt(0.0, R1), E, Specification.Length - E) ||
        !AddLineStrip(CornerBAt(0.0, R1), E, Specification.Length - E))
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "oblique quadratic partial-edge support surfaces are degenerate");
    const auto AddTransition = [&](double Along, double Radius) -> bool
    {
        Deliver<NurbsCurve> CornerA = CornerAAt(Along, Radius);
        Deliver<NurbsCurve> Arc = ArcAt(Along, Radius);
        Deliver<NurbsCurve> CornerB = CornerBAt(Along, Radius);
        if (!CornerA || !Arc || !CornerB) return false;
        Deliver<NurbsSurface> Cap = SkinSolver::CoonsPatch({ CornerA.Payload, Arc.Payload, CornerB.Payload });
        if (!Cap) return false;
        Surfaces.push_back(std::move(Cap.Payload));
        return true;
    };
    if (!AddTransition(S, R0) || !AddTransition(E, R1))
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "oblique quadratic partial-edge transition cap is degenerate");
    const auto AddEndCap = [&](double Along, double Radius) -> bool
    {
        const Mat4 Transform = Mat4::Translation(Axis * Along);
        Deliver<NurbsCurve> CornerA = CornerAAt(0.0, Radius);
        Deliver<NurbsCurve> WallA = WallAAt(0.0, Radius);
        Deliver<NurbsCurve> Outer = OuterAt(0.0);
        Deliver<NurbsCurve> WallB = WallBAt(0.0, Radius);
        Deliver<NurbsCurve> CornerB = CornerBAt(0.0, Radius);
        Deliver<NurbsCurve> Chord = Line(TangentA(0.0, Radius), TangentB(0.0, Radius));
        if (!CornerA || !WallA || !Outer || !WallB || !CornerB || !Chord) return false;
        Deliver<NurbsSurface> CornerFill = SkinSolver::CoonsPatch({ CornerA.Payload.Transformed(Transform),
                                                                      Chord.Payload.Transformed(Transform),
                                                                      CornerB.Payload.Transformed(Transform) });
        Deliver<NurbsSurface> OuterFill = SkinSolver::CoonsPatch({ WallA.Payload.Transformed(Transform),
                                                                    Outer.Payload.Transformed(Transform),
                                                                    WallB.Payload.Transformed(Transform),
                                                                    Chord.Payload.Reversed().Transformed(Transform) });
        if (!CornerFill || !OuterFill) return false;
        Surfaces.push_back(std::move(CornerFill.Payload));
        Surfaces.push_back(std::move(OuterFill.Payload));
        return true;
    };
    if (!AddEndCap(0.0, R0) || !AddEndCap(Specification.Length, R1))
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "oblique quadratic partial-edge finite cap is degenerate");
    Deliver<BrepBody> Result = BrepBody::Sew(Surfaces, ScalarCriteria::MergeTolerance, false);
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason,
                                                   "oblique quadratic partial-edge surfaces could not be sewn");
    const double SharpArea = 0.5 * Specification.WidthA * Specification.WidthB * std::sin(Theta);
    const double RemovedCoefficient = 0.5 * std::sin(Theta) * (CotHalf * CotHalf + 1.0) -
        0.5 * (ScalarCriteria::Pi - Theta);
    const double ExpectedVolume = Specification.Length * SharpArea -
        RemovedCoefficient * Specification.RadiusLaw.IntegratedSquare(E - S);
    const BodyReport Report = Result.Payload.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                                          "oblique quadratic partial-edge blend did not reach closed manifold topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence,
                                          "oblique quadratic partial-edge volume failed analytic acceptance");
    return Result;
}

Deliver<BrepBody> BlendSolver::ReconstructConeApexChamfer(
    const ConeApexChamferSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Base.X) || !std::isfinite(Specification.Base.Y) ||
        !std::isfinite(Specification.Base.Z) || !std::isfinite(Specification.BaseRadius) ||
        !std::isfinite(Specification.Height) || !std::isfinite(Specification.SetBack) ||
        Specification.BaseRadius <= Tol || Specification.Height <= Tol || Specification.SetBack <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "cone-apex chamfer dimensions and set-back must be finite and positive");
    if (!std::isfinite(Specification.Axis.X) || !std::isfinite(Specification.Axis.Y) ||
        !std::isfinite(Specification.Axis.Z) || Specification.Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "cone-apex chamfer axis is degenerate");
    const Vec3 Axis = Specification.Axis.Normalised();
    const double R = Specification.BaseRadius;
    const double H = Specification.Height;
    const double SetBack = Specification.SetBack;
    if (SetBack >= H - Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "apex chamfer set-back consumes the native cone height");
    const double RetainedHeight = H - SetBack;
    const double CapRadius = R * RetainedHeight / H;
    if (!std::isfinite(CapRadius) || CapRadius <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "apex chamfer leaves a degenerate planar cap");

    Deliver<BrepBody> Result = BrepBody::Cone(Specification.Base, Axis, R, CapRadius, RetainedHeight);
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason,
                                                   "native cone apex chamfer could not be reconstructed");
    const BodyReport Report = Result.Payload.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 ||
        Result.Payload.Vertices.size() != 2 || Result.Payload.Edges.size() != 3 ||
        Result.Payload.Coedges.size() != 6 || Result.Payload.Loops.size() != 3 ||
        Result.Payload.Faces.size() != 3)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                                          "native cone apex chamfer did not reach V2/E3/C6/L3/F3 topology");
    const double ExpectedVolume = ScalarCriteria::Pi * RetainedHeight *
        (R * R + R * CapRadius + CapRadius * CapRadius) / 3.0;
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence,
                                          "native cone apex chamfer volume failed analytic acceptance");
    return Result;
}

Deliver<ConeApexChamferSpecification> BlendSolver::ClassifyConeApexChamferVertex(
    const BrepBody& Body, int Vertex, double SetBack) noexcept
{
    if (!std::isfinite(SetBack) || SetBack <= ScalarCriteria::MergeTolerance)
        return Deliver<ConeApexChamferSpecification>::Reject(RefusalReason::DegenerateInput,
                                                              "cone-apex vertex chamfer set-back must be finite and positive");
    const BodyReport Report = Body.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 ||
        Body.Vertices.size() != 2 || Body.Edges.size() != 2 || Body.Coedges.size() != 4 ||
        Body.Loops.size() != 2 || Body.Faces.size() != 2)
        return Deliver<ConeApexChamferSpecification>::Reject(RefusalReason::NonManifold,
                                                              "source is not the bounded canonical native cone topology");
    if (Vertex < 0 || Vertex >= static_cast<int>(Body.Vertices.size()))
        return Deliver<ConeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                              "selected cone-apex chamfer vertex is out of range");

    int ConeFace = -1, PlaneFace = -1;
    for (size_t I = 0; I < Body.Faces.size(); ++I)
    {
        const BrepFace& Face = Body.Faces[I];
        if (Face.Loops.size() != 1)
            return Deliver<ConeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                  "native cone face has an unsupported loop count");
        if (Face.Surface.Classification == SurfaceClassification::Cone)
        {
            if (ConeFace >= 0) return Deliver<ConeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                                       "source has multiple cone faces");
            ConeFace = static_cast<int>(I);
        }
        else if (Face.Surface.Classification == SurfaceClassification::Plane)
        {
            if (PlaneFace >= 0) return Deliver<ConeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                                       "source has multiple planar cap faces");
            PlaneFace = static_cast<int>(I);
        }
        else return Deliver<ConeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                    "source is not a native cone and planar base pair");
    }
    if (ConeFace < 0 || PlaneFace < 0)
        return Deliver<ConeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                              "native cone is missing its cone or base face");
    const NurbsSurface& Cone = Body.Faces[ConeFace].Surface;
    if (!std::isfinite(Cone.Origin.X) || !std::isfinite(Cone.Origin.Y) || !std::isfinite(Cone.Origin.Z) ||
        !std::isfinite(Cone.RadiusMajor) || !std::isfinite(Cone.RadiusMinor) ||
        Cone.RadiusMajor <= ScalarCriteria::MergeTolerance ||
        std::fabs(Cone.RadiusMinor) > ScalarCriteria::GeometricTolerance ||
        Cone.Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<ConeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                              "native cone does not have a positive base and zero apex radius");

    const Vec3 Axis = Cone.Axis.Normalised();
    const Vec3 Base = Cone.Origin;
    int Apex = -1, BaseVertex = -1;
    double Height = 0.0;
    for (size_t I = 0; I < Body.Vertices.size(); ++I)
    {
        const Vec3 Delta = Body.Vertices[I].Point - Base;
        const double Along = Delta.Dot(Axis);
        const double Radial = (Delta - Axis * Along).Length();
        if (std::fabs(Along) <= ScalarCriteria::GeometricTolerance &&
            std::fabs(Radial - Cone.RadiusMajor) <= ScalarCriteria::GeometricTolerance *
                                                     std::max(1.0, Cone.RadiusMajor))
        {
            if (BaseVertex >= 0) return Deliver<ConeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                                         "native cone has multiple base vertices");
            BaseVertex = static_cast<int>(I);
        }
        else if (Along > ScalarCriteria::MergeTolerance &&
                 Radial <= ScalarCriteria::GeometricTolerance * std::max(1.0, Along))
        {
            if (Apex >= 0) return Deliver<ConeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                                  "native cone has multiple axis apex vertices");
            Apex = static_cast<int>(I);
            Height = Along;
        }
        else return Deliver<ConeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                    "native cone vertex geometry is not a base rim and apex pair");
    }
    if (Apex < 0 || BaseVertex < 0 || Height <= ScalarCriteria::MergeTolerance || Vertex != Apex)
        return Deliver<ConeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                              "selected vertex is not the unique native cone apex");

    int Rim = 0, Seam = 0;
    for (const BrepEdge& EdgeData : Body.Edges)
    {
        if (EdgeData.Coedges.size() != 2)
            return Deliver<ConeApexChamferSpecification>::Reject(RefusalReason::NonManifold,
                                                                  "native cone edge is not manifold");
        if (EdgeData.Closed() && EdgeData.VertexStart == BaseVertex &&
            EdgeData.Curve.Classification == CurveClassification::Circle && EdgeData.Curve.Degree == 2 &&
            EdgeData.Curve.Rational()) ++Rim;
        else if (!EdgeData.Closed() && EdgeData.Curve.Classification == CurveClassification::Line &&
                 EdgeData.Curve.Degree == 1 &&
                 ((EdgeData.VertexStart == BaseVertex && EdgeData.VertexEnd == Apex) ||
                  (EdgeData.VertexStart == Apex && EdgeData.VertexEnd == BaseVertex))) ++Seam;
        else return Deliver<ConeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                    "native cone has unsupported edge geometry");
    }
    Vec3 PlaneNormal;
    if (Rim != 1 || Seam != 1 || !PlanarNormal(Body, PlaneFace, PlaneNormal) ||
        std::fabs(std::fabs(PlaneNormal.Dot(Axis)) - 1.0) > ScalarCriteria::AngularTolerance)
        return Deliver<ConeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                              "native cone base cap or rim is not exact");

    ConeApexChamferSpecification Specification;
    Specification.Base = Base;
    Specification.Axis = Axis;
    Specification.BaseRadius = Cone.RadiusMajor;
    Specification.Height = Height;
    Specification.SetBack = SetBack;
    const Deliver<BrepBody> Feasible = ReconstructConeApexChamfer(Specification);
    if (!Feasible) return Deliver<ConeApexChamferSpecification>::Reject(Feasible.Denial.Reason, Feasible.Denial.Detail);
    return Deliver<ConeApexChamferSpecification>::Accept(std::move(Specification));
}

Deliver<BrepBody> BlendSolver::ReconstructPartialConeApexChamfer(
    const PartialConeApexChamferSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Base.X) || !std::isfinite(Specification.Base.Y) ||
        !std::isfinite(Specification.Base.Z) || !std::isfinite(Specification.BaseRadius) ||
        !std::isfinite(Specification.Height) || !std::isfinite(Specification.SetBack) ||
        !std::isfinite(Specification.SweepAngle) || Specification.BaseRadius <= Tol ||
        Specification.Height <= Tol || Specification.SetBack <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "partial cone-apex chamfer dimensions must be finite and positive");
    if (!std::isfinite(Specification.Axis.X) || !std::isfinite(Specification.Axis.Y) ||
        !std::isfinite(Specification.Axis.Z) || Specification.Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial cone-apex chamfer axis is degenerate");
    if (Specification.SweepAngle <= ScalarCriteria::SweepTolerance ||
        Specification.SweepAngle >= ScalarCriteria::TwoPi - ScalarCriteria::SweepTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "partial cone-apex chamfer sweep must be strictly between zero and a full turn");
    const Vec3 Axis = Specification.Axis.Normalised();
    const double R = Specification.BaseRadius;
    const double H = Specification.Height;
    const double SetBack = Specification.SetBack;
    if (SetBack >= H - Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "partial apex chamfer set-back consumes the native cone height");
    const double RetainedHeight = H - SetBack;
    const double CapRadius = R * RetainedHeight / H;
    if (!std::isfinite(CapRadius) || CapRadius <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "partial apex chamfer leaves a degenerate planar cap");
    Vec3 Radial = Workplane::FromNormal(Specification.Base, Axis).AxisX.Normalised();
    if (Radial.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial apex chamfer radial frame is degenerate");
    const Vec3 Base = Specification.Base;
    const Vec3 Top = Base + Axis * RetainedHeight;
    const Vec3 BaseRim = Base + Radial * R;
    const Vec3 CapRim = Top + Radial * CapRadius;

    Deliver<NurbsCurve> BaseProfile = NurbsCurve::Line(Base, BaseRim);
    Deliver<NurbsCurve> ConeProfile = NurbsCurve::Line(BaseRim, CapRim);
    Deliver<NurbsCurve> CapProfile = NurbsCurve::Line(CapRim, Top);
    Deliver<NurbsCurve> AxisProfile = NurbsCurve::Line(Top, Base);
    if (!BaseProfile || !ConeProfile || !CapProfile || !AxisProfile)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "partial apex chamfer meridian profiles are degenerate");

    Deliver<NurbsSurface> BaseSurface = NurbsSurface::Revolution(BaseProfile.Payload, Base, Axis, Specification.SweepAngle);
    Deliver<NurbsSurface> ConeSurface = NurbsSurface::Revolution(ConeProfile.Payload, Base, Axis, Specification.SweepAngle);
    Deliver<NurbsSurface> CapSurface = NurbsSurface::Revolution(CapProfile.Payload, Base, Axis, Specification.SweepAngle);
    if (!BaseSurface || !ConeSurface || !CapSurface)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "partial apex chamfer revolution surfaces are degenerate");
    BaseSurface.Payload.Classification = SurfaceClassification::Plane;
    BaseSurface.Payload.Origin = Base; BaseSurface.Payload.Axis = Axis;
    ConeSurface.Payload.Classification = SurfaceClassification::Cone;
    ConeSurface.Payload.Origin = Base; ConeSurface.Payload.Axis = Axis;
    ConeSurface.Payload.RadiusMajor = R; ConeSurface.Payload.RadiusMinor = CapRadius;
    ConeSurface.Payload.HalfAngle = std::atan2(R - CapRadius, RetainedHeight);
    CapSurface.Payload.Classification = SurfaceClassification::Plane;
    CapSurface.Payload.Origin = Top; CapSurface.Payload.Axis = Axis;

    auto MeridianCap = [&](const Mat4& Transform) -> Deliver<NurbsSurface>
    {
        const NurbsCurve D = BaseProfile.Payload.Transformed(Transform);
        const NurbsCurve C = ConeProfile.Payload.Transformed(Transform);
        const NurbsCurve S = CapProfile.Payload.Transformed(Transform);
        const NurbsCurve A = AxisProfile.Payload.Transformed(Transform);
        if (D.Validate() || C.Validate() || S.Validate() || A.Validate())
            return Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput,
                                                  "partial apex chamfer radial cap is invalid");
        return SkinSolver::CoonsPatch({ D, C, S, A });
    };
    const Mat4 StartTransform = Mat4::Identity();
    const Mat4 EndTransform = Mat4::Translation(Base) *
        Mat4::Rotation(Axis, Specification.SweepAngle) * Mat4::Translation(-Base);
    Deliver<NurbsSurface> StartCap = MeridianCap(StartTransform);
    Deliver<NurbsSurface> EndCap = StartCap
        ? Deliver<NurbsSurface>::Accept(StartCap.Payload.Transformed(EndTransform))
        : Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput, "partial apex chamfer end cap is invalid");
    if (!StartCap || !EndCap)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "partial apex chamfer radial caps could not be constructed");

    Deliver<BrepBody> Result = BrepBody::Sew({ BaseSurface.Payload, ConeSurface.Payload, CapSurface.Payload,
                                                StartCap.Payload, EndCap.Payload }, Tol, true);
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason,
                                                   "partial apex chamfer surfaces could not be sewn");
    const Vec3 EndRadial = Mat4::Rotation(Axis, Specification.SweepAngle).TransformDirection(Radial).Normalised();
    for (BrepFace& Face : Result.Payload.Faces)
    {
        if (Face.Surface.Classification != SurfaceClassification::Coons) continue;
        const double U = 0.5 * (Face.Surface.DomainStartU() + Face.Surface.DomainEndU());
        const double V = 0.5 * (Face.Surface.DomainStartV() + Face.Surface.DomainEndV());
        const Vec3 P = Face.Surface.Sample(U, V);
        const Vec3 RDirection = (P - Base - Axis * (P - Base).Dot(Axis)).Normalised();
        if (RDirection.Dot(EndRadial) > 1.0 - 1e-6) Face.Reversed = !Face.Reversed;
    }
    const BodyReport Report = Result.Payload.Validate();
    const double ExpectedVolume = ScalarCriteria::Pi * RetainedHeight *
        (R * R + R * CapRadius + CapRadius * CapRadius) / 3.0 *
        Specification.SweepAngle / ScalarCriteria::TwoPi;
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 ||
        Result.Payload.Vertices.size() != 6 || Result.Payload.Edges.size() != 9 ||
        Result.Payload.Coedges.size() != 18 || Result.Payload.Loops.size() != 5 ||
        Result.Payload.Faces.size() != 5)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                                          "partial apex chamfer did not reach V6/E9/C18/L5/F5 topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence,
                                          "partial apex chamfer volume failed analytic acceptance");
    return Result;
}

Deliver<PartialConeApexChamferSpecification> BlendSolver::ClassifyPartialConeApexChamferVertex(
    const BrepBody& Body, int Vertex, double SetBack) noexcept
{
    if (!std::isfinite(SetBack) || SetBack <= ScalarCriteria::MergeTolerance)
        return Deliver<PartialConeApexChamferSpecification>::Reject(RefusalReason::DegenerateInput,
                                                                      "partial cone-apex vertex chamfer set-back must be finite and positive");
    const BodyReport Report = Body.Validate();
    if (Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 ||
        Body.Vertices.size() != 4 || Body.Edges.size() != 6 ||
        Body.Coedges.size() != 8 || Body.Loops.size() != 3 || Body.Faces.size() != 3)
        return Deliver<PartialConeApexChamferSpecification>::Reject(RefusalReason::NonManifold,
                                                                      "source is not the bounded native partial-cone topology");
    if (Vertex < 0 || Vertex >= static_cast<int>(Body.Vertices.size()))
        return Deliver<PartialConeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                      "selected partial cone-apex chamfer vertex is out of range");
    for (const BrepFace& Face : Body.Faces)
        if (Face.Loops.size() != 1 || Face.Surface.Classification != SurfaceClassification::Revolution)
            return Deliver<PartialConeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                          "source is not the exact native partial-cone revolve surface set");

    int BaseVertex = -1, Apex = -1, RimA = -1, RimB = -1;
    Vec3 Base{}, Axis{};
    double Height = 0.0, BaseRadius = 0.0;
    for (int CandidateBase = 0; CandidateBase < static_cast<int>(Body.Vertices.size()); ++CandidateBase)
    {
        for (int CandidateApex = 0; CandidateApex < static_cast<int>(Body.Vertices.size()); ++CandidateApex)
        {
            if (CandidateBase == CandidateApex) continue;
            const Vec3 Delta = Body.Vertices[CandidateApex].Point - Body.Vertices[CandidateBase].Point;
            const double CandidateHeight = Delta.Length();
            if (CandidateHeight <= ScalarCriteria::MergeTolerance) continue;
            const Vec3 CandidateAxis = Delta / CandidateHeight;
            int FirstRim = -1, SecondRim = -1;
            double Radius = 0.0;
            bool Valid = true;
            for (int Other = 0; Other < static_cast<int>(Body.Vertices.size()); ++Other)
            {
                if (Other == CandidateBase || Other == CandidateApex) continue;
                const Vec3 FromBase = Body.Vertices[Other].Point - Body.Vertices[CandidateBase].Point;
                const double Along = FromBase.Dot(CandidateAxis);
                const Vec3 Radial = FromBase - CandidateAxis * Along;
                if (std::fabs(Along) > ScalarCriteria::GeometricTolerance * std::max(1.0, CandidateHeight) ||
                    Radial.Length() <= ScalarCriteria::MergeTolerance)
                { Valid = false; break; }
                if (FirstRim < 0) { FirstRim = Other; Radius = Radial.Length(); }
                else if (std::fabs(Radial.Length() - Radius) > ScalarCriteria::GeometricTolerance *
                         std::max(1.0, Radius)) Valid = false;
                else SecondRim = Other;
            }
            if (!Valid || FirstRim < 0 || SecondRim < 0 || Radius <= ScalarCriteria::MergeTolerance) continue;
            if (BaseVertex >= 0)
                return Deliver<PartialConeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                               "partial cone has multiple apex frames");
            BaseVertex = CandidateBase; Apex = CandidateApex; RimA = FirstRim; RimB = SecondRim;
            Base = Body.Vertices[CandidateBase].Point; Axis = CandidateAxis;
            Height = CandidateHeight; BaseRadius = Radius;
        }
    }
    if (BaseVertex < 0 || Apex < 0 || Vertex != Apex || RimA < 0 || RimB < 0)
        return Deliver<PartialConeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                      "selected vertex is not the unique partial-cone apex");

    int RimEdge = -1;
    for (size_t I = 0; I < Body.Edges.size(); ++I)
    {
        const BrepEdge& EdgeData = Body.Edges[I];
        const bool JoinsRim = (EdgeData.VertexStart == RimA && EdgeData.VertexEnd == RimB) ||
                              (EdgeData.VertexStart == RimB && EdgeData.VertexEnd == RimA);
        if (!EdgeData.Closed() && JoinsRim &&
            (EdgeData.Curve.Classification == CurveClassification::Arc ||
             EdgeData.Curve.Classification == CurveClassification::Circle) &&
            EdgeData.Curve.Degree == 2 && EdgeData.Curve.Rational())
        {
            if (RimEdge >= 0)
                return Deliver<PartialConeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                               "partial cone has multiple base-rim paths");
            RimEdge = static_cast<int>(I);
        }
    }
    if (RimEdge < 0)
        return Deliver<PartialConeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                      "partial cone has no native circular base-rim path");
    const Vec3 R0 = (Body.Vertices[RimA].Point - Base).Normalised();
    const NurbsCurve& RimCurve = Body.Edges[RimEdge].Curve;
    double Previous = 0.0, Accumulated = 0.0;
    for (int I = 0; I <= 128; ++I)
    {
        const double T = RimCurve.DomainStart() + (RimCurve.DomainEnd() - RimCurve.DomainStart()) *
                         static_cast<double>(I) / 128.0;
        const Vec3 FromBase = RimCurve.Sample(T) - Base;
        const double Along = FromBase.Dot(Axis);
        const Vec3 Radial = FromBase - Axis * Along;
        if (Radial.Length() <= ScalarCriteria::MergeTolerance ||
            std::fabs(Along) > ScalarCriteria::GeometricTolerance * std::max(1.0, Height) ||
            std::fabs(Radial.Length() - BaseRadius) > ScalarCriteria::GeometricTolerance *
                                                     std::max(1.0, BaseRadius))
            return Deliver<PartialConeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                           "partial cone base-rim path is not circular");
        const double Angle = std::atan2(Axis.Dot(R0.Cross(Radial.Normalised())),
                                        R0.Dot(Radial.Normalised()));
        if (I > 0)
        {
            double Delta = Angle - Previous;
            while (Delta > ScalarCriteria::Pi) Delta -= ScalarCriteria::TwoPi;
            while (Delta < -ScalarCriteria::Pi) Delta += ScalarCriteria::TwoPi;
            Accumulated += Delta;
        }
        Previous = Angle;
    }
    const double Sweep = std::fabs(Accumulated);
    if (Sweep <= ScalarCriteria::SweepTolerance ||
        Sweep >= ScalarCriteria::TwoPi - ScalarCriteria::SweepTolerance)
        return Deliver<PartialConeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                      "partial cone sweep is not strictly partial");

    PartialConeApexChamferSpecification Specification;
    Specification.Base = Base;
    Specification.Axis = Axis;
    Specification.BaseRadius = BaseRadius;
    Specification.Height = Height;
    Specification.SetBack = SetBack;
    Specification.SweepAngle = Sweep;
    const Deliver<BrepBody> Feasible = ReconstructPartialConeApexChamfer(Specification);
    if (!Feasible) return Deliver<PartialConeApexChamferSpecification>::Reject(Feasible.Denial.Reason,
                                                                                 Feasible.Denial.Detail);
    return Deliver<PartialConeApexChamferSpecification>::Accept(std::move(Specification));
}

Deliver<BrepBody> BlendSolver::ReconstructUnequalConeApexChamfer(
    const UnequalConeApexChamferSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Apex.X) || !std::isfinite(Specification.Apex.Y) ||
        !std::isfinite(Specification.Apex.Z) || !std::isfinite(Specification.LowerRadius) ||
        !std::isfinite(Specification.UpperRadius) || !std::isfinite(Specification.LowerHeight) ||
        !std::isfinite(Specification.UpperHeight) || !std::isfinite(Specification.SetBack) ||
        Specification.LowerRadius <= Tol || Specification.UpperRadius <= Tol ||
        Specification.LowerHeight <= Tol || Specification.UpperHeight <= Tol || Specification.SetBack <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "unequal bicone apex dimensions must be finite and positive");
    if (!std::isfinite(Specification.Axis.X) || !std::isfinite(Specification.Axis.Y) ||
        !std::isfinite(Specification.Axis.Z) || Specification.Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "unequal bicone apex axis is degenerate");
    const double RadiusScale = std::max({ 1.0, Specification.LowerRadius, Specification.UpperRadius });
    if (std::fabs(Specification.LowerRadius - Specification.UpperRadius) <=
        ScalarCriteria::GeometricTolerance * RadiusScale)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "unequal bicone apex route requires distinct support radii");
    const Vec3 Axis = Specification.Axis.Normalised();
    const Vec3 Apex = Specification.Apex;
    const double LowerHeight = Specification.LowerHeight;
    const double UpperHeight = Specification.UpperHeight;
    const double SetBack = Specification.SetBack;
    if (SetBack >= std::min(LowerHeight, UpperHeight) - Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "unequal bicone apex set-back consumes one conical support");
    const Vec3 Radial = Workplane::FromNormal(Apex, Axis).AxisX.Normalised();
    if (Radial.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "unequal bicone apex radial frame is degenerate");
    const Vec3 LowerBase = Apex - Axis * LowerHeight;
    const Vec3 UpperBase = Apex + Axis * UpperHeight;
    const Vec3 LowerContact = Apex - Axis * SetBack;
    const Vec3 UpperContact = Apex + Axis * SetBack;
    const double LowerContactRadius = Specification.LowerRadius * SetBack / LowerHeight;
    const double UpperContactRadius = Specification.UpperRadius * SetBack / UpperHeight;
    if (LowerContactRadius <= Tol || UpperContactRadius <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "unequal bicone apex chamfer contact ring is degenerate");

    auto RevolveLine = [&](Vec3 Start, Vec3 End) -> Deliver<NurbsSurface>
    {
        Deliver<NurbsCurve> Line = NurbsCurve::Line(Start, End);
        return Line ? NurbsSurface::Revolution(Line.Payload, Apex, Axis, ScalarCriteria::TwoPi)
                    : Deliver<NurbsSurface>::Reject(Line.Denial.Reason, Line.Denial.Detail);
    };
    Deliver<NurbsSurface> Lower = RevolveLine(LowerBase + Radial * Specification.LowerRadius,
                                               LowerContact + Radial * LowerContactRadius);
    Deliver<NurbsSurface> Chamfer = RevolveLine(LowerContact + Radial * LowerContactRadius,
                                                UpperContact + Radial * UpperContactRadius);
    Deliver<NurbsSurface> Upper = RevolveLine(UpperContact + Radial * UpperContactRadius,
                                               UpperBase + Radial * Specification.UpperRadius);
    Deliver<NurbsSurface> LowerDisk = RevolveLine(LowerBase, LowerBase + Radial * Specification.LowerRadius);
    Deliver<NurbsSurface> UpperDisk = RevolveLine(UpperBase, UpperBase + Radial * Specification.UpperRadius);
    if (!Lower || !Chamfer || !Upper || !LowerDisk || !UpperDisk)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "unequal bicone apex chamfer surfaces are degenerate");

    Lower.Payload.Classification = SurfaceClassification::Cone;
    Lower.Payload.Origin = LowerBase; Lower.Payload.Axis = Axis;
    Lower.Payload.RadiusMajor = Specification.LowerRadius;
    Lower.Payload.RadiusMinor = LowerContactRadius;
    Lower.Payload.HalfAngle = std::atan2(Specification.LowerRadius - LowerContactRadius,
                                         LowerHeight - SetBack);
    Chamfer.Payload.Classification = SurfaceClassification::Cone;
    Chamfer.Payload.Origin = LowerContact; Chamfer.Payload.Axis = Axis;
    Chamfer.Payload.RadiusMajor = LowerContactRadius;
    Chamfer.Payload.RadiusMinor = UpperContactRadius;
    Chamfer.Payload.HalfAngle = std::atan2(UpperContactRadius - LowerContactRadius, 2.0 * SetBack);
    Upper.Payload.Classification = SurfaceClassification::Cone;
    Upper.Payload.Origin = UpperContact; Upper.Payload.Axis = Axis;
    Upper.Payload.RadiusMajor = UpperContactRadius;
    Upper.Payload.RadiusMinor = Specification.UpperRadius;
    Upper.Payload.HalfAngle = std::atan2(Specification.UpperRadius - UpperContactRadius,
                                         UpperHeight - SetBack);
    LowerDisk.Payload.Classification = SurfaceClassification::Plane;
    LowerDisk.Payload.Origin = LowerBase; LowerDisk.Payload.Axis = Axis;
    UpperDisk.Payload.Classification = SurfaceClassification::Plane;
    UpperDisk.Payload.Origin = UpperBase; UpperDisk.Payload.Axis = Axis;

    Deliver<BrepBody> Result = BrepBody::Sew({ Lower.Payload, Chamfer.Payload, Upper.Payload,
                                                LowerDisk.Payload, UpperDisk.Payload }, Tol, true);
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason,
                                                   "unequal bicone apex chamfer surfaces could not be sewn");
    const BodyReport Report = Result.Payload.Validate();
    const double LowerRetained = LowerHeight - SetBack;
    const double UpperRetained = UpperHeight - SetBack;
    const double ExpectedVolume = ScalarCriteria::Pi * LowerRetained *
        (Specification.LowerRadius * Specification.LowerRadius +
         Specification.LowerRadius * LowerContactRadius + LowerContactRadius * LowerContactRadius) / 3.0 +
        ScalarCriteria::Pi * (2.0 * SetBack) *
        (LowerContactRadius * LowerContactRadius + LowerContactRadius * UpperContactRadius +
         UpperContactRadius * UpperContactRadius) / 3.0 +
        ScalarCriteria::Pi * UpperRetained *
        (UpperContactRadius * UpperContactRadius + UpperContactRadius * Specification.UpperRadius +
         Specification.UpperRadius * Specification.UpperRadius) / 3.0;
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 ||
        Result.Payload.Vertices.size() != 6 || Result.Payload.Edges.size() != 9 ||
        Result.Payload.Coedges.size() != 18 || Result.Payload.Loops.size() != 5 ||
        Result.Payload.Faces.size() != 5)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                                          "unequal bicone apex chamfer did not reach V6/E9/C18/L5/F5 topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence,
                                          "unequal bicone apex chamfer volume failed analytic acceptance");
    return Result;
}

Deliver<UnequalConeApexChamferSpecification> BlendSolver::ClassifyUnequalConeApexChamferVertex(
    const BrepBody& Body, int Vertex, double SetBack) noexcept
{
    if (!std::isfinite(SetBack) || SetBack <= ScalarCriteria::MergeTolerance)
        return Deliver<UnequalConeApexChamferSpecification>::Reject(RefusalReason::DegenerateInput,
                                                                      "unequal bicone apex set-back must be finite and positive");
    const BodyReport Report = Body.Validate();
    if (!Report.Solid() || Report.Hulls != 2 || Report.Genus != 1 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 ||
        Body.Vertices.size() != 5 || Body.Edges.size() != 6 || Body.Coedges.size() != 12 ||
        Body.Loops.size() != 4 || Body.Faces.size() != 4)
        return Deliver<UnequalConeApexChamferSpecification>::Reject(RefusalReason::NonManifold,
                                                                      "source is not the canonical unequal bicone apex topology");
    if (Vertex < 0 || Vertex >= static_cast<int>(Body.Vertices.size()))
        return Deliver<UnequalConeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                      "selected unequal bicone apex vertex is out of range");
    for (const BrepFace& Face : Body.Faces)
        if (Face.Loops.size() != 1 || Face.Surface.Classification != SurfaceClassification::Revolution)
            return Deliver<UnequalConeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                          "source is not the exact native unequal bicone revolve set");

    std::vector<int> RimEdges, LineEdges;
    for (const BrepEdge& EdgeData : Body.Edges)
    {
        if (EdgeData.Coedges.size() != 2) return Deliver<UnequalConeApexChamferSpecification>::Reject(
            RefusalReason::NonManifold, "unequal bicone edge is not manifold");
        if (EdgeData.Closed() && EdgeData.Curve.Classification == CurveClassification::Circle &&
            EdgeData.Curve.Degree == 2 && EdgeData.Curve.Rational()) RimEdges.push_back(&EdgeData - Body.Edges.data());
        else if (!EdgeData.Closed() && EdgeData.Curve.Classification == CurveClassification::Line &&
                 EdgeData.Curve.Degree == 1) LineEdges.push_back(&EdgeData - Body.Edges.data());
        else return Deliver<UnequalConeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                            "native unequal bicone has unsupported edge geometry");
    }
    if (RimEdges.size() != 2 || LineEdges.size() != 4)
        return Deliver<UnequalConeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                      "native unequal bicone does not have two rims and four generator lines");
    const int RimA = RimEdges[0], RimB = RimEdges[1];
    const int RimVertexA = Body.Edges[RimA].VertexStart, RimVertexB = Body.Edges[RimB].VertexStart;
    int Apex = -1;
    for (int Candidate = 0; Candidate < static_cast<int>(Body.Vertices.size()); ++Candidate)
    {
        if (Candidate == RimVertexA || Candidate == RimVertexB) continue;
        bool ToA = false, ToB = false;
        for (int Edge : LineEdges)
        {
            const BrepEdge& Data = Body.Edges[Edge];
            const int Other = Data.VertexStart == Candidate ? Data.VertexEnd :
                              (Data.VertexEnd == Candidate ? Data.VertexStart : -1);
            if (Other == RimVertexA) ToA = true;
            if (Other == RimVertexB) ToB = true;
        }
        if (ToA && ToB)
        {
            if (Apex >= 0) return Deliver<UnequalConeApexChamferSpecification>::Reject(
                RefusalReason::Unsupported, "native unequal bicone has multiple shared apex vertices");
            Apex = Candidate;
        }
    }
    if (Apex < 0 || Vertex != Apex)
        return Deliver<UnequalConeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                      "selected vertex is not the unique unequal bicone apex");

    struct RimGeometry { Vec3 Centre, Normal; double Radius = 0.0; int Edge = -1; int ConeFace = -1; int PlaneFace = -1; };
    RimGeometry Geometry[2]{};
    for (int K = 0; K < 2; ++K)
    {
        Geometry[K].Edge = RimEdges[K];
        if (!CircularFrame(Body.Edges[Geometry[K].Edge].Curve, Geometry[K].Centre,
                           Geometry[K].Normal, Geometry[K].Radius))
            return Deliver<UnequalConeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                           "native unequal bicone rim is not circular");
        for (size_t F = 0; F < Body.Faces.size(); ++F)
        {
            bool UsesRim = false, UsesApexLine = false;
            for (int Loop : Body.Faces[F].Loops)
                for (int Coedge : Body.Loops[Loop].Coedges)
                {
                    const int Edge = Body.Coedges[Coedge].Edge;
                    if (Edge == Geometry[K].Edge) UsesRim = true;
                    if (Edge >= 0 && Edge < static_cast<int>(Body.Edges.size()) &&
                        std::find(LineEdges.begin(), LineEdges.end(), Edge) != LineEdges.end())
                    {
                        const BrepEdge& Data = Body.Edges[Edge];
                        if (Data.VertexStart == Apex || Data.VertexEnd == Apex) UsesApexLine = true;
                    }
                }
            if (!UsesRim) continue;
            if (UsesApexLine) Geometry[K].ConeFace = static_cast<int>(F);
            else Geometry[K].PlaneFace = static_cast<int>(F);
        }
        if (Geometry[K].ConeFace < 0 || Geometry[K].PlaneFace < 0)
            return Deliver<UnequalConeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                           "native unequal bicone support pair is incomplete");
        Vec3 PlaneNormal;
        if (!PlanarNormal(Body, Geometry[K].PlaneFace, PlaneNormal))
            return Deliver<UnequalConeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                           "native unequal bicone cap is not planar");
    }
    if (Geometry[0].Radius <= Tol || Geometry[1].Radius <= Tol)
        return Deliver<UnequalConeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                      "native unequal bicone rim radius is not positive");
    const double RadiusScale = std::max({ 1.0, Geometry[0].Radius, Geometry[1].Radius });
    if (std::fabs(Geometry[0].Radius - Geometry[1].Radius) <=
        ScalarCriteria::GeometricTolerance * RadiusScale)
        return Deliver<UnequalConeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                      "apex combination has equal rather than unequal radii");
    Vec3 Axis = (Geometry[1].Centre - Geometry[0].Centre).Normalised();
    if (Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<UnequalConeApexChamferSpecification>::Reject(RefusalReason::DegenerateInput,
                                                                      "unequal bicone support axis is degenerate");
    if ((std::fabs(Axis.X) > ScalarCriteria::GeometricTolerance && Axis.X < 0.0) ||
        (std::fabs(Axis.X) <= ScalarCriteria::GeometricTolerance && std::fabs(Axis.Y) > ScalarCriteria::GeometricTolerance && Axis.Y < 0.0) ||
        (std::fabs(Axis.X) <= ScalarCriteria::GeometricTolerance && std::fabs(Axis.Y) <= ScalarCriteria::GeometricTolerance && Axis.Z < 0.0)) Axis = -Axis;
    for (const RimGeometry& G : Geometry)
    {
        if (std::fabs(std::fabs(G.Normal.Dot(Axis)) - 1.0) > ScalarCriteria::AngularTolerance ||
            (G.Centre - Body.Vertices[Apex].Point - Axis * (G.Centre - Body.Vertices[Apex].Point).Dot(Axis)).Length() >
                ScalarCriteria::GeometricTolerance * std::max(1.0, G.Centre.Distance(Body.Vertices[Apex].Point)))
            return Deliver<UnequalConeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                           "native unequal bicone rims are not coaxial with the apex");
    }
    if ((Geometry[0].Centre - Geometry[1].Centre).Dot(Axis) > 0.0) std::swap(Geometry[0], Geometry[1]);
    const Vec3 ApexPoint = Body.Vertices[Apex].Point;
    const double LowerHeight = ApexPoint.Distance(Geometry[0].Centre);
    const double UpperHeight = ApexPoint.Distance(Geometry[1].Centre);
    if (LowerHeight <= Tol || UpperHeight <= Tol)
        return Deliver<UnequalConeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                      "native unequal bicone height is degenerate");
    UnequalConeApexChamferSpecification Specification;
    Specification.Apex = ApexPoint;
    Specification.Axis = Axis;
    Specification.LowerRadius = Geometry[0].Radius;
    Specification.UpperRadius = Geometry[1].Radius;
    Specification.LowerHeight = LowerHeight;
    Specification.UpperHeight = UpperHeight;
    Specification.SetBack = SetBack;
    const Deliver<BrepBody> Feasible = ReconstructUnequalConeApexChamfer(Specification);
    if (!Feasible) return Deliver<UnequalConeApexChamferSpecification>::Reject(Feasible.Denial.Reason,
                                                                                  Feasible.Denial.Detail);
    return Deliver<UnequalConeApexChamferSpecification>::Accept(std::move(Specification));
}

Deliver<BrepBody> BlendSolver::ReconstructEqualRadiusBiconeApexChamfer(
    const EqualRadiusBiconeApexChamferSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Apex.X) || !std::isfinite(Specification.Apex.Y) ||
        !std::isfinite(Specification.Apex.Z) || !std::isfinite(Specification.Radius) ||
        !std::isfinite(Specification.LowerHeight) || !std::isfinite(Specification.UpperHeight) ||
        !std::isfinite(Specification.SetBack) || Specification.Radius <= Tol ||
        Specification.LowerHeight <= Tol || Specification.UpperHeight <= Tol || Specification.SetBack <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "equal bicone apex dimensions must be finite and positive");
    if (!std::isfinite(Specification.Axis.X) || !std::isfinite(Specification.Axis.Y) ||
        !std::isfinite(Specification.Axis.Z) || Specification.Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "equal bicone apex axis is degenerate");
    if (Specification.SetBack >= std::min(Specification.LowerHeight, Specification.UpperHeight) - Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "equal bicone apex set-back consumes one conical support");
    const Vec3 Axis = Specification.Axis.Normalised();
    const Vec3 Apex = Specification.Apex;
    const Vec3 Radial = Workplane::FromNormal(Apex, Axis).AxisX.Normalised();
    if (Radial.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "equal bicone apex radial frame is degenerate");
    const Vec3 LowerBase = Apex - Axis * Specification.LowerHeight;
    const Vec3 UpperBase = Apex + Axis * Specification.UpperHeight;
    const Vec3 LowerContact = Apex - Axis * Specification.SetBack;
    const Vec3 UpperContact = Apex + Axis * Specification.SetBack;
    const double LowerContactRadius = Specification.Radius * Specification.SetBack / Specification.LowerHeight;
    const double UpperContactRadius = Specification.Radius * Specification.SetBack / Specification.UpperHeight;
    if (LowerContactRadius <= Tol || UpperContactRadius <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "equal bicone apex chamfer contact ring is degenerate");
    auto RevolveLine = [&](Vec3 Start, Vec3 End) -> Deliver<NurbsSurface>
    {
        Deliver<NurbsCurve> Line = NurbsCurve::Line(Start, End);
        return Line ? NurbsSurface::Revolution(Line.Payload, Apex, Axis, ScalarCriteria::TwoPi)
                    : Deliver<NurbsSurface>::Reject(Line.Denial.Reason, Line.Denial.Detail);
    };
    Deliver<NurbsSurface> Lower = RevolveLine(LowerBase + Radial * Specification.Radius,
                                               LowerContact + Radial * LowerContactRadius);
    Deliver<NurbsSurface> Chamfer = RevolveLine(LowerContact + Radial * LowerContactRadius,
                                                UpperContact + Radial * UpperContactRadius);
    Deliver<NurbsSurface> Upper = RevolveLine(UpperContact + Radial * UpperContactRadius,
                                               UpperBase + Radial * Specification.Radius);
    Deliver<NurbsSurface> LowerDisk = RevolveLine(LowerBase, LowerBase + Radial * Specification.Radius);
    Deliver<NurbsSurface> UpperDisk = RevolveLine(UpperBase, UpperBase + Radial * Specification.Radius);
    if (!Lower || !Chamfer || !Upper || !LowerDisk || !UpperDisk)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "equal bicone apex chamfer surfaces are degenerate");
    Lower.Payload.Classification = SurfaceClassification::Cone;
    Lower.Payload.Origin = LowerBase; Lower.Payload.Axis = Axis;
    Lower.Payload.RadiusMajor = Specification.Radius; Lower.Payload.RadiusMinor = LowerContactRadius;
    Lower.Payload.HalfAngle = std::atan2(Specification.Radius - LowerContactRadius,
                                         Specification.LowerHeight - Specification.SetBack);
    Chamfer.Payload.Classification = SurfaceClassification::Cone;
    Chamfer.Payload.Origin = LowerContact; Chamfer.Payload.Axis = Axis;
    Chamfer.Payload.RadiusMajor = LowerContactRadius; Chamfer.Payload.RadiusMinor = UpperContactRadius;
    Chamfer.Payload.HalfAngle = std::atan2(UpperContactRadius - LowerContactRadius,
                                           2.0 * Specification.SetBack);
    Upper.Payload.Classification = SurfaceClassification::Cone;
    Upper.Payload.Origin = UpperContact; Upper.Payload.Axis = Axis;
    Upper.Payload.RadiusMajor = UpperContactRadius; Upper.Payload.RadiusMinor = Specification.Radius;
    Upper.Payload.HalfAngle = std::atan2(Specification.Radius - UpperContactRadius,
                                         Specification.UpperHeight - Specification.SetBack);
    LowerDisk.Payload.Classification = SurfaceClassification::Plane;
    LowerDisk.Payload.Origin = LowerBase; LowerDisk.Payload.Axis = Axis;
    UpperDisk.Payload.Classification = SurfaceClassification::Plane;
    UpperDisk.Payload.Origin = UpperBase; UpperDisk.Payload.Axis = Axis;
    Deliver<BrepBody> Result = BrepBody::Sew({ Lower.Payload, Chamfer.Payload, Upper.Payload,
                                                LowerDisk.Payload, UpperDisk.Payload });
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason,
                                                   "equal bicone apex chamfer surfaces could not be sewn");
    const BodyReport Report = Result.Payload.Validate();
    const double LowerRetained = Specification.LowerHeight - Specification.SetBack;
    const double UpperRetained = Specification.UpperHeight - Specification.SetBack;
    const double ExpectedVolume = ScalarCriteria::Pi * LowerRetained *
        (Specification.Radius * Specification.Radius + Specification.Radius * LowerContactRadius +
         LowerContactRadius * LowerContactRadius) / 3.0 +
        ScalarCriteria::Pi * (2.0 * Specification.SetBack) *
        (LowerContactRadius * LowerContactRadius + LowerContactRadius * UpperContactRadius +
         UpperContactRadius * UpperContactRadius) / 3.0 +
        ScalarCriteria::Pi * UpperRetained *
        (UpperContactRadius * UpperContactRadius + UpperContactRadius * Specification.Radius +
         Specification.Radius * Specification.Radius) / 3.0;
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 ||
        Result.Payload.Vertices.size() != 6 || Result.Payload.Edges.size() != 9 ||
        Result.Payload.Coedges.size() != 18 || Result.Payload.Loops.size() != 5 ||
        Result.Payload.Faces.size() != 5)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                                          "equal bicone apex chamfer did not reach V6/E9/C18/L5/F5 topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence,
                                          "equal bicone apex chamfer volume failed analytic acceptance");
    return Result;
}

Deliver<EqualRadiusBiconeApexChamferSpecification> BlendSolver::ClassifyEqualRadiusBiconeApexChamferVertex(
    const BrepBody& Body, int Vertex, double SetBack) noexcept
{
    if (!std::isfinite(SetBack) || SetBack <= ScalarCriteria::MergeTolerance)
        return Deliver<EqualRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::DegenerateInput,
                                                                      "equal bicone apex set-back must be finite and positive");
    const BodyReport Report = Body.Validate();
    if (!Report.Solid() || Report.Hulls != 2 || Report.Genus != 1 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 ||
        Body.Vertices.size() != 5 || Body.Edges.size() != 6 || Body.Coedges.size() != 12 ||
        Body.Loops.size() != 4 || Body.Faces.size() != 4)
        return Deliver<EqualRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::NonManifold,
                                                                      "source is not the canonical equal bicone apex topology");
    if (Vertex < 0 || Vertex >= static_cast<int>(Body.Vertices.size()))
        return Deliver<EqualRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                      "selected equal bicone apex vertex is out of range");
    for (const BrepFace& Face : Body.Faces)
        if (Face.Loops.size() != 1 || Face.Surface.Classification != SurfaceClassification::Revolution)
            return Deliver<EqualRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                          "source is not the exact native equal bicone revolve set");

    std::vector<int> RimEdges, LineEdges;
    for (const BrepEdge& EdgeData : Body.Edges)
    {
        if (EdgeData.Coedges.size() != 2) return Deliver<EqualRadiusBiconeApexChamferSpecification>::Reject(
            RefusalReason::NonManifold, "equal bicone edge is not manifold");
        if (EdgeData.Closed() && EdgeData.Curve.Classification == CurveClassification::Circle &&
            EdgeData.Curve.Degree == 2 && EdgeData.Curve.Rational()) RimEdges.push_back(&EdgeData - Body.Edges.data());
        else if (!EdgeData.Closed() && EdgeData.Curve.Classification == CurveClassification::Line &&
                 EdgeData.Curve.Degree == 1) LineEdges.push_back(&EdgeData - Body.Edges.data());
        else return Deliver<EqualRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                            "native equal bicone has unsupported edge geometry");
    }
    if (RimEdges.size() != 2 || LineEdges.size() != 4)
        return Deliver<EqualRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                      "native equal bicone does not have two rims and four generator lines");
    const int RimA = RimEdges[0], RimB = RimEdges[1];
    const int RimVertexA = Body.Edges[RimA].VertexStart, RimVertexB = Body.Edges[RimB].VertexStart;
    int Apex = -1;
    for (int Candidate = 0; Candidate < static_cast<int>(Body.Vertices.size()); ++Candidate)
    {
        if (Candidate == RimVertexA || Candidate == RimVertexB) continue;
        bool ToA = false, ToB = false;
        for (int Edge : LineEdges)
        {
            const BrepEdge& Data = Body.Edges[Edge];
            const int Other = Data.VertexStart == Candidate ? Data.VertexEnd :
                              (Data.VertexEnd == Candidate ? Data.VertexStart : -1);
            if (Other == RimVertexA) ToA = true;
            if (Other == RimVertexB) ToB = true;
        }
        if (ToA && ToB)
        {
            if (Apex >= 0) return Deliver<EqualRadiusBiconeApexChamferSpecification>::Reject(
                RefusalReason::Unsupported, "native equal bicone has multiple shared apex vertices");
            Apex = Candidate;
        }
    }
    if (Apex < 0 || Vertex != Apex)
        return Deliver<EqualRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                      "selected vertex is not the unique equal bicone apex");

    struct RimGeometry { Vec3 Centre, Normal; double Radius = 0.0; int Edge = -1; int ConeFace = -1; int PlaneFace = -1; };
    RimGeometry Geometry[2]{};
    for (int K = 0; K < 2; ++K)
    {
        Geometry[K].Edge = RimEdges[K];
        if (!CircularFrame(Body.Edges[Geometry[K].Edge].Curve, Geometry[K].Centre,
                           Geometry[K].Normal, Geometry[K].Radius))
            return Deliver<EqualRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                           "native equal bicone rim is not circular");
        for (size_t F = 0; F < Body.Faces.size(); ++F)
        {
            bool UsesRim = false, UsesApexLine = false;
            for (int Loop : Body.Faces[F].Loops)
                for (int Coedge : Body.Loops[Loop].Coedges)
                {
                    const int Edge = Body.Coedges[Coedge].Edge;
                    if (Edge == Geometry[K].Edge) UsesRim = true;
                    if (Edge >= 0 && Edge < static_cast<int>(Body.Edges.size()) &&
                        std::find(LineEdges.begin(), LineEdges.end(), Edge) != LineEdges.end())
                    {
                        const BrepEdge& Data = Body.Edges[Edge];
                        if (Data.VertexStart == Apex || Data.VertexEnd == Apex) UsesApexLine = true;
                    }
                }
            if (!UsesRim) continue;
            if (UsesApexLine) Geometry[K].ConeFace = static_cast<int>(F);
            else Geometry[K].PlaneFace = static_cast<int>(F);
        }
        if (Geometry[K].ConeFace < 0 || Geometry[K].PlaneFace < 0)
            return Deliver<EqualRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                           "native equal bicone support pair is incomplete");
        Vec3 PlaneNormal;
        if (!PlanarNormal(Body, Geometry[K].PlaneFace, PlaneNormal))
            return Deliver<EqualRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                           "native equal bicone cap is not planar");
    }
    if (Geometry[0].Radius <= Tol || Geometry[1].Radius <= Tol)
        return Deliver<EqualRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                      "native equal bicone rim radius is not positive");
    const double RadiusScale = std::max({ 1.0, Geometry[0].Radius, Geometry[1].Radius });
    if (std::fabs(Geometry[0].Radius - Geometry[1].Radius) >
        ScalarCriteria::GeometricTolerance * RadiusScale)
        return Deliver<EqualRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                      "equal bicone apex route requires equal radii");
    Vec3 Axis = (Geometry[1].Centre - Geometry[0].Centre).Normalised();
    if (Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<EqualRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::DegenerateInput,
                                                                      "equal bicone support axis is degenerate");
    if ((std::fabs(Axis.X) > ScalarCriteria::GeometricTolerance && Axis.X < 0.0) ||
        (std::fabs(Axis.X) <= ScalarCriteria::GeometricTolerance && std::fabs(Axis.Y) > ScalarCriteria::GeometricTolerance && Axis.Y < 0.0) ||
        (std::fabs(Axis.X) <= ScalarCriteria::GeometricTolerance && std::fabs(Axis.Y) <= ScalarCriteria::GeometricTolerance && Axis.Z < 0.0)) Axis = -Axis;
    for (const RimGeometry& G : Geometry)
    {
        if (std::fabs(std::fabs(G.Normal.Dot(Axis)) - 1.0) > ScalarCriteria::AngularTolerance ||
            (G.Centre - Body.Vertices[Apex].Point - Axis * (G.Centre - Body.Vertices[Apex].Point).Dot(Axis)).Length() >
                ScalarCriteria::GeometricTolerance * std::max(1.0, G.Centre.Distance(Body.Vertices[Apex].Point)))
            return Deliver<EqualRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                           "native equal bicone rims are not coaxial with the apex");
    }
    if ((Geometry[0].Centre - Geometry[1].Centre).Dot(Axis) > 0.0) std::swap(Geometry[0], Geometry[1]);
    const Vec3 ApexPoint = Body.Vertices[Apex].Point;
    const double LowerHeight = ApexPoint.Distance(Geometry[0].Centre);
    const double UpperHeight = ApexPoint.Distance(Geometry[1].Centre);
    if (LowerHeight <= Tol || UpperHeight <= Tol)
        return Deliver<EqualRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported,
                                                                      "native equal bicone height is degenerate");
    EqualRadiusBiconeApexChamferSpecification Specification;
    Specification.Apex = ApexPoint;
    Specification.Axis = Axis;
    Specification.Radius = Geometry[0].Radius;
    Specification.LowerHeight = LowerHeight;
    Specification.UpperHeight = UpperHeight;
    Specification.SetBack = SetBack;
    const Deliver<BrepBody> Feasible = ReconstructEqualRadiusBiconeApexChamfer(Specification);
    if (!Feasible) return Deliver<EqualRadiusBiconeApexChamferSpecification>::Reject(Feasible.Denial.Reason,
                                                                                  Feasible.Denial.Detail);
    return Deliver<EqualRadiusBiconeApexChamferSpecification>::Accept(std::move(Specification));
}

Deliver<BrepBody> BlendSolver::ReconstructUnequalConeApexUnequalSetbackChamfer(
    const UnequalConeApexUnequalSetbackChamferSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Apex.X) || !std::isfinite(Specification.Apex.Y) ||
        !std::isfinite(Specification.Apex.Z) || !std::isfinite(Specification.LowerRadius) ||
        !std::isfinite(Specification.UpperRadius) || !std::isfinite(Specification.LowerHeight) ||
        !std::isfinite(Specification.UpperHeight) || !std::isfinite(Specification.LowerSetBack) ||
        !std::isfinite(Specification.UpperSetBack) || Specification.LowerRadius <= Tol ||
        Specification.UpperRadius <= Tol || Specification.LowerHeight <= Tol ||
        Specification.UpperHeight <= Tol || Specification.LowerSetBack <= Tol || Specification.UpperSetBack <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "unequal-setback bicone dimensions must be finite and positive");
    if (!std::isfinite(Specification.Axis.X) || !std::isfinite(Specification.Axis.Y) ||
        !std::isfinite(Specification.Axis.Z) || Specification.Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "unequal-setback bicone axis is degenerate");
    const double RadiusScale = std::max({ 1.0, Specification.LowerRadius, Specification.UpperRadius });
    if (std::fabs(Specification.LowerRadius - Specification.UpperRadius) <=
        ScalarCriteria::GeometricTolerance * RadiusScale)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "unequal-setback route requires distinct support radii");
    const double SetbackScale = std::max({ 1.0, Specification.LowerSetBack, Specification.UpperSetBack });
    if (std::fabs(Specification.LowerSetBack - Specification.UpperSetBack) <=
        ScalarCriteria::GeometricTolerance * SetbackScale)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "equal support set-backs belong to the bounded equal-setback route");
    if (Specification.LowerSetBack >= Specification.LowerHeight - Tol ||
        Specification.UpperSetBack >= Specification.UpperHeight - Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "unequal-setback bicone set-back consumes a support");

    const Vec3 Axis = Specification.Axis.Normalised();
    const Vec3 Apex = Specification.Apex;
    const Vec3 Radial = Workplane::FromNormal(Apex, Axis).AxisX.Normalised();
    if (Radial.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "unequal-setback bicone radial frame is degenerate");
    const Vec3 LowerBase = Apex - Axis * Specification.LowerHeight;
    const Vec3 UpperBase = Apex + Axis * Specification.UpperHeight;
    const Vec3 LowerContact = Apex - Axis * Specification.LowerSetBack;
    const Vec3 UpperContact = Apex + Axis * Specification.UpperSetBack;
    const double LowerContactRadius = Specification.LowerRadius * Specification.LowerSetBack / Specification.LowerHeight;
    const double UpperContactRadius = Specification.UpperRadius * Specification.UpperSetBack / Specification.UpperHeight;
    const double BridgeLength = Specification.LowerSetBack + Specification.UpperSetBack;
    if (LowerContactRadius <= Tol || UpperContactRadius <= Tol || BridgeLength <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "unequal-setback bicone contact ring is degenerate");

    auto RevolveLine = [&](Vec3 Start, Vec3 End) -> Deliver<NurbsSurface>
    {
        Deliver<NurbsCurve> Line = NurbsCurve::Line(Start, End);
        return Line ? NurbsSurface::Revolution(Line.Payload, Apex, Axis, ScalarCriteria::TwoPi)
                    : Deliver<NurbsSurface>::Reject(Line.Denial.Reason, Line.Denial.Detail);
    };
    Deliver<NurbsSurface> Lower = RevolveLine(LowerBase + Radial * Specification.LowerRadius,
                                               LowerContact + Radial * LowerContactRadius);
    Deliver<NurbsSurface> Chamfer = RevolveLine(LowerContact + Radial * LowerContactRadius,
                                                UpperContact + Radial * UpperContactRadius);
    Deliver<NurbsSurface> Upper = RevolveLine(UpperContact + Radial * UpperContactRadius,
                                               UpperBase + Radial * Specification.UpperRadius);
    Deliver<NurbsSurface> LowerDisk = RevolveLine(LowerBase, LowerBase + Radial * Specification.LowerRadius);
    Deliver<NurbsSurface> UpperDisk = RevolveLine(UpperBase, UpperBase + Radial * Specification.UpperRadius);
    if (!Lower || !Chamfer || !Upper || !LowerDisk || !UpperDisk)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "unequal-setback bicone surfaces are degenerate");

    Lower.Payload.Classification = SurfaceClassification::Cone;
    Lower.Payload.Origin = LowerBase; Lower.Payload.Axis = Axis;
    Lower.Payload.RadiusMajor = Specification.LowerRadius; Lower.Payload.RadiusMinor = LowerContactRadius;
    Lower.Payload.HalfAngle = std::atan2(Specification.LowerRadius - LowerContactRadius,
                                         Specification.LowerHeight - Specification.LowerSetBack);
    Chamfer.Payload.Classification = SurfaceClassification::Cone;
    Chamfer.Payload.Origin = LowerContact; Chamfer.Payload.Axis = Axis;
    Chamfer.Payload.RadiusMajor = LowerContactRadius; Chamfer.Payload.RadiusMinor = UpperContactRadius;
    Chamfer.Payload.HalfAngle = std::atan2(UpperContactRadius - LowerContactRadius, BridgeLength);
    Upper.Payload.Classification = SurfaceClassification::Cone;
    Upper.Payload.Origin = UpperContact; Upper.Payload.Axis = Axis;
    Upper.Payload.RadiusMajor = UpperContactRadius; Upper.Payload.RadiusMinor = Specification.UpperRadius;
    Upper.Payload.HalfAngle = std::atan2(Specification.UpperRadius - UpperContactRadius,
                                         Specification.UpperHeight - Specification.UpperSetBack);
    LowerDisk.Payload.Classification = SurfaceClassification::Plane;
    LowerDisk.Payload.Origin = LowerBase; LowerDisk.Payload.Axis = Axis;
    UpperDisk.Payload.Classification = SurfaceClassification::Plane;
    UpperDisk.Payload.Origin = UpperBase; UpperDisk.Payload.Axis = Axis;

    Deliver<BrepBody> Result = BrepBody::Sew({ Lower.Payload, Chamfer.Payload, Upper.Payload,
                                                LowerDisk.Payload, UpperDisk.Payload }, Tol, true);
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason,
                                                   "unequal-setback bicone surfaces could not be sewn");
    const BodyReport Report = Result.Payload.Validate();
    const double ExpectedVolume =
        ScalarCriteria::Pi * (Specification.LowerHeight - Specification.LowerSetBack) *
            (Specification.LowerRadius * Specification.LowerRadius +
             Specification.LowerRadius * LowerContactRadius + LowerContactRadius * LowerContactRadius) / 3.0 +
        ScalarCriteria::Pi * BridgeLength *
            (LowerContactRadius * LowerContactRadius + LowerContactRadius * UpperContactRadius +
             UpperContactRadius * UpperContactRadius) / 3.0 +
        ScalarCriteria::Pi * (Specification.UpperHeight - Specification.UpperSetBack) *
            (UpperContactRadius * UpperContactRadius + UpperContactRadius * Specification.UpperRadius +
             Specification.UpperRadius * Specification.UpperRadius) / 3.0;
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 ||
        Result.Payload.Vertices.size() != 6 || Result.Payload.Edges.size() != 9 ||
        Result.Payload.Coedges.size() != 18 || Result.Payload.Loops.size() != 5 || Result.Payload.Faces.size() != 5)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                                          "unequal-setback bicone did not reach V6/E9/C18/L5/F5 topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence,
                                          "unequal-setback bicone volume failed analytic acceptance");
    return Result;
}

Deliver<UnequalConeApexUnequalSetbackChamferSpecification>
BlendSolver::ClassifyUnequalConeApexUnequalSetbackChamferVertex(
    const BrepBody& Body, int Vertex, double LowerSetBack, double UpperSetBack) noexcept
{
    if (!std::isfinite(LowerSetBack) || !std::isfinite(UpperSetBack) ||
        LowerSetBack <= ScalarCriteria::MergeTolerance || UpperSetBack <= ScalarCriteria::MergeTolerance)
        return Deliver<UnequalConeApexUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::DegenerateInput, "unequal support set-backs must be finite and positive");
    const double SetbackScale = std::max({ 1.0, LowerSetBack, UpperSetBack });
    if (std::fabs(LowerSetBack - UpperSetBack) <= ScalarCriteria::GeometricTolerance * SetbackScale)
        return Deliver<UnequalConeApexUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::Unsupported, "equal support set-backs belong to the bounded 38w route");
    const double ProbeSetBack = std::min(LowerSetBack, UpperSetBack);
    const Deliver<UnequalConeApexChamferSpecification> Base =
        ClassifyUnequalConeApexChamferVertex(Body, Vertex, ProbeSetBack);
    if (!Base) return Deliver<UnequalConeApexUnequalSetbackChamferSpecification>::Reject(
        Base.Denial.Reason, Base.Denial.Detail);
    UnequalConeApexUnequalSetbackChamferSpecification Specification;
    Specification.Apex = Base.Payload.Apex;
    Specification.Axis = Base.Payload.Axis;
    Specification.LowerRadius = Base.Payload.LowerRadius;
    Specification.UpperRadius = Base.Payload.UpperRadius;
    Specification.LowerHeight = Base.Payload.LowerHeight;
    Specification.UpperHeight = Base.Payload.UpperHeight;
    Specification.LowerSetBack = LowerSetBack;
    Specification.UpperSetBack = UpperSetBack;
    const Deliver<BrepBody> Feasible = ReconstructUnequalConeApexUnequalSetbackChamfer(Specification);
    if (!Feasible) return Deliver<UnequalConeApexUnequalSetbackChamferSpecification>::Reject(
        Feasible.Denial.Reason, Feasible.Denial.Detail);
    return Deliver<UnequalConeApexUnequalSetbackChamferSpecification>::Accept(std::move(Specification));
}

Deliver<BrepBody> BlendSolver::ReconstructEqualRadiusBiconeUnequalSetbackChamfer(
    const EqualRadiusBiconeUnequalSetbackChamferSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Apex.X) || !std::isfinite(Specification.Apex.Y) ||
        !std::isfinite(Specification.Apex.Z) || !std::isfinite(Specification.Radius) ||
        !std::isfinite(Specification.LowerHeight) || !std::isfinite(Specification.UpperHeight) ||
        !std::isfinite(Specification.LowerSetBack) || !std::isfinite(Specification.UpperSetBack) ||
        Specification.Radius <= Tol || Specification.LowerHeight <= Tol || Specification.UpperHeight <= Tol ||
        Specification.LowerSetBack <= Tol || Specification.UpperSetBack <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "equal-radius independent-setback bicone dimensions must be finite and positive");
    if (!std::isfinite(Specification.Axis.X) || !std::isfinite(Specification.Axis.Y) ||
        !std::isfinite(Specification.Axis.Z) || Specification.Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "equal-radius independent-setback bicone axis is degenerate");
    const double SetbackScale = std::max({ 1.0, Specification.LowerSetBack, Specification.UpperSetBack });
    if (std::fabs(Specification.LowerSetBack - Specification.UpperSetBack) <=
        ScalarCriteria::GeometricTolerance * SetbackScale)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "equal support set-backs belong to the bounded Phase 39e route");
    if (Specification.LowerSetBack >= Specification.LowerHeight - Tol ||
        Specification.UpperSetBack >= Specification.UpperHeight - Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "equal-radius independent-setback bicone consumes a support");

    const Vec3 Axis = Specification.Axis.Normalised();
    const Vec3 Apex = Specification.Apex;
    const Vec3 Radial = Workplane::FromNormal(Apex, Axis).AxisX.Normalised();
    if (Radial.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "equal-radius independent-setback bicone radial frame is degenerate");
    const Vec3 LowerBase = Apex - Axis * Specification.LowerHeight;
    const Vec3 UpperBase = Apex + Axis * Specification.UpperHeight;
    const Vec3 LowerContact = Apex - Axis * Specification.LowerSetBack;
    const Vec3 UpperContact = Apex + Axis * Specification.UpperSetBack;
    const double LowerContactRadius = Specification.Radius * Specification.LowerSetBack / Specification.LowerHeight;
    const double UpperContactRadius = Specification.Radius * Specification.UpperSetBack / Specification.UpperHeight;
    const double BridgeLength = Specification.LowerSetBack + Specification.UpperSetBack;
    if (LowerContactRadius <= Tol || UpperContactRadius <= Tol || BridgeLength <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "equal-radius independent-setback bicone contact ring is degenerate");

    auto RevolveLine = [&](Vec3 Start, Vec3 End) -> Deliver<NurbsSurface>
    {
        Deliver<NurbsCurve> Line = NurbsCurve::Line(Start, End);
        return Line ? NurbsSurface::Revolution(Line.Payload, Apex, Axis, ScalarCriteria::TwoPi)
                    : Deliver<NurbsSurface>::Reject(Line.Denial.Reason, Line.Denial.Detail);
    };
    Deliver<NurbsSurface> Lower = RevolveLine(LowerBase + Radial * Specification.Radius,
                                               LowerContact + Radial * LowerContactRadius);
    Deliver<NurbsSurface> Chamfer = RevolveLine(LowerContact + Radial * LowerContactRadius,
                                                UpperContact + Radial * UpperContactRadius);
    Deliver<NurbsSurface> Upper = RevolveLine(UpperContact + Radial * UpperContactRadius,
                                               UpperBase + Radial * Specification.Radius);
    Deliver<NurbsSurface> LowerDisk = RevolveLine(LowerBase, LowerBase + Radial * Specification.Radius);
    Deliver<NurbsSurface> UpperDisk = RevolveLine(UpperBase, UpperBase + Radial * Specification.Radius);
    if (!Lower || !Chamfer || !Upper || !LowerDisk || !UpperDisk)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "equal-radius independent-setback bicone surfaces are degenerate");

    Lower.Payload.Classification = SurfaceClassification::Cone;
    Lower.Payload.Origin = LowerBase; Lower.Payload.Axis = Axis;
    Lower.Payload.RadiusMajor = Specification.Radius; Lower.Payload.RadiusMinor = LowerContactRadius;
    Lower.Payload.HalfAngle = std::atan2(Specification.Radius - LowerContactRadius,
                                         Specification.LowerHeight - Specification.LowerSetBack);
    Chamfer.Payload.Classification = SurfaceClassification::Cone;
    Chamfer.Payload.Origin = LowerContact; Chamfer.Payload.Axis = Axis;
    Chamfer.Payload.RadiusMajor = LowerContactRadius; Chamfer.Payload.RadiusMinor = UpperContactRadius;
    Chamfer.Payload.HalfAngle = std::atan2(UpperContactRadius - LowerContactRadius, BridgeLength);
    Upper.Payload.Classification = SurfaceClassification::Cone;
    Upper.Payload.Origin = UpperContact; Upper.Payload.Axis = Axis;
    Upper.Payload.RadiusMajor = UpperContactRadius; Upper.Payload.RadiusMinor = Specification.Radius;
    Upper.Payload.HalfAngle = std::atan2(Specification.Radius - UpperContactRadius,
                                         Specification.UpperHeight - Specification.UpperSetBack);
    LowerDisk.Payload.Classification = SurfaceClassification::Plane;
    LowerDisk.Payload.Origin = LowerBase; LowerDisk.Payload.Axis = Axis;
    UpperDisk.Payload.Classification = SurfaceClassification::Plane;
    UpperDisk.Payload.Origin = UpperBase; UpperDisk.Payload.Axis = Axis;

    Deliver<BrepBody> Result = BrepBody::Sew({ Lower.Payload, Chamfer.Payload, Upper.Payload,
                                                LowerDisk.Payload, UpperDisk.Payload });
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason,
                                                   "equal-radius independent-setback bicone surfaces could not be sewn");
    const BodyReport Report = Result.Payload.Validate();
    const double ExpectedVolume =
        ScalarCriteria::Pi * (Specification.LowerHeight - Specification.LowerSetBack) *
            (Specification.Radius * Specification.Radius + Specification.Radius * LowerContactRadius +
             LowerContactRadius * LowerContactRadius) / 3.0 +
        ScalarCriteria::Pi * BridgeLength *
            (LowerContactRadius * LowerContactRadius + LowerContactRadius * UpperContactRadius +
             UpperContactRadius * UpperContactRadius) / 3.0 +
        ScalarCriteria::Pi * (Specification.UpperHeight - Specification.UpperSetBack) *
            (UpperContactRadius * UpperContactRadius + UpperContactRadius * Specification.Radius +
             Specification.Radius * Specification.Radius) / 3.0;
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 ||
        Result.Payload.Vertices.size() != 6 || Result.Payload.Edges.size() != 9 ||
        Result.Payload.Coedges.size() != 18 || Result.Payload.Loops.size() != 5 ||
        Result.Payload.Faces.size() != 5)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                                          "equal-radius independent-setback bicone did not reach V6/E9/C18/L5/F5 topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence,
                                          "equal-radius independent-setback bicone volume failed analytic acceptance");
    return Result;
}

Deliver<EqualRadiusBiconeUnequalSetbackChamferSpecification>
BlendSolver::ClassifyEqualRadiusBiconeUnequalSetbackChamferVertex(
    const BrepBody& Body, int Vertex, double LowerSetBack, double UpperSetBack) noexcept
{
    if (!std::isfinite(LowerSetBack) || !std::isfinite(UpperSetBack) ||
        LowerSetBack <= ScalarCriteria::MergeTolerance || UpperSetBack <= ScalarCriteria::MergeTolerance)
        return Deliver<EqualRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::DegenerateInput, "equal-radius support set-backs must be finite and positive");
    const double SetbackScale = std::max({ 1.0, LowerSetBack, UpperSetBack });
    if (std::fabs(LowerSetBack - UpperSetBack) <= ScalarCriteria::GeometricTolerance * SetbackScale)
        return Deliver<EqualRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::Unsupported, "equal support set-backs belong to the bounded Phase 39e route");
    const double ProbeSetBack = std::min(LowerSetBack, UpperSetBack);
    const Deliver<EqualRadiusBiconeApexChamferSpecification> Base =
        ClassifyEqualRadiusBiconeApexChamferVertex(Body, Vertex, ProbeSetBack);
    if (!Base) return Deliver<EqualRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
        Base.Denial.Reason, Base.Denial.Detail);
    EqualRadiusBiconeUnequalSetbackChamferSpecification Specification;
    Specification.Apex = Base.Payload.Apex;
    Specification.Axis = Base.Payload.Axis;
    Specification.Radius = Base.Payload.Radius;
    Specification.LowerHeight = Base.Payload.LowerHeight;
    Specification.UpperHeight = Base.Payload.UpperHeight;
    Specification.LowerSetBack = LowerSetBack;
    Specification.UpperSetBack = UpperSetBack;
    const Deliver<BrepBody> Feasible = ReconstructEqualRadiusBiconeUnequalSetbackChamfer(Specification);
    if (!Feasible) return Deliver<EqualRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
        Feasible.Denial.Reason, Feasible.Denial.Detail);
    return Deliver<EqualRadiusBiconeUnequalSetbackChamferSpecification>::Accept(std::move(Specification));
}

Deliver<BrepBody> BlendSolver::ReconstructUnequalConeApexFillet(
    const UnequalConeApexFilletSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Apex.X) || !std::isfinite(Specification.Apex.Y) ||
        !std::isfinite(Specification.Apex.Z) || !std::isfinite(Specification.LowerRadius) ||
        !std::isfinite(Specification.UpperRadius) || !std::isfinite(Specification.LowerHeight) ||
        !std::isfinite(Specification.UpperHeight) || !std::isfinite(Specification.FilletRadius) ||
        Specification.LowerRadius <= Tol || Specification.UpperRadius <= Tol ||
        Specification.LowerHeight <= Tol || Specification.UpperHeight <= Tol || Specification.FilletRadius <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "unequal bicone fillet dimensions must be finite and positive");
    if (!std::isfinite(Specification.Axis.X) || !std::isfinite(Specification.Axis.Y) ||
        !std::isfinite(Specification.Axis.Z) || Specification.Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "unequal bicone fillet axis is degenerate");
    const double RadiusScale = std::max({ 1.0, Specification.LowerRadius, Specification.UpperRadius });
    if (std::fabs(Specification.LowerRadius - Specification.UpperRadius) <=
        ScalarCriteria::GeometricTolerance * RadiusScale)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "unequal bicone fillet route requires distinct support radii");

    const Vec3 Axis = Specification.Axis.Normalised();
    const Vec3 Apex = Specification.Apex;
    const double LowerAngle = std::atan2(Specification.LowerRadius, Specification.LowerHeight);
    const double UpperAngle = std::atan2(Specification.UpperRadius, Specification.UpperHeight);
    const double SinSum = std::sin(LowerAngle + UpperAngle);
    if (SinSum <= ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "unequal bicone fillet support angles are degenerate");
    const double SinLower = std::sin(LowerAngle), SinUpper = std::sin(UpperAngle);
    const double CosLower = std::cos(LowerAngle), CosUpper = std::cos(UpperAngle);
    const double Q = Specification.FilletRadius;
    const double Major = Q * (SinLower + SinUpper) / SinSum;
    const double CentreOffset = Q * (CosUpper - CosLower) / SinSum;
    if (Major <= Q + Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "unequal bicone torus would cross the axis");
    const double LowerTangent = Major * SinLower - CentreOffset * CosLower;
    const double UpperTangent = Major * SinUpper + CentreOffset * CosUpper;
    const double LowerSlant = std::hypot(Specification.LowerHeight, Specification.LowerRadius);
    const double UpperSlant = std::hypot(Specification.UpperHeight, Specification.UpperRadius);
    if (LowerTangent <= Tol || UpperTangent <= Tol || LowerTangent >= LowerSlant - Tol ||
        UpperTangent >= UpperSlant - Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "unequal bicone torus contact consumes a conical support");

    const Vec3 Radial = Workplane::FromNormal(Apex, Axis).AxisX.Normalised();
    if (Radial.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "unequal bicone fillet radial frame is degenerate");
    const Vec3 LowerBase = Apex - Axis * Specification.LowerHeight;
    const Vec3 UpperBase = Apex + Axis * Specification.UpperHeight;
    const Vec3 LowerContact = Apex - Axis * (LowerTangent * CosLower) + Radial * (LowerTangent * SinLower);
    const Vec3 UpperContact = Apex + Axis * (UpperTangent * CosUpper) + Radial * (UpperTangent * SinUpper);
    const Vec3 TubeCentre = Apex + Axis * CentreOffset + Radial * Major;
    const Vec3 InnerPoint = Apex + Axis * CentreOffset + Radial * (Major - Q);
    const double LowerContactRadius = LowerTangent * SinLower;
    const double UpperContactRadius = UpperTangent * SinUpper;
    if (LowerContactRadius <= Tol || UpperContactRadius <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "unequal bicone torus contact ring is degenerate");

    Deliver<NurbsCurve> LowerProfile = NurbsCurve::Line(LowerBase + Radial * Specification.LowerRadius, LowerContact);
    Deliver<NurbsCurve> UpperProfile = NurbsCurve::Line(UpperContact, UpperBase + Radial * Specification.UpperRadius);
    Deliver<NurbsCurve> TorusProfile = NurbsCurve::ArcThreePoints(LowerContact, InnerPoint, UpperContact);
    Deliver<NurbsCurve> LowerDiskProfile = NurbsCurve::Line(LowerBase, LowerBase + Radial * Specification.LowerRadius);
    Deliver<NurbsCurve> UpperDiskProfile = NurbsCurve::Line(UpperBase, UpperBase + Radial * Specification.UpperRadius);
    if (!LowerProfile || !UpperProfile || !TorusProfile || !LowerDiskProfile || !UpperDiskProfile)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "unequal bicone torus profiles are degenerate");
    auto Revolve = [&](const Deliver<NurbsCurve>& Profile) -> Deliver<NurbsSurface>
    {
        return NurbsSurface::Revolution(Profile.Payload, Apex, Axis, ScalarCriteria::TwoPi);
    };
    Deliver<NurbsSurface> Lower = Revolve(LowerProfile);
    Deliver<NurbsSurface> Upper = Revolve(UpperProfile);
    Deliver<NurbsSurface> Torus = Revolve(TorusProfile);
    Deliver<NurbsSurface> LowerDisk = Revolve(LowerDiskProfile);
    Deliver<NurbsSurface> UpperDisk = Revolve(UpperDiskProfile);
    if (!Lower || !Upper || !Torus || !LowerDisk || !UpperDisk)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "unequal bicone torus surfaces are degenerate");

    Lower.Payload.Classification = SurfaceClassification::Cone;
    Lower.Payload.Origin = LowerBase; Lower.Payload.Axis = Axis;
    Lower.Payload.RadiusMajor = Specification.LowerRadius; Lower.Payload.RadiusMinor = LowerContactRadius;
    Lower.Payload.HalfAngle = std::atan2(Specification.LowerRadius - LowerContactRadius,
                                         Specification.LowerHeight - LowerTangent * CosLower);
    Upper.Payload.Classification = SurfaceClassification::Cone;
    Upper.Payload.Origin = UpperContact; Upper.Payload.Axis = Axis;
    Upper.Payload.RadiusMajor = UpperContactRadius; Upper.Payload.RadiusMinor = Specification.UpperRadius;
    Upper.Payload.HalfAngle = std::atan2(Specification.UpperRadius - UpperContactRadius,
                                         Specification.UpperHeight - UpperTangent * CosUpper);
    Torus.Payload.Classification = SurfaceClassification::Torus;
    Torus.Payload.Origin = Apex + Axis * CentreOffset; Torus.Payload.Axis = Axis;
    Torus.Payload.RadiusMajor = Major; Torus.Payload.RadiusMinor = Q;
    LowerDisk.Payload.Classification = SurfaceClassification::Plane;
    LowerDisk.Payload.Origin = LowerBase; LowerDisk.Payload.Axis = Axis;
    UpperDisk.Payload.Classification = SurfaceClassification::Plane;
    UpperDisk.Payload.Origin = UpperBase; UpperDisk.Payload.Axis = Axis;

    Deliver<BrepBody> Result = BrepBody::Sew({ Lower.Payload, Torus.Payload, Upper.Payload,
                                                LowerDisk.Payload, UpperDisk.Payload }, Tol, true);
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason,
                                                   "unequal bicone torus surfaces could not be sewn");

    const double LowerDepth = LowerTangent * CosLower;
    const double UpperDepth = UpperTangent * CosUpper;
    const double LowerRetained = Specification.LowerHeight - LowerDepth;
    const double UpperRetained = Specification.UpperHeight - UpperDepth;
    const double ThetaLower = std::atan2(-LowerTangent * CosLower - CentreOffset,
                                          LowerContactRadius - Major);
    double ThetaUpper = std::atan2(UpperTangent * CosUpper - CentreOffset,
                                   UpperContactRadius - Major);
    double ThetaStart = ThetaLower;
    while (ThetaUpper >= ThetaStart) ThetaUpper -= ScalarCriteria::TwoPi;
    const double ArcSpan = ThetaUpper - ThetaStart;
    if (ArcSpan >= -ScalarCriteria::AngularTolerance || ArcSpan < -ScalarCriteria::Pi - ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence,
                                          "unequal bicone torus meridian selected an invalid contact arc");
    auto TorusIntegral = [&](double Theta) noexcept
    {
        const double Sine = std::sin(Theta);
        return Major * Major * Sine + Major * Q * (Theta + std::sin(2.0 * Theta) / 2.0) +
               Q * Q * (Sine - Sine * Sine * Sine / 3.0);
    };
    const double MeridionalIntegral = Q * (TorusIntegral(ThetaUpper) - TorusIntegral(ThetaStart));
    const double ExpectedVolume =
        ScalarCriteria::Pi * LowerRetained *
            (Specification.LowerRadius * Specification.LowerRadius +
             Specification.LowerRadius * LowerContactRadius + LowerContactRadius * LowerContactRadius) / 3.0 +
        ScalarCriteria::Pi * MeridionalIntegral +
        ScalarCriteria::Pi * UpperRetained *
            (UpperContactRadius * UpperContactRadius + UpperContactRadius * Specification.UpperRadius +
             Specification.UpperRadius * Specification.UpperRadius) / 3.0;
    const BodyReport Report = Result.Payload.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 ||
        Result.Payload.Vertices.size() != 6 || Result.Payload.Edges.size() != 9 ||
        Result.Payload.Coedges.size() != 18 || Result.Payload.Loops.size() != 5 || Result.Payload.Faces.size() != 5)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                                          "unequal bicone torus fillet did not reach V6/E9/C18/L5/F5 topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence,
                                          "unequal bicone torus fillet volume failed analytic acceptance");
    (void)TubeCentre;
    return Result;
}

Deliver<UnequalConeApexFilletSpecification> BlendSolver::ClassifyUnequalConeApexFilletVertex(
    const BrepBody& Body, int Vertex, double FilletRadius) noexcept
{
    if (!std::isfinite(FilletRadius) || FilletRadius <= ScalarCriteria::MergeTolerance)
        return Deliver<UnequalConeApexFilletSpecification>::Reject(RefusalReason::DegenerateInput,
                                                                     "unequal bicone fillet radius must be finite and positive");
    const Box3 SourceBounds = Body.Bounds();
    const double ProbeSetBack = std::max(ScalarCriteria::MergeTolerance * 10.0,
                                         SourceBounds.High.Distance(SourceBounds.Low) * 1e-3);
    const Deliver<UnequalConeApexChamferSpecification> Base =
        ClassifyUnequalConeApexChamferVertex(Body, Vertex, ProbeSetBack);
    if (!Base) return Deliver<UnequalConeApexFilletSpecification>::Reject(Base.Denial.Reason, Base.Denial.Detail);
    UnequalConeApexFilletSpecification Specification;
    Specification.Apex = Base.Payload.Apex;
    Specification.Axis = Base.Payload.Axis;
    Specification.LowerRadius = Base.Payload.LowerRadius;
    Specification.UpperRadius = Base.Payload.UpperRadius;
    Specification.LowerHeight = Base.Payload.LowerHeight;
    Specification.UpperHeight = Base.Payload.UpperHeight;
    Specification.FilletRadius = FilletRadius;
    const Deliver<BrepBody> Feasible = ReconstructUnequalConeApexFillet(Specification);
    if (!Feasible) return Deliver<UnequalConeApexFilletSpecification>::Reject(Feasible.Denial.Reason,
                                                                                 Feasible.Denial.Detail);
    return Deliver<UnequalConeApexFilletSpecification>::Accept(std::move(Specification));
}

Deliver<BrepBody> BlendSolver::ReconstructEqualRadiusBiconeApexFillet(
    const EqualRadiusBiconeApexFilletSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Apex.X) || !std::isfinite(Specification.Apex.Y) ||
        !std::isfinite(Specification.Apex.Z) || !std::isfinite(Specification.Radius) ||
        !std::isfinite(Specification.LowerHeight) || !std::isfinite(Specification.UpperHeight) ||
        !std::isfinite(Specification.FilletRadius) || Specification.Radius <= Tol ||
        Specification.LowerHeight <= Tol || Specification.UpperHeight <= Tol || Specification.FilletRadius <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "equal bicone fillet dimensions must be finite and positive");
    if (!std::isfinite(Specification.Axis.X) || !std::isfinite(Specification.Axis.Y) ||
        !std::isfinite(Specification.Axis.Z) || Specification.Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "equal bicone fillet axis is degenerate");
    const Vec3 Axis = Specification.Axis.Normalised();
    const Vec3 Apex = Specification.Apex;
    const double LowerAngle = std::atan2(Specification.Radius, Specification.LowerHeight);
    const double UpperAngle = std::atan2(Specification.Radius, Specification.UpperHeight);
    const double SinSum = std::sin(LowerAngle + UpperAngle);
    if (SinSum <= ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "equal bicone fillet support angles are degenerate");
    const double SinLower = std::sin(LowerAngle), SinUpper = std::sin(UpperAngle);
    const double CosLower = std::cos(LowerAngle), CosUpper = std::cos(UpperAngle);
    const double Q = Specification.FilletRadius;
    const double Major = Q * (SinLower + SinUpper) / SinSum;
    const double CentreOffset = Q * (CosUpper - CosLower) / SinSum;
    if (Major <= Q + Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "equal bicone torus would cross the axis");
    const double LowerTangent = Major * SinLower - CentreOffset * CosLower;
    const double UpperTangent = Major * SinUpper + CentreOffset * CosUpper;
    if (LowerTangent <= Tol || UpperTangent <= Tol ||
        LowerTangent >= std::hypot(Specification.LowerHeight, Specification.Radius) - Tol ||
        UpperTangent >= std::hypot(Specification.UpperHeight, Specification.Radius) - Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "equal bicone torus consumes a support");
    const Vec3 Radial = Workplane::FromNormal(Apex, Axis).AxisX.Normalised();
    if (Radial.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "equal bicone fillet radial frame is degenerate");
    const Vec3 LowerBase = Apex - Axis * Specification.LowerHeight;
    const Vec3 UpperBase = Apex + Axis * Specification.UpperHeight;
    const double LowerContactRadius = LowerTangent * SinLower;
    const double UpperContactRadius = UpperTangent * SinUpper;
    const Vec3 LowerContact = Apex - Axis * (LowerTangent * CosLower) + Radial * LowerContactRadius;
    const Vec3 UpperContact = Apex + Axis * (UpperTangent * CosUpper) + Radial * UpperContactRadius;
    const Vec3 InnerPoint = Apex + Axis * CentreOffset + Radial * (Major - Q);
    auto Line = [&](Vec3 Start, Vec3 End) -> Deliver<NurbsCurve> { return NurbsCurve::Line(Start, End); };
    Deliver<NurbsCurve> LowerProfile = Line(LowerBase + Radial * Specification.Radius, LowerContact);
    Deliver<NurbsCurve> UpperProfile = Line(UpperContact, UpperBase + Radial * Specification.Radius);
    Deliver<NurbsCurve> TorusProfile = NurbsCurve::ArcThreePoints(LowerContact, InnerPoint, UpperContact);
    Deliver<NurbsCurve> LowerDiskProfile = Line(LowerBase, LowerBase + Radial * Specification.Radius);
    Deliver<NurbsCurve> UpperDiskProfile = Line(UpperBase, UpperBase + Radial * Specification.Radius);
    if (!LowerProfile || !UpperProfile || !TorusProfile || !LowerDiskProfile || !UpperDiskProfile)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "equal bicone fillet profiles are degenerate");
    auto Revolve = [&](const Deliver<NurbsCurve>& Profile) -> Deliver<NurbsSurface>
    {
        return NurbsSurface::Revolution(Profile.Payload, Apex, Axis, ScalarCriteria::TwoPi);
    };
    Deliver<NurbsSurface> Lower = Revolve(LowerProfile);
    Deliver<NurbsSurface> Upper = Revolve(UpperProfile);
    Deliver<NurbsSurface> Torus = Revolve(TorusProfile);
    Deliver<NurbsSurface> LowerDisk = Revolve(LowerDiskProfile);
    Deliver<NurbsSurface> UpperDisk = Revolve(UpperDiskProfile);
    if (!Lower || !Upper || !Torus || !LowerDisk || !UpperDisk)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "equal bicone fillet surfaces are degenerate");
    Lower.Payload.Classification = SurfaceClassification::Cone;
    Lower.Payload.Origin = LowerBase; Lower.Payload.Axis = Axis;
    Lower.Payload.RadiusMajor = Specification.Radius; Lower.Payload.RadiusMinor = LowerContactRadius;
    Upper.Payload.Classification = SurfaceClassification::Cone;
    Upper.Payload.Origin = UpperContact; Upper.Payload.Axis = Axis;
    Upper.Payload.RadiusMajor = UpperContactRadius; Upper.Payload.RadiusMinor = Specification.Radius;
    Torus.Payload.Classification = SurfaceClassification::Torus;
    Torus.Payload.Origin = Apex + Axis * CentreOffset; Torus.Payload.Axis = Axis;
    Torus.Payload.RadiusMajor = Major; Torus.Payload.RadiusMinor = Q;
    LowerDisk.Payload.Classification = SurfaceClassification::Plane;
    LowerDisk.Payload.Origin = LowerBase; LowerDisk.Payload.Axis = Axis;
    UpperDisk.Payload.Classification = SurfaceClassification::Plane;
    UpperDisk.Payload.Origin = UpperBase; UpperDisk.Payload.Axis = Axis;
    Deliver<BrepBody> Result = BrepBody::Sew({ Lower.Payload, Torus.Payload, Upper.Payload,
                                                LowerDisk.Payload, UpperDisk.Payload }, Tol, true);
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, "equal bicone fillet surfaces could not be sewn");

    const double LowerDepth = LowerTangent * CosLower, UpperDepth = UpperTangent * CosUpper;
    const double ThetaLower = std::atan2(-LowerDepth - CentreOffset, LowerContactRadius - Major);
    double ThetaUpper = std::atan2(UpperDepth - CentreOffset, UpperContactRadius - Major);
    while (ThetaUpper >= ThetaLower) ThetaUpper -= ScalarCriteria::TwoPi;
    const auto Primitive = [&](double Theta) noexcept
    {
        const double Sine = std::sin(Theta);
        return Major * Major * Sine + Major * Q * (Theta + std::sin(2.0 * Theta) / 2.0) +
               Q * Q * (Sine - Sine * Sine * Sine / 3.0);
    };
    const double MeridionalIntegral = Q * (Primitive(ThetaUpper) - Primitive(ThetaLower));
    const double ExpectedVolume =
        ScalarCriteria::Pi * (Specification.LowerHeight - LowerDepth) *
            (Specification.Radius * Specification.Radius + Specification.Radius * LowerContactRadius +
             LowerContactRadius * LowerContactRadius) / 3.0 +
        ScalarCriteria::Pi * MeridionalIntegral +
        ScalarCriteria::Pi * (Specification.UpperHeight - UpperDepth) *
            (UpperContactRadius * UpperContactRadius + UpperContactRadius * Specification.Radius +
             Specification.Radius * Specification.Radius) / 3.0;
    const BodyReport Report = Result.Payload.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Result.Payload.Vertices.size() != 6 ||
        Result.Payload.Edges.size() != 9 || Result.Payload.Coedges.size() != 18 || Result.Payload.Loops.size() != 5 ||
        Result.Payload.Faces.size() != 5)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "equal bicone fillet did not reach V6/E9/C18/L5/F5 topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence, "equal bicone fillet volume failed analytic acceptance");
    return Result;
}

Deliver<EqualRadiusBiconeApexFilletSpecification> BlendSolver::ClassifyEqualRadiusBiconeApexFilletVertex(
    const BrepBody& Body, int Vertex, double FilletRadius) noexcept
{
    if (!std::isfinite(FilletRadius) || FilletRadius <= ScalarCriteria::MergeTolerance)
        return Deliver<EqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::DegenerateInput, "equal bicone fillet radius must be finite and positive");
    const BodyReport Report = Body.Validate();
    if (!Report.Solid() || Report.Hulls != 2 || Report.Genus != 1 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Body.Vertices.size() != 5 ||
        Body.Edges.size() != 6 || Body.Coedges.size() != 12 || Body.Loops.size() != 4 || Body.Faces.size() != 4)
        return Deliver<EqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::NonManifold, "source is not the canonical equal bicone topology");
    if (Vertex < 0 || Vertex >= static_cast<int>(Body.Vertices.size()))
        return Deliver<EqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported, "selected equal bicone vertex is out of range");
    for (const BrepFace& Face : Body.Faces)
        if (Face.Loops.size() != 1 || Face.Surface.Classification != SurfaceClassification::Revolution)
            return Deliver<EqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported, "source is not the exact native equal bicone revolve set");
    std::vector<int> Rims, Lines;
    for (size_t I = 0; I < Body.Edges.size(); ++I)
    {
        const BrepEdge& EdgeData = Body.Edges[I];
        if (EdgeData.Coedges.size() != 2) return Deliver<EqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::NonManifold, "equal bicone edge is not manifold");
        if (EdgeData.Closed() && EdgeData.Curve.Classification == CurveClassification::Circle && EdgeData.Curve.Degree == 2 && EdgeData.Curve.Rational()) Rims.push_back(static_cast<int>(I));
        else if (!EdgeData.Closed() && EdgeData.Curve.Classification == CurveClassification::Line && EdgeData.Curve.Degree == 1) Lines.push_back(static_cast<int>(I));
        else return Deliver<EqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported, "equal bicone has unsupported edge geometry");
    }
    if (Rims.size() != 2 || Lines.size() != 4) return Deliver<EqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported, "equal bicone does not have two rims and four generators");
    const int RimVertexA = Body.Edges[Rims[0]].VertexStart, RimVertexB = Body.Edges[Rims[1]].VertexStart;
    int Apex = -1;
    for (int Candidate = 0; Candidate < static_cast<int>(Body.Vertices.size()); ++Candidate)
    {
        if (Candidate == RimVertexA || Candidate == RimVertexB) continue;
        bool ToA = false, ToB = false;
        for (int Edge : Lines)
        {
            const BrepEdge& Data = Body.Edges[Edge];
            const int Other = Data.VertexStart == Candidate ? Data.VertexEnd : (Data.VertexEnd == Candidate ? Data.VertexStart : -1);
            ToA = ToA || Other == RimVertexA; ToB = ToB || Other == RimVertexB;
        }
        if (ToA && ToB) { if (Apex >= 0) return Deliver<EqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported, "equal bicone has multiple apex vertices"); Apex = Candidate; }
    }
    if (Apex < 0 || Vertex != Apex) return Deliver<EqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported, "selected vertex is not the equal bicone apex");
    struct Rim { Vec3 Centre, Normal; double Radius = 0.0; } G[2];
    for (int K = 0; K < 2; ++K)
    {
        if (!CircularFrame(Body.Edges[Rims[K]].Curve, G[K].Centre, G[K].Normal, G[K].Radius)) return Deliver<EqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported, "equal bicone rim is not circular");
        if (G[K].Radius <= Tol) return Deliver<EqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported, "equal bicone rim radius is degenerate");
    }
    const double RadiusScale = std::max(1.0, G[0].Radius);
    if (std::fabs(G[0].Radius - G[1].Radius) > ScalarCriteria::GeometricTolerance * RadiusScale)
        return Deliver<EqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported, "source radii are unequal; use the unequal bicone route");
    Vec3 Axis = (G[1].Centre - G[0].Centre).Normalised();
    if (Axis.Length() <= ScalarCriteria::GeometricTolerance) return Deliver<EqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::DegenerateInput, "equal bicone axis is degenerate");
    if ((std::fabs(Axis.X) > ScalarCriteria::GeometricTolerance && Axis.X < 0.0) ||
        (std::fabs(Axis.X) <= ScalarCriteria::GeometricTolerance && std::fabs(Axis.Y) > ScalarCriteria::GeometricTolerance && Axis.Y < 0.0) ||
        (std::fabs(Axis.X) <= ScalarCriteria::GeometricTolerance && std::fabs(Axis.Y) <= ScalarCriteria::GeometricTolerance && Axis.Z < 0.0)) Axis = -Axis;
    for (const Rim& R : G)
        if (std::fabs(std::fabs(R.Normal.Dot(Axis)) - 1.0) > ScalarCriteria::AngularTolerance ||
            (R.Centre - Body.Vertices[Apex].Point - Axis * (R.Centre - Body.Vertices[Apex].Point).Dot(Axis)).Length() > ScalarCriteria::GeometricTolerance * std::max(1.0, R.Centre.Distance(Body.Vertices[Apex].Point)))
            return Deliver<EqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported, "equal bicone rims are not coaxial with the apex");
    if ((G[0].Centre - G[1].Centre).Dot(Axis) > 0.0) std::swap(G[0], G[1]);
    EqualRadiusBiconeApexFilletSpecification Specification;
    Specification.Apex = Body.Vertices[Apex].Point; Specification.Axis = Axis; Specification.Radius = G[0].Radius;
    Specification.LowerHeight = Specification.Apex.Distance(G[0].Centre); Specification.UpperHeight = Specification.Apex.Distance(G[1].Centre);
    if (Specification.LowerHeight <= Tol || Specification.UpperHeight <= Tol) return Deliver<EqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported, "equal bicone support height is degenerate");
    Specification.FilletRadius = FilletRadius;
    const Deliver<BrepBody> Final = ReconstructEqualRadiusBiconeApexFillet(Specification);
    if (!Final) return Deliver<EqualRadiusBiconeApexFilletSpecification>::Reject(Final.Denial.Reason, Final.Denial.Detail);
    return Deliver<EqualRadiusBiconeApexFilletSpecification>::Accept(std::move(Specification));
}

Deliver<BrepBody> BlendSolver::ReconstructPartialUnequalConeApexChamfer(
    const PartialUnequalConeApexChamferSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Apex.X) || !std::isfinite(Specification.Apex.Y) ||
        !std::isfinite(Specification.Apex.Z) || !std::isfinite(Specification.LowerRadius) ||
        !std::isfinite(Specification.UpperRadius) || !std::isfinite(Specification.LowerHeight) ||
        !std::isfinite(Specification.UpperHeight) || !std::isfinite(Specification.SetBack) ||
        !std::isfinite(Specification.SweepAngle) || Specification.LowerRadius <= Tol ||
        Specification.UpperRadius <= Tol || Specification.LowerHeight <= Tol ||
        Specification.UpperHeight <= Tol || Specification.SetBack <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "partial unequal bicone dimensions must be finite and positive");
    if (!std::isfinite(Specification.Axis.X) || !std::isfinite(Specification.Axis.Y) ||
        !std::isfinite(Specification.Axis.Z) || Specification.Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial unequal bicone axis is degenerate");
    if (Specification.SweepAngle <= ScalarCriteria::AngularTolerance ||
        Specification.SweepAngle >= ScalarCriteria::Pi - ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "partial unequal bicone route requires a strict non-reflex sector");
    const double RadiusScale = std::max({ 1.0, Specification.LowerRadius, Specification.UpperRadius });
    if (std::fabs(Specification.LowerRadius - Specification.UpperRadius) <=
        ScalarCriteria::GeometricTolerance * RadiusScale)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "partial unequal bicone route requires distinct support radii");
    if (Specification.SetBack >= std::min(Specification.LowerHeight, Specification.UpperHeight) - Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "partial unequal bicone set-back consumes a support");
    const Vec3 Apex = Specification.Apex;
    const Vec3 Axis = Specification.Axis.Normalised();
    const Vec3 Radial = Workplane::FromNormal(Apex, Axis).AxisX.Normalised();
    if (Radial.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial unequal bicone radial frame is degenerate");
    const Vec3 LowerBase = Apex - Axis * Specification.LowerHeight;
    const Vec3 UpperBase = Apex + Axis * Specification.UpperHeight;
    const Vec3 LowerContact = Apex - Axis * Specification.SetBack;
    const Vec3 UpperContact = Apex + Axis * Specification.SetBack;
    const double LowerContactRadius = Specification.LowerRadius * Specification.SetBack / Specification.LowerHeight;
    const double UpperContactRadius = Specification.UpperRadius * Specification.SetBack / Specification.UpperHeight;
    if (LowerContactRadius <= Tol || UpperContactRadius <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial unequal bicone contact ring is degenerate");
    auto RevolveLine = [&](Vec3 Start, Vec3 End) -> Deliver<NurbsSurface>
    {
        Deliver<NurbsCurve> Line = NurbsCurve::Line(Start, End);
        return Line ? NurbsSurface::Revolution(Line.Payload, Apex, Axis, Specification.SweepAngle)
                    : Deliver<NurbsSurface>::Reject(Line.Denial.Reason, Line.Denial.Detail);
    };
    Deliver<NurbsSurface> Lower = RevolveLine(LowerBase + Radial * Specification.LowerRadius,
                                               LowerContact + Radial * LowerContactRadius);
    Deliver<NurbsSurface> Chamfer = RevolveLine(LowerContact + Radial * LowerContactRadius,
                                                UpperContact + Radial * UpperContactRadius);
    Deliver<NurbsSurface> Upper = RevolveLine(UpperContact + Radial * UpperContactRadius,
                                               UpperBase + Radial * Specification.UpperRadius);
    Deliver<NurbsSurface> LowerDisk = RevolveLine(LowerBase, LowerBase + Radial * Specification.LowerRadius);
    Deliver<NurbsSurface> UpperDisk = RevolveLine(UpperBase, UpperBase + Radial * Specification.UpperRadius);
    if (!Lower || !Chamfer || !Upper || !LowerDisk || !UpperDisk)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial unequal bicone surfaces are degenerate");
    Lower.Payload.Classification = SurfaceClassification::Cone;
    Lower.Payload.Origin = LowerBase; Lower.Payload.Axis = Axis;
    Lower.Payload.RadiusMajor = Specification.LowerRadius; Lower.Payload.RadiusMinor = LowerContactRadius;
    Chamfer.Payload.Classification = SurfaceClassification::Cone;
    Chamfer.Payload.Origin = LowerContact; Chamfer.Payload.Axis = Axis;
    Chamfer.Payload.RadiusMajor = LowerContactRadius; Chamfer.Payload.RadiusMinor = UpperContactRadius;
    Upper.Payload.Classification = SurfaceClassification::Cone;
    Upper.Payload.Origin = UpperContact; Upper.Payload.Axis = Axis;
    Upper.Payload.RadiusMajor = UpperContactRadius; Upper.Payload.RadiusMinor = Specification.UpperRadius;
    LowerDisk.Payload.Classification = SurfaceClassification::Plane;
    LowerDisk.Payload.Origin = LowerBase; LowerDisk.Payload.Axis = Axis;
    UpperDisk.Payload.Classification = SurfaceClassification::Plane;
    UpperDisk.Payload.Origin = UpperBase; UpperDisk.Payload.Axis = Axis;
    Deliver<BrepBody> Result = BrepBody::Sew({ Lower.Payload, Chamfer.Payload, Upper.Payload,
                                                LowerDisk.Payload, UpperDisk.Payload }, Tol, true);
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, "partial unequal bicone surfaces could not be sewn");
    if (!CapRadialSector(Result.Payload, LowerBase, UpperBase, Radial, Specification.SweepAngle,
                         std::max(Specification.LowerRadius, Specification.UpperRadius)) || !Result.Payload.Orient())
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "partial unequal bicone radial caps could not heal");
    const double LowerRetained = Specification.LowerHeight - Specification.SetBack;
    const double UpperRetained = Specification.UpperHeight - Specification.SetBack;
    const double ExpectedVolume =
        (ScalarCriteria::Pi * LowerRetained *
            (Specification.LowerRadius * Specification.LowerRadius + Specification.LowerRadius * LowerContactRadius +
             LowerContactRadius * LowerContactRadius) / 3.0 +
         ScalarCriteria::Pi * 2.0 * Specification.SetBack *
            (LowerContactRadius * LowerContactRadius + LowerContactRadius * UpperContactRadius +
             UpperContactRadius * UpperContactRadius) / 3.0 +
         ScalarCriteria::Pi * UpperRetained *
            (UpperContactRadius * UpperContactRadius + UpperContactRadius * Specification.UpperRadius +
             Specification.UpperRadius * Specification.UpperRadius) / 3.0) *
        Specification.SweepAngle / ScalarCriteria::TwoPi;
    const BodyReport Report = Result.Payload.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Result.Payload.Vertices.size() != 10 ||
        Result.Payload.Edges.size() != 15 || Result.Payload.Coedges.size() != 30 || Result.Payload.Loops.size() != 7 ||
        Result.Payload.Faces.size() != 7)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                                          "partial unequal bicone did not reach V10/E15/C30/L7/F7 topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence, "partial unequal bicone volume failed analytic acceptance");
    return Result;
}

Deliver<PartialUnequalConeApexChamferSpecification>
BlendSolver::ClassifyPartialUnequalConeApexChamferVertex(const BrepBody& Body, int Vertex, double SetBack) noexcept
{
    if (!std::isfinite(SetBack) || SetBack <= ScalarCriteria::MergeTolerance)
        return Deliver<PartialUnequalConeApexChamferSpecification>::Reject(RefusalReason::DegenerateInput, "partial unequal bicone set-back must be finite and positive");
    const BodyReport Report = Body.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Body.Vertices.size() != 7 ||
        Body.Edges.size() != 11 || Body.Coedges.size() != 22 || Body.Loops.size() != 6 || Body.Faces.size() != 6)
        return Deliver<PartialUnequalConeApexChamferSpecification>::Reject(RefusalReason::NonManifold, "source is not the capped partial unequal bicone topology");
    if (Vertex < 0 || Vertex >= static_cast<int>(Body.Vertices.size()))
        return Deliver<PartialUnequalConeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "selected partial unequal bicone vertex is out of range");
    int RevolutionFaces = 0, PlaneFaces = 0;
    for (const BrepFace& Face : Body.Faces)
    {
        if (Face.Loops.size() != 1) return Deliver<PartialUnequalConeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "partial unequal bicone face is not single-loop");
        if (Face.Surface.Classification == SurfaceClassification::Revolution) ++RevolutionFaces;
        else if (Face.Surface.Classification == SurfaceClassification::Plane) ++PlaneFaces;
        else return Deliver<PartialUnequalConeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "partial unequal bicone has unsupported face geometry");
    }
    if (RevolutionFaces != 4 || PlaneFaces != 2)
        return Deliver<PartialUnequalConeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "partial unequal bicone does not have four revolve faces and two radial caps");
    std::vector<int> RimEdges, LineEdges;
    for (size_t I = 0; I < Body.Edges.size(); ++I)
    {
        const BrepEdge& EdgeData = Body.Edges[I];
        if (EdgeData.Coedges.size() != 2) return Deliver<PartialUnequalConeApexChamferSpecification>::Reject(RefusalReason::NonManifold, "partial unequal bicone edge is not manifold");
        if (!EdgeData.Closed() && EdgeData.Curve.Classification == CurveClassification::Arc && EdgeData.Curve.Degree == 2 && EdgeData.Curve.Rational()) RimEdges.push_back(static_cast<int>(I));
        else if (!EdgeData.Closed() && EdgeData.Curve.Classification == CurveClassification::Line && EdgeData.Curve.Degree == 1) LineEdges.push_back(static_cast<int>(I));
        else return Deliver<PartialUnequalConeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "partial unequal bicone has unsupported edge geometry");
    }
    if (RimEdges.size() != 2 || LineEdges.size() != 9)
        return Deliver<PartialUnequalConeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "partial unequal bicone does not have two arc rims and nine lines");
    int Apex = -1;
    for (int Candidate = 0; Candidate < static_cast<int>(Body.Vertices.size()); ++Candidate)
    {
        int Degree = 0;
        for (int Edge : LineEdges) if (Body.Edges[Edge].VertexStart == Candidate || Body.Edges[Edge].VertexEnd == Candidate) ++Degree;
        if (Degree == 4) { if (Apex >= 0) return Deliver<PartialUnequalConeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "partial unequal bicone has multiple apex candidates"); Apex = Candidate; }
    }
    if (Apex < 0 || Vertex != Apex) return Deliver<PartialUnequalConeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "selected vertex is not the partial unequal bicone apex");
    struct Rim { Vec3 Centre, Normal; double Radius = 0.0; int Edge = -1; } G[2];
    for (int K = 0; K < 2; ++K)
    {
        G[K].Edge = RimEdges[K];
        if (!CircularFrame(Body.Edges[RimEdges[K]].Curve, G[K].Centre, G[K].Normal, G[K].Radius) || G[K].Radius <= Tol)
            return Deliver<PartialUnequalConeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "partial unequal bicone rim is not circular");
    }
    Vec3 Axis = (G[1].Centre - G[0].Centre).Normalised();
    if (Axis.Length() <= ScalarCriteria::GeometricTolerance) return Deliver<PartialUnequalConeApexChamferSpecification>::Reject(RefusalReason::DegenerateInput, "partial unequal bicone axis is degenerate");
    if ((std::fabs(Axis.X) > ScalarCriteria::GeometricTolerance && Axis.X < 0.0) ||
        (std::fabs(Axis.X) <= ScalarCriteria::GeometricTolerance && std::fabs(Axis.Y) > ScalarCriteria::GeometricTolerance && Axis.Y < 0.0) ||
        (std::fabs(Axis.X) <= ScalarCriteria::GeometricTolerance && std::fabs(Axis.Y) <= ScalarCriteria::GeometricTolerance && Axis.Z < 0.0)) Axis = -Axis;
    for (const Rim& R : G)
        if (std::fabs(std::fabs(R.Normal.Dot(Axis)) - 1.0) > ScalarCriteria::AngularTolerance ||
            (R.Centre - Body.Vertices[Apex].Point - Axis * (R.Centre - Body.Vertices[Apex].Point).Dot(Axis)).Length() > ScalarCriteria::GeometricTolerance * std::max(1.0, R.Centre.Distance(Body.Vertices[Apex].Point)))
            return Deliver<PartialUnequalConeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "partial unequal bicone rims are not coaxial");
    if ((G[0].Centre - G[1].Centre).Dot(Axis) > 0.0) std::swap(G[0], G[1]);
    const Vec3 ApexPoint = Body.Vertices[Apex].Point;
    const Vec3 StartVector = (Body.Vertices[Body.Edges[G[0].Edge].VertexStart].Point - G[0].Centre).Normalised();
    const Vec3 EndVector = (Body.Vertices[Body.Edges[G[0].Edge].VertexEnd].Point - G[0].Centre).Normalised();
    const double Sweep = std::atan2(Axis.Dot(StartVector.Cross(EndVector)), ScalarCriteria::Clamp(StartVector.Dot(EndVector), -1.0, 1.0));
    const Vec3 CanonicalRadial = Workplane::FromNormal(ApexPoint, Axis).AxisX.Normalised();
    if (Sweep <= ScalarCriteria::AngularTolerance || Sweep >= ScalarCriteria::Pi - ScalarCriteria::AngularTolerance ||
        CanonicalRadial.Distance(StartVector) > ScalarCriteria::GeometricTolerance)
        return Deliver<PartialUnequalConeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "partial unequal bicone sweep is not the bounded canonical sector");
    for (const BrepFace& Face : Body.Faces)
        if (Face.Surface.Classification == SurfaceClassification::Plane)
        {
            Vec3 Normal;
            if (!PlanarNormal(Body, static_cast<int>(&Face - Body.Faces.data()), Normal) || std::fabs(Normal.Dot(Axis)) > ScalarCriteria::AngularTolerance)
                return Deliver<PartialUnequalConeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "partial unequal bicone radial cap is not planar");
        }
    const double RadiusScale = std::max({ 1.0, G[0].Radius, G[1].Radius });
    if (std::fabs(G[0].Radius - G[1].Radius) <= ScalarCriteria::GeometricTolerance * RadiusScale)
        return Deliver<PartialUnequalConeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "partial route requires unequal rim radii");
    PartialUnequalConeApexChamferSpecification Specification;
    Specification.Apex = ApexPoint; Specification.Axis = Axis; Specification.LowerRadius = G[0].Radius; Specification.UpperRadius = G[1].Radius;
    Specification.LowerHeight = ApexPoint.Distance(G[0].Centre); Specification.UpperHeight = ApexPoint.Distance(G[1].Centre);
    Specification.SetBack = SetBack; Specification.SweepAngle = Sweep;
    const Deliver<BrepBody> Feasible = ReconstructPartialUnequalConeApexChamfer(Specification);
    if (!Feasible) return Deliver<PartialUnequalConeApexChamferSpecification>::Reject(Feasible.Denial.Reason, Feasible.Denial.Detail);
    return Deliver<PartialUnequalConeApexChamferSpecification>::Accept(std::move(Specification));
}

Deliver<BrepBody> BlendSolver::ReconstructPartialEqualRadiusBiconeApexChamfer(
    const PartialEqualRadiusBiconeApexChamferSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Apex.X) || !std::isfinite(Specification.Apex.Y) ||
        !std::isfinite(Specification.Apex.Z) || !std::isfinite(Specification.Radius) ||
        !std::isfinite(Specification.LowerHeight) || !std::isfinite(Specification.UpperHeight) ||
        !std::isfinite(Specification.SetBack) || !std::isfinite(Specification.SweepAngle) ||
        Specification.Radius <= Tol || Specification.LowerHeight <= Tol ||
        Specification.UpperHeight <= Tol || Specification.SetBack <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "partial equal bicone dimensions must be finite and positive");
    if (!std::isfinite(Specification.Axis.X) || !std::isfinite(Specification.Axis.Y) ||
        !std::isfinite(Specification.Axis.Z) || Specification.Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial equal bicone axis is degenerate");
    if (Specification.SweepAngle <= ScalarCriteria::AngularTolerance ||
        Specification.SweepAngle >= ScalarCriteria::Pi - ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "partial equal bicone route requires a strict non-reflex sector");
    if (Specification.SetBack >= std::min(Specification.LowerHeight, Specification.UpperHeight) - Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "partial equal bicone set-back consumes a support");
    const Vec3 Apex = Specification.Apex;
    const Vec3 Axis = Specification.Axis.Normalised();
    const Vec3 Radial = Workplane::FromNormal(Apex, Axis).AxisX.Normalised();
    if (Radial.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial equal bicone radial frame is degenerate");
    const Vec3 LowerBase = Apex - Axis * Specification.LowerHeight;
    const Vec3 UpperBase = Apex + Axis * Specification.UpperHeight;
    const Vec3 LowerContact = Apex - Axis * Specification.SetBack;
    const Vec3 UpperContact = Apex + Axis * Specification.SetBack;
    const double LowerContactRadius = Specification.Radius * Specification.SetBack / Specification.LowerHeight;
    const double UpperContactRadius = Specification.Radius * Specification.SetBack / Specification.UpperHeight;
    if (LowerContactRadius <= Tol || UpperContactRadius <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial equal bicone contact ring is degenerate");
    auto RevolveLine = [&](Vec3 Start, Vec3 End) -> Deliver<NurbsSurface>
    {
        Deliver<NurbsCurve> Line = NurbsCurve::Line(Start, End);
        return Line ? NurbsSurface::Revolution(Line.Payload, Apex, Axis, Specification.SweepAngle)
                    : Deliver<NurbsSurface>::Reject(Line.Denial.Reason, Line.Denial.Detail);
    };
    Deliver<NurbsSurface> Lower = RevolveLine(LowerBase + Radial * Specification.Radius,
                                               LowerContact + Radial * LowerContactRadius);
    Deliver<NurbsSurface> Chamfer = RevolveLine(LowerContact + Radial * LowerContactRadius,
                                                UpperContact + Radial * UpperContactRadius);
    Deliver<NurbsSurface> Upper = RevolveLine(UpperContact + Radial * UpperContactRadius,
                                               UpperBase + Radial * Specification.Radius);
    Deliver<NurbsSurface> LowerDisk = RevolveLine(LowerBase, LowerBase + Radial * Specification.Radius);
    Deliver<NurbsSurface> UpperDisk = RevolveLine(UpperBase, UpperBase + Radial * Specification.Radius);
    if (!Lower || !Chamfer || !Upper || !LowerDisk || !UpperDisk)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial equal bicone surfaces are degenerate");
    Lower.Payload.Classification = SurfaceClassification::Cone;
    Lower.Payload.Origin = LowerBase; Lower.Payload.Axis = Axis;
    Lower.Payload.RadiusMajor = Specification.Radius; Lower.Payload.RadiusMinor = LowerContactRadius;
    Chamfer.Payload.Classification = SurfaceClassification::Cone;
    Chamfer.Payload.Origin = LowerContact; Chamfer.Payload.Axis = Axis;
    Chamfer.Payload.RadiusMajor = LowerContactRadius; Chamfer.Payload.RadiusMinor = UpperContactRadius;
    Upper.Payload.Classification = SurfaceClassification::Cone;
    Upper.Payload.Origin = UpperContact; Upper.Payload.Axis = Axis;
    Upper.Payload.RadiusMajor = UpperContactRadius; Upper.Payload.RadiusMinor = Specification.Radius;
    LowerDisk.Payload.Classification = SurfaceClassification::Plane;
    LowerDisk.Payload.Origin = LowerBase; LowerDisk.Payload.Axis = Axis;
    UpperDisk.Payload.Classification = SurfaceClassification::Plane;
    UpperDisk.Payload.Origin = UpperBase; UpperDisk.Payload.Axis = Axis;
    Deliver<BrepBody> Result = BrepBody::Sew({ Lower.Payload, Chamfer.Payload, Upper.Payload,
                                                LowerDisk.Payload, UpperDisk.Payload }, Tol, true);
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, "partial equal bicone surfaces could not be sewn");
    if (!CapRadialSector(Result.Payload, LowerBase, UpperBase, Radial, Specification.SweepAngle,
                         Specification.Radius) || !Result.Payload.Orient())
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "partial equal bicone radial caps could not heal");
    const double LowerRetained = Specification.LowerHeight - Specification.SetBack;
    const double UpperRetained = Specification.UpperHeight - Specification.SetBack;
    const double ExpectedVolume =
        (ScalarCriteria::Pi * LowerRetained *
            (Specification.Radius * Specification.Radius + Specification.Radius * LowerContactRadius +
             LowerContactRadius * LowerContactRadius) / 3.0 +
         ScalarCriteria::Pi * 2.0 * Specification.SetBack *
            (LowerContactRadius * LowerContactRadius + LowerContactRadius * UpperContactRadius +
             UpperContactRadius * UpperContactRadius) / 3.0 +
         ScalarCriteria::Pi * UpperRetained *
            (UpperContactRadius * UpperContactRadius + UpperContactRadius * Specification.Radius +
             Specification.Radius * Specification.Radius) / 3.0) *
        Specification.SweepAngle / ScalarCriteria::TwoPi;
    const BodyReport Report = Result.Payload.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Result.Payload.Vertices.size() != 10 ||
        Result.Payload.Edges.size() != 15 || Result.Payload.Coedges.size() != 30 || Result.Payload.Loops.size() != 7 ||
        Result.Payload.Faces.size() != 7)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                                          "partial equal bicone did not reach V10/E15/C30/L7/F7 topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence, "partial equal bicone volume failed analytic acceptance");
    return Result;
}

Deliver<PartialEqualRadiusBiconeApexChamferSpecification>
BlendSolver::ClassifyPartialEqualRadiusBiconeApexChamferVertex(
    const BrepBody& Body, int Vertex, double SetBack) noexcept
{
    if (!std::isfinite(SetBack) || SetBack <= ScalarCriteria::MergeTolerance)
        return Deliver<PartialEqualRadiusBiconeApexChamferSpecification>::Reject(
            RefusalReason::DegenerateInput, "partial equal bicone set-back must be finite and positive");
    const BodyReport Report = Body.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Body.Vertices.size() != 7 ||
        Body.Edges.size() != 11 || Body.Coedges.size() != 22 || Body.Loops.size() != 6 || Body.Faces.size() != 6)
        return Deliver<PartialEqualRadiusBiconeApexChamferSpecification>::Reject(
            RefusalReason::NonManifold, "source is not the capped partial equal bicone topology");
    if (Vertex < 0 || Vertex >= static_cast<int>(Body.Vertices.size()))
        return Deliver<PartialEqualRadiusBiconeApexChamferSpecification>::Reject(
            RefusalReason::Unsupported, "selected partial equal bicone vertex is out of range");
    int RevolutionFaces = 0, PlaneFaces = 0;
    for (const BrepFace& Face : Body.Faces)
    {
        if (Face.Loops.size() != 1)
            return Deliver<PartialEqualRadiusBiconeApexChamferSpecification>::Reject(
                RefusalReason::Unsupported, "partial equal bicone face is not single-loop");
        if (Face.Surface.Classification == SurfaceClassification::Revolution) ++RevolutionFaces;
        else if (Face.Surface.Classification == SurfaceClassification::Plane) ++PlaneFaces;
        else return Deliver<PartialEqualRadiusBiconeApexChamferSpecification>::Reject(
            RefusalReason::Unsupported, "partial equal bicone has unsupported face geometry");
    }
    if (RevolutionFaces != 4 || PlaneFaces != 2)
        return Deliver<PartialEqualRadiusBiconeApexChamferSpecification>::Reject(
            RefusalReason::Unsupported, "partial equal bicone does not have four revolve faces and two radial caps");
    std::vector<int> RimEdges, LineEdges;
    for (size_t I = 0; I < Body.Edges.size(); ++I)
    {
        const BrepEdge& EdgeData = Body.Edges[I];
        if (EdgeData.Coedges.size() != 2)
            return Deliver<PartialEqualRadiusBiconeApexChamferSpecification>::Reject(
                RefusalReason::NonManifold, "partial equal bicone edge is not manifold");
        if (!EdgeData.Closed() && EdgeData.Curve.Classification == CurveClassification::Arc &&
            EdgeData.Curve.Degree == 2 && EdgeData.Curve.Rational()) RimEdges.push_back(static_cast<int>(I));
        else if (!EdgeData.Closed() && EdgeData.Curve.Classification == CurveClassification::Line &&
                 EdgeData.Curve.Degree == 1) LineEdges.push_back(static_cast<int>(I));
        else return Deliver<PartialEqualRadiusBiconeApexChamferSpecification>::Reject(
            RefusalReason::Unsupported, "partial equal bicone has unsupported edge geometry");
    }
    if (RimEdges.size() != 2 || LineEdges.size() != 9)
        return Deliver<PartialEqualRadiusBiconeApexChamferSpecification>::Reject(
            RefusalReason::Unsupported, "partial equal bicone does not have two arc rims and nine lines");
    int Apex = -1;
    for (int Candidate = 0; Candidate < static_cast<int>(Body.Vertices.size()); ++Candidate)
    {
        int Degree = 0;
        for (int Edge : LineEdges)
            if (Body.Edges[Edge].VertexStart == Candidate || Body.Edges[Edge].VertexEnd == Candidate) ++Degree;
        if (Degree == 4)
        {
            if (Apex >= 0) return Deliver<PartialEqualRadiusBiconeApexChamferSpecification>::Reject(
                RefusalReason::Unsupported, "partial equal bicone has multiple apex candidates");
            Apex = Candidate;
        }
    }
    if (Apex < 0 || Vertex != Apex)
        return Deliver<PartialEqualRadiusBiconeApexChamferSpecification>::Reject(
            RefusalReason::Unsupported, "selected vertex is not the partial equal bicone apex");
    struct Rim { Vec3 Centre, Normal; double Radius = 0.0; int Edge = -1; } G[2];
    for (int K = 0; K < 2; ++K)
    {
        G[K].Edge = RimEdges[K];
        if (!CircularFrame(Body.Edges[G[K].Edge].Curve, G[K].Centre, G[K].Normal, G[K].Radius) || G[K].Radius <= Tol)
            return Deliver<PartialEqualRadiusBiconeApexChamferSpecification>::Reject(
                RefusalReason::Unsupported, "partial equal bicone rim is not circular");
    }
    Vec3 Axis = (G[1].Centre - G[0].Centre).Normalised();
    if (Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<PartialEqualRadiusBiconeApexChamferSpecification>::Reject(
            RefusalReason::DegenerateInput, "partial equal bicone axis is degenerate");
    if ((std::fabs(Axis.X) > ScalarCriteria::GeometricTolerance && Axis.X < 0.0) ||
        (std::fabs(Axis.X) <= ScalarCriteria::GeometricTolerance && std::fabs(Axis.Y) > ScalarCriteria::GeometricTolerance && Axis.Y < 0.0) ||
        (std::fabs(Axis.X) <= ScalarCriteria::GeometricTolerance && std::fabs(Axis.Y) <= ScalarCriteria::GeometricTolerance && Axis.Z < 0.0)) Axis = -Axis;
    const Vec3 ApexPoint = Body.Vertices[Apex].Point;
    for (const Rim& R : G)
        if (std::fabs(std::fabs(R.Normal.Dot(Axis)) - 1.0) > ScalarCriteria::AngularTolerance ||
            (R.Centre - ApexPoint - Axis * (R.Centre - ApexPoint).Dot(Axis)).Length() >
                ScalarCriteria::GeometricTolerance * std::max(1.0, R.Centre.Distance(ApexPoint)))
            return Deliver<PartialEqualRadiusBiconeApexChamferSpecification>::Reject(
                RefusalReason::Unsupported, "partial equal bicone rims are not coaxial");
    if ((G[0].Centre - G[1].Centre).Dot(Axis) > 0.0) std::swap(G[0], G[1]);
    const Vec3 CanonicalRadial = Workplane::FromNormal(ApexPoint, Axis).AxisX.Normalised();
    const Vec3 StartVector = (Body.Vertices[Body.Edges[G[0].Edge].VertexStart].Point - G[0].Centre).Normalised();
    const double Sweep = std::atan2(Axis.Dot(StartVector.Cross(
        (Body.Vertices[Body.Edges[G[0].Edge].VertexEnd].Point - G[0].Centre).Normalised())),
        ScalarCriteria::Clamp(StartVector.Dot(
            (Body.Vertices[Body.Edges[G[0].Edge].VertexEnd].Point - G[0].Centre).Normalised()), -1.0, 1.0));
    if (Sweep <= ScalarCriteria::AngularTolerance || Sweep >= ScalarCriteria::Pi - ScalarCriteria::AngularTolerance ||
        CanonicalRadial.Distance(StartVector) > ScalarCriteria::GeometricTolerance)
        return Deliver<PartialEqualRadiusBiconeApexChamferSpecification>::Reject(
            RefusalReason::Unsupported, "partial equal bicone sweep is not the bounded canonical sector");
    for (size_t I = 0; I < Body.Faces.size(); ++I)
        if (Body.Faces[I].Surface.Classification == SurfaceClassification::Plane)
        {
            Vec3 Normal;
            if (!PlanarNormal(Body, static_cast<int>(I), Normal) || std::fabs(Normal.Dot(Axis)) > ScalarCriteria::AngularTolerance)
                return Deliver<PartialEqualRadiusBiconeApexChamferSpecification>::Reject(
                    RefusalReason::Unsupported, "partial equal bicone radial cap is not planar");
        }
    const double RadiusScale = std::max(1.0, G[0].Radius);
    if (std::fabs(G[0].Radius - G[1].Radius) > ScalarCriteria::GeometricTolerance * RadiusScale)
        return Deliver<PartialEqualRadiusBiconeApexChamferSpecification>::Reject(
            RefusalReason::Unsupported, "partial equal route requires equal rim radii");
    PartialEqualRadiusBiconeApexChamferSpecification Specification;
    Specification.Apex = ApexPoint; Specification.Axis = Axis; Specification.Radius = G[0].Radius;
    Specification.LowerHeight = ApexPoint.Distance(G[0].Centre); Specification.UpperHeight = ApexPoint.Distance(G[1].Centre);
    Specification.SetBack = SetBack; Specification.SweepAngle = Sweep;
    const Deliver<BrepBody> Feasible = ReconstructPartialEqualRadiusBiconeApexChamfer(Specification);
    if (!Feasible) return Deliver<PartialEqualRadiusBiconeApexChamferSpecification>::Reject(
        Feasible.Denial.Reason, Feasible.Denial.Detail);
    return Deliver<PartialEqualRadiusBiconeApexChamferSpecification>::Accept(std::move(Specification));
}

Deliver<BrepBody> BlendSolver::ReconstructPartialUnequalBiconeUnequalSetbackChamfer(
    const PartialUnequalBiconeUnequalSetbackChamferSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Apex.X) || !std::isfinite(Specification.Apex.Y) ||
        !std::isfinite(Specification.Apex.Z) || !std::isfinite(Specification.LowerRadius) ||
        !std::isfinite(Specification.UpperRadius) || !std::isfinite(Specification.LowerHeight) ||
        !std::isfinite(Specification.UpperHeight) || !std::isfinite(Specification.LowerSetBack) ||
        !std::isfinite(Specification.UpperSetBack) || !std::isfinite(Specification.SweepAngle) ||
        Specification.LowerRadius <= Tol || Specification.UpperRadius <= Tol ||
        Specification.LowerHeight <= Tol || Specification.UpperHeight <= Tol ||
        Specification.LowerSetBack <= Tol || Specification.UpperSetBack <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "partial unequal-setback bicone dimensions must be finite and positive");
    if (!std::isfinite(Specification.Axis.X) || !std::isfinite(Specification.Axis.Y) ||
        !std::isfinite(Specification.Axis.Z) || Specification.Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "partial unequal-setback bicone axis is degenerate");
    if (Specification.SweepAngle <= ScalarCriteria::AngularTolerance ||
        Specification.SweepAngle >= ScalarCriteria::Pi - ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "partial unequal-setback route requires a strict non-reflex sector");
    const double RadiusScale = std::max({ 1.0, Specification.LowerRadius, Specification.UpperRadius });
    if (std::fabs(Specification.LowerRadius - Specification.UpperRadius) <=
        ScalarCriteria::GeometricTolerance * RadiusScale)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "partial unequal-setback route requires unequal support radii");
    const double SetbackScale = std::max({ 1.0, Specification.LowerSetBack, Specification.UpperSetBack });
    if (std::fabs(Specification.LowerSetBack - Specification.UpperSetBack) <=
        ScalarCriteria::GeometricTolerance * SetbackScale)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "equal set-backs belong to the bounded partial unequal route");
    if (Specification.LowerSetBack >= Specification.LowerHeight - Tol ||
        Specification.UpperSetBack >= Specification.UpperHeight - Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "partial unequal-setback chamfer consumes a support");
    const Vec3 Apex = Specification.Apex;
    const Vec3 Axis = Specification.Axis.Normalised();
    const Vec3 Radial = Workplane::FromNormal(Apex, Axis).AxisX.Normalised();
    if (Radial.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "partial unequal-setback radial frame is degenerate");
    const Vec3 LowerBase = Apex - Axis * Specification.LowerHeight;
    const Vec3 UpperBase = Apex + Axis * Specification.UpperHeight;
    const Vec3 LowerContact = Apex - Axis * Specification.LowerSetBack;
    const Vec3 UpperContact = Apex + Axis * Specification.UpperSetBack;
    const double LowerContactRadius = Specification.LowerRadius * Specification.LowerSetBack / Specification.LowerHeight;
    const double UpperContactRadius = Specification.UpperRadius * Specification.UpperSetBack / Specification.UpperHeight;
    if (LowerContactRadius <= Tol || UpperContactRadius <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "partial unequal-setback contact ring is degenerate");
    auto RevolveLine = [&](Vec3 Start, Vec3 End) -> Deliver<NurbsSurface>
    {
        Deliver<NurbsCurve> Line = NurbsCurve::Line(Start, End);
        return Line ? NurbsSurface::Revolution(Line.Payload, Apex, Axis, Specification.SweepAngle)
                    : Deliver<NurbsSurface>::Reject(Line.Denial.Reason, Line.Denial.Detail);
    };
    Deliver<NurbsSurface> Lower = RevolveLine(LowerBase + Radial * Specification.LowerRadius,
                                               LowerContact + Radial * LowerContactRadius);
    Deliver<NurbsSurface> Chamfer = RevolveLine(LowerContact + Radial * LowerContactRadius,
                                                UpperContact + Radial * UpperContactRadius);
    Deliver<NurbsSurface> Upper = RevolveLine(UpperContact + Radial * UpperContactRadius,
                                               UpperBase + Radial * Specification.UpperRadius);
    Deliver<NurbsSurface> LowerDisk = RevolveLine(LowerBase, LowerBase + Radial * Specification.LowerRadius);
    Deliver<NurbsSurface> UpperDisk = RevolveLine(UpperBase, UpperBase + Radial * Specification.UpperRadius);
    if (!Lower || !Chamfer || !Upper || !LowerDisk || !UpperDisk)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "partial unequal-setback bicone surfaces are degenerate");
    Lower.Payload.Classification = SurfaceClassification::Cone;
    Lower.Payload.Origin = LowerBase; Lower.Payload.Axis = Axis;
    Lower.Payload.RadiusMajor = Specification.LowerRadius; Lower.Payload.RadiusMinor = LowerContactRadius;
    Chamfer.Payload.Classification = SurfaceClassification::Cone;
    Chamfer.Payload.Origin = LowerContact; Chamfer.Payload.Axis = Axis;
    Chamfer.Payload.RadiusMajor = LowerContactRadius; Chamfer.Payload.RadiusMinor = UpperContactRadius;
    Upper.Payload.Classification = SurfaceClassification::Cone;
    Upper.Payload.Origin = UpperContact; Upper.Payload.Axis = Axis;
    Upper.Payload.RadiusMajor = UpperContactRadius; Upper.Payload.RadiusMinor = Specification.UpperRadius;
    LowerDisk.Payload.Classification = SurfaceClassification::Plane;
    LowerDisk.Payload.Origin = LowerBase; LowerDisk.Payload.Axis = Axis;
    UpperDisk.Payload.Classification = SurfaceClassification::Plane;
    UpperDisk.Payload.Origin = UpperBase; UpperDisk.Payload.Axis = Axis;
    Deliver<BrepBody> Result = BrepBody::Sew({ Lower.Payload, Chamfer.Payload, Upper.Payload,
                                                LowerDisk.Payload, UpperDisk.Payload }, Tol, true);
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason,
                                                   "partial unequal-setback bicone surfaces could not be sewn");
    if (!CapRadialSector(Result.Payload, LowerBase, UpperBase, Radial, Specification.SweepAngle,
                         std::max(Specification.LowerRadius, Specification.UpperRadius)) || !Result.Payload.Orient())
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                                          "partial unequal-setback bicone radial caps could not heal");
    const double BridgeLength = Specification.LowerSetBack + Specification.UpperSetBack;
    const double ExpectedVolume =
        (ScalarCriteria::Pi * (Specification.LowerHeight - Specification.LowerSetBack) *
            (Specification.LowerRadius * Specification.LowerRadius + Specification.LowerRadius * LowerContactRadius +
             LowerContactRadius * LowerContactRadius) / 3.0 +
         ScalarCriteria::Pi * BridgeLength *
            (LowerContactRadius * LowerContactRadius + LowerContactRadius * UpperContactRadius +
             UpperContactRadius * UpperContactRadius) / 3.0 +
         ScalarCriteria::Pi * (Specification.UpperHeight - Specification.UpperSetBack) *
            (UpperContactRadius * UpperContactRadius + UpperContactRadius * Specification.UpperRadius +
             Specification.UpperRadius * Specification.UpperRadius) / 3.0) *
        Specification.SweepAngle / ScalarCriteria::TwoPi;
    const BodyReport Report = Result.Payload.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Result.Payload.Vertices.size() != 10 ||
        Result.Payload.Edges.size() != 15 || Result.Payload.Coedges.size() != 30 || Result.Payload.Loops.size() != 7 ||
        Result.Payload.Faces.size() != 7)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                                          "partial unequal-setback bicone did not reach V10/E15/C30/L7/F7 topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence,
                                          "partial unequal-setback bicone volume failed analytic acceptance");
    return Result;
}

Deliver<PartialUnequalBiconeUnequalSetbackChamferSpecification>
BlendSolver::ClassifyPartialUnequalBiconeUnequalSetbackChamferVertex(
    const BrepBody& Body, int Vertex, double LowerSetBack, double UpperSetBack) noexcept
{
    if (!std::isfinite(LowerSetBack) || !std::isfinite(UpperSetBack) ||
        LowerSetBack <= ScalarCriteria::MergeTolerance || UpperSetBack <= ScalarCriteria::MergeTolerance)
        return Deliver<PartialUnequalBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::DegenerateInput, "partial unequal-setback values must be finite and positive");
    const BodyReport Report = Body.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Body.Vertices.size() != 7 ||
        Body.Edges.size() != 11 || Body.Coedges.size() != 22 || Body.Loops.size() != 6 || Body.Faces.size() != 6)
        return Deliver<PartialUnequalBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::NonManifold, "source is not the capped partial unequal bicone topology");
    if (Vertex < 0 || Vertex >= static_cast<int>(Body.Vertices.size()))
        return Deliver<PartialUnequalBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::Unsupported, "selected partial unequal-setback vertex is out of range");
    int RevolutionFaces = 0, PlaneFaces = 0;
    for (const BrepFace& Face : Body.Faces)
    {
        if (Face.Loops.size() != 1)
            return Deliver<PartialUnequalBiconeUnequalSetbackChamferSpecification>::Reject(
                RefusalReason::Unsupported, "partial unequal-setback face is not single-loop");
        if (Face.Surface.Classification == SurfaceClassification::Revolution) ++RevolutionFaces;
        else if (Face.Surface.Classification == SurfaceClassification::Plane) ++PlaneFaces;
        else return Deliver<PartialUnequalBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::Unsupported, "partial unequal-setback has unsupported face geometry");
    }
    if (RevolutionFaces != 4 || PlaneFaces != 2)
        return Deliver<PartialUnequalBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::Unsupported, "partial unequal-setback does not have four revolve faces and two radial caps");
    std::vector<int> RimEdges, LineEdges;
    for (size_t I = 0; I < Body.Edges.size(); ++I)
    {
        const BrepEdge& EdgeData = Body.Edges[I];
        if (EdgeData.Coedges.size() != 2)
            return Deliver<PartialUnequalBiconeUnequalSetbackChamferSpecification>::Reject(
                RefusalReason::NonManifold, "partial unequal-setback edge is not manifold");
        if (!EdgeData.Closed() && EdgeData.Curve.Classification == CurveClassification::Arc &&
            EdgeData.Curve.Degree == 2 && EdgeData.Curve.Rational()) RimEdges.push_back(static_cast<int>(I));
        else if (!EdgeData.Closed() && EdgeData.Curve.Classification == CurveClassification::Line &&
                 EdgeData.Curve.Degree == 1) LineEdges.push_back(static_cast<int>(I));
        else return Deliver<PartialUnequalBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::Unsupported, "partial unequal-setback has unsupported edge geometry");
    }
    if (RimEdges.size() != 2 || LineEdges.size() != 9)
        return Deliver<PartialUnequalBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::Unsupported, "partial unequal-setback does not have two arc rims and nine lines");
    int Apex = -1;
    for (int Candidate = 0; Candidate < static_cast<int>(Body.Vertices.size()); ++Candidate)
    {
        int Degree = 0;
        for (int Edge : LineEdges)
            if (Body.Edges[Edge].VertexStart == Candidate || Body.Edges[Edge].VertexEnd == Candidate) ++Degree;
        if (Degree == 4)
        {
            if (Apex >= 0) return Deliver<PartialUnequalBiconeUnequalSetbackChamferSpecification>::Reject(
                RefusalReason::Unsupported, "partial unequal-setback has multiple apex candidates");
            Apex = Candidate;
        }
    }
    if (Apex < 0 || Vertex != Apex)
        return Deliver<PartialUnequalBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::Unsupported, "selected vertex is not the partial unequal-setback apex");
    struct Rim { Vec3 Centre, Normal; double Radius = 0.0; int Edge = -1; } G[2];
    for (int K = 0; K < 2; ++K)
    {
        G[K].Edge = RimEdges[K];
        if (!CircularFrame(Body.Edges[G[K].Edge].Curve, G[K].Centre, G[K].Normal, G[K].Radius) || G[K].Radius <= Tol)
            return Deliver<PartialUnequalBiconeUnequalSetbackChamferSpecification>::Reject(
                RefusalReason::Unsupported, "partial unequal-setback rim is not circular");
    }
    Vec3 Axis = (G[1].Centre - G[0].Centre).Normalised();
    if (Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<PartialUnequalBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::DegenerateInput, "partial unequal-setback axis is degenerate");
    if ((std::fabs(Axis.X) > ScalarCriteria::GeometricTolerance && Axis.X < 0.0) ||
        (std::fabs(Axis.X) <= ScalarCriteria::GeometricTolerance && std::fabs(Axis.Y) > ScalarCriteria::GeometricTolerance && Axis.Y < 0.0) ||
        (std::fabs(Axis.X) <= ScalarCriteria::GeometricTolerance && std::fabs(Axis.Y) <= ScalarCriteria::GeometricTolerance && Axis.Z < 0.0)) Axis = -Axis;
    const Vec3 ApexPoint = Body.Vertices[Apex].Point;
    for (const Rim& R : G)
        if (std::fabs(std::fabs(R.Normal.Dot(Axis)) - 1.0) > ScalarCriteria::AngularTolerance ||
            (R.Centre - ApexPoint - Axis * (R.Centre - ApexPoint).Dot(Axis)).Length() >
                ScalarCriteria::GeometricTolerance * std::max(1.0, R.Centre.Distance(ApexPoint)))
            return Deliver<PartialUnequalBiconeUnequalSetbackChamferSpecification>::Reject(
                RefusalReason::Unsupported, "partial unequal-setback rims are not coaxial");
    if ((G[0].Centre - G[1].Centre).Dot(Axis) > 0.0) std::swap(G[0], G[1]);
    const Vec3 CanonicalRadial = Workplane::FromNormal(ApexPoint, Axis).AxisX.Normalised();
    const Vec3 StartVector = (Body.Vertices[Body.Edges[G[0].Edge].VertexStart].Point - G[0].Centre).Normalised();
    const Vec3 EndVector = (Body.Vertices[Body.Edges[G[0].Edge].VertexEnd].Point - G[0].Centre).Normalised();
    const double Sweep = std::atan2(Axis.Dot(StartVector.Cross(EndVector)),
                                    ScalarCriteria::Clamp(StartVector.Dot(EndVector), -1.0, 1.0));
    if (Sweep <= ScalarCriteria::AngularTolerance || Sweep >= ScalarCriteria::Pi - ScalarCriteria::AngularTolerance ||
        CanonicalRadial.Distance(StartVector) > ScalarCriteria::GeometricTolerance)
        return Deliver<PartialUnequalBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::Unsupported, "partial unequal-setback sweep is not the bounded canonical sector");
    for (size_t I = 0; I < Body.Faces.size(); ++I)
        if (Body.Faces[I].Surface.Classification == SurfaceClassification::Plane)
        {
            Vec3 Normal;
            if (!PlanarNormal(Body, static_cast<int>(I), Normal) || std::fabs(Normal.Dot(Axis)) > ScalarCriteria::AngularTolerance)
                return Deliver<PartialUnequalBiconeUnequalSetbackChamferSpecification>::Reject(
                    RefusalReason::Unsupported, "partial unequal-setback radial cap is not planar");
        }
    const double RadiusScale = std::max({ 1.0, G[0].Radius, G[1].Radius });
    if (std::fabs(G[0].Radius - G[1].Radius) <= ScalarCriteria::GeometricTolerance * RadiusScale)
        return Deliver<PartialUnequalBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::Unsupported, "partial unequal-setback route requires unequal radii");
    const double SetbackScale = std::max({ 1.0, LowerSetBack, UpperSetBack });
    if (std::fabs(LowerSetBack - UpperSetBack) <= ScalarCriteria::GeometricTolerance * SetbackScale)
        return Deliver<PartialUnequalBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::Unsupported, "partial route requires distinct lower and upper set-backs");
    const double LowerHeight = ApexPoint.Distance(G[0].Centre);
    const double UpperHeight = ApexPoint.Distance(G[1].Centre);
    if (LowerSetBack >= LowerHeight - Tol || UpperSetBack >= UpperHeight - Tol)
        return Deliver<PartialUnequalBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::Unsupported, "partial unequal-setback consumes a support");
    PartialUnequalBiconeUnequalSetbackChamferSpecification Specification;
    Specification.Apex = ApexPoint; Specification.Axis = Axis;
    Specification.LowerRadius = G[0].Radius; Specification.UpperRadius = G[1].Radius;
    Specification.LowerHeight = LowerHeight; Specification.UpperHeight = UpperHeight;
    Specification.LowerSetBack = LowerSetBack; Specification.UpperSetBack = UpperSetBack;
    Specification.SweepAngle = Sweep;
    const Deliver<BrepBody> Feasible = ReconstructPartialUnequalBiconeUnequalSetbackChamfer(Specification);
    if (!Feasible) return Deliver<PartialUnequalBiconeUnequalSetbackChamferSpecification>::Reject(
        Feasible.Denial.Reason, Feasible.Denial.Detail);
    return Deliver<PartialUnequalBiconeUnequalSetbackChamferSpecification>::Accept(std::move(Specification));
}

Deliver<BrepBody> BlendSolver::ReconstructHalfTurnEqualRadiusBiconeUnequalSetbackChamfer(
    const HalfTurnEqualRadiusBiconeUnequalSetbackChamferSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Apex.X) || !std::isfinite(Specification.Apex.Y) ||
        !std::isfinite(Specification.Apex.Z) || !std::isfinite(Specification.Radius) ||
        !std::isfinite(Specification.LowerHeight) || !std::isfinite(Specification.UpperHeight) ||
        !std::isfinite(Specification.LowerSetBack) || !std::isfinite(Specification.UpperSetBack) ||
        !std::isfinite(Specification.SweepAngle) || Specification.Radius <= Tol ||
        Specification.LowerHeight <= Tol || Specification.UpperHeight <= Tol ||
        Specification.LowerSetBack <= Tol || Specification.UpperSetBack <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "half-turn equal-radius independent-setback bicone dimensions must be finite and positive");
    if (!std::isfinite(Specification.Axis.X) || !std::isfinite(Specification.Axis.Y) ||
        !std::isfinite(Specification.Axis.Z) || Specification.Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "half-turn equal-radius independent-setback bicone axis is degenerate");
    if (std::fabs(Specification.SweepAngle - ScalarCriteria::Pi) > ScalarCriteria::SweepTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "half-turn equal-radius independent-setback route requires exactly a half turn");
    const double SetbackScale = std::max({ 1.0, Specification.LowerSetBack, Specification.UpperSetBack });
    if (std::fabs(Specification.LowerSetBack - Specification.UpperSetBack) <=
        ScalarCriteria::GeometricTolerance * SetbackScale)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "equal set-backs belong to the bounded partial equal route");
    if (Specification.LowerSetBack >= Specification.LowerHeight - Tol ||
        Specification.UpperSetBack >= Specification.UpperHeight - Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "half-turn equal-radius independent-setback chamfer consumes a support");
    const Vec3 Apex = Specification.Apex;
    const Vec3 Axis = Specification.Axis.Normalised();
    const Vec3 Radial = Workplane::FromNormal(Apex, Axis).AxisX.Normalised();
    if (Radial.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "half-turn equal-radius independent-setback radial frame is degenerate");
    const Vec3 LowerBase = Apex - Axis * Specification.LowerHeight;
    const Vec3 UpperBase = Apex + Axis * Specification.UpperHeight;
    const Vec3 LowerContact = Apex - Axis * Specification.LowerSetBack;
    const Vec3 UpperContact = Apex + Axis * Specification.UpperSetBack;
    const double LowerContactRadius = Specification.Radius * Specification.LowerSetBack / Specification.LowerHeight;
    const double UpperContactRadius = Specification.Radius * Specification.UpperSetBack / Specification.UpperHeight;
    if (LowerContactRadius <= Tol || UpperContactRadius <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "half-turn equal-radius independent-setback contact ring is degenerate");
    auto RevolveLine = [&](Vec3 Start, Vec3 End) -> Deliver<NurbsSurface>
    {
        Deliver<NurbsCurve> Line = NurbsCurve::Line(Start, End);
        return Line ? NurbsSurface::Revolution(Line.Payload, Apex, Axis, Specification.SweepAngle)
                    : Deliver<NurbsSurface>::Reject(Line.Denial.Reason, Line.Denial.Detail);
    };
    Deliver<NurbsSurface> Lower = RevolveLine(LowerBase + Radial * Specification.Radius,
                                               LowerContact + Radial * LowerContactRadius);
    Deliver<NurbsSurface> Chamfer = RevolveLine(LowerContact + Radial * LowerContactRadius,
                                                UpperContact + Radial * UpperContactRadius);
    Deliver<NurbsSurface> Upper = RevolveLine(UpperContact + Radial * UpperContactRadius,
                                               UpperBase + Radial * Specification.Radius);
    Deliver<NurbsSurface> LowerDisk = RevolveLine(LowerBase, LowerBase + Radial * Specification.Radius);
    Deliver<NurbsSurface> UpperDisk = RevolveLine(UpperBase, UpperBase + Radial * Specification.Radius);
    if (!Lower || !Chamfer || !Upper || !LowerDisk || !UpperDisk)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "half-turn equal-radius independent-setback bicone surfaces are degenerate");
    Lower.Payload.Classification = SurfaceClassification::Cone;
    Lower.Payload.Origin = LowerBase; Lower.Payload.Axis = Axis;
    Lower.Payload.RadiusMajor = Specification.Radius; Lower.Payload.RadiusMinor = LowerContactRadius;
    Chamfer.Payload.Classification = SurfaceClassification::Cone;
    Chamfer.Payload.Origin = LowerContact; Chamfer.Payload.Axis = Axis;
    Chamfer.Payload.RadiusMajor = LowerContactRadius; Chamfer.Payload.RadiusMinor = UpperContactRadius;
    Upper.Payload.Classification = SurfaceClassification::Cone;
    Upper.Payload.Origin = UpperContact; Upper.Payload.Axis = Axis;
    Upper.Payload.RadiusMajor = UpperContactRadius; Upper.Payload.RadiusMinor = Specification.Radius;
    LowerDisk.Payload.Classification = SurfaceClassification::Plane;
    LowerDisk.Payload.Origin = LowerBase; LowerDisk.Payload.Axis = Axis;
    UpperDisk.Payload.Classification = SurfaceClassification::Plane;
    UpperDisk.Payload.Origin = UpperBase; UpperDisk.Payload.Axis = Axis;
    Deliver<BrepBody> Result = BrepBody::Sew({ Lower.Payload, Chamfer.Payload, Upper.Payload,
                                                LowerDisk.Payload, UpperDisk.Payload }, Tol, false);
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason,
                                                   "half-turn equal-radius independent-setback bicone surfaces could not be sewn");
    if (!CapRadialSector(Result.Payload, LowerBase, UpperBase, Radial, Specification.SweepAngle,
                         Specification.Radius) || !Result.Payload.Orient())
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                                          "half-turn equal-radius independent-setback bicone radial caps could not heal");
    const double BridgeLength = Specification.LowerSetBack + Specification.UpperSetBack;
    const double ExpectedVolume =
        (ScalarCriteria::Pi * (Specification.LowerHeight - Specification.LowerSetBack) *
            (Specification.Radius * Specification.Radius + Specification.Radius * LowerContactRadius +
             LowerContactRadius * LowerContactRadius) / 3.0 +
         ScalarCriteria::Pi * BridgeLength *
            (LowerContactRadius * LowerContactRadius + LowerContactRadius * UpperContactRadius +
             UpperContactRadius * UpperContactRadius) / 3.0 +
         ScalarCriteria::Pi * (Specification.UpperHeight - Specification.UpperSetBack) *
            (UpperContactRadius * UpperContactRadius + UpperContactRadius * Specification.Radius +
             Specification.Radius * Specification.Radius) / 3.0) *
        Specification.SweepAngle / ScalarCriteria::TwoPi;
    const BodyReport Report = Result.Payload.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Result.Payload.Vertices.size() != 10 ||
        Result.Payload.Edges.size() != 15 || Result.Payload.Coedges.size() != 30 || Result.Payload.Loops.size() != 7 ||
        Result.Payload.Faces.size() != 7)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                                          "half-turn equal-radius independent-setback bicone did not reach V10/E15/C30/L7/F7 topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence,
                                          "half-turn equal-radius independent-setback bicone volume failed analytic acceptance");
    return Result;
}

Deliver<HalfTurnEqualRadiusBiconeUnequalSetbackChamferSpecification>
BlendSolver::ClassifyHalfTurnEqualRadiusBiconeUnequalSetbackChamferVertex(
    const BrepBody& Body, int Vertex, double LowerSetBack, double UpperSetBack) noexcept
{
    if (!std::isfinite(LowerSetBack) || !std::isfinite(UpperSetBack) ||
        LowerSetBack <= ScalarCriteria::MergeTolerance || UpperSetBack <= ScalarCriteria::MergeTolerance)
        return Deliver<HalfTurnEqualRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::DegenerateInput, "half-turn equal bicone set-backs must be finite and positive");
    const double SetbackScale = std::max({ 1.0, LowerSetBack, UpperSetBack });
    if (std::fabs(LowerSetBack - UpperSetBack) <= ScalarCriteria::GeometricTolerance * SetbackScale)
        return Deliver<HalfTurnEqualRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::Unsupported, "equal set-backs belong to the full-turn equal-radius route");
    const BodyReport Report = Body.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Body.Vertices.size() != 7 ||
        Body.Edges.size() != 11 || Body.Coedges.size() != 22 || Body.Loops.size() != 6 || Body.Faces.size() != 6)
        return Deliver<HalfTurnEqualRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::NonManifold, "source is not the capped half-turn equal bicone topology");
    if (Vertex < 0 || Vertex >= static_cast<int>(Body.Vertices.size()))
        return Deliver<HalfTurnEqualRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::Unsupported, "selected half-turn equal bicone vertex is out of range");
    int RevolutionFaces = 0, PlaneFaces = 0;
    for (const BrepFace& Face : Body.Faces)
    {
        if (Face.Loops.size() != 1)
            return Deliver<HalfTurnEqualRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
                RefusalReason::Unsupported, "half-turn equal bicone face is not single-loop");
        if (Face.Surface.Classification == SurfaceClassification::Revolution) ++RevolutionFaces;
        else if (Face.Surface.Classification == SurfaceClassification::Plane) ++PlaneFaces;
        else return Deliver<HalfTurnEqualRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::Unsupported, "half-turn equal bicone has unsupported face geometry");
    }
    if (RevolutionFaces != 4 || PlaneFaces != 2)
        return Deliver<HalfTurnEqualRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::Unsupported, "half-turn equal bicone does not have four revolve faces and two radial caps");
    std::vector<int> RimEdges, LineEdges;
    for (size_t I = 0; I < Body.Edges.size(); ++I)
    {
        const BrepEdge& EdgeData = Body.Edges[I];
        if (EdgeData.Coedges.size() != 2)
            return Deliver<HalfTurnEqualRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
                RefusalReason::NonManifold, "half-turn equal bicone edge is not manifold");
        if (!EdgeData.Closed() && EdgeData.Curve.Classification == CurveClassification::Arc &&
            EdgeData.Curve.Degree == 2 && EdgeData.Curve.Rational()) RimEdges.push_back(static_cast<int>(I));
        else if (!EdgeData.Closed() && EdgeData.Curve.Classification == CurveClassification::Line &&
                 EdgeData.Curve.Degree == 1) LineEdges.push_back(static_cast<int>(I));
        else return Deliver<HalfTurnEqualRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::Unsupported, "half-turn equal bicone has unsupported edge geometry");
    }
    if (RimEdges.size() != 2 || LineEdges.size() != 9)
        return Deliver<HalfTurnEqualRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::Unsupported, "half-turn equal bicone does not have two arc rims and nine lines");
    int Apex = -1;
    for (int Candidate = 0; Candidate < static_cast<int>(Body.Vertices.size()); ++Candidate)
    {
        int Degree = 0;
        for (int Edge : LineEdges)
            if (Body.Edges[Edge].VertexStart == Candidate || Body.Edges[Edge].VertexEnd == Candidate) ++Degree;
        if (Degree == 4)
        {
            if (Apex >= 0) return Deliver<HalfTurnEqualRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
                RefusalReason::Unsupported, "half-turn equal bicone has multiple apex candidates");
            Apex = Candidate;
        }
    }
    if (Apex < 0 || Vertex != Apex)
        return Deliver<HalfTurnEqualRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::Unsupported, "selected vertex is not the half-turn equal bicone apex");
    struct Rim { Vec3 Centre, Normal; double Radius = 0.0; int Edge = -1; } G[2];
    for (int K = 0; K < 2; ++K)
    {
        G[K].Edge = RimEdges[K];
        if (!CircularFrame(Body.Edges[G[K].Edge].Curve, G[K].Centre, G[K].Normal, G[K].Radius) || G[K].Radius <= Tol)
            return Deliver<HalfTurnEqualRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
                RefusalReason::Unsupported, "half-turn equal bicone rim is not circular");
    }
    Vec3 Axis = (G[1].Centre - G[0].Centre).Normalised();
    if (Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<HalfTurnEqualRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::DegenerateInput, "half-turn equal bicone axis is degenerate");
    if ((std::fabs(Axis.X) > ScalarCriteria::GeometricTolerance && Axis.X < 0.0) ||
        (std::fabs(Axis.X) <= ScalarCriteria::GeometricTolerance && std::fabs(Axis.Y) > ScalarCriteria::GeometricTolerance && Axis.Y < 0.0) ||
        (std::fabs(Axis.X) <= ScalarCriteria::GeometricTolerance && std::fabs(Axis.Y) <= ScalarCriteria::GeometricTolerance && Axis.Z < 0.0)) Axis = -Axis;
    const Vec3 ApexPoint = Body.Vertices[Apex].Point;
    for (const Rim& R : G)
        if (std::fabs(std::fabs(R.Normal.Dot(Axis)) - 1.0) > ScalarCriteria::AngularTolerance ||
            (R.Centre - ApexPoint - Axis * (R.Centre - ApexPoint).Dot(Axis)).Length() >
                ScalarCriteria::GeometricTolerance * std::max(1.0, R.Centre.Distance(ApexPoint)))
            return Deliver<HalfTurnEqualRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
                RefusalReason::Unsupported, "half-turn equal bicone rims are not coaxial");
    if ((G[0].Centre - G[1].Centre).Dot(Axis) > 0.0) std::swap(G[0], G[1]);
    const Vec3 CanonicalRadial = Workplane::FromNormal(ApexPoint, Axis).AxisX.Normalised();
    const Vec3 StartVector = (Body.Vertices[Body.Edges[G[0].Edge].VertexStart].Point - G[0].Centre).Normalised();
    const double Sweep = std::atan2(Axis.Dot(StartVector.Cross(
        (Body.Vertices[Body.Edges[G[0].Edge].VertexEnd].Point - G[0].Centre).Normalised())),
        ScalarCriteria::Clamp(StartVector.Dot(
            (Body.Vertices[Body.Edges[G[0].Edge].VertexEnd].Point - G[0].Centre).Normalised()), -1.0, 1.0));
    if (std::fabs(Sweep - ScalarCriteria::Pi) > ScalarCriteria::SweepTolerance ||
        CanonicalRadial.Distance(StartVector) > ScalarCriteria::GeometricTolerance)
        return Deliver<HalfTurnEqualRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::Unsupported, "half-turn equal bicone sweep is not exactly a canonical half turn");
    for (size_t I = 0; I < Body.Faces.size(); ++I)
        if (Body.Faces[I].Surface.Classification == SurfaceClassification::Plane)
        {
            Vec3 Normal;
            if (!PlanarNormal(Body, static_cast<int>(I), Normal) || std::fabs(Normal.Dot(Axis)) > ScalarCriteria::AngularTolerance)
                return Deliver<HalfTurnEqualRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
                    RefusalReason::Unsupported, "half-turn equal bicone radial cap is not planar");
        }
    const double RadiusScale = std::max(1.0, G[0].Radius);
    if (std::fabs(G[0].Radius - G[1].Radius) > ScalarCriteria::GeometricTolerance * RadiusScale)
        return Deliver<HalfTurnEqualRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::Unsupported, "partial equal route requires equal rim radii");
    HalfTurnEqualRadiusBiconeUnequalSetbackChamferSpecification Specification;
    Specification.Apex = ApexPoint; Specification.Axis = Axis; Specification.Radius = G[0].Radius;
    Specification.LowerHeight = ApexPoint.Distance(G[0].Centre); Specification.UpperHeight = ApexPoint.Distance(G[1].Centre);
    Specification.LowerSetBack = LowerSetBack; Specification.UpperSetBack = UpperSetBack;
    Specification.SweepAngle = ScalarCriteria::Pi;
    const Deliver<BrepBody> Feasible = ReconstructHalfTurnEqualRadiusBiconeUnequalSetbackChamfer(Specification);
    if (!Feasible) return Deliver<HalfTurnEqualRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
        Feasible.Denial.Reason, Feasible.Denial.Detail);
    return Deliver<HalfTurnEqualRadiusBiconeUnequalSetbackChamferSpecification>::Accept(std::move(Specification));
}


Deliver<BrepBody> BlendSolver::ReconstructHalfTurnUnequalRadiusBiconeUnequalSetbackChamfer(
    const HalfTurnUnequalRadiusBiconeUnequalSetbackChamferSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Apex.X) || !std::isfinite(Specification.Apex.Y) ||
        !std::isfinite(Specification.Apex.Z) || !std::isfinite(Specification.LowerRadius) ||
        !std::isfinite(Specification.UpperRadius) || !std::isfinite(Specification.LowerHeight) ||
        !std::isfinite(Specification.UpperHeight) || !std::isfinite(Specification.LowerSetBack) ||
        !std::isfinite(Specification.UpperSetBack) || !std::isfinite(Specification.SweepAngle) ||
        Specification.LowerRadius <= Tol || Specification.UpperRadius <= Tol ||
        Specification.LowerHeight <= Tol || Specification.UpperHeight <= Tol ||
        Specification.LowerSetBack <= Tol || Specification.UpperSetBack <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "half-turn unequal-setback bicone dimensions must be finite and positive");
    if (!std::isfinite(Specification.Axis.X) || !std::isfinite(Specification.Axis.Y) ||
        !std::isfinite(Specification.Axis.Z) || Specification.Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "half-turn unequal-setback bicone axis is degenerate");
    if (std::fabs(Specification.SweepAngle - ScalarCriteria::Pi) > ScalarCriteria::SweepTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "half-turn unequal-setback route requires exactly a half turn");
    const double RadiusScale = std::max({ 1.0, Specification.LowerRadius, Specification.UpperRadius });
    if (std::fabs(Specification.LowerRadius - Specification.UpperRadius) <=
        ScalarCriteria::GeometricTolerance * RadiusScale)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "half-turn unequal-setback route requires unequal support radii");
    const double SetbackScale = std::max({ 1.0, Specification.LowerSetBack, Specification.UpperSetBack });
    if (std::fabs(Specification.LowerSetBack - Specification.UpperSetBack) <=
        ScalarCriteria::GeometricTolerance * SetbackScale)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "equal set-backs belong to the bounded half-turn unequal route");
    if (Specification.LowerSetBack >= Specification.LowerHeight - Tol ||
        Specification.UpperSetBack >= Specification.UpperHeight - Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "half-turn unequal-setback chamfer consumes a support");
    const Vec3 Apex = Specification.Apex;
    const Vec3 Axis = Specification.Axis.Normalised();
    const Vec3 Radial = Workplane::FromNormal(Apex, Axis).AxisX.Normalised();
    if (Radial.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "half-turn unequal-setback radial frame is degenerate");
    const Vec3 LowerBase = Apex - Axis * Specification.LowerHeight;
    const Vec3 UpperBase = Apex + Axis * Specification.UpperHeight;
    const Vec3 LowerContact = Apex - Axis * Specification.LowerSetBack;
    const Vec3 UpperContact = Apex + Axis * Specification.UpperSetBack;
    const double LowerContactRadius = Specification.LowerRadius * Specification.LowerSetBack / Specification.LowerHeight;
    const double UpperContactRadius = Specification.UpperRadius * Specification.UpperSetBack / Specification.UpperHeight;
    if (LowerContactRadius <= Tol || UpperContactRadius <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "half-turn unequal-setback contact ring is degenerate");
    auto RevolveLine = [&](Vec3 Start, Vec3 End) -> Deliver<NurbsSurface>
    {
        Deliver<NurbsCurve> Line = NurbsCurve::Line(Start, End);
        return Line ? NurbsSurface::Revolution(Line.Payload, Apex, Axis, Specification.SweepAngle)
                    : Deliver<NurbsSurface>::Reject(Line.Denial.Reason, Line.Denial.Detail);
    };
    Deliver<NurbsSurface> Lower = RevolveLine(LowerBase + Radial * Specification.LowerRadius,
                                               LowerContact + Radial * LowerContactRadius);
    Deliver<NurbsSurface> Chamfer = RevolveLine(LowerContact + Radial * LowerContactRadius,
                                                UpperContact + Radial * UpperContactRadius);
    Deliver<NurbsSurface> Upper = RevolveLine(UpperContact + Radial * UpperContactRadius,
                                               UpperBase + Radial * Specification.UpperRadius);
    Deliver<NurbsSurface> LowerDisk = RevolveLine(LowerBase, LowerBase + Radial * Specification.LowerRadius);
    Deliver<NurbsSurface> UpperDisk = RevolveLine(UpperBase, UpperBase + Radial * Specification.UpperRadius);
    if (!Lower || !Chamfer || !Upper || !LowerDisk || !UpperDisk)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "half-turn unequal-setback bicone surfaces are degenerate");
    Lower.Payload.Classification = SurfaceClassification::Cone;
    Lower.Payload.Origin = LowerBase; Lower.Payload.Axis = Axis;
    Lower.Payload.RadiusMajor = Specification.LowerRadius; Lower.Payload.RadiusMinor = LowerContactRadius;
    Chamfer.Payload.Classification = SurfaceClassification::Cone;
    Chamfer.Payload.Origin = LowerContact; Chamfer.Payload.Axis = Axis;
    Chamfer.Payload.RadiusMajor = LowerContactRadius; Chamfer.Payload.RadiusMinor = UpperContactRadius;
    Upper.Payload.Classification = SurfaceClassification::Cone;
    Upper.Payload.Origin = UpperContact; Upper.Payload.Axis = Axis;
    Upper.Payload.RadiusMajor = UpperContactRadius; Upper.Payload.RadiusMinor = Specification.UpperRadius;
    LowerDisk.Payload.Classification = SurfaceClassification::Plane;
    LowerDisk.Payload.Origin = LowerBase; LowerDisk.Payload.Axis = Axis;
    UpperDisk.Payload.Classification = SurfaceClassification::Plane;
    UpperDisk.Payload.Origin = UpperBase; UpperDisk.Payload.Axis = Axis;
    Deliver<BrepBody> Result = BrepBody::Sew({ Lower.Payload, Chamfer.Payload, Upper.Payload,
                                                LowerDisk.Payload, UpperDisk.Payload }, Tol, false);
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason,
                                                   "half-turn unequal-setback bicone surfaces could not be sewn");
    if (!CapRadialSector(Result.Payload, LowerBase, UpperBase, Radial, Specification.SweepAngle,
                         std::max(Specification.LowerRadius, Specification.UpperRadius)) || !Result.Payload.Orient())
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                                          "half-turn unequal-setback bicone radial caps could not heal");
    const double BridgeLength = Specification.LowerSetBack + Specification.UpperSetBack;
    const double ExpectedVolume =
        (ScalarCriteria::Pi * (Specification.LowerHeight - Specification.LowerSetBack) *
            (Specification.LowerRadius * Specification.LowerRadius + Specification.LowerRadius * LowerContactRadius +
             LowerContactRadius * LowerContactRadius) / 3.0 +
         ScalarCriteria::Pi * BridgeLength *
            (LowerContactRadius * LowerContactRadius + LowerContactRadius * UpperContactRadius +
             UpperContactRadius * UpperContactRadius) / 3.0 +
         ScalarCriteria::Pi * (Specification.UpperHeight - Specification.UpperSetBack) *
            (UpperContactRadius * UpperContactRadius + UpperContactRadius * Specification.UpperRadius +
             Specification.UpperRadius * Specification.UpperRadius) / 3.0) *
        Specification.SweepAngle / ScalarCriteria::TwoPi;
    const BodyReport Report = Result.Payload.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Result.Payload.Vertices.size() != 10 ||
        Result.Payload.Edges.size() != 15 || Result.Payload.Coedges.size() != 30 || Result.Payload.Loops.size() != 7 ||
        Result.Payload.Faces.size() != 7)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                                          "half-turn unequal-setback bicone did not reach V10/E15/C30/L7/F7 topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence,
                                          "half-turn unequal-setback bicone volume failed analytic acceptance");
    return Result;
}

Deliver<HalfTurnUnequalRadiusBiconeUnequalSetbackChamferSpecification>
BlendSolver::ClassifyHalfTurnUnequalRadiusBiconeUnequalSetbackChamferVertex(
    const BrepBody& Body, int Vertex, double LowerSetBack, double UpperSetBack) noexcept
{
    if (!std::isfinite(LowerSetBack) || !std::isfinite(UpperSetBack) ||
        LowerSetBack <= ScalarCriteria::MergeTolerance || UpperSetBack <= ScalarCriteria::MergeTolerance)
        return Deliver<HalfTurnUnequalRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::DegenerateInput, "half-turn unequal-setback values must be finite and positive");
    const BodyReport Report = Body.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Body.Vertices.size() != 7 ||
        Body.Edges.size() != 11 || Body.Coedges.size() != 22 || Body.Loops.size() != 6 || Body.Faces.size() != 6)
        return Deliver<HalfTurnUnequalRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::NonManifold, "source is not the capped half-turn unequal bicone topology");
    if (Vertex < 0 || Vertex >= static_cast<int>(Body.Vertices.size()))
        return Deliver<HalfTurnUnequalRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::Unsupported, "selected half-turn unequal-setback vertex is out of range");
    int RevolutionFaces = 0, PlaneFaces = 0;
    for (const BrepFace& Face : Body.Faces)
    {
        if (Face.Loops.size() != 1)
            return Deliver<HalfTurnUnequalRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
                RefusalReason::Unsupported, "half-turn unequal-setback face is not single-loop");
        if (Face.Surface.Classification == SurfaceClassification::Revolution) ++RevolutionFaces;
        else if (Face.Surface.Classification == SurfaceClassification::Plane) ++PlaneFaces;
        else return Deliver<HalfTurnUnequalRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::Unsupported, "half-turn unequal-setback has unsupported face geometry");
    }
    if (RevolutionFaces != 4 || PlaneFaces != 2)
        return Deliver<HalfTurnUnequalRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::Unsupported, "half-turn unequal-setback does not have four revolve faces and two radial caps");
    std::vector<int> RimEdges, LineEdges;
    for (size_t I = 0; I < Body.Edges.size(); ++I)
    {
        const BrepEdge& EdgeData = Body.Edges[I];
        if (EdgeData.Coedges.size() != 2)
            return Deliver<HalfTurnUnequalRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
                RefusalReason::NonManifold, "half-turn unequal-setback edge is not manifold");
        if (!EdgeData.Closed() && EdgeData.Curve.Classification == CurveClassification::Arc &&
            EdgeData.Curve.Degree == 2 && EdgeData.Curve.Rational()) RimEdges.push_back(static_cast<int>(I));
        else if (!EdgeData.Closed() && EdgeData.Curve.Classification == CurveClassification::Line &&
                 EdgeData.Curve.Degree == 1) LineEdges.push_back(static_cast<int>(I));
        else return Deliver<HalfTurnUnequalRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::Unsupported, "half-turn unequal-setback has unsupported edge geometry");
    }
    if (RimEdges.size() != 2 || LineEdges.size() != 9)
        return Deliver<HalfTurnUnequalRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::Unsupported, "half-turn unequal-setback does not have two arc rims and nine lines");
    int Apex = -1;
    for (int Candidate = 0; Candidate < static_cast<int>(Body.Vertices.size()); ++Candidate)
    {
        int Degree = 0;
        for (int Edge : LineEdges)
            if (Body.Edges[Edge].VertexStart == Candidate || Body.Edges[Edge].VertexEnd == Candidate) ++Degree;
        if (Degree == 4)
        {
            if (Apex >= 0) return Deliver<HalfTurnUnequalRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
                RefusalReason::Unsupported, "half-turn unequal-setback has multiple apex candidates");
            Apex = Candidate;
        }
    }
    if (Apex < 0 || Vertex != Apex)
        return Deliver<HalfTurnUnequalRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::Unsupported, "selected vertex is not the half-turn unequal-setback apex");
    struct Rim { Vec3 Centre, Normal; double Radius = 0.0; int Edge = -1; } G[2];
    for (int K = 0; K < 2; ++K)
    {
        G[K].Edge = RimEdges[K];
        if (!CircularFrame(Body.Edges[G[K].Edge].Curve, G[K].Centre, G[K].Normal, G[K].Radius) || G[K].Radius <= Tol)
            return Deliver<HalfTurnUnequalRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
                RefusalReason::Unsupported, "half-turn unequal-setback rim is not circular");
    }
    Vec3 Axis = (G[1].Centre - G[0].Centre).Normalised();
    if (Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<HalfTurnUnequalRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::DegenerateInput, "half-turn unequal-setback axis is degenerate");
    if ((std::fabs(Axis.X) > ScalarCriteria::GeometricTolerance && Axis.X < 0.0) ||
        (std::fabs(Axis.X) <= ScalarCriteria::GeometricTolerance && std::fabs(Axis.Y) > ScalarCriteria::GeometricTolerance && Axis.Y < 0.0) ||
        (std::fabs(Axis.X) <= ScalarCriteria::GeometricTolerance && std::fabs(Axis.Y) <= ScalarCriteria::GeometricTolerance && Axis.Z < 0.0)) Axis = -Axis;
    const Vec3 ApexPoint = Body.Vertices[Apex].Point;
    for (const Rim& R : G)
        if (std::fabs(std::fabs(R.Normal.Dot(Axis)) - 1.0) > ScalarCriteria::AngularTolerance ||
            (R.Centre - ApexPoint - Axis * (R.Centre - ApexPoint).Dot(Axis)).Length() >
                ScalarCriteria::GeometricTolerance * std::max(1.0, R.Centre.Distance(ApexPoint)))
            return Deliver<HalfTurnUnequalRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
                RefusalReason::Unsupported, "half-turn unequal-setback rims are not coaxial");
    if ((G[0].Centre - G[1].Centre).Dot(Axis) > 0.0) std::swap(G[0], G[1]);
    const Vec3 CanonicalRadial = Workplane::FromNormal(ApexPoint, Axis).AxisX.Normalised();
    const Vec3 StartVector = (Body.Vertices[Body.Edges[G[0].Edge].VertexStart].Point - G[0].Centre).Normalised();
    const Vec3 EndVector = (Body.Vertices[Body.Edges[G[0].Edge].VertexEnd].Point - G[0].Centre).Normalised();
    const double Sweep = std::atan2(Axis.Dot(StartVector.Cross(EndVector)),
                                    ScalarCriteria::Clamp(StartVector.Dot(EndVector), -1.0, 1.0));
    if (std::fabs(Sweep - ScalarCriteria::Pi) > ScalarCriteria::SweepTolerance ||
        CanonicalRadial.Distance(StartVector) > ScalarCriteria::GeometricTolerance)
        return Deliver<HalfTurnUnequalRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::Unsupported, "half-turn unequal-setback sweep is not exactly a canonical half turn");
    for (size_t I = 0; I < Body.Faces.size(); ++I)
        if (Body.Faces[I].Surface.Classification == SurfaceClassification::Plane)
        {
            Vec3 Normal;
            if (!PlanarNormal(Body, static_cast<int>(I), Normal) || std::fabs(Normal.Dot(Axis)) > ScalarCriteria::AngularTolerance)
                return Deliver<HalfTurnUnequalRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
                    RefusalReason::Unsupported, "half-turn unequal-setback radial cap is not planar");
        }
    const double RadiusScale = std::max({ 1.0, G[0].Radius, G[1].Radius });
    if (std::fabs(G[0].Radius - G[1].Radius) <= ScalarCriteria::GeometricTolerance * RadiusScale)
        return Deliver<HalfTurnUnequalRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::Unsupported, "half-turn unequal-setback route requires unequal radii");
    const double SetbackScale = std::max({ 1.0, LowerSetBack, UpperSetBack });
    if (std::fabs(LowerSetBack - UpperSetBack) <= ScalarCriteria::GeometricTolerance * SetbackScale)
        return Deliver<HalfTurnUnequalRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::Unsupported, "half-turn route requires distinct lower and upper set-backs");
    const double LowerHeight = ApexPoint.Distance(G[0].Centre);
    const double UpperHeight = ApexPoint.Distance(G[1].Centre);
    if (LowerSetBack >= LowerHeight - Tol || UpperSetBack >= UpperHeight - Tol)
        return Deliver<HalfTurnUnequalRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::Unsupported, "half-turn unequal-setback consumes a support");
    HalfTurnUnequalRadiusBiconeUnequalSetbackChamferSpecification Specification;
    Specification.Apex = ApexPoint; Specification.Axis = Axis;
    Specification.LowerRadius = G[0].Radius; Specification.UpperRadius = G[1].Radius;
    Specification.LowerHeight = LowerHeight; Specification.UpperHeight = UpperHeight;
    Specification.LowerSetBack = LowerSetBack; Specification.UpperSetBack = UpperSetBack;
    Specification.SweepAngle = ScalarCriteria::Pi;
    const Deliver<BrepBody> Feasible = ReconstructHalfTurnUnequalRadiusBiconeUnequalSetbackChamfer(Specification);
    if (!Feasible) return Deliver<HalfTurnUnequalRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
        Feasible.Denial.Reason, Feasible.Denial.Detail);
    return Deliver<HalfTurnUnequalRadiusBiconeUnequalSetbackChamferSpecification>::Accept(std::move(Specification));
}


Deliver<BrepBody> BlendSolver::ReconstructPartialEqualRadiusBiconeUnequalSetbackChamfer(
    const PartialEqualRadiusBiconeUnequalSetbackChamferSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Apex.X) || !std::isfinite(Specification.Apex.Y) ||
        !std::isfinite(Specification.Apex.Z) || !std::isfinite(Specification.Radius) ||
        !std::isfinite(Specification.LowerHeight) || !std::isfinite(Specification.UpperHeight) ||
        !std::isfinite(Specification.LowerSetBack) || !std::isfinite(Specification.UpperSetBack) ||
        !std::isfinite(Specification.SweepAngle) || Specification.Radius <= Tol ||
        Specification.LowerHeight <= Tol || Specification.UpperHeight <= Tol ||
        Specification.LowerSetBack <= Tol || Specification.UpperSetBack <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "partial independent-setback bicone dimensions must be finite and positive");
    if (!std::isfinite(Specification.Axis.X) || !std::isfinite(Specification.Axis.Y) ||
        !std::isfinite(Specification.Axis.Z) || Specification.Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "partial independent-setback bicone axis is degenerate");
    if (Specification.SweepAngle <= ScalarCriteria::AngularTolerance ||
        Specification.SweepAngle >= ScalarCriteria::Pi - ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "partial independent-setback route requires a strict non-reflex sector");
    const double SetbackScale = std::max({ 1.0, Specification.LowerSetBack, Specification.UpperSetBack });
    if (std::fabs(Specification.LowerSetBack - Specification.UpperSetBack) <=
        ScalarCriteria::GeometricTolerance * SetbackScale)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "equal set-backs belong to the bounded partial equal route");
    if (Specification.LowerSetBack >= Specification.LowerHeight - Tol ||
        Specification.UpperSetBack >= Specification.UpperHeight - Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "partial independent-setback chamfer consumes a support");
    const Vec3 Apex = Specification.Apex;
    const Vec3 Axis = Specification.Axis.Normalised();
    const Vec3 Radial = Workplane::FromNormal(Apex, Axis).AxisX.Normalised();
    if (Radial.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "partial independent-setback radial frame is degenerate");
    const Vec3 LowerBase = Apex - Axis * Specification.LowerHeight;
    const Vec3 UpperBase = Apex + Axis * Specification.UpperHeight;
    const Vec3 LowerContact = Apex - Axis * Specification.LowerSetBack;
    const Vec3 UpperContact = Apex + Axis * Specification.UpperSetBack;
    const double LowerContactRadius = Specification.Radius * Specification.LowerSetBack / Specification.LowerHeight;
    const double UpperContactRadius = Specification.Radius * Specification.UpperSetBack / Specification.UpperHeight;
    if (LowerContactRadius <= Tol || UpperContactRadius <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "partial independent-setback contact ring is degenerate");
    auto RevolveLine = [&](Vec3 Start, Vec3 End) -> Deliver<NurbsSurface>
    {
        Deliver<NurbsCurve> Line = NurbsCurve::Line(Start, End);
        return Line ? NurbsSurface::Revolution(Line.Payload, Apex, Axis, Specification.SweepAngle)
                    : Deliver<NurbsSurface>::Reject(Line.Denial.Reason, Line.Denial.Detail);
    };
    Deliver<NurbsSurface> Lower = RevolveLine(LowerBase + Radial * Specification.Radius,
                                               LowerContact + Radial * LowerContactRadius);
    Deliver<NurbsSurface> Chamfer = RevolveLine(LowerContact + Radial * LowerContactRadius,
                                                UpperContact + Radial * UpperContactRadius);
    Deliver<NurbsSurface> Upper = RevolveLine(UpperContact + Radial * UpperContactRadius,
                                               UpperBase + Radial * Specification.Radius);
    Deliver<NurbsSurface> LowerDisk = RevolveLine(LowerBase, LowerBase + Radial * Specification.Radius);
    Deliver<NurbsSurface> UpperDisk = RevolveLine(UpperBase, UpperBase + Radial * Specification.Radius);
    if (!Lower || !Chamfer || !Upper || !LowerDisk || !UpperDisk)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "partial independent-setback bicone surfaces are degenerate");
    Lower.Payload.Classification = SurfaceClassification::Cone;
    Lower.Payload.Origin = LowerBase; Lower.Payload.Axis = Axis;
    Lower.Payload.RadiusMajor = Specification.Radius; Lower.Payload.RadiusMinor = LowerContactRadius;
    Chamfer.Payload.Classification = SurfaceClassification::Cone;
    Chamfer.Payload.Origin = LowerContact; Chamfer.Payload.Axis = Axis;
    Chamfer.Payload.RadiusMajor = LowerContactRadius; Chamfer.Payload.RadiusMinor = UpperContactRadius;
    Upper.Payload.Classification = SurfaceClassification::Cone;
    Upper.Payload.Origin = UpperContact; Upper.Payload.Axis = Axis;
    Upper.Payload.RadiusMajor = UpperContactRadius; Upper.Payload.RadiusMinor = Specification.Radius;
    LowerDisk.Payload.Classification = SurfaceClassification::Plane;
    LowerDisk.Payload.Origin = LowerBase; LowerDisk.Payload.Axis = Axis;
    UpperDisk.Payload.Classification = SurfaceClassification::Plane;
    UpperDisk.Payload.Origin = UpperBase; UpperDisk.Payload.Axis = Axis;
    Deliver<BrepBody> Result = BrepBody::Sew({ Lower.Payload, Chamfer.Payload, Upper.Payload,
                                                LowerDisk.Payload, UpperDisk.Payload }, Tol, true);
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason,
                                                   "partial independent-setback bicone surfaces could not be sewn");
    if (!CapRadialSector(Result.Payload, LowerBase, UpperBase, Radial, Specification.SweepAngle,
                         Specification.Radius) || !Result.Payload.Orient())
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                                          "partial independent-setback bicone radial caps could not heal");
    const double BridgeLength = Specification.LowerSetBack + Specification.UpperSetBack;
    const double ExpectedVolume =
        (ScalarCriteria::Pi * (Specification.LowerHeight - Specification.LowerSetBack) *
            (Specification.Radius * Specification.Radius + Specification.Radius * LowerContactRadius +
             LowerContactRadius * LowerContactRadius) / 3.0 +
         ScalarCriteria::Pi * BridgeLength *
            (LowerContactRadius * LowerContactRadius + LowerContactRadius * UpperContactRadius +
             UpperContactRadius * UpperContactRadius) / 3.0 +
         ScalarCriteria::Pi * (Specification.UpperHeight - Specification.UpperSetBack) *
            (UpperContactRadius * UpperContactRadius + UpperContactRadius * Specification.Radius +
             Specification.Radius * Specification.Radius) / 3.0) *
        Specification.SweepAngle / ScalarCriteria::TwoPi;
    const BodyReport Report = Result.Payload.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Result.Payload.Vertices.size() != 10 ||
        Result.Payload.Edges.size() != 15 || Result.Payload.Coedges.size() != 30 || Result.Payload.Loops.size() != 7 ||
        Result.Payload.Faces.size() != 7)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                                          "partial independent-setback bicone did not reach V10/E15/C30/L7/F7 topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence,
                                          "partial independent-setback bicone volume failed analytic acceptance");
    return Result;
}

Deliver<PartialEqualRadiusBiconeUnequalSetbackChamferSpecification>
BlendSolver::ClassifyPartialEqualRadiusBiconeUnequalSetbackChamferVertex(
    const BrepBody& Body, int Vertex, double LowerSetBack, double UpperSetBack) noexcept
{
    if (!std::isfinite(LowerSetBack) || !std::isfinite(UpperSetBack) ||
        LowerSetBack <= ScalarCriteria::MergeTolerance || UpperSetBack <= ScalarCriteria::MergeTolerance)
        return Deliver<PartialEqualRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::DegenerateInput, "partial independent-setback values must be finite and positive");
    const double SetbackScale = std::max({ 1.0, LowerSetBack, UpperSetBack });
    if (std::fabs(LowerSetBack - UpperSetBack) <= ScalarCriteria::GeometricTolerance * SetbackScale)
        return Deliver<PartialEqualRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
            RefusalReason::Unsupported, "equal set-backs belong to the bounded partial equal route");
    const double ProbeSetBack = std::min(LowerSetBack, UpperSetBack);
    const Deliver<PartialEqualRadiusBiconeApexChamferSpecification> Base =
        ClassifyPartialEqualRadiusBiconeApexChamferVertex(Body, Vertex, ProbeSetBack);
    if (!Base) return Deliver<PartialEqualRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
        Base.Denial.Reason, Base.Denial.Detail);
    PartialEqualRadiusBiconeUnequalSetbackChamferSpecification Specification;
    Specification.Apex = Base.Payload.Apex;
    Specification.Axis = Base.Payload.Axis;
    Specification.Radius = Base.Payload.Radius;
    Specification.LowerHeight = Base.Payload.LowerHeight;
    Specification.UpperHeight = Base.Payload.UpperHeight;
    Specification.LowerSetBack = LowerSetBack;
    Specification.UpperSetBack = UpperSetBack;
    Specification.SweepAngle = Base.Payload.SweepAngle;
    const Deliver<BrepBody> Feasible = ReconstructPartialEqualRadiusBiconeUnequalSetbackChamfer(Specification);
    if (!Feasible) return Deliver<PartialEqualRadiusBiconeUnequalSetbackChamferSpecification>::Reject(
        Feasible.Denial.Reason, Feasible.Denial.Detail);
    return Deliver<PartialEqualRadiusBiconeUnequalSetbackChamferSpecification>::Accept(std::move(Specification));
}

Deliver<BrepBody> BlendSolver::ReconstructPartialUnequalBiconeApexFillet(
    const PartialUnequalBiconeApexFilletSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Apex.X) || !std::isfinite(Specification.Apex.Y) ||
        !std::isfinite(Specification.Apex.Z) || !std::isfinite(Specification.LowerRadius) ||
        !std::isfinite(Specification.UpperRadius) || !std::isfinite(Specification.LowerHeight) ||
        !std::isfinite(Specification.UpperHeight) || !std::isfinite(Specification.FilletRadius) ||
        !std::isfinite(Specification.SweepAngle) || Specification.LowerRadius <= Tol ||
        Specification.UpperRadius <= Tol || Specification.LowerHeight <= Tol ||
        Specification.UpperHeight <= Tol || Specification.FilletRadius <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "partial unequal bicone fillet dimensions must be finite and positive");
    if (!std::isfinite(Specification.Axis.X) || !std::isfinite(Specification.Axis.Y) ||
        !std::isfinite(Specification.Axis.Z) || Specification.Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial unequal bicone fillet axis is degenerate");
    if (Specification.SweepAngle <= ScalarCriteria::AngularTolerance ||
        Specification.SweepAngle >= ScalarCriteria::Pi - ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "partial unequal bicone fillet requires a strict non-reflex sector");
    const double RadiusScale = std::max({ 1.0, Specification.LowerRadius, Specification.UpperRadius });
    if (std::fabs(Specification.LowerRadius - Specification.UpperRadius) <=
        ScalarCriteria::GeometricTolerance * RadiusScale)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "partial unequal bicone fillet requires distinct support radii");
    const Vec3 Apex = Specification.Apex;
    const Vec3 Axis = Specification.Axis.Normalised();
    const double LowerAngle = std::atan2(Specification.LowerRadius, Specification.LowerHeight);
    const double UpperAngle = std::atan2(Specification.UpperRadius, Specification.UpperHeight);
    const double SinSum = std::sin(LowerAngle + UpperAngle);
    if (SinSum <= ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "partial unequal bicone fillet support angles are degenerate");
    const double SinLower = std::sin(LowerAngle), SinUpper = std::sin(UpperAngle);
    const double CosLower = std::cos(LowerAngle), CosUpper = std::cos(UpperAngle);
    const double Q = Specification.FilletRadius;
    const double Major = Q * (SinLower + SinUpper) / SinSum;
    const double CentreOffset = Q * (CosUpper - CosLower) / SinSum;
    if (Major <= Q + Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "partial unequal bicone torus would cross the axis");
    const double LowerTangent = Major * SinLower - CentreOffset * CosLower;
    const double UpperTangent = Major * SinUpper + CentreOffset * CosUpper;
    const double LowerSlant = std::hypot(Specification.LowerHeight, Specification.LowerRadius);
    const double UpperSlant = std::hypot(Specification.UpperHeight, Specification.UpperRadius);
    if (LowerTangent <= Tol || UpperTangent <= Tol || LowerTangent >= LowerSlant - Tol ||
        UpperTangent >= UpperSlant - Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "partial unequal bicone torus contact consumes a conical support");
    const Vec3 Radial = Workplane::FromNormal(Apex, Axis).AxisX.Normalised();
    if (Radial.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial unequal bicone fillet radial frame is degenerate");
    const Vec3 LowerBase = Apex - Axis * Specification.LowerHeight;
    const Vec3 UpperBase = Apex + Axis * Specification.UpperHeight;
    const Vec3 LowerContact = Apex - Axis * (LowerTangent * CosLower) + Radial * (LowerTangent * SinLower);
    const Vec3 UpperContact = Apex + Axis * (UpperTangent * CosUpper) + Radial * (UpperTangent * SinUpper);
    const Vec3 InnerPoint = Apex + Axis * CentreOffset + Radial * (Major - Q);
    const double LowerContactRadius = LowerTangent * SinLower;
    const double UpperContactRadius = UpperTangent * SinUpper;
    if (LowerContactRadius <= Tol || UpperContactRadius <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial unequal bicone torus contact ring is degenerate");
    Deliver<NurbsCurve> LowerProfile = NurbsCurve::Line(LowerBase + Radial * Specification.LowerRadius, LowerContact);
    Deliver<NurbsCurve> UpperProfile = NurbsCurve::Line(UpperContact, UpperBase + Radial * Specification.UpperRadius);
    Deliver<NurbsCurve> TorusProfile = NurbsCurve::ArcThreePoints(LowerContact, InnerPoint, UpperContact);
    Deliver<NurbsCurve> LowerDiskProfile = NurbsCurve::Line(LowerBase, LowerBase + Radial * Specification.LowerRadius);
    Deliver<NurbsCurve> UpperDiskProfile = NurbsCurve::Line(UpperBase, UpperBase + Radial * Specification.UpperRadius);
    if (!LowerProfile || !UpperProfile || !TorusProfile || !LowerDiskProfile || !UpperDiskProfile)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial unequal bicone torus profiles are degenerate");
    auto Revolve = [&](const Deliver<NurbsCurve>& Profile) -> Deliver<NurbsSurface>
    {
        return NurbsSurface::Revolution(Profile.Payload, Apex, Axis, Specification.SweepAngle);
    };
    Deliver<NurbsSurface> Lower = Revolve(LowerProfile), Torus = Revolve(TorusProfile),
                              Upper = Revolve(UpperProfile), LowerDisk = Revolve(LowerDiskProfile),
                              UpperDisk = Revolve(UpperDiskProfile);
    if (!Lower || !Torus || !Upper || !LowerDisk || !UpperDisk)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial unequal bicone torus surfaces are degenerate");
    Lower.Payload.Classification = SurfaceClassification::Cone;
    Lower.Payload.Origin = LowerBase; Lower.Payload.Axis = Axis;
    Lower.Payload.RadiusMajor = Specification.LowerRadius; Lower.Payload.RadiusMinor = LowerContactRadius;
    Lower.Payload.HalfAngle = std::atan2(Specification.LowerRadius - LowerContactRadius,
                                         Specification.LowerHeight - LowerTangent * CosLower);
    Upper.Payload.Classification = SurfaceClassification::Cone;
    Upper.Payload.Origin = UpperContact; Upper.Payload.Axis = Axis;
    Upper.Payload.RadiusMajor = UpperContactRadius; Upper.Payload.RadiusMinor = Specification.UpperRadius;
    Upper.Payload.HalfAngle = std::atan2(Specification.UpperRadius - UpperContactRadius,
                                         Specification.UpperHeight - UpperTangent * CosUpper);
    Torus.Payload.Classification = SurfaceClassification::Torus;
    Torus.Payload.Origin = Apex + Axis * CentreOffset; Torus.Payload.Axis = Axis;
    Torus.Payload.RadiusMajor = Major; Torus.Payload.RadiusMinor = Q;
    LowerDisk.Payload.Classification = SurfaceClassification::Plane;
    LowerDisk.Payload.Origin = LowerBase; LowerDisk.Payload.Axis = Axis;
    UpperDisk.Payload.Classification = SurfaceClassification::Plane;
    UpperDisk.Payload.Origin = UpperBase; UpperDisk.Payload.Axis = Axis;
    Deliver<BrepBody> Result = BrepBody::Sew({ Lower.Payload, Torus.Payload, Upper.Payload,
                                                LowerDisk.Payload, UpperDisk.Payload }, Tol, true);
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, "partial unequal bicone torus surfaces could not be sewn");
    if (!CapRadialSector(Result.Payload, LowerBase, UpperBase, Radial, Specification.SweepAngle,
                         std::max(Specification.LowerRadius, Specification.UpperRadius)) || !Result.Payload.Orient())
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "partial unequal bicone torus radial caps could not heal");
    const double LowerDepth = LowerTangent * CosLower;
    const double UpperDepth = UpperTangent * CosUpper;
    const double ThetaLower = std::atan2(-LowerDepth - CentreOffset, LowerContactRadius - Major);
    double ThetaUpper = std::atan2(UpperDepth - CentreOffset, UpperContactRadius - Major);
    while (ThetaUpper >= ThetaLower) ThetaUpper -= ScalarCriteria::TwoPi;
    const double ArcSpan = ThetaUpper - ThetaLower;
    if (ArcSpan >= -ScalarCriteria::AngularTolerance || ArcSpan < -ScalarCriteria::Pi - ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence, "partial unequal bicone torus selected an invalid contact arc");
    const auto Primitive = [&](double Theta) noexcept
    {
        const double Sine = std::sin(Theta);
        return Major * Major * Sine + Major * Q * (Theta + std::sin(2.0 * Theta) / 2.0) +
               Q * Q * (Sine - Sine * Sine * Sine / 3.0);
    };
    const double MeridionalIntegral = Q * (Primitive(ThetaUpper) - Primitive(ThetaLower));
    const double LowerRetained = Specification.LowerHeight - LowerDepth;
    const double UpperRetained = Specification.UpperHeight - UpperDepth;
    const double FullVolume =
        ScalarCriteria::Pi * LowerRetained *
            (Specification.LowerRadius * Specification.LowerRadius + Specification.LowerRadius * LowerContactRadius +
             LowerContactRadius * LowerContactRadius) / 3.0 +
        ScalarCriteria::Pi * MeridionalIntegral +
        ScalarCriteria::Pi * UpperRetained *
            (UpperContactRadius * UpperContactRadius + UpperContactRadius * Specification.UpperRadius +
             Specification.UpperRadius * Specification.UpperRadius) / 3.0;
    const BodyReport Report = Result.Payload.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Result.Payload.Vertices.size() != 10 ||
        Result.Payload.Edges.size() != 15 || Result.Payload.Coedges.size() != 30 || Result.Payload.Loops.size() != 7 ||
        Result.Payload.Faces.size() != 7)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                                          "partial unequal bicone torus fillet did not reach V10/E15/C30/L7/F7 topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, FullVolume * Specification.SweepAngle / ScalarCriteria::TwoPi))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence, "partial unequal bicone torus volume failed analytic acceptance");
    return Result;
}

Deliver<PartialUnequalBiconeApexFilletSpecification>
BlendSolver::ClassifyPartialUnequalBiconeApexFilletVertex(const BrepBody& Body, int Vertex, double FilletRadius) noexcept
{
    if (!std::isfinite(FilletRadius) || FilletRadius <= ScalarCriteria::MergeTolerance)
        return Deliver<PartialUnequalBiconeApexFilletSpecification>::Reject(RefusalReason::DegenerateInput,
                                                                              "partial unequal bicone fillet radius must be finite and positive");
    const double ProbeSetBack = std::max(ScalarCriteria::MergeTolerance * 10.0,
                                         std::min(Body.Bounds().High.Distance(Body.Bounds().Low), 1.0) * 1e-3);
    const Deliver<PartialUnequalConeApexChamferSpecification> Base =
        ClassifyPartialUnequalConeApexChamferVertex(Body, Vertex, ProbeSetBack);
    if (!Base) return Deliver<PartialUnequalBiconeApexFilletSpecification>::Reject(Base.Denial.Reason, Base.Denial.Detail);
    PartialUnequalBiconeApexFilletSpecification Specification;
    Specification.Apex = Base.Payload.Apex; Specification.Axis = Base.Payload.Axis;
    Specification.LowerRadius = Base.Payload.LowerRadius; Specification.UpperRadius = Base.Payload.UpperRadius;
    Specification.LowerHeight = Base.Payload.LowerHeight; Specification.UpperHeight = Base.Payload.UpperHeight;
    Specification.FilletRadius = FilletRadius; Specification.SweepAngle = Base.Payload.SweepAngle;
    const Deliver<BrepBody> Feasible = ReconstructPartialUnequalBiconeApexFillet(Specification);
    if (!Feasible) return Deliver<PartialUnequalBiconeApexFilletSpecification>::Reject(Feasible.Denial.Reason, Feasible.Denial.Detail);
    return Deliver<PartialUnequalBiconeApexFilletSpecification>::Accept(std::move(Specification));
}


Deliver<BrepBody> BlendSolver::ReconstructHalfTurnUnequalRadiusBiconeApexFillet(
    const HalfTurnUnequalRadiusBiconeApexFilletSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Apex.X) || !std::isfinite(Specification.Apex.Y) ||
        !std::isfinite(Specification.Apex.Z) || !std::isfinite(Specification.LowerRadius) ||
        !std::isfinite(Specification.UpperRadius) || !std::isfinite(Specification.LowerHeight) ||
        !std::isfinite(Specification.UpperHeight) || !std::isfinite(Specification.FilletRadius) ||
        !std::isfinite(Specification.SweepAngle) || Specification.LowerRadius <= Tol ||
        Specification.UpperRadius <= Tol || Specification.LowerHeight <= Tol ||
        Specification.UpperHeight <= Tol || Specification.FilletRadius <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "half-turn unequal bicone fillet dimensions must be finite and positive");
    if (!std::isfinite(Specification.Axis.X) || !std::isfinite(Specification.Axis.Y) ||
        !std::isfinite(Specification.Axis.Z) || Specification.Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "half-turn unequal bicone fillet axis is degenerate");
    if (std::fabs(Specification.SweepAngle - ScalarCriteria::Pi) > ScalarCriteria::SweepTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "half-turn unequal bicone fillet requires exactly a half turn");
    const double RadiusScale = std::max({ 1.0, Specification.LowerRadius, Specification.UpperRadius });
    if (std::fabs(Specification.LowerRadius - Specification.UpperRadius) <=
        ScalarCriteria::GeometricTolerance * RadiusScale)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "half-turn unequal bicone fillet requires distinct support radii");
    const Vec3 Apex = Specification.Apex;
    const Vec3 Axis = Specification.Axis.Normalised();
    const double LowerAngle = std::atan2(Specification.LowerRadius, Specification.LowerHeight);
    const double UpperAngle = std::atan2(Specification.UpperRadius, Specification.UpperHeight);
    const double SinSum = std::sin(LowerAngle + UpperAngle);
    if (SinSum <= ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "half-turn unequal bicone fillet support angles are degenerate");
    const double SinLower = std::sin(LowerAngle), SinUpper = std::sin(UpperAngle);
    const double CosLower = std::cos(LowerAngle), CosUpper = std::cos(UpperAngle);
    const double Q = Specification.FilletRadius;
    const double Major = Q * (SinLower + SinUpper) / SinSum;
    const double CentreOffset = Q * (CosUpper - CosLower) / SinSum;
    if (Major <= Q + Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "half-turn unequal bicone torus would cross the axis");
    const double LowerTangent = Major * SinLower - CentreOffset * CosLower;
    const double UpperTangent = Major * SinUpper + CentreOffset * CosUpper;
    const double LowerSlant = std::hypot(Specification.LowerHeight, Specification.LowerRadius);
    const double UpperSlant = std::hypot(Specification.UpperHeight, Specification.UpperRadius);
    if (LowerTangent <= Tol || UpperTangent <= Tol || LowerTangent >= LowerSlant - Tol ||
        UpperTangent >= UpperSlant - Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "half-turn unequal bicone torus contact consumes a conical support");
    const Vec3 Radial = Workplane::FromNormal(Apex, Axis).AxisX.Normalised();
    if (Radial.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "half-turn unequal bicone fillet radial frame is degenerate");
    const Vec3 LowerBase = Apex - Axis * Specification.LowerHeight;
    const Vec3 UpperBase = Apex + Axis * Specification.UpperHeight;
    const Vec3 LowerContact = Apex - Axis * (LowerTangent * CosLower) + Radial * (LowerTangent * SinLower);
    const Vec3 UpperContact = Apex + Axis * (UpperTangent * CosUpper) + Radial * (UpperTangent * SinUpper);
    const Vec3 InnerPoint = Apex + Axis * CentreOffset + Radial * (Major - Q);
    const double LowerContactRadius = LowerTangent * SinLower;
    const double UpperContactRadius = UpperTangent * SinUpper;
    if (LowerContactRadius <= Tol || UpperContactRadius <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "half-turn unequal bicone torus contact ring is degenerate");
    Deliver<NurbsCurve> LowerProfile = NurbsCurve::Line(LowerBase + Radial * Specification.LowerRadius, LowerContact);
    Deliver<NurbsCurve> UpperProfile = NurbsCurve::Line(UpperContact, UpperBase + Radial * Specification.UpperRadius);
    Deliver<NurbsCurve> TorusProfile = NurbsCurve::ArcThreePoints(LowerContact, InnerPoint, UpperContact);
    Deliver<NurbsCurve> LowerDiskProfile = NurbsCurve::Line(LowerBase, LowerBase + Radial * Specification.LowerRadius);
    Deliver<NurbsCurve> UpperDiskProfile = NurbsCurve::Line(UpperBase, UpperBase + Radial * Specification.UpperRadius);
    if (!LowerProfile || !UpperProfile || !TorusProfile || !LowerDiskProfile || !UpperDiskProfile)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "half-turn unequal bicone torus profiles are degenerate");
    auto Revolve = [&](const Deliver<NurbsCurve>& Profile) -> Deliver<NurbsSurface>
    {
        return NurbsSurface::Revolution(Profile.Payload, Apex, Axis, Specification.SweepAngle);
    };
    Deliver<NurbsSurface> Lower = Revolve(LowerProfile), Torus = Revolve(TorusProfile),
                              Upper = Revolve(UpperProfile), LowerDisk = Revolve(LowerDiskProfile),
                              UpperDisk = Revolve(UpperDiskProfile);
    if (!Lower || !Torus || !Upper || !LowerDisk || !UpperDisk)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "half-turn unequal bicone torus surfaces are degenerate");
    Lower.Payload.Classification = SurfaceClassification::Cone;
    Lower.Payload.Origin = LowerBase; Lower.Payload.Axis = Axis;
    Lower.Payload.RadiusMajor = Specification.LowerRadius; Lower.Payload.RadiusMinor = LowerContactRadius;
    Lower.Payload.HalfAngle = std::atan2(Specification.LowerRadius - LowerContactRadius,
                                         Specification.LowerHeight - LowerTangent * CosLower);
    Upper.Payload.Classification = SurfaceClassification::Cone;
    Upper.Payload.Origin = UpperContact; Upper.Payload.Axis = Axis;
    Upper.Payload.RadiusMajor = UpperContactRadius; Upper.Payload.RadiusMinor = Specification.UpperRadius;
    Upper.Payload.HalfAngle = std::atan2(Specification.UpperRadius - UpperContactRadius,
                                         Specification.UpperHeight - UpperTangent * CosUpper);
    Torus.Payload.Classification = SurfaceClassification::Torus;
    Torus.Payload.Origin = Apex + Axis * CentreOffset; Torus.Payload.Axis = Axis;
    Torus.Payload.RadiusMajor = Major; Torus.Payload.RadiusMinor = Q;
    LowerDisk.Payload.Classification = SurfaceClassification::Plane;
    LowerDisk.Payload.Origin = LowerBase; LowerDisk.Payload.Axis = Axis;
    UpperDisk.Payload.Classification = SurfaceClassification::Plane;
    UpperDisk.Payload.Origin = UpperBase; UpperDisk.Payload.Axis = Axis;
    Deliver<BrepBody> Result = BrepBody::Sew({ Lower.Payload, Torus.Payload, Upper.Payload,
                                                LowerDisk.Payload, UpperDisk.Payload }, Tol, false);
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, "half-turn unequal bicone torus surfaces could not be sewn");
    if (!CapRadialSector(Result.Payload, LowerBase, UpperBase, Radial, Specification.SweepAngle,
                         std::max(Specification.LowerRadius, Specification.UpperRadius)) || !Result.Payload.Orient())
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "half-turn unequal bicone torus radial caps could not heal");
    const double LowerDepth = LowerTangent * CosLower;
    const double UpperDepth = UpperTangent * CosUpper;
    const double ThetaLower = std::atan2(-LowerDepth - CentreOffset, LowerContactRadius - Major);
    double ThetaUpper = std::atan2(UpperDepth - CentreOffset, UpperContactRadius - Major);
    while (ThetaUpper >= ThetaLower) ThetaUpper -= ScalarCriteria::TwoPi;
    const double ArcSpan = ThetaUpper - ThetaLower;
    if (ArcSpan >= -ScalarCriteria::AngularTolerance || ArcSpan < -ScalarCriteria::Pi - ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence, "half-turn unequal bicone torus selected an invalid contact arc");
    const auto Primitive = [&](double Theta) noexcept
    {
        const double Sine = std::sin(Theta);
        return Major * Major * Sine + Major * Q * (Theta + std::sin(2.0 * Theta) / 2.0) +
               Q * Q * (Sine - Sine * Sine * Sine / 3.0);
    };
    const double MeridionalIntegral = Q * (Primitive(ThetaUpper) - Primitive(ThetaLower));
    const double LowerRetained = Specification.LowerHeight - LowerDepth;
    const double UpperRetained = Specification.UpperHeight - UpperDepth;
    const double FullVolume =
        ScalarCriteria::Pi * LowerRetained *
            (Specification.LowerRadius * Specification.LowerRadius + Specification.LowerRadius * LowerContactRadius +
             LowerContactRadius * LowerContactRadius) / 3.0 +
        ScalarCriteria::Pi * MeridionalIntegral +
        ScalarCriteria::Pi * UpperRetained *
            (UpperContactRadius * UpperContactRadius + UpperContactRadius * Specification.UpperRadius +
             Specification.UpperRadius * Specification.UpperRadius) / 3.0;
    const BodyReport Report = Result.Payload.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Result.Payload.Vertices.size() != 10 ||
        Result.Payload.Edges.size() != 15 || Result.Payload.Coedges.size() != 30 || Result.Payload.Loops.size() != 7 ||
        Result.Payload.Faces.size() != 7)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                                          "half-turn unequal bicone torus fillet did not reach V10/E15/C30/L7/F7 topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, FullVolume * Specification.SweepAngle / ScalarCriteria::TwoPi))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence, "half-turn unequal bicone torus volume failed analytic acceptance");
    return Result;
}

Deliver<HalfTurnUnequalRadiusBiconeApexFilletSpecification>
BlendSolver::ClassifyHalfTurnUnequalRadiusBiconeApexFilletVertex(const BrepBody& Body, int Vertex, double FilletRadius) noexcept
{
    if (!std::isfinite(FilletRadius) || FilletRadius <= ScalarCriteria::MergeTolerance)
        return Deliver<HalfTurnUnequalRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::DegenerateInput,
                                                                              "half-turn unequal bicone fillet radius must be finite and positive");
    const BodyReport Report = Body.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Body.Vertices.size() != 7 ||
        Body.Edges.size() != 11 || Body.Coedges.size() != 22 || Body.Loops.size() != 6 || Body.Faces.size() != 6)
        return Deliver<HalfTurnUnequalRadiusBiconeApexFilletSpecification>::Reject(
            RefusalReason::NonManifold, "source is not the capped half-turn unequal bicone topology");
    if (Vertex < 0 || Vertex >= static_cast<int>(Body.Vertices.size()))
        return Deliver<HalfTurnUnequalRadiusBiconeApexFilletSpecification>::Reject(
            RefusalReason::Unsupported, "selected half-turn unequal bicone vertex is out of range");
    int RevolutionFaces = 0, PlaneFaces = 0;
    for (const BrepFace& Face : Body.Faces)
    {
        if (Face.Loops.size() != 1)
            return Deliver<HalfTurnUnequalRadiusBiconeApexFilletSpecification>::Reject(
                RefusalReason::Unsupported, "half-turn unequal bicone face is not single-loop");
        if (Face.Surface.Classification == SurfaceClassification::Revolution) ++RevolutionFaces;
        else if (Face.Surface.Classification == SurfaceClassification::Plane) ++PlaneFaces;
        else return Deliver<HalfTurnUnequalRadiusBiconeApexFilletSpecification>::Reject(
            RefusalReason::Unsupported, "half-turn unequal bicone has unsupported face geometry");
    }
    if (RevolutionFaces != 4 || PlaneFaces != 2)
        return Deliver<HalfTurnUnequalRadiusBiconeApexFilletSpecification>::Reject(
            RefusalReason::Unsupported, "half-turn unequal bicone does not have four revolve faces and two radial caps");
    std::vector<int> RimEdges, LineEdges;
    for (size_t I = 0; I < Body.Edges.size(); ++I)
    {
        const BrepEdge& EdgeData = Body.Edges[I];
        if (EdgeData.Coedges.size() != 2)
            return Deliver<HalfTurnUnequalRadiusBiconeApexFilletSpecification>::Reject(
                RefusalReason::NonManifold, "half-turn unequal bicone edge is not manifold");
        if (!EdgeData.Closed() && EdgeData.Curve.Classification == CurveClassification::Arc &&
            EdgeData.Curve.Degree == 2 && EdgeData.Curve.Rational()) RimEdges.push_back(static_cast<int>(I));
        else if (!EdgeData.Closed() && EdgeData.Curve.Classification == CurveClassification::Line &&
                 EdgeData.Curve.Degree == 1) LineEdges.push_back(static_cast<int>(I));
        else return Deliver<HalfTurnUnequalRadiusBiconeApexFilletSpecification>::Reject(
            RefusalReason::Unsupported, "half-turn unequal bicone has unsupported edge geometry");
    }
    if (RimEdges.size() != 2 || LineEdges.size() != 9)
        return Deliver<HalfTurnUnequalRadiusBiconeApexFilletSpecification>::Reject(
            RefusalReason::Unsupported, "half-turn unequal bicone does not have two arc rims and nine lines");
    int ApexIndex = -1;
    for (int Candidate = 0; Candidate < static_cast<int>(Body.Vertices.size()); ++Candidate)
    {
        int Degree = 0;
        for (int Edge : LineEdges)
            if (Body.Edges[Edge].VertexStart == Candidate || Body.Edges[Edge].VertexEnd == Candidate) ++Degree;
        if (Degree == 4)
        {
            if (ApexIndex >= 0) return Deliver<HalfTurnUnequalRadiusBiconeApexFilletSpecification>::Reject(
                RefusalReason::Unsupported, "half-turn unequal bicone has multiple apex candidates");
            ApexIndex = Candidate;
        }
    }
    if (ApexIndex < 0 || Vertex != ApexIndex)
        return Deliver<HalfTurnUnequalRadiusBiconeApexFilletSpecification>::Reject(
            RefusalReason::Unsupported, "selected vertex is not the half-turn unequal bicone apex");
    struct Rim { Vec3 Centre, Normal; double Radius = 0.0; int Edge = -1; } G[2];
    for (int K = 0; K < 2; ++K)
    {
        G[K].Edge = RimEdges[K];
        if (!CircularFrame(Body.Edges[G[K].Edge].Curve, G[K].Centre, G[K].Normal, G[K].Radius) || G[K].Radius <= Tol)
            return Deliver<HalfTurnUnequalRadiusBiconeApexFilletSpecification>::Reject(
                RefusalReason::Unsupported, "half-turn unequal bicone rim is not circular");
    }
    Vec3 Axis = (G[1].Centre - G[0].Centre).Normalised();
    if (Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<HalfTurnUnequalRadiusBiconeApexFilletSpecification>::Reject(
            RefusalReason::DegenerateInput, "half-turn unequal bicone axis is degenerate");
    if ((std::fabs(Axis.X) > ScalarCriteria::GeometricTolerance && Axis.X < 0.0) ||
        (std::fabs(Axis.X) <= ScalarCriteria::GeometricTolerance && std::fabs(Axis.Y) > ScalarCriteria::GeometricTolerance && Axis.Y < 0.0) ||
        (std::fabs(Axis.X) <= ScalarCriteria::GeometricTolerance && std::fabs(Axis.Y) <= ScalarCriteria::GeometricTolerance && Axis.Z < 0.0)) Axis = -Axis;
    const Vec3 ApexPoint = Body.Vertices[ApexIndex].Point;
    for (const Rim& R : G)
        if (std::fabs(std::fabs(R.Normal.Dot(Axis)) - 1.0) > ScalarCriteria::AngularTolerance ||
            (R.Centre - ApexPoint - Axis * (R.Centre - ApexPoint).Dot(Axis)).Length() >
                ScalarCriteria::GeometricTolerance * std::max(1.0, R.Centre.Distance(ApexPoint)))
            return Deliver<HalfTurnUnequalRadiusBiconeApexFilletSpecification>::Reject(
                RefusalReason::Unsupported, "half-turn unequal bicone rims are not coaxial");
    if ((G[0].Centre - G[1].Centre).Dot(Axis) > 0.0) std::swap(G[0], G[1]);
    const Vec3 CanonicalRadial = Workplane::FromNormal(ApexPoint, Axis).AxisX.Normalised();
    const Vec3 StartVector = (Body.Vertices[Body.Edges[G[0].Edge].VertexStart].Point - G[0].Centre).Normalised();
    const Vec3 EndVector = (Body.Vertices[Body.Edges[G[0].Edge].VertexEnd].Point - G[0].Centre).Normalised();
    const double Sweep = std::atan2(Axis.Dot(StartVector.Cross(EndVector)),
                                    ScalarCriteria::Clamp(StartVector.Dot(EndVector), -1.0, 1.0));
    if (std::fabs(Sweep - ScalarCriteria::Pi) > ScalarCriteria::SweepTolerance ||
        CanonicalRadial.Distance(StartVector) > ScalarCriteria::GeometricTolerance)
        return Deliver<HalfTurnUnequalRadiusBiconeApexFilletSpecification>::Reject(
            RefusalReason::Unsupported, "half-turn unequal bicone sweep is not exactly a canonical half turn");
    for (size_t I = 0; I < Body.Faces.size(); ++I)
        if (Body.Faces[I].Surface.Classification == SurfaceClassification::Plane)
        {
            Vec3 Normal;
            if (!PlanarNormal(Body, static_cast<int>(I), Normal) || std::fabs(Normal.Dot(Axis)) > ScalarCriteria::AngularTolerance)
                return Deliver<HalfTurnUnequalRadiusBiconeApexFilletSpecification>::Reject(
                    RefusalReason::Unsupported, "half-turn unequal bicone radial cap is not planar");
        }
    const double RadiusScale = std::max({ 1.0, G[0].Radius, G[1].Radius });
    if (std::fabs(G[0].Radius - G[1].Radius) <= ScalarCriteria::GeometricTolerance * RadiusScale)
        return Deliver<HalfTurnUnequalRadiusBiconeApexFilletSpecification>::Reject(
            RefusalReason::Unsupported, "half-turn unequal bicone route requires distinct support radii");
    HalfTurnUnequalRadiusBiconeApexFilletSpecification Specification;
    Specification.Apex = ApexPoint; Specification.Axis = Axis;
    Specification.LowerRadius = G[0].Radius; Specification.UpperRadius = G[1].Radius;
    Specification.LowerHeight = ApexPoint.Distance(G[0].Centre); Specification.UpperHeight = ApexPoint.Distance(G[1].Centre);
    Specification.FilletRadius = FilletRadius; Specification.SweepAngle = ScalarCriteria::Pi;
    const Deliver<BrepBody> Feasible = ReconstructHalfTurnUnequalRadiusBiconeApexFillet(Specification);
    if (!Feasible) return Deliver<HalfTurnUnequalRadiusBiconeApexFilletSpecification>::Reject(
        Feasible.Denial.Reason, Feasible.Denial.Detail);
    return Deliver<HalfTurnUnequalRadiusBiconeApexFilletSpecification>::Accept(std::move(Specification));
}


Deliver<BrepBody> BlendSolver::ReconstructHalfTurnUnequalRadiusBiconeApexChamfer(
    const HalfTurnUnequalRadiusBiconeApexChamferSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Apex.X) || !std::isfinite(Specification.Apex.Y) ||
        !std::isfinite(Specification.Apex.Z) || !std::isfinite(Specification.LowerRadius) ||
        !std::isfinite(Specification.UpperRadius) || !std::isfinite(Specification.LowerHeight) ||
        !std::isfinite(Specification.UpperHeight) || !std::isfinite(Specification.SetBack) ||
        !std::isfinite(Specification.SweepAngle) || Specification.LowerRadius <= Tol ||
        Specification.UpperRadius <= Tol || Specification.LowerHeight <= Tol ||
        Specification.UpperHeight <= Tol || Specification.SetBack <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "half-turn unequal bicone dimensions must be finite and positive");
    if (!std::isfinite(Specification.Axis.X) || !std::isfinite(Specification.Axis.Y) ||
        !std::isfinite(Specification.Axis.Z) || Specification.Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "half-turn unequal bicone axis is degenerate");
    if (std::fabs(Specification.SweepAngle - ScalarCriteria::Pi) > ScalarCriteria::SweepTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "half-turn unequal bicone route requires exactly a half turn");
    const double RadiusScale = std::max({ 1.0, Specification.LowerRadius, Specification.UpperRadius });
    if (std::fabs(Specification.LowerRadius - Specification.UpperRadius) <=
        ScalarCriteria::GeometricTolerance * RadiusScale)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "half-turn unequal bicone route requires distinct support radii");
    if (Specification.SetBack >= std::min(Specification.LowerHeight, Specification.UpperHeight) - Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "half-turn unequal bicone set-back consumes a support");
    const Vec3 Apex = Specification.Apex;
    const Vec3 Axis = Specification.Axis.Normalised();
    const Vec3 Radial = Workplane::FromNormal(Apex, Axis).AxisX.Normalised();
    if (Radial.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "half-turn unequal bicone radial frame is degenerate");
    const Vec3 LowerBase = Apex - Axis * Specification.LowerHeight;
    const Vec3 UpperBase = Apex + Axis * Specification.UpperHeight;
    const Vec3 LowerContact = Apex - Axis * Specification.SetBack;
    const Vec3 UpperContact = Apex + Axis * Specification.SetBack;
    const double LowerContactRadius = Specification.LowerRadius * Specification.SetBack / Specification.LowerHeight;
    const double UpperContactRadius = Specification.UpperRadius * Specification.SetBack / Specification.UpperHeight;
    if (LowerContactRadius <= Tol || UpperContactRadius <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "half-turn unequal bicone contact ring is degenerate");
    auto RevolveLine = [&](Vec3 Start, Vec3 End) -> Deliver<NurbsSurface>
    {
        Deliver<NurbsCurve> Line = NurbsCurve::Line(Start, End);
        return Line ? NurbsSurface::Revolution(Line.Payload, Apex, Axis, Specification.SweepAngle)
                    : Deliver<NurbsSurface>::Reject(Line.Denial.Reason, Line.Denial.Detail);
    };
    Deliver<NurbsSurface> Lower = RevolveLine(LowerBase + Radial * Specification.LowerRadius,
                                               LowerContact + Radial * LowerContactRadius);
    Deliver<NurbsSurface> Chamfer = RevolveLine(LowerContact + Radial * LowerContactRadius,
                                                UpperContact + Radial * UpperContactRadius);
    Deliver<NurbsSurface> Upper = RevolveLine(UpperContact + Radial * UpperContactRadius,
                                               UpperBase + Radial * Specification.UpperRadius);
    Deliver<NurbsSurface> LowerDisk = RevolveLine(LowerBase, LowerBase + Radial * Specification.LowerRadius);
    Deliver<NurbsSurface> UpperDisk = RevolveLine(UpperBase, UpperBase + Radial * Specification.UpperRadius);
    if (!Lower || !Chamfer || !Upper || !LowerDisk || !UpperDisk)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "half-turn unequal bicone surfaces are degenerate");
    Lower.Payload.Classification = SurfaceClassification::Cone;
    Lower.Payload.Origin = LowerBase; Lower.Payload.Axis = Axis;
    Lower.Payload.RadiusMajor = Specification.LowerRadius; Lower.Payload.RadiusMinor = LowerContactRadius;
    Chamfer.Payload.Classification = SurfaceClassification::Cone;
    Chamfer.Payload.Origin = LowerContact; Chamfer.Payload.Axis = Axis;
    Chamfer.Payload.RadiusMajor = LowerContactRadius; Chamfer.Payload.RadiusMinor = UpperContactRadius;
    Upper.Payload.Classification = SurfaceClassification::Cone;
    Upper.Payload.Origin = UpperContact; Upper.Payload.Axis = Axis;
    Upper.Payload.RadiusMajor = UpperContactRadius; Upper.Payload.RadiusMinor = Specification.UpperRadius;
    LowerDisk.Payload.Classification = SurfaceClassification::Plane;
    LowerDisk.Payload.Origin = LowerBase; LowerDisk.Payload.Axis = Axis;
    UpperDisk.Payload.Classification = SurfaceClassification::Plane;
    UpperDisk.Payload.Origin = UpperBase; UpperDisk.Payload.Axis = Axis;
    Deliver<BrepBody> Result = BrepBody::Sew({ Lower.Payload, Chamfer.Payload, Upper.Payload,
                                                LowerDisk.Payload, UpperDisk.Payload }, Tol, false);
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, "half-turn unequal bicone surfaces could not be sewn");
    if (!CapRadialSector(Result.Payload, LowerBase, UpperBase, Radial, Specification.SweepAngle,
                         std::max(Specification.LowerRadius, Specification.UpperRadius)) || !Result.Payload.Orient())
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "half-turn unequal bicone radial caps could not heal");
    const double LowerRetained = Specification.LowerHeight - Specification.SetBack;
    const double UpperRetained = Specification.UpperHeight - Specification.SetBack;
    const double ExpectedVolume =
        (ScalarCriteria::Pi * LowerRetained *
            (Specification.LowerRadius * Specification.LowerRadius + Specification.LowerRadius * LowerContactRadius +
             LowerContactRadius * LowerContactRadius) / 3.0 +
         ScalarCriteria::Pi * 2.0 * Specification.SetBack *
            (LowerContactRadius * LowerContactRadius + LowerContactRadius * UpperContactRadius +
             UpperContactRadius * UpperContactRadius) / 3.0 +
         ScalarCriteria::Pi * UpperRetained *
            (UpperContactRadius * UpperContactRadius + UpperContactRadius * Specification.UpperRadius +
             Specification.UpperRadius * Specification.UpperRadius) / 3.0) *
        Specification.SweepAngle / ScalarCriteria::TwoPi;
    const BodyReport Report = Result.Payload.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Result.Payload.Vertices.size() != 10 ||
        Result.Payload.Edges.size() != 15 || Result.Payload.Coedges.size() != 30 || Result.Payload.Loops.size() != 7 ||
        Result.Payload.Faces.size() != 7)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                                          "half-turn unequal bicone did not reach V10/E15/C30/L7/F7 topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence, "half-turn unequal bicone volume failed analytic acceptance");
    return Result;
}

Deliver<HalfTurnUnequalRadiusBiconeApexChamferSpecification>
BlendSolver::ClassifyHalfTurnUnequalRadiusBiconeApexChamferVertex(const BrepBody& Body, int Vertex, double SetBack) noexcept
{
    if (!std::isfinite(SetBack) || SetBack <= ScalarCriteria::MergeTolerance)
        return Deliver<HalfTurnUnequalRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::DegenerateInput, "half-turn unequal bicone set-back must be finite and positive");
    const BodyReport Report = Body.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Body.Vertices.size() != 7 ||
        Body.Edges.size() != 11 || Body.Coedges.size() != 22 || Body.Loops.size() != 6 || Body.Faces.size() != 6)
        return Deliver<HalfTurnUnequalRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::NonManifold, "source is not the capped half-turn unequal bicone topology");
    if (Vertex < 0 || Vertex >= static_cast<int>(Body.Vertices.size()))
        return Deliver<HalfTurnUnequalRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "selected half-turn unequal bicone vertex is out of range");
    int RevolutionFaces = 0, PlaneFaces = 0;
    for (const BrepFace& Face : Body.Faces)
    {
        if (Face.Loops.size() != 1) return Deliver<HalfTurnUnequalRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "half-turn unequal bicone face is not single-loop");
        if (Face.Surface.Classification == SurfaceClassification::Revolution) ++RevolutionFaces;
        else if (Face.Surface.Classification == SurfaceClassification::Plane) ++PlaneFaces;
        else return Deliver<HalfTurnUnequalRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "half-turn unequal bicone has unsupported face geometry");
    }
    if (RevolutionFaces != 4 || PlaneFaces != 2)
        return Deliver<HalfTurnUnequalRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "half-turn unequal bicone does not have four revolve faces and two radial caps");
    std::vector<int> RimEdges, LineEdges;
    for (size_t I = 0; I < Body.Edges.size(); ++I)
    {
        const BrepEdge& EdgeData = Body.Edges[I];
        if (EdgeData.Coedges.size() != 2) return Deliver<HalfTurnUnequalRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::NonManifold, "half-turn unequal bicone edge is not manifold");
        if (!EdgeData.Closed() && EdgeData.Curve.Classification == CurveClassification::Arc && EdgeData.Curve.Degree == 2 && EdgeData.Curve.Rational()) RimEdges.push_back(static_cast<int>(I));
        else if (!EdgeData.Closed() && EdgeData.Curve.Classification == CurveClassification::Line && EdgeData.Curve.Degree == 1) LineEdges.push_back(static_cast<int>(I));
        else return Deliver<HalfTurnUnequalRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "half-turn unequal bicone has unsupported edge geometry");
    }
    if (RimEdges.size() != 2 || LineEdges.size() != 9)
        return Deliver<HalfTurnUnequalRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "half-turn unequal bicone does not have two arc rims and nine lines");
    int Apex = -1;
    for (int Candidate = 0; Candidate < static_cast<int>(Body.Vertices.size()); ++Candidate)
    {
        int Degree = 0;
        for (int Edge : LineEdges) if (Body.Edges[Edge].VertexStart == Candidate || Body.Edges[Edge].VertexEnd == Candidate) ++Degree;
        if (Degree == 4) { if (Apex >= 0) return Deliver<HalfTurnUnequalRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "half-turn unequal bicone has multiple apex candidates"); Apex = Candidate; }
    }
    if (Apex < 0 || Vertex != Apex) return Deliver<HalfTurnUnequalRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "selected vertex is not the half-turn unequal bicone apex");
    struct Rim { Vec3 Centre, Normal; double Radius = 0.0; int Edge = -1; } G[2];
    for (int K = 0; K < 2; ++K)
    {
        G[K].Edge = RimEdges[K];
        if (!CircularFrame(Body.Edges[RimEdges[K]].Curve, G[K].Centre, G[K].Normal, G[K].Radius) || G[K].Radius <= Tol)
            return Deliver<HalfTurnUnequalRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "half-turn unequal bicone rim is not circular");
    }
    Vec3 Axis = (G[1].Centre - G[0].Centre).Normalised();
    if (Axis.Length() <= ScalarCriteria::GeometricTolerance) return Deliver<HalfTurnUnequalRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::DegenerateInput, "half-turn unequal bicone axis is degenerate");
    if ((std::fabs(Axis.X) > ScalarCriteria::GeometricTolerance && Axis.X < 0.0) ||
        (std::fabs(Axis.X) <= ScalarCriteria::GeometricTolerance && std::fabs(Axis.Y) > ScalarCriteria::GeometricTolerance && Axis.Y < 0.0) ||
        (std::fabs(Axis.X) <= ScalarCriteria::GeometricTolerance && std::fabs(Axis.Y) <= ScalarCriteria::GeometricTolerance && Axis.Z < 0.0)) Axis = -Axis;
    for (const Rim& R : G)
        if (std::fabs(std::fabs(R.Normal.Dot(Axis)) - 1.0) > ScalarCriteria::AngularTolerance ||
            (R.Centre - Body.Vertices[Apex].Point - Axis * (R.Centre - Body.Vertices[Apex].Point).Dot(Axis)).Length() > ScalarCriteria::GeometricTolerance * std::max(1.0, R.Centre.Distance(Body.Vertices[Apex].Point)))
            return Deliver<HalfTurnUnequalRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "half-turn unequal bicone rims are not coaxial");
    if ((G[0].Centre - G[1].Centre).Dot(Axis) > 0.0) std::swap(G[0], G[1]);
    const Vec3 ApexPoint = Body.Vertices[Apex].Point;
    const Vec3 StartVector = (Body.Vertices[Body.Edges[G[0].Edge].VertexStart].Point - G[0].Centre).Normalised();
    const Vec3 EndVector = (Body.Vertices[Body.Edges[G[0].Edge].VertexEnd].Point - G[0].Centre).Normalised();
    const double Sweep = std::atan2(Axis.Dot(StartVector.Cross(EndVector)), ScalarCriteria::Clamp(StartVector.Dot(EndVector), -1.0, 1.0));
    const Vec3 CanonicalRadial = Workplane::FromNormal(ApexPoint, Axis).AxisX.Normalised();
    if (std::fabs(Sweep - ScalarCriteria::Pi) > ScalarCriteria::SweepTolerance ||
        CanonicalRadial.Distance(StartVector) > ScalarCriteria::GeometricTolerance)
        return Deliver<HalfTurnUnequalRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "half-turn unequal bicone sweep is not exactly a canonical half turn");
    for (const BrepFace& Face : Body.Faces)
        if (Face.Surface.Classification == SurfaceClassification::Plane)
        {
            Vec3 Normal;
            if (!PlanarNormal(Body, static_cast<int>(&Face - Body.Faces.data()), Normal) || std::fabs(Normal.Dot(Axis)) > ScalarCriteria::AngularTolerance)
                return Deliver<HalfTurnUnequalRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "half-turn unequal bicone radial cap is not planar");
        }
    const double RadiusScale = std::max({ 1.0, G[0].Radius, G[1].Radius });
    if (std::fabs(G[0].Radius - G[1].Radius) <= ScalarCriteria::GeometricTolerance * RadiusScale)
        return Deliver<HalfTurnUnequalRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "partial route requires unequal rim radii");
    HalfTurnUnequalRadiusBiconeApexChamferSpecification Specification;
    Specification.Apex = ApexPoint; Specification.Axis = Axis; Specification.LowerRadius = G[0].Radius; Specification.UpperRadius = G[1].Radius;
    Specification.LowerHeight = ApexPoint.Distance(G[0].Centre); Specification.UpperHeight = ApexPoint.Distance(G[1].Centre);
    Specification.SetBack = SetBack; Specification.SweepAngle = ScalarCriteria::Pi;
    const Deliver<BrepBody> Feasible = ReconstructHalfTurnUnequalRadiusBiconeApexChamfer(Specification);
    if (!Feasible) return Deliver<HalfTurnUnequalRadiusBiconeApexChamferSpecification>::Reject(Feasible.Denial.Reason, Feasible.Denial.Detail);
    return Deliver<HalfTurnUnequalRadiusBiconeApexChamferSpecification>::Accept(std::move(Specification));
}


Deliver<BrepBody> BlendSolver::ReconstructReflexUnequalRadiusBiconeApexChamfer(
    const ReflexUnequalRadiusBiconeApexChamferSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Apex.X) || !std::isfinite(Specification.Apex.Y) ||
        !std::isfinite(Specification.Apex.Z) || !std::isfinite(Specification.LowerRadius) ||
        !std::isfinite(Specification.UpperRadius) || !std::isfinite(Specification.LowerHeight) ||
        !std::isfinite(Specification.UpperHeight) || !std::isfinite(Specification.SetBack) ||
        !std::isfinite(Specification.SweepAngle) || Specification.LowerRadius <= Tol ||
        Specification.UpperRadius <= Tol || Specification.LowerHeight <= Tol ||
        Specification.UpperHeight <= Tol || Specification.SetBack <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "reflex unequal bicone dimensions must be finite and positive");
    if (!std::isfinite(Specification.Axis.X) || !std::isfinite(Specification.Axis.Y) ||
        !std::isfinite(Specification.Axis.Z) || Specification.Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "reflex unequal bicone axis is degenerate");
    if (std::fabs(Specification.SweepAngle - (4.0 * ScalarCriteria::Pi / 3.0)) > ScalarCriteria::SweepTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "reflex unequal bicone route requires exactly the canonical reflex angle");
    const double RadiusScale = std::max({ 1.0, Specification.LowerRadius, Specification.UpperRadius });
    if (std::fabs(Specification.LowerRadius - Specification.UpperRadius) <=
        ScalarCriteria::GeometricTolerance * RadiusScale)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "reflex unequal bicone route requires distinct support radii");
    if (Specification.SetBack >= std::min(Specification.LowerHeight, Specification.UpperHeight) - Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "reflex unequal bicone set-back consumes a support");
    const Vec3 Apex = Specification.Apex;
    const Vec3 Axis = Specification.Axis.Normalised();
    const Vec3 Radial = Workplane::FromNormal(Apex, Axis).AxisX.Normalised();
    if (Radial.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "reflex unequal bicone radial frame is degenerate");
    const Vec3 LowerBase = Apex - Axis * Specification.LowerHeight;
    const Vec3 UpperBase = Apex + Axis * Specification.UpperHeight;
    const Vec3 LowerContact = Apex - Axis * Specification.SetBack;
    const Vec3 UpperContact = Apex + Axis * Specification.SetBack;
    const double LowerContactRadius = Specification.LowerRadius * Specification.SetBack / Specification.LowerHeight;
    const double UpperContactRadius = Specification.UpperRadius * Specification.SetBack / Specification.UpperHeight;
    if (LowerContactRadius <= Tol || UpperContactRadius <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "reflex unequal bicone contact ring is degenerate");
    auto RevolveLine = [&](Vec3 Start, Vec3 End) -> Deliver<NurbsSurface>
    {
        Deliver<NurbsCurve> Line = NurbsCurve::Line(Start, End);
        return Line ? NurbsSurface::Revolution(Line.Payload, Apex, Axis, Specification.SweepAngle)
                    : Deliver<NurbsSurface>::Reject(Line.Denial.Reason, Line.Denial.Detail);
    };
    Deliver<NurbsSurface> Lower = RevolveLine(LowerBase + Radial * Specification.LowerRadius,
                                               LowerContact + Radial * LowerContactRadius);
    Deliver<NurbsSurface> Chamfer = RevolveLine(LowerContact + Radial * LowerContactRadius,
                                                UpperContact + Radial * UpperContactRadius);
    Deliver<NurbsSurface> Upper = RevolveLine(UpperContact + Radial * UpperContactRadius,
                                               UpperBase + Radial * Specification.UpperRadius);
    Deliver<NurbsSurface> LowerDisk = RevolveLine(LowerBase, LowerBase + Radial * Specification.LowerRadius);
    Deliver<NurbsSurface> UpperDisk = RevolveLine(UpperBase, UpperBase + Radial * Specification.UpperRadius);
    if (!Lower || !Chamfer || !Upper || !LowerDisk || !UpperDisk)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "reflex unequal bicone surfaces are degenerate");
    Lower.Payload.Classification = SurfaceClassification::Cone;
    Lower.Payload.Origin = LowerBase; Lower.Payload.Axis = Axis;
    Lower.Payload.RadiusMajor = Specification.LowerRadius; Lower.Payload.RadiusMinor = LowerContactRadius;
    Chamfer.Payload.Classification = SurfaceClassification::Cone;
    Chamfer.Payload.Origin = LowerContact; Chamfer.Payload.Axis = Axis;
    Chamfer.Payload.RadiusMajor = LowerContactRadius; Chamfer.Payload.RadiusMinor = UpperContactRadius;
    Upper.Payload.Classification = SurfaceClassification::Cone;
    Upper.Payload.Origin = UpperContact; Upper.Payload.Axis = Axis;
    Upper.Payload.RadiusMajor = UpperContactRadius; Upper.Payload.RadiusMinor = Specification.UpperRadius;
    LowerDisk.Payload.Classification = SurfaceClassification::Plane;
    LowerDisk.Payload.Origin = LowerBase; LowerDisk.Payload.Axis = Axis;
    UpperDisk.Payload.Classification = SurfaceClassification::Plane;
    UpperDisk.Payload.Origin = UpperBase; UpperDisk.Payload.Axis = Axis;
    Deliver<BrepBody> Result = BrepBody::Sew({ Lower.Payload, Chamfer.Payload, Upper.Payload,
                                                LowerDisk.Payload, UpperDisk.Payload }, Tol, false);
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, "reflex unequal bicone surfaces could not be sewn");
    if (!CapRadialSector(Result.Payload, LowerBase, UpperBase, Radial, Specification.SweepAngle,
                         std::max(Specification.LowerRadius, Specification.UpperRadius)) || !Result.Payload.Orient())
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "reflex unequal bicone radial caps could not close");
    const double LowerRetained = Specification.LowerHeight - Specification.SetBack;
    const double UpperRetained = Specification.UpperHeight - Specification.SetBack;
    const double ExpectedVolume =
        (ScalarCriteria::Pi * LowerRetained *
            (Specification.LowerRadius * Specification.LowerRadius + Specification.LowerRadius * LowerContactRadius +
             LowerContactRadius * LowerContactRadius) / 3.0 +
         ScalarCriteria::Pi * 2.0 * Specification.SetBack *
            (LowerContactRadius * LowerContactRadius + LowerContactRadius * UpperContactRadius +
             UpperContactRadius * UpperContactRadius) / 3.0 +
         ScalarCriteria::Pi * UpperRetained *
            (UpperContactRadius * UpperContactRadius + UpperContactRadius * Specification.UpperRadius +
             Specification.UpperRadius * Specification.UpperRadius) / 3.0) *
        Specification.SweepAngle / ScalarCriteria::TwoPi;
    const BodyReport Report = Result.Payload.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Result.Payload.Vertices.size() != 10 ||
        Result.Payload.Edges.size() != 15 || Result.Payload.Coedges.size() != 30 || Result.Payload.Loops.size() != 7 ||
        Result.Payload.Faces.size() != 7)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                                          "reflex unequal bicone did not reach V10/E15/C30/L7/F7 topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence, "reflex unequal bicone volume failed analytic acceptance");
    return Result;
}

Deliver<ReflexUnequalRadiusBiconeApexChamferSpecification>
BlendSolver::ClassifyReflexUnequalRadiusBiconeApexChamferVertex(const BrepBody& Body, int Vertex, double SetBack) noexcept
{
    if (!std::isfinite(SetBack) || SetBack <= ScalarCriteria::MergeTolerance)
        return Deliver<ReflexUnequalRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::DegenerateInput, "reflex unequal bicone set-back must be finite and positive");
    const BodyReport Report = Body.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Body.Vertices.size() != 7 ||
        Body.Edges.size() != 11 || Body.Coedges.size() != 22 || Body.Loops.size() != 6 || Body.Faces.size() != 6)
        return Deliver<ReflexUnequalRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::NonManifold, "source is not the capped reflex unequal bicone topology");
    if (Vertex < 0 || Vertex >= static_cast<int>(Body.Vertices.size()))
        return Deliver<ReflexUnequalRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "selected reflex unequal bicone vertex is out of range");
    int RevolutionFaces = 0, PlaneFaces = 0;
    for (const BrepFace& Face : Body.Faces)
    {
        if (Face.Loops.size() != 1) return Deliver<ReflexUnequalRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "reflex unequal bicone face is not single-loop");
        if (Face.Surface.Classification == SurfaceClassification::Revolution) ++RevolutionFaces;
        else if (Face.Surface.Classification == SurfaceClassification::Plane) ++PlaneFaces;
        else return Deliver<ReflexUnequalRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "reflex unequal bicone has unsupported face geometry");
    }
    if (RevolutionFaces != 4 || PlaneFaces != 2)
        return Deliver<ReflexUnequalRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "reflex unequal bicone does not have four revolve faces and two radial caps");
    std::vector<int> RimEdges, LineEdges;
    for (size_t I = 0; I < Body.Edges.size(); ++I)
    {
        const BrepEdge& EdgeData = Body.Edges[I];
        if (EdgeData.Coedges.size() != 2) return Deliver<ReflexUnequalRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::NonManifold, "reflex unequal bicone edge is not manifold");
        if (!EdgeData.Closed() && EdgeData.Curve.Classification == CurveClassification::Arc && EdgeData.Curve.Degree == 2 && EdgeData.Curve.Rational()) RimEdges.push_back(static_cast<int>(I));
        else if (!EdgeData.Closed() && EdgeData.Curve.Classification == CurveClassification::Line && EdgeData.Curve.Degree == 1) LineEdges.push_back(static_cast<int>(I));
        else return Deliver<ReflexUnequalRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "reflex unequal bicone has unsupported edge geometry");
    }
    if (RimEdges.size() != 2 || LineEdges.size() != 9)
        return Deliver<ReflexUnequalRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "reflex unequal bicone does not have two arc rims and nine lines");
    int Apex = -1;
    for (int Candidate = 0; Candidate < static_cast<int>(Body.Vertices.size()); ++Candidate)
    {
        int Degree = 0;
        for (int Edge : LineEdges) if (Body.Edges[Edge].VertexStart == Candidate || Body.Edges[Edge].VertexEnd == Candidate) ++Degree;
        if (Degree == 4) { if (Apex >= 0) return Deliver<ReflexUnequalRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "reflex unequal bicone has multiple apex candidates"); Apex = Candidate; }
    }
    if (Apex < 0 || Vertex != Apex) return Deliver<ReflexUnequalRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "selected vertex is not the reflex unequal bicone apex");
    struct Rim { Vec3 Centre, Normal; double Radius = 0.0; int Edge = -1; } G[2];
    for (int K = 0; K < 2; ++K)
    {
        G[K].Edge = RimEdges[K];
        if (!CircularFrame(Body.Edges[RimEdges[K]].Curve, G[K].Centre, G[K].Normal, G[K].Radius) || G[K].Radius <= Tol)
            return Deliver<ReflexUnequalRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "reflex unequal bicone rim is not circular");
    }
    Vec3 Axis = (G[1].Centre - G[0].Centre).Normalised();
    if (Axis.Length() <= ScalarCriteria::GeometricTolerance) return Deliver<ReflexUnequalRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::DegenerateInput, "reflex unequal bicone axis is degenerate");
    if ((std::fabs(Axis.X) > ScalarCriteria::GeometricTolerance && Axis.X < 0.0) ||
        (std::fabs(Axis.X) <= ScalarCriteria::GeometricTolerance && std::fabs(Axis.Y) > ScalarCriteria::GeometricTolerance && Axis.Y < 0.0) ||
        (std::fabs(Axis.X) <= ScalarCriteria::GeometricTolerance && std::fabs(Axis.Y) <= ScalarCriteria::GeometricTolerance && Axis.Z < 0.0)) Axis = -Axis;
    for (const Rim& R : G)
        if (std::fabs(std::fabs(R.Normal.Dot(Axis)) - 1.0) > ScalarCriteria::AngularTolerance ||
            (R.Centre - Body.Vertices[Apex].Point - Axis * (R.Centre - Body.Vertices[Apex].Point).Dot(Axis)).Length() > ScalarCriteria::GeometricTolerance * std::max(1.0, R.Centre.Distance(Body.Vertices[Apex].Point)))
            return Deliver<ReflexUnequalRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "reflex unequal bicone rims are not coaxial");
    if ((G[0].Centre - G[1].Centre).Dot(Axis) > 0.0) std::swap(G[0], G[1]);
    const Vec3 ApexPoint = Body.Vertices[Apex].Point;
    const NurbsCurve& RimCurve = Body.Edges[G[0].Edge].Curve;
    const double T0 = RimCurve.DomainStart(), T1 = RimCurve.DomainEnd();
    const Vec3 StartVector = (RimCurve.Sample(T0) - G[0].Centre).Normalised();
    const Vec3 MiddleVector = (RimCurve.Sample(0.5 * (T0 + T1)) - G[0].Centre).Normalised();
    const Vec3 EndVector = (RimCurve.Sample(T1) - G[0].Centre).Normalised();
    const auto SignedTurn = [&](Vec3 A, Vec3 B) noexcept
    {
        return std::atan2(Axis.Dot(A.Cross(B)), ScalarCriteria::Clamp(A.Dot(B), -1.0, 1.0));
    };
    const double Sweep = SignedTurn(StartVector, MiddleVector) + SignedTurn(MiddleVector, EndVector);
    const Vec3 CanonicalRadial = Workplane::FromNormal(ApexPoint, Axis).AxisX.Normalised();
    if (std::fabs(Sweep - (4.0 * ScalarCriteria::Pi / 3.0)) > ScalarCriteria::SweepTolerance ||
        CanonicalRadial.Distance(StartVector) > ScalarCriteria::GeometricTolerance)
        return Deliver<ReflexUnequalRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "reflex unequal bicone sweep is not exactly the canonical four-thirds turn");
    for (const BrepFace& Face : Body.Faces)
        if (Face.Surface.Classification == SurfaceClassification::Plane)
        {
            Vec3 Normal;
            if (!PlanarNormal(Body, static_cast<int>(&Face - Body.Faces.data()), Normal) || std::fabs(Normal.Dot(Axis)) > ScalarCriteria::AngularTolerance)
                return Deliver<ReflexUnequalRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "reflex unequal bicone radial cap is not planar");
        }
    const double RadiusScale = std::max({ 1.0, G[0].Radius, G[1].Radius });
    if (std::fabs(G[0].Radius - G[1].Radius) <= ScalarCriteria::GeometricTolerance * RadiusScale)
        return Deliver<ReflexUnequalRadiusBiconeApexChamferSpecification>::Reject(RefusalReason::Unsupported, "reflex route requires unequal rim radii");
    ReflexUnequalRadiusBiconeApexChamferSpecification Specification;
    Specification.Apex = ApexPoint; Specification.Axis = Axis; Specification.LowerRadius = G[0].Radius; Specification.UpperRadius = G[1].Radius;
    Specification.LowerHeight = ApexPoint.Distance(G[0].Centre); Specification.UpperHeight = ApexPoint.Distance(G[1].Centre);
    Specification.SetBack = SetBack; Specification.SweepAngle = 4.0 * ScalarCriteria::Pi / 3.0;
    const Deliver<BrepBody> Feasible = ReconstructReflexUnequalRadiusBiconeApexChamfer(Specification);
    if (!Feasible) return Deliver<ReflexUnequalRadiusBiconeApexChamferSpecification>::Reject(Feasible.Denial.Reason, Feasible.Denial.Detail);
    return Deliver<ReflexUnequalRadiusBiconeApexChamferSpecification>::Accept(std::move(Specification));
}


Deliver<BrepBody> BlendSolver::ReconstructHalfTurnEqualRadiusBiconeApexChamfer(
    const HalfTurnEqualRadiusBiconeApexChamferSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Apex.X) || !std::isfinite(Specification.Apex.Y) ||
        !std::isfinite(Specification.Apex.Z) || !std::isfinite(Specification.Radius) ||
        !std::isfinite(Specification.LowerHeight) || !std::isfinite(Specification.UpperHeight) ||
        !std::isfinite(Specification.SetBack) || !std::isfinite(Specification.SweepAngle) ||
        Specification.Radius <= Tol || Specification.LowerHeight <= Tol ||
        Specification.UpperHeight <= Tol || Specification.SetBack <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "half-turn equal bicone dimensions must be finite and positive");
    if (!std::isfinite(Specification.Axis.X) || !std::isfinite(Specification.Axis.Y) ||
        !std::isfinite(Specification.Axis.Z) || Specification.Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "half-turn equal bicone axis is degenerate");
    if (std::fabs(Specification.SweepAngle - ScalarCriteria::Pi) > ScalarCriteria::SweepTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "half-turn equal bicone route requires exactly a half turn");
    if (Specification.SetBack >= std::min(Specification.LowerHeight, Specification.UpperHeight) - Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "half-turn equal bicone set-back consumes a support");
    const Vec3 Apex = Specification.Apex;
    const Vec3 Axis = Specification.Axis.Normalised();
    const Vec3 Radial = Workplane::FromNormal(Apex, Axis).AxisX.Normalised();
    if (Radial.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "half-turn equal bicone radial frame is degenerate");
    const Vec3 LowerBase = Apex - Axis * Specification.LowerHeight;
    const Vec3 UpperBase = Apex + Axis * Specification.UpperHeight;
    const Vec3 LowerContact = Apex - Axis * Specification.SetBack;
    const Vec3 UpperContact = Apex + Axis * Specification.SetBack;
    const double LowerContactRadius = Specification.Radius * Specification.SetBack / Specification.LowerHeight;
    const double UpperContactRadius = Specification.Radius * Specification.SetBack / Specification.UpperHeight;
    if (LowerContactRadius <= Tol || UpperContactRadius <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "half-turn equal bicone contact ring is degenerate");
    auto RevolveLine = [&](Vec3 Start, Vec3 End) -> Deliver<NurbsSurface>
    {
        Deliver<NurbsCurve> Line = NurbsCurve::Line(Start, End);
        return Line ? NurbsSurface::Revolution(Line.Payload, Apex, Axis, Specification.SweepAngle)
                    : Deliver<NurbsSurface>::Reject(Line.Denial.Reason, Line.Denial.Detail);
    };
    Deliver<NurbsSurface> Lower = RevolveLine(LowerBase + Radial * Specification.Radius,
                                               LowerContact + Radial * LowerContactRadius);
    Deliver<NurbsSurface> Chamfer = RevolveLine(LowerContact + Radial * LowerContactRadius,
                                                UpperContact + Radial * UpperContactRadius);
    Deliver<NurbsSurface> Upper = RevolveLine(UpperContact + Radial * UpperContactRadius,
                                               UpperBase + Radial * Specification.Radius);
    Deliver<NurbsSurface> LowerDisk = RevolveLine(LowerBase, LowerBase + Radial * Specification.Radius);
    Deliver<NurbsSurface> UpperDisk = RevolveLine(UpperBase, UpperBase + Radial * Specification.Radius);
    if (!Lower || !Chamfer || !Upper || !LowerDisk || !UpperDisk)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "half-turn equal bicone surfaces are degenerate");
    Lower.Payload.Classification = SurfaceClassification::Cone;
    Lower.Payload.Origin = LowerBase; Lower.Payload.Axis = Axis;
    Lower.Payload.RadiusMajor = Specification.Radius; Lower.Payload.RadiusMinor = LowerContactRadius;
    Chamfer.Payload.Classification = SurfaceClassification::Cone;
    Chamfer.Payload.Origin = LowerContact; Chamfer.Payload.Axis = Axis;
    Chamfer.Payload.RadiusMajor = LowerContactRadius; Chamfer.Payload.RadiusMinor = UpperContactRadius;
    Upper.Payload.Classification = SurfaceClassification::Cone;
    Upper.Payload.Origin = UpperContact; Upper.Payload.Axis = Axis;
    Upper.Payload.RadiusMajor = UpperContactRadius; Upper.Payload.RadiusMinor = Specification.Radius;
    LowerDisk.Payload.Classification = SurfaceClassification::Plane;
    LowerDisk.Payload.Origin = LowerBase; LowerDisk.Payload.Axis = Axis;
    UpperDisk.Payload.Classification = SurfaceClassification::Plane;
    UpperDisk.Payload.Origin = UpperBase; UpperDisk.Payload.Axis = Axis;
    Deliver<BrepBody> Result = BrepBody::Sew({ Lower.Payload, Chamfer.Payload, Upper.Payload,
                                                LowerDisk.Payload, UpperDisk.Payload }, Tol, false);
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, "half-turn equal bicone surfaces could not be sewn");
    if (!CapRadialSector(Result.Payload, LowerBase, UpperBase, Radial, Specification.SweepAngle,
                         Specification.Radius) || !Result.Payload.Orient())
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "half-turn equal bicone radial caps could not heal");
    const double LowerRetained = Specification.LowerHeight - Specification.SetBack;
    const double UpperRetained = Specification.UpperHeight - Specification.SetBack;
    const double ExpectedVolume =
        (ScalarCriteria::Pi * LowerRetained *
            (Specification.Radius * Specification.Radius + Specification.Radius * LowerContactRadius +
             LowerContactRadius * LowerContactRadius) / 3.0 +
         ScalarCriteria::Pi * 2.0 * Specification.SetBack *
            (LowerContactRadius * LowerContactRadius + LowerContactRadius * UpperContactRadius +
             UpperContactRadius * UpperContactRadius) / 3.0 +
         ScalarCriteria::Pi * UpperRetained *
            (UpperContactRadius * UpperContactRadius + UpperContactRadius * Specification.Radius +
             Specification.Radius * Specification.Radius) / 3.0) *
        Specification.SweepAngle / ScalarCriteria::TwoPi;
    const BodyReport Report = Result.Payload.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Result.Payload.Vertices.size() != 10 ||
        Result.Payload.Edges.size() != 15 || Result.Payload.Coedges.size() != 30 || Result.Payload.Loops.size() != 7 ||
        Result.Payload.Faces.size() != 7)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                                          "half-turn equal bicone did not reach V10/E15/C30/L7/F7 topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence, "half-turn equal bicone volume failed analytic acceptance");
    return Result;
}

Deliver<HalfTurnEqualRadiusBiconeApexChamferSpecification>
BlendSolver::ClassifyHalfTurnEqualRadiusBiconeApexChamferVertex(
    const BrepBody& Body, int Vertex, double SetBack) noexcept
{
    if (!std::isfinite(SetBack) || SetBack <= ScalarCriteria::MergeTolerance)
        return Deliver<HalfTurnEqualRadiusBiconeApexChamferSpecification>::Reject(
            RefusalReason::DegenerateInput, "half-turn equal bicone set-back must be finite and positive");
    const BodyReport Report = Body.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Body.Vertices.size() != 7 ||
        Body.Edges.size() != 11 || Body.Coedges.size() != 22 || Body.Loops.size() != 6 || Body.Faces.size() != 6)
        return Deliver<HalfTurnEqualRadiusBiconeApexChamferSpecification>::Reject(
            RefusalReason::NonManifold, "source is not the capped half-turn equal bicone topology");
    if (Vertex < 0 || Vertex >= static_cast<int>(Body.Vertices.size()))
        return Deliver<HalfTurnEqualRadiusBiconeApexChamferSpecification>::Reject(
            RefusalReason::Unsupported, "selected half-turn equal bicone vertex is out of range");
    int RevolutionFaces = 0, PlaneFaces = 0;
    for (const BrepFace& Face : Body.Faces)
    {
        if (Face.Loops.size() != 1)
            return Deliver<HalfTurnEqualRadiusBiconeApexChamferSpecification>::Reject(
                RefusalReason::Unsupported, "half-turn equal bicone face is not single-loop");
        if (Face.Surface.Classification == SurfaceClassification::Revolution) ++RevolutionFaces;
        else if (Face.Surface.Classification == SurfaceClassification::Plane) ++PlaneFaces;
        else return Deliver<HalfTurnEqualRadiusBiconeApexChamferSpecification>::Reject(
            RefusalReason::Unsupported, "half-turn equal bicone has unsupported face geometry");
    }
    if (RevolutionFaces != 4 || PlaneFaces != 2)
        return Deliver<HalfTurnEqualRadiusBiconeApexChamferSpecification>::Reject(
            RefusalReason::Unsupported, "half-turn equal bicone does not have four revolve faces and two radial caps");
    std::vector<int> RimEdges, LineEdges;
    for (size_t I = 0; I < Body.Edges.size(); ++I)
    {
        const BrepEdge& EdgeData = Body.Edges[I];
        if (EdgeData.Coedges.size() != 2)
            return Deliver<HalfTurnEqualRadiusBiconeApexChamferSpecification>::Reject(
                RefusalReason::NonManifold, "half-turn equal bicone edge is not manifold");
        if (!EdgeData.Closed() && EdgeData.Curve.Classification == CurveClassification::Arc &&
            EdgeData.Curve.Degree == 2 && EdgeData.Curve.Rational()) RimEdges.push_back(static_cast<int>(I));
        else if (!EdgeData.Closed() && EdgeData.Curve.Classification == CurveClassification::Line &&
                 EdgeData.Curve.Degree == 1) LineEdges.push_back(static_cast<int>(I));
        else return Deliver<HalfTurnEqualRadiusBiconeApexChamferSpecification>::Reject(
            RefusalReason::Unsupported, "half-turn equal bicone has unsupported edge geometry");
    }
    if (RimEdges.size() != 2 || LineEdges.size() != 9)
        return Deliver<HalfTurnEqualRadiusBiconeApexChamferSpecification>::Reject(
            RefusalReason::Unsupported, "half-turn equal bicone does not have two arc rims and nine lines");
    int Apex = -1;
    for (int Candidate = 0; Candidate < static_cast<int>(Body.Vertices.size()); ++Candidate)
    {
        int Degree = 0;
        for (int Edge : LineEdges)
            if (Body.Edges[Edge].VertexStart == Candidate || Body.Edges[Edge].VertexEnd == Candidate) ++Degree;
        if (Degree == 4)
        {
            if (Apex >= 0) return Deliver<HalfTurnEqualRadiusBiconeApexChamferSpecification>::Reject(
                RefusalReason::Unsupported, "half-turn equal bicone has multiple apex candidates");
            Apex = Candidate;
        }
    }
    if (Apex < 0 || Vertex != Apex)
        return Deliver<HalfTurnEqualRadiusBiconeApexChamferSpecification>::Reject(
            RefusalReason::Unsupported, "selected vertex is not the half-turn equal bicone apex");
    struct Rim { Vec3 Centre, Normal; double Radius = 0.0; int Edge = -1; } G[2];
    for (int K = 0; K < 2; ++K)
    {
        G[K].Edge = RimEdges[K];
        if (!CircularFrame(Body.Edges[G[K].Edge].Curve, G[K].Centre, G[K].Normal, G[K].Radius) || G[K].Radius <= Tol)
            return Deliver<HalfTurnEqualRadiusBiconeApexChamferSpecification>::Reject(
                RefusalReason::Unsupported, "half-turn equal bicone rim is not circular");
    }
    Vec3 Axis = (G[1].Centre - G[0].Centre).Normalised();
    if (Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<HalfTurnEqualRadiusBiconeApexChamferSpecification>::Reject(
            RefusalReason::DegenerateInput, "half-turn equal bicone axis is degenerate");
    if ((std::fabs(Axis.X) > ScalarCriteria::GeometricTolerance && Axis.X < 0.0) ||
        (std::fabs(Axis.X) <= ScalarCriteria::GeometricTolerance && std::fabs(Axis.Y) > ScalarCriteria::GeometricTolerance && Axis.Y < 0.0) ||
        (std::fabs(Axis.X) <= ScalarCriteria::GeometricTolerance && std::fabs(Axis.Y) <= ScalarCriteria::GeometricTolerance && Axis.Z < 0.0)) Axis = -Axis;
    const Vec3 ApexPoint = Body.Vertices[Apex].Point;
    for (const Rim& R : G)
        if (std::fabs(std::fabs(R.Normal.Dot(Axis)) - 1.0) > ScalarCriteria::AngularTolerance ||
            (R.Centre - ApexPoint - Axis * (R.Centre - ApexPoint).Dot(Axis)).Length() >
                ScalarCriteria::GeometricTolerance * std::max(1.0, R.Centre.Distance(ApexPoint)))
            return Deliver<HalfTurnEqualRadiusBiconeApexChamferSpecification>::Reject(
                RefusalReason::Unsupported, "half-turn equal bicone rims are not coaxial");
    if ((G[0].Centre - G[1].Centre).Dot(Axis) > 0.0) std::swap(G[0], G[1]);
    const Vec3 CanonicalRadial = Workplane::FromNormal(ApexPoint, Axis).AxisX.Normalised();
    const Vec3 StartVector = (Body.Vertices[Body.Edges[G[0].Edge].VertexStart].Point - G[0].Centre).Normalised();
    const double Sweep = std::atan2(Axis.Dot(StartVector.Cross(
        (Body.Vertices[Body.Edges[G[0].Edge].VertexEnd].Point - G[0].Centre).Normalised())),
        ScalarCriteria::Clamp(StartVector.Dot(
            (Body.Vertices[Body.Edges[G[0].Edge].VertexEnd].Point - G[0].Centre).Normalised()), -1.0, 1.0));
    if (std::fabs(Sweep - ScalarCriteria::Pi) > ScalarCriteria::SweepTolerance ||
        CanonicalRadial.Distance(StartVector) > ScalarCriteria::GeometricTolerance)
        return Deliver<HalfTurnEqualRadiusBiconeApexChamferSpecification>::Reject(
            RefusalReason::Unsupported, "half-turn equal bicone sweep is not exactly a canonical half turn");
    for (size_t I = 0; I < Body.Faces.size(); ++I)
        if (Body.Faces[I].Surface.Classification == SurfaceClassification::Plane)
        {
            Vec3 Normal;
            if (!PlanarNormal(Body, static_cast<int>(I), Normal) || std::fabs(Normal.Dot(Axis)) > ScalarCriteria::AngularTolerance)
                return Deliver<HalfTurnEqualRadiusBiconeApexChamferSpecification>::Reject(
                    RefusalReason::Unsupported, "half-turn equal bicone radial cap is not planar");
        }
    const double RadiusScale = std::max(1.0, G[0].Radius);
    if (std::fabs(G[0].Radius - G[1].Radius) > ScalarCriteria::GeometricTolerance * RadiusScale)
        return Deliver<HalfTurnEqualRadiusBiconeApexChamferSpecification>::Reject(
            RefusalReason::Unsupported, "half-turn equal route requires equal rim radii");
    HalfTurnEqualRadiusBiconeApexChamferSpecification Specification;
    Specification.Apex = ApexPoint; Specification.Axis = Axis; Specification.Radius = G[0].Radius;
    Specification.LowerHeight = ApexPoint.Distance(G[0].Centre); Specification.UpperHeight = ApexPoint.Distance(G[1].Centre);
    Specification.SetBack = SetBack; Specification.SweepAngle = ScalarCriteria::Pi;
    const Deliver<BrepBody> Feasible = ReconstructHalfTurnEqualRadiusBiconeApexChamfer(Specification);
    if (!Feasible) return Deliver<HalfTurnEqualRadiusBiconeApexChamferSpecification>::Reject(
        Feasible.Denial.Reason, Feasible.Denial.Detail);
    return Deliver<HalfTurnEqualRadiusBiconeApexChamferSpecification>::Accept(std::move(Specification));
}


Deliver<BrepBody> BlendSolver::ReconstructHalfTurnEqualRadiusBiconeApexFillet(
    const HalfTurnEqualRadiusBiconeApexFilletSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Apex.X) || !std::isfinite(Specification.Apex.Y) ||
        !std::isfinite(Specification.Apex.Z) || !std::isfinite(Specification.Radius) ||
        !std::isfinite(Specification.LowerHeight) || !std::isfinite(Specification.UpperHeight) ||
        !std::isfinite(Specification.FilletRadius) || Specification.Radius <= Tol ||
        Specification.LowerHeight <= Tol || Specification.UpperHeight <= Tol || Specification.FilletRadius <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "half-turn equal bicone fillet dimensions must be finite and positive");
    if (!std::isfinite(Specification.Axis.X) || !std::isfinite(Specification.Axis.Y) ||
        !std::isfinite(Specification.Axis.Z) || Specification.Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "half-turn equal bicone fillet axis is degenerate");
    if (!std::isfinite(Specification.SweepAngle) ||
        std::fabs(Specification.SweepAngle - ScalarCriteria::Pi) > ScalarCriteria::SweepTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "half-turn equal bicone fillet requires exactly a half turn");
    const Vec3 Axis = Specification.Axis.Normalised();
    const Vec3 Apex = Specification.Apex;
    const double LowerAngle = std::atan2(Specification.Radius, Specification.LowerHeight);
    const double UpperAngle = std::atan2(Specification.Radius, Specification.UpperHeight);
    const double SinSum = std::sin(LowerAngle + UpperAngle);
    if (SinSum <= ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "equal bicone fillet support angles are degenerate");
    const double SinLower = std::sin(LowerAngle), SinUpper = std::sin(UpperAngle);
    const double CosLower = std::cos(LowerAngle), CosUpper = std::cos(UpperAngle);
    const double Q = Specification.FilletRadius;
    const double Major = Q * (SinLower + SinUpper) / SinSum;
    const double CentreOffset = Q * (CosUpper - CosLower) / SinSum;
    if (Major <= Q + Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "half-turn equal bicone torus would cross the axis");
    const double LowerTangent = Major * SinLower - CentreOffset * CosLower;
    const double UpperTangent = Major * SinUpper + CentreOffset * CosUpper;
    if (LowerTangent <= Tol || UpperTangent <= Tol ||
        LowerTangent >= std::hypot(Specification.LowerHeight, Specification.Radius) - Tol ||
        UpperTangent >= std::hypot(Specification.UpperHeight, Specification.Radius) - Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "half-turn equal bicone torus consumes a support");
    const Vec3 Radial = Workplane::FromNormal(Apex, Axis).AxisX.Normalised();
    if (Radial.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "half-turn equal bicone fillet radial frame is degenerate");
    const Vec3 LowerBase = Apex - Axis * Specification.LowerHeight;
    const Vec3 UpperBase = Apex + Axis * Specification.UpperHeight;
    const double LowerContactRadius = LowerTangent * SinLower;
    const double UpperContactRadius = UpperTangent * SinUpper;
    const Vec3 LowerContact = Apex - Axis * (LowerTangent * CosLower) + Radial * LowerContactRadius;
    const Vec3 UpperContact = Apex + Axis * (UpperTangent * CosUpper) + Radial * UpperContactRadius;
    const Vec3 InnerPoint = Apex + Axis * CentreOffset + Radial * (Major - Q);
    auto Line = [&](Vec3 Start, Vec3 End) -> Deliver<NurbsCurve> { return NurbsCurve::Line(Start, End); };
    Deliver<NurbsCurve> LowerProfile = Line(LowerBase + Radial * Specification.Radius, LowerContact);
    Deliver<NurbsCurve> UpperProfile = Line(UpperContact, UpperBase + Radial * Specification.Radius);
    Deliver<NurbsCurve> TorusProfile = NurbsCurve::ArcThreePoints(LowerContact, InnerPoint, UpperContact);
    Deliver<NurbsCurve> LowerDiskProfile = Line(LowerBase, LowerBase + Radial * Specification.Radius);
    Deliver<NurbsCurve> UpperDiskProfile = Line(UpperBase, UpperBase + Radial * Specification.Radius);
    if (!LowerProfile || !UpperProfile || !TorusProfile || !LowerDiskProfile || !UpperDiskProfile)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "half-turn equal bicone fillet profiles are degenerate");
    auto Revolve = [&](const Deliver<NurbsCurve>& Profile) -> Deliver<NurbsSurface>
    {
        return NurbsSurface::Revolution(Profile.Payload, Apex, Axis, Specification.SweepAngle);
    };
    Deliver<NurbsSurface> Lower = Revolve(LowerProfile);
    Deliver<NurbsSurface> Upper = Revolve(UpperProfile);
    Deliver<NurbsSurface> Torus = Revolve(TorusProfile);
    Deliver<NurbsSurface> LowerDisk = Revolve(LowerDiskProfile);
    Deliver<NurbsSurface> UpperDisk = Revolve(UpperDiskProfile);
    if (!Lower || !Upper || !Torus || !LowerDisk || !UpperDisk)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "half-turn equal bicone fillet surfaces are degenerate");
    Lower.Payload.Classification = SurfaceClassification::Cone;
    Lower.Payload.Origin = LowerBase; Lower.Payload.Axis = Axis;
    Lower.Payload.RadiusMajor = Specification.Radius; Lower.Payload.RadiusMinor = LowerContactRadius;
    Upper.Payload.Classification = SurfaceClassification::Cone;
    Upper.Payload.Origin = UpperContact; Upper.Payload.Axis = Axis;
    Upper.Payload.RadiusMajor = UpperContactRadius; Upper.Payload.RadiusMinor = Specification.Radius;
    Torus.Payload.Classification = SurfaceClassification::Torus;
    Torus.Payload.Origin = Apex + Axis * CentreOffset; Torus.Payload.Axis = Axis;
    Torus.Payload.RadiusMajor = Major; Torus.Payload.RadiusMinor = Q;
    LowerDisk.Payload.Classification = SurfaceClassification::Plane;
    LowerDisk.Payload.Origin = LowerBase; LowerDisk.Payload.Axis = Axis;
    UpperDisk.Payload.Classification = SurfaceClassification::Plane;
    UpperDisk.Payload.Origin = UpperBase; UpperDisk.Payload.Axis = Axis;
    Deliver<BrepBody> Result = BrepBody::Sew({ Lower.Payload, Torus.Payload, Upper.Payload,
                                                LowerDisk.Payload, UpperDisk.Payload }, Tol, false);
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, "half-turn equal bicone fillet surfaces could not be sewn");
    if (!CapRadialSector(Result.Payload, LowerBase, UpperBase, Radial, Specification.SweepAngle,
                         Specification.Radius) || !Result.Payload.Orient())
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "half-turn equal bicone fillet radial caps could not heal");

    const double LowerDepth = LowerTangent * CosLower, UpperDepth = UpperTangent * CosUpper;
    const double ThetaLower = std::atan2(-LowerDepth - CentreOffset, LowerContactRadius - Major);
    double ThetaUpper = std::atan2(UpperDepth - CentreOffset, UpperContactRadius - Major);
    while (ThetaUpper >= ThetaLower) ThetaUpper -= ScalarCriteria::TwoPi;
    const auto Primitive = [&](double Theta) noexcept
    {
        const double Sine = std::sin(Theta);
        return Major * Major * Sine + Major * Q * (Theta + std::sin(2.0 * Theta) / 2.0) +
               Q * Q * (Sine - Sine * Sine * Sine / 3.0);
    };
    const double MeridionalIntegral = Q * (Primitive(ThetaUpper) - Primitive(ThetaLower));
    const double ExpectedVolume =
        ScalarCriteria::Pi * (Specification.LowerHeight - LowerDepth) *
            (Specification.Radius * Specification.Radius + Specification.Radius * LowerContactRadius +
             LowerContactRadius * LowerContactRadius) / 3.0 +
        ScalarCriteria::Pi * MeridionalIntegral +
        ScalarCriteria::Pi * (Specification.UpperHeight - UpperDepth) *
            (UpperContactRadius * UpperContactRadius + UpperContactRadius * Specification.Radius +
             Specification.Radius * Specification.Radius) / 3.0;
    const BodyReport Report = Result.Payload.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Result.Payload.Vertices.size() != 10 ||
        Result.Payload.Edges.size() != 15 || Result.Payload.Coedges.size() != 30 || Result.Payload.Loops.size() != 7 ||
        Result.Payload.Faces.size() != 7)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "half-turn equal bicone fillet did not reach V10/E15/C30/L7/F7 topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume * Specification.SweepAngle / ScalarCriteria::TwoPi))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence, "half-turn equal bicone fillet volume failed analytic acceptance");
    return Result;
}


Deliver<HalfTurnEqualRadiusBiconeApexFilletSpecification>
BlendSolver::ClassifyHalfTurnEqualRadiusBiconeApexFilletVertex(const BrepBody& Body, int Vertex, double FilletRadius) noexcept
{
    if (!std::isfinite(FilletRadius) || FilletRadius <= ScalarCriteria::MergeTolerance)
        return Deliver<HalfTurnEqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::DegenerateInput,
                                                                                  "half-turn equal bicone fillet radius must be finite and positive");
    const BodyReport Report = Body.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Body.Vertices.size() != 7 ||
        Body.Edges.size() != 11 || Body.Coedges.size() != 22 || Body.Loops.size() != 6 || Body.Faces.size() != 6)
        return Deliver<HalfTurnEqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::NonManifold,
                                                                                  "source is not the capped half-turn equal bicone topology");
    if (Vertex < 0 || Vertex >= static_cast<int>(Body.Vertices.size()))
        return Deliver<HalfTurnEqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                                  "selected half-turn equal bicone vertex is out of range");
    int RevolutionFaces = 0, PlaneFaces = 0;
    for (const BrepFace& Face : Body.Faces)
    {
        if (Face.Loops.size() != 1)
            return Deliver<HalfTurnEqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                                      "half-turn equal bicone face is not single-loop");
        if (Face.Surface.Classification == SurfaceClassification::Revolution) ++RevolutionFaces;
        else if (Face.Surface.Classification == SurfaceClassification::Plane) ++PlaneFaces;
        else return Deliver<HalfTurnEqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                                      "half-turn equal bicone has unsupported face geometry");
    }
    if (RevolutionFaces != 4 || PlaneFaces != 2)
        return Deliver<HalfTurnEqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                                  "half-turn equal bicone does not have four revolve faces and two radial caps");
    std::vector<int> RimEdges, LineEdges;
    for (size_t I = 0; I < Body.Edges.size(); ++I)
    {
        const BrepEdge& EdgeData = Body.Edges[I];
        if (EdgeData.Coedges.size() != 2)
            return Deliver<HalfTurnEqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::NonManifold,
                                                                                      "half-turn equal bicone edge is not manifold");
        if (!EdgeData.Closed() && EdgeData.Curve.Classification == CurveClassification::Arc &&
            EdgeData.Curve.Degree == 2 && EdgeData.Curve.Rational()) RimEdges.push_back(static_cast<int>(I));
        else if (!EdgeData.Closed() && EdgeData.Curve.Classification == CurveClassification::Line &&
                 EdgeData.Curve.Degree == 1) LineEdges.push_back(static_cast<int>(I));
        else return Deliver<HalfTurnEqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                                       "half-turn equal bicone has unsupported edge geometry");
    }
    if (RimEdges.size() != 2 || LineEdges.size() != 9)
        return Deliver<HalfTurnEqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                                  "half-turn equal bicone does not have two arc rims and nine lines");
    int Apex = -1;
    for (int Candidate = 0; Candidate < static_cast<int>(Body.Vertices.size()); ++Candidate)
    {
        int Degree = 0;
        for (int Edge : LineEdges)
            if (Body.Edges[Edge].VertexStart == Candidate || Body.Edges[Edge].VertexEnd == Candidate) ++Degree;
        if (Degree == 4)
        {
            if (Apex >= 0) return Deliver<HalfTurnEqualRadiusBiconeApexFilletSpecification>::Reject(
                RefusalReason::Unsupported, "half-turn equal bicone has multiple apex candidates");
            Apex = Candidate;
        }
    }
    if (Apex < 0 || Vertex != Apex)
        return Deliver<HalfTurnEqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                                  "selected vertex is not the half-turn equal bicone apex");
    struct Rim { Vec3 Centre, Normal; double Radius = 0.0; int Edge = -1; } G[2];
    for (int K = 0; K < 2; ++K)
    {
        G[K].Edge = RimEdges[K];
        if (!CircularFrame(Body.Edges[G[K].Edge].Curve, G[K].Centre, G[K].Normal, G[K].Radius) || G[K].Radius <= Tol)
            return Deliver<HalfTurnEqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                                      "half-turn equal bicone rim is not circular");
    }
    Vec3 Axis = (G[1].Centre - G[0].Centre).Normalised();
    if (Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<HalfTurnEqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::DegenerateInput,
                                                                                  "half-turn equal bicone axis is degenerate");
    if ((std::fabs(Axis.X) > ScalarCriteria::GeometricTolerance && Axis.X < 0.0) ||
        (std::fabs(Axis.X) <= ScalarCriteria::GeometricTolerance && std::fabs(Axis.Y) > ScalarCriteria::GeometricTolerance && Axis.Y < 0.0) ||
        (std::fabs(Axis.X) <= ScalarCriteria::GeometricTolerance && std::fabs(Axis.Y) <= ScalarCriteria::GeometricTolerance && Axis.Z < 0.0)) Axis = -Axis;
    const Vec3 ApexPoint = Body.Vertices[Apex].Point;
    for (const Rim& R : G)
        if (std::fabs(std::fabs(R.Normal.Dot(Axis)) - 1.0) > ScalarCriteria::AngularTolerance ||
            (R.Centre - ApexPoint - Axis * (R.Centre - ApexPoint).Dot(Axis)).Length() >
                ScalarCriteria::GeometricTolerance * std::max(1.0, R.Centre.Distance(ApexPoint)))
            return Deliver<HalfTurnEqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                                       "half-turn equal bicone rims are not coaxial");
    if ((G[0].Centre - G[1].Centre).Dot(Axis) > 0.0) std::swap(G[0], G[1]);
    const Vec3 CanonicalRadial = Workplane::FromNormal(ApexPoint, Axis).AxisX.Normalised();
    const Vec3 StartVector = (Body.Vertices[Body.Edges[G[0].Edge].VertexStart].Point - G[0].Centre).Normalised();
    const Vec3 EndVector = (Body.Vertices[Body.Edges[G[0].Edge].VertexEnd].Point - G[0].Centre).Normalised();
    const double Sweep = std::atan2(Axis.Dot(StartVector.Cross(EndVector)), ScalarCriteria::Clamp(StartVector.Dot(EndVector), -1.0, 1.0));
    if (std::fabs(Sweep - ScalarCriteria::Pi) > ScalarCriteria::SweepTolerance ||
        CanonicalRadial.Distance(StartVector) > ScalarCriteria::GeometricTolerance)
        return Deliver<HalfTurnEqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                                  "half-turn equal bicone sweep is not exactly a canonical half turn");
    for (size_t I = 0; I < Body.Faces.size(); ++I)
        if (Body.Faces[I].Surface.Classification == SurfaceClassification::Plane)
        {
            Vec3 Normal;
            if (!PlanarNormal(Body, static_cast<int>(I), Normal) || std::fabs(Normal.Dot(Axis)) > ScalarCriteria::AngularTolerance)
                return Deliver<HalfTurnEqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                                          "half-turn equal bicone radial cap is not planar");
        }
    const double RadiusScale = std::max(1.0, G[0].Radius);
    if (std::fabs(G[0].Radius - G[1].Radius) > ScalarCriteria::GeometricTolerance * RadiusScale)
        return Deliver<HalfTurnEqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                                  "half-turn equal route requires equal rim radii");
    HalfTurnEqualRadiusBiconeApexFilletSpecification Specification;
    Specification.Apex = ApexPoint; Specification.Axis = Axis; Specification.Radius = G[0].Radius;
    Specification.LowerHeight = ApexPoint.Distance(G[0].Centre); Specification.UpperHeight = ApexPoint.Distance(G[1].Centre);
    Specification.FilletRadius = FilletRadius; Specification.SweepAngle = ScalarCriteria::Pi;
    const Deliver<BrepBody> Feasible = ReconstructHalfTurnEqualRadiusBiconeApexFillet(Specification);
    if (!Feasible) return Deliver<HalfTurnEqualRadiusBiconeApexFilletSpecification>::Reject(Feasible.Denial.Reason, Feasible.Denial.Detail);
    return Deliver<HalfTurnEqualRadiusBiconeApexFilletSpecification>::Accept(std::move(Specification));
}


Deliver<BrepBody> BlendSolver::ReconstructPartialEqualRadiusBiconeApexFillet(
    const PartialEqualRadiusBiconeApexFilletSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Apex.X) || !std::isfinite(Specification.Apex.Y) ||
        !std::isfinite(Specification.Apex.Z) || !std::isfinite(Specification.Radius) ||
        !std::isfinite(Specification.LowerHeight) || !std::isfinite(Specification.UpperHeight) ||
        !std::isfinite(Specification.FilletRadius) || Specification.Radius <= Tol ||
        Specification.LowerHeight <= Tol || Specification.UpperHeight <= Tol || Specification.FilletRadius <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "partial equal bicone fillet dimensions must be finite and positive");
    if (!std::isfinite(Specification.Axis.X) || !std::isfinite(Specification.Axis.Y) ||
        !std::isfinite(Specification.Axis.Z) || Specification.Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial equal bicone fillet axis is degenerate");
    if (!std::isfinite(Specification.SweepAngle) || Specification.SweepAngle <= ScalarCriteria::AngularTolerance ||
        Specification.SweepAngle >= ScalarCriteria::Pi - ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "partial equal bicone fillet requires a strict non-reflex sector");
    const Vec3 Axis = Specification.Axis.Normalised();
    const Vec3 Apex = Specification.Apex;
    const double LowerAngle = std::atan2(Specification.Radius, Specification.LowerHeight);
    const double UpperAngle = std::atan2(Specification.Radius, Specification.UpperHeight);
    const double SinSum = std::sin(LowerAngle + UpperAngle);
    if (SinSum <= ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "equal bicone fillet support angles are degenerate");
    const double SinLower = std::sin(LowerAngle), SinUpper = std::sin(UpperAngle);
    const double CosLower = std::cos(LowerAngle), CosUpper = std::cos(UpperAngle);
    const double Q = Specification.FilletRadius;
    const double Major = Q * (SinLower + SinUpper) / SinSum;
    const double CentreOffset = Q * (CosUpper - CosLower) / SinSum;
    if (Major <= Q + Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "partial equal bicone torus would cross the axis");
    const double LowerTangent = Major * SinLower - CentreOffset * CosLower;
    const double UpperTangent = Major * SinUpper + CentreOffset * CosUpper;
    if (LowerTangent <= Tol || UpperTangent <= Tol ||
        LowerTangent >= std::hypot(Specification.LowerHeight, Specification.Radius) - Tol ||
        UpperTangent >= std::hypot(Specification.UpperHeight, Specification.Radius) - Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "partial equal bicone torus consumes a support");
    const Vec3 Radial = Workplane::FromNormal(Apex, Axis).AxisX.Normalised();
    if (Radial.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial equal bicone fillet radial frame is degenerate");
    const Vec3 LowerBase = Apex - Axis * Specification.LowerHeight;
    const Vec3 UpperBase = Apex + Axis * Specification.UpperHeight;
    const double LowerContactRadius = LowerTangent * SinLower;
    const double UpperContactRadius = UpperTangent * SinUpper;
    const Vec3 LowerContact = Apex - Axis * (LowerTangent * CosLower) + Radial * LowerContactRadius;
    const Vec3 UpperContact = Apex + Axis * (UpperTangent * CosUpper) + Radial * UpperContactRadius;
    const Vec3 InnerPoint = Apex + Axis * CentreOffset + Radial * (Major - Q);
    auto Line = [&](Vec3 Start, Vec3 End) -> Deliver<NurbsCurve> { return NurbsCurve::Line(Start, End); };
    Deliver<NurbsCurve> LowerProfile = Line(LowerBase + Radial * Specification.Radius, LowerContact);
    Deliver<NurbsCurve> UpperProfile = Line(UpperContact, UpperBase + Radial * Specification.Radius);
    Deliver<NurbsCurve> TorusProfile = NurbsCurve::ArcThreePoints(LowerContact, InnerPoint, UpperContact);
    Deliver<NurbsCurve> LowerDiskProfile = Line(LowerBase, LowerBase + Radial * Specification.Radius);
    Deliver<NurbsCurve> UpperDiskProfile = Line(UpperBase, UpperBase + Radial * Specification.Radius);
    if (!LowerProfile || !UpperProfile || !TorusProfile || !LowerDiskProfile || !UpperDiskProfile)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial equal bicone fillet profiles are degenerate");
    auto Revolve = [&](const Deliver<NurbsCurve>& Profile) -> Deliver<NurbsSurface>
    {
        return NurbsSurface::Revolution(Profile.Payload, Apex, Axis, Specification.SweepAngle);
    };
    Deliver<NurbsSurface> Lower = Revolve(LowerProfile);
    Deliver<NurbsSurface> Upper = Revolve(UpperProfile);
    Deliver<NurbsSurface> Torus = Revolve(TorusProfile);
    Deliver<NurbsSurface> LowerDisk = Revolve(LowerDiskProfile);
    Deliver<NurbsSurface> UpperDisk = Revolve(UpperDiskProfile);
    if (!Lower || !Upper || !Torus || !LowerDisk || !UpperDisk)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial equal bicone fillet surfaces are degenerate");
    Lower.Payload.Classification = SurfaceClassification::Cone;
    Lower.Payload.Origin = LowerBase; Lower.Payload.Axis = Axis;
    Lower.Payload.RadiusMajor = Specification.Radius; Lower.Payload.RadiusMinor = LowerContactRadius;
    Upper.Payload.Classification = SurfaceClassification::Cone;
    Upper.Payload.Origin = UpperContact; Upper.Payload.Axis = Axis;
    Upper.Payload.RadiusMajor = UpperContactRadius; Upper.Payload.RadiusMinor = Specification.Radius;
    Torus.Payload.Classification = SurfaceClassification::Torus;
    Torus.Payload.Origin = Apex + Axis * CentreOffset; Torus.Payload.Axis = Axis;
    Torus.Payload.RadiusMajor = Major; Torus.Payload.RadiusMinor = Q;
    LowerDisk.Payload.Classification = SurfaceClassification::Plane;
    LowerDisk.Payload.Origin = LowerBase; LowerDisk.Payload.Axis = Axis;
    UpperDisk.Payload.Classification = SurfaceClassification::Plane;
    UpperDisk.Payload.Origin = UpperBase; UpperDisk.Payload.Axis = Axis;
    Deliver<BrepBody> Result = BrepBody::Sew({ Lower.Payload, Torus.Payload, Upper.Payload,
                                                LowerDisk.Payload, UpperDisk.Payload }, Tol, true);
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, "partial equal bicone fillet surfaces could not be sewn");
    if (!CapRadialSector(Result.Payload, LowerBase, UpperBase, Radial, Specification.SweepAngle,
                         Specification.Radius) || !Result.Payload.Orient())
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "partial equal bicone fillet radial caps could not heal");

    const double LowerDepth = LowerTangent * CosLower, UpperDepth = UpperTangent * CosUpper;
    const double ThetaLower = std::atan2(-LowerDepth - CentreOffset, LowerContactRadius - Major);
    double ThetaUpper = std::atan2(UpperDepth - CentreOffset, UpperContactRadius - Major);
    while (ThetaUpper >= ThetaLower) ThetaUpper -= ScalarCriteria::TwoPi;
    const auto Primitive = [&](double Theta) noexcept
    {
        const double Sine = std::sin(Theta);
        return Major * Major * Sine + Major * Q * (Theta + std::sin(2.0 * Theta) / 2.0) +
               Q * Q * (Sine - Sine * Sine * Sine / 3.0);
    };
    const double MeridionalIntegral = Q * (Primitive(ThetaUpper) - Primitive(ThetaLower));
    const double ExpectedVolume =
        ScalarCriteria::Pi * (Specification.LowerHeight - LowerDepth) *
            (Specification.Radius * Specification.Radius + Specification.Radius * LowerContactRadius +
             LowerContactRadius * LowerContactRadius) / 3.0 +
        ScalarCriteria::Pi * MeridionalIntegral +
        ScalarCriteria::Pi * (Specification.UpperHeight - UpperDepth) *
            (UpperContactRadius * UpperContactRadius + UpperContactRadius * Specification.Radius +
             Specification.Radius * Specification.Radius) / 3.0;
    const BodyReport Report = Result.Payload.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Result.Payload.Vertices.size() != 10 ||
        Result.Payload.Edges.size() != 15 || Result.Payload.Coedges.size() != 30 || Result.Payload.Loops.size() != 7 ||
        Result.Payload.Faces.size() != 7)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "partial equal bicone fillet did not reach V10/E15/C30/L7/F7 topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume * Specification.SweepAngle / ScalarCriteria::TwoPi))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence, "partial equal bicone fillet volume failed analytic acceptance");
    return Result;
}


Deliver<PartialEqualRadiusBiconeApexFilletSpecification>
BlendSolver::ClassifyPartialEqualRadiusBiconeApexFilletVertex(const BrepBody& Body, int Vertex, double FilletRadius) noexcept
{
    if (!std::isfinite(FilletRadius) || FilletRadius <= ScalarCriteria::MergeTolerance)
        return Deliver<PartialEqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::DegenerateInput,
                                                                                  "partial equal bicone fillet radius must be finite and positive");
    const BodyReport Report = Body.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Body.Vertices.size() != 7 ||
        Body.Edges.size() != 11 || Body.Coedges.size() != 22 || Body.Loops.size() != 6 || Body.Faces.size() != 6)
        return Deliver<PartialEqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::NonManifold,
                                                                                  "source is not the capped partial equal bicone topology");
    if (Vertex < 0 || Vertex >= static_cast<int>(Body.Vertices.size()))
        return Deliver<PartialEqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                                  "selected partial equal bicone vertex is out of range");
    int RevolutionFaces = 0, PlaneFaces = 0;
    for (const BrepFace& Face : Body.Faces)
    {
        if (Face.Loops.size() != 1)
            return Deliver<PartialEqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                                      "partial equal bicone face is not single-loop");
        if (Face.Surface.Classification == SurfaceClassification::Revolution) ++RevolutionFaces;
        else if (Face.Surface.Classification == SurfaceClassification::Plane) ++PlaneFaces;
        else return Deliver<PartialEqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                                      "partial equal bicone has unsupported face geometry");
    }
    if (RevolutionFaces != 4 || PlaneFaces != 2)
        return Deliver<PartialEqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                                  "partial equal bicone does not have four revolve faces and two radial caps");
    std::vector<int> RimEdges, LineEdges;
    for (size_t I = 0; I < Body.Edges.size(); ++I)
    {
        const BrepEdge& EdgeData = Body.Edges[I];
        if (EdgeData.Coedges.size() != 2)
            return Deliver<PartialEqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::NonManifold,
                                                                                      "partial equal bicone edge is not manifold");
        if (!EdgeData.Closed() && EdgeData.Curve.Classification == CurveClassification::Arc &&
            EdgeData.Curve.Degree == 2 && EdgeData.Curve.Rational()) RimEdges.push_back(static_cast<int>(I));
        else if (!EdgeData.Closed() && EdgeData.Curve.Classification == CurveClassification::Line &&
                 EdgeData.Curve.Degree == 1) LineEdges.push_back(static_cast<int>(I));
        else return Deliver<PartialEqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                                       "partial equal bicone has unsupported edge geometry");
    }
    if (RimEdges.size() != 2 || LineEdges.size() != 9)
        return Deliver<PartialEqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                                  "partial equal bicone does not have two arc rims and nine lines");
    int Apex = -1;
    for (int Candidate = 0; Candidate < static_cast<int>(Body.Vertices.size()); ++Candidate)
    {
        int Degree = 0;
        for (int Edge : LineEdges)
            if (Body.Edges[Edge].VertexStart == Candidate || Body.Edges[Edge].VertexEnd == Candidate) ++Degree;
        if (Degree == 4)
        {
            if (Apex >= 0) return Deliver<PartialEqualRadiusBiconeApexFilletSpecification>::Reject(
                RefusalReason::Unsupported, "partial equal bicone has multiple apex candidates");
            Apex = Candidate;
        }
    }
    if (Apex < 0 || Vertex != Apex)
        return Deliver<PartialEqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                                  "selected vertex is not the partial equal bicone apex");
    struct Rim { Vec3 Centre, Normal; double Radius = 0.0; int Edge = -1; } G[2];
    for (int K = 0; K < 2; ++K)
    {
        G[K].Edge = RimEdges[K];
        if (!CircularFrame(Body.Edges[G[K].Edge].Curve, G[K].Centre, G[K].Normal, G[K].Radius) || G[K].Radius <= Tol)
            return Deliver<PartialEqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                                      "partial equal bicone rim is not circular");
    }
    Vec3 Axis = (G[1].Centre - G[0].Centre).Normalised();
    if (Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<PartialEqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::DegenerateInput,
                                                                                  "partial equal bicone axis is degenerate");
    if ((std::fabs(Axis.X) > ScalarCriteria::GeometricTolerance && Axis.X < 0.0) ||
        (std::fabs(Axis.X) <= ScalarCriteria::GeometricTolerance && std::fabs(Axis.Y) > ScalarCriteria::GeometricTolerance && Axis.Y < 0.0) ||
        (std::fabs(Axis.X) <= ScalarCriteria::GeometricTolerance && std::fabs(Axis.Y) <= ScalarCriteria::GeometricTolerance && Axis.Z < 0.0)) Axis = -Axis;
    const Vec3 ApexPoint = Body.Vertices[Apex].Point;
    for (const Rim& R : G)
        if (std::fabs(std::fabs(R.Normal.Dot(Axis)) - 1.0) > ScalarCriteria::AngularTolerance ||
            (R.Centre - ApexPoint - Axis * (R.Centre - ApexPoint).Dot(Axis)).Length() >
                ScalarCriteria::GeometricTolerance * std::max(1.0, R.Centre.Distance(ApexPoint)))
            return Deliver<PartialEqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                                       "partial equal bicone rims are not coaxial");
    if ((G[0].Centre - G[1].Centre).Dot(Axis) > 0.0) std::swap(G[0], G[1]);
    const Vec3 CanonicalRadial = Workplane::FromNormal(ApexPoint, Axis).AxisX.Normalised();
    const Vec3 StartVector = (Body.Vertices[Body.Edges[G[0].Edge].VertexStart].Point - G[0].Centre).Normalised();
    const Vec3 EndVector = (Body.Vertices[Body.Edges[G[0].Edge].VertexEnd].Point - G[0].Centre).Normalised();
    const double Sweep = std::atan2(Axis.Dot(StartVector.Cross(EndVector)), ScalarCriteria::Clamp(StartVector.Dot(EndVector), -1.0, 1.0));
    if (Sweep <= ScalarCriteria::AngularTolerance || Sweep >= ScalarCriteria::Pi - ScalarCriteria::AngularTolerance ||
        CanonicalRadial.Distance(StartVector) > ScalarCriteria::GeometricTolerance)
        return Deliver<PartialEqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                                  "partial equal bicone sweep is not the bounded canonical sector");
    for (size_t I = 0; I < Body.Faces.size(); ++I)
        if (Body.Faces[I].Surface.Classification == SurfaceClassification::Plane)
        {
            Vec3 Normal;
            if (!PlanarNormal(Body, static_cast<int>(I), Normal) || std::fabs(Normal.Dot(Axis)) > ScalarCriteria::AngularTolerance)
                return Deliver<PartialEqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                                          "partial equal bicone radial cap is not planar");
        }
    const double RadiusScale = std::max(1.0, G[0].Radius);
    if (std::fabs(G[0].Radius - G[1].Radius) > ScalarCriteria::GeometricTolerance * RadiusScale)
        return Deliver<PartialEqualRadiusBiconeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                                  "partial equal route requires equal rim radii");
    PartialEqualRadiusBiconeApexFilletSpecification Specification;
    Specification.Apex = ApexPoint; Specification.Axis = Axis; Specification.Radius = G[0].Radius;
    Specification.LowerHeight = ApexPoint.Distance(G[0].Centre); Specification.UpperHeight = ApexPoint.Distance(G[1].Centre);
    Specification.FilletRadius = FilletRadius; Specification.SweepAngle = Sweep;
    const Deliver<BrepBody> Feasible = ReconstructPartialEqualRadiusBiconeApexFillet(Specification);
    if (!Feasible) return Deliver<PartialEqualRadiusBiconeApexFilletSpecification>::Reject(Feasible.Denial.Reason, Feasible.Denial.Detail);
    return Deliver<PartialEqualRadiusBiconeApexFilletSpecification>::Accept(std::move(Specification));
}

Deliver<BrepBody> BlendSolver::ReconstructConeApexFillet(const ConeApexFilletSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Base.X) || !std::isfinite(Specification.Base.Y) ||
        !std::isfinite(Specification.Base.Z) || !std::isfinite(Specification.BaseRadius) ||
        !std::isfinite(Specification.Height) || !std::isfinite(Specification.FilletRadius) ||
        Specification.BaseRadius <= Tol || Specification.Height <= Tol || Specification.FilletRadius <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "cone apex dimensions and radius must be finite and positive");
    if (!std::isfinite(Specification.Axis.X) || !std::isfinite(Specification.Axis.Y) ||
        !std::isfinite(Specification.Axis.Z) || Specification.Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "cone apex axis is degenerate");
    const Vec3 Axis = Specification.Axis.Normalised();
    const double R = Specification.BaseRadius;
    const double H = Specification.Height;
    const double Q = Specification.FilletRadius;
    const double Slant = std::hypot(H, R);
    const double MaximumFilletRadius = H * R / Slant;
    if (Q >= MaximumFilletRadius - Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "cone apex sphere consumes the cone before a positive frustum remains");

    Vec3 Radial = Axis.Cross(Vec3::UnitX());
    if (Radial.Length() <= ScalarCriteria::GeometricTolerance) Radial = Axis.Cross(Vec3::UnitY());
    if (Radial.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "cone apex radial frame is degenerate");
    Radial = Radial.Normalised();
    const Vec3 Base = Specification.Base;
    const double CentreHeight = H - Q * Slant / R;
    const double SeamRadius = Q * H / Slant;
    const double SeamHeight = H - Q * H * H / (R * Slant);
    const double TopHeight = CentreHeight + Q;
    if (CentreHeight <= Tol || SeamRadius <= Tol || SeamHeight <= Tol || TopHeight >= H - Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "cone apex sphere has a degenerate tangent seam");
    const Vec3 Centre = Base + Axis * CentreHeight;
    const Vec3 Seam = Base + Axis * SeamHeight + Radial * SeamRadius;
    const double CosTheta = ScalarCriteria::Clamp((SeamHeight - CentreHeight) / Q, -1.0, 1.0);
    const double Theta = std::acos(CosTheta);
    const double HalfTheta = 0.5 * Theta;
    const Vec3 Pole = Base + Axis * TopHeight;
    const Vec3 Middle = Centre + Axis * (Q * std::cos(HalfTheta)) + Radial * (Q * std::sin(HalfTheta));

    Deliver<NurbsCurve> ConeProfile = NurbsCurve::Line(Base + Radial * R, Seam);
    Deliver<NurbsCurve> CapProfile = NurbsCurve::ArcThreePoints(Pole, Middle, Seam);
    Deliver<NurbsCurve> DiskProfile = NurbsCurve::Line(Base, Base + Radial * R);
    if (!ConeProfile || !CapProfile || !DiskProfile)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "cone apex profiles are degenerate");
    Deliver<NurbsSurface> Cone = NurbsSurface::Revolution(ConeProfile.Payload, Base, Axis, ScalarCriteria::TwoPi);
    Deliver<NurbsSurface> Cap = NurbsSurface::Revolution(CapProfile.Payload, Base, Axis, ScalarCriteria::TwoPi);
    Deliver<NurbsSurface> Disk = NurbsSurface::Revolution(DiskProfile.Payload, Base, Axis, ScalarCriteria::TwoPi);
    if (!Cone || !Cap || !Disk)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "cone apex revolution surfaces are degenerate");
    Cone.Payload.Classification = SurfaceClassification::Cone;
    Cone.Payload.Origin = Base; Cone.Payload.Axis = Axis;
    Cone.Payload.RadiusMajor = R; Cone.Payload.RadiusMinor = SeamRadius;
    Cap.Payload.Classification = SurfaceClassification::Sphere;
    Cap.Payload.Origin = Centre; Cap.Payload.Axis = Axis;
    Cap.Payload.RadiusMajor = Q; Cap.Payload.RadiusMinor = Q;
    Disk.Payload.Classification = SurfaceClassification::Plane;
    Disk.Payload.Origin = Base; Disk.Payload.Axis = Axis;

    const Vec3 ConeNormal = (Radial * H + Axis * R).Normalised();
    const Vec3 CapNormal = (Radial * SeamRadius + Axis * (SeamHeight - CentreHeight)).Normalised();
    if (ConeNormal.Dot(CapNormal) < 1.0 - ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence, "cone apex spherical cap is not G1 tangent at the analytic seam");
    Deliver<BrepBody> Result = BrepBody::Sew({ Cone.Payload, Cap.Payload, Disk.Payload }, Tol, true);
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, "cone apex surfaces could not be sewn");
    const BodyReport Report = Result.Payload.Validate();
    const double FrustumVolume = ScalarCriteria::Pi * SeamHeight * (R * R + R * SeamRadius + SeamRadius * SeamRadius) / 3.0;
    const double CapHeight = TopHeight - SeamHeight;
    const double SphericalCapVolume = ScalarCriteria::Pi * CapHeight * CapHeight * (3.0 * Q - CapHeight) / 3.0;
    const double ExpectedVolume = FrustumVolume + SphericalCapVolume;
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "cone apex fillet did not reach closed manifold topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence, "cone apex fillet volume failed analytic acceptance");
    return Result;
}

Deliver<ConeApexFilletSpecification> BlendSolver::ClassifyConeApexFilletVertex(
    const BrepBody& Body, int Vertex, double FilletRadius) noexcept
{
    if (!std::isfinite(FilletRadius) || FilletRadius <= ScalarCriteria::MergeTolerance)
        return Deliver<ConeApexFilletSpecification>::Reject(RefusalReason::DegenerateInput,
                                                             "cone-apex vertex fillet radius must be finite and positive");
    const BodyReport Report = Body.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 ||
        Body.Vertices.size() != 2 || Body.Edges.size() != 2 || Body.Coedges.size() != 4 ||
        Body.Loops.size() != 2 || Body.Faces.size() != 2)
        return Deliver<ConeApexFilletSpecification>::Reject(RefusalReason::NonManifold,
                                                             "source is not the bounded canonical native cone topology");
    if (Vertex < 0 || Vertex >= static_cast<int>(Body.Vertices.size()))
        return Deliver<ConeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                             "selected cone-apex vertex is out of range");
    int ConeFace = -1, PlaneFace = -1;
    for (size_t I = 0; I < Body.Faces.size(); ++I)
    {
        const SurfaceClassification Classification = Body.Faces[I].Surface.Classification;
        if (Classification == SurfaceClassification::Cone)
        {
            if (ConeFace >= 0) return Deliver<ConeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                                     "source has multiple cone faces");
            ConeFace = static_cast<int>(I);
        }
        else if (Classification == SurfaceClassification::Plane)
        {
            if (PlaneFace >= 0) return Deliver<ConeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                                      "source has multiple planar cap faces");
            PlaneFace = static_cast<int>(I);
        }
        else return Deliver<ConeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                   "source is not a native cone and planar base pair");
    }
    if (ConeFace < 0 || PlaneFace < 0 || Body.Faces[ConeFace].Loops.size() != 1 ||
        Body.Faces[PlaneFace].Loops.size() != 1)
        return Deliver<ConeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                             "native cone faces do not have the bounded single loops");
    const NurbsSurface& Cone = Body.Faces[ConeFace].Surface;
    if (!std::isfinite(Cone.RadiusMajor) || Cone.RadiusMajor <= ScalarCriteria::MergeTolerance ||
        !std::isfinite(Cone.RadiusMinor) || std::fabs(Cone.RadiusMinor) > ScalarCriteria::GeometricTolerance ||
        Cone.Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<ConeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                             "native cone does not have a positive base and zero apex radius");
    const Vec3 Axis = Cone.Axis.Normalised();
    const Vec3 Base = Cone.Origin;
    int Apex = -1, BaseVertex = -1;
    double Height = 0.0;
    for (size_t I = 0; I < Body.Vertices.size(); ++I)
    {
        const Vec3 Delta = Body.Vertices[I].Point - Base;
        const double Along = Delta.Dot(Axis);
        const double Radial = (Delta - Axis * Along).Length();
        if (std::fabs(Along) <= ScalarCriteria::GeometricTolerance &&
            std::fabs(Radial - Cone.RadiusMajor) <= ScalarCriteria::GeometricTolerance * std::max(1.0, Cone.RadiusMajor))
        {
            if (BaseVertex >= 0) return Deliver<ConeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                                        "native cone has multiple base vertices");
            BaseVertex = static_cast<int>(I);
        }
        else if (Along > ScalarCriteria::MergeTolerance && Radial <= ScalarCriteria::GeometricTolerance * std::max(1.0, Along))
        {
            if (Apex >= 0) return Deliver<ConeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                                 "native cone has multiple axis apex vertices");
            Apex = static_cast<int>(I);
            Height = Along;
        }
        else return Deliver<ConeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                   "native cone vertex geometry is not a base rim and apex pair");
    }
    if (Apex < 0 || BaseVertex < 0 || Height <= ScalarCriteria::MergeTolerance || Vertex != Apex)
        return Deliver<ConeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                             "selected vertex is not the unique native cone apex");
    int Rim = 0, Seam = 0;
    for (const BrepEdge& EdgeData : Body.Edges)
    {
        if (EdgeData.Coedges.size() != 2) return Deliver<ConeApexFilletSpecification>::Reject(RefusalReason::NonManifold,
                                                                                                "native cone edge is not manifold");
        if (EdgeData.Closed() && EdgeData.VertexStart == BaseVertex &&
            EdgeData.Curve.Classification == CurveClassification::Circle && EdgeData.Curve.Rational()) ++Rim;
        else if (!EdgeData.Closed() && EdgeData.Curve.Classification == CurveClassification::Line &&
                 ((EdgeData.VertexStart == BaseVertex && EdgeData.VertexEnd == Apex) ||
                  (EdgeData.VertexStart == Apex && EdgeData.VertexEnd == BaseVertex))) ++Seam;
        else return Deliver<ConeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                   "native cone has unsupported edge geometry");
    }
    Vec3 PlaneNormal;
    if (Rim != 1 || Seam != 1 || !PlanarNormal(Body, PlaneFace, PlaneNormal) ||
        std::fabs(std::fabs(PlaneNormal.Dot(Axis)) - 1.0) > ScalarCriteria::AngularTolerance)
        return Deliver<ConeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                             "native cone base cap or rim is not exact");
    ConeApexFilletSpecification Specification;
    Specification.Base = Base;
    Specification.Axis = Axis;
    Specification.BaseRadius = Cone.RadiusMajor;
    Specification.Height = Height;
    Specification.FilletRadius = FilletRadius;
    const Deliver<BrepBody> Feasible = ReconstructConeApexFillet(Specification);
    if (!Feasible) return Deliver<ConeApexFilletSpecification>::Reject(Feasible.Denial.Reason, Feasible.Denial.Detail);
    return Deliver<ConeApexFilletSpecification>::Accept(std::move(Specification));
}

Deliver<PartialConeApexFilletSpecification> BlendSolver::ClassifyPartialConeApexFilletVertex(
    const BrepBody& Body, int Vertex, double FilletRadius) noexcept
{
    if (!std::isfinite(FilletRadius) || FilletRadius <= ScalarCriteria::MergeTolerance)
        return Deliver<PartialConeApexFilletSpecification>::Reject(RefusalReason::DegenerateInput,
                                                                    "partial cone-apex vertex fillet radius must be finite and positive");
    const BodyReport Report = Body.Validate();
    if (!Report.Solid() || Report.OpenEdges != 0 || Report.NonManifoldEdges != 0 ||
        Report.MisorientedEdges != 0 || Body.Vertices.size() != 4 || Body.Edges.size() != 6 || Body.Coedges.size() != 12 ||
        Body.Loops.size() != 4 || Body.Faces.size() != 4)
        return Deliver<PartialConeApexFilletSpecification>::Reject(RefusalReason::NonManifold,
                                                                    "source is not the bounded canonical half-turn partial cone topology");
    if (Vertex < 0 || Vertex >= static_cast<int>(Body.Vertices.size()))
        return Deliver<PartialConeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                    "selected partial cone-apex vertex is out of range");
    int RevolutionFaces = 0, PlaneFaces = 0;
    for (const BrepFace& Face : Body.Faces)
    {
        if (Face.Loops.size() != 1) return Deliver<PartialConeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                                                  "partial cone face loops are outside the bounded route");
        if (Face.Surface.Classification == SurfaceClassification::Revolution) ++RevolutionFaces;
        else if (Face.Surface.Classification == SurfaceClassification::Plane) ++PlaneFaces;
        else return Deliver<PartialConeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                          "source is not a bounded partial cone surface set");
    }
    if (RevolutionFaces != 3 || PlaneFaces != 1)
        return Deliver<PartialConeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                    "source does not have three partial cone revolutions and one base plane");

    int BaseVertex = -1, Apex = -1, RimA = -1, RimB = -1;
    Vec3 Base{}, Axis{};
    double Height = 0.0, BaseRadius = 0.0;
    for (int CandidateBase = 0; CandidateBase < static_cast<int>(Body.Vertices.size()); ++CandidateBase)
    {
        for (int CandidateApex = 0; CandidateApex < static_cast<int>(Body.Vertices.size()); ++CandidateApex)
        {
            if (CandidateBase == CandidateApex) continue;
            const Vec3 Delta = Body.Vertices[CandidateApex].Point - Body.Vertices[CandidateBase].Point;
            const double CandidateHeight = Delta.Length();
            if (CandidateHeight <= ScalarCriteria::MergeTolerance) continue;
            const Vec3 CandidateAxis = Delta / CandidateHeight;
            int FirstRim = -1, SecondRim = -1; double Radius = 0.0; bool Valid = true;
            for (int Other = 0; Other < static_cast<int>(Body.Vertices.size()); ++Other)
            {
                if (Other == CandidateBase || Other == CandidateApex) continue;
                const Vec3 FromBase = Body.Vertices[Other].Point - Body.Vertices[CandidateBase].Point;
                const double Along = FromBase.Dot(CandidateAxis);
                const Vec3 Radial = FromBase - CandidateAxis * Along;
                if (std::fabs(Along) > ScalarCriteria::GeometricTolerance * std::max(1.0, CandidateHeight) ||
                    Radial.Length() <= ScalarCriteria::MergeTolerance)
                { Valid = false; break; }
                if (FirstRim < 0) { FirstRim = Other; Radius = Radial.Length(); }
                else { SecondRim = Other; if (std::fabs(Radial.Length() - Radius) > ScalarCriteria::GeometricTolerance * std::max(1.0, Radius)) Valid = false; }
            }
            if (!Valid || FirstRim < 0 || SecondRim < 0 || Radius <= ScalarCriteria::MergeTolerance) continue;
            const Vec3 R0 = (Body.Vertices[FirstRim].Point - Body.Vertices[CandidateBase].Point).Normalised();
            const Vec3 R1 = (Body.Vertices[SecondRim].Point - Body.Vertices[CandidateBase].Point).Normalised();
            const double Sweep = std::acos(ScalarCriteria::Clamp(R0.Dot(R1), -1.0, 1.0));
            if (std::fabs(Sweep - ScalarCriteria::Pi) > ScalarCriteria::AngularTolerance) continue;
            if (BaseVertex >= 0) return Deliver<PartialConeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                                               "partial cone has multiple half-turn apex frames");
            BaseVertex = CandidateBase; Apex = CandidateApex; RimA = FirstRim; RimB = SecondRim;
            Base = Body.Vertices[CandidateBase].Point; Axis = CandidateAxis; Height = CandidateHeight; BaseRadius = Radius;
        }
    }
    if (BaseVertex < 0 || Apex < 0 || Vertex != Apex)
        return Deliver<PartialConeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                    "selected vertex is not the unique half-turn partial-cone apex");
    if (RimA < 0 || RimB < 0 || BaseRadius <= ScalarCriteria::MergeTolerance)
        return Deliver<PartialConeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                    "partial cone base rim is not a positive equal-radius pair");
    PartialConeApexFilletSpecification Specification;
    Specification.Base = Base;
    Specification.Axis = Axis;
    Specification.BaseRadius = BaseRadius;
    Specification.Height = Height;
    Specification.FilletRadius = FilletRadius;
    Specification.SweepAngle = ScalarCriteria::Pi;
    const Deliver<BrepBody> Feasible = ReconstructPartialConeApexFillet(Specification);
    if (!Feasible) return Deliver<PartialConeApexFilletSpecification>::Reject(Feasible.Denial.Reason, Feasible.Denial.Detail);
    return Deliver<PartialConeApexFilletSpecification>::Accept(std::move(Specification));
}

Deliver<PartialConeApexFilletSpecification> BlendSolver::ClassifyGeneralPartialConeApexFilletVertex(
    const BrepBody& Body, int Vertex, double FilletRadius) noexcept
{
    if (!std::isfinite(FilletRadius) || FilletRadius <= ScalarCriteria::MergeTolerance)
        return Deliver<PartialConeApexFilletSpecification>::Reject(RefusalReason::DegenerateInput,
                                                                    "general partial cone-apex vertex radius must be finite and positive");
    const BodyReport Report = Body.Validate();
    if (Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 ||
        Body.Vertices.size() != 4 || Body.Edges.size() != 6 || Body.Coedges.size() != 8 ||
        Body.Loops.size() != 3 || Body.Faces.size() != 3)
        return Deliver<PartialConeApexFilletSpecification>::Reject(RefusalReason::NonManifold,
                                                                    "source is not the bounded non-reflex partial-cone revolve topology");
    if (Vertex < 0 || Vertex >= static_cast<int>(Body.Vertices.size()))
        return Deliver<PartialConeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                    "selected general partial-cone vertex is out of range");
    int RevolutionFaces = 0;
    for (const BrepFace& Face : Body.Faces)
    {
        if (Face.Loops.size() != 1 || Face.Surface.Classification != SurfaceClassification::Revolution)
            return Deliver<PartialConeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                         "source is not the exact native partial-cone revolve surface set");
        ++RevolutionFaces;
    }
    if (RevolutionFaces != 3)
        return Deliver<PartialConeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                    "source does not have three native partial-cone revolve surfaces");
    int BaseVertex = -1, Apex = -1;
    Vec3 Base{}, Axis{};
    double Height = 0.0, BaseRadius = 0.0, Sweep = 0.0;
    for (int CandidateBase = 0; CandidateBase < static_cast<int>(Body.Vertices.size()); ++CandidateBase)
    {
        for (int CandidateApex = 0; CandidateApex < static_cast<int>(Body.Vertices.size()); ++CandidateApex)
        {
            if (CandidateBase == CandidateApex) continue;
            const Vec3 Delta = Body.Vertices[CandidateApex].Point - Body.Vertices[CandidateBase].Point;
            const double CandidateHeight = Delta.Length();
            if (CandidateHeight <= ScalarCriteria::MergeTolerance) continue;
            const Vec3 CandidateAxis = Delta / CandidateHeight;
            int FirstRim = -1, SecondRim = -1; double Radius = 0.0; bool Valid = true;
            for (int Other = 0; Other < static_cast<int>(Body.Vertices.size()); ++Other)
            {
                if (Other == CandidateBase || Other == CandidateApex) continue;
                const Vec3 FromBase = Body.Vertices[Other].Point - Body.Vertices[CandidateBase].Point;
                const double Along = FromBase.Dot(CandidateAxis);
                const Vec3 Radial = FromBase - CandidateAxis * Along;
                if (std::fabs(Along) > ScalarCriteria::GeometricTolerance * std::max(1.0, CandidateHeight) ||
                    Radial.Length() <= ScalarCriteria::MergeTolerance)
                { Valid = false; break; }
                if (FirstRim < 0) { FirstRim = Other; Radius = Radial.Length(); }
                else { SecondRim = Other; if (std::fabs(Radial.Length() - Radius) > ScalarCriteria::GeometricTolerance * std::max(1.0, Radius)) Valid = false; }
            }
            if (!Valid || FirstRim < 0 || SecondRim < 0 || Radius <= ScalarCriteria::MergeTolerance) continue;
            const Vec3 R0 = (Body.Vertices[FirstRim].Point - Body.Vertices[CandidateBase].Point).Normalised();
            const Vec3 R1 = (Body.Vertices[SecondRim].Point - Body.Vertices[CandidateBase].Point).Normalised();
            const double CandidateSweep = std::acos(ScalarCriteria::Clamp(R0.Dot(R1), -1.0, 1.0));
            if (CandidateSweep <= ScalarCriteria::AngularTolerance ||
                CandidateSweep >= ScalarCriteria::Pi - ScalarCriteria::AngularTolerance) continue;
            if (BaseVertex >= 0) return Deliver<PartialConeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                                               "partial cone has multiple non-reflex apex frames");
            BaseVertex = CandidateBase; Apex = CandidateApex; Base = Body.Vertices[CandidateBase].Point;
            Axis = CandidateAxis; Height = CandidateHeight; BaseRadius = Radius; Sweep = CandidateSweep;
        }
    }
    if (BaseVertex < 0 || Apex < 0 || Vertex != Apex)
        return Deliver<PartialConeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                    "selected vertex is not the unique non-reflex partial-cone apex");
    PartialConeApexFilletSpecification Specification;
    Specification.Base = Base;
    Specification.Axis = Axis;
    Specification.BaseRadius = BaseRadius;
    Specification.Height = Height;
    Specification.FilletRadius = FilletRadius;
    Specification.SweepAngle = Sweep;
    const Deliver<BrepBody> Feasible = ReconstructPartialConeApexFillet(Specification);
    if (!Feasible) return Deliver<PartialConeApexFilletSpecification>::Reject(Feasible.Denial.Reason, Feasible.Denial.Detail);
    return Deliver<PartialConeApexFilletSpecification>::Accept(std::move(Specification));
}

Deliver<PartialConeApexFilletSpecification> BlendSolver::ClassifyReflexPartialConeApexFilletVertex(
    const BrepBody& Body, int Vertex, double FilletRadius) noexcept
{
    if (!std::isfinite(FilletRadius) || FilletRadius <= ScalarCriteria::MergeTolerance)
        return Deliver<PartialConeApexFilletSpecification>::Reject(RefusalReason::DegenerateInput,
                                                                    "reflex partial cone-apex vertex radius must be finite and positive");
    const BodyReport Report = Body.Validate();
    if (Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 ||
        Body.Vertices.size() != 4 || Body.Edges.size() != 6 || Body.Coedges.size() != 8 ||
        Body.Loops.size() != 3 || Body.Faces.size() != 3)
        return Deliver<PartialConeApexFilletSpecification>::Reject(RefusalReason::NonManifold,
                                                                    "source is not the bounded reflex partial-cone revolve topology");
    if (Vertex < 0 || Vertex >= static_cast<int>(Body.Vertices.size()))
        return Deliver<PartialConeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                    "selected reflex partial-cone vertex is out of range");
    for (const BrepFace& Face : Body.Faces)
        if (Face.Loops.size() != 1 || Face.Surface.Classification != SurfaceClassification::Revolution)
            return Deliver<PartialConeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                         "source is not the exact native reflex partial-revolve surface set");

    int BaseVertex = -1, Apex = -1, RimA = -1, RimB = -1;
    Vec3 Base{}, Axis{};
    double Height = 0.0, BaseRadius = 0.0;
    for (int CandidateBase = 0; CandidateBase < static_cast<int>(Body.Vertices.size()); ++CandidateBase)
    {
        for (int CandidateApex = 0; CandidateApex < static_cast<int>(Body.Vertices.size()); ++CandidateApex)
        {
            if (CandidateBase == CandidateApex) continue;
            const Vec3 Delta = Body.Vertices[CandidateApex].Point - Body.Vertices[CandidateBase].Point;
            const double CandidateHeight = Delta.Length();
            if (CandidateHeight <= ScalarCriteria::MergeTolerance) continue;
            const Vec3 CandidateAxis = Delta / CandidateHeight;
            int FirstRim = -1, SecondRim = -1; double Radius = 0.0; bool Valid = true;
            for (int Other = 0; Other < static_cast<int>(Body.Vertices.size()); ++Other)
            {
                if (Other == CandidateBase || Other == CandidateApex) continue;
                const Vec3 FromBase = Body.Vertices[Other].Point - Body.Vertices[CandidateBase].Point;
                const double Along = FromBase.Dot(CandidateAxis);
                const Vec3 Radial = FromBase - CandidateAxis * Along;
                if (std::fabs(Along) > ScalarCriteria::GeometricTolerance * std::max(1.0, CandidateHeight) ||
                    Radial.Length() <= ScalarCriteria::MergeTolerance)
                { Valid = false; break; }
                if (FirstRim < 0) { FirstRim = Other; Radius = Radial.Length(); }
                else { SecondRim = Other; if (std::fabs(Radial.Length() - Radius) > ScalarCriteria::GeometricTolerance * std::max(1.0, Radius)) Valid = false; }
            }
            if (!Valid || FirstRim < 0 || SecondRim < 0 || Radius <= ScalarCriteria::MergeTolerance) continue;
            if (BaseVertex >= 0) return Deliver<PartialConeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                                               "reflex partial cone has multiple apex frames");
            BaseVertex = CandidateBase; Apex = CandidateApex; RimA = FirstRim; RimB = SecondRim;
            Base = Body.Vertices[CandidateBase].Point; Axis = CandidateAxis; Height = CandidateHeight; BaseRadius = Radius;
        }
    }
    if (BaseVertex < 0 || Apex < 0 || Vertex != Apex || RimA < 0 || RimB < 0)
        return Deliver<PartialConeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                    "selected vertex is not the unique reflex partial-cone apex");
    const Vec3 R0 = (Body.Vertices[RimA].Point - Base).Normalised();
    int RimEdge = -1;
    for (size_t I = 0; I < Body.Edges.size(); ++I)
    {
        const BrepEdge& EdgeData = Body.Edges[I];
        if (!EdgeData.Closed() && ((EdgeData.VertexStart == RimA && EdgeData.VertexEnd == RimB) ||
                                   (EdgeData.VertexStart == RimB && EdgeData.VertexEnd == RimA)))
        {
            if (RimEdge >= 0) return Deliver<PartialConeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                                           "reflex partial cone has multiple base-rim paths");
            RimEdge = static_cast<int>(I);
        }
    }
    if (RimEdge < 0) return Deliver<PartialConeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                                  "reflex partial cone has no base-rim path");
    const NurbsCurve& RimCurve = Body.Edges[RimEdge].Curve;
    double Previous = 0.0, Accumulated = 0.0;
    for (int I = 0; I <= 64; ++I)
    {
        const double T = RimCurve.DomainStart() + (RimCurve.DomainEnd() - RimCurve.DomainStart()) * static_cast<double>(I) / 64.0;
        const Vec3 FromBase = RimCurve.Sample(T) - Base;
        const double Along = FromBase.Dot(Axis);
        const Vec3 Radial = FromBase - Axis * Along;
        if (Radial.Length() <= ScalarCriteria::MergeTolerance ||
            std::fabs(Along) > ScalarCriteria::GeometricTolerance * std::max(1.0, Height) ||
            std::fabs(Radial.Length() - BaseRadius) > ScalarCriteria::GeometricTolerance * std::max(1.0, BaseRadius))
            return Deliver<PartialConeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                         "reflex partial cone base-rim path is not circular");
        const double Angle = std::atan2(Axis.Dot(R0.Cross(Radial.Normalised())), R0.Dot(Radial.Normalised()));
        if (I > 0)
        {
            double Delta = Angle - Previous;
            while (Delta > ScalarCriteria::Pi) Delta -= ScalarCriteria::TwoPi;
            while (Delta < -ScalarCriteria::Pi) Delta += ScalarCriteria::TwoPi;
            Accumulated += Delta;
        }
        Previous = Angle;
    }
    const double Sweep = std::fabs(Accumulated);
    if (Sweep <= ScalarCriteria::Pi + ScalarCriteria::AngularTolerance ||
        Sweep >= ScalarCriteria::TwoPi - ScalarCriteria::SweepTolerance)
        return Deliver<PartialConeApexFilletSpecification>::Reject(RefusalReason::Unsupported,
                                                                    "partial cone sweep is not strictly reflex");
    PartialConeApexFilletSpecification Specification;
    Specification.Base = Base;
    Specification.Axis = Axis;
    Specification.BaseRadius = BaseRadius;
    Specification.Height = Height;
    Specification.FilletRadius = FilletRadius;
    Specification.SweepAngle = Sweep;
    const Deliver<BrepBody> Feasible = ReconstructPartialConeApexFillet(Specification);
    if (!Feasible) return Deliver<PartialConeApexFilletSpecification>::Reject(Feasible.Denial.Reason, Feasible.Denial.Detail);
    return Deliver<PartialConeApexFilletSpecification>::Accept(std::move(Specification));
}

Deliver<BrepBody> BlendSolver::ReconstructPartialConeApexFillet(
    const PartialConeApexFilletSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Base.X) || !std::isfinite(Specification.Base.Y) ||
        !std::isfinite(Specification.Base.Z) || !std::isfinite(Specification.BaseRadius) ||
        !std::isfinite(Specification.Height) || !std::isfinite(Specification.FilletRadius) ||
        !std::isfinite(Specification.SweepAngle) || Specification.BaseRadius <= Tol ||
        Specification.Height <= Tol || Specification.FilletRadius <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial cone-apex dimensions must be finite and positive");
    if (Specification.SweepAngle <= ScalarCriteria::SweepTolerance ||
        Specification.SweepAngle >= ScalarCriteria::TwoPi - ScalarCriteria::SweepTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "partial cone-apex sweep must be strictly between zero and a full turn");
    if (!std::isfinite(Specification.Axis.X) || !std::isfinite(Specification.Axis.Y) ||
        !std::isfinite(Specification.Axis.Z) || Specification.Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial cone-apex axis is degenerate");
    const Vec3 Axis = Specification.Axis.Normalised();
    const double R = Specification.BaseRadius;
    const double H = Specification.Height;
    const double Q = Specification.FilletRadius;
    const double Slant = std::hypot(H, R);
    const double MaximumFilletRadius = H * R / Slant;
    if (Q >= MaximumFilletRadius - Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "partial cone-apex sphere consumes the cone before a positive frustum remains");
    const Vec3 Base = Specification.Base;
    const Vec3 Radial = Workplane::FromNormal(Base, Axis).AxisX.Normalised();
    if (Radial.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial cone-apex radial frame is degenerate");
    const double CentreHeight = H - Q * Slant / R;
    const double SeamRadius = Q * H / Slant;
    const double SeamHeight = H - Q * H * H / (R * Slant);
    const double TopHeight = CentreHeight + Q;
    if (CentreHeight <= Tol || SeamRadius <= Tol || SeamHeight <= Tol || TopHeight >= H - Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial cone-apex sphere has a degenerate tangent seam");
    const Vec3 Centre = Base + Axis * CentreHeight;
    const Vec3 Seam = Base + Axis * SeamHeight + Radial * SeamRadius;
    const double CosTheta = ScalarCriteria::Clamp((SeamHeight - CentreHeight) / Q, -1.0, 1.0);
    const double HalfTheta = 0.5 * std::acos(CosTheta);
    const Vec3 Pole = Base + Axis * TopHeight;
    const Vec3 Middle = Centre + Axis * (Q * std::cos(HalfTheta)) + Radial * (Q * std::sin(HalfTheta));
    Deliver<NurbsCurve> ConeProfile = NurbsCurve::Line(Base + Radial * R, Seam);
    Deliver<NurbsCurve> CapProfile = NurbsCurve::ArcThreePoints(Pole, Middle, Seam);
    Deliver<NurbsCurve> DiskProfile = NurbsCurve::Line(Base, Base + Radial * R);
    Deliver<NurbsCurve> AxisProfile = NurbsCurve::Line(Pole, Base);
    if (!ConeProfile || !CapProfile || !DiskProfile || !AxisProfile)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial cone-apex meridian profiles are degenerate");
    Deliver<NurbsSurface> Cone = NurbsSurface::Revolution(ConeProfile.Payload, Base, Axis, Specification.SweepAngle);
    Deliver<NurbsSurface> Cap = NurbsSurface::Revolution(CapProfile.Payload, Base, Axis, Specification.SweepAngle);
    Deliver<NurbsSurface> Disk = NurbsSurface::Revolution(DiskProfile.Payload, Base, Axis, Specification.SweepAngle);
    if (!Cone || !Cap || !Disk)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial cone-apex revolution surfaces are degenerate");
    Cone.Payload.Classification = SurfaceClassification::Cone;
    Cone.Payload.Origin = Base; Cone.Payload.Axis = Axis;
    Cone.Payload.RadiusMajor = R; Cone.Payload.RadiusMinor = SeamRadius;
    Cap.Payload.Classification = SurfaceClassification::Sphere;
    Cap.Payload.Origin = Centre; Cap.Payload.Axis = Axis;
    Cap.Payload.RadiusMajor = Q; Cap.Payload.RadiusMinor = Q;
    Disk.Payload.Classification = SurfaceClassification::Plane;
    Disk.Payload.Origin = Base; Disk.Payload.Axis = Axis;
    auto MeridianCap = [&](const Mat4& Transform) -> Deliver<NurbsSurface>
    {
        const NurbsCurve D = DiskProfile.Payload.Transformed(Transform);
        const NurbsCurve C = ConeProfile.Payload.Transformed(Transform);
        const NurbsCurve S = CapProfile.Payload.Reversed().Transformed(Transform);
        const NurbsCurve A = AxisProfile.Payload.Transformed(Transform);
        if (D.Validate() || C.Validate() || S.Validate() || A.Validate())
            return Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput, "partial cone-apex meridian cap is invalid");
        return SkinSolver::CoonsPatch({ D, C, S, A });
    };
    const Mat4 StartTransform = Mat4::Identity();
    const Mat4 EndTransform = Mat4::Translation(Base) * Mat4::Rotation(Axis, Specification.SweepAngle) * Mat4::Translation(-Base);
    Deliver<NurbsSurface> StartCap = MeridianCap(StartTransform);
    Deliver<NurbsSurface> EndCap = StartCap
        ? Deliver<NurbsSurface>::Accept(StartCap.Payload.Transformed(EndTransform))
        : Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput, "partial cone-apex end cap is invalid");
    if (!StartCap || !EndCap)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial cone-apex meridian caps could not be constructed");
    Deliver<BrepBody> Result = BrepBody::Sew({ Cone.Payload, Cap.Payload, Disk.Payload, StartCap.Payload, EndCap.Payload }, Tol, true);
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, "partial cone-apex surfaces could not be sewn");
    // The generic sew orientation walk intentionally prefers the source surface orientation at a meridian;
    // the end meridian is the one bounded case where the positive-volume walk needs an explicit face flip.
    const Vec3 EndRadial = Mat4::Rotation(Axis, Specification.SweepAngle).TransformDirection(Radial).Normalised();
    for (BrepFace& Face : Result.Payload.Faces)
    {
        if (Face.Surface.Classification != SurfaceClassification::Coons) continue;
        const double U = 0.5 * (Face.Surface.DomainStartU() + Face.Surface.DomainEndU());
        const double V = 0.5 * (Face.Surface.DomainStartV() + Face.Surface.DomainEndV());
        const Vec3 P = Face.Surface.Sample(U, V);
        const Vec3 RDirection = (P - Base - Axis * (P - Base).Dot(Axis)).Normalised();
        if (RDirection.Dot(EndRadial) > 1.0 - 1e-6) Face.Reversed = !Face.Reversed;
    }
    const BodyReport Report = Result.Payload.Validate();
    const double FrustumVolume = ScalarCriteria::Pi * SeamHeight * (R * R + R * SeamRadius + SeamRadius * SeamRadius) / 3.0;
    const double CapHeight = TopHeight - SeamHeight;
    const double SphericalCapVolume = ScalarCriteria::Pi * CapHeight * CapHeight * (3.0 * Q - CapHeight) / 3.0;
    const double ExpectedVolume = (FrustumVolume + SphericalCapVolume) * Specification.SweepAngle / ScalarCriteria::TwoPi;
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "partial cone-apex fillet did not reach closed manifold topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence, "partial cone-apex volume failed analytic acceptance");
    return Result;
}

Deliver<BrepBody> BlendSolver::ReconstructQuadraticPartialEdgeFillet(
    const QuadraticPartialEdgeFilletSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Origin.X) || !std::isfinite(Specification.Origin.Y) ||
        !std::isfinite(Specification.Origin.Z) || !std::isfinite(Specification.Length) ||
        !std::isfinite(Specification.Start) || !std::isfinite(Specification.End) ||
        !std::isfinite(Specification.Width) || Specification.Length <= Tol || Specification.Width <= Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "quadratic partial-edge dimensions must be finite and positive");
    if (Specification.Start <= Tol || Specification.End >= Specification.Length - Tol ||
        Specification.End <= Specification.Start + Tol)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "quadratic partial-edge interval must be strict and interior");
    if (!Specification.RadiusLaw.Positive() || !Specification.RadiusLaw.Nonlinear())
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "quadratic partial-edge route requires a genuinely nonlinear positive radius law");
    if (!std::isfinite(Specification.EdgeAxis.X) || !std::isfinite(Specification.EdgeAxis.Y) ||
        !std::isfinite(Specification.EdgeAxis.Z) || Specification.EdgeAxis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "quadratic partial-edge axis is degenerate");
    const Vec3 Axis = Specification.EdgeAxis.Normalised();
    Vec3 U = Axis.Cross(Vec3::UnitX());
    if (U.Length() <= ScalarCriteria::GeometricTolerance) U = Axis.Cross(Vec3::UnitY());
    if (U.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "quadratic partial-edge frame is degenerate");
    U = U.Normalised();
    const Vec3 V = Axis.Cross(U).Normalised();
    const double S = Specification.Start;
    const double E = Specification.End;
    const double M = 0.5 * (S + E);
    const double W = Specification.Width;
    const double R0 = Specification.RadiusLaw.Radius(0.0);
    const double Rm = Specification.RadiusLaw.Radius(0.5);
    const double R1 = Specification.RadiusLaw.Radius(1.0);
    for (int I = 0; I <= 32; ++I)
    {
        const double T = static_cast<double>(I) / 32.0;
        const double Radius = Specification.RadiusLaw.Radius(T);
        if (!std::isfinite(Radius) || Radius <= Tol || Radius >= W - Tol)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "quadratic partial-edge radius leaves the positive support clearance");
    }
    const Vec3 Origin = Specification.Origin;
    auto At = [&](double T, double AlongU, double AlongV) noexcept
    {
        return Origin + Axis * T + U * AlongU + V * AlongV;
    };
    auto Line = [](Vec3 A, Vec3 B) -> Deliver<NurbsCurve> { return NurbsCurve::Line(A, B); };
    auto SectionLine = [&](double T, double Radius, Vec3 A, Vec3 B) -> Deliver<NurbsCurve>
    {
        (void)Radius;
        return Line(At(T, A.X, A.Y), At(T, B.X, B.Y));
    };
    auto SectionArc = [&](double T, double Radius) -> Deliver<NurbsCurve>
    {
        const double Offset = Radius - Radius / std::sqrt(2.0);
        return NurbsCurve::ArcThreePoints(At(T, Radius, 0.0), At(T, Offset, Offset), At(T, 0.0, Radius));
    };
    std::vector<NurbsSurface> Faces;
    auto AddPlane = [&](Vec3 Corner, Vec3 UAxis, Vec3 VAxis, double LU, double LV) -> bool
    {
        Deliver<NurbsSurface> Face = NurbsSurface::Plane(Corner, UAxis, VAxis, LU, LV);
        if (!Face) return false;
        Faces.push_back(std::move(Face.Payload));
        return true;
    };
    auto AddWallInterval = [&](double A, double B, double Radius) -> bool
    {
        const double D = B - A;
        return AddPlane(At(A, 0.0, 0.0), Axis, U, D, Radius) &&
               AddPlane(At(A, Radius, 0.0), Axis, U, D, W - Radius) &&
               AddPlane(At(A, W, 0.0), Axis, V, D, Radius) &&
               AddPlane(At(A, W, Radius), Axis, V, D, W - Radius) &&
               AddPlane(At(A, 0.0, W), Axis, U, D, Radius) &&
               AddPlane(At(A, Radius, W), Axis, U, D, W - Radius) &&
               AddPlane(At(A, 0.0, 0.0), Axis, V, D, Radius) &&
               AddPlane(At(A, 0.0, Radius), Axis, V, D, W - Radius);
    };
    auto Loft = [&](std::vector<Deliver<NurbsCurve>> Sections) -> Deliver<NurbsSurface>
    {
        std::vector<NurbsCurve> Curves;
        Curves.reserve(Sections.size());
        for (const Deliver<NurbsCurve>& Section : Sections)
        {
            if (!Section) return Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput, "quadratic partial-edge section is degenerate");
            Curves.push_back(Section.Payload);
        }
        return NurbsSurface::Loft(Curves, 2);
    };
    // The blend section endpoints are expressed directly at each station; this keeps the six
    // support patches explicit rather than broadening the route into arbitrary variable support handling.
    auto BlendLine = [&](double T, double Radius, Vec3 A, Vec3 B) { return SectionLine(T, Radius, A, B); };
    auto AddThree = [&](std::vector<Deliver<NurbsCurve>> Curves) -> bool
    {
        Deliver<NurbsSurface> Surface = Loft(std::move(Curves));
        if (!Surface) return false;
        Faces.push_back(std::move(Surface.Payload));
        return true;
    };
    if (!AddWallInterval(0.0, S, R0) || !AddWallInterval(E, Specification.Length, R1) ||
        !AddThree({ BlendLine(S, R0, { R0, 0, 0 }, { W, 0, 0 }), BlendLine(M, Rm, { Rm, 0, 0 }, { W, 0, 0 }), BlendLine(E, R1, { R1, 0, 0 }, { W, 0, 0 }) }) ||
        !AddThree({ BlendLine(S, R0, { W, 0, 0 }, { W, R0, 0 }), BlendLine(M, Rm, { W, 0, 0 }, { W, Rm, 0 }), BlendLine(E, R1, { W, 0, 0 }, { W, R1, 0 }) }) ||
        !AddThree({ BlendLine(S, R0, { W, R0, 0 }, { W, W, 0 }), BlendLine(M, Rm, { W, Rm, 0 }, { W, W, 0 }), BlendLine(E, R1, { W, R1, 0 }, { W, W, 0 }) }) ||
        !AddThree({ BlendLine(S, R0, { 0, W, 0 }, { R0, W, 0 }), BlendLine(M, Rm, { 0, W, 0 }, { Rm, W, 0 }), BlendLine(E, R1, { 0, W, 0 }, { R1, W, 0 }) }) ||
        !AddThree({ BlendLine(S, R0, { R0, W, 0 }, { W, W, 0 }), BlendLine(M, Rm, { Rm, W, 0 }, { W, W, 0 }), BlendLine(E, R1, { R1, W, 0 }, { W, W, 0 }) }) ||
        !AddThree({ BlendLine(S, R0, { 0, R0, 0 }, { 0, W, 0 }), BlendLine(M, Rm, { 0, Rm, 0 }, { 0, W, 0 }), BlendLine(E, R1, { 0, R1, 0 }, { 0, W, 0 }) }) ||
        !AddThree({ SectionArc(S, R0), SectionArc(M, Rm), SectionArc(E, R1) }))
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "quadratic partial-edge surfaces could not be constructed");
    auto AddTransition = [&](double T, double Radius) -> bool
    {
        Deliver<NurbsCurve> OA = Line(At(T, 0.0, 0.0), At(T, Radius, 0.0));
        Deliver<NurbsCurve> AE = SectionArc(T, Radius);
        Deliver<NurbsCurve> EO = Line(At(T, 0.0, Radius), At(T, 0.0, 0.0));
        if (!OA || !AE || !EO) return false;
        Deliver<NurbsSurface> Cap = SkinSolver::CoonsPatch({ OA.Payload, AE.Payload, EO.Payload });
        if (!Cap) return false;
        Faces.push_back(std::move(Cap.Payload));
        return true;
    };
    auto AddEndCap = [&](double T, double Radius) -> bool
    {
        return AddPlane(At(T, 0.0, 0.0), U, V, Radius, Radius) &&
               AddPlane(At(T, Radius, 0.0), U, V, W - Radius, Radius) &&
               AddPlane(At(T, Radius, Radius), U, V, W - Radius, W - Radius) &&
               AddPlane(At(T, 0.0, Radius), U, V, Radius, W - Radius);
    };
    if (!AddTransition(S, R0) || !AddTransition(E, R1) || !AddEndCap(0.0, R0) || !AddEndCap(Specification.Length, R1))
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "quadratic partial-edge caps are degenerate");
    Deliver<BrepBody> Result = BrepBody::Sew(Faces, Tol, true);
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, "quadratic partial-edge surfaces could not be sewn");
    const BodyReport Report = Result.Payload.Validate();
    const double IntervalLength = E - S;
    const double ExpectedVolume = Specification.Length * W * W -
        (1.0 - ScalarCriteria::Pi / 4.0) * Specification.RadiusLaw.IntegratedSquare(IntervalLength);
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 ||
        Result.Payload.Vertices.size() != 34 || Result.Payload.Edges.size() != 67 ||
        Result.Payload.Faces.size() != 35 || Result.Payload.Loops.size() != 35)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "quadratic partial-edge blend did not reach exact capped topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence, "quadratic partial-edge volume failed analytic acceptance");
    return Result;
}

Deliver<BrepBody> BlendSolver::ReconstructObliqueQuadraticEdgeFillet(
    const ObliqueQuadraticEdgeFilletSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Origin.X) || !std::isfinite(Specification.Origin.Y) ||
        !std::isfinite(Specification.Origin.Z) || !std::isfinite(Specification.Length) ||
        !std::isfinite(Specification.WidthA) || !std::isfinite(Specification.WidthB) ||
        Specification.Length <= ScalarCriteria::MergeTolerance ||
        Specification.WidthA <= ScalarCriteria::MergeTolerance ||
        Specification.WidthB <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "oblique quadratic edge dimensions must be finite and positive");
    if (!Specification.RadiusLaw.Positive() || !Specification.RadiusLaw.Nonlinear())
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "oblique quadratic edge route requires a genuinely nonlinear positive radius law");
    if (Specification.EdgeAxis.Length() <= ScalarCriteria::GeometricTolerance ||
        Specification.SupportA.Length() <= ScalarCriteria::GeometricTolerance ||
        Specification.SupportB.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "oblique quadratic edge frame contains a degenerate direction");
    const Vec3 Axis = Specification.EdgeAxis.Normalised();
    const Vec3 SupportA = Specification.SupportA.Normalised();
    const Vec3 SupportB = Specification.SupportB.Normalised();
    if (std::fabs(Axis.Dot(SupportA)) > ScalarCriteria::AngularTolerance ||
        std::fabs(Axis.Dot(SupportB)) > ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "oblique quadratic edge supports must be perpendicular to the edge");
    const double Theta = std::acos(ScalarCriteria::Clamp(SupportA.Dot(SupportB), -1.0, 1.0));
    if (Theta <= ScalarCriteria::AngularTolerance || Theta >= ScalarCriteria::Pi - ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "oblique quadratic edge support angle must be strictly between zero and pi");
    const double HalfTheta = Theta * 0.5;
    const double CotHalf = std::cos(HalfTheta) / std::sin(HalfTheta);
    const Vec3 Origin = Specification.Origin;
    const Vec3 OuterA = Origin + SupportA * Specification.WidthA;
    const Vec3 OuterB = Origin + SupportB * Specification.WidthB;
    const auto RadiusAt = [&](double T) noexcept { return Specification.RadiusLaw.Radius(T); };
    for (int I = 0; I <= 64; ++I)
    {
        const double T = static_cast<double>(I) / 64.0;
        const double Radius = RadiusAt(T);
        const double TangentDistance = Radius * CotHalf;
        if (!std::isfinite(Radius) || Radius <= ScalarCriteria::MergeTolerance ||
            TangentDistance >= Specification.WidthA - ScalarCriteria::MergeTolerance ||
            TangentDistance >= Specification.WidthB - ScalarCriteria::MergeTolerance)
            return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                              "oblique quadratic edge radius consumes a finite support extent");
    }
    const auto Point = [&](double Along, Vec3 Planar) noexcept { return Origin + Axis * Along + Planar; };
    const auto TangentA = [&](double Along, double Radius) noexcept
    {
        return Point(Along, SupportA * (Radius * CotHalf));
    };
    const auto TangentB = [&](double Along, double Radius) noexcept
    {
        return Point(Along, SupportB * (Radius * CotHalf));
    };
    const auto ArcMiddle = [&](double Along, double Radius) noexcept
    {
        const Vec3 Centre = Origin + Axis * Along + (SupportA + SupportB).Normalised() * (Radius / std::sin(HalfTheta));
        const Vec3 RadialA = (TangentA(Along, Radius) - Centre).Normalised();
        const Vec3 RadialB = (TangentB(Along, Radius) - Centre).Normalised();
        return Centre + (RadialA + RadialB).Normalised() * Radius;
    };
    const auto Line = [](Vec3 A, Vec3 B) -> Deliver<NurbsCurve> { return NurbsCurve::Line(A, B); };
    const auto WallAAt = [&](double Along, double Radius)
    {
        return Line(TangentA(Along, Radius), Point(Along, OuterA - Origin));
    };
    const auto OuterAt = [&](double Along)
    {
        return Line(Point(Along, OuterA - Origin), Point(Along, OuterB - Origin));
    };
    const auto WallBAt = [&](double Along, double Radius)
    {
        return Line(Point(Along, OuterB - Origin), TangentB(Along, Radius));
    };
    const auto ArcAt = [&](double Along, double Radius)
    {
        return NurbsCurve::ArcThreePoints(TangentA(Along, Radius), ArcMiddle(Along, Radius), TangentB(Along, Radius));
    };
    const auto StationSurface = [&](std::vector<Deliver<NurbsCurve>> Sections) -> Deliver<NurbsSurface>
    {
        if (Sections.size() != 3) return Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput,
                                                                        "oblique quadratic edge requires three stations");
        std::vector<NurbsCurve> Rows;
        Rows.reserve(3);
        for (const Deliver<NurbsCurve>& Section : Sections)
        {
            if (!Section) return Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput,
                                                                "oblique quadratic edge station is degenerate");
            Rows.push_back(Section.Payload.Degree < 2 ? Section.Payload.Elevated(2) : Section.Payload);
        }
        for (NurbsCurve& Row : Rows) Row = Row.Reparameterised(0.0, 1.0);
        const int CountU = Rows.front().PoleCount();
        for (const NurbsCurve& Row : Rows)
            if (Row.PoleCount() != CountU || Row.Knots != Rows.front().Knots)
                return Deliver<NurbsSurface>::Reject(RefusalReason::NoConvergence,
                                                      "oblique quadratic edge station curves are incompatible");
        std::vector<Vec4> Poles(static_cast<size_t>(CountU) * 3);
        for (int I = 0; I < CountU; ++I)
        {
            Poles[static_cast<size_t>(I) * 3] = Rows[0].Poles[I];
            Poles[static_cast<size_t>(I) * 3 + 1] = Rows[1].Poles[I] * 2.0 -
                (Rows[0].Poles[I] + Rows[2].Poles[I]) * 0.5;
            Poles[static_cast<size_t>(I) * 3 + 2] = Rows[2].Poles[I];
        }
        Deliver<NurbsSurface> Surface = NurbsSurface::Build(2, 2, CountU, 3, std::move(Poles),
                                                              Rows.front().Knots, { 0, 0, 0, 1, 1, 1 });
        if (Surface) Surface.Payload.Classification = SurfaceClassification::Loft;
        return Surface;
    };
    const double R0 = RadiusAt(0.0);
    const double Rm = RadiusAt(0.5);
    const double R1 = RadiusAt(1.0);
    std::vector<NurbsSurface> Surfaces;
    auto Add = [&](std::vector<Deliver<NurbsCurve>> Sections) -> bool
    {
        Deliver<NurbsSurface> Surface = StationSurface(std::move(Sections));
        if (!Surface) return false;
        Surfaces.push_back(std::move(Surface.Payload));
        return true;
    };
    if (!Add({ WallAAt(0.0, R0), WallAAt(Specification.Length * 0.5, Rm), WallAAt(Specification.Length, R1) }) ||
        !Add({ OuterAt(0.0), OuterAt(Specification.Length * 0.5), OuterAt(Specification.Length) }) ||
        !Add({ WallBAt(0.0, R0), WallBAt(Specification.Length * 0.5, Rm), WallBAt(Specification.Length, R1) }) ||
        !Add({ ArcAt(0.0, R0), ArcAt(Specification.Length * 0.5, Rm), ArcAt(Specification.Length, R1) }))
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "oblique quadratic edge support surfaces are degenerate");
    const auto AddEndCap = [&](double Along, double Radius) -> bool
    {
        const Mat4 Transform = Mat4::Translation(Axis * Along);
        Deliver<NurbsCurve> WallA = WallAAt(0.0, Radius);
        Deliver<NurbsCurve> Outer = OuterAt(0.0);
        Deliver<NurbsCurve> WallB = WallBAt(0.0, Radius);
        Deliver<NurbsCurve> Arc = ArcAt(0.0, Radius);
        if (!WallA || !Outer || !WallB || !Arc) return false;
        Deliver<NurbsSurface> EndCap = SkinSolver::CoonsPatch({ WallA.Payload.Transformed(Transform),
                                                                  Outer.Payload.Transformed(Transform),
                                                                  WallB.Payload.Transformed(Transform),
                                                                  Arc.Payload.Reversed().Transformed(Transform) });
        if (!EndCap) return false;
        Surfaces.push_back(std::move(EndCap.Payload));
        return true;
    };
    if (!AddEndCap(0.0, R0) || !AddEndCap(Specification.Length, R1))
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "oblique quadratic edge finite caps are degenerate");
    Deliver<BrepBody> Result = BrepBody::Sew(Surfaces, ScalarCriteria::MergeTolerance, false);
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason,
                                                   "oblique quadratic edge surfaces could not be sewn");
    const double SharpArea = 0.5 * Specification.WidthA * Specification.WidthB * std::sin(Theta);
    const double RemovedCoefficient = 0.5 * std::sin(Theta) * (CotHalf * CotHalf + 1.0) -
        0.5 * (ScalarCriteria::Pi - Theta);
    const double ExpectedVolume = Specification.Length * SharpArea -
        RemovedCoefficient * Specification.RadiusLaw.IntegratedSquare(Specification.Length);
    const BodyReport Report = Result.Payload.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                                          "oblique quadratic edge did not reach closed manifold topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence,
                                          "oblique quadratic edge volume failed analytic acceptance");
    return Result;
}

Deliver<BrepBody> BlendSolver::ReconstructNonlinearVariableRadiusCornerBlend(
    const NonlinearVariableRadiusCornerSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Setback) || Specification.Setback <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "nonlinear variable-radius setback must be positive");
    auto Surface = BuildQuadraticVariableRadiusSurface(Specification);
    if (!Surface) return Deliver<BrepBody>::Reject(Surface.Denial.Reason, Surface.Denial.Detail);

    double MaximumCircumferential = 0.0;
    double MaximumMeridional = 0.0;
    for (int I = 0; I <= 32; ++I)
    {
        const double T = static_cast<double>(I) / 32.0;
        MaximumCircumferential = std::max(MaximumCircumferential, Surface.Payload.CircumferentialCurvature(T));
        MaximumMeridional = std::max(MaximumMeridional, std::fabs(Surface.Payload.MeridionalCurvature(T)));
    }
    std::string CurvatureRefusal;
    if (!ValidateQuadraticSurfaceCurvature(Surface.Payload, MaximumCircumferential + 1e-9,
                                            MaximumMeridional + 1e-9, CurvatureRefusal))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence,
                                          "nonlinear variable-radius curvature acceptance failed");

    const Vec3 Axis = Surface.Payload.Axis;
    const Vec3 U = (Surface.Payload.Radial - Axis * Surface.Payload.Radial.Dot(Axis)).Normalised();
    const Vec3 V = Axis.Cross(U).Normalised();
    auto Point = [&](double T, double AlongU, double AlongV)
    {
        return Specification.Origin + Axis * (Specification.Length * T) + U * AlongU + V * AlongV;
    };
    auto SectionLine = [&](double T, Vec3 A, Vec3 B) -> Deliver<NurbsCurve>
    {
        return NurbsCurve::Line(Point(T, A.X, A.Y), Point(T, B.X, B.Y));
    };
    auto SectionArc = [&](double T) -> Deliver<NurbsCurve>
    {
        const double Radius = Specification.RadiusLaw.Radius(T);
        const double Offset = Radius - Radius / std::sqrt(2.0);
        return NurbsCurve::ArcThreePoints(Point(T, 0.0, Radius),
                                          Point(T, Offset, Offset), Point(T, Radius, 0.0));
    };
    auto LoftThree = [&](const std::vector<Deliver<NurbsCurve>>& Sections) -> Deliver<NurbsSurface>
    {
        std::vector<NurbsCurve> Curves;
        Curves.reserve(Sections.size());
        for (const Deliver<NurbsCurve>& Section : Sections)
        {
            if (!Section) return Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput,
                                                                "nonlinear variable-radius section is degenerate");
            Curves.push_back(Section.Payload);
        }
        return NurbsSurface::Loft(Curves, 2);
    };
    auto Add = [&](const std::vector<Deliver<NurbsCurve>>& Sections, std::vector<NurbsSurface>& Surfaces) -> bool
    {
        Deliver<NurbsSurface> SurfacePart = LoftThree(Sections);
        if (!SurfacePart) return false;
        Surfaces.push_back(std::move(SurfacePart.Payload));
        return true;
    };

    const double R0 = Specification.RadiusLaw.Radius(0.0);
    const double Rm = Specification.RadiusLaw.Radius(0.5);
    const double R1 = Specification.RadiusLaw.Radius(1.0);
    const double D0 = R0 + Specification.Setback;
    const double Dm = Rm + Specification.Setback;
    const double D1 = R1 + Specification.Setback;
    const Vec3 A0{ R0, 0, 0 }, B0{ D0, 0, 0 }, C0{ D0, D0, 0 }, D0Point{ 0, D0, 0 }, E0{ 0, R0, 0 };
    const Vec3 Am{ Rm, 0, 0 }, Bm{ Dm, 0, 0 }, Cm{ Dm, Dm, 0 }, DmPoint{ 0, Dm, 0 }, Em{ 0, Rm, 0 };
    const Vec3 A1{ R1, 0, 0 }, B1{ D1, 0, 0 }, C1{ D1, D1, 0 }, D1Point{ 0, D1, 0 }, E1{ 0, R1, 0 };
    std::vector<NurbsSurface> Surfaces;
    if (!Add({ SectionLine(0.0, A0, B0), SectionLine(0.5, Am, Bm), SectionLine(1.0, A1, B1) }, Surfaces) ||
        !Add({ SectionLine(0.0, B0, C0), SectionLine(0.5, Bm, Cm), SectionLine(1.0, B1, C1) }, Surfaces) ||
        !Add({ SectionLine(0.0, C0, D0Point), SectionLine(0.5, Cm, DmPoint), SectionLine(1.0, C1, D1Point) }, Surfaces) ||
        !Add({ SectionLine(0.0, D0Point, E0), SectionLine(0.5, DmPoint, Em), SectionLine(1.0, D1Point, E1) }, Surfaces) ||
        !Add({ SectionArc(0.0), SectionArc(0.5), SectionArc(1.0) }, Surfaces))
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "nonlinear variable-radius surfaces could not be constructed");

    Deliver<BrepBody> Result = BrepBody::Sew(Surfaces, ScalarCriteria::MergeTolerance, true);
    if (!Result) return Result;
    const BodyReport Report = Result.Payload.Validate();
    const QuadraticRadiusLaw OuterLaw{ R0 + Specification.Setback, Rm + Specification.Setback,
                                       R1 + Specification.Setback };
    const double ExpectedVolume = OuterLaw.IntegratedSquare(Specification.Length) -
        (1.0 - ScalarCriteria::Pi / 4.0) * Specification.RadiusLaw.IntegratedSquare(Specification.Length);
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 ||
        Result.Payload.Vertices.size() != 10 || Result.Payload.Edges.size() != 15 ||
        Result.Payload.Coedges.size() != 30 || Result.Payload.Loops.size() != 7 || Result.Payload.Faces.size() != 7)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                                          "nonlinear variable-radius corner did not reach exact capped topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence,
                                          "nonlinear variable-radius volume failed analytic acceptance");
    return Result;
}

Deliver<NurbsCurve> BlendSolver::BuildG2CornerProfile(const G2PlanarCornerSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Radius) || !std::isfinite(Specification.Width) ||
        Specification.Radius <= ScalarCriteria::MergeTolerance ||
        Specification.Width <= Specification.Radius + ScalarCriteria::MergeTolerance)
        return Deliver<NurbsCurve>::Reject(RefusalReason::DegenerateInput,
                                            "G2 corner radius and width are not feasible");
    if (!std::isfinite(Specification.HandleFraction) || Specification.HandleFraction <= ScalarCriteria::GeometricTolerance ||
        Specification.HandleFraction >= 0.5 - ScalarCriteria::GeometricTolerance)
        return Deliver<NurbsCurve>::Reject(RefusalReason::DegenerateInput,
                                            "G2 corner handle fraction must lie strictly between zero and one half");
    const Vec3 Axis = Specification.EdgeAxis.Normalised();
    if (Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<NurbsCurve>::Reject(RefusalReason::DegenerateInput, "G2 corner edge axis is degenerate");
    Vec3 U = Axis.Cross(Vec3::UnitX());
    if (U.Length() <= ScalarCriteria::GeometricTolerance) U = Axis.Cross(Vec3::UnitY());
    if (U.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<NurbsCurve>::Reject(RefusalReason::DegenerateInput, "G2 corner frame is degenerate");
    U = U.Normalised();
    const Vec3 V = Axis.Cross(U).Normalised();
    const double R = Specification.Radius;
    const double H = R * Specification.HandleFraction;
    const auto Point = [&](double AlongU, double AlongV) { return Specification.Origin + U * AlongU + V * AlongV; };
    // E -> A: vertical tangent at E, horizontal tangent at A, and zero second derivative at both ends.
    return NurbsCurve::Bezier({ Point(0.0, R), Point(0.0, R - H), Point(0.0, R - 2.0 * H),
                                Point(R - 2.0 * H, 0.0), Point(R - H, 0.0), Point(R, 0.0) });
}

bool BlendSolver::ValidateG2CornerProfile(const NurbsCurve& Profile,
                                             const G2PlanarCornerSpecification& Specification,
                                             std::string& Refusal) noexcept
{
    if (Profile.Degree != 5 || Profile.PoleCount() != 6 || Profile.Validate())
    { Refusal = "G2 corner profile is not a valid quintic"; return false; }
    const Vec3 Axis = Specification.EdgeAxis.Normalised();
    Vec3 U = Axis.Cross(Vec3::UnitX());
    if (U.Length() <= ScalarCriteria::GeometricTolerance) U = Axis.Cross(Vec3::UnitY());
    if (U.Length() <= ScalarCriteria::GeometricTolerance)
    { Refusal = "G2 corner profile frame is degenerate"; return false; }
    U = U.Normalised();
    const Vec3 V = Axis.Cross(U).Normalised();
    const double CurvatureTolerance = 1e-8 / std::max(1.0, Specification.Radius);
    auto EndpointCurvature = [&](double T) noexcept
    {
        Vec3 Derivatives[3]; Profile.Derivatives(T, 2, Derivatives);
        const double Speed = Derivatives[1].Length();
        return Speed > ScalarCriteria::GeometricTolerance
            ? Derivatives[1].Cross(Derivatives[2]).Length() / (Speed * Speed * Speed) : ScalarCriteria::Infinity;
    };
    if (EndpointCurvature(0.0) > CurvatureTolerance || EndpointCurvature(1.0) > CurvatureTolerance)
    { Refusal = "G2 corner profile has non-zero curvature at a planar support join"; return false; }
    Vec3 StartDerivative[3], EndDerivative[3];
    Profile.Derivatives(0.0, 1, StartDerivative); Profile.Derivatives(1.0, 1, EndDerivative);
    if (StartDerivative[1].Normalised().Dot(-V) < 1.0 - ScalarCriteria::AngularTolerance ||
        EndDerivative[1].Normalised().Dot(U) < 1.0 - ScalarCriteria::AngularTolerance)
    { Refusal = "G2 corner profile endpoint tangents do not follow the supports"; return false; }
    for (int I = 0; I <= 32; ++I)
    {
        const Vec3 P = Profile.Sample(static_cast<double>(I) / 32.0) - Specification.Origin;
        const double X = P.Dot(U), Y = P.Dot(V);
        if (X < -ScalarCriteria::MergeTolerance || Y < -ScalarCriteria::MergeTolerance ||
            X > Specification.Radius + ScalarCriteria::MergeTolerance ||
            Y > Specification.Radius + ScalarCriteria::MergeTolerance)
        { Refusal = "G2 corner profile leaves its support quadrant"; return false; }
    }
    return true;
}

double BlendSolver::G2CornerRemovalArea(double Radius, double HandleFraction) noexcept
{
    const double H = Radius * HandleFraction;
    // Green's theorem on the quintic control polygon, reduced symbolically for this symmetric
    // control net. The sign is reversed from the E->A traversal, so this is the removed area.
    return 0.5 * Radius * Radius - (55.0 / 42.0) * Radius * H + (65.0 / 84.0) * H * H;
}

Deliver<BrepBody> BlendSolver::ReconstructG2PlanarCorner(const G2PlanarCornerSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Length) || Specification.Length <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "G2 corner length must be positive");
    auto Profile = BuildG2CornerProfile(Specification);
    if (!Profile) return Deliver<BrepBody>::Reject(Profile.Denial.Reason, Profile.Denial.Detail);
    std::string Refusal;
    if (!ValidateG2CornerProfile(Profile.Payload, Specification, Refusal))
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "G2 corner profile failed endpoint acceptance");
    const Vec3 Axis = Specification.EdgeAxis.Normalised();
    Vec3 U = Axis.Cross(Vec3::UnitX());
    if (U.Length() <= ScalarCriteria::GeometricTolerance) U = Axis.Cross(Vec3::UnitY());
    U = U.Normalised();
    const Vec3 V = Axis.Cross(U).Normalised();
    const double W = Specification.Width;
    auto Point = [&](double AlongU, double AlongV)
    {
        return Specification.Origin + U * AlongU + V * AlongV;
    };
    const double R = Specification.Radius;
    const Vec3 A{ R, 0, 0 }, B{ W, 0, 0 }, C{ W, W, 0 }, D{ 0, W, 0 }, E{ 0, R, 0 };
    std::vector<NurbsSurface> Surfaces;
    // The profile curve is already in world coordinates; the four straight boundaries are
    // represented directly as edge-axis extrusions.
    std::vector<NurbsCurve> Boundaries;
    auto MakeLine = [&](Vec3 P0, Vec3 P1) -> bool
    {
        Deliver<NurbsCurve> Curve = NurbsCurve::Line(P0, P1);
        if (!Curve) return false;
        Boundaries.push_back(std::move(Curve.Payload));
        return true;
    };
    if (!MakeLine(Point(A.X, A.Y), Point(B.X, B.Y)) ||
        !MakeLine(Point(B.X, B.Y), Point(C.X, C.Y)) ||
        !MakeLine(Point(C.X, C.Y), Point(D.X, D.Y)) ||
        !MakeLine(Point(D.X, D.Y), Point(E.X, E.Y)))
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "G2 corner support boundary is degenerate");
    for (const NurbsCurve& Boundary : Boundaries)
    {
        Deliver<NurbsSurface> Surface = NurbsSurface::Extrusion(Boundary, Axis, Specification.Length);
        if (!Surface) return Deliver<BrepBody>::Reject(Surface.Denial.Reason, "G2 corner support surface failed");
        Surfaces.push_back(std::move(Surface.Payload));
    }
    Deliver<NurbsSurface> Roll = NurbsSurface::Extrusion(Profile.Payload, Axis, Specification.Length);
    if (!Roll) return Deliver<BrepBody>::Reject(Roll.Denial.Reason, "G2 corner transition surface failed");
    Surfaces.push_back(std::move(Roll.Payload));

    Deliver<BrepBody> Result = BrepBody::Sew(Surfaces, ScalarCriteria::MergeTolerance, true);
    if (!Result) return Result;
    const BodyReport Report = Result.Payload.Validate();
    const double ExpectedVolume = Specification.Length *
        (Specification.Width * Specification.Width - G2CornerRemovalArea(Specification.Radius, Specification.HandleFraction));
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 ||
        Result.Payload.Vertices.size() != 10 || Result.Payload.Edges.size() != 15 ||
        Result.Payload.Coedges.size() != 30 || Result.Payload.Loops.size() != 7 || Result.Payload.Faces.size() != 7)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "G2 planar corner did not reach exact capped topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence, "G2 planar corner volume failed analytic acceptance");
    return Result;
}

Deliver<G2RollingBallProfile> BlendSolver::BuildG2RollingBallProfile(
    const G2RollingBallPlanarCornerSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Radius) || Specification.Radius <= ScalarCriteria::MergeTolerance ||
        !std::isfinite(Specification.Width) || Specification.Width <= Specification.Radius + ScalarCriteria::MergeTolerance)
        return Deliver<G2RollingBallProfile>::Reject(RefusalReason::DegenerateInput,
                                                      "G2 rolling-ball radius and width are not feasible");
    if (!std::isfinite(Specification.TransitionAngle) ||
        Specification.TransitionAngle <= 4.0 * ScalarCriteria::AngularTolerance ||
        Specification.TransitionAngle >= (ScalarCriteria::HalfPi * 0.5) - 4.0 * ScalarCriteria::AngularTolerance)
        return Deliver<G2RollingBallProfile>::Reject(RefusalReason::DegenerateInput,
                                                      "G2 rolling-ball transition angle must leave a circular core");
    const Vec3 Axis = Specification.EdgeAxis.Normalised();
    const Vec3 SupportA = Specification.SupportA.Normalised();
    const Vec3 SupportB = Specification.SupportB.Normalised();
    if (Axis.Length() <= ScalarCriteria::GeometricTolerance ||
        SupportA.Length() <= ScalarCriteria::GeometricTolerance ||
        SupportB.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<G2RollingBallProfile>::Reject(RefusalReason::DegenerateInput,
                                                      "G2 rolling-ball frame is degenerate");
    if (std::fabs(Axis.Dot(SupportA)) > ScalarCriteria::AngularTolerance ||
        std::fabs(Axis.Dot(SupportB)) > ScalarCriteria::AngularTolerance ||
        std::fabs(SupportA.Dot(SupportB)) > ScalarCriteria::AngularTolerance ||
        Axis.Dot(SupportA.Cross(SupportB)) <= ScalarCriteria::AngularTolerance)
        return Deliver<G2RollingBallProfile>::Reject(RefusalReason::Unsupported,
                                                      "G2 rolling-ball supports must be positively oriented and perpendicular");

    const Vec3 U = SupportA;
    const Vec3 V = SupportB;
    const double R = Specification.Radius;
    const double A = Specification.TransitionAngle;
    const Vec3 Centre = Specification.Origin + (U + V) * R;
    const auto Point = [&](double Angle, double Radial) noexcept
    {
        return Centre - U * (Radial * std::cos(Angle)) - V * (Radial * std::sin(Angle));
    };
    const Vec3 Start = Point(0.0, R);
    const Vec3 CoreStart = Point(A, R);
    const Vec3 CoreEnd = Point(ScalarCriteria::HalfPi - A, R);
    // A quintic Hermite piece starts with the support tangent and zero second derivative, then
    // arrives at the exact circle with the same tangent and normal curvature. Its radial law is
    // equivalent to r(u) = R * (1 + u²/2 * (1-u/A)^3) near the support, so it stays inside
    // the retained quadrant instead of producing a visually plausible but consuming overshoot.
    const double Speed = R * A;
    const Vec3 D0 = -V * Speed;
    const Vec3 D1 = (U * std::sin(A) - V * std::cos(A)) * Speed;
    const Vec3 D21 = (U * std::cos(A) + V * std::sin(A)) * (Speed * A);
    std::vector<Vec3> StartControls{
        Start,
        Start + D0 / 5.0,
        Start + D0 * (2.0 / 5.0),
        CoreStart - D1 * (2.0 / 5.0) + D21 / 20.0,
        CoreStart - D1 / 5.0,
        CoreStart };
    Deliver<NurbsCurve> StartTransition = NurbsCurve::Bezier(StartControls);
    if (!StartTransition)
        return Deliver<G2RollingBallProfile>::Reject(StartTransition.Denial.Reason,
                                                      "G2 rolling-ball start transition failed");

    std::vector<Vec3> EndControls;
    EndControls.reserve(StartControls.size());
    for (auto It = StartControls.rbegin(); It != StartControls.rend(); ++It)
    {
        const Vec3 Relative = *It - Specification.Origin;
        EndControls.push_back(Specification.Origin + U * Relative.Dot(V) + V * Relative.Dot(U));
    }
    // The support frame is orthonormal, so the reflected/reversed start controls are the
    // curvature-matched end transition without introducing a second independently tuned law.
    Deliver<NurbsCurve> EndTransition = NurbsCurve::Bezier(EndControls);
    if (!EndTransition)
        return Deliver<G2RollingBallProfile>::Reject(EndTransition.Denial.Reason,
                                                      "G2 rolling-ball end transition failed");
    Deliver<NurbsCurve> Core = NurbsCurve::ArcThreePoints(CoreStart, Point((ScalarCriteria::HalfPi * 0.5), R), CoreEnd);
    if (!Core)
        return Deliver<G2RollingBallProfile>::Reject(Core.Denial.Reason,
                                                      "G2 rolling-ball exact circular core failed");

    G2RollingBallProfile Result;
    Result.Pieces.reserve(3);
    Result.Pieces.push_back(std::move(StartTransition.Payload));
    Result.Pieces.push_back(std::move(Core.Payload));
    Result.Pieces.push_back(std::move(EndTransition.Payload));
    Result.Origin = Specification.Origin;
    Result.AxisU = U;
    Result.AxisV = V;
    Result.Radius = R;
    Result.TransitionAngle = A;
    return Deliver<G2RollingBallProfile>::Accept(std::move(Result));
}

bool BlendSolver::ValidateG2RollingBallProfile(const G2RollingBallProfile& Profile,
                                                 const G2RollingBallPlanarCornerSpecification& Specification,
                                                 std::string& Refusal) noexcept
{
    if (Profile.Pieces.size() != 3)
    { Refusal = "G2 rolling-ball profile does not have three sections"; return false; }
    for (const NurbsCurve& Piece : Profile.Pieces)
        if (Piece.Validate())
        { Refusal = "G2 rolling-ball profile contains an invalid section"; return false; }
    const double R = Specification.Radius;
    const double A = Specification.TransitionAngle;
    const Vec3 U = Profile.AxisU;
    const Vec3 V = Profile.AxisV;
    const Vec3 Centre = Specification.Origin + (U + V) * R;
    const auto Point = [&](double Angle) noexcept
    {
        return Centre - U * (R * std::cos(Angle)) - V * (R * std::sin(Angle));
    };
    const Vec3 CoreStart = Point(A);
    const Vec3 CoreEnd = Point(ScalarCriteria::HalfPi - A);
    const Vec3 End = Point(ScalarCriteria::HalfPi);
    const auto Coincident = [](const Vec3& Left, const Vec3& Right) noexcept
    {
        return Left.Distance(Right) <= ScalarCriteria::MergeTolerance;
    };
    if (!Coincident(Profile.Pieces[0].StartPoint(), Specification.Origin + V * R) ||
        !Coincident(Profile.Pieces[0].EndPoint(), CoreStart) ||
        !Coincident(Profile.Pieces[1].StartPoint(), CoreStart) ||
        !Coincident(Profile.Pieces[1].EndPoint(), CoreEnd) ||
        !Coincident(Profile.Pieces[2].StartPoint(), CoreEnd) ||
        !Coincident(Profile.Pieces[2].EndPoint(), End))
    { Refusal = "G2 rolling-ball profile sections do not share exact boundaries"; return false; }
    const double CurvatureTolerance = 2e-7 / std::max(1.0, R);
    const double SupportCurvature = Profile.Pieces[0].Curvature(0.0);
    const double StartCoreCurvature = Profile.Pieces[0].Curvature(1.0);
    const double ExactCoreCurvature = Profile.Pieces[1].Curvature(0.0);
    const double EndCoreCurvature = Profile.Pieces[2].Curvature(0.0);
    const double EndSupportCurvature = Profile.Pieces[2].Curvature(1.0);
    if (!std::isfinite(SupportCurvature) || !std::isfinite(StartCoreCurvature) ||
        !std::isfinite(ExactCoreCurvature) || !std::isfinite(EndCoreCurvature) || !std::isfinite(EndSupportCurvature) ||
        SupportCurvature > CurvatureTolerance || EndSupportCurvature > CurvatureTolerance ||
        std::fabs(StartCoreCurvature - 1.0 / R) > CurvatureTolerance ||
        std::fabs(ExactCoreCurvature - 1.0 / R) > CurvatureTolerance ||
        std::fabs(EndCoreCurvature - 1.0 / R) > CurvatureTolerance)
    { Refusal = "G2 rolling-ball profile fails support or circular-core curvature matching"; return false; }
    const Vec3 StartTangent = Profile.Pieces[0].Tangent(0.0);
    const Vec3 EndTangent = Profile.Pieces[2].Tangent(1.0);
    if (StartTangent.Dot(-V) < 1.0 - ScalarCriteria::AngularTolerance ||
        EndTangent.Dot(U) < 1.0 - ScalarCriteria::AngularTolerance)
    { Refusal = "G2 rolling-ball support tangents are not support-aligned"; return false; }
    if (!Profile.Pieces[1].Rational())
    { Refusal = "G2 rolling-ball core is not rational"; return false; }
    for (const NurbsCurve& Piece : Profile.Pieces)
        for (int I = 0; I <= 32; ++I)
        {
            const Vec3 P = Piece.Sample(static_cast<double>(I) / 32.0) - Specification.Origin;
            if (P.Dot(U) < -ScalarCriteria::MergeTolerance || P.Dot(V) < -ScalarCriteria::MergeTolerance ||
                P.Dot(U) > R + ScalarCriteria::MergeTolerance || P.Dot(V) > R + ScalarCriteria::MergeTolerance)
            { Refusal = "G2 rolling-ball transition leaves its retained quadrant"; return false; }
        }
    return true;
}

double BlendSolver::G2RollingBallRemovalArea(const G2RollingBallProfile& Profile) noexcept
{
    // Green's theorem over the exact NURBS profile pieces. The circular section is rational;
    // the fixed Gauss rule integrates each polynomial/rational section deterministically.
    constexpr double Nodes[8] = { -0.9602898564975363, -0.7966664774136267, -0.5255324099163290,
                                   -0.1834346424956498,  0.1834346424956498,  0.5255324099163290,
                                    0.7966664774136267,  0.9602898564975363 };
    constexpr double Weights[8] = { 0.1012285362903763, 0.2223810344533745, 0.3137066458778873,
                                     0.3626837833783620, 0.3626837833783620, 0.3137066458778873,
                                     0.2223810344533745, 0.1012285362903763 };
    double Integral = 0.0;
    for (const NurbsCurve& Piece : Profile.Pieces)
        for (int I = 0; I < 8; ++I)
        {
            const double T = 0.5 * (Nodes[I] + 1.0);
            Vec3 D[2];
            Piece.Derivatives(T, 1, D);
            const Vec3 Relative = D[0] - Profile.Origin;
            Integral += Weights[I] * 0.25 * (Relative.Dot(Profile.AxisU) * D[1].Dot(Profile.AxisV) -
                                              Relative.Dot(Profile.AxisV) * D[1].Dot(Profile.AxisU));
        }
    return -Integral;
}

Deliver<BrepBody> BlendSolver::ReconstructG2RollingBallPlanarCorner(
    const G2RollingBallPlanarCornerSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Length) || Specification.Length <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "G2 rolling-ball corner length must be positive");
    Deliver<G2RollingBallProfile> Profile = BuildG2RollingBallProfile(Specification);
    if (!Profile) return Deliver<BrepBody>::Reject(Profile.Denial.Reason, Profile.Denial.Detail);
    std::string Refusal;
    if (!ValidateG2RollingBallProfile(Profile.Payload, Specification, Refusal))
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "G2 rolling-ball profile failed acceptance");

    const Vec3 U = Profile.Payload.AxisU;
    const Vec3 V = Profile.Payload.AxisV;
    const double W = Specification.Width;
    const double R = Specification.Radius;
    const auto Point = [&](double AlongU, double AlongV) noexcept
    {
        return Specification.Origin + U * AlongU + V * AlongV;
    };
    const Vec3 A{ R, 0, 0 }, B{ W, 0, 0 }, C{ W, W, 0 }, D{ 0, W, 0 }, E{ 0, R, 0 };
    std::vector<NurbsSurface> Surfaces;
    const auto AddLineExtrusion = [&](Vec3 P0, Vec3 P1) noexcept -> bool
    {
        Deliver<NurbsCurve> Boundary = NurbsCurve::Line(P0, P1);
        if (!Boundary) return false;
        Deliver<NurbsSurface> Surface = NurbsSurface::Extrusion(Boundary.Payload, Specification.EdgeAxis.Normalised(), Specification.Length);
        if (!Surface) return false;
        Surfaces.push_back(std::move(Surface.Payload));
        return true;
    };
    if (!AddLineExtrusion(Point(A.X, A.Y), Point(B.X, B.Y)) ||
        !AddLineExtrusion(Point(B.X, B.Y), Point(C.X, C.Y)) ||
        !AddLineExtrusion(Point(C.X, C.Y), Point(D.X, D.Y)) ||
        !AddLineExtrusion(Point(D.X, D.Y), Point(E.X, E.Y)))
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "G2 rolling-ball support surface failed");
    for (const NurbsCurve& Piece : Profile.Payload.Pieces)
    {
        Deliver<NurbsSurface> Surface = NurbsSurface::Extrusion(Piece, Specification.EdgeAxis.Normalised(), Specification.Length);
        if (!Surface) return Deliver<BrepBody>::Reject(Surface.Denial.Reason, "G2 rolling-ball transition surface failed");
        Surfaces.push_back(std::move(Surface.Payload));
    }
    Deliver<BrepBody> Result = BrepBody::Sew(Surfaces, ScalarCriteria::MergeTolerance, true);
    if (!Result) return Result;
    const BodyReport Report = Result.Payload.Validate();
    const double RemovedArea = G2RollingBallRemovalArea(Profile.Payload);
    const double ExpectedVolume = Specification.Length * (W * W - RemovedArea);
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 ||
        Result.Payload.Vertices.size() != 14 || Result.Payload.Edges.size() != 21 ||
        Result.Payload.Coedges.size() != 42 || Result.Payload.Loops.size() != 9 || Result.Payload.Faces.size() != 9)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                                          "G2 rolling-ball corner did not reach exact capped topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence,
                                          "G2 rolling-ball volume failed profile line-integral acceptance");
    return Result;
}

Deliver<ObliqueG2RollingBallProfile> BlendSolver::BuildObliqueG2RollingBallProfile(
    const ObliqueG2RollingBallPlanarCornerSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Radius) || Specification.Radius <= ScalarCriteria::MergeTolerance ||
        !std::isfinite(Specification.WidthA) || !std::isfinite(Specification.WidthB) ||
        Specification.WidthA <= ScalarCriteria::MergeTolerance || Specification.WidthB <= ScalarCriteria::MergeTolerance)
        return Deliver<ObliqueG2RollingBallProfile>::Reject(RefusalReason::DegenerateInput,
                                                              "oblique G2 rolling-ball radius and widths are not feasible");
    const Vec3 Axis = Specification.EdgeAxis.Normalised();
    const Vec3 A = Specification.SupportA.Normalised();
    const Vec3 B = Specification.SupportB.Normalised();
    if (Axis.Length() <= ScalarCriteria::GeometricTolerance ||
        A.Length() <= ScalarCriteria::GeometricTolerance || B.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<ObliqueG2RollingBallProfile>::Reject(RefusalReason::DegenerateInput,
                                                              "oblique G2 rolling-ball frame is degenerate");
    if (std::fabs(Axis.Dot(A)) > ScalarCriteria::AngularTolerance ||
        std::fabs(Axis.Dot(B)) > ScalarCriteria::AngularTolerance)
        return Deliver<ObliqueG2RollingBallProfile>::Reject(RefusalReason::Unsupported,
                                                              "oblique G2 rolling-ball supports must be perpendicular to the edge");
    const double CosTheta = ScalarCriteria::Clamp(A.Dot(B), -1.0, 1.0);
    const double Theta = std::acos(CosTheta);
    const double Orientation = Axis.Dot(A.Cross(B));
    if (!std::isfinite(Theta) || Theta <= ScalarCriteria::AngularTolerance ||
        Theta >= ScalarCriteria::Pi - ScalarCriteria::AngularTolerance ||
        std::fabs(Orientation) <= ScalarCriteria::AngularTolerance)
        return Deliver<ObliqueG2RollingBallProfile>::Reject(RefusalReason::Unsupported,
                                                              "oblique G2 rolling-ball angle must be strict and non-reflex");
    if (Orientation < 0.0)
        return Deliver<ObliqueG2RollingBallProfile>::Reject(RefusalReason::Unsupported,
                                                              "oblique G2 rolling-ball support orientation is reversed");
    const double HalfTheta = Theta * 0.5;
    const double TangentDistance = Specification.Radius / std::tan(HalfTheta);
    const double CoreSweep = ScalarCriteria::Pi - Theta;
    if (!std::isfinite(Specification.TransitionAngle) ||
        Specification.TransitionAngle <= 4.0 * ScalarCriteria::AngularTolerance ||
        Specification.TransitionAngle >= CoreSweep * 0.5 - 4.0 * ScalarCriteria::AngularTolerance)
        return Deliver<ObliqueG2RollingBallProfile>::Reject(RefusalReason::DegenerateInput,
                                                              "oblique G2 rolling-ball transition leaves no circular core");
    if (TangentDistance >= Specification.WidthA - ScalarCriteria::MergeTolerance ||
        TangentDistance >= Specification.WidthB - ScalarCriteria::MergeTolerance)
        return Deliver<ObliqueG2RollingBallProfile>::Reject(RefusalReason::DegenerateInput,
                                                              "oblique G2 rolling-ball tangent distance consumes a support");

    const double R = Specification.Radius;
    const double Transition = Specification.TransitionAngle;
    const Vec3 Bisector = (A + B).Normalised();
    const Vec3 Centre = Specification.Origin + Bisector * (R / std::sin(HalfTheta));
    const Vec3 TangentA = Specification.Origin + A * TangentDistance;
    const Vec3 TangentB = Specification.Origin + B * TangentDistance;
    const Vec3 RadialA = (TangentA - Centre).Normalised();
    const auto RotatedRadial = [&](const Vec3& Radial, double Angle) noexcept
    {
        return (Radial * std::cos(Angle) - Axis.Cross(Radial) * std::sin(Angle)).Normalised();
    };
    const auto CirclePoint = [&](const Vec3& Radial) noexcept { return Centre + Radial * R; };
    const Vec3 CoreStartRadial = RotatedRadial(RadialA, Transition);
    const Vec3 CoreEndRadial = RotatedRadial(RadialA, CoreSweep - Transition);
    const Vec3 CoreStart = CirclePoint(CoreStartRadial);
    const Vec3 CoreEnd = CirclePoint(CoreEndRadial);
    const double Speed = R * Transition;
    const Vec3 StartTangent = (-Axis.Cross(RadialA)).Normalised();
    const Vec3 StartCoreTangent = (-Axis.Cross(CoreStartRadial)).Normalised();
    const Vec3 StartD0 = StartTangent * Speed;
    const Vec3 StartD1 = StartCoreTangent * Speed;
    const Vec3 StartD2 = -CoreStartRadial * (R * Transition * Transition);
    const std::vector<Vec3> StartControls{
        TangentA,
        TangentA + StartD0 / 5.0,
        TangentA + StartD0 * (2.0 / 5.0),
        CoreStart - StartD1 * (2.0 / 5.0) + StartD2 / 20.0,
        CoreStart - StartD1 / 5.0,
        CoreStart };
    Deliver<NurbsCurve> StartTransition = NurbsCurve::Bezier(StartControls);
    if (!StartTransition)
        return Deliver<ObliqueG2RollingBallProfile>::Reject(StartTransition.Denial.Reason,
                                                              "oblique G2 rolling-ball start transition failed");

    const Vec3 CoreEndTangent = (-Axis.Cross(CoreEndRadial)).Normalised();
    const Vec3 EndD0 = CoreEndTangent * Speed;
    const Vec3 EndD2 = -CoreEndRadial * (R * Transition * Transition);
    const Vec3 EndTangent = (-Axis.Cross((TangentB - Centre).Normalised())).Normalised();
    const std::vector<Vec3> EndControls{
        CoreEnd,
        CoreEnd + EndD0 / 5.0,
        CoreEnd + EndD0 * (2.0 / 5.0) + EndD2 / 20.0,
        TangentB - EndTangent * (2.0 * Speed / 5.0),
        TangentB - EndTangent * (Speed / 5.0),
        TangentB };
    Deliver<NurbsCurve> EndTransition = NurbsCurve::Bezier(EndControls);
    if (!EndTransition)
        return Deliver<ObliqueG2RollingBallProfile>::Reject(EndTransition.Denial.Reason,
                                                              "oblique G2 rolling-ball end transition failed");
    const Vec3 CoreMiddle = CirclePoint(RotatedRadial(RadialA, CoreSweep * 0.5));
    Deliver<NurbsCurve> Core = NurbsCurve::ArcThreePoints(CoreStart, CoreMiddle, CoreEnd);
    if (!Core)
        return Deliver<ObliqueG2RollingBallProfile>::Reject(Core.Denial.Reason,
                                                              "oblique G2 rolling-ball exact circular core failed");

    ObliqueG2RollingBallProfile Result;
    Result.Pieces.reserve(3);
    Result.Pieces.push_back(std::move(StartTransition.Payload));
    Result.Pieces.push_back(std::move(Core.Payload));
    Result.Pieces.push_back(std::move(EndTransition.Payload));
    Result.Origin = Specification.Origin;
    Result.EdgeAxis = Axis;
    Result.SupportA = A;
    Result.SupportB = B;
    Result.Centre = Centre;
    Result.Radius = R;
    Result.InteriorAngle = Theta;
    Result.TransitionAngle = Transition;
    return Deliver<ObliqueG2RollingBallProfile>::Accept(std::move(Result));
}

bool BlendSolver::ValidateObliqueG2RollingBallProfile(
    const ObliqueG2RollingBallProfile& Profile,
    const ObliqueG2RollingBallPlanarCornerSpecification& Specification,
    std::string& Refusal) noexcept
{
    if (Profile.Pieces.size() != 3)
    { Refusal = "oblique G2 rolling-ball profile does not have three sections"; return false; }
    for (const NurbsCurve& Piece : Profile.Pieces)
        if (Piece.Validate())
        { Refusal = "oblique G2 rolling-ball profile contains an invalid section"; return false; }
    const Vec3 Axis = Profile.EdgeAxis;
    const Vec3 A = Profile.SupportA;
    const Vec3 B = Profile.SupportB;
    const double Theta = Profile.InteriorAngle;
    const double HalfTheta = Theta * 0.5;
    const double D = Profile.Radius / std::tan(HalfTheta);
    const double Sweep = ScalarCriteria::Pi - Theta;
    const Vec3 TangentA = Specification.Origin + A * D;
    const Vec3 TangentB = Specification.Origin + B * D;
    const auto Coincident = [](const Vec3& Left, const Vec3& Right) noexcept
    {
        return Left.Distance(Right) <= ScalarCriteria::MergeTolerance;
    };
    if (!Coincident(Profile.Pieces[0].StartPoint(), TangentA) ||
        !Coincident(Profile.Pieces[0].EndPoint(), Profile.Pieces[1].StartPoint()) ||
        !Coincident(Profile.Pieces[1].EndPoint(), Profile.Pieces[2].StartPoint()) ||
        !Coincident(Profile.Pieces[2].EndPoint(), TangentB))
    { Refusal = "oblique G2 rolling-ball profile sections do not share exact boundaries"; return false; }
    const double CurvatureTolerance = 2e-7 / std::max(1.0, Profile.Radius);
    const double StartSupportCurvature = Profile.Pieces[0].Curvature(0.0);
    const double StartCoreCurvature = Profile.Pieces[0].Curvature(1.0);
    const double ExactCoreCurvature = Profile.Pieces[1].Curvature(0.5);
    const double EndCoreCurvature = Profile.Pieces[2].Curvature(0.0);
    const double EndSupportCurvature = Profile.Pieces[2].Curvature(1.0);
    if (!std::isfinite(StartSupportCurvature) || !std::isfinite(StartCoreCurvature) ||
        !std::isfinite(ExactCoreCurvature) || !std::isfinite(EndCoreCurvature) || !std::isfinite(EndSupportCurvature) ||
        StartSupportCurvature > CurvatureTolerance || EndSupportCurvature > CurvatureTolerance ||
        std::fabs(StartCoreCurvature - 1.0 / Profile.Radius) > CurvatureTolerance ||
        std::fabs(ExactCoreCurvature - 1.0 / Profile.Radius) > CurvatureTolerance ||
        std::fabs(EndCoreCurvature - 1.0 / Profile.Radius) > CurvatureTolerance)
    { Refusal = "oblique G2 rolling-ball profile fails curvature matching"; return false; }
    if (!Profile.Pieces[1].Rational())
    { Refusal = "oblique G2 rolling-ball core is not rational"; return false; }
    const Vec3 StartRadial = (TangentA - Profile.Centre).Normalised();
    const Vec3 CoreStartRadial = (Profile.Pieces[1].StartPoint() - Profile.Centre).Normalised();
    const Vec3 CoreEndRadial = (Profile.Pieces[1].EndPoint() - Profile.Centre).Normalised();
    if (std::fabs(CoreStartRadial.Dot(StartRadial) - std::cos(Profile.TransitionAngle)) > 2e-10 ||
        std::fabs(CoreEndRadial.Dot(StartRadial) - std::cos(Sweep - Profile.TransitionAngle)) > 2e-10)
    { Refusal = "oblique G2 rolling-ball core sweep is not exact"; return false; }
    const double Det = Axis.Dot(A.Cross(B));
    for (const NurbsCurve& Piece : Profile.Pieces)
        for (int I = 0; I <= 32; ++I)
        {
            const Vec3 Q = Piece.Sample(static_cast<double>(I) / 32.0) - Specification.Origin;
            const double Alpha = Axis.Dot(Q.Cross(B)) / Det;
            const double Beta = Axis.Dot(A.Cross(Q)) / Det;
            if (Alpha < -ScalarCriteria::MergeTolerance || Beta < -ScalarCriteria::MergeTolerance ||
                Alpha > Specification.WidthA + ScalarCriteria::MergeTolerance ||
                Beta > Specification.WidthB + ScalarCriteria::MergeTolerance)
            { Refusal = "oblique G2 rolling-ball transition leaves its finite wedge"; return false; }
        }
    return true;
}

double BlendSolver::ObliqueG2RollingBallRemovalArea(const ObliqueG2RollingBallProfile& Profile) noexcept
{
    constexpr double Nodes[8] = { -0.9602898564975363, -0.7966664774136267, -0.5255324099163290,
                                   -0.1834346424956498,  0.1834346424956498,  0.5255324099163290,
                                    0.7966664774136267,  0.9602898564975363 };
    constexpr double Weights[8] = { 0.1012285362903763, 0.2223810344533745, 0.3137066458778873,
                                     0.3626837833783620, 0.3626837833783620, 0.3137066458778873,
                                     0.2223810344533745, 0.1012285362903763 };
    double Integral = 0.0;
    for (const NurbsCurve& Piece : Profile.Pieces)
        for (int I = 0; I < 8; ++I)
        {
            const double T = 0.5 * (Nodes[I] + 1.0);
            Vec3 D[2];
            Piece.Derivatives(T, 1, D);
            Integral += Weights[I] * 0.25 * Profile.EdgeAxis.Dot((D[0] - Profile.Origin).Cross(D[1]));
        }
    // The profile runs from support A to support B, so its positive orientation bounds the removed corner.
    return Integral;
}

Deliver<BrepBody> BlendSolver::ReconstructObliqueG2RollingBallPlanarCorner(
    const ObliqueG2RollingBallPlanarCornerSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Length) || Specification.Length <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "oblique G2 rolling-ball length must be positive");
    Deliver<ObliqueG2RollingBallProfile> Profile = BuildObliqueG2RollingBallProfile(Specification);
    if (!Profile) return Deliver<BrepBody>::Reject(Profile.Denial.Reason, Profile.Denial.Detail);
    std::string Refusal;
    if (!ValidateObliqueG2RollingBallProfile(Profile.Payload, Specification, Refusal))
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "oblique G2 rolling-ball profile failed acceptance");
    const Vec3 A = Profile.Payload.SupportA;
    const Vec3 B = Profile.Payload.SupportB;
    const double Theta = Profile.Payload.InteriorAngle;
    const double D = Specification.Radius / std::tan(Theta * 0.5);
    const auto Point = [&](const Vec3& Direction, double Distance) noexcept
    {
        return Specification.Origin + Direction * Distance;
    };
    const Vec3 TangentA = Point(A, D);
    const Vec3 OuterA = Point(A, Specification.WidthA);
    const Vec3 OuterB = Point(B, Specification.WidthB);
    const Vec3 TangentB = Point(B, D);
    const auto AddLineExtrusion = [&](Vec3 P0, Vec3 P1, std::vector<NurbsSurface>& Surfaces) noexcept -> bool
    {
        Deliver<NurbsCurve> Boundary = NurbsCurve::Line(P0, P1);
        if (!Boundary) return false;
        Deliver<NurbsSurface> Surface = NurbsSurface::Extrusion(Boundary.Payload, Profile.Payload.EdgeAxis, Specification.Length);
        if (!Surface) return false;
        Surfaces.push_back(std::move(Surface.Payload));
        return true;
    };
    std::vector<NurbsSurface> Surfaces;
    if (!AddLineExtrusion(TangentA, OuterA, Surfaces) ||
        !AddLineExtrusion(OuterA, OuterB, Surfaces) ||
        !AddLineExtrusion(OuterB, TangentB, Surfaces))
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "oblique G2 rolling-ball support surface failed");
    for (const NurbsCurve& Piece : Profile.Payload.Pieces)
    {
        Deliver<NurbsSurface> Surface = NurbsSurface::Extrusion(Piece, Profile.Payload.EdgeAxis, Specification.Length);
        if (!Surface) return Deliver<BrepBody>::Reject(Surface.Denial.Reason, "oblique G2 rolling-ball transition surface failed");
        Surfaces.push_back(std::move(Surface.Payload));
    }
    Deliver<BrepBody> Result = BrepBody::Sew(Surfaces, ScalarCriteria::MergeTolerance, true);
    if (!Result) return Result;
    const BodyReport Report = Result.Payload.Validate();
    const double SharpArea = 0.5 * Specification.WidthA * Specification.WidthB * std::sin(Theta);
    const double ExpectedVolume = Specification.Length * (SharpArea - ObliqueG2RollingBallRemovalArea(Profile.Payload));
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 ||
        Result.Payload.Vertices.size() != 12 || Result.Payload.Edges.size() != 18 ||
        Result.Payload.Coedges.size() != 36 || Result.Payload.Loops.size() != 8 || Result.Payload.Faces.size() != 8)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                                          "oblique G2 rolling-ball corner did not reach exact capped topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence,
                                          "oblique G2 rolling-ball volume failed profile line-integral acceptance");
    return Result;
}

Deliver<BrepBody> BlendSolver::ReconstructVariableG2RollingBallPlanarCorner(
    const VariableG2RollingBallPlanarCornerSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Length) || Specification.Length <= ScalarCriteria::MergeTolerance ||
        !std::isfinite(Specification.Width) || Specification.Width <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "variable G2 rolling-ball dimensions must be finite and positive");
    if (!Specification.RadiusLaw.Positive() || !Specification.RadiusLaw.Nonlinear())
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "variable G2 rolling-ball route requires a genuinely nonlinear positive radius law");
    if (Specification.EdgeAxis.Length() <= ScalarCriteria::GeometricTolerance ||
        Specification.SupportA.Length() <= ScalarCriteria::GeometricTolerance ||
        Specification.SupportB.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "variable G2 rolling-ball frame is degenerate");
    const Vec3 Axis = Specification.EdgeAxis.Normalised();
    const Vec3 SupportA = Specification.SupportA.Normalised();
    const Vec3 SupportB = Specification.SupportB.Normalised();
    if (std::fabs(Axis.Dot(SupportA)) > ScalarCriteria::AngularTolerance ||
        std::fabs(Axis.Dot(SupportB)) > ScalarCriteria::AngularTolerance ||
        std::fabs(SupportA.Dot(SupportB)) > ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "variable G2 rolling-ball supports must be perpendicular to the edge and each other");
    if (!std::isfinite(Specification.TransitionAngle) ||
        Specification.TransitionAngle <= 4.0 * ScalarCriteria::AngularTolerance ||
        Specification.TransitionAngle >= (ScalarCriteria::HalfPi * 0.5) - 4.0 * ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "variable G2 rolling-ball transition angle leaves no circular core");
    for (int I = 0; I <= 64; ++I)
    {
        const double T = static_cast<double>(I) / 64.0;
        const double Radius = Specification.RadiusLaw.Radius(T);
        if (!std::isfinite(Radius) || Radius <= ScalarCriteria::MergeTolerance ||
            Radius >= Specification.Width - ScalarCriteria::MergeTolerance)
            return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                              "variable G2 rolling-ball radius consumes a finite support");
    }

    const auto MakeSection = [&](double Along, double Radius) -> Deliver<G2RollingBallProfile>
    {
        G2RollingBallPlanarCornerSpecification Section;
        Section.Origin = Specification.Origin + Axis * Along;
        Section.EdgeAxis = Axis;
        Section.SupportA = SupportA;
        Section.SupportB = SupportB;
        Section.Length = 1.0;
        Section.Width = Specification.Width;
        Section.Radius = Radius;
        Section.TransitionAngle = Specification.TransitionAngle;
        return BuildG2RollingBallProfile(Section);
    };
    const double R0 = Specification.RadiusLaw.Radius(0.0);
    const double Rm = Specification.RadiusLaw.Radius(0.5);
    const double R1 = Specification.RadiusLaw.Radius(1.0);
    Deliver<G2RollingBallProfile> S0 = MakeSection(0.0, R0);
    Deliver<G2RollingBallProfile> Sm = MakeSection(Specification.Length * 0.5, Rm);
    Deliver<G2RollingBallProfile> S1 = MakeSection(Specification.Length, R1);
    if (!S0 || !Sm || !S1)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "variable G2 rolling-ball station profile is degenerate");

    const auto StationSurface = [&](std::vector<NurbsCurve> Rows) -> Deliver<NurbsSurface>
    {
        if (Rows.size() != 3) return Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput,
                                                                    "variable G2 rolling-ball requires three stations");
        for (NurbsCurve& Row : Rows)
        {
            if (Row.Degree < 2) Row = Row.Elevated(2);
            Row = Row.Reparameterised(0.0, 1.0);
        }
        const int CountU = Rows.front().PoleCount();
        for (const NurbsCurve& Row : Rows)
            if (Row.PoleCount() != CountU || Row.Knots != Rows.front().Knots)
                return Deliver<NurbsSurface>::Reject(RefusalReason::NoConvergence,
                                                      "variable G2 rolling-ball station curves are incompatible");
        std::vector<Vec4> Poles(static_cast<size_t>(CountU) * 3);
        for (int I = 0; I < CountU; ++I)
        {
            Poles[static_cast<size_t>(I) * 3] = Rows[0].Poles[I];
            Poles[static_cast<size_t>(I) * 3 + 1] = Rows[1].Poles[I] * 2.0 -
                (Rows[0].Poles[I] + Rows[2].Poles[I]) * 0.5;
            Poles[static_cast<size_t>(I) * 3 + 2] = Rows[2].Poles[I];
        }
        Deliver<NurbsSurface> Surface = NurbsSurface::Build(Rows.front().Degree, 2, CountU, 3,
                                                              std::move(Poles), Rows.front().Knots,
                                                              { 0, 0, 0, 1, 1, 1 });
        if (Surface) Surface.Payload.Classification = SurfaceClassification::Loft;
        return Surface;
    };
    const auto OuterPoint = [&](double Along, const Vec3& Direction, double Distance) noexcept
    {
        return Specification.Origin + Axis * Along + Direction * Distance;
    };
    std::vector<NurbsSurface> Surfaces;
    const auto Add = [&](std::vector<NurbsCurve> Rows) noexcept -> bool
    {
        Deliver<NurbsSurface> Surface = StationSurface(std::move(Rows));
        if (!Surface) return false;
        Surfaces.push_back(std::move(Surface.Payload));
        return true;
    };
    const Vec3 StartB = S0.Payload.Pieces[0].StartPoint();
    const Vec3 MidB = Sm.Payload.Pieces[0].StartPoint();
    const Vec3 EndB = S1.Payload.Pieces[0].StartPoint();
    const Vec3 StartA = S0.Payload.Pieces[2].EndPoint();
    const Vec3 MidA = Sm.Payload.Pieces[2].EndPoint();
    const Vec3 EndA = S1.Payload.Pieces[2].EndPoint();
    const auto Line = [](Vec3 P0, Vec3 P1) -> Deliver<NurbsCurve> { return NurbsCurve::Line(P0, P1); };
    const auto Corner = [&](double Along) noexcept
    {
        return OuterPoint(Along, SupportA, Specification.Width) + SupportB * Specification.Width;
    };
    Deliver<NurbsCurve> A0 = Line(StartA, OuterPoint(0.0, SupportA, Specification.Width));
    Deliver<NurbsCurve> Am = Line(MidA, OuterPoint(Specification.Length * 0.5, SupportA, Specification.Width));
    Deliver<NurbsCurve> A1 = Line(EndA, OuterPoint(Specification.Length, SupportA, Specification.Width));
    Deliver<NurbsCurve> O0 = Line(OuterPoint(0.0, SupportA, Specification.Width), Corner(0.0));
    Deliver<NurbsCurve> Om = Line(OuterPoint(Specification.Length * 0.5, SupportA, Specification.Width), Corner(Specification.Length * 0.5));
    Deliver<NurbsCurve> O1 = Line(OuterPoint(Specification.Length, SupportA, Specification.Width), Corner(Specification.Length));
    Deliver<NurbsCurve> C0 = Line(Corner(0.0), OuterPoint(0.0, SupportB, Specification.Width));
    Deliver<NurbsCurve> Cm = Line(Corner(Specification.Length * 0.5), OuterPoint(Specification.Length * 0.5, SupportB, Specification.Width));
    Deliver<NurbsCurve> C1 = Line(Corner(Specification.Length), OuterPoint(Specification.Length, SupportB, Specification.Width));
    Deliver<NurbsCurve> B0 = Line(OuterPoint(0.0, SupportB, Specification.Width), StartB);
    Deliver<NurbsCurve> Bm = Line(OuterPoint(Specification.Length * 0.5, SupportB, Specification.Width), MidB);
    Deliver<NurbsCurve> B1 = Line(OuterPoint(Specification.Length, SupportB, Specification.Width), EndB);
    if (!A0 || !Am || !A1 || !O0 || !Om || !O1 || !C0 || !Cm || !C1 || !B0 || !Bm || !B1 ||
        !Add({ A0.Payload, Am.Payload, A1.Payload }) ||
        !Add({ O0.Payload, Om.Payload, O1.Payload }) ||
        !Add({ C0.Payload, Cm.Payload, C1.Payload }) ||
        !Add({ B0.Payload, Bm.Payload, B1.Payload }) ||
        !Add({ S0.Payload.Pieces[0], Sm.Payload.Pieces[0], S1.Payload.Pieces[0] }) ||
        !Add({ S0.Payload.Pieces[1], Sm.Payload.Pieces[1], S1.Payload.Pieces[1] }) ||
        !Add({ S0.Payload.Pieces[2], Sm.Payload.Pieces[2], S1.Payload.Pieces[2] }))
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "variable G2 rolling-ball surfaces are degenerate");

    Deliver<BrepBody> Result = BrepBody::Sew(Surfaces, ScalarCriteria::MergeTolerance, true);
    if (!Result) return Result;
    G2RollingBallPlanarCornerSpecification Unit;
    Unit.Origin = Specification.Origin;
    Unit.EdgeAxis = Axis;
    Unit.SupportA = SupportA;
    Unit.SupportB = SupportB;
    Unit.Length = 1.0;
    Unit.Width = Specification.Width;
    Unit.Radius = 1.0;
    Unit.TransitionAngle = Specification.TransitionAngle;
    Deliver<G2RollingBallProfile> UnitProfile = BuildG2RollingBallProfile(Unit);
    if (!UnitProfile) return Deliver<BrepBody>::Reject(UnitProfile.Denial.Reason, "variable G2 unit profile failed");
    const double RemovedCoefficient = G2RollingBallRemovalArea(UnitProfile.Payload);
    const double ExpectedVolume = Specification.Length * Specification.Width * Specification.Width -
        RemovedCoefficient * Specification.RadiusLaw.IntegratedSquare(Specification.Length);
    const BodyReport Report = Result.Payload.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 ||
        Result.Payload.Vertices.size() != 14 || Result.Payload.Edges.size() != 21 ||
        Result.Payload.Coedges.size() != 42 || Result.Payload.Loops.size() != 9 || Result.Payload.Faces.size() != 9)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                                          "variable G2 rolling-ball did not reach exact capped topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence,
                                          "variable G2 rolling-ball volume failed integrated radius-square acceptance");
    return Result;
}

Deliver<BrepBody> BlendSolver::ReconstructObliqueVariableG2RollingBallPlanarCorner(
    const ObliqueVariableG2RollingBallPlanarCornerSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Length) || Specification.Length <= ScalarCriteria::MergeTolerance ||
        !std::isfinite(Specification.WidthA) || !std::isfinite(Specification.WidthB) ||
        Specification.WidthA <= ScalarCriteria::MergeTolerance || Specification.WidthB <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "oblique variable G2 dimensions must be finite and positive");
    if (!Specification.RadiusLaw.Positive() || !Specification.RadiusLaw.Nonlinear())
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "oblique variable G2 route requires a genuinely nonlinear positive radius law");
    if (Specification.EdgeAxis.Length() <= ScalarCriteria::GeometricTolerance ||
        Specification.SupportA.Length() <= ScalarCriteria::GeometricTolerance ||
        Specification.SupportB.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "oblique variable G2 frame is degenerate");
    const Vec3 Axis = Specification.EdgeAxis.Normalised();
    const Vec3 SupportA = Specification.SupportA.Normalised();
    const Vec3 SupportB = Specification.SupportB.Normalised();
    if (std::fabs(Axis.Dot(SupportA)) > ScalarCriteria::AngularTolerance ||
        std::fabs(Axis.Dot(SupportB)) > ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "oblique variable G2 supports must be perpendicular to the edge");
    const double Theta = std::acos(ScalarCriteria::Clamp(SupportA.Dot(SupportB), -1.0, 1.0));
    if (Theta <= ScalarCriteria::AngularTolerance || Theta >= ScalarCriteria::Pi - ScalarCriteria::AngularTolerance ||
        Axis.Dot(SupportA.Cross(SupportB)) <= ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "oblique variable G2 angle must be strict and positively oriented");
    const double HalfTheta = Theta * 0.5;
    const double CotHalf = std::cos(HalfTheta) / std::sin(HalfTheta);
    const double CoreSweep = ScalarCriteria::Pi - Theta;
    if (!std::isfinite(Specification.TransitionAngle) ||
        Specification.TransitionAngle <= 4.0 * ScalarCriteria::AngularTolerance ||
        Specification.TransitionAngle >= CoreSweep * 0.5 - 4.0 * ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "oblique variable G2 transition leaves no circular core");
    for (int I = 0; I <= 64; ++I)
    {
        const double T = static_cast<double>(I) / 64.0;
        const double Radius = Specification.RadiusLaw.Radius(T);
        const double TangentDistance = Radius * CotHalf;
        if (!std::isfinite(Radius) || Radius <= ScalarCriteria::MergeTolerance ||
            TangentDistance >= Specification.WidthA - ScalarCriteria::MergeTolerance ||
            TangentDistance >= Specification.WidthB - ScalarCriteria::MergeTolerance)
            return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                              "oblique variable G2 radius consumes a finite support");
    }
    const auto MakeSection = [&](double Along, double Radius) -> Deliver<ObliqueG2RollingBallProfile>
    {
        ObliqueG2RollingBallPlanarCornerSpecification Section;
        Section.Origin = Specification.Origin + Axis * Along;
        Section.EdgeAxis = Axis;
        Section.SupportA = SupportA;
        Section.SupportB = SupportB;
        Section.Length = 1.0;
        Section.WidthA = Specification.WidthA;
        Section.WidthB = Specification.WidthB;
        Section.Radius = Radius;
        Section.TransitionAngle = Specification.TransitionAngle;
        return BuildObliqueG2RollingBallProfile(Section);
    };
    Deliver<ObliqueG2RollingBallProfile> S0 = MakeSection(0.0, Specification.RadiusLaw.Radius(0.0));
    Deliver<ObliqueG2RollingBallProfile> Sm = MakeSection(Specification.Length * 0.5, Specification.RadiusLaw.Radius(0.5));
    Deliver<ObliqueG2RollingBallProfile> S1 = MakeSection(Specification.Length, Specification.RadiusLaw.Radius(1.0));
    if (!S0 || !Sm || !S1)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "oblique variable G2 station profile is degenerate");

    const auto StationSurface = [&](std::vector<NurbsCurve> Rows) -> Deliver<NurbsSurface>
    {
        if (Rows.size() != 3) return Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput,
                                                                    "oblique variable G2 requires three stations");
        for (NurbsCurve& Row : Rows)
        {
            if (Row.Degree < 2) Row = Row.Elevated(2);
            Row = Row.Reparameterised(0.0, 1.0);
        }
        const int CountU = Rows.front().PoleCount();
        for (const NurbsCurve& Row : Rows)
            if (Row.PoleCount() != CountU || Row.Knots != Rows.front().Knots)
                return Deliver<NurbsSurface>::Reject(RefusalReason::NoConvergence,
                                                      "oblique variable G2 station curves are incompatible");
        std::vector<Vec4> Poles(static_cast<size_t>(CountU) * 3);
        for (int I = 0; I < CountU; ++I)
        {
            Poles[static_cast<size_t>(I) * 3] = Rows[0].Poles[I];
            Poles[static_cast<size_t>(I) * 3 + 1] = Rows[1].Poles[I] * 2.0 -
                (Rows[0].Poles[I] + Rows[2].Poles[I]) * 0.5;
            Poles[static_cast<size_t>(I) * 3 + 2] = Rows[2].Poles[I];
        }
        Deliver<NurbsSurface> Surface = NurbsSurface::Build(Rows.front().Degree, 2, CountU, 3,
                                                              std::move(Poles), Rows.front().Knots,
                                                              { 0, 0, 0, 1, 1, 1 });
        if (Surface) Surface.Payload.Classification = SurfaceClassification::Loft;
        return Surface;
    };
    const auto Point = [&](double Along, const Vec3& Direction, double Distance) noexcept
    {
        return Specification.Origin + Axis * Along + Direction * Distance;
    };
    const auto Add = [&](std::vector<NurbsCurve> Rows, std::vector<NurbsSurface>& Surfaces) noexcept -> bool
    {
        Deliver<NurbsSurface> Surface = StationSurface(std::move(Rows));
        if (!Surface) return false;
        Surfaces.push_back(std::move(Surface.Payload));
        return true;
    };
    const Vec3 S0A = S0.Payload.Pieces[0].StartPoint();
    const Vec3 SmA = Sm.Payload.Pieces[0].StartPoint();
    const Vec3 S1A = S1.Payload.Pieces[0].StartPoint();
    const Vec3 S0B = S0.Payload.Pieces[2].EndPoint();
    const Vec3 SmB = Sm.Payload.Pieces[2].EndPoint();
    const Vec3 S1B = S1.Payload.Pieces[2].EndPoint();
    const auto Line = [](Vec3 P0, Vec3 P1) -> Deliver<NurbsCurve> { return NurbsCurve::Line(P0, P1); };
    Deliver<NurbsCurve> A0 = Line(S0A, Point(0.0, SupportA, Specification.WidthA));
    Deliver<NurbsCurve> Am = Line(SmA, Point(Specification.Length * 0.5, SupportA, Specification.WidthA));
    Deliver<NurbsCurve> A1 = Line(S1A, Point(Specification.Length, SupportA, Specification.WidthA));
    Deliver<NurbsCurve> O0 = Line(Point(0.0, SupportA, Specification.WidthA), Point(0.0, SupportB, Specification.WidthB));
    Deliver<NurbsCurve> Om = Line(Point(Specification.Length * 0.5, SupportA, Specification.WidthA), Point(Specification.Length * 0.5, SupportB, Specification.WidthB));
    Deliver<NurbsCurve> O1 = Line(Point(Specification.Length, SupportA, Specification.WidthA), Point(Specification.Length, SupportB, Specification.WidthB));
    Deliver<NurbsCurve> B0 = Line(Point(0.0, SupportB, Specification.WidthB), S0B);
    Deliver<NurbsCurve> Bm = Line(Point(Specification.Length * 0.5, SupportB, Specification.WidthB), SmB);
    Deliver<NurbsCurve> B1 = Line(Point(Specification.Length, SupportB, Specification.WidthB), S1B);
    std::vector<NurbsSurface> Surfaces;
    if (!A0 || !Am || !A1 || !O0 || !Om || !O1 || !B0 || !Bm || !B1 ||
        !Add({ A0.Payload, Am.Payload, A1.Payload }, Surfaces) ||
        !Add({ O0.Payload, Om.Payload, O1.Payload }, Surfaces) ||
        !Add({ B0.Payload, Bm.Payload, B1.Payload }, Surfaces) ||
        !Add({ S0.Payload.Pieces[0], Sm.Payload.Pieces[0], S1.Payload.Pieces[0] }, Surfaces) ||
        !Add({ S0.Payload.Pieces[1], Sm.Payload.Pieces[1], S1.Payload.Pieces[1] }, Surfaces) ||
        !Add({ S0.Payload.Pieces[2], Sm.Payload.Pieces[2], S1.Payload.Pieces[2] }, Surfaces))
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "oblique variable G2 surfaces are degenerate");
    Deliver<BrepBody> Result = BrepBody::Sew(Surfaces, ScalarCriteria::MergeTolerance, true);
    if (!Result) return Result;
    ObliqueG2RollingBallPlanarCornerSpecification Unit;
    Unit.Origin = Specification.Origin;
    Unit.EdgeAxis = Axis;
    Unit.SupportA = SupportA;
    Unit.SupportB = SupportB;
    Unit.Length = 1.0;
    Unit.WidthA = Specification.WidthA;
    Unit.WidthB = Specification.WidthB;
    Unit.Radius = 1.0;
    Unit.TransitionAngle = Specification.TransitionAngle;
    Deliver<ObliqueG2RollingBallProfile> UnitProfile = BuildObliqueG2RollingBallProfile(Unit);
    if (!UnitProfile) return Deliver<BrepBody>::Reject(UnitProfile.Denial.Reason, "oblique variable G2 unit profile failed");
    const double RemovedCoefficient = ObliqueG2RollingBallRemovalArea(UnitProfile.Payload);
    const double SharpArea = 0.5 * Specification.WidthA * Specification.WidthB * std::sin(Theta);
    const double ExpectedVolume = Specification.Length * SharpArea -
        RemovedCoefficient * Specification.RadiusLaw.IntegratedSquare(Specification.Length);
    const BodyReport Report = Result.Payload.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 ||
        Result.Payload.Vertices.size() != 12 || Result.Payload.Edges.size() != 18 ||
        Result.Payload.Coedges.size() != 36 || Result.Payload.Loops.size() != 8 || Result.Payload.Faces.size() != 8)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                                          "oblique variable G2 did not reach exact capped topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence,
                                          "oblique variable G2 volume failed integrated radius-square acceptance");
    return Result;
}

Deliver<G2RollingBallPlanarCornerSpecification> BlendSolver::ClassifyG2RollingBallEdge(
    const BrepBody& Body, int Edge, double Radius, double TransitionAngle) noexcept
{
    if (!std::isfinite(Radius) || Radius <= ScalarCriteria::MergeTolerance ||
        !std::isfinite(TransitionAngle) || TransitionAngle <= 0.0)
        return Deliver<G2RollingBallPlanarCornerSpecification>::Reject(RefusalReason::DegenerateInput,
                                                                         "selected G2 edge radius or transition is invalid");
    const BodyReport BodyReportValue = Body.Validate();
    if (!BodyReportValue.Solid())
        return Deliver<G2RollingBallPlanarCornerSpecification>::Reject(RefusalReason::NonManifold,
                                                                         "selected G2 source is not a closed manifold B-rep");
    EdgeCornerFrame FrameResult;
    std::string Refusal;
    if (!Frame(Body, Edge, FrameResult, Refusal))
        return Deliver<G2RollingBallPlanarCornerSpecification>::Reject(RefusalReason::Unsupported,
                                                                         "selected edge is outside the bounded G2 dispatch");
    if (std::fabs(FrameResult.Dihedral - ScalarCriteria::HalfPi) > ScalarCriteria::AngularTolerance)
        return Deliver<G2RollingBallPlanarCornerSpecification>::Reject(RefusalReason::Unsupported,
                                                                         "selected edge is not a strict orthogonal corner");
    const auto IsRectangularPlanarFace = [&](int Face) noexcept
    {
        if (Face < 0 || Face >= static_cast<int>(Body.Faces.size()) || Body.Faces[Face].Loops.size() != 1)
            return false;
        const int Loop = Body.Faces[Face].Loops.front();
        if (Loop < 0 || Loop >= static_cast<int>(Body.Loops.size()) || Body.Loops[Loop].Coedges.size() != 4)
            return false;
        std::vector<int> Boundary;
        Boundary.reserve(4);
        int PreviousEnd = -1;
        for (int Coedge : Body.Loops[Loop].Coedges)
        {
            if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return false;
            const BrepCoedge& Use = Body.Coedges[Coedge];
            if (Use.Edge < 0 || Use.Edge >= static_cast<int>(Body.Edges.size())) return false;
            const BrepEdge& BoundaryEdge = Body.Edges[Use.Edge];
            if (BoundaryEdge.Closed() || BoundaryEdge.Curve.Classification != CurveClassification::Line) return false;
            const int Start = Use.Reversed ? BoundaryEdge.VertexEnd : BoundaryEdge.VertexStart;
            const int End = Use.Reversed ? BoundaryEdge.VertexStart : BoundaryEdge.VertexEnd;
            if (Start < 0 || Start >= static_cast<int>(Body.Vertices.size()) ||
                End < 0 || End >= static_cast<int>(Body.Vertices.size())) return false;
            if (PreviousEnd >= 0 && PreviousEnd != Start) return false;
            Boundary.push_back(Start);
            PreviousEnd = End;
            if (Boundary.size() == 4 && End != Boundary.front()) return false;
        }
        for (size_t I = 0; I < Boundary.size(); ++I)
        {
            const Vec3 Here = Body.Vertices[Boundary[I]].Point;
            const Vec3 Previous = Body.Vertices[Boundary[(I + 3) % 4]].Point - Here;
            const Vec3 Next = Body.Vertices[Boundary[(I + 1) % 4]].Point - Here;
            if (Previous.Length() <= ScalarCriteria::MergeTolerance || Next.Length() <= ScalarCriteria::MergeTolerance ||
                std::fabs(Previous.Normalised().Dot(Next.Normalised())) > ScalarCriteria::AngularTolerance)
                return false;
        }
        return true;
    };
    if (!IsRectangularPlanarFace(FrameResult.FaceA) || !IsRectangularPlanarFace(FrameResult.FaceB))
        return Deliver<G2RollingBallPlanarCornerSpecification>::Reject(RefusalReason::Unsupported,
                                                                         "selected corner faces are not planar rectangles");
    const auto FaceWidth = [&](int Face, const Vec3& Direction) noexcept
    {
        if (Face < 0 || Face >= static_cast<int>(Body.Faces.size()) || Body.Faces[Face].Loops.size() != 1)
            return 0.0;
        std::vector<int> Vertices;
        const int Loop = Body.Faces[Face].Loops.front();
        if (Loop < 0 || Loop >= static_cast<int>(Body.Loops.size())) return 0.0;
        for (int Coedge : Body.Loops[Loop].Coedges)
        {
            if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return 0.0;
            const int EdgeIndex = Body.Coedges[Coedge].Edge;
            if (EdgeIndex < 0 || EdgeIndex >= static_cast<int>(Body.Edges.size())) return 0.0;
            const int Candidate[] = { Body.Edges[EdgeIndex].VertexStart, Body.Edges[EdgeIndex].VertexEnd };
            for (int Vertex : Candidate)
                if (std::find(Vertices.begin(), Vertices.end(), Vertex) == Vertices.end()) Vertices.push_back(Vertex);
        }
        if (Vertices.size() != 4) return 0.0;
        double Width = 0.0;
        for (int Vertex : Vertices)
        {
            if (Vertex < 0 || Vertex >= static_cast<int>(Body.Vertices.size())) return 0.0;
            Width = std::max(Width, (Body.Vertices[Vertex].Point - FrameResult.Start).Dot(Direction));
        }
        return Width;
    };
    double WidthA = FaceWidth(FrameResult.FaceA, FrameResult.InA);
    double WidthB = FaceWidth(FrameResult.FaceB, FrameResult.InB);
    if (WidthA <= ScalarCriteria::MergeTolerance || WidthB <= ScalarCriteria::MergeTolerance)
        return Deliver<G2RollingBallPlanarCornerSpecification>::Reject(RefusalReason::Unsupported,
                                                                         "selected corner does not have two rectangular support faces");
    if (std::fabs(WidthA - WidthB) > ScalarCriteria::GeometricTolerance * std::max({ 1.0, WidthA, WidthB }))
        return Deliver<G2RollingBallPlanarCornerSpecification>::Reject(RefusalReason::Unsupported,
                                                                         "selected G2 dispatch requires equal support widths");
    Vec3 SupportA = FrameResult.InA;
    Vec3 SupportB = FrameResult.InB;
    if (FrameResult.Tangent.Dot(SupportA.Cross(SupportB)) < 0.0) std::swap(SupportA, SupportB);
    G2RollingBallPlanarCornerSpecification Specification;
    Specification.Origin = FrameResult.Start;
    Specification.EdgeAxis = FrameResult.Tangent;
    Specification.SupportA = SupportA;
    Specification.SupportB = SupportB;
    Specification.Length = FrameResult.Length;
    Specification.Width = (WidthA + WidthB) * 0.5;
    Specification.Radius = Radius;
    Specification.TransitionAngle = TransitionAngle;
    Deliver<G2RollingBallProfile> Profile = BuildG2RollingBallProfile(Specification);
    if (!Profile) return Deliver<G2RollingBallPlanarCornerSpecification>::Reject(Profile.Denial.Reason, Profile.Denial.Detail);
    return Deliver<G2RollingBallPlanarCornerSpecification>::Accept(std::move(Specification));
}

Deliver<ObliqueG2RollingBallPlanarCornerSpecification> BlendSolver::ClassifyObliqueG2RollingBallEdge(
    const BrepBody& Body, int Edge, double Radius, double TransitionAngle) noexcept
{
    if (!std::isfinite(Radius) || Radius <= ScalarCriteria::MergeTolerance ||
        !std::isfinite(TransitionAngle) || TransitionAngle <= 0.0)
        return Deliver<ObliqueG2RollingBallPlanarCornerSpecification>::Reject(RefusalReason::DegenerateInput,
                                                                                 "selected oblique G2 radius or transition is invalid");
    if (!Body.Validate().Solid())
        return Deliver<ObliqueG2RollingBallPlanarCornerSpecification>::Reject(RefusalReason::NonManifold,
                                                                                 "selected oblique G2 source is not a closed manifold B-rep");
    EdgeCornerFrame FrameResult;
    std::string Refusal;
    if (!Frame(Body, Edge, FrameResult, Refusal))
        return Deliver<ObliqueG2RollingBallPlanarCornerSpecification>::Reject(RefusalReason::Unsupported,
                                                                                 "selected edge is outside the bounded oblique G2 dispatch");
    if (std::fabs(FrameResult.Dihedral - ScalarCriteria::HalfPi) <= ScalarCriteria::AngularTolerance ||
        FrameResult.Dihedral <= ScalarCriteria::AngularTolerance ||
        FrameResult.Dihedral >= ScalarCriteria::Pi - ScalarCriteria::AngularTolerance)
        return Deliver<ObliqueG2RollingBallPlanarCornerSpecification>::Reject(RefusalReason::Unsupported,
                                                                                 "selected edge is not a strict non-orthogonal corner");
    const auto IsRectangularPlanarFace = [&](int Face) noexcept
    {
        if (Face < 0 || Face >= static_cast<int>(Body.Faces.size()) || Body.Faces[Face].Loops.size() != 1)
            return false;
        const int Loop = Body.Faces[Face].Loops.front();
        if (Loop < 0 || Loop >= static_cast<int>(Body.Loops.size()) || Body.Loops[Loop].Coedges.size() != 4)
            return false;
        std::vector<int> Boundary;
        Boundary.reserve(4);
        int PreviousEnd = -1;
        for (int Coedge : Body.Loops[Loop].Coedges)
        {
            if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return false;
            const BrepCoedge& Use = Body.Coedges[Coedge];
            if (Use.Edge < 0 || Use.Edge >= static_cast<int>(Body.Edges.size())) return false;
            const BrepEdge& BoundaryEdge = Body.Edges[Use.Edge];
            if (BoundaryEdge.Closed() || BoundaryEdge.Curve.Classification != CurveClassification::Line) return false;
            const int Start = Use.Reversed ? BoundaryEdge.VertexEnd : BoundaryEdge.VertexStart;
            const int End = Use.Reversed ? BoundaryEdge.VertexStart : BoundaryEdge.VertexEnd;
            if (Start < 0 || Start >= static_cast<int>(Body.Vertices.size()) ||
                End < 0 || End >= static_cast<int>(Body.Vertices.size())) return false;
            if (PreviousEnd >= 0 && PreviousEnd != Start) return false;
            Boundary.push_back(Start);
            PreviousEnd = End;
            if (Boundary.size() == 4 && End != Boundary.front()) return false;
        }
        for (size_t I = 0; I < Boundary.size(); ++I)
        {
            const Vec3 Here = Body.Vertices[Boundary[I]].Point;
            const Vec3 Previous = Body.Vertices[Boundary[(I + 3) % 4]].Point - Here;
            const Vec3 Next = Body.Vertices[Boundary[(I + 1) % 4]].Point - Here;
            if (Previous.Length() <= ScalarCriteria::MergeTolerance || Next.Length() <= ScalarCriteria::MergeTolerance ||
                std::fabs(Previous.Normalised().Dot(Next.Normalised())) > ScalarCriteria::AngularTolerance)
                return false;
        }
        return true;
    };
    if (!IsRectangularPlanarFace(FrameResult.FaceA) || !IsRectangularPlanarFace(FrameResult.FaceB))
        return Deliver<ObliqueG2RollingBallPlanarCornerSpecification>::Reject(RefusalReason::Unsupported,
                                                                                 "selected corner faces are not planar rectangles");
    const auto FaceWidth = [&](int Face, const Vec3& Direction) noexcept
    {
        if (Face < 0 || Face >= static_cast<int>(Body.Faces.size()) || Body.Faces[Face].Loops.size() != 1)
            return 0.0;
        std::vector<int> Vertices;
        const int Loop = Body.Faces[Face].Loops.front();
        if (Loop < 0 || Loop >= static_cast<int>(Body.Loops.size())) return 0.0;
        for (int Coedge : Body.Loops[Loop].Coedges)
        {
            if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return 0.0;
            const int EdgeIndex = Body.Coedges[Coedge].Edge;
            if (EdgeIndex < 0 || EdgeIndex >= static_cast<int>(Body.Edges.size())) return 0.0;
            const int Candidate[] = { Body.Edges[EdgeIndex].VertexStart, Body.Edges[EdgeIndex].VertexEnd };
            for (int Vertex : Candidate)
                if (std::find(Vertices.begin(), Vertices.end(), Vertex) == Vertices.end()) Vertices.push_back(Vertex);
        }
        if (Vertices.size() != 4) return 0.0;
        double Width = 0.0;
        for (int Vertex : Vertices)
        {
            if (Vertex < 0 || Vertex >= static_cast<int>(Body.Vertices.size())) return 0.0;
            Width = std::max(Width, (Body.Vertices[Vertex].Point - FrameResult.Start).Dot(Direction));
        }
        return Width;
    };
    const double WidthA = FaceWidth(FrameResult.FaceA, FrameResult.InA);
    const double WidthB = FaceWidth(FrameResult.FaceB, FrameResult.InB);
    if (WidthA <= ScalarCriteria::MergeTolerance || WidthB <= ScalarCriteria::MergeTolerance)
        return Deliver<ObliqueG2RollingBallPlanarCornerSpecification>::Reject(RefusalReason::Unsupported,
                                                                                 "selected corner does not have two rectangular support faces");
    Vec3 SupportA = FrameResult.InA;
    Vec3 SupportB = FrameResult.InB;
    double OrientedWidthA = WidthA;
    double OrientedWidthB = WidthB;
    if (FrameResult.Tangent.Dot(SupportA.Cross(SupportB)) < 0.0)
    {
        std::swap(SupportA, SupportB);
        std::swap(OrientedWidthA, OrientedWidthB);
    }
    ObliqueG2RollingBallPlanarCornerSpecification Specification;
    Specification.Origin = FrameResult.Start;
    Specification.EdgeAxis = FrameResult.Tangent;
    Specification.SupportA = SupportA;
    Specification.SupportB = SupportB;
    Specification.Length = FrameResult.Length;
    Specification.WidthA = OrientedWidthA;
    Specification.WidthB = OrientedWidthB;
    Specification.Radius = Radius;
    Specification.TransitionAngle = TransitionAngle;
    Deliver<ObliqueG2RollingBallProfile> Profile = BuildObliqueG2RollingBallProfile(Specification);
    if (!Profile)
        return Deliver<ObliqueG2RollingBallPlanarCornerSpecification>::Reject(Profile.Denial.Reason,
                                                                                 Profile.Denial.Detail);
    return Deliver<ObliqueG2RollingBallPlanarCornerSpecification>::Accept(std::move(Specification));
}

Deliver<VariableG2RollingBallPlanarCornerSpecification> BlendSolver::ClassifyVariableG2RollingBallEdge(
    const BrepBody& Body, int Edge, const QuadraticRadiusLaw& RadiusLaw, double TransitionAngle) noexcept
{
    if (!RadiusLaw.Positive() || !RadiusLaw.Nonlinear())
        return Deliver<VariableG2RollingBallPlanarCornerSpecification>::Reject(RefusalReason::Unsupported,
                                                                                  "variable G2 edge dispatch requires a positive nonlinear radius law");
    const Deliver<G2RollingBallPlanarCornerSpecification> Constant =
        ClassifyG2RollingBallEdge(Body, Edge, RadiusLaw.Start, TransitionAngle);
    if (!Constant)
        return Deliver<VariableG2RollingBallPlanarCornerSpecification>::Reject(Constant.Denial.Reason,
                                                                                  Constant.Denial.Detail);
    VariableG2RollingBallPlanarCornerSpecification Specification;
    Specification.Origin = Constant.Payload.Origin;
    Specification.EdgeAxis = Constant.Payload.EdgeAxis;
    Specification.SupportA = Constant.Payload.SupportA;
    Specification.SupportB = Constant.Payload.SupportB;
    Specification.Length = Constant.Payload.Length;
    Specification.Width = Constant.Payload.Width;
    Specification.RadiusLaw = RadiusLaw;
    Specification.TransitionAngle = TransitionAngle;
    const Deliver<BrepBody> Feasible = ReconstructVariableG2RollingBallPlanarCorner(Specification);
    if (!Feasible)
        return Deliver<VariableG2RollingBallPlanarCornerSpecification>::Reject(Feasible.Denial.Reason,
                                                                                  Feasible.Denial.Detail);
    return Deliver<VariableG2RollingBallPlanarCornerSpecification>::Accept(std::move(Specification));
}

Deliver<ObliqueVariableG2RollingBallPlanarCornerSpecification>
BlendSolver::ClassifyObliqueVariableG2RollingBallEdge(const BrepBody& Body, int Edge,
                                                       const QuadraticRadiusLaw& RadiusLaw,
                                                       double TransitionAngle) noexcept
{
    if (!RadiusLaw.Positive() || !RadiusLaw.Nonlinear())
        return Deliver<ObliqueVariableG2RollingBallPlanarCornerSpecification>::Reject(
            RefusalReason::Unsupported, "oblique variable G2 edge dispatch requires a positive nonlinear radius law");
    const Deliver<ObliqueG2RollingBallPlanarCornerSpecification> Constant =
        ClassifyObliqueG2RollingBallEdge(Body, Edge, RadiusLaw.Start, TransitionAngle);
    if (!Constant)
        return Deliver<ObliqueVariableG2RollingBallPlanarCornerSpecification>::Reject(
            Constant.Denial.Reason, Constant.Denial.Detail);
    ObliqueVariableG2RollingBallPlanarCornerSpecification Specification;
    Specification.Origin = Constant.Payload.Origin;
    Specification.EdgeAxis = Constant.Payload.EdgeAxis;
    Specification.SupportA = Constant.Payload.SupportA;
    Specification.SupportB = Constant.Payload.SupportB;
    Specification.Length = Constant.Payload.Length;
    Specification.WidthA = Constant.Payload.WidthA;
    Specification.WidthB = Constant.Payload.WidthB;
    Specification.RadiusLaw = RadiusLaw;
    Specification.TransitionAngle = TransitionAngle;
    const Deliver<BrepBody> Feasible = ReconstructObliqueVariableG2RollingBallPlanarCorner(Specification);
    if (!Feasible)
        return Deliver<ObliqueVariableG2RollingBallPlanarCornerSpecification>::Reject(
            Feasible.Denial.Reason, Feasible.Denial.Detail);
    return Deliver<ObliqueVariableG2RollingBallPlanarCornerSpecification>::Accept(std::move(Specification));
}

Deliver<BrepBody> BlendSolver::ReconstructNonlinearVariableSetbackCornerBlend(
    const NonlinearVariableSetbackCornerSpecification& Specification) noexcept
{
    if (!std::isfinite(Specification.Length) || Specification.Length <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "nonlinear setback corner length must be positive");
    if (!Specification.RadiusLaw.Positive())
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "nonlinear setback corner radius law is not positive");
    if (!Specification.SetbackLaw.Positive() || !Specification.SetbackLaw.Nonlinear())
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported,
                                          "nonlinear setback corner requires a positive nonlinear setback law");
    const Vec3 Axis = Specification.EdgeAxis.Normalised();
    if (Axis.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "nonlinear setback corner edge axis is degenerate");
    Vec3 U = Axis.Cross(Vec3::UnitX());
    if (U.Length() <= ScalarCriteria::GeometricTolerance) U = Axis.Cross(Vec3::UnitY());
    if (U.Length() <= ScalarCriteria::GeometricTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "nonlinear setback corner frame is degenerate");
    U = U.Normalised();
    const Vec3 V = Axis.Cross(U).Normalised();
    const QuadraticVariableRadiusSurface RadiusSurface{
        Specification.Origin, Axis, U, Specification.Length, Specification.RadiusLaw };
    double MaximumCircumferential = 0.0;
    double MaximumMeridional = 0.0;
    for (int I = 0; I <= 32; ++I)
    {
        const double T = static_cast<double>(I) / 32.0;
        MaximumCircumferential = std::max(MaximumCircumferential, RadiusSurface.CircumferentialCurvature(T));
        MaximumMeridional = std::max(MaximumMeridional, std::fabs(RadiusSurface.MeridionalCurvature(T)));
    }
    std::string CurvatureRefusal;
    if (!ValidateQuadraticSurfaceCurvature(RadiusSurface, MaximumCircumferential + 1e-9,
                                            MaximumMeridional + 1e-9, CurvatureRefusal))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence,
                                          "nonlinear setback corner curvature acceptance failed");

    auto Point = [&](double T, double AlongU, double AlongV)
    {
        return Specification.Origin + Axis * (Specification.Length * T) + U * AlongU + V * AlongV;
    };
    auto LineSection = [&](double T, Vec3 A, Vec3 B) -> Deliver<NurbsCurve>
    {
        return NurbsCurve::Line(Point(T, A.X, A.Y), Point(T, B.X, B.Y));
    };
    auto ArcSection = [&](double T) -> Deliver<NurbsCurve>
    {
        const double R = Specification.RadiusLaw.Radius(T);
        const double Offset = R - R / std::sqrt(2.0);
        return NurbsCurve::ArcThreePoints(Point(T, 0.0, R), Point(T, Offset, Offset), Point(T, R, 0.0));
    };
    auto LoftThree = [&](const std::vector<Deliver<NurbsCurve>>& Sections) -> Deliver<NurbsSurface>
    {
        std::vector<NurbsCurve> Curves;
        Curves.reserve(Sections.size());
        for (const Deliver<NurbsCurve>& Section : Sections)
        {
            if (!Section) return Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput,
                                                                "nonlinear setback corner section is degenerate");
            Curves.push_back(Section.Payload);
        }
        return NurbsSurface::Loft(Curves, 2);
    };
    auto Add = [&](const std::vector<Deliver<NurbsCurve>>& Sections,
                   std::vector<NurbsSurface>& Surfaces) -> bool
    {
        Deliver<NurbsSurface> Surface = LoftThree(Sections);
        if (!Surface) return false;
        Surfaces.push_back(std::move(Surface.Payload));
        return true;
    };

    const double R0 = Specification.RadiusLaw.Radius(0.0);
    const double Rm = Specification.RadiusLaw.Radius(0.5);
    const double R1 = Specification.RadiusLaw.Radius(1.0);
    const double S0 = Specification.SetbackLaw.Radius(0.0);
    const double Sm = Specification.SetbackLaw.Radius(0.5);
    const double S1 = Specification.SetbackLaw.Radius(1.0);
    const double D0 = R0 + S0;
    const double Dm = Rm + Sm;
    const double D1 = R1 + S1;
    const Vec3 A0{ R0, 0, 0 }, B0{ D0, 0, 0 }, C0{ D0, D0, 0 }, D0Point{ 0, D0, 0 }, E0{ 0, R0, 0 };
    const Vec3 Am{ Rm, 0, 0 }, Bm{ Dm, 0, 0 }, Cm{ Dm, Dm, 0 }, DmPoint{ 0, Dm, 0 }, Em{ 0, Rm, 0 };
    const Vec3 A1{ R1, 0, 0 }, B1{ D1, 0, 0 }, C1{ D1, D1, 0 }, D1Point{ 0, D1, 0 }, E1{ 0, R1, 0 };
    std::vector<NurbsSurface> Surfaces;
    if (!Add({ LineSection(0.0, A0, B0), LineSection(0.5, Am, Bm), LineSection(1.0, A1, B1) }, Surfaces) ||
        !Add({ LineSection(0.0, B0, C0), LineSection(0.5, Bm, Cm), LineSection(1.0, B1, C1) }, Surfaces) ||
        !Add({ LineSection(0.0, C0, D0Point), LineSection(0.5, Cm, DmPoint), LineSection(1.0, C1, D1Point) }, Surfaces) ||
        !Add({ LineSection(0.0, D0Point, E0), LineSection(0.5, DmPoint, Em), LineSection(1.0, D1Point, E1) }, Surfaces) ||
        !Add({ ArcSection(0.0), ArcSection(0.5), ArcSection(1.0) }, Surfaces))
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput,
                                          "nonlinear setback corner surfaces could not be constructed");

    Deliver<BrepBody> Result = BrepBody::Sew(Surfaces, ScalarCriteria::MergeTolerance, true);
    if (!Result) return Result;
    const QuadraticRadiusLaw OuterLaw{ R0 + S0, Rm + Sm, R1 + S1 };
    const double ExpectedVolume = OuterLaw.IntegratedSquare(Specification.Length) -
        (1.0 - ScalarCriteria::Pi / 4.0) * Specification.RadiusLaw.IntegratedSquare(Specification.Length);
    const BodyReport Report = Result.Payload.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 ||
        Result.Payload.Vertices.size() != 10 || Result.Payload.Edges.size() != 15 ||
        Result.Payload.Coedges.size() != 30 || Result.Payload.Loops.size() != 7 || Result.Payload.Faces.size() != 7)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold,
                                          "nonlinear setback corner did not reach exact capped topology");
    if (!ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume))
        return Deliver<BrepBody>::Reject(RefusalReason::NoConvergence,
                                          "nonlinear setback corner volume failed analytic acceptance");
    return Result;
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
