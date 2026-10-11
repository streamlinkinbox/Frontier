//=============================================================================================================================================
// SolidArc · Phase 36b · general connected same-body face-loft slice
//
// This slice expands the safe domain without claiming arbitrary same-body surgery:
// native cylinder/cone cap pairs take exact identity routes, while a same-document multi-hull
// selection can make a non-identity ruled replacement between dissimilar planar rims. Adjacent
// faces and duplicate bands still refuse; every result is validated and healed transactionally.
//=============================================================================================================================================
#include "Kernel/SkinSolver.h"
#include "Kernel/IntersectionSolver.h"
#include "Kernel/FaceEditSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>
#include <filesystem>
#include <string>

using namespace Frontier;

namespace
{
constexpr double VolumeLimit = 1e-3;

[[nodiscard]] BrepBody Box(Vec3 A, Vec3 B) noexcept { return BrepBody::Box(A, B).Payload; }
[[nodiscard]] BrepBody Cylinder(Vec3 A, Vec3 Axis, double Radius, double Height) noexcept { return BrepBody::Cylinder(A, Axis, Radius, Height).Payload; }
[[nodiscard]] BrepBody Cone(Vec3 A, Vec3 Axis, double R0, double R1, double Height) noexcept { return BrepBody::Cone(A, Axis, R0, R1, Height).Payload; }

[[nodiscard]] int FaceToward(const BrepBody& Body, Vec3 Direction) noexcept
{
    int Best = -1; double Score = -2.0;
    for (int F = 0; F < static_cast<int>(Body.Faces.size()); ++F)
    {
        const BrepFace& Face = Body.Faces[F];
        const Vec3 N = Body.FaceNormal(F, 0.5 * (Face.Surface.DomainStartU() + Face.Surface.DomainEndU()),
                                       0.5 * (Face.Surface.DomainStartV() + Face.Surface.DomainEndV()));
        const double D = N.Normalised().Dot(Direction.Normalised());
        if (D > Score) { Score = D; Best = F; }
    }
    return Best;
}

[[nodiscard]] int FaceAt(const BrepBody& Body, double X, Vec3 Direction) noexcept
{
    int Best = -1; double Score = -ScalarCriteria::Infinity;
    for (int F = 0; F < static_cast<int>(Body.Faces.size()); ++F)
    {
        const BrepFace& Face = Body.Faces[F];
        const Vec3 N = Body.FaceNormal(F, 0.5 * (Face.Surface.DomainStartU() + Face.Surface.DomainEndU()),
                                       0.5 * (Face.Surface.DomainStartV() + Face.Surface.DomainEndV()));
        const Box3 B = Face.Surface.Bounds();
        const double CentreX = 0.5 * (B.Low.X + B.High.X);
        const double Candidate = N.Normalised().Dot(Direction.Normalised()) * 1000.0 - std::fabs(CentreX - X);
        if (Candidate > Score) { Score = Candidate; Best = F; }
    }
    return Best;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 36b · general connected same-body face lofts");

    Panel.Section("Native analytic connected solids take exact identity replacement routes");
    const BrepBody CylinderBody = Cylinder({ 0, 0, 0 }, { 0, 0, 1 }, 2.0, 5.0);
    const int CylinderTop = FaceToward(CylinderBody, { 0, 0, 1 });
    const int CylinderBottom = FaceToward(CylinderBody, { 0, 0, -1 });
    const BodyReport CylinderBefore = CylinderBody.Validate();
    Deliver<BrepBody> CylinderResult = SkinSolver::LoftFaces(CylinderBody, CylinderTop, CylinderBody, CylinderBottom);
    Panel.Expect("Connected cylinder caps commit through the analytic identity route", CylinderResult && CylinderResult.Payload.Validate().Solid());
    if (CylinderResult)
    {
        Panel.Expect("The cylinder keeps V2/E3/F3 and one hull", CylinderResult.Payload.Vertices.size() == 2 && CylinderResult.Payload.Edges.size() == 3 && CylinderResult.Payload.Faces.size() == 3 && CylinderResult.Payload.Validate().Hulls == 1);
        Panel.Within("The cylinder identity preserves volume", std::fabs(CylinderResult.Payload.Validate().Volume - CylinderBefore.Volume) / CylinderBefore.Volume, 1e-12);
    }
    Panel.Expect("The cylinder source remains immutable", CylinderBody.Faces.size() == 3 && std::fabs(CylinderBody.Validate().Volume - CylinderBefore.Volume) < 1e-12);

    const BrepBody ConeBody = Cone({ 8, 0, 0 }, { 0, 0, 1 }, 3.0, 1.0, 5.0);
    const int ConeTop = FaceToward(ConeBody, { 0, 0, 1 });
    const int ConeBottom = FaceToward(ConeBody, { 0, 0, -1 });
    int ConeSide = -1;
    for (int F = 0; F < static_cast<int>(ConeBody.Faces.size()); ++F)
        if (ConeBody.Faces[F].Surface.Classification == SurfaceClassification::Cone) ConeSide = F;
    const BodyReport ConeBefore = ConeBody.Validate();
    Deliver<BrepBody> ConeResult = SkinSolver::LoftFaces(ConeBody, ConeTop, ConeBody, ConeBottom);
    Panel.Expect("Connected cone caps commit through the analytic identity route", ConeResult && ConeResult.Payload.Validate().Solid());
    if (ConeResult) Panel.Within("The cone identity preserves volume", std::fabs(ConeResult.Payload.Validate().Volume - ConeBefore.Volume) / ConeBefore.Volume, 1e-12);
    Panel.Expect("A cap-to-side selection remains an explicit refusal", ConeSide >= 0 && !SkinSolver::LoftFaces(ConeBody, ConeTop, ConeBody, ConeSide));

    Panel.Section("A hole-free prismatic extrusion takes a connected cap-defined replacement route");
    Deliver<NurbsCurve> HexProfile = NurbsCurve::Polyline({ { 2, 0, 0 }, { 1, 1.7320508075688772, 0 }, { -1, 1.7320508075688772, 0 },
                                                              { -2, 0, 0 }, { -1, -1.7320508075688772, 0 }, { 1, -1.7320508075688772, 0 } }, true);
    Deliver<BrepBody> HexPrism = HexProfile ? BrepBody::Extrude(HexProfile.Payload, { 0, 0, 1 }, 5.0)
                                             : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "hex profile refused");
    Panel.Expect("The prismatic connected fixture is a valid single solid", HexPrism && HexPrism.Payload.Validate().Solid() && HexPrism.Payload.Validate().Hulls == 1);
    const int HexBottom = HexPrism ? FaceToward(HexPrism.Payload, { 0, 0, -1 }) : -1;
    const int HexTop = HexPrism ? FaceToward(HexPrism.Payload, { 0, 0, 1 }) : -1;
    const double HexSourceVolume = HexPrism ? HexPrism.Payload.Validate().Volume : 0.0;
    Deliver<BrepBody> HexReplacement = HexPrism ? SkinSolver::LoftFaces(HexPrism.Payload, HexBottom, HexPrism.Payload, HexTop)
                                                : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "hex fixture refused");
    Panel.Expect("Connected planar end caps replace native extrusion sides transactionally", HexReplacement && HexReplacement.Payload.Validate().Solid());
    if (HexReplacement)
    {
        const BodyReport R = HexReplacement.Payload.Validate();
        Panel.Expect("The connected replacement is one oriented manifold hull", R.Hulls == 1 && R.OpenEdges == 0 && R.NonManifoldEdges == 0 && R.MisorientedEdges == 0);
        Panel.Expect("The replacement remains positive-volume and preserves its cap-defined volume", R.Volume > ScalarCriteria::VolumeTolerance && std::fabs(R.Volume - HexSourceVolume) / HexSourceVolume < 1e-6);
        Deliver<BrepBody> Healed = FaceEditSolver::RemoveSlivers(HexReplacement.Payload);
        Panel.Expect("Healing preserves the accepted connected replacement", Healed && Healed.Payload.Validate().Solid() && Healed.Payload.Validate().OpenEdges == 0);
    }
    Panel.Expect("The prismatic source remains immutable after replacement", HexPrism && HexPrism.Payload.Validate().Faces == 8 && std::fabs(HexPrism.Payload.Validate().Volume - HexSourceVolume) < 1e-9);

    Deliver<BrepBody> HexSideReplacement = HexPrism ? SkinSolver::LoftFaces(HexPrism.Payload, 0, HexPrism.Payload, 3)
                                                    : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "hex fixture refused");
    Panel.Expect("Separated connected side faces take a non-identity ruled replacement", HexSideReplacement && HexSideReplacement.Payload.Validate().Solid());
    if (HexSideReplacement)
    {
        const BodyReport R = HexSideReplacement.Payload.Validate();
        Panel.Expect("The side replacement remains one valid positive-volume hull", R.Hulls == 1 && R.OpenEdges == 0 && R.NonManifoldEdges == 0 && R.MisorientedEdges == 0 && R.Volume > ScalarCriteria::VolumeTolerance);
        Panel.Expect("The side replacement changes topology without mutating the source", R.Faces != HexPrism.Payload.Validate().Faces && HexPrism.Payload.Validate().Faces == 8);
    }

    Panel.Section("Dissimilar same-document hulls take a non-identity replacement loft");
    const BrepBody Small = Box({ 0, 0, 0 }, { 4, 4, 4 });
    const BrepBody Large = Box({ 10, -1, -1 }, { 14, 5, 5 });
    Deliver<BrepBody> MultiHull = IntersectionSolver::Combine(Small, Large, BodyOperation::Union);
    Panel.Expect("The fixture is one B-rep containing two valid disconnected solids", MultiHull && MultiHull.Payload.Validate().Solid() && MultiHull.Payload.Validate().Hulls == 2);
    const int SmallRim = MultiHull ? FaceAt(MultiHull.Payload, 4.0, { 1, 0, 0 }) : -1;
    const int LargeRim = MultiHull ? FaceAt(MultiHull.Payload, 10.0, { -1, 0, 0 }) : -1;
    const double SourceVolume = MultiHull ? MultiHull.Payload.Validate().Volume : 0.0;
    Deliver<BrepBody> Replacement = MultiHull ? SkinSolver::LoftFaces(MultiHull.Payload, SmallRim, MultiHull.Payload, LargeRim)
                                               : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "fixture refused");
    Panel.Expect("Dissimilar planar rims commit a non-identity same-body bridge", Replacement && Replacement.Payload.Validate().Solid());
    if (Replacement)
    {
        const BodyReport R = Replacement.Payload.Validate();
        Panel.Expect("The bridge heals to one manifold genus-zero hull", R.Hulls == 1 && R.Genus == 0 && R.OpenEdges == 0 && R.NonManifoldEdges == 0 && R.MisorientedEdges == 0);
        Panel.Within("The ruled replacement volume is 360 m³", std::fabs(R.Volume - 360.0) / 360.0, VolumeLimit);
        Panel.Expect("The result is non-identity (it adds the six-unit replacement band)", R.Volume > SourceVolume + 100.0);
        Deliver<BrepBody> Healed = FaceEditSolver::RemoveSlivers(Replacement.Payload);
        Panel.Expect("General topology healing accepts the natural replacement result", Healed && Healed.Payload.Validate().Solid() && Healed.Payload.Validate().OpenEdges == 0);
    }
    Panel.Expect("The multi-hull source remains unchanged after the replacement", MultiHull && MultiHull.Payload.Validate().Hulls == 2 && std::fabs(MultiHull.Payload.Validate().Volume - SourceVolume) < 1e-9);
    Panel.Expect("Adjacent same-body faces still refuse instead of duplicating an overlapping band", !SkinSolver::LoftFaces(Small, FaceToward(Small, { 1, 0, 0 }), Small, FaceToward(Small, { 0, 1, 0 })));

    Panel.Section("Console transaction and visible proof");
    ConsoleHost Commands(SOLIDARC_PROOF_FOLDER, 1280, 800);
    (void)Commands.Document().AddBody("Cylinder", CylinderBody);
    const bool Committed = Commands.Execute("loft Cylinder:f" + std::to_string(CylinderTop) + " Cylinder:f" + std::to_string(CylinderBottom) + " --keep --name=CylinderIdentity");
    Panel.Expect("The console commits and keeps a connected cylinder cap identity loft", Committed && Commands.Document().Find("Cylinder") && Commands.Document().Find("CylinderIdentity"));

    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase36b_GeneralConnectedFaceLoft.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    ConsoleHost ProofHost(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = Replacement &&
        ProofHost.Document().AddBody("Cylinder", CylinderBody.Transformed(Mat4::Translation({ -18, 0, 0 }))).Identity > 0 &&
        ProofHost.Document().AddBody("Cone", ConeBody.Transformed(Mat4::Translation({ -8, 0, 0 }))).Identity > 0 &&
        HexPrism && HexReplacement &&
        ProofHost.Document().AddBody("HexPrismSource", HexPrism.Payload.Transformed(Mat4::Translation({ 2, 0, 0 }))).Identity > 0 &&
        ProofHost.Document().AddBody("HexConnectedReplacement", HexReplacement.Payload.Transformed(Mat4::Translation({ 2, 8, 0 }))).Identity > 0 &&
        ProofHost.Document().AddBody("MultiHullSource", MultiHull.Payload.Transformed(Mat4::Translation({ 12, 0, 0 }))).Identity > 0 &&
        ProofHost.Document().AddBody("NonIdentityBridge", Replacement.Payload.Transformed(Mat4::Translation({ 12, 8, 0 }))).Identity > 0;
    const bool Rendered = Added && ProofHost.Execute("view iso") && ProofHost.Execute("view orbit 35 -15") && ProofHost.Execute("view fit") && ProofHost.Execute("render Phase36b_GeneralConnectedFaceLoft");
    Panel.Expect("The general connected-face source/result proof render completes", Rendered);
    Panel.Expect("The general connected-face proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);

    return Panel.Conclude();
}
