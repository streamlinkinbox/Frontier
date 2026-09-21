//=============================================================================================================================================
// SolidArc · Phase 36c · first general curved-edge chamfer
//
// Bounded slice: a complete circular edge where a planar annular shoulder meets a native cylindrical boss.
// The edge is not a native cylinder cap. The exact concave chamfer is reconstructed as a conical band between
// the radial shoulder setback and axial boss setback; partial loops, mixed/freeform supports, and arbitrary curved
// edges remain explicit refusals.
//=============================================================================================================================================
#include "Kernel/BlendSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>
#include <filesystem>
#include <string>

using namespace Frontier;

namespace
{
[[nodiscard]] Deliver<BrepBody> SteppedBoss(Vec3 Base, Vec3 Axis, double OuterRadius, double ShoulderHeight,
                                             double BossRadius, double BossHeight) noexcept
{
    Axis = Axis.Normalised();
    Workplane Frame = Workplane::FromNormal(Base, Axis);
    const Vec3 ShoulderCentre = Base + Axis * ShoulderHeight;
    Deliver<NurbsSurface> Outer = NurbsSurface::Cylinder(Base, Axis, OuterRadius, ShoulderHeight);
    Deliver<NurbsCurve> ShoulderLine = NurbsCurve::Line(ShoulderCentre + Frame.AxisX * OuterRadius,
                                                        ShoulderCentre + Frame.AxisX * BossRadius);
    Deliver<NurbsSurface> Shoulder = ShoulderLine
        ? NurbsSurface::Revolution(ShoulderLine.Payload, Base, Axis, ScalarCriteria::TwoPi)
        : Deliver<NurbsSurface>::Reject(ShoulderLine.Denial.Reason, ShoulderLine.Denial.Detail);
    Deliver<NurbsSurface> Boss = NurbsSurface::Cylinder(ShoulderCentre, Axis, BossRadius, BossHeight);
    if (!Outer || !Shoulder || !Boss)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "stepped-boss support is degenerate");
    return BrepBody::Sew({ Outer.Payload, Shoulder.Payload, Boss.Payload });
}

[[nodiscard]] int CurvedRootEdge(const BrepBody& Body) noexcept
{
    for (int Edge = 0; Edge < static_cast<int>(Body.Edges.size()); ++Edge)
    {
        const BrepEdge& E = Body.Edges[Edge];
        if (!E.Closed() || E.Coedges.size() != 2) continue;
        bool HasBossCylinder = false, HasShoulder = false;
        for (int Coedge : E.Coedges)
        {
            const int Face = Body.Coedges[Coedge].Face;
            if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) continue;
            const NurbsSurface& Surface = Body.Faces[Face].Surface;
            HasBossCylinder = HasBossCylinder || (Surface.Classification == SurfaceClassification::Cylinder && Surface.RadiusMajor < 4.0);
            HasShoulder = HasShoulder || (Surface.Classification != SurfaceClassification::Cylinder && Surface.Classification != SurfaceClassification::Plane);
        }
        if (HasBossCylinder && HasShoulder) return Edge;
    }
    return -1;
}

[[nodiscard]] int ConeFace(const BrepBody& Body) noexcept
{
    for (int Face = 0; Face < static_cast<int>(Body.Faces.size()); ++Face)
        if (Body.Faces[Face].Surface.Classification == SurfaceClassification::Cone) return Face;
    return -1;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 36c · general curved-edge chamfer");
    constexpr double OuterRadius = 8.0, ShoulderHeight = 4.0, BossRadius = 3.0, BossHeight = 6.0, SetBack = 0.5;
    const BrepBody Source = SteppedBoss({ 0, 0, 0 }, { 0, 0, 1 }, OuterRadius, ShoulderHeight, BossRadius, BossHeight).Payload;
    const BodyReport SourceReport = Source.Validate();
    const int RootEdge = CurvedRootEdge(Source);
    Panel.Expect("The mixed plane/cylinder fixture is a valid V4/E7/F5 solid", SourceReport.Solid() && SourceReport.Vertices == 4 && SourceReport.Edges == 7 && SourceReport.Faces == 5 && RootEdge >= 0);

    int Applied = 0;
    Deliver<BrepBody> Result = BlendSolver::ChamferEdges(Source, { RootEdge }, SetBack, &Applied);
    Panel.Expect("A complete plane-cylinder curved root chamfer commits transactionally", Result && Result.Payload.Validate().Solid() && Applied == 1);
    if (Result)
    {
        const BodyReport R = Result.Payload.Validate();
        const double AddedWedge = ScalarCriteria::Pi * SetBack * (BossRadius * SetBack + SetBack * SetBack / 3.0);
        Panel.Expect("The curved root heals to V5/E9/F6 with no open or non-manifold edges", R.Vertices == 5 && R.Edges == 9 && R.Faces == 6 && R.OpenEdges == 0 && R.NonManifoldEdges == 0 && R.MisorientedEdges == 0);
        Panel.Within("The concave chamfer has the exact annular-cone wedge volume", std::fabs((R.Volume - SourceReport.Volume) - AddedWedge) / AddedWedge, 2e-3);
        const int ChamferFace = ConeFace(Result.Payload);
        Panel.Expect("The new curved chamfer support remains an analytic cone", ChamferFace >= 0 &&
                     std::fabs(Result.Payload.Faces[ChamferFace].Surface.RadiusMajor - (BossRadius + SetBack)) < 1e-9 &&
                     std::fabs(Result.Payload.Faces[ChamferFace].Surface.RadiusMinor - BossRadius) < 1e-9);
    }
    Panel.Expect("The mixed-support source remains immutable", Source.Validate().Faces == 5 && std::fabs(Source.Validate().Volume - SourceReport.Volume) < 1e-9);

    auto Torus = BrepBody::Torus({ 20, 0, 0 }, { 0, 0, 1 }, 4.0, 1.0);
    Panel.Expect("An arbitrary torus edge remains an explicit refusal", Torus && !BlendSolver::ChamferEdge(Torus.Payload, 0, SetBack));
    Panel.Expect("A zero or consuming root setback refuses transactionally", !BlendSolver::ChamferEdge(Source, RootEdge, 0.0) && !BlendSolver::ChamferEdge(Source, RootEdge, BossHeight));

    Panel.Section("Console transaction and visible proof");
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = Host.Document().AddBody("SteppedBoss", Source).Identity > 0;
    const bool ConsoleCommit = Added && Host.Execute("chamfer SteppedBoss 0.5 --edges=" + std::to_string(RootEdge) + " --name=RootChamfer") &&
                               Host.Document().Find("RootChamfer") && Host.Document().Find("RootChamfer")->Body.Validate().Solid();
    Panel.Expect("The console commits the curved root chamfer", ConsoleCommit);

    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase36c_GeneralCurvedChamfer.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    ConsoleHost ProofHost(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Rendered = Result &&
        ProofHost.Document().AddBody("RootSource", Source.Transformed(Mat4::Translation({ -10, 0, 0 }))).Identity > 0 &&
        ProofHost.Document().AddBody("RootChamfer", Result.Payload.Transformed(Mat4::Translation({ 10, 0, 0 }))).Identity > 0 &&
        ProofHost.Execute("view iso") && ProofHost.Execute("view fit") && ProofHost.Execute("render Phase36c_GeneralCurvedChamfer");
    Panel.Expect("The curved-edge source/result proof render completes", Rendered);
    Panel.Expect("The curved-edge proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
