/*====================================================================================================================================
                                                         SOLIDARCSNAPPING.CPP
====================================================================================================================================*/
// 🧩 Snap catalogue (exact carriers of edges, sketches, SpaceCurves, faces) and the pointer search over it

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "UI/SolidArc/Viewport/SolidArcSnapping.h"

#include "Engine/GeometryProcessing/Intersection/ImplicitSurface.h"
#include "Engine/GeometryProcessing/Measurement/MassProperties.h"
#include "Engine/GeometryProcessing/Sketch/SketchSpline.h"
#include "Engine/GeometryProcessing/Sketch/SketchTrim.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace Frontier::SolidArc
{

namespace
{

using GeometryProcessing::Vector2;
using GeometryProcessing::Vector3;

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr int    CurveSamples   = 48;                                                        // [-]  - Samples per curved carrier
constexpr int    LengthPanels   = 32;                                                        // [-]  - Arc-length panels (8-point Gauss each)
constexpr double RankPixels     = 0.75;                                                      // [px] - Tie-break per kind rank
constexpr double MeetTolerance  = 1.0e-9;                                                    // [-]  - Crossing gap / scene size
constexpr int    NearCurveLimit = 6;                                                         // [-]  - Curves tried for crossings

constexpr std::array<double, 8> GaussNodes   = { -0.9602898564975363, -0.7966664774136267, -0.5255324099163290, -0.1834346424956498,
                                                 0.1834346424956498,  0.5255324099163290,  0.7966664774136267,  0.9602898564975363 };
constexpr std::array<double, 8> GaussWeights = { 0.1012285362903763, 0.2223810344533745, 0.3137066458778873, 0.3626837833783620,
                                                 0.3626837833783620, 0.3137066458778873, 0.2223810344533745, 0.1012285362903763 };

//------------------------------------------------------------------------------------------------------------------------
//                                                    CURVE EVALUATION
//------------------------------------------------------------------------------------------------------------------------

struct CurvePoint
{
    Vector3 Position   = {};                                                                 // [len] - C(s)
    Vector3 Derivative = {};                                                                 // [len] - dC/ds
};

[[nodiscard]] Vector3 PlacePlanar(const GeometryProcessing::OrthonormalFrame& Plane, Vector2 Point) noexcept
{
    return Plane.Origin + Plane.XAxis * Point.X + Plane.YAxis * Point.Y;
}

[[nodiscard]] CurvePoint EvaluateSnapCurve(const SolidArcSnapCurve& Curve, double Parameter)
{
    if (Curve.Carrier)
    {
        const GeometryProcessing::CurveSample Sample = GeometryProcessing::EvaluateCurve(*Curve.Carrier, Parameter);
        return { Sample.Position, Sample.Derivative };
    }
    const Vector2 Point = GeometryProcessing::EvaluateProfileSegment(*Curve.Segment, Parameter);
    const Vector2 Rate  = GeometryProcessing::EvaluateProfileTangent(*Curve.Segment, Parameter);
    return { PlacePlanar(Curve.Plane, Point), Curve.Plane.XAxis * Rate.X + Curve.Plane.YAxis * Rate.Y };
}

// 📝 Closed carriers (full circles, closed splines) wrap; open ones clamp.
[[nodiscard]] bool WrappingCondition(const SolidArcSnapCurve& Curve) noexcept
{
    if (Curve.Segment) return Curve.Segment->Kind == GeometryProcessing::ProfileSegmentKind::Circle;
    return Curve.Carrier && GeometryProcessing::PeriodicCondition(*Curve.Carrier) && Curve.High - Curve.Low >= 2.0 * std::numbers::pi - 1.0e-9;
}

[[nodiscard]] double ConfineParameter(const SolidArcSnapCurve& Curve, double Parameter) noexcept
{
    const double Span = Curve.High - Curve.Low;
    if (WrappingCondition(Curve)) return Curve.Low + (Parameter - Curve.Low) - Span * std::floor((Parameter - Curve.Low) / Span);
    return std::clamp(Parameter, Curve.Low, Curve.High);
}

// Unit tangent and normal: in-plane normal (n × T) for planar curves, else the principal normal, else T × ẑ (T × ŷ).
void DescribeCurvePoint(const SolidArcSnapCurve& Curve, double Parameter, Vector3& Tangent, Vector3& Normal)
{
    const CurvePoint Point = EvaluateSnapCurve(Curve, Parameter);
    Tangent = GeometryProcessing::Length(Point.Derivative) > 0.0 ? GeometryProcessing::Normalize(Point.Derivative) : Vector3{};
    Normal  = {};
    if (GeometryProcessing::Length(Tangent) == 0.0) return;
    if (Curve.PlanarCondition)
    {
        Normal = GeometryProcessing::Normalize(GeometryProcessing::Cross(Curve.PlaneNormal, Tangent));
        return;
    }
    const double Step = 1.0e-5 * (Curve.High - Curve.Low);
    const Vector3 Bend = (EvaluateSnapCurve(Curve, ConfineParameter(Curve, Parameter + Step)).Derivative - EvaluateSnapCurve(Curve, ConfineParameter(Curve, Parameter - Step)).Derivative) * (0.5 / Step);
    const Vector3 Across = Bend - Tangent * GeometryProcessing::Dot(Bend, Tangent);
    if (GeometryProcessing::Length(Across) > 1.0e-9 * (1.0 + GeometryProcessing::Length(Bend))) { Normal = GeometryProcessing::Normalize(Across); return; }
    Vector3 Fallback = GeometryProcessing::Cross(Tangent, Vector3{ 0.0, 0.0, 1.0 });
    if (GeometryProcessing::Length(Fallback) < 1.0e-6) Fallback = GeometryProcessing::Cross(Tangent, Vector3{ 0.0, 1.0, 0.0 });
    Normal = GeometryProcessing::Normalize(Fallback);
}

// Pieces of [Low, High] the carrier is analytic on (knot spans, intersection guide knots, ≤ 45° arc pieces).
[[nodiscard]] std::vector<double> CollectCurveStops(const SolidArcSnapCurve& Curve)
{
    std::vector<double> Stops = { Curve.Low };
    if (Curve.Carrier) for (double Knot : GeometryProcessing::CollectCurveBreakpoints(*Curve.Carrier, Curve.Low, Curve.High)) Stops.push_back(Knot);
    else for (double Break : GeometryProcessing::CollectProfileBreaks(*Curve.Segment)) if (Break > 0.0 && Break < 1.0) Stops.push_back(Break);
    Stops.push_back(Curve.High);
    std::sort(Stops.begin(), Stops.end());
    Stops.erase(std::unique(Stops.begin(), Stops.end()), Stops.end());
    return Stops;
}

[[nodiscard]] double IntegrateSpeed(const SolidArcSnapCurve& Curve, double From, double To)
{
    double Total = 0.0;
    for (size_t Node = 0; Node < GaussNodes.size(); ++Node)
        Total += GaussWeights[Node] * GeometryProcessing::Length(EvaluateSnapCurve(Curve, 0.5 * (From + To) + 0.5 * (To - From) * GaussNodes[Node]).Derivative);
    return 0.5 * (To - From) * Total;
}

// 📝 Arc-length fraction point (midpoint ½, quarter points ¼ ¾): cumulative length over LengthPanels panels per analytic
//    piece (8-point Gauss), then Newton on L(s) = f·L inside the panel that holds it. Lines and circular arcs are parametrised by length already (closed form).
[[nodiscard]] double LocateFraction(const SolidArcSnapCurve& Curve, double Fraction)
{
    const bool Uniform = (Curve.Carrier && (std::holds_alternative<GeometryProcessing::StraightLine>(*Curve.Carrier) || std::holds_alternative<GeometryProcessing::CircularArc>(*Curve.Carrier)))
                      || (Curve.Segment && Curve.Segment->Kind != GeometryProcessing::ProfileSegmentKind::Spline);
    if (Uniform) return Curve.Low + Fraction * (Curve.High - Curve.Low);
    const std::vector<double> Stops = CollectCurveStops(Curve);
    std::vector<double> Edges, Lengths = { 0.0 };
    for (size_t Piece = 0; Piece + 1 < Stops.size(); ++Piece)
        for (int Panel = 0; Panel < LengthPanels; ++Panel)
        {
            const double From = Stops[Piece] + (Stops[Piece + 1] - Stops[Piece]) * Panel / LengthPanels;
            const double To   = Stops[Piece] + (Stops[Piece + 1] - Stops[Piece]) * (Panel + 1) / LengthPanels;
            Edges.push_back(From);
            Lengths.push_back(Lengths.back() + IntegrateSpeed(Curve, From, To));
        }
    Edges.push_back(Curve.High);
    const double Half  = Fraction * Lengths.back();                                          // [len] - Arc length to reach
    const size_t Panel = std::min<size_t>(Edges.size() - 2, size_t(std::upper_bound(Lengths.begin(), Lengths.end(), Half) - Lengths.begin()) - 1);
    double Low = Edges[Panel], High = Edges[Panel + 1], Parameter = 0.5 * (Low + High);
    for (int Iteration = 0; Iteration < 40; ++Iteration)
    {
        const double Error = Lengths[Panel] + IntegrateSpeed(Curve, Edges[Panel], Parameter) - Half;
        if (Error > 0.0) High = Parameter; else Low = Parameter;
        const double Speed = GeometryProcessing::Length(EvaluateSnapCurve(Curve, Parameter).Derivative);
        double Next = Speed > 0.0 ? Parameter - Error / Speed : 0.5 * (Low + High);
        if (!(Next > Low && Next < High)) Next = 0.5 * (Low + High);                         // Newton left the bracket: bisect
        if (std::fabs(Next - Parameter) <= 1.0e-15 * (1.0 + std::fabs(Parameter))) { Parameter = Next; break; }
        Parameter = Next;
    }
    return Parameter;
}

void SampleCurve(SolidArcSnapCurve& Curve, int Count)
{
    const std::vector<double> Stops = CollectCurveStops(Curve);
    for (int Index = 0; Index <= Count; ++Index) Curve.Parameters.push_back(Curve.Low + (Curve.High - Curve.Low) * Index / Count);
    for (double Stop : Stops) Curve.Parameters.push_back(Stop);
    std::sort(Curve.Parameters.begin(), Curve.Parameters.end());
    Curve.Parameters.erase(std::unique(Curve.Parameters.begin(), Curve.Parameters.end()), Curve.Parameters.end());
    for (double Parameter : Curve.Parameters) Curve.Points.push_back(EvaluateSnapCurve(Curve, Parameter).Position);
}

void AppendPoint(std::vector<SolidArcSnapPoint>& Points, SolidArcSnapPoint Point, double Scale)
{
    for (const SolidArcSnapPoint& Existing : Points)
        if (Existing.Kind == Point.Kind && GeometryProcessing::Distance(Existing.Position, Point.Position) <= 1.0e-12 * (1.0 + Scale)) return;
    Points.push_back(std::move(Point));
}

// Endpoints, midpoint and centre of a catalogued curve.
void CatalogueCurvePoints(SolidArcSnapCatalogue& Catalogue, const SolidArcSnapCurve& Curve, bool OpenCondition)
{
    Vector3 Tangent, Normal;
    if (OpenCondition)
    {
        for (const double Parameter : { Curve.Low, Curve.High })
        {
            DescribeCurvePoint(Curve, Parameter, Tangent, Normal);
            AppendPoint(Catalogue.Points, { SolidArcSnapKind::Endpoint, EvaluateSnapCurve(Curve, Parameter).Position, Tangent, Normal, Curve.Source, Curve.Body }, Catalogue.Scale);
        }
        const double Middle = LocateFraction(Curve, 0.5);
        DescribeCurvePoint(Curve, Middle, Tangent, Normal);
        AppendPoint(Catalogue.Points, { SolidArcSnapKind::Midpoint, EvaluateSnapCurve(Curve, Middle).Position, Tangent, Normal, Curve.Source, Curve.Body }, Catalogue.Scale);
    }
    // Quarter points: ¼ and ¾ of an open curve; a closed one (circle, closed spline) at 0, ¼, ½, ¾ — a circle's quadrants.
    const std::vector<double> Fractions = OpenCondition ? std::vector<double>{ 0.25, 0.75 } : std::vector<double>{ 0.0, 0.25, 0.5, 0.75 };
    for (const double Fraction : Fractions)
    {
        const double Parameter = LocateFraction(Curve, Fraction);
        DescribeCurvePoint(Curve, Parameter, Tangent, Normal);
        AppendPoint(Catalogue.Points, { SolidArcSnapKind::Quarter, EvaluateSnapCurve(Curve, Parameter).Position, Tangent, Normal, Curve.Source, Curve.Body }, Catalogue.Scale);
    }
    if (Curve.Centre) AppendPoint(Catalogue.Points, { SolidArcSnapKind::Centre, *Curve.Centre, {}, Curve.PlaneNormal, Curve.Source, Curve.Body }, Catalogue.Scale);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SCREEN
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] Vector3 RayOrigin(const OrbitRay& Ray) noexcept { return { Ray.Origin[0], Ray.Origin[1], Ray.Origin[2] }; }
[[nodiscard]] Vector3 RayDirection(const OrbitRay& Ray) noexcept { return { Ray.Direction[0], Ray.Direction[1], Ray.Direction[2] }; }

[[nodiscard]] double MeasurePixels(const SolidArcSnapQuery& Query, Vector3 Point) noexcept
{
    double PixelX = 0.0, PixelY = 0.0;
    if (!ProjectSolidArcPoint(Query.Frame, Point, PixelX, PixelY)) return 1.0e300;
    return std::hypot(PixelX - Query.PixelX, PixelY - Query.PixelY);
}

// Pixel distance from the pointer to the segment between two projected points.
[[nodiscard]] double MeasureSegmentPixels(double AX, double AY, double BX, double BY, double PointerX, double PointerY, double& Fraction) noexcept
{
    const double DX = BX - AX, DY = BY - AY, Square = DX * DX + DY * DY;
    Fraction = Square > 0.0 ? std::clamp(((PointerX - AX) * DX + (PointerY - AY) * DY) / Square, 0.0, 1.0) : 0.0;
    return std::hypot(AX + Fraction * DX - PointerX, AY + Fraction * DY - PointerY);
}

// 📝 Hidden when a face of the scene lies in front of it along its own pixel ray (tolerance: tessellation sag + 3 px), or
//    when it sits on the removed side of the section. X-ray shows everything that is not cut away.
[[nodiscard]] bool VisibleCondition(const SolidArcScene& Scene, const SolidArcSnapQuery& Query, Vector3 Point, double Scale, bool Overlay = false)
{
    if (Query.Section)
    {
        const std::array<double, 4>& Plane = Query.Section->Plane;
        if (Plane[0] * Point.X + Plane[1] * Point.Y + Plane[2] * Point.Z + Plane[3] < -1.0e-9 * (1.0 + Scale)) return false;
    }
    if (Query.XRayEnabled || Overlay || Scene.Corners.empty()) return true;
    double PixelX = 0.0, PixelY = 0.0;
    if (!ProjectSolidArcPoint(Query.Frame, Point, PixelX, PixelY)) return false;
    const OrbitRay Ray   = CastOrbitRay(Query.Frame, PixelX, PixelY);
    const double      Depth = GeometryProcessing::Dot(Point - RayOrigin(Ray), RayDirection(Ray));
    const std::optional<SolidArcPick> Hit = PickSolidArcScene(Scene, Ray, 0.0, false, Query.Section);
    return !Hit || Hit->Distance >= Depth - (3.0e-3 * Scale + 3.0 * Query.Frame.WorldPerPixel);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CANDIDATES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] int RankOf(SolidArcSnapKind Kind) noexcept
{
    switch (Kind)
    {
    case SolidArcSnapKind::Endpoint:      return 0;
    case SolidArcSnapKind::Intersection:  return 1;
    case SolidArcSnapKind::Centre:        return 2;
    case SolidArcSnapKind::Midpoint:      return 3;
    case SolidArcSnapKind::Quarter:       return 6;                                // Generic: a reference-driven point there wins
    case SolidArcSnapKind::Tangent:       return 4;
    case SolidArcSnapKind::Perpendicular: return 5;
    default:                              return 9;
    }
}

[[nodiscard]] bool EnabledCondition(const SolidArcSnapFilter& Filter, SolidArcSnapKind Kind) noexcept
{
    switch (Kind)
    {
    case SolidArcSnapKind::Endpoint:      return Filter.EndpointEnabled;
    case SolidArcSnapKind::Midpoint:      return Filter.MidpointEnabled;
    case SolidArcSnapKind::Quarter:       return Filter.QuarterEnabled;
    case SolidArcSnapKind::Centre:        return Filter.CentreEnabled;
    case SolidArcSnapKind::Intersection:  return Filter.IntersectionEnabled;
    case SolidArcSnapKind::Tangent:       return Filter.TangentEnabled;
    case SolidArcSnapKind::Perpendicular: return Filter.PerpendicularEnabled;
    case SolidArcSnapKind::Curve:         return Filter.CurveEnabled;
    case SolidArcSnapKind::Face:          return Filter.FaceEnabled;
    default:                              return true;
    }
}

// The best point candidate so far (pixels + rank tie-break).
struct PointContest
{
    SolidArcSnap Best  = {};
    double       Score = 1.0e300;

    void Offer(const SolidArcSnapQuery& Query, SolidArcSnapKind Kind, Vector3 Position, Vector3 Tangent, Vector3 Normal, const std::string& Source, double Pixels)
    {
        if (!EnabledCondition(Query.Filter, Kind) || Pixels > Query.PointReach) return;
        const double Candidate = Pixels + RankPixels * RankOf(Kind);
        if (Candidate >= Score) return;
        Score = Candidate;
        Best  = { Kind, Position, Tangent, Normal, Source, Pixels };
    }
};

// A curve near the pointer: its nearest sample segment on screen.
struct NearCurve
{
    size_t Index    = 0;                                                                     // [-]  - Into Catalogue.Curves
    double Pixels   = 1.0e300;                                                               // [px] - Polyline → pointer
    double Seed     = 0.0;                                                                   // [-]  - Parameter at the nearest polyline point
    size_t Sample   = 0;                                                                     // [-]  - Segment start sample
};

[[nodiscard]] std::optional<NearCurve> ApproachCurve(const SolidArcSnapCatalogue& Catalogue, const SolidArcSnapQuery& Query, size_t Index)
{
    const SolidArcSnapCurve& Curve = Catalogue.Curves[Index];
    NearCurve Near{ Index };
    double PreviousX = 0.0, PreviousY = 0.0;
    bool   PreviousCondition = false;
    for (size_t Sample = 0; Sample < Curve.Points.size(); ++Sample)
    {
        double PixelX = 0.0, PixelY = 0.0;
        const bool Shown = ProjectSolidArcPoint(Query.Frame, Curve.Points[Sample], PixelX, PixelY);
        if (Shown && PreviousCondition)
        {
            double Fraction = 0.0;
            const double Pixels = MeasureSegmentPixels(PreviousX, PreviousY, PixelX, PixelY, Query.PixelX, Query.PixelY, Fraction);
            if (Pixels < Near.Pixels)
            {
                Near.Pixels = Pixels;
                Near.Sample = Sample - 1;
                Near.Seed   = Curve.Parameters[Sample - 1] + Fraction * (Curve.Parameters[Sample] - Curve.Parameters[Sample - 1]);
            }
        }
        PreviousX = PixelX; PreviousY = PixelY; PreviousCondition = Shown;
    }
    if (Near.Pixels == 1.0e300) return std::nullopt;
    return Near;
}

// 📝 Nearest point of the curve to the pointer ray: golden section on |w − (w·d)d|², w = C(s) − O, over the two sample
//    segments around the screen seed. C(s) is on the carrier whatever s converges to.
[[nodiscard]] double ApproachRay(const SolidArcSnapCurve& Curve, const NearCurve& Near, const OrbitRay& Ray)
{
    const Vector3 Origin = RayOrigin(Ray), Direction = RayDirection(Ray);
    const auto Gap = [&](double Parameter)
    {
        const Vector3 Offset = EvaluateSnapCurve(Curve, Parameter).Position - Origin;
        const Vector3 Across = Offset - Direction * GeometryProcessing::Dot(Offset, Direction);
        return GeometryProcessing::Dot(Across, Across);
    };
    double Low  = Curve.Parameters[Near.Sample > 0 ? Near.Sample - 1 : 0];
    double High = Curve.Parameters[std::min(Near.Sample + 2, Curve.Parameters.size() - 1)];
    constexpr double Golden = 0.6180339887498949;
    double Left = High - Golden * (High - Low), Right = Low + Golden * (High - Low);
    double LeftGap = Gap(Left), RightGap = Gap(Right);
    for (int Iteration = 0; Iteration < 90 && High - Low > 1.0e-15 * (1.0 + std::fabs(High)); ++Iteration)
    {
        if (LeftGap < RightGap) { High = Right; Right = Left; RightGap = LeftGap; Left = High - Golden * (High - Low); LeftGap = Gap(Left); }
        else { Low = Left; Left = Right; LeftGap = RightGap; Right = Low + Golden * (High - Low); RightGap = Gap(Right); }
    }
    return 0.5 * (Low + High);
}

// 📝 Crossing of two carriers: Gauss–Newton on A(s) − B(t) = 0 from the screen seeds (normal equations of the 3 × 2
//    Jacobian [A′ −B′]); a crossing when the gap closes below MeetTolerance of the scene size.
[[nodiscard]] std::optional<Vector3> MeetCurves(const SolidArcSnapCurve& First, double FirstSeed, const SolidArcSnapCurve& Second, double SecondSeed, double Scale)
{
    double S = FirstSeed, T = SecondSeed;
    for (int Iteration = 0; Iteration < 60; ++Iteration)
    {
        const CurvePoint A = EvaluateSnapCurve(First, S), B = EvaluateSnapCurve(Second, T);
        const Vector3 Gap = A.Position - B.Position;
        if (GeometryProcessing::Length(Gap) <= 1.0e-15 * (1.0 + Scale)) break;
        const double AA = GeometryProcessing::Dot(A.Derivative, A.Derivative), AB = -GeometryProcessing::Dot(A.Derivative, B.Derivative), BB = GeometryProcessing::Dot(B.Derivative, B.Derivative);
        const double Determinant = AA * BB - AB * AB;
        if (!(std::fabs(Determinant) > 1.0e-14 * AA * BB)) return std::nullopt;               // Parallel carriers: no isolated crossing
        const double RightS = -GeometryProcessing::Dot(A.Derivative, Gap), RightT = GeometryProcessing::Dot(B.Derivative, Gap);
        const double StepS = (RightS * BB - AB * RightT) / Determinant, StepT = (AA * RightT - AB * RightS) / Determinant;
        S = ConfineParameter(First, S + StepS);
        T = ConfineParameter(Second, T + StepT);
        if (std::fabs(StepS) + std::fabs(StepT) <= 1.0e-16 * (1.0 + std::fabs(S) + std::fabs(T))) break;
    }
    const CurvePoint A = EvaluateSnapCurve(First, S), B = EvaluateSnapCurve(Second, T);
    if (GeometryProcessing::Distance(A.Position, B.Position) > MeetTolerance * (1.0 + Scale)) return std::nullopt;
    // Overlapping carriers (a consumed sketch line on its body edge) touch everywhere: only a transversal meeting counts.
    const double Crossing = GeometryProcessing::Length(GeometryProcessing::Cross(A.Derivative, B.Derivative)), Speeds = GeometryProcessing::Length(A.Derivative) * GeometryProcessing::Length(B.Derivative);
    if (!(Crossing > 1.0e-6 * Speeds)) return std::nullopt;
    return A.Position;
}

// 📝 Roots of Residual(s) over the samples: sign changes (or exact zeros) bracketed and bisected to machine precision.
template <typename ResidualFunction>
std::vector<double> BracketRoots(const SolidArcSnapCurve& Curve, ResidualFunction Residual)
{
    std::vector<double> Roots;
    double PreviousParameter = Curve.Parameters.front(), Previous = Residual(PreviousParameter);
    for (size_t Sample = 1; Sample < Curve.Parameters.size(); ++Sample)
    {
        const double Parameter = Curve.Parameters[Sample], Value = Residual(Parameter);
        if (Previous == 0.0) Roots.push_back(PreviousParameter);
        else if (Value != 0.0 && (Previous < 0.0) != (Value < 0.0))
        {
            double Low = PreviousParameter, High = Parameter, LowValue = Previous;
            for (int Halving = 0; Halving < 80 && High - Low > 0.0; ++Halving)
            {
                const double Middle = 0.5 * (Low + High);
                if (Middle <= Low || Middle >= High) break;
                const double MiddleValue = Residual(Middle);
                if (MiddleValue == 0.0) { Low = High = Middle; break; }
                if ((MiddleValue < 0.0) == (LowValue < 0.0)) { Low = Middle; LowValue = MiddleValue; } else High = Middle;
            }
            Roots.push_back(0.5 * (Low + High));
        }
        PreviousParameter = Parameter;
        Previous          = Value;
    }
    if (Previous == 0.0) Roots.push_back(PreviousParameter);
    return Roots;
}

// 📝 Exact ray hit on a face carrier: Newton on the signed distance along the ray from the tessellation hit; the result
//    is the carrier foot of the last iterate (on the surface to rounding).
[[nodiscard]] Vector3 PierceSurface(const GeometryProcessing::SurfaceGeometry& Surface, const OrbitRay& Ray, double Reach, Vector3& Normal, double Scale)
{
    const Vector3 Origin = RayOrigin(Ray), Direction = RayDirection(Ray);
    double Along = Reach;
    GeometryProcessing::SurfaceDistance Measure = GeometryProcessing::MeasureSurfaceDistance(Surface, Origin + Direction * Along);
    for (int Iteration = 0; Iteration < 16 && std::fabs(Measure.Distance) > 1.0e-14 * (1.0 + Scale); ++Iteration)
    {
        const double Facing = GeometryProcessing::Dot(Direction, Measure.Normal);
        if (std::fabs(Facing) < 1.0e-6) break;
        Along  -= Measure.Distance / Facing;
        Measure = GeometryProcessing::MeasureSurfaceDistance(Surface, Origin + Direction * Along);
    }
    Normal = Measure.Normal;
    return Measure.Foot;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool ProjectSolidArcPoint(const OrbitCameraFrame& Frame, Vector3 Point, double& PixelX, double& PixelY) noexcept
{
    double Clip[4] = {};
    for (int Row = 0; Row < 4; ++Row)
        Clip[Row] = Frame.ClipFromWorld[0 * 4 + Row] * Point.X + Frame.ClipFromWorld[1 * 4 + Row] * Point.Y + Frame.ClipFromWorld[2 * 4 + Row] * Point.Z + Frame.ClipFromWorld[3 * 4 + Row];
    if (Clip[3] <= 1.0e-12) return false;
    PixelX = (Clip[0] / Clip[3] * 0.5 + 0.5) * Frame.Width;
    PixelY = (Clip[1] / Clip[3] * 0.5 + 0.5) * Frame.Height;
    return true;
}

std::optional<double> ApproachSolidArcLine(const OrbitRay& Ray, Vector3 Origin, Vector3 Direction) noexcept
{
    // Closest points of two lines: Origin + t·u and O + s·d; t = (b·e − c·d′) / (a·c − b²) with the usual dot products.
    const Vector3 Unit = GeometryProcessing::Normalize(Direction), Along = RayDirection(Ray), Offset = Origin - RayOrigin(Ray);
    const double  Cosine = GeometryProcessing::Dot(Unit, Along), Denominator = 1.0 - Cosine * Cosine;
    if (Denominator < 1.0e-12) return std::nullopt;
    const double Parameter = (Cosine * GeometryProcessing::Dot(Offset, Along) - GeometryProcessing::Dot(Offset, Unit)) / Denominator;
    return Parameter / GeometryProcessing::Length(Direction);
}

SolidArcSnapCatalogue CatalogueSolidArcSnaps(const SolidArcDocument& Document, const SolidArcScene& Scene, const std::vector<std::string>& Excluded)
{
    SolidArcSnapCatalogue Catalogue;
    Catalogue.Revision = Document.Revision;
    for (const std::string& Key : Excluded) Catalogue.ExclusionKey += Key + ";";
    Catalogue.Scale = std::max(1.0e-9, std::hypot(Scene.BoundsHigh[0] - Scene.BoundsLow[0], Scene.BoundsHigh[1] - Scene.BoundsLow[1], Scene.BoundsHigh[2] - Scene.BoundsLow[2]));
    Catalogue.Surfaces.resize(Scene.Faces.size());
    Catalogue.FaceTokens.resize(Scene.Faces.size());
    Catalogue.FaceNames.resize(Scene.Faces.size());
    const auto ExcludedCondition = [&](const std::string& Key) { return std::find(Excluded.begin(), Excluded.end(), Key) != Excluded.end(); };

    // ---- Bodies: face carriers by (body, slot) → scene face; edges with their exact carriers and vertices
    if (Document.Partition)
    {
        const GeometryProcessing::ModelPartition& Partition = *Document.Partition;
        std::map<std::pair<std::string, uint32_t>, uint32_t> SceneFaces;
        for (const SolidArcSceneBody& Body : Scene.Bodies)
            for (uint32_t Face = Body.FirstFace; Face < Body.FirstFace + Body.FaceCount; ++Face) SceneFaces[{ Body.Key, Scene.Faces[Face].Slot }] = Face;
        for (const GeometryProcessing::RegeneratedBody& Body : Document.Regeneration.Bodies)
        {
            if (!Body.Body.PresentCondition() || Document.Appearance.HiddenBodies.contains(Body.Key) || ExcludedCondition(Body.Key)) continue;
            const GeometryProcessing::BodyInventory Inventory = Partition.CollectInventory(Body.Body);
            for (const GeometryProcessing::FaceToken Face : Inventory.Faces)
            {
                const auto Found = SceneFaces.find({ Body.Key, Face.Slot });
                const GeometryProcessing::FaceProfile& Profile = Partition.Faces.Resolve(Face);
                if (Found == SceneFaces.end() || !Profile.Surface.PresentCondition()) continue;
                Catalogue.Surfaces[Found->second]   = Partition.Surfaces.Resolve(Profile.Surface);
                Catalogue.FaceTokens[Found->second] = Face;
                const auto Name = Body.Naming.Faces.find(Face.Slot);
                Catalogue.FaceNames[Found->second]  = Name != Body.Naming.Faces.end() ? Name->second : Scene.Faces[Found->second].Name;
            }
            for (const GeometryProcessing::EdgeToken Edge : Inventory.Edges)
            {
                const GeometryProcessing::EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
                if (!Profile.Curve.PresentCondition()) continue;
                SolidArcSnapCurve Curve;
                const auto Name = Body.Naming.Edges.find(Edge.Slot);
                Curve.Source  = "Edge " + (Name != Body.Naming.Edges.end() ? Name->second : std::to_string(Edge.Slot));
                Curve.Body    = Body.Key;
                Curve.Carrier = Partition.Curves.Resolve(Profile.Curve);
                Curve.Low     = Profile.IntervalLow;
                Curve.High    = Profile.IntervalHigh;
                if (const std::optional<GeometryProcessing::ConicForm> Conic = GeometryProcessing::ResolveConicForm(*Curve.Carrier))
                {
                    Curve.PlanarCondition = true;
                    Curve.PlaneNormal     = Conic->Normal;
                    Curve.Centre          = Conic->Centre;
                    if (const auto* Arc = std::get_if<GeometryProcessing::CircularArc>(&*Curve.Carrier)) Curve.Radius = Arc->Radius;
                }
                const bool Straight = std::holds_alternative<GeometryProcessing::StraightLine>(*Curve.Carrier);
                SampleCurve(Curve, Straight ? 1 : CurveSamples);
                const Vector3 Start = Partition.Vertices.Resolve(Profile.Start).Position, End = Partition.Vertices.Resolve(Profile.End).Position;
                const bool    Open  = GeometryProcessing::Distance(Start, End) > 1.0e-12 * (1.0 + Catalogue.Scale);
                CatalogueCurvePoints(Catalogue, Curve, Open);
                Catalogue.Curves.push_back(std::move(Curve));
            }
        }
    }

    // ---- Sketches: solved, on the elevated plane; ends, midpoints, centres; crossings within one plane frame
    struct PlacedSegment { size_t Curve; GeometryProcessing::OrthonormalFrame Plane; Vector2 Low, High; };
    std::vector<PlacedSegment> Placed;
    for (const GeometryProcessing::FeatureStep& Step : Document.Tree.Steps)
    {
        if (Step.Kind != GeometryProcessing::FeatureStepKind::Sketch || ExcludedCondition(Step.Label) || Document.Appearance.HiddenCurves.contains(Step.Label)) continue;
        const auto Solved = Document.Regeneration.Sketches.find(Step.Label);
        const GeometryProcessing::SketchModel& Sketch = Solved != Document.Regeneration.Sketches.end() ? Solved->second : Step.Sketch;
        GeometryProcessing::OrthonormalFrame Plane = Step.Plane;
        Plane.Origin = Plane.Origin + Plane.ZAxis * GeometryProcessing::EvaluateFeatureValue(Document.Tree, Step.Elevation);
        const auto   Caption = Document.Captions.find(Step.Label);
        const std::string Owner = Caption != Document.Captions.end() ? Caption->second : Step.Label;
        for (int Index = 0; Index < int(Sketch.Curves.size()); ++Index)
        {
            std::string Refusal;
            const GeometryProcessing::ProfileSegment Segment = GeometryProcessing::ResolveSketchCurveSegment(Sketch, Index, 1.0e-9, Refusal);
            if (!Refusal.empty()) continue;
            SolidArcSnapCurve Curve;
            const std::string Name = Segment.Name.empty() ? "Curve" : Segment.Name;
            Curve.Source          = Name == Owner ? Name : Name + " · " + Owner;
            Curve.OverlaySnap     = !SolidArcSketchConsumedCondition(Document, Step.Label);
            Curve.Segment         = Segment;
            Curve.Plane           = Plane;
            Curve.PlanarCondition = true;
            Curve.PlaneNormal     = Plane.ZAxis;
            if (Segment.Kind == GeometryProcessing::ProfileSegmentKind::Arc || Segment.Kind == GeometryProcessing::ProfileSegmentKind::Circle)
            {
                Curve.Centre = PlacePlanar(Plane, Segment.Centre);
                Curve.Radius = Segment.Radius;
            }
            SampleCurve(Curve, Segment.Kind == GeometryProcessing::ProfileSegmentKind::Line ? 1 : CurveSamples);
            CatalogueCurvePoints(Catalogue, Curve, Segment.Kind != GeometryProcessing::ProfileSegmentKind::Circle);
            Vector2 Low{ 1.0e300, 1.0e300 }, High{ -1.0e300, -1.0e300 };
            for (double Parameter : Curve.Parameters)
            {
                const Vector2 Point = GeometryProcessing::EvaluateProfileSegment(Segment, Parameter);
                Low  = { std::min(Low.X, Point.X), std::min(Low.Y, Point.Y) };
                High = { std::max(High.X, Point.X), std::max(High.Y, Point.Y) };
            }
            const double Bulge = Segment.Kind == GeometryProcessing::ProfileSegmentKind::Line ? 0.0 : 0.05 * std::max(High.X - Low.X, High.Y - Low.Y) + Segment.Radius * 0.01;
            Placed.push_back({ Catalogue.Curves.size(), Plane, { Low.X - Bulge, Low.Y - Bulge }, { High.X + Bulge, High.Y + Bulge } });
            Catalogue.Curves.push_back(std::move(Curve));
        }
    }
    const auto SameFrame = [&](const GeometryProcessing::OrthonormalFrame& First, const GeometryProcessing::OrthonormalFrame& Second)
    {
        const double Tolerance = 1.0e-12 * (1.0 + Catalogue.Scale);
        return GeometryProcessing::Distance(First.Origin, Second.Origin) <= Tolerance && GeometryProcessing::Distance(First.XAxis, Second.XAxis) <= 1.0e-12 && GeometryProcessing::Distance(First.YAxis, Second.YAxis) <= 1.0e-12;
    };
    for (size_t First = 0; First < Placed.size(); ++First)
        for (size_t Second = First + 1; Second < Placed.size(); ++Second)
        {
            const PlacedSegment& A = Placed[First];
            const PlacedSegment& B = Placed[Second];
            if (A.High.X < B.Low.X || B.High.X < A.Low.X || A.High.Y < B.Low.Y || B.High.Y < A.Low.Y || !SameFrame(A.Plane, B.Plane)) continue;
            const SolidArcSnapCurve& CurveA = Catalogue.Curves[A.Curve];
            const SolidArcSnapCurve& CurveB = Catalogue.Curves[B.Curve];
            for (const GeometryProcessing::SegmentCrossing& Crossing : GeometryProcessing::IntersectProfileSegments(*CurveA.Segment, *CurveB.Segment, 1.0e-9))
            {
                Vector3 Tangent, Normal;
                DescribeCurvePoint(CurveA, Crossing.TargetFraction, Tangent, Normal);
                AppendPoint(Catalogue.Points, { SolidArcSnapKind::Intersection, PlacePlanar(A.Plane, Crossing.Position), Tangent, Normal, CurveA.Source + " × " + CurveB.Source, {} }, Catalogue.Scale);
            }
        }

    // ---- Fitted SpaceCurves (NURBS over [0, 1])
    for (const auto& [Label, Fit] : Document.Regeneration.Curves)
    {
        if (!Fit.Curve.Definition || ExcludedCondition(Label) || Document.Appearance.HiddenCurves.contains(Label)) continue;
        SolidArcSnapCurve Curve;
        const auto Caption = Document.Captions.find(Label);
        Curve.OverlaySnap = true;
        Curve.Source  = "Curve " + (Caption != Document.Captions.end() ? Caption->second : Label);
        Curve.Carrier = GeometryProcessing::CurveGeometry{ Fit.Curve };
        Curve.Low     = 0.0;
        Curve.High    = 1.0;
        if (GeometryProcessing::Length(Fit.PlaneNormal) > 0.5) { Curve.PlanarCondition = true; Curve.PlaneNormal = GeometryProcessing::Normalize(Fit.PlaneNormal); }
        SampleCurve(Curve, CurveSamples * 2);
        const bool Open = GeometryProcessing::Distance(Curve.Points.front(), Curve.Points.back()) > 1.0e-12 * (1.0 + Catalogue.Scale);
        CatalogueCurvePoints(Catalogue, Curve, Open);
        Catalogue.Curves.push_back(std::move(Curve));
    }
    return Catalogue;
}

SolidArcSnap FindSolidArcSnap(SolidArcSnapCatalogue& Catalogue, const SolidArcDocument& Document, const SolidArcScene& Scene, const SolidArcSnapQuery& Query)
{
    const double      Scale = Catalogue.Scale;
    const OrbitRay Ray   = CastOrbitRay(Query.Frame, Query.PixelX, Query.PixelY);
    PointContest      Contest;

    // ---- Fixed and transient points
    for (const SolidArcSnapPoint& Point : Catalogue.Points)
    {
        if (!EnabledCondition(Query.Filter, Point.Kind)) continue;
        const bool Overlay = std::any_of(Catalogue.Curves.begin(),Catalogue.Curves.end(),[&](const auto& C){return C.OverlaySnap && C.Source==Point.Source;});
        if(Query.FreeCurvesOnly && !Overlay) continue;
        const double Pixels = MeasurePixels(Query, Point.Position);
        if (Pixels > Query.PointReach || !VisibleCondition(Scene, Query, Point.Position, Scale, Query.OverlayCurvesVisible && Overlay)) continue;
        Contest.Offer(Query, Point.Kind, Point.Position, Point.Tangent, Point.Normal, Point.Source, Pixels);
    }
    for (const SolidArcSnapPoint& Point : Query.Transient)
        Contest.Offer(Query, Point.Kind, Point.Position, Point.Tangent, Point.Normal, Point.Source, MeasurePixels(Query, Point.Position));

    // ---- Curves near the pointer
    std::vector<NearCurve> Near;
    const double Reach = std::max(Query.PointReach, Query.CurveReach);
    if (Query.Filter.CurveEnabled || Query.Filter.IntersectionEnabled || Query.Filter.TangentEnabled || Query.Filter.PerpendicularEnabled)
        for (size_t Index = 0; Index < Catalogue.Curves.size(); ++Index)
        {
            if (Query.BodyCurvesOnly && Catalogue.Curves[Index].Body.empty()) continue;
            if (Query.FreeCurvesOnly && !Catalogue.Curves[Index].OverlaySnap) continue;
            if (const std::optional<NearCurve> Approach = ApproachCurve(Catalogue, Query, Index); Approach && Approach->Pixels <= Reach) Near.push_back(*Approach);
        }
    std::sort(Near.begin(), Near.end(), [](const NearCurve& First, const NearCurve& Second) { return First.Pixels < Second.Pixels; });

    // ---- Crossings of the nearest curves (both carriers exact)
    if (Query.Filter.IntersectionEnabled)
        for (size_t First = 0; First < std::min<size_t>(Near.size(), NearCurveLimit); ++First)
            for (size_t Second = First + 1; Second < std::min<size_t>(Near.size(), NearCurveLimit); ++Second)
            {
                const SolidArcSnapCurve& A = Catalogue.Curves[Near[First].Index];
                const SolidArcSnapCurve& B = Catalogue.Curves[Near[Second].Index];
                const std::optional<Vector3> Meeting = MeetCurves(A, Near[First].Seed, B, Near[Second].Seed, Scale);
                if (!Meeting || !VisibleCondition(Scene, Query, *Meeting, Scale)) continue;
                Vector3 Tangent, Normal;
                DescribeCurvePoint(A, ConfineParameter(A, Near[First].Seed), Tangent, Normal);
                Contest.Offer(Query, SolidArcSnapKind::Intersection, *Meeting, Tangent, Normal, A.Source + " × " + B.Source, MeasurePixels(Query, *Meeting));
            }

    // ---- Tangent and perpendicular points from the last placed point
    if (Query.Reference && (Query.Filter.TangentEnabled || Query.Filter.PerpendicularEnabled))
    {
        const Vector3 Reference = *Query.Reference;
        for (const NearCurve& Each : Near)
        {
            if (Each.Pixels > Query.PointReach) continue;
            const SolidArcSnapCurve& Curve = Catalogue.Curves[Each.Index];
            const auto Offer = [&](SolidArcSnapKind Kind, double Parameter)
            {
                const Vector3 Position = EvaluateSnapCurve(Curve, Parameter).Position;
                if (GeometryProcessing::Distance(Position, Reference) <= 1.0e-9 * (1.0 + Scale) || !VisibleCondition(Scene, Query, Position, Scale)) return;
                Vector3 Tangent, Normal;
                DescribeCurvePoint(Curve, Parameter, Tangent, Normal);
                Contest.Offer(Query, Kind, Position, Tangent, Normal, Curve.Source, MeasurePixels(Query, Position));
            };
            if (Query.Filter.PerpendicularEnabled)
                for (double Root : BracketRoots(Curve, [&](double Parameter) { const CurvePoint Point = EvaluateSnapCurve(Curve, Parameter); return GeometryProcessing::Dot(Point.Position - Reference, Point.Derivative); }))
                    Offer(SolidArcSnapKind::Perpendicular, Root);
            // Tangent lines exist in the curve's plane only: the reference must lie in it.
            const bool Coplanar = Curve.PlanarCondition && std::fabs(GeometryProcessing::Dot(Reference - Curve.Points.front(), Curve.PlaneNormal)) <= 1.0e-9 * (1.0 + Scale);
            if (Query.Filter.TangentEnabled && Coplanar)
                for (double Root : BracketRoots(Curve, [&](double Parameter) { const CurvePoint Point = EvaluateSnapCurve(Curve, Parameter); return GeometryProcessing::Dot(GeometryProcessing::Cross(Point.Position - Reference, Point.Derivative), Curve.PlaneNormal); }))
                    Offer(SolidArcSnapKind::Tangent, Root);
        }
    }
    if (Contest.Best.Kind != SolidArcSnapKind::None) return Contest.Best;

    // ---- Nearest point on a curve
    if (Query.Filter.CurveEnabled)
    {
        SolidArcSnap Best;
        double BestPixels = 1.0e300;
        for (const NearCurve& Each : Near)
        {
            if (Each.Pixels > Query.CurveReach) continue;
            const SolidArcSnapCurve& Curve     = Catalogue.Curves[Each.Index];
            const double             Parameter = ApproachRay(Curve, Each, Ray);
            const Vector3            Position  = EvaluateSnapCurve(Curve, Parameter).Position;
            const double             Pixels    = MeasurePixels(Query, Position);
            if (Pixels > Query.CurveReach || Pixels >= BestPixels || !VisibleCondition(Scene, Query, Position, Scale, Query.OverlayCurvesVisible && Curve.OverlaySnap)) continue;
            Vector3 Tangent, Normal;
            DescribeCurvePoint(Curve, Parameter, Tangent, Normal);
            BestPixels = Pixels;
            Best       = { SolidArcSnapKind::Curve, Position, Tangent, Normal, Curve.Source, Pixels };
        }
        if (Best.Kind != SolidArcSnapKind::None) return Best;
    }

    // ---- The face under the pointer: its centre (planar faces) or the exact ray hit
    if (Query.Filter.FaceEnabled || Query.Filter.CentreEnabled)
    {
        const std::optional<SolidArcPick> Hit = PickSolidArcScene(Scene, Ray, 0.0, false, Query.Section);
        if (Hit && Hit->Identity != 0 && Hit->Identity != IdentitySectionCap && !(Hit->Identity & IdentityEdgeBit) && Hit->Identity - 1 < Catalogue.Surfaces.size())
        {
            const uint32_t Face = Hit->Identity - 1;
            if (const std::optional<GeometryProcessing::SurfaceGeometry>& Surface = Catalogue.Surfaces[Face])
            {
                const double Sense = Document.Partition && Document.Partition->Faces.Resolve(Catalogue.FaceTokens[Face]).Sense == GeometryProcessing::Orientation::Reversed ? -1.0 : 1.0;
                if (Query.Filter.CentreEnabled && std::holds_alternative<GeometryProcessing::PlaneSurface>(*Surface) && Document.Partition)
                {
                    auto Found = Catalogue.FaceCentres.find(Face);
                    if (Found == Catalogue.FaceCentres.end())
                    {
                        const GeometryProcessing::FaceAreaMeasure Area = GeometryProcessing::MeasureFaceArea(*Document.Partition, Catalogue.FaceTokens[Face]);
                        Found = Catalogue.FaceCentres.emplace(Face, Area.Area > 0.0 ? std::optional<Vector3>(Area.Centroid) : std::nullopt).first;
                    }
                    if (Found->second)
                    {
                        const double Pixels = MeasurePixels(Query, *Found->second);
                        if (Pixels <= Query.PointReach && VisibleCondition(Scene, Query, *Found->second, Scale))
                            return { SolidArcSnapKind::Centre, *Found->second, {}, std::get<GeometryProcessing::PlaneSurface>(*Surface).Frame.ZAxis * Sense, "Face " + Catalogue.FaceNames[Face] + " centre", Pixels };
                    }
                }
                if (Query.Filter.FaceEnabled)
                {
                    Vector3 Normal;
                    const Vector3 Position = PierceSurface(*Surface, Ray, Hit->Distance, Normal, Scale);
                    return { SolidArcSnapKind::Face, Position, {}, Normal * Sense, "Face " + Catalogue.FaceNames[Face], MeasurePixels(Query, Position) };
                }
            }
        }
    }
    return {};
}

const char* SolidArcSnapLabel(SolidArcSnapKind Kind) noexcept
{
    switch (Kind)
    {
    case SolidArcSnapKind::Grid:          return "Grid";
    case SolidArcSnapKind::Endpoint:      return "Endpoint";
    case SolidArcSnapKind::Midpoint:      return "Midpoint";
    case SolidArcSnapKind::Quarter:       return "Quarter";
    case SolidArcSnapKind::Centre:        return "Centre";
    case SolidArcSnapKind::Intersection:  return "Intersection";
    case SolidArcSnapKind::Tangent:       return "Tangent";
    case SolidArcSnapKind::Perpendicular: return "Perpendicular";
    case SolidArcSnapKind::Curve:         return "On curve";
    case SolidArcSnapKind::Face:          return "On face";
    case SolidArcSnapKind::Axis:          return "Axis";
    default:                              return "";
    }
}

const char* SolidArcSnapLockLabel(SolidArcSnapLockKind Kind) noexcept
{
    switch (Kind)
    {
    case SolidArcSnapLockKind::X:        return "x";
    case SolidArcSnapLockKind::Y:        return "y";
    case SolidArcSnapLockKind::Z:        return "z";
    case SolidArcSnapLockKind::Tangent:  return "tangent";
    case SolidArcSnapLockKind::Normal:   return "normal";
    case SolidArcSnapLockKind::Binormal: return "binormal";
    default:                             return "";
    }
}

} // namespace Frontier::SolidArc
