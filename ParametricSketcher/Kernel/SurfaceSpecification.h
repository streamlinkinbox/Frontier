//============================================================================================================================================
// 📦 ParametricSketcher/Kernel/SurfaceSpecification.h — NURBS tensor-product surface: exact quadrics, patches, sweeps, tessellation
//============================================================================================================================================
// Every face in the workspace sits on a NurbsSurface. Planes are bilinear (1×1), cylinders / cones / spheres / tori are
//    exact rational (degree 2 around the axis), B-spline patches are whatever the user asked for. Poles are stored row-
//    major by U then V: Pole(I, J) = Poles[I * CountV + J].
//
// Normal convention: N = ∂S/∂u × ∂S/∂v. Constructors are arranged so N points OUTWARD for every closed primitive —
//    that is the winding contract every downstream solver (boolean classification, back-face tint) relies on.
#pragma once

#include "CurveSpecification.h"

namespace Frontier
{

enum class SurfaceClassification : uint8_t
{
    Freeform = 0,
    Plane,
    Cylinder,
    Cone,
    Sphere,
    Torus,
    Extrusion,
    Revolution,
    Ruled,
    Loft,
};

[[nodiscard]] const char* Describe(SurfaceClassification Classification) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                  NURBS SURFACE
//------------------------------------------------------------------------------------------------------------------------

class NurbsSurface
{
public:
    int                   DegreeU = 1;                                                  // [-]
    int                   DegreeV = 1;                                                  // [-]
    int                   CountU = 0;                                                   // [-] poles along U
    int                   CountV = 0;                                                   // [-] poles along V
    std::vector<Vec4>     Poles;                                                        // [m·w] CountU × CountV, U-major
    std::vector<double>   KnotsU;                                                       // [-]
    std::vector<double>   KnotsV;                                                       // [-]
    SurfaceClassification Classification = SurfaceClassification::Freeform;            // [-]

    // Analytic memory for classified surfaces (axis frame + radii) — exact snapping, exact intersections later.
    Vec3   Origin = {};                                                                 // [m]
    Vec3   Axis   = Vec3::UnitZ();                                                      // [-] unit
    double RadiusMajor = 0.0;                                                           // [m]
    double RadiusMinor = 0.0;                                                           // [m]
    double HalfAngle   = 0.0;                                                           // [rad] cone

    //---------------------------------------------- construction ----------------------------------------------
    [[nodiscard]] static Deliver<NurbsSurface> Build(int DegreeU, int DegreeV, int CountU, int CountV, std::vector<Vec4> Poles,
                                                     std::vector<double> KnotsU, std::vector<double> KnotsV) noexcept;
    [[nodiscard]] static Deliver<NurbsSurface> Plane(Vec3 Origin, Vec3 AxisU, Vec3 AxisV, double LengthU, double LengthV) noexcept;
    [[nodiscard]] static Deliver<NurbsSurface> Sphere(Vec3 Centre, double Radius) noexcept;
    [[nodiscard]] static Deliver<NurbsSurface> Cylinder(Vec3 FootCentre, Vec3 Axis, double Radius, double Height) noexcept;
    [[nodiscard]] static Deliver<NurbsSurface> Cone(Vec3 FootCentre, Vec3 Axis, double RadiusFoot, double RadiusTop, double Height) noexcept;
    [[nodiscard]] static Deliver<NurbsSurface> Torus(Vec3 Centre, Vec3 Axis, double RadiusMajor, double RadiusMinor) noexcept;
    [[nodiscard]] static Deliver<NurbsSurface> Patch(int DegreeU, int DegreeV, int CountU, int CountV, const std::vector<Vec3>& Points) noexcept;
    [[nodiscard]] static Deliver<NurbsSurface> Extrusion(const NurbsCurve& Profile, Vec3 Direction, double Length) noexcept;
    [[nodiscard]] static Deliver<NurbsSurface> Revolution(const NurbsCurve& Profile, Vec3 AxisOrigin, Vec3 AxisDirection, double Angle) noexcept;
    [[nodiscard]] static Deliver<NurbsSurface> Ruled(const NurbsCurve& A, const NurbsCurve& B) noexcept;
    [[nodiscard]] static Deliver<NurbsSurface> Loft(const std::vector<NurbsCurve>& Sections, int DegreeV = 3) noexcept;

    //---------------------------------------------- queries ----------------------------------------------
    [[nodiscard]] const Vec4& Pole(int I, int J) const noexcept { return Poles[static_cast<size_t>(I) * CountV + J]; }
    [[nodiscard]] Vec4& Pole(int I, int J) noexcept { return Poles[static_cast<size_t>(I) * CountV + J]; }
    [[nodiscard]] double DomainStartU() const noexcept { return KnotsU[DegreeU]; }
    [[nodiscard]] double DomainEndU() const noexcept { return KnotsU[CountU]; }
    [[nodiscard]] double DomainStartV() const noexcept { return KnotsV[DegreeV]; }
    [[nodiscard]] double DomainEndV() const noexcept { return KnotsV[CountV]; }
    [[nodiscard]] bool   ClosedU(double Tolerance = ScalarCriteria::MergeTolerance) const noexcept;
    [[nodiscard]] bool   ClosedV(double Tolerance = ScalarCriteria::MergeTolerance) const noexcept;
    [[nodiscard]] bool   Rational() const noexcept;
    [[nodiscard]] Box3   Bounds() const noexcept;
    [[nodiscard]] Refusal Validate() const noexcept;

    [[nodiscard]] Vec4 SampleHomogeneous(double U, double V) const noexcept;
    [[nodiscard]] Vec3 Sample(double U, double V) const noexcept;
    void               Derivatives(double U, double V, Vec3& Point, Vec3& DerivativeU, Vec3& DerivativeV) const noexcept;
    [[nodiscard]] Vec3 Normal(double U, double V) const noexcept;                       // unit; degenerate poles handled by nudging
    [[nodiscard]] NurbsCurve IsoCurveU(double U) const noexcept;                        // curve in V at fixed U
    [[nodiscard]] NurbsCurve IsoCurveV(double V) const noexcept;                        // curve in U at fixed V

    // Closest point (U, V) to P: coarse grid seed + Newton on the two-equation system (Piegl & Tiller 6.1).
    void ClosestParameter(Vec3 P, double& U, double& V, double* DistanceOut = nullptr) const noexcept;

    //---------------------------------------------- editing ----------------------------------------------
    [[nodiscard]] NurbsSurface InsertKnotU(double U, int Multiplicity = 1) const noexcept;
    [[nodiscard]] NurbsSurface InsertKnotV(double V, int Multiplicity = 1) const noexcept;
    [[nodiscard]] std::pair<NurbsSurface, NurbsSurface> SplitU(double U) const noexcept;
    [[nodiscard]] std::pair<NurbsSurface, NurbsSurface> SplitV(double V) const noexcept;
    [[nodiscard]] NurbsSurface Transformed(const Mat4& M) const noexcept;
    [[nodiscard]] NurbsSurface Reversed() const noexcept;                               // swap U/V → flips normal
    [[nodiscard]] NurbsSurface Transposed() const noexcept { return Reversed(); }

    //---------------------------------------------- tessellation ----------------------------------------------
    // Curvature-adaptive grid: per-span subdivision counts from the second-difference of the control net, then a
    //    uniform grid inside each span. Emits positions, normals, (u,v) and CCW triangles seen from +Normal.
    struct Tessellation
    {
        std::vector<Vec3>     Positions;                                                // [m]
        std::vector<Vec3>     Normals;                                                  // [-]
        std::vector<Vec2>     Parameters;                                               // [-] (u, v)
        std::vector<uint32_t> Triangles;                                                // [-] index triples
        int                   ColumnCount = 0;                                          // [-] samples along U
        int                   RowCount = 0;                                             // [-] samples along V
    };
    [[nodiscard]] Tessellation Tessellate(double ChordTolerance = ScalarCriteria::ChordTolerance, int MinimumPerSpan = 2, int MaximumPerSpan = 32) const noexcept;

private:
    [[nodiscard]] static NurbsSurface Skin(const std::vector<NurbsCurve>& Rows, int DegreeV, const std::vector<double>& KnotsV) noexcept;
    [[nodiscard]] int SpanSubdivision(const std::vector<double>& Knots, int Degree, int Count, bool AlongU, double ChordTolerance, int Minimum, int Maximum, int SpanIndex) const noexcept;
};

} // namespace Frontier
