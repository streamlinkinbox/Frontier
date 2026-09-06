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
// 0:    attempt to silence tiny_bvh.h's informational C0000 ("AVX2 and FMA not enabled" — expected: this
//        build targets /arch:AVX for Sandy Bridge hosts, so tinybvh knowingly uses its SSE/scalar fallback).
//        Harmless no-op if the message bypasses the warning system.
#pragma warning(disable : 0 4005 4244 4267 4310 4324 4456 4457 4458 4459 4701 4702 4996)
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
    //    This function is a REFERENCE path used by proofs, never by a frame, so correctness beats speed and the
    //    binary tree is the honest oracle. bvh8.bvh is the same tree the CWBVH was collapsed from, so a hit here
    //    is a hit the GPU will also find, and refitting updates it in place.
    Impl->Tree.bvh8.bvh.Intersect(Ray);
    if (Ray.hit.t >= 1e30f) return false;
    OutDistance  = Ray.hit.t;
    OutPrimitive = Ray.hit.prim;
    return true;
}

} // namespace Frontier
