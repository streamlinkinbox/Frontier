//=============================================================================================================================================
// SolidArc · Stage 4x · bounded unequal-setback unequal-radius bicone apex chamfer
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
struct Fixture
{
    Vec3 Apex{};
    Vec3 Axis{ 0, 0, 1 };
    double LowerRadius = 0.0;
    double UpperRadius = 0.0;
    double LowerHeight = 0.0;
    double UpperHeight = 0.0;
};

[[nodiscard]] Deliver<BrepBody> MakeNativeBicone(const Fixture& F) noexcept
{
    const Vec3 Axis = F.Axis.Normalised();
    const Vec3 Radial = Workplane::FromNormal(F.Apex, Axis).AxisX.Normalised();
    const Vec3 LowerBase = F.Apex - Axis * F.LowerHeight;
    const Vec3 UpperBase = F.Apex + Axis * F.UpperHeight;
    const std::vector<std::pair<Vec3, Vec3>> Edges{
        { LowerBase + Radial * F.LowerRadius, F.Apex },
        { F.Apex, UpperBase + Radial * F.UpperRadius },
        { LowerBase, LowerBase + Radial * F.LowerRadius },
        { UpperBase, UpperBase + Radial * F.UpperRadius }
    };
    std::vector<NurbsSurface> Surfaces;
    for (const auto& [Start, End] : Edges)
    {
        const Deliver<NurbsCurve> Profile = NurbsCurve::Line(Start, End);
        if (!Profile) return Deliver<BrepBody>::Reject(Profile.Denial.Reason, Profile.Denial.Detail);
        const Deliver<NurbsSurface> Surface = NurbsSurface::Revolution(Profile.Payload, F.Apex, Axis,
                                                                         ScalarCriteria::TwoPi);
        if (!Surface) return Deliver<BrepBody>::Reject(Surface.Denial.Reason, Surface.Denial.Detail);
        Surfaces.push_back(Surface.Payload);
    }
    return BrepBody::Sew(Surfaces);
}

[[nodiscard]] bool SameSource(const BrepBody& Body, const BrepBody& Snapshot, const BodyReport& Before) noexcept
{
    const BodyReport After = Body.Validate();
    if (After.Vertices != Before.Vertices || After.Edges != Before.Edges || After.Faces != Before.Faces ||
        After.Loops != Before.Loops || After.OpenEdges != Before.OpenEdges ||
        After.NonManifoldEdges != Before.NonManifoldEdges || After.MisorientedEdges != Before.MisorientedEdges ||
        std::fabs(After.Volume - Before.Volume) > 1e-12 || Body.Vertices.size() != Snapshot.Vertices.size() ||
        Body.Edges.size() != Snapshot.Edges.size() || Body.Coedges.size() != Snapshot.Coedges.size() ||
        Body.Loops.size() != Snapshot.Loops.size() || Body.Faces.size() != Snapshot.Faces.size()) return false;
    for (size_t I = 0; I < Body.Vertices.size(); ++I)
        if (Body.Vertices[I].Point.Distance(Snapshot.Vertices[I].Point) > 1e-12) return false;
    return true;
}

[[nodiscard]] bool OutwardNormals(const BrepBody& Body,
                                  const UnequalConeApexUnequalSetbackChamferSpecification& S) noexcept
{
    const Vec3 Axis = S.Axis.Normalised();
    const Vec3 LowerBase = S.Apex - Axis * S.LowerHeight;
    const Vec3 LowerContact = S.Apex - Axis * S.LowerSetBack;
    const double LowerContactRadius = S.LowerRadius * S.LowerSetBack / S.LowerHeight;
    const double UpperContactRadius = S.UpperRadius * S.UpperSetBack / S.UpperHeight;
    const double BridgeLength = S.LowerSetBack + S.UpperSetBack;
    int Cones = 0, Planes = 0;
    for (size_t I = 0; I < Body.Faces.size(); ++I)
    {
        const NurbsSurface& Surface = Body.Faces[I].Surface;
        const double U = Surface.DomainStartU() + 0.37 * (Surface.DomainEndU() - Surface.DomainStartU());
        const double V = Surface.DomainStartV() + 0.53 * (Surface.DomainEndV() - Surface.DomainStartV());
        const Vec3 P = Surface.Sample(U, V);
        const Vec3 N = Body.FaceNormal(static_cast<int>(I), U, V).Normalised();
        if (N.Length() <= ScalarCriteria::GeometricTolerance) return false;
        Vec3 Expected;
        if (Surface.Classification == SurfaceClassification::Cone)
        {
            ++Cones;
            const Vec3 FromOrigin = P - Surface.Origin;
            const double Along = FromOrigin.Dot(Axis);
            const Vec3 Radial = (FromOrigin - Axis * Along).Normalised();
            const bool Lower = Surface.Origin.Distance(LowerBase) <= 1e-9;
            const bool Bridge = Surface.Origin.Distance(LowerContact) <= 1e-9;
            const double Length = Lower ? S.LowerHeight - S.LowerSetBack
                : (Bridge ? BridgeLength : S.UpperHeight - S.UpperSetBack);
            const double RadiusDelta = Lower ? S.LowerRadius - LowerContactRadius
                : (Bridge ? LowerContactRadius - UpperContactRadius : UpperContactRadius - S.UpperRadius);
            Expected = (Radial * Length + Axis * RadiusDelta).Normalised();
        }
        else if (Surface.Classification == SurfaceClassification::Plane)
        {
            ++Planes;
            Expected = (P - LowerBase).Dot(Axis) < 0.5 * S.LowerHeight ? -Axis : Axis;
        }
        else return false;
        if (N.Dot(Expected) < 1.0 - 2e-6) return false;
    }
    return Cones == 3 && Planes == 2;
}

[[nodiscard]] bool ExactSurfaces(const BrepBody& Body,
                                 const UnequalConeApexUnequalSetbackChamferSpecification& S) noexcept
{
    const Vec3 Axis = S.Axis.Normalised();
    const double LowerContactRadius = S.LowerRadius * S.LowerSetBack / S.LowerHeight;
    const double UpperContactRadius = S.UpperRadius * S.UpperSetBack / S.UpperHeight;
    bool Lower = false, Bridge = false, Upper = false, LowerPlane = false, UpperPlane = false;
    for (const BrepFace& Face : Body.Faces)
    {
        const NurbsSurface& Surface = Face.Surface;
        if (Surface.Classification == SurfaceClassification::Cone &&
            Surface.Origin.Distance(S.Apex - Axis * S.LowerHeight) <= 1e-12 &&
            std::fabs(Surface.RadiusMajor - S.LowerRadius) <= 1e-12 &&
            std::fabs(Surface.RadiusMinor - LowerContactRadius) <= 1e-12) Lower = true;
        else if (Surface.Classification == SurfaceClassification::Cone &&
                 Surface.Origin.Distance(S.Apex - Axis * S.LowerSetBack) <= 1e-12 &&
                 std::fabs(Surface.RadiusMajor - LowerContactRadius) <= 1e-12 &&
                 std::fabs(Surface.RadiusMinor - UpperContactRadius) <= 1e-12) Bridge = true;
        else if (Surface.Classification == SurfaceClassification::Cone &&
                 Surface.Origin.Distance(S.Apex + Axis * S.UpperSetBack) <= 1e-12 &&
                 std::fabs(Surface.RadiusMajor - UpperContactRadius) <= 1e-12 &&
                 std::fabs(Surface.RadiusMinor - S.UpperRadius) <= 1e-12) Upper = true;
        else if (Surface.Classification == SurfaceClassification::Plane &&
                 Surface.Origin.Distance(S.Apex - Axis * S.LowerHeight) <= 1e-12) LowerPlane = true;
        else if (Surface.Classification == SurfaceClassification::Plane &&
                 Surface.Origin.Distance(S.Apex + Axis * S.UpperHeight) <= 1e-12) UpperPlane = true;
    }
    return Lower && Bridge && Upper && LowerPlane && UpperPlane;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · bounded unequal-setback unequal-radius bicone apex chamfer");
    const Fixture F{ { 0, 0, 0 }, { 0, 0, 1 }, 5.0, 2.5, 7.0, 4.0 };
    const double LowerSetBack = 0.8;
    const double UpperSetBack = 1.3;
    const Deliver<BrepBody> SourceDeliver = MakeNativeBicone(F);
    Panel.Expect("The distinct unequal-setback source is constructed", static_cast<bool>(SourceDeliver));
    Panel.Expect("The source retains V5/E6/C12/L4/F4 topology", SourceDeliver &&
                 SourceDeliver.Payload.Vertices.size() == 5 && SourceDeliver.Payload.Edges.size() == 6 &&
                 SourceDeliver.Payload.Coedges.size() == 12 && SourceDeliver.Payload.Loops.size() == 4 &&
                 SourceDeliver.Payload.Faces.size() == 4);
    const BrepBody Source = SourceDeliver.Payload;
    const BrepBody Snapshot = Source;
    const BodyReport Before = Source.Validate();
    Panel.Expect("The source is the two-hull point-contact bicone", Before.Solid() && Before.Hulls == 2 &&
                 Before.Genus == 1 && Before.OpenEdges == 0 && Before.NonManifoldEdges == 0 &&
                 Before.MisorientedEdges == 0);
    constexpr int ApexVertex = 1;
    Panel.Expect("The source exposes the shared apex at the origin", Source.Vertices.size() > ApexVertex &&
                 Source.Vertices[ApexVertex].Point.Distance(F.Apex) <= 1e-12);

    const Deliver<UnequalConeApexUnequalSetbackChamferSpecification> Candidate =
        BlendSolver::ClassifyUnequalConeApexUnequalSetbackChamferVertex(Source, ApexVertex,
                                                                          LowerSetBack, UpperSetBack);
    Panel.Expect("The unequal-setback apex dispatch accepts the selected vertex", static_cast<bool>(Candidate));
    if (Candidate)
    {
        const auto& S = Candidate.Payload;
        Panel.Within("The lower radius is extracted exactly", std::fabs(S.LowerRadius - F.LowerRadius), 1e-12);
        Panel.Within("The upper radius is extracted exactly", std::fabs(S.UpperRadius - F.UpperRadius), 1e-12);
        Panel.Within("The lower height is extracted exactly", std::fabs(S.LowerHeight - F.LowerHeight), 1e-12);
        Panel.Within("The upper height is extracted exactly", std::fabs(S.UpperHeight - F.UpperHeight), 1e-12);
        Panel.Within("The lower set-back is retained independently", std::fabs(S.LowerSetBack - LowerSetBack), 1e-12);
        Panel.Within("The upper set-back is retained independently", std::fabs(S.UpperSetBack - UpperSetBack), 1e-12);
        Panel.Expect("The support set-backs are strictly unequal", std::fabs(S.LowerSetBack - S.UpperSetBack) > 1e-9);
        Panel.Expect("The unequal-setback classification is transactional", SameSource(Source, Snapshot, Before));
        const auto Repeat = BlendSolver::ClassifyUnequalConeApexUnequalSetbackChamferVertex(
            Source, ApexVertex, LowerSetBack, UpperSetBack);
        Panel.Expect("The unequal-setback dispatch is deterministic", Repeat &&
                     Repeat.Payload.Apex.Distance(S.Apex) <= 1e-12 &&
                     Repeat.Payload.Axis.Distance(S.Axis) <= 1e-12 &&
                     std::fabs(Repeat.Payload.LowerSetBack - S.LowerSetBack) <= 1e-12 &&
                     std::fabs(Repeat.Payload.UpperSetBack - S.UpperSetBack) <= 1e-12);
    }

    Panel.Section("Independent lower and upper contact-ring reconstruction");
    const auto Result = Candidate ? BlendSolver::ReconstructUnequalConeApexUnequalSetbackChamfer(Candidate.Payload)
                                  : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no eligible apex");
    Panel.Expect("The unequal-setback reconstruction is a solid", Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        Panel.Expect("The result has V6/E9/C18/L5/F5 topology", Result.Payload.Vertices.size() == 6 &&
                     Result.Payload.Edges.size() == 9 && Result.Payload.Coedges.size() == 18 &&
                     Result.Payload.Loops.size() == 5 && Result.Payload.Faces.size() == 5 &&
                     Report.Hulls == 1 && Report.Genus == 0 && Report.OpenEdges == 0 &&
                     Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0);
        Panel.Expect("The three cones and two caps have outward normals", OutwardNormals(Result.Payload, Candidate.Payload));
        Panel.Expect("The independent contact and support radii are exact", ExactSurfaces(Result.Payload, Candidate.Payload));
        const double QLower = F.LowerRadius * LowerSetBack / F.LowerHeight;
        const double QUpper = F.UpperRadius * UpperSetBack / F.UpperHeight;
        const double Bridge = LowerSetBack + UpperSetBack;
        const double Expected = ScalarCriteria::Pi * (F.LowerHeight - LowerSetBack) *
            (F.LowerRadius * F.LowerRadius + F.LowerRadius * QLower + QLower * QLower) / 3.0 +
            ScalarCriteria::Pi * Bridge * (QLower * QLower + QLower * QUpper + QUpper * QUpper) / 3.0 +
            ScalarCriteria::Pi * (F.UpperHeight - UpperSetBack) *
            (QUpper * QUpper + QUpper * F.UpperRadius + F.UpperRadius * F.UpperRadius) / 3.0;
        Panel.Within("The volume follows the exact three-frustum identity", std::fabs(Report.Volume - Expected), 1e-1);
        Panel.Expect("The separate reconstruction leaves the source unchanged", SameSource(Source, Snapshot, Before));
    }

    Panel.Section("Independent-setback refusal boundaries");
    Panel.Expect("A non-apex vertex refuses", !BlendSolver::ClassifyUnequalConeApexUnequalSetbackChamferVertex(
        Source, 0, LowerSetBack, UpperSetBack));
    Panel.Expect("An out-of-range vertex refuses", !BlendSolver::ClassifyUnequalConeApexUnequalSetbackChamferVertex(
        Source, 99, LowerSetBack, UpperSetBack));
    Panel.Expect("Equal set-backs remain on the previous route", !BlendSolver::ClassifyUnequalConeApexUnequalSetbackChamferVertex(
        Source, ApexVertex, 1.0, 1.0));
    Panel.Expect("Zero, negative, and non-finite set-backs refuse",
                 !BlendSolver::ClassifyUnequalConeApexUnequalSetbackChamferVertex(Source, ApexVertex, 0.0, UpperSetBack) &&
                 !BlendSolver::ClassifyUnequalConeApexUnequalSetbackChamferVertex(Source, ApexVertex, LowerSetBack, -0.2) &&
                 !BlendSolver::ClassifyUnequalConeApexUnequalSetbackChamferVertex(Source, ApexVertex, LowerSetBack,
                                                                                   std::numeric_limits<double>::infinity()));
    Panel.Expect("A consuming lower or upper set-back refuses",
                 !BlendSolver::ClassifyUnequalConeApexUnequalSetbackChamferVertex(Source, ApexVertex, F.LowerHeight, UpperSetBack) &&
                 !BlendSolver::ClassifyUnequalConeApexUnequalSetbackChamferVertex(Source, ApexVertex, LowerSetBack, F.UpperHeight));
    const Fixture EqualRadius{ F.Apex, F.Axis, 5.0, 5.0, F.LowerHeight, F.UpperHeight };
    const auto EqualSource = MakeNativeBicone(EqualRadius);
    Panel.Expect("An equal-radius source remains outside the unequal route", EqualSource &&
                 !BlendSolver::ClassifyUnequalConeApexUnequalSetbackChamferVertex(EqualSource.Payload, ApexVertex,
                                                                                    LowerSetBack, UpperSetBack));
    const auto Cylinder = BrepBody::Cylinder(F.Apex - F.Axis * F.LowerHeight, F.Axis, F.LowerRadius, F.LowerHeight);
    Panel.Expect("A cylinder remains outside independent-setback dispatch", Cylinder &&
                 !BlendSolver::ClassifyUnequalConeApexUnequalSetbackChamferVertex(Cylinder.Payload, 0,
                                                                                    LowerSetBack, UpperSetBack));
    BrepBody Malformed = Source;
    Malformed.Edges[0].Coedges.push_back(Malformed.Edges[0].Coedges.front());
    Panel.Expect("A malformed source refuses transactionally", !BlendSolver::ClassifyUnequalConeApexUnequalSetbackChamferVertex(
        Malformed, ApexVertex, LowerSetBack, UpperSetBack));
    Panel.Expect("All refusal paths preserve the source", SameSource(Source, Snapshot, Before));

    Panel.Section("Distinct sharp-versus-independent-setback proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) /
        "Phase38x_UnequalSetbackBiconeApexChamfer.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = SourceDeliver && Result &&
        Host.Document().AddBody("UnequalSetbackSource", Source.Transformed(Mat4::Translation({ -9, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("UnequalSetbackChamfer", Result.Payload.Transformed(Mat4::Translation({ 9, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 205 -12") && Host.Execute("view fit") &&
        Host.Execute("render Phase38x_UnequalSetbackBiconeApexChamfer");
    Panel.Expect("The independent-setback proof render completes", Rendered);
    Panel.Expect("The independent-setback proof PNG is visible",
                 std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
