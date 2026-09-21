//=============================================================================================================================================
// SolidArc · Phase 36a · conservative general face editing batch
//
// This proof intentionally tests the accepted domain instead of pretending these routes are arbitrary
// B-rep surgery. Canonical axis-aligned boxes take exact transactional routes; curved and trimmed
// bodies refuse. The last section emits a visible source/result proof image.
//=============================================================================================================================================
#include "Kernel/FaceEditSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>
#include <filesystem>
#include <string>

using namespace Frontier;

namespace
{
constexpr double Tol = 1e-9;

[[nodiscard]] BrepBody Box(Vec3 A, Vec3 B) noexcept
{
    return BrepBody::Box(A, B).Payload;
}

[[nodiscard]] int FaceToward(const BrepBody& Body, Vec3 Direction) noexcept
{
    int Best = -1; double BestDot = -2.0;
    for (int F = 0; F < static_cast<int>(Body.Faces.size()); ++F)
    {
        const BrepFace& Face = Body.Faces[F];
        const Vec3 N = Body.FaceNormal(F, 0.5 * (Face.Surface.DomainStartU() + Face.Surface.DomainEndU()),
                                       0.5 * (Face.Surface.DomainStartV() + Face.Surface.DomainEndV()));
        const double D = N.Normalised().Dot(Direction.Normalised());
        if (D > BestDot) { BestDot = D; Best = F; }
    }
    return Best;
}

[[nodiscard]] bool SameBody(const BrepBody& A, const BrepBody& B) noexcept
{
    const BodyReport AR = A.Validate(), BR = B.Validate();
    return AR.Vertices == BR.Vertices && AR.Edges == BR.Edges && AR.Faces == BR.Faces &&
           std::fabs(AR.Volume - BR.Volume) <= Tol && std::fabs(AR.Area - BR.Area) <= Tol;
}

[[nodiscard]] NurbsSurface ReplacementFor(const BrepBody& Body, int Face) noexcept
{
    const BrepFace& F = Body.Faces[Face];
    const double U0 = F.Surface.DomainStartU(), U1 = F.Surface.DomainEndU();
    const double V0 = F.Surface.DomainStartV(), V1 = F.Surface.DomainEndV();
    return NurbsSurface::Patch(1, 1, 2, 2, {
        F.Surface.Sample(U0, V0), F.Surface.Sample(U0, V1),
        F.Surface.Sample(U1, V0), F.Surface.Sample(U1, V1) }).Payload;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 36a · general face editing");
    const BrepBody Source = Box({ 0, 0, 0 }, { 4, 5, 6 });
    const BodyReport Before = Source.Validate();
    const int Top = FaceToward(Source, { 0, 0, 1 });
    const int Right = FaceToward(Source, { 1, 0, 0 });
    Panel.Expect("Canonical source is a closed V8/E12/F6 solid", FaceEditSolver::IsCanonicalBox(Source) && Before.Solid() && Before.Vertices == 8 && Before.Edges == 12 && Before.Faces == 6);
    Panel.Expect("Top and right face selections resolve", Top >= 0 && Right >= 0 && Top != Right);

    Panel.Section("Exact offset, extension, trim and draft are transactional");
    Deliver<BrepBody> Offset = FaceEditSolver::OffsetFace(Source, Top, 1.0);
    Panel.Expect("Positive top offset commits a closed solid", Offset && Offset.Payload.Validate().Solid());
    if (Offset) Panel.Equal("Top offset changes volume by exact 4×5×1", Offset.Payload.Validate().Volume, 140.0, Tol);

    Deliver<BrepBody> Extended = FaceEditSolver::ExtendFace(Source, Top, 1.0);
    Panel.Expect("Top face extension commits a closed solid", Extended && Extended.Payload.Validate().Solid());
    if (Extended) Panel.Equal("Face extension grows both in-plane spans exactly", Extended.Payload.Validate().Volume, 6.0 * 7.0 * 6.0, Tol);

    Deliver<BrepBody> Trimmed = FaceEditSolver::TrimFace(Source, Top, 0.5);
    Panel.Expect("Top face trim commits a closed solid", Trimmed && Trimmed.Payload.Validate().Solid());
    if (Trimmed) Panel.Equal("Face trim shrinks both in-plane spans exactly", Trimmed.Payload.Validate().Volume, 3.0 * 4.0 * 6.0, Tol);

    Deliver<BrepBody> Drafted = FaceEditSolver::Draft(Source, Right, ScalarCriteria::Radians(10.0));
    Panel.Expect("A vertical side draft commits a closed solid", Drafted && Drafted.Payload.Validate().Solid());
    if (Drafted)
    {
        const double Delta = std::tan(ScalarCriteria::Radians(10.0)) * 6.0;
        const double Expected = 5.0 * 6.0 * (4.0 + (4.0 + Delta)) * 0.5;
        Panel.Equal("Draft volume follows the exact trapezoidal prism identity", Drafted.Payload.Validate().Volume, Expected, 1e-7);
    }
    Panel.Expect("The source remains byte-for-byte topologically and metrically unchanged", SameBody(Source, Box({ 0, 0, 0 }, { 4, 5, 6 })) && std::fabs(Source.Validate().Volume - Before.Volume) < Tol);

    Panel.Section("Shell / thicken beyond the single-surface MVP");
    Deliver<BrepBody> Shelled = FaceEditSolver::Shell(Source, Top, 0.5);
    Panel.Expect("A selected top face produces a real hollow shell solid", Shelled && Shelled.Payload.Validate().Solid());
    if (Shelled)
    {
        const BodyReport R = Shelled.Payload.Validate();
        Panel.Equal("Shell volume follows the exact mitered U-profile", R.Volume, 36.25, 1e-7);
        Panel.Expect("Shell has no open/non-manifold/sliver edges", R.OpenEdges == 0 && R.NonManifoldEdges == 0 && R.MisorientedEdges == 0);
    }
    Panel.Expect("Over-thick shell refuses without mutating the source", !FaceEditSolver::Shell(Source, Top, 3.1) && SameBody(Source, Box({ 0, 0, 0 }, { 4, 5, 6 })));

    Panel.Section("Delete and replace face preserve exact rims");
    Deliver<BrepBody> Deleted = FaceEditSolver::DeleteFace(Source, Top);
    Panel.Expect("Delete face commits a five-face open sheet", Deleted && !Deleted.Payload.Validate().Solid() && Deleted.Payload.Validate().Faces == 5 && Deleted.Payload.Validate().OpenEdges == 4);
    NurbsSurface Replacement = ReplacementFor(Source, Top);
    Deliver<BrepBody> Replaced = FaceEditSolver::ReplaceFace(Source, Top, Replacement);
    Panel.Expect("Same-rim replacement commits a closed solid", Replaced && Replaced.Payload.Validate().Solid());
    if (Replaced) Panel.Equal("Same-rim replacement preserves exact source volume", Replaced.Payload.Validate().Volume, Before.Volume, Tol);
    Deliver<NurbsSurface> WrongSurface = NurbsSurface::Plane({ 0, 0, 9 }, Vec3::UnitX(), Vec3::UnitY(), 4, 5);
    Panel.Expect("A mismatched replacement rim refuses transactionally", WrongSurface && !FaceEditSolver::ReplaceFace(Source, Top, WrongSurface.Payload));

    Panel.Section("Healing and unsupported geometry are conservative");
    Deliver<BrepBody> Healed = Offset ? FaceEditSolver::RemoveSlivers(Offset.Payload) : Deliver<BrepBody>{};
    Panel.Expect("Healing re-sews the offset without sliver collapse", Healed && Healed.Payload.Validate().Solid() && Healed.Payload.Validate().OpenEdges == 0);
    const BrepBody Cylinder = BrepBody::Cylinder({ 12, 0, 0 }, { 0, 0, 1 }, 2.0, 5.0).Payload;
    Panel.Expect("Curved source refuses exact general face offset", !FaceEditSolver::OffsetFace(Cylinder, FaceToward(Cylinder, { 0, 0, 1 }), 1.0));
    Panel.Expect("Curved source refuses exact face delete", !FaceEditSolver::DeleteFace(Cylinder, 0));
    Panel.Expect("Source still matches its pre-operation report", SameBody(Source, Box({ 0, 0, 0 }, { 4, 5, 6 })));

    Panel.Section("Console transactions and visible proof image");
    ConsoleHost Commands(SOLIDARC_PROOF_FOLDER, 1280, 800);
    (void)Commands.Document().AddBody("Box", Source);
    const bool OffsetCommand = Commands.Execute("offsetface Box 1 --face=" + std::to_string(Top) + " --name=Offset");
    Panel.Expect("Console exposes the exact offset route", OffsetCommand && Commands.Document().Find("Offset") && !Commands.Document().Find("Box"));
    ConsoleHost ProofHost(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = ProofHost.Document().AddBody("SourceBox", Source.Transformed(Mat4::Translation({ -12, 0, 0 }))).Identity > 0 &&
                       Offset && ProofHost.Document().AddBody("OffsetBox", Offset.Payload.Transformed(Mat4::Translation({ 0, 0, 0 }))).Identity > 0 &&
                       Shelled && ProofHost.Document().AddBody("ShellBox", Shelled.Payload.Transformed(Mat4::Translation({ 12, 0, 0 }))).Identity > 0;
    const std::string ProofName = "Phase36a_FaceEdits";
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / (ProofName + ".png");
    std::error_code Error; std::filesystem::remove(Proof, Error);
    const bool Rendered = Added && ProofHost.Execute("view iso") && ProofHost.Execute("view orbit 35 -15") && ProofHost.Execute("view fit") && ProofHost.Execute("render " + ProofName);
    Panel.Expect("Face-edit source/offset/shell proof render completes", Rendered);
    Panel.Expect("Face-edit proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);

    return Panel.Conclude();
}
