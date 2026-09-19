//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Verification/KernelVerification.cpp — Phase 1 proofs: vectors, exact conics, de Boor, refinement, surfaces
//============================================================================================================================================

#include "Kernel/SurfaceSpecification.h"
#include "Kernel/BlendSolver.h"
#include "VerificationPanel.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <random>

using namespace Frontier;

namespace
{

// Bernstein evaluation of a Bézier — the independent oracle for de Boor on single-span curves.
Vec3 BernsteinOracle(const std::vector<Vec4>& Poles, double T) noexcept
{
    int N = static_cast<int>(Poles.size()) - 1;
    Vec4 Sum{ 0, 0, 0, 0 };
    for (int I = 0; I <= N; ++I)
    {
        double Binomial = 1.0;
        for (int K = 1; K <= I; ++K) Binomial = Binomial * (N - I + K) / K;
        Sum += Poles[I] * (Binomial * std::pow(T, I) * std::pow(1.0 - T, N - I));
    }
    return Sum.Divide();
}

double MaxRadiusError(const NurbsCurve& C, Vec3 Centre, double Radius, int Samples) noexcept
{
    double Worst = 0.0;
    for (int I = 0; I <= Samples; ++I)
    {
        double T = ScalarCriteria::Lerp(C.DomainStart(), C.DomainEnd(), static_cast<double>(I) / Samples);
        Worst = std::max(Worst, std::fabs(C.Sample(T).Distance(Centre) - Radius));
    }
    return Worst;
}

} // namespace

int main()
{
    VerificationPanel Panel("SolidArc · Phase 1 · Kernel Verification — VectorSpecification · CurveSpecification · SurfaceSpecification");

    //------------------------------------------------------------------ asymmetric endpoint support classifier
    Panel.Section("BlendSolver · asymmetric endpoint support classifier");
    {
        EndpointSupport Low{ { 0, 0, 0 }, { 0, 0, 1 }, 2.0 };
        EndpointSupport High{ { 0, 0, 8 }, { 0, 0, -1 }, 1.25 };
        std::string Refusal;
        Panel.Expect("Asymmetric parallel endpoint supports are accepted", BlendSolver::ValidateAsymmetricEndpointPair(Low, High, 0.1, Refusal));
        Panel.Expect("Equal endpoint radii are refused", !BlendSolver::ValidateAsymmetricEndpointPair(Low, EndpointSupport{ High.Centre, High.Normal, 2.0 }, 0.1, Refusal));
        Panel.Expect("Non-parallel endpoint normals are refused", !BlendSolver::ValidateAsymmetricEndpointPair(Low, EndpointSupport{ High.Centre, { 1, 0, 0 }, 1.25 }, 0.1, Refusal));
        Panel.Expect("Consumed endpoint ligament is refused", !BlendSolver::ValidateAsymmetricEndpointPair(Low, EndpointSupport{ { 3, 0, 0 }, High.Normal, 1.25 }, 0.1, Refusal));
        Panel.Expect("Degenerate endpoint normals are refused", !BlendSolver::ValidateAsymmetricEndpointPair(Low, EndpointSupport{ High.Centre, {}, 1.25 }, 0.1, Refusal));
        VariableRadiusSurface Surface{ Low.Centre, High.Centre - Low.Centre, { 1, 0, 0 }, 8.0, { 2.0, 1.25 } };
        Panel.Expect("Variable-radius surface reaches both endpoint radii", Surface.Evaluate(0.0, 0.0).Distance(Low.Centre) == 2.0 && Surface.Evaluate(1.0, 0.0).Distance(High.Centre) == 1.25);
        Panel.Expect("Variable-radius surface tangent is finite", Surface.TangentAlong(ScalarCriteria::HalfPi).Length() > 0.0);
        Panel.Expect("Variable-radius surface normal is finite", Surface.Normal(ScalarCriteria::HalfPi).Length() > 0.0);
        Panel.Expect("Linear radius law has zero meridional curvature", Surface.MeridionalCurvature() == 0.0);
        Panel.Expect("Variable-radius surface has finite circumferential curvature", std::isfinite(Surface.CircumferentialCurvature(0.5)));
        Panel.Expect("Variable-radius surface curvature stays within bound", BlendSolver::ValidateVariableSurfaceCurvature(Surface, 1.0, Refusal));
        Panel.Expect("Variable-radius surface rejects a tight curvature bound", !BlendSolver::ValidateVariableSurfaceCurvature(Surface, 0.1, Refusal));
        Panel.Expect("Variable-radius surface G1 endpoints align", BlendSolver::ValidateVariableSurfaceG1(Surface, Surface.Normal(ScalarCriteria::HalfPi), Surface.Normal(ScalarCriteria::HalfPi), ScalarCriteria::HalfPi, Refusal));
        Panel.Expect("Variable-radius surface rejects a non-G1 support", !BlendSolver::ValidateVariableSurfaceG1(Surface, { 1, 0, 0 }, Surface.Normal(ScalarCriteria::HalfPi), ScalarCriteria::HalfPi, Refusal));
        AsymmetricEndpointChain Chain{ { Low, High, EndpointSupport{ { 0, 0, 16 }, High.Normal, 0.75, ScalarCriteria::HalfPi } } };
        Panel.Expect("Collinear asymmetric endpoint chain is accepted", BlendSolver::ValidateAsymmetricEndpointChain(Chain, 0.1, Refusal));
        Chain.Supports[2].Centre = { 1, 0, 16 };
        Panel.Expect("Bending endpoint chain is refused", !BlendSolver::ValidateAsymmetricEndpointChain(Chain, 0.1, Refusal));
        Panel.Expect("G1 endpoint normals are accepted", BlendSolver::ValidateG1EndpointMatch({ 0, 0, 1 }, { 0, 0, -1 }, Refusal));
        Panel.Expect("Non-G1 endpoint normals are refused", !BlendSolver::ValidateG1EndpointMatch({ 1, 0, 0 }, { 0, 0, 1 }, Refusal));
        AsymmetricBlendSpecification Taper{ Low, EndpointSupport{ High.Centre, High.Normal, 1.25, ScalarCriteria::HalfPi }, AsymmetricSupportKind::TaperedFrustum, 0.1, 0.0 };
        Taper.Low.EndpointAngle = ScalarCriteria::HalfPi;
        Panel.Expect("Tapered asymmetric specification is accepted", BlendSolver::ValidateAsymmetricSpecification(Taper, Refusal));
        Deliver<BrepBody> Frustum = BlendSolver::ReconstructAsymmetricFrustum(Taper);
        Panel.Expect("Asymmetric tapered frustum reconstructs", Frustum && Frustum.Payload.Validate().Solid());
        if (Frustum)
        {
            const BodyReport Report = Frustum.Payload.Validate();
            Panel.Expect("Asymmetric frustum is closed and manifold", Report.Closed && Report.Manifold && Report.Oriented);
            Panel.Expect("Asymmetric frustum has one hull", Report.Hulls == 1);
            Panel.Expect("Asymmetric frustum has two endpoint circular rims", Report.Faces >= 3 && Report.Edges >= 3);
        }
        AsymmetricBlendSpecification Transformed = Taper;
        Transformed.Low.Centre = { 4, -3, 2 };
        Transformed.High.Centre = { 4, 5, 10 };
        Transformed.Low.Normal = { 0, 1, 1 };
        Transformed.High.Normal = { 0, -1, -1 };
        Panel.Expect("Translated oblique asymmetric frustum reconstructs", BlendSolver::ReconstructAsymmetricFrustum(Transformed));
        Panel.Within("Asymmetric frustum analytic volume", Frustum ? Frustum.Payload.Validate().Volume : 0.0,
                     ScalarCriteria::Pi * 8.0 / 3.0 * (4.0 + 2.5 + 1.5625), 1e-6);
        Panel.Expect("Non-tapered modes refuse frustum reconstruction", !BlendSolver::ReconstructAsymmetricFrustum(AsymmetricBlendSpecification{ Low, Taper.High, AsymmetricSupportKind::VariableRadiusRoll, 0.1, 0.25 }));
        Taper.Kind = AsymmetricSupportKind::UnequalRadialCaps;
        Panel.Expect("Unequal radial caps reconstruct through the bounded support route", BlendSolver::ReconstructAsymmetricSupport(Taper));
        Taper.Kind = AsymmetricSupportKind::PartialEndpointChain;
        Panel.Expect("Partial endpoint chain remains explicitly refused", !BlendSolver::ReconstructAsymmetricSupport(Taper));
        Taper.Kind = AsymmetricSupportKind::VariableRadiusRoll;
        Panel.Expect("Variable-radius roll requires a blend radius", !BlendSolver::ValidateAsymmetricSpecification(Taper, Refusal));
        Taper.BlendRadius = 0.25;
        Taper.RadiusLaw = { Taper.Low.Radius, Taper.High.Radius };
        Panel.Expect("Variable-radius law is positive", Taper.RadiusLaw.Positive());
        Panel.Expect("Variable-radius law interpolates endpoints", Taper.RadiusLaw.Evaluate(0.0) == Taper.Low.Radius && Taper.RadiusLaw.Evaluate(1.0) == Taper.High.Radius);
        Panel.Expect("Variable-radius law decreases monotonically", Taper.RadiusLaw.Decreasing());
        Panel.Expect("Variable-radius law swept volume is positive", Taper.RadiusLaw.SweptVolume(8.0) > 0.0);
        Panel.Expect("Variable-radius roll accepts a matching linear law", BlendSolver::ValidateAsymmetricSpecification(Taper, Refusal));
        Deliver<VariableRadiusSurface> BuiltSurface = BlendSolver::BuildVariableRadiusSurface(Taper);
        Panel.Expect("Variable-radius surface builds from specification", BuiltSurface && BuiltSurface.Payload.Length > 0.0);
        Deliver<BrepBody> Ruled = BlendSolver::ReconstructVariableRadiusRuledSolid(Taper);
        Panel.Expect("Variable-radius ruled solid reconstructs", Ruled && Ruled.Payload.Validate().Solid());
        Taper.Kind = AsymmetricSupportKind::EqualRadiusAsymmetricPlanes;
        Panel.Expect("Equal-radius plane mode refuses unequal radii", !BlendSolver::ValidateAsymmetricSpecification(Taper, Refusal));
        Taper.Low.Radius = Taper.High.Radius;
        Panel.Expect("Equal-radius plane mode accepts equal radii", BlendSolver::ValidateAsymmetricSpecification(Taper, Refusal));
        Panel.Expect("Equal-radius asymmetric planes reconstruct", BlendSolver::ReconstructAsymmetricSupport(Taper));
    }

    //------------------------------------------------------------------ scalar tolerance policy
    Panel.Section("ScalarCriteria · centralized volume acceptance");
    {
        Panel.Expect("Volume gate accepts exact value", ScalarCriteria::WithinVolumeTolerance(12.0, 12.0));
        Panel.Expect("Volume gate accepts its scaled boundary", ScalarCriteria::WithinVolumeTolerance(12.0 + ScalarCriteria::VolumeTolerance * 12.0, 12.0));
        Panel.Expect("Volume gate refuses beyond scaled boundary", !ScalarCriteria::WithinVolumeTolerance(12.0 + ScalarCriteria::VolumeTolerance * 12.1, 12.0));
        Panel.Expect("Volume gate has absolute floor for small solids", ScalarCriteria::WithinVolumeTolerance(0.0, 0.25 * ScalarCriteria::VolumeTolerance));
        Panel.Expect("Generic scaled gate preserves strict geometry checks", ScalarCriteria::WithinScaledTolerance(2.0 + 2e-8, 2.0, 1e-8));
        Panel.Expect("Generic scaled gate rejects beyond its bound", !ScalarCriteria::WithinScaledTolerance(2.0 + 2.1e-8, 2.0, 1e-8));
        Panel.Expect("Sweep gate accepts its boundary", ScalarCriteria::WithinAngularTolerance(ScalarCriteria::Pi + ScalarCriteria::SweepTolerance, ScalarCriteria::Pi));
        Panel.Expect("Sweep gate rejects beyond boundary", !ScalarCriteria::WithinAngularTolerance(ScalarCriteria::Pi + 1.1 * ScalarCriteria::SweepTolerance, ScalarCriteria::Pi));
        Panel.Expect("Circular gate accepts its boundary", ScalarCriteria::WithinCircularTolerance(2.0 + ScalarCriteria::CircularTolerance, 2.0));
        Panel.Expect("Circular gate rejects beyond boundary", !ScalarCriteria::WithinCircularTolerance(2.0 + 1.1 * ScalarCriteria::CircularTolerance, 2.0));
        Panel.Expect("Curve tolerance is tighter than merge tolerance", ScalarCriteria::CurveTolerance < ScalarCriteria::MergeTolerance);
        Panel.Expect("Scaled position tolerance is tighter than merge tolerance", ScalarCriteria::ScaledPositionTolerance < ScalarCriteria::MergeTolerance);
        Panel.Expect("Direction tolerance is distinct from angular policy", ScalarCriteria::DirectionTolerance > ScalarCriteria::AngularTolerance);
        Panel.Expect("Distance tolerance is positive", ScalarCriteria::DistanceTolerance > 0.0);
    }

    //------------------------------------------------------------------ vectors & matrices
    Panel.Section("VectorSpecification");
    {
        Mat4 M = Mat4::Translation({ 1, 2, 3 }) * Mat4::Rotation(Vec3::UnitZ(), ScalarCriteria::Radians(37.0)) * Mat4::Scaling({ 2, 2, 2 });
        Mat4 I = M * M.Inverse();
        double Worst = 0.0;
        for (int R = 0; R < 4; ++R) for (int C = 0; C < 4; ++C) Worst = std::max(Worst, std::fabs(I.At(R, C) - (R == C ? 1.0 : 0.0)));
        Panel.Within("Mat4 · Inverse → identity (TRS)", Worst, 1e-12);

        Quat Q = Quat::AxisAngle(Vec3::UnitZ(), ScalarCriteria::HalfPi);
        Panel.Within("Quat rotate X by 90° about Z → +Y", Q.Rotate(Vec3::UnitX()).Distance(Vec3::UnitY()), 1e-15);
        Panel.Within("Mat4::Rotation(Quat) agrees with Quat::Rotate", Mat4::Rotation(Q).TransformDirection({ 0.3, -0.7, 0.2 }).Distance(Q.Rotate({ 0.3, -0.7, 0.2 })), 1e-15);

        Workplane W = Workplane::FromNormal({ 1, 1, 1 }, { 1, 1, 1 });
        Panel.Within("Workplane::FromNormal is orthonormal", std::fabs(W.AxisX.Dot(W.AxisY)) + std::fabs(W.AxisX.Length() - 1) + std::fabs(W.AxisY.Length() - 1), 1e-15);
        Vec2 Local{ 0.7, -0.4 };
        Panel.Within("Workplane ToWorld∘ToLocal round trip", W.ToLocal(W.ToWorld(Local)).Distance(Local), 1e-15);

        Ray R{ { 0, 0, 5 }, { 0, 0, -1 } };
        double T = 0;
        Panel.Expect("Ray ∩ plane z=0 hits", R.Intersect(Plane::FromPointNormal({}, Vec3::UnitZ()), T) && ScalarCriteria::Coincident(T, 5.0));
        Panel.Equal("Ray closest parameter on skew line", Ray{ { 0, 0, 1 }, { 0, 1, 0 } }.ClosestParameterOnLine({ 0, 0, 0 }, { 1, 0, 0 }), 0.0, 1e-15);
    }

    //------------------------------------------------------------------ exact conics
    Panel.Section("CurveSpecification · exact rational conics");
    {
        Deliver<NurbsCurve> Circle = NurbsCurve::Circle({ 2, -1, 0.5 }, { 0, 0, 1 }, 3.0);
        Panel.Expect("Circle constructs", static_cast<bool>(Circle));
        Panel.Within("Circle radius error over 2000 samples", MaxRadiusError(Circle.Payload, { 2, -1, 0.5 }, 3.0, 2000), 1e-12);
        Panel.Expect("Circle is classified Circle & rational", Circle.Payload.Classification == CurveClassification::Circle && Circle.Payload.Rational());
        Panel.Expect("Circle is closed", Circle.Payload.Closed());
        Panel.Equal("Circle length = 2πr", Circle.Payload.Length(), ScalarCriteria::TwoPi * 3.0, 1e-9);
        Panel.Equal("Circle curvature = 1/r", Circle.Payload.Curvature(0.3141), 1.0 / 3.0, 1e-9);
        Panel.Note("poles=%d degree=%d knots=%zu", Circle.Payload.PoleCount(), Circle.Payload.Degree, Circle.Payload.Knots.size());

        Deliver<NurbsCurve> Arc = NurbsCurve::Arc({}, { 0, 1, 0 }, 1.5, ScalarCriteria::Radians(20), ScalarCriteria::Radians(200));
        Panel.Within("Arc (200°, tilted plane) radius error", MaxRadiusError(Arc.Payload, {}, 1.5, 1000), 1e-12);
        Panel.Equal("Arc length = r·θ", Arc.Payload.Length(), 1.5 * ScalarCriteria::Radians(200), 1e-9);

        Deliver<NurbsCurve> Three = NurbsCurve::ArcThreePoints({ 1, 0, 0 }, { 0, 1, 0 }, { -1, 0, 0 });
        Panel.Expect("Arc through 3 points constructs", static_cast<bool>(Three));
        Panel.Within("Arc through 3 points passes through the middle point", Three.Payload.Sample(Three.Payload.ClosestParameter({ 0, 1, 0 })).Distance({ 0, 1, 0 }), 1e-12);
        Panel.Equal("Arc through 3 points sweep = 180°", Three.Payload.Length(), ScalarCriteria::Pi, 1e-9);

        Deliver<NurbsCurve> Ellipse = NurbsCurve::Ellipse({}, Vec3::UnitZ(), Vec3::UnitX(), 4.0, 2.0);
        double Worst = 0.0;
        for (int I = 0; I <= 1000; ++I)
        {
            Vec3 P = Ellipse.Payload.Sample(I / 1000.0);
            Worst = std::max(Worst, std::fabs(P.X * P.X / 16.0 + P.Y * P.Y / 4.0 - 1.0));
        }
        Panel.Within("Ellipse implicit residual x²/a²+y²/b²−1", Worst, 1e-12);

        Workplane XY = Workplane::XY();
        Deliver<NurbsCurve> Slot = NurbsCurve::Slot(XY, { -2, 0 }, { 2, 0 }, 1.0);
        Panel.Expect("Slot constructs & closes", Slot && Slot.Payload.Closed());
        Panel.Equal("Slot length = 2·4 + 2π", Slot.Payload.Length(), 8.0 + ScalarCriteria::TwoPi, 1e-8);
        Deliver<NurbsCurve> Rounded = NurbsCurve::Rectangle(XY, { 0, 0 }, { 6, 4 }, 1.0);
        Panel.Expect("Rounded rectangle constructs & closes", Rounded && Rounded.Payload.Closed());
        Panel.Equal("Rounded rectangle perimeter", Rounded.Payload.Length(), 2 * (4.0 + 2.0) + ScalarCriteria::TwoPi, 1e-8);
        Deliver<NurbsCurve> Hex = NurbsCurve::Polygon(XY, { 0, 0 }, 1.0, 6, 0.0, true);
        Panel.Equal("Hexagon (inscribed r=1) perimeter = 6", Hex.Payload.Length(), 6.0, 1e-12);
        Deliver<NurbsCurve> Rect = NurbsCurve::Rectangle(XY, { 0, 0 }, { 3, 2 });
        Panel.Equal("Sharp rectangle perimeter = 10", Rect.Payload.Length(), 10.0, 1e-12);
        Plane P;
        Panel.Expect("Slot is planar", Slot.Payload.Planar(P));
    }

    //------------------------------------------------------------------ de Boor vs Bernstein
    Panel.Section("CurveSpecification · de Boor vs Bernstein, derivatives, refinement invariance");
    {
        std::vector<Vec3> CP{ { 0, 0, 0 }, { 1, 2, 0 }, { 3, 3, 1 }, { 4, 0, 2 }, { 6, 1, 0 } };
        Deliver<NurbsCurve> Bez = NurbsCurve::Bezier(CP);
        double Worst = 0.0;
        for (int I = 0; I <= 500; ++I) Worst = std::max(Worst, Bez.Payload.Sample(I / 500.0).Distance(BernsteinOracle(Bez.Payload.Poles, I / 500.0)));
        Panel.Within("Quartic Bézier: de Boor vs Bernstein", Worst, 1e-14);

        // Rational Bézier (weights) too.
        NurbsCurve RBez = Bez.Payload;
        RBez.Poles[1] = Vec4::Weighted(CP[1], 2.5); RBez.Poles[3] = Vec4::Weighted(CP[3], 0.4);
        Worst = 0.0;
        for (int I = 0; I <= 500; ++I) Worst = std::max(Worst, RBez.Sample(I / 500.0).Distance(BernsteinOracle(RBez.Poles, I / 500.0)));
        Panel.Within("Rational quartic Bézier: de Boor vs Bernstein", Worst, 1e-14);

        Deliver<NurbsCurve> Spline = NurbsCurve::ControlPoints(3, { { 0, 0, 0 }, { 1, 3, 0 }, { 2, -1, 1 }, { 4, 2, 0 }, { 5, 0, 2 }, { 7, 1, 1 }, { 8, -2, 0 } }, false);
        Panel.Expect("Cubic B-spline (7 CPs) constructs", static_cast<bool>(Spline));

        // Derivative vs central difference.
        Worst = 0.0;
        for (int I = 1; I < 100; ++I)
        {
            double T = (I + 0.37) / 100.0, H1 = 1e-6, H2 = 1e-4;     // off-knot; H2 larger because ε/H² round-off dominates C″
            Vec3 D[4]; Spline.Payload.Derivatives(T, 2, D);
            Vec3 Numeric1 = (Spline.Payload.Sample(T + H1) - Spline.Payload.Sample(T - H1)) / (2 * H1);
            Vec3 Numeric2 = (Spline.Payload.Sample(T + H2) - Spline.Payload.Sample(T) * 2.0 + Spline.Payload.Sample(T - H2)) / (H2 * H2);
            Worst = std::max(Worst, std::max(D[1].Distance(Numeric1), D[2].Distance(Numeric2)));
        }
        Panel.Within("Analytic C′, C″ vs finite differences", Worst, 1e-5);

        // Knot insertion, Bézier decomposition, degree elevation, split, reverse all leave the geometry unchanged.
        NurbsCurve Refined = Spline.Payload.Refined({ 0.1, 0.35, 0.35, 0.8 });
        NurbsCurve Elevated = Spline.Payload.Elevated(5);
        std::vector<NurbsCurve> Pieces = Spline.Payload.BezierSegments();
        auto Halves = Spline.Payload.Split(0.42);
        NurbsCurve Back = Spline.Payload.Reversed();
        double WR = 0, WE = 0, WB = 0, WS = 0, WV = 0;
        for (int I = 0; I <= 400; ++I)
        {
            double T = I / 400.0;
            Vec3 P = Spline.Payload.Sample(T);
            WR = std::max(WR, Refined.Sample(T).Distance(P));
            WE = std::max(WE, Elevated.Sample(T).Distance(P));
            WS = std::max(WS, (T <= 0.42 ? Halves.first : Halves.second).Sample(T).Distance(P));
            WV = std::max(WV, Back.Sample(1.0 - T).Distance(P));
            for (const NurbsCurve& Piece : Pieces)
                if (T >= Piece.DomainStart() && T <= Piece.DomainEnd()) WB = std::max(WB, Piece.Sample(T).Distance(P));
        }
        Panel.Within("Knot refinement is geometry-invariant", WR, 1e-13);
        Panel.Within("Degree elevation 3→5 is geometry-invariant", WE, 1e-13);
        Panel.Within("Bézier decomposition is geometry-invariant", WB, 1e-13);
        Panel.Within("Split at t=0.42 is geometry-invariant", WS, 1e-13);
        Panel.Within("Reverse is geometry-invariant", WV, 1e-13);
        Panel.Note("refined poles=%d  elevated degree=%d poles=%d  bezier pieces=%zu", Refined.PoleCount(), Elevated.Degree, Elevated.PoleCount(), Pieces.size());

        // Circle survives elevation & splitting exactly (rational path).
        NurbsCurve Circle = NurbsCurve::Circle({}, Vec3::UnitZ(), 2.0).Payload;
        Panel.Within("Circle after degree elevation 2→4 stays exact", MaxRadiusError(Circle.Elevated(4), {}, 2.0, 1000), 1e-12);
        Panel.Within("Circle after split at t=0.3 stays exact (right half)", MaxRadiusError(Circle.Split(0.3).second, {}, 2.0, 1000), 1e-12);

        // Interpolation passes through its points.
        std::vector<Vec3> Through{ { 0, 0, 0 }, { 1, 1, 0 }, { 2, -1, 0.5 }, { 3.5, 0.5, 0 }, { 5, 0, 1 }, { 6, 2, 0 } };
        Deliver<NurbsCurve> Interp = NurbsCurve::Interpolate(Through, 3);
        Panel.Expect("Global interpolation solves", static_cast<bool>(Interp));
        Worst = 0.0;
        for (Vec3 Q : Through) { double D = 0; (void)Interp.Payload.ClosestParameter(Q, &D); Worst = std::max(Worst, D); }
        Panel.Within("Interpolating spline passes through all 6 points", Worst, 1e-9);

        // Join two lines → polyline, continuity preserved.
        Deliver<NurbsCurve> J = NurbsCurve::Join(NurbsCurve::Line({ 0, 0, 0 }, { 1, 0, 0 }).Payload, NurbsCurve::Line({ 1, 0, 0 }, { 1, 1, 0 }).Payload);
        Panel.Expect("Join L-shaped lines", J && J.Payload.Classification == CurveClassification::Polyline);
        Panel.Equal("Joined polyline length = 2", J.Payload.Length(), 2.0, 1e-12);
        Panel.Expect("Join refuses a gap (Refusal::OpenWire)", !NurbsCurve::Join(NurbsCurve::Line({ 0, 0, 0 }, { 1, 0, 0 }).Payload, NurbsCurve::Line({ 2, 0, 0 }, { 3, 0, 0 }).Payload) );
    }

    //------------------------------------------------------------------ closest point
    Panel.Section("CurveSpecification · closest point (Newton) & tessellation");
    {
        NurbsCurve Circle = NurbsCurve::Circle({}, Vec3::UnitZ(), 2.0).Payload;
        std::mt19937 Rng(7);
        std::uniform_real_distribution<double> U(-5, 5);
        double Worst = 0.0;
        for (int I = 0; I < 200; ++I)
        {
            Vec3 P{ U(Rng), U(Rng), U(Rng) * 0.2 };
            double D = 0; (void)Circle.ClosestParameter(P, &D);
            double Exact = std::sqrt(std::pow(std::hypot(P.X, P.Y) - 2.0, 2) + P.Z * P.Z);
            Worst = std::max(Worst, std::fabs(D - Exact));
        }
        Panel.Within("Circle closest-point distance vs analytic (200 random)", Worst, 1e-9);

        NurbsCurve Spline = NurbsCurve::ControlPoints(3, { { 0, 0, 0 }, { 1, 3, 0 }, { 2, -1, 1 }, { 4, 2, 0 }, { 5, 0, 2 }, { 7, 1, 1 } }, false).Payload;
        Worst = 0.0;
        for (int I = 0; I < 100; ++I)
        {
            double T = (I + 0.5) / 100.0;
            Vec3 On = Spline.Sample(T);
            double Found = Spline.ClosestParameter(On);
            Worst = std::max(Worst, Spline.Sample(Found).Distance(On));
        }
        Panel.Within("Spline closest-point recovers on-curve points", Worst, 1e-9);

        std::vector<Vec3> Points; std::vector<double> Params;
        Circle.Tessellate(Points, &Params, 1e-4);
        double Sagitta = 0.0;
        for (size_t I = 0; I + 1 < Points.size(); ++I)
        {
            Vec3 Mid = (Points[I] + Points[I + 1]) * 0.5;
            Sagitta = std::max(Sagitta, 2.0 - Mid.Length());
        }
        Panel.Within("Circle tessellation sagitta ≤ 1e-4", Sagitta, 1e-4);
        Panel.Expect("Tessellation parameters are monotone & aligned", Params.size() == Points.size() && std::is_sorted(Params.begin(), Params.end()));
        Panel.Note("circle r=2 tessellated into %zu segments at 1e-4 chord tolerance", Points.size() - 1);
    }

    //------------------------------------------------------------------ surfaces
    Panel.Section("SurfaceSpecification · exact quadrics, outward normals, derivatives, closest point");
    {
        Deliver<NurbsSurface> Sphere = NurbsSurface::Sphere({ 1, 2, 3 }, 2.5);
        Panel.Expect("NURBS sphere constructs", static_cast<bool>(Sphere));
        const NurbsSurface& S = Sphere.Payload;
        Panel.Note("sphere: degreeU=%d degreeV=%d poles=%d×%d rational=%s", S.DegreeU, S.DegreeV, S.CountU, S.CountV, S.Rational() ? "yes" : "no");
        double WorstR = 0.0, WorstN = 0.0, WorstOut = 1.0;
        for (int I = 0; I <= 60; ++I)
            for (int J = 1; J < 60; ++J)
            {
                double U = ScalarCriteria::Lerp(S.DomainStartU(), S.DomainEndU(), I / 60.0);
                double V = ScalarCriteria::Lerp(S.DomainStartV(), S.DomainEndV(), J / 60.0);
                Vec3 P = S.Sample(U, V);
                Vec3 Radial = (P - Vec3{ 1, 2, 3 });
                WorstR = std::max(WorstR, std::fabs(Radial.Length() - 2.5));
                Vec3 N = S.Normal(U, V);
                WorstN = std::max(WorstN, N.Distance(Radial.Normalised()));
                WorstOut = std::min(WorstOut, N.Dot(Radial.Normalised()));
            }
        Panel.Within("Sphere radius error (61×59 samples)", WorstR, 1e-12);
        Panel.Within("Sphere analytic normal vs radial direction", WorstN, 1e-9);
        Panel.Expect("Sphere normals point OUTWARD everywhere (∂u×∂v rule)", WorstOut > 0.999);
        Vec3 NorthPole = S.Normal(0.37, S.DomainEndV());
        Panel.Within("Sphere normal at the degenerate north pole = +Z", NorthPole.Distance(Vec3::UnitZ()), 1e-3);

        Deliver<NurbsSurface> Cyl = NurbsSurface::Cylinder({ 0, 0, -1 }, Vec3::UnitZ(), 1.5, 3.0);
        double WorstC = 0.0, WorstCN = 1.0;
        for (int I = 0; I <= 50; ++I) for (int J = 0; J <= 10; ++J)
        {
            double U = ScalarCriteria::Lerp(Cyl.Payload.DomainStartU(), Cyl.Payload.DomainEndU(), I / 50.0);
            double V = ScalarCriteria::Lerp(Cyl.Payload.DomainStartV(), Cyl.Payload.DomainEndV(), J / 10.0);
            Vec3 P = Cyl.Payload.Sample(U, V);
            WorstC = std::max(WorstC, std::fabs(std::hypot(P.X, P.Y) - 1.5));
            WorstCN = std::min(WorstCN, Cyl.Payload.Normal(U, V).Dot(Vec3{ P.X, P.Y, 0 }.Normalised()));
        }
        Panel.Within("Cylinder radius error", WorstC, 1e-12);
        Panel.Expect("Cylinder normals outward", WorstCN > 0.999);

        Deliver<NurbsSurface> Tor = NurbsSurface::Torus({}, Vec3::UnitZ(), 3.0, 1.0);
        double WorstT = 0.0, WorstTN = 1.0;
        for (int I = 0; I <= 40; ++I) for (int J = 0; J <= 40; ++J)
        {
            double U = ScalarCriteria::Lerp(Tor.Payload.DomainStartU(), Tor.Payload.DomainEndU(), I / 40.0);
            double V = ScalarCriteria::Lerp(Tor.Payload.DomainStartV(), Tor.Payload.DomainEndV(), J / 40.0);
            Vec3 P = Tor.Payload.Sample(U, V);
            double Ring = std::hypot(P.X, P.Y);
            WorstT = std::max(WorstT, std::fabs(std::hypot(Ring - 3.0, P.Z) - 1.0));
            Vec3 TubeCentre = Vec3{ P.X, P.Y, 0 }.Normalised() * 3.0;
            WorstTN = std::min(WorstTN, Tor.Payload.Normal(U, V).Dot((P - TubeCentre).Normalised()));
        }
        Panel.Within("Torus implicit residual", WorstT, 1e-12);
        Panel.Expect("Torus normals outward", WorstTN > 0.999);

        Deliver<NurbsSurface> Cone = NurbsSurface::Cone({}, Vec3::UnitZ(), 2.0, 0.0, 4.0);
        Panel.Expect("Cone (to apex) constructs", static_cast<bool>(Cone));
        Vec3 ApexNormal = Cone.Payload.Normal(0.2, Cone.Payload.DomainEndV());
        Panel.Expect("Cone normal near apex is finite & outward-ish", std::isfinite(ApexNormal.X) && ApexNormal.Z > 0.0);

        // Derivatives vs finite differences on a free-form patch.
        std::vector<Vec3> Net;
        for (int I = 0; I < 5; ++I) for (int J = 0; J < 4; ++J) Net.emplace_back(I * 1.0, J * 1.2, std::sin(I * 0.9) * std::cos(J * 1.1));
        Deliver<NurbsSurface> Patch = NurbsSurface::Patch(3, 2, 5, 4, Net);
        Panel.Expect("B-spline patch 5×4 (3×2) constructs", static_cast<bool>(Patch));
        double WorstD = 0.0;
        for (int I = 1; I < 20; ++I) for (int J = 1; J < 20; ++J)
        {
            double U = (I + 0.37) / 20.0, V = (J + 0.37) / 20.0, H = 1e-6;   // off-knot (degree-2 V direction is only C¹ at knots)
            Vec3 P, DU, DV; Patch.Payload.Derivatives(U, V, P, DU, DV);
            Vec3 NU = (Patch.Payload.Sample(U + H, V) - Patch.Payload.Sample(U - H, V)) / (2 * H);
            Vec3 NV = (Patch.Payload.Sample(U, V + H) - Patch.Payload.Sample(U, V - H)) / (2 * H);
            WorstD = std::max(WorstD, std::max(DU.Distance(NU), DV.Distance(NV)));
        }
        Panel.Within("Patch ∂u, ∂v vs finite differences", WorstD, 1e-7);

        // Closest point on sphere.
        double CU = 0, CV = 0, CD = 0;
        S.ClosestParameter({ 10, -4, 6 }, CU, CV, &CD);
        Panel.Equal("Sphere closest-point distance", CD, Vec3{ 10, -4, 6 }.Distance({ 1, 2, 3 }) - 2.5, 1e-9);

        // Iso-curves lie on the surface; split is invariant.
        NurbsCurve Iso = S.IsoCurveV(0.5 * (S.DomainStartV() + S.DomainEndV()));
        Panel.Within("Sphere equator iso-curve radius", MaxRadiusError(Iso, { 1, 2, 3 }, 2.5, 500), 1e-12);
        auto SplitS = S.SplitU(0.3);
        Panel.Within("Sphere SplitU right half still on the sphere", std::fabs(SplitS.second.Sample(0.6, 0.4).Distance({ 1, 2, 3 }) - 2.5), 1e-12);

        // Tessellation: every vertex on the sphere, all triangles CCW seen from outside.
        NurbsSurface::Tessellation Tessellation = S.Tessellate(1e-3);
        double WorstV = 0.0; int Inward = 0;
        for (Vec3 P : Tessellation.Positions) WorstV = std::max(WorstV, std::fabs(P.Distance({ 1, 2, 3 }) - 2.5));
        for (size_t T = 0; T < Tessellation.Triangles.size(); T += 3)
        {
            Vec3 A = Tessellation.Positions[Tessellation.Triangles[T]], B = Tessellation.Positions[Tessellation.Triangles[T + 1]], C = Tessellation.Positions[Tessellation.Triangles[T + 2]];
            Vec3 FaceNormal = (B - A).Cross(C - A);
            if (FaceNormal.Dot((A + B + C) / 3.0 - Vec3{ 1, 2, 3 }) < 0.0) ++Inward;
        }
        Panel.Within("Sphere tessellation vertices on surface", WorstV, 1e-12);
        Panel.Expect("Sphere tessellation: zero inward-facing triangles", Inward == 0);
        Panel.Note("sphere tessellation: %d×%d samples, %zu triangles, inward=%d", Tessellation.ColumnCount, Tessellation.RowCount, Tessellation.Triangles.size() / 3, Inward);

        // Extrusion & revolution of a profile.
        NurbsCurve Profile = NurbsCurve::Circle({}, Vec3::UnitZ(), 1.0).Payload;
        Deliver<NurbsSurface> Ext = NurbsSurface::Extrusion(Profile, Vec3::UnitZ(), 2.0);
        Panel.Expect("Extrusion of circle constructs", static_cast<bool>(Ext));
        Panel.Expect("Extruded cylinder normal outward at (u,v)", Ext.Payload.Normal(0.13, 1.0).Dot(Vec3{ Ext.Payload.Sample(0.13, 1.0).X, Ext.Payload.Sample(0.13, 1.0).Y, 0 }) > 0.999);
        NurbsCurve Vase = NurbsCurve::ControlPoints(3, { { 1, 0, 0 }, { 1.5, 0, 0.5 }, { 0.6, 0, 1.2 }, { 1.2, 0, 2.0 }, { 0.8, 0, 2.6 } }, false).Payload;
        Deliver<NurbsSurface> Rev = NurbsSurface::Revolution(Vase, {}, Vec3::UnitZ(), ScalarCriteria::TwoPi);
        Panel.Expect("Revolution of a cubic profile constructs", static_cast<bool>(Rev));
        Panel.Expect("Revolved surface closed in U", Rev.Payload.ClosedU());
        Deliver<NurbsSurface> Lofted = NurbsSurface::Loft({ NurbsCurve::Circle({ 0, 0, 0 }, Vec3::UnitZ(), 1.0).Payload, NurbsCurve::Circle({ 0, 0, 1 }, Vec3::UnitZ(), 1.5).Payload, NurbsCurve::Circle({ 0, 0, 2 }, Vec3::UnitZ(), 0.8).Payload }, 2);
        Panel.Expect("Loft through 3 circles constructs", static_cast<bool>(Lofted));
        double LoftWorst = 0.0;
        for (int I = 0; I < 24; ++I)
        {
            double A = ScalarCriteria::TwoPi * I / 24.0;
            double LU = 0, LV = 0, LD = 0;
            Lofted.Payload.ClosestParameter({ 1.5 * std::cos(A), 1.5 * std::sin(A), 1.0 }, LU, LV, &LD);
            LoftWorst = std::max(LoftWorst, LD);
        }
        Panel.Within("Loft passes through the middle section (r=1.5 at z=1, 24 pts)", LoftWorst, 1e-9);
    }

    //------------------------------------------------------------------ refusals
    Panel.Section("Refusal paths (fail-fast, no exceptions)");
    {
        Panel.Expect("Line with coincident endpoints → DegenerateInput", NurbsCurve::Line({ 1, 1, 1 }, { 1, 1, 1 }).Denial.Reason == RefusalReason::DegenerateInput);
        Panel.Expect("Circle radius 0 → DegenerateInput", NurbsCurve::Circle({}, Vec3::UnitZ(), 0.0).Denial.Reason == RefusalReason::DegenerateInput);
        Panel.Expect("Collinear 3-point arc → DegenerateInput", NurbsCurve::ArcThreePoints({ 0, 0, 0 }, { 1, 0, 0 }, { 2, 0, 0 }).Denial.Reason == RefusalReason::DegenerateInput);
        Panel.Expect("Cubic with 3 CPs → InvalidDegree", NurbsCurve::ControlPoints(3, { {}, { 1, 0, 0 }, { 2, 0, 0 } }, false).Denial.Reason == RefusalReason::InvalidDegree);
        Panel.Expect("Bad knot vector → InvalidKnotVector", NurbsCurve::Build(1, { Vec4({}, 1), Vec4({ 1, 0, 0 }, 1) }, { 0, 1, 0, 1 }).Denial.Reason == RefusalReason::InvalidKnotVector);
        Panel.Expect("Polygon with 2 sides → DegenerateInput", NurbsCurve::Polygon(Workplane::XY(), {}, 1, 2, 0, true).Denial.Reason == RefusalReason::DegenerateInput);
    }

    return Panel.Conclude();
}
