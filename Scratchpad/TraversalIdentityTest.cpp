//============================================================================================================================================
//                                                    TRAVERSALIDENTITYTEST.CPP
//============================================================================================================================================
// 🧩 The D1 regression gate. Proves that introducing the BLAS/TLAS split changes NOTHING for a scene that has a
//    single, identity-transformed instance — which is every scene the renderer has today.
//
//    The check is deliberately brutal: it hashes the CWBVH node and leaf blobs byte-for-byte and compares them
//    against the single-BLAS path. A BVH is a pile of floats whose exact values depend on build order, split
//    choices and collapse; if the split perturbed any of that, a "looks the same" image test could still pass
//    while temporal reuse quietly decorrelates. Bit identity is the only honest gate.
//
//    It also traces a fixed fan of rays through both paths and requires identical (distance, primitive) results,
//    so traversal is checked as well as construction.
//
//    Build (from the repository root):
//        g++ -std=c++20 -O2 -I Engine -I ExternalPackages/tinybvh \
//            Scratchpad/TraversalIdentityTest.cpp Engine/GeometricRaster/TraversalIndex.cpp \
//            -o /tmp/TraversalIdentityTest && /tmp/TraversalIdentityTest

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
    std::printf("  %-62s %s\n", Name, Condition ? "PASS" : "FAIL");
    if (!Condition) ++Failures;
}

// FNV-1a over the raw float bytes. Any change in build order, split choice or collapse moves this.
uint64_t HashFloats(const std::vector<float>& Values)
{
    uint64_t Hash = 1469598103934665603ull;
    const auto* Bytes = reinterpret_cast<const unsigned char*>(Values.data());
    const size_t Count = Values.size() * sizeof(float);
    for (size_t I = 0; I < Count; ++I) { Hash ^= Bytes[I]; Hash *= 1099511628211ull; }
    return Hash;
}

// A Cornell-box-shaped stand-in: an axis-aligned room plus a couple of boxes. Deterministic, no file I/O, so the
//    gate runs anywhere — the point is blob identity across code paths, not the specific geometry.
void AppendQuad(std::vector<Frontier::TriangleIndex>& Out,
                float ax, float ay, float az, float bx, float by, float bz,
                float cx, float cy, float cz, float dx, float dy, float dz)
{
    Frontier::TriangleIndex T{};
    T.VertexAlphaX = ax; T.VertexAlphaY = ay; T.VertexAlphaZ = az;
    T.VertexBetaX  = bx; T.VertexBetaY  = by; T.VertexBetaZ  = bz;
    T.VertexGammaX = cx; T.VertexGammaY = cy; T.VertexGammaZ = cz;
    Out.push_back(T);
    T.VertexAlphaX = ax; T.VertexAlphaY = ay; T.VertexAlphaZ = az;
    T.VertexBetaX  = cx; T.VertexBetaY  = cy; T.VertexBetaZ  = cz;
    T.VertexGammaX = dx; T.VertexGammaY = dy; T.VertexGammaZ = dz;
    Out.push_back(T);
}

std::vector<Frontier::TriangleIndex> ConstructReferenceRoom()
{
    std::vector<Frontier::TriangleIndex> Facets;
    AppendQuad(Facets, -2,-2,0,  2,-2,0,  2,3,0, -2,3,0);       // floor
    AppendQuad(Facets, -2,-2,3,  2,-2,3,  2,3,3, -2,3,3);       // ceiling
    AppendQuad(Facets, -2,-2,0, -2,3,0,  -2,3,3, -2,-2,3);      // left wall
    AppendQuad(Facets,  2,-2,0,  2,3,0,   2,3,3,  2,-2,3);      // right wall
    AppendQuad(Facets, -2,3,0,   2,3,0,   2,3,3, -2,3,3);       // back wall

    // Two blocks, so the tree has interior structure rather than one flat span.
    std::mt19937 Generator(20260905u);
    std::uniform_real_distribution<float> Jitter(-0.35f, 0.35f);
    for (int Block = 0; Block < 2; ++Block)
    {
        const float Cx = Block == 0 ? -0.7f : 0.8f;
        const float Cy = Block == 0 ?  0.9f : 1.6f;
        for (int Face = 0; Face < 40; ++Face)
        {
            Frontier::TriangleIndex T{};
            T.VertexAlphaX = Cx + Jitter(Generator); T.VertexAlphaY = Cy + Jitter(Generator); T.VertexAlphaZ = 0.4f + Jitter(Generator);
            T.VertexBetaX  = Cx + Jitter(Generator); T.VertexBetaY  = Cy + Jitter(Generator); T.VertexBetaZ  = 0.4f + Jitter(Generator);
            T.VertexGammaX = Cx + Jitter(Generator); T.VertexGammaY = Cy + Jitter(Generator); T.VertexGammaZ = 0.4f + Jitter(Generator);
            Facets.push_back(T);
        }
    }
    return Facets;
}

} // namespace

int main()
{
    std::printf("\n=== D1 traversal identity gate ===\n\n");

    const std::vector<Frontier::TriangleIndex> Facets = ConstructReferenceRoom();
    std::printf("  reference room: %zu triangles\n\n", Facets.size());

    //──────────────────────────────────────────────────────────────────────────
    // ① The single-BLAS path — what the renderer does today.
    //──────────────────────────────────────────────────────────────────────────
    Frontier::TraversalIndex Reference;
    CheckTrue("reference build succeeds", Reference.Build(Facets, false));

    const uint64_t ReferenceNodeHash = HashFloats(Reference.QueryNodeBlob());
    const uint64_t ReferenceLeafHash = HashFloats(Reference.QueryLeafBlob());
    const Frontier::TraversalMetrics ReferenceMetrics = Reference.QueryMetrics();

    std::printf("  reference: %u nodes, %u node B, %u leaf B, SAH %.4f\n",
                ReferenceMetrics.NodeCount, ReferenceMetrics.NodeByteCount,
                ReferenceMetrics.LeafByteCount, static_cast<double>(ReferenceMetrics.SahCost));
    std::printf("  reference hashes: node %016llx  leaf %016llx\n\n",
                static_cast<unsigned long long>(ReferenceNodeHash),
                static_cast<unsigned long long>(ReferenceLeafHash));

    //──────────────────────────────────────────────────────────────────────────
    // ② The same geometry through the BLAS entry point, one identity instance.
    //──────────────────────────────────────────────────────────────────────────
    // BuildBottomLevel must be exactly Build for a single instance: same builder, same input, same blobs. If these
    //    hashes ever diverge, the split has perturbed the acceleration structure and every downstream proof
    //    (Cornell bit identity, ReSTIR temporal reuse) is invalidated.
    Frontier::TraversalIndex Split;
    CheckTrue("bottom-level build succeeds", Split.BuildBottomLevel(Facets, false));

    const uint64_t SplitNodeHash = HashFloats(Split.QueryNodeBlob());
    const uint64_t SplitLeafHash = HashFloats(Split.QueryLeafBlob());
    const Frontier::TraversalMetrics SplitMetrics = Split.QueryMetrics();

    std::printf("  split hashes:     node %016llx  leaf %016llx\n\n",
                static_cast<unsigned long long>(SplitNodeHash),
                static_cast<unsigned long long>(SplitLeafHash));

    CheckTrue("node blob is bit-identical",        SplitNodeHash == ReferenceNodeHash);
    CheckTrue("leaf blob is bit-identical",        SplitLeafHash == ReferenceLeafHash);
    CheckTrue("node blob length matches",          Split.QueryNodeBlob().size() == Reference.QueryNodeBlob().size());
    CheckTrue("leaf blob length matches",          Split.QueryLeafBlob().size() == Reference.QueryLeafBlob().size());
    CheckTrue("node count matches",                SplitMetrics.NodeCount     == ReferenceMetrics.NodeCount);
    CheckTrue("SAH cost matches exactly",          SplitMetrics.SahCost       == ReferenceMetrics.SahCost);
    CheckTrue("triangle count matches",            SplitMetrics.TriangleCount == ReferenceMetrics.TriangleCount);

    //──────────────────────────────────────────────────────────────────────────
    // ③ Traversal agreement — construction identity is not enough on its own.
    //──────────────────────────────────────────────────────────────────────────
    uint32_t Traced = 0u, Hits = 0u, Divergent = 0u;
    std::mt19937 Generator(12345u);
    std::uniform_real_distribution<float> Span(-1.0f, 1.0f);

    for (uint32_t Sample = 0u; Sample < 4096u; ++Sample)
    {
        const float Origin[3] = { 0.0f, -1.7f, 1.45f };
        float Direction[3] = { Span(Generator), std::fabs(Span(Generator)) + 0.15f, Span(Generator) };
        const float Length = std::sqrt(Direction[0]*Direction[0] + Direction[1]*Direction[1] + Direction[2]*Direction[2]);
        Direction[0] /= Length; Direction[1] /= Length; Direction[2] /= Length;

        float    DistanceA = 0.0f, DistanceB = 0.0f;
        uint32_t PrimitiveA = 0u,  PrimitiveB = 0u;
        const bool HitA = Reference.TraceClosest(Origin, Direction, DistanceA, PrimitiveA);
        const bool HitB = Split.TraceClosest    (Origin, Direction, DistanceB, PrimitiveB);

        ++Traced;
        if (HitA) ++Hits;
        if (HitA != HitB || (HitA && (DistanceA != DistanceB || PrimitiveA != PrimitiveB))) ++Divergent;
    }

    std::printf("\n  traced %u rays, %u hit the room\n", Traced, Hits);
    CheckTrue("rays actually hit geometry (the trace is meaningful)", Hits > Traced / 4u);
    CheckTrue("every ray agrees on distance and primitive",           Divergent == 0u);

    std::printf(Failures ? "\n>>> %d FAILURE(S)\n\n" : "\n>>> ALL PASS (0 failures)\n\n", Failures);
    return Failures ? 1 : 0;
}
