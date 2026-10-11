//=============================================================================================================================================
// SolidArc · Batch 59 · exact twin-elliptical-hole and elliptical/circular-hole prism face offsets
//=============================================================================================================================================
#include "Kernel/FaceEditSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>
#include <filesystem>
#include <limits>
#include <vector>

using namespace Frontier;

namespace
{
constexpr double Height = 6.0;
constexpr double OffsetA = 0.5;
constexpr double OffsetB = 1.5;
constexpr double OffsetC = 3.0;
constexpr double EllipseMajor = 2.2;
constexpr double EllipseMinor = 1.4;
constexpr double CircleRadius = 1.6;

[[nodiscard]] Deliver<BrepBody> EllipticalMultiLoopPrism(bool Mixed, double SecondMajor = EllipseMajor,
                                                          double SecondMinor = EllipseMinor, double MixedRadius = CircleRadius) noexcept
{
    Workplane W;
    const auto Outer = NurbsCurve::Rectangle(W, { -10, -6 }, { 10, 6 });
    const auto Left = NurbsCurve::Ellipse({ -4, 0, 0 }, Vec3::UnitZ(), Vec3::UnitX(), EllipseMajor, EllipseMinor);
    if (!Outer || !Left) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "multi-loop elliptical fixture");
    std::vector<NurbsCurve> Profiles{ Outer.Payload, Left.Payload };
    if (Mixed)
    {
        const auto Right = NurbsCurve::Circle({ 4, 0, 0 }, Vec3::UnitZ(), MixedRadius);
        if (!Right) return Deliver<BrepBody>::Reject(Right.Denial.Reason, Right.Denial.Detail);
        Profiles.push_back(Right.Payload);
    }
    else
    {
        const auto Right = NurbsCurve::Ellipse({ 4, 0, 0 }, Vec3::UnitZ(), Vec3::UnitX(), SecondMajor, SecondMinor);
        if (!Right) return Deliver<BrepBody>::Reject(Right.Denial.Reason, Right.Denial.Detail);
        Profiles.push_back(Right.Payload);
    }
    return BrepBody::Extrude(Profiles, Vec3::UnitZ(), Height);
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
           After.Loops == Before.Loops && After.EulerCharacteristic == Before.EulerCharacteristic && After.Genus == Before.Genus &&
           After.OpenEdges == Before.OpenEdges && After.NonManifoldEdges == Before.NonManifoldEdges &&
           After.MisorientedEdges == Before.MisorientedEdges && std::fabs(After.Volume - Before.Volume) <= 1e-12 &&
           Body.Vertices.size() == Snapshot.Vertices.size() && Body.Edges.size() == Snapshot.Edges.size() &&
           Body.Coedges.size() == Snapshot.Coedges.size() && Body.Loops.size() == Snapshot.Loops.size() && Body.Faces.size() == Snapshot.Faces.size();
}

[[nodiscard]] bool ExactTopology(const BrepBody& Body) noexcept
{
    const BodyReport R = Body.Validate();
    return R.Solid() && R.Hulls == 1 && R.Genus == 2 && R.OpenEdges == 0 && R.NonManifoldEdges == 0 && R.MisorientedEdges == 0 &&
           Body.Vertices.size() == 12 && Body.Edges.size() == 18 && Body.Coedges.size() == 36 && Body.Loops.size() == 12 && Body.Faces.size() == 8;
}

[[nodiscard]] bool ExactSupports(const BrepBody& Body) noexcept
{
    for (const BrepFace& F : Body.Faces)
        if (F.Surface.Classification != SurfaceClassification::Plane && F.Surface.Classification != SurfaceClassification::Extrusion) return false;
    return true;
}

[[nodiscard]] bool CapMatches(const BrepBody& Body, int Face, bool Mixed) noexcept
{
    if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return false;
    const BrepFace& F = Body.Faces[Face];
    if (F.Surface.Classification != SurfaceClassification::Plane || F.Loops.size() != 3) return false;
    const Vec3 N = Body.FaceNormal(Face, 0.5 * (F.Surface.DomainStartU() + F.Surface.DomainEndU()),
                                   0.5 * (F.Surface.DomainStartV() + F.Surface.DomainEndV())).Normalised();
    if (N.Dot(Vec3::UnitZ()) < 1.0 - 1e-6) return false;
    int Outer = 0, Ellipses = 0, Circles = 0;
    for (int Loop : F.Loops)
    {
        if (Loop < 0 || Loop >= static_cast<int>(Body.Loops.size())) return false;
        const BrepLoop& L = Body.Loops[Loop];
        if (L.Outer && L.Coedges.size() == 4) { ++Outer; continue; }
        if (L.Outer || L.Coedges.size() != 1) return false;
        const int Coedge = L.Coedges.front();
        if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return false;
        const int Edge = Body.Coedges[Coedge].Edge;
        if (Edge < 0 || Edge >= static_cast<int>(Body.Edges.size())) return false;
        const NurbsCurve& Curve = Body.Edges[Edge].Curve;
        if (!Curve.Closed() || !Curve.Rational()) return false;
        const Box3 CurveBounds = Curve.Bounds();
        const bool Ellipse = Curve.PoleCount() == 9 && std::fabs((CurveBounds.High.X - CurveBounds.Low.X) - (CurveBounds.High.Y - CurveBounds.Low.Y)) > 1e-6;
        if (Ellipse) ++Ellipses;
        else if (Curve.Classification == CurveClassification::Circle && Curve.Degree == 2) ++Circles;
        else return false;
    }
    return Outer == 1 && Ellipses == (Mixed ? 1 : 2) && Circles == (Mixed ? 1 : 0);
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Batch 59 · twin elliptical-hole and elliptical/circular-hole prism offsets");
    const auto TwinDeliver = EllipticalMultiLoopPrism(false);
    const auto MixedDeliver = EllipticalMultiLoopPrism(true);
    Panel.Expect("Both exact elliptical multi-loop fixtures are constructed", TwinDeliver && MixedDeliver);
    if (!TwinDeliver || !MixedDeliver) return Panel.Conclude();

    const BrepBody Twin = TwinDeliver.Payload, TwinSnapshot = Twin;
    const BrepBody Mixed = MixedDeliver.Payload, MixedSnapshot = Mixed;
    const BodyReport TwinBefore = Twin.Validate(), MixedBefore = Mixed.Validate();
    const int TwinTop = FaceToward(Twin, Vec3::UnitZ()), TwinBottom = FaceToward(Twin, -Vec3::UnitZ()), TwinSide = FaceToward(Twin, Vec3::UnitX());
    const int MixedTop = FaceToward(Mixed, Vec3::UnitZ()), MixedBottom = FaceToward(Mixed, -Vec3::UnitZ()), MixedSide = FaceToward(Mixed, Vec3::UnitX());

    Panel.Section("Exact elliptical and mixed-support source recognition");
    Panel.Expect("Twin elliptical-hole source is closed genus-two V12/E18/C36/L12/F8", ExactTopology(Twin) && ExactSupports(Twin) && CapMatches(Twin, TwinTop, false));
    Panel.Expect("Elliptical/circular-hole source is closed genus-two V12/E18/C36/L12/F8", ExactTopology(Mixed) && ExactSupports(Mixed) && CapMatches(Mixed, MixedTop, true));
    Panel.Expect("Each source selects distinct upper, lower, and side faces", TwinTop != TwinBottom && TwinTop != TwinSide && MixedTop != MixedBottom && MixedTop != MixedSide);

    Panel.Section("Multiple positive upper-cap distances");
    for (const double Distance : { OffsetA, OffsetB, OffsetC })
    {
        const auto TwinResult = FaceEditSolver::OffsetExtrudedTwinEllipticalHoledPrism(Twin, TwinTop, Distance);
        const auto MixedResult = FaceEditSolver::OffsetExtrudedEllipticalCircularHoledPrism(Mixed, MixedTop, Distance);
        const double TwinArea = 240.0 - 2.0 * ScalarCriteria::Pi * EllipseMajor * EllipseMinor;
        const double MixedArea = 240.0 - ScalarCriteria::Pi * EllipseMajor * EllipseMinor - ScalarCriteria::Pi * CircleRadius * CircleRadius;
        Panel.Expect("Twin elliptical-hole offset retains genus-two topology", TwinResult && ExactTopology(TwinResult.Payload) && ExactSupports(TwinResult.Payload) &&
                     CapMatches(TwinResult.Payload, FaceToward(TwinResult.Payload, Vec3::UnitZ()), false));
        Panel.Expect("Elliptical/circular-hole offset retains mixed analytic supports", MixedResult && ExactTopology(MixedResult.Payload) && ExactSupports(MixedResult.Payload) &&
                     CapMatches(MixedResult.Payload, FaceToward(MixedResult.Payload, Vec3::UnitZ()), true));
        if (TwinResult) Panel.Within("Twin elliptical-hole volume follows exact ellipse-area identity", std::fabs(TwinResult.Payload.Validate().Volume - TwinArea * (Height + Distance)) / (TwinArea * (Height + Distance)), 1e-3);
        if (MixedResult) Panel.Within("Elliptical/circular-hole volume follows exact mixed-area identity", std::fabs(MixedResult.Payload.Validate().Volume - MixedArea * (Height + Distance)) / (MixedArea * (Height + Distance)), 1e-3);
    }
    Panel.Expect("The public dispatcher reaches both elliptical multi-loop routes", FaceEditSolver::OffsetFace(Twin, TwinTop, OffsetB) && FaceEditSolver::OffsetFace(Mixed, MixedTop, OffsetB));
    Panel.Expect("Both elliptical multi-loop reconstructions preserve their sources", SameSource(Twin, TwinSnapshot, TwinBefore) && SameSource(Mixed, MixedSnapshot, MixedBefore));

    Panel.Section("Transactional refusal boundaries");
    Panel.Expect("Only the twin elliptical upper cap is supported", !FaceEditSolver::OffsetExtrudedTwinEllipticalHoledPrism(Twin, TwinBottom, OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedTwinEllipticalHoledPrism(Twin, TwinSide, OffsetB));
    Panel.Expect("Only the mixed elliptical/circular upper cap is supported", !FaceEditSolver::OffsetExtrudedEllipticalCircularHoledPrism(Mixed, MixedBottom, OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedEllipticalCircularHoledPrism(Mixed, MixedSide, OffsetB));
    Panel.Expect("The two strict routes refuse each other's support set", !FaceEditSolver::OffsetExtrudedTwinEllipticalHoledPrism(Mixed, MixedTop, OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedEllipticalCircularHoledPrism(Twin, TwinTop, OffsetB));
    Panel.Expect("Zero, negative, and non-finite distances refuse for both routes", !FaceEditSolver::OffsetExtrudedTwinEllipticalHoledPrism(Twin, TwinTop, 0.0) &&
                 !FaceEditSolver::OffsetExtrudedTwinEllipticalHoledPrism(Twin, TwinTop, -0.1) &&
                 !FaceEditSolver::OffsetExtrudedTwinEllipticalHoledPrism(Twin, TwinTop, std::numeric_limits<double>::infinity()) &&
                 !FaceEditSolver::OffsetExtrudedEllipticalCircularHoledPrism(Mixed, MixedTop, 0.0) &&
                 !FaceEditSolver::OffsetExtrudedEllipticalCircularHoledPrism(Mixed, MixedTop, -0.1) &&
                 !FaceEditSolver::OffsetExtrudedEllipticalCircularHoledPrism(Mixed, MixedTop, std::numeric_limits<double>::infinity()));
    const BrepBody TwinMoved = Twin.Transformed(Mat4::Translation({ 2, 0, 0 }));
    const BrepBody MixedMoved = Mixed.Transformed(Mat4::Translation({ 2, 0, 0 }));
    Panel.Expect("Translated elliptical multi-loop profiles refuse the origin-anchored routes", !FaceEditSolver::OffsetExtrudedTwinEllipticalHoledPrism(TwinMoved, FaceToward(TwinMoved, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedEllipticalCircularHoledPrism(MixedMoved, FaceToward(MixedMoved, Vec3::UnitZ()), OffsetB));
    const auto ArbitraryTwin = EllipticalMultiLoopPrism(false, 2.0, EllipseMinor);
    const auto ArbitraryMixed = EllipticalMultiLoopPrism(true, EllipseMajor, EllipseMinor, 1.4);
    Panel.Expect("Non-canonical ellipse and circle dimensions remain refused", ArbitraryTwin && ArbitraryMixed &&
                 !FaceEditSolver::OffsetExtrudedTwinEllipticalHoledPrism(ArbitraryTwin.Payload, FaceToward(ArbitraryTwin.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedEllipticalCircularHoledPrism(ArbitraryMixed.Payload, FaceToward(ArbitraryMixed.Payload, Vec3::UnitZ()), OffsetB));
    const auto OneEllipse = BrepBody::Extrude(std::vector<NurbsCurve>{
        NurbsCurve::Rectangle(Workplane{}, { -10, -6 }, { 10, 6 }).Payload,
        NurbsCurve::Ellipse({ 0, 0, 0 }, Vec3::UnitZ(), Vec3::UnitX(), EllipseMajor, EllipseMinor).Payload }, Vec3::UnitZ(), Height);
    Panel.Expect("A single elliptical-hole prism remains outside the twin-hole route", OneEllipse &&
                 !FaceEditSolver::OffsetExtrudedTwinEllipticalHoledPrism(OneEllipse.Payload, FaceToward(OneEllipse.Payload, Vec3::UnitZ()), OffsetB));
    BrepBody MalformedTwin = Twin; MalformedTwin.Edges.front().Coedges.push_back(MalformedTwin.Edges.front().Coedges.front());
    BrepBody MalformedMixed = Mixed; MalformedMixed.Edges.front().Coedges.push_back(MalformedMixed.Edges.front().Coedges.front());
    Panel.Expect("Malformed elliptical multi-loop profiles refuse without healing", !FaceEditSolver::OffsetExtrudedTwinEllipticalHoledPrism(MalformedTwin, TwinTop, OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedEllipticalCircularHoledPrism(MalformedMixed, MixedTop, OffsetB));
    Panel.Expect("All refusal paths preserve both valid sources", SameSource(Twin, TwinSnapshot, TwinBefore) && SameSource(Mixed, MixedSnapshot, MixedBefore));

    Panel.Section("Durable elliptical multi-loop contact sheet");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Batch59_TwinEllipticalAndEllipticalCircularHoledFaceOffset.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    const auto TwinResult = FaceEditSolver::OffsetExtrudedTwinEllipticalHoledPrism(Twin, TwinTop, OffsetB);
    const auto MixedResult = FaceEditSolver::OffsetExtrudedEllipticalCircularHoledPrism(Mixed, MixedTop, OffsetB);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1800, 900);
    const bool Added = TwinResult && MixedResult &&
        Host.Document().AddBody("SharpTwinEllipticalHoles", Twin.Transformed(Mat4::Translation({ -12, -8, 0 }))).Identity > 0 &&
        Host.Document().AddBody("OffsetTwinEllipticalHoles", TwinResult.Payload.Transformed(Mat4::Translation({ -12, 8, 0 }))).Identity > 0 &&
        Host.Document().AddBody("SharpEllipticalCircularHoles", Mixed.Transformed(Mat4::Translation({ 12, -8, 0 }))).Identity > 0 &&
        Host.Document().AddBody("OffsetEllipticalCircularHoles", MixedResult.Payload.Transformed(Mat4::Translation({ 12, 8, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") && Host.Execute("view orbit 25 -18") &&
        Host.Execute("view fit") && Host.Execute("render Batch59_TwinEllipticalAndEllipticalCircularHoledFaceOffset");
    Panel.Expect("The consolidated elliptical multi-loop proof render completes", Rendered);
    Panel.Expect("The durable elliptical multi-loop proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
