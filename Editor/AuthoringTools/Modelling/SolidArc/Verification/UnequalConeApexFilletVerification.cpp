//=============================================================================================================================================
// SolidArc · Stage 4y · bounded unequal-radius coaxial bicone apex toroidal fillet
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

struct Tangency
{
    double LowerAngle = 0.0, UpperAngle = 0.0;
    double Major = 0.0, CentreOffset = 0.0;
    double LowerTangent = 0.0, UpperTangent = 0.0;
    Vec3 LowerContact{}, UpperContact{}, TubeCentre{};
};

[[nodiscard]] Tangency Derive(const UnequalConeApexFilletSpecification& S) noexcept
{
    const Vec3 Axis = S.Axis.Normalised();
    const Vec3 Radial = Workplane::FromNormal(S.Apex, Axis).AxisX.Normalised();
    Tangency T;
    T.LowerAngle = std::atan2(S.LowerRadius, S.LowerHeight);
    T.UpperAngle = std::atan2(S.UpperRadius, S.UpperHeight);
    const double SinSum = std::sin(T.LowerAngle + T.UpperAngle);
    T.Major = S.FilletRadius * (std::sin(T.LowerAngle) + std::sin(T.UpperAngle)) / SinSum;
    T.CentreOffset = S.FilletRadius * (std::cos(T.UpperAngle) - std::cos(T.LowerAngle)) / SinSum;
    T.LowerTangent = T.Major * std::sin(T.LowerAngle) - T.CentreOffset * std::cos(T.LowerAngle);
    T.UpperTangent = T.Major * std::sin(T.UpperAngle) + T.CentreOffset * std::cos(T.UpperAngle);
    T.LowerContact = S.Apex - Axis * (T.LowerTangent * std::cos(T.LowerAngle)) +
                     Radial * (T.LowerTangent * std::sin(T.LowerAngle));
    T.UpperContact = S.Apex + Axis * (T.UpperTangent * std::cos(T.UpperAngle)) +
                     Radial * (T.UpperTangent * std::sin(T.UpperAngle));
    T.TubeCentre = S.Apex + Axis * T.CentreOffset + Radial * T.Major;
    return T;
}

[[nodiscard]] Deliver<BrepBody> MakeSource(const Fixture& F) noexcept
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
        const Deliver<NurbsCurve> Curve = NurbsCurve::Line(Start, End);
        if (!Curve) return Deliver<BrepBody>::Reject(Curve.Denial.Reason, Curve.Denial.Detail);
        const Deliver<NurbsSurface> Surface = NurbsSurface::Revolution(Curve.Payload, F.Apex, Axis,
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

[[nodiscard]] bool OutwardNormals(const BrepBody& Body, const UnequalConeApexFilletSpecification& S) noexcept
{
    const Vec3 Axis = S.Axis.Normalised();
    const Vec3 LowerBase = S.Apex - Axis * S.LowerHeight;
    const Vec3 UpperBase = S.Apex + Axis * S.UpperHeight;
    const Tangency T = Derive(S);
    const double LowerContactRadius = T.LowerTangent * std::sin(T.LowerAngle);
    const double UpperContactRadius = T.UpperTangent * std::sin(T.UpperAngle);
    int Cones = 0, Tori = 0, Planes = 0;
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
            const Vec3 FromOrigin = P - S.Apex;
            const double Along = FromOrigin.Dot(Axis);
            const Vec3 Radial = (FromOrigin - Axis * Along).Normalised();
            const bool Lower = Surface.Origin.Distance(LowerBase) <= 1e-9;
            const double Length = Lower ? S.LowerHeight - T.LowerTangent * std::cos(T.LowerAngle)
                                        : S.UpperHeight - T.UpperTangent * std::cos(T.UpperAngle);
            const double RadiusDelta = Lower ? S.LowerRadius - LowerContactRadius
                                             : UpperContactRadius - S.UpperRadius;
            Expected = (Radial * Length + Axis * RadiusDelta).Normalised();
        }
        else if (Surface.Classification == SurfaceClassification::Torus)
        {
            ++Tori;
            const Vec3 AxisCentre = Surface.Origin;
            const Vec3 FromCentre = P - AxisCentre;
            const double Along = FromCentre.Dot(Axis);
            const Vec3 RadialPart = FromCentre - Axis * Along;
            const double RadialLength = RadialPart.Length();
            Expected = -(RadialPart.Normalised() * (RadialLength - T.Major) + Axis * (Along)).Normalised();
        }
        else if (Surface.Classification == SurfaceClassification::Plane)
        {
            ++Planes;
            Expected = P.Distance(LowerBase) < P.Distance(UpperBase) ? -Axis : Axis;
        }
        else return false;
        if (N.Dot(Expected) < 1.0 - 3e-6) return false;
    }
    return Cones == 2 && Tori == 1 && Planes == 2;
}

[[nodiscard]] bool ExactMetadata(const BrepBody& Body, const UnequalConeApexFilletSpecification& S) noexcept
{
    const Tangency T = Derive(S);
    const Vec3 Axis = S.Axis.Normalised();
    const Vec3 LowerBase = S.Apex - Axis * S.LowerHeight;
    const Vec3 UpperBase = S.Apex + Axis * S.UpperHeight;
    const double LowerContactRadius = T.LowerTangent * std::sin(T.LowerAngle);
    const double UpperContactRadius = T.UpperTangent * std::sin(T.UpperAngle);
    bool Lower = false, Upper = false, Torus = false, LowerPlane = false, UpperPlane = false;
    for (const BrepFace& Face : Body.Faces)
    {
        const NurbsSurface& Surface = Face.Surface;
        if (Surface.Classification == SurfaceClassification::Cone &&
            Surface.Origin.Distance(LowerBase) <= 1e-12 &&
            std::fabs(Surface.RadiusMajor - S.LowerRadius) <= 1e-12 &&
            std::fabs(Surface.RadiusMinor - LowerContactRadius) <= 1e-12) Lower = true;
        else if (Surface.Classification == SurfaceClassification::Cone &&
                 Surface.Origin.Distance(T.UpperContact) <= 1e-12 &&
                 std::fabs(Surface.RadiusMajor - UpperContactRadius) <= 1e-12 &&
                 std::fabs(Surface.RadiusMinor - S.UpperRadius) <= 1e-12) Upper = true;
        else if (Surface.Classification == SurfaceClassification::Torus &&
                 Surface.Origin.Distance(S.Apex + Axis * T.CentreOffset) <= 1e-12 &&
                 std::fabs(Surface.RadiusMajor - T.Major) <= 1e-12 &&
                 std::fabs(Surface.RadiusMinor - S.FilletRadius) <= 1e-12) Torus = true;
        else if (Surface.Classification == SurfaceClassification::Plane &&
                 Surface.Origin.Distance(LowerBase) <= 1e-12) LowerPlane = true;
        else if (Surface.Classification == SurfaceClassification::Plane &&
                 Surface.Origin.Distance(UpperBase) <= 1e-12) UpperPlane = true;
    }
    return Lower && Upper && Torus && LowerPlane && UpperPlane;
}

[[nodiscard]] bool TangentContacts(const BrepBody& Body, const UnequalConeApexFilletSpecification& S) noexcept
{
    const Tangency T = Derive(S);
    bool Lower = false, Upper = false;
    for (const BrepVertex& Vertex : Body.Vertices)
    {
        Lower = Lower || Vertex.Point.Distance(T.LowerContact) <= 1e-8;
        Upper = Upper || Vertex.Point.Distance(T.UpperContact) <= 1e-8;
    }
    return Lower && Upper && std::fabs(T.TubeCentre.Distance(T.LowerContact) - S.FilletRadius) <= 1e-8 &&
           std::fabs(T.TubeCentre.Distance(T.UpperContact) - S.FilletRadius) <= 1e-8 &&
           T.LowerTangent > 0.0 && T.UpperTangent > 0.0;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · bounded unequal-radius coaxial bicone apex toroidal fillet");
    const Fixture F{ { 0, 0, 0 }, { 0, 0, 1 }, 4.5, 2.75, 6.5, 4.5 };
    const double FilletRadius = 0.75;
    const Deliver<BrepBody> SourceDeliver = MakeSource(F);
    Panel.Expect("The distinct unequal-radius source is constructed", static_cast<bool>(SourceDeliver));
    Panel.Expect("The source has V5/E6/C12/L4/F4 topology", SourceDeliver &&
                 SourceDeliver.Payload.Vertices.size() == 5 && SourceDeliver.Payload.Edges.size() == 6 &&
                 SourceDeliver.Payload.Coedges.size() == 12 && SourceDeliver.Payload.Loops.size() == 4 &&
                 SourceDeliver.Payload.Faces.size() == 4);
    const BrepBody Source = SourceDeliver.Payload;
    const BrepBody Snapshot = Source;
    const BodyReport Before = Source.Validate();
    Panel.Expect("The source is a two-hull point-contact bicone", Before.Solid() && Before.Hulls == 2 &&
                 Before.Genus == 1 && Before.OpenEdges == 0 && Before.NonManifoldEdges == 0 &&
                 Before.MisorientedEdges == 0);
    constexpr int ApexVertex = 1;
    Panel.Expect("The source exposes the selected shared apex", Source.Vertices.size() > ApexVertex &&
                 Source.Vertices[ApexVertex].Point.Distance(F.Apex) <= 1e-12);

    const auto Candidate = BlendSolver::ClassifyUnequalConeApexFilletVertex(Source, ApexVertex, FilletRadius);
    Panel.Expect("The unequal-radius apex fillet dispatch accepts the selected vertex", static_cast<bool>(Candidate));
    if (Candidate)
    {
        const auto& S = Candidate.Payload;
        Panel.Within("The fillet apex is extracted exactly", S.Apex.Distance(F.Apex), 1e-12);
        Panel.Within("The fillet axis is normalized exactly", S.Axis.Distance(F.Axis), 1e-12);
        Panel.Within("The lower radius is extracted exactly", std::fabs(S.LowerRadius - F.LowerRadius), 1e-12);
        Panel.Within("The upper radius is extracted exactly", std::fabs(S.UpperRadius - F.UpperRadius), 1e-12);
        Panel.Within("The lower height is extracted exactly", std::fabs(S.LowerHeight - F.LowerHeight), 1e-12);
        Panel.Within("The upper height is extracted exactly", std::fabs(S.UpperHeight - F.UpperHeight), 1e-12);
        Panel.Within("The toroidal fillet radius is retained", std::fabs(S.FilletRadius - FilletRadius), 1e-12);
        Panel.Expect("The support radii remain unequal", std::fabs(S.LowerRadius - S.UpperRadius) > 1e-9);
        Panel.Expect("The toroidal dispatch is deterministic", BlendSolver::ClassifyUnequalConeApexFilletVertex(
            Source, ApexVertex, FilletRadius) && SameSource(Source, Snapshot, Before));
    }

    Panel.Section("Analytic unequal-cone toroidal apex fillet");
    const auto Result = Candidate ? BlendSolver::ReconstructUnequalConeApexFillet(Candidate.Payload)
                                  : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no eligible unequal apex");
    Panel.Expect("The toroidal apex fillet reconstructs one solid", Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        Panel.Expect("The toroidal result has V6/E9/C18/L5/F5 topology", Result.Payload.Vertices.size() == 6 &&
                     Result.Payload.Edges.size() == 9 && Result.Payload.Coedges.size() == 18 &&
                     Result.Payload.Loops.size() == 5 && Result.Payload.Faces.size() == 5 &&
                     Report.Hulls == 1 && Report.Genus == 0 && Report.OpenEdges == 0 &&
                     Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0);
        Panel.Expect("The two cones, torus, and caps have outward normals", OutwardNormals(Result.Payload, Candidate.Payload));
        Panel.Expect("The torus and unequal support metadata are exact", ExactMetadata(Result.Payload, Candidate.Payload));
        Panel.Expect("The torus contacts both unequal cone generators tangentially", TangentContacts(Result.Payload, Candidate.Payload));
        const Tangency T = Derive(Candidate.Payload);
        const double LowerRim = T.LowerTangent * std::sin(T.LowerAngle);
        const double UpperRim = T.UpperTangent * std::sin(T.UpperAngle);
        const double LowerDepth = T.LowerTangent * std::cos(T.LowerAngle);
        const double UpperDepth = T.UpperTangent * std::cos(T.UpperAngle);
        const double ThetaLower = std::atan2(-LowerDepth - T.CentreOffset, LowerRim - T.Major);
        double ThetaUpper = std::atan2(UpperDepth - T.CentreOffset, UpperRim - T.Major);
        while (ThetaUpper >= ThetaLower) ThetaUpper -= ScalarCriteria::TwoPi;
        const auto Primitive = [&](double Theta)
        {
            const double Sine = std::sin(Theta);
            return T.Major * T.Major * Sine + T.Major * Candidate.Payload.FilletRadius *
                (Theta + std::sin(2.0 * Theta) / 2.0) + Candidate.Payload.FilletRadius * Candidate.Payload.FilletRadius *
                (Sine - Sine * Sine * Sine / 3.0);
        };
        const double MeridionalIntegral = Candidate.Payload.FilletRadius * (Primitive(ThetaUpper) - Primitive(ThetaLower));
        const double Expected = ScalarCriteria::Pi * (Candidate.Payload.LowerHeight - LowerDepth) *
            (Candidate.Payload.LowerRadius * Candidate.Payload.LowerRadius + Candidate.Payload.LowerRadius * LowerRim +
             LowerRim * LowerRim) / 3.0 + ScalarCriteria::Pi * MeridionalIntegral +
            ScalarCriteria::Pi * (Candidate.Payload.UpperHeight - UpperDepth) *
            (UpperRim * UpperRim + UpperRim * Candidate.Payload.UpperRadius +
             Candidate.Payload.UpperRadius * Candidate.Payload.UpperRadius) / 3.0;
        Panel.Within("The volume follows two frusta plus the torus meridian integral",
                     std::fabs(Report.Volume - Expected), 1e-1);
        Panel.Expect("The separate toroidal reconstruction preserves the source", SameSource(Source, Snapshot, Before));
    }

    Panel.Section("Unequal-radius toroidal apex refusal boundaries");
    Panel.Expect("A non-apex vertex refuses", !BlendSolver::ClassifyUnequalConeApexFilletVertex(Source, 0, FilletRadius));
    Panel.Expect("An out-of-range vertex refuses", !BlendSolver::ClassifyUnequalConeApexFilletVertex(Source, 99, FilletRadius));
    Panel.Expect("Zero, negative, and non-finite radii refuse",
                 !BlendSolver::ClassifyUnequalConeApexFilletVertex(Source, ApexVertex, 0.0) &&
                 !BlendSolver::ClassifyUnequalConeApexFilletVertex(Source, ApexVertex, -0.2) &&
                 !BlendSolver::ClassifyUnequalConeApexFilletVertex(Source, ApexVertex, std::numeric_limits<double>::infinity()));
    Panel.Expect("A consuming toroidal radius refuses",
                 !BlendSolver::ClassifyUnequalConeApexFilletVertex(Source, ApexVertex, 100.0));
    const Fixture EqualRadius{ F.Apex, F.Axis, F.LowerRadius, F.LowerRadius, F.LowerHeight, F.UpperHeight };
    const auto EqualSource = MakeSource(EqualRadius);
    Panel.Expect("An equal-radius bicone remains outside this unequal route", EqualSource &&
                 !BlendSolver::ClassifyUnequalConeApexFilletVertex(EqualSource.Payload, ApexVertex, FilletRadius));
    const auto Cylinder = BrepBody::Cylinder(F.Apex - F.Axis * F.LowerHeight, F.Axis, F.LowerRadius, F.LowerHeight);
    Panel.Expect("A cylinder remains outside unequal apex filleting", Cylinder &&
                 !BlendSolver::ClassifyUnequalConeApexFilletVertex(Cylinder.Payload, 0, FilletRadius));
    BrepBody Malformed = Source;
    Malformed.Edges[0].Coedges.push_back(Malformed.Edges[0].Coedges.front());
    Panel.Expect("A malformed source refuses transactionally", !BlendSolver::ClassifyUnequalConeApexFilletVertex(
        Malformed, ApexVertex, FilletRadius));
    Panel.Expect("All refusals preserve the source", SameSource(Source, Snapshot, Before));

    Panel.Section("Distinct sharp-versus-toroidal-cap proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) /
        "Phase38y_UnequalConeApexFillet.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = SourceDeliver && Result &&
        Host.Document().AddBody("UnequalApexFilletSource", Source.Transformed(Mat4::Translation({ -9, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("UnequalApexToroidalFillet", Result.Payload.Transformed(Mat4::Translation({ 9, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 205 -12") && Host.Execute("view fit") &&
        Host.Execute("render Phase38y_UnequalConeApexFillet");
    Panel.Expect("The unequal toroidal apex proof render completes", Rendered);
    Panel.Expect("The unequal toroidal apex proof PNG is visible",
                 std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
