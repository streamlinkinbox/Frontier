//=============================================================================================================================================
// SolidArc · Stage 4q · bounded native-cone apex vertex dispatch
//=============================================================================================================================================
#include "Kernel/BlendSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>
#include <filesystem>

using namespace Frontier;

namespace
{
[[nodiscard]] bool OutwardNormals(const BrepBody& Body, const ConeApexFilletSpecification& Specification) noexcept
{
    const Vec3 Axis = Specification.Axis.Normalised();
    const Vec3 Centre = Specification.Base + Axis *
        (Specification.Height - Specification.FilletRadius * std::hypot(Specification.Height, Specification.BaseRadius) /
         Specification.BaseRadius);
    int Cones = 0, Spheres = 0, Planes = 0;
    for (size_t Face = 0; Face < Body.Faces.size(); ++Face)
    {
        const NurbsSurface& Surface = Body.Faces[Face].Surface;
        const double U = 0.37 * (Surface.DomainEndU() - Surface.DomainStartU()) + Surface.DomainStartU();
        const double V = 0.53 * (Surface.DomainEndV() - Surface.DomainStartV()) + Surface.DomainStartV();
        const Vec3 Position = Surface.Sample(U, V);
        const Vec3 Normal = Body.FaceNormal(static_cast<int>(Face), U, V).Normalised();
        if (!std::isfinite(Normal.X) || !std::isfinite(Normal.Y) || !std::isfinite(Normal.Z)) return false;
        if (Surface.Classification == SurfaceClassification::Cone)
        {
            const Vec3 Radial = (Position - Specification.Base - Axis *
                                 (Position - Specification.Base).Dot(Axis)).Normalised();
            const Vec3 Expected = (Radial * Specification.Height + Axis * Specification.BaseRadius).Normalised();
            if (Normal.Dot(Expected) < 1.0 - 1e-6) return false;
            ++Cones;
        }
        else if (Surface.Classification == SurfaceClassification::Sphere)
        {
            if (Normal.Dot((Position - Centre).Normalised()) < 1.0 - 1e-6) return false;
            ++Spheres;
        }
        else if (Surface.Classification == SurfaceClassification::Plane)
        {
            if (Normal.Dot(Axis) > -1.0 + 1e-6) return false;
            ++Planes;
        }
        else return false;
    }
    return Cones == 1 && Spheres == 1 && Planes == 1;
}

[[nodiscard]] bool SameSource(const BrepBody& Body, const BodyReport& Before) noexcept
{
    const BodyReport After = Body.Validate();
    return After.Solid() == Before.Solid() && After.Hulls == Before.Hulls && After.Genus == Before.Genus &&
           std::fabs(After.Volume - Before.Volume) <= 1e-9 && Body.Vertices.size() == 2 &&
           Body.Edges.size() == 2 && Body.Coedges.size() == 4 && Body.Loops.size() == 2 && Body.Faces.size() == 2;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · bounded native-cone apex vertex dispatch");
    const Vec3 Base{ 0, 0, 0 };
    const Vec3 Axis{ 0, 0, 1 };
    const double BaseRadius = 5.0;
    const double Height = 7.0;
    const double FilletRadius = 0.6;
    const Deliver<BrepBody> SourceDeliver = BrepBody::Cone(Base, Axis, BaseRadius, 0.0, Height);
    Panel.Expect("The native right cone is a closed solid", SourceDeliver && SourceDeliver.Payload.Validate().Solid());
    if (SourceDeliver)
        Panel.Expect("The native cone has canonical V2/E2/F2 topology", SourceDeliver.Payload.Vertices.size() == 2 &&
                     SourceDeliver.Payload.Edges.size() == 2 && SourceDeliver.Payload.Coedges.size() == 4 &&
                     SourceDeliver.Payload.Loops.size() == 2 && SourceDeliver.Payload.Faces.size() == 2);
    const BrepBody Source = SourceDeliver.Payload;
    const BodyReport Before = Source.Validate();
    constexpr int ApexVertex = 1;
    const Deliver<ConeApexFilletSpecification> Candidate =
        BlendSolver::ClassifyConeApexFilletVertex(Source, ApexVertex, FilletRadius);
    Panel.Expect("One explicit native-cone apex vertex dispatches", static_cast<bool>(Candidate));
    if (Candidate)
    {
        const ConeApexFilletSpecification& Specification = Candidate.Payload;
        Panel.Within("The cone base origin is extracted exactly", Specification.Base.Distance(Base), 1e-12);
        Panel.Within("The cone axis is extracted exactly", Specification.Axis.Distance(Axis), 1e-12);
        Panel.Within("The cone base radius is extracted exactly", Specification.BaseRadius - BaseRadius, 1e-12);
        Panel.Within("The cone height is extracted exactly", Specification.Height - Height, 1e-12);
        Panel.Within("The requested apex radius is retained", Specification.FilletRadius - FilletRadius, 1e-12);
        const Deliver<ConeApexFilletSpecification> Repeat =
            BlendSolver::ClassifyConeApexFilletVertex(Source, ApexVertex, FilletRadius);
        Panel.Expect("Apex dispatch is deterministic", Repeat && Repeat.Payload.Base.Distance(Specification.Base) <= 1e-12 &&
                     Repeat.Payload.Axis.Distance(Specification.Axis) <= 1e-12 &&
                     std::fabs(Repeat.Payload.BaseRadius - Specification.BaseRadius) <= 1e-12 &&
                     std::fabs(Repeat.Payload.Height - Specification.Height) <= 1e-12);
        Panel.Expect("Apex dispatch leaves the source unchanged", SameSource(Source, Before));
    }

    Panel.Section("Separate spherical-cap reconstruction after vertex dispatch");
    const Deliver<BrepBody> Result = Candidate
        ? BlendSolver::ReconstructConeApexFillet(Candidate.Payload)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no eligible apex vertex");
    Panel.Expect("The dispatched cone-apex specification reconstructs a solid", Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        Panel.Expect("The dispatched result retains V4/E5/F3/L3 topology",
                     Result.Payload.Vertices.size() == 4 && Result.Payload.Edges.size() == 5 &&
                     Result.Payload.Coedges.size() == 10 && Result.Payload.Loops.size() == 3 &&
                     Result.Payload.Faces.size() == 3 && Report.Hulls == 1 && Report.Genus == 0 &&
                     Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0);
        Panel.Expect("The dispatched apex result has outward normals", OutwardNormals(Result.Payload, Candidate.Payload));
        Panel.Expect("Apex dispatch remains transactional", SameSource(Source, Before));
    }

    Panel.Section("Unsupported vertex and source refusals");
    Panel.Expect("The base-rim vertex refuses", !BlendSolver::ClassifyConeApexFilletVertex(Source, 0, FilletRadius));
    Panel.Expect("An out-of-range vertex refuses", !BlendSolver::ClassifyConeApexFilletVertex(Source, 99, FilletRadius));
    Panel.Expect("A zero fillet radius refuses", !BlendSolver::ClassifyConeApexFilletVertex(Source, ApexVertex, 0.0));
    Panel.Expect("A negative fillet radius refuses", !BlendSolver::ClassifyConeApexFilletVertex(Source, ApexVertex, -0.2));
    const double MaximumRadius = Height * BaseRadius / std::hypot(Height, BaseRadius);
    Panel.Expect("A radius consuming the cone refuses",
                 !BlendSolver::ClassifyConeApexFilletVertex(Source, ApexVertex, MaximumRadius));
    const Deliver<BrepBody> Cylinder = BrepBody::Cylinder(Base, Axis, BaseRadius, Height);
    Panel.Expect("A cylinder vertex remains outside native-cone apex dispatch",
                 Cylinder && !BlendSolver::ClassifyConeApexFilletVertex(Cylinder.Payload, 0, FilletRadius));
    BrepBody NonManifold = Source;
    NonManifold.Edges[0].Coedges.push_back(NonManifold.Edges[0].Coedges.front());
    Panel.Expect("A malformed cone refuses transactionally",
                 !BlendSolver::ClassifyConeApexFilletVertex(NonManifold, ApexVertex, FilletRadius));
    Panel.Expect("The source remains unchanged after all vertex refusals", SameSource(Source, Before));

    Panel.Section("Distinct sharp/rounded apex proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) /
        "Phase38q_ConeApexVertexDispatch.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = SourceDeliver && Result &&
        Host.Document().AddBody("SelectedApexSource", Source.Transformed(Mat4::Translation({ -8, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("DispatchedApexFillet", Result.Payload.Transformed(Mat4::Translation({ 8, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 195 -12") && Host.Execute("view fit") &&
        Host.Execute("render Phase38q_ConeApexVertexDispatch");
    Panel.Expect("The sharp/rounded apex proof render completes", Rendered);
    Panel.Expect("The apex dispatch proof PNG is visible",
                 std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
