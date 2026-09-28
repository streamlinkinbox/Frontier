//=============================================================================================================================================
// SolidArc · Batch 58 · exact twin-rectangular-hole and triple-circular-hole prism face offsets
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
constexpr double TripleRadius = 1.2;

[[nodiscard]] Deliver<BrepBody> TwinRectangularHolePrism(bool Canonical = true) noexcept
{
    Workplane W;
    const auto Outer = NurbsCurve::Rectangle(W, { -9, -5 }, { 9, 5 });
    const auto HoleA = NurbsCurve::Rectangle(W, { -6, -1 }, { -3, 1 });
    const auto HoleB = Canonical ? NurbsCurve::Rectangle(W, { 3, -1 }, { 6, 1 }) : NurbsCurve::Rectangle(W, { 2, -1 }, { 5, 1 });
    if (!Outer || !HoleA || !HoleB) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "twin rectangular-hole fixture");
    return BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, HoleA.Payload, HoleB.Payload }, Vec3::UnitZ(), Height);
}

[[nodiscard]] Deliver<BrepBody> TripleCircularHolePrism(int HoleCount = 3, double Radius = TripleRadius, double Spacing = 6.0) noexcept
{
    Workplane W;
    const auto Outer = NurbsCurve::Rectangle(W, { -10, -5 }, { 10, 5 });
    if (!Outer) return Deliver<BrepBody>::Reject(Outer.Denial.Reason, Outer.Denial.Detail);
    std::vector<NurbsCurve> Profiles{ Outer.Payload };
    for (int I = 0; I < HoleCount; ++I)
    {
        const double X = (I - (HoleCount - 1) * 0.5) * Spacing;
        const auto Hole = NurbsCurve::Circle({ X, 0, 0 }, Vec3::UnitZ(), Radius);
        if (!Hole) return Deliver<BrepBody>::Reject(Hole.Denial.Reason, Hole.Denial.Detail);
        Profiles.push_back(Hole.Payload);
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

[[nodiscard]] bool ExactTopology(const BrepBody& Body, int Genus, size_t V, size_t E, size_t C, size_t L, size_t F) noexcept
{
    const BodyReport R = Body.Validate();
    return R.Solid() && R.Hulls == 1 && R.Genus == Genus && R.OpenEdges == 0 && R.NonManifoldEdges == 0 && R.MisorientedEdges == 0 &&
           Body.Vertices.size() == V && Body.Edges.size() == E && Body.Coedges.size() == C && Body.Loops.size() == L && Body.Faces.size() == F;
}

[[nodiscard]] bool ExactSupports(const BrepBody& Body) noexcept
{
    for (const BrepFace& F : Body.Faces)
        if (F.Surface.Classification != SurfaceClassification::Plane && F.Surface.Classification != SurfaceClassification::Extrusion) return false;
    return true;
}

[[nodiscard]] bool TwinRectangularCap(const BrepBody& Body, int Face) noexcept
{
    if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return false;
    const BrepFace& F = Body.Faces[Face];
    if (F.Surface.Classification != SurfaceClassification::Plane || F.Loops.size() != 3) return false;
    const Vec3 N = Body.FaceNormal(Face, 0.5 * (F.Surface.DomainStartU() + F.Surface.DomainEndU()),
                                   0.5 * (F.Surface.DomainStartV() + F.Surface.DomainEndV())).Normalised();
    if (N.Dot(Vec3::UnitZ()) < 1.0 - 1e-6) return false;
    int RectLoops = 0;
    for (int Loop : F.Loops)
    {
        if (Loop < 0 || Loop >= static_cast<int>(Body.Loops.size()) || Body.Loops[Loop].Coedges.size() != 4) return false;
        ++RectLoops;
        for (int Coedge : Body.Loops[Loop].Coedges)
        {
            if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return false;
            const int Edge = Body.Coedges[Coedge].Edge;
            if (Edge < 0 || Edge >= static_cast<int>(Body.Edges.size()) || Body.Edges[Edge].Curve.Classification != CurveClassification::Line) return false;
        }
    }
    return RectLoops == 3;
}

[[nodiscard]] bool TripleCircularCap(const BrepBody& Body, int Face) noexcept
{
    if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return false;
    const BrepFace& F = Body.Faces[Face];
    if (F.Surface.Classification != SurfaceClassification::Plane || F.Loops.size() != 4) return false;
    const Vec3 N = Body.FaceNormal(Face, 0.5 * (F.Surface.DomainStartU() + F.Surface.DomainEndU()),
                                   0.5 * (F.Surface.DomainStartV() + F.Surface.DomainEndV())).Normalised();
    if (N.Dot(Vec3::UnitZ()) < 1.0 - 1e-6) return false;
    int Outer = 0, Circles = 0;
    for (int Loop : F.Loops)
    {
        if (Loop < 0 || Loop >= static_cast<int>(Body.Loops.size())) return false;
        const BrepLoop& L = Body.Loops[Loop];
        if (L.Outer && L.Coedges.size() == 4) ++Outer;
        else if (!L.Outer && L.Coedges.size() == 1)
        {
            const int C = L.Coedges.front();
            if (C < 0 || C >= static_cast<int>(Body.Coedges.size())) return false;
            const int E = Body.Coedges[C].Edge;
            if (E < 0 || E >= static_cast<int>(Body.Edges.size())) return false;
            const NurbsCurve& Curve = Body.Edges[E].Curve;
            if (Curve.Classification != CurveClassification::Circle || !Curve.Rational() || !Curve.Closed()) return false;
            ++Circles;
        }
        else return false;
    }
    return Outer == 1 && Circles == 3;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Batch 58 · twin rectangular-hole and triple circular-hole prism offsets");
    const auto TwinDeliver = TwinRectangularHolePrism();
    const auto TripleDeliver = TripleCircularHolePrism();
    Panel.Expect("Both exact multi-loop fixtures are constructed", TwinDeliver && TripleDeliver);
    if (!TwinDeliver || !TripleDeliver) return Panel.Conclude();

    const BrepBody Twin = TwinDeliver.Payload, TwinSnapshot = Twin;
    const BrepBody Triple = TripleDeliver.Payload, TripleSnapshot = Triple;
    const BodyReport TwinBefore = Twin.Validate(), TripleBefore = Triple.Validate();
    const int TwinTop = FaceToward(Twin, Vec3::UnitZ()), TwinBottom = FaceToward(Twin, -Vec3::UnitZ()), TwinSide = FaceToward(Twin, Vec3::UnitX());
    const int TripleTop = FaceToward(Triple, Vec3::UnitZ()), TripleBottom = FaceToward(Triple, -Vec3::UnitZ()), TripleSide = FaceToward(Triple, Vec3::UnitX());

    Panel.Section("Exact multi-loop source recognition");
    Panel.Expect("Twin rectangular-hole source is closed genus-two V24/E36/C72/L18/F14", ExactTopology(Twin, 2, 24, 36, 72, 18, 14) &&
                 ExactSupports(Twin) && TwinRectangularCap(Twin, TwinTop));
    Panel.Expect("Triple circular-hole source is closed genus-three V14/E21/C42/L15/F9", ExactTopology(Triple, 3, 14, 21, 42, 15, 9) &&
                 ExactSupports(Triple) && TripleCircularCap(Triple, TripleTop));
    Panel.Expect("Each source selects distinct upper, lower, and side faces", TwinTop != TwinBottom && TwinTop != TwinSide &&
                 TripleTop != TripleBottom && TripleTop != TripleSide);
    Panel.Section("Multiple positive upper-cap distances");
    for (const double Distance : { OffsetA, OffsetB, OffsetC })
    {
        const auto TwinResult = FaceEditSolver::OffsetExtrudedTwinRectangularHoledPrism(Twin, TwinTop, Distance);
        const auto TripleResult = FaceEditSolver::OffsetExtrudedTripleHoledPrism(Triple, TripleTop, Distance);
        const double TwinArea = 18.0 * 10.0 - 2.0 * 3.0 * 2.0;
        const double TripleArea = 20.0 * 10.0 - 3.0 * ScalarCriteria::Pi * TripleRadius * TripleRadius;
        Panel.Expect("Twin rectangular-hole offset retains genus-two topology", TwinResult && ExactTopology(TwinResult.Payload, 2, 24, 36, 72, 18, 14) &&
                     ExactSupports(TwinResult.Payload) && TwinRectangularCap(TwinResult.Payload, FaceToward(TwinResult.Payload, Vec3::UnitZ())));
        Panel.Expect("Triple circular-hole offset retains genus-three topology", TripleResult && ExactTopology(TripleResult.Payload, 3, 14, 21, 42, 15, 9) &&
                     ExactSupports(TripleResult.Payload) && TripleCircularCap(TripleResult.Payload, FaceToward(TripleResult.Payload, Vec3::UnitZ())));
        if (TwinResult) Panel.Within("Twin rectangular-hole volume follows exact area times height", std::fabs(TwinResult.Payload.Validate().Volume - TwinArea * (Height + Distance)) / (TwinArea * (Height + Distance)), 1e-3);
        if (TripleResult) Panel.Within("Triple circular-hole volume follows exact area times height", std::fabs(TripleResult.Payload.Validate().Volume - TripleArea * (Height + Distance)) / (TripleArea * (Height + Distance)), 1e-3);
    }
    Panel.Expect("The public dispatcher reaches both multi-loop routes", FaceEditSolver::OffsetFace(Twin, TwinTop, OffsetB) && FaceEditSolver::OffsetFace(Triple, TripleTop, OffsetB));
    Panel.Expect("Both multi-loop reconstructions preserve their sources", SameSource(Twin, TwinSnapshot, TwinBefore) && SameSource(Triple, TripleSnapshot, TripleBefore));

    Panel.Section("Transactional refusal boundaries");
    Panel.Expect("Only the twin rectangular upper cap is supported", !FaceEditSolver::OffsetExtrudedTwinRectangularHoledPrism(Twin, TwinBottom, OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedTwinRectangularHoledPrism(Twin, TwinSide, OffsetB));
    Panel.Expect("Only the triple circular upper cap is supported", !FaceEditSolver::OffsetExtrudedTripleHoledPrism(Triple, TripleBottom, OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedTripleHoledPrism(Triple, TripleSide, OffsetB));
    Panel.Expect("The routes refuse each other's multi-loop profile", !FaceEditSolver::OffsetExtrudedTwinRectangularHoledPrism(Triple, TripleTop, OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedTripleHoledPrism(Twin, TwinTop, OffsetB));
    Panel.Expect("Zero, negative, and non-finite distances refuse for both routes", !FaceEditSolver::OffsetExtrudedTwinRectangularHoledPrism(Twin, TwinTop, 0.0) &&
                 !FaceEditSolver::OffsetExtrudedTwinRectangularHoledPrism(Twin, TwinTop, -0.1) &&
                 !FaceEditSolver::OffsetExtrudedTwinRectangularHoledPrism(Twin, TwinTop, std::numeric_limits<double>::infinity()) &&
                 !FaceEditSolver::OffsetExtrudedTripleHoledPrism(Triple, TripleTop, 0.0) &&
                 !FaceEditSolver::OffsetExtrudedTripleHoledPrism(Triple, TripleTop, -0.1) &&
                 !FaceEditSolver::OffsetExtrudedTripleHoledPrism(Triple, TripleTop, std::numeric_limits<double>::infinity()));
    const BrepBody TwinMoved = Twin.Transformed(Mat4::Translation({ 2, 0, 0 }));
    const BrepBody TripleMoved = Triple.Transformed(Mat4::Translation({ 2, 0, 0 }));
    Panel.Expect("Translated multi-loop profiles refuse the origin-anchored routes", !FaceEditSolver::OffsetExtrudedTwinRectangularHoledPrism(TwinMoved, FaceToward(TwinMoved, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedTripleHoledPrism(TripleMoved, FaceToward(TripleMoved, Vec3::UnitZ()), OffsetB));
    const auto TwoCircular = TripleCircularHolePrism(2);
    Panel.Expect("A two-circular-hole genus-two prism remains outside the triple-hole route", TwoCircular &&
                 !FaceEditSolver::OffsetExtrudedTripleHoledPrism(TwoCircular.Payload, FaceToward(TwoCircular.Payload, Vec3::UnitZ()), OffsetB));
    const auto ArbitraryTwin = TwinRectangularHolePrism(false);
    const auto ArbitraryTriple = TripleCircularHolePrism(3, 1.0, 5.0);
    Panel.Expect("Non-canonical hole dimensions and placements remain outside both strict routes", ArbitraryTwin && ArbitraryTriple &&
                 !FaceEditSolver::OffsetExtrudedTwinRectangularHoledPrism(ArbitraryTwin.Payload, FaceToward(ArbitraryTwin.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedTripleHoledPrism(ArbitraryTriple.Payload, FaceToward(ArbitraryTriple.Payload, Vec3::UnitZ()), OffsetB));
    const auto OneRectangular = BrepBody::Extrude(std::vector<NurbsCurve>{
        NurbsCurve::Rectangle(Workplane{}, { -9, -5 }, { 9, 5 }).Payload,
        NurbsCurve::Rectangle(Workplane{}, { -3, -1 }, { 3, 1 }).Payload }, Vec3::UnitZ(), Height);
    Panel.Expect("A single rectangular-hole prism remains outside the twin-rectangular-hole route", OneRectangular &&
                 !FaceEditSolver::OffsetExtrudedTwinRectangularHoledPrism(OneRectangular.Payload, FaceToward(OneRectangular.Payload, Vec3::UnitZ()), OffsetB));
    BrepBody MalformedTwin = Twin; MalformedTwin.Edges.front().Coedges.push_back(MalformedTwin.Edges.front().Coedges.front());
    BrepBody MalformedTriple = Triple; MalformedTriple.Edges.front().Coedges.push_back(MalformedTriple.Edges.front().Coedges.front());
    Panel.Expect("Malformed multi-loop profiles refuse without healing", !FaceEditSolver::OffsetExtrudedTwinRectangularHoledPrism(MalformedTwin, TwinTop, OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedTripleHoledPrism(MalformedTriple, TripleTop, OffsetB));
    Panel.Expect("All refusal paths preserve both valid sources", SameSource(Twin, TwinSnapshot, TwinBefore) && SameSource(Triple, TripleSnapshot, TripleBefore));

    Panel.Section("Durable multi-loop contact sheet");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Batch58_TwinRectangularAndTripleCircularHoledFaceOffset.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    const auto TwinResult = FaceEditSolver::OffsetExtrudedTwinRectangularHoledPrism(Twin, TwinTop, OffsetB);
    const auto TripleResult = FaceEditSolver::OffsetExtrudedTripleHoledPrism(Triple, TripleTop, OffsetB);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1800, 900);
    const bool Added = TwinResult && TripleResult &&
        Host.Document().AddBody("SharpTwinRectangularHoles", Twin.Transformed(Mat4::Translation({ -13, -4, 0 }))).Identity > 0 &&
        Host.Document().AddBody("OffsetTwinRectangularHoles", TwinResult.Payload.Transformed(Mat4::Translation({ -4, -4, 0 }))).Identity > 0 &&
        Host.Document().AddBody("SharpTripleCircularHoles", Triple.Transformed(Mat4::Translation({ 5, -4, 0 }))).Identity > 0 &&
        Host.Document().AddBody("OffsetTripleCircularHoles", TripleResult.Payload.Transformed(Mat4::Translation({ 14, -4, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") && Host.Execute("view orbit 25 -18") &&
        Host.Execute("view fit") && Host.Execute("render Batch58_TwinRectangularAndTripleCircularHoledFaceOffset");
    Panel.Expect("The consolidated multi-loop proof render completes", Rendered);
    Panel.Expect("The durable multi-loop proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
