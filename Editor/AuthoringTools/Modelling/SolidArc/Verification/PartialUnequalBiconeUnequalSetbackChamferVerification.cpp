//=============================================================================================================================================
// SolidArc · Phase 39f · bounded partial unequal-radius bicone chamfer with independent set-backs
//=============================================================================================================================================
#include "Kernel/BlendSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <algorithm>
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
    double LowerRadius = 0.0, UpperRadius = 0.0;
    double LowerHeight = 0.0, UpperHeight = 0.0;
    double Sweep = 0.0;
};

[[nodiscard]] bool CloseSector(BrepBody& Body, Vec3 AxisStart, Vec3 AxisEnd, Vec3 RadialStart,
                               double Sweep, double RadialExtent) noexcept
{
    const double Tol = ScalarCriteria::MergeTolerance;
    const Vec3 Axis = (AxisEnd - AxisStart).Normalised();
    RadialStart = (RadialStart - Axis * RadialStart.Dot(Axis)).Normalised();
    const Vec3 RadialEnd = RadialStart * std::cos(Sweep) + Axis.Cross(RadialStart) * std::sin(Sweep);
    if (Axis.Length() <= Tol || RadialStart.Length() <= Tol || RadialEnd.Length() <= Tol || RadialExtent <= Tol) return false;
    std::vector<int> StartEdges, EndEdges;
    for (size_t I = 0; I < Body.Edges.size(); ++I)
    {
        if (Body.Edges[I].Coedges.size() != 1) continue;
        const NurbsCurve& Curve = Body.Edges[I].Curve;
        Vec3 Middle = Curve.Sample(0.5 * (Curve.DomainStart() + Curve.DomainEnd()));
        Vec3 Radial = Middle - (AxisStart + Axis * (Middle - AxisStart).Dot(Axis));
        if (Radial.Length() <= Tol) return false;
        Radial = Radial.Normalised();
        (std::fabs(Radial.Dot(RadialStart)) >= std::fabs(Radial.Dot(RadialEnd)) ? StartEdges : EndEdges).push_back(static_cast<int>(I));
    }
    if (StartEdges.empty() || EndEdges.empty()) return false;
    const Deliver<NurbsCurve> AxisCurve = NurbsCurve::Line(AxisStart, AxisEnd);
    if (!AxisCurve) return false;
    const int AxisEdge = Body.AddEdge(AxisCurve.Payload, Tol);
    const int StartVertex = Body.AddVertex(AxisStart, Tol);
    const int EndVertex = Body.AddVertex(AxisEnd, Tol);
    auto AddCap = [&](std::vector<int> Edges, Vec3 Radial) noexcept
    {
        std::vector<std::pair<int, bool>> Path;
        int Current = StartVertex;
        while (Current != EndVertex)
        {
            auto It = std::find_if(Edges.begin(), Edges.end(), [&](int Edge)
            { return Body.Edges[Edge].VertexStart == Current || Body.Edges[Edge].VertexEnd == Current; });
            if (It == Edges.end()) return false;
            const int Edge = *It;
            const bool Reversed = Body.Edges[Edge].VertexEnd == Current;
            Current = Reversed ? Body.Edges[Edge].VertexStart : Body.Edges[Edge].VertexEnd;
            Path.push_back({ Edge, Reversed });
            Edges.erase(It);
        }
        if (!Edges.empty()) return false;
        const double Pad = 0.01 * std::max(RadialExtent, AxisStart.Distance(AxisEnd)) + Tol;
        const Deliver<NurbsSurface> Plane = NurbsSurface::Plane(
            AxisStart - Radial * Pad - Axis * Pad, Radial, Axis,
            RadialExtent + 2.0 * Pad, AxisStart.Distance(AxisEnd) + 2.0 * Pad);
        if (!Plane) return false;
        const int Face = Body.AddFace(std::move(Plane.Payload));
        Body.Faces[Face].Natural = false;
        const int Loop = Body.AddLoop(Face, true);
        for (const auto& [Edge, Reversed] : Path) Body.AddCoedge(Edge, Reversed, Face, Loop);
        // The meridian path already runs from the lower axial centre to the upper one;
        // close the radial cap with the shared axis edge in reverse.
        Body.AddCoedge(AxisEdge, Body.Edges[AxisEdge].VertexStart == StartVertex, Face, Loop);
        return true;
    };
    return AddCap(StartEdges, RadialStart) && AddCap(EndEdges, RadialEnd) && Body.Orient();
}

[[nodiscard]] Deliver<BrepBody> MakeSource(const Fixture& F) noexcept
{
    const Vec3 Axis = F.Axis.Normalised();
    const Vec3 Radial = Workplane::FromNormal(F.Apex, Axis).AxisX.Normalised();
    const Vec3 LowerBase = F.Apex - Axis * F.LowerHeight;
    const Vec3 UpperBase = F.Apex + Axis * F.UpperHeight;
    const std::vector<std::pair<Vec3, Vec3>> Profiles{
        { LowerBase + Radial * F.LowerRadius, F.Apex },
        { F.Apex, UpperBase + Radial * F.UpperRadius },
        { LowerBase, LowerBase + Radial * F.LowerRadius },
        { UpperBase, UpperBase + Radial * F.UpperRadius }
    };
    std::vector<NurbsSurface> Surfaces;
    for (const auto& [Start, End] : Profiles)
    {
        const Deliver<NurbsCurve> Curve = NurbsCurve::Line(Start, End);
        if (!Curve) return Deliver<BrepBody>::Reject(Curve.Denial.Reason, Curve.Denial.Detail);
        const Deliver<NurbsSurface> Surface = NurbsSurface::Revolution(Curve.Payload, F.Apex, Axis, F.Sweep);
        if (!Surface) return Deliver<BrepBody>::Reject(Surface.Denial.Reason, Surface.Denial.Detail);
        Surfaces.push_back(Surface.Payload);
    }
    Deliver<BrepBody> Body = BrepBody::Sew(Surfaces, ScalarCriteria::MergeTolerance, true);
    if (!Body || !CloseSector(Body.Payload, LowerBase, UpperBase, Radial, F.Sweep,
                               std::max(F.LowerRadius, F.UpperRadius)))
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "partial source radial caps failed");
    return Body;
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

[[nodiscard]] bool OutputNormals(
    const BrepBody& Body, const PartialUnequalBiconeUnequalSetbackChamferSpecification& S) noexcept
{
    const Vec3 Axis = S.Axis.Normalised();
    const Vec3 LowerBase = S.Apex - Axis * S.LowerHeight;
    const Vec3 LowerContact = S.Apex - Axis * S.LowerSetBack;
    const double LowerQ = S.LowerRadius * S.LowerSetBack / S.LowerHeight;
    const double UpperQ = S.UpperRadius * S.UpperSetBack / S.UpperHeight;
    const double BridgeLength = S.LowerSetBack + S.UpperSetBack;
    int Cones = 0, BasePlanes = 0, RadialPlanes = 0;
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
            const double Delta = Lower ? S.LowerRadius - LowerQ
                : (Bridge ? LowerQ - UpperQ : UpperQ - S.UpperRadius);
            Expected = (Radial * Length + Axis * Delta).Normalised();
        }
        else if (Surface.Classification == SurfaceClassification::Plane)
        {
            const Vec3 PlaneNormal = N;
            if (std::fabs(PlaneNormal.Dot(Axis)) > 1.0 - 1e-6)
            {
                ++BasePlanes;
                Expected = P.Distance(LowerBase) < P.Distance(S.Apex + Axis * S.UpperHeight) ? -Axis : Axis;
            }
            else { ++RadialPlanes; Expected = PlaneNormal; }
        }
        else return false;
        if (N.Dot(Expected) < 1.0 - 4e-6) return false;
    }
    return Cones == 3 && BasePlanes == 2 && RadialPlanes == 2;
}}

int main()
{
    VerificationPanel Panel("SolidArc · bounded partial unequal-radius bicone unequal-setback chamfer");
    const Fixture F{ { 0, 0, 0 }, { 0, 0, 1 }, 4.8, 2.6, 6.7, 4.3, 110.0 * ScalarCriteria::Pi / 180.0 };
    const double LowerSetBack = 0.75;
    const double UpperSetBack = 1.15;
    const auto SourceDeliver = MakeSource(F);
    Panel.Expect("The distinct capped partial source is constructed", static_cast<bool>(SourceDeliver));
    Panel.Expect("The source has V7/E11/C22/L6/F6 topology", SourceDeliver &&
                 SourceDeliver.Payload.Vertices.size() == 7 && SourceDeliver.Payload.Edges.size() == 11 &&
                 SourceDeliver.Payload.Coedges.size() == 22 && SourceDeliver.Payload.Loops.size() == 6 &&
                 SourceDeliver.Payload.Faces.size() == 6);
    const BrepBody Source = SourceDeliver.Payload;
    const BrepBody Snapshot = Source;
    const BodyReport Before = Source.Validate();
    Panel.Expect("The source is a closed genus-zero partial bicone", Before.Solid() && Before.Hulls == 1 &&
                 Before.Genus == 0 && Before.OpenEdges == 0 && Before.NonManifoldEdges == 0 &&
                 Before.MisorientedEdges == 0);
    constexpr int ApexVertex = 2;
    Panel.Expect("The source exposes the selected apex", Source.Vertices.size() > ApexVertex &&
                 Source.Vertices[ApexVertex].Point.Distance(F.Apex) <= 1e-12);
    const auto Candidate = BlendSolver::ClassifyPartialUnequalBiconeUnequalSetbackChamferVertex(Source, ApexVertex, LowerSetBack, UpperSetBack);
    Panel.Expect("The partial unequal-setback dispatch accepts the selected vertex", static_cast<bool>(Candidate));
    if (Candidate)
    {
        const auto& S = Candidate.Payload;
        Panel.Within("The lower radius is extracted exactly", std::fabs(S.LowerRadius - F.LowerRadius), 1e-12);
        Panel.Within("The upper radius is extracted exactly", std::fabs(S.UpperRadius - F.UpperRadius), 1e-12);
        Panel.Within("The lower height is extracted exactly", std::fabs(S.LowerHeight - F.LowerHeight), 1e-12);
        Panel.Within("The upper height is extracted exactly", std::fabs(S.UpperHeight - F.UpperHeight), 1e-12);
        Panel.Within("The native partial sweep is retained", std::fabs(S.SweepAngle - F.Sweep), 1e-9);
        Panel.Within("The lower set-back is retained independently", std::fabs(S.LowerSetBack - LowerSetBack), 1e-12);
        Panel.Within("The upper set-back is retained independently", std::fabs(S.UpperSetBack - UpperSetBack), 1e-12);
        Panel.Expect("The support radii remain unequal", std::fabs(S.LowerRadius - S.UpperRadius) > 1e-9);
        Panel.Expect("The support set-backs remain unequal", std::fabs(S.LowerSetBack - S.UpperSetBack) > 1e-9);
        Panel.Expect("The partial unequal-setback dispatch is deterministic and transactional",
                     BlendSolver::ClassifyPartialUnequalBiconeUnequalSetbackChamferVertex(
                         Source, ApexVertex, LowerSetBack, UpperSetBack) && SameSource(Source, Snapshot, Before));
    }
    Panel.Section("Partial unequal-setback three-frustum and radial-cap reconstruction");
    const auto Result = Candidate ? BlendSolver::ReconstructPartialUnequalBiconeUnequalSetbackChamfer(Candidate.Payload)
                                  : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no eligible partial apex");
    Panel.Expect("The partial unequal-setback chamfer reconstructs one solid", Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        Panel.Expect("The result has V10/E15/C30/L7/F7 topology", Result.Payload.Vertices.size() == 10 &&
                     Result.Payload.Edges.size() == 15 && Result.Payload.Coedges.size() == 30 &&
                     Result.Payload.Loops.size() == 7 && Result.Payload.Faces.size() == 7 && Report.Hulls == 1 &&
                     Report.Genus == 0 && Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 &&
                     Report.MisorientedEdges == 0);
        Panel.Expect("The three cones, two base caps, and two radial caps have outward normals",
                     OutputNormals(Result.Payload, Candidate.Payload));
        const double LowerQ = Candidate.Payload.LowerRadius * LowerSetBack / Candidate.Payload.LowerHeight;
        const double UpperQ = Candidate.Payload.UpperRadius * UpperSetBack / Candidate.Payload.UpperHeight;
        const double BridgeLength = LowerSetBack + UpperSetBack;
        const double FullVolume = ScalarCriteria::Pi * (Candidate.Payload.LowerHeight - LowerSetBack) *
            (Candidate.Payload.LowerRadius * Candidate.Payload.LowerRadius + Candidate.Payload.LowerRadius * LowerQ + LowerQ * LowerQ) / 3.0 +
            ScalarCriteria::Pi * BridgeLength * (LowerQ * LowerQ + LowerQ * UpperQ + UpperQ * UpperQ) / 3.0 +
            ScalarCriteria::Pi * (Candidate.Payload.UpperHeight - UpperSetBack) *
            (UpperQ * UpperQ + UpperQ * Candidate.Payload.UpperRadius + Candidate.Payload.UpperRadius * Candidate.Payload.UpperRadius) / 3.0;
        Panel.Within("The partial volume is the exact sector-scaled independent-setback identity",
                     std::fabs(Report.Volume - FullVolume * F.Sweep / ScalarCriteria::TwoPi), 1e-1);
        Panel.Expect("The separate partial reconstruction preserves the source", SameSource(Source, Snapshot, Before));
    }
    Panel.Section("Strict partial unequal-setback refusal boundaries");
    Panel.Expect("A non-apex vertex refuses", !BlendSolver::ClassifyPartialUnequalBiconeUnequalSetbackChamferVertex(
        Source, 0, LowerSetBack, UpperSetBack));
    Panel.Expect("An out-of-range vertex refuses", !BlendSolver::ClassifyPartialUnequalBiconeUnequalSetbackChamferVertex(
        Source, 99, LowerSetBack, UpperSetBack));
    Panel.Expect("Zero, negative, and non-finite set-backs refuse",
                 !BlendSolver::ClassifyPartialUnequalBiconeUnequalSetbackChamferVertex(Source, ApexVertex, 0.0, UpperSetBack) &&
                 !BlendSolver::ClassifyPartialUnequalBiconeUnequalSetbackChamferVertex(Source, ApexVertex, LowerSetBack, -0.2) &&
                 !BlendSolver::ClassifyPartialUnequalBiconeUnequalSetbackChamferVertex(Source, ApexVertex, LowerSetBack,
                                                                                         std::numeric_limits<double>::infinity()));
    Panel.Expect("Equal set-backs remain on the single-setback route",
                 !BlendSolver::ClassifyPartialUnequalBiconeUnequalSetbackChamferVertex(Source, ApexVertex, 0.9, 0.9));
    Panel.Expect("A consuming lower or upper set-back refuses",
                 !BlendSolver::ClassifyPartialUnequalBiconeUnequalSetbackChamferVertex(Source, ApexVertex, F.LowerHeight, UpperSetBack) &&
                 !BlendSolver::ClassifyPartialUnequalBiconeUnequalSetbackChamferVertex(Source, ApexVertex, LowerSetBack, F.UpperHeight));
    auto FullCandidate = Candidate.Payload; FullCandidate.SweepAngle = ScalarCriteria::TwoPi;
    Panel.Expect("A complete-turn specification refuses", !BlendSolver::ReconstructPartialUnequalBiconeUnequalSetbackChamfer(FullCandidate));
    const Fixture EqualFixture{ F.Apex, F.Axis, F.LowerRadius, F.LowerRadius, F.LowerHeight, F.UpperHeight, F.Sweep };
    const auto EqualSource = MakeSource(EqualFixture);
    Panel.Expect("An equal-radius partial bicone remains outside this unequal route", EqualSource &&
                 !BlendSolver::ClassifyPartialUnequalBiconeUnequalSetbackChamferVertex(EqualSource.Payload, ApexVertex,
                                                                                        LowerSetBack, UpperSetBack));
    const auto Cylinder = BrepBody::Cylinder(F.Apex - F.Axis * F.LowerHeight, F.Axis, F.LowerRadius, F.LowerHeight);
    Panel.Expect("A cylinder remains outside partial unequal-setback dispatch", Cylinder &&
                 !BlendSolver::ClassifyPartialUnequalBiconeUnequalSetbackChamferVertex(Cylinder.Payload, 0,
                                                                                         LowerSetBack, UpperSetBack));
    BrepBody Malformed = Source; Malformed.Edges[0].Coedges.push_back(Malformed.Edges[0].Coedges.front());
    Panel.Expect("A malformed partial source refuses transactionally", !BlendSolver::ClassifyPartialUnequalBiconeUnequalSetbackChamferVertex(
        Malformed, ApexVertex, LowerSetBack, UpperSetBack));
    Panel.Expect("All refusal paths preserve the source", SameSource(Source, Snapshot, Before));

    Panel.Section("Distinct sharp-versus-independent-setback chamfer proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) /
        "Phase39f_PartialUnequalBiconeUnequalSetbackChamfer.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = SourceDeliver && Result &&
        Host.Document().AddBody("PartialUnequalSetbackApexSource", Source.Transformed(Mat4::Translation({ -9, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("PartialUnequalSetbackApexChamfer", Result.Payload.Transformed(Mat4::Translation({ 9, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        // Keep the camera outside the 100-degree sector so the translated proof bodies
        // avoid the shader's intentional red back-face diagnostic.
        Host.Execute("view orbit 300 -12") && Host.Execute("view fit") &&
        Host.Execute("render Phase39f_PartialUnequalBiconeUnequalSetbackChamfer");
    Panel.Expect("The partial unequal-setback apex proof render completes", Rendered);
    Panel.Expect("The partial unequal-setback apex proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
