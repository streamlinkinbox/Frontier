//=============================================================================================================================================
// SolidArc · Phase 52 · bounded analytic torus face offset
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
constexpr double MajorRadius = 8.0;
constexpr double MinorRadius = 2.0;
constexpr double Offset = 0.75;

[[nodiscard]] bool IsTorus(const BrepBody& Body, double Major, double Minor, Vec3 Axis = Vec3::UnitZ()) noexcept
{
    if (Body.Faces.size() != 1 || Body.Vertices.size() != 1 || Body.Edges.size() != 2 ||
        Body.Coedges.size() != 4 || Body.Loops.size() != 1) return false;
    const BrepFace& Face = Body.Faces.front();
    return Face.Natural && Face.Loops.size() == 1 && Face.Surface.Classification == SurfaceClassification::Torus &&
           std::fabs(Face.Surface.RadiusMajor - Major) <= 1e-9 &&
           std::fabs(Face.Surface.RadiusMinor - Minor) <= 1e-9 &&
           Face.Surface.Origin.Length() <= 1e-9 && Face.Surface.Axis.Normalised().Dot(Axis.Normalised()) >= 1.0 - 1e-9;
}

[[nodiscard]] bool SameSource(const BrepBody& Body, const BrepBody& Snapshot, const BodyReport& Before) noexcept
{
    const BodyReport After = Body.Validate();
    return After.Vertices == Before.Vertices && After.Edges == Before.Edges && After.Faces == Before.Faces &&
           After.Loops == Before.Loops && After.Genus == Before.Genus && After.OpenEdges == Before.OpenEdges &&
           After.NonManifoldEdges == Before.NonManifoldEdges && After.MisorientedEdges == Before.MisorientedEdges &&
           After.EulerCharacteristic == Before.EulerCharacteristic && std::fabs(After.Volume - Before.Volume) <= 1e-12 &&
           Body.Vertices.size() == Snapshot.Vertices.size() && Body.Edges.size() == Snapshot.Edges.size() &&
           Body.Coedges.size() == Snapshot.Coedges.size() && Body.Loops.size() == Snapshot.Loops.size() &&
           Body.Faces.size() == Snapshot.Faces.size();
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
    VerificationPanel Panel("SolidArc · Phase 52 · bounded analytic torus face offset");
    const auto SourceDeliver = BrepBody::Torus({ 0, 0, 0 }, Vec3::UnitZ(), MajorRadius, MinorRadius);
    Panel.Expect("The ring torus is constructed", static_cast<bool>(SourceDeliver));
    const BrepBody Source = SourceDeliver.Payload;
    const BrepBody Snapshot = Source;
    const BodyReport Before = Source.Validate();
    Panel.Expect("The source is a closed genus-one V1/E2/C4/L1/F1 body", SourceDeliver && Before.Solid() && Before.Hulls == 1 &&
                 Before.Genus == 1 && Source.Vertices.size() == 1 && Source.Edges.size() == 2 && Source.Coedges.size() == 4 &&
                 Source.Loops.size() == 1 && Source.Faces.size() == 1);
    Panel.Expect("The source is one natural analytic torus face", IsTorus(Source, MajorRadius, MinorRadius));

    Panel.Section("Exact torus face offset reconstruction");
    const auto Result = SourceDeliver ? FaceEditSolver::OffsetTorusFace(Source, 0, Offset)
                                      : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no torus source");
    Panel.Expect("The torus face offset produces a closed solid", Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport R = Result.Payload.Validate();
        Panel.Expect("The result retains V1/E2/C4/L1/F1 genus-one topology", R.Hulls == 1 && R.Genus == 1 &&
                     Result.Payload.Vertices.size() == 1 && Result.Payload.Edges.size() == 2 &&
                     Result.Payload.Coedges.size() == 4 && Result.Payload.Loops.size() == 1 && Result.Payload.Faces.size() == 1 &&
                     R.OpenEdges == 0 && R.NonManifoldEdges == 0 && R.MisorientedEdges == 0);
        Panel.Expect("The result preserves the major radius and expands only the minor radius", IsTorus(Result.Payload, MajorRadius, MinorRadius + Offset));
        const double ExpectedVolume = 2.0 * ScalarCriteria::Pi * ScalarCriteria::Pi * MajorRadius * (MinorRadius + Offset) * (MinorRadius + Offset);
        Panel.Within("The offset volume follows the torus volume identity", std::fabs(R.Volume - ExpectedVolume) / ExpectedVolume, 1e-3);
        Panel.Expect("The separate reconstruction preserves the source", SameSource(Source, Snapshot, Before));
    }
    const auto Dispatch = FaceEditSolver::OffsetFace(Source, 0, Offset);
    Panel.Expect("The public offset dispatcher reaches the torus route", Dispatch && IsTorus(Dispatch.Payload, MajorRadius, MinorRadius + Offset));

    Panel.Section("Torus face offset refusal boundaries");
    const auto Sphere = BrepBody::Sphere({ 0, 0, 0 }, MinorRadius);
    Panel.Expect("A sphere remains outside the torus route", Sphere && !FaceEditSolver::OffsetTorusFace(Sphere.Payload, 0, Offset));
    const auto Cylinder = BrepBody::Cylinder({ 0, 0, 0 }, Vec3::UnitZ(), MajorRadius, 4.0);
    Panel.Expect("A cylinder refuses", Cylinder && !FaceEditSolver::OffsetTorusFace(Cylinder.Payload, 0, Offset));
    const auto Annulus = AnnularPrism();
    Panel.Expect("A revolved annular prism remains a separate route", Annulus && !FaceEditSolver::OffsetTorusFace(Annulus.Payload, 0, Offset));
    const auto Tilted = BrepBody::Torus({ 0, 0, 0 }, Vec3::UnitX(), MajorRadius, MinorRadius);
    Panel.Expect("A tilted torus refuses", Tilted && !FaceEditSolver::OffsetTorusFace(Tilted.Payload, 0, Offset));
    const auto Translated = BrepBody::Torus({ 2, 0, 0 }, Vec3::UnitZ(), MajorRadius, MinorRadius);
    Panel.Expect("A translated torus refuses", Translated && !FaceEditSolver::OffsetTorusFace(Translated.Payload, 0, Offset));
    const auto Spindle = BrepBody::Torus({ 0, 0, 0 }, Vec3::UnitZ(), 2.5, 3.0);
    Panel.Expect("A spindle torus source refuses", Spindle && !FaceEditSolver::OffsetTorusFace(Spindle.Payload, 0, Offset));
    Panel.Expect("Zero, negative, non-finite, and self-intersecting offsets refuse", !FaceEditSolver::OffsetTorusFace(Source, 0, 0.0) &&
                 !FaceEditSolver::OffsetTorusFace(Source, 0, -0.1) &&
                 !FaceEditSolver::OffsetTorusFace(Source, 0, std::numeric_limits<double>::infinity()) &&
                 !FaceEditSolver::OffsetTorusFace(Source, 0, MajorRadius - MinorRadius));
    BrepBody Malformed = Source;
    Malformed.Faces[0].Natural = false;
    Panel.Expect("A malformed torus face refuses transactionally", !FaceEditSolver::OffsetTorusFace(Malformed, 0, Offset));
    Panel.Expect("All refusal paths preserve the source", SameSource(Source, Snapshot, Before));

    Panel.Section("Distinct torus face offset proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase52_TorusFaceOffset.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = SourceDeliver && Result &&
        Host.Document().AddBody("SharpRingTorus", Source.Transformed(Mat4::Translation({ -10, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("OffsetRingTorus", Result.Payload.Transformed(Mat4::Translation({ 10, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 25 -18") && Host.Execute("view fit") && Host.Execute("render Phase52_TorusFaceOffset");
    Panel.Expect("The torus face offset proof render completes", Rendered);
    Panel.Expect("The torus proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
