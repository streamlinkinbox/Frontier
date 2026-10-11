//=============================================================================================================================================
// SolidArc · Stage 4u · bounded native-cone apex vertex chamfer
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
[[nodiscard]] bool OutwardNormals(const BrepBody& Body, const ConeApexChamferSpecification& Specification) noexcept
{
    const Vec3 Axis = Specification.Axis.Normalised();
    const double RetainedHeight = Specification.Height - Specification.SetBack;
    const double CapRadius = Specification.BaseRadius * RetainedHeight / Specification.Height;
    int Cones = 0, Planes = 0;
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
            const Vec3 FromBase = P - Specification.Base;
            const double Along = FromBase.Dot(Axis);
            const Vec3 Radial = (FromBase - Axis * Along).Normalised();
            const Vec3 Expected = (Radial * RetainedHeight + Axis * (Specification.BaseRadius - CapRadius)).Normalised();
            if (N.Dot(Expected) < 1.0 - 1e-6) return false;
            ++Cones;
        }
        else if (Surface.Classification == SurfaceClassification::Plane)
        {
            const double Along = (P - Specification.Base).Dot(Axis);
            const Vec3 Expected = Along < RetainedHeight * 0.5 ? Axis * -1.0 : Axis;
            if (N.Dot(Expected) < 1.0 - 1e-6) return false;
            ++Planes;
        }
        else return false;
    }
    return Cones == 1 && Planes == 2;
}

[[nodiscard]] bool SameSource(const BrepBody& Body, const BodyReport& Before) noexcept
{
    const BodyReport After = Body.Validate();
    return After.Vertices == Before.Vertices && After.Edges == Before.Edges &&
           After.Faces == Before.Faces && After.Loops == Before.Loops &&
           After.OpenEdges == Before.OpenEdges && After.NonManifoldEdges == Before.NonManifoldEdges &&
           After.MisorientedEdges == Before.MisorientedEdges &&
           std::fabs(After.Volume - Before.Volume) <= 1e-9 && Body.Vertices.size() == 2 &&
           Body.Edges.size() == 2 && Body.Coedges.size() == 4 && Body.Loops.size() == 2 &&
           Body.Faces.size() == 2;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · bounded native-cone apex vertex chamfer");
    const Vec3 Base{ 0, 0, 0 };
    const Vec3 Axis{ 0, 0, 1 };
    const double BaseRadius = 5.0;
    const double Height = 7.0;
    const double SetBack = 1.25;
    const Deliver<BrepBody> SourceDeliver = BrepBody::Cone(Base, Axis, BaseRadius, 0.0, Height);
    Panel.Expect("The native right cone is a closed solid", SourceDeliver && SourceDeliver.Payload.Validate().Solid());
    if (SourceDeliver)
        Panel.Expect("The native cone has canonical V2/E2/F2 topology", SourceDeliver.Payload.Vertices.size() == 2 &&
                     SourceDeliver.Payload.Edges.size() == 2 && SourceDeliver.Payload.Coedges.size() == 4 &&
                     SourceDeliver.Payload.Loops.size() == 2 && SourceDeliver.Payload.Faces.size() == 2);
    const BrepBody Source = SourceDeliver.Payload;
    const BodyReport Before = Source.Validate();
    constexpr int ApexVertex = 1;
    const Deliver<ConeApexChamferSpecification> Candidate =
        BlendSolver::ClassifyConeApexChamferVertex(Source, ApexVertex, SetBack);
    Panel.Expect("One explicit native-cone apex vertex dispatches", static_cast<bool>(Candidate));
    if (Candidate)
    {
        const ConeApexChamferSpecification& Specification = Candidate.Payload;
        Panel.Within("The chamfer base origin is extracted exactly", Specification.Base.Distance(Base), 1e-12);
        Panel.Within("The chamfer axis is normalized exactly", Specification.Axis.Distance(Axis), 1e-12);
        Panel.Within("The chamfer base radius is extracted exactly", Specification.BaseRadius - BaseRadius, 1e-12);
        Panel.Within("The chamfer height is extracted exactly", Specification.Height - Height, 1e-12);
        Panel.Within("The requested axial set-back is retained", Specification.SetBack - SetBack, 1e-12);
        Panel.Expect("The set-back leaves a positive retained cone", Specification.SetBack > 0.0 &&
                     Specification.SetBack < Specification.Height);
        const Deliver<ConeApexChamferSpecification> Repeat =
            BlendSolver::ClassifyConeApexChamferVertex(Source, ApexVertex, SetBack);
        Panel.Expect("Apex chamfer dispatch is deterministic", Repeat &&
                     Repeat.Payload.Base.Distance(Specification.Base) <= 1e-12 &&
                     Repeat.Payload.Axis.Distance(Specification.Axis) <= 1e-12 &&
                     std::fabs(Repeat.Payload.BaseRadius - Specification.BaseRadius) <= 1e-12 &&
                     std::fabs(Repeat.Payload.Height - Specification.Height) <= 1e-12 &&
                     std::fabs(Repeat.Payload.SetBack - Specification.SetBack) <= 1e-12);
        Panel.Expect("Apex chamfer dispatch leaves the source unchanged", SameSource(Source, Before));
    }

    Panel.Section("Separate planar-cap reconstruction after vertex dispatch");
    const Deliver<BrepBody> Result = Candidate
        ? BlendSolver::ReconstructConeApexChamfer(Candidate.Payload)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no eligible apex vertex");
    Panel.Expect("The dispatched cone-apex chamfer reconstructs a solid", Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        Panel.Expect("The dispatched chamfer retains V2/E3/C6/L3/F3 topology",
                     Result.Payload.Vertices.size() == 2 && Result.Payload.Edges.size() == 3 &&
                     Result.Payload.Coedges.size() == 6 && Result.Payload.Loops.size() == 3 &&
                     Result.Payload.Faces.size() == 3 && Report.Hulls == 1 && Report.Genus == 0 &&
                     Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0);
        Panel.Expect("The dispatched apex chamfer has outward normals", OutwardNormals(Result.Payload, Candidate.Payload));
        const double RetainedHeight = Candidate.Payload.Height - Candidate.Payload.SetBack;
        const double CapRadius = Candidate.Payload.BaseRadius * RetainedHeight / Candidate.Payload.Height;
        bool HasCap = false, ExactFrustum = false;
        for (const BrepFace& Face : Result.Payload.Faces)
        {
            const NurbsSurface& Surface = Face.Surface;
            if (Surface.Classification == SurfaceClassification::Plane)
            {
                const Vec3 P = Surface.Sample(0.5 * (Surface.DomainStartU() + Surface.DomainEndU()),
                                               0.5 * (Surface.DomainStartV() + Surface.DomainEndV()));
                const double Along = (P - Candidate.Payload.Base).Dot(Candidate.Payload.Axis);
                if (std::fabs(Along - RetainedHeight) <= 1e-9) HasCap = true;
            }
            else if (Surface.Classification == SurfaceClassification::Cone)
                ExactFrustum = Surface.Origin.Distance(Candidate.Payload.Base) <= 1e-12 &&
                    Surface.Axis.Normalised().Distance(Candidate.Payload.Axis) <= 1e-12 &&
                    std::fabs(Surface.RadiusMajor - Candidate.Payload.BaseRadius) <= 1e-12 &&
                    std::fabs(Surface.RadiusMinor - CapRadius) <= 1e-12;
        }
        Panel.Expect("The new planar apex cap and exact frustum radii are retained",
                     HasCap && ExactFrustum);
        Panel.Expect("Apex chamfer remains transactional", SameSource(Source, Before));
    }

    Panel.Section("Unsupported vertex and source refusals");
    Panel.Expect("The base-rim vertex refuses", !BlendSolver::ClassifyConeApexChamferVertex(Source, 0, SetBack));
    Panel.Expect("An out-of-range vertex refuses", !BlendSolver::ClassifyConeApexChamferVertex(Source, 99, SetBack));
    Panel.Expect("A zero set-back refuses", !BlendSolver::ClassifyConeApexChamferVertex(Source, ApexVertex, 0.0));
    Panel.Expect("A negative set-back refuses", !BlendSolver::ClassifyConeApexChamferVertex(Source, ApexVertex, -0.2));
    Panel.Expect("A set-back consuming the cone refuses", !BlendSolver::ClassifyConeApexChamferVertex(Source, ApexVertex, Height));
    Panel.Expect("An oversized set-back refuses", !BlendSolver::ClassifyConeApexChamferVertex(Source, ApexVertex, Height + 0.2));
    Panel.Expect("A non-finite set-back refuses", !BlendSolver::ClassifyConeApexChamferVertex(Source, ApexVertex, std::numeric_limits<double>::infinity()));
    const Deliver<BrepBody> Cylinder = BrepBody::Cylinder(Base, Axis, BaseRadius, Height);
    Panel.Expect("A cylinder vertex remains outside native-cone apex chamfer dispatch",
                 Cylinder && !BlendSolver::ClassifyConeApexChamferVertex(Cylinder.Payload, 0, SetBack));
    const Deliver<BrepBody> Frustum = BrepBody::Cone(Base, Axis, BaseRadius, 2.0, Height);
    Panel.Expect("A frustum remains outside native-apex chamfer dispatch",
                 Frustum && !BlendSolver::ClassifyConeApexChamferVertex(Frustum.Payload, 1, SetBack));
    BrepBody NonManifold = Source;
    NonManifold.Edges[0].Coedges.push_back(NonManifold.Edges[0].Coedges.front());
    Panel.Expect("A malformed cone refuses transactionally",
                 !BlendSolver::ClassifyConeApexChamferVertex(NonManifold, ApexVertex, SetBack));
    Panel.Expect("The source remains unchanged after all chamfer refusals", SameSource(Source, Before));

    Panel.Section("Distinct sharp/planar-cap apex proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) /
        "Phase38u_ConeApexVertexChamfer.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = SourceDeliver && Result &&
        Host.Document().AddBody("SelectedApexSource", Source.Transformed(Mat4::Translation({ -8, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("ApexChamfer", Result.Payload.Transformed(Mat4::Translation({ 8, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 195 -12") && Host.Execute("view fit") &&
        Host.Execute("render Phase38u_ConeApexVertexChamfer");
    Panel.Expect("The sharp/planar-cap apex proof render completes", Rendered);
    Panel.Expect("The apex chamfer proof PNG is visible",
                 std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
