//=============================================================================================================================================
// SolidArc · Phase 39e · bounded equal-radius coaxial bicone apex chamfer
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
struct BiconeFixture
{
    Vec3 Apex{};
    Vec3 Axis{ 0, 0, 1 };
    double LowerRadius = 0.0;
    double UpperRadius = 0.0;
    double LowerHeight = 0.0;
    double UpperHeight = 0.0;
};

[[nodiscard]] Deliver<BrepBody> MakeNativeEqualBicone(const BiconeFixture& F,
                                                        double Sweep = ScalarCriteria::TwoPi) noexcept
{
    const Vec3 Axis = F.Axis.Normalised();
    const Vec3 Radial = Workplane::FromNormal(F.Apex, Axis).AxisX.Normalised();
    const Vec3 LowerBase = F.Apex - Axis * F.LowerHeight;
    const Vec3 UpperBase = F.Apex + Axis * F.UpperHeight;
    const Vec3 LowerRim = LowerBase + Radial * F.LowerRadius;
    const Vec3 UpperRim = UpperBase + Radial * F.UpperRadius;
    const std::vector<std::pair<Vec3, Vec3>> ProfileEdges{
        { LowerRim, F.Apex }, { F.Apex, UpperRim },
        { LowerBase, LowerRim }, { UpperBase, UpperRim }
    };
    std::vector<NurbsSurface> Surfaces;
    Surfaces.reserve(ProfileEdges.size());
    for (const auto& [Start, End] : ProfileEdges)
    {
        const Deliver<NurbsCurve> Profile = NurbsCurve::Line(Start, End);
        if (!Profile) return Deliver<BrepBody>::Reject(Profile.Denial.Reason, Profile.Denial.Detail);
        const Deliver<NurbsSurface> Surface = NurbsSurface::Revolution(Profile.Payload, F.Apex, Axis,
                                                                         Sweep);
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

[[nodiscard]] bool OutwardNormals(const BrepBody& Body, const EqualRadiusBiconeApexChamferSpecification& S) noexcept
{
    const Vec3 Axis = S.Axis.Normalised();
    const Vec3 LowerBase = S.Apex - Axis * S.LowerHeight;
    const Vec3 UpperBase = S.Apex + Axis * S.UpperHeight;
    const Vec3 LowerContact = S.Apex - Axis * S.SetBack;
    const double LowerContactRadius = S.Radius * S.SetBack / S.LowerHeight;
    const double UpperContactRadius = S.Radius * S.SetBack / S.UpperHeight;
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
            const Vec3 Origin = Surface.Origin;
            const Vec3 FromOrigin = P - Origin;
            const double Along = FromOrigin.Dot(Axis);
            const Vec3 Radial = (FromOrigin - Axis * Along).Normalised();
            const bool IsLower = Origin.Distance(LowerBase) <= 1e-9;
            const bool IsChamfer = Origin.Distance(LowerContact) <= 1e-9;
            const double Length = IsLower ? S.LowerHeight - S.SetBack
                : (IsChamfer ? 2.0 * S.SetBack : S.UpperHeight - S.SetBack);
            const double RadiusDelta = IsLower ? S.Radius - LowerContactRadius
                : (IsChamfer ? LowerContactRadius - UpperContactRadius : UpperContactRadius - S.Radius);
            Expected = (Radial * Length + Axis * RadiusDelta).Normalised();
        }
        else if (Surface.Classification == SurfaceClassification::Plane)
        {
            ++Planes;
            const double LowerSide = (P - LowerBase).Dot(Axis);
            const double UpperSide = (P - UpperBase).Dot(Axis);
            Expected = LowerSide < 0.5 * S.LowerHeight ? -Axis : Axis;
            if (std::fabs(UpperSide) < std::fabs(LowerSide)) Expected = Axis;
        }
        else return false;
        if (N.Dot(Expected) < 1.0 - 2e-6) return false;
    }
    return Cones == 3 && Planes == 2;
}

[[nodiscard]] bool ExactChamferSurfaces(const BrepBody& Body, const EqualRadiusBiconeApexChamferSpecification& S) noexcept
{
    const double LowerContactRadius = S.Radius * S.SetBack / S.LowerHeight;
    const double UpperContactRadius = S.Radius * S.SetBack / S.UpperHeight;
    bool Lower = false, Chamfer = false, Upper = false, LowerPlane = false, UpperPlane = false;
    const Vec3 Axis = S.Axis.Normalised();
    for (const BrepFace& Face : Body.Faces)
    {
        const NurbsSurface& Surface = Face.Surface;
        if (Surface.Classification == SurfaceClassification::Cone &&
            Surface.Origin.Distance(S.Apex - Axis * S.LowerHeight) <= 1e-12 &&
            std::fabs(Surface.RadiusMajor - S.Radius) <= 1e-12 &&
            std::fabs(Surface.RadiusMinor - LowerContactRadius) <= 1e-12) Lower = true;
        else if (Surface.Classification == SurfaceClassification::Cone &&
                 Surface.Origin.Distance(S.Apex - Axis * S.SetBack) <= 1e-12 &&
                 std::fabs(Surface.RadiusMajor - LowerContactRadius) <= 1e-12 &&
                 std::fabs(Surface.RadiusMinor - UpperContactRadius) <= 1e-12) Chamfer = true;
        else if (Surface.Classification == SurfaceClassification::Cone &&
                 Surface.Origin.Distance(S.Apex + Axis * S.SetBack) <= 1e-12 &&
                 std::fabs(Surface.RadiusMajor - UpperContactRadius) <= 1e-12 &&
                 std::fabs(Surface.RadiusMinor - S.Radius) <= 1e-12) Upper = true;
        else if (Surface.Classification == SurfaceClassification::Plane &&
                 Surface.Origin.Distance(S.Apex - Axis * S.LowerHeight) <= 1e-12) LowerPlane = true;
        else if (Surface.Classification == SurfaceClassification::Plane &&
                 Surface.Origin.Distance(S.Apex + Axis * S.UpperHeight) <= 1e-12) UpperPlane = true;
    }
    return Lower && Chamfer && Upper && LowerPlane && UpperPlane;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · bounded equal-radius coaxial bicone apex chamfer");
    const BiconeFixture Fixture{ { 0, 0, 0 }, { 0, 0, 1 }, 3.25, 3.25, 5.8, 4.9 };
    const double SetBack = 0.7;
    const Deliver<BrepBody> SourceDeliver = MakeNativeEqualBicone(Fixture);
    Panel.Expect("The distinct shared-apex equal-radius source is constructed", static_cast<bool>(SourceDeliver));
    if (SourceDeliver)
        Panel.Expect("The source has canonical V5/E6/C12/L4/F4 topology", SourceDeliver.Payload.Vertices.size() == 5 &&
                     SourceDeliver.Payload.Edges.size() == 6 && SourceDeliver.Payload.Coedges.size() == 12 &&
                     SourceDeliver.Payload.Loops.size() == 4 && SourceDeliver.Payload.Faces.size() == 4);
    const BrepBody Source = SourceDeliver.Payload;
    const BrepBody Snapshot = Source;
    const BodyReport Before = Source.Validate();
    Panel.Expect("The source is two closed equal-radius hulls meeting only at the selected apex",
                 Before.Solid() && Before.Hulls == 2 && Before.Genus == 1 && Before.OpenEdges == 0 &&
                 Before.NonManifoldEdges == 0 && Before.MisorientedEdges == 0);
    constexpr int ApexVertex = 1;
    Panel.Expect("The fixture exposes one geometric shared apex", Source.Vertices.size() > ApexVertex &&
                 Source.Vertices[ApexVertex].Point.Distance(Fixture.Apex) <= 1e-12);

    const Deliver<EqualRadiusBiconeApexChamferSpecification> Candidate =
        BlendSolver::ClassifyEqualRadiusBiconeApexChamferVertex(Source, ApexVertex, SetBack);
    Panel.Expect("The equal-radius apex dispatch accepts the selected vertex", static_cast<bool>(Candidate));
    if (Candidate)
    {
        const EqualRadiusBiconeApexChamferSpecification& S = Candidate.Payload;
        Panel.Within("The shared apex is extracted exactly", S.Apex.Distance(Fixture.Apex), 1e-12);
        Panel.Within("The coaxial direction is normalized exactly", S.Axis.Distance(Fixture.Axis), 1e-12);
        Panel.Within("The equal support radius is extracted exactly", S.Radius - Fixture.LowerRadius, 1e-12);
        Panel.Within("The upper rim matches the equal support radius exactly", Fixture.UpperRadius - Fixture.LowerRadius, 1e-12);
        Panel.Within("The lower support height is extracted exactly", S.LowerHeight - Fixture.LowerHeight, 1e-12);
        Panel.Within("The upper support height is extracted exactly", S.UpperHeight - Fixture.UpperHeight, 1e-12);
        Panel.Within("The equal apex set-back is retained", S.SetBack - SetBack, 1e-12);
        Panel.Expect("The two support radii are exactly equal", std::fabs(Fixture.LowerRadius - Fixture.UpperRadius) <= 1e-12);
        const Deliver<EqualRadiusBiconeApexChamferSpecification> Repeat =
            BlendSolver::ClassifyEqualRadiusBiconeApexChamferVertex(Source, ApexVertex, SetBack);
        Panel.Expect("Equal-radius apex dispatch is deterministic", Repeat &&
                     Repeat.Payload.Apex.Distance(S.Apex) <= 1e-12 &&
                     Repeat.Payload.Axis.Distance(S.Axis) <= 1e-12 &&
                     std::fabs(Repeat.Payload.Radius - S.Radius) <= 1e-12 &&
                     std::fabs(Repeat.Payload.SetBack - S.SetBack) <= 1e-12);
        Panel.Expect("Equal-radius classification leaves the source unchanged", SameSource(Source, Snapshot, Before));
    }

    Panel.Section("Separate three-cone reconstruction with an equal conical apex chamfer");
    const Deliver<BrepBody> Result = Candidate
        ? BlendSolver::ReconstructEqualRadiusBiconeApexChamfer(Candidate.Payload)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no eligible equal-radius apex");
    Panel.Expect("The equal-radius apex chamfer reconstructs one solid", Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        Panel.Expect("The reconstructed chamfer has V6/E9/C18/L5/F5 topology",
                     Result.Payload.Vertices.size() == 6 && Result.Payload.Edges.size() == 9 &&
                     Result.Payload.Coedges.size() == 18 && Result.Payload.Loops.size() == 5 &&
                     Result.Payload.Faces.size() == 5 && Report.Hulls == 1 && Report.Genus == 0 &&
                     Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0);
        Panel.Expect("The three conical supports and two caps have outward normals",
                     OutwardNormals(Result.Payload, Candidate.Payload));
        Panel.Expect("The reconstructed surfaces retain exact equal support and chamfer radii",
                     ExactChamferSurfaces(Result.Payload, Candidate.Payload));
        const double LowerContactRadius = Candidate.Payload.Radius * SetBack / Candidate.Payload.LowerHeight;
        const double UpperContactRadius = Candidate.Payload.Radius * SetBack / Candidate.Payload.UpperHeight;
        const double ExpectedVolume = ScalarCriteria::Pi * (Candidate.Payload.LowerHeight - SetBack) *
            (Candidate.Payload.Radius * Candidate.Payload.Radius + Candidate.Payload.Radius * LowerContactRadius +
             LowerContactRadius * LowerContactRadius) / 3.0 +
            ScalarCriteria::Pi * 2.0 * SetBack *
            (LowerContactRadius * LowerContactRadius + LowerContactRadius * UpperContactRadius +
             UpperContactRadius * UpperContactRadius) / 3.0 +
            ScalarCriteria::Pi * (Candidate.Payload.UpperHeight - SetBack) *
            (UpperContactRadius * UpperContactRadius + UpperContactRadius * Candidate.Payload.Radius +
             Candidate.Payload.Radius * Candidate.Payload.Radius) / 3.0;
        Panel.Within("The bicone chamfer volume follows three exact frusta", std::fabs(Report.Volume - ExpectedVolume), 1e-1);
        Panel.Expect("The separate reconstruction remains transactional", SameSource(Source, Snapshot, Before));
    }

    Panel.Section("Equal-radius route refusal boundaries");
    Panel.Expect("A non-apex source vertex refuses", !BlendSolver::ClassifyEqualRadiusBiconeApexChamferVertex(Source, 0, SetBack));
    Panel.Expect("The opposite rim vertex refuses", !BlendSolver::ClassifyEqualRadiusBiconeApexChamferVertex(Source, 2, SetBack));
    Panel.Expect("An out-of-range vertex refuses", !BlendSolver::ClassifyEqualRadiusBiconeApexChamferVertex(Source, 99, SetBack));
    Panel.Expect("Zero, negative, and non-finite set-backs refuse",
                 !BlendSolver::ClassifyEqualRadiusBiconeApexChamferVertex(Source, ApexVertex, 0.0) &&
                 !BlendSolver::ClassifyEqualRadiusBiconeApexChamferVertex(Source, ApexVertex, -0.2) &&
                 !BlendSolver::ClassifyEqualRadiusBiconeApexChamferVertex(Source, ApexVertex, std::numeric_limits<double>::infinity()));
    Panel.Expect("A set-back consuming either equal support refuses",
                 !BlendSolver::ClassifyEqualRadiusBiconeApexChamferVertex(Source, ApexVertex, Fixture.LowerHeight) &&
                 !BlendSolver::ClassifyEqualRadiusBiconeApexChamferVertex(Source, ApexVertex, Fixture.UpperHeight));
    const BiconeFixture UnequalFixture{ Fixture.Apex, Fixture.Axis, 3.25, 2.6, Fixture.LowerHeight, Fixture.UpperHeight };
    const Deliver<BrepBody> UnequalSource = MakeNativeEqualBicone(UnequalFixture);
    Panel.Expect("An unequal-radius bicone remains outside this equal route",
                 UnequalSource && !BlendSolver::ClassifyEqualRadiusBiconeApexChamferVertex(UnequalSource.Payload, ApexVertex, SetBack));
    const Deliver<NurbsCurve> ConeProfile = NurbsCurve::Polyline({ Vec3{ 0, 0, -Fixture.LowerHeight },
                                                                    Vec3{ Fixture.LowerRadius, 0, -Fixture.LowerHeight },
                                                                    Fixture.Apex }, true);
    const Deliver<BrepBody> CompleteCone = ConeProfile
        ? BrepBody::Revolve(ConeProfile.Payload, Fixture.Apex - Fixture.Axis * Fixture.LowerHeight,
                            Fixture.Axis, ScalarCriteria::TwoPi)
        : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "cone profile is degenerate");
    Panel.Expect("A single complete cone remains outside equal bicone dispatch",
                 CompleteCone && !BlendSolver::ClassifyEqualRadiusBiconeApexChamferVertex(CompleteCone.Payload, 1, SetBack));
    const Deliver<BrepBody> Cylinder = BrepBody::Cylinder(Fixture.Apex - Fixture.Axis * Fixture.LowerHeight,
                                                           Fixture.Axis, Fixture.LowerRadius, Fixture.LowerHeight);
    Panel.Expect("A cylinder remains outside equal bicone dispatch",
                 Cylinder && !BlendSolver::ClassifyEqualRadiusBiconeApexChamferVertex(Cylinder.Payload, 0, SetBack));
    const Deliver<BrepBody> PartialSource = MakeNativeEqualBicone(Fixture, ScalarCriteria::Pi * 0.75);
    Panel.Expect("A partial-sector source remains outside this complete-turn route",
                 PartialSource && !BlendSolver::ClassifyEqualRadiusBiconeApexChamferVertex(
                     PartialSource.Payload, ApexVertex, SetBack));
    BrepBody Malformed = Source;
    Malformed.Edges[0].Coedges.push_back(Malformed.Edges[0].Coedges.front());
    Panel.Expect("A malformed equal shared-apex source refuses transactionally",
                 !BlendSolver::ClassifyEqualRadiusBiconeApexChamferVertex(Malformed, ApexVertex, SetBack));
    Panel.Expect("All equal-route refusals leave the source unchanged", SameSource(Source, Snapshot, Before));

    Panel.Section("Distinct sharp-versus-conical-cap equal-apex proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) /
        "Phase39e_EqualRadiusBiconeApexChamfer.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = SourceDeliver && Result &&
        Host.Document().AddBody("SharedApexEqualSource", Source.Transformed(Mat4::Translation({ -9, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("EqualApexChamfer", Result.Payload.Transformed(Mat4::Translation({ 9, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 205 -12") && Host.Execute("view fit") &&
        Host.Execute("render Phase39e_EqualRadiusBiconeApexChamfer");
    Panel.Expect("The equal-radius apex proof render completes", Rendered);
    Panel.Expect("The equal-radius apex proof PNG is visible",
                 std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
