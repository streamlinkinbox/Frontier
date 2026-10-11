//=============================================================================================================================================
// SolidArc · Stage 4s · bounded non-reflex partial-cone apex vertex dispatch
//=============================================================================================================================================
#include "Kernel/BlendSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>
#include <filesystem>

using namespace Frontier;

namespace
{
[[nodiscard]] bool AnalyticNormals(const BrepBody& Body, const PartialConeApexFilletSpecification& S) noexcept
{
    const Vec3 Axis = S.Axis.Normalised();
    const Vec3 Radial = Workplane::FromNormal(S.Base, Axis).AxisX.Normalised();
    const Vec3 Tangent = Axis.Cross(Radial).Normalised();
    const double Slant = std::hypot(S.Height, S.BaseRadius);
    const Vec3 Centre = S.Base + Axis * (S.Height - S.FilletRadius * Slant / S.BaseRadius);
    int Cones = 0, Spheres = 0, Planes = 0, Meridians = 0;
    for (size_t I = 0; I < Body.Faces.size(); ++I)
    {
        const NurbsSurface& Surface = Body.Faces[I].Surface;
        const double U = 0.37 * (Surface.DomainEndU() - Surface.DomainStartU()) + Surface.DomainStartU();
        const double V = 0.53 * (Surface.DomainEndV() - Surface.DomainStartV()) + Surface.DomainStartV();
        const Vec3 P = Surface.Sample(U, V);
        const Vec3 N = Body.FaceNormal(static_cast<int>(I), U, V).Normalised();
        if (N.Length() <= ScalarCriteria::GeometricTolerance) return false;
        if (Surface.Classification == SurfaceClassification::Cone)
        {
            const Vec3 R = (P - S.Base - Axis * (P - S.Base).Dot(Axis)).Normalised();
            if (N.Dot((R * S.Height + Axis * S.BaseRadius).Normalised()) < 1.0 - 1e-6) return false;
            ++Cones;
        }
        else if (Surface.Classification == SurfaceClassification::Sphere)
        {
            if (N.Dot((P - Centre).Normalised()) < 1.0 - 1e-6) return false;
            ++Spheres;
        }
        else if (Surface.Classification == SurfaceClassification::Plane)
        {
            if (N.Dot(Axis) > -1.0 + 1e-6) return false;
            ++Planes;
        }
        else if (Surface.Classification == SurfaceClassification::Coons)
        {
            const Vec3 FromBase = P - S.Base;
            const double Angle = std::atan2(FromBase.Dot(Tangent), FromBase.Dot(Radial));
            const Vec3 EndTangent = Mat4::Rotation(Axis, S.SweepAngle).TransformDirection(Tangent).Normalised();
            const Vec3 Expected = Angle < S.SweepAngle * 0.5 ? Tangent * -1.0 : EndTangent * -1.0;
            if (N.Dot(Expected) < 1.0 - 1e-6) return false;
            ++Meridians;
        }
        else return false;
    }
    return Cones == 1 && Spheres == 1 && Planes == 1 && Meridians == 2;
}

[[nodiscard]] bool SameSource(const BrepBody& Body, const BodyReport& Before) noexcept
{
    const BodyReport After = Body.Validate();
    return After.Volume == Before.Volume && Body.Vertices.size() == 4 && Body.Edges.size() == 6 &&
           Body.Coedges.size() == 8 && Body.Loops.size() == 3 && Body.Faces.size() == 3;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · bounded non-reflex partial-cone apex vertex dispatch");
    const Vec3 Base{ 0, 0, 0 };
    const Vec3 Axis{ 0, 0, 1 };
    const double BaseRadius = 5.0;
    const double Height = 7.0;
    const double FilletRadius = 0.6;
    const double Sweep = ScalarCriteria::Pi / 3.0;
    const Deliver<NurbsCurve> Profile = NurbsCurve::Polyline({ Base, Base + Vec3::UnitX() * BaseRadius,
                                                                 Base + Axis * Height }, true);
    const Deliver<BrepBody> SourceDeliver = Profile
        ? BrepBody::Revolve(Profile.Payload, Base, Axis, Sweep)
        : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "general partial cone profile is degenerate");
    Panel.Expect("The non-reflex native partial-cone revolve exists", static_cast<bool>(SourceDeliver));
    if (SourceDeliver)
        Panel.Expect("The source has canonical V4/E6/C8/L3/F3 topology", SourceDeliver.Payload.Vertices.size() == 4 &&
                     SourceDeliver.Payload.Edges.size() == 6 && SourceDeliver.Payload.Coedges.size() == 8 &&
                     SourceDeliver.Payload.Loops.size() == 3 && SourceDeliver.Payload.Faces.size() == 3);
    const BrepBody Source = SourceDeliver.Payload;
    const BodyReport Before = Source.Validate();
    constexpr int ApexVertex = 3;
    const Deliver<PartialConeApexFilletSpecification> Candidate =
        BlendSolver::ClassifyGeneralPartialConeApexFilletVertex(Source, ApexVertex, FilletRadius);
    Panel.Expect("One explicit non-reflex apex vertex dispatches", static_cast<bool>(Candidate));
    if (Candidate)
    {
        const PartialConeApexFilletSpecification& Specification = Candidate.Payload;
        Panel.Within("The general partial-cone base is extracted exactly", Specification.Base.Distance(Base), 1e-12);
        Panel.Within("The general partial-cone axis is extracted exactly", Specification.Axis.Distance(Axis), 1e-12);
        Panel.Within("The general partial-cone base radius is extracted exactly", Specification.BaseRadius - BaseRadius, 1e-10);
        Panel.Within("The general partial-cone height is extracted exactly", Specification.Height - Height, 1e-10);
        Panel.Within("The non-reflex sweep is extracted exactly", Specification.SweepAngle - Sweep, 1e-10);
        Panel.Within("The requested general partial-apex radius is retained", Specification.FilletRadius - FilletRadius, 1e-12);
        const Deliver<PartialConeApexFilletSpecification> Repeat =
            BlendSolver::ClassifyGeneralPartialConeApexFilletVertex(Source, ApexVertex, FilletRadius);
        Panel.Expect("General partial-apex dispatch is deterministic", Repeat &&
                     Repeat.Payload.Base.Distance(Specification.Base) <= 1e-12 &&
                     Repeat.Payload.Axis.Distance(Specification.Axis) <= 1e-12 &&
                     std::fabs(Repeat.Payload.SweepAngle - Specification.SweepAngle) <= 1e-12);
        Panel.Expect("General partial-apex dispatch leaves the source unchanged", SameSource(Source, Before));
    }

    Panel.Section("Separate general partial spherical-cap reconstruction after vertex dispatch");
    const Deliver<BrepBody> Result = Candidate
        ? BlendSolver::ReconstructPartialConeApexFillet(Candidate.Payload)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no eligible general partial apex vertex");
    Panel.Expect("The dispatched general partial-apex specification reconstructs a solid", Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        Panel.Expect("The dispatched result retains V6/E9/F5/L5 topology",
                     Result.Payload.Vertices.size() == 6 && Result.Payload.Edges.size() == 9 &&
                     Result.Payload.Coedges.size() == 18 && Result.Payload.Loops.size() == 5 &&
                     Result.Payload.Faces.size() == 5 && Report.Hulls == 1 && Report.Genus == 0 &&
                     Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0);
        Panel.Expect("The dispatched general partial-apex result has outward normals",
                     AnalyticNormals(Result.Payload, Candidate.Payload));
        Panel.Expect("General partial-apex dispatch remains transactional", SameSource(Source, Before));
    }

    Panel.Section("Unsupported general partial-apex vertices and source refusals");
    Panel.Expect("The base-center vertex refuses", !BlendSolver::ClassifyGeneralPartialConeApexFilletVertex(Source, 0, FilletRadius));
    Panel.Expect("A base-rim vertex refuses", !BlendSolver::ClassifyGeneralPartialConeApexFilletVertex(Source, 2, FilletRadius));
    Panel.Expect("An out-of-range vertex refuses", !BlendSolver::ClassifyGeneralPartialConeApexFilletVertex(Source, 99, FilletRadius));
    Panel.Expect("A zero general partial-apex radius refuses", !BlendSolver::ClassifyGeneralPartialConeApexFilletVertex(Source, ApexVertex, 0.0));
    Panel.Expect("A negative general partial-apex radius refuses", !BlendSolver::ClassifyGeneralPartialConeApexFilletVertex(Source, ApexVertex, -0.2));
    const double MaximumRadius = Height * BaseRadius / std::hypot(Height, BaseRadius);
    Panel.Expect("A radius consuming the general partial cone refuses",
                 !BlendSolver::ClassifyGeneralPartialConeApexFilletVertex(Source, ApexVertex, MaximumRadius));
    const Deliver<NurbsCurve> HalfProfile = NurbsCurve::Polyline({ Base, Base + Vec3::UnitX() * BaseRadius,
                                                                     Base + Axis * Height }, true);
    const Deliver<BrepBody> HalfSource = HalfProfile
        ? BrepBody::Revolve(HalfProfile.Payload, Base, Axis, ScalarCriteria::Pi)
        : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "half-turn profile is degenerate");
    Panel.Expect("A half-turn source remains in the dedicated Stage 4r route",
                 HalfSource && !BlendSolver::ClassifyGeneralPartialConeApexFilletVertex(HalfSource.Payload, 3, FilletRadius));
    const Deliver<BrepBody> FullSource = HalfProfile
        ? BrepBody::Revolve(HalfProfile.Payload, Base, Axis, ScalarCriteria::TwoPi)
        : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "full-turn profile is degenerate");
    Panel.Expect("A full-turn source remains outside partial dispatch",
                 FullSource && !BlendSolver::ClassifyGeneralPartialConeApexFilletVertex(FullSource.Payload, 1, FilletRadius));
    const Deliver<BrepBody> Cylinder = BrepBody::Cylinder(Base, Axis, BaseRadius, Height);
    Panel.Expect("A cylinder remains outside general partial-apex dispatch",
                 Cylinder && !BlendSolver::ClassifyGeneralPartialConeApexFilletVertex(Cylinder.Payload, 0, FilletRadius));
    BrepBody NonManifold = Source;
    NonManifold.Edges[0].Coedges.push_back(NonManifold.Edges[0].Coedges.front());
    Panel.Expect("A malformed general partial cone refuses transactionally",
                 !BlendSolver::ClassifyGeneralPartialConeApexFilletVertex(NonManifold, ApexVertex, FilletRadius));
    Panel.Expect("The source remains unchanged after all general partial-apex refusals", SameSource(Source, Before));

    Panel.Section("Distinct sharp/rounded general partial-apex proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) /
        "Phase38s_GeneralPartialConeApexVertexDispatch.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = SourceDeliver && Result &&
        Host.Document().AddBody("SelectedGeneralPartialApex", Source.Transformed(Mat4::Translation({ -9, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("DispatchedGeneralPartialApex", Result.Payload.Transformed(Mat4::Translation({ 9, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 195 -12") && Host.Execute("view fit") &&
        Host.Execute("render Phase38s_GeneralPartialConeApexVertexDispatch");
    Panel.Expect("The sharp/rounded general partial-apex proof render completes", Rendered);
    Panel.Expect("The general partial-apex dispatch proof PNG is visible",
                 std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
