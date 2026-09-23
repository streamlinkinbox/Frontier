//=============================================================================================================================================
// SolidArc · Stage 4u · bounded native-cone apex vertex chamfer dispatch
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
[[nodiscard]] bool SameSource(const BrepBody& Body, const BodyReport& Before) noexcept
{
    const BodyReport After = Body.Validate();
    return After.Solid() == Before.Solid() && After.Hulls == Before.Hulls && After.Genus == Before.Genus &&
           After.OpenEdges == Before.OpenEdges && After.NonManifoldEdges == Before.NonManifoldEdges &&
           After.MisorientedEdges == Before.MisorientedEdges && std::fabs(After.Volume - Before.Volume) <= 1e-9 &&
           Body.Vertices.size() == 2 && Body.Edges.size() == 2 && Body.Coedges.size() == 4 &&
           Body.Loops.size() == 2 && Body.Faces.size() == 2;
}

[[nodiscard]] bool ExactFrustumGeometry(const BrepBody& Body, const ConeApexChamferSpecification& Specification) noexcept
{
    const Vec3 Axis = Specification.Axis.Normalised();
    const double Slant = std::hypot(Specification.Height, Specification.BaseRadius);
    const double RetainedHeight = Specification.Height - Specification.SetBack * Specification.Height / Slant;
    const double CapRadius = Specification.SetBack * Specification.BaseRadius / Slant;
    int Cones = 0, BasePlanes = 0, CapPlanes = 0;
    for (size_t I = 0; I < Body.Faces.size(); ++I)
    {
        const NurbsSurface& Surface = Body.Faces[I].Surface;
        const double U = 0.37 * (Surface.DomainEndU() - Surface.DomainStartU()) + Surface.DomainStartU();
        const double V = 0.53 * (Surface.DomainEndV() - Surface.DomainStartV()) + Surface.DomainStartV();
        const Vec3 Position = Surface.Sample(U, V);
        if (Surface.Classification == SurfaceClassification::Cone)
            ++Cones;
        else if (Surface.Classification == SurfaceClassification::Plane)
        {
            const double Along = (Position - Specification.Base).Dot(Axis);
            if (std::fabs(Along) <= 1e-9)
                ++BasePlanes;
            else if (std::fabs(Along - RetainedHeight) <= 1e-9)
                ++CapPlanes;
            else return false;
        }
        else return false;
    }
    int BaseVertices = 0, CapVertices = 0;
    for (const BrepVertex& Vertex : Body.Vertices)
    {
        const Vec3 FromBase = Vertex.Point - Specification.Base;
        const double Along = FromBase.Dot(Axis);
        const double Radial = (FromBase - Axis * Along).Length();
        if (std::fabs(Along) <= 1e-9 && std::fabs(Radial - Specification.BaseRadius) <= 1e-9)
            ++BaseVertices;
        else if (std::fabs(Along - RetainedHeight) <= 1e-9 && std::fabs(Radial - CapRadius) <= 1e-9)
            ++CapVertices;
        else return false;
    }
    return Cones == 1 && BasePlanes == 1 && CapPlanes == 1 && BaseVertices == 1 && CapVertices == 1;
}

[[nodiscard]] bool OutwardNormals(const BrepBody& Body, const ConeApexChamferSpecification& Specification) noexcept
{
    const Vec3 Axis = Specification.Axis.Normalised();
    const double Slant = std::hypot(Specification.Height, Specification.BaseRadius);
    const double RetainedHeight = Specification.Height - Specification.SetBack * Specification.Height / Slant;
    const double CapRadius = Specification.SetBack * Specification.BaseRadius / Slant;
    int Cones = 0, BasePlanes = 0, CapPlanes = 0;
    for (size_t I = 0; I < Body.Faces.size(); ++I)
    {
        const NurbsSurface& Surface = Body.Faces[I].Surface;
        const double U = 0.41 * (Surface.DomainEndU() - Surface.DomainStartU()) + Surface.DomainStartU();
        const double V = 0.61 * (Surface.DomainEndV() - Surface.DomainStartV()) + Surface.DomainStartV();
        const Vec3 Position = Surface.Sample(U, V);
        const Vec3 Normal = Body.FaceNormal(static_cast<int>(I), U, V).Normalised();
        if (!std::isfinite(Normal.X) || !std::isfinite(Normal.Y) || !std::isfinite(Normal.Z)) return false;
        if (Surface.Classification == SurfaceClassification::Cone)
        {
            const Vec3 Radial = (Position - Specification.Base - Axis *
                                 (Position - Specification.Base).Dot(Axis)).Normalised();
            const Vec3 Expected = (Radial * RetainedHeight + Axis *
                                   (Specification.BaseRadius - CapRadius)).Normalised();
            if (Normal.Dot(Expected) < 1.0 - 1e-6) return false;
            ++Cones;
        }
        else if (Surface.Classification == SurfaceClassification::Plane)
        {
            const double Along = (Position - Specification.Base).Dot(Axis);
            if (std::fabs(Along) <= 1e-9)
            {
                if (Normal.Dot(Axis) > -1.0 + 1e-6) return false;
                ++BasePlanes;
            }
            else if (std::fabs(Along - RetainedHeight) <= 1e-9)
            {
                if (Normal.Dot(Axis) < 1.0 - 1e-6) return false;
                ++CapPlanes;
            }
            else return false;
        }
        else return false;
    }
    return Cones == 1 && BasePlanes == 1 && CapPlanes == 1;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · bounded native-cone apex vertex chamfer dispatch");
    const Vec3 Base{ 0, 0, 0 };
    const Vec3 Axis{ 0, 0, 1 };
    const double BaseRadius = 5.0;
    const double Height = 7.0;
    const double SetBack = 0.8;
    const double SlantHeight = std::hypot(Height, BaseRadius);
    const double ExpectedRetainedHeight = Height - SetBack * Height / SlantHeight;
    const double ExpectedCapRadius = SetBack * BaseRadius / SlantHeight;
    const Deliver<BrepBody> SourceDeliver = BrepBody::Cone(Base, Axis, BaseRadius, 0.0, Height);
    Panel.Expect("The native right cone is a closed solid", SourceDeliver && SourceDeliver.Payload.Validate().Solid());
    if (SourceDeliver)
        Panel.Expect("The source has canonical V2/E2/C4/L2/F2 topology", SourceDeliver.Payload.Vertices.size() == 2 &&
                     SourceDeliver.Payload.Edges.size() == 2 && SourceDeliver.Payload.Coedges.size() == 4 &&
                     SourceDeliver.Payload.Loops.size() == 2 && SourceDeliver.Payload.Faces.size() == 2);
    const BrepBody Source = SourceDeliver.Payload;
    const BodyReport Before = Source.Validate();
    constexpr int ApexVertex = 1;
    const Deliver<ConeApexChamferSpecification> Candidate =
        BlendSolver::ClassifyConeApexChamferVertex(Source, ApexVertex, SetBack);
    Panel.Expect("One explicit native-cone apex vertex chamfer dispatches", static_cast<bool>(Candidate));
    if (Candidate)
    {
        const ConeApexChamferSpecification& Specification = Candidate.Payload;
        Panel.Within("The chamfer base is extracted exactly", Specification.Base.Distance(Base), 1e-12);
        Panel.Within("The chamfer axis is extracted exactly", Specification.Axis.Distance(Axis), 1e-12);
        Panel.Within("The chamfer base radius is extracted exactly", Specification.BaseRadius - BaseRadius, 1e-12);
        Panel.Within("The chamfer height is extracted exactly", Specification.Height - Height, 1e-12);
        Panel.Within("The requested generator setback is retained", Specification.SetBack - SetBack, 1e-12);
        const Deliver<ConeApexChamferSpecification> Repeat =
            BlendSolver::ClassifyConeApexChamferVertex(Source, ApexVertex, SetBack);
        Panel.Expect("Apex chamfer dispatch is deterministic", Repeat &&
                     Repeat.Payload.Base.Distance(Specification.Base) <= 1e-12 &&
                     Repeat.Payload.Axis.Distance(Specification.Axis) <= 1e-12 &&
                     std::fabs(Repeat.Payload.SetBack - Specification.SetBack) <= 1e-12);
        Panel.Expect("Apex chamfer dispatch leaves the source unchanged", SameSource(Source, Before));
    }

    Panel.Section("Separate exact frustum reconstruction after vertex dispatch");
    const Deliver<BrepBody> Result = Candidate
        ? BlendSolver::ReconstructConeApexChamfer(Candidate.Payload)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no eligible apex chamfer vertex");
    Panel.Expect("The dispatched chamfer specification reconstructs a solid", Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        Panel.Expect("The chamfer result retains V2/E3/C6/L3/F3 topology",
                     Result.Payload.Vertices.size() == 2 && Result.Payload.Edges.size() == 3 &&
                     Result.Payload.Coedges.size() == 6 && Result.Payload.Loops.size() == 3 &&
                     Result.Payload.Faces.size() == 3 && Report.Hulls == 1 && Report.Genus == 0 &&
                     Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0);
        const double ExpectedVolume = ScalarCriteria::Pi * ExpectedRetainedHeight *
            (BaseRadius * BaseRadius + BaseRadius * ExpectedCapRadius + ExpectedCapRadius * ExpectedCapRadius) / 3.0;
        Panel.Expect("The frustum volume follows the exact chamfer identity",
                     ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume));
        Panel.Expect("The result retains the exact retained height and cap radius", ExactFrustumGeometry(Result.Payload, Candidate.Payload));
        Panel.Expect("The dispatched apex chamfer result has outward normals", OutwardNormals(Result.Payload, Candidate.Payload));
        Panel.Expect("Apex chamfer dispatch remains transactional", SameSource(Source, Before));
    }

    Panel.Section("Unsupported vertex, radius, and source refusals");
    Panel.Expect("The base-rim vertex refuses", !BlendSolver::ClassifyConeApexChamferVertex(Source, 0, SetBack));
    Panel.Expect("An out-of-range vertex refuses", !BlendSolver::ClassifyConeApexChamferVertex(Source, 99, SetBack));
    Panel.Expect("Zero, negative, and non-finite setbacks refuse",
                 !BlendSolver::ClassifyConeApexChamferVertex(Source, ApexVertex, 0.0) &&
                 !BlendSolver::ClassifyConeApexChamferVertex(Source, ApexVertex, -0.2) &&
                 !BlendSolver::ClassifyConeApexChamferVertex(Source, ApexVertex, std::numeric_limits<double>::quiet_NaN()));
    Panel.Expect("A setback consuming the cone refuses",
                 !BlendSolver::ClassifyConeApexChamferVertex(Source, ApexVertex, SlantHeight));
    const Deliver<BrepBody> Frustum = BrepBody::Cone(Base, Axis, BaseRadius, 1.0, Height);
    Panel.Expect("A frustum remains outside native-cone apex chamfer dispatch",
                 Frustum && !BlendSolver::ClassifyConeApexChamferVertex(Frustum.Payload, 1, SetBack));
    const Deliver<BrepBody> Cylinder = BrepBody::Cylinder(Base, Axis, BaseRadius, Height);
    Panel.Expect("A cylinder remains outside native-cone apex chamfer dispatch",
                 Cylinder && !BlendSolver::ClassifyConeApexChamferVertex(Cylinder.Payload, 0, SetBack));
    BrepBody NonManifold = Source;
    NonManifold.Edges[0].Coedges.push_back(NonManifold.Edges[0].Coedges.front());
    Panel.Expect("A malformed cone refuses transactionally",
                 !BlendSolver::ClassifyConeApexChamferVertex(NonManifold, ApexVertex, SetBack));
    Panel.Expect("The source remains unchanged after all chamfer refusals", SameSource(Source, Before));

    Panel.Section("Distinct sharp/chamfered apex proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) /
        "Phase38u_ConeApexVertexChamferDispatch.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = SourceDeliver && Result &&
        Host.Document().AddBody("SelectedApexCone", Source.Transformed(Mat4::Translation({ -8, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("ChamferedApexCone", Result.Payload.Transformed(Mat4::Translation({ 8, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 195 -12") && Host.Execute("view fit") &&
        Host.Execute("render Phase38u_ConeApexVertexChamferDispatch");
    Panel.Expect("The sharp/chamfered apex proof render completes", Rendered);
    Panel.Expect("The apex chamfer dispatch proof PNG is visible",
                 std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
