/*==========================================================================================================================================
                                                          BOUNDINGSTRUCTURE.H
==========================================================================================================================================*/
// 🧩 Binned-SAH bounding volume structure over one facet set, flattened for the distance transfer (BoundingStructure.js).

#pragma once

#include <cstdint>
#include <vector>

namespace Frontier::GlobalIllumination
{

// 📝 Entry layout, 32 bytes: Minimum.xyz, LeftOrFirst (u32), Maximum.xyz, Count (u32). Count > 0 marks a leaf whose
//    facets are [LeftOrFirst, LeftOrFirst + Count); an interior entry's down-links are LeftOrFirst and LeftOrFirst + 1.
struct BoundingStructureArchive
{
    std::vector<uint8_t> EntryBytes{};     // [B] 32 per entry
    uint32_t             EntryCount = 0;   // [-]
    std::vector<float>   FacetCorners{};   // [m] 12 floats per facet (three xyz_ corners), in leaf order
    uint32_t             FacetCount = 0;   // [-]
};

// Corners: 9 float32 per facet (the demo copies the facet corners into a Float32Array first).
[[nodiscard]] BoundingStructureArchive ConstructBoundingStructure(const std::vector<float>& Corners);

} // namespace Frontier::GlobalIllumination
