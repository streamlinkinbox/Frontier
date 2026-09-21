//=============================================================================================================================================
// SolidArc · Phase 36f · bounded complete plane–cone boss-root chamfer
//
// This is the first Set 3 mixed-support chamfer slice. It is intentionally narrower than a general curved-edge
// solver: a complete circular root where a planar annular shoulder meets a native coaxial conical frustum. The route
// reconstructs exact cylinder/revolution/cone supports and refuses arbitrary, partial, or malformed curved geometry.
//=============================================================================================================================================
#include "Kernel/BlendSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"

#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace Frontier;

namespace
{
[[nodiscard]] Deliver<BrepBody> TaperedBoss(Vec3 Base, Vec3 Axis, double OuterRadius, double ShoulderHeight,
                                            double FootRadius, double TopRadius, double BossHeight) noexcept
{
    Axis = Axis.Normalised();
    const Workplane Frame = Workplane::FromNormal(Base, Axis);
    const Vec3 ShoulderCentre = Base + Axis * ShoulderHeight;
    Deliver<NurbsSurface> Outer = NurbsSurface::Cylinder(Base, Axis, OuterRadius, ShoulderHeight);
    Deliver<NurbsCurve> ShoulderLine = NurbsCurve::Line(ShoulderCentre + Frame.AxisX * OuterRadius,
                                                        ShoulderCentre + Frame.AxisX * FootRadius);
    Deliver<NurbsSurface> Shoulder = ShoulderLine
        ? NurbsSurface::Revolution(ShoulderLine.Payload, Base, Axis, ScalarCriteria::TwoPi)
        : Deliver<NurbsSurface>::Reject(ShoulderLine.Denial.Reason, ShoulderLine.Denial.Detail);
    Deliver<NurbsSurface> Boss = NurbsSurface::Cone(ShoulderCentre, Axis, FootRadius, TopRadius, BossHeight);
    if (!Outer || !Shoulder || !Boss)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "plane-cone fixture is degenerate");
    return BrepBody::Sew({ Outer.Payload, Shoulder.Payload, Boss.Payload });
}

[[nodiscard]] int RootEdge(const BrepBody& Body) noexcept
{
    for (int Edge = 0; Edge < static_cast<int>(Body.Edges.size()); ++Edge)
    {
        const BrepEdge& Candidate = Body.Edges[Edge];
        if (!Candidate.Closed() || Candidate.Coedges.size() != 2) continue;
        bool Cone = false, Shoulder = false;
        for (int Coedge : Candidate.Coedges)
        {
            if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) continue;
            const int Face = Body.Coedges[Coedge].Face;
            if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) continue;
            const SurfaceClassification Class = Body.Faces[Face].Surface.Classification;
            Cone = Cone || Class == SurfaceClassification::Cone;
            Shoulder = Shoulder || (Class != SurfaceClassification::Cone && Class != SurfaceClassification::Cylinder &&
                                    Class != SurfaceClassification::Plane);
        }
        if (Cone && Shoulder) return Edge;
    }
    return -1;
}

[[nodiscard]] bool SameSource(const BrepBody& Body, const BodyReport& Before) noexcept
{
    const BodyReport After = Body.Validate();
    if (After.Vertices != Before.Vertices || After.Edges != Before.Edges || After.Faces != Before.Faces ||
        After.OpenEdges != Before.OpenEdges || After.NonManifoldEdges != Before.NonManifoldEdges ||
        After.MisorientedEdges != Before.MisorientedEdges || std::fabs(After.Volume - Before.Volume) > 1e-9)
        return false;
    return true;
}

[[nodiscard]] double AddedWedge(double FootRadius, double TopRadius, double BossHeight, double SetBack) noexcept
{
    const double Slant = std::hypot(BossHeight, TopRadius - FootRadius);
    const double Axial = SetBack * BossHeight / Slant;
    const double Contact = FootRadius + (TopRadius - FootRadius) * Axial / BossHeight;
    const double NewRoot = FootRadius + SetBack;
    // The revolved meridian difference is the square-radius integral of the new straight band minus the old cone.
    return ScalarCriteria::Pi * Axial / 3.0 *
        (NewRoot * NewRoot + NewRoot * Contact + Contact * Contact -
         FootRadius * FootRadius - FootRadius * Contact - Contact * Contact);
}

struct Fixture
{
    double OuterRadius = 0.0, ShoulderHeight = 0.0, FootRadius = 0.0, TopRadius = 0.0, BossHeight = 0.0;
    double SetBack = 0.0;
    Deliver<BrepBody> Source = Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "not built");
    int Edge = -1;

    Fixture(double Outer, double Shoulder, double Foot, double Top, double Height, double Back) noexcept
        : OuterRadius(Outer), ShoulderHeight(Shoulder), FootRadius(Foot), TopRadius(Top), BossHeight(Height), SetBack(Back)
    {
        Source = TaperedBoss({ 0, 0, 0 }, { 0, 0, 1 }, OuterRadius, ShoulderHeight,
                              FootRadius, TopRadius, BossHeight);
        if (Source) Edge = RootEdge(Source.Payload);
    }
};

[[nodiscard]] bool HasConeBand(const BrepBody& Body, double A, double B) noexcept
{
    for (const BrepFace& Face : Body.Faces)
    {
        if (Face.Surface.Classification != SurfaceClassification::Cone) continue;
        const bool First = std::fabs(Face.Surface.RadiusMajor - A) < 1e-9 &&
                           std::fabs(Face.Surface.RadiusMinor - B) < 1e-9;
        const bool Reversed = std::fabs(Face.Surface.RadiusMajor - B) < 1e-9 &&
                              std::fabs(Face.Surface.RadiusMinor - A) < 1e-9;
        if (First || Reversed) return true;
    }
    return false;
}

[[nodiscard]] bool ExactSupportSet(const BrepBody& Body, const Fixture& F) noexcept
{
    int Cylinders = 0, Revolutions = 0, Cones = 0, Planes = 0;
    for (const BrepFace& Face : Body.Faces)
    {
        switch (Face.Surface.Classification)
        {
        case SurfaceClassification::Cylinder: ++Cylinders; break;
        case SurfaceClassification::Revolution: ++Revolutions; break;
        case SurfaceClassification::Cone: ++Cones; break;
        case SurfaceClassification::Plane: ++Planes; break;
        default: break;
        }
    }
    const double Slant = std::hypot(F.BossHeight, F.TopRadius - F.FootRadius);
    const double Axial = F.SetBack * F.BossHeight / Slant;
    const double Contact = F.FootRadius + (F.TopRadius - F.FootRadius) * Axial / F.BossHeight;
    return Cylinders == 1 && Revolutions == 1 && Cones == 2 && Planes == 2 &&
           HasConeBand(Body, F.FootRadius + F.SetBack, Contact) && HasConeBand(Body, Contact, F.TopRadius);
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 36f · complete plane–cone boss-root chamfer");
    const Fixture Narrowing(8.0, 4.0, 5.0, 3.0, 6.0, 0.5);
    const Fixture Flaring(10.0, 3.0, 3.0, 5.0, 7.0, 0.4);

    Panel.Section("Complete mixed-support fixtures");
    Panel.Expect("The narrowing native plane-cone fixture is a V4/E7/F5 solid", Narrowing.Source && Narrowing.Source.Payload.Validate().Solid() &&
                 Narrowing.Source.Payload.Vertices.size() == 4 && Narrowing.Source.Payload.Edges.size() == 7 &&
                 Narrowing.Source.Payload.Faces.size() == 5 && Narrowing.Edge >= 0);
    Panel.Expect("The flaring native plane-cone fixture is a complete valid solid", Flaring.Source && Flaring.Source.Payload.Validate().Solid() &&
                 Flaring.Edge >= 0);

    Panel.Section("Exact analytic reconstruction");
    for (const Fixture* Case : { &Narrowing, &Flaring })
    {
        if (!Case->Source || Case->Edge < 0) continue;
        const BodyReport Before = Case->Source.Payload.Validate();
        int Applied = 0;
        Deliver<BrepBody> Result = BlendSolver::ChamferEdges(Case->Source.Payload, { Case->Edge }, Case->SetBack, &Applied);
        const BodyReport After = Result ? Result.Payload.Validate() : BodyReport{};
        Panel.Expect("The complete plane-cone root chamfer commits exactly one selected edge", Result && After.Solid() && Applied == 1);
        Panel.Expect("The result is one genus-zero manifold V5/E9/F6 solid", Result && After.Hulls == 1 && After.Genus == 0 &&
                     After.Vertices == 5 && After.Edges == 9 && After.Faces == 6 && After.OpenEdges == 0 &&
                     After.NonManifoldEdges == 0 && After.MisorientedEdges == 0);
        if (Result)
        {
            const double ExpectedAdded = AddedWedge(Case->FootRadius, Case->TopRadius, Case->BossHeight, Case->SetBack);
            Panel.Within("The conical setback wedge follows the exact square-radius integral", 
                         std::fabs((After.Volume - Before.Volume) - ExpectedAdded) / ExpectedAdded, 2e-3);
            Panel.Expect("The retained cylinder, shoulder, two cone bands and caps remain analytic", ExactSupportSet(Result.Payload, *Case));
            Panel.Expect("The original mixed-support source remains immutable", SameSource(Case->Source.Payload, Before));
        }
    }

    Panel.Section("Transactional feasibility and explicit scope limits");
    if (Narrowing.Source && Narrowing.Edge >= 0)
    {
        const BodyReport Before = Narrowing.Source.Payload.Validate();
        Panel.Expect("A zero or negative setback refuses without mutating the source",
                     !BlendSolver::ChamferEdge(Narrowing.Source.Payload, Narrowing.Edge, 0.0) &&
                     !BlendSolver::ChamferEdge(Narrowing.Source.Payload, Narrowing.Edge, -0.1) &&
                     SameSource(Narrowing.Source.Payload, Before));
        Panel.Expect("A setback consuming the boss or shoulder refuses transactionally",
                     !BlendSolver::ChamferEdge(Narrowing.Source.Payload, Narrowing.Edge, Narrowing.BossHeight) &&
                     !BlendSolver::ChamferEdge(Narrowing.Source.Payload, Narrowing.Edge, Narrowing.OuterRadius - Narrowing.FootRadius) &&
                     SameSource(Narrowing.Source.Payload, Before));
    }
    const Fixture Apex(8.0, 4.0, 5.0, 0.0, 6.0, 0.5);
    Panel.Expect("An apex/zero-radius cone remains an explicit refusal", !Apex.Source || Apex.Edge < 0 ||
                 !BlendSolver::ChamferEdge(Apex.Source.Payload, Apex.Edge, Apex.SetBack));
    auto Torus = BrepBody::Torus({ 20, 0, 0 }, { 0, 0, 1 }, 4.0, 1.0);
    Panel.Expect("An arbitrary freeform-like torus curved edge remains an explicit refusal", Torus &&
                 !BlendSolver::ChamferEdge(Torus.Payload, 0, Narrowing.SetBack));

    Panel.Section("Console transaction and visible proof");
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = Host.Document().AddBody("PlaneConeSource", Narrowing.Source.Payload).Identity > 0;
    const bool ConsoleCommit = Added && Host.Execute("chamfer PlaneConeSource 0.5 --edges=" + std::to_string(Narrowing.Edge) +
                                                     " --name=PlaneConeChamfer") &&
                               Host.Document().Find("PlaneConeChamfer") &&
                               Host.Document().Find("PlaneConeChamfer")->Body.Validate().Solid();
    Panel.Expect("The console commits the bounded plane-cone route", ConsoleCommit);

    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase36f_PlaneConeChamfer.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost ProofHost(SOLIDARC_PROOF_FOLDER, 1600, 900);
    Deliver<BrepBody> ProofResult = Narrowing.Source && Narrowing.Edge >= 0
        ? BlendSolver::ChamferEdge(Narrowing.Source.Payload, Narrowing.Edge, Narrowing.SetBack)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "proof fixture is unavailable");
    const bool Rendered = Narrowing.Source && ProofResult &&
        ProofHost.Document().AddBody("SharpPlaneCone", Narrowing.Source.Payload.Transformed(Mat4::Translation({ -11, 0, 0 }))).Identity > 0 &&
        ProofHost.Document().AddBody("ChamferedPlaneCone", ProofResult.Payload.Transformed(Mat4::Translation({ 11, 0, 0 }))).Identity > 0 &&
        ProofHost.Execute("view iso") && ProofHost.Execute("view fit") && ProofHost.Execute("render Phase36f_PlaneConeChamfer");
    Panel.Expect("The source/result plane-cone proof render completes", Rendered);
    Panel.Expect("The plane-cone chamfer proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
