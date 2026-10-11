//============================================================================================================================================
//                                                        TRAVERSALINDEX.CPP
//============================================================================================================================================
// 🧩 tinybvh CWBVH builder. This is the only translation unit that defines TINYBVH_IMPLEMENTATION.

#include "TraversalIndex.h"
#include "../DeviceExchange/SwapchainExchange.h"   // TriangleIndex

#include <chrono>
#include <cstring>

#if defined(_MSC_VER)
#pragma warning(push)
// 4005: tiny_bvh.h unconditionally re-#defines WIN32_LEAN_AND_MEAN (already /D-defined by the toolchain).
// tiny_bvh.h's informational C0000 ("AVX not enabled" — expected: baseline ISA builds use its SSE/scalar
//    fallback) bypasses the warning system entirely, so it cannot be disabled here — and listing `0` in the
//    disable list is itself invalid (it produced C4616 on every build). It is a message, not a defect.
#pragma warning(disable : 4005 4244 4267 4310 4324 4456 4457 4458 4459 4701 4702 4996)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define NO_DOUBLE_PRECISION_SUPPORT
#define TINYBVH_IMPLEMENTATION
#include <tiny_bvh.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace Frontier {

struct TraversalIndex::Implementation
{
    std::vector<tinybvh::bvhvec4> Vertices;   // 3 per triangle, w unused (tinybvh reads xyz)
    tinybvh::BVH8_CWBVH           Tree;
};

TraversalIndex::TraversalIndex() noexcept : Impl(std::make_unique<Implementation>()) {}
TraversalIndex::~TraversalIndex() = default;

bool TraversalIndex::Build(const std::vector<TriangleIndex>& Triangles, bool HighQuality) noexcept
{
    NodeBlob.clear(); LeafBlob.clear(); Metrics = {};
    if (Triangles.empty()) return false;

    const auto Start = std::chrono::steady_clock::now();

    Impl->Vertices.resize(Triangles.size() * 3u);
    for (size_t I = 0; I < Triangles.size(); ++I)
    {
        const TriangleIndex& T = Triangles[I];
        Impl->Vertices[I * 3u + 0u] = tinybvh::bvhvec4(T.VertexAlphaX, T.VertexAlphaY, T.VertexAlphaZ, 0.0f);
        Impl->Vertices[I * 3u + 1u] = tinybvh::bvhvec4(T.VertexBetaX,  T.VertexBetaY,  T.VertexBetaZ,  0.0f);
        Impl->Vertices[I * 3u + 2u] = tinybvh::bvhvec4(T.VertexGammaX, T.VertexGammaY, T.VertexGammaZ, 0.0f);
    }

    const uint32_t PrimitiveCount = static_cast<uint32_t>(Triangles.size());
    // Flat vertex list → primitive index == flat triangle index (what the kernel needs). BVH8_CWBVH::Build runs the
    // binary binned-SAH build (BuildHQ: SBVH spatial splits), collapses to an 8-wide MBVH and compresses.
    if (HighQuality) Impl->Tree.BuildHQ(Impl->Vertices.data(), PrimitiveCount);
    else             Impl->Tree.Build  (Impl->Vertices.data(), PrimitiveCount);

    const tinybvh::BVH8_CWBVH& Tree = Impl->Tree;
    const size_t NodeFloats = static_cast<size_t>(Tree.usedBlocks) * 4u;            // blocks of float4
    const size_t LeafFloats = static_cast<size_t>(Tree.bvh8.idxCount) * 3u * 4u;    // 3 float4 per referenced triangle
    NodeBlob.resize(NodeFloats);
    LeafBlob.resize(LeafFloats);
    std::memcpy(NodeBlob.data(), Tree.bvh8Data, NodeFloats * sizeof(float));
    std::memcpy(LeafBlob.data(), Tree.bvh8Tris, LeafFloats * sizeof(float));

    const auto End = std::chrono::steady_clock::now();
    Metrics.TriangleCount     = PrimitiveCount;
    Metrics.NodeCount         = Tree.usedBlocks / 5u;
    Metrics.NodeByteCount     = static_cast<uint32_t>(NodeFloats * sizeof(float));
    Metrics.LeafByteCount     = static_cast<uint32_t>(LeafFloats * sizeof(float));
    Metrics.SahCost           = Impl->Tree.bvh8.bvh.SAHCost();
    Metrics.BuildMilliseconds = std::chrono::duration<float, std::milli>(End - Start).count();
    Metrics.HighQuality       = HighQuality;
    return true;
}

bool TraversalIndex::RefitBottomLevel(const std::vector<TriangleIndex>& Triangles) noexcept
{
    RefitMilliseconds = 0.0f;
    if (!IsRefittable()) return false;

    // The triangle COUNT must be unchanged — refit preserves topology, so the tree still refers to the same
    //    primitive slots. A different count means the scene changed shape and needs a rebuild, not a refit.
    const uint32_t PrimitiveCount = static_cast<uint32_t>(Triangles.size());
    if (PrimitiveCount != Metrics.TriangleCount) return false;
    if (Impl->Vertices.size() != static_cast<size_t>(PrimitiveCount) * 3u) return false;

    const auto Start = std::chrono::steady_clock::now();

    // Rewrite the vertex positions in place. tinybvh refits against the same array it was built from, so this
    //    must be the very buffer handed to Build — hence updating Impl->Vertices rather than a copy.
    for (size_t I = 0; I < Triangles.size(); ++I)
    {
        const TriangleIndex& T = Triangles[I];
        Impl->Vertices[I * 3u + 0u] = tinybvh::bvhvec4(T.VertexAlphaX, T.VertexAlphaY, T.VertexAlphaZ, 0.0f);
        Impl->Vertices[I * 3u + 1u] = tinybvh::bvhvec4(T.VertexBetaX,  T.VertexBetaY,  T.VertexBetaZ,  0.0f);
        Impl->Vertices[I * 3u + 2u] = tinybvh::bvhvec4(T.VertexGammaX, T.VertexGammaY, T.VertexGammaZ, 0.0f);
    }

    // Refit the binary tree, then re-collapse and re-compress so the GPU blobs match. The last step dominates
    //    (see the header note); it is O(total nodes) because CWBVH is a packed format with no partial update.
    Impl->Tree.bvh8.bvh.Refit();
    Impl->Tree.bvh8.ConvertFrom(Impl->Tree.bvh8.bvh, true);
    Impl->Tree.ConvertFrom(Impl->Tree.bvh8, true);

    const tinybvh::BVH8_CWBVH& Tree = Impl->Tree;
    const size_t NodeFloats = static_cast<size_t>(Tree.usedBlocks) * 4u;
    const size_t LeafFloats = static_cast<size_t>(Tree.bvh8.idxCount) * 3u * 4u;
    NodeBlob.resize(NodeFloats);
    LeafBlob.resize(LeafFloats);
    std::memcpy(NodeBlob.data(), Tree.bvh8Data, NodeFloats * sizeof(float));
    std::memcpy(LeafBlob.data(), Tree.bvh8Tris, LeafFloats * sizeof(float));

    const auto End = std::chrono::steady_clock::now();
    RefitMilliseconds  = std::chrono::duration<float, std::milli>(End - Start).count();
    Metrics.NodeCount  = Tree.usedBlocks / 5u;
    Metrics.NodeByteCount = static_cast<uint32_t>(NodeFloats * sizeof(float));
    Metrics.LeafByteCount = static_cast<uint32_t>(LeafFloats * sizeof(float));
    return true;
}

bool TraversalIndex::RefitTriangleTree(const std::vector<TriangleIndex>& Triangles) noexcept
{
    RefitMilliseconds = 0.0f;
    if (!IsRefittable()) return false;

    const uint32_t PrimitiveCount = static_cast<uint32_t>(Triangles.size());
    if (PrimitiveCount != Metrics.TriangleCount) return false;
    if (Impl->Vertices.size() != static_cast<size_t>(PrimitiveCount) * 3u) return false;

    const auto Start = std::chrono::steady_clock::now();
    for (size_t I = 0; I < Triangles.size(); ++I)
    {
        const TriangleIndex& T = Triangles[I];
        Impl->Vertices[I * 3u + 0u] = tinybvh::bvhvec4(T.VertexAlphaX, T.VertexAlphaY, T.VertexAlphaZ, 0.0f);
        Impl->Vertices[I * 3u + 1u] = tinybvh::bvhvec4(T.VertexBetaX,  T.VertexBetaY,  T.VertexBetaZ,  0.0f);
        Impl->Vertices[I * 3u + 2u] = tinybvh::bvhvec4(T.VertexGammaX, T.VertexGammaY, T.VertexGammaZ, 0.0f);
    }
    Impl->Tree.bvh8.bvh.Refit();
    RefitMilliseconds = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - Start).count();

    // The packed blobs are now STALE BY CONSTRUCTION (the geometry moved under them). Say so in the metrics
    //    rather than leaving a reader to infer it from a timestamp.
    Metrics.NodeByteCount = static_cast<uint32_t>(NodeBlob.size() * sizeof(float));
    return true;
}

bool TraversalIndex::TraceClosest(const float Origin[3], const float Direction[3], float& OutDistance, uint32_t& OutPrimitive) const noexcept
{
    if (!IsReady()) return false;
    tinybvh::Ray Ray(tinybvh::bvhvec3(Origin[0], Origin[1], Origin[2]), tinybvh::bvhvec3(Direction[0], Direction[1], Direction[2]));

    // ⚠️ Traverse the inner BINARY tree, not BVH8_CWBVH::Intersect.
    //
    //    tinybvh's CWBVH CPU traversal both hard-requires AVX and, even where AVX is present, was measured
    //    returning misses for rays the plain BVH hits — a trivial floor quad struck head-on came back empty
    //    (Scratchpad/TraversalRefitTest.cpp exists partly because of that discovery). The GPU blobs it emits are
    //    correct; it is the host-side walker that is unreliable.
    //
    //    ✅ That symptom now has a cause, found while writing D8's independent walker (Docs/DynamicGeometry.md §6):
    //    "struck head-on" is the whole story. A ray with an exactly zero direction component has rD = +inf on that
    //    axis, so every quantised slab value along it is 0 · inf = NaN. SPIR-V's FMax/FMin — what the kernel's max()
    //    and min() lower to — return the operand that is NOT NaN and walk straight through it, but the x86
    //    MAXPS/MINPS that the host path uses return their SECOND operand whenever either input is NaN, so a NaN
    //    decides the comparison and the node is pruned. The blobs are fine and the kernel is fine; a host walker is
    //    only right if it uses FMax/FMin semantics (see Exhibits/Workbench/Traversal/TwoLevelBvhProof.cpp, whose
    //    §⑧ walker carries a gate for exactly these axis-aligned rays).
    //
    //    This function is a REFERENCE path used by proofs, never by a frame, so correctness beats speed and the
    //    binary tree is the honest oracle. bvh8.bvh is the same tree the CWBVH was collapsed from, so a hit here
    //    is a hit the GPU will also find, and refitting updates it in place.
    Impl->Tree.bvh8.bvh.Intersect(Ray);
    if (Ray.hit.t >= 1e30f) return false;
    OutDistance  = Ray.hit.t;
    OutPrimitive = Ray.hit.prim;
    return true;
}

bool TraversalIndex::TraceClosestObjectSpace(const float Origin[3], const float Direction[3], float MaxDistance,
                                             float& OutDistance, uint32_t& OutPrimitive) const noexcept
{
    if (!IsReady()) return false;

    // Filled by hand rather than through tinybvh::Ray's constructor: that constructor writes
    //    `D = tinybvh_normalize( direction )`, and for a two-level trace the direction is already the object-space one
    //    the transform produced — normalising it again would both rescale t and perturb a grazing ray by an ulp.
    //    Field for field the same setup otherwise (zero-initialised Ray, O, D, rD = 1/D, hit.t = the upper bound,
    //    full mask).
    tinybvh::Ray Ray{};
    Ray.O = tinybvh::bvhvec3(Origin[0], Origin[1], Origin[2]);
    Ray.D = tinybvh::bvhvec3(Direction[0], Direction[1], Direction[2]);
    // ⚠️ tinybvh_rcp, NOT 1/x: tinybvh's helper clamps near-zero components to ±FLT_MAX, where 1/0 would give ±inf
    //    and `0 * inf` (a slab test on an exactly axis-aligned ray) would give NaN and silently drop subtrees.
    Ray.rD = tinybvh::tinybvh_rcp(Ray.D);
    Ray.hit.t = MaxDistance;
    Ray.mask = 0xFFFFu;

    Impl->Tree.bvh8.bvh.Intersect(Ray);
    // The walker leaves hit.t at its initial value when it finds nothing, so the bound IS the miss sentinel here.
    if (!(Ray.hit.t < MaxDistance)) return false;
    OutDistance  = Ray.hit.t;
    OutPrimitive = Ray.hit.prim;
    return true;
}

} // namespace Frontier
