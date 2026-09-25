//=============================================================================================================================================
// SolidArc · conservative face-edit routes (Phase 36a, bounded non-box Phase 41–42 extensions)
//=============================================================================================================================================
#include "FaceEditSolver.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace Frontier
{
namespace
{
constexpr double UnitTolerance = 1e-6;

struct BoxFrame
{
    Vec3 Low{};
    Vec3 High{};
};

struct FaceFrame
{
    BoxFrame Box;
    int Axis = -1;                                                                    // world axis normal to the face
    int Sign = 0;                                                                     // outward normal sign
};

[[nodiscard]] double Component(Vec3 P, int Axis) noexcept
{
    return Axis == 0 ? P.X : Axis == 1 ? P.Y : P.Z;
}

[[nodiscard]] Vec3 AxisVector(int Axis, double Sign = 1.0) noexcept
{
    return Axis == 0 ? Vec3(Sign, 0, 0) : Axis == 1 ? Vec3(0, Sign, 0) : Vec3(0, 0, Sign);
}

[[nodiscard]] bool Close(double A, double B, double Tolerance) noexcept
{
    return std::fabs(A - B) <= Tolerance * std::max(1.0, std::max(std::fabs(A), std::fabs(B)));
}

[[nodiscard]] bool ClosePoint(Vec3 A, Vec3 B, double Tolerance) noexcept
{
    return A.Distance(B) <= Tolerance * std::max(1.0, std::max(A.Length(), B.Length()));
}

[[nodiscard]] Deliver<NurbsSurface> Quad(Vec3 P00, Vec3 P10, Vec3 P01, Vec3 P11) noexcept
{
    // A degree-one tensor patch is an exact plane for a parallelogram and an exact bilinear patch for
    // a drafted quadrilateral. It is preferable to silently fitting a plane: the four rim points are
    // retained exactly and the topology builder still has natural four-edge boundaries.
    return NurbsSurface::Patch(1, 1, 2, 2, { P00, P01, P10, P11 });
}

[[nodiscard]] Deliver<BrepBody> SewNatural(const std::vector<NurbsSurface>& Surfaces, bool RequireSolid) noexcept
{
    Deliver<BrepBody> Sewn = BrepBody::Sew(Surfaces, ScalarCriteria::MergeTolerance, false);
    if (!Sewn) return Sewn;
    BrepBody Result = std::move(Sewn.Payload);
    const BodyReport R = Result.Validate();
    if (R.NonManifoldEdges != 0 || R.MisorientedEdges != 0 || R.Faces == 0)
    {
        if (R.NonManifoldEdges != 0) return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "face edit produced non-manifold topology");
        if (R.MisorientedEdges != 0) return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "face edit produced misoriented topology");
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "face edit produced no faces");
    }
    if (RequireSolid && !R.Solid())
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "face edit did not close a positive-volume solid");
    return Deliver<BrepBody>::Accept(std::move(Result));
}

[[nodiscard]] bool ReadBox(const BrepBody& Source, BoxFrame& Out) noexcept
{
    const BodyReport R = Source.Validate();
    if (!R.Solid() || R.Vertices != 8 || R.Edges != 12 || R.Faces != 6 || R.Hulls != 1)
        return false;
    Out.Low = Source.Bounds().Low;
    Out.High = Source.Bounds().High;
    const Vec3 D = Out.High - Out.Low;
    if (D.X <= ScalarCriteria::MergeTolerance || D.Y <= ScalarCriteria::MergeTolerance || D.Z <= ScalarCriteria::MergeTolerance)
        return false;
    for (const BrepFace& F : Source.Faces)
        if (!F.Natural || F.Surface.Classification != SurfaceClassification::Plane || F.Loops.size() != 1)
            return false;
    return true;
}

[[nodiscard]] bool ReadFace(const BrepBody& Source, int Face, FaceFrame& Out) noexcept
{
    if (Face < 0 || Face >= static_cast<int>(Source.Faces.size())) return false;
    if (!ReadBox(Source, Out.Box)) return false;
    const BrepFace& F = Source.Faces[Face];
    const Vec3 N = Source.FaceNormal(Face, 0.5 * (F.Surface.DomainStartU() + F.Surface.DomainEndU()),
                                      0.5 * (F.Surface.DomainStartV() + F.Surface.DomainEndV()));
    const double A[] = { std::fabs(N.X), std::fabs(N.Y), std::fabs(N.Z) };
    Out.Axis = static_cast<int>(std::max_element(std::begin(A), std::end(A)) - std::begin(A));
    if (A[Out.Axis] < 1.0 - UnitTolerance) return false;
    Out.Sign = Component(N, Out.Axis) >= 0.0 ? 1 : -1;
    const Box3 FB = F.Surface.Bounds();
    const double PlaneCoordinate = Out.Sign > 0 ? Component(Out.Box.High, Out.Axis) : Component(Out.Box.Low, Out.Axis);
    if (!Close(Component(FB.Low, Out.Axis), PlaneCoordinate, ScalarCriteria::MergeTolerance) ||
        !Close(Component(FB.High, Out.Axis), PlaneCoordinate, ScalarCriteria::MergeTolerance)) return false;
    return true;
}

[[nodiscard]] std::vector<NurbsSurface> PrismSurfaces(Vec3 Low, Vec3 High, Vec3 TopLow, Vec3 TopHigh) noexcept
{
    // Lower and upper rectangles share X/Y bounds. TopLow/TopHigh may differ from Low/High in
    // one coordinate, which is the exact drafted-prism route.
    const Vec3 L00{ Low.X, Low.Y, Low.Z }, L10{ Low.X, High.Y, Low.Z };
    const Vec3 L01{ High.X, Low.Y, Low.Z }, L11{ High.X, High.Y, Low.Z };
    const Vec3 U00{ TopLow.X, TopLow.Y, TopLow.Z }, U10{ TopLow.X, TopHigh.Y, TopLow.Z };
    const Vec3 U01{ TopHigh.X, TopLow.Y, TopHigh.Z }, U11{ TopHigh.X, TopHigh.Y, TopHigh.Z };
    std::vector<NurbsSurface> Faces;
    auto Add = [&](Deliver<NurbsSurface> S) { if (S) Faces.push_back(std::move(S.Payload)); };
    // For an ordinary box TopLow/TopHigh are (low.x,low.y,high.z)/(high.x,high.y,high.z).
    Add(Quad(L00, L01, L10, L11));                                                // z = low
    Add(Quad(U00, U01, U10, U11));                                                // z = high / drafted top
    Add(Quad(L00, U00, L01, U01));                                                // y = low
    Add(Quad(L10, L11, U10, U11));                                                // y = high
    Add(Quad(L00, L10, U00, U10));                                                // x = low
    Add(Quad(L01, U01, L11, U11));                                                // x = high / drafted side
    return Faces;
}

[[nodiscard]] Deliver<BrepBody> BuildDraftedPrism(const BoxFrame& B, int Axis, int Sign, double Delta) noexcept
{
    if (Axis == 2) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "draft currently supports vertical side faces (+/-X or +/-Y), not a cap");
    Vec3 TopLow{ B.Low.X, B.Low.Y, B.High.Z }, TopHigh{ B.High.X, B.High.Y, B.High.Z };
    if (Axis == 0)
    {
        if (Sign > 0) TopHigh.X += Delta; else TopLow.X -= Delta;
    }
    else
    {
        if (Sign > 0) TopHigh.Y += Delta; else TopLow.Y -= Delta;
    }
    const double WidthX = TopHigh.X - TopLow.X, WidthY = TopHigh.Y - TopLow.Y;
    if (WidthX <= ScalarCriteria::MergeTolerance || WidthY <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "draft collapses the upper section");
    return SewNatural(PrismSurfaces(B.Low, B.High, TopLow, TopHigh), true);
}

[[nodiscard]] bool BoundaryCurveMatches(const NurbsCurve& A, const NurbsCurve& B, double Tolerance) noexcept
{
    const bool SameSense = ClosePoint(A.StartPoint(), B.StartPoint(), Tolerance) && ClosePoint(A.EndPoint(), B.EndPoint(), Tolerance);
    const bool OppSense = ClosePoint(A.StartPoint(), B.EndPoint(), Tolerance) && ClosePoint(A.EndPoint(), B.StartPoint(), Tolerance);
    if (!SameSense && !OppSense) return false;
    for (int I = 1; I < 6; ++I)
    {
        const double T = A.DomainStart() + (A.DomainEnd() - A.DomainStart()) * I / 6.0;
        double Distance = 0.0;
        (void)B.ClosestParameter(A.Sample(T), &Distance);
        if (Distance > Tolerance * 10.0) return false;
    }
    return true;
}

[[nodiscard]] std::vector<NurbsCurve> NaturalBoundaryEdges(const BrepBody& Body, int Face) noexcept
{
    std::vector<NurbsCurve> Out;
    if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return Out;
    for (int L : Body.Faces[Face].Loops)
        for (int C : Body.Loops[L].Coedges) Out.push_back(Body.CoedgeCurve(C));
    return Out;
}

[[nodiscard]] bool SameRim(const BrepBody& Source, int Face, const NurbsSurface& Replacement, double Tolerance) noexcept
{
    const std::vector<NurbsCurve> SourceEdges = NaturalBoundaryEdges(Source, Face);
    const BrepBody ReplacementBody = BrepBody::FromSurface(Replacement);
    const std::vector<NurbsCurve> ReplacementEdges = NaturalBoundaryEdges(ReplacementBody, 0);
    if (SourceEdges.size() != 4 || ReplacementEdges.size() != 4) return false;
    for (const NurbsCurve& A : SourceEdges)
    {
        bool Found = false;
        for (const NurbsCurve& B : ReplacementEdges) if (BoundaryCurveMatches(A, B, Tolerance)) { Found = true; break; }
        if (!Found) return false;
    }
    return true;
}

[[nodiscard]] bool ReadExtrudedConvexPrism(const BrepBody& Source, int Face, std::vector<Vec3>& Polygon,
                                       double& Low, double& High, size_t Sides) noexcept
{
    const BodyReport R = Source.Validate();
    if (Sides < 3 || !R.Solid() || R.Hulls != 1 || R.Genus != 0 || R.OpenEdges != 0 || R.NonManifoldEdges != 0 ||
        R.MisorientedEdges != 0 || Source.Vertices.size() != 2 * Sides || Source.Edges.size() != 3 * Sides ||
        Source.Coedges.size() != 6 * Sides || Source.Loops.size() != Sides + 2 || Source.Faces.size() != Sides + 2)
        return false;
    if (Face < 0 || Face >= static_cast<int>(Source.Faces.size())) return false;
    const BrepFace& Cap = Source.Faces[Face];
    if (Cap.Surface.Classification != SurfaceClassification::Plane || Cap.Loops.size() != 1) return false;
    const Vec3 Normal = Source.FaceNormal(Face,
        0.5 * (Cap.Surface.DomainStartU() + Cap.Surface.DomainEndU()),
        0.5 * (Cap.Surface.DomainStartV() + Cap.Surface.DomainEndV())).Normalised();
    if (Normal.Dot(Vec3::UnitZ()) < 1.0 - UnitTolerance) return false;
    const int Loop = Cap.Loops.front();
    if (Loop < 0 || Loop >= static_cast<int>(Source.Loops.size()) || Source.Loops[Loop].Coedges.size() != Sides) return false;
    for (int Vertex = 0; Vertex < static_cast<int>(Source.Vertices.size()); ++Vertex)
    {
        const double Z = Source.Vertices[Vertex].Point.Z;
        if (Vertex == 0) { Low = Z; High = Z; }
        else { Low = std::min(Low, Z); High = std::max(High, Z); }
    }
    if (High - Low <= ScalarCriteria::MergeTolerance) return false;
    int LowVertices = 0, HighVertices = 0;
    for (const BrepVertex& Vertex : Source.Vertices)
    {
        if (std::fabs(Vertex.Point.Z - Low) <= ScalarCriteria::GeometricTolerance) ++LowVertices;
        else if (std::fabs(Vertex.Point.Z - High) <= ScalarCriteria::GeometricTolerance) ++HighVertices;
        else return false;
    }
    if (LowVertices != static_cast<int>(Sides) || HighVertices != static_cast<int>(Sides)) return false;
    for (const BrepEdge& Edge : Source.Edges)
    {
        if (Edge.Curve.Classification != CurveClassification::Line || Edge.Curve.Degree != 1 ||
            Edge.Coedges.size() != 2 || Edge.VertexStart < 0 || Edge.VertexEnd < 0 ||
            Edge.VertexStart >= static_cast<int>(Source.Vertices.size()) || Edge.VertexEnd >= static_cast<int>(Source.Vertices.size()) ||
            Edge.VertexStart == Edge.VertexEnd) return false;
    }
    for (const BrepFace& FaceData : Source.Faces)
    {
        if (FaceData.Loops.size() != 1 || (FaceData.Surface.Classification != SurfaceClassification::Plane &&
                                           FaceData.Surface.Classification != SurfaceClassification::Extrusion)) return false;
    }
    Polygon.clear(); Polygon.reserve(Sides);
    for (int Coedge : Source.Loops[Loop].Coedges)
    {
        if (Coedge < 0 || Coedge >= static_cast<int>(Source.Coedges.size())) return false;
        const BrepCoedge& C = Source.Coedges[Coedge];
        if (C.Edge < 0 || C.Edge >= static_cast<int>(Source.Edges.size())) return false;
        const BrepEdge& E = Source.Edges[C.Edge];
        if (E.Curve.Classification != CurveClassification::Line || E.Curve.Degree != 1 ||
            E.Coedges.size() != 2 || E.VertexStart < 0 || E.VertexEnd < 0) return false;
        const int Start = C.Reversed ? E.VertexEnd : E.VertexStart;
        const int End = C.Reversed ? E.VertexStart : E.VertexEnd;
        if (Start < 0 || End < 0 || Start == End) return false;
        Polygon.push_back(Source.Vertices[Start].Point);
        if (std::fabs(Source.Vertices[Start].Point.Z - High) > ScalarCriteria::GeometricTolerance ||
            std::fabs(Source.Vertices[End].Point.Z - High) > ScalarCriteria::GeometricTolerance) return false;
    }
    if (Polygon.size() != Sides) return false;
    double Area2 = 0.0;
    for (size_t I = 0; I < Polygon.size(); ++I)
    {
        const Vec3& A = Polygon[I];
        const Vec3& B = Polygon[(I + 1) % Polygon.size()];
        Area2 += A.X * B.Y - B.X * A.Y;
    }
    if (std::fabs(Area2) <= ScalarCriteria::GeometricTolerance) return false;
    double Sign = 0.0;
    for (size_t I = 0; I < Polygon.size(); ++I)
    {
        const Vec3& A = Polygon[I];
        const Vec3& B = Polygon[(I + 1) % Polygon.size()];
        const Vec3& C = Polygon[(I + 2) % Polygon.size()];
        const double Cross = (B.X - A.X) * (C.Y - B.Y) - (B.Y - A.Y) * (C.X - B.X);
        if (std::fabs(Cross) <= ScalarCriteria::GeometricTolerance ||
            (Sign != 0.0 && Cross * Sign <= 0.0)) return false;
        if (Sign == 0.0) Sign = Cross;
    }
    return true;
}

[[nodiscard]] bool OffsetConvexPolygon(const std::vector<Vec3>& Polygon, double Thickness,
                                       std::vector<Vec3>& Inner) noexcept
{
    if (Polygon.size() != 6 || !std::isfinite(Thickness) || Thickness <= ScalarCriteria::MergeTolerance) return false;
    double Area2 = 0.0;
    for (size_t I = 0; I < Polygon.size(); ++I)
        Area2 += Polygon[I].X * Polygon[(I + 1) % Polygon.size()].Y -
                 Polygon[(I + 1) % Polygon.size()].X * Polygon[I].Y;
    if (std::fabs(Area2) <= ScalarCriteria::GeometricTolerance) return false;
    const double Orientation = Area2 > 0.0 ? 1.0 : -1.0;
    struct Line { Vec3 Point, Direction; } Lines[6];
    for (size_t I = 0; I < Polygon.size(); ++I)
    {
        const Vec3& A = Polygon[I];
        const Vec3& B = Polygon[(I + 1) % Polygon.size()];
        const Vec3 D = B - A;
        const double Length = std::hypot(D.X, D.Y);
        if (Length <= ScalarCriteria::MergeTolerance) return false;
        const Vec3 Inward = Orientation > 0.0 ? Vec3(-D.Y / Length, D.X / Length, 0.0)
                                              : Vec3(D.Y / Length, -D.X / Length, 0.0);
        Lines[I] = { A + Inward * Thickness, D / Length };
    }
    Inner.clear(); Inner.reserve(6);
    for (size_t I = 0; I < Polygon.size(); ++I)
    {
        const Line& A = Lines[(I + Polygon.size() - 1) % Polygon.size()];
        const Line& B = Lines[I];
        const double Cross = A.Direction.X * B.Direction.Y - A.Direction.Y * B.Direction.X;
        if (std::fabs(Cross) <= ScalarCriteria::GeometricTolerance) return false;
        const Vec3 Delta = B.Point - A.Point;
        const double T = (Delta.X * B.Direction.Y - Delta.Y * B.Direction.X) / Cross;
        Vec3 P = A.Point + A.Direction * T;
        P.Z = Polygon[I].Z;
        Inner.push_back(P);
    }
    for (size_t I = 0; I < Inner.size(); ++I)
    {
        const Vec3& A = Inner[I];
        const Vec3& B = Inner[(I + 1) % Inner.size()];
        const Vec3& C = Inner[(I + 2) % Inner.size()];
        const double Cross = (B.X - A.X) * (C.Y - B.Y) - (B.Y - A.Y) * (C.X - B.X);
        if (Cross * Orientation <= ScalarCriteria::GeometricTolerance) return false;
    }
    return true;
}

[[nodiscard]] bool ReadExtrudedConcavePrism(const BrepBody& Source, int Face, std::vector<Vec3>& Polygon,
                                             double& Low, double& High) noexcept;

[[nodiscard]] bool OffsetConcavePolygon(const std::vector<Vec3>& Polygon, double Thickness,
                                        std::vector<Vec3>& Inner) noexcept
{
    if (Polygon.size() != 6 || !std::isfinite(Thickness) || Thickness <= ScalarCriteria::MergeTolerance) return false;
    double Area2 = 0.0;
    for (size_t I = 0; I < Polygon.size(); ++I)
        Area2 += Polygon[I].X * Polygon[(I + 1) % Polygon.size()].Y -
                 Polygon[(I + 1) % Polygon.size()].X * Polygon[I].Y;
    if (std::fabs(Area2) <= ScalarCriteria::GeometricTolerance) return false;
    const double Orientation = Area2 > 0.0 ? 1.0 : -1.0;
    struct Line { Vec3 Point, Direction; } Lines[6];
    for (size_t I = 0; I < Polygon.size(); ++I)
    {
        const Vec3& A = Polygon[I];
        const Vec3& B = Polygon[(I + 1) % Polygon.size()];
        const Vec3 D = B - A;
        const double Length = std::hypot(D.X, D.Y);
        if (Length <= ScalarCriteria::MergeTolerance) return false;
        const Vec3 Inward = Orientation > 0.0 ? Vec3(-D.Y / Length, D.X / Length, 0.0)
                                              : Vec3(D.Y / Length, -D.X / Length, 0.0);
        Lines[I] = { A + Inward * Thickness, D / Length };
    }
    Inner.clear(); Inner.reserve(Polygon.size());
    for (size_t I = 0; I < Polygon.size(); ++I)
    {
        const Line& A = Lines[(I + Polygon.size() - 1) % Polygon.size()];
        const Line& B = Lines[I];
        const double Cross = A.Direction.X * B.Direction.Y - A.Direction.Y * B.Direction.X;
        if (std::fabs(Cross) <= ScalarCriteria::GeometricTolerance) return false;
        const Vec3 Delta = B.Point - A.Point;
        const double T = (Delta.X * B.Direction.Y - Delta.Y * B.Direction.X) / Cross;
        Vec3 P = A.Point + A.Direction * T;
        P.Z = Polygon[I].Z;
        Inner.push_back(P);
    }
    double InnerArea2 = 0.0;
    int PositiveTurns = 0, NegativeTurns = 0;
    for (size_t I = 0; I < Inner.size(); ++I)
    {
        const Vec3& A = Inner[I];
        const Vec3& B = Inner[(I + 1) % Inner.size()];
        const Vec3& C = Inner[(I + 2) % Inner.size()];
        InnerArea2 += A.X * B.Y - B.X * A.Y;
        const double Cross = (B.X - A.X) * (C.Y - B.Y) - (B.Y - A.Y) * (C.X - B.X);
        if (std::fabs(Cross) <= ScalarCriteria::GeometricTolerance) return false;
        if (Cross > 0.0) ++PositiveTurns; else ++NegativeTurns;
    }
    if (std::fabs(InnerArea2) <= ScalarCriteria::GeometricTolerance ||
        std::min(PositiveTurns, NegativeTurns) != 1 || std::max(PositiveTurns, NegativeTurns) != 5) return false;
    return true;
}

[[nodiscard]] Deliver<BrepBody> BuildExtrudedConvexPrismShell(const BrepBody& Source, int Face, double Thickness) noexcept
{
    std::vector<Vec3> OuterTop;
    double Low = 0.0, High = 0.0;
    if (!ReadExtrudedConvexPrism(Source, Face, OuterTop, Low, High, 6))
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "convex-prism shell requires a six-sided vertical prism and its upper cap");
    if (!std::isfinite(Thickness) || Thickness <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "prism shell thickness must be finite and positive");
    if (Thickness * 2.0 >= High - Low)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "prism shell thickness leaves no positive floor or wall");
    std::vector<Vec3> InnerTop;
    if (!OffsetConvexPolygon(OuterTop, Thickness, InnerTop))
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "prism shell profile is not a feasible convex offset");
    std::vector<NurbsSurface> Surfaces;
    Surfaces.reserve(18);
    const Vec3 Axis = Vec3::UnitZ();
    for (size_t I = 0; I < OuterTop.size(); ++I)
    {
        const size_t J = (I + 1) % OuterTop.size();
        const Vec3 OuterBottomA{ OuterTop[I].X, OuterTop[I].Y, Low };
        const Vec3 OuterBottomB{ OuterTop[J].X, OuterTop[J].Y, Low };
        const Vec3 InnerFloorA{ InnerTop[I].X, InnerTop[I].Y, Low + Thickness };
        const Vec3 InnerFloorB{ InnerTop[J].X, InnerTop[J].Y, Low + Thickness };
        const Vec3 OuterA = OuterTop[I], OuterB = OuterTop[J];
        const Vec3 InnerA = InnerTop[I], InnerB = InnerTop[J];
        const Deliver<NurbsCurve> OuterLine = NurbsCurve::Line(OuterBottomA, OuterBottomB);
        const Deliver<NurbsCurve> InnerLine = NurbsCurve::Line(InnerFloorA, InnerFloorB);
        const Deliver<NurbsCurve> RimOuter = NurbsCurve::Line(OuterA, OuterB);
        const Deliver<NurbsCurve> RimInner = NurbsCurve::Line(InnerA, InnerB);
        if (!OuterLine || !InnerLine || !RimOuter || !RimInner)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "prism shell generated a degenerate boundary");
        const Deliver<NurbsSurface> OuterWall = NurbsSurface::Extrusion(OuterLine.Payload, Axis, High - Low);
        const Deliver<NurbsSurface> InnerWall = NurbsSurface::Extrusion(InnerLine.Payload, Axis, High - Low - Thickness);
        const Deliver<NurbsSurface> Rim = NurbsSurface::Ruled(RimOuter.Payload, RimInner.Payload);
        if (!OuterWall || !InnerWall || !Rim)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "prism shell generated a degenerate wall");
        Surfaces.push_back(OuterWall.Payload);
        Surfaces.push_back(InnerWall.Payload);
        Surfaces.push_back(Rim.Payload);
    }
    Deliver<BrepBody> Result = BrepBody::Sew(Surfaces, ScalarCriteria::MergeTolerance, true);
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, "prism shell surfaces could not be sewn");
    Result.Payload.Orient();
    const BodyReport Report = Result.Payload.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Result.Payload.Vertices.size() != 24 ||
        Result.Payload.Edges.size() != 42 || Result.Payload.Coedges.size() != 84 || Result.Payload.Loops.size() != 20 ||
        Result.Payload.Faces.size() != 20)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "prism shell did not reach V24/E42/C84/L20/F20 topology");
    return Result;
}

[[nodiscard]] Deliver<BrepBody> BuildExtrudedConcavePrismShell(const BrepBody& Source, int Face, double Thickness) noexcept
{
    std::vector<Vec3> OuterTop;
    double Low = 0.0, High = 0.0;
    if (!ReadExtrudedConcavePrism(Source, Face, OuterTop, Low, High))
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "concave-prism shell requires a six-edge orthogonal L-profile and its upper cap");
    if (!std::isfinite(Thickness) || Thickness <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "concave-prism shell thickness must be finite and positive");
    if (Thickness * 2.0 >= High - Low)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "concave-prism shell thickness leaves no positive floor or wall");
    std::vector<Vec3> InnerTop;
    if (!OffsetConcavePolygon(OuterTop, Thickness, InnerTop))
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "concave-prism shell profile is not a feasible inward offset");
    std::vector<NurbsSurface> Surfaces;
    Surfaces.reserve(18);
    const Vec3 Axis = Vec3::UnitZ();
    for (size_t I = 0; I < OuterTop.size(); ++I)
    {
        const size_t J = (I + 1) % OuterTop.size();
        const Vec3 OuterBottomA{ OuterTop[I].X, OuterTop[I].Y, Low };
        const Vec3 OuterBottomB{ OuterTop[J].X, OuterTop[J].Y, Low };
        const Vec3 InnerFloorA{ InnerTop[I].X, InnerTop[I].Y, Low + Thickness };
        const Vec3 InnerFloorB{ InnerTop[J].X, InnerTop[J].Y, Low + Thickness };
        const Vec3 OuterA = OuterTop[I], OuterB = OuterTop[J];
        const Vec3 InnerA = InnerTop[I], InnerB = InnerTop[J];
        const Deliver<NurbsCurve> OuterLine = NurbsCurve::Line(OuterBottomA, OuterBottomB);
        const Deliver<NurbsCurve> InnerLine = NurbsCurve::Line(InnerFloorA, InnerFloorB);
        const Deliver<NurbsCurve> RimOuter = NurbsCurve::Line(OuterA, OuterB);
        const Deliver<NurbsCurve> RimInner = NurbsCurve::Line(InnerA, InnerB);
        if (!OuterLine || !InnerLine || !RimOuter || !RimInner)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "concave-prism shell generated a degenerate boundary");
        const Deliver<NurbsSurface> OuterWall = NurbsSurface::Extrusion(OuterLine.Payload, Axis, High - Low);
        const Deliver<NurbsSurface> InnerWall = NurbsSurface::Extrusion(InnerLine.Payload, Axis, High - Low - Thickness);
        const Deliver<NurbsSurface> Rim = NurbsSurface::Ruled(RimOuter.Payload, RimInner.Payload);
        if (!OuterWall || !InnerWall || !Rim)
            return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "concave-prism shell generated a degenerate wall");
        Surfaces.push_back(OuterWall.Payload);
        Surfaces.push_back(InnerWall.Payload);
        Surfaces.push_back(Rim.Payload);
    }
    Deliver<BrepBody> Result = BrepBody::Sew(Surfaces, ScalarCriteria::MergeTolerance, true);
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, "concave-prism shell surfaces could not be sewn");
    Result.Payload.Orient();
    const BodyReport Report = Result.Payload.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Result.Payload.Vertices.size() != 24 ||
        Result.Payload.Edges.size() != 42 || Result.Payload.Coedges.size() != 84 || Result.Payload.Loops.size() != 20 ||
        Result.Payload.Faces.size() != 20)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "concave-prism shell did not reach V24/E42/C84/L20/F20 topology");
    return Result;
}

[[nodiscard]] Deliver<BrepBody> BuildPentagonalPrismFaceOffset(const BrepBody& Source, int Face, double Distance) noexcept
{
    std::vector<Vec3> Top;
    double Low = 0.0, High = 0.0;
    if (!ReadExtrudedConvexPrism(Source, Face, Top, Low, High, 5))
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "pentagonal-prism offset requires a five-sided vertical prism and its upper cap");
    if (!std::isfinite(Distance) || Distance <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "pentagonal-prism offset distance must be finite and positive");

    std::vector<Vec3> Base;
    Base.reserve(Top.size());
    for (const Vec3& P : Top) Base.push_back({ P.X, P.Y, Low });
    const Deliver<NurbsCurve> Profile = NurbsCurve::Polyline(Base, true);
    if (!Profile) return Deliver<BrepBody>::Reject(Profile.Denial.Reason, Profile.Denial.Detail);
    Deliver<BrepBody> Result = BrepBody::Extrude(Profile.Payload, Vec3::UnitZ(), High - Low + Distance);
    if (!Result) return Result;
    Result.Payload.Orient();
    const BodyReport Report = Result.Payload.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Result.Payload.Vertices.size() != 10 ||
        Result.Payload.Edges.size() != 15 || Result.Payload.Coedges.size() != 30 || Result.Payload.Loops.size() != 7 ||
        Result.Payload.Faces.size() != 7)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "pentagonal-prism offset did not retain V10/E15/C30/L7/F7 topology");
    return Result;
}

[[nodiscard]] bool ReadExtrudedConcavePrism(const BrepBody& Source, int Face, std::vector<Vec3>& Polygon,
                                             double& Low, double& High) noexcept
{
    const BodyReport R = Source.Validate();
    constexpr size_t Sides = 6;
    if (!R.Solid() || R.Hulls != 1 || R.Genus != 0 || R.OpenEdges != 0 || R.NonManifoldEdges != 0 ||
        R.MisorientedEdges != 0 || Source.Vertices.size() != 2 * Sides || Source.Edges.size() != 3 * Sides ||
        Source.Coedges.size() != 6 * Sides || Source.Loops.size() != Sides + 2 || Source.Faces.size() != Sides + 2)
        return false;
    if (Face < 0 || Face >= static_cast<int>(Source.Faces.size())) return false;
    const BrepFace& Cap = Source.Faces[Face];
    if (Cap.Surface.Classification != SurfaceClassification::Plane || Cap.Loops.size() != 1) return false;
    const Vec3 Normal = Source.FaceNormal(Face,
        0.5 * (Cap.Surface.DomainStartU() + Cap.Surface.DomainEndU()),
        0.5 * (Cap.Surface.DomainStartV() + Cap.Surface.DomainEndV())).Normalised();
    if (Normal.Dot(Vec3::UnitZ()) < 1.0 - UnitTolerance) return false;
    const int Loop = Cap.Loops.front();
    if (Loop < 0 || Loop >= static_cast<int>(Source.Loops.size()) || Source.Loops[Loop].Coedges.size() != Sides) return false;
    Low = Source.Bounds().Low.Z; High = Source.Bounds().High.Z;
    if (High - Low <= ScalarCriteria::MergeTolerance) return false;
    int LowVertices = 0, HighVertices = 0;
    for (const BrepVertex& Vertex : Source.Vertices)
    {
        if (std::fabs(Vertex.Point.Z - Low) <= ScalarCriteria::GeometricTolerance) ++LowVertices;
        else if (std::fabs(Vertex.Point.Z - High) <= ScalarCriteria::GeometricTolerance) ++HighVertices;
        else return false;
    }
    if (LowVertices != static_cast<int>(Sides) || HighVertices != static_cast<int>(Sides)) return false;

    for (const BrepEdge& Edge : Source.Edges)
    {
        if (Edge.Curve.Classification != CurveClassification::Line || Edge.Curve.Degree != 1 || Edge.Coedges.size() != 2 ||
            Edge.VertexStart < 0 || Edge.VertexEnd < 0 || Edge.VertexStart >= static_cast<int>(Source.Vertices.size()) ||
            Edge.VertexEnd >= static_cast<int>(Source.Vertices.size()) || Edge.VertexStart == Edge.VertexEnd) return false;
        const Vec3 A = Source.Vertices[Edge.VertexStart].Point, B = Source.Vertices[Edge.VertexEnd].Point;
        const bool AOnLevel = std::fabs(A.Z - Low) <= ScalarCriteria::GeometricTolerance ||
                              std::fabs(A.Z - High) <= ScalarCriteria::GeometricTolerance;
        const bool BOnLevel = std::fabs(B.Z - Low) <= ScalarCriteria::GeometricTolerance ||
                              std::fabs(B.Z - High) <= ScalarCriteria::GeometricTolerance;
        if (!AOnLevel || !BOnLevel) return false;
        if (std::fabs(A.Z - B.Z) > ScalarCriteria::GeometricTolerance &&
            (!Close(A.X, B.X, ScalarCriteria::GeometricTolerance) || !Close(A.Y, B.Y, ScalarCriteria::GeometricTolerance))) return false;
    }
    for (const BrepFace& FaceData : Source.Faces)
        if (FaceData.Loops.size() != 1 || (FaceData.Surface.Classification != SurfaceClassification::Plane &&
                                           FaceData.Surface.Classification != SurfaceClassification::Extrusion)) return false;

    Polygon.clear(); Polygon.reserve(Sides);
    for (int Coedge : Source.Loops[Loop].Coedges)
    {
        if (Coedge < 0 || Coedge >= static_cast<int>(Source.Coedges.size())) return false;
        const BrepCoedge& C = Source.Coedges[Coedge];
        if (C.Edge < 0 || C.Edge >= static_cast<int>(Source.Edges.size())) return false;
        const BrepEdge& E = Source.Edges[C.Edge];
        if (E.Curve.Classification != CurveClassification::Line || E.Coedges.size() != 2) return false;
        const int Start = C.Reversed ? E.VertexEnd : E.VertexStart;
        const int End = C.Reversed ? E.VertexStart : E.VertexEnd;
        if (Start < 0 || End < 0 || Start == End ||
            std::fabs(Source.Vertices[Start].Point.Z - High) > ScalarCriteria::GeometricTolerance ||
            std::fabs(Source.Vertices[End].Point.Z - High) > ScalarCriteria::GeometricTolerance) return false;
        Polygon.push_back(Source.Vertices[Start].Point);
    }
    if (Polygon.size() != Sides) return false;
    double Area2 = 0.0;
    int PositiveTurns = 0, NegativeTurns = 0;
    for (size_t I = 0; I < Polygon.size(); ++I)
    {
        const Vec3& A = Polygon[I];
        const Vec3& B = Polygon[(I + 1) % Polygon.size()];
        const Vec3& C = Polygon[(I + 2) % Polygon.size()];
        const double DX = B.X - A.X, DY = B.Y - A.Y;
        if ((std::fabs(DX) <= ScalarCriteria::GeometricTolerance) == (std::fabs(DY) <= ScalarCriteria::GeometricTolerance)) return false;
        Area2 += A.X * B.Y - B.X * A.Y;
        const double Cross = DX * (C.Y - B.Y) - DY * (C.X - B.X);
        if (std::fabs(Cross) <= ScalarCriteria::GeometricTolerance) return false;
        if (Cross > 0.0) ++PositiveTurns; else ++NegativeTurns;
    }
    if (std::fabs(Area2) <= ScalarCriteria::GeometricTolerance ||
        std::min(PositiveTurns, NegativeTurns) != 1 || std::max(PositiveTurns, NegativeTurns) != 5) return false;
    return true;
}

[[nodiscard]] Deliver<BrepBody> BuildExtrudedConcavePrismFaceOffset(const BrepBody& Source, int Face, double Distance) noexcept
{
    std::vector<Vec3> Polygon; double Low = 0.0, High = 0.0;
    if (!ReadExtrudedConcavePrism(Source, Face, Polygon, Low, High))
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "concave-prism offset requires a six-edge orthogonal L-profile and its upper cap");
    if (!std::isfinite(Distance) || Distance <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "concave-prism offset distance must be finite and positive");
    std::vector<Vec3> Profile;
    Profile.reserve(Polygon.size());
    for (const Vec3& P : Polygon) Profile.push_back({ P.X, P.Y, Low });
    const Deliver<NurbsCurve> Curve = NurbsCurve::Polyline(Profile, true);
    if (!Curve) return Deliver<BrepBody>::Reject(Curve.Denial.Reason, Curve.Denial.Detail);
    Deliver<BrepBody> Result = BrepBody::Extrude(Curve.Payload, Vec3::UnitZ(), High - Low + Distance);
    if (!Result) return Result;
    Result.Payload.Orient();
    const BodyReport Report = Result.Payload.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 || Report.NonManifoldEdges != 0 ||
        Report.MisorientedEdges != 0 || Result.Payload.Vertices.size() != 12 || Result.Payload.Edges.size() != 18 ||
        Result.Payload.Coedges.size() != 36 || Result.Payload.Loops.size() != 8 || Result.Payload.Faces.size() != 8)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "concave-prism offset did not retain V12/E18/C36/L8/F8 topology");
    return Result;
}

[[nodiscard]] bool ReadExtrudedHoledPrism(const BrepBody& Source, int Face, Vec3& Low, Vec3& High,
                                          Vec3& HoleCentre, double& HoleRadius) noexcept
{
    const BodyReport R = Source.Validate();
    if (!R.Solid() || R.Hulls != 1 || R.Genus != 1 || R.OpenEdges != 0 || R.NonManifoldEdges != 0 ||
        R.MisorientedEdges != 0 || Source.Vertices.size() != 10 || Source.Edges.size() != 15 ||
        Source.Coedges.size() != 30 || Source.Loops.size() != 9 || Source.Faces.size() != 7)
        return false;
    if (Face < 0 || Face >= static_cast<int>(Source.Faces.size())) return false;
    const BrepFace& Cap = Source.Faces[Face];
    if (Cap.Surface.Classification != SurfaceClassification::Plane || Cap.Loops.size() != 2) return false;
    const Vec3 Normal = Source.FaceNormal(Face,
        0.5 * (Cap.Surface.DomainStartU() + Cap.Surface.DomainEndU()),
        0.5 * (Cap.Surface.DomainStartV() + Cap.Surface.DomainEndV())).Normalised();
    if (Normal.Dot(Vec3::UnitZ()) < 1.0 - UnitTolerance) return false;

    Low = Source.Bounds().Low;
    High = Source.Bounds().High;
    if (High.Z - Low.Z <= ScalarCriteria::MergeTolerance) return false;
    int LowVertices = 0, HighVertices = 0;
    for (const BrepVertex& Vertex : Source.Vertices)
    {
        if (std::fabs(Vertex.Point.Z - Low.Z) <= ScalarCriteria::GeometricTolerance) ++LowVertices;
        else if (std::fabs(Vertex.Point.Z - High.Z) <= ScalarCriteria::GeometricTolerance) ++HighVertices;
        else return false;
    }
    if (LowVertices != 5 || HighVertices != 5) return false;

    int OuterLoop = -1, HoleLoop = -1;
    for (int Loop : Cap.Loops)
    {
        if (Loop < 0 || Loop >= static_cast<int>(Source.Loops.size())) return false;
        const BrepLoop& L = Source.Loops[Loop];
        if (L.Outer && L.Coedges.size() == 4 && OuterLoop < 0) OuterLoop = Loop;
        else if (!L.Outer && L.Coedges.size() == 1 && HoleLoop < 0) HoleLoop = Loop;
        else return false;
    }
    if (OuterLoop < 0 || HoleLoop < 0) return false;

    std::vector<Vec3> Outer;
    for (int Coedge : Source.Loops[OuterLoop].Coedges)
    {
        if (Coedge < 0 || Coedge >= static_cast<int>(Source.Coedges.size())) return false;
        const BrepCoedge& C = Source.Coedges[Coedge];
        if (C.Edge < 0 || C.Edge >= static_cast<int>(Source.Edges.size())) return false;
        const BrepEdge& E = Source.Edges[C.Edge];
        if (E.Curve.Classification != CurveClassification::Line || E.Curve.Degree != 1 ||
            E.Coedges.size() != 2 || E.VertexStart < 0 || E.VertexEnd < 0 || E.VertexStart == E.VertexEnd)
            return false;
        const int Start = C.Reversed ? E.VertexEnd : E.VertexStart;
        const int End = C.Reversed ? E.VertexStart : E.VertexEnd;
        if (Start < 0 || End < 0 || Start >= static_cast<int>(Source.Vertices.size()) ||
            End >= static_cast<int>(Source.Vertices.size())) return false;
        const Vec3 A = Source.Vertices[Start].Point;
        const Vec3 B = Source.Vertices[End].Point;
        if (std::fabs(A.Z - High.Z) > ScalarCriteria::GeometricTolerance ||
            std::fabs(B.Z - High.Z) > ScalarCriteria::GeometricTolerance ||
            (!Close(A.X, B.X, ScalarCriteria::GeometricTolerance) &&
             !Close(A.Y, B.Y, ScalarCriteria::GeometricTolerance))) return false;
        Outer.push_back(A);
    }
    if (Outer.size() != 4) return false;
    const double MinX = std::min_element(Outer.begin(), Outer.end(), [](Vec3 A, Vec3 B) { return A.X < B.X; })->X;
    const double MaxX = std::max_element(Outer.begin(), Outer.end(), [](Vec3 A, Vec3 B) { return A.X < B.X; })->X;
    const double MinY = std::min_element(Outer.begin(), Outer.end(), [](Vec3 A, Vec3 B) { return A.Y < B.Y; })->Y;
    const double MaxY = std::max_element(Outer.begin(), Outer.end(), [](Vec3 A, Vec3 B) { return A.Y < B.Y; })->Y;
    if (MaxX - MinX <= ScalarCriteria::MergeTolerance || MaxY - MinY <= ScalarCriteria::MergeTolerance) return false;
    const std::array<Vec3, 4> Corners{{ { MinX, MinY, High.Z }, { MaxX, MinY, High.Z },
                                         { MaxX, MaxY, High.Z }, { MinX, MaxY, High.Z } }};
    for (const Vec3& Corner : Corners)
    {
        bool Found = false;
        for (const Vec3& P : Outer) if (ClosePoint(P, Corner, ScalarCriteria::GeometricTolerance)) { Found = true; break; }
        if (!Found) return false;
    }

    const int HoleCoedge = Source.Loops[HoleLoop].Coedges.front();
    if (HoleCoedge < 0 || HoleCoedge >= static_cast<int>(Source.Coedges.size())) return false;
    const BrepCoedge& HC = Source.Coedges[HoleCoedge];
    if (HC.Edge < 0 || HC.Edge >= static_cast<int>(Source.Edges.size())) return false;
    const BrepEdge& HoleEdge = Source.Edges[HC.Edge];
    if (HoleEdge.Curve.Classification != CurveClassification::Circle || !HoleEdge.Curve.Rational() ||
        !HoleEdge.Curve.Closed() || HoleEdge.Coedges.size() != 2 ||
        std::fabs(HoleEdge.Curve.AxisZ.Normalised().Dot(Vec3::UnitZ())) < 1.0 - UnitTolerance)
        return false;
    const Box3 HoleBounds = HoleEdge.Curve.Bounds();
    HoleRadius = 0.25 * ((HoleBounds.High.X - HoleBounds.Low.X) + (HoleBounds.High.Y - HoleBounds.Low.Y));
    HoleCentre = { 0.5 * (HoleBounds.Low.X + HoleBounds.High.X), 0.5 * (HoleBounds.Low.Y + HoleBounds.High.Y), Low.Z };
    if (!std::isfinite(HoleRadius) || HoleRadius <= ScalarCriteria::MergeTolerance ||
        std::fabs((HoleBounds.High.X - HoleBounds.Low.X) - 2.0 * HoleRadius) > ScalarCriteria::GeometricTolerance ||
        std::fabs((HoleBounds.High.Y - HoleBounds.Low.Y) - 2.0 * HoleRadius) > ScalarCriteria::GeometricTolerance ||
        HoleCentre.X <= MinX + HoleRadius + ScalarCriteria::MergeTolerance ||
        HoleCentre.X >= MaxX - HoleRadius - ScalarCriteria::MergeTolerance ||
        HoleCentre.Y <= MinY + HoleRadius + ScalarCriteria::MergeTolerance ||
        HoleCentre.Y >= MaxY - HoleRadius - ScalarCriteria::MergeTolerance) return false;

    int CircularEdges = 0;
    for (const BrepEdge& E : Source.Edges)
    {
        if (E.Curve.Classification == CurveClassification::Circle)
        {
            if (!E.Curve.Rational() || !E.Curve.Closed() || E.Coedges.size() != 2) return false;
            ++CircularEdges;
        }
        else if (E.Curve.Classification != CurveClassification::Line || E.Curve.Degree != 1 || E.Coedges.size() != 2)
            return false;
    }
    if (CircularEdges != 2) return false;
    for (const BrepFace& F : Source.Faces)
        if ((F.Surface.Classification != SurfaceClassification::Plane && F.Surface.Classification != SurfaceClassification::Extrusion) ||
            F.Loops.empty() || F.Loops.size() > 2) return false;
    return true;
}

[[nodiscard]] Deliver<BrepBody> BuildExtrudedHoledPrismFaceOffset(const BrepBody& Source, int Face, double Distance) noexcept
{
    Vec3 Low{}, High{}, HoleCentre{};
    double HoleRadius = 0.0;
    if (!ReadExtrudedHoledPrism(Source, Face, Low, High, HoleCentre, HoleRadius))
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "holed-prism offset requires a rectangular genus-one prism and its upper annular cap");
    if (!std::isfinite(Distance) || Distance <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "holed-prism offset distance must be finite and positive");
    const std::vector<Vec3> OuterPoints{
        { Low.X, Low.Y, Low.Z }, { High.X, Low.Y, Low.Z },
        { High.X, High.Y, Low.Z }, { Low.X, High.Y, Low.Z } };
    const Deliver<NurbsCurve> Outer = NurbsCurve::Polyline(OuterPoints, true);
    const Deliver<NurbsCurve> Hole = NurbsCurve::Circle({ HoleCentre.X, HoleCentre.Y, Low.Z }, Vec3::UnitZ(), HoleRadius);
    if (!Outer || !Hole)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "holed-prism offset generated a degenerate profile");
    Deliver<BrepBody> Result = BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, Hole.Payload },
                                                  Vec3::UnitZ(), High.Z - Low.Z + Distance);
    if (!Result) return Result;
    Result.Payload.Orient();
    const BodyReport Report = Result.Payload.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 1 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Result.Payload.Vertices.size() != 10 ||
        Result.Payload.Edges.size() != 15 || Result.Payload.Coedges.size() != 30 || Result.Payload.Loops.size() != 9 ||
        Result.Payload.Faces.size() != 7)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "holed-prism offset did not retain V10/E15/C30/L9/F7 topology");
    return Result;
}

[[nodiscard]] bool ExactAxisAlignedEllipse(const NurbsCurve& Curve, double& MajorRadius, double& MinorRadius,
                                            Vec3& Centre) noexcept
{
    if (!Curve.Closed() || !Curve.Rational() || Curve.Degree != 2 || Curve.PoleCount() != 9 ||
        std::fabs(Curve.AxisZ.Normalised().Dot(Vec3::UnitZ())) < 1.0 - UnitTolerance) return false;
    const Box3 Bounds = Curve.Bounds();
    const double SpanX = Bounds.High.X - Bounds.Low.X;
    const double SpanY = Bounds.High.Y - Bounds.Low.Y;
    if (SpanX <= ScalarCriteria::MergeTolerance || SpanY <= ScalarCriteria::MergeTolerance ||
        std::fabs(SpanX - SpanY) <= ScalarCriteria::GeometricTolerance) return false;
    MajorRadius = std::max(SpanX, SpanY) * 0.5;
    MinorRadius = std::min(SpanX, SpanY) * 0.5;
    Centre = { 0.5 * (Bounds.Low.X + Bounds.High.X), 0.5 * (Bounds.Low.Y + Bounds.High.Y), Bounds.Low.Z };
    const Deliver<NurbsCurve> Expected = NurbsCurve::Ellipse(Centre, Vec3::UnitZ(), Vec3::UnitX(), MajorRadius, MinorRadius);
    if (!Expected || std::fabs(Bounds.High.Z - Bounds.Low.Z) > ScalarCriteria::GeometricTolerance) return false;
    for (int I = 0; I <= 32; ++I)
    {
        const double T = Curve.DomainStart() + (Curve.DomainEnd() - Curve.DomainStart()) * I / 32.0;
        double Distance = 0.0;
        (void)Expected.Payload.ClosestParameter(Curve.Sample(T), &Distance);
        if (Distance > ScalarCriteria::GeometricTolerance * 100.0) return false;
    }
    return true;
}

[[nodiscard]] bool ReadExtrudedEllipticalPrism(const BrepBody& Source, int Face, Vec3& Low, Vec3& High,
                                               double& MajorRadius, double& MinorRadius, Vec3& Centre) noexcept
{
    const BodyReport R = Source.Validate();
    if (!R.Solid() || R.Hulls != 1 || R.Genus != 0 || R.OpenEdges != 0 || R.NonManifoldEdges != 0 ||
        R.MisorientedEdges != 0 || Source.Vertices.size() != 2 || Source.Edges.size() != 3 ||
        Source.Coedges.size() != 6 || Source.Loops.size() != 3 || Source.Faces.size() != 3)
        return false;
    if (Face < 0 || Face >= static_cast<int>(Source.Faces.size())) return false;
    const BrepFace& Cap = Source.Faces[Face];
    if (Cap.Surface.Classification != SurfaceClassification::Plane || Cap.Loops.size() != 1) return false;
    const Vec3 Normal = Source.FaceNormal(Face,
        0.5 * (Cap.Surface.DomainStartU() + Cap.Surface.DomainEndU()),
        0.5 * (Cap.Surface.DomainStartV() + Cap.Surface.DomainEndV())).Normalised();
    if (Normal.Dot(Vec3::UnitZ()) < 1.0 - UnitTolerance) return false;

    Low = Source.Bounds().Low;
    High = Source.Bounds().High;
    if (High.Z - Low.Z <= ScalarCriteria::MergeTolerance) return false;
    int LowVertices = 0, HighVertices = 0;
    for (const BrepVertex& Vertex : Source.Vertices)
    {
        if (std::fabs(Vertex.Point.Z - Low.Z) <= ScalarCriteria::GeometricTolerance) ++LowVertices;
        else if (std::fabs(Vertex.Point.Z - High.Z) <= ScalarCriteria::GeometricTolerance) ++HighVertices;
        else return false;
    }
    if (LowVertices != 1 || HighVertices != 1) return false;

    int EllipseEdges = 0, LineEdges = 0;
    double FirstMajor = 0.0, FirstMinor = 0.0;
    Vec3 FirstCentre{};
    for (const BrepEdge& Edge : Source.Edges)
    {
        if (Edge.Coedges.size() != 2) return false;
        if (Edge.Closed())
        {
            double EdgeMajor = 0.0, EdgeMinor = 0.0; Vec3 EdgeCentre{};
            if (!ExactAxisAlignedEllipse(Edge.Curve, EdgeMajor, EdgeMinor, EdgeCentre)) return false;
            if (EllipseEdges++ == 0) { FirstMajor = EdgeMajor; FirstMinor = EdgeMinor; FirstCentre = EdgeCentre; }
            else if (std::fabs(EdgeMajor - FirstMajor) > ScalarCriteria::GeometricTolerance ||
                     std::fabs(EdgeMinor - FirstMinor) > ScalarCriteria::GeometricTolerance ||
                     std::fabs(EdgeCentre.X - FirstCentre.X) > ScalarCriteria::GeometricTolerance ||
                     std::fabs(EdgeCentre.Y - FirstCentre.Y) > ScalarCriteria::GeometricTolerance) return false;
        }
        else
        {
            if (Edge.Curve.Classification != CurveClassification::Line || Edge.Curve.Degree != 1 ||
                Edge.VertexStart < 0 || Edge.VertexEnd < 0 || Edge.VertexStart == Edge.VertexEnd) return false;
            ++LineEdges;
        }
    }
    if (EllipseEdges != 2 || LineEdges != 1) return false;
    for (const BrepFace& F : Source.Faces)
        if ((F.Surface.Classification != SurfaceClassification::Plane && F.Surface.Classification != SurfaceClassification::Extrusion) ||
            F.Loops.size() != 1) return false;
    MajorRadius = FirstMajor; MinorRadius = FirstMinor; Centre = FirstCentre;
    if (Centre.X <= Low.X || Centre.X >= High.X || Centre.Y <= Low.Y || Centre.Y >= High.Y) return false;
    return true;
}

[[nodiscard]] bool ReadExtrudedTwinHoledPrism(const BrepBody& Source, int Face, Vec3& Low, Vec3& High,
                                              std::vector<Vec3>& Centres, std::vector<double>& Radii) noexcept
{
    const BodyReport R = Source.Validate();
    if (!R.Solid() || R.Hulls != 1 || R.Genus != 2 || R.OpenEdges != 0 || R.NonManifoldEdges != 0 ||
        R.MisorientedEdges != 0 || Source.Vertices.size() != 12 || Source.Edges.size() != 18 ||
        Source.Coedges.size() != 36 || Source.Loops.size() != 12 || Source.Faces.size() != 8)
        return false;
    if (Face < 0 || Face >= static_cast<int>(Source.Faces.size())) return false;
    const BrepFace& Cap = Source.Faces[Face];
    if (Cap.Surface.Classification != SurfaceClassification::Plane || Cap.Loops.size() != 3) return false;
    const Vec3 Normal = Source.FaceNormal(Face,
        0.5 * (Cap.Surface.DomainStartU() + Cap.Surface.DomainEndU()),
        0.5 * (Cap.Surface.DomainStartV() + Cap.Surface.DomainEndV())).Normalised();
    if (Normal.Dot(Vec3::UnitZ()) < 1.0 - UnitTolerance) return false;
    Low = Source.Bounds().Low; High = Source.Bounds().High;
    if (High.Z - Low.Z <= ScalarCriteria::MergeTolerance) return false;
    int LowVertices = 0, HighVertices = 0;
    for (const BrepVertex& Vertex : Source.Vertices)
    {
        if (std::fabs(Vertex.Point.Z - Low.Z) <= ScalarCriteria::GeometricTolerance) ++LowVertices;
        else if (std::fabs(Vertex.Point.Z - High.Z) <= ScalarCriteria::GeometricTolerance) ++HighVertices;
        else return false;
    }
    if (LowVertices != 6 || HighVertices != 6) return false;

    int OuterLoop = -1;
    std::vector<int> HoleLoops;
    for (int Loop : Cap.Loops)
    {
        if (Loop < 0 || Loop >= static_cast<int>(Source.Loops.size())) return false;
        const BrepLoop& L = Source.Loops[Loop];
        if (L.Outer && L.Coedges.size() == 4 && OuterLoop < 0) OuterLoop = Loop;
        else if (!L.Outer && L.Coedges.size() == 1) HoleLoops.push_back(Loop);
        else return false;
    }
    if (OuterLoop < 0 || HoleLoops.size() != 2) return false;

    std::vector<Vec3> Outer;
    for (int Coedge : Source.Loops[OuterLoop].Coedges)
    {
        if (Coedge < 0 || Coedge >= static_cast<int>(Source.Coedges.size())) return false;
        const BrepCoedge& C = Source.Coedges[Coedge];
        if (C.Edge < 0 || C.Edge >= static_cast<int>(Source.Edges.size())) return false;
        const BrepEdge& E = Source.Edges[C.Edge];
        if (E.Curve.Classification != CurveClassification::Line || E.Curve.Degree != 1 || E.Coedges.size() != 2 ||
            E.VertexStart < 0 || E.VertexEnd < 0 || E.VertexStart == E.VertexEnd) return false;
        const int Start = C.Reversed ? E.VertexEnd : E.VertexStart;
        const int End = C.Reversed ? E.VertexStart : E.VertexEnd;
        const Vec3 A = Source.Vertices[Start].Point, B = Source.Vertices[End].Point;
        if (std::fabs(A.Z - High.Z) > ScalarCriteria::GeometricTolerance ||
            std::fabs(B.Z - High.Z) > ScalarCriteria::GeometricTolerance ||
            (!Close(A.X, B.X, ScalarCriteria::GeometricTolerance) && !Close(A.Y, B.Y, ScalarCriteria::GeometricTolerance))) return false;
        Outer.push_back(A);
    }
    if (Outer.size() != 4) return false;
    const double MinX = std::min_element(Outer.begin(), Outer.end(), [](Vec3 A, Vec3 B) { return A.X < B.X; })->X;
    const double MaxX = std::max_element(Outer.begin(), Outer.end(), [](Vec3 A, Vec3 B) { return A.X < B.X; })->X;
    const double MinY = std::min_element(Outer.begin(), Outer.end(), [](Vec3 A, Vec3 B) { return A.Y < B.Y; })->Y;
    const double MaxY = std::max_element(Outer.begin(), Outer.end(), [](Vec3 A, Vec3 B) { return A.Y < B.Y; })->Y;
    if (MaxX - MinX <= ScalarCriteria::MergeTolerance || MaxY - MinY <= ScalarCriteria::MergeTolerance) return false;
    const std::array<Vec3, 4> Corners{{ { MinX, MinY, High.Z }, { MaxX, MinY, High.Z },
                                         { MaxX, MaxY, High.Z }, { MinX, MaxY, High.Z } }};
    for (const Vec3& Corner : Corners)
    {
        bool Found = false;
        for (const Vec3& P : Outer) if (ClosePoint(P, Corner, ScalarCriteria::GeometricTolerance)) { Found = true; break; }
        if (!Found) return false;
    }

    Centres.clear(); Radii.clear();
    for (int Loop : HoleLoops)
    {
        const int Coedge = Source.Loops[Loop].Coedges.front();
        if (Coedge < 0 || Coedge >= static_cast<int>(Source.Coedges.size())) return false;
        const int EdgeIndex = Source.Coedges[Coedge].Edge;
        if (EdgeIndex < 0 || EdgeIndex >= static_cast<int>(Source.Edges.size())) return false;
        const BrepEdge& E = Source.Edges[EdgeIndex];
        if (E.Curve.Classification != CurveClassification::Circle || !E.Curve.Rational() || !E.Curve.Closed() || E.Coedges.size() != 2 ||
            std::fabs(E.Curve.AxisZ.Normalised().Dot(Vec3::UnitZ())) < 1.0 - UnitTolerance) return false;
        const Box3 B = E.Curve.Bounds();
        const double Radius = 0.25 * ((B.High.X - B.Low.X) + (B.High.Y - B.Low.Y));
        const Vec3 Centre{ 0.5 * (B.Low.X + B.High.X), 0.5 * (B.Low.Y + B.High.Y), Low.Z };
        if (!std::isfinite(Radius) || Radius <= ScalarCriteria::MergeTolerance ||
            std::fabs((B.High.X - B.Low.X) - 2.0 * Radius) > ScalarCriteria::GeometricTolerance ||
            std::fabs((B.High.Y - B.Low.Y) - 2.0 * Radius) > ScalarCriteria::GeometricTolerance ||
            Centre.X <= MinX + Radius + ScalarCriteria::MergeTolerance || Centre.X >= MaxX - Radius - ScalarCriteria::MergeTolerance ||
            Centre.Y <= MinY + Radius + ScalarCriteria::MergeTolerance || Centre.Y >= MaxY - Radius - ScalarCriteria::MergeTolerance) return false;
        Centres.push_back(Centre); Radii.push_back(Radius);
    }
    if (Centres.size() != 2 || std::hypot(Centres[0].X - Centres[1].X, Centres[0].Y - Centres[1].Y) <= Radii[0] + Radii[1] + ScalarCriteria::MergeTolerance) return false;
    if (Centres[1].X < Centres[0].X || (Close(Centres[1].X, Centres[0].X, ScalarCriteria::GeometricTolerance) && Centres[1].Y < Centres[0].Y))
    { std::swap(Centres[0], Centres[1]); std::swap(Radii[0], Radii[1]); }

    int CircularEdges = 0;
    for (const BrepEdge& E : Source.Edges)
    {
        if (E.Curve.Classification == CurveClassification::Circle)
        {
            if (!E.Curve.Rational() || !E.Curve.Closed() || E.Coedges.size() != 2) return false;
            const Box3 CircleBounds = E.Curve.Bounds();
            if ((std::fabs(CircleBounds.Low.Z - Low.Z) > ScalarCriteria::GeometricTolerance &&
                 std::fabs(CircleBounds.Low.Z - High.Z) > ScalarCriteria::GeometricTolerance) ||
                std::fabs(CircleBounds.High.Z - CircleBounds.Low.Z) > ScalarCriteria::GeometricTolerance) return false;
            ++CircularEdges;
        }
        else if (E.Curve.Classification == CurveClassification::Line)
        {
            if (E.Curve.Degree != 1 || E.Coedges.size() != 2 || E.VertexStart < 0 || E.VertexEnd < 0 ||
                E.VertexStart >= static_cast<int>(Source.Vertices.size()) || E.VertexEnd >= static_cast<int>(Source.Vertices.size())) return false;
            const Vec3 A = Source.Vertices[E.VertexStart].Point, B = Source.Vertices[E.VertexEnd].Point;
            const bool AOnLevel = std::fabs(A.Z - Low.Z) <= ScalarCriteria::GeometricTolerance ||
                                  std::fabs(A.Z - High.Z) <= ScalarCriteria::GeometricTolerance;
            const bool BOnLevel = std::fabs(B.Z - Low.Z) <= ScalarCriteria::GeometricTolerance ||
                                  std::fabs(B.Z - High.Z) <= ScalarCriteria::GeometricTolerance;
            if (!AOnLevel || !BOnLevel) return false;
            if (std::fabs(A.Z - B.Z) > ScalarCriteria::GeometricTolerance &&
                (!Close(A.X, B.X, ScalarCriteria::GeometricTolerance) || !Close(A.Y, B.Y, ScalarCriteria::GeometricTolerance))) return false;
        }
        else return false;
    }
    if (CircularEdges != 4) return false;
    for (const BrepFace& F : Source.Faces)
        if ((F.Surface.Classification != SurfaceClassification::Plane && F.Surface.Classification != SurfaceClassification::Extrusion) ||
            F.Loops.empty() || F.Loops.size() > 3) return false;
    return true;
}

[[nodiscard]] Deliver<BrepBody> BuildExtrudedTwinHoledPrismFaceOffset(const BrepBody& Source, int Face, double Distance) noexcept
{
    Vec3 Low{}, High{}; std::vector<Vec3> Centres; std::vector<double> Radii;
    if (!ReadExtrudedTwinHoledPrism(Source, Face, Low, High, Centres, Radii))
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "twin-holed-prism offset requires a rectangular genus-two prism and its upper three-loop cap");
    if (!std::isfinite(Distance) || Distance <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "twin-holed-prism offset distance must be finite and positive");
    const Deliver<NurbsCurve> Outer = NurbsCurve::Polyline({ { Low.X, Low.Y, Low.Z }, { High.X, Low.Y, Low.Z },
                                                               { High.X, High.Y, Low.Z }, { Low.X, High.Y, Low.Z } }, true);
    if (!Outer) return Deliver<BrepBody>::Reject(Outer.Denial.Reason, Outer.Denial.Detail);
    std::vector<NurbsCurve> Loops{ Outer.Payload };
    for (size_t I = 0; I < Centres.size(); ++I)
    {
        const Deliver<NurbsCurve> Hole = NurbsCurve::Circle({ Centres[I].X, Centres[I].Y, Low.Z }, Vec3::UnitZ(), Radii[I]);
        if (!Hole) return Deliver<BrepBody>::Reject(Hole.Denial.Reason, Hole.Denial.Detail);
        Loops.push_back(Hole.Payload);
    }
    Deliver<BrepBody> Result = BrepBody::Extrude(Loops, Vec3::UnitZ(), High.Z - Low.Z + Distance);
    if (!Result) return Result;
    Result.Payload.Orient();
    const BodyReport Report = Result.Payload.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 2 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Result.Payload.Vertices.size() != 12 ||
        Result.Payload.Edges.size() != 18 || Result.Payload.Coedges.size() != 36 || Result.Payload.Loops.size() != 12 ||
        Result.Payload.Faces.size() != 8)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "twin-holed-prism offset did not retain V12/E18/C36/L12/F8 topology");
    return Result;
}

[[nodiscard]] Deliver<BrepBody> BuildExtrudedEllipticalPrismFaceOffset(const BrepBody& Source, int Face, double Distance) noexcept
{
    Vec3 Low{}, High{}, Centre{};
    double MajorRadius = 0.0, MinorRadius = 0.0;
    if (!ReadExtrudedEllipticalPrism(Source, Face, Low, High, MajorRadius, MinorRadius, Centre))
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "elliptical-prism offset requires an exact axis-aligned elliptical prism and its upper cap");
    if (!std::isfinite(Distance) || Distance <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "elliptical-prism offset distance must be finite and positive");
    const Deliver<NurbsCurve> Profile = NurbsCurve::Ellipse({ Centre.X, Centre.Y, Low.Z }, Vec3::UnitZ(), Vec3::UnitX(), MajorRadius, MinorRadius);
    if (!Profile) return Deliver<BrepBody>::Reject(Profile.Denial.Reason, Profile.Denial.Detail);
    Deliver<BrepBody> Result = BrepBody::Extrude(Profile.Payload, Vec3::UnitZ(), High.Z - Low.Z + Distance);
    if (!Result) return Result;
    Result.Payload.Orient();
    const BodyReport Report = Result.Payload.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Result.Payload.Vertices.size() != 2 ||
        Result.Payload.Edges.size() != 3 || Result.Payload.Coedges.size() != 6 || Result.Payload.Loops.size() != 3 ||
        Result.Payload.Faces.size() != 3)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "elliptical-prism offset did not retain V2/E3/C6/L3/F3 topology");
    return Result;
}

[[nodiscard]] bool ReadObliqueTriangularPrism(const BrepBody& Source, int Face, std::vector<Vec3>& Top,
                                              Vec3& Translation) noexcept
{
    const BodyReport R = Source.Validate();
    if (!R.Solid() || R.Hulls != 1 || R.Genus != 0 || R.OpenEdges != 0 || R.NonManifoldEdges != 0 ||
        R.MisorientedEdges != 0 || Source.Vertices.size() != 6 || Source.Edges.size() != 9 ||
        Source.Coedges.size() != 18 || Source.Loops.size() != 5 || Source.Faces.size() != 5)
        return false;
    if (Face < 0 || Face >= static_cast<int>(Source.Faces.size())) return false;
    const BrepFace& Cap = Source.Faces[Face];
    if (Cap.Surface.Classification != SurfaceClassification::Plane || Cap.Loops.size() != 1 ||
        Source.Loops[Cap.Loops.front()].Coedges.size() != 3) return false;
    const Vec3 Normal = Source.FaceNormal(Face,
        0.5 * (Cap.Surface.DomainStartU() + Cap.Surface.DomainEndU()),
        0.5 * (Cap.Surface.DomainStartV() + Cap.Surface.DomainEndV())).Normalised();
    if (Normal.Dot(Vec3::UnitZ()) < 1.0 - UnitTolerance) return false;

    const Box3 Bounds = Source.Bounds();
    if (Bounds.High.Z - Bounds.Low.Z <= ScalarCriteria::MergeTolerance) return false;
    int LowVertices = 0, HighVertices = 0;
    for (const BrepVertex& Vertex : Source.Vertices)
    {
        if (std::fabs(Vertex.Point.Z - Bounds.Low.Z) <= ScalarCriteria::GeometricTolerance) ++LowVertices;
        else if (std::fabs(Vertex.Point.Z - Bounds.High.Z) <= ScalarCriteria::GeometricTolerance) ++HighVertices;
        else return false;
    }
    if (LowVertices != 3 || HighVertices != 3) return false;

    std::vector<int> CapEdges;
    Top.clear(); Top.reserve(3);
    for (int Coedge : Source.Loops[Cap.Loops.front()].Coedges)
    {
        if (Coedge < 0 || Coedge >= static_cast<int>(Source.Coedges.size())) return false;
        const BrepCoedge& C = Source.Coedges[Coedge];
        if (C.Edge < 0 || C.Edge >= static_cast<int>(Source.Edges.size())) return false;
        const BrepEdge& E = Source.Edges[C.Edge];
        if (E.Curve.Classification != CurveClassification::Line || E.Curve.Degree != 1 || E.Coedges.size() != 2 ||
            E.VertexStart < 0 || E.VertexEnd < 0 || E.VertexStart == E.VertexEnd) return false;
        const int Start = C.Reversed ? E.VertexEnd : E.VertexStart;
        const int End = C.Reversed ? E.VertexStart : E.VertexEnd;
        if (Start < 0 || End < 0 || Start >= static_cast<int>(Source.Vertices.size()) ||
            End >= static_cast<int>(Source.Vertices.size())) return false;
        const Vec3 A = Source.Vertices[Start].Point, B = Source.Vertices[End].Point;
        if (std::fabs(A.Z - Bounds.High.Z) > ScalarCriteria::GeometricTolerance ||
            std::fabs(B.Z - Bounds.High.Z) > ScalarCriteria::GeometricTolerance) return false;
        CapEdges.push_back(C.Edge); Top.push_back(A);
    }
    if (Top.size() != 3) return false;
    const double TriangleCross = (Top[1].X - Top[0].X) * (Top[2].Y - Top[0].Y) -
                                 (Top[1].Y - Top[0].Y) * (Top[2].X - Top[0].X);
    if (std::fabs(TriangleCross) <= ScalarCriteria::GeometricTolerance) return false;

    for (const BrepEdge& E : Source.Edges)
        if (E.Curve.Classification != CurveClassification::Line || E.Curve.Degree != 1 || E.Coedges.size() != 2 ||
            E.VertexStart < 0 || E.VertexEnd < 0 || E.VertexStart == E.VertexEnd) return false;

    bool FoundTranslation = false;
    for (size_t I = 0; I < Source.Edges.size(); ++I)
    {
        if (std::find(CapEdges.begin(), CapEdges.end(), static_cast<int>(I)) != CapEdges.end()) continue;
        const BrepEdge& E = Source.Edges[I];
        const Vec3 A = Source.Vertices[E.VertexStart].Point, B = Source.Vertices[E.VertexEnd].Point;
        const bool AHigh = std::fabs(A.Z - Bounds.High.Z) <= ScalarCriteria::GeometricTolerance;
        const bool BHigh = std::fabs(B.Z - Bounds.High.Z) <= ScalarCriteria::GeometricTolerance;
        const bool ALow = std::fabs(A.Z - Bounds.Low.Z) <= ScalarCriteria::GeometricTolerance;
        const bool BLow = std::fabs(B.Z - Bounds.Low.Z) <= ScalarCriteria::GeometricTolerance;
        if (AHigh == BHigh || !(ALow || BLow) || !(AHigh || BHigh)) continue;
        const Vec3 Candidate = AHigh ? A - B : B - A;
        if (!FoundTranslation) { Translation = Candidate; FoundTranslation = true; }
        else if (!ClosePoint(Candidate, Translation, ScalarCriteria::GeometricTolerance)) return false;
    }
    if (!FoundTranslation || Translation.Z <= ScalarCriteria::MergeTolerance ||
        std::hypot(Translation.X, Translation.Y) <= ScalarCriteria::MergeTolerance) return false;
    int SideFaces = 0;
    for (const BrepFace& F : Source.Faces)
    {
        if ((F.Surface.Classification != SurfaceClassification::Plane && F.Surface.Classification != SurfaceClassification::Extrusion) ||
            F.Loops.size() != 1) return false;
        if (F.Loops.front() == Cap.Loops.front()) continue;
        ++SideFaces;
    }
    return SideFaces == 4;
}

[[nodiscard]] Deliver<BrepBody> BuildObliqueTriangularPrismFaceOffset(const BrepBody& Source, int Face, double Distance) noexcept
{
    std::vector<Vec3> Top;
    Vec3 Translation{};
    if (!ReadObliqueTriangularPrism(Source, Face, Top, Translation))
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "oblique triangular-prism offset requires a non-vertical straight prism and its upper cap");
    if (!std::isfinite(Distance) || Distance <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "oblique triangular-prism offset distance must be finite and positive");
    const Vec3 BaseA = Top[0] - Translation;
    const Vec3 BaseB = Top[1] - Translation;
    const Vec3 BaseC = Top[2] - Translation;
    const Deliver<NurbsCurve> Profile = NurbsCurve::Polyline({ BaseA, BaseB, BaseC }, true);
    if (!Profile) return Deliver<BrepBody>::Reject(Profile.Denial.Reason, Profile.Denial.Detail);
    const Vec3 NewTranslation = Translation + Vec3::UnitZ() * Distance;
    const Deliver<BrepBody> Result = BrepBody::Extrude(Profile.Payload, NewTranslation.Normalised(), NewTranslation.Length());
    if (!Result) return Result;
    BrepBody Output = Result.Payload;
    Output.Orient();
    const BodyReport Report = Output.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Output.Vertices.size() != 6 ||
        Output.Edges.size() != 9 || Output.Coedges.size() != 18 || Output.Loops.size() != 5 || Output.Faces.size() != 5)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "oblique triangular-prism offset did not retain V6/E9/C18/L5/F5 topology");
    return Deliver<BrepBody>::Accept(std::move(Output));
}

[[nodiscard]] bool ReadTriangularPrismDraft(const BrepBody& Source, int Face, std::vector<Vec3>& Bottom,
                                            std::vector<Vec3>& Top, int& SelectedA, int& SelectedB,
                                            Vec3& OutwardNormal, double& Low, double& High) noexcept
{
    const BodyReport R = Source.Validate();
    if (!R.Solid() || R.Hulls != 1 || R.Genus != 0 || R.OpenEdges != 0 || R.NonManifoldEdges != 0 ||
        R.MisorientedEdges != 0 || Source.Vertices.size() != 6 || Source.Edges.size() != 9 ||
        Source.Coedges.size() != 18 || Source.Loops.size() != 5 || Source.Faces.size() != 5)
        return false;
    if (Face < 0 || Face >= static_cast<int>(Source.Faces.size())) return false;
    const BrepFace& Wall = Source.Faces[Face];
    if (Wall.Surface.Classification != SurfaceClassification::Extrusion || Wall.Loops.size() != 1 ||
        Source.Loops[Wall.Loops.front()].Coedges.size() != 4) return false;
    Low = Source.Bounds().Low.Z; High = Source.Bounds().High.Z;
    if (High - Low <= ScalarCriteria::MergeTolerance) return false;
    int LowVertices = 0, HighVertices = 0;
    for (const BrepVertex& Vertex : Source.Vertices)
    {
        if (std::fabs(Vertex.Point.Z - Low) <= ScalarCriteria::GeometricTolerance) ++LowVertices;
        else if (std::fabs(Vertex.Point.Z - High) <= ScalarCriteria::GeometricTolerance) ++HighVertices;
        else return false;
    }
    if (LowVertices != 3 || HighVertices != 3) return false;
    OutwardNormal = Source.FaceNormal(Face, 0.5 * (Wall.Surface.DomainStartU() + Wall.Surface.DomainEndU()),
                                      0.5 * (Wall.Surface.DomainStartV() + Wall.Surface.DomainEndV())).Normalised();
    OutwardNormal.Z = 0.0; OutwardNormal = OutwardNormal.Normalised();
    if (OutwardNormal.LengthSquared() <= 0.5) return false;

    for (const BrepFace& F : Source.Faces)
        if ((F.Surface.Classification != SurfaceClassification::Plane && F.Surface.Classification != SurfaceClassification::Extrusion) ||
            F.Loops.size() != 1) return false;
    for (const BrepEdge& E : Source.Edges)
        if (E.Curve.Classification != CurveClassification::Line || E.Curve.Degree != 1 || E.Coedges.size() != 2 ||
            E.VertexStart < 0 || E.VertexEnd < 0 || E.VertexStart == E.VertexEnd) return false;

    const int WallLoop = Wall.Loops.front();
    std::vector<int> WallLowVertices;
    for (int Coedge : Source.Loops[WallLoop].Coedges)
    {
        if (Coedge < 0 || Coedge >= static_cast<int>(Source.Coedges.size())) return false;
        const BrepCoedge& C = Source.Coedges[Coedge];
        const BrepEdge& E = Source.Edges[C.Edge];
        const Vec3 A = Source.Vertices[E.VertexStart].Point, B = Source.Vertices[E.VertexEnd].Point;
        const bool ALow = std::fabs(A.Z - Low) <= ScalarCriteria::GeometricTolerance;
        const bool BLow = std::fabs(B.Z - Low) <= ScalarCriteria::GeometricTolerance;
        const bool AHigh = std::fabs(A.Z - High) <= ScalarCriteria::GeometricTolerance;
        const bool BHigh = std::fabs(B.Z - High) <= ScalarCriteria::GeometricTolerance;
        if (ALow && BLow) { WallLowVertices.push_back(E.VertexStart); WallLowVertices.push_back(E.VertexEnd); }
        else if (!(AHigh && BHigh) && !((ALow && BHigh) || (AHigh && BLow))) return false;
    }
    if (WallLowVertices.size() != 2) return false;
    const auto IsSameXY = [&](Vec3 A, Vec3 B) { return std::fabs(A.X - B.X) <= ScalarCriteria::GeometricTolerance &&
                                                       std::fabs(A.Y - B.Y) <= ScalarCriteria::GeometricTolerance; };
    for (const BrepVertex& V : Source.Vertices) if (std::fabs(V.Point.Z - Low) <= ScalarCriteria::GeometricTolerance) Bottom.push_back(V.Point);
    if (Bottom.size() != 3) return false;
    const Vec3 Centroid = (Bottom[0] + Bottom[1] + Bottom[2]) / 3.0;
    std::sort(Bottom.begin(), Bottom.end(), [&](Vec3 A, Vec3 B) {
        return std::atan2(A.Y - Centroid.Y, A.X - Centroid.X) < std::atan2(B.Y - Centroid.Y, B.X - Centroid.X);
    });
    Top.reserve(3);
    for (const Vec3& P : Bottom)
    {
        bool Found = false;
        for (const BrepVertex& V : Source.Vertices)
            if (std::fabs(V.Point.Z - High) <= ScalarCriteria::GeometricTolerance && IsSameXY(P, { V.Point.X, V.Point.Y, Low }))
            { Top.push_back(V.Point); Found = true; break; }
        if (!Found) return false;
    }
    auto FindIndex = [&](int VertexIndex) {
        const Vec3 P = Source.Vertices[VertexIndex].Point;
        for (int I = 0; I < static_cast<int>(Bottom.size()); ++I) if (IsSameXY(Bottom[I], P)) return I;
        return -1;
    };
    SelectedA = FindIndex(WallLowVertices[0]); SelectedB = FindIndex(WallLowVertices[1]);
    if (SelectedA < 0 || SelectedB < 0 || SelectedA == SelectedB) return false;
    const double Area2 = (Bottom[1].X - Bottom[0].X) * (Bottom[2].Y - Bottom[0].Y) -
                         (Bottom[1].Y - Bottom[0].Y) * (Bottom[2].X - Bottom[0].X);
    const double TopArea2 = (Top[1].X - Top[0].X) * (Top[2].Y - Top[0].Y) -
                            (Top[1].Y - Top[0].Y) * (Top[2].X - Top[0].X);
    return std::fabs(Area2) > ScalarCriteria::GeometricTolerance && Area2 * TopArea2 > 0.0;
}

[[nodiscard]] Deliver<BrepBody> BuildTriangularPrismDraft(const BrepBody& Source, int Face, double AngleRadians) noexcept
{
    std::vector<Vec3> Bottom, Top;
    int SelectedA = -1, SelectedB = -1;
    Vec3 OutwardNormal{};
    double Low = 0.0, High = 0.0;
    if (!ReadTriangularPrismDraft(Source, Face, Bottom, Top, SelectedA, SelectedB, OutwardNormal, Low, High))
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "triangular-prism draft requires a vertical triangular side face");
    if (!std::isfinite(AngleRadians) || std::fabs(AngleRadians) < ScalarCriteria::KernelTolerance ||
        std::fabs(AngleRadians) >= ScalarCriteria::HalfPi - ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "triangular-prism draft angle must be finite, non-zero, and below 90 degrees");
    const double Delta = std::tan(AngleRadians) * (High - Low);
    if (!std::isfinite(Delta) || std::fabs(Delta) <= ScalarCriteria::KernelTolerance) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "triangular-prism draft produces no measurable change");
    Top[SelectedA] += OutwardNormal * Delta;
    Top[SelectedB] += OutwardNormal * Delta;
    const double TopArea2 = (Top[1].X - Top[0].X) * (Top[2].Y - Top[0].Y) -
                            (Top[1].Y - Top[0].Y) * (Top[2].X - Top[0].X);
    const double BottomArea2 = (Bottom[1].X - Bottom[0].X) * (Bottom[2].Y - Bottom[0].Y) -
                               (Bottom[1].Y - Bottom[0].Y) * (Bottom[2].X - Bottom[0].X);
    if (BottomArea2 * TopArea2 <= ScalarCriteria::GeometricTolerance) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "triangular-prism draft collapses or inverts the upper profile");

    std::vector<NurbsSurface> Surfaces;
    for (int I = 0; I < 3; ++I)
    {
        const int J = (I + 1) % 3;
        const Deliver<NurbsCurve> Lower = NurbsCurve::Line(Bottom[I], Bottom[J]);
        const Deliver<NurbsCurve> Upper = NurbsCurve::Line(Top[I], Top[J]);
        if (!Lower || !Upper) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "triangular-prism draft generated a degenerate edge");
        const Deliver<NurbsSurface> Wall = NurbsSurface::Ruled(Lower.Payload, Upper.Payload);
        if (!Wall) return Deliver<BrepBody>::Reject(Wall.Denial.Reason, Wall.Denial.Detail);
        Surfaces.push_back(Wall.Payload);
    }
    Deliver<BrepBody> Sewn = BrepBody::Sew(Surfaces, ScalarCriteria::MergeTolerance, true);
    if (!Sewn) return Sewn;
    Sewn.Payload.Orient();
    const BodyReport Report = Sewn.Payload.Validate();
    if (!Report.Solid() || Report.Hulls != 1 || Report.Genus != 0 || Report.OpenEdges != 0 ||
        Report.NonManifoldEdges != 0 || Report.MisorientedEdges != 0 || Sewn.Payload.Vertices.size() != 6 ||
        Sewn.Payload.Edges.size() != 9 || Sewn.Payload.Coedges.size() != 18 || Sewn.Payload.Loops.size() != 5 ||
        Sewn.Payload.Faces.size() != 5)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "triangular-prism draft did not retain V6/E9/C18/L5/F5 topology");
    return Sewn;
}

[[nodiscard]] Deliver<BrepBody> ShellByExtrudedU(const BoxFrame& B, int Axis, int Sign, double T) noexcept
{
    // Offset a rectangular prism in a local 2D cross-section and extrude it along the
    // remaining box axis. This is an exact closed U-profile: the selected face is the opening,
    // the inner floor/walls are real B-rep faces, and no Boolean or zero-area rim is involved.
    Vec3 N = AxisVector(Axis, static_cast<double>(Sign));
    int UAxis = 0, VAxis = 1;
    if (Axis == 0) { UAxis = 1; VAxis = 2; }
    if (Axis == 1) { UAxis = 0; VAxis = 2; }
    if (Axis == 2) { UAxis = 0; VAxis = 1; }
    Vec3 U = AxisVector(UAxis), V = AxisVector(VAxis);
    const double Q0 = Sign > 0 ? Component(B.Low, Axis) : -Component(B.High, Axis);
    const double Q1 = Sign > 0 ? Component(B.High, Axis) : -Component(B.Low, Axis);
    const double U0 = Component(B.Low, UAxis), U1 = Component(B.High, UAxis);
    const double V0 = Component(B.Low, VAxis), V1 = Component(B.High, VAxis);
    const double qi0 = Q0 + T, qi1 = Q1 - T, ui0 = U0 + T, ui1 = U1 - T;
    auto P = [&](double Q, double X, double Along) { return N * Q + U * X + V * Along; };
    // The profile walks outer bottom, outer opening edge, inset inner wall, inner floor,
    // then back out around the opposite wall. The small diagonal transitions are the exact
    // mitered material at the open rim, so the route remains manifold at all four corners.
    std::vector<Vec3> Points = {
        P(Q0, U0, V0), P(Q0, U1, V0), P(Q1, U1, V0),
        P(qi1, ui1, V0), P(qi0, ui1, V0), P(qi0, ui0, V0),
        P(qi1, ui0, V0), P(Q1, U0, V0) };
    Deliver<NurbsCurve> Profile = NurbsCurve::Polyline(Points, true);
    if (!Profile) return Deliver<BrepBody>::Reject(Profile.Denial.Reason, Profile.Denial.Detail);
    return BrepBody::Extrude(Profile.Payload, V, V1 - V0);
}

} // namespace

bool FaceEditSolver::IsCanonicalBox(const BrepBody& Source) noexcept
{
    BoxFrame B;
    return ReadBox(Source, B);
}

Deliver<BrepBody> FaceEditSolver::Heal(const BrepBody& Source, double Tolerance) noexcept
{
    if (Tolerance <= 0.0) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "healing tolerance must be positive");
    if (Source.Faces.empty()) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "cannot heal an empty body");
    std::vector<NurbsSurface> Surfaces;
    Surfaces.reserve(Source.Faces.size());
    bool HasTrimmedFace = false;
    for (const BrepFace& F : Source.Faces)
    {
        HasTrimmedFace = HasTrimmedFace || !F.Natural;
        const Box3 B = F.Surface.Bounds();
        if (B.Empty() || B.Diagonal() <= Tolerance) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "sliver face detected");
        if (F.Natural) Surfaces.push_back(F.Surface);
    }
    // Capped extrusions and shells legitimately contain trimmed planar caps. Rebuilding those
    // surfaces from their support would discard their loops, so validate and return an immutable
    // copy instead of pretending a support-only re-sew is a healing operation.
    if (HasTrimmedFace)
    {
        const BodyReport Existing = Source.Validate();
        for (const BrepEdge& E : Source.Edges)
            if (E.Curve.Length() <= Tolerance) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "sliver edge detected; healing refused to collapse it");
        if (Existing.NonManifoldEdges != 0 || Existing.MisorientedEdges != 0)
            return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "healing found non-manifold or misoriented trimmed topology");
        return Deliver<BrepBody>::Accept(Source);
    }
    Deliver<BrepBody> Result = BrepBody::Sew(Surfaces, Tolerance, false);
    if (!Result) return Result;
    const BodyReport R = Result.Payload.Validate();
    // Re-sewing is the only safe sliver cleanup in this kernel: it removes orphaned duplicate
    // boundaries while preserving every source surface. A tiny edge is rejected, never collapsed.
    for (const BrepEdge& E : Result.Payload.Edges)
        if (E.Curve.Length() <= Tolerance) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "sliver edge detected; healing refused to collapse it");
    if (R.NonManifoldEdges != 0 || R.MisorientedEdges != 0)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "healing left non-manifold or misoriented topology");
    // Re-sewing natural analytic faces can split seam edges into a disconnected shell even when
    // the source is already a valid solid (notably a ruled face loft with a periodic side). Never
    // return that approximation: preserve the validated source transactionally instead.
    const BodyReport Existing = Source.Validate();
    if (Existing.Solid() && (!R.Solid() || R.Hulls != Existing.Hulls || R.OpenEdges != 0))
        return Deliver<BrepBody>::Accept(Source);
    return Result;
}

Deliver<BrepBody> FaceEditSolver::RemoveSlivers(const BrepBody& Source, double Tolerance) noexcept
{
    return Heal(Source, Tolerance);
}

Deliver<BrepBody> FaceEditSolver::OffsetFace(const BrepBody& Source, int Face, double Distance) noexcept
{
    FaceFrame F;
    if (!ReadFace(Source, Face, F))
    {
        Deliver<BrepBody> Pentagon = OffsetExtrudedConvexPrism(Source, Face, Distance);
        if (Pentagon) return Pentagon;
        Deliver<BrepBody> Concave = OffsetExtrudedConcavePrism(Source, Face, Distance);
        if (Concave) return Concave;
        Deliver<BrepBody> Holed = OffsetExtrudedHoledPrism(Source, Face, Distance);
        if (Holed) return Holed;
        Deliver<BrepBody> TwinHoled = OffsetExtrudedTwinHoledPrism(Source, Face, Distance);
        if (TwinHoled) return TwinHoled;
        Deliver<BrepBody> Elliptical = OffsetExtrudedEllipticalPrism(Source, Face, Distance);
        return Elliptical ? Elliptical : OffsetObliqueTriangularPrism(Source, Face, Distance);
    }
    if (!std::isfinite(Distance) || std::fabs(Distance) <= ScalarCriteria::KernelTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "face offset distance is zero or non-finite");
    BoxFrame B = F.Box;
    if (F.Axis == 0) { if (F.Sign > 0) B.High.X += Distance; else B.Low.X -= Distance; }
    if (F.Axis == 1) { if (F.Sign > 0) B.High.Y += Distance; else B.Low.Y -= Distance; }
    if (F.Axis == 2) { if (F.Sign > 0) B.High.Z += Distance; else B.Low.Z -= Distance; }
    if (B.High.X - B.Low.X <= ScalarCriteria::MergeTolerance || B.High.Y - B.Low.Y <= ScalarCriteria::MergeTolerance || B.High.Z - B.Low.Z <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "face offset would invert or collapse the box");
    return BrepBody::Box(B.Low, B.High);
}

Deliver<BrepBody> FaceEditSolver::ExtendFace(const BrepBody& Source, int Face, double Distance) noexcept
{
    FaceFrame F;
    if (!ReadFace(Source, Face, F)) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "exact face extension currently requires a canonical axis-aligned box face");
    if (!std::isfinite(Distance) || Distance <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "face extension distance must be positive");
    BoxFrame B = F.Box;
    if (F.Axis != 0) { B.Low.X -= Distance; B.High.X += Distance; }
    if (F.Axis != 1) { B.Low.Y -= Distance; B.High.Y += Distance; }
    if (F.Axis != 2) { B.Low.Z -= Distance; B.High.Z += Distance; }
    return BrepBody::Box(B.Low, B.High);
}

Deliver<BrepBody> FaceEditSolver::TrimFace(const BrepBody& Source, int Face, double Distance) noexcept
{
    FaceFrame F;
    if (!ReadFace(Source, Face, F)) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "exact face trim currently requires a canonical axis-aligned box face");
    if (!std::isfinite(Distance) || Distance <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "face trim distance must be positive");
    BoxFrame B = F.Box;
    if (F.Axis != 0) { B.Low.X += Distance; B.High.X -= Distance; }
    if (F.Axis != 1) { B.Low.Y += Distance; B.High.Y -= Distance; }
    if (F.Axis != 2) { B.Low.Z += Distance; B.High.Z -= Distance; }
    if (B.High.X - B.Low.X <= ScalarCriteria::MergeTolerance || B.High.Y - B.Low.Y <= ScalarCriteria::MergeTolerance || B.High.Z - B.Low.Z <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "face trim would collapse a neighbouring span");
    return BrepBody::Box(B.Low, B.High);
}

Deliver<BrepBody> FaceEditSolver::Draft(const BrepBody& Source, int Face, double AngleRadians) noexcept
{
    FaceFrame F;
    if (!ReadFace(Source, Face, F))
    {
        Deliver<BrepBody> Triangular = DraftExtrudedTriangularPrism(Source, Face, AngleRadians);
        if (Triangular) return Triangular;
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "exact draft currently requires a canonical box or bounded triangular-prism side face");
    }
    if (!std::isfinite(AngleRadians) || std::fabs(AngleRadians) >= ScalarCriteria::HalfPi - ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "draft angle must be finite and strictly below 90 degrees");
    const double Delta = std::tan(AngleRadians) * (F.Box.High.Z - F.Box.Low.Z);
    if (!std::isfinite(Delta) || std::fabs(Delta) <= ScalarCriteria::KernelTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "draft produces no measurable change");
    return BuildDraftedPrism(F.Box, F.Axis, F.Sign, Delta);
}

Deliver<BrepBody> FaceEditSolver::DeleteFace(const BrepBody& Source, int Face) noexcept
{
    BoxFrame B;
    if (!ReadBox(Source, B) || Face < 0 || Face >= static_cast<int>(Source.Faces.size()))
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "face deletion currently requires a canonical axis-aligned box");
    std::vector<NurbsSurface> Surfaces;
    for (int I = 0; I < static_cast<int>(Source.Faces.size()); ++I) if (I != Face) Surfaces.push_back(Source.Faces[I].Surface);
    return SewNatural(Surfaces, false);
}

Deliver<BrepBody> FaceEditSolver::ReplaceFace(const BrepBody& Source, int Face, const NurbsSurface& Replacement) noexcept
{
    BoxFrame B;
    if (!ReadBox(Source, B) || Face < 0 || Face >= static_cast<int>(Source.Faces.size()))
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "face replacement currently requires a canonical axis-aligned box");
    const Refusal ReplacementError = Replacement.Validate();
    if (ReplacementError) return Deliver<BrepBody>::Reject(ReplacementError.Reason, ReplacementError.Detail);
    if (!SameRim(Source, Face, Replacement, ScalarCriteria::MergeTolerance))
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "replacement face rim is not identical; adjacent faces were left untouched");
    std::vector<NurbsSurface> Surfaces;
    for (int I = 0; I < static_cast<int>(Source.Faces.size()); ++I) Surfaces.push_back(I == Face ? Replacement : Source.Faces[I].Surface);
    Deliver<BrepBody> Result = SewNatural(Surfaces, true);
    if (!Result) return Result;
    if (Result.Payload.Faces.size() != Source.Faces.size())
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "replacement changed face count during sewing");
    return Result;
}

Deliver<BrepBody> FaceEditSolver::Shell(const BrepBody& Source, int Face, double Thickness) noexcept
{
    FaceFrame F;
    if (!ReadFace(Source, Face, F))
    {
        Deliver<BrepBody> Convex = ShellExtrudedConvexPrism(Source, Face, Thickness);
        return Convex ? Convex : ShellExtrudedConcavePrism(Source, Face, Thickness);
    }
    if (!std::isfinite(Thickness) || Thickness <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "shell thickness must be positive");
    const Vec3 D = F.Box.High - F.Box.Low;
    if (Thickness * 2.0 >= std::min(D.X, std::min(D.Y, D.Z)))
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "shell thickness leaves no positive inner cavity");
    return ShellByExtrudedU(F.Box, F.Axis, F.Sign, Thickness);
}

Deliver<BrepBody> FaceEditSolver::ShellExtrudedConvexPrism(const BrepBody& Source, int Face, double Thickness) noexcept
{
    return BuildExtrudedConvexPrismShell(Source, Face, Thickness);
}

Deliver<BrepBody> FaceEditSolver::ShellExtrudedConcavePrism(const BrepBody& Source, int Face, double Thickness) noexcept
{
    return BuildExtrudedConcavePrismShell(Source, Face, Thickness);
}

Deliver<BrepBody> FaceEditSolver::OffsetExtrudedConvexPrism(const BrepBody& Source, int Face, double Distance) noexcept
{
    return BuildPentagonalPrismFaceOffset(Source, Face, Distance);
}

Deliver<BrepBody> FaceEditSolver::OffsetExtrudedConcavePrism(const BrepBody& Source, int Face, double Distance) noexcept
{
    return BuildExtrudedConcavePrismFaceOffset(Source, Face, Distance);
}

Deliver<BrepBody> FaceEditSolver::OffsetExtrudedHoledPrism(const BrepBody& Source, int Face, double Distance) noexcept
{
    return BuildExtrudedHoledPrismFaceOffset(Source, Face, Distance);
}

Deliver<BrepBody> FaceEditSolver::OffsetExtrudedTwinHoledPrism(const BrepBody& Source, int Face, double Distance) noexcept
{
    return BuildExtrudedTwinHoledPrismFaceOffset(Source, Face, Distance);
}

Deliver<BrepBody> FaceEditSolver::OffsetExtrudedEllipticalPrism(const BrepBody& Source, int Face, double Distance) noexcept
{
    return BuildExtrudedEllipticalPrismFaceOffset(Source, Face, Distance);
}

Deliver<BrepBody> FaceEditSolver::OffsetObliqueTriangularPrism(const BrepBody& Source, int Face, double Distance) noexcept
{
    return BuildObliqueTriangularPrismFaceOffset(Source, Face, Distance);
}

Deliver<BrepBody> FaceEditSolver::DraftExtrudedTriangularPrism(const BrepBody& Source, int Face, double AngleRadians) noexcept
{
    return BuildTriangularPrismDraft(Source, Face, AngleRadians);
}

} // namespace Frontier
