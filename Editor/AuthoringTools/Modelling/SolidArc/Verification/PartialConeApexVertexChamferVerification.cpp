//=============================================================================================================================================
// SolidArc · Stage 4v · bounded native partial-cone apex vertex chamfer
//=============================================================================================================================================
#include "Kernel/BlendSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>
#include <filesystem>
#include <limits>

using namespace Frontier;

namespace
{
[[nodiscard]] bool OutwardNormals(const BrepBody& Body, const PartialConeApexChamferSpecification& S) noexcept
{
    const Vec3 Axis = S.Axis.Normalised();
    const Vec3 Radial = Workplane::FromNormal(S.Base, Axis).AxisX.Normalised();
    const Vec3 Tangent = Axis.Cross(Radial).Normalised();
    const Vec3 EndRadial = Mat4::Rotation(Axis, S.SweepAngle).TransformDirection(Radial).Normalised();
    const Vec3 EndTangent = Mat4::Rotation(Axis, S.SweepAngle).TransformDirection(Tangent).Normalised();
    const double RetainedHeight = S.Height - S.SetBack;
    const double CapRadius = S.BaseRadius * RetainedHeight / S.Height;
    int Cones = 0, Planes = 0, Meridians = 0;
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
            const Vec3 FromBase = P - S.Base;
            const double Along = FromBase.Dot(Axis);
            const Vec3 LocalRadial = (FromBase - Axis * Along).Normalised();
            const Vec3 Expected = (LocalRadial * RetainedHeight + Axis * (S.BaseRadius - CapRadius)).Normalised();
            if (N.Dot(Expected) < 1.0 - 1e-6) return false;
            ++Cones;
        }
        else if (Surface.Classification == SurfaceClassification::Plane)
        {
            const double Along = (P - S.Base).Dot(Axis);
            const Vec3 Expected = Along < RetainedHeight * 0.5 ? Axis * -1.0 : Axis;
            if (N.Dot(Expected) < 1.0 - 1e-6) return false;
            ++Planes;
        }
        else if (Surface.Classification == SurfaceClassification::Coons)
        {
            const Vec3 FromBase = P - S.Base;
            const Vec3 LocalRadial = (FromBase - Axis * FromBase.Dot(Axis)).Normalised();
            const Vec3 Expected = LocalRadial.Dot(EndRadial) > 1.0 - 1e-6 ? EndTangent * -1.0 : Tangent * -1.0;
            if (N.Dot(Expected) < 1.0 - 1e-6) return false;
            ++Meridians;
        }
        else return false;
    }
    return Cones == 1 && Planes == 2 && Meridians == 2;
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
    VerificationPanel Panel("SolidArc · bounded native partial-cone apex vertex chamfer");
    const Vec3 Base{ 0, 0, 0 };
    const Vec3 Axis{ 0, 0, 1 };
    const double BaseRadius = 5.0;
    const double Height = 7.0;
    const double SetBack = 1.25;
    const double Sweep = ScalarCriteria::Pi * 2.0 / 3.0;
    const Deliver<NurbsCurve> Profile = NurbsCurve::Polyline({ Base, Base + Vec3::UnitX() * BaseRadius,
                                                                 Base + Axis * Height }, true);
    const Deliver<BrepBody> SourceDeliver = Profile
        ? BrepBody::Revolve(Profile.Payload, Base, Axis, Sweep)
        : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "partial cone profile is degenerate");
    Panel.Expect("The native partial-cone source is constructed", static_cast<bool>(SourceDeliver));
    if (SourceDeliver)
        Panel.Expect("The source has canonical V4/E6/C8/L3/F3 topology", SourceDeliver.Payload.Vertices.size() == 4 &&
                     SourceDeliver.Payload.Edges.size() == 6 && SourceDeliver.Payload.Coedges.size() == 8 &&
                     SourceDeliver.Payload.Loops.size() == 3 && SourceDeliver.Payload.Faces.size() == 3);
    const BrepBody Source = SourceDeliver.Payload;
    const BodyReport Before = Source.Validate();
    constexpr int ApexVertex = 3;
    const Deliver<PartialConeApexChamferSpecification> Candidate =
        BlendSolver::ClassifyPartialConeApexChamferVertex(Source, ApexVertex, SetBack);
    Panel.Expect("One explicit partial-cone apex vertex dispatches", static_cast<bool>(Candidate));
    if (Candidate)
    {
        const PartialConeApexChamferSpecification& S = Candidate.Payload;
        Panel.Within("The partial-cone base is extracted exactly", S.Base.Distance(Base), 1e-12);
        Panel.Within("The partial-cone axis is normalized exactly", S.Axis.Distance(Axis), 1e-12);
        Panel.Within("The partial-cone base radius is extracted exactly", S.BaseRadius - BaseRadius, 1e-10);
        Panel.Within("The partial-cone height is extracted exactly", S.Height - Height, 1e-10);
        Panel.Within("The actual native sweep is retained exactly", S.SweepAngle - Sweep, 1e-9);
        Panel.Within("The requested axial set-back is retained", S.SetBack - SetBack, 1e-12);
        Panel.Expect("The retained sweep is strictly partial", S.SweepAngle > 0.0 && S.SweepAngle < ScalarCriteria::TwoPi);
        const Deliver<PartialConeApexChamferSpecification> Repeat =
            BlendSolver::ClassifyPartialConeApexChamferVertex(Source, ApexVertex, SetBack);
        Panel.Expect("Partial-apex chamfer dispatch is deterministic", Repeat &&
                     Repeat.Payload.Base.Distance(S.Base) <= 1e-12 &&
                     Repeat.Payload.Axis.Distance(S.Axis) <= 1e-12 &&
                     std::fabs(Repeat.Payload.SweepAngle - S.SweepAngle) <= 1e-12 &&
                     std::fabs(Repeat.Payload.SetBack - S.SetBack) <= 1e-12);
        Panel.Expect("Partial-apex chamfer dispatch leaves the source unchanged", SameSource(Source, Before));
    }

    Panel.Section("Separate partial frustum-and-cap reconstruction after vertex dispatch");
    const Deliver<BrepBody> Result = Candidate
        ? BlendSolver::ReconstructPartialConeApexChamfer(Candidate.Payload)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no eligible partial apex vertex");
    Panel.Expect("The dispatched partial-apex chamfer reconstructs a solid", Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        Panel.Expect("The dispatched partial chamfer retains V6/E9/C18/L5/F5 topology",
                     Result.Payload.Vertices.size() == 6 && Result.Payload.Edges.size() == 9 &&
                     Result.Payload.Coedges.size() == 18 && Result.Payload.Loops.size() == 5 &&
                     Result.Payload.Faces.size() == 5 && Report.Hulls == 1 && Report.Genus == 0 &&
                     Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0);
        Panel.Expect("The dispatched partial chamfer has outward normals", OutwardNormals(Result.Payload, Candidate.Payload));
        const double RetainedHeight = Candidate.Payload.Height - Candidate.Payload.SetBack;
        const double CapRadius = Candidate.Payload.BaseRadius * RetainedHeight / Candidate.Payload.Height;
        const double ExpectedVolume = ScalarCriteria::Pi * RetainedHeight *
            (Candidate.Payload.BaseRadius * Candidate.Payload.BaseRadius + Candidate.Payload.BaseRadius * CapRadius +
             CapRadius * CapRadius) / 3.0 * Candidate.Payload.SweepAngle / ScalarCriteria::TwoPi;
        Panel.Within("The partial chamfer volume follows the exact sector frustum", Report.Volume - ExpectedVolume, 2e-3);
        bool ExactSurfaces = false;
        for (const BrepFace& Face : Result.Payload.Faces)
            if (Face.Surface.Classification == SurfaceClassification::Cone)
                ExactSurfaces = Face.Surface.Origin.Distance(Candidate.Payload.Base) <= 1e-12 &&
                    Face.Surface.Axis.Normalised().Distance(Candidate.Payload.Axis) <= 1e-12 &&
                    std::fabs(Face.Surface.RadiusMajor - Candidate.Payload.BaseRadius) <= 1e-12 &&
                    std::fabs(Face.Surface.RadiusMinor - CapRadius) <= 1e-12;
        Panel.Expect("The partial frustum retains exact cone support radii", ExactSurfaces);
        Panel.Expect("Partial-apex chamfer remains transactional", SameSource(Source, Before));
    }

    Panel.Section("Unsupported partial-apex vertices and explicit boundaries");
    Panel.Expect("The base-center vertex refuses", !BlendSolver::ClassifyPartialConeApexChamferVertex(Source, 0, SetBack));
    Panel.Expect("A base-rim vertex refuses", !BlendSolver::ClassifyPartialConeApexChamferVertex(Source, 2, SetBack));
    Panel.Expect("An out-of-range vertex refuses", !BlendSolver::ClassifyPartialConeApexChamferVertex(Source, 99, SetBack));
    Panel.Expect("A zero/negative/non-finite set-back refuses",
                 !BlendSolver::ClassifyPartialConeApexChamferVertex(Source, ApexVertex, 0.0) &&
                 !BlendSolver::ClassifyPartialConeApexChamferVertex(Source, ApexVertex, -0.2) &&
                 !BlendSolver::ClassifyPartialConeApexChamferVertex(Source, ApexVertex, std::numeric_limits<double>::infinity()));
    Panel.Expect("A consuming or oversized set-back refuses",
                 !BlendSolver::ClassifyPartialConeApexChamferVertex(Source, ApexVertex, Height) &&
                 !BlendSolver::ClassifyPartialConeApexChamferVertex(Source, ApexVertex, Height + 0.2));
    const Deliver<BrepBody> Full = BrepBody::Revolve(Profile.Payload, Base, Axis, ScalarCriteria::TwoPi);
    Panel.Expect("A full-turn native cone remains outside partial dispatch",
                 Full && !BlendSolver::ClassifyPartialConeApexChamferVertex(Full.Payload, 1, SetBack));
    const Deliver<BrepBody> Cylinder = BrepBody::Cylinder(Base, Axis, BaseRadius, Height);
    Panel.Expect("A cylinder remains outside partial-apex chamfer dispatch",
                 Cylinder && !BlendSolver::ClassifyPartialConeApexChamferVertex(Cylinder.Payload, 0, SetBack));
    BrepBody NonManifold = Source;
    NonManifold.Edges[0].Coedges.push_back(NonManifold.Edges[0].Coedges.front());
    Panel.Expect("A malformed partial cone refuses transactionally",
                 !BlendSolver::ClassifyPartialConeApexChamferVertex(NonManifold, ApexVertex, SetBack));
    Panel.Expect("The source remains unchanged after all partial refusals", SameSource(Source, Before));

    Panel.Section("Distinct sharp/planar-cap partial-apex proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) /
        "Phase38v_PartialConeApexVertexChamfer.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = SourceDeliver && Result &&
        Host.Document().AddBody("SelectedPartialApexSource", Source.Transformed(Mat4::Translation({ -9, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("PartialApexChamfer", Result.Payload.Transformed(Mat4::Translation({ 9, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 205 -12") && Host.Execute("view fit") &&
        Host.Execute("render Phase38v_PartialConeApexVertexChamfer");
    Panel.Expect("The sharp/planar-cap partial-apex proof render completes", Rendered);
    Panel.Expect("The partial-apex chamfer proof PNG is visible",
                 std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
