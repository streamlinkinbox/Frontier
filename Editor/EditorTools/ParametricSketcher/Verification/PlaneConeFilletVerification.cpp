//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Verification/PlaneConeFilletVerification.cpp — Phase 32z unequal-radius support fillet
//============================================================================================================================================
// The first asymmetric (unequal-radius) support pair: a native conical frustum boss leaving a planar annular shoulder.
// Its foot and top circles are coaxial but unequal, so the boss wall is a cone rather than a cylinder. The exact
// rolling-ball patch is still a rational torus band — plane and coaxial cone are both surfaces of revolution — whose
// meridian spans π/2 − α for the cone half-angle α, adds the root wedge given in closed form by Pappus, and meets both
// retained supports at G1. The suite also carries the specification-level validators the route rests on.
#include "Kernel/BlendSolver.h"
#include "Kernel/IntersectionSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace Frontier;

namespace
{
[[nodiscard]] Deliver<BrepBody> TaperedBoss(Vec3 Base, Vec3 Axis, double OuterRadius, double ShoulderHeight,
                                            double FootRadius, double TopRadius, double BossHeight) noexcept
{
    Axis = Axis.Normalised();
    Workplane Axes = Workplane::FromNormal(Base, Axis);
    Vec3 ShoulderCentre = Base + Axis * ShoulderHeight;
    Deliver<NurbsSurface> Outer = NurbsSurface::Cylinder(Base, Axis, OuterRadius, ShoulderHeight);
    Deliver<NurbsCurve> ShoulderLine = NurbsCurve::Line(ShoulderCentre + Axes.AxisX * OuterRadius,
                                                        ShoulderCentre + Axes.AxisX * FootRadius);
    Deliver<NurbsSurface> Shoulder = ShoulderLine
        ? NurbsSurface::Revolution(ShoulderLine.Payload, Base, Axis, ScalarCriteria::TwoPi)
        : Deliver<NurbsSurface>::Reject(ShoulderLine.Denial.Reason, ShoulderLine.Denial.Detail);
    Deliver<NurbsSurface> Boss = NurbsSurface::Cone(ShoulderCentre, Axis, FootRadius, TopRadius, BossHeight);
    if (!Outer || !Shoulder || !Boss)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "tapered-boss support is degenerate");
    return BrepBody::Sew({ Outer.Payload, Shoulder.Payload, Boss.Payload });
}

[[nodiscard]] bool CircleFrame(const NurbsCurve& Curve, Vec3& Centre, Vec3& Normal, double& Radius) noexcept
{
    if (Curve.Classification != CurveClassification::Circle || Curve.Degree != 2 ||
        !Curve.Rational() || !Curve.Closed()) return false;
    double T0 = Curve.DomainStart(), Span = Curve.DomainEnd() - T0;
    Vec3 P0 = Curve.Sample(T0), P1 = Curve.Sample(T0 + Span * 0.25), P2 = Curve.Sample(T0 + Span * 0.5);
    Vec3 U = P1 - P0, V = P2 - P0, Cross = U.Cross(V);
    double Denominator = 2.0 * Cross.LengthSquared();
    if (Span <= ScalarCriteria::ParametricEpsilon || Denominator <= ScalarCriteria::KernelTolerance) return false;
    Centre = P0 + (Cross.Cross(U) * V.LengthSquared() + V.Cross(Cross) * U.LengthSquared()) / Denominator;
    Radius = Centre.Distance(P0); Normal = Cross.Normalised();
    return Radius > ScalarCriteria::MergeTolerance;
}

[[nodiscard]] int CircularEdgeAt(const BrepBody& Body, Vec3 Centre, Vec3 Axis, double Radius) noexcept
{
    Axis = Axis.Normalised();
    for (size_t Edge = 0; Edge < Body.Edges.size(); ++Edge)
    {
        Vec3 CandidateCentre, Normal; double CandidateRadius = 0.0;
        if (CircleFrame(Body.Edges[Edge].Curve, CandidateCentre, Normal, CandidateRadius) &&
            CandidateCentre.Distance(Centre) < 1e-8 && std::fabs(CandidateRadius - Radius) < 1e-8 &&
            std::fabs(Normal.Dot(Axis)) > 1.0 - 1e-8) return static_cast<int>(Edge);
    }
    return -1;
}

[[nodiscard]] int FaceOf(const BrepBody& Body, SurfaceClassification Classification) noexcept
{
    for (size_t Face = 0; Face < Body.Faces.size(); ++Face)
        if (Body.Faces[Face].Surface.Classification == Classification) return static_cast<int>(Face);
    return -1;
}

// Closed-form geometry of the plane–cone rolling ball, the numbers every check below is measured against.
struct ConeRoll
{
    double HalfAngle = 0.0;                                                             // [rad] tan α = (R_f − R_t) / H
    double ContactHeight = 0.0;                                                         // [m]   z_t = r (1 − sin α)
    double ContactRadius = 0.0;                                                         // [m]   ρ_t = R_f − z_t tan α
    double SpineRadius = 0.0;                                                           // [m]   ρ_c = R_f + r (1 − sin α) / cos α
    double AddedVolume = 0.0;                                                           // [m³]  2π · first moment of the meridian wedge
    double SourceVolume = 0.0;                                                          // [m³]
};

[[nodiscard]] ConeRoll AnalyticRoll(double OuterRadius, double ShoulderHeight, double FootRadius, double TopRadius,
                                    double BossHeight, double Radius) noexcept
{
    ConeRoll Roll;
    Roll.HalfAngle = std::atan2(FootRadius - TopRadius, BossHeight);
    const double SinA = std::sin(Roll.HalfAngle), CosA = std::cos(Roll.HalfAngle);
    Roll.ContactHeight = Radius * (1.0 - SinA);
    Roll.SpineRadius = FootRadius + Radius * (1.0 - SinA) / CosA;
    Roll.ContactRadius = FootRadius - Roll.ContactHeight * std::tan(Roll.HalfAngle);
    // Quadrilateral (root corner, shoulder contact, arc centre, cone contact) minus the sector the arc cuts from it.
    const double Quad[4][2] = { { FootRadius, 0.0 }, { Roll.SpineRadius, 0.0 }, { Roll.SpineRadius, Radius },
                                { Roll.ContactRadius, Roll.ContactHeight } };
    double QuadMoment = 0.0;
    for (int I = 0; I < 4; ++I)
    {
        const double* P = Quad[I]; const double* Q = Quad[(I + 1) % 4];
        QuadMoment += (P[0] + Q[0]) * (P[0] * Q[1] - Q[0] * P[1]);
    }
    QuadMoment = std::fabs(QuadMoment) / 6.0;
    const double Theta0 = ScalarCriteria::Pi + Roll.HalfAngle, Theta1 = 1.5 * ScalarCriteria::Pi;
    const double SectorMoment = Roll.SpineRadius * Radius * Radius * (Theta1 - Theta0) / 2.0 +
                                Radius * Radius * Radius * (std::sin(Theta1) - std::sin(Theta0)) / 3.0;
    Roll.AddedVolume = ScalarCriteria::TwoPi * (QuadMoment - SectorMoment);
    Roll.SourceVolume = ScalarCriteria::Pi * OuterRadius * OuterRadius * ShoulderHeight +
        ScalarCriteria::Pi * BossHeight * (FootRadius * FootRadius + FootRadius * TopRadius + TopRadius * TopRadius) / 3.0;
    return Roll;
}

struct FilletInspection
{
    bool Topology = false;
    bool AnalyticIdentity = false;
    bool Supports = false;
    double TorusResidual = ScalarCriteria::Infinity;
    double ShoulderTangentBreak = ScalarCriteria::Infinity;
    double ConeTangentBreak = ScalarCriteria::Infinity;
    double VolumeError = ScalarCriteria::Infinity;                                      // [-] total, relative to the closed form
    double WedgeError = ScalarCriteria::Infinity;                                       // [-] (rounded − source) relative to the closed-form wedge
};

// Declared acceptance. The kernel measures volume by tessellation at a 1e-4 sagitta, which leaves even a sharp source
//    body ≈ 3.8e-4 below its exact volume, so the total is gated at the kernel's own VolumeTolerance (1e-3, the gate
//    the route enforces on itself). The added material is the sharper test: (rounded − source) cancels that shared
//    floor and follows the Pappus wedge to ≈ 1.3e-3 of the wedge across every fixture below, gated with margin at 5e-3.
constexpr double VolumeLimit = 1e-3;                                                    // [-]
constexpr double WedgeLimit = 5e-3;                                                     // [-]

[[nodiscard]] FilletInspection InspectFillet(const BrepBody& Body, Vec3 Base, Vec3 Axis, double OuterRadius,
                                             double ShoulderHeight, double FootRadius, double TopRadius,
                                             double BossHeight, double Radius, double SourceVolume) noexcept
{
    FilletInspection Result;
    Axis = Axis.Normalised();
    const ConeRoll Roll = AnalyticRoll(OuterRadius, ShoulderHeight, FootRadius, TopRadius, BossHeight, Radius);
    const BodyReport Report = Body.Validate();
    Result.Topology = Report.Solid() && Report.Hulls == 1 && Report.Genus == 0 && Body.Vertices.size() == 5 &&
        Body.Edges.size() == 9 && Body.Coedges.size() == 18 && Body.Loops.size() == 6 && Body.Faces.size() == 6;
    Result.VolumeError = std::fabs(Report.Volume - (Roll.SourceVolume + Roll.AddedVolume)) / (Roll.SourceVolume + Roll.AddedVolume);
    Result.WedgeError = std::fabs((Report.Volume - SourceVolume) - Roll.AddedVolume) / Roll.AddedVolume;
    const int RollFace = FaceOf(Body, SurfaceClassification::Torus);
    if (RollFace < 0) return Result;

    const NurbsSurface& Torus = Body.Faces[RollFace].Surface;
    const Vec3 ShoulderCentre = Base + Axis * ShoulderHeight;
    const Vec3 ExpectedOrigin = ShoulderCentre + Axis * Radius;
    Result.AnalyticIdentity = Torus.Origin.Distance(ExpectedOrigin) < 1e-9 &&
        Torus.Axis.Normalised().Dot(Axis) > 1.0 - 1e-12 &&
        std::fabs(Torus.RadiusMajor - Roll.SpineRadius) < 1e-10 &&
        std::fabs(Torus.RadiusMinor - Radius) < 1e-10;

    Result.TorusResidual = 0.0;
    Result.ShoulderTangentBreak = 0.0;
    Result.ConeTangentBreak = 0.0;
    const double U0 = Torus.DomainStartU(), U1 = Torus.DomainEndU();
    const double V0 = Torus.DomainStartV(), V1 = Torus.DomainEndV();
    const double SinA = std::sin(Roll.HalfAngle), CosA = std::cos(Roll.HalfAngle);
    for (int I = 0; I < 9; ++I)
    {
        const double U = U0 + (U1 - U0) * (static_cast<double>(I) / 8.0);
        for (int J = 0; J < 5; ++J)
        {
            const double V = V0 + (V1 - V0) * (static_cast<double>(J) / 4.0);
            Vec3 Point = Torus.Sample(U, V);
            double Along = (Point - ExpectedOrigin).Dot(Axis);
            Vec3 Radial = Point - (ExpectedOrigin + Axis * Along);
            double Q = Radial.Length() - Roll.SpineRadius;
            Result.TorusResidual = std::max(Result.TorusResidual, std::fabs(Q * Q + Along * Along - Radius * Radius));
        }
        // Shoulder contact row: the roll's outward normal is the shoulder's (+Axis).
        Vec3 ShoulderNormal = Body.FaceNormal(RollFace, U, V0).Normalised();
        Result.ShoulderTangentBreak = std::max(Result.ShoulderTangentBreak, 1.0 - ShoulderNormal.Dot(Axis));
        // Cone contact row: the roll's outward normal is the cone's, cos α radial + sin α axial.
        Vec3 ConePoint = Torus.Sample(U, V1);
        double Along = (ConePoint - ExpectedOrigin).Dot(Axis);
        Vec3 ConeRadial = (ConePoint - (ExpectedOrigin + Axis * Along)).Normalised();
        Vec3 ConeNormal = (ConeRadial * CosA + Axis * SinA).Normalised();
        Vec3 RollNormal = Body.FaceNormal(RollFace, U, V1).Normalised();
        Result.ConeTangentBreak = std::max(Result.ConeTangentBreak, 1.0 - RollNormal.Dot(ConeNormal));
    }

    int Cylinders = 0, Cones = 0, Caps = 0, Shoulders = 0;
    bool OuterOkay = false, ConeOkay = false, ShoulderOkay = false;
    for (size_t Face = 0; Face < Body.Faces.size(); ++Face)
    {
        const NurbsSurface& Surface = Body.Faces[Face].Surface;
        const double SU0 = Surface.DomainStartU(), SU1 = Surface.DomainEndU();
        const double SV0 = Surface.DomainStartV(), SV1 = Surface.DomainEndV();
        if (Surface.Classification == SurfaceClassification::Cylinder)
        {
            ++Cylinders;
            Vec3 C0 = (Surface.Sample(SU0, SV0) + Surface.Sample(0.5 * (SU0 + SU1), SV0)) * 0.5;
            Vec3 C1 = (Surface.Sample(SU0, SV1) + Surface.Sample(0.5 * (SU0 + SU1), SV1)) * 0.5;
            double T0 = (C0 - Base).Dot(Axis), T1 = (C1 - Base).Dot(Axis);
            OuterOkay = std::fabs(Surface.RadiusMajor - OuterRadius) < 1e-9 &&
                std::fabs(std::min(T0, T1)) < 1e-8 && std::fabs(std::max(T0, T1) - ShoulderHeight) < 1e-8;
        }
        else if (Surface.Classification == SurfaceClassification::Cone)
        {
            ++Cones;
            // Both end rows: one at the contact circle (ρ_t, z_t), the other at the retained top rim (R_t, H).
            double LowHeight = ScalarCriteria::Infinity, HighHeight = -ScalarCriteria::Infinity;
            double LowRadius = 0.0, HighRadius = 0.0;
            for (double V : { SV0, SV1 })
            {
                Vec3 P = Surface.Sample(0.5 * (SU0 + SU1), V);
                double Height = (P - ShoulderCentre).Dot(Axis);
                double R = (P - (ShoulderCentre + Axis * Height)).Length();
                if (Height < LowHeight) { LowHeight = Height; LowRadius = R; }
                if (Height > HighHeight) { HighHeight = Height; HighRadius = R; }
            }
            ConeOkay = std::fabs(LowHeight - Roll.ContactHeight) < 1e-8 && std::fabs(LowRadius - Roll.ContactRadius) < 1e-8 &&
                std::fabs(HighHeight - BossHeight) < 1e-8 && std::fabs(HighRadius - TopRadius) < 1e-8;
        }
        else if (Surface.Classification == SurfaceClassification::Plane) ++Caps;
        else if (static_cast<int>(Face) != RollFace)
        {
            ++Shoulders;
            double MinRadius = ScalarCriteria::Infinity, MaxRadius = 0.0, PlaneError = 0.0;
            for (int I = 0; I < 5; ++I)
                for (int J = 0; J < 3; ++J)
                {
                    Vec3 Point = Surface.Sample(SU0 + (SU1 - SU0) * (static_cast<double>(I) / 4.0),
                                                SV0 + (SV1 - SV0) * (static_cast<double>(J) / 2.0));
                    double Along = (Point - ShoulderCentre).Dot(Axis);
                    double R = (Point - (ShoulderCentre + Axis * Along)).Length();
                    MinRadius = std::min(MinRadius, R); MaxRadius = std::max(MaxRadius, R);
                    PlaneError = std::max(PlaneError, std::fabs(Along));
                }
            ShoulderOkay = PlaneError < 1e-9 && std::fabs(MinRadius - Roll.SpineRadius) < 1e-8 &&
                std::fabs(MaxRadius - OuterRadius) < 1e-8;
        }
    }
    Result.Supports = Cylinders == 1 && Cones == 1 && Caps == 2 && Shoulders == 1 && OuterOkay && ConeOkay && ShoulderOkay;
    return Result;
}

// One tapered fixture rounded through the kernel, with its analytic geometry alongside.
struct RoundedFixture
{
    Vec3 Base, Axis;
    double OuterRadius, ShoulderHeight, FootRadius, TopRadius, BossHeight, Radius;
    Deliver<BrepBody> Source = Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "not built");
    Deliver<BrepBody> Rounded = Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "not built");
    int RootEdge = -1;
    FilletInspection Inspection;

    RoundedFixture(Vec3 B, Vec3 A, double Outer, double Shoulder, double Foot, double Top, double Height, double R) noexcept
        : Base(B), Axis(A.Normalised()), OuterRadius(Outer), ShoulderHeight(Shoulder), FootRadius(Foot), TopRadius(Top),
          BossHeight(Height), Radius(R)
    {
        Source = TaperedBoss(Base, Axis, OuterRadius, ShoulderHeight, FootRadius, TopRadius, BossHeight);
        if (!Source) return;
        RootEdge = CircularEdgeAt(Source.Payload, Base + Axis * ShoulderHeight, Axis, FootRadius);
        if (RootEdge < 0) return;
        Rounded = BlendSolver::FilletEdge(Source.Payload, RootEdge, Radius);
        if (Rounded)
            Inspection = InspectFillet(Rounded.Payload, Base, Axis, OuterRadius, ShoulderHeight, FootRadius, TopRadius,
                                       BossHeight, Radius, Source.Payload.Validate().Volume);
    }
    [[nodiscard]] bool Exact() const noexcept
    {
        return Rounded && Inspection.Topology && Inspection.AnalyticIdentity && Inspection.Supports &&
            Inspection.TorusResidual < 1e-9 && Inspection.ShoulderTangentBreak < 1e-10 &&
            Inspection.ConeTangentBreak < 1e-10 && Inspection.VolumeError < VolumeLimit && Inspection.WedgeError < WedgeLimit;
    }
    // One line of measured numbers under the fixture's verdict, or the refusal it produced.
    void Report(VerificationPanel& Panel) const noexcept
    {
        if (!Rounded) { Panel.Note("refused: %s", Rounded.Denial.Detail); return; }
        Panel.Note("α = %+.4f rad · torus residual %.1e · G1 breaks %.1e / %.1e · volume %.1e · wedge %.1e",
                   std::atan2(FootRadius - TopRadius, BossHeight), Inspection.TorusResidual, Inspection.ShoulderTangentBreak,
                   Inspection.ConeTangentBreak, Inspection.VolumeError, Inspection.WedgeError);
    }
};
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 32z · Plane–Cone Fillet Verification — exact unequal-radius support roll");

    //------------------------------------------------------------------ specification-level prerequisites
    Panel.Section("Unequal-radius support pair prerequisites");
    {
        EndpointSupport Low{ { 0, 0, 0 }, { 0, 0, 1 }, 2.0 };
        EndpointSupport High{ { 0, 0, 8 }, { 0, 0, -1 }, 1.25 };
        std::string Refusal;
        Panel.Expect("Unequal parallel endpoint supports classify", BlendSolver::ValidateAsymmetricEndpointPair(Low, High, 0.1, Refusal));
        Panel.Expect("Equal endpoint radii refuse", !BlendSolver::ValidateAsymmetricEndpointPair(Low, EndpointSupport{ High.Centre, High.Normal, 2.0 }, 0.1, Refusal));
        Panel.Expect("Non-parallel endpoint normals refuse", !BlendSolver::ValidateAsymmetricEndpointPair(Low, EndpointSupport{ High.Centre, { 1, 0, 0 }, 1.25 }, 0.1, Refusal));
        Panel.Expect("A consumed ligament refuses", !BlendSolver::ValidateAsymmetricEndpointPair(Low, EndpointSupport{ { 3, 0, 0 }, High.Normal, 1.25 }, 0.1, Refusal));
        Panel.Expect("A degenerate endpoint normal refuses", !BlendSolver::ValidateAsymmetricEndpointPair(Low, EndpointSupport{ High.Centre, {}, 1.25 }, 0.1, Refusal));

        // The ruled surface is measured, not trusted: its generator tangent must be the derivative of its own samples.
        VariableRadiusSurface Surface{ Low.Centre, High.Centre - Low.Centre, { 1, 0, 0 }, 8.0, { 2.0, 1.25 } };
        Panel.Equal("Ruled surface reaches the low radius", Surface.Sample(0.0, 0.0).Distance(Low.Centre), 2.0, 1e-12);
        Panel.Equal("Ruled surface reaches the high radius", Surface.Sample(1.0, 0.0).Distance(High.Centre), 1.25, 1e-12);
        double TangentError = 0.0, NormalError = 0.0;
        for (double Angle : { 0.0, 0.7, ScalarCriteria::HalfPi, 2.3, ScalarCriteria::Pi, 4.0 })
        {
            const double Step = 1e-4;
            Vec3 Difference = (Surface.Sample(0.5 + Step, Angle) - Surface.Sample(0.5 - Step, Angle)) / (2.0 * Step);
            TangentError = std::max(TangentError, Difference.Distance(Surface.TangentAlong(Angle)));
            Vec3 Circumferential = (Surface.Sample(0.5, Angle + Step) - Surface.Sample(0.5, Angle - Step)) / (2.0 * Step);
            NormalError = std::max({ NormalError, std::fabs(Surface.Normal(Angle).Dot(Surface.TangentAlong(Angle).Normalised())),
                                     std::fabs(Surface.Normal(Angle).Dot(Circumferential.Normalised())) });
        }
        Panel.Within("Generator tangent matches the sampled derivative at six angles", TangentError, 1e-6);
        Panel.Within("Surface normal is perpendicular to both parametric directions", NormalError, 1e-6);
        const double HalfAngle = std::atan2(0.75, 8.0);
        Panel.Equal("Circumferential curvature is cos α / r(T)", Surface.CircumferentialCurvature(0.5),
                    std::cos(HalfAngle) / Surface.Law.Radius(0.5), 1e-12);
        Panel.Expect("A linear radius law has zero meridional curvature", Surface.MeridionalCurvature() == 0.0);
        Panel.Expect("Curvature acceptance admits a bound above the tight end", BlendSolver::ValidateVariableSurfaceCurvature(Surface, 0.8, Refusal));
        Panel.Expect("Curvature acceptance refuses a bound below the tight end", !BlendSolver::ValidateVariableSurfaceCurvature(Surface, 0.79, Refusal));
        Panel.Expect("G1 endpoint match accepts anti-parallel normals", BlendSolver::ValidateG1EndpointMatch({ 0, 0, 1 }, { 0, 0, -1 }, Refusal));
        Panel.Expect("G1 endpoint match refuses perpendicular normals", !BlendSolver::ValidateG1EndpointMatch({ 1, 0, 0 }, { 0, 0, 1 }, Refusal));
        Panel.Expect("A support normal off the ruled surface normal is not G1",
                     !BlendSolver::ValidateVariableSurfaceG1(Surface, { 0, 0, 1 }, Surface.Normal(0.0), 0.0, Refusal));
        AsymmetricEndpointChain Chain{ { Low, High, EndpointSupport{ { 0, 0, 16 }, High.Normal, 0.75 } } };
        Panel.Expect("A collinear unequal-radius chain classifies", BlendSolver::ValidateAsymmetricEndpointChain(Chain, 0.1, Refusal));
        Chain.Supports[2].Centre = { 1, 0, 16 };
        Panel.Expect("A bending chain refuses", !BlendSolver::ValidateAsymmetricEndpointChain(Chain, 0.1, Refusal));

        AsymmetricBlendSpecification Taper{ Low, High, AsymmetricSupportClassification::TaperedFrustum, 0.1, 0.0 };
        Panel.Expect("A tapered specification classifies", BlendSolver::ValidateAsymmetricSpecification(Taper, Refusal));
        Deliver<BrepBody> Frustum = BlendSolver::ReconstructAsymmetricSupport(Taper);
        const double FrustumVolume = VariableRadiusLaw{ 2.0, 1.25 }.SweptVolume(8.0);
        Panel.Expect("The tapered frustum cross-check reconstructs a solid", Frustum && Frustum.Payload.Validate().Solid());
        Panel.Within("Frustum cross-check volume follows the law's swept volume (relative)",
                     Frustum ? std::fabs(Frustum.Payload.Validate().Volume - FrustumVolume) / FrustumVolume : ScalarCriteria::Infinity, VolumeLimit);
        Taper.Classification = AsymmetricSupportClassification::VariableRadiusRoll;
        Panel.Expect("A variable-radius roll without a blend radius refuses", !BlendSolver::ValidateAsymmetricSpecification(Taper, Refusal));
        Taper.BlendRadius = 0.25; Taper.RadiusLaw = { 2.0, 1.0 };
        Panel.Expect("A radius law that misses its endpoint radii refuses", !BlendSolver::ValidateAsymmetricSpecification(Taper, Refusal));
        Taper.RadiusLaw = { 2.0, 1.25 };
        Panel.Expect("A matching linear law classifies", BlendSolver::ValidateAsymmetricSpecification(Taper, Refusal));
        Taper.Classification = AsymmetricSupportClassification::PartialEndpointChain;
        Panel.Expect("A partial endpoint chain stays explicitly refused", !BlendSolver::ReconstructAsymmetricSupport(Taper));
    }

    //------------------------------------------------------------------ the bounded B-rep route
    const Vec3 Base{ 0, 0, 0 }, Axis{ 0, 0, 1 };
    constexpr double OuterRadius = 10.0, ShoulderHeight = 8.0, FootRadius = 5.0, TopRadius = 3.5, BossHeight = 7.0, Radius = 2.0;
    RoundedFixture Narrowing(Base, Axis, OuterRadius, ShoulderHeight, FootRadius, TopRadius, BossHeight, Radius);
    const BrepBody& Source = Narrowing.Source.Payload;
    const ConeRoll Roll = AnalyticRoll(OuterRadius, ShoulderHeight, FootRadius, TopRadius, BossHeight, Radius);
    const Vec3 RootCentre = Base + Axis * ShoulderHeight;

    Panel.Section("Bounded plane–cone support classifier");
    Panel.Expect("The tapered stepped solid is a closed V4/E7/C14/L5/F5 B-rep with one conical face",
                 Narrowing.Source && Source.Validate().Solid() && Source.Vertices.size() == 4 && Source.Edges.size() == 7 &&
                 Source.Coedges.size() == 14 && Source.Loops.size() == 5 && Source.Faces.size() == 5 &&
                 FaceOf(Source, SurfaceClassification::Cone) >= 0);
    Panel.Expect("The conical boss root is found from its geometry", Narrowing.RootEdge >= 0);
    Panel.Note("half-angle α = %.6f rad, contact height z_t = %.9f, contact radius ρ_t = %.9f, spine ρ_c = %.9f",
               Roll.HalfAngle, Roll.ContactHeight, Roll.ContactRadius, Roll.SpineRadius);

    Panel.Section("Exact rational rolling-ball result");
    Panel.Expect("The plane–cone root fillet returns a closed manifold solid", Narrowing.Rounded && Narrowing.Rounded.Payload.Validate().Solid());
    Panel.Expect("The result has the exact V5/E9/C18/L6/F6 genus-zero topology", Narrowing.Inspection.Topology);
    Panel.Expect("The roll retains exact partial-torus identity at the analytic spine radius", Narrowing.Inspection.AnalyticIdentity);
    Panel.Within("Torus band implicit residual", Narrowing.Inspection.TorusResidual, 1e-9);
    Panel.Within("G1 break at the planar shoulder contact", Narrowing.Inspection.ShoulderTangentBreak, 1e-10);
    Panel.Within("G1 break at the conical boss contact", Narrowing.Inspection.ConeTangentBreak, 1e-10);
    Panel.Expect("Outer wall, annular shoulder, shortened cone, and both caps retain exact extents", Narrowing.Inspection.Supports);
    Panel.Expect("A concave boss-root roll adds material",
                 Narrowing.Rounded && Narrowing.Rounded.Payload.Validate().Volume > Source.Validate().Volume);
    Panel.Within("Tessellated volume follows the Pappus closed form (relative)", Narrowing.Inspection.VolumeError, VolumeLimit);
    Panel.Within("Added material follows the closed-form wedge (relative to the wedge)", Narrowing.Inspection.WedgeError, WedgeLimit);
    Panel.Note("closed-form wedge = %.9f · sharp source's own tessellation floor = %.2e · rounded total = %.2e",
               Roll.AddedVolume, std::fabs(Source.Validate().Volume - Roll.SourceVolume) / Roll.SourceVolume, Narrowing.Inspection.VolumeError);
    Narrowing.Report(Panel);
    Panel.Expect("The sharp root edge is replaced by the two analytic contact circles",
                 Narrowing.Rounded && CircularEdgeAt(Narrowing.Rounded.Payload, RootCentre, Axis, FootRadius) < 0 &&
                 CircularEdgeAt(Narrowing.Rounded.Payload, RootCentre, Axis, Roll.SpineRadius) >= 0 &&
                 CircularEdgeAt(Narrowing.Rounded.Payload, RootCentre + Axis * Roll.ContactHeight, Axis, Roll.ContactRadius) >= 0);
    Panel.Expect("The source body is untouched by the operation",
                 Narrowing.Source && Source.Vertices.size() == 4 && Source.Edges.size() == 7 && Source.Faces.size() == 5 &&
                 CircularEdgeAt(Source, RootCentre, Axis, FootRadius) == Narrowing.RootEdge);

    Panel.Section("Half-angle sign, axis direction, and the α → 0 limit are not special cases");
    RoundedFixture Flaring(Base, Axis, 12.0, 5.0, 4.0, 5.5, 6.0, 1.5);
    Panel.Expect("An undercut flaring frustum (R_t > R_f, α < 0) rolls exactly with a span above a quarter turn", Flaring.Exact());
    Flaring.Report(Panel);
    Panel.Note("flaring meridian span π/2 − α = %.6f rad", ScalarCriteria::HalfPi - std::atan2(4.0 - 5.5, 6.0));
    RoundedFixture Oblique({ 3, -4, 2 }, { 2, -1, 4 }, 8.0, 6.0, 3.0, 2.0, 5.0, 1.25);
    Panel.Expect("A non-unit oblique construction axis exposes the same support pair and exact roll", Oblique.Exact());
    Oblique.Report(Panel);
    RoundedFixture Reversed({ 0, 0, 15 }, { 0, 0, -1 }, 9.0, 5.0, 4.0, 2.5, 6.0, 1.5);
    Panel.Expect("Reversing the world direction retains exact supports and G1 contacts", Reversed.Exact());
    Reversed.Report(Panel);
    RoundedFixture Steep(Base, Axis, 10.0, 4.0, 6.0, 1.0, 5.0, 1.0);
    Panel.Expect("A steep 45° frustum keeps the exact roll and analytic volume", Steep.Exact());
    Steep.Report(Panel);
    RoundedFixture Limit(Base, Axis, OuterRadius, ShoulderHeight, FootRadius, FootRadius, BossHeight, Radius);
    Panel.Expect("A cone-tagged equal-radius boss (α = 0) rolls exactly through the plane–cone route", Limit.Exact());
    Limit.Report(Panel);
    Panel.Equal("The α = 0 spine radius equals Phase 31's boss radius + r",
                Limit.Rounded ? Limit.Rounded.Payload.Faces[FaceOf(Limit.Rounded.Payload, SurfaceClassification::Torus)].Surface.RadiusMajor : 0.0,
                FootRadius + Radius, 1e-12);
    Panel.Equal("The α = 0 added volume equals Phase 31's closed form",
                AnalyticRoll(OuterRadius, ShoulderHeight, FootRadius, FootRadius, BossHeight, Radius).AddedVolume,
                2.0 * ScalarCriteria::Pi * Radius * Radius *
                    (FootRadius * (1.0 - ScalarCriteria::Pi / 4.0) + Radius * (5.0 / 6.0 - ScalarCriteria::Pi / 4.0)), 1e-12);

    Panel.Section("Feasibility and unsupported selections refuse cleanly");
    Panel.Expect("Zero radius refuses", !BlendSolver::FilletEdge(Source, Narrowing.RootEdge, 0.0));
    // Both feasibility limits are exercised on a fixture where that limit binds first (same α as the main fixture):
    //    the height limit z_t = H needs a wide shoulder (R_out = 30 leaves ρ_c = 12.0 clear), the shoulder limit
    //    ρ_c = R_out needs a narrow one (R_out = 6.5 binds at r ≈ 1.855 while z_t = H would need r ≈ 8.86).
    const double SinA = std::sin(Roll.HalfAngle), CosA = std::cos(Roll.HalfAngle);
    const double HeightLimit = BossHeight / (1.0 - SinA);                              // z_t = H
    const double ShoulderLimit = (6.5 - FootRadius) * CosA / (1.0 - SinA);             // ρ_c = R_outer = 6.5
    Panel.Expect("A radius whose cone contact reaches the boss top refuses", !BlendSolver::FilletEdge(Source, Narrowing.RootEdge, HeightLimit));
    RoundedFixture Tall(Base, Axis, 30.0, ShoulderHeight, FootRadius, TopRadius, BossHeight, HeightLimit * 0.98);
    Panel.Expect("A radius just inside the boss-height limit still rolls exactly", Tall.Exact());
    Tall.Report(Panel);
    RoundedFixture Wide(Base, Axis, 6.5, ShoulderHeight, FootRadius, TopRadius, BossHeight, ShoulderLimit * 0.98);
    Panel.Expect("A radius whose spine reaches the outer wall refuses",
                 Wide.Source && !BlendSolver::FilletEdge(Wide.Source.Payload, Wide.RootEdge, ShoulderLimit));
    Panel.Expect("A radius just inside the shoulder limit still rolls exactly", Wide.Exact());
    Wide.Report(Panel);
    const int OuterRoot = CircularEdgeAt(Source, RootCentre, Axis, OuterRadius);
    Panel.Expect("The opposite shoulder rim is not misclassified as a boss root",
                 OuterRoot >= 0 && !BlendSolver::FilletEdge(Source, OuterRoot, Radius));
    const int TopRim = CircularEdgeAt(Source, RootCentre + Axis * BossHeight, Axis, TopRadius);
    Panel.Expect("The conical top rim has no exact route and refuses", TopRim >= 0 && !BlendSolver::FilletEdge(Source, TopRim, Radius));
    Deliver<BrepBody> ApexSource = TaperedBoss(Base, Axis, OuterRadius, ShoulderHeight, FootRadius, 0.0, BossHeight);
    const int ApexRoot = ApexSource ? CircularEdgeAt(ApexSource.Payload, RootCentre, Axis, FootRadius) : -1;
    Panel.Expect("An apex cone boss is outside the bounded frustum route and refuses",
                 ApexSource && ApexRoot >= 0 && !BlendSolver::FilletEdge(ApexSource.Payload, ApexRoot, Radius));
    Deliver<BrepBody> NativeCylinder = BrepBody::Cylinder(Base, Axis, 6.0, 12.0);
    int NativeTop = NativeCylinder ? CircularEdgeAt(NativeCylinder.Payload, { 0, 0, 12 }, Axis, 6.0) : -1;
    Panel.Expect("The established native-cylinder cap fillet remains available",
                 NativeTop >= 0 && BlendSolver::FilletEdge(NativeCylinder.Payload, NativeTop, 1.0));

    // A Boolean-built source must either classify into the same exact route or refuse: never a plausible approximation.
    Deliver<BrepBody> Disc = BrepBody::Cylinder(Base, Axis, OuterRadius, ShoulderHeight);
    Deliver<BrepBody> Frustum = BrepBody::Cone(RootCentre, Axis, FootRadius, TopRadius, BossHeight);
    Deliver<BrepBody> United = Disc && Frustum
        ? IntersectionSolver::Combine(Disc.Payload, Frustum.Payload, BodyOperation::Union)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "operands not built");
    int UnitedRoot = United ? CircularEdgeAt(United.Payload, RootCentre, Axis, FootRadius) : -1;
    Deliver<BrepBody> UnitedRolled = UnitedRoot >= 0
        ? BlendSolver::FilletEdge(United.Payload, UnitedRoot, Radius)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no Boolean root");
    Panel.Expect("A Boolean-united cylinder/frustum source is either rolled exactly or refused",
                 !UnitedRolled || InspectFillet(UnitedRolled.Payload, Base, Axis, OuterRadius, ShoulderHeight, FootRadius,
                                                TopRadius, BossHeight, Radius, United.Payload.Validate().Volume).AnalyticIdentity);
    Panel.Note("Boolean-built source: %s", UnitedRolled ? "classified and rolled exactly" : UnitedRoot >= 0 ? "root found, route refused" : "no canonical root rim");

    Panel.Section("Transactional multi-edge dispatch, console commit, and visual proof");
    int Applied = 0;
    Deliver<BrepBody> Batched = BlendSolver::FilletEdges(Source, { Narrowing.RootEdge, Narrowing.RootEdge }, Radius, &Applied);
    Panel.Expect("The transactional seed set dispatches one chain through the plane–cone route",
                 Batched && Applied == 1 && InspectFillet(Batched.Payload, Base, Axis, OuterRadius, ShoulderHeight, FootRadius,
                                                          TopRadius, BossHeight, Radius, Source.Validate().Volume).AnalyticIdentity);
#ifndef SOLIDARC_PROOF_FOLDER
#error SOLIDARC_PROOF_FOLDER must be supplied by the build
#endif
    ConsoleHost CommandHost(SOLIDARC_PROOF_FOLDER, 1280, 800);
    (void)CommandHost.Document().AddBody("Tapered", Source);
    const std::string FilletCommand = "fillet Tapered 2 --edges=" + std::to_string(Narrowing.RootEdge) + " --name=Rounded";
    const bool CommandAccepted = CommandHost.Execute(FilletCommand);
    const SceneFigure* CommandResult = CommandHost.Document().Find("Rounded");
    Panel.Expect("The C++ console accepts the plane–cone support fillet", CommandAccepted);
    Panel.Expect("The console commits the exact kernel result",
                 CommandResult && CommandResult->Classification == FigureClassification::Body &&
                 InspectFillet(CommandResult->Body, Base, Axis, OuterRadius, ShoulderHeight, FootRadius, TopRadius,
                               BossHeight, Radius, Source.Validate().Volume).AnalyticIdentity);

    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase32z_PlaneConeFillet.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost ProofHost(SOLIDARC_PROOF_FOLDER, 1280, 800);
    auto Reset = [&]() { return ProofHost.Execute("reset") && ProofHost.Execute("gizmo off"); };
    auto Add = [&](const char* Name, BrepBody Body, uint8_t Matcap) -> bool
    {
        SceneFigure& Figure = ProofHost.Document().AddBody(Name, std::move(Body));
        Figure.Matcap = Matcap;
        return true;
    };
    Deliver<BrepBody> SmallRoll = BlendSolver::FilletEdge(Source, Narrowing.RootEdge, 0.75);
    const bool Rendered = Narrowing.Rounded && Flaring.Rounded && Oblique.Rounded && SmallRoll &&
        Reset() && Add("Sharp", Source.Transformed(Mat4::Translation({ -13, 0, 0 })), 0) &&
        Add("Rolled", Narrowing.Rounded.Payload.Transformed(Mat4::Translation({ 13, 0, 0 })), 2) &&
        ProofHost.Execute("view iso") && ProofHost.Execute("view fit") && ProofHost.Execute("render sheet 0") &&
        Reset() && Add("R2", Narrowing.Rounded.Payload, 2) && ProofHost.Execute("view front") && ProofHost.Execute("view fit") &&
        ProofHost.Execute("view dolly 0.72") && ProofHost.Execute("render sheet 1") &&
        Reset() && Add("FlaringSharp", Flaring.Source.Payload.Transformed(Mat4::Translation({ -15, 0, 0 })), 0) &&
        Add("Flaring", Flaring.Rounded.Payload.Transformed(Mat4::Translation({ 15, 0, 0 })), 6) &&
        ProofHost.Execute("view iso") && ProofHost.Execute("view fit") && ProofHost.Execute("render sheet 2") &&
        Reset() && Add("R0.75", SmallRoll.Payload, 3) && Add("Oblique", Oblique.Rounded.Payload.Transformed(Mat4::Translation({ 22, 0, 0 })), 5) &&
        ProofHost.Execute("view iso") && ProofHost.Execute("view fit") && ProofHost.Execute("render sheet 3") &&
        ProofHost.Execute("render sheet finalize Phase32z_PlaneConeFillet");
    Panel.Expect("C++ proof commands complete without refusal", Rendered);
    Panel.Expect("C++ contact-sheet proof is written and non-trivial",
                 std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);

    return Panel.Conclude();
}
