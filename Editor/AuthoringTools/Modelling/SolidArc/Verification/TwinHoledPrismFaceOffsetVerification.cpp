//=============================================================================================================================================
// SolidArc · Phase 46 · bounded genus-two twin-holed-prism face offset
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
constexpr double Width = 16.0;
constexpr double Depth = 10.0;
constexpr double Height = 6.0;
constexpr double HoleRadius = 1.25;
constexpr double Offset = 1.5;

[[nodiscard]] Deliver<BrepBody> TwinPrism(int HoleCount = 2, bool Reverse = false, bool Circular = true, double Separation = 8.0) noexcept
{
    Workplane Plane;
    const Deliver<NurbsCurve> Outer = NurbsCurve::Rectangle(Plane, { -Width / 2.0, -Depth / 2.0 }, { Width / 2.0, Depth / 2.0 });
    if (!Outer) return Deliver<BrepBody>::Reject(Outer.Denial.Reason, Outer.Denial.Detail);
    std::vector<NurbsCurve> Loops{ Outer.Payload };
    for (int I = 0; I < HoleCount; ++I)
    {
        const int J = Reverse ? HoleCount - 1 - I : I;
        const double X = HoleCount == 1 ? 0.0 : (J - (HoleCount - 1) * 0.5) * Separation;
        Deliver<NurbsCurve> Hole = Circular
            ? NurbsCurve::Circle({ X, 0, 0 }, Vec3::UnitZ(), HoleRadius)
            : NurbsCurve::Rectangle(Plane, { X - 1.0, -1.0 }, { X + 1.0, 1.0 });
        if (!Hole) return Deliver<BrepBody>::Reject(Hole.Denial.Reason, Hole.Denial.Detail);
        Loops.push_back(std::move(Hole.Payload));
    }
    return BrepBody::Extrude(Loops, Vec3::UnitZ(), Height);
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
           After.Loops == Before.Loops && After.Genus == Before.Genus && After.OpenEdges == Before.OpenEdges &&
           After.NonManifoldEdges == Before.NonManifoldEdges && After.MisorientedEdges == Before.MisorientedEdges &&
           std::fabs(After.Volume - Before.Volume) <= 1e-12 && Body.Vertices.size() == Snapshot.Vertices.size() &&
           Body.Edges.size() == Snapshot.Edges.size() && Body.Coedges.size() == Snapshot.Coedges.size() &&
           Body.Loops.size() == Snapshot.Loops.size() && Body.Faces.size() == Snapshot.Faces.size();
}

[[nodiscard]] bool IsTwinAnnularCap(const BrepBody& Body, int Face, bool Upper) noexcept
{
    if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return false;
    const BrepFace& F = Body.Faces[Face];
    if (F.Surface.Classification != SurfaceClassification::Plane || F.Loops.size() != 3) return false;
    const Vec3 N = Body.FaceNormal(Face, 0.5 * (F.Surface.DomainStartU() + F.Surface.DomainEndU()),
                                   0.5 * (F.Surface.DomainStartV() + F.Surface.DomainEndV())).Normalised();
    if ((Upper && N.Dot(Vec3::UnitZ()) < 1.0 - 1e-6) || (!Upper && N.Dot(-Vec3::UnitZ()) < 1.0 - 1e-6)) return false;
    int Outer = 0, Holes = 0, Circles = 0;
    for (int Loop : F.Loops)
    {
        if (Loop < 0 || Loop >= static_cast<int>(Body.Loops.size())) return false;
        const BrepLoop& L = Body.Loops[Loop];
        if (L.Outer && L.Coedges.size() == 4) ++Outer;
        else if (!L.Outer && L.Coedges.size() == 1)
        {
            ++Holes;
            const int C = L.Coedges.front();
            if (C < 0 || C >= static_cast<int>(Body.Coedges.size())) return false;
            const int E = Body.Coedges[C].Edge;
            if (E < 0 || E >= static_cast<int>(Body.Edges.size())) return false;
            const BrepEdge& Edge = Body.Edges[E];
            Circles += Edge.Curve.Classification == CurveClassification::Circle && Edge.Curve.Rational() && Edge.Curve.Closed();
        }
        else return false;
    }
    return Outer == 1 && Holes == 2 && Circles == 2;
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
    VerificationPanel Panel("SolidArc · Phase 46 · bounded genus-two twin-holed-prism face offset");
    const auto SourceDeliver = TwinPrism();
    Panel.Expect("The rectangular prism with two circular through-holes is constructed", static_cast<bool>(SourceDeliver));
    const BrepBody Source = SourceDeliver.Payload;
    const BrepBody Snapshot = Source;
    const BodyReport Before = Source.Validate();
    const int Top = FaceToward(Source, Vec3::UnitZ());
    const int Bottom = FaceToward(Source, -Vec3::UnitZ());
    Panel.Expect("The source has genus-two V12/E18/C36/L12/F8 topology", SourceDeliver && Before.Solid() && Before.Hulls == 1 &&
                 Before.Genus == 2 && Source.Vertices.size() == 12 && Source.Edges.size() == 18 &&
                 Source.Coedges.size() == 36 && Source.Loops.size() == 12 && Source.Faces.size() == 8);
    Panel.Expect("Both caps have one outer loop and two circular inner loops", IsTwinAnnularCap(Source, Top, true) &&
                 IsTwinAnnularCap(Source, Bottom, false) && Top != Bottom);
    Panel.Expect("The source contains exactly four rational circular hole rims", ExactCircularEdges(Source) == 4);

    Panel.Section("Exact genus-two non-box face offset reconstruction");
    const auto Result = SourceDeliver ? FaceEditSolver::OffsetExtrudedTwinHoledPrism(Source, Top, Offset)
                                      : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no twin-holed source");
    Panel.Expect("The selected upper three-loop cap produces a closed genus-two solid", Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport R = Result.Payload.Validate();
        const double Area = Width * Depth - 2.0 * ScalarCriteria::Pi * HoleRadius * HoleRadius;
        const double Expected = Area * (Height + Offset);
        Panel.Expect("The offset retains V12/E18/C36/L12/F8 genus-two topology", R.Hulls == 1 && R.Genus == 2 &&
                     Result.Payload.Vertices.size() == 12 && Result.Payload.Edges.size() == 18 &&
                     Result.Payload.Coedges.size() == 36 && Result.Payload.Loops.size() == 12 &&
                     Result.Payload.Faces.size() == 8 && R.OpenEdges == 0 && R.NonManifoldEdges == 0 &&
                     R.MisorientedEdges == 0);
        Panel.Within("The offset volume follows rectangle area minus two hole areas", std::fabs(R.Volume - Expected), 1e-1);
        Panel.Expect("The result preserves both three-loop caps", IsTwinAnnularCap(Result.Payload, FaceToward(Result.Payload, Vec3::UnitZ()), true) &&
                     IsTwinAnnularCap(Result.Payload, FaceToward(Result.Payload, -Vec3::UnitZ()), false));
        Panel.Expect("All four exact rational circular hole rims survive", ExactCircularEdges(Result.Payload) == 4);
        Panel.Expect("The separate reconstruction preserves the genus-two source", SameSource(Source, Snapshot, Before));
    }
    const auto Dispatch = FaceEditSolver::OffsetFace(Source, Top, Offset);
    Panel.Expect("The public face-offset dispatcher reaches the genus-two route", Dispatch && Dispatch.Payload.Validate().Genus == 2);
    Panel.Expect("Only the upper three-loop cap is supported", !FaceEditSolver::OffsetExtrudedTwinHoledPrism(Source, Bottom, Offset));
    const auto Reversed = TwinPrism(2, true);
    const auto ReversedResult = Reversed ? FaceEditSolver::OffsetExtrudedTwinHoledPrism(Reversed.Payload, FaceToward(Reversed.Payload, Vec3::UnitZ()), Offset)
                                         : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "reversed fixture");
    Panel.Expect("Reversing inner-loop construction order gives the same deterministic topology", ReversedResult &&
                 ReversedResult.Payload.Validate().Genus == 2 && ReversedResult.Payload.Vertices.size() == 12);

    Panel.Section("Twin-holed-prism offset refusal boundaries");
    const auto Box = BrepBody::Box({ -8, -5, 0 }, { 8, 5, Height });
    Panel.Expect("A hole-free box remains outside the twin-holed API", Box && !FaceEditSolver::OffsetExtrudedTwinHoledPrism(Box.Payload, FaceToward(Box.Payload, Vec3::UnitZ()), Offset));
    const auto OneHole = TwinPrism(1);
    Panel.Expect("A genus-one single-hole prism refuses the genus-two route", OneHole && !FaceEditSolver::OffsetExtrudedTwinHoledPrism(OneHole.Payload, FaceToward(OneHole.Payload, Vec3::UnitZ()), Offset));
    const auto ThreeHoles = TwinPrism(3, false, true, 4.0);
    Panel.Expect("A three-hole prism remains outside the exact twin-hole route", ThreeHoles && !FaceEditSolver::OffsetExtrudedTwinHoledPrism(ThreeHoles.Payload, FaceToward(ThreeHoles.Payload, Vec3::UnitZ()), Offset));
    const auto NonCircular = TwinPrism(2, false, false);
    Panel.Expect("Non-circular inner loops refuse", NonCircular && !FaceEditSolver::OffsetExtrudedTwinHoledPrism(NonCircular.Payload, FaceToward(NonCircular.Payload, Vec3::UnitZ()), Offset));
    const auto Overlap = TwinPrism(2, false, true, 1.0);
    Panel.Expect("Overlapping holes refuse without healing", !Overlap || !FaceEditSolver::OffsetExtrudedTwinHoledPrism(Overlap.Payload, FaceToward(Overlap.Payload, Vec3::UnitZ()), Offset));
    const auto Cylinder = BrepBody::Cylinder({ 0, 0, 0 }, Vec3::UnitZ(), 5.0, Height);
    Panel.Expect("A curved cylinder refuses the twin-holed-prism route", Cylinder && !FaceEditSolver::OffsetExtrudedTwinHoledPrism(Cylinder.Payload, FaceToward(Cylinder.Payload, Vec3::UnitZ()), Offset));
    Panel.Expect("Side, zero, negative, and non-finite offsets refuse", !FaceEditSolver::OffsetExtrudedTwinHoledPrism(Source, FaceToward(Source, Vec3::UnitX()), Offset) &&
                 !FaceEditSolver::OffsetExtrudedTwinHoledPrism(Source, Top, 0.0) &&
                 !FaceEditSolver::OffsetExtrudedTwinHoledPrism(Source, Top, -0.1) &&
                 !FaceEditSolver::OffsetExtrudedTwinHoledPrism(Source, Top, std::numeric_limits<double>::infinity()));
    BrepBody Malformed = Source;
    Malformed.Edges[0].Coedges.push_back(Malformed.Edges[0].Coedges.front());
    Panel.Expect("A malformed twin-holed prism refuses transactionally", !FaceEditSolver::OffsetExtrudedTwinHoledPrism(Malformed, Top, Offset));
    Panel.Expect("All refusal paths preserve the genus-two source", SameSource(Source, Snapshot, Before));

    Panel.Section("Distinct genus-two face-offset proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) /
        "Phase46_TwinHoledPrismFaceOffset.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = SourceDeliver && Result &&
        Host.Document().AddBody("SharpTwinHoledPrism", Source.Transformed(Mat4::Translation({ -10, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("OffsetTwinHoledPrism", Result.Payload.Transformed(Mat4::Translation({ 10, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 35 -18") && Host.Execute("view fit") &&
        Host.Execute("render Phase46_TwinHoledPrismFaceOffset");
    Panel.Expect("The genus-two twin-hole proof render completes", Rendered);
    Panel.Expect("The genus-two twin-hole proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
