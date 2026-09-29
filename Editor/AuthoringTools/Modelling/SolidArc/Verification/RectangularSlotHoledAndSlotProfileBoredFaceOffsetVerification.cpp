//=============================================================================================================================================
// SolidArc · Batch 63 · exact rectangular slot-holed prism and slot-profile circular-bored prism face offsets
//=============================================================================================================================================
#include "Kernel/FaceEditSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <algorithm>
#include <array>
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
constexpr double RectHalfX = 8.0;
constexpr double RectHalfY = 5.0;
constexpr double HalfSpan = 3.0;
constexpr double SlotRadius = 1.5;
constexpr double BoreRadius = 0.8;

// Exact analytic slot area: the rectangle between the two arc centres plus one full end circle.
constexpr double SlotArea = 2.0 * HalfSpan * 2.0 * SlotRadius + ScalarCriteria::Pi * SlotRadius * SlotRadius;

[[nodiscard]] Deliver<NurbsCurve> SlotAt(double Half = HalfSpan, double Radius = SlotRadius, Vec2 Offset = { 0.0, 0.0 }) noexcept
{
    Workplane Plane;
    return NurbsCurve::Slot(Plane, { Offset.X - Half, Offset.Y }, { Offset.X + Half, Offset.Y }, Radius);
}

[[nodiscard]] Deliver<BrepBody> RectangularSlotHoledPrism(double Half = HalfSpan, double Radius = SlotRadius,
                                                          double RectX = RectHalfX, double RectY = RectHalfY) noexcept
{
    Workplane Plane;
    const auto Outer = NurbsCurve::Rectangle(Plane, { -RectX, -RectY }, { RectX, RectY });
    const auto Hole = SlotAt(Half, Radius);
    if (!Outer || !Hole) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "rectangular slot-holed fixture");
    return BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, Hole.Payload }, Vec3::UnitZ(), Height);
}

[[nodiscard]] Deliver<BrepBody> SlotProfileBoredPrism(double Half = HalfSpan, double Radius = SlotRadius,
                                                      double Bore = BoreRadius, double BoreX = 0.0) noexcept
{
    const auto Outer = SlotAt(Half, Radius);
    const auto Inner = NurbsCurve::Circle({ BoreX, 0, 0 }, Vec3::UnitZ(), Bore);
    if (!Outer || !Inner) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "slot-profile bored fixture");
    return BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, Inner.Payload }, Vec3::UnitZ(), Height);
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
           Body.Coedges.size() == Snapshot.Coedges.size() && Body.Loops.size() == Snapshot.Loops.size() &&
           Body.Faces.size() == Snapshot.Faces.size();
}

[[nodiscard]] bool ExactCounts(const BrepBody& Body, int Genus, size_t Vertices, size_t Edges, size_t Coedges,
                               size_t Loops, size_t Faces) noexcept
{
    const BodyReport R = Body.Validate();
    return R.Solid() && R.Hulls == 1 && R.Genus == Genus && R.OpenEdges == 0 && R.NonManifoldEdges == 0 &&
           R.MisorientedEdges == 0 && Body.Vertices.size() == Vertices && Body.Edges.size() == Edges &&
           Body.Coedges.size() == Coedges && Body.Loops.size() == Loops && Body.Faces.size() == Faces;
}

[[nodiscard]] bool ExactFaceCensus(const BrepBody& Body, size_t TwoLoopPlanes, size_t Extrusions) noexcept
{
    size_t Planes2 = 0, Walls = 0;
    for (const BrepFace& F : Body.Faces)
    {
        if (F.Surface.Classification == SurfaceClassification::Plane && F.Loops.size() == 2) ++Planes2;
        else if (F.Surface.Classification == SurfaceClassification::Extrusion && F.Loops.size() == 1) ++Walls;
        else return false;
    }
    return Planes2 == TwoLoopPlanes && Walls == Extrusions;
}

// The extrusion pipeline flattens a slot curve to a degree-two rational chain, so recognition is structural plus sampled.
[[nodiscard]] bool ExactSlot(const NurbsCurve& Curve, double& Half, double& Radius, Vec3& Centre) noexcept
{
    if (!Curve.Closed() || !Curve.Rational() || Curve.Degree != 2 || Curve.PoleCount() != 13 ||
        std::fabs(Curve.AxisZ.Normalised().Dot(Vec3::UnitZ())) < 1.0 - 1e-6) return false;
    const Box3 B = Curve.Bounds();
    const double SpanX = B.High.X - B.Low.X, SpanY = B.High.Y - B.Low.Y;
    if (SpanX <= 1e-9 || SpanY <= 1e-9 || std::fabs(B.High.Z - B.Low.Z) > 1e-9 || SpanX <= SpanY + 1e-9) return false;
    Radius = 0.5 * SpanY;
    Half = 0.5 * (SpanX - SpanY);
    if (Half <= 1e-9 || Radius <= 1e-9) return false;
    Centre = { 0.5 * (B.Low.X + B.High.X), 0.5 * (B.Low.Y + B.High.Y), B.Low.Z };
    Workplane Plane;
    Plane.Origin = { 0.0, 0.0, Centre.Z };
    const auto Lifted = NurbsCurve::Slot(Plane, { Centre.X - Half, Centre.Y }, { Centre.X + Half, Centre.Y }, Radius);
    if (!Lifted) return false;
    for (int I = 0; I <= 64; ++I)
    {
        const double T = Curve.DomainStart() + (Curve.DomainEnd() - Curve.DomainStart()) * static_cast<double>(I) / 64.0;
        double Distance = 0.0;
        (void)Lifted.Payload.ClosestParameter(Curve.Sample(T), &Distance);
        if (!std::isfinite(Distance) || Distance > 1e-6) return false;
    }
    return true;
}

[[nodiscard]] bool ExactCircle(const NurbsCurve& Curve, double& Radius, Vec3& Centre) noexcept
{
    if (!Curve.Closed() || !Curve.Rational() || Curve.Degree != 2 || Curve.PoleCount() != 9 ||
        std::fabs(Curve.AxisZ.Normalised().Dot(Vec3::UnitZ())) < 1.0 - 1e-6) return false;
    const Box3 B = Curve.Bounds();
    const double SpanX = B.High.X - B.Low.X, SpanY = B.High.Y - B.Low.Y;
    if (SpanX <= 1e-9 || SpanY <= 1e-9 || std::fabs(SpanX - SpanY) > 1e-9 || std::fabs(B.High.Z - B.Low.Z) > 1e-9) return false;
    Radius = 0.5 * SpanX;
    Centre = { 0.5 * (B.Low.X + B.High.X), 0.5 * (B.Low.Y + B.High.Y), B.Low.Z };
    return true;
}

// Reads the single edge of a one-coedge cap loop and classifies it as a slot or a circle.
[[nodiscard]] bool CapLoopSupport(const BrepBody& Body, int Loop, bool& Slot, double& Half, double& Radius, Vec3& Centre) noexcept
{
    if (Loop < 0 || Loop >= static_cast<int>(Body.Loops.size()) || Body.Loops[Loop].Coedges.size() != 1) return false;
    const int Coedge = Body.Loops[Loop].Coedges.front();
    if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return false;
    const int Edge = Body.Coedges[Coedge].Edge;
    if (Edge < 0 || Edge >= static_cast<int>(Body.Edges.size())) return false;
    const NurbsCurve& Curve = Body.Edges[Edge].Curve;
    double BoreRadius = 0.0; Vec3 BoreCentre{};
    if (ExactSlot(Curve, Half, Radius, Centre)) { Slot = true; return true; }
    if (ExactCircle(Curve, BoreRadius, BoreCentre)) { Slot = false; Radius = BoreRadius; Half = BoreRadius; Centre = BoreCentre; return true; }
    return false;
}

[[nodiscard]] bool RectangularSlotHoledCap(const BrepBody& Body, int Face) noexcept
{
    if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return false;
    const BrepFace& F = Body.Faces[Face];
    if (F.Surface.Classification != SurfaceClassification::Plane || F.Loops.size() != 2) return false;
    const Vec3 N = Body.FaceNormal(Face, 0.5 * (F.Surface.DomainStartU() + F.Surface.DomainEndU()),
                                   0.5 * (F.Surface.DomainStartV() + F.Surface.DomainEndV())).Normalised();
    if (N.Dot(Vec3::UnitZ()) < 1.0 - 1e-6) return false;
    int Outer = -1, Hole = -1;
    for (int Loop : F.Loops)
    {
        if (Loop < 0 || Loop >= static_cast<int>(Body.Loops.size())) return false;
        if (Body.Loops[Loop].Outer && Body.Loops[Loop].Coedges.size() == 4 && Outer < 0) Outer = Loop;
        else if (!Body.Loops[Loop].Outer && Body.Loops[Loop].Coedges.size() == 1 && Hole < 0) Hole = Loop;
        else return false;
    }
    if (Outer < 0 || Hole < 0) return false;
    std::vector<Vec3> Corners;
    Corners.reserve(4);
    for (int Coedge : Body.Loops[Outer].Coedges)
    {
        if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return false;
        const BrepCoedge& C = Body.Coedges[Coedge];
        if (C.Edge < 0 || C.Edge >= static_cast<int>(Body.Edges.size())) return false;
        const BrepEdge& E = Body.Edges[C.Edge];
        if (E.Curve.Classification != CurveClassification::Line || E.Curve.Degree != 1 ||
            E.VertexStart < 0 || E.VertexEnd < 0 || E.VertexStart == E.VertexEnd) return false;
        const Vec3 Start = Body.Vertices[C.Reversed ? E.VertexEnd : E.VertexStart].Point;
        const Vec3 End = Body.Vertices[C.Reversed ? E.VertexStart : E.VertexEnd].Point;
        if (std::fabs(Start.Z - End.Z) > 1e-9) return false;
        if (std::fabs(Start.X - End.X) > 1e-9 && std::fabs(Start.Y - End.Y) > 1e-9) return false;
        Corners.push_back(Start);
    }
    const std::array<Vec3, 4> Canonical{{ { -RectHalfX, -RectHalfY, 0.0 }, { RectHalfX, -RectHalfY, 0.0 },
                                          { RectHalfX, RectHalfY, 0.0 }, { -RectHalfX, RectHalfY, 0.0 } }};
    for (const Vec3& Want : Canonical)
    {
        bool Found = false;
        for (const Vec3& P : Corners)
            if (std::fabs(P.X - Want.X) <= 1e-9 && std::fabs(P.Y - Want.Y) <= 1e-9) { Found = true; break; }
        if (!Found) return false;
    }
    bool Slot = false; double Half = 0.0, Radius = 0.0; Vec3 Centre{};
    if (!CapLoopSupport(Body, Hole, Slot, Half, Radius, Centre) || !Slot) return false;
    return std::fabs(Half - HalfSpan) <= 1e-9 && std::fabs(Radius - SlotRadius) <= 1e-9 &&
           std::fabs(Centre.X) <= 1e-9 && std::fabs(Centre.Y) <= 1e-9;
}

[[nodiscard]] bool SlotProfileBoredCap(const BrepBody& Body, int Face) noexcept
{
    if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return false;
    const BrepFace& F = Body.Faces[Face];
    if (F.Surface.Classification != SurfaceClassification::Plane || F.Loops.size() != 2) return false;
    const Vec3 N = Body.FaceNormal(Face, 0.5 * (F.Surface.DomainStartU() + F.Surface.DomainEndU()),
                                   0.5 * (F.Surface.DomainStartV() + F.Surface.DomainEndV())).Normalised();
    if (N.Dot(Vec3::UnitZ()) < 1.0 - 1e-6) return false;
    int Outer = -1, Bore = -1;
    for (int Loop : F.Loops)
    {
        if (Loop < 0 || Loop >= static_cast<int>(Body.Loops.size())) return false;
        if (Body.Loops[Loop].Outer && Outer < 0) Outer = Loop;
        else if (!Body.Loops[Loop].Outer && Bore < 0) Bore = Loop;
        else return false;
    }
    if (Outer < 0 || Bore < 0) return false;
    bool Slot = false; double Half = 0.0, Radius = 0.0; Vec3 Centre{};
    if (!CapLoopSupport(Body, Outer, Slot, Half, Radius, Centre) || !Slot ||
        std::fabs(Half - HalfSpan) > 1e-9 || std::fabs(Radius - SlotRadius) > 1e-9 ||
        std::fabs(Centre.X) > 1e-9 || std::fabs(Centre.Y) > 1e-9) return false;
    Slot = true; Half = 0.0; Radius = 0.0; Centre = {};
    if (!CapLoopSupport(Body, Bore, Slot, Half, Radius, Centre) || Slot ||
        std::fabs(Radius - BoreRadius) > 1e-9 || std::fabs(Centre.X) > 1e-9 || std::fabs(Centre.Y) > 1e-9) return false;
    return true;
}

[[nodiscard]] bool ExactSupportCensus(const BrepBody& Body, size_t Lines, size_t Circles, size_t Slots) noexcept
{
    size_t LineCount = 0, CircleCount = 0, SlotCount = 0;
    for (const BrepEdge& E : Body.Edges)
    {
        if (E.Coedges.size() != 2) return false;
        double Half = 0.0, Radius = 0.0; Vec3 Centre{};
        if (E.Curve.Classification == CurveClassification::Line && E.Curve.Degree == 1) ++LineCount;
        else if (ExactSlot(E.Curve, Half, Radius, Centre)) ++SlotCount;
        else if (ExactCircle(E.Curve, Radius, Centre)) ++CircleCount;
        else return false;
    }
    return LineCount == Lines && CircleCount == Circles && SlotCount == Slots;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Batch 63 · rectangular slot-holed and slot-profile circular-bored prism offsets");
    const auto RectSlotDeliver = RectangularSlotHoledPrism();
    const auto SlotBoreDeliver = SlotProfileBoredPrism();
    Panel.Expect("Both exact slot-support fixtures are constructed", RectSlotDeliver && SlotBoreDeliver);
    if (!RectSlotDeliver || !SlotBoreDeliver) return Panel.Conclude();

    const BrepBody RectSlot = RectSlotDeliver.Payload, RectSlotSnapshot = RectSlot;
    const BrepBody SlotBore = SlotBoreDeliver.Payload, SlotBoreSnapshot = SlotBore;
    const BodyReport RectSlotBefore = RectSlot.Validate(), SlotBoreBefore = SlotBore.Validate();
    const int RectSlotTop = FaceToward(RectSlot, Vec3::UnitZ()), RectSlotBottom = FaceToward(RectSlot, -Vec3::UnitZ()),
              RectSlotSide = FaceToward(RectSlot, Vec3::UnitX());
    const int SlotBoreTop = FaceToward(SlotBore, Vec3::UnitZ()), SlotBoreBottom = FaceToward(SlotBore, -Vec3::UnitZ()),
              SlotBoreSide = FaceToward(SlotBore, Vec3::UnitY());

    Panel.Section("Exact slot-support source recognition");
    Panel.Expect("Rectangular slot-holed source is closed genus-one V10/E15/C30/L9/F7",
                 ExactCounts(RectSlot, 1, 10, 15, 30, 9, 7) && ExactFaceCensus(RectSlot, 2, 5) &&
                 RectangularSlotHoledCap(RectSlot, RectSlotTop));
    Panel.Expect("Slot-profile bored source is closed genus-one V4/E6/C12/L6/F4",
                 ExactCounts(SlotBore, 1, 4, 6, 12, 6, 4) && ExactFaceCensus(SlotBore, 2, 2) &&
                 SlotProfileBoredCap(SlotBore, SlotBoreTop));
    Panel.Expect("Each source carries exactly two exact thirteen-pole slot curves",
                 ExactSupportCensus(RectSlot, 13, 0, 2) && ExactSupportCensus(SlotBore, 2, 2, 2));
    Panel.Expect("Each source selects distinct upper, lower, and side faces",
                 RectSlotTop != RectSlotBottom && RectSlotTop != RectSlotSide &&
                 SlotBoreTop != SlotBoreBottom && SlotBoreTop != SlotBoreSide);

    Panel.Section("Multiple positive upper-cap distances");
    const double RectSlotArea = 4.0 * RectHalfX * RectHalfY - SlotArea;
    const double SlotBoreArea = SlotArea - ScalarCriteria::Pi * BoreRadius * BoreRadius;
    for (const double Distance : { OffsetA, OffsetB, OffsetC })
    {
        const auto RectSlotResult = FaceEditSolver::OffsetExtrudedRectangularSlotHoledPrism(RectSlot, RectSlotTop, Distance);
        const auto SlotBoreResult = FaceEditSolver::OffsetExtrudedSlotProfileCircularBoredPrism(SlotBore, SlotBoreTop, Distance);
        Panel.Expect("Rectangular slot-holed offset retains genus-one topology and slot hole",
                     RectSlotResult && ExactCounts(RectSlotResult.Payload, 1, 10, 15, 30, 9, 7) &&
                     ExactFaceCensus(RectSlotResult.Payload, 2, 5) &&
                     RectangularSlotHoledCap(RectSlotResult.Payload, FaceToward(RectSlotResult.Payload, Vec3::UnitZ())));
        Panel.Expect("Slot-profile bored offset retains genus-one topology and circular bore",
                     SlotBoreResult && ExactCounts(SlotBoreResult.Payload, 1, 4, 6, 12, 6, 4) &&
                     ExactFaceCensus(SlotBoreResult.Payload, 2, 2) &&
                     SlotBoreResult.Payload.Validate().Volume > 0.0 &&
                     SlotProfileBoredCap(SlotBoreResult.Payload, FaceToward(SlotBoreResult.Payload, Vec3::UnitZ())));
        if (RectSlotResult) Panel.Within("Rectangular slot-holed volume follows the exact rectangle/slot identity",
                                         std::fabs(RectSlotResult.Payload.Validate().Volume - RectSlotArea * (Height + Distance)) /
                                             (RectSlotArea * (Height + Distance)), 1e-3);
        if (SlotBoreResult) Panel.Within("Slot-profile bored volume follows the exact slot/bore identity",
                                         std::fabs(SlotBoreResult.Payload.Validate().Volume - SlotBoreArea * (Height + Distance)) /
                                             (SlotBoreArea * (Height + Distance)), 1e-3);
    }
    const auto DispatchedRectSlot = FaceEditSolver::OffsetFace(RectSlot, RectSlotTop, OffsetB);
    const auto DispatchedSlotBore = FaceEditSolver::OffsetFace(SlotBore, SlotBoreTop, OffsetB);
    const auto StrictRectSlot = FaceEditSolver::OffsetExtrudedRectangularSlotHoledPrism(RectSlot, RectSlotTop, OffsetB);
    const auto StrictSlotBore = FaceEditSolver::OffsetExtrudedSlotProfileCircularBoredPrism(SlotBore, SlotBoreTop, OffsetB);
    Panel.Expect("The public dispatcher reaches both new slot routes with identical results",
                 DispatchedRectSlot && DispatchedSlotBore && StrictRectSlot && StrictSlotBore &&
                 ExactCounts(DispatchedRectSlot.Payload, 1, 10, 15, 30, 9, 7) &&
                 ExactCounts(DispatchedSlotBore.Payload, 1, 4, 6, 12, 6, 4) &&
                 std::fabs(DispatchedRectSlot.Payload.Validate().Volume - StrictRectSlot.Payload.Validate().Volume) <= 1e-9 &&
                 std::fabs(DispatchedSlotBore.Payload.Validate().Volume - StrictSlotBore.Payload.Validate().Volume) <= 1e-9);
    Panel.Expect("Both new slot reconstructions preserve their sources",
                 SameSource(RectSlot, RectSlotSnapshot, RectSlotBefore) && SameSource(SlotBore, SlotBoreSnapshot, SlotBoreBefore));

    Panel.Section("Transactional refusal boundaries");
    Panel.Expect("Only the rectangular slot-holed upper cap is supported",
                 !FaceEditSolver::OffsetExtrudedRectangularSlotHoledPrism(RectSlot, RectSlotBottom, OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedRectangularSlotHoledPrism(RectSlot, RectSlotSide, OffsetB));
    Panel.Expect("Only the slot-profile bored upper cap is supported",
                 !FaceEditSolver::OffsetExtrudedSlotProfileCircularBoredPrism(SlotBore, SlotBoreBottom, OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedSlotProfileCircularBoredPrism(SlotBore, SlotBoreSide, OffsetB));
    Panel.Expect("The two strict slot routes refuse each other's source",
                 !FaceEditSolver::OffsetExtrudedRectangularSlotHoledPrism(SlotBore, SlotBoreTop, OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedSlotProfileCircularBoredPrism(RectSlot, RectSlotTop, OffsetB));
    Panel.Expect("Zero, negative, and non-finite distances refuse for both routes",
                 !FaceEditSolver::OffsetExtrudedRectangularSlotHoledPrism(RectSlot, RectSlotTop, 0.0) &&
                 !FaceEditSolver::OffsetExtrudedRectangularSlotHoledPrism(RectSlot, RectSlotTop, -0.1) &&
                 !FaceEditSolver::OffsetExtrudedRectangularSlotHoledPrism(RectSlot, RectSlotTop, std::numeric_limits<double>::infinity()) &&
                 !FaceEditSolver::OffsetExtrudedSlotProfileCircularBoredPrism(SlotBore, SlotBoreTop, 0.0) &&
                 !FaceEditSolver::OffsetExtrudedSlotProfileCircularBoredPrism(SlotBore, SlotBoreTop, -0.1) &&
                 !FaceEditSolver::OffsetExtrudedSlotProfileCircularBoredPrism(SlotBore, SlotBoreTop, std::numeric_limits<double>::quiet_NaN()));
    const BrepBody RectSlotMoved = RectSlot.Transformed(Mat4::Translation({ 2, 0, 0 }));
    const BrepBody SlotBoreMoved = SlotBore.Transformed(Mat4::Translation({ 0, 2, 0 }));
    Panel.Expect("Translated slot sources refuse the anchored routes",
                 !FaceEditSolver::OffsetExtrudedRectangularSlotHoledPrism(RectSlotMoved, FaceToward(RectSlotMoved, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedSlotProfileCircularBoredPrism(SlotBoreMoved, FaceToward(SlotBoreMoved, Vec3::UnitZ()), OffsetB));

    const auto ShortSlot = RectangularSlotHoledPrism(2.0, SlotRadius);
    const auto ThinSlot = RectangularSlotHoledPrism(HalfSpan, 1.2);
    const auto NarrowRect = RectangularSlotHoledPrism(HalfSpan, SlotRadius, 7.5, RectHalfY);
    const auto ShallowRect = RectangularSlotHoledPrism(HalfSpan, SlotRadius, RectHalfX, 4.5);
    const auto SmallBore = SlotProfileBoredPrism(HalfSpan, SlotRadius, 0.6);
    const auto ShortProfile = SlotProfileBoredPrism(2.5, SlotRadius, BoreRadius);
    const auto ThinProfile = SlotProfileBoredPrism(HalfSpan, 1.3, BoreRadius);
    Panel.Expect("Non-canonical rectangle, slot, and bore dimensions remain refused",
                 ShortSlot && ThinSlot && NarrowRect && ShallowRect && SmallBore && ShortProfile && ThinProfile &&
                 !FaceEditSolver::OffsetExtrudedRectangularSlotHoledPrism(ShortSlot.Payload, FaceToward(ShortSlot.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedRectangularSlotHoledPrism(ThinSlot.Payload, FaceToward(ThinSlot.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedRectangularSlotHoledPrism(NarrowRect.Payload, FaceToward(NarrowRect.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedRectangularSlotHoledPrism(ShallowRect.Payload, FaceToward(ShallowRect.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedSlotProfileCircularBoredPrism(SmallBore.Payload, FaceToward(SmallBore.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedSlotProfileCircularBoredPrism(ShortProfile.Payload, FaceToward(ShortProfile.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedSlotProfileCircularBoredPrism(ThinProfile.Payload, FaceToward(ThinProfile.Payload, Vec3::UnitZ()), OffsetB));

    const auto TwinSlotHoled = [&]() noexcept
    {
        Workplane Plane;
        const auto Outer = NurbsCurve::Rectangle(Plane, { -RectHalfX, -RectHalfY }, { RectHalfX, RectHalfY });
        const auto Left = SlotAt(2.0, 1.2, { -3.2, 0.0 });
        const auto Right = SlotAt(2.0, 1.2, { 3.2, 0.0 });
        if (!Outer || !Left || !Right) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "twin slot-holed fixture");
        return BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, Left.Payload, Right.Payload }, Vec3::UnitZ(), Height);
    }();
    const auto TwinBored = [&]() noexcept
    {
        const auto Outer = SlotAt();
        const auto Left = NurbsCurve::Circle({ 0, 0, 0 }, Vec3::UnitZ(), BoreRadius);
        const auto Right = NurbsCurve::Circle({ -2.4, 0, 0 }, Vec3::UnitZ(), 0.5);
        if (!Outer || !Left || !Right) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "twin bored fixture");
        return BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, Left.Payload, Right.Payload }, Vec3::UnitZ(), Height);
    }();
    const auto EllipticalBore = [&]() noexcept
    {
        const auto Outer = SlotAt();
        const auto Inner = NurbsCurve::Ellipse({ 0, 0, 0 }, Vec3::UnitZ(), Vec3::UnitX(), 1.4, 0.8);
        if (!Outer || !Inner) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "elliptical bore fixture");
        return BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, Inner.Payload }, Vec3::UnitZ(), Height);
    }();
    const auto EccentricBore = SlotProfileBoredPrism(HalfSpan, SlotRadius, BoreRadius, 1.5);
    Panel.Expect("Wrong slot counts and mismatched bore supports stay outside the new routes",
                 TwinSlotHoled && TwinBored && EllipticalBore && EccentricBore &&
                 !FaceEditSolver::OffsetExtrudedRectangularSlotHoledPrism(TwinSlotHoled.Payload, FaceToward(TwinSlotHoled.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedSlotProfileCircularBoredPrism(TwinBored.Payload, FaceToward(TwinBored.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedSlotProfileCircularBoredPrism(EllipticalBore.Payload, FaceToward(EllipticalBore.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedSlotProfileCircularBoredPrism(EccentricBore.Payload, FaceToward(EccentricBore.Payload, Vec3::UnitZ()), OffsetB));

    const auto CircularHoledRect = [&]() noexcept
    {
        Workplane Plane;
        const auto Outer = NurbsCurve::Rectangle(Plane, { -RectHalfX, -RectHalfY }, { RectHalfX, RectHalfY });
        const auto Hole = NurbsCurve::Circle({ 0, 0, 0 }, Vec3::UnitZ(), SlotRadius);
        if (!Outer || !Hole) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "circular holed rectangle fixture");
        return BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, Hole.Payload }, Vec3::UnitZ(), Height);
    }();
    const auto CircularBoredEllipse = [&]() noexcept
    {
        const auto Outer = NurbsCurve::Ellipse({ 0, 0, 0 }, Vec3::UnitZ(), Vec3::UnitX(), 6.0, 4.0);
        const auto Hole = NurbsCurve::Circle({ 2, 0, 0 }, Vec3::UnitZ(), 1.2);
        if (!Outer || !Hole) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "circular bored ellipse fixture");
        return BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, Hole.Payload }, Vec3::UnitZ(), Height);
    }();
    Panel.Expect("Earlier circular-holed and bored-elliptical domains still route exactly",
                 CircularHoledRect && CircularBoredEllipse &&
                 FaceEditSolver::OffsetExtrudedHoledPrism(CircularHoledRect.Payload, FaceToward(CircularHoledRect.Payload, Vec3::UnitZ()), OffsetB) &&
                 FaceEditSolver::OffsetCircularBoredEllipticalPrism(CircularBoredEllipse.Payload, FaceToward(CircularBoredEllipse.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedRectangularSlotHoledPrism(CircularHoledRect.Payload, FaceToward(CircularHoledRect.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedSlotProfileCircularBoredPrism(CircularBoredEllipse.Payload, FaceToward(CircularBoredEllipse.Payload, Vec3::UnitZ()), OffsetB));

    const auto HoleFreeSlot = [&]() noexcept
    {
        const auto Outer = SlotAt();
        return Outer ? BrepBody::Extrude(Outer.Payload, Vec3::UnitZ(), Height)
                     : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "hole-free slot fixture");
    }();
    Panel.Expect("The hole-free slot prism remains outside both new slot routes",
                 HoleFreeSlot && ExactCounts(HoleFreeSlot.Payload, 0, 2, 3, 6, 3, 3) &&
                 !FaceEditSolver::OffsetExtrudedRectangularSlotHoledPrism(HoleFreeSlot.Payload, FaceToward(HoleFreeSlot.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedSlotProfileCircularBoredPrism(HoleFreeSlot.Payload, FaceToward(HoleFreeSlot.Payload, Vec3::UnitZ()), OffsetB));

    BrepBody MalformedRectSlot = RectSlot; MalformedRectSlot.Edges.front().Coedges.push_back(MalformedRectSlot.Edges.front().Coedges.front());
    BrepBody MalformedSlotBore = SlotBore; MalformedSlotBore.Edges.front().Coedges.push_back(MalformedSlotBore.Edges.front().Coedges.front());
    Panel.Expect("Malformed slot sources refuse without healing",
                 !FaceEditSolver::OffsetExtrudedRectangularSlotHoledPrism(MalformedRectSlot, RectSlotTop, OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedSlotProfileCircularBoredPrism(MalformedSlotBore, SlotBoreTop, OffsetB));
    Panel.Expect("All refusal paths preserve both valid sources",
                 SameSource(RectSlot, RectSlotSnapshot, RectSlotBefore) && SameSource(SlotBore, SlotBoreSnapshot, SlotBoreBefore));

    Panel.Section("Durable slot-support contact sheet");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Batch63_RectangularSlotHoledAndSlotProfileBoredFaceOffset.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    const auto ProofRectSlot = FaceEditSolver::OffsetExtrudedRectangularSlotHoledPrism(RectSlot, RectSlotTop, OffsetB);
    const auto ProofSlotBore = FaceEditSolver::OffsetExtrudedSlotProfileCircularBoredPrism(SlotBore, SlotBoreTop, OffsetB);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1800, 900);
    const bool Added = ProofRectSlot && ProofSlotBore &&
        Host.Document().AddBody("SharpRectangularSlotHoled", RectSlot.Transformed(Mat4::Translation({ -13, -8, 0 }))).Identity > 0 &&
        Host.Document().AddBody("OffsetRectangularSlotHoled", ProofRectSlot.Payload.Transformed(Mat4::Translation({ -13, 8, 0 }))).Identity > 0 &&
        Host.Document().AddBody("SharpSlotProfileBored", SlotBore.Transformed(Mat4::Translation({ 9, -8, 0 }))).Identity > 0 &&
        Host.Document().AddBody("OffsetSlotProfileBored", ProofSlotBore.Payload.Transformed(Mat4::Translation({ 9, 8, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") && Host.Execute("view orbit 25 -18") &&
        Host.Execute("view fit") && Host.Execute("render Batch63_RectangularSlotHoledAndSlotProfileBoredFaceOffset");
    Panel.Expect("The consolidated slot-support proof render completes", Rendered);
    Panel.Expect("The durable slot-support proof PNG is visible",
                 std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
