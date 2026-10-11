//=============================================================================================================================================
// SolidArc · Phase 42 · bounded genus-one holed-prism face offset
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
constexpr double Width = 12.0;
constexpr double Depth = 9.0;
constexpr double Height = 6.0;
constexpr double HoleRadius = 1.5;
constexpr double Offset = 1.5;

[[nodiscard]] Deliver<BrepBody> HoledPrism(int Holes = 1, bool Circular = true) noexcept
{
    Workplane Plane;
    const Deliver<NurbsCurve> Outer = NurbsCurve::Rectangle(Plane, { -Width / 2.0, -Depth / 2.0 }, { Width / 2.0, Depth / 2.0 });
    if (!Outer) return Deliver<BrepBody>::Reject(Outer.Denial.Reason, Outer.Denial.Detail);
    std::vector<NurbsCurve> Loops{ Outer.Payload };
    for (int I = 0; I < Holes; ++I)
    {
        const double X = (I - (Holes - 1) * 0.5) * 5.0;
        Deliver<NurbsCurve> Hole = Circular
            ? NurbsCurve::Circle({ X, 0, 0 }, Vec3::UnitZ(), HoleRadius)
            : NurbsCurve::Rectangle(Plane, { X - 1.0, -1.0 }, { X + 1.0, 1.0 });
        if (!Hole) return Deliver<BrepBody>::Reject(Hole.Denial.Reason, Hole.Denial.Detail);
        Loops.push_back(std::move(Hole.Payload));
    }
    return BrepBody::Extrude(Loops, Vec3::UnitZ(), Height);
}

[[nodiscard]] Deliver<BrepBody> PentagonPrism() noexcept
{
    std::vector<Vec3> Points;
    for (int I = 0; I < 5; ++I)
    {
        const double A = ScalarCriteria::TwoPi * static_cast<double>(I) / 5.0;
        Points.push_back({ 4.5 * std::cos(A), 4.5 * std::sin(A), 0.0 });
    }
    const Deliver<NurbsCurve> Profile = NurbsCurve::Polyline(Points, true);
    return Profile ? BrepBody::Extrude(Profile.Payload, Vec3::UnitZ(), Height)
                   : Deliver<BrepBody>::Reject(Profile.Denial.Reason, Profile.Denial.Detail);
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
           After.Loops == Before.Loops && After.EulerCharacteristic == Before.EulerCharacteristic &&
           After.Genus == Before.Genus && After.OpenEdges == Before.OpenEdges &&
           After.NonManifoldEdges == Before.NonManifoldEdges && After.MisorientedEdges == Before.MisorientedEdges &&
           std::fabs(After.Volume - Before.Volume) <= 1e-12 && Body.Vertices.size() == Snapshot.Vertices.size() &&
           Body.Edges.size() == Snapshot.Edges.size() && Body.Coedges.size() == Snapshot.Coedges.size() &&
           Body.Loops.size() == Snapshot.Loops.size() && Body.Faces.size() == Snapshot.Faces.size();
}

[[nodiscard]] bool IsAnnularCap(const BrepBody& Body, int Face, bool Upper) noexcept
{
    if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return false;
    const BrepFace& F = Body.Faces[Face];
    if (F.Surface.Classification != SurfaceClassification::Plane || F.Loops.size() != 2) return false;
    const Vec3 N = Body.FaceNormal(Face, 0.5 * (F.Surface.DomainStartU() + F.Surface.DomainEndU()),
                                   0.5 * (F.Surface.DomainStartV() + F.Surface.DomainEndV())).Normalised();
    if ((Upper && N.Dot(Vec3::UnitZ()) < 1.0 - 1e-6) || (!Upper && N.Dot(-Vec3::UnitZ()) < 1.0 - 1e-6)) return false;
    int OuterLoops = 0, HoleLoops = 0, CircleEdges = 0;
    for (int Loop : F.Loops)
    {
        if (Loop < 0 || Loop >= static_cast<int>(Body.Loops.size())) return false;
        const BrepLoop& L = Body.Loops[Loop];
        if (L.Outer && L.Coedges.size() == 4) ++OuterLoops;
        else if (!L.Outer && L.Coedges.size() == 1)
        {
            ++HoleLoops;
            const int C = L.Coedges.front();
            if (C < 0 || C >= static_cast<int>(Body.Coedges.size())) return false;
            const int E = Body.Coedges[C].Edge;
            if (E < 0 || E >= static_cast<int>(Body.Edges.size())) return false;
            const BrepEdge& Edge = Body.Edges[E];
            CircleEdges += Edge.Curve.Classification == CurveClassification::Circle && Edge.Curve.Rational() && Edge.Curve.Closed();
        }
        else return false;
    }
    return OuterLoops == 1 && HoleLoops == 1 && CircleEdges == 1;
}

[[nodiscard]] int ExactCircularEdges(const BrepBody& Body) noexcept
{
    int Count = 0;
    for (const BrepEdge& E : Body.Edges)
        Count += E.Curve.Classification == CurveClassification::Circle && E.Curve.Rational() && E.Curve.Closed() && E.Coedges.size() == 2;
    return Count;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 42 · bounded genus-one holed-prism face offset");
    const auto SourceDeliver = HoledPrism();
    Panel.Expect("The rectangular prism with one circular through-hole is constructed", static_cast<bool>(SourceDeliver));
    const BrepBody Source = SourceDeliver.Payload;
    const BrepBody Snapshot = Source;
    const BodyReport Before = Source.Validate();
    const int Top = FaceToward(Source, Vec3::UnitZ());
    const int Bottom = FaceToward(Source, -Vec3::UnitZ());
    Panel.Expect("The source has genus-one V10/E15/C30/L9/F7 topology", SourceDeliver && Before.Solid() && Before.Hulls == 1 &&
                 Before.Genus == 1 && Source.Vertices.size() == 10 && Source.Edges.size() == 15 &&
                 Source.Coedges.size() == 30 && Source.Loops.size() == 9 && Source.Faces.size() == 7);
    Panel.Expect("Both planar caps are annular and the upper cap is selected", IsAnnularCap(Source, Top, true) &&
                 IsAnnularCap(Source, Bottom, false) && Top != Bottom);
    Panel.Expect("The source contains exactly two rational circular hole rims", ExactCircularEdges(Source) == 2);

    Panel.Section("Exact genus-one non-box face offset reconstruction");
    const auto Result = SourceDeliver ? FaceEditSolver::OffsetExtrudedHoledPrism(Source, Top, Offset)
                                      : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no holed prism source");
    Panel.Expect("The selected upper annular cap produces a closed genus-one solid", Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport R = Result.Payload.Validate();
        const double Area = Width * Depth - ScalarCriteria::Pi * HoleRadius * HoleRadius;
        const double Expected = Area * (Height + Offset);
        Panel.Expect("The offset retains V10/E15/C30/L9/F7 genus-one topology", R.Hulls == 1 && R.Genus == 1 &&
                     Result.Payload.Vertices.size() == 10 && Result.Payload.Edges.size() == 15 &&
                     Result.Payload.Coedges.size() == 30 && Result.Payload.Loops.size() == 9 &&
                     Result.Payload.Faces.size() == 7 && R.OpenEdges == 0 && R.NonManifoldEdges == 0 &&
                     R.MisorientedEdges == 0);
        Panel.Within("The offset volume follows rectangle area minus the through-hole", std::fabs(R.Volume - Expected), 5e-2);
        Panel.Expect("The result retains annular upper and lower caps", IsAnnularCap(Result.Payload, FaceToward(Result.Payload, Vec3::UnitZ()), true) &&
                     IsAnnularCap(Result.Payload, FaceToward(Result.Payload, -Vec3::UnitZ()), false));
        Panel.Expect("Both exact rational circular hole rims survive", ExactCircularEdges(Result.Payload) == 2);
        Panel.Expect("The result remains analytic planar/extruded geometry", std::all_of(Result.Payload.Faces.begin(), Result.Payload.Faces.end(),
            [](const BrepFace& F) { return F.Surface.Classification == SurfaceClassification::Plane || F.Surface.Classification == SurfaceClassification::Extrusion; }));
        Panel.Expect("The separate reconstruction preserves the source", SameSource(Source, Snapshot, Before));
    }
    const auto Dispatch = FaceEditSolver::OffsetFace(Source, Top, Offset);
    Panel.Expect("The public face-offset dispatcher reaches the genus-one route", Dispatch && Dispatch.Payload.Validate().Genus == 1);
    Panel.Expect("Only the upper annular cap is supported", !FaceEditSolver::OffsetExtrudedHoledPrism(Source, Bottom, Offset));

    Panel.Section("Holed-prism offset refusal boundaries");
    const auto Box = BrepBody::Box({ -6, -4.5, 0 }, { 6, 4.5, Height });
    Panel.Expect("A hole-free box remains outside the holed-prism API", Box && !FaceEditSolver::OffsetExtrudedHoledPrism(Box.Payload, FaceToward(Box.Payload, Vec3::UnitZ()), Offset));
    const auto Pentagon = PentagonPrism();
    Panel.Expect("A hole-free pentagonal prism remains outside the genus-one route", Pentagon && !FaceEditSolver::OffsetExtrudedHoledPrism(Pentagon.Payload, FaceToward(Pentagon.Payload, Vec3::UnitZ()), Offset));
    const auto Dual = HoledPrism(2);
    Panel.Expect("A two-hole prism refuses the single-hole route", Dual && !FaceEditSolver::OffsetExtrudedHoledPrism(Dual.Payload, FaceToward(Dual.Payload, Vec3::UnitZ()), Offset));
    const auto RectangularHole = HoledPrism(1, false);
    Panel.Expect("A non-circular inner loop refuses", RectangularHole && !FaceEditSolver::OffsetExtrudedHoledPrism(RectangularHole.Payload, FaceToward(RectangularHole.Payload, Vec3::UnitZ()), Offset));
    const auto Cylinder = BrepBody::Cylinder({ 0, 0, 0 }, Vec3::UnitZ(), 4.0, Height);
    Panel.Expect("A curved cylinder refuses the rectangular holed-prism route", Cylinder && !FaceEditSolver::OffsetExtrudedHoledPrism(Cylinder.Payload, FaceToward(Cylinder.Payload, Vec3::UnitZ()), Offset));
    Panel.Expect("Side, zero, negative, and non-finite offsets refuse", !FaceEditSolver::OffsetExtrudedHoledPrism(Source, FaceToward(Source, Vec3::UnitX()), Offset) &&
                 !FaceEditSolver::OffsetExtrudedHoledPrism(Source, Top, 0.0) &&
                 !FaceEditSolver::OffsetExtrudedHoledPrism(Source, Top, -0.1) &&
                 !FaceEditSolver::OffsetExtrudedHoledPrism(Source, Top, std::numeric_limits<double>::infinity()));
    BrepBody Malformed = Source;
    Malformed.Edges[0].Coedges.push_back(Malformed.Edges[0].Coedges.front());
    Panel.Expect("A malformed holed prism refuses transactionally", !FaceEditSolver::OffsetExtrudedHoledPrism(Malformed, Top, Offset));
    Panel.Expect("All refusal paths preserve the genus-one source", SameSource(Source, Snapshot, Before));

    Panel.Section("Distinct genus-one face-offset proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) /
        "Phase42_HoledPrismFaceOffset.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = SourceDeliver && Result &&
        Host.Document().AddBody("SharpHoledPrism", Source.Transformed(Mat4::Translation({ -8, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("OffsetHoledPrism", Result.Payload.Transformed(Mat4::Translation({ 8, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 35 -18") && Host.Execute("view fit") &&
        Host.Execute("render Phase42_HoledPrismFaceOffset");
    Panel.Expect("The genus-one holed-prism proof render completes", Rendered);
    Panel.Expect("The genus-one holed-prism proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
