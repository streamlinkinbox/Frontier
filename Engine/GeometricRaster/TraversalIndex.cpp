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

bool TraversalIndex::TraceClosest(const float Origin[3], const float Direction[3], float& OutDistance, uint32_t& OutPrimitive) const noexcept
{
    if (!IsReady()) return false;
    tinybvh::Ray Ray(tinybvh::bvhvec3(Origin[0], Origin[1], Origin[2]), tinybvh::bvhvec3(Direction[0], Direction[1], Direction[2]));
    Impl->Tree.Intersect(Ray);
    if (Ray.hit.t >= 1e30f) return false;
    OutDistance  = Ray.hit.t;
    OutPrimitive = Ray.hit.prim;
    return true;
}

} // namespace Frontier
