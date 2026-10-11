//=============================================================================================================================================
// SolidArc · Stage 4r · bounded half-turn partial-cone apex vertex dispatch
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
            const Vec3 Expected = Angle < S.SweepAngle * 0.5 ? Tangent * -1.0 : Tangent;
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
    return After.Solid() == Before.Solid() && std::fabs(After.Volume - Before.Volume) <= 1e-9 &&
           Body.Vertices.size() == 4 && Body.Edges.size() == 6 && Body.Coedges.size() == 12 &&
           Body.Loops.size() == 4 && Body.Faces.size() == 4;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · bounded half-turn partial-cone apex vertex dispatch");
    const Vec3 Base{ 0, 0, 0 };
    const Vec3 Axis{ 0, 0, 1 };
    const double BaseRadius = 5.0;
    const double Height = 7.0;
    const double FilletRadius = 0.6;
    const double Sweep = ScalarCriteria::Pi;
    const Deliver<NurbsCurve> Profile = NurbsCurve::Polyline({ Base, Base + Vec3::UnitX() * BaseRadius,
                                                                 Base + Axis * Height }, true);
    const Deliver<BrepBody> SourceDeliver = Profile
        ? BrepBody::Revolve(Profile.Payload, Base, Axis, Sweep)
        : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial cone profile is degenerate");
    Panel.Expect("The half-turn partial cone is a closed solid", SourceDeliver && SourceDeliver.Payload.Validate().Solid());
    if (SourceDeliver)
        Panel.Expect("The source has canonical V4/E6/F4/L4 topology", SourceDeliver.Payload.Vertices.size() == 4 &&
                     SourceDeliver.Payload.Edges.size() == 6 && SourceDeliver.Payload.Coedges.size() == 12 &&
                     SourceDeliver.Payload.Loops.size() == 4 && SourceDeliver.Payload.Faces.size() == 4);
    const BrepBody Source = SourceDeliver.Payload;
    const BodyReport Before = Source.Validate();
    constexpr int ApexVertex = 3;
    const Deliver<PartialConeApexFilletSpecification> Candidate =
        BlendSolver::ClassifyPartialConeApexFilletVertex(Source, ApexVertex, FilletRadius);
    Panel.Expect("One explicit half-turn apex vertex dispatches", static_cast<bool>(Candidate));
    if (Candidate)
    {
        const PartialConeApexFilletSpecification& Specification = Candidate.Payload;
        Panel.Within("The partial-cone base is extracted exactly", Specification.Base.Distance(Base), 1e-12);
        Panel.Within("The partial-cone axis is extracted exactly", Specification.Axis.Distance(Axis), 1e-12);
        Panel.Within("The partial-cone base radius is extracted exactly", Specification.BaseRadius - BaseRadius, 1e-10);
        Panel.Within("The partial-cone height is extracted exactly", Specification.Height - Height, 1e-10);
        Panel.Within("The half-turn sweep is extracted exactly", Specification.SweepAngle - Sweep, 1e-12);
        Panel.Within("The requested partial-apex radius is retained", Specification.FilletRadius - FilletRadius, 1e-12);
        const Deliver<PartialConeApexFilletSpecification> Repeat =
            BlendSolver::ClassifyPartialConeApexFilletVertex(Source, ApexVertex, FilletRadius);
        Panel.Expect("Partial-apex dispatch is deterministic", Repeat &&
                     Repeat.Payload.Base.Distance(Specification.Base) <= 1e-12 &&
                     Repeat.Payload.Axis.Distance(Specification.Axis) <= 1e-12 &&
                     std::fabs(Repeat.Payload.BaseRadius - Specification.BaseRadius) <= 1e-12 &&
                     std::fabs(Repeat.Payload.Height - Specification.Height) <= 1e-12);
        Panel.Expect("Partial-apex dispatch leaves the source unchanged", SameSource(Source, Before));
    }

    Panel.Section("Separate partial spherical-cap reconstruction after vertex dispatch");
    const Deliver<BrepBody> Result = Candidate
        ? BlendSolver::ReconstructPartialConeApexFillet(Candidate.Payload)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no eligible partial apex vertex");
    Panel.Expect("The dispatched partial-apex specification reconstructs a solid", Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        Panel.Expect("The dispatched result retains V6/E9/F5/L5 topology",
                     Result.Payload.Vertices.size() == 6 && Result.Payload.Edges.size() == 9 &&
                     Result.Payload.Coedges.size() == 18 && Result.Payload.Loops.size() == 5 &&
                     Result.Payload.Faces.size() == 5 && Report.Hulls == 1 && Report.Genus == 0 &&
                     Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0);
        Panel.Expect("The dispatched partial-apex result has outward normals", AnalyticNormals(Result.Payload, Candidate.Payload));
        Panel.Expect("Partial-apex dispatch remains transactional", SameSource(Source, Before));
    }

    Panel.Section("Unsupported partial-apex vertices and source refusals");
    Panel.Expect("The base-center vertex refuses", !BlendSolver::ClassifyPartialConeApexFilletVertex(Source, 0, FilletRadius));
    Panel.Expect("A base-rim vertex refuses", !BlendSolver::ClassifyPartialConeApexFilletVertex(Source, 2, FilletRadius));
    Panel.Expect("An out-of-range vertex refuses", !BlendSolver::ClassifyPartialConeApexFilletVertex(Source, 99, FilletRadius));
    Panel.Expect("A zero partial-apex radius refuses", !BlendSolver::ClassifyPartialConeApexFilletVertex(Source, ApexVertex, 0.0));
    Panel.Expect("A negative partial-apex radius refuses", !BlendSolver::ClassifyPartialConeApexFilletVertex(Source, ApexVertex, -0.2));
    const double MaximumRadius = Height * BaseRadius / std::hypot(Height, BaseRadius);
    Panel.Expect("A radius consuming the partial cone refuses",
                 !BlendSolver::ClassifyPartialConeApexFilletVertex(Source, ApexVertex, MaximumRadius));
    const Deliver<NurbsCurve> FullProfile = NurbsCurve::Polyline({ Base, Base + Vec3::UnitX() * BaseRadius,
                                                                     Base + Axis * Height }, true);
    const Deliver<BrepBody> FullSource = FullProfile
        ? BrepBody::Revolve(FullProfile.Payload, Base, Axis, ScalarCriteria::TwoPi)
        : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "full cone profile is degenerate");
    Panel.Expect("A full-turn revolution remains outside partial dispatch",
                 FullSource && !BlendSolver::ClassifyPartialConeApexFilletVertex(FullSource.Payload, 1, FilletRadius));
    const Deliver<BrepBody> Cylinder = BrepBody::Cylinder(Base, Axis, BaseRadius, Height);
    Panel.Expect("A cylinder remains outside partial-cone apex dispatch",
                 Cylinder && !BlendSolver::ClassifyPartialConeApexFilletVertex(Cylinder.Payload, 0, FilletRadius));
    BrepBody NonManifold = Source;
    NonManifold.Edges[0].Coedges.push_back(NonManifold.Edges[0].Coedges.front());
    Panel.Expect("A malformed partial cone refuses transactionally",
                 !BlendSolver::ClassifyPartialConeApexFilletVertex(NonManifold, ApexVertex, FilletRadius));
    Panel.Expect("The source remains unchanged after all partial-apex refusals", SameSource(Source, Before));

    Panel.Section("Distinct sharp/rounded partial-apex proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) /
        "Phase38r_PartialConeApexVertexDispatch.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = SourceDeliver && Result &&
        Host.Document().AddBody("SelectedPartialApexSource", Source.Transformed(Mat4::Translation({ -9, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("DispatchedPartialApex", Result.Payload.Transformed(Mat4::Translation({ 9, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 195 -12") && Host.Execute("view fit") &&
        Host.Execute("render Phase38r_PartialConeApexVertexDispatch");
    Panel.Expect("The sharp/rounded partial-apex proof render completes", Rendered);
    Panel.Expect("The partial-apex dispatch proof PNG is visible",
                 std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
