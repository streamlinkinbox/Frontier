// Scratch probe: per-face tessellated areas and cap traces after an in-plane edge tweak of a hexagonal prism.
#include "Kernel/TweakSolver.h"
#include <cstdio>
#include <cmath>
using namespace Frontier;
static double FaceArea(const BrepBody& B, int F)
{
    BrepBody::FaceTriangles T = B.TessellateFace(F);
    double A = 0;
    for (size_t I = 0; I + 2 < T.Triangles.size(); I += 3)
        A += (T.Positions[T.Triangles[I + 1]] - T.Positions[T.Triangles[I]]).Cross(T.Positions[T.Triangles[I + 2]] - T.Positions[T.Triangles[I]]).Length() * 0.5;
    return A;
}
int main()
{
    const double s3 = std::sqrt(3.0);
    NurbsCurve Hex = NurbsCurve::Polyline({ { 2, 0, 0 }, { 1, s3, 0 }, { -1, s3, 0 }, { -2, 0, 0 }, { -1, -s3, 0 }, { 1, -s3, 0 } }, true).Payload;
    BrepBody P = BrepBody::Extrude(Hex, { 0, 0, 1 }, 2.0).Payload;
    int Side = -1;
    for (size_t E = 0; E < P.Edges.size(); ++E) { Vec3 A = P.Vertices[P.Edges[E].VertexStart].Point, Z = P.Vertices[P.Edges[E].VertexEnd].Point; if (std::fabs(A.X - 2) < 1e-9 && std::fabs(A.Y) < 1e-9 && std::fabs(Z.X - 2) < 1e-9 && std::fabs(Z.Y) < 1e-9) Side = (int)E; }
    Deliver<BrepBody> R = TweakSolver::TranslateEdge(P, Side, { 0.6, 0, 0 }, false);
    std::printf("edge %d ok=%d volume before %.6f after %.6f (expected %.6f)\n", Side, (bool)R, P.Validate().Volume, R ? R.Payload.Validate().Volume : 0.0, (6 * s3 + 0.6 * s3) * 2);
    for (size_t F = 0; F < P.Faces.size(); ++F)
    {
        const BrepFace& Fa = R.Payload.Faces[F];
        std::printf("f%zu %-9s %s natural=%d  area before %.6f after %.6f", F, Describe(Fa.Surface.Classification), Fa.Natural ? "nat" : "trim", Fa.Natural, FaceArea(P, (int)F), FaceArea(R.Payload, (int)F));
        if (!Fa.Natural)
        {
            std::printf("  knotsU %.3f..%.3f  poles", Fa.Surface.DomainStartU(), Fa.Surface.DomainEndU());
            for (int I = 0; I < 2; ++I) for (int J = 0; J < 2; ++J) { Vec3 Q = Fa.Surface.Pole(I, J).Divide(); std::printf(" (%.2f,%.2f,%.2f)", Q.X, Q.Y, Q.Z); }
            std::printf("\n   traces:");
            for (int L : Fa.Loops) for (int C : R.Payload.Loops[L].Coedges)
            {
                const BrepCoedge& Co = R.Payload.Coedges[C];
                std::printf(" [e%d %zu pts", Co.Edge, Co.Trace.size());
                for (const Vec2& T : Co.Trace) std::printf(" (%.3f,%.3f)", T.X, T.Y);
                Vec3 S = R.Payload.CoedgeStart(C); std::printf(" start3d (%.2f,%.2f)]", S.X, S.Y);
            }
        }
        std::printf("\n");
    }
}
