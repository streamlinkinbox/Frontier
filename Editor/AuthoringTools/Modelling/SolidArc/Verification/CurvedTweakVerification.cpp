//=============================================================================================================================================
// SolidArc Phase 35a: exact native circular-cylinder cap and edge tweaks.
//=============================================================================================================================================
#include "Console/ConsoleHost.h"
#include "Kernel/TweakSolver.h"
#include "VerificationPanel.h"

#include <cmath>
#include <filesystem>
#include <string>

using namespace Frontier;

namespace
{
    int CapFace(const BrepBody& Body, Vec3 Direction)
    {
        int Best = -1; double Score = -2.0;
        for (int F = 0; F < static_cast<int>(Body.Faces.size()); ++F)
        {
            if (Body.Faces[F].Surface.Classification != SurfaceClassification::Plane) continue;
            const NurbsSurface& S = Body.Faces[F].Surface;
            const Vec3 N = Body.FaceNormal(F, 0.5 * (S.DomainStartU() + S.DomainEndU()), 0.5 * (S.DomainStartV() + S.DomainEndV()));
            const double D = N.Normalised().Dot(Direction.Normalised());
            if (D > Score) { Score = D; Best = F; }
        }
        return Best;
    }

    int CapEdge(const BrepBody& Body, int Face)
    {
        if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return -1;
        for (int Loop : Body.Faces[Face].Loops)
            for (int Coedge : Body.Loops[Loop].Coedges)
            {
                const int Edge = Body.Coedges[Coedge].Edge;
                if (Body.Edges[Edge].Closed() && Body.Edges[Edge].Curve.Classification == CurveClassification::Circle) return Edge;
            }
        return -1;
    }

    double Relative(double A, double B) { return std::fabs(A - B) / std::max(1.0, std::fabs(B)); }
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 35a · native curved-face and curved-edge tweaks");
    auto Source = BrepBody::Cylinder({ 0, 0, 0 }, { 0, 0, 1 }, 2.0, 4.0);
    Panel.Expect("The native cylinder fixture is a closed solid", Source && Source.Payload.Validate().Solid());
    if (!Source) return Panel.Conclude();

    const BrepBody Original = Source.Payload;
    const int Top = CapFace(Original, { 0, 0, 1 });
    const int Rim = CapEdge(Original, Top);
    Panel.Expect("The selected cap and its closed circular edge are found", Top >= 0 && Rim >= 0);

    Panel.Section("Axial cap and circular-edge moves retain exact analytic cylinder geometry");
    auto Raised = TweakSolver::TranslateFace(Original, Top, { 0, 0, 1 }, false);
    Panel.Expect("The curved planar cap translates axially", Raised && Raised.Payload.Validate().Solid());
    if (Raised)
    {
        const BodyReport R = Raised.Payload.Validate();
        Panel.Expect("The cap move keeps native V2/E3/F3 topology", R.Vertices == 2 && R.Edges == 3 && R.Faces == 3);
        Panel.Within("The raised cylinder volume is 20π", Relative(R.Volume, 20.0 * ScalarCriteria::Pi), 2e-3);
        Panel.Expect("The moved rim remains a rational closed circle", Raised.Payload.Edges[Rim].Closed() &&
                     Raised.Payload.Edges[Rim].Curve.Classification == CurveClassification::Circle &&
                     Raised.Payload.Edges[Rim].Curve.Rational());
    }
    Panel.Within("The source cylinder remains 16π", Relative(Original.Validate().Volume, 16.0 * ScalarCriteria::Pi), 2e-3);

    auto RaisedByEdge = TweakSolver::TranslateEdge(Original, Rim, { 0, 0, 1 }, false);
    Panel.Expect("The closed circular cap edge translates axially as one exact cap edit", RaisedByEdge && RaisedByEdge.Payload.Validate().Solid());
    if (Raised && RaisedByEdge)
        Panel.Within("Face and circular-edge routes agree numerically", Relative(Raised.Payload.Validate().Volume, RaisedByEdge.Payload.Validate().Volume), 1e-9);

    Panel.Section("Curved-domain safety refusals remain explicit");
    Panel.Expect("A lateral cap move refuses instead of shearing the cylinder", !TweakSolver::TranslateFace(Original, Top, { 1, 0, 0 }, false));
    Panel.Expect("A lateral circular-edge move refuses", !TweakSolver::TranslateEdge(Original, Rim, { 1, 0, 0 }, false));
    const int Side = [&]() { for (int F = 0; F < static_cast<int>(Original.Faces.size()); ++F) if (Original.Faces[F].Surface.Classification == SurfaceClassification::Cylinder) return F; return -1; }();
    Panel.Expect("The curved cylinder side face remains outside the bounded route", Side >= 0 && !TweakSolver::TranslateFace(Original, Side, { 0, 0, 1 }, false));
    auto Sphere = BrepBody::Sphere({ 8, 0, 0 }, 2.0);
    Panel.Expect("A non-native curved body refuses without approximation", Sphere && !TweakSolver::TranslateFace(Sphere.Payload, 0, { 0, 0, 1 }, false));

    Panel.Section("Console commit and visible proof");
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 800);
    (void)Host.Document().AddBody("Source", Original);
    const bool FaceCommand = Host.Execute("tweak Source (0,0,1) --face=" + std::to_string(Top) + " --name=CapMoved");
    const bool EdgeCommand = Host.Execute("tweak CapMoved (0,0,1) --edge=" + std::to_string(Rim) + " --name=EdgeMoved");
    Panel.Expect("The console commits native curved-face and curved-edge tweaks", FaceCommand && EdgeCommand &&
                 !Host.Document().Find("CapMoved") && Host.Document().Find("EdgeMoved") && Host.Document().Find("EdgeMoved")->Body.Validate().Solid());

    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase35a_CurvedCapTweaks.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost ProofHost(SOLIDARC_PROOF_FOLDER, 1600, 800);
    const bool Rendered = ProofHost.Document().AddBody("Original", Original.Transformed(Mat4::Translation({ -7, 0, 0 }))).Identity > 0 &&
                          ProofHost.Document().AddBody("CapMoved", Raised.Payload.Transformed(Mat4::Translation({ 0, 0, 0 }))).Identity > 0 &&
                          ProofHost.Document().AddBody("EdgeMoved", RaisedByEdge.Payload.Transformed(Mat4::Translation({ 7, 0, 0 }))).Identity > 0 &&
                          ProofHost.Execute("view iso") && ProofHost.Execute("view fit") && ProofHost.Execute("render Phase35a_CurvedCapTweaks");
    Panel.Expect("The curved-tweak proof render completes", Rendered);
    Panel.Expect("The curved-tweak proof PNG is written", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
