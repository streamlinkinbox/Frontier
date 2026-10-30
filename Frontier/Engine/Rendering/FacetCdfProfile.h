/*============================================================================================================================================
                                                          FACETCDFPROFILE.H
============================================================================================================================================*/
// 🧩 Encode the sixteen-byte source-profile header shared by maintained facet CDF shaders.

#pragma once
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     PROFILE HEADER
//------------------------------------------------------------------------------------------------------------------------

// 📝 Upload these uint32 bit patterns,not numeric float conversions,to Receivers[0].PositionHit.
//    Source quartets begin after the header and camera rows. This does not validate indices,geometry or areas;
//    the GPU preparation owns those checks. Neither source count nor geometry is bounded by camera count.
struct FacetCdfProfileHeader
{
    std::uint32_t SourceCount, CameraCount, SourceStart, QuartetCapacity;
};
static_assert(sizeof(FacetCdfProfileHeader) == 16 && alignof(FacetCdfProfileHeader) == 4);
static_assert(offsetof(FacetCdfProfileHeader, SourceCount) == 0 &&
              offsetof(FacetCdfProfileHeader, CameraCount) == 4 &&
              offsetof(FacetCdfProfileHeader, SourceStart) == 8 &&
              offsetof(FacetCdfProfileHeader, QuartetCapacity) == 12);

inline FacetCdfProfileHeader ConstructFacetCdfHeader(std::uint32_t Sources, std::uint32_t Cameras)
{
    const std::uint64_t Capacity = (std::uint64_t(Sources) + 3) / 4;
    const std::uint64_t Start    = std::uint64_t(Cameras) + 1;
    if (Sources == 0 || Sources > 16777216u || Start + Capacity > UINT32_MAX)
        throw std::invalid_argument("Invalid or overflowing facet CDF profile header");
    return {Sources, Cameras, static_cast<std::uint32_t>(Start), static_cast<std::uint32_t>(Capacity)};
}
} // namespace Frontier
