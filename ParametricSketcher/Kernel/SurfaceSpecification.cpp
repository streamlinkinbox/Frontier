//============================================================================================================================================
// 📦 ParametricSketcher/Kernel/SurfaceSpecification.cpp — NURBS surface algorithms
//============================================================================================================================================

#include "SurfaceSpecification.h"
#include <algorithm>
#include <cmath>

namespace Frontier
{

const char* Describe(SurfaceClassification Classification) noexcept
{
    switch (Classification)
    {
        case SurfaceClassification::Freeform:   return "Freeform";
        case SurfaceClassification::Plane:      return "Plane";
        case SurfaceClassification::Cylinder:   return "Cylinder";
        case SurfaceClassification::Cone:       return "Cone";
        case SurfaceClassification::Sphere:     return "Sphere";
        case SurfaceClassification::Torus:      return "Torus";
        case SurfaceClassification::Extrusion:  return "Extrusion";
        case SurfaceClassification::Revolution: return "Revolution";
        case SurfaceClassification::Ruled:      return "Ruled";
        case SurfaceClassification::Loft:       return "Loft";
    }
    return "Unknown";
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<NurbsSurface> NurbsSurface::Build(int DegreeU, int DegreeV, int CountU, int CountV, std::vector<Vec4> Poles,
                                          std::vector<double> KnotsU, std::vector<double> KnotsV) noexcept
{
    NurbsSurface S;
    S.DegreeU = DegreeU; S.DegreeV = DegreeV; S.CountU = CountU; S.CountV = CountV;
    S.Poles = std::move(Poles); S.KnotsU = std::move(KnotsU); S.KnotsV = std::move(KnotsV);
    if (Refusal Denial = S.Validate()) return Deliver<NurbsSurface>::Reject(Denial.Reason, Denial.Detail);
    return Deliver<NurbsSurface>::Accept(std::move(S));
}

Refusal NurbsSurface::Validate() const noexcept
{
    if (DegreeU < 1 || DegreeV < 1) return Refusal::Reject(RefusalReason::InvalidDegree, "surface degrees must be ≥ 1");
    if (CountU <= DegreeU || CountV <= DegreeV) return Refusal::Reject(RefusalReason::InvalidDegree, "need more than Degree poles in each direction");
    if (Poles.size() != static_cast<size_t>(CountU) * CountV) return Refusal::Reject(RefusalReason::DegenerateInput, "pole count ≠ CountU × CountV");
    if (KnotsU.size() != static_cast<size_t>(CountU + DegreeU + 1)) return Refusal::Reject(RefusalReason::InvalidKnotVector, "U knot count");
    if (KnotsV.size() != static_cast<size_t>(CountV + DegreeV + 1)) return Refusal::Reject(RefusalReason::InvalidKnotVector, "V knot count");
    for (size_t I = 1; I < KnotsU.size(); ++I) if (KnotsU[I] < KnotsU[I - 1]) return Refusal::Reject(RefusalReason::InvalidKnotVector, "U knots decreasing");
    for (size_t I = 1; I < KnotsV.size(); ++I) if (KnotsV[I] < KnotsV[I - 1]) return Refusal::Reject(RefusalReason::InvalidKnotVector, "V knots decreasing");
    for (const Vec4& P : Poles) if (P.W <= 0.0) return Refusal::Reject(RefusalReason::DegenerateInput, "weights must be positive");
    return Refusal::Accept();
}

// The recurring pattern for quadrics: take a profile curve (in U … here we call the around-the-axis direction U) and a
//    generator, form the tensor product. Every rational quadric here is Circle ⊗ Profile.
NurbsSurface NurbsSurface::Skin(const std::vector<NurbsCurve>& Rows, int DegreeV, const std::vector<double>& KnotsV) noexcept
{
    // Rows are iso-V curves (all identical in degree/knots along U); Rows[J] is at V-pole index J.
    NurbsSurface S;
    S.DegreeU = Rows.front().Degree;
    S.DegreeV = DegreeV;
    S.CountU = Rows.front().PoleCount();
    S.CountV = static_cast<int>(Rows.size());
    S.KnotsU = Rows.front().Knots;
    S.KnotsV = KnotsV;
    S.Poles.resize(static_cast<size_t>(S.CountU) * S.CountV);
    for (int I = 0; I < S.CountU; ++I)
        for (int J = 0; J < S.CountV; ++J)
            S.Pole(I, J) = Rows[J].Poles[I];
    return S;
}

Deliver<NurbsSurface> NurbsSurface::Plane(Vec3 Origin, Vec3 AxisU, Vec3 AxisV, double LengthU, double LengthV) noexcept
{
    Vec3 U = AxisU.Normalised(), V = AxisV.Normalised();
    if (U.Parallel(V)) return Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput, "plane axes are parallel");
    std::vector<Vec4> Poles{
        Vec4(Origin, 1.0), Vec4(Origin + V * LengthV, 1.0),
        Vec4(Origin + U * LengthU, 1.0), Vec4(Origin + U * LengthU + V * LengthV, 1.0) };
    Deliver<NurbsSurface> R = Build(1, 1, 2, 2, std::move(Poles), { 0, 0, LengthU, LengthU }, { 0, 0, LengthV, LengthV });
    if (R) { R.Payload.Classification = SurfaceClassification::Plane; R.Payload.Origin = Origin; R.Payload.Axis = U.Cross(V).Normalised(); }
    return R;
}

// Revolve a planar profile curve (lying in the plane spanned by Axis and a radial direction) about the axis by Angle.
//    Piegl & Tiller A8.1 in spirit: the around-axis direction is an exact rational circle per profile pole.
Deliver<NurbsSurface> NurbsSurface::Revolution(const NurbsCurve& Profile, Vec3 AxisOrigin, Vec3 AxisDirection, double Angle) noexcept
{
    if (std::fabs(Angle) <= ScalarCriteria::AngularTolerance) return Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput, "zero revolve angle");
    Vec3 Axis = AxisDirection.Normalised();
    if (Axis.LengthSquared() < 0.5) return Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput, "zero revolve axis");
    Angle = ScalarCriteria::Clamp(Angle, -ScalarCriteria::TwoPi, ScalarCriteria::TwoPi);

    // For each profile pole build the circle it traces; all circles share one knot vector, so we can skin directly.
    //    Poles ON the axis produce a degenerate circle (all poles equal) — legitimate, that is how spheres get their poles.
    Workplane Axes = Workplane::FromNormal(AxisOrigin, Axis);
    std::vector<NurbsCurve> Rows;
    for (const Vec4& Homogeneous : Profile.Poles)
    {
        Vec3 P = Homogeneous.Divide();
        Vec2 Local = Axes.ToLocal(P);
        double Radius = Local.Length();
        double Height = (P - AxisOrigin).Dot(Axis);
        double Start = Radius > ScalarCriteria::KernelTolerance ? Local.Angle() : 0.0;
        Vec3 Centre = AxisOrigin + Axis * Height;
        NurbsCurve Ring;
        if (Radius > ScalarCriteria::KernelTolerance)
        {
            Deliver<NurbsCurve> D = NurbsCurve::Arc(Centre, Axis, Radius, Start, Angle);
            if (!D) return Deliver<NurbsSurface>::Reject(D.Denial.Reason, D.Denial.Detail);
            Ring = std::move(D.Payload);
        }
        else
        {
            // Degenerate ring: same knot structure as a real one of this sweep, every pole at the centre.
            Deliver<NurbsCurve> D = NurbsCurve::Arc(Centre, Axis, 1.0, 0.0, Angle);
            if (!D) return Deliver<NurbsSurface>::Reject(D.Denial.Reason, D.Denial.Detail);
            Ring = std::move(D.Payload);
            for (Vec4& Q : Ring.Poles) Q = Vec4::Weighted(Centre, Q.W);
        }
        // Carry the profile weight through: the surface pole weight is w_profile · w_ring.
        for (Vec4& Q : Ring.Poles) Q = Q * Homogeneous.W;
        Rows.push_back(std::move(Ring));
    }
    NurbsSurface S = Skin(Rows, Profile.Degree, Profile.Knots);
    S.Classification = SurfaceClassification::Revolution;
    S.Origin = AxisOrigin; S.Axis = Axis;
    return Deliver<NurbsSurface>::Accept(std::move(S));
}

Deliver<NurbsSurface> NurbsSurface::Sphere(Vec3 Centre, double Radius) noexcept
{
    if (Radius <= ScalarCriteria::KernelTolerance) return Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput, "zero sphere radius");
    // Profile: half circle in the XZ plane from the south pole (0,0,−R) over +X to the north pole (0,0,+R).
    //    Its plane normal is +Y so the arc runs −90° → +90° CCW as seen from +Y: from (0,0,-R) through (R,0,0) to (0,0,R).
    //    Revolving CCW about +Z (U direction) with V going south→north gives ∂u × ∂v pointing outward.
    Deliver<NurbsCurve> Profile = NurbsCurve::Arc(Centre, Vec3::UnitY() * -1.0, Radius, -ScalarCriteria::HalfPi, ScalarCriteria::Pi);
    if (!Profile) return Deliver<NurbsSurface>::Reject(Profile.Denial.Reason, Profile.Denial.Detail);
    Deliver<NurbsSurface> S = Revolution(Profile.Payload, Centre, Vec3::UnitZ(), ScalarCriteria::TwoPi);
    if (S)
    {
        S.Payload.Classification = SurfaceClassification::Sphere;
        S.Payload.Origin = Centre; S.Payload.Axis = Vec3::UnitZ();
        S.Payload.RadiusMajor = S.Payload.RadiusMinor = Radius;
    }
    return S;
}

Deliver<NurbsSurface> NurbsSurface::Cylinder(Vec3 FootCentre, Vec3 AxisDirection, double Radius, double Height) noexcept
{
    if (Radius <= ScalarCriteria::KernelTolerance || std::fabs(Height) <= ScalarCriteria::KernelTolerance) return Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput, "zero cylinder radius or height");
    Vec3 Axis = AxisDirection.Normalised();
    Workplane Axes = Workplane::FromNormal(FootCentre, Axis);
    // Generator line at +X radius, going up: revolving CCW about the axis → outward normal.
    Deliver<NurbsCurve> Generator = NurbsCurve::Line(FootCentre + Axes.AxisX * Radius, FootCentre + Axes.AxisX * Radius + Axis * Height);
    if (!Generator) return Deliver<NurbsSurface>::Reject(Generator.Denial.Reason, Generator.Denial.Detail);
    Deliver<NurbsSurface> S = Revolution(Generator.Payload, FootCentre, Axis, ScalarCriteria::TwoPi);
    if (S)
    {
        S.Payload.Classification = SurfaceClassification::Cylinder;
        S.Payload.Origin = FootCentre; S.Payload.Axis = Axis;
        S.Payload.RadiusMajor = S.Payload.RadiusMinor = Radius;
    }
    return S;
}

Deliver<NurbsSurface> NurbsSurface::Cone(Vec3 FootCentre, Vec3 AxisDirection, double RadiusFoot, double RadiusTop, double Height) noexcept
{
    if (std::fabs(Height) <= ScalarCriteria::KernelTolerance) return Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput, "zero cone height");
    if (RadiusFoot < 0.0 || RadiusTop < 0.0 || (RadiusFoot <= ScalarCriteria::KernelTolerance && RadiusTop <= ScalarCriteria::KernelTolerance))
        return Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput, "cone radii");
    Vec3 Axis = AxisDirection.Normalised();
    Workplane Axes = Workplane::FromNormal(FootCentre, Axis);
    Deliver<NurbsCurve> Generator = NurbsCurve::Line(FootCentre + Axes.AxisX * RadiusFoot, FootCentre + Axes.AxisX * RadiusTop + Axis * Height);
    if (!Generator) return Deliver<NurbsSurface>::Reject(Generator.Denial.Reason, Generator.Denial.Detail);
    Deliver<NurbsSurface> S = Revolution(Generator.Payload, FootCentre, Axis, ScalarCriteria::TwoPi);
    if (S)
    {
        S.Payload.Classification = SurfaceClassification::Cone;
        S.Payload.Origin = FootCentre; S.Payload.Axis = Axis;
        S.Payload.RadiusMajor = RadiusFoot; S.Payload.RadiusMinor = RadiusTop;
        S.Payload.HalfAngle = std::atan2(RadiusFoot - RadiusTop, Height);
    }
    return S;
}

Deliver<NurbsSurface> NurbsSurface::Torus(Vec3 Centre, Vec3 AxisDirection, double RadiusMajor, double RadiusMinor) noexcept
{
    if (RadiusMinor <= ScalarCriteria::KernelTolerance || RadiusMajor <= ScalarCriteria::KernelTolerance) return Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput, "zero torus radius");
    Vec3 Axis = AxisDirection.Normalised();
    Workplane Axes = Workplane::FromNormal(Centre, Axis);
    // Tube circle in the plane spanned by radial (AxisX) and Axis, centred at major radius. Its normal must be −AxisY
    //    so that the circle runs outer-equator → top → inner → bottom, i.e. V increases "upward" on the outside, which
    //    with U revolving CCW gives an outward ∂u × ∂v.
    Vec3 TubeCentre = Centre + Axes.AxisX * RadiusMajor;
    Deliver<NurbsCurve> Tube = NurbsCurve::Arc(TubeCentre, Axes.AxisY * -1.0, RadiusMinor, 0.0, ScalarCriteria::TwoPi);
    if (!Tube) return Deliver<NurbsSurface>::Reject(Tube.Denial.Reason, Tube.Denial.Detail);
    Deliver<NurbsSurface> S = Revolution(Tube.Payload, Centre, Axis, ScalarCriteria::TwoPi);
    if (S)
    {
        S.Payload.Classification = SurfaceClassification::Torus;
        S.Payload.Origin = Centre; S.Payload.Axis = Axis;
        S.Payload.RadiusMajor = RadiusMajor; S.Payload.RadiusMinor = RadiusMinor;
    }
    return S;
}

static std::vector<double> ClampedUniform(int Count, int Degree) noexcept
{
    std::vector<double> K(Count + Degree + 1);
    int Interior = Count - Degree - 1;
    for (int I = 0; I <= Degree; ++I) { K[I] = 0.0; K[Count + I] = 1.0; }
    for (int I = 1; I <= Interior; ++I) K[Degree + I] = static_cast<double>(I) / (Interior + 1);
    return K;
}

Deliver<NurbsSurface> NurbsSurface::Patch(int DegreeU, int DegreeV, int CountU, int CountV, const std::vector<Vec3>& Points) noexcept
{
    if (Points.size() != static_cast<size_t>(CountU) * CountV) return Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput, "patch point count ≠ CountU × CountV");
    std::vector<Vec4> Poles; Poles.reserve(Points.size());
    for (Vec3 P : Points) Poles.emplace_back(P, 1.0);
    return Build(DegreeU, DegreeV, CountU, CountV, std::move(Poles), ClampedUniform(CountU, DegreeU), ClampedUniform(CountV, DegreeV));
}

Deliver<NurbsSurface> NurbsSurface::Extrusion(const NurbsCurve& Profile, Vec3 Direction, double Length) noexcept
{
    Vec3 D = Direction.Normalised();
    if (D.LengthSquared() < 0.5 || std::fabs(Length) <= ScalarCriteria::KernelTolerance) return Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput, "zero extrusion");
    // U = profile, V = along the direction. For a CCW profile (seen from +D) this gives ∂u × ∂v outward.
    std::vector<NurbsCurve> Rows{ Profile, Profile.Transformed(Mat4::Translation(D * Length)) };
    NurbsSurface S = Skin(Rows, 1, { 0.0, 0.0, std::fabs(Length), std::fabs(Length) });
    S.Classification = SurfaceClassification::Extrusion;
    S.Origin = Profile.StartPoint(); S.Axis = D;
    return Deliver<NurbsSurface>::Accept(std::move(S));
}

Deliver<NurbsSurface> NurbsSurface::Ruled(const NurbsCurve& A, const NurbsCurve& B) noexcept
{
    // Bring both rails to a common degree and knot vector (merge), then skin linearly in V.
    int Degree = std::max(A.Degree, B.Degree);
    NurbsCurve RA = (A.Degree < Degree ? A.Elevated(Degree) : A).Reparameterised(0.0, 1.0);
    NurbsCurve RB = (B.Degree < Degree ? B.Elevated(Degree) : B).Reparameterised(0.0, 1.0);
    std::vector<double> Union;
    for (double K : RA.Knots) Union.push_back(K);
    for (double K : RB.Knots) Union.push_back(K);
    std::sort(Union.begin(), Union.end());
    // Insert missing knots into each so the vectors match (multiplicity-aware).
    auto Multiplicity = [](const std::vector<double>& Knots, double T) { int M = 0; for (double K : Knots) if (ScalarCriteria::Coincident(K, T, ScalarCriteria::ParametricEpsilon)) ++M; return M; };
    std::vector<double> Distinct;
    for (double K : Union) if (Distinct.empty() || !ScalarCriteria::Coincident(Distinct.back(), K, ScalarCriteria::ParametricEpsilon)) Distinct.push_back(K);
    for (double T : Distinct)
    {
        int Target = std::max(Multiplicity(RA.Knots, T), Multiplicity(RB.Knots, T));
        int Need = Target - Multiplicity(RA.Knots, T); if (Need > 0) RA = RA.InsertKnot(T, Need);
        Need = Target - Multiplicity(RB.Knots, T); if (Need > 0) RB = RB.InsertKnot(T, Need);
    }
    if (RA.PoleCount() != RB.PoleCount()) return Deliver<NurbsSurface>::Reject(RefusalReason::NoConvergence, "ruled rails could not be made compatible");
    NurbsSurface S = Skin({ RA, RB }, 1, { 0, 0, 1, 1 });
    S.Classification = SurfaceClassification::Ruled;
    return Deliver<NurbsSurface>::Accept(std::move(S));
}

// Loft by skinning: make sections compatible, then interpolate the poles across sections in V (A10.3 simplified: the
//    V interpolation uses the same global scheme as NurbsCurve::Interpolate, one column at a time).
Deliver<NurbsSurface> NurbsSurface::Loft(const std::vector<NurbsCurve>& Sections, int DegreeV) noexcept
{
    int Count = static_cast<int>(Sections.size());
    if (Count < 2) return Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput, "loft needs ≥ 2 sections");
    DegreeV = std::min(DegreeV, Count - 1);
    int Degree = 1;
    for (const NurbsCurve& C : Sections) Degree = std::max(Degree, C.Degree);
    std::vector<NurbsCurve> Rows;
    for (const NurbsCurve& C : Sections) Rows.push_back((C.Degree < Degree ? C.Elevated(Degree) : C).Reparameterised(0.0, 1.0));
    std::vector<double> Distinct;
    for (const NurbsCurve& C : Rows) for (double K : C.Knots) Distinct.push_back(K);
    std::sort(Distinct.begin(), Distinct.end());
    Distinct.erase(std::unique(Distinct.begin(), Distinct.end(), [](double A, double B) { return ScalarCriteria::Coincident(A, B, ScalarCriteria::ParametricEpsilon); }), Distinct.end());
    auto Multiplicity = [](const std::vector<double>& Knots, double T) { int M = 0; for (double K : Knots) if (ScalarCriteria::Coincident(K, T, ScalarCriteria::ParametricEpsilon)) ++M; return M; };
    for (double T : Distinct)
    {
        int Target = 0;
        for (const NurbsCurve& C : Rows) Target = std::max(Target, Multiplicity(C.Knots, T));
        for (NurbsCurve& C : Rows) { int Need = Target - Multiplicity(C.Knots, T); if (Need > 0) C = C.InsertKnot(T, Need); }
    }
    int CountU = Rows.front().PoleCount();
    for (const NurbsCurve& C : Rows) if (C.PoleCount() != CountU) return Deliver<NurbsSurface>::Reject(RefusalReason::NoConvergence, "loft sections could not be made compatible");
    if (DegreeV == 1 || Count == 2)
    {
        std::vector<double> KV = ClampedUniform(Count, 1);
        NurbsSurface S = Skin(Rows, 1, KV);
        S.Classification = SurfaceClassification::Loft;
        return Deliver<NurbsSurface>::Accept(std::move(S));
    }
    // Interpolate each column of poles through the sections in HOMOGENEOUS space (weights ride along, so rational
    //    sections stay exact) with ONE shared V parameterisation: chord lengths averaged over all columns (A10.3).
    std::vector<double> ParametersV(Count, 0.0);
    for (int I = 0; I < CountU; ++I)
    {
        double Total = 0.0;
        std::vector<double> Local(Count, 0.0);
        for (int J = 1; J < Count; ++J) { Local[J] = Rows[J].Poles[I].Divide().Distance(Rows[J - 1].Poles[I].Divide()); Total += Local[J]; }
        if (Total <= ScalarCriteria::KernelTolerance) continue;
        double Running = 0.0;
        for (int J = 1; J < Count; ++J) { Running += Local[J] / Total; ParametersV[J] += Running / CountU; }
    }
    ParametersV[Count - 1] = 1.0;
    std::vector<NurbsCurve> Columns;
    std::vector<double> KnotsV;
    for (int I = 0; I < CountU; ++I)
    {
        std::vector<Vec4> Through;
        for (const NurbsCurve& C : Rows) Through.push_back(C.Poles[I]);
        Deliver<NurbsCurve> Column = NurbsCurve::InterpolateHomogeneous(Through, DegreeV, &ParametersV);
        if (!Column) return Deliver<NurbsSurface>::Reject(Column.Denial.Reason, Column.Denial.Detail);
        if (Columns.empty()) KnotsV = Column.Payload.Knots;
        Columns.push_back(std::move(Column.Payload));
    }
    NurbsSurface S;
    S.DegreeU = Degree; S.DegreeV = DegreeV; S.CountU = CountU; S.CountV = Columns.front().PoleCount();
    S.KnotsU = Rows.front().Knots; S.KnotsV = KnotsV;
    S.Poles.resize(static_cast<size_t>(S.CountU) * S.CountV);
    for (int I = 0; I < S.CountU; ++I) for (int J = 0; J < S.CountV; ++J) S.Pole(I, J) = Columns[I].Poles[J];
    S.Classification = SurfaceClassification::Loft;
    return Deliver<NurbsSurface>::Accept(std::move(S));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  QUERIES
//------------------------------------------------------------------------------------------------------------------------

bool NurbsSurface::ClosedU(double Tolerance) const noexcept
{
    for (int J = 0; J < CountV; ++J) if (!Pole(0, J).Divide().Coincident(Pole(CountU - 1, J).Divide(), Tolerance)) return false;
    return true;
}

bool NurbsSurface::ClosedV(double Tolerance) const noexcept
{
    for (int I = 0; I < CountU; ++I) if (!Pole(I, 0).Divide().Coincident(Pole(I, CountV - 1).Divide(), Tolerance)) return false;
    return true;
}

bool NurbsSurface::Rational() const noexcept
{
    for (const Vec4& P : Poles) if (!ScalarCriteria::Coincident(P.W, 1.0, 1e-12)) return true;
    return false;
}

Box3 NurbsSurface::Bounds() const noexcept
{
    Box3 B;
    for (const Vec4& P : Poles) B.Include(P.Divide());
    return B;
}

NurbsCurve NurbsSurface::IsoCurveV(double V) const noexcept
{
    // Curve in U at fixed V: evaluate the V-basis and blend each U-column of poles.
    NurbsCurve AxisCurve; AxisCurve.Degree = DegreeV; AxisCurve.Knots = KnotsV; AxisCurve.Poles.assign(CountV, Vec4{});
    V = ScalarCriteria::Clamp(V, DomainStartV(), DomainEndV());
    int Span = AxisCurve.FindSpan(V);
    double Basis[16]; AxisCurve.BasisFunctions(Span, V, Basis);
    NurbsCurve C; C.Degree = DegreeU; C.Knots = KnotsU; C.Poles.assign(CountU, Vec4{ 0, 0, 0, 0 });
    for (int I = 0; I < CountU; ++I)
        for (int K = 0; K <= DegreeV; ++K) C.Poles[I] += Pole(I, Span - DegreeV + K) * Basis[K];
    return C;
}

NurbsCurve NurbsSurface::IsoCurveU(double U) const noexcept
{
    NurbsCurve AxisCurve; AxisCurve.Degree = DegreeU; AxisCurve.Knots = KnotsU; AxisCurve.Poles.assign(CountU, Vec4{});
    U = ScalarCriteria::Clamp(U, DomainStartU(), DomainEndU());
    int Span = AxisCurve.FindSpan(U);
    double Basis[16]; AxisCurve.BasisFunctions(Span, U, Basis);
    NurbsCurve C; C.Degree = DegreeV; C.Knots = KnotsV; C.Poles.assign(CountV, Vec4{ 0, 0, 0, 0 });
    for (int J = 0; J < CountV; ++J)
        for (int K = 0; K <= DegreeU; ++K) C.Poles[J] += Pole(Span - DegreeU + K, J) * Basis[K];
    return C;
}

Vec4 NurbsSurface::SampleHomogeneous(double U, double V) const noexcept
{
    return IsoCurveV(V).SampleHomogeneous(U);
}

Vec3 NurbsSurface::Sample(double U, double V) const noexcept
{
    return SampleHomogeneous(U, V).Divide();
}

// A3.6 + A4.4 (first derivatives only).
void NurbsSurface::Derivatives(double U, double V, Vec3& Point, Vec3& DerivativeU, Vec3& DerivativeV) const noexcept
{
    U = ScalarCriteria::Clamp(U, DomainStartU(), DomainEndU());
    V = ScalarCriteria::Clamp(V, DomainStartV(), DomainEndV());
    NurbsCurve HU; HU.Degree = DegreeU; HU.Knots = KnotsU; HU.Poles.assign(CountU, Vec4{});
    NurbsCurve HV; HV.Degree = DegreeV; HV.Knots = KnotsV; HV.Poles.assign(CountV, Vec4{});
    int SpanU = HU.FindSpan(U), SpanV = HV.FindSpan(V);
    double BU[2 * 16], BV[2 * 16];
    HU.BasisDerivatives(SpanU, U, 1, BU);
    HV.BasisDerivatives(SpanV, V, 1, BV);
    Vec4 S{ 0, 0, 0, 0 }, SU{ 0, 0, 0, 0 }, SV{ 0, 0, 0, 0 };
    for (int K = 0; K <= DegreeU; ++K)
        for (int L = 0; L <= DegreeV; ++L)
        {
            const Vec4& P = Pole(SpanU - DegreeU + K, SpanV - DegreeV + L);
            S  += P * (BU[K] * BV[L]);
            SU += P * (BU[(DegreeU + 1) + K] * BV[L]);
            SV += P * (BU[K] * BV[(DegreeV + 1) + L]);
        }
    Point = S.Divide();
    DerivativeU = (SU.XYZ() - Point * SU.W) / S.W;
    DerivativeV = (SV.XYZ() - Point * SV.W) / S.W;
}

Vec3 NurbsSurface::Normal(double U, double V) const noexcept
{
    Vec3 P, DU, DV;
    Derivatives(U, V, P, DU, DV);
    Vec3 N = DU.Cross(DV);
    if (N.LengthSquared() > 1e-24) return N.Normalised();
    // Degenerate (a pole of a sphere, apex of a cone): step slightly inward along the collapsed direction and retry,
    //    which converges to the true limit normal for every quadric we build.
    double StepU = (DomainEndU() - DomainStartU()) * 1e-4, StepV = (DomainEndV() - DomainStartV()) * 1e-4;
    double U2 = U, V2 = V;
    if (DU.LengthSquared() < 1e-24) V2 += (V < (DomainStartV() + DomainEndV()) * 0.5) ? StepV : -StepV;
    if (DV.LengthSquared() < 1e-24) U2 += (U < (DomainStartU() + DomainEndU()) * 0.5) ? StepU : -StepU;
    Derivatives(U2, V2, P, DU, DV);
    N = DU.Cross(DV);
    if (N.LengthSquared() > 1e-24) return N.Normalised();
    Derivatives(U2 + StepU, V2 + StepV, P, DU, DV);
    return DU.Cross(DV).Normalised();
}

// Piegl & Tiller 6.1: two-equation Newton with the four stopping criteria.
void NurbsSurface::ClosestParameter(Vec3 Target, double& U, double& V, double* DistanceOut) const noexcept
{
    double U0 = DomainStartU(), U1 = DomainEndU(), V0 = DomainStartV(), V1 = DomainEndV();
    int NU = std::max(8, CountU * 4), NV = std::max(8, CountV * 4);
    double BestD = ScalarCriteria::Infinity;
    for (int I = 0; I <= NU; ++I)
        for (int J = 0; J <= NV; ++J)
        {
            double SU = ScalarCriteria::Lerp(U0, U1, static_cast<double>(I) / NU), SV = ScalarCriteria::Lerp(V0, V1, static_cast<double>(J) / NV);
            double D = Sample(SU, SV).Distance(Target);
            if (D < BestD) { BestD = D; U = SU; V = SV; }
        }
    bool WrapU = ClosedU(ScalarCriteria::KernelTolerance), WrapV = ClosedV(ScalarCriteria::KernelTolerance);
    for (int Iteration = 0; Iteration < 40; ++Iteration)
    {
        Vec3 P, DU, DV;
        Derivatives(U, V, P, DU, DV);
        Vec3 R = P - Target;
        double Distance = R.Length();
        if (Distance <= ScalarCriteria::KernelTolerance) break;
        double FU = DU.Dot(R), FV = DV.Dot(R);
        double LU = DU.Length(), LV = DV.Length();
        bool PerpU = LU < 1e-14 || std::fabs(FU) / (LU * Distance) <= ScalarCriteria::AngularTolerance;
        bool PerpV = LV < 1e-14 || std::fabs(FV) / (LV * Distance) <= ScalarCriteria::AngularTolerance;
        if (PerpU && PerpV) break;
        // Second derivatives by central difference of first derivatives (adequate for the quadrics & patches here).
        double HU = (U1 - U0) * 1e-5, HV = (V1 - V0) * 1e-5;
        Vec3 P2, DUu, DVu, DUv, DVv;
        Derivatives(std::min(U + HU, U1), V, P2, DUu, DVu);
        Vec3 P3, DUm, DVm;
        Derivatives(std::max(U - HU, U0), V, P3, DUm, DVm);
        Vec3 SUU = (DUu - DUm) / (2 * HU), SVU = (DVu - DVm) / (2 * HU);
        Derivatives(U, std::min(V + HV, V1), P2, DUv, DVv);
        Derivatives(U, std::max(V - HV, V0), P3, DUm, DVm);
        Vec3 SUV = (DUv - DUm) / (2 * HV), SVV = (DVv - DVm) / (2 * HV);
        double A = LU * LU + R.Dot(SUU), B = DU.Dot(DV) + R.Dot(SUV), C = DU.Dot(DV) + R.Dot(SVU), D = LV * LV + R.Dot(SVV);
        double Det = A * D - B * C;
        if (std::fabs(Det) < 1e-300) break;
        double DeltaU = (-FU * D + FV * B) / Det;
        double DeltaV = (-A * FV + C * FU) / Det;
        double NextU = U + DeltaU, NextV = V + DeltaV;
        if (WrapU) { double S = U1 - U0; while (NextU < U0) NextU += S; while (NextU > U1) NextU -= S; } else NextU = ScalarCriteria::Clamp(NextU, U0, U1);
        if (WrapV) { double S = V1 - V0; while (NextV < V0) NextV += S; while (NextV > V1) NextV -= S; } else NextV = ScalarCriteria::Clamp(NextV, V0, V1);
        if ((DU * (NextU - U) + DV * (NextV - V)).Length() <= ScalarCriteria::KernelTolerance) { U = NextU; V = NextV; break; }
        U = NextU; V = NextV;
    }
    if (DistanceOut) *DistanceOut = Sample(U, V).Distance(Target);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  EDITING
//------------------------------------------------------------------------------------------------------------------------

NurbsSurface NurbsSurface::InsertKnotU(double U, int Multiplicity) const noexcept
{
    // Insert into every V-column's U-curve; they all share KnotsU so the results line up.
    std::vector<NurbsCurve> Columns;
    for (int J = 0; J < CountV; ++J)
    {
        NurbsCurve C; C.Degree = DegreeU; C.Knots = KnotsU;
        for (int I = 0; I < CountU; ++I) C.Poles.push_back(Pole(I, J));
        Columns.push_back(C.InsertKnot(U, Multiplicity));
    }
    NurbsSurface S = *this;
    S.CountU = Columns.front().PoleCount(); S.KnotsU = Columns.front().Knots;
    S.Poles.resize(static_cast<size_t>(S.CountU) * S.CountV);
    for (int I = 0; I < S.CountU; ++I) for (int J = 0; J < S.CountV; ++J) S.Pole(I, J) = Columns[J].Poles[I];
    return S;
}

NurbsSurface NurbsSurface::InsertKnotV(double V, int Multiplicity) const noexcept
{
    std::vector<NurbsCurve> Rows;
    for (int I = 0; I < CountU; ++I)
    {
        NurbsCurve C; C.Degree = DegreeV; C.Knots = KnotsV;
        for (int J = 0; J < CountV; ++J) C.Poles.push_back(Pole(I, J));
        Rows.push_back(C.InsertKnot(V, Multiplicity));
    }
    NurbsSurface S = *this;
    S.CountV = Rows.front().PoleCount(); S.KnotsV = Rows.front().Knots;
    S.Poles.resize(static_cast<size_t>(S.CountU) * S.CountV);
    for (int I = 0; I < S.CountU; ++I) for (int J = 0; J < S.CountV; ++J) S.Pole(I, J) = Rows[I].Poles[J];
    return S;
}

std::pair<NurbsSurface, NurbsSurface> NurbsSurface::SplitU(double U) const noexcept
{
    std::vector<NurbsCurve> Left, Right;
    for (int J = 0; J < CountV; ++J)
    {
        NurbsCurve C; C.Degree = DegreeU; C.Knots = KnotsU;
        for (int I = 0; I < CountU; ++I) C.Poles.push_back(Pole(I, J));
        auto Halves = C.Split(U);
        Left.push_back(Halves.first); Right.push_back(Halves.second);
    }
    NurbsSurface A = Skin(Left, DegreeV, KnotsV), B = Skin(Right, DegreeV, KnotsV);
    A.Classification = B.Classification = Classification;
    A.Origin = B.Origin = Origin; A.Axis = B.Axis = Axis;
    A.RadiusMajor = B.RadiusMajor = RadiusMajor; A.RadiusMinor = B.RadiusMinor = RadiusMinor; A.HalfAngle = B.HalfAngle = HalfAngle;
    return { A, B };
}

std::pair<NurbsSurface, NurbsSurface> NurbsSurface::SplitV(double V) const noexcept
{
    auto Halves = Reversed().SplitU(V);
    return { Halves.first.Reversed(), Halves.second.Reversed() };
}

NurbsSurface NurbsSurface::Transformed(const Mat4& M) const noexcept
{
    NurbsSurface S = *this;
    for (Vec4& P : S.Poles) P = Vec4::Weighted(M.TransformPoint(P.Divide()), P.W);
    S.Origin = M.TransformPoint(Origin);
    S.Axis = M.TransformDirection(Axis).Normalised();
    Vec3 SX = M.AxisX(), SY = M.AxisY(), SZ = M.AxisZ();
    bool Uniform = ScalarCriteria::Coincident(SX.LengthSquared(), SY.LengthSquared(), 1e-9) && ScalarCriteria::Coincident(SX.LengthSquared(), SZ.LengthSquared(), 1e-9);
    if (Uniform) { double F = SX.Length(); S.RadiusMajor *= F; S.RadiusMinor *= F; }
    else if (Classification != SurfaceClassification::Plane) S.Classification = SurfaceClassification::Freeform;
    return S;
}

NurbsSurface NurbsSurface::Reversed() const noexcept
{
    NurbsSurface S = *this;
    std::swap(S.DegreeU, S.DegreeV); std::swap(S.CountU, S.CountV); std::swap(S.KnotsU, S.KnotsV);
    for (int I = 0; I < CountU; ++I) for (int J = 0; J < CountV; ++J) S.Poles[static_cast<size_t>(J) * CountU + I] = Pole(I, J);
    return S;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  TESSELLATION
//------------------------------------------------------------------------------------------------------------------------

// Subdivision count for one knot span from the control net's deviation from linearity across that span. For rational
//    quadrics this yields the classic "N per 90°" behaviour scaled by radius / tolerance.
int NurbsSurface::SpanSubdivision(const std::vector<double>& Knots, int Degree, int Count, bool AlongU, double ChordTolerance, int Minimum, int Maximum, int SpanIndex) const noexcept
{
    (void)Knots; (void)Count;
    int First = SpanIndex - Degree;                                                     // first pole influencing the span
    double Deviation = 0.0;
    int Other = AlongU ? CountV : CountU;
    for (int K = 0; K < Other; ++K)
    {
        for (int I = First; I + 2 <= First + Degree; ++I)
        {
            Vec3 A = (AlongU ? Pole(I, K) : Pole(K, I)).Divide();
            Vec3 B = (AlongU ? Pole(I + 1, K) : Pole(K, I + 1)).Divide();
            Vec3 C = (AlongU ? Pole(I + 2, K) : Pole(K, I + 2)).Divide();
            Deviation = std::max(Deviation, (A - B * 2.0 + C).Length());
        }
    }
    if (Degree == 1) return Minimum;
    // A quadratic Bézier with second difference d deviates from its chord by at most d/4; halving the span quarters it.
    double Sagitta = Deviation * 0.25;
    int N = Minimum;
    while (Sagitta > ChordTolerance && N < Maximum) { Sagitta *= 0.25; N *= 2; }
    return std::min(std::max(N, Minimum), Maximum);
}

NurbsSurface::Tessellation NurbsSurface::Tessellate(double ChordTolerance, int MinimumPerSpan, int MaximumPerSpan) const noexcept
{
    Tessellation T;
    std::vector<double> SamplesU, SamplesV;
    SamplesU.push_back(DomainStartU());
    for (int I = DegreeU; I < CountU; ++I)
    {
        double A = KnotsU[I], B = KnotsU[I + 1];
        if (B - A <= ScalarCriteria::ParametricEpsilon) continue;
        int N = SpanSubdivision(KnotsU, DegreeU, CountU, true, ChordTolerance, MinimumPerSpan, MaximumPerSpan, I);
        for (int K = 1; K <= N; ++K) SamplesU.push_back(ScalarCriteria::Lerp(A, B, static_cast<double>(K) / N));
    }
    SamplesV.push_back(DomainStartV());
    for (int J = DegreeV; J < CountV; ++J)
    {
        double A = KnotsV[J], B = KnotsV[J + 1];
        if (B - A <= ScalarCriteria::ParametricEpsilon) continue;
        int N = SpanSubdivision(KnotsV, DegreeV, CountV, false, ChordTolerance, MinimumPerSpan, MaximumPerSpan, J);
        for (int K = 1; K <= N; ++K) SamplesV.push_back(ScalarCriteria::Lerp(A, B, static_cast<double>(K) / N));
    }
    T.ColumnCount = static_cast<int>(SamplesU.size());
    T.RowCount = static_cast<int>(SamplesV.size());
    T.Positions.reserve(static_cast<size_t>(T.ColumnCount) * T.RowCount);
    for (int J = 0; J < T.RowCount; ++J)
        for (int I = 0; I < T.ColumnCount; ++I)
        {
            Vec3 P, DU, DV;
            Derivatives(SamplesU[I], SamplesV[J], P, DU, DV);
            Vec3 N = DU.Cross(DV);
            T.Positions.push_back(P);
            T.Normals.push_back(N.LengthSquared() > 1e-24 ? N.Normalised() : Normal(SamplesU[I], SamplesV[J]));
            T.Parameters.emplace_back(SamplesU[I], SamplesV[J]);
        }
    for (int J = 0; J + 1 < T.RowCount; ++J)
        for (int I = 0; I + 1 < T.ColumnCount; ++I)
        {
            uint32_t A = static_cast<uint32_t>(J * T.ColumnCount + I), B = A + 1;
            uint32_t C = static_cast<uint32_t>((J + 1) * T.ColumnCount + I), D = C + 1;
            // CCW seen from +N = +(∂u × ∂v): (A, B, D) and (A, D, C). Skip zero-area triangles at degenerate poles.
            auto Push = [&](uint32_t X, uint32_t Y, uint32_t Z)
            {
                Vec3 E1 = T.Positions[Y] - T.Positions[X], E2 = T.Positions[Z] - T.Positions[X];
                if (E1.Cross(E2).LengthSquared() > 1e-30) { T.Triangles.push_back(X); T.Triangles.push_back(Y); T.Triangles.push_back(Z); }
            };
            Push(A, B, D);
            Push(A, D, C);
        }
    return T;
}

} // namespace Frontier
