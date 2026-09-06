//============================================================================================================================================
// 📦 ParametricSketcher/Kernel/CurveSpecification.h — NURBS curve: exact conics, de Boor evaluation, refinement, splitting, tessellation
//============================================================================================================================================
// Every sketch entity in the workspace IS a NurbsCurve. Lines are degree 1, arcs / circles / ellipses are exact rational
//    quadratics (Piegl & Tiller §7.5), splines are non-rational cubics by default. Storing one representation means every
//    downstream solver (trim, offset, intersect, sweep, boolean) is written once.
//
// Conventions: clamped knot vectors, Domain() = [Knot[Degree], Knot[Count]], control points homogeneous (Vec4).
#pragma once

#include "VectorSpecification.h"
#include <vector>
#include <utility>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  CURVE CLASSIFICATION
//------------------------------------------------------------------------------------------------------------------------
// The NURBS is the truth; the classification is a hint carried from construction so the console can say "Circle" and
//    the snap solver can offer a centre. Any edit that breaks the analytic form resets it to Freeform.

enum class CurveClassification : uint8_t
{
    Freeform = 0,
    Line,
    Polyline,
    Arc,
    Circle,
    Ellipse,
    Rectangle,
    Polygon,
    Slot,
};

[[nodiscard]] const char* Describe(CurveClassification Classification) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                  NURBS CURVE
//------------------------------------------------------------------------------------------------------------------------

class NurbsCurve
{
public:
    int                 Degree = 1;                                                     // [-]
    std::vector<Vec4>   Poles;                                                          // [m·w] homogeneous control points
    std::vector<double> Knots;                                                          // [-]  size = Poles + Degree + 1
    CurveClassification Classification = CurveClassification::Freeform;                // [-]

    // Analytic memory for the classified forms (centre / radii / axes) so snapping and reporting are exact.
    Vec3   Centre = {};                                                                 // [m]
    Vec3   AxisZ  = Vec3::UnitZ();                                                      // [-] plane normal for planar forms
    double RadiusMajor = 0.0;                                                           // [m]
    double RadiusMinor = 0.0;                                                           // [m]

    //---------------------------------------------- construction ----------------------------------------------
    [[nodiscard]] static Deliver<NurbsCurve> Build(int Degree, std::vector<Vec4> Poles, std::vector<double> Knots) noexcept;
    [[nodiscard]] static Deliver<NurbsCurve> Line(Vec3 A, Vec3 B) noexcept;
    [[nodiscard]] static Deliver<NurbsCurve> Polyline(const std::vector<Vec3>& Points, bool Closed) noexcept;
    [[nodiscard]] static Deliver<NurbsCurve> Circle(Vec3 Centre, Vec3 Normal, double Radius) noexcept;
    [[nodiscard]] static Deliver<NurbsCurve> Arc(Vec3 Centre, Vec3 Normal, double Radius, double StartAngle, double SweepAngle) noexcept;
    [[nodiscard]] static Deliver<NurbsCurve> ArcThreePoints(Vec3 A, Vec3 B, Vec3 C) noexcept;
    [[nodiscard]] static Deliver<NurbsCurve> Ellipse(Vec3 Centre, Vec3 Normal, Vec3 MajorDirection, double RadiusMajor, double RadiusMinor) noexcept;
    [[nodiscard]] static Deliver<NurbsCurve> Rectangle(const Workplane& Plane, Vec2 CornerA, Vec2 CornerB, double CornerRadius = 0.0) noexcept;
    [[nodiscard]] static Deliver<NurbsCurve> Polygon(const Workplane& Plane, Vec2 Centre, double Radius, int Sides, double Rotation, bool Inscribed) noexcept;
    [[nodiscard]] static Deliver<NurbsCurve> Slot(const Workplane& Plane, Vec2 CentreA, Vec2 CentreB, double Radius) noexcept;
    [[nodiscard]] static Deliver<NurbsCurve> Bezier(const std::vector<Vec3>& ControlPoints) noexcept;
    [[nodiscard]] static Deliver<NurbsCurve> ControlPoints(int Degree, const std::vector<Vec3>& Points, bool Periodic) noexcept;
    [[nodiscard]] static Deliver<NurbsCurve> Interpolate(const std::vector<Vec3>& Through, int Degree = 3, bool Closed = false) noexcept;
    // Homogeneous variant: interpolates (wx, wy, wz, w) so rational sections (circles) survive a loft exactly.
    [[nodiscard]] static Deliver<NurbsCurve> InterpolateHomogeneous(const std::vector<Vec4>& Through, int Degree, const std::vector<double>* Parameters = nullptr) noexcept;

    //---------------------------------------------- queries ----------------------------------------------
    [[nodiscard]] int    PoleCount() const noexcept { return static_cast<int>(Poles.size()); }
    [[nodiscard]] int    Order() const noexcept { return Degree + 1; }
    [[nodiscard]] double DomainStart() const noexcept { return Knots[Degree]; }
    [[nodiscard]] double DomainEnd() const noexcept { return Knots[Poles.size()]; }
    [[nodiscard]] bool   Rational() const noexcept;
    [[nodiscard]] bool   Closed(double Tolerance = ScalarCriteria::MergeTolerance) const noexcept;
    [[nodiscard]] bool   Planar(Plane& Result, double Tolerance = ScalarCriteria::MergeTolerance) const noexcept;
    [[nodiscard]] Box3   Bounds() const noexcept;                                       // control-polygon hull (conservative)
    [[nodiscard]] Refusal Validate() const noexcept;

    [[nodiscard]] int    FindSpan(double T) const noexcept;
    void                 BasisFunctions(int Span, double T, double* Out) const noexcept;                 // Out[Order]
    void                 BasisDerivatives(int Span, double T, int DerivativeCount, double* Out) const noexcept; // Out[(D+1)*Order]

    [[nodiscard]] Vec4   SampleHomogeneous(double T) const noexcept;
    [[nodiscard]] Vec3   Sample(double T) const noexcept;
    [[nodiscard]] Vec3   Tangent(double T) const noexcept;                              // unit
    void                 Derivatives(double T, int Count, Vec3* Out) const noexcept;    // Out[0] = point, Out[1] = C', Out[2] = C''
    [[nodiscard]] double Curvature(double T) const noexcept;                            // [1/m]
    [[nodiscard]] Vec3   StartPoint() const noexcept { return Poles.front().Divide(); }
    [[nodiscard]] Vec3   EndPoint() const noexcept { return Poles.back().Divide(); }
    [[nodiscard]] double Length(double Tolerance = 1e-9) const noexcept;                // Gauss-Legendre per span
    [[nodiscard]] double ParameterAtLength(double Length) const noexcept;

    // Closest point on the curve to P: coarse sampling seeds a Newton iteration (Piegl & Tiller 6.3). Returns parameter.
    [[nodiscard]] double ClosestParameter(Vec3 P, double* DistanceOut = nullptr) const noexcept;

    //---------------------------------------------- editing (return new curves; originals untouched) ----------------------------------------------
    [[nodiscard]] NurbsCurve InsertKnot(double T, int Multiplicity = 1) const noexcept;
    [[nodiscard]] NurbsCurve Refined(const std::vector<double>& NewKnots) const noexcept;
    [[nodiscard]] std::pair<NurbsCurve, NurbsCurve> Split(double T) const noexcept;
    [[nodiscard]] NurbsCurve Trimmed(double T0, double T1) const noexcept;
    [[nodiscard]] NurbsCurve Reversed() const noexcept;
    [[nodiscard]] NurbsCurve Elevated(int TargetDegree) const noexcept;
    [[nodiscard]] NurbsCurve Reparameterised(double T0, double T1) const noexcept;      // affine remap of the domain
    [[nodiscard]] NurbsCurve Transformed(const Mat4& M) const noexcept;
    [[nodiscard]] std::vector<NurbsCurve> BezierSegments() const noexcept;

    // C0 concatenation: B is appended at A's end (endpoints must be within MergeTolerance). Degrees are equalised.
    [[nodiscard]] static Deliver<NurbsCurve> Join(const NurbsCurve& A, const NurbsCurve& B) noexcept;

    //---------------------------------------------- tessellation ----------------------------------------------
    // Adaptive: subdivides until chord sagitta ≤ ChordTolerance and turning angle ≤ AngleTolerance. Emits parameters
    //    so callers can carry them through to trim curves on surfaces.
    void Tessellate(std::vector<Vec3>& Points, std::vector<double>* Parameters,
                    double ChordTolerance = ScalarCriteria::ChordTolerance, double AngleTolerance = 0.05) const noexcept;

private:
    void TessellateSpan(double T0, double T1, Vec3 P0, Vec3 P1, int Depth, double ChordTolerance, double AngleTolerance,
                        std::vector<Vec3>& Points, std::vector<double>* Parameters) const noexcept;
    [[nodiscard]] static NurbsCurve PlanarChain(const Workplane& Plane, const std::vector<NurbsCurve>& Pieces) noexcept;
};

} // namespace Frontier
