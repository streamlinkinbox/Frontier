#pragma once
/*==========================================================================================================================================
                                                             FACETARCHIVE.H
==========================================================================================================================================*/
// 🧩 Reads the 'FCT1' facet streams the vehicle was authored into (the same archives the HTML prototype's
//    renderer consumes): positions, facet corners, material families, optional corner normals. This is how
//    the same ControlVehicle exists in C++ — one source of geometry, two renderers.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

struct FacetArchiveData
{
    uint32_t VertexCount = 0;
    uint32_t FacetCount = 0;
    std::vector<float> Positions;       // [VertexCount * 3]
    std::vector<uint32_t> Facets;       // [FacetCount * 3] vertex indices
    std::vector<uint32_t> Families;     // [FacetCount] material family per facet
    std::vector<float> CornerNormals;   // [FacetCount * 9] when authored, empty otherwise
    std::array<float, 3> BoundsMinimum = { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> BoundsMaximum = { 0.0f, 0.0f, 0.0f };

    [[nodiscard]] bool IsValid() const noexcept { return VertexCount > 0 && FacetCount > 0; }
};

// 📝 Decodes one stream from memory. Returns empty data (IsValid() == false) on any disagreement with the
//    header — a corrupt archive is a missing car, never a crash.
[[nodiscard]] FacetArchiveData DecodeFacetArchive(const uint8_t* Bytes, size_t ByteCount);

// 📝 Loads and decodes a stream from disk.
[[nodiscard]] FacetArchiveData LoadFacetArchive(const std::string& Path);

} // namespace Frontier
