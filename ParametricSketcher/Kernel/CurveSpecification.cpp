//============================================================================================================================================
// 📦 ParametricSketcher/Kernel/CurveSpecification.cpp — NURBS curve algorithms (Piegl & Tiller numbering cited per routine)
//============================================================================================================================================

#include "CurveSpecification.h"
#include <algorithm>
#include <cmath>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  CLASSIFICATION TEXT
//------------------------------------------------------------------------------------------------------------------------

const char* Describe(CurveClassification Classification) noexcept
{
    switch (Classification)
    {
        case CurveClassification::Freeform:  return "Freeform";
        case CurveClassification::Line:      return "Line";
        case CurveClassification::Polyline:  return "Polyline";
        case CurveClassification::Arc:       return "Arc";
        case CurveClassification::Circle:    return "Circle";
        case CurveClassification::Ellipse:   return "Ellipse";
        case CurveClassification::Rectangle: return "Rectangle";
        case CurveClassification::Polygon:   return "Polygon";
        case CurveClassification::Slot:      return "Slot";
    }
    return "Unknown";
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<NurbsCurve> NurbsCurve::Build(int Degree, std::vector<Vec4> Poles, std::vector<double> Knots) noexcept
{
    NurbsCurve Curve;
    Curve.Degree = Degree;
    Curve.Poles  = std::move(Poles);
    Curve.Knots  = std::move(Knots);
    if (Refusal Denial = Curve.Validate()) return Deliver<NurbsCurve>::Reject(Denial.Reason, Denial.Detail);
    return Deliver<NurbsCurve>::Accept(std::move(Curve));
}

Refusal NurbsCurve::Validate() const noexcept
{
    if (Degree < 1) return Refusal::Reject(RefusalReason::InvalidDegree, "degree must be ≥ 1");
    if (static_cast<int>(Poles.size()) <= Degree) return Refusal::Reject(RefusalReason::InvalidDegree, "need more than Degree poles");
    if (Knots.size() != Poles.size() + Degree + 1) return Refusal::Reject(RefusalReason::InvalidKnotVector, "knot count ≠ poles + degree + 1");
    for (size_t I = 1; I < Knots.size(); ++I)
        if (Knots[I] < Knots[I - 1]) return Refusal::Reject(RefusalReason::InvalidKnotVector, "knots must be non-decreasing");
    if (DomainEnd() - DomainStart() <= ScalarCriteria::ParametricEpsilon) return Refusal::Reject(RefusalReason::InvalidKnotVector, "empty domain");
    for (const Vec4& P : Poles)
        if (P.W <= 0.0) return Refusal::Reject(RefusalReason::DegenerateInput, "weights must be positive");
    return Refusal::Accept();
}

static std::vector<double> ClampedUniformKnots(int PoleCount, int Degree) noexcept
{
    std::vector<double> Knots(PoleCount + Degree + 1);
    int Interior = PoleCount - Degree - 1;                                              // interior knot count
    for (int I = 0; I <= Degree; ++I) { Knots[I] = 0.0; Knots[PoleCount + I] = 1.0; }
    for (int I = 1; I <= Interior; ++I) Knots[Degree + I] = static_cast<double>(I) / (Interior + 1);
    return Knots;
}

Deliver<NurbsCurve> NurbsCurve::Line(Vec3 A, Vec3 B) noexcept
{
    if (A.Coincident(B)) return Deliver<NurbsCurve>::Reject(RefusalReason::DegenerateInput, "line endpoints coincide");
    Deliver<NurbsCurve> Result = Build(1, { Vec4(A, 1.0), Vec4(B, 1.0) }, { 0, 0, 1, 1 });
    if (Result) Result.Payload.Classification = CurveClassification::Line;
    return Result;
}

Deliver<NurbsCurve> NurbsCurve::Polyline(const std::vector<Vec3>& Points, bool Closed) noexcept
{
    std::vector<Vec3> Chain = Points;
    if (Closed && Chain.size() > 2 && !Chain.front().Coincident(Chain.back(), ScalarCriteria::MergeTolerance)) Chain.push_back(Chain.front());
    if (Chain.size() < 2) return Deliver<NurbsCurve>::Reject(RefusalReason::DegenerateInput, "polyline needs ≥ 2 points");
    std::vector<Vec4> Poles;
    std::vector<double> Knots{ 0.0, 0.0 };
    double Accumulated = 0.0;
    Poles.emplace_back(Chain[0], 1.0);
    for (size_t I = 1; I < Chain.size(); ++I)
    {
        double Segment = Chain[I].Distance(Chain[I - 1]);
        if (Segment <= ScalarCriteria::KernelTolerance) continue;                       // drop zero-length segments
        Accumulated += Segment;
        Poles.emplace_back(Chain[I], 1.0);
        Knots.push_back(Accumulated);
    }
    Knots.push_back(Accumulated);
    if (Poles.size() < 2) return Deliver<NurbsCurve>::Reject(RefusalReason::DegenerateInput, "all polyline points coincide");
    Deliver<NurbsCurve> Result = Build(1, std::move(Poles), std::move(Knots));
    if (Result) Result.Payload.Classification = Poles.size() == 2 ? CurveClassification::Line : CurveClassification::Polyline;
    return Result;
}

// Piegl & Tiller A7.1 — arc as rational quadratic pieces, each ≤ 90°. Exact for every sweep in (0, 2π].
Deliver<NurbsCurve> NurbsCurve::Arc(Vec3 Centre, Vec3 Normal, double Radius, double StartAngle, double SweepAngle) noexcept
{
    if (Radius <= ScalarCriteria::KernelTolerance) return Deliver<NurbsCurve>::Reject(RefusalReason::DegenerateInput, "zero radius");
    if (std::fabs(SweepAngle) <= ScalarCriteria::AngularTolerance) return Deliver<NurbsCurve>::Reject(RefusalReason::DegenerateInput, "zero sweep");
    SweepAngle = ScalarCriteria::Clamp(SweepAngle, -ScalarCriteria::TwoPi, ScalarCriteria::TwoPi);

    Workplane Axes = Workplane::FromNormal(Centre, Normal);
    int ArcCount = static_cast<int>(std::ceil(std::fabs(SweepAngle) / ScalarCriteria::HalfPi - 1e-9));
    ArcCount = std::max(1, std::min(4, ArcCount));
    double Delta = SweepAngle / ArcCount;
    double Weight = std::cos(Delta * 0.5);

    std::vector<Vec4> Poles(2 * ArcCount + 1);
    Vec3 P0 = Axes.ToWorld(Vec2::Polar(Radius, StartAngle));
    Vec3 T0 = Axes.AxisX * -std::sin(StartAngle) + Axes.AxisY * std::cos(StartAngle);
    Poles[0] = Vec4(P0, 1.0);
    double Angle = StartAngle;
    for (int I = 1, Index = 0; I <= ArcCount; ++I)
    {
        Angle += Delta;
        Vec3 P2 = Axes.ToWorld(Vec2::Polar(Radius, Angle));
        Vec3 T2 = Axes.AxisX * -std::sin(Angle) + Axes.AxisY * std::cos(Angle);
        // P1 = intersection of the two tangent lines; for a symmetric arc piece it sits on the bisector.
        Vec3 Bisector = Axes.ToWorld(Vec2::Polar(Radius / Weight, Angle - Delta * 0.5));
        (void)T0; (void)T2;
        Poles[Index + 1] = Vec4::Weighted(Bisector, Weight);
        Poles[Index + 2] = Vec4(P2, 1.0);
        Index += 2;
        P0 = P2; T0 = T2;
    }

    std::vector<double> Knots(2 * ArcCount + 4);
    Knots[0] = Knots[1] = Knots[2] = 0.0;
    for (int I = 1; I < ArcCount; ++I) Knots[2 * I + 1] = Knots[2 * I + 2] = static_cast<double>(I) / ArcCount;
    size_t N = Knots.size();
    Knots[N - 1] = Knots[N - 2] = Knots[N - 3] = 1.0;

    Deliver<NurbsCurve> Result = Build(2, std::move(Poles), std::move(Knots));
    if (Result)
    {
        NurbsCurve& C = Result.Payload;
        bool Full = ScalarCriteria::Coincident(std::fabs(SweepAngle), ScalarCriteria::TwoPi, 1e-9);
        C.Classification = Full ? CurveClassification::Circle : CurveClassification::Arc;
        C.Centre = Centre; C.AxisZ = Axes.Normal(); C.RadiusMajor = C.RadiusMinor = Radius;
    }
    return Result;
}

Deliver<NurbsCurve> NurbsCurve::Circle(Vec3 Centre, Vec3 Normal, double Radius) noexcept
{
    return Arc(Centre, Normal, Radius, 0.0, ScalarCriteria::TwoPi);
}

Deliver<NurbsCurve> NurbsCurve::ArcThreePoints(Vec3 A, Vec3 B, Vec3 C) noexcept
{
    Vec3 AB = B - A, AC = C - A;
    Vec3 N = AB.Cross(AC);
    double NN = N.LengthSquared();
    if (NN <= ScalarCriteria::KernelTolerance * ScalarCriteria::KernelTolerance) return Deliver<NurbsCurve>::Reject(RefusalReason::DegenerateInput, "three points are collinear");
    // Circumcentre (standard formula).
    Vec3 Centre = A + (AC * AB.LengthSquared() - AB * AC.LengthSquared()).Cross(N) / (2.0 * NN);
    double Radius = Centre.Distance(A);
    Workplane Axes = Workplane::FromNormal(Centre, N);
    double AngleA = Axes.ToLocal(A).Angle();
    double AngleB = Axes.ToLocal(B).Angle();
    double AngleC = Axes.ToLocal(C).Angle();
    // Sweep CCW from A to C in this frame; because the frame normal follows AB×AC, B is always on that CCW path.
    double Sweep = ScalarCriteria::WrapAngle(AngleC - AngleA);
    (void)AngleB;
    return Arc(Centre, Axes.Normal(), Radius, AngleA, Sweep);
}

Deliver<NurbsCurve> NurbsCurve::Ellipse(Vec3 Centre, Vec3 Normal, Vec3 MajorDirection, double RadiusMajor, double RadiusMinor) noexcept
{
    if (RadiusMajor <= ScalarCriteria::KernelTolerance || RadiusMinor <= ScalarCriteria::KernelTolerance) return Deliver<NurbsCurve>::Reject(RefusalReason::DegenerateInput, "zero ellipse radius");
    Deliver<NurbsCurve> Unit = Circle({}, Vec3::UnitZ(), 1.0);
    if (!Unit) return Unit;
    Vec3 N = Normal.Normalised();
    Vec3 U = (MajorDirection - N * MajorDirection.Dot(N)).Normalised();
    if (U.LengthSquared() < 0.5) U = N.AnyPerpendicular();
    Vec3 V = N.Cross(U);
    Mat4 Affine = Mat4::Axes(Centre, U * RadiusMajor, V * RadiusMinor, N);
    NurbsCurve E = Unit.Payload.Transformed(Affine);
    E.Classification = CurveClassification::Ellipse;
    E.Centre = Centre; E.AxisZ = N; E.RadiusMajor = RadiusMajor; E.RadiusMinor = RadiusMinor;
    return Deliver<NurbsCurve>::Accept(std::move(E));
}

// Stitch C0 pieces (lines / arcs already in world space) into one degree-2 curve. Lines are elevated to degree 2 so
//    the knot vector is uniform in degree; parameterisation is by cumulative piece count.
NurbsCurve NurbsCurve::PlanarChain(const Workplane&, const std::vector<NurbsCurve>& Pieces) noexcept
{
    NurbsCurve Result = Pieces.front().Degree < 2 ? Pieces.front().Elevated(2) : Pieces.front();
    for (size_t I = 1; I < Pieces.size(); ++I)
    {
        NurbsCurve Next = Pieces[I].Degree < 2 ? Pieces[I].Elevated(2) : Pieces[I];
        Deliver<NurbsCurve> Joined = Join(Result, Next);
        if (Joined) Result = std::move(Joined.Payload);
    }
    return Result;
}

Deliver<NurbsCurve> NurbsCurve::Rectangle(const Workplane& Plane, Vec2 CornerA, Vec2 CornerB, double CornerRadius) noexcept
{
    Vec2 Low{ std::min(CornerA.X, CornerB.X), std::min(CornerA.Y, CornerB.Y) };
    Vec2 High{ std::max(CornerA.X, CornerB.X), std::max(CornerA.Y, CornerB.Y) };
    double Width = High.X - Low.X, Height = High.Y - Low.Y;
    if (Width <= ScalarCriteria::KernelTolerance || Height <= ScalarCriteria::KernelTolerance) return Deliver<NurbsCurve>::Reject(RefusalReason::DegenerateInput, "rectangle has zero width or height");
    double R = std::min(CornerRadius, 0.5 * std::min(Width, Height) - ScalarCriteria::KernelTolerance);
    if (R <= ScalarCriteria::KernelTolerance)
    {
        Deliver<NurbsCurve> Result = Polyline({ Plane.ToWorld(Low), Plane.ToWorld({ High.X, Low.Y }), Plane.ToWorld(High), Plane.ToWorld({ Low.X, High.Y }) }, true);
        if (Result) Result.Payload.Classification = CurveClassification::Rectangle;
        return Result;
    }
    Vec3 N = Plane.Normal();
    std::vector<NurbsCurve> Pieces;
    auto Push = [&](Deliver<NurbsCurve> D) { if (D) Pieces.push_back(std::move(D.Payload)); };
    // CCW starting on the bottom edge, rounded corners as 90° arcs.
    Push(Line(Plane.ToWorld({ Low.X + R, Low.Y }), Plane.ToWorld({ High.X - R, Low.Y })));
    Push(Arc(Plane.ToWorld({ High.X - R, Low.Y + R }), N, R, -ScalarCriteria::HalfPi, ScalarCriteria::HalfPi));
    Push(Line(Plane.ToWorld({ High.X, Low.Y + R }), Plane.ToWorld({ High.X, High.Y - R })));
    Push(Arc(Plane.ToWorld({ High.X - R, High.Y - R }), N, R, 0.0, ScalarCriteria::HalfPi));
    Push(Line(Plane.ToWorld({ High.X - R, High.Y }), Plane.ToWorld({ Low.X + R, High.Y })));
    Push(Arc(Plane.ToWorld({ Low.X + R, High.Y - R }), N, R, ScalarCriteria::HalfPi, ScalarCriteria::HalfPi));
    Push(Line(Plane.ToWorld({ Low.X, High.Y - R }), Plane.ToWorld({ Low.X, Low.Y + R })));
    Push(Arc(Plane.ToWorld({ Low.X + R, Low.Y + R }), N, R, ScalarCriteria::Pi, ScalarCriteria::HalfPi));
    NurbsCurve Result = PlanarChain(Plane, Pieces);
    Result.Classification = CurveClassification::Rectangle;
    return Deliver<NurbsCurve>::Accept(std::move(Result));
}

Deliver<NurbsCurve> NurbsCurve::Polygon(const Workplane& Plane, Vec2 Centre, double Radius, int Sides, double Rotation, bool Inscribed) noexcept
{
    if (Sides < 3) return Deliver<NurbsCurve>::Reject(RefusalReason::DegenerateInput, "polygon needs ≥ 3 sides");
    if (Radius <= ScalarCriteria::KernelTolerance) return Deliver<NurbsCurve>::Reject(RefusalReason::DegenerateInput, "zero polygon radius");
    double Circumradius = Inscribed ? Radius : Radius / std::cos(ScalarCriteria::Pi / Sides);
    std::vector<Vec3> Points;
    for (int I = 0; I < Sides; ++I)
        Points.push_back(Plane.ToWorld(Centre + Vec2::Polar(Circumradius, Rotation + ScalarCriteria::TwoPi * I / Sides)));
    Deliver<NurbsCurve> Result = Polyline(Points, true);
    if (Result)
    {
        Result.Payload.Classification = CurveClassification::Polygon;
        Result.Payload.Centre = Plane.ToWorld(Centre); Result.Payload.AxisZ = Plane.Normal();
        Result.Payload.RadiusMajor = Circumradius; Result.Payload.RadiusMinor = Circumradius * std::cos(ScalarCriteria::Pi / Sides);
    }
    return Result;
}

Deliver<NurbsCurve> NurbsCurve::Slot(const Workplane& Plane, Vec2 CentreA, Vec2 CentreB, double Radius) noexcept
{
    if (Radius <= ScalarCriteria::KernelTolerance) return Deliver<NurbsCurve>::Reject(RefusalReason::DegenerateInput, "zero slot radius");
    Vec2 Axis = CentreB - CentreA;
    if (Axis.LengthSquared() <= ScalarCriteria::KernelTolerance * ScalarCriteria::KernelTolerance)
        return Circle(Plane.ToWorld(CentreA), Plane.Normal(), Radius);
    Vec2 D = Axis.Normalised();
    Vec2 P = D.Perpendicular();
    double Heading = D.Angle();
    Vec3 N = Plane.Normal();
    std::vector<NurbsCurve> Pieces;
    auto Push = [&](Deliver<NurbsCurve> Dl) { if (Dl) Pieces.push_back(std::move(Dl.Payload)); };
    Push(Line(Plane.ToWorld(CentreA - P * Radius), Plane.ToWorld(CentreB - P * Radius)));
    Push(Arc(Plane.ToWorld(CentreB), N, Radius, Heading - ScalarCriteria::HalfPi, ScalarCriteria::Pi));
    Push(Line(Plane.ToWorld(CentreB + P * Radius), Plane.ToWorld(CentreA + P * Radius)));
    Push(Arc(Plane.ToWorld(CentreA), N, Radius, Heading + ScalarCriteria::HalfPi, ScalarCriteria::Pi));
    NurbsCurve Result = PlanarChain(Plane, Pieces);
    Result.Classification = CurveClassification::Slot;
    Result.Centre = Plane.ToWorld((CentreA + CentreB) * 0.5); Result.AxisZ = N;
    Result.RadiusMajor = Axis.Length() * 0.5 + Radius; Result.RadiusMinor = Radius;
    return Deliver<NurbsCurve>::Accept(std::move(Result));
}

Deliver<NurbsCurve> NurbsCurve::Bezier(const std::vector<Vec3>& ControlPoints) noexcept
{
    int N = static_cast<int>(ControlPoints.size());
    if (N < 2) return Deliver<NurbsCurve>::Reject(RefusalReason::DegenerateInput, "Bézier needs ≥ 2 control points");
    std::vector<Vec4> Poles;
    for (Vec3 P : ControlPoints) Poles.emplace_back(P, 1.0);
    std::vector<double> Knots(2 * N, 0.0);
    for (int I = N; I < 2 * N; ++I) Knots[I] = 1.0;
    return Build(N - 1, std::move(Poles), std::move(Knots));
}

Deliver<NurbsCurve> NurbsCurve::ControlPoints(int Degree, const std::vector<Vec3>& Points, bool Periodic) noexcept
{
    std::vector<Vec3> P = Points;
    if (Periodic)
    {
        // Wrap Degree points so the periodic curve is C^(Degree-1) across the seam; knots uniform (unclamped).
        if (static_cast<int>(P.size()) < Degree + 1) return Deliver<NurbsCurve>::Reject(RefusalReason::InvalidDegree, "periodic curve needs > Degree points");
        for (int I = 0; I < Degree; ++I) P.push_back(Points[I]);
        std::vector<Vec4> Poles; for (Vec3 Q : P) Poles.emplace_back(Q, 1.0);
        int Count = static_cast<int>(Poles.size());
        std::vector<double> Knots(Count + Degree + 1);
        for (int I = 0; I < static_cast<int>(Knots.size()); ++I) Knots[I] = static_cast<double>(I - Degree) / (Count - Degree);
        return Build(Degree, std::move(Poles), std::move(Knots));
    }
    if (static_cast<int>(P.size()) <= Degree) return Deliver<NurbsCurve>::Reject(RefusalReason::InvalidDegree, "need more than Degree control points");
    std::vector<Vec4> Poles; for (Vec3 Q : P) Poles.emplace_back(Q, 1.0);
    std::vector<double> Knots = ClampedUniformKnots(static_cast<int>(Poles.size()), Degree);
    return Build(Degree, std::move(Poles), std::move(Knots));
}

// Piegl & Tiller A9.1 — global interpolation, chord-length parameters, averaged knots, dense solve (sizes are sketch-scale).
Deliver<NurbsCurve> NurbsCurve::Interpolate(const std::vector<Vec3>& Through, int Degree, bool Closed) noexcept
{
    std::vector<Vec3> Q = Through;
    if (Closed && Q.size() > 2 && !Q.front().Coincident(Q.back(), ScalarCriteria::MergeTolerance)) Q.push_back(Q.front());
    std::vector<Vec4> H; H.reserve(Q.size());
    for (Vec3 P : Q) H.emplace_back(P, 1.0);
    return InterpolateHomogeneous(H, Degree);
}

Deliver<NurbsCurve> NurbsCurve::InterpolateHomogeneous(const std::vector<Vec4>& Q, int Degree, const std::vector<double>* Parameters) noexcept
{
    int N = static_cast<int>(Q.size());
    if (N <= Degree) return Deliver<NurbsCurve>::Reject(RefusalReason::InvalidDegree, "need more than Degree points to interpolate");

    std::vector<double> U(N, 0.0);
    if (Parameters && static_cast<int>(Parameters->size()) == N) U = *Parameters;
    else
    {
        double Total = 0.0;
        for (int I = 1; I < N; ++I) Total += Q[I].Divide().Distance(Q[I - 1].Divide());
        if (Total <= ScalarCriteria::KernelTolerance) return Deliver<NurbsCurve>::Reject(RefusalReason::DegenerateInput, "interpolation points coincide");
        for (int I = 1; I < N; ++I) U[I] = U[I - 1] + Q[I].Divide().Distance(Q[I - 1].Divide()) / Total;
        U[N - 1] = 1.0;
    }

    std::vector<double> Knots(N + Degree + 1, 0.0);
    for (int I = 0; I <= Degree; ++I) Knots[N + I] = 1.0;
    for (int J = 1; J < N - Degree; ++J)
    {
        double Sum = 0.0;
        for (int I = J; I < J + Degree; ++I) Sum += U[I];
        Knots[J + Degree] = Sum / Degree;
    }

    NurbsCurve Shape;
    Shape.Degree = Degree; Shape.Knots = Knots; Shape.Poles.assign(N, Vec4{});
    std::vector<double> A(static_cast<size_t>(N) * N, 0.0);
    std::vector<double> Basis(Degree + 1);
    for (int I = 0; I < N; ++I)
    {
        int Span = Shape.FindSpan(U[I]);
        Shape.BasisFunctions(Span, U[I], Basis.data());
        for (int K = 0; K <= Degree; ++K) A[static_cast<size_t>(I) * N + (Span - Degree + K)] = Basis[K];
    }
    // Gaussian elimination with partial pivoting, four right-hand sides (homogeneous).
    std::vector<Vec4> B = Q;
    for (int Column = 0; Column < N; ++Column)
    {
        int Pivot = Column;
        for (int Row = Column + 1; Row < N; ++Row)
            if (std::fabs(A[static_cast<size_t>(Row) * N + Column]) > std::fabs(A[static_cast<size_t>(Pivot) * N + Column])) Pivot = Row;
        if (std::fabs(A[static_cast<size_t>(Pivot) * N + Column]) < 1e-300) return Deliver<NurbsCurve>::Reject(RefusalReason::NoConvergence, "singular interpolation system");
        if (Pivot != Column)
        {
            for (int K = 0; K < N; ++K) std::swap(A[static_cast<size_t>(Pivot) * N + K], A[static_cast<size_t>(Column) * N + K]);
            std::swap(B[Pivot], B[Column]);
        }
        for (int Row = Column + 1; Row < N; ++Row)
        {
            double Factor = A[static_cast<size_t>(Row) * N + Column] / A[static_cast<size_t>(Column) * N + Column];
            if (Factor == 0.0) continue;
            for (int K = Column; K < N; ++K) A[static_cast<size_t>(Row) * N + K] -= Factor * A[static_cast<size_t>(Column) * N + K];
            B[Row] = B[Row] - B[Column] * Factor;
        }
    }
    std::vector<Vec4> P(N);
    for (int Row = N - 1; Row >= 0; --Row)
    {
        Vec4 Sum = B[Row];
        for (int K = Row + 1; K < N; ++K) Sum = Sum - P[K] * A[static_cast<size_t>(Row) * N + K];
        P[Row] = Sum * (1.0 / A[static_cast<size_t>(Row) * N + Row]);
    }
    Shape.Poles = std::move(P);
    if (Refusal Denial = Shape.Validate()) return Deliver<NurbsCurve>::Reject(Denial.Reason, Denial.Detail);
    return Deliver<NurbsCurve>::Accept(std::move(Shape));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  QUERIES
//------------------------------------------------------------------------------------------------------------------------

bool NurbsCurve::Rational() const noexcept
{
    for (const Vec4& P : Poles) if (!ScalarCriteria::Coincident(P.W, 1.0, 1e-12)) return true;
    return false;
}

bool NurbsCurve::Closed(double Tolerance) const noexcept
{
    return StartPoint().Coincident(EndPoint(), Tolerance);
}

bool NurbsCurve::Planar(Plane& Result, double Tolerance) const noexcept
{
    // Newell's method over the control polygon gives a robust normal; then test every pole against it.
    Vec3 Normal{};
    Vec3 Centroid{};
    int N = PoleCount();
    for (int I = 0; I < N; ++I)
    {
        Vec3 A = Poles[I].Divide(), B = Poles[(I + 1) % N].Divide();
        Normal += Vec3{ (A.Y - B.Y) * (A.Z + B.Z), (A.Z - B.Z) * (A.X + B.X), (A.X - B.X) * (A.Y + B.Y) };
        Centroid += A;
    }
    Centroid = Centroid / N;
    if (Normal.LengthSquared() < 1e-24)
    {
        // Collinear control polygon: any plane containing the line works — pick one.
        Vec3 D = (EndPoint() - StartPoint()).Normalised();
        Normal = D.AnyPerpendicular();
    }
    Result = Plane::FromPointNormal(Centroid, Normal);
    for (const Vec4& P : Poles) if (std::fabs(Result.SignedDistance(P.Divide())) > Tolerance) return false;
    return true;
}

Box3 NurbsCurve::Bounds() const noexcept
{
    Box3 B;
    for (const Vec4& P : Poles) B.Include(P.Divide());
    return B;
}

// A2.1
int NurbsCurve::FindSpan(double T) const noexcept
{
    int N = PoleCount() - 1;
    if (T >= Knots[N + 1]) return N;
    if (T <= Knots[Degree]) return Degree;
    int Low = Degree, High = N + 1, Mid = (Low + High) / 2;
    while (T < Knots[Mid] || T >= Knots[Mid + 1])
    {
        if (T < Knots[Mid]) High = Mid; else Low = Mid;
        Mid = (Low + High) / 2;
    }
    return Mid;
}

// A2.2
void NurbsCurve::BasisFunctions(int Span, double T, double* Out) const noexcept
{
    double Left[16], Right[16];                                                          // degree ≤ 15
    Out[0] = 1.0;
    for (int J = 1; J <= Degree; ++J)
    {
        Left[J] = T - Knots[Span + 1 - J];
        Right[J] = Knots[Span + J] - T;
        double Saved = 0.0;
        for (int R = 0; R < J; ++R)
        {
            double Temp = Out[R] / (Right[R + 1] + Left[J - R]);
            Out[R] = Saved + Right[R + 1] * Temp;
            Saved = Left[J - R] * Temp;
        }
        Out[J] = Saved;
    }
}

// A2.3 — Out laid out as [Derivative][Function], row stride = Order.
void NurbsCurve::BasisDerivatives(int Span, double T, int DerivativeCount, double* Out) const noexcept
{
    const int P = Degree;
    double Ndu[16][16], Left[16], Right[16], A[2][16];
    Ndu[0][0] = 1.0;
    for (int J = 1; J <= P; ++J)
    {
        Left[J] = T - Knots[Span + 1 - J];
        Right[J] = Knots[Span + J] - T;
        double Saved = 0.0;
        for (int R = 0; R < J; ++R)
        {
            Ndu[J][R] = Right[R + 1] + Left[J - R];
            double Temp = Ndu[R][J - 1] / Ndu[J][R];
            Ndu[R][J] = Saved + Right[R + 1] * Temp;
            Saved = Left[J - R] * Temp;
        }
        Ndu[J][J] = Saved;
    }
    for (int J = 0; J <= P; ++J) Out[J] = Ndu[J][P];
    for (int R = 0; R <= P; ++R)
    {
        int S1 = 0, S2 = 1;
        A[0][0] = 1.0;
        for (int K = 1; K <= DerivativeCount; ++K)
        {
            double D = 0.0;
            int RK = R - K, PK = P - K;
            if (R >= K) { A[S2][0] = A[S1][0] / Ndu[PK + 1][RK]; D = A[S2][0] * Ndu[RK][PK]; }
            int J1 = RK >= -1 ? 1 : -RK;
            int J2 = (R - 1 <= PK) ? K - 1 : P - R;
            for (int J = J1; J <= J2; ++J)
            {
                A[S2][J] = (A[S1][J] - A[S1][J - 1]) / Ndu[PK + 1][RK + J];
                D += A[S2][J] * Ndu[RK + J][PK];
            }
            if (R <= PK) { A[S2][K] = -A[S1][K - 1] / Ndu[PK + 1][R]; D += A[S2][K] * Ndu[R][PK]; }
            Out[K * (P + 1) + R] = D;
            std::swap(S1, S2);
        }
    }
    double Factor = P;
    for (int K = 1; K <= DerivativeCount; ++K)
    {
        for (int J = 0; J <= P; ++J) Out[K * (P + 1) + J] *= Factor;
        Factor *= (P - K);
    }
}

Vec4 NurbsCurve::SampleHomogeneous(double T) const noexcept
{
    T = ScalarCriteria::Clamp(T, DomainStart(), DomainEnd());
    int Span = FindSpan(T);
    double Basis[16];
    BasisFunctions(Span, T, Basis);
    Vec4 Sum{ 0, 0, 0, 0 };
    for (int I = 0; I <= Degree; ++I) Sum += Poles[Span - Degree + I] * Basis[I];
    return Sum;
}

Vec3 NurbsCurve::Sample(double T) const noexcept
{
    return SampleHomogeneous(T).Divide();
}

// A3.2 + A4.2: homogeneous derivatives then the rational quotient rule.
void NurbsCurve::Derivatives(double T, int Count, Vec3* Out) const noexcept
{
    Count = std::min(Count, Degree);
    T = ScalarCriteria::Clamp(T, DomainStart(), DomainEnd());
    int Span = FindSpan(T);
    double Basis[16 * 16];
    BasisDerivatives(Span, T, Count, Basis);
    Vec4 Homogeneous[16];
    for (int K = 0; K <= Count; ++K)
    {
        Homogeneous[K] = Vec4{ 0, 0, 0, 0 };
        for (int J = 0; J <= Degree; ++J) Homogeneous[K] += Poles[Span - Degree + J] * Basis[K * (Degree + 1) + J];
    }
    // Binomial coefficients up to 3 are enough for curvature; general loop for completeness.
    auto Binomial = [](int N, int K) { double R = 1.0; for (int I = 1; I <= K; ++I) R = R * (N - K + I) / I; return R; };
    for (int K = 0; K <= Count; ++K)
    {
        Vec3 V = Homogeneous[K].XYZ();
        for (int I = 1; I <= K; ++I) V -= Out[K - I] * (Binomial(K, I) * Homogeneous[I].W);
        Out[K] = V / Homogeneous[0].W;
    }
    for (int K = Count + 1; K < 4; ++K) Out[K] = Vec3{};
}

Vec3 NurbsCurve::Tangent(double T) const noexcept
{
    Vec3 D[4];
    Derivatives(T, 1, D);
    Vec3 Tn = D[1].Normalised();
    if (Tn.LengthSquared() < 0.5)
    {
        // Stationary point (repeated pole) — nudge inward.
        double Step = (DomainEnd() - DomainStart()) * 1e-6;
        Derivatives(T + (T < (DomainStart() + DomainEnd()) * 0.5 ? Step : -Step), 1, D);
        Tn = D[1].Normalised();
    }
    return Tn;
}

double NurbsCurve::Curvature(double T) const noexcept
{
    Vec3 D[4];
    Derivatives(T, 2, D);
    double Speed = D[1].Length();
    if (Speed <= ScalarCriteria::KernelTolerance) return 0.0;
    return D[1].Cross(D[2]).Length() / (Speed * Speed * Speed);
}

// 5-point Gauss–Legendre per knot span, recursively bisected until the estimate stabilises.
static double GaussLength(const NurbsCurve& C, double A, double B, double Reference, double Tolerance, int Depth) noexcept
{
    static const double Abscissa[5] = { 0.0, 0.5384693101056831, -0.5384693101056831, 0.9061798459386640, -0.9061798459386640 };
    static const double Weight[5]   = { 0.5688888888888889, 0.4786286704993665, 0.4786286704993665, 0.2369268850561891, 0.2369268850561891 };
    auto Integrate = [&](double Lo, double Hi)
    {
        double Half = (Hi - Lo) * 0.5, Mid = (Hi + Lo) * 0.5, Sum = 0.0;
        Vec3 D[4];
        for (int I = 0; I < 5; ++I) { C.Derivatives(Mid + Half * Abscissa[I], 1, D); Sum += Weight[I] * D[1].Length(); }
        return Sum * Half;
    };
    double M = (A + B) * 0.5;
    double Left = Integrate(A, M), Right = Integrate(M, B);
    if (Depth >= 12 || std::fabs(Left + Right - Reference) <= Tolerance) return Left + Right;
    return GaussLength(C, A, M, Left, Tolerance * 0.5, Depth + 1) + GaussLength(C, M, B, Right, Tolerance * 0.5, Depth + 1);
}

double NurbsCurve::Length(double Tolerance) const noexcept
{
    double Total = 0.0;
    for (size_t I = Degree; I < Poles.size(); ++I)
    {
        double A = Knots[I], B = Knots[I + 1];
        if (B - A <= ScalarCriteria::ParametricEpsilon) continue;
        Total += GaussLength(*this, A, B, Sample(A).Distance(Sample(B)), Tolerance, 0);
    }
    return Total;
}

double NurbsCurve::ParameterAtLength(double Target) const noexcept
{
    // Bisection on cumulative length; adequate for sketch-scale curves and monotone by construction.
    double Lo = DomainStart(), Hi = DomainEnd();
    if (Target <= 0.0) return Lo;
    double Total = Length();
    if (Target >= Total) return Hi;
    for (int I = 0; I < 60; ++I)
    {
        double Mid = (Lo + Hi) * 0.5;
        if (Trimmed(DomainStart(), Mid).Length() < Target) Lo = Mid; else Hi = Mid;
        if (Hi - Lo < 1e-12) break;
    }
    return (Lo + Hi) * 0.5;
}

// Piegl & Tiller 6.3: coarse sample, then Newton on f(t) = C'(t)·(C(t)−P) with the two standard stopping criteria.
double NurbsCurve::ClosestParameter(Vec3 P, double* DistanceOut) const noexcept
{
    double T0 = DomainStart(), T1 = DomainEnd();
    int SampleCount = std::max(16, PoleCount() * 8);
    double BestT = T0, BestD = ScalarCriteria::Infinity;
    for (int I = 0; I <= SampleCount; ++I)
    {
        double T = ScalarCriteria::Lerp(T0, T1, static_cast<double>(I) / SampleCount);
        double D = Sample(T).Distance(P);
        if (D < BestD) { BestD = D; BestT = T; }
    }
    bool Periodic = Closed(ScalarCriteria::KernelTolerance);
    double T = BestT;
    Vec3 D[4];
    for (int Iteration = 0; Iteration < 32; ++Iteration)
    {
        Derivatives(T, 2, D);
        Vec3 Delta = D[0] - P;
        double Distance = Delta.Length();
        if (Distance <= ScalarCriteria::KernelTolerance) break;                         // point coincidence
        double Speed = D[1].Length();
        if (Speed <= ScalarCriteria::KernelTolerance) break;
        double Cosine = std::fabs(D[1].Dot(Delta)) / (Speed * Distance);
        if (Cosine <= ScalarCriteria::AngularTolerance) break;                          // zero cosine: perpendicular reached
        double Numerator = D[1].Dot(Delta);
        double Denominator = D[2].Dot(Delta) + Speed * Speed;
        if (ScalarCriteria::Vanishing(Denominator, 1e-300)) break;
        double Next = T - Numerator / Denominator;
        if (Periodic) { double Span = T1 - T0; while (Next < T0) Next += Span; while (Next > T1) Next -= Span; }
        else Next = ScalarCriteria::Clamp(Next, T0, T1);
        if (std::fabs((Next - T) * Speed) <= ScalarCriteria::KernelTolerance) { T = Next; break; }   // parameter no longer moving
        T = Next;
    }
    if (DistanceOut) *DistanceOut = Sample(T).Distance(P);
    return T;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  EDITING
//------------------------------------------------------------------------------------------------------------------------

// A5.1 — Boehm insertion, homogeneous coordinates so rational curves stay exact.
NurbsCurve NurbsCurve::InsertKnot(double T, int Multiplicity) const noexcept
{
    NurbsCurve Result = *this;
    for (int Round = 0; Round < Multiplicity; ++Round)
    {
        int K = Result.FindSpan(T);
        int Existing = 0;
        for (double Knot : Result.Knots) if (ScalarCriteria::Coincident(Knot, T, ScalarCriteria::ParametricEpsilon)) ++Existing;
        if (Existing >= Result.Degree) return Result;                                  // already at full multiplicity
        std::vector<Vec4> NewPoles(Result.Poles.size() + 1);
        for (int I = 0; I <= K - Result.Degree; ++I) NewPoles[I] = Result.Poles[I];
        for (int I = K - Existing; I < static_cast<int>(Result.Poles.size()); ++I) NewPoles[I + 1] = Result.Poles[I];
        for (int I = K - Result.Degree + 1; I <= K - Existing; ++I)
        {
            double Alpha = (T - Result.Knots[I]) / (Result.Knots[I + Result.Degree] - Result.Knots[I]);
            NewPoles[I] = Result.Poles[I] * Alpha + Result.Poles[I - 1] * (1.0 - Alpha);
        }
        Result.Knots.insert(Result.Knots.begin() + K + 1, T);
        Result.Poles = std::move(NewPoles);
    }
    Result.Classification = Classification;                                            // geometry unchanged
    return Result;
}

NurbsCurve NurbsCurve::Refined(const std::vector<double>& NewKnots) const noexcept
{
    NurbsCurve Result = *this;
    for (double T : NewKnots) Result = Result.InsertKnot(T, 1);
    return Result;
}

std::pair<NurbsCurve, NurbsCurve> NurbsCurve::Split(double T) const noexcept
{
    T = ScalarCriteria::Clamp(T, DomainStart(), DomainEnd());
    NurbsCurve Full = InsertKnot(T, Degree);
    int K = Full.FindSpan(T);
    // After insertion to multiplicity Degree, knots K-Degree+1 .. K equal T; the split pole is index K-Degree.
    int SplitPole = K - Degree;
    if (T >= Full.DomainEnd()) SplitPole = Full.PoleCount() - 1;
    NurbsCurve Left, Right;
    Left.Degree = Right.Degree = Degree;
    Left.Poles.assign(Full.Poles.begin(), Full.Poles.begin() + SplitPole + 1);
    Right.Poles.assign(Full.Poles.begin() + SplitPole, Full.Poles.end());
    Left.Knots.assign(Full.Knots.begin(), Full.Knots.begin() + SplitPole + Degree + 1);
    Left.Knots.push_back(T);
    Right.Knots.push_back(T);
    Right.Knots.insert(Right.Knots.end(), Full.Knots.begin() + SplitPole + 1, Full.Knots.end());
    // Ensure clamped ends.
    for (int I = 0; I <= Degree; ++I) { Left.Knots[Left.Knots.size() - 1 - I] = T; Right.Knots[I] = T; }
    Left.Centre = Right.Centre = Centre; Left.AxisZ = Right.AxisZ = AxisZ;
    Left.RadiusMajor = Right.RadiusMajor = RadiusMajor; Left.RadiusMinor = Right.RadiusMinor = RadiusMinor;
    CurveClassification Piece = Classification;
    if (Piece == CurveClassification::Circle) Piece = CurveClassification::Arc;
    else if (Piece == CurveClassification::Polyline || Piece == CurveClassification::Rectangle || Piece == CurveClassification::Polygon) Piece = CurveClassification::Polyline;
    else if (Piece != CurveClassification::Line && Piece != CurveClassification::Arc) Piece = CurveClassification::Freeform;
    Left.Classification = Right.Classification = Piece;
    return { Left, Right };
}

NurbsCurve NurbsCurve::Trimmed(double T0, double T1) const noexcept
{
    if (T0 > T1) std::swap(T0, T1);
    T0 = ScalarCriteria::Clamp(T0, DomainStart(), DomainEnd());
    T1 = ScalarCriteria::Clamp(T1, DomainStart(), DomainEnd());
    NurbsCurve Result = *this;
    if (T1 < DomainEnd() - ScalarCriteria::ParametricEpsilon) Result = Result.Split(T1).first;
    if (T0 > DomainStart() + ScalarCriteria::ParametricEpsilon) Result = Result.Split(T0).second;
    return Result;
}

NurbsCurve NurbsCurve::Reversed() const noexcept
{
    NurbsCurve Result = *this;
    std::reverse(Result.Poles.begin(), Result.Poles.end());
    double A = DomainStart(), B = DomainEnd();
    for (size_t I = 0; I < Knots.size(); ++I) Result.Knots[I] = A + B - Knots[Knots.size() - 1 - I];
    return Result;
}

NurbsCurve NurbsCurve::Reparameterised(double T0, double T1) const noexcept
{
    NurbsCurve Result = *this;
    double A = DomainStart(), Span = DomainEnd() - A;
    for (double& K : Result.Knots) K = T0 + (K - A) / Span * (T1 - T0);
    return Result;
}

NurbsCurve NurbsCurve::Transformed(const Mat4& M) const noexcept
{
    NurbsCurve Result = *this;
    for (Vec4& P : Result.Poles) P = Vec4::Weighted(M.TransformPoint(P.Divide()), P.W);
    Result.Centre = M.TransformPoint(Centre);
    Result.AxisZ = M.TransformDirection(AxisZ).Normalised();
    // A non-uniform scale breaks circle/arc exactness of the classification (the NURBS stays exact as an ellipse).
    Vec3 SX = M.AxisX(), SY = M.AxisY();
    bool Uniform = ScalarCriteria::Coincident(SX.LengthSquared(), SY.LengthSquared(), 1e-9) && ScalarCriteria::Vanishing(SX.Dot(SY), 1e-9);
    if (Uniform) { double S = SX.Length(); Result.RadiusMajor *= S; Result.RadiusMinor *= S; }
    else if (Classification == CurveClassification::Circle || Classification == CurveClassification::Arc) Result.Classification = CurveClassification::Freeform;
    return Result;
}

// A5.6 — decompose into Bézier pieces by inserting every interior knot to multiplicity Degree.
std::vector<NurbsCurve> NurbsCurve::BezierSegments() const noexcept
{
    NurbsCurve Full = *this;
    for (size_t I = Degree + 1; I < Poles.size(); ++I)
        if (Knots[I] > Knots[I - 1] + ScalarCriteria::ParametricEpsilon) Full = Full.InsertKnot(Knots[I], Degree);
    std::vector<NurbsCurve> Pieces;
    for (int Start = 0; Start + Degree < Full.PoleCount(); Start += Degree)
    {
        NurbsCurve Piece;
        Piece.Degree = Degree;
        Piece.Poles.assign(Full.Poles.begin() + Start, Full.Poles.begin() + Start + Degree + 1);
        double A = Full.Knots[Start + Degree], B = Full.Knots[Start + Degree + 1];
        Piece.Knots.assign(2 * (Degree + 1), A);
        for (int I = 0; I <= Degree; ++I) Piece.Knots[Degree + 1 + I] = B;
        Pieces.push_back(std::move(Piece));
    }
    return Pieces;
}

// Degree elevation via Bézier pieces (each elevated exactly), reassembled with interior knots at multiplicity
//    TargetDegree. The representation is C0-compact rather than minimal; geometry and parameterisation are exact.
NurbsCurve NurbsCurve::Elevated(int TargetDegree) const noexcept
{
    if (TargetDegree <= Degree) return *this;
    std::vector<NurbsCurve> Pieces = BezierSegments();
    int Raise = TargetDegree - Degree;
    NurbsCurve Result;
    Result.Degree = TargetDegree;
    for (size_t S = 0; S < Pieces.size(); ++S)
    {
        std::vector<Vec4> Bez = Pieces[S].Poles;
        for (int Step = 0; Step < Raise; ++Step)
        {
            int N = static_cast<int>(Bez.size()) - 1;                                   // current degree
            std::vector<Vec4> Up(N + 2);
            Up[0] = Bez[0]; Up[N + 1] = Bez[N];
            for (int I = 1; I <= N; ++I)
            {
                double Alpha = static_cast<double>(I) / (N + 1);
                Up[I] = Bez[I - 1] * Alpha + Bez[I] * (1.0 - Alpha);
            }
            Bez = std::move(Up);
        }
        double A = Pieces[S].DomainStart(), B = Pieces[S].DomainEnd();
        if (S == 0) { Result.Knots.assign(TargetDegree + 1, A); Result.Poles.push_back(Bez[0]); }
        for (int I = 1; I <= TargetDegree; ++I) Result.Poles.push_back(Bez[I]);
        for (int I = 0; I < TargetDegree; ++I) Result.Knots.push_back(B);
    }
    Result.Knots.push_back(Pieces.back().DomainEnd());
    Result.Classification = Classification; Result.Centre = Centre; Result.AxisZ = AxisZ;
    Result.RadiusMajor = RadiusMajor; Result.RadiusMinor = RadiusMinor;
    return Result;
}

Deliver<NurbsCurve> NurbsCurve::Join(const NurbsCurve& A, const NurbsCurve& B) noexcept
{
    if (!A.EndPoint().Coincident(B.StartPoint(), ScalarCriteria::MergeTolerance)) return Deliver<NurbsCurve>::Reject(RefusalReason::OpenWire, "curves do not meet end to start");
    int Degree = std::max(A.Degree, B.Degree);
    NurbsCurve Left = A.Degree < Degree ? A.Elevated(Degree) : A;
    NurbsCurve Right = B.Degree < Degree ? B.Elevated(Degree) : B;
    // Shift B's domain to continue A's, then merge the poles (the shared pole is kept once) and knots with the
    //    interior break at multiplicity Degree (C0 join — exactly what a sketch corner is).
    double Shift = Left.DomainEnd() - Right.DomainStart();
    Right = Right.Reparameterised(Right.DomainStart() + Shift, Right.DomainEnd() + Shift);
    NurbsCurve Result;
    Result.Degree = Degree;
    Result.Poles = Left.Poles;
    Result.Poles.back() = (Left.Poles.back() + Right.Poles.front()) * 0.5;             // weld within MergeTolerance
    Result.Poles.insert(Result.Poles.end(), Right.Poles.begin() + 1, Right.Poles.end());
    Result.Knots.assign(Left.Knots.begin(), Left.Knots.end() - 1);                       // drop one end knot of A
    Result.Knots.insert(Result.Knots.end(), Right.Knots.begin() + Degree + 1, Right.Knots.end());
    if (Refusal Denial = Result.Validate()) return Deliver<NurbsCurve>::Reject(Denial.Reason, Denial.Detail);
    Result.Classification = (A.Classification == CurveClassification::Line && B.Classification == CurveClassification::Line) ? CurveClassification::Polyline : CurveClassification::Freeform;
    return Deliver<NurbsCurve>::Accept(std::move(Result));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  TESSELLATION
//------------------------------------------------------------------------------------------------------------------------

void NurbsCurve::TessellateSpan(double T0, double T1, Vec3 P0, Vec3 P1, int Depth, double ChordTolerance, double AngleTolerance,
                                std::vector<Vec3>& Points, std::vector<double>* Parameters) const noexcept
{
    double Tm = (T0 + T1) * 0.5;
    Vec3 Pm = Sample(Tm);
    // Sagitta: distance of the midpoint from the chord.
    Vec3 Chord = P1 - P0;
    double ChordLength = Chord.Length();
    double Sagitta = ChordLength > ScalarCriteria::KernelTolerance ? (Pm - P0).Cross(Chord).Length() / ChordLength : (Pm - P0).Length();
    // Turning angle between the two half chords.
    // Turning angle is meaningless once the half chords are shorter than the chord tolerance (degenerate spans,
    //    e.g. sphere-pole isocurves) — otherwise noise refines them to the depth limit.
    double HalfA = (Pm - P0).Length(), HalfB = (P1 - Pm).Length();
    double Turn = 0.0;
    if (HalfA > ChordTolerance && HalfB > ChordTolerance)
        Turn = std::acos(ScalarCriteria::Clamp((Pm - P0).Dot(P1 - Pm) / (HalfA * HalfB), -1.0, 1.0));
    bool Refine = (Sagitta > ChordTolerance || Turn > AngleTolerance) && Depth < 16;
    if (Depth < 2 && Degree > 1) Refine = true;                                         // never trust a single chord on a curved span
    if (Refine)
    {
        TessellateSpan(T0, Tm, P0, Pm, Depth + 1, ChordTolerance, AngleTolerance, Points, Parameters);
        TessellateSpan(Tm, T1, Pm, P1, Depth + 1, ChordTolerance, AngleTolerance, Points, Parameters);
        return;
    }
    Points.push_back(P1);
    if (Parameters) Parameters->push_back(T1);
}

void NurbsCurve::Tessellate(std::vector<Vec3>& Points, std::vector<double>* Parameters, double ChordTolerance, double AngleTolerance) const noexcept
{
    Points.clear();
    if (Parameters) Parameters->clear();
    Points.push_back(Sample(DomainStart()));
    if (Parameters) Parameters->push_back(DomainStart());
    for (size_t I = Degree; I < Poles.size(); ++I)
    {
        double A = Knots[I], B = Knots[I + 1];
        if (B - A <= ScalarCriteria::ParametricEpsilon) continue;
        if (Degree == 1) { Points.push_back(Sample(B)); if (Parameters) Parameters->push_back(B); continue; }
        TessellateSpan(A, B, Sample(A), Sample(B), 0, ChordTolerance, AngleTolerance, Points, Parameters);
    }
}

} // namespace Frontier
