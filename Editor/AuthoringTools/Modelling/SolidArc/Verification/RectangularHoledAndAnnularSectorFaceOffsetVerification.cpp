//=============================================================================================================================================
// SolidArc · Batch 55 · exact rectangular-frame and quarter annular-sector face offsets
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
constexpr double OuterRadius = 5.0;
constexpr double InnerRadius = 3.0;
constexpr double SectorSweep = ScalarCriteria::HalfPi;

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
           After.Loops == Before.Loops && After.EulerCharacteristic == Before.EulerCharacteristic &&
           After.Genus == Before.Genus && After.OpenEdges == Before.OpenEdges && After.NonManifoldEdges == Before.NonManifoldEdges &&
           After.MisorientedEdges == Before.MisorientedEdges && std::fabs(After.Volume - Before.Volume) <= 1e-12 &&
           Body.Vertices.size() == Snapshot.Vertices.size() && Body.Edges.size() == Snapshot.Edges.size() &&
           Body.Coedges.size() == Snapshot.Coedges.size() && Body.Loops.size() == Snapshot.Loops.size() && Body.Faces.size() == Snapshot.Faces.size();
}

[[nodiscard]] bool AllPlanarOrExtrusion(const BrepBody& Body) noexcept
{
    for (const BrepFace& F : Body.Faces)
        if (F.Surface.Classification != SurfaceClassification::Plane && F.Surface.Classification != SurfaceClassification::Extrusion) return false;
    return true;
}

[[nodiscard]] bool IsRectangularCap(const BrepBody& Body, int Face, bool Upper) noexcept
{
    if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return false;
    const BrepFace& F = Body.Faces[Face];
    if (F.Surface.Classification != SurfaceClassification::Plane || F.Loops.size() != 2) return false;
    const Vec3 N = Body.FaceNormal(Face, 0.5 * (F.Surface.DomainStartU() + F.Surface.DomainEndU()),
                                   0.5 * (F.Surface.DomainStartV() + F.Surface.DomainEndV())).Normalised();
    if ((Upper && N.Dot(Vec3::UnitZ()) < 1.0 - 1e-6) || (!Upper && N.Dot(-Vec3::UnitZ()) < 1.0 - 1e-6)) return false;
    for (int Loop : F.Loops)
    {
        if (Loop < 0 || Loop >= static_cast<int>(Body.Loops.size()) || Body.Loops[Loop].Coedges.size() != 4) return false;
        for (int Coedge : Body.Loops[Loop].Coedges)
        {
            if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return false;
            const int Edge = Body.Coedges[Coedge].Edge;
            if (Edge < 0 || Edge >= static_cast<int>(Body.Edges.size()) || Body.Edges[Edge].Curve.Classification != CurveClassification::Line) return false;
        }
    }
    return true;
}

[[nodiscard]] bool IsAnnularSectorCap(const BrepBody& Body, int Face, bool Upper) noexcept
{
    if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return false;
    const BrepFace& F = Body.Faces[Face];
    if (F.Surface.Classification != SurfaceClassification::Plane || F.Loops.size() != 1) return false;
    const Vec3 N = Body.FaceNormal(Face, 0.5 * (F.Surface.DomainStartU() + F.Surface.DomainEndU()),
                                   0.5 * (F.Surface.DomainStartV() + F.Surface.DomainEndV())).Normalised();
    if ((Upper && N.Dot(Vec3::UnitZ()) < 1.0 - 1e-6) || (!Upper && N.Dot(-Vec3::UnitZ()) < 1.0 - 1e-6)) return false;
    const int Loop = F.Loops.front();
    if (Loop < 0 || Loop >= static_cast<int>(Body.Loops.size()) || Body.Loops[Loop].Coedges.size() != 4) return false;
    const auto IsExactLine = [](const NurbsCurve& C) noexcept {
        if (C.Classification == CurveClassification::Line && C.Degree == 1) return true;
        if (C.Classification != CurveClassification::Freeform || C.Rational() || C.PoleCount() < 2) return false;
        const Vec3 A = C.StartPoint(), B = C.EndPoint(), D = B - A;
        if (D.Length() <= ScalarCriteria::MergeTolerance) return false;
        for (int I = 1; I < 4; ++I)
            if ((C.Sample(C.DomainStart() + (C.DomainEnd() - C.DomainStart()) * I / 4.0) - A).Cross(D).Length() > ScalarCriteria::GeometricTolerance * D.Length()) return false;
        return true;
    };
    int Arcs = 0, Lines = 0;
    for (int Coedge : Body.Loops[Loop].Coedges)
    {
        if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return false;
        const int Edge = Body.Coedges[Coedge].Edge;
        if (Edge < 0 || Edge >= static_cast<int>(Body.Edges.size())) return false;
        const NurbsCurve& C = Body.Edges[Edge].Curve;
        if (C.Classification == CurveClassification::Arc && C.Degree == 2 && C.Rational() && !C.Closed()) ++Arcs;
        else if (IsExactLine(C)) ++Lines;
        else return false;
    }
    return Arcs == 2 && Lines == 2;
}

[[nodiscard]] Deliver<BrepBody> RectangularFrame() noexcept
{
    Workplane W;
    const auto Outer = NurbsCurve::Rectangle(W, { -6, -4 }, { 6, 4 });
    const auto Inner = NurbsCurve::Rectangle(W, { -2, -1 }, { 2, 1 });
    return Outer && Inner ? BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, Inner.Payload }, Vec3::UnitZ(), Height)
                          : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "rectangular-frame fixture");
}

[[nodiscard]] Deliver<BrepBody> AnnularSector() noexcept
{
    const Vec3 Centre{}, Axis = Vec3::UnitZ();
    const Vec3 OuterStart{ OuterRadius, 0, 0 }, OuterEnd{ 0, OuterRadius, 0 };
    const Vec3 InnerStart{ InnerRadius, 0, 0 }, InnerEnd{ 0, InnerRadius, 0 };
    const auto Outer = NurbsCurve::Arc(Centre, Axis, OuterRadius, 0.0, SectorSweep);
    const auto End = NurbsCurve::Line(OuterEnd, InnerEnd);
    const auto Inner = NurbsCurve::Arc(Centre, Axis, InnerRadius, SectorSweep, -SectorSweep);
    const auto Start = NurbsCurve::Line(InnerStart, OuterStart);
    if (!Outer || !End || !Inner || !Start) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "annular-sector fixture curves");
    const auto J1 = NurbsCurve::Join(Outer.Payload, End.Payload);
    const auto J2 = J1 ? NurbsCurve::Join(J1.Payload, Inner.Payload) : Deliver<NurbsCurve>::Reject(RefusalReason::NonManifold, "annular-sector fixture join");
    const auto J3 = J2 ? NurbsCurve::Join(J2.Payload, Start.Payload) : Deliver<NurbsCurve>::Reject(RefusalReason::NonManifold, "annular-sector fixture join");
    return J3 && J3.Payload.Closed() ? BrepBody::Extrude(J3.Payload, Axis, Height)
                                     : Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "annular-sector fixture is open");
}

[[nodiscard]] bool OffsetHasTopology(const Deliver<BrepBody>& Result, std::size_t V, std::size_t E, std::size_t C,
                                     std::size_t L, std::size_t F, int Genus) noexcept
{
    if (!Result) return false;
    const BodyReport R = Result.Payload.Validate();
    return R.Solid() && R.Hulls == 1 && R.Genus == Genus && R.OpenEdges == 0 && R.NonManifoldEdges == 0 && R.MisorientedEdges == 0 &&
           Result.Payload.Vertices.size() == V && Result.Payload.Edges.size() == E && Result.Payload.Coedges.size() == C &&
           Result.Payload.Loops.size() == L && Result.Payload.Faces.size() == F && AllPlanarOrExtrusion(Result.Payload);
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Batch 55 · rectangular-frame and annular-sector face offsets");
    const auto RectDeliver = RectangularFrame();
    const auto SectorDeliver = AnnularSector();
    Panel.Expect("Both exact batch fixtures are constructed", RectDeliver && SectorDeliver);
    if (!RectDeliver || !SectorDeliver) return Panel.Conclude();

    const BrepBody Rect = RectDeliver.Payload, RectSnapshot = Rect;
    const BrepBody Sector = SectorDeliver.Payload, SectorSnapshot = Sector;
    const BodyReport RectBefore = Rect.Validate(), SectorBefore = Sector.Validate();
    const int RectTop = FaceToward(Rect, Vec3::UnitZ()), RectBottom = FaceToward(Rect, -Vec3::UnitZ()), RectSide = FaceToward(Rect, Vec3::UnitX());
    const int SectorTop = FaceToward(Sector, Vec3::UnitZ()), SectorBottom = FaceToward(Sector, -Vec3::UnitZ()), SectorSide = FaceToward(Sector, Vec3::UnitX());
    Panel.Section("Exact source recognition");
    Panel.Expect("Rectangular frame is closed genus-one V16/E24/C48/L12/F10", RectBefore.Solid() && RectBefore.Hulls == 1 && RectBefore.Genus == 1 &&
                 Rect.Vertices.size() == 16 && Rect.Edges.size() == 24 && Rect.Coedges.size() == 48 && Rect.Loops.size() == 12 && Rect.Faces.size() == 10);
    Panel.Expect("Rectangular frame has two exact planar annular caps", IsRectangularCap(Rect, RectTop, true) && IsRectangularCap(Rect, RectBottom, false));
    Panel.Expect("Annular sector is closed genus-zero V8/E12/C24/L6/F6", SectorBefore.Solid() && SectorBefore.Hulls == 1 && SectorBefore.Genus == 0 &&
                 Sector.Vertices.size() == 8 && Sector.Edges.size() == 12 && Sector.Coedges.size() == 24 && Sector.Loops.size() == 6 && Sector.Faces.size() == 6);
    Panel.Expect("Annular sector has two exact rational arcs and two radial lines per cap", IsAnnularSectorCap(Sector, SectorTop, true) && IsAnnularSectorCap(Sector, SectorBottom, false));
    Panel.Expect("Each route selects a distinct upper, lower, and side face", RectTop != RectBottom && RectTop != RectSide && SectorTop != SectorBottom && SectorTop != SectorSide);
    Panel.Expect("Both source families remain planar or analytic extrusion geometry", AllPlanarOrExtrusion(Rect) && AllPlanarOrExtrusion(Sector));
    Panel.Section("Multiple positive distances and public dispatch");
    for (const double Distance : { OffsetA, OffsetB, OffsetC })
    {
        const auto RectResult = FaceEditSolver::OffsetExtrudedRectangularHoledPrism(Rect, RectTop, Distance);
        const auto SectorResult = FaceEditSolver::OffsetExtrudedAnnularSectorPrism(Sector, SectorTop, Distance);
        const double RectExpected = (12.0 * 8.0 - 4.0 * 2.0) * (Height + Distance);
        const double SectorExpected = ScalarCriteria::Pi * (OuterRadius * OuterRadius - InnerRadius * InnerRadius) * 0.25 * (Height + Distance);
        Panel.Expect("Rectangular-frame offset retains closed genus-one topology", OffsetHasTopology(RectResult, 16, 24, 48, 12, 10, 1));
        Panel.Expect("Rectangular-frame result retains two planar rectangular cap loops", RectResult && IsRectangularCap(RectResult.Payload, FaceToward(RectResult.Payload, Vec3::UnitZ()), true));
        Panel.Expect("Annular-sector offset retains closed genus-zero topology", OffsetHasTopology(SectorResult, 8, 12, 24, 6, 6, 0));
        Panel.Expect("Annular-sector result retains two rational arcs and two radial cap lines", SectorResult && IsAnnularSectorCap(SectorResult.Payload, FaceToward(SectorResult.Payload, Vec3::UnitZ()), true));
        if (RectResult) Panel.Within("Rectangular-frame volume matches exact annular area", std::fabs(RectResult.Payload.Validate().Volume - RectExpected) / RectExpected, 1e-9);
        if (SectorResult) Panel.Within("Annular-sector volume matches exact sector area", std::fabs(SectorResult.Payload.Validate().Volume - SectorExpected) / SectorExpected, 1e-3);
    }
    Panel.Expect("The dispatcher reaches both bounded routes", FaceEditSolver::OffsetFace(Rect, RectTop, OffsetB) && FaceEditSolver::OffsetFace(Sector, SectorTop, OffsetB));
    Panel.Expect("Rectangular route preserves its source transactionally", SameSource(Rect, RectSnapshot, RectBefore));
    Panel.Expect("Annular-sector route preserves its source transactionally", SameSource(Sector, SectorSnapshot, SectorBefore));

    Panel.Section("Transactional refusal boundaries");
    Panel.Expect("Only rectangular upper cap is supported", !FaceEditSolver::OffsetExtrudedRectangularHoledPrism(Rect, RectBottom, OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedRectangularHoledPrism(Rect, RectSide, OffsetB));
    Panel.Expect("Only annular-sector upper cap is supported", !FaceEditSolver::OffsetExtrudedAnnularSectorPrism(Sector, SectorBottom, OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedAnnularSectorPrism(Sector, SectorSide, OffsetB));
    Panel.Expect("Zero, negative, and non-finite distances refuse", !FaceEditSolver::OffsetExtrudedRectangularHoledPrism(Rect, RectTop, 0.0) &&
                 !FaceEditSolver::OffsetExtrudedRectangularHoledPrism(Rect, RectTop, -0.1) &&
                 !FaceEditSolver::OffsetExtrudedRectangularHoledPrism(Rect, RectTop, std::numeric_limits<double>::infinity()) &&
                 !FaceEditSolver::OffsetExtrudedAnnularSectorPrism(Sector, SectorTop, 0.0) &&
                 !FaceEditSolver::OffsetExtrudedAnnularSectorPrism(Sector, SectorTop, -0.1) &&
                 !FaceEditSolver::OffsetExtrudedAnnularSectorPrism(Sector, SectorTop, std::numeric_limits<double>::infinity()));
    BrepBody MalformedRect = Rect; MalformedRect.Edges.front().Coedges.push_back(MalformedRect.Edges.front().Coedges.front());
    BrepBody MalformedSector = Sector; MalformedSector.Edges.front().Coedges.push_back(MalformedSector.Edges.front().Coedges.front());
    Panel.Expect("Malformed inputs refuse without healing", !FaceEditSolver::OffsetExtrudedRectangularHoledPrism(MalformedRect, RectTop, OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedAnnularSectorPrism(MalformedSector, SectorTop, OffsetB));
    const auto TranslatedRectDeliver = RectangularFrame();
    const auto TranslatedSectorDeliver = AnnularSector();
    const BrepBody TranslatedRect = TranslatedRectDeliver.Payload.Transformed(Mat4::Translation({ 2, 0, 0 }));
    const BrepBody TranslatedSector = TranslatedSectorDeliver.Payload.Transformed(Mat4::Translation({ 2, 0, 0 }));
    Panel.Expect("Translated fixtures refuse the origin-anchored exact routes", !FaceEditSolver::OffsetExtrudedRectangularHoledPrism(TranslatedRect, FaceToward(TranslatedRect, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedAnnularSectorPrism(TranslatedSector, FaceToward(TranslatedSector, Vec3::UnitZ()), OffsetB));
    Panel.Expect("The domains refuse each other's profiles", !FaceEditSolver::OffsetExtrudedRectangularHoledPrism(Sector, SectorTop, OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedAnnularSectorPrism(Rect, RectTop, OffsetB));
    Panel.Expect("All refusal paths preserve both sources", SameSource(Rect, RectSnapshot, RectBefore) && SameSource(Sector, SectorSnapshot, SectorBefore));

    Panel.Section("Durable batch contact sheet");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Batch55_RectangularHoledAndAnnularSectorFaceOffset.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    const auto RectResult = FaceEditSolver::OffsetExtrudedRectangularHoledPrism(Rect, RectTop, OffsetB);
    const auto SectorResult = FaceEditSolver::OffsetExtrudedAnnularSectorPrism(Sector, SectorTop, OffsetB);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1800, 900);
    const bool Added = RectResult && SectorResult &&
        Host.Document().AddBody("SharpRectangularFrame", Rect.Transformed(Mat4::Translation({ -14, -5, 0 }))).Identity > 0 &&
        Host.Document().AddBody("OffsetRectangularFrame", RectResult.Payload.Transformed(Mat4::Translation({ -4, -5, 0 }))).Identity > 0 &&
        Host.Document().AddBody("SharpAnnularSector", Sector.Transformed(Mat4::Translation({ 6, -5, 0 }))).Identity > 0 &&
        Host.Document().AddBody("OffsetAnnularSector", SectorResult.Payload.Transformed(Mat4::Translation({ 14, -5, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") && Host.Execute("view orbit 25 -18") &&
        Host.Execute("view fit") && Host.Execute("render Batch55_RectangularHoledAndAnnularSectorFaceOffset");
    Panel.Expect("The consolidated proof render completes", Rendered);
    Panel.Expect("The durable batch proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
