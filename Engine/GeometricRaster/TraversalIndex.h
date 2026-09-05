//============================================================================================================================================
//                                                        TRAVERSALINDEX.H
//============================================================================================================================================
// 🧩 R3 — Tier A software acceleration structure: tinybvh binned-SAH build → 8-wide compressed BVH (CWBVH, Ylitie 2017)
//    over the resident scene's world-space triangles. The GPU kernel traverses the two blobs this produces
//    (Shaders/TraversalCWBVH.slang, a 1:1 port of tinybvh kernels/traverse_cwbvh.cl).
//
//    Blob layout (both arrays of float4, 16 B "blocks"):
//        Nodes      80 B per node = 5 blocks: [p.xyz | e.xyz,imask] [childBase | triBase | meta 0-3 | meta 4-7]
//                   [qlo.x 0-7] [qlo.y 0-3, qhi.x] ... (Ylitie §4.2, exactly as tinybvh emits them)
//        Triangles  48 B per triangle = 3 blocks: e1 = v2 − v0, e2 = v1 − v0, v0 (w = primitive index bits)
//
//    Primitive index = flat triangle index into SceneStructure::QueryFlatTriangles() (which the kernel already
//    binds as `Triangles[]` for material / normal lookup), so a hit resolves with one extra fetch.
//
//    Coordinates: world space, RH +Z up, metres (CLAUDE.md §7) — tinybvh is axis-agnostic; nothing is swapped.
//    SIMD: the build uses whatever tinybvh detects at compile time (AVX2 on the toolchain's /arch:AVX2; scalar
//    fallback otherwise). Build is CPU-side, once per scene load (R8 moves dynamic geometry to GPU H-PLOC).

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace Frontier {

struct TriangleIndex;   // DeviceExchange/SwapchainExchange.h — flat world-space triangle (48 B)

struct TraversalMetrics
{
    uint32_t TriangleCount   = 0u;      // [cnt] input triangles
    uint32_t NodeCount       = 0u;      // [cnt] CWBVH nodes (80 B each)
    uint32_t NodeByteCount   = 0u;      // [B]
    uint32_t LeafByteCount   = 0u;      // [B]  triangle blob
    float    SahCost         = 0.0f;    // [-]  tinybvh SAH cost of the binary tree before collapse
    float    BuildMilliseconds = 0.0f;  // [ms] wall clock, binary build + collapse + compress
    bool     HighQuality     = false;   // [-]  spatial-split (SBVH) build used
};

class TraversalIndex
{
public:
    TraversalIndex() noexcept;
    ~TraversalIndex();
    TraversalIndex(const TraversalIndex&) = delete;
    TraversalIndex& operator=(const TraversalIndex&) = delete;

    // Builds from the flat world-space triangles. HighQuality → BuildHQ (spatial splits; ~5× slower, ~10-20 %
    // faster traversal on Sponza-class scenes). Returns false on empty input.
    bool Build(const std::vector<TriangleIndex>& Triangles, bool HighQuality) noexcept;

    // D1 — bottom-level entry point. Identical work to Build(); the distinct name records the INTENT that these
    //    triangles are one instance's geometry rather than the whole world, so a reader (and the identity gate)
    //    can tell the two apart before the transform plumbing exists.
    //
    //    ⚠️ This must never become a separate implementation. It forwards to Build so the two paths cannot drift:
    //    Scratchpad/TraversalIdentityTest.cpp hashes both blobs and requires bit identity, because a perturbed
    //    tree would silently decorrelate ReSTIR's temporal reuse rather than fail loudly.
    //
    //    Triangles are object-space once instances carry transforms. For a single identity-transformed instance —
    //    every scene the renderer has today — object space IS world space, which is what makes D1 a no-op.
    bool BuildBottomLevel(const std::vector<TriangleIndex>& Triangles, bool HighQuality) noexcept
    {
        return Build(Triangles, HighQuality);
    }

    [[nodiscard]] bool                      IsReady()        const noexcept { return !NodeBlob.empty(); }
    [[nodiscard]] const std::vector<float>& QueryNodeBlob()  const noexcept { return NodeBlob; }   // float4 × 5 per node
    [[nodiscard]] const std::vector<float>& QueryLeafBlob()  const noexcept { return LeafBlob; }   // float4 × 3 per triangle
    [[nodiscard]] const TraversalMetrics&   QueryMetrics()   const noexcept { return Metrics; }

    // Reference CPU trace (tinybvh's own CWBVH traversal) — used by the self-test / proofs, never per frame.
    [[nodiscard]] bool TraceClosest(const float Origin[3], const float Direction[3], float& OutDistance, uint32_t& OutPrimitive) const noexcept;

private:
    struct Implementation;
    std::unique_ptr<Implementation> Impl;
    std::vector<float>   NodeBlob;
    std::vector<float>   LeafBlob;
    TraversalMetrics     Metrics;
};

} // namespace Frontier
