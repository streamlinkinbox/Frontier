#pragma once

#include "GeometryStructure.h"
#include <array>
#include <bit>
#include <unordered_map>

namespace Frontier {
// Restore indexed adjacency in triangle-soup imports. Exact complete attributes only:
// never weld across UV, normal, tangent/sign seams or across mesh registrations.
// Padding is deliberately excluded. Existing vertex storage and triangle order stay intact.
inline std::vector<uint32_t> CanonicalVertexIndices(const std::vector<VertexRecord>& Vertices)
{
    using Key = std::array<uint32_t, 12>;
    struct Hash {
        size_t operator()(const Key& K) const noexcept {
            size_t H = 2166136261u;
            for (uint32_t W : K) H = (H ^ W) * 16777619u;
            return H;
        }
    };
    std::unordered_map<Key, uint32_t, Hash> Seen;
    Seen.reserve(Vertices.size());
    std::vector<uint32_t> Canonical;
    Canonical.reserve(Vertices.size());
    for (uint32_t I = 0; I < Vertices.size(); ++I) {
        const auto& V = Vertices[I];
        const std::array<float, 12> Values = {
            V.SpatialLocation.x, V.SpatialLocation.y, V.SpatialLocation.z,
            V.NormalDirection.x, V.NormalDirection.y, V.NormalDirection.z,
            V.TangentDirection.x, V.TangentDirection.y, V.TangentDirection.z, V.TangentDirection.w,
            V.TextureCoordinateU, V.TextureCoordinateV
        };
        Key K{};
        for (size_t J = 0; J < K.size(); ++J) K[J] = std::bit_cast<uint32_t>(Values[J]);
        Canonical.push_back(Seen.emplace(K, I).first->second);
    }
    return Canonical;
}
}
