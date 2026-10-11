//=============================================================================================================================================
// SolidArc · Phase 50 · bounded circular-sector-prism face offset
//=============================================================================================================================================
#include "Kernel/FaceEditSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <vector>

using namespace Frontier;

namespace
{
constexpr double Radius = 5.0;
constexpr double Height = 6.0;
constexpr double Offset = 1.5;
constexpr double SectorSweep = ScalarCriteria::HalfPi;

[[nodiscard]] Deliver<BrepBody> SectorPrism(double Sweep = SectorSweep) noexcept
{
    const Vec3 Centre{ 0, 0, 0 }, HighCentre{ 0, 0, Height }, Axis = Vec3::UnitZ();
    const Deliver<NurbsCurve> LowRadial = NurbsCurve::Line(Centre, { Radius, 0, 0 });
    const Deliver<NurbsCurve> HighRadial = NurbsCurve::Line(HighCentre, { Radius, 0, Height });
    const Deliver<NurbsCurve> LowOther = NurbsCurve::Line(Centre, { Radius * std::cos(Sweep), Radius * std::sin(Sweep), 0 });
    const Deliver<NurbsCurve> HighOther = NurbsCurve::Line(HighCentre, { Radius * std::cos(Sweep), Radius * std::sin(Sweep), Height });
    const Deliver<NurbsCurve> LowArc = NurbsCurve::Arc(Centre, Axis, Radius, 0.0, Sweep);
    const Deliver<NurbsCurve> HighArc = NurbsCurve::Arc(HighCentre, Axis, Radius, 0.0, Sweep);
    if (!LowRadial || !HighRadial || !LowOther || !HighOther || !LowArc || !HighArc)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "sector fixture curve construction failed");
    const Deliver<NurbsSurface> LowCap = NurbsSurface::Revolution(LowRadial.Payload, Centre, Axis, Sweep);
    const Deliver<NurbsSurface> HighCap = NurbsSurface::Revolution(HighRadial.Payload, HighCentre, Axis, Sweep);
    const Deliver<NurbsSurface> RadialWall = NurbsSurface::Extrusion(LowRadial.Payload, Axis, Height);
    const Deliver<NurbsSurface> OtherWall = NurbsSurface::Extrusion(LowOther.Payload, Axis, Height);
    const Deliver<NurbsSurface> ArcWall = NurbsSurface::Extrusion(LowArc.Payload, Axis, Height);
    if (!LowCap || !HighCap || !RadialWall || !OtherWall || !ArcWall)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "sector fixture surface construction failed");
    return BrepBody::Sew({ LowCap.Payload, HighCap.Payload, RadialWall.Payload, OtherWall.Payload, ArcWall.Payload },
                          ScalarCriteria::MergeTolerance, true);
}

[[nodiscard]] int FaceToward(const BrepBody& Body, Vec3 Direction) noexcept
{
    int Best = -1; double BestDot = -2.0;
    for (int I = 0; I < static_cast<int>(Body.Faces.size()); ++I)
    {
        const BrepFace& F = Body.Faces[I];
        const Vec3 N = Body.FaceNormal(I, 0.5 * (F.Surface.DomainStartU() + F.Surface.DomainEndU()),
                                       0.5 * (F.Surface.DomainStartV() + F.Surface.DomainEndV())).Normalised();
        const double D = N.Dot(Direction.Normalised());
        if (D > BestDot) { BestDot = D; Best = I; }
    }
    return Best;
}

[[nodiscard]] bool SameSource(const BrepBody& Body, const BrepBody& Snapshot, const BodyReport& Before) noexcept
{
    const BodyReport After = Body.Validate();
    return After.Vertices == Before.Vertices && After.Edges == Before.Edges && After.Faces == Before.Faces &&
           After.Loops == Before.Loops && After.Genus == Before.Genus && After.OpenEdges == Before.OpenEdges &&
           After.NonManifoldEdges == Before.NonManifoldEdges && After.MisorientedEdges == Before.MisorientedEdges &&
           std::fabs(After.Volume - Before.Volume) <= 1e-12 && Body.Vertices.size() == Snapshot.Vertices.size() &&
           Body.Edges.size() == Snapshot.Edges.size() && Body.Coedges.size() == Snapshot.Coedges.size() &&
           Body.Loops.size() == Snapshot.Loops.size() && Body.Faces.size() == Snapshot.Faces.size();
}

[[nodiscard]] bool IsSectorCap(const BrepBody& Body, int Face, bool Upper) noexcept
{
    if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return false;
    const BrepFace& Cap = Body.Faces[Face];
    if (Cap.Surface.Classification != SurfaceClassification::Revolution || Cap.Loops.size() != 1) return false;
    const Vec3 N = Body.FaceNormal(Face, 0.5 * (Cap.Surface.DomainStartU() + Cap.Surface.DomainEndU()),
                                   0.5 * (Cap.Surface.DomainStartV() + Cap.Surface.DomainEndV())).Normalised();
    if ((Upper && N.Dot(Vec3::UnitZ()) < 1.0 - 1e-6) || (!Upper && N.Dot(-Vec3::UnitZ()) < 1.0 - 1e-6)) return false;
    const int Loop = Cap.Loops.front();
    if (Loop < 0 || Loop >= static_cast<int>(Body.Loops.size()) || Body.Loops[Loop].Coedges.size() != 3) return false;
    int Lines = 0, Arcs = 0;
    for (int Coedge : Body.Loops[Loop].Coedges)
    {
        if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return false;
        const int EdgeIndex = Body.Coedges[Coedge].Edge;
        if (EdgeIndex < 0 || EdgeIndex >= static_cast<int>(Body.Edges.size())) return false;
        const BrepEdge& E = Body.Edges[EdgeIndex];
        if (E.Curve.Classification == CurveClassification::Line && E.Curve.Degree == 1) ++Lines;
        else if (E.Curve.Classification == CurveClassification::Arc && E.Curve.Degree == 2 && E.Curve.Rational() && !E.Curve.Closed()) ++Arcs;
        else return false;
    }
    return Lines == 2 && Arcs == 1;
}

[[nodiscard]] bool HasSupports(const BrepBody& Body) noexcept
{
    int Revolutions = 0, Extrusions = 0;
    for (const BrepFace& F : Body.Faces)
    {
        if (F.Surface.Classification == SurfaceClassification::Revolution) ++Revolutions;
        else if (F.Surface.Classification == SurfaceClassification::Extrusion) ++Extrusions;
        else return false;
    }
    return Revolutions == 2 && Extrusions == 3;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 50 · bounded circular-sector-prism face offset");
    const auto SourceDeliver = SectorPrism();
    Panel.Expect("The exact quarter-sector prism is constructed", static_cast<bool>(SourceDeliver));
    const BrepBody Source = SourceDeliver.Payload;
    const BrepBody Snapshot = Source;
    const BodyReport Before = Source.Validate();
    const int Top = FaceToward(Source, Vec3::UnitZ());
    const int Bottom = FaceToward(Source, -Vec3::UnitZ());
    const int Side = FaceToward(Source, Vec3::UnitX());
    Panel.Expect("The source has closed genus-zero V6/E9/C18/L5/F5 topology", SourceDeliver && Before.Solid() &&
                 Before.Hulls == 1 && Before.Genus == 0 && Source.Vertices.size() == 6 && Source.Edges.size() == 9 &&
                 Source.Coedges.size() == 18 && Source.Loops.size() == 5 && Source.Faces.size() == 5);
    Panel.Expect("Both planar sector caps contain two radial lines and one rational arc", IsSectorCap(Source, Top, true) &&
                 IsSectorCap(Source, Bottom, false));
    Panel.Expect("The selected cap and side face are distinct", Top >= 0 && Bottom >= 0 && Side >= 0 && Top != Bottom && Top != Side);
    Panel.Expect("The source supports are two revolved caps and three extruded walls", HasSupports(Source));

    Panel.Section("Exact curved-profile face offset reconstruction");
    const auto Result = SourceDeliver ? FaceEditSolver::OffsetExtrudedCircularSectorPrism(Source, Top, Offset)
                                      : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no sector source");
    Panel.Expect("The selected upper sector cap produces a closed solid", Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport R = Result.Payload.Validate();
        Panel.Expect("The result retains V6/E9/C18/L5/F5 topology", R.Hulls == 1 && R.Genus == 0 &&
                     Result.Payload.Vertices.size() == 6 && Result.Payload.Edges.size() == 9 &&
                     Result.Payload.Coedges.size() == 18 && Result.Payload.Loops.size() == 5 && Result.Payload.Faces.size() == 5 &&
                     R.OpenEdges == 0 && R.NonManifoldEdges == 0 && R.MisorientedEdges == 0);
        Panel.Within("The offset volume follows quarter-circle sector area times extended height", std::fabs(R.Volume - ScalarCriteria::Pi * Radius * Radius * 0.25 * (Height + Offset)), 1e-1);
        Panel.Expect("The result preserves both analytic sector caps", IsSectorCap(Result.Payload, FaceToward(Result.Payload, Vec3::UnitZ()), true) &&
                     IsSectorCap(Result.Payload, FaceToward(Result.Payload, -Vec3::UnitZ()), false));
        Panel.Expect("The result preserves two radial walls and one arc wall", HasSupports(Result.Payload));
        Panel.Expect("The separate reconstruction preserves the source", SameSource(Source, Snapshot, Before));
    }
    const auto Dispatch = FaceEditSolver::OffsetFace(Source, Top, Offset);
    Panel.Expect("The public offset dispatcher reaches the sector route", Dispatch && Dispatch.Payload.Validate().Solid());
    Panel.Expect("Only the upper sector cap is supported", !FaceEditSolver::OffsetExtrudedCircularSectorPrism(Source, Bottom, Offset) &&
                 !FaceEditSolver::OffsetExtrudedCircularSectorPrism(Source, Side, Offset));

    Panel.Section("Circular-sector offset refusal boundaries");
    const auto Box = BrepBody::Box({ -5, -5, 0 }, { 5, 5, Height });
    Panel.Expect("A box remains outside the curved-sector API", Box && !FaceEditSolver::OffsetExtrudedCircularSectorPrism(Box.Payload, FaceToward(Box.Payload, Vec3::UnitZ()), Offset));
    const auto Triangle = [&]() { const auto P = NurbsCurve::Polyline({ { 0, 0, 0 }, { 5, 0, 0 }, { 0, 5, 0 } }, true); return P ? BrepBody::Extrude(P.Payload, Vec3::UnitZ(), Height) : Deliver<BrepBody>::Reject(P.Denial.Reason, P.Denial.Detail); }();
    Panel.Expect("A polygonal triangular prism refuses", Triangle && !FaceEditSolver::OffsetExtrudedCircularSectorPrism(Triangle.Payload, FaceToward(Triangle.Payload, Vec3::UnitZ()), Offset));
    const auto Ellipse = [&]() { const auto P = NurbsCurve::Ellipse({ 0, 0, 0 }, Vec3::UnitZ(), Vec3::UnitX(), 5.0, 3.0); return P ? BrepBody::Extrude(P.Payload, Vec3::UnitZ(), Height) : Deliver<BrepBody>::Reject(P.Denial.Reason, P.Denial.Detail); }();
    Panel.Expect("A full ellipse prism refuses", Ellipse && !FaceEditSolver::OffsetExtrudedCircularSectorPrism(Ellipse.Payload, FaceToward(Ellipse.Payload, Vec3::UnitZ()), Offset));
    const auto Cylinder = BrepBody::Cylinder({ 0, 0, 0 }, Vec3::UnitZ(), Radius, Height);
    Panel.Expect("A full cylinder refuses", Cylinder && !FaceEditSolver::OffsetExtrudedCircularSectorPrism(Cylinder.Payload, FaceToward(Cylinder.Payload, Vec3::UnitZ()), Offset));
    const auto NonQuarter = SectorPrism(ScalarCriteria::Pi / 3.0);
    Panel.Expect("A non-quarter circular sector refuses", NonQuarter && !FaceEditSolver::OffsetExtrudedCircularSectorPrism(NonQuarter.Payload, FaceToward(NonQuarter.Payload, Vec3::UnitZ()), Offset));
    Panel.Expect("Zero, negative, and non-finite offsets refuse", !FaceEditSolver::OffsetExtrudedCircularSectorPrism(Source, Top, 0.0) &&
                 !FaceEditSolver::OffsetExtrudedCircularSectorPrism(Source, Top, -0.1) &&
                 !FaceEditSolver::OffsetExtrudedCircularSectorPrism(Source, Top, std::numeric_limits<double>::infinity()));
    BrepBody Malformed = Source;
    Malformed.Edges[0].Coedges.push_back(Malformed.Edges[0].Coedges.front());
    Panel.Expect("A malformed sector prism refuses transactionally", !FaceEditSolver::OffsetExtrudedCircularSectorPrism(Malformed, Top, Offset));
    Panel.Expect("All refusal paths preserve the source", SameSource(Source, Snapshot, Before));

    Panel.Section("Distinct circular-sector proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) /
        "Phase50_CircularSectorPrismFaceOffset.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = SourceDeliver && Result &&
        Host.Document().AddBody("SharpSectorPrism", Source.Transformed(Mat4::Translation({ -8, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("OffsetSectorPrism", Result.Payload.Transformed(Mat4::Translation({ 8, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 35 -18") && Host.Execute("view fit") &&
        Host.Execute("render Phase50_CircularSectorPrismFaceOffset");
    Panel.Expect("The circular-sector offset proof render completes", Rendered);
    Panel.Expect("The circular-sector offset proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
