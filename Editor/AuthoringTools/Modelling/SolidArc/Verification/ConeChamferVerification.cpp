//=============================================================================================================================================
// SolidArc Phase 35c: exact chamfer of a native right-cone circular cap edge.
#include "Console/ConsoleHost.h"
#include "Kernel/BlendSolver.h"
#include "VerificationPanel.h"

#include <cmath>
#include <filesystem>
#include <string>

using namespace Frontier;

namespace
{
    int CircularCapEdge(const BrepBody& Body, bool Upper)
    {
        int Candidate = -1;
        for (int E = 0; E < static_cast<int>(Body.Edges.size()); ++E)
        {
            const BrepEdge& Edge = Body.Edges[E];
            if (!Edge.Closed() || Edge.Curve.Classification != CurveClassification::Circle || Edge.Coedges.size() != 2) continue;
            Vec3 Centre{};
            for (int Coedge : Edge.Coedges)
            {
                const int Face = Body.Coedges[Coedge].Face;
                const NurbsSurface& S = Body.Faces[Face].Surface;
                if (S.Classification == SurfaceClassification::Plane)
                    Centre = S.Sample(0.5 * (S.DomainStartU() + S.DomainEndU()), 0.5 * (S.DomainStartV() + S.DomainEndV()));
            }
            if ((Upper && Centre.Z > 4.0) || (!Upper && Centre.Z < 1.0)) { Candidate = E; break; }
        }
        return Candidate;
    }
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 35c · native conical-cap chamfer");
    const double RadiusFoot = 3.0, RadiusTop = 1.0, Height = 5.0, SetBack = 0.3;
    auto Source = BrepBody::Cone({ 0, 0, 0 }, { 0, 0, 1 }, RadiusFoot, RadiusTop, Height);
    Panel.Expect("The native cone fixture is a closed solid", Source && Source.Payload.Validate().Solid());
    if (!Source) return Panel.Conclude();
    const double OriginalVolume = Source.Payload.Validate().Volume;
    const int Edge = CircularCapEdge(Source.Payload, true);
    Panel.Expect("The upper rational circular cap edge is found", Edge >= 0);

    const double Slant = std::hypot(Height, RadiusTop - RadiusFoot);
    const double JoinHeight = SetBack * Height / Slant;
    const double JoinRadius = RadiusTop - SetBack * (RadiusTop - RadiusFoot) / Slant;
    const double CapRadius = RadiusTop - SetBack;
    const double ExpectedVolume = ScalarCriteria::Pi / 3.0 *
        (JoinHeight * (RadiusFoot * RadiusFoot + RadiusFoot * JoinRadius + JoinRadius * JoinRadius) +
         (Height - JoinHeight) * (JoinRadius * JoinRadius + JoinRadius * CapRadius + CapRadius * CapRadius));

    Panel.Section("Exact conical-cap route");
    int Applied = 0;
    auto Result = Edge >= 0 ? BlendSolver::ChamferEdges(Source.Payload, { Edge }, SetBack, &Applied)
                            : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "edge fixture failed");
    Panel.Expect("The native conical circular edge chamfer commits", Result && Result.Payload.Validate().Solid() && Applied == 1);
    if (Result)
    {
        const BodyReport R = Result.Payload.Validate();
        Panel.Expect("The cone chamfer has V3/E5/F4 topology", R.Vertices == 3 && R.Edges == 5 && R.Faces == 4);
        Panel.Within("The two-cone frustum volume is exact", std::fabs(R.Volume - ExpectedVolume) / ExpectedVolume, 2e-3);
        Panel.Expect("The source cone remains unchanged", Source.Payload.Validate().Faces == 3 &&
                     std::fabs(Source.Payload.Validate().Volume - OriginalVolume) < 1e-9);
    }

    Panel.Section("Feasibility and scope refusals");
    Panel.Expect("A set-back reaching the cone apex/top radius refuses", Edge >= 0 && !BlendSolver::ChamferEdge(Source.Payload, Edge, 1.0));
    auto Torus = BrepBody::Torus({ 10, 0, 0 }, { 0, 0, 1 }, 3.0, 1.0);
    Panel.Expect("A general torus curved edge remains explicit refusal", Torus && !BlendSolver::ChamferEdge(Torus.Payload, 0, SetBack));

    Panel.Section("Console commit and visible proof");
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 800);
    const bool ConsoleBuilt = Host.Execute("cone (0,0,0) 3 1 5 --axis=(0,0,1) --name=NativeCone") && Host.Document().Find("NativeCone");
    const SceneFigure* Cone = Host.Document().Find("NativeCone");
    const int ConsoleEdge = Cone ? CircularCapEdge(Cone->Body, true) : -1;
    const bool ConsoleChamfer = ConsoleBuilt && ConsoleEdge >= 0 &&
        Host.Execute("chamfer NativeCone 0.3 --edges=" + std::to_string(ConsoleEdge) + " --name=ConeBevel") &&
        Host.Document().Find("ConeBevel") && Host.Document().Find("ConeBevel")->Body.Validate().Solid();
    Panel.Expect("The console commits the native cone cap chamfer", ConsoleChamfer);

    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase35c_ConeCapChamfer.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost ProofHost(SOLIDARC_PROOF_FOLDER, 1600, 800);
    const bool Rendered = ProofHost.Document().AddBody("SharpCone", Source.Payload.Transformed(Mat4::Translation({ -7, 0, 0 }))).Identity > 0 &&
                          ProofHost.Document().AddBody("ConeBevel", Result.Payload.Transformed(Mat4::Translation({ 7, 0, 0 }))).Identity > 0 &&
                          ProofHost.Execute("view iso") && ProofHost.Execute("view fit") && ProofHost.Execute("render Phase35c_ConeCapChamfer");
    Panel.Expect("The cone-chamfer proof render completes", Rendered);
    Panel.Expect("The cone-chamfer proof PNG is written", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
