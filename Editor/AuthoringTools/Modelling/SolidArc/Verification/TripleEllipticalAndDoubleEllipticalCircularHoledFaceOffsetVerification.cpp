//=============================================================================================================================================
// SolidArc · Batch 60 · exact triple-elliptical-hole and double-elliptical/circular-hole prism face offsets
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
constexpr double EllipseMajor = 1.8;
constexpr double EllipseMinor = 1.2;
constexpr double CircleRadius = 1.4;

[[nodiscard]] Deliver<BrepBody> TripleEllipticalMultiLoopPrism(bool Mixed, double SecondMajor = EllipseMajor,
                                                                double SecondMinor = EllipseMinor, double MixedRadius = CircleRadius) noexcept
{
    Workplane W;
    const auto Outer = NurbsCurve::Rectangle(W, { -12, -7 }, { 12, 7 });
    const auto Left = NurbsCurve::Ellipse({ -6, 0, 0 }, Vec3::UnitZ(), Vec3::UnitX(), EllipseMajor, EllipseMinor);
    const auto Middle = NurbsCurve::Ellipse({ 0, 0, 0 }, Vec3::UnitZ(), Vec3::UnitX(), EllipseMajor, EllipseMinor);
    if (!Outer || !Left || !Middle) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "triple elliptical fixture");
    std::vector<NurbsCurve> Profiles{ Outer.Payload, Left.Payload, Middle.Payload };
    if (Mixed)
    {
        const auto Right = NurbsCurve::Circle({ 6, 0, 0 }, Vec3::UnitZ(), MixedRadius);
        if (!Right) return Deliver<BrepBody>::Reject(Right.Denial.Reason, Right.Denial.Detail);
        Profiles.push_back(Right.Payload);
    }
    else
    {
        const auto Right = NurbsCurve::Ellipse({ 6, 0, 0 }, Vec3::UnitZ(), Vec3::UnitX(), SecondMajor, SecondMinor);
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
    return R.Solid() && R.Hulls == 1 && R.Genus == 3 && R.OpenEdges == 0 && R.NonManifoldEdges == 0 && R.MisorientedEdges == 0 &&
           Body.Vertices.size() == 14 && Body.Edges.size() == 21 && Body.Coedges.size() == 42 && Body.Loops.size() == 15 && Body.Faces.size() == 9;
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
    if (F.Surface.Classification != SurfaceClassification::Plane || F.Loops.size() != 4) return false;
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
        const Box3 B = Curve.Bounds();
        const bool Ellipse = Curve.PoleCount() == 9 && std::fabs((B.High.X - B.Low.X) - (B.High.Y - B.Low.Y)) > 1e-6;
        if (Ellipse) ++Ellipses;
        else if (Curve.Classification == CurveClassification::Circle && Curve.Degree == 2) ++Circles;
        else return false;
    }
    return Outer == 1 && Ellipses == (Mixed ? 2 : 3) && Circles == (Mixed ? 1 : 0);
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Batch 60 · triple elliptical-hole and double elliptical/circular-hole prism offsets");
    const auto TripleDeliver = TripleEllipticalMultiLoopPrism(false);
    const auto MixedDeliver = TripleEllipticalMultiLoopPrism(true);
    Panel.Expect("Both exact genus-three elliptical multi-loop fixtures are constructed", TripleDeliver && MixedDeliver);
    if (!TripleDeliver || !MixedDeliver) return Panel.Conclude();

    const BrepBody Triple = TripleDeliver.Payload, TripleSnapshot = Triple;
    const BrepBody Mixed = MixedDeliver.Payload, MixedSnapshot = Mixed;
    const BodyReport TripleBefore = Triple.Validate(), MixedBefore = Mixed.Validate();
    const int TripleTop = FaceToward(Triple, Vec3::UnitZ()), TripleBottom = FaceToward(Triple, -Vec3::UnitZ()), TripleSide = FaceToward(Triple, Vec3::UnitX());
    const int MixedTop = FaceToward(Mixed, Vec3::UnitZ()), MixedBottom = FaceToward(Mixed, -Vec3::UnitZ()), MixedSide = FaceToward(Mixed, Vec3::UnitX());

    Panel.Section("Exact genus-three elliptical and mixed-support source recognition");
    Panel.Expect("Triple elliptical-hole source is closed genus-three V14/E21/C42/L15/F9", ExactTopology(Triple) && ExactSupports(Triple) && CapMatches(Triple, TripleTop, false));
    Panel.Expect("Double elliptical/circular-hole source is closed genus-three V14/E21/C42/L15/F9", ExactTopology(Mixed) && ExactSupports(Mixed) && CapMatches(Mixed, MixedTop, true));
    Panel.Expect("Each source selects distinct upper, lower, and side faces", TripleTop != TripleBottom && TripleTop != TripleSide && MixedTop != MixedBottom && MixedTop != MixedSide);

    Panel.Section("Multiple positive upper-cap distances");
    for (const double Distance : { OffsetA, OffsetB, OffsetC })
    {
        const auto TripleResult = FaceEditSolver::OffsetExtrudedTripleEllipticalHoledPrism(Triple, TripleTop, Distance);
        const auto MixedResult = FaceEditSolver::OffsetExtrudedDoubleEllipticalCircularHoledPrism(Mixed, MixedTop, Distance);
        const double TripleArea = 336.0 - 3.0 * ScalarCriteria::Pi * EllipseMajor * EllipseMinor;
        const double MixedArea = 336.0 - 2.0 * ScalarCriteria::Pi * EllipseMajor * EllipseMinor - ScalarCriteria::Pi * CircleRadius * CircleRadius;
        Panel.Expect("Triple elliptical-hole offset retains genus-three topology", TripleResult && ExactTopology(TripleResult.Payload) && ExactSupports(TripleResult.Payload) &&
                     CapMatches(TripleResult.Payload, FaceToward(TripleResult.Payload, Vec3::UnitZ()), false));
        Panel.Expect("Double elliptical/circular-hole offset retains mixed analytic supports", MixedResult && ExactTopology(MixedResult.Payload) && ExactSupports(MixedResult.Payload) &&
                     CapMatches(MixedResult.Payload, FaceToward(MixedResult.Payload, Vec3::UnitZ()), true));
        if (TripleResult) Panel.Within("Triple elliptical-hole volume follows exact ellipse-area identity", std::fabs(TripleResult.Payload.Validate().Volume - TripleArea * (Height + Distance)) / (TripleArea * (Height + Distance)), 1e-3);
        if (MixedResult) Panel.Within("Double elliptical/circular-hole volume follows exact mixed-area identity", std::fabs(MixedResult.Payload.Validate().Volume - MixedArea * (Height + Distance)) / (MixedArea * (Height + Distance)), 1e-3);
    }
    Panel.Expect("The public dispatcher reaches both genus-three elliptical routes", FaceEditSolver::OffsetFace(Triple, TripleTop, OffsetB) && FaceEditSolver::OffsetFace(Mixed, MixedTop, OffsetB));
    Panel.Expect("Both genus-three elliptical reconstructions preserve their sources", SameSource(Triple, TripleSnapshot, TripleBefore) && SameSource(Mixed, MixedSnapshot, MixedBefore));

    Panel.Section("Transactional refusal boundaries");
    Panel.Expect("Only the triple elliptical upper cap is supported", !FaceEditSolver::OffsetExtrudedTripleEllipticalHoledPrism(Triple, TripleBottom, OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedTripleEllipticalHoledPrism(Triple, TripleSide, OffsetB));
    Panel.Expect("Only the mixed genus-three upper cap is supported", !FaceEditSolver::OffsetExtrudedDoubleEllipticalCircularHoledPrism(Mixed, MixedBottom, OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedDoubleEllipticalCircularHoledPrism(Mixed, MixedSide, OffsetB));
    Panel.Expect("The two strict routes refuse each other's support set", !FaceEditSolver::OffsetExtrudedTripleEllipticalHoledPrism(Mixed, MixedTop, OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedDoubleEllipticalCircularHoledPrism(Triple, TripleTop, OffsetB));
    Panel.Expect("Zero, negative, and non-finite distances refuse for both routes", !FaceEditSolver::OffsetExtrudedTripleEllipticalHoledPrism(Triple, TripleTop, 0.0) &&
                 !FaceEditSolver::OffsetExtrudedTripleEllipticalHoledPrism(Triple, TripleTop, -0.1) &&
                 !FaceEditSolver::OffsetExtrudedTripleEllipticalHoledPrism(Triple, TripleTop, std::numeric_limits<double>::infinity()) &&
                 !FaceEditSolver::OffsetExtrudedDoubleEllipticalCircularHoledPrism(Mixed, MixedTop, 0.0) &&
                 !FaceEditSolver::OffsetExtrudedDoubleEllipticalCircularHoledPrism(Mixed, MixedTop, -0.1) &&
                 !FaceEditSolver::OffsetExtrudedDoubleEllipticalCircularHoledPrism(Mixed, MixedTop, std::numeric_limits<double>::infinity()));
    const BrepBody TripleMoved = Triple.Transformed(Mat4::Translation({ 2, 0, 0 }));
    const BrepBody MixedMoved = Mixed.Transformed(Mat4::Translation({ 2, 0, 0 }));
    Panel.Expect("Translated genus-three elliptical profiles refuse the origin-anchored routes", !FaceEditSolver::OffsetExtrudedTripleEllipticalHoledPrism(TripleMoved, FaceToward(TripleMoved, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedDoubleEllipticalCircularHoledPrism(MixedMoved, FaceToward(MixedMoved, Vec3::UnitZ()), OffsetB));
    const auto ArbitraryTriple = TripleEllipticalMultiLoopPrism(false, 1.6, EllipseMinor);
    const auto ArbitraryMixed = TripleEllipticalMultiLoopPrism(true, EllipseMajor, EllipseMinor, 1.1);
    Panel.Expect("Non-canonical genus-three ellipse and circle dimensions remain refused", ArbitraryTriple && ArbitraryMixed &&
                 !FaceEditSolver::OffsetExtrudedTripleEllipticalHoledPrism(ArbitraryTriple.Payload, FaceToward(ArbitraryTriple.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedDoubleEllipticalCircularHoledPrism(ArbitraryMixed.Payload, FaceToward(ArbitraryMixed.Payload, Vec3::UnitZ()), OffsetB));
    const auto TwoEllipses = BrepBody::Extrude(std::vector<NurbsCurve>{
        NurbsCurve::Rectangle(Workplane{}, { -12, -7 }, { 12, 7 }).Payload,
        NurbsCurve::Ellipse({ -6, 0, 0 }, Vec3::UnitZ(), Vec3::UnitX(), EllipseMajor, EllipseMinor).Payload,
        NurbsCurve::Ellipse({ 6, 0, 0 }, Vec3::UnitZ(), Vec3::UnitX(), EllipseMajor, EllipseMinor).Payload }, Vec3::UnitZ(), Height);
    Panel.Expect("A two-elliptical-hole genus-two prism remains outside the triple-hole route", TwoEllipses &&
                 !FaceEditSolver::OffsetExtrudedTripleEllipticalHoledPrism(TwoEllipses.Payload, FaceToward(TwoEllipses.Payload, Vec3::UnitZ()), OffsetB));
    BrepBody MalformedTriple = Triple; MalformedTriple.Edges.front().Coedges.push_back(MalformedTriple.Edges.front().Coedges.front());
    BrepBody MalformedMixed = Mixed; MalformedMixed.Edges.front().Coedges.push_back(MalformedMixed.Edges.front().Coedges.front());
    Panel.Expect("Malformed genus-three elliptical profiles refuse without healing", !FaceEditSolver::OffsetExtrudedTripleEllipticalHoledPrism(MalformedTriple, TripleTop, OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedDoubleEllipticalCircularHoledPrism(MalformedMixed, MixedTop, OffsetB));
    Panel.Expect("All refusal paths preserve both valid sources", SameSource(Triple, TripleSnapshot, TripleBefore) && SameSource(Mixed, MixedSnapshot, MixedBefore));

    Panel.Section("Durable genus-three elliptical multi-loop contact sheet");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Batch60_TripleEllipticalAndDoubleEllipticalCircularHoledFaceOffset.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    const auto TripleResult = FaceEditSolver::OffsetExtrudedTripleEllipticalHoledPrism(Triple, TripleTop, OffsetB);
    const auto MixedResult = FaceEditSolver::OffsetExtrudedDoubleEllipticalCircularHoledPrism(Mixed, MixedTop, OffsetB);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1800, 900);
    const bool Added = TripleResult && MixedResult &&
        Host.Document().AddBody("SharpTripleEllipticalHoles", Triple.Transformed(Mat4::Translation({ -14, -8, 0 }))).Identity > 0 &&
        Host.Document().AddBody("OffsetTripleEllipticalHoles", TripleResult.Payload.Transformed(Mat4::Translation({ -14, 8, 0 }))).Identity > 0 &&
        Host.Document().AddBody("SharpDoubleEllipticalCircularHoles", Mixed.Transformed(Mat4::Translation({ 14, -8, 0 }))).Identity > 0 &&
        Host.Document().AddBody("OffsetDoubleEllipticalCircularHoles", MixedResult.Payload.Transformed(Mat4::Translation({ 14, 8, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") && Host.Execute("view orbit 25 -18") &&
        Host.Execute("view fit") && Host.Execute("render Batch60_TripleEllipticalAndDoubleEllipticalCircularHoledFaceOffset");
    Panel.Expect("The consolidated genus-three elliptical proof render completes", Rendered);
    Panel.Expect("The durable genus-three elliptical proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
