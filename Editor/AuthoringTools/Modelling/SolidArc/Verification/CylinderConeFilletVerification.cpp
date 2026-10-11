//=============================================================================================================================================
// SolidArc · Phase 36p · bounded complete cylinder–cone boss-root fillet
//
// The second Set 3 mixed-support slice. A complete circular edge shared by a native coaxial cylinder and a native
// coaxial conical frustum is rebuilt from exact retained supports and one revolved straight meridian band. This does
// not claim arbitrary cone/cylinder intersections or general curved-edge networks.
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
[[nodiscard]] Deliver<BrepBody> CylinderConeBoss(Vec3 Base, Vec3 Axis, double OuterRadius, double ShoulderHeight,
                                                 double BossRadius, double CylinderHeight, double TopRadius,
                                                 double ConeHeight) noexcept
{
    Axis = Axis.Normalised();
    const Workplane Frame = Workplane::FromNormal(Base, Axis);
    const Vec3 ShoulderCentre = Base + Axis * ShoulderHeight;
    const Vec3 RootCentre = ShoulderCentre + Axis * CylinderHeight;
    Deliver<NurbsSurface> Outer = NurbsSurface::Cylinder(Base, Axis, OuterRadius, ShoulderHeight);
    Deliver<NurbsCurve> ShoulderLine = NurbsCurve::Line(ShoulderCentre + Frame.AxisX * OuterRadius,
                                                        ShoulderCentre + Frame.AxisX * BossRadius);
    Deliver<NurbsSurface> Shoulder = ShoulderLine
        ? NurbsSurface::Revolution(ShoulderLine.Payload, Base, Axis, ScalarCriteria::TwoPi)
        : Deliver<NurbsSurface>::Reject(ShoulderLine.Denial.Reason, ShoulderLine.Denial.Detail);
    Deliver<NurbsSurface> Boss = NurbsSurface::Cylinder(ShoulderCentre, Axis, BossRadius, CylinderHeight);
    Deliver<NurbsSurface> Cone = NurbsSurface::Cone(RootCentre, Axis, BossRadius, TopRadius, ConeHeight);
    if (!Outer || !Shoulder || !Boss || !Cone)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "cylinder-cone fixture is degenerate");
    return BrepBody::Sew({ Outer.Payload, Shoulder.Payload, Boss.Payload, Cone.Payload });
}

[[nodiscard]] int RootEdge(const BrepBody& Body) noexcept
{
    for (int Edge = 0; Edge < static_cast<int>(Body.Edges.size()); ++Edge)
    {
        const BrepEdge& Candidate = Body.Edges[Edge];
        if (!Candidate.Closed() || Candidate.Coedges.size() != 2) continue;
        bool Cylinder = false, Cone = false;
        for (int Coedge : Candidate.Coedges)
        {
            if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) continue;
            const int Face = Body.Coedges[Coedge].Face;
            if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) continue;
            const SurfaceClassification Class = Body.Faces[Face].Surface.Classification;
            Cylinder = Cylinder || Class == SurfaceClassification::Cylinder;
            Cone = Cone || Class == SurfaceClassification::Cone;
        }
        if (Cylinder && Cone) return Edge;
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

[[nodiscard]] double FilletDelta(double BossRadius, double TopRadius, double ConeHeight, double Radius) noexcept
{
    const double HalfAngle = std::atan2(BossRadius - TopRadius, ConeHeight);
    const double CentreZ = -Radius * std::tan(0.5 * HalfAngle);
    const double CentreR = BossRadius - Radius;
    const double ContactR = CentreR + Radius * std::cos(HalfAngle);
    const double ContactZ = CentreZ + Radius * std::sin(HalfAngle);
    auto SegmentMoment = [](double R0, double Z0, double R1, double Z1) noexcept
    { return (Z1 - Z0) * (R0 * R0 + R0 * R1 + R1 * R1) / 6.0; };
    auto ArcMoment = [&]() noexcept
    {
        const double S0 = std::sin(HalfAngle), S1 = 0.0;
        const double S20 = std::sin(2.0 * HalfAngle), S21 = 0.0;
        return Radius / 2.0 * (CentreR * CentreR * (S1 - S0) +
            2.0 * CentreR * Radius * ((-HalfAngle) / 2.0 + (S21 - S20) / 4.0) +
            Radius * Radius * ((S1 - S0) - (S1 * S1 * S1 - S0 * S0 * S0) / 3.0));
    };
    const double Moment = SegmentMoment(BossRadius, CentreZ, BossRadius, 0.0) +
        SegmentMoment(BossRadius, 0.0, ContactR, ContactZ) + ArcMoment();
    return ScalarCriteria::TwoPi * std::fabs(Moment);
}

struct Fixture
{
    double OuterRadius = 0.0, ShoulderHeight = 0.0, BossRadius = 0.0, CylinderHeight = 0.0;
    double TopRadius = 0.0, ConeHeight = 0.0, Radius = 0.0;
    Deliver<BrepBody> Source = Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "not built");
    int Edge = -1;

    Fixture(double Outer, double Shoulder, double Boss, double Cylinder, double Top, double Cone, double Back) noexcept
        : OuterRadius(Outer), ShoulderHeight(Shoulder), BossRadius(Boss), CylinderHeight(Cylinder),
          TopRadius(Top), ConeHeight(Cone), Radius(Back)
    {
        Source = CylinderConeBoss({ 0, 0, 0 }, { 0, 0, 1 }, OuterRadius, ShoulderHeight, BossRadius,
                                   CylinderHeight, TopRadius, ConeHeight);
        if (Source) Edge = RootEdge(Source.Payload);
    }
};

[[nodiscard]] bool ExactSupportSet(const BrepBody& Body, const Fixture& F) noexcept
{
    int Cylinders = 0, Revolutions = 0, Cones = 0, Tori = 0, Planes = 0;
    bool RetainedCylinder = false, RetainedCone = false, Roll = false;
    const double HalfAngle = std::atan2(F.BossRadius - F.TopRadius, F.ConeHeight);
    const double CentreR = F.BossRadius - F.Radius;
    const double ContactR = CentreR + F.Radius * std::cos(HalfAngle);
    for (const BrepFace& Face : Body.Faces)
    {
        const NurbsSurface& Surface = Face.Surface;
        if (Surface.Classification == SurfaceClassification::Cylinder)
        {
            ++Cylinders;
            RetainedCylinder = RetainedCylinder || std::fabs(Surface.RadiusMajor - F.BossRadius) < 1e-9;
        }
        else if (Surface.Classification == SurfaceClassification::Revolution) ++Revolutions;
        else if (Surface.Classification == SurfaceClassification::Cone)
        {
            ++Cones;
            RetainedCone = RetainedCone ||
                (std::fabs(Surface.RadiusMajor - ContactR) < 1e-9 && std::fabs(Surface.RadiusMinor - F.TopRadius) < 1e-9);
        }
        else if (Surface.Classification == SurfaceClassification::Torus)
        {
            ++Tori;
            Roll = std::fabs(Surface.RadiusMajor - CentreR) < 1e-9 && std::fabs(Surface.RadiusMinor - F.Radius) < 1e-9;
        }
        else if (Surface.Classification == SurfaceClassification::Plane) ++Planes;
    }
    return Cylinders == 2 && Revolutions == 1 && Cones == 1 && Tori == 1 && Planes == 2 &&
           RetainedCylinder && RetainedCone && Roll;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 36p · complete cylinder–cone boss-root fillet");
    const Fixture Narrowing(8.0, 4.0, 5.0, 3.0, 3.0, 6.0, 0.5);
    const Fixture Flaring(10.0, 3.0, 3.0, 2.5, 5.0, 7.0, 0.4);

    Panel.Section("Complete mixed curved-support fixtures");
    Panel.Expect("The narrowing cylinder-cone source is a V5/E9/F6 solid", Narrowing.Source && Narrowing.Source.Payload.Validate().Solid() &&
                 Narrowing.Source.Payload.Vertices.size() == 5 && Narrowing.Source.Payload.Edges.size() == 9 &&
                 Narrowing.Source.Payload.Faces.size() == 6 && Narrowing.Edge >= 0);
    Panel.Expect("The flaring cylinder-cone source is a valid complete solid", Flaring.Source && Flaring.Source.Payload.Validate().Solid() &&
                 Flaring.Edge >= 0);

    Panel.Section("Exact retained supports and conical radius band");
    for (const Fixture* Case : { &Narrowing })
    {
        if (!Case->Source || Case->Edge < 0) continue;
        const BodyReport Before = Case->Source.Payload.Validate();
        int Applied = 0;
        Deliver<BrepBody> Result = BlendSolver::FilletEdges(Case->Source.Payload, { Case->Edge }, Case->Radius, &Applied);
        const BodyReport After = Result ? Result.Payload.Validate() : BodyReport{};
        Panel.Expect("The complete cylinder-cone fillet commits exactly one edge", Result && After.Solid() && Applied == 1);
        Panel.Expect("The result is one genus-zero manifold V6/E11/F7 solid", Result && After.Hulls == 1 && After.Genus == 0 &&
                     After.Vertices == 6 && After.Edges == 11 && Result.Payload.Coedges.size() == 22 && After.Faces == 7 && After.OpenEdges == 0 &&
                     After.NonManifoldEdges == 0 && After.MisorientedEdges == 0);
        if (Result)
        {
            const double ExpectedDelta = FilletDelta(Case->BossRadius, Case->TopRadius, Case->ConeHeight, Case->Radius);
            Panel.Within("The cylinder-cone fillet removal follows the exact meridian first-moment integral",
                         std::fabs((Before.Volume - After.Volume) - ExpectedDelta) / std::max(1e-9, std::fabs(ExpectedDelta)), 2e-3);
            Panel.Expect("The retained cylinder, shoulder, toroidal roll, cone and caps remain analytic", ExactSupportSet(Result.Payload, *Case));
            Panel.Expect("The cylinder-cone source remains immutable", SameSource(Case->Source.Payload, Before));
        }
    }

    Panel.Section("Transactional refusal boundaries");
    if (Narrowing.Source && Narrowing.Edge >= 0)
    {
        const BodyReport Before = Narrowing.Source.Payload.Validate();
        Panel.Expect("Zero, negative, and cylinder-consuming radii refuse transactionally",
                     !BlendSolver::FilletEdge(Narrowing.Source.Payload, Narrowing.Edge, 0.0) &&
                     !BlendSolver::FilletEdge(Narrowing.Source.Payload, Narrowing.Edge, -0.1) &&
                     !BlendSolver::FilletEdge(Narrowing.Source.Payload, Narrowing.Edge, 20.0) &&
                     SameSource(Narrowing.Source.Payload, Before));
        Panel.Expect("A radius consuming the conical support refuses transactionally",
                     !BlendSolver::FilletEdge(Narrowing.Source.Payload, Narrowing.Edge,
                                                20.0) &&
                     SameSource(Narrowing.Source.Payload, Before));
    }
    auto Torus = BrepBody::Torus({ 20, 0, 0 }, { 0, 0, 1 }, 4.0, 1.0);
    Panel.Expect("An arbitrary torus curved edge remains an explicit refusal", Torus &&
                 !BlendSolver::FilletEdge(Torus.Payload, 0, Narrowing.Radius));
    Panel.Expect("Flaring cylinder-cone roots remain an explicit refusal", Flaring.Source && Flaring.Edge >= 0 &&
                 !BlendSolver::FilletEdge(Flaring.Source.Payload, Flaring.Edge, Flaring.Radius));
    auto Apex = CylinderConeBoss({ 0, 0, 0 }, { 0, 0, 1 }, 8.0, 4.0, 5.0, 3.0, 0.0, 6.0);
    Panel.Expect("Apex cylinder-cone roots remain an explicit refusal", Apex &&
                 !BlendSolver::FilletEdge(Apex.Payload, RootEdge(Apex.Payload), Narrowing.Radius));

    Panel.Section("Console transaction and visible proof");
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = Host.Document().AddBody("CylinderConeSource", Narrowing.Source.Payload).Identity > 0;
    const bool ConsoleCommit = Added && Host.Execute("fillet CylinderConeSource 0.5 --edges=" + std::to_string(Narrowing.Edge) +
                                                     " --name=CylinderConeFillet") &&
                               Host.Document().Find("CylinderConeFillet") &&
                               Host.Document().Find("CylinderConeFillet")->Body.Validate().Solid();
    Panel.Expect("The console commits the bounded cylinder-cone route", ConsoleCommit);

    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase36p_CylinderConeFillet.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost ProofHost(SOLIDARC_PROOF_FOLDER, 1600, 900);
    Deliver<BrepBody> ProofResult = Narrowing.Source && Narrowing.Edge >= 0
        ? BlendSolver::FilletEdge(Narrowing.Source.Payload, Narrowing.Edge, Narrowing.Radius)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "proof fixture is unavailable");
    const bool Rendered = Narrowing.Source && ProofResult &&
        ProofHost.Document().AddBody("SharpCylinderCone", Narrowing.Source.Payload.Transformed(Mat4::Translation({ -11, 0, 0 }))).Identity > 0 &&
        ProofHost.Document().AddBody("FilletedCylinderCone", ProofResult.Payload.Transformed(Mat4::Translation({ 11, 0, 0 }))).Identity > 0 &&
        ProofHost.Execute("view front") && ProofHost.Execute("view fit") && ProofHost.Execute("view dolly 0.7") && ProofHost.Execute("render Phase36p_CylinderConeFillet");
    Panel.Expect("The cylinder-cone source/result proof render completes", Rendered);
    Panel.Expect("The cylinder-cone fillet proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 10000);
    return Panel.Conclude();
}
