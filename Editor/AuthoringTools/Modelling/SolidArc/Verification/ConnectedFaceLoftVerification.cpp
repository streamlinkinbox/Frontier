//=============================================================================================================================================
// 📦 Editor/AuthoringTools/Modelling/SolidArc/Verification/ConnectedFaceLoftVerification.cpp — Phase 35d
//=============================================================================================================================================
// A connected same-body face loft is intentionally bounded. Opposite planar end caps of a canonical axis-aligned box
// can be removed and lofted through the existing prism exactly; the direct route rebuilds that identity prism rather
// than accepting the degenerate genus-one periodic skin produced by a generic seam. Other connected selections refuse.
#include "Kernel/SkinSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>
#include <filesystem>
#include <string>

using namespace Frontier;

namespace
{
constexpr double ExactLimit = 1e-12;

[[nodiscard]] BrepBody Box(Vec3 Low, Vec3 High) noexcept
{
    return BrepBody::Box(Low, High).Payload;
}

[[nodiscard]] int FaceToward(const BrepBody& Body, Vec3 Direction) noexcept
{
    int Best = -1; double BestDot = -2.0;
    for (int F = 0; F < static_cast<int>(Body.Faces.size()); ++F)
    {
        const NurbsSurface& S = Body.Faces[F].Surface;
        const Vec3 N = Body.FaceNormal(F, 0.5 * (S.DomainStartU() + S.DomainEndU()), 0.5 * (S.DomainStartV() + S.DomainEndV()));
        const double D = N.Normalised().Dot(Direction.Normalised());
        if (D > BestDot) { BestDot = D; Best = F; }
    }
    return Best;
}

[[nodiscard]] int CylinderSide(const BrepBody& Body) noexcept
{
    for (int F = 0; F < static_cast<int>(Body.Faces.size()); ++F)
        if (Body.Faces[F].Surface.Classification == SurfaceClassification::Cylinder) return F;
    return -1;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 35d · connected same-body face loft");

    Panel.Section("Opposite end caps of one canonical prism take the exact identity-loft route");
    const BrepBody Source = Box({ 0, 0, 0 }, { 4, 5, 6 });
    const BodyReport Before = Source.Validate();
    const int Top = FaceToward(Source, { 0, 0, 1 });
    const int Bottom = FaceToward(Source, { 0, 0, -1 });
    Panel.Expect("The source prism is a closed V8/E12/F6 solid", Before.Solid() && Before.Vertices == 8 && Before.Edges == 12 && Before.Faces == 6 && Before.Hulls == 1);
    Panel.Expect("Two opposite cap faces are selected", Top >= 0 && Bottom >= 0 && Top != Bottom);

    Deliver<BrepBody> Lofted = SkinSolver::LoftFaces(Source, Top, Source, Bottom);
    Panel.Expect("Connected same-body cap loft commits", Lofted && Lofted.Payload.Validate().Solid());
    if (Lofted)
    {
        const BodyReport After = Lofted.Payload.Validate();
        Panel.Expect("The identity loft remains one genus-zero hull with V8/E12/F6", After.Hulls == 1 && After.Genus == 0 && After.Vertices == 8 && After.Edges == 12 && After.Faces == 6);
        Panel.Within("The loft preserves the exact prism volume", std::fabs(After.Volume - Before.Volume) / Before.Volume, ExactLimit);
        Panel.Within("The loft preserves the exact prism area", std::fabs(After.Area - Before.Area) / Before.Area, ExactLimit);
        Panel.Expect("Every edge remains manifold and oriented", After.OpenEdges == 0 && After.NonManifoldEdges == 0 && After.MisorientedEdges == 0 && After.Oriented);
    }
    Panel.Expect("The source stays immutable after the same-body operation", Source.Vertices.size() == 8 && Source.Edges.size() == 12 && Source.Faces.size() == 6 && std::fabs(Source.Validate().Volume - Before.Volume) < 1e-12);

    Panel.Section("Connected selections outside the bounded identity topology refuse transactionally");
    const int XFace = FaceToward(Source, { 1, 0, 0 });
    const int YFace = FaceToward(Source, { 0, 1, 0 });
    Panel.Expect("Adjacent planar faces refuse instead of manufacturing a collapsed skin", !SkinSolver::LoftFaces(Source, XFace, Source, YFace));
    const BrepBody Round = BrepBody::Cylinder({ 12, 0, 0 }, { 0, 0, 1 }, 2.0, 5.0).Payload;
    const int RoundTop = FaceToward(Round, { 0, 0, 1 });
    const int RoundBottom = FaceToward(Round, { 0, 0, -1 });
    Panel.Expect("Connected cylinder caps remain outside the rectangular identity route", !SkinSolver::LoftFaces(Round, RoundTop, Round, RoundBottom));
    Panel.Expect("A connected curved side selection remains outside the route", !SkinSolver::LoftFaces(Round, RoundTop, Round, CylinderSide(Round)));
    Panel.Expect("A same-body cap/side refusal leaves the cylinder unchanged", Round.Faces.size() == 3 && std::fabs(Round.Validate().Volume - ScalarCriteria::Pi * 4.0 * 5.0) / (ScalarCriteria::Pi * 4.0 * 5.0) < 2e-3);

    Panel.Section("Console commit and visible source/result proof");
    ConsoleHost Commit(SOLIDARC_PROOF_FOLDER, 1280, 800);
    (void)Commit.Document().AddBody("Prism", Source);
    const std::string KeepCommand = "loft Prism:f" + std::to_string(Top) + " Prism:f" + std::to_string(Bottom) + " --keep --name=CapLoft";
    const bool Kept = Commit.Execute(KeepCommand);
    Panel.Expect("`loft Body:fN Body:fM --keep` commits and keeps the same source", Kept && Commit.Document().Find("Prism") && Commit.Document().Find("CapLoft") && Commit.Document().Find("CapLoft")->Body.Validate().Solid());
    ConsoleHost Consume(SOLIDARC_PROOF_FOLDER, 1280, 800);
    (void)Consume.Document().AddBody("Prism", Source);
    const bool Consumed = Consume.Execute("loft Prism:f" + std::to_string(Top) + " Prism:f" + std::to_string(Bottom) + " --name=CapLoft");
    Panel.Expect("The non-keep command replaces the source transactionally", Consumed && !Consume.Document().Find("Prism") && Consume.Document().Find("CapLoft"));

    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase35d_ConnectedFaceLoft.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost ProofHost(SOLIDARC_PROOF_FOLDER, 1600, 800);
    const bool Added = ProofHost.Document().AddBody("SourcePrism", Source.Transformed(Mat4::Translation({ -7, 0, 0 }))).Identity > 0 &&
                       Lofted && ProofHost.Document().AddBody("ConnectedCapLoft", Lofted.Payload.Transformed(Mat4::Translation({ 7, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && ProofHost.Execute("view iso") && ProofHost.Execute("view orbit 35 -12") && ProofHost.Execute("view fit") && ProofHost.Execute("render Phase35d_ConnectedFaceLoft");
    Panel.Expect("The connected-face source/result proof render completes", Rendered);
    Panel.Expect("The connected-face proof PNG is written", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);

    return Panel.Conclude();
}
