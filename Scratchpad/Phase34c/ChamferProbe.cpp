// Scratch probe for the topological chamfer: analytic volumes on a box edge, a hexagonal prism edge and both rims.
#include "Kernel/ChamferSolver.h"
#include "Kernel/ProfileSolver.h"
#include "Kernel/SkinSolver.h"
#include <cstdio>
#include <cmath>
using namespace Frontier;
static void Report(const char* Name, const Deliver<BrepBody>& R, double Expected)
{
    if (!R) { std::printf("%-34s REFUSED: %s\n", Name, R.Denial.Detail); return; }
    BodyReport B = R.Payload.Validate();
    std::printf("%-34s V%d E%d F%d genus %d solid %d  vol %.6f  expected %.6f  err %.2e\n", Name, B.Vertices, B.Edges, B.Faces, B.Genus, (int)B.Solid(), B.Volume, Expected, std::fabs(B.Volume - Expected) / Expected);
}
int Hex();
int main()
{
    Deliver<BrepBody> Box = BrepBody::Box({ 0, 0, 0 }, { 4, 3, 2 });
    if (!Box) { std::printf("no box\n"); return 1; }
    const double d = 0.4;
    // Box: edge 0..11 — pick each and report; expected = 24 − 0.5·d²·L with L the edge length.
    for (int E = 0; E < (int)Box.Payload.Edges.size(); ++E)
    {
        const BrepEdge& Ed = Box.Payload.Edges[E];
        double L = Box.Payload.Vertices[Ed.VertexStart].Point.Distance(Box.Payload.Vertices[Ed.VertexEnd].Point);
        char Name[64]; std::snprintf(Name, sizeof Name, "box edge %d (L=%.0f)", E, L);
        Report(Name, ChamferSolver::ChamferEdge(Box.Payload, E, d), 24.0 - 0.5 * d * d * L);
    }
    // Box top face rim: P0 = 14, corners 90° → Σ cot(45°) = 4: removed = 14·d²/2 − 4·d³/3
    for (int F = 0; F < (int)Box.Payload.Faces.size(); ++F)
    {
        char Name[64]; std::snprintf(Name, sizeof Name, "box face %d rim", F);
        Vec3 N = Box.Payload.FaceNormal(F, 0.5, 0.5);
        double P0 = std::fabs(N.Z) > 0.5 ? 14.0 : (std::fabs(N.Y) > 0.5 ? 12.0 : 10.0);
        Report(Name, ChamferSolver::ChamferFaceRim(Box.Payload, F, d), 24.0 - (P0 * d * d / 2.0 - 4.0 * d * d * d / 3.0));
    }
    return Hex();
}
// (appended) hexagonal prism: side 2, height 2 — the reproducer's case
int Hex()
{
    std::vector<Vec3> P; for (int I = 0; I < 6; ++I) { double A = ScalarCriteria::TwoPi * I / 6.0; P.push_back({ 2.0 * std::cos(A), 2.0 * std::sin(A), 0.0 }); }
    Deliver<NurbsCurve> Poly = NurbsCurve::Polyline(P, true);
    if (!Poly) { std::printf("no polygon\n"); return 1; }
    Deliver<BrepBody> Prism = BrepBody::Extrude(Poly.Payload, { 0, 0, 1 }, 2.0);
    if (!Prism) { std::printf("no prism: %s\n", Prism.Denial.Detail); return 1; }
    const double d = 0.4, V0 = 6.0 * std::sqrt(3.0) * 2.0;                                // 20.7846
    BodyReport R = Prism.Payload.Validate();
    std::printf("hex prism V%d E%d F%d vol %.6f (exact %.6f)\n", R.Vertices, R.Edges, R.Faces, R.Volume, V0);
    for (int E = 0; E < (int)Prism.Payload.Edges.size(); ++E)
    {
        const BrepEdge& Ed = Prism.Payload.Edges[E];
        Vec3 A = Prism.Payload.Vertices[Ed.VertexStart].Point, B = Prism.Payload.Vertices[Ed.VertexEnd].Point;
        bool Vertical = std::fabs(A.X - B.X) < 1e-9 && std::fabs(A.Y - B.Y) < 1e-9;
        double L = A.Distance(B);
        // vertical edge: dihedral 120° → 0.5 d² sin 120°; cap edge: 90° → 0.5 d²
        double Expected = V0 - (Vertical ? 0.5 * d * d * std::sin(ScalarCriteria::Radians(120.0)) : 0.5 * d * d) * L;
        char Name[64]; std::snprintf(Name, sizeof Name, "hex edge %d (%s)", E, Vertical ? "120 deg" : "cap 90 deg");
        Report(Name, ChamferSolver::ChamferEdge(Prism.Payload, E, d), Expected);
    }
    // rims: cap P0 = 12, interior 120° → Σ cot 60° = 6/√3: removed = 12 d²/2 − (6/√3) d³/3
    for (int F = 0; F < (int)Prism.Payload.Faces.size(); ++F)
    {
        char Name[64]; std::snprintf(Name, sizeof Name, "hex face %d rim", F);
        Vec3 N = Prism.Payload.FaceNormal(F, 0.5, 0.5);
        double Expected = std::fabs(N.Z) > 0.5 ? V0 - (12.0 * d * d / 2.0 - (6.0 / std::sqrt(3.0)) * d * d * d / 3.0)
                                               : V0 - (8.0 * d * d / 2.0 - (2.0 / std::tan(ScalarCriteria::Radians(45.0)) + 2.0 / std::tan(ScalarCriteria::Radians(45.0))) * d * d * d / 3.0);
        Report(Name, ChamferSolver::ChamferFaceRim(Prism.Payload, F, d), Expected);
    }
    // dispatch: the full top rim as an edge set
    std::vector<int> Top; for (int E = 0; E < (int)Prism.Payload.Edges.size(); ++E) { const BrepEdge& Ed = Prism.Payload.Edges[E]; if (Prism.Payload.Vertices[Ed.VertexStart].Point.Z > 1.5 && Prism.Payload.Vertices[Ed.VertexEnd].Point.Z > 1.5) Top.push_back(E); }
    std::printf("top rim edges: %zu → RimFace %d\n", Top.size(), ChamferSolver::RimFace(Prism.Payload, Top));
    Report("hex dispatch(top rim set)", ChamferSolver::ChamferEdges(Prism.Payload, Top, d), V0 - (12.0 * d * d / 2.0 - (6.0 / std::sqrt(3.0)) * d * d * d / 3.0));
    std::vector<int> Two = { Top[0], Top[1] };
    Report("hex dispatch(two edges)", ChamferSolver::ChamferEdges(Prism.Payload, Two, d), V0);
    Report("hex too large set-back", ChamferSolver::ChamferEdge(Prism.Payload, Top[0], 2.5), V0);
    return 0;
}
