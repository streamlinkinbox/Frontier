//=============================================================================================================================================
// SolidArc · Phase 36d · arbitrary non-planar edge-loop chamfer
//
// Bounded slice: one closed, non-planar six-edge loop on a convex box. Every selected edge remains
// straight and has planar supports, but the loop's vertices occupy two support planes. The full
// convex half-space system is solved together so the mitres are shared and the transaction cannot
// publish a partial/sequential approximation.
//=============================================================================================================================================
#include "Kernel/BlendSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace Frontier;

namespace
{
constexpr double SetBack = 0.2;
const std::vector<int> LoopEdges{ 0, 8, 4, 7, 11, 3 };

[[nodiscard]] bool IsClosedNonPlanarLoop(const BrepBody& Body, const std::vector<int>& Edges) noexcept
{
    std::vector<int> Vertices;
    for (int Edge : Edges)
    {
        if (Edge < 0 || Edge >= static_cast<int>(Body.Edges.size())) return false;
        Vertices.push_back(Body.Edges[Edge].VertexStart);
        Vertices.push_back(Body.Edges[Edge].VertexEnd);
    }
    std::sort(Vertices.begin(), Vertices.end());
    Vertices.erase(std::unique(Vertices.begin(), Vertices.end()), Vertices.end());
    if (Vertices.size() != Edges.size()) return false;
    for (int Vertex : Vertices)
    {
        int Degree = 0;
        for (int Edge : Edges)
            Degree += Body.Edges[Edge].VertexStart == Vertex || Body.Edges[Edge].VertexEnd == Vertex;
        if (Degree != 2) return false;
    }
    const Vec3 A = Body.Vertices[Vertices[0]].Point;
    const Vec3 B = Body.Vertices[Vertices[1]].Point;
    const Vec3 C = Body.Vertices[Vertices[2]].Point;
    const Vec3 Normal = (B - A).Cross(C - A).Normalised();
    if (Normal.Length() <= 1e-9) return false;
    for (int Vertex : Vertices)
        if (std::fabs((Body.Vertices[Vertex].Point - A).Dot(Normal)) > 1e-6) return true;
    return false;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 36d · arbitrary non-planar edge-loop chamfer");
    const BrepBody Source = BrepBody::Box({ 0, 0, 0 }, { 4, 5, 6 }).Payload;
    const BodyReport Before = Source.Validate();
    Panel.Expect("The convex source is a valid V8/E12/F6 solid", Before.Solid() && Before.Vertices == 8 && Before.Edges == 12 && Before.Faces == 6);
    Panel.Expect("The six selected edges form one closed non-planar loop", IsClosedNonPlanarLoop(Source, LoopEdges));

    int Applied = 0;
    Deliver<BrepBody> Result = BlendSolver::ChamferEdges(Source, LoopEdges, SetBack, &Applied);
    Panel.Expect("The non-planar loop chamfer commits atomically", Result && Result.Payload.Validate().Solid() && Applied == static_cast<int>(LoopEdges.size()));
    if (Result)
    {
        const BodyReport R = Result.Payload.Validate();
        Panel.Expect("The loop result is one manifold oriented hull", R.Hulls == 1 && R.Genus == 0 && R.OpenEdges == 0 && R.NonManifoldEdges == 0 && R.MisorientedEdges == 0);
        Panel.Expect("The full loop creates shared mitre topology", R.Vertices == 14 && R.Edges == 24 && R.Faces == 12);
        Panel.Expect("The loop removes positive material without over-cutting", R.Volume > 0.0 && R.Volume < Before.Volume && Before.Volume - R.Volume < 1.0);
    }
    Panel.Expect("The non-planar loop source remains immutable", Source.Validate().Faces == Before.Faces && std::fabs(Source.Validate().Volume - Before.Volume) < 1e-12);

    auto Cylinder = BrepBody::Cylinder({ 10, 0, 0 }, { 0, 0, 1 }, 2.0, 4.0);
    Panel.Expect("A curved-support loop remains outside the planar half-space route", Cylinder && !BlendSolver::ChamferEdges(Cylinder.Payload, { 0, 1, 2 }, SetBack));
    Panel.Expect("An open non-planar selection refuses before reconstruction", !BlendSolver::ChamferEdges(Source, { 0, 8, 5, 6 }, SetBack));

    Panel.Section("Console transaction and visible proof");
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = Host.Document().AddBody("NonPlanarLoop", Source).Identity > 0;
    const bool ConsoleCommit = Added && Host.Execute("chamfer NonPlanarLoop 0.2 --edges=0,8,4,7,11,3 --name=LoopChamfer") &&
                               Host.Document().Find("LoopChamfer") && Host.Document().Find("LoopChamfer")->Body.Validate().Solid();
    Panel.Expect("The console commits the non-planar loop transaction", ConsoleCommit);

    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase36d_ArbitraryNonPlanarEdgeLoop.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    ConsoleHost ProofHost(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Rendered = Result &&
        ProofHost.Document().AddBody("LoopSource", Source.Transformed(Mat4::Translation({ -8, 0, 0 }))).Identity > 0 &&
        ProofHost.Document().AddBody("LoopChamfer", Result.Payload.Transformed(Mat4::Translation({ 8, 0, 0 }))).Identity > 0 &&
        ProofHost.Execute("view iso") && ProofHost.Execute("view fit") && ProofHost.Execute("render Phase36d_ArbitraryNonPlanarEdgeLoop");
    Panel.Expect("The non-planar loop source/result proof render completes", Rendered);
    Panel.Expect("The non-planar loop proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
