//=============================================================================================================================================
// SolidArc · Phase 53 · bounded analytic sphere face offset
//=============================================================================================================================================
#include "Kernel/FaceEditSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>
#include <filesystem>
#include <limits>

using namespace Frontier;

namespace
{
constexpr double Radius = 5.0;
constexpr double Offset = 1.25;

[[nodiscard]] bool IsSphere(const BrepBody& Body, double R, Vec3 Centre = {}) noexcept
{
    if (Body.Faces.size() != 1 || Body.Vertices.size() != 2 || Body.Edges.size() != 1 ||
        Body.Coedges.size() != 2 || Body.Loops.size() != 1) return false;
    const BrepFace& Face = Body.Faces.front();
    return Face.Natural && Face.Loops.size() == 1 && Body.Loops.front().Coedges.size() == 2 &&
           Body.Edges.front().Coedges.size() == 2 && Face.Surface.Classification == SurfaceClassification::Sphere &&
           std::fabs(Face.Surface.RadiusMajor - R) <= 1e-9 && std::fabs(Face.Surface.RadiusMinor - R) <= 1e-9 &&
           (Face.Surface.Origin - Centre).Length() <= 1e-9 && Face.Surface.Axis.Normalised().Dot(Vec3::UnitZ()) >= 1.0 - 1e-9;
}

[[nodiscard]] bool SameSource(const BrepBody& Body, const BrepBody& Snapshot, const BodyReport& Before) noexcept
{
    const BodyReport After = Body.Validate();
    return After.Vertices == Before.Vertices && After.Edges == Before.Edges && After.Faces == Before.Faces &&
           After.Loops == Before.Loops && After.Genus == Before.Genus && After.OpenEdges == Before.OpenEdges &&
           After.NonManifoldEdges == Before.NonManifoldEdges && After.MisorientedEdges == Before.MisorientedEdges &&
           After.EulerCharacteristic == Before.EulerCharacteristic && std::fabs(After.Volume - Before.Volume) <= 1e-12 &&
           Body.Vertices.size() == Snapshot.Vertices.size() && Body.Edges.size() == Snapshot.Edges.size() &&
           Body.Coedges.size() == Snapshot.Coedges.size() && Body.Loops.size() == Snapshot.Loops.size() && Body.Faces.size() == Snapshot.Faces.size();
}

[[nodiscard]] Deliver<BrepBody> AnnularPrism() noexcept
{
    const Deliver<NurbsCurve> Profile = NurbsCurve::Polyline({ { 4, 0, 0 }, { 6, 0, 0 }, { 6, 0, 4 }, { 4, 0, 4 } }, true);
    return Profile ? BrepBody::Revolve(Profile.Payload, { 0, 0, 0 }, Vec3::UnitZ(), ScalarCriteria::TwoPi)
                   : Deliver<BrepBody>::Reject(Profile.Denial.Reason, Profile.Denial.Detail);
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 53 · bounded analytic sphere face offset");
    const auto SourceDeliver = BrepBody::Sphere({ 0, 0, 0 }, Radius);
    Panel.Expect("The analytic sphere is constructed", static_cast<bool>(SourceDeliver));
    const BrepBody Source = SourceDeliver.Payload;
    const BrepBody Snapshot = Source;
    const BodyReport Before = Source.Validate();
    Panel.Expect("The source is a closed genus-zero V2/E1/C2/L1/F1 body", SourceDeliver && Before.Solid() && Before.Hulls == 1 &&
                 Before.Genus == 0 && Source.Vertices.size() == 2 && Source.Edges.size() == 1 && Source.Coedges.size() == 2 &&
                 Source.Loops.size() == 1 && Source.Faces.size() == 1);
    Panel.Expect("The source is one natural canonical analytic sphere face", IsSphere(Source, Radius));

    Panel.Section("Exact sphere face offset reconstruction");
    const auto Result = SourceDeliver ? FaceEditSolver::OffsetSphereFace(Source, 0, Offset)
                                      : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no sphere source");
    Panel.Expect("The sphere face offset produces a closed solid", Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport R = Result.Payload.Validate();
        Panel.Expect("The result retains V2/E1/C2/L1/F1 genus-zero topology", R.Hulls == 1 && R.Genus == 0 &&
                     Result.Payload.Vertices.size() == 2 && Result.Payload.Edges.size() == 1 &&
                     Result.Payload.Coedges.size() == 2 && Result.Payload.Loops.size() == 1 && Result.Payload.Faces.size() == 1 &&
                     R.OpenEdges == 0 && R.NonManifoldEdges == 0 && R.MisorientedEdges == 0);
        Panel.Expect("The result expands only the radius and preserves the centre", IsSphere(Result.Payload, Radius + Offset));
        const double ExpectedVolume = 4.0 / 3.0 * ScalarCriteria::Pi * (Radius + Offset) * (Radius + Offset) * (Radius + Offset);
        Panel.Within("The offset volume follows the sphere volume identity", std::fabs(R.Volume - ExpectedVolume) / ExpectedVolume, 5e-3);
        Panel.Expect("The separate reconstruction preserves the source", SameSource(Source, Snapshot, Before));
    }
    const auto Dispatch = FaceEditSolver::OffsetFace(Source, 0, Offset);
    Panel.Expect("The public offset dispatcher reaches the sphere route", Dispatch && IsSphere(Dispatch.Payload, Radius + Offset));

    Panel.Section("Sphere face offset refusal boundaries");
    const auto Torus = BrepBody::Torus({ 0, 0, 0 }, Vec3::UnitZ(), 8.0, 2.0);
    Panel.Expect("A torus remains outside the sphere route", Torus && !FaceEditSolver::OffsetSphereFace(Torus.Payload, 0, Offset));
    const auto Cylinder = BrepBody::Cylinder({ 0, 0, 0 }, Vec3::UnitZ(), Radius, 4.0);
    Panel.Expect("A cylinder refuses", Cylinder && !FaceEditSolver::OffsetSphereFace(Cylinder.Payload, 0, Offset));
    const auto Annulus = AnnularPrism();
    Panel.Expect("A revolved annular prism remains a separate route", Annulus && !FaceEditSolver::OffsetSphereFace(Annulus.Payload, 0, Offset));
    const auto Translated = BrepBody::Sphere({ 2, 0, 0 }, Radius);
    Panel.Expect("A translated sphere refuses", Translated && !FaceEditSolver::OffsetSphereFace(Translated.Payload, 0, Offset));
    Panel.Expect("Zero, negative, and non-finite offsets refuse", !FaceEditSolver::OffsetSphereFace(Source, 0, 0.0) &&
                 !FaceEditSolver::OffsetSphereFace(Source, 0, -0.1) &&
                 !FaceEditSolver::OffsetSphereFace(Source, 0, std::numeric_limits<double>::infinity()));
    BrepBody Malformed = Source;
    Malformed.Faces[0].Natural = false;
    Panel.Expect("A malformed sphere face refuses transactionally", !FaceEditSolver::OffsetSphereFace(Malformed, 0, Offset));
    Panel.Expect("All refusal paths preserve the source", SameSource(Source, Snapshot, Before));

    Panel.Section("Distinct sphere face offset proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase53_SphereFaceOffset.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = SourceDeliver && Result &&
        Host.Document().AddBody("SharpSphere", Source.Transformed(Mat4::Translation({ -8, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("OffsetSphere", Result.Payload.Transformed(Mat4::Translation({ 8, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 25 -18") && Host.Execute("view fit") && Host.Execute("render Phase53_SphereFaceOffset");
    Panel.Expect("The sphere face offset proof render completes", Rendered);
    Panel.Expect("The sphere proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
