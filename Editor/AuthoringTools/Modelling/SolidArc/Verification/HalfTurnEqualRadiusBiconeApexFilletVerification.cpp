//=============================================================================================================================================
// SolidArc · Phase 39k · bounded half-turn equal-radius bicone apex toroidal fillet
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
        (Radial.Dot(RadialStart) >= Radial.Dot(RadialEnd) ? StartEdges : EndEdges).push_back(static_cast<int>(I));
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
    Deliver<BrepBody> Body = BrepBody::Sew(Surfaces, ScalarCriteria::MergeTolerance, false);
    if (!Body || !CloseSector(Body.Payload, LowerBase, UpperBase, Radial, F.Sweep,
                               std::max(F.LowerRadius, F.UpperRadius)))
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "half-turn source radial caps failed");
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



[[nodiscard]] bool HasPoint(const BrepBody& Body, Vec3 Point, double Tolerance = 1e-8) noexcept
{
    for (const BrepVertex& Vertex : Body.Vertices) if (Vertex.Point.Distance(Point) <= Tolerance) return true;
    return false;
}

[[nodiscard]] bool OutputNormals(const BrepBody& Body, const HalfTurnEqualRadiusBiconeApexFilletSpecification& S) noexcept
{
    const Vec3 Axis = S.Axis.Normalised();
    const Vec3 LowerBase = S.Apex - Axis * S.LowerHeight;
    const double LowerAngle = std::atan2(S.Radius, S.LowerHeight);
    const double UpperAngle = std::atan2(S.Radius, S.UpperHeight);
    const double SinLower = std::sin(LowerAngle), SinUpper = std::sin(UpperAngle);
    const double CosLower = std::cos(LowerAngle), CosUpper = std::cos(UpperAngle);
    const double SinSum = std::sin(LowerAngle + UpperAngle);
    const double Major = S.FilletRadius * (SinLower + SinUpper) / SinSum;
    const double Offset = S.FilletRadius * (CosUpper - CosLower) / SinSum;
    const double Lt = Major * SinLower - Offset * CosLower, Ut = Major * SinUpper + Offset * CosUpper;
    const double LowerQ = Lt * SinLower, UpperQ = Ut * SinUpper;
    const double LowerDepth = Lt * CosLower, UpperDepth = Ut * CosUpper;
    int Cones = 0, Torus = 0, BasePlanes = 0, RadialPlanes = 0;
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
            const Vec3 FromApex = P - S.Apex;
            const double Along = FromApex.Dot(Axis);
            const Vec3 Radial = (FromApex - Axis * Along).Normalised();
            const bool Lower = Surface.Origin.Distance(LowerBase) <= 1e-8;
            const double Length = Lower ? S.LowerHeight - LowerDepth : S.UpperHeight - UpperDepth;
            const double Delta = Lower ? S.Radius - LowerQ : UpperQ - S.Radius;
            Expected = (Radial * Length + Axis * Delta).Normalised();
        }
        else if (Surface.Classification == SurfaceClassification::Torus)
        {
            ++Torus;
            const Vec3 FromCentre = P - Surface.Origin;
            const double Along = FromCentre.Dot(Axis);
            const Vec3 Ring = FromCentre - Axis * Along;
            if (Ring.Length() <= ScalarCriteria::GeometricTolerance) return false;
            Expected = -(Ring.Normalised() * (Ring.Length() - Surface.RadiusMajor) + Axis * Along).Normalised();
        }
        else if (Surface.Classification == SurfaceClassification::Plane)
        {
            if (std::fabs(N.Dot(Axis)) > 1.0 - 1e-6) ++BasePlanes;
            else ++RadialPlanes;
            Expected = N;
        }
        else return false;
        if (N.Dot(Expected) < 1.0 - 8e-5) return false;
    }
    return Cones == 2 && Torus == 1 && BasePlanes == 2 && RadialPlanes == 2;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · bounded half-turn equal-radius bicone apex toroidal fillet");
    const Fixture F{ { 0, 0, 0 }, { 0, 0, 1 }, 3.6, 3.6, 6.2, 4.7, ScalarCriteria::Pi };
    const double FilletRadius = 0.58;
    const auto SourceDeliver = MakeSource(F);
    Panel.Expect("The distinct capped equal-radius half-turn source is constructed", static_cast<bool>(SourceDeliver));
    Panel.Expect("The source has V7/E11/C22/L6/F6 topology", SourceDeliver && SourceDeliver.Payload.Vertices.size() == 7 &&
                 SourceDeliver.Payload.Edges.size() == 11 && SourceDeliver.Payload.Coedges.size() == 22 &&
                 SourceDeliver.Payload.Loops.size() == 6 && SourceDeliver.Payload.Faces.size() == 6);
    const BrepBody Source = SourceDeliver.Payload;
    const BrepBody Snapshot = Source;
    const BodyReport Before = Source.Validate();
    Panel.Expect("The source is a closed genus-zero equal-radius half-turn bicone", Before.Solid() && Before.Hulls == 1 && Before.Genus == 0 &&
                 Before.OpenEdges == 0 && Before.NonManifoldEdges == 0 && Before.MisorientedEdges == 0);
    constexpr int ApexVertex = 2;
    Panel.Expect("The source exposes the selected apex", Source.Vertices.size() > ApexVertex && Source.Vertices[ApexVertex].Point.Distance(F.Apex) <= 1e-12);
    const auto Candidate = BlendSolver::ClassifyHalfTurnEqualRadiusBiconeApexFilletVertex(Source, ApexVertex, FilletRadius);
    Panel.Expect("The half-turn equal-fillet dispatch accepts the selected vertex", static_cast<bool>(Candidate));
    if (Candidate)
    {
        Panel.Within("The equal support radius is extracted exactly", std::fabs(Candidate.Payload.Radius - F.LowerRadius), 1e-12);
        Panel.Within("The lower height is extracted exactly", std::fabs(Candidate.Payload.LowerHeight - F.LowerHeight), 1e-12);
        Panel.Within("The upper height is extracted exactly", std::fabs(Candidate.Payload.UpperHeight - F.UpperHeight), 1e-12);
        Panel.Within("The exact half-turn sweep is retained", std::fabs(Candidate.Payload.SweepAngle - F.Sweep), 1e-9);
        Panel.Within("The fillet radius is retained", std::fabs(Candidate.Payload.FilletRadius - FilletRadius), 1e-12);
        Panel.Expect("The support radii are exactly equal", std::fabs(F.LowerRadius - F.UpperRadius) <= 1e-12);
        Panel.Expect("The equal half-turn dispatch is transactional", SameSource(Source, Snapshot, Before));
    }
    Panel.Section("Half-turn equal-radius toroidal reconstruction");
    const auto Result = Candidate ? BlendSolver::ReconstructHalfTurnEqualRadiusBiconeApexFillet(Candidate.Payload)
                                  : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no eligible half-turn equal apex");
    Panel.Expect("The half-turn equal toroidal fillet reconstructs one solid", Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        Panel.Expect("The result has V10/E15/C30/L7/F7 topology", Result.Payload.Vertices.size() == 10 && Result.Payload.Edges.size() == 15 &&
                     Result.Payload.Coedges.size() == 30 && Result.Payload.Loops.size() == 7 && Result.Payload.Faces.size() == 7 &&
                     Report.Hulls == 1 && Report.Genus == 0 && Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0);
        int Cones = 0, Tori = 0, Planes = 0;
        for (const BrepFace& Face : Result.Payload.Faces)
        {
            Cones += Face.Surface.Classification == SurfaceClassification::Cone;
            Tori += Face.Surface.Classification == SurfaceClassification::Torus;
            Planes += Face.Surface.Classification == SurfaceClassification::Plane;
        }
        Panel.Expect("The result contains two cones, one torus, and four planes", Cones == 2 && Tori == 1 && Planes == 4);
        Panel.Expect("The cones, torus, base caps, and radial caps have outward normals", OutputNormals(Result.Payload, Candidate.Payload));
        const Vec3 Axis = Candidate.Payload.Axis.Normalised();
        const Vec3 Radial = Workplane::FromNormal(Candidate.Payload.Apex, Axis).AxisX.Normalised();
        const double LowerAngle = std::atan2(Candidate.Payload.Radius, Candidate.Payload.LowerHeight);
        const double UpperAngle = std::atan2(Candidate.Payload.Radius, Candidate.Payload.UpperHeight);
        const double SinLower = std::sin(LowerAngle), SinUpper = std::sin(UpperAngle);
        const double CosLower = std::cos(LowerAngle), CosUpper = std::cos(UpperAngle);
        const double Major = FilletRadius * (SinLower + SinUpper) / std::sin(LowerAngle + UpperAngle);
        const double Offset = FilletRadius * (CosUpper - CosLower) / std::sin(LowerAngle + UpperAngle);
        const double Lt = Major * SinLower - Offset * CosLower, Ut = Major * SinUpper + Offset * CosUpper;
        const double LowerQ = Lt * SinLower, UpperQ = Ut * SinUpper;
        const Vec3 LowerContact = Candidate.Payload.Apex - Axis * (Lt * CosLower) + Radial * LowerQ;
        const Vec3 UpperContact = Candidate.Payload.Apex + Axis * (Ut * CosUpper) + Radial * UpperQ;
        const Vec3 RadialEnd = Radial * std::cos(F.Sweep) + Axis.Cross(Radial) * std::sin(F.Sweep);
        Panel.Expect("The tangent contacts are present at both sector ends", HasPoint(Result.Payload, LowerContact) && HasPoint(Result.Payload, UpperContact) &&
                     HasPoint(Result.Payload, Candidate.Payload.Apex - Axis * (Lt * CosLower) + RadialEnd * LowerQ) &&
                     HasPoint(Result.Payload, Candidate.Payload.Apex + Axis * (Ut * CosUpper) + RadialEnd * UpperQ));
        bool Metadata = false;
        for (const BrepFace& Face : Result.Payload.Faces)
            if (Face.Surface.Classification == SurfaceClassification::Torus)
                Metadata = Face.Surface.Origin.Distance(Candidate.Payload.Apex + Axis * Offset) <= 1e-8 &&
                           std::fabs(Face.Surface.RadiusMajor - Major) <= 1e-8 && std::fabs(Face.Surface.RadiusMinor - FilletRadius) <= 1e-8;
        Panel.Expect("The torus metadata matches the equal tangent solution", Metadata);
        const double LowerDepth = Lt * CosLower, UpperDepth = Ut * CosUpper;
        const double ThetaLower = std::atan2(-LowerDepth - Offset, LowerQ - Major);
        double ThetaUpper = std::atan2(UpperDepth - Offset, UpperQ - Major);
        while (ThetaUpper >= ThetaLower) ThetaUpper -= ScalarCriteria::TwoPi;
        const auto Primitive = [&](double Theta) noexcept
        {
            const double Sine = std::sin(Theta);
            return Major * Major * Sine + Major * FilletRadius * (Theta + std::sin(2.0 * Theta) / 2.0) +
                   FilletRadius * FilletRadius * (Sine - Sine * Sine * Sine / 3.0);
        };
        const double TorusIntegral = FilletRadius * (Primitive(ThetaUpper) - Primitive(ThetaLower));
        const double FullVolume = ScalarCriteria::Pi * (Candidate.Payload.LowerHeight - LowerDepth) *
            (Candidate.Payload.Radius * Candidate.Payload.Radius + Candidate.Payload.Radius * LowerQ + LowerQ * LowerQ) / 3.0 +
            ScalarCriteria::Pi * TorusIntegral + ScalarCriteria::Pi * (Candidate.Payload.UpperHeight - UpperDepth) *
            (UpperQ * UpperQ + UpperQ * Candidate.Payload.Radius + Candidate.Payload.Radius * Candidate.Payload.Radius) / 3.0;
        Panel.Within("The half-turn volume is the sector-scaled equal cone/torus identity",
                     std::fabs(Report.Volume - FullVolume * F.Sweep / ScalarCriteria::TwoPi), 1e-1);
        Panel.Expect("The separate half-turn equal toroidal reconstruction preserves the source", SameSource(Source, Snapshot, Before));
    }
    Panel.Section("Strict half-turn equal-fillet refusal boundaries");
    Panel.Expect("A non-apex vertex refuses", !BlendSolver::ClassifyHalfTurnEqualRadiusBiconeApexFilletVertex(Source, 0, FilletRadius));
    Panel.Expect("An out-of-range vertex refuses", !BlendSolver::ClassifyHalfTurnEqualRadiusBiconeApexFilletVertex(Source, 99, FilletRadius));
    Panel.Expect("Zero, negative, and non-finite radii refuse", !BlendSolver::ClassifyHalfTurnEqualRadiusBiconeApexFilletVertex(Source, ApexVertex, 0.0) &&
                 !BlendSolver::ClassifyHalfTurnEqualRadiusBiconeApexFilletVertex(Source, ApexVertex, -0.2) &&
                 !BlendSolver::ClassifyHalfTurnEqualRadiusBiconeApexFilletVertex(Source, ApexVertex, std::numeric_limits<double>::infinity()));
    if (Candidate)
    {
        auto TooLarge = Candidate.Payload; TooLarge.FilletRadius = 10.0;
        auto FullTurn = Candidate.Payload; FullTurn.SweepAngle = ScalarCriteria::TwoPi;
        Panel.Expect("A consuming fillet radius refuses", !BlendSolver::ReconstructHalfTurnEqualRadiusBiconeApexFillet(TooLarge));
        Panel.Expect("A complete-turn specification refuses", !BlendSolver::ReconstructHalfTurnEqualRadiusBiconeApexFillet(FullTurn));
    }
    Fixture PartialFixture = F;
    PartialFixture.Sweep = 115.0 * ScalarCriteria::Pi / 180.0;
    const auto PartialSource = MakeSource(PartialFixture);
    Panel.Expect("A strict non-half partial sector remains outside the half-turn route", PartialSource &&
                 !BlendSolver::ClassifyHalfTurnEqualRadiusBiconeApexFilletVertex(PartialSource.Payload, ApexVertex, FilletRadius));
    PartialFixture.Sweep = 1.25 * ScalarCriteria::Pi;
    const auto ReflexSource = MakeSource(PartialFixture);
    Panel.Expect("A reflex sector remains outside the half-turn route", ReflexSource &&
                 !BlendSolver::ClassifyHalfTurnEqualRadiusBiconeApexFilletVertex(ReflexSource.Payload, ApexVertex, FilletRadius));
    const Fixture UnequalFixture{ F.Apex, F.Axis, 4.5, 2.7, F.LowerHeight, F.UpperHeight, F.Sweep };
    const auto UnequalSource = MakeSource(UnequalFixture);
    Panel.Expect("An unequal-radius half-turn bicone remains outside this equal route", UnequalSource &&
                 !BlendSolver::ClassifyHalfTurnEqualRadiusBiconeApexFilletVertex(UnequalSource.Payload, ApexVertex, FilletRadius));
    const auto Cylinder = BrepBody::Cylinder(F.Apex - F.Axis * F.LowerHeight, F.Axis, F.LowerRadius, F.LowerHeight);
    Panel.Expect("A cylinder remains outside half-turn equal-apex dispatch", Cylinder &&
                 !BlendSolver::ClassifyHalfTurnEqualRadiusBiconeApexFilletVertex(Cylinder.Payload, 0, FilletRadius));
    BrepBody Malformed = Source; Malformed.Edges[0].Coedges.push_back(Malformed.Edges[0].Coedges.front());
    Panel.Expect("A malformed half-turn source refuses transactionally", !BlendSolver::ClassifyHalfTurnEqualRadiusBiconeApexFilletVertex(Malformed, ApexVertex, FilletRadius));
    Panel.Expect("All refusal paths preserve the source", SameSource(Source, Snapshot, Before));
    Panel.Section("Distinct sharp-versus-half-turn-equal-toroidal proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase39k_HalfTurnEqualRadiusBiconeApexFillet.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = SourceDeliver && Result && Host.Document().AddBody("HalfTurnEqualApexSource", Source.Transformed(Mat4::Translation({ -9, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("HalfTurnEqualApexToroidalFillet", Result.Payload.Transformed(Mat4::Translation({ 9, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") && Host.Execute("view orbit 300 -12") &&
        Host.Execute("view fit") && Host.Execute("render Phase39k_HalfTurnEqualRadiusBiconeApexFillet");
    Panel.Expect("The half-turn equal-fillet proof render completes", Rendered);
    Panel.Expect("The half-turn equal-fillet proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
