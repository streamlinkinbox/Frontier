//============================================================================================================================================
//                                                     TRAVERSALREFITTEST.CPP
//============================================================================================================================================
// 🧩 The D5 gate. A refit must answer rays the same way a full rebuild would, or moving bodies will cast shadows
//    and reflections that disagree with where they are drawn.
//
//    The important check is agreement against a REBUILD of the moved geometry, not merely "the refit ran". A
//    refit keeps the tree topology, so a wrong implementation still returns plausible hits — just slightly wrong
//    ones, which in a path tracer looks like noise rather than a bug. Tracing thousands of rays through both
//    structures and demanding the same (distance, primitive) is the only way to tell them apart.
//
//    Also asserts the two refusals that matter: a spatial-split tree cannot be refitted (tinybvh aborts), and a
//    changed triangle count is a rebuild, not a refit.
//
//    Build: bash Scratchpad/CheckTraversalRefit.sh

#include "GeometricRaster/TraversalIndex.h"
#include "DeviceExchange/SwapchainExchange.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace {

int Failures = 0;

void CheckTrue(const char* Name, bool Condition)
{
    std::printf("  %-64s %s\n", Name, Condition ? "PASS" : "FAIL");
    if (!Condition) ++Failures;
}

// A static room plus a block of "body" triangles that will move. Laid out per-instance contiguously, exactly as
//    SceneStructure flattens: statics first, dynamics last.
constexpr uint32_t kStaticTriangles = 900u;
constexpr uint32_t kBodyTriangles   = 600u;

std::vector<Frontier::TriangleIndex> ConstructScene(float BodyDropZ, float BodySpin)
{
    std::vector<Frontier::TriangleIndex> Facets;
    Facets.reserve(kStaticTriangles + kBodyTriangles);

    std::mt19937 Generator(4242u);
    std::uniform_real_distribution<float> Span(-1.6f, 1.6f);
    std::uniform_real_distribution<float> Jitter(-0.06f, 0.06f);

    for (uint32_t I = 0u; I < kStaticTriangles; ++I)
    {
        const float Cx = Span(Generator), Cy = Span(Generator), Cz = std::fabs(Span(Generator));
        Frontier::TriangleIndex T{};
        T.VertexAlphaX = Cx + Jitter(Generator); T.VertexAlphaY = Cy + Jitter(Generator); T.VertexAlphaZ = Cz + Jitter(Generator);
        T.VertexBetaX  = Cx + Jitter(Generator); T.VertexBetaY  = Cy + Jitter(Generator); T.VertexBetaZ  = Cz + Jitter(Generator);
        T.VertexGammaX = Cx + Jitter(Generator); T.VertexGammaY = Cy + Jitter(Generator); T.VertexGammaZ = Cz + Jitter(Generator);
        Facets.push_back(T);
    }

    // Bodies: a fixed rest shape transformed by (spin about Z, drop in Z) — the rigid motion D4 produces.
    std::mt19937 BodyGenerator(777u);
    std::uniform_real_distribution<float> Local(-0.25f, 0.25f);
    const float C = std::cos(BodySpin), S = std::sin(BodySpin);
    const auto Place = [&](float X, float Y, float Z, float* OutX, float* OutY, float* OutZ)
    {
        *OutX = X * C - Y * S;
        *OutY = X * S + Y * C;
        *OutZ = Z + BodyDropZ;
    };
    for (uint32_t I = 0u; I < kBodyTriangles; ++I)
    {
        const float Cx = Local(BodyGenerator) * 4.0f, Cy = Local(BodyGenerator) * 4.0f, Cz = 1.2f + Local(BodyGenerator);
        Frontier::TriangleIndex T{};
        Place(Cx + Local(BodyGenerator), Cy + Local(BodyGenerator), Cz + Local(BodyGenerator), &T.VertexAlphaX, &T.VertexAlphaY, &T.VertexAlphaZ);
        Place(Cx + Local(BodyGenerator), Cy + Local(BodyGenerator), Cz + Local(BodyGenerator), &T.VertexBetaX,  &T.VertexBetaY,  &T.VertexBetaZ);
        Place(Cx + Local(BodyGenerator), Cy + Local(BodyGenerator), Cz + Local(BodyGenerator), &T.VertexGammaX, &T.VertexGammaY, &T.VertexGammaZ);
        Facets.push_back(T);
    }
    return Facets;
}

// Trace a deterministic fan through both structures and count disagreements.
uint32_t CompareTraversal(const Frontier::TraversalIndex& A, const Frontier::TraversalIndex& B,
                          uint32_t& OutTraced, uint32_t& OutHits)
{
    uint32_t Divergent = 0u;
    OutTraced = 0u; OutHits = 0u;
    std::mt19937 Generator(9182u);
    std::uniform_real_distribution<float> Span(-1.0f, 1.0f);

    for (uint32_t Sample = 0u; Sample < 6000u; ++Sample)
    {
        const float Origin[3] = { Span(Generator) * 0.4f, -3.0f, 1.2f + Span(Generator) * 0.4f };
        float Direction[3] = { Span(Generator) * 0.6f, 1.0f, Span(Generator) * 0.6f };
        const float Length = std::sqrt(Direction[0]*Direction[0] + Direction[1]*Direction[1] + Direction[2]*Direction[2]);
        Direction[0] /= Length; Direction[1] /= Length; Direction[2] /= Length;

        float    DistanceA = 0.0f, DistanceB = 0.0f;
        uint32_t PrimitiveA = 0u,  PrimitiveB = 0u;
        const bool HitA = A.TraceClosest(Origin, Direction, DistanceA, PrimitiveA);
        const bool HitB = B.TraceClosest(Origin, Direction, DistanceB, PrimitiveB);

        ++OutTraced;
        if (HitA) ++OutHits;
        // Distances may differ in the last bit between two structures over the same geometry; primitive identity
        //    must be exact, and the distance within a hair. A real error moves the hit, not the ulp.
        if (HitA != HitB || (HitA && (PrimitiveA != PrimitiveB || std::fabs(DistanceA - DistanceB) > 1e-4f)))
            ++Divergent;
    }
    return Divergent;
}

} // namespace

int main()
{
    std::printf("\n=== D5 traversal refit gate ===\n\n");

    const std::vector<Frontier::TriangleIndex> AtRest = ConstructScene(0.0f, 0.0f);
    std::printf("  scene: %zu triangles (%u static + %u body)\n\n",
                AtRest.size(), kStaticTriangles, kBodyTriangles);

    Frontier::TraversalIndex Refitted;
    CheckTrue("initial build succeeds",  Refitted.BuildBottomLevel(AtRest, false));
    CheckTrue("a binned-SAH tree is refittable", Refitted.IsRefittable());

    //──────────────────────────────────────────────────────────────────────────
    // ① Refitting unchanged geometry must not disturb anything.
    //──────────────────────────────────────────────────────────────────────────
    CheckTrue("refit of unchanged geometry succeeds", Refitted.RefitBottomLevel(AtRest));
    {
        Frontier::TraversalIndex Reference;
        (void)Reference.BuildBottomLevel(AtRest, false);
        uint32_t Traced = 0u, Hits = 0u;
        const uint32_t Divergent = CompareTraversal(Refitted, Reference, Traced, Hits);
        CheckTrue("rays agree with a rebuild after a no-op refit", Divergent == 0u);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ② The real case: bodies moved, refit must match a rebuild of the SAME geometry.
    //──────────────────────────────────────────────────────────────────────────
    struct Motion { const char* Name; float Drop; float Spin; };
    const Motion Steps[] = {
        { "small drop",        -0.05f, 0.05f },
        { "half a metre",      -0.50f, 0.60f },
        { "a full metre spun", -1.00f, 1.80f },
    };

    for (const Motion& Step : Steps)
    {
        const std::vector<Frontier::TriangleIndex> Moved = ConstructScene(Step.Drop, Step.Spin);

        Frontier::TraversalIndex Rebuilt;
        (void)Rebuilt.BuildBottomLevel(Moved, false);
        CheckTrue("refit accepts the moved geometry", Refitted.RefitBottomLevel(Moved));

        uint32_t Traced = 0u, Hits = 0u;
        const uint32_t Divergent = CompareTraversal(Refitted, Rebuilt, Traced, Hits);

        char Line[192];
        std::snprintf(Line, sizeof(Line), "%s: %u/%u rays hit, refit agrees with rebuild", Step.Name, Hits, Traced);
        CheckTrue(Line, Divergent == 0u && Hits > Traced / 10u);
        std::printf("        refit %.3f ms, %u nodes\n",
                    static_cast<double>(Refitted.QueryRefitMilliseconds()), Refitted.QueryMetrics().NodeCount);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ③ Refusals — a refit that silently did the wrong thing would be worse than one that declines.
    //──────────────────────────────────────────────────────────────────────────
    {
        std::vector<Frontier::TriangleIndex> Shorter = AtRest;
        Shorter.pop_back();
        CheckTrue("a changed triangle count is refused", !Refitted.RefitBottomLevel(Shorter));
    }
    {
        Frontier::TraversalIndex HighQuality;
        (void)HighQuality.BuildBottomLevel(AtRest, true);
        CheckTrue("a spatial-split tree reports itself unrefittable", !HighQuality.IsRefittable());
        CheckTrue("a spatial-split tree refuses to refit", !HighQuality.RefitBottomLevel(AtRest));
    }
    {
        Frontier::TraversalIndex Empty;
        CheckTrue("an unbuilt structure refuses to refit", !Empty.RefitBottomLevel(AtRest));
    }

    std::printf(Failures ? "\n>>> %d FAILURE(S)\n\n" : "\n>>> ALL PASS (0 failures)\n\n", Failures);
    return Failures ? 1 : 0;
}
