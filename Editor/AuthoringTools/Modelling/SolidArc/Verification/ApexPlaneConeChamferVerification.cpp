#include "Kernel/BlendSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>
#include <filesystem>
#include <string>
using namespace Frontier;
namespace
{
Deliver<BrepBody> ApexBoss(Vec3 Base, Vec3 Axis, double OuterRadius, double ShoulderHeight, double FootRadius, double BossHeight) noexcept
{
    Axis = Axis.Normalised(); const Vec3 Radial = Workplane::FromNormal(Base, Axis).AxisX;
    const Vec3 Shoulder = Base + Axis * ShoulderHeight;
    Deliver<NurbsSurface> Outer = NurbsSurface::Cylinder(Base, Axis, OuterRadius, ShoulderHeight);
    Deliver<NurbsCurve> ShoulderLine = NurbsCurve::Line(Shoulder + Radial * OuterRadius, Shoulder + Radial * FootRadius);
    Deliver<NurbsSurface> ShoulderSurface = ShoulderLine ? NurbsSurface::Revolution(ShoulderLine.Payload, Base, Axis, ScalarCriteria::TwoPi)
        : Deliver<NurbsSurface>::Reject(ShoulderLine.Denial.Reason, ShoulderLine.Denial.Detail);
    Deliver<NurbsSurface> Boss = NurbsSurface::Cone(Shoulder, Axis, FootRadius, 0.0, BossHeight);
    if (!Outer || !ShoulderSurface || !Boss) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "apex fixture construction failed");
    return BrepBody::Sew({ Outer.Payload, ShoulderSurface.Payload, Boss.Payload });
}
[[nodiscard]] int RootEdge(const BrepBody& Body) noexcept
{
    for (int Edge = 0; Edge < static_cast<int>(Body.Edges.size()); ++Edge)
    {
        const BrepEdge& Candidate = Body.Edges[Edge]; if (!Candidate.Closed() || Candidate.Coedges.size() != 2) continue;
        bool Cone = false, Shoulder = false;
        for (int Coedge : Candidate.Coedges)
        {
            const int Face = Body.Coedges[Coedge].Face; if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) continue;
            Cone = Cone || Body.Faces[Face].Surface.Classification == SurfaceClassification::Cone;
            Shoulder = Shoulder || Body.Faces[Face].Surface.Classification == SurfaceClassification::Revolution;
        }
        if (Cone && Shoulder) return Edge;
    }
    return -1;
}
[[nodiscard]] bool SameSource(const BrepBody& Body, const BodyReport& Before) noexcept
{
    const BodyReport After = Body.Validate();
    return After.Vertices == Before.Vertices && After.Edges == Before.Edges && After.Faces == Before.Faces &&
           After.OpenEdges == Before.OpenEdges && After.NonManifoldEdges == Before.NonManifoldEdges &&
           After.MisorientedEdges == Before.MisorientedEdges && std::fabs(After.Volume - Before.Volume) < 1e-9;
}
}
int main()
{
    VerificationPanel Panel("SolidArc · Phase 36m · apex plane–cone root chamfer");
    constexpr double OuterRadius = 8.0, ShoulderHeight = 4.0, FootRadius = 5.0, BossHeight = 6.0, SetBack = 0.5;
    const Vec3 Base{ 0, 0, 0 }, Axis{ 0, 0, 1 };
    const Deliver<BrepBody> SourceResult = ApexBoss(Base, Axis, OuterRadius, ShoulderHeight, FootRadius, BossHeight);
    const BrepBody Source = SourceResult.Payload; const BodyReport Before = Source.Validate(); const int Edge = SourceResult ? RootEdge(Source) : -1;
    Panel.Expect("The native apex source is one V4/E6/F4 solid", SourceResult && Before.Solid() && Before.Hulls == 1 && Before.Genus == 0 &&
                 Source.Vertices.size() == 4 && Source.Edges.size() == 6 && Source.Coedges.size() == 12 && Source.Loops.size() == 4 &&
                 Source.Faces.size() == 4 && Edge >= 0);
    int Applied = 0;
    Deliver<BrepBody> Result = Edge >= 0 ? BlendSolver::ChamferEdges(Source, { Edge }, SetBack, &Applied)
                                         : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "apex root was not found");
    Panel.Expect("The apex plane-cone chamfer commits one selected root", Result && Applied == 1 && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport After = Result.Payload.Validate();
        Panel.Note("result V=%zu E=%zu C=%zu L=%zu F=%zu", Result.Payload.Vertices.size(), Result.Payload.Edges.size(),
                   Result.Payload.Coedges.size(), Result.Payload.Loops.size(), Result.Payload.Faces.size());
        Panel.Expect("The apex result is exact V6/E9/F5 genus-zero topology", After.Hulls == 1 && After.Genus == 0 &&
                     After.OpenEdges == 0 && After.NonManifoldEdges == 0 && After.MisorientedEdges == 0 &&
                     Result.Payload.Vertices.size() == 6 && Result.Payload.Edges.size() == 9 &&
                     Result.Payload.Coedges.size() == 18 && Result.Payload.Loops.size() == 5 && Result.Payload.Faces.size() == 5);
        const double Slant = std::hypot(BossHeight, FootRadius);
        const double Axial = SetBack * BossHeight / Slant;
        const double Contact = FootRadius * (1.0 - Axial / BossHeight);
        const double Expected = ScalarCriteria::Pi * Axial / 3.0 *
            ((FootRadius + SetBack) * (FootRadius + SetBack) + (FootRadius + SetBack) * Contact -
             FootRadius * FootRadius - FootRadius * Contact);
        Panel.Within("The apex wedge follows the exact meridian square-radius integral",
                     std::fabs((After.Volume - Before.Volume) - Expected) / Expected, 2e-3);
        Panel.Expect("The apex source remains immutable", SameSource(Source, Before));
    }
    Panel.Expect("Zero, consuming, and arbitrary curved selections refuse", Edge >= 0 &&
                 !BlendSolver::ChamferEdge(Source, Edge, 0.0) &&
                 !BlendSolver::ChamferEdge(Source, Edge, BossHeight) &&
                 SameSource(Source, Before));
    auto Torus = BrepBody::Torus({ 20, 0, 0 }, Axis, 4.0, 1.0);
    Panel.Expect("A torus remains outside the bounded apex route", Torus && !BlendSolver::ChamferEdge(Torus.Payload, 0, SetBack));

    Panel.Section("Console transaction and visible proof");
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = Edge >= 0 && Host.Document().AddBody("ApexPlaneConeSource", Source).Identity > 0;
    const bool ConsoleCommit = Added && Host.Execute("chamfer ApexPlaneConeSource 0.5 --edges=" + std::to_string(Edge) + " --name=ApexPlaneConeChamfer") &&
        Host.Document().Find("ApexPlaneConeChamfer") && Host.Document().Find("ApexPlaneConeChamfer")->Body.Validate().Solid();
    Panel.Expect("The console commits the bounded apex route", ConsoleCommit);
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase36m_ApexPlaneConeChamfer.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    ConsoleHost ProofHost(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Rendered = SourceResult && Result &&
        ProofHost.Document().AddBody("SharpApexPlaneCone", Source.Transformed(Mat4::Translation({ -11, 0, 0 }))).Identity > 0 &&
        ProofHost.Document().AddBody("ChamferedApexPlaneCone", Result.Payload.Transformed(Mat4::Translation({ 11, 0, 0 }))).Identity > 0 &&
        ProofHost.Execute("view iso") && ProofHost.Execute("view fit") && ProofHost.Execute("render Phase36m_ApexPlaneConeChamfer");
    Panel.Expect("The apex source/result proof render completes", Rendered);
    Panel.Expect("The apex proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
