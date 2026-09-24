//=============================================================================================================================================
// SolidArc · Phase 39l · bounded half-turn unequal-radius bicone apex chamfer
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
        const bool HalfTurn = std::fabs(std::fabs(Sweep) - ScalarCriteria::Pi) <= ScalarCriteria::SweepTolerance;
        const double AtStart = HalfTurn ? Radial.Dot(RadialStart) : std::fabs(Radial.Dot(RadialStart));
        const double AtEnd = HalfTurn ? Radial.Dot(RadialEnd) : std::fabs(Radial.Dot(RadialEnd));
        (AtStart >= AtEnd ? StartEdges : EndEdges).push_back(static_cast<int>(I));
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

[[nodiscard]] bool OutputNormals(const BrepBody& Body, const HalfTurnUnequalRadiusBiconeApexFilletSpecification& S) noexcept
{
    const Vec3 Axis = S.Axis.Normalised();
    const Vec3 LowerBase = S.Apex - Axis * S.LowerHeight;
    const double LowerAngle = std::atan2(S.LowerRadius, S.LowerHeight);
    const double UpperAngle = std::atan2(S.UpperRadius, S.UpperHeight);
    const double SinLower = std::sin(LowerAngle), SinUpper = std::sin(UpperAngle);
    const double CosLower = std::cos(LowerAngle), CosUpper = std::cos(UpperAngle);
    const double SinSum = std::sin(LowerAngle + UpperAngle);
    const double Major = S.FilletRadius * (SinLower + SinUpper) / SinSum;
    const double CentreOffset = S.FilletRadius * (CosUpper - CosLower) / SinSum;
    const double LowerTangent = Major * SinLower - CentreOffset * CosLower;
    const double UpperTangent = Major * SinUpper + CentreOffset * CosUpper;
    const double LowerQ = LowerTangent * SinLower, UpperQ = UpperTangent * SinUpper;
    const double LowerDepth = LowerTangent * CosLower, UpperDepth = UpperTangent * CosUpper;
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
            const double Delta = Lower ? S.LowerRadius - LowerQ : UpperQ - S.UpperRadius;
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
    VerificationPanel Panel("SolidArc · bounded half-turn unequal-radius bicone apex toroidal fillet");
    const Fixture F{ { 0, 0, 0 }, { 0, 0, 1 }, 4.5, 2.7, 6.8, 4.6, ScalarCriteria::Pi };
    const double FilletRadius = 0.62;
    const auto SourceDeliver = MakeSource(F);
    Panel.Expect("The distinct capped half-turn source is constructed", static_cast<bool>(SourceDeliver));
    Panel.Expect("The source has V7/E11/C22/L6/F6 topology", SourceDeliver &&
                 SourceDeliver.Payload.Vertices.size() == 7 && SourceDeliver.Payload.Edges.size() == 11 &&
                 SourceDeliver.Payload.Coedges.size() == 22 && SourceDeliver.Payload.Loops.size() == 6 && SourceDeliver.Payload.Faces.size() == 6);
    const BrepBody Source = SourceDeliver.Payload;
    const BrepBody Snapshot = Source;
    const BodyReport Before = Source.Validate();
    Panel.Expect("The source is a closed genus-zero half-turn bicone", Before.Solid() && Before.Hulls == 1 && Before.Genus == 0 &&
                 Before.OpenEdges == 0 && Before.NonManifoldEdges == 0 && Before.MisorientedEdges == 0);
    constexpr int ApexVertex = 2;
    Panel.Expect("The source exposes the selected apex", Source.Vertices.size() > ApexVertex && Source.Vertices[ApexVertex].Point.Distance(F.Apex) <= 1e-12);
    const auto Candidate = BlendSolver::ClassifyHalfTurnUnequalRadiusBiconeApexFilletVertex(Source, ApexVertex, FilletRadius);
    Panel.Expect("The half-turn unequal-fillet dispatch accepts the selected vertex", static_cast<bool>(Candidate));
    if (Candidate)
    {
        const auto& S = Candidate.Payload;
        Panel.Within("The lower radius is extracted exactly", std::fabs(S.LowerRadius - F.LowerRadius), 1e-12);
        Panel.Within("The upper radius is extracted exactly", std::fabs(S.UpperRadius - F.UpperRadius), 1e-12);
        Panel.Within("The lower height is extracted exactly", std::fabs(S.LowerHeight - F.LowerHeight), 1e-12);
        Panel.Within("The upper height is extracted exactly", std::fabs(S.UpperHeight - F.UpperHeight), 1e-12);
        Panel.Within("The exact half-turn sweep is retained", std::fabs(S.SweepAngle - F.Sweep), 1e-9);
        Panel.Within("The fillet radius is retained", std::fabs(S.FilletRadius - FilletRadius), 1e-12);
        Panel.Expect("The support radii remain unequal", std::fabs(S.LowerRadius - S.UpperRadius) > 1e-9);
        Panel.Expect("The half-turn unequal-fillet dispatch is transactional", SameSource(Source, Snapshot, Before));
    }
    Panel.Section("Half-turn toroidal reconstruction and tangent contacts");
    const auto Result = Candidate ? BlendSolver::ReconstructHalfTurnUnequalRadiusBiconeApexFillet(Candidate.Payload)
                                  : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no eligible half-turn apex");
    Panel.Expect("The half-turn unequal toroidal fillet reconstructs one solid", Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        Panel.Expect("The result has V10/E15/C30/L7/F7 topology", Result.Payload.Vertices.size() == 10 && Result.Payload.Edges.size() == 15 &&
                     Result.Payload.Coedges.size() == 30 && Result.Payload.Loops.size() == 7 && Result.Payload.Faces.size() == 7 &&
                     Report.Hulls == 1 && Report.Genus == 0 && Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0);
        int ConeFaces = 0, TorusFaces = 0, PlaneFaces = 0;
        for (const BrepFace& Face : Result.Payload.Faces)
        {
            ConeFaces += Face.Surface.Classification == SurfaceClassification::Cone;
            TorusFaces += Face.Surface.Classification == SurfaceClassification::Torus;
            PlaneFaces += Face.Surface.Classification == SurfaceClassification::Plane;
        }
        Panel.Expect("The result contains two cones, one torus, and four planes", ConeFaces == 2 && TorusFaces == 1 && PlaneFaces == 4);
        Panel.Expect("The cones, torus, base caps, and radial caps have outward normals", OutputNormals(Result.Payload, Candidate.Payload));
        const Vec3 Axis = Candidate.Payload.Axis.Normalised();
        const Vec3 Radial = Workplane::FromNormal(Candidate.Payload.Apex, Axis).AxisX.Normalised();
        const double LowerAngle = std::atan2(Candidate.Payload.LowerRadius, Candidate.Payload.LowerHeight);
        const double UpperAngle = std::atan2(Candidate.Payload.UpperRadius, Candidate.Payload.UpperHeight);
        const double SinLower = std::sin(LowerAngle), SinUpper = std::sin(UpperAngle);
        const double CosLower = std::cos(LowerAngle), CosUpper = std::cos(UpperAngle);
        const double Major = FilletRadius * (SinLower + SinUpper) / std::sin(LowerAngle + UpperAngle);
        const double Offset = FilletRadius * (CosUpper - CosLower) / std::sin(LowerAngle + UpperAngle);
        const double Lt = Major * SinLower - Offset * CosLower, Ut = Major * SinUpper + Offset * CosUpper;
        const Vec3 LowerContact = Candidate.Payload.Apex - Axis * (Lt * CosLower) + Radial * (Lt * SinLower);
        const Vec3 UpperContact = Candidate.Payload.Apex + Axis * (Ut * CosUpper) + Radial * (Ut * SinUpper);
        const Vec3 RadialEnd = Radial * std::cos(F.Sweep) + Axis.Cross(Radial) * std::sin(F.Sweep);
        Panel.Expect("The lower and upper tangent contacts are present at both sector ends",
                     HasPoint(Result.Payload, LowerContact) && HasPoint(Result.Payload, UpperContact) &&
                     HasPoint(Result.Payload, Candidate.Payload.Apex - Axis * (Lt * CosLower) + RadialEnd * (Lt * SinLower)) &&
                     HasPoint(Result.Payload, Candidate.Payload.Apex + Axis * (Ut * CosUpper) + RadialEnd * (Ut * SinUpper)));
        bool TorusMetadata = false;
        for (const BrepFace& Face : Result.Payload.Faces)
            if (Face.Surface.Classification == SurfaceClassification::Torus)
                TorusMetadata = Face.Surface.Origin.Distance(Candidate.Payload.Apex + Axis * Offset) <= 1e-8 &&
                                std::fabs(Face.Surface.RadiusMajor - Major) <= 1e-8 &&
                                std::fabs(Face.Surface.RadiusMinor - FilletRadius) <= 1e-8;
        Panel.Expect("The torus metadata matches the tangent solution", TorusMetadata);
        const double LowerQ = Lt * SinLower, UpperQ = Ut * SinUpper;
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
            (Candidate.Payload.LowerRadius * Candidate.Payload.LowerRadius + Candidate.Payload.LowerRadius * LowerQ + LowerQ * LowerQ) / 3.0 +
            ScalarCriteria::Pi * TorusIntegral + ScalarCriteria::Pi * (Candidate.Payload.UpperHeight - UpperDepth) *
            (UpperQ * UpperQ + UpperQ * Candidate.Payload.UpperRadius + Candidate.Payload.UpperRadius * Candidate.Payload.UpperRadius) / 3.0;
        Panel.Within("The half-turn volume is the sector-scaled cone/torus identity",
                     std::fabs(Report.Volume - FullVolume * F.Sweep / ScalarCriteria::TwoPi), 1e-1);
        Panel.Expect("The separate toroidal reconstruction preserves the source", SameSource(Source, Snapshot, Before));
    }
    Panel.Section("Strict half-turn unequal-fillet refusal boundaries");
    Panel.Expect("A non-apex vertex refuses", !BlendSolver::ClassifyHalfTurnUnequalRadiusBiconeApexFilletVertex(Source, 0, FilletRadius));
    Panel.Expect("An out-of-range vertex refuses", !BlendSolver::ClassifyHalfTurnUnequalRadiusBiconeApexFilletVertex(Source, 99, FilletRadius));
    Panel.Expect("Zero, negative, and non-finite radii refuse",
                 !BlendSolver::ClassifyHalfTurnUnequalRadiusBiconeApexFilletVertex(Source, ApexVertex, 0.0) &&
                 !BlendSolver::ClassifyHalfTurnUnequalRadiusBiconeApexFilletVertex(Source, ApexVertex, -0.2) &&
                 !BlendSolver::ClassifyHalfTurnUnequalRadiusBiconeApexFilletVertex(Source, ApexVertex, std::numeric_limits<double>::infinity()));
    if (Candidate)
    {
        auto TooLarge = Candidate.Payload; TooLarge.FilletRadius = 10.0;
        auto FullTurn = Candidate.Payload; FullTurn.SweepAngle = ScalarCriteria::TwoPi;
        Panel.Expect("A consuming fillet radius refuses", !BlendSolver::ReconstructHalfTurnUnequalRadiusBiconeApexFillet(TooLarge));
        Panel.Expect("A complete-turn specification refuses", !BlendSolver::ReconstructHalfTurnUnequalRadiusBiconeApexFillet(FullTurn));
    }
    Fixture PartialFixture = F;
    PartialFixture.Sweep = 115.0 * ScalarCriteria::Pi / 180.0;
    const auto PartialSource = MakeSource(PartialFixture);
    Panel.Expect("A strict non-half partial sector remains outside the half-turn route", PartialSource &&
                 !BlendSolver::ClassifyHalfTurnUnequalRadiusBiconeApexFilletVertex(PartialSource.Payload, ApexVertex, FilletRadius));
    PartialFixture.Sweep = 1.25 * ScalarCriteria::Pi;
    const auto ReflexSource = MakeSource(PartialFixture);
    Panel.Expect("A reflex sector remains outside the half-turn route", ReflexSource &&
                 !BlendSolver::ClassifyHalfTurnUnequalRadiusBiconeApexFilletVertex(ReflexSource.Payload, ApexVertex, FilletRadius));
    const Fixture EqualFixture{ F.Apex, F.Axis, F.LowerRadius, F.LowerRadius, F.LowerHeight, F.UpperHeight, F.Sweep };
    const auto EqualSource = MakeSource(EqualFixture);
    Panel.Expect("An equal-radius half-turn bicone remains outside this unequal route", EqualSource &&
                 !BlendSolver::ClassifyHalfTurnUnequalRadiusBiconeApexFilletVertex(EqualSource.Payload, ApexVertex, FilletRadius));
    const auto Cylinder = BrepBody::Cylinder(F.Apex - F.Axis * F.LowerHeight, F.Axis, F.LowerRadius, F.LowerHeight);
    Panel.Expect("A cylinder remains outside half-turn unequal-apex dispatch", Cylinder &&
                 !BlendSolver::ClassifyHalfTurnUnequalRadiusBiconeApexFilletVertex(Cylinder.Payload, 0, FilletRadius));
    BrepBody Malformed = Source; Malformed.Edges[0].Coedges.push_back(Malformed.Edges[0].Coedges.front());
    Panel.Expect("A malformed half-turn source refuses transactionally", !BlendSolver::ClassifyHalfTurnUnequalRadiusBiconeApexFilletVertex(Malformed, ApexVertex, FilletRadius));
    Panel.Expect("All refusal paths preserve the source", SameSource(Source, Snapshot, Before));
    Panel.Section("Distinct sharp-versus-half-turn-toroidal proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase39l_HalfTurnUnequalRadiusBiconeApexFillet.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = SourceDeliver && Result &&
        Host.Document().AddBody("HalfTurnUnequalApexSource", Source.Transformed(Mat4::Translation({ -9, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("HalfTurnUnequalApexToroidalFillet", Result.Payload.Transformed(Mat4::Translation({ 9, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 300 -12") && Host.Execute("view fit") &&
        Host.Execute("render Phase39l_HalfTurnUnequalRadiusBiconeApexFillet");
    Panel.Expect("The half-turn unequal-fillet proof render completes", Rendered);
    Panel.Expect("The half-turn unequal-fillet proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
