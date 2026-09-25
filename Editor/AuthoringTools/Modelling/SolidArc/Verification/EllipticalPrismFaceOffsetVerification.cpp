//=============================================================================================================================================
// SolidArc · Phase 43 · bounded elliptical-prism face offset
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
constexpr double MajorRadius = 6.0;
constexpr double MinorRadius = 3.0;
constexpr double Height = 5.0;
constexpr double Offset = 2.0;

[[nodiscard]] Deliver<BrepBody> EllipsePrism() noexcept
{
    const Deliver<NurbsCurve> Profile = NurbsCurve::Ellipse({ 0, 0, 0 }, Vec3::UnitZ(), Vec3::UnitX(), MajorRadius, MinorRadius);
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
           After.Loops == Before.Loops && After.Genus == Before.Genus && After.OpenEdges == Before.OpenEdges &&
           After.NonManifoldEdges == Before.NonManifoldEdges && After.MisorientedEdges == Before.MisorientedEdges &&
           std::fabs(After.Volume - Before.Volume) <= 1e-12 && Body.Vertices.size() == Snapshot.Vertices.size() &&
           Body.Edges.size() == Snapshot.Edges.size() && Body.Coedges.size() == Snapshot.Coedges.size() &&
           Body.Loops.size() == Snapshot.Loops.size() && Body.Faces.size() == Snapshot.Faces.size();
}

[[nodiscard]] bool IsEllipseRim(const BrepBody& Body, int Face, bool Upper) noexcept
{
    if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return false;
    const BrepFace& F = Body.Faces[Face];
    if (F.Surface.Classification != SurfaceClassification::Plane || F.Loops.size() != 1) return false;
    const Vec3 N = Body.FaceNormal(Face, 0.5 * (F.Surface.DomainStartU() + F.Surface.DomainEndU()),
                                   0.5 * (F.Surface.DomainStartV() + F.Surface.DomainEndV())).Normalised();
    if ((Upper && N.Dot(Vec3::UnitZ()) < 1.0 - 1e-6) || (!Upper && N.Dot(-Vec3::UnitZ()) < 1.0 - 1e-6)) return false;
    const BrepLoop& L = Body.Loops[F.Loops.front()];
    if (L.Coedges.size() != 1) return false;
    const int C = L.Coedges.front();
    if (C < 0 || C >= static_cast<int>(Body.Coedges.size())) return false;
    const int E = Body.Coedges[C].Edge;
    if (E < 0 || E >= static_cast<int>(Body.Edges.size())) return false;
    const NurbsCurve& Rim = Body.Edges[E].Curve;
    if (!Rim.Closed() || !Rim.Rational() || Rim.Degree != 2 || Rim.PoleCount() != 9) return false;
    const Box3 B = Rim.Bounds();
    if (std::fabs((B.High.X - B.Low.X) - 2.0 * MajorRadius) > 1e-7 ||
        std::fabs((B.High.Y - B.Low.Y) - 2.0 * MinorRadius) > 1e-7 ||
        std::fabs(B.High.Z - B.Low.Z) > 1e-7) return false;
    const Deliver<NurbsCurve> Expected = NurbsCurve::Ellipse({ 0, 0, B.Low.Z }, Vec3::UnitZ(), Vec3::UnitX(), MajorRadius, MinorRadius);
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

[[nodiscard]] double Relative(double A, double B) noexcept
{
    return std::fabs(A - B) / std::max(1.0, std::fabs(B));
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 43 · bounded elliptical-prism face offset");
    const auto SourceDeliver = EllipsePrism();
    Panel.Expect("The exact rational elliptical prism source is constructed", static_cast<bool>(SourceDeliver));
    const BrepBody Source = SourceDeliver.Payload;
    const BrepBody Snapshot = Source;
    const BodyReport Before = Source.Validate();
    const int Top = FaceToward(Source, Vec3::UnitZ());
    const int Bottom = FaceToward(Source, -Vec3::UnitZ());
    Panel.Expect("The source has V2/E3/C6/L3/F3 topology", SourceDeliver && Before.Solid() && Before.Hulls == 1 &&
                 Before.Genus == 0 && Source.Vertices.size() == 2 && Source.Edges.size() == 3 &&
                 Source.Coedges.size() == 6 && Source.Loops.size() == 3 && Source.Faces.size() == 3);
    Panel.Expect("The upper and lower rims are exact major-6/minor-3 ellipses", IsEllipseRim(Source, Top, true) &&
                 IsEllipseRim(Source, Bottom, false) && Top != Bottom);

    Panel.Section("Exact curved-profile face offset reconstruction");
    const auto Result = SourceDeliver ? FaceEditSolver::OffsetExtrudedEllipticalPrism(Source, Top, Offset)
                                      : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no elliptical prism source");
    Panel.Expect("The selected upper cap produces a closed elliptical prism", Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport R = Result.Payload.Validate();
        const double Expected = ScalarCriteria::Pi * MajorRadius * MinorRadius * (Height + Offset);
        Panel.Expect("The offset retains V2/E3/C6/L3/F3 topology", R.Hulls == 1 && R.Genus == 0 &&
                     Result.Payload.Vertices.size() == 2 && Result.Payload.Edges.size() == 3 &&
                     Result.Payload.Coedges.size() == 6 && Result.Payload.Loops.size() == 3 &&
                     Result.Payload.Faces.size() == 3 && R.OpenEdges == 0 && R.NonManifoldEdges == 0 &&
                     R.MisorientedEdges == 0);
        Panel.Within("The offset volume follows pi times the ellipse area and extended height", Relative(R.Volume, Expected), 1e-3);
        Panel.Expect("The result preserves both exact ellipse rims", IsEllipseRim(Result.Payload, FaceToward(Result.Payload, Vec3::UnitZ()), true) &&
                     IsEllipseRim(Result.Payload, FaceToward(Result.Payload, -Vec3::UnitZ()), false));
        Panel.Expect("The result remains analytic planar/extruded geometry", Result.Payload.Faces[0].Surface.Classification == SurfaceClassification::Extrusion &&
                     Result.Payload.Faces[1].Surface.Classification == SurfaceClassification::Plane &&
                     Result.Payload.Faces[2].Surface.Classification == SurfaceClassification::Plane);
        Panel.Expect("The separate reconstruction preserves the source", SameSource(Source, Snapshot, Before));
    }
    const auto Dispatch = FaceEditSolver::OffsetFace(Source, Top, Offset);
    Panel.Expect("The public face-offset dispatcher reaches the elliptical route", Dispatch && Dispatch.Payload.Validate().Solid());
    Panel.Expect("Only the upper cap is supported by this bounded route", !FaceEditSolver::OffsetExtrudedEllipticalPrism(Source, Bottom, Offset));

    Panel.Section("Elliptical-prism offset refusal boundaries");
    const auto Cylinder = BrepBody::Cylinder({ 0, 0, 0 }, Vec3::UnitZ(), MajorRadius, Height);
    Panel.Expect("A circular cylinder refuses the non-circular elliptical route", Cylinder && !FaceEditSolver::OffsetExtrudedEllipticalPrism(Cylinder.Payload, FaceToward(Cylinder.Payload, Vec3::UnitZ()), Offset));
    const std::vector<Vec3> PentagonPoints{ { 4.5, 0, 0 }, { 1.391, 4.28, 0 }, { -3.641, 2.645, 0 }, { -3.641, -2.645, 0 }, { 1.391, -4.28, 0 } };
    const auto PentagonProfile = NurbsCurve::Polyline(PentagonPoints, true);
    const auto Pentagon = PentagonProfile ? BrepBody::Extrude(PentagonProfile.Payload, Vec3::UnitZ(), Height)
                                          : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "pentagon fixture");
    Panel.Expect("A polygonal prism refuses the elliptical route", Pentagon && !FaceEditSolver::OffsetExtrudedEllipticalPrism(Pentagon.Payload, FaceToward(Pentagon.Payload, Vec3::UnitZ()), Offset));
    const auto Holed = [&]() { Workplane W; auto O = NurbsCurve::Rectangle(W, {-6, -4.5}, {6, 4.5}); auto H = NurbsCurve::Circle({0,0,0}, Vec3::UnitZ(), 1.5); return O && H ? BrepBody::Extrude(std::vector<NurbsCurve>{O.Payload,H.Payload}, Vec3::UnitZ(), Height) : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "hole fixture"); }();
    Panel.Expect("A multi-loop holed prism refuses the single-profile route", Holed && !FaceEditSolver::OffsetExtrudedEllipticalPrism(Holed.Payload, FaceToward(Holed.Payload, Vec3::UnitZ()), Offset));
    const auto NonElliptic = NurbsCurve::Interpolate({ { 6, 0, 0 }, { 0, 3, 0 }, { -6, 0, 0 }, { 0, -3, 0 }, { 6, 0, 0 } }, 3, true);
    const auto Freeform = NonElliptic ? BrepBody::Extrude(NonElliptic.Payload, Vec3::UnitZ(), Height)
                                      : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "freeform fixture");
    Panel.Expect("A non-rational/non-elliptic closed profile refuses", Freeform && !FaceEditSolver::OffsetExtrudedEllipticalPrism(Freeform.Payload, FaceToward(Freeform.Payload, Vec3::UnitZ()), Offset));
    Panel.Expect("Side, zero, negative, and non-finite offsets refuse", !FaceEditSolver::OffsetExtrudedEllipticalPrism(Source, FaceToward(Source, Vec3::UnitX()), Offset) &&
                 !FaceEditSolver::OffsetExtrudedEllipticalPrism(Source, Top, 0.0) &&
                 !FaceEditSolver::OffsetExtrudedEllipticalPrism(Source, Top, -0.1) &&
                 !FaceEditSolver::OffsetExtrudedEllipticalPrism(Source, Top, std::numeric_limits<double>::infinity()));
    BrepBody Malformed = Source;
    Malformed.Edges[0].Coedges.push_back(Malformed.Edges[0].Coedges.front());
    Panel.Expect("A malformed elliptical prism refuses transactionally", !FaceEditSolver::OffsetExtrudedEllipticalPrism(Malformed, Top, Offset));
    Panel.Expect("All refusal paths preserve the source", SameSource(Source, Snapshot, Before));

    Panel.Section("Distinct curved-profile proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) /
        "Phase43_EllipticalPrismFaceOffset.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = SourceDeliver && Result &&
        Host.Document().AddBody("SharpEllipsePrism", Source.Transformed(Mat4::Translation({ -9, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("OffsetEllipsePrism", Result.Payload.Transformed(Mat4::Translation({ 9, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 35 -18") && Host.Execute("view fit") &&
        Host.Execute("render Phase43_EllipticalPrismFaceOffset");
    Panel.Expect("The elliptical-prism proof render completes", Rendered);
    Panel.Expect("The elliptical-prism proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
