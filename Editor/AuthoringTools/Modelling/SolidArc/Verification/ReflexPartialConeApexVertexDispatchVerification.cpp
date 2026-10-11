//=============================================================================================================================================
// SolidArc · Stage 4t · bounded reflex partial-cone apex vertex dispatch
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
            double Angle = std::atan2(FromBase.Dot(Tangent), FromBase.Dot(Radial));
            if (Angle < 0.0) Angle += ScalarCriteria::TwoPi;
            const Vec3 LocalRadial = (FromBase - Axis * FromBase.Dot(Axis)).Normalised();
            const Vec3 Expected = S.SweepAngle > ScalarCriteria::Pi
                ? Axis.Cross(LocalRadial) * -1.0
                : (Angle < S.SweepAngle * 0.5 ? Tangent * -1.0 : Tangent);
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
    return After.Vertices == Before.Vertices && After.Edges == Before.Edges &&
           After.Faces == Before.Faces && After.Loops == Before.Loops &&
           After.OpenEdges == Before.OpenEdges && After.NonManifoldEdges == Before.NonManifoldEdges &&
           After.MisorientedEdges == Before.MisorientedEdges && std::fabs(After.Volume - Before.Volume) <= 1e-9 &&
           Body.Vertices.size() == 4 && Body.Edges.size() == 6 && Body.Coedges.size() == 8 &&
           Body.Loops.size() == 3 && Body.Faces.size() == 3;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · bounded reflex partial-cone apex vertex dispatch");
    const Vec3 Base{ 0, 0, 0 };
    const Vec3 Axis{ 0, 0, 1 };
    const double BaseRadius = 5.0;
    const double Height = 7.0;
    const double FilletRadius = 0.6;
    const double Sweep = ScalarCriteria::Pi * 1.5;
    const Deliver<NurbsCurve> Profile = NurbsCurve::Polyline({ Base, Base + Vec3::UnitX() * BaseRadius,
                                                                 Base + Axis * Height }, true);
    const Deliver<BrepBody> SourceDeliver = Profile
        ? BrepBody::Revolve(Profile.Payload, Base, Axis, Sweep)
        : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "reflex partial cone profile is degenerate");
    Panel.Expect("The reflex native partial cone source is constructed", static_cast<bool>(SourceDeliver));
    if (SourceDeliver)
        Panel.Expect("The source has canonical V4/E6/C8/L3/F3 topology", SourceDeliver.Payload.Vertices.size() == 4 &&
                     SourceDeliver.Payload.Edges.size() == 6 && SourceDeliver.Payload.Coedges.size() == 8 &&
                     SourceDeliver.Payload.Loops.size() == 3 && SourceDeliver.Payload.Faces.size() == 3);
    const BrepBody Source = SourceDeliver.Payload;
    const BodyReport Before = Source.Validate();
    constexpr int ApexVertex = 3;
    const Deliver<PartialConeApexFilletSpecification> Candidate =
        BlendSolver::ClassifyReflexPartialConeApexFilletVertex(Source, ApexVertex, FilletRadius);
    Panel.Expect("One explicit reflex apex vertex dispatches", static_cast<bool>(Candidate));
    if (Candidate)
    {
        const PartialConeApexFilletSpecification& Specification = Candidate.Payload;
        Panel.Within("The reflex base is extracted exactly", Specification.Base.Distance(Base), 1e-12);
        Panel.Within("The reflex axis is extracted exactly", Specification.Axis.Distance(Axis), 1e-12);
        Panel.Within("The reflex base radius is extracted exactly", Specification.BaseRadius - BaseRadius, 1e-10);
        Panel.Within("The reflex height is extracted exactly", Specification.Height - Height, 1e-10);
        Panel.Within("The actual reflex sweep is retained", Specification.SweepAngle - Sweep, 1e-9);
        Panel.Within("The requested reflex-apex radius is retained", Specification.FilletRadius - FilletRadius, 1e-12);
        Panel.Expect("The retained sweep is strictly reflex", Specification.SweepAngle > ScalarCriteria::Pi &&
                     Specification.SweepAngle < ScalarCriteria::TwoPi);
        const Deliver<PartialConeApexFilletSpecification> Repeat =
            BlendSolver::ClassifyReflexPartialConeApexFilletVertex(Source, ApexVertex, FilletRadius);
        Panel.Expect("Reflex dispatch is deterministic", Repeat &&
                     Repeat.Payload.Base.Distance(Specification.Base) <= 1e-12 &&
                     Repeat.Payload.Axis.Distance(Specification.Axis) <= 1e-12 &&
                     std::fabs(Repeat.Payload.SweepAngle - Specification.SweepAngle) <= 1e-12);
        Panel.Expect("Reflex dispatch leaves the source unchanged", SameSource(Source, Before));
    }

    Panel.Section("Separate reflex partial spherical-cap reconstruction after dispatch");
    const Deliver<BrepBody> Result = Candidate
        ? BlendSolver::ReconstructPartialConeApexFillet(Candidate.Payload)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no eligible reflex apex vertex");
    Panel.Expect("The dispatched reflex specification reconstructs a solid", Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        Panel.Expect("The dispatched reflex result retains V6/E9/F5/L5 topology",
                     Result.Payload.Vertices.size() == 6 && Result.Payload.Edges.size() == 9 &&
                     Result.Payload.Coedges.size() == 18 && Result.Payload.Loops.size() == 5 &&
                     Result.Payload.Faces.size() == 5 && Report.Hulls == 1 && Report.Genus == 0 &&
                     Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0);
        Panel.Expect("The dispatched reflex result has outward normals", AnalyticNormals(Result.Payload, Candidate.Payload));
        Panel.Expect("Reflex dispatch remains transactional", SameSource(Source, Before));
    }

    Panel.Section("Unsupported reflex boundaries and source refusals");
    Panel.Expect("A non-reflex source remains outside reflex dispatch", [&]()
    {
        const Deliver<BrepBody> NonReflex = BrepBody::Revolve(Profile.Payload, Base, Axis, ScalarCriteria::Pi * 0.8);
        return NonReflex && !BlendSolver::ClassifyReflexPartialConeApexFilletVertex(NonReflex.Payload, ApexVertex, FilletRadius);
    }());
    Panel.Expect("A half-turn source remains outside reflex dispatch", [&]()
    {
        const Deliver<BrepBody> Half = BrepBody::Revolve(Profile.Payload, Base, Axis, ScalarCriteria::Pi);
        return Half && !BlendSolver::ClassifyReflexPartialConeApexFilletVertex(Half.Payload, ApexVertex, FilletRadius);
    }());
    Panel.Expect("A full-turn source remains outside reflex dispatch", [&]()
    {
        const Deliver<BrepBody> Full = BrepBody::Revolve(Profile.Payload, Base, Axis, ScalarCriteria::TwoPi);
        return Full && !BlendSolver::ClassifyReflexPartialConeApexFilletVertex(Full.Payload, 1, FilletRadius);
    }());
    Panel.Expect("The base-center vertex refuses", !BlendSolver::ClassifyReflexPartialConeApexFilletVertex(Source, 0, FilletRadius));
    Panel.Expect("A base-rim vertex refuses", !BlendSolver::ClassifyReflexPartialConeApexFilletVertex(Source, 2, FilletRadius));
    Panel.Expect("An out-of-range vertex refuses", !BlendSolver::ClassifyReflexPartialConeApexFilletVertex(Source, 99, FilletRadius));
    Panel.Expect("A zero or consuming radius refuses", !BlendSolver::ClassifyReflexPartialConeApexFilletVertex(Source, ApexVertex, 0.0) &&
                 !BlendSolver::ClassifyReflexPartialConeApexFilletVertex(Source, ApexVertex,
                     Height * BaseRadius / std::hypot(Height, BaseRadius)));
    const Deliver<BrepBody> Cylinder = BrepBody::Cylinder(Base, Axis, BaseRadius, Height);
    Panel.Expect("A cylinder remains outside reflex partial-cone dispatch",
                 Cylinder && !BlendSolver::ClassifyReflexPartialConeApexFilletVertex(Cylinder.Payload, 0, FilletRadius));
    BrepBody NonManifold = Source;
    NonManifold.Edges[0].Coedges.push_back(NonManifold.Edges[0].Coedges.front());
    Panel.Expect("A malformed reflex source refuses transactionally",
                 !BlendSolver::ClassifyReflexPartialConeApexFilletVertex(NonManifold, ApexVertex, FilletRadius));
    Panel.Expect("The source remains unchanged after all reflex refusals", SameSource(Source, Before));

    Panel.Section("Distinct sharp/rounded reflex-apex proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) /
        "Phase38t_ReflexPartialConeApexVertexDispatch.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = SourceDeliver && Result &&
        Host.Document().AddBody("SelectedReflexApexSource", Source.Transformed(Mat4::Translation({ -9, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("DispatchedReflexApex", Result.Payload.Transformed(Mat4::Translation({ 9, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 205 -12") && Host.Execute("view fit") &&
        Host.Execute("render Phase38t_ReflexPartialConeApexVertexDispatch");
    Panel.Expect("The sharp/rounded reflex-apex proof render completes", Rendered);
    Panel.Expect("The reflex-apex dispatch proof PNG is visible",
                 std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
