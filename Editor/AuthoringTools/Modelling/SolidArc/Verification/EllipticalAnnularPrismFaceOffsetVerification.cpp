//=============================================================================================================================================
// SolidArc · Phase 54 · bounded elliptical-annular-prism face offset
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
constexpr double OuterMajor = 6.0;
constexpr double OuterMinor = 3.0;
constexpr double InnerMajor = 2.0;
constexpr double InnerMinor = 1.0;
constexpr double Height = 5.0;
constexpr double Offset = 1.5;

[[nodiscard]] Deliver<BrepBody> EllipticalAnnularPrism(Vec3 Centre = {}) noexcept
{
    const auto Outer = NurbsCurve::Ellipse(Centre, Vec3::UnitZ(), Vec3::UnitX(), OuterMajor, OuterMinor);
    const auto Inner = NurbsCurve::Ellipse(Centre, Vec3::UnitZ(), Vec3::UnitX(), InnerMajor, InnerMinor);
    return Outer && Inner ? BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, Inner.Payload }, Vec3::UnitZ(), Height)
                          : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "ellipse fixture");
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

[[nodiscard]] bool RimMatches(const BrepBody& Body, int Coedge, double Major, double Minor, double Z) noexcept
{
    if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return false;
    const int EdgeIndex = Body.Coedges[Coedge].Edge;
    if (EdgeIndex < 0 || EdgeIndex >= static_cast<int>(Body.Edges.size())) return false;
    const NurbsCurve& Rim = Body.Edges[EdgeIndex].Curve;
    if (!Rim.Closed() || !Rim.Rational() || Rim.Degree != 2 || Rim.PoleCount() != 9) return false;
    const Box3 B = Rim.Bounds();
    if (std::fabs((B.High.X - B.Low.X) - 2.0 * Major) > 1e-7 ||
        std::fabs((B.High.Y - B.Low.Y) - 2.0 * Minor) > 1e-7 || std::fabs(B.High.Z - B.Low.Z) > 1e-7 ||
        std::fabs(B.Low.Z - Z) > 1e-7) return false;
    const auto Expected = NurbsCurve::Ellipse({ 0, 0, Z }, Vec3::UnitZ(), Vec3::UnitX(), Major, Minor);
    if (!Expected) return false;
    for (int I = 0; I <= 32; ++I)
    {
        const double T = Rim.DomainStart() + (Rim.DomainEnd() - Rim.DomainStart()) * I / 32.0;
        double Distance = 0.0;
        (void)Expected.Payload.ClosestParameter(Rim.Sample(T), &Distance);
        if (Distance > 1e-5) return false;
    }
    return true;
}

[[nodiscard]] bool IsAnnularCap(const BrepBody& Body, int Face, bool Upper) noexcept
{
    if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return false;
    const BrepFace& F = Body.Faces[Face];
    if (F.Surface.Classification != SurfaceClassification::Plane || F.Loops.size() != 2) return false;
    const Vec3 N = Body.FaceNormal(Face, 0.5 * (F.Surface.DomainStartU() + F.Surface.DomainEndU()),
                                   0.5 * (F.Surface.DomainStartV() + F.Surface.DomainEndV())).Normalised();
    if ((Upper && N.Dot(Vec3::UnitZ()) < 1.0 - 1e-6) || (!Upper && N.Dot(-Vec3::UnitZ()) < 1.0 - 1e-6)) return false;
    const double RimZ = Upper ? Body.Bounds().High.Z : Body.Bounds().Low.Z;
    bool Outer = false, Inner = false;
    for (int Loop : F.Loops)
    {
        if (Loop < 0 || Loop >= static_cast<int>(Body.Loops.size()) || Body.Loops[Loop].Coedges.size() != 1) return false;
        const int Coedge = Body.Loops[Loop].Coedges.front();
        Outer = Outer || RimMatches(Body, Coedge, OuterMajor, OuterMinor, RimZ);
        Inner = Inner || RimMatches(Body, Coedge, InnerMajor, InnerMinor, RimZ);
    }
    return Outer && Inner;
}

[[nodiscard]] bool AllAnalyticExtrusion(const BrepBody& Body) noexcept
{
    return std::all_of(Body.Faces.begin(), Body.Faces.end(), [](const BrepFace& F) {
        return F.Surface.Classification == SurfaceClassification::Plane || F.Surface.Classification == SurfaceClassification::Extrusion;
    });
}

[[nodiscard]] Deliver<BrepBody> CircularHolePrism() noexcept
{
    Workplane W;
    const auto Outer = NurbsCurve::Rectangle(W, { -6, -4 }, { 6, 4 });
    const auto Hole = NurbsCurve::Circle({ 0, 0, 0 }, Vec3::UnitZ(), 1.25);
    return Outer && Hole ? BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, Hole.Payload }, Vec3::UnitZ(), Height)
                         : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "circular hole fixture");
}

[[nodiscard]] Deliver<BrepBody> RectangularHolePrism() noexcept
{
    Workplane W;
    const auto Outer = NurbsCurve::Ellipse({ 0, 0, 0 }, Vec3::UnitZ(), Vec3::UnitX(), OuterMajor, OuterMinor);
    const auto Hole = NurbsCurve::Rectangle(W, { -1.5, -0.75 }, { 1.5, 0.75 });
    return Outer && Hole ? BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, Hole.Payload }, Vec3::UnitZ(), Height)
                         : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "rectangular hole fixture");
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 54 · bounded elliptical-annular-prism face offset");
    const auto SourceDeliver = EllipticalAnnularPrism();
    Panel.Expect("The exact elliptical-annular prism is constructed", static_cast<bool>(SourceDeliver));
    const BrepBody Source = SourceDeliver.Payload;
    const BrepBody Snapshot = Source;
    const BodyReport Before = Source.Validate();
    const int Top = FaceToward(Source, Vec3::UnitZ());
    const int Bottom = FaceToward(Source, -Vec3::UnitZ());
    const int Side = FaceToward(Source, Vec3::UnitX());
    Panel.Expect("The source has closed genus-one two-loop topology", SourceDeliver && Before.Solid() && Before.Hulls == 1 && Before.Genus == 1 &&
                 Source.Vertices.size() == 4 && Source.Edges.size() == 6 && Source.Coedges.size() == 12 &&
                 Source.Loops.size() == 6 && Source.Faces.size() == 4);
    Panel.Expect("Both caps are exact elliptical annuli", IsAnnularCap(Source, Top, true) && IsAnnularCap(Source, Bottom, false));
    Panel.Expect("The selected upper cap and side face are distinct", Top >= 0 && Bottom >= 0 && Side >= 0 && Top != Bottom && Top != Side);
    Panel.Expect("All source supports are planar or analytic extrusion faces", AllAnalyticExtrusion(Source));

    Panel.Section("Exact elliptical-annulus face offset reconstruction");
    const auto Result = SourceDeliver ? FaceEditSolver::OffsetExtrudedEllipticalAnnularPrism(Source, Top, Offset)
                                      : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no elliptical-annular source");
    Panel.Expect("The selected upper annular cap produces a closed solid", Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport R = Result.Payload.Validate();
        const double ExpectedVolume = ScalarCriteria::Pi * (OuterMajor * OuterMinor - InnerMajor * InnerMinor) * (Height + Offset);
        Panel.Expect("The result retains genus-one V4/E6/C12/L6/F4 topology", R.Hulls == 1 && R.Genus == 1 &&
                     Result.Payload.Vertices.size() == 4 && Result.Payload.Edges.size() == 6 && Result.Payload.Coedges.size() == 12 &&
                     Result.Payload.Loops.size() == 6 && Result.Payload.Faces.size() == 4 && R.OpenEdges == 0 && R.NonManifoldEdges == 0 && R.MisorientedEdges == 0);
        Panel.Within("The offset volume follows outer ellipse area minus inner ellipse area", std::fabs(R.Volume - ExpectedVolume) / ExpectedVolume, 1e-3);
        Panel.Expect("The result retains both exact elliptical annular caps", IsAnnularCap(Result.Payload, FaceToward(Result.Payload, Vec3::UnitZ()), true) &&
                     IsAnnularCap(Result.Payload, FaceToward(Result.Payload, -Vec3::UnitZ()), false));
        Panel.Expect("The result remains analytic planar/extrusion geometry", AllAnalyticExtrusion(Result.Payload));
        Panel.Expect("The separate reconstruction preserves the source", SameSource(Source, Snapshot, Before));
    }
    const auto Dispatch = FaceEditSolver::OffsetFace(Source, Top, Offset);
    Panel.Expect("The public offset dispatcher reaches the elliptical-annulus route", Dispatch && Dispatch.Payload.Validate().Genus == 1);
    Panel.Expect("Only the upper annular cap is supported", !FaceEditSolver::OffsetExtrudedEllipticalAnnularPrism(Source, Bottom, Offset) &&
                 !FaceEditSolver::OffsetExtrudedEllipticalAnnularPrism(Source, Side, Offset));

    Panel.Section("Elliptical-annulus offset refusal boundaries");
    const auto SingleEllipseCurve = NurbsCurve::Ellipse({ 0, 0, 0 }, Vec3::UnitZ(), Vec3::UnitX(), OuterMajor, OuterMinor);
    const auto SingleEllipse = SingleEllipseCurve ? BrepBody::Extrude(SingleEllipseCurve.Payload, Vec3::UnitZ(), Height)
                                                  : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "single ellipse fixture");
    Panel.Expect("A single-loop elliptical prism refuses the annular route", SingleEllipse && !FaceEditSolver::OffsetExtrudedEllipticalAnnularPrism(SingleEllipse.Payload, FaceToward(SingleEllipse.Payload, Vec3::UnitZ()), Offset));
    const auto CircularHole = CircularHolePrism();
    Panel.Expect("A circular-holed prism refuses the elliptical-annulus route", CircularHole && !FaceEditSolver::OffsetExtrudedEllipticalAnnularPrism(CircularHole.Payload, FaceToward(CircularHole.Payload, Vec3::UnitZ()), Offset));
    const auto RectangularHole = RectangularHolePrism();
    Panel.Expect("A rectangular inner loop refuses", RectangularHole && !FaceEditSolver::OffsetExtrudedEllipticalAnnularPrism(RectangularHole.Payload, FaceToward(RectangularHole.Payload, Vec3::UnitZ()), Offset));
    const auto Translated = EllipticalAnnularPrism({ 2, 0, 0 });
    Panel.Expect("A translated elliptical-annular prism refuses", Translated && !FaceEditSolver::OffsetExtrudedEllipticalAnnularPrism(Translated.Payload, FaceToward(Translated.Payload, Vec3::UnitZ()), Offset));
    const auto Sphere = BrepBody::Sphere({ 0, 0, 0 }, 5.0);
    Panel.Expect("A sphere refuses", Sphere && !FaceEditSolver::OffsetExtrudedEllipticalAnnularPrism(Sphere.Payload, 0, Offset));
    Panel.Expect("Zero, negative, and non-finite offsets refuse", !FaceEditSolver::OffsetExtrudedEllipticalAnnularPrism(Source, Top, 0.0) &&
                 !FaceEditSolver::OffsetExtrudedEllipticalAnnularPrism(Source, Top, -0.1) &&
                 !FaceEditSolver::OffsetExtrudedEllipticalAnnularPrism(Source, Top, std::numeric_limits<double>::infinity()));
    BrepBody Malformed = Source;
    Malformed.Edges[0].Coedges.push_back(Malformed.Edges[0].Coedges.front());
    Panel.Expect("A malformed elliptical-annular prism refuses transactionally", !FaceEditSolver::OffsetExtrudedEllipticalAnnularPrism(Malformed, Top, Offset));
    Panel.Expect("All refusal paths preserve the source", SameSource(Source, Snapshot, Before));

    Panel.Section("Distinct elliptical-annulus proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase54_EllipticalAnnularPrismFaceOffset.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = SourceDeliver && Result &&
        Host.Document().AddBody("SharpEllipticalAnnulus", Source.Transformed(Mat4::Translation({ -9, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("OffsetEllipticalAnnulus", Result.Payload.Transformed(Mat4::Translation({ 9, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 25 -18") && Host.Execute("view fit") && Host.Execute("render Phase54_EllipticalAnnularPrismFaceOffset");
    Panel.Expect("The elliptical-annulus proof render completes", Rendered);
    Panel.Expect("The elliptical-annulus proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
