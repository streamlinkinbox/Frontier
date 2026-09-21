//=============================================================================================================================================
// SolidArc Phase 35b: bounded concave/non-convex planar chamfer networks on extruded simple profiles.
#include "Console/ConsoleHost.h"
#include "Kernel/BlendSolver.h"
#include "Kernel/ProfileSolver.h"
#include "VerificationPanel.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace Frontier;

namespace
{
    struct P2 { double X, Y; };

    double Area(const std::vector<P2>& P)
    {
        double A = 0.0;
        for (size_t I = 0; I < P.size(); ++I) A += P[I].X * P[(I + 1) % P.size()].Y - P[(I + 1) % P.size()].X * P[I].Y;
        return std::fabs(A) * 0.5;
    }

    int VerticalEdgeAt(const BrepBody& Body, P2 Target)
    {
        for (int E = 0; E < static_cast<int>(Body.Edges.size()); ++E)
        {
            const BrepEdge& Edge = Body.Edges[E];
            if (Edge.Coedges.size() != 2 || Edge.Closed() || Edge.Curve.Degree != 1) continue;
            const Vec3 A = Body.Vertices[Edge.VertexStart].Point, B = Body.Vertices[Edge.VertexEnd].Point;
            if (std::fabs(A.X - Target.X) < 1e-9 && std::fabs(A.Y - Target.Y) < 1e-9 &&
                std::fabs(B.X - Target.X) < 1e-9 && std::fabs(B.Y - Target.Y) < 1e-9 && std::fabs(A.Z - B.Z) > 1.0) return E;
        }
        return -1;
    }

    std::vector<P2> ChamferedOutline(std::vector<P2> P, const std::vector<int>& Selected, double D)
    {
        std::vector<P2> Out;
        for (size_t I = 0; I < P.size(); ++I)
        {
            if (std::find(Selected.begin(), Selected.end(), static_cast<int>(I)) == Selected.end()) { Out.push_back(P[I]); continue; }
            const P2 Prev = P[(I + P.size() - 1) % P.size()], Next = P[(I + 1) % P.size()];
            const double LP = std::hypot(Prev.X - P[I].X, Prev.Y - P[I].Y), LN = std::hypot(Next.X - P[I].X, Next.Y - P[I].Y);
            Out.push_back({ P[I].X + (Prev.X - P[I].X) * D / LP, P[I].Y + (Prev.Y - P[I].Y) * D / LP });
            Out.push_back({ P[I].X + (Next.X - P[I].X) * D / LN, P[I].Y + (Next.Y - P[I].Y) * D / LN });
        }
        return Out;
    }
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 35b · concave planar chamfer networks");
    const std::vector<P2> Profile = { { 0, 0 }, { 8, 0 }, { 8, 8 }, { 5, 8 }, { 5, 3 }, { 3, 3 }, { 3, 8 }, { 0, 8 } };
    std::vector<Vec3> Points; for (P2 P : Profile) Points.push_back({ P.X, P.Y, 0 });
    auto Curve = NurbsCurve::Polyline(Points, true);
    auto Source = Curve ? BrepBody::Extrude(Curve.Payload, { 0, 0, 1 }, 4.0)
                        : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "profile fixture failed");
    Panel.Expect("The concave U-profile extrusion is a closed solid", Source && Source.Payload.Validate().Solid());
    if (!Source) return Panel.Conclude();

    const int LeftReflex = VerticalEdgeAt(Source.Payload, { 3, 3 });
    const int RightReflex = VerticalEdgeAt(Source.Payload, { 5, 3 });
    Panel.Expect("Both reflex vertical edges are addressable", LeftReflex >= 0 && RightReflex >= 0);
    const double SetBack = 0.4;
    const double SourceArea = Area(Profile), ExpectedArea = Area(ChamferedOutline(Profile, { 4, 5 }, SetBack));
    const double SourceVolume = Source.Payload.Validate().Volume;

    Panel.Section("One transactional route solves both reflex corners from the complete concave profile");
    int Applied = 0;
    auto Result = (LeftReflex >= 0 && RightReflex >= 0)
        ? BlendSolver::ChamferEdges(Source.Payload, { LeftReflex, RightReflex }, SetBack, &Applied)
        : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "edge fixture failed");
    Panel.Expect("The two-edge concave network commits atomically", Result && Result.Payload.Validate().Solid() && Applied == 2);
    if (Result)
    {
        const BodyReport R = Result.Payload.Validate();
        Panel.Expect("The concave network is one manifold solid", R.Hulls == 1 && R.OpenEdges == 0 && R.NonManifoldEdges == 0 && R.MisorientedEdges == 0);
        Panel.Expect("The reconstruction keeps the two new bevel planes", R.Faces == 12 && R.Edges == 30 && R.Vertices == 20);
        Panel.Within("The volume follows the exact chamfered-profile area × length", std::fabs(R.Volume - ExpectedArea * 4.0) / std::max(1.0, ExpectedArea * 4.0), 2e-3);
        Panel.Note("source profile area %.4f, chamfered profile area %.4f, source volume %.4f", SourceArea, ExpectedArea, SourceVolume);
    }
    Panel.Expect("The source remains untouched", Source.Payload.Validate().Faces == 10 && std::fabs(Source.Payload.Validate().Volume - SourceVolume) < 1e-9);

    Panel.Section("Feasibility and transactional refusal");
    auto TooLarge = (LeftReflex >= 0 && RightReflex >= 0)
        ? BlendSolver::ChamferEdges(Source.Payload, { LeftReflex, RightReflex }, 3.0)
        : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "edge fixture failed");
    Panel.Expect("An over-large concave network refuses", !TooLarge);
    Panel.Expect("The refused concave network leaves the source unchanged", Source.Payload.Validate().Solid() && Source.Payload.Validate().Faces == 10 &&
                 std::fabs(Source.Payload.Validate().Volume - SourceVolume) < 1e-9);

    Panel.Section("Console commit and visible proof");
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 800);
    const bool ConsoleBuilt = Host.Execute("polyline (0,0) (8,0) (8,8) (5,8) (5,3) (3,3) (3,8) (0,8) --closed --name=UProfile") &&
                              Host.Execute("extrude UProfile 4 --name=U") && Host.Document().Find("U");
    const SceneFigure* U = Host.Document().Find("U");
    const int ConsoleLeft = U ? VerticalEdgeAt(U->Body, { 3, 3 }) : -1;
    const int ConsoleRight = U ? VerticalEdgeAt(U->Body, { 5, 3 }) : -1;
    const bool ConsoleChamfer = ConsoleBuilt && ConsoleLeft >= 0 && ConsoleRight >= 0 &&
        Host.Execute("chamfer U 0.4 --edges=" + std::to_string(ConsoleLeft) + "," + std::to_string(ConsoleRight) + " --name=ConcaveBevel") &&
        Host.Document().Find("ConcaveBevel") && Host.Document().Find("ConcaveBevel")->Body.Validate().Solid();
    Panel.Expect("The console commits the concave network", ConsoleChamfer);

    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase35b_ConcaveChamferNetwork.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost ProofHost(SOLIDARC_PROOF_FOLDER, 1600, 800);
    const bool Rendered = ProofHost.Document().AddBody("SharpU", Source.Payload.Transformed(Mat4::Translation({ -10, 0, 0 }))).Identity > 0 &&
                          ProofHost.Document().AddBody("ConcaveBevel", Result.Payload.Transformed(Mat4::Translation({ 10, 0, 0 }))).Identity > 0 &&
                          ProofHost.Execute("view iso") && ProofHost.Execute("view fit") && ProofHost.Execute("render Phase35b_ConcaveChamferNetwork");
    Panel.Expect("The concave chamfer proof render completes", Rendered);
    Panel.Expect("The concave chamfer proof PNG is written", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
