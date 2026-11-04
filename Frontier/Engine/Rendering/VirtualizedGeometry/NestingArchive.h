/*==========================================================================================================================================
                                                           NESTINGARCHIVE.H
==========================================================================================================================================*/
// 🧩 The cluster nestings of a scene, built once at import and stored beside its .facetspace (<stem>.nestingspace), so the editor
//    loads them instead of building (refinement I2).

// 📝 Each entry names the facet set it was built from by index, facet count and a 64-bit FNV-1a digest of the facets, and the
//    archive records the refinement profile it was built with; an entry that disagrees with the scene or profile is stale.
//    Layout (little-endian): 'FNA1', u32 version, u32 profile, u32 entry count, then per entry u32 facet set, u32 facet count,
//    u64 digest, u64 payload bytes and the nesting (summary, clusters, tiers), every scalar as stored in GeometryNesting.
#pragma once
#include "NestingConstruction.h"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
namespace Frontier::VirtualizedGeometry
{
struct NestingArchiveEntry
{
    uint32_t        FacetSet   = 0;
    uint32_t        FacetCount = 0;
    uint64_t        Digest     = 0;   // [-] DigestSurfaceSoup of the set's facets
    GeometryNesting Nesting{};
};
struct NestingArchive
{
    uint32_t                         Profile = 0;   // [-] NestingProfileBits of the options the entries were built with
    std::vector<NestingArchiveEntry> Entries{};
};
[[nodiscard]] uint32_t NestingProfileBits(const NestingOptions& Options) noexcept;
[[nodiscard]] uint64_t DigestSurfaceSoup(const SurfaceSoup& Soup) noexcept;
[[nodiscard]] std::vector<uint8_t> EncodeNestingArchive(const NestingArchive& Archive);
[[nodiscard]] std::optional<NestingArchive> DecodeNestingArchive(const std::vector<uint8_t>& Bytes, std::string* Diagnostic = nullptr);
bool WriteNestingArchive(const std::filesystem::path& Path, const NestingArchive& Archive, std::string* Diagnostic = nullptr);
[[nodiscard]] std::optional<NestingArchive> ReadNestingArchive(const std::filesystem::path& Path, std::string* Diagnostic = nullptr);
} // namespace Frontier::VirtualizedGeometry
