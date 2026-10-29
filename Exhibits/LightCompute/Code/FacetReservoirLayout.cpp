/*============================================================================================================================================
                                                     FACETRESERVOIRLAYOUT.CPP
============================================================================================================================================*/
// 🧩 Measure unpacked RGB descriptor packets against expanded geometry and equal-format raw candidates.
#include "FacetReservoirDescriptor.h"
#include <cstdio>
#include <type_traits>

struct alignas(16) ExpandedFacetSample
{
    std::array<float,3> Position, Normal, Outgoing;
    float Weight;
    std::uint32_t FacetIndex, Revision;
};
struct alignas(16) FacetQuartet
{
    std::array<FacetReservoirDescriptor,4> Selected;
    std::array<std::uint32_t,4> Revision;
};
struct alignas(16) ExpandedQuartet
{
    std::array<ExpandedFacetSample,4> Selected;
    std::array<std::uint32_t,4> Revision;
};
struct alignas(16) RawFacetOctet
{
    std::array<FacetReservoirDescriptor,8> Candidates;
    std::array<std::uint32_t,4> Revision;
};
static_assert(std::is_standard_layout_v<FacetReservoirDescriptor>);
static_assert(std::is_trivially_copyable_v<FacetReservoirDescriptor>);
static_assert(sizeof(ExpandedFacetSample)==48 && sizeof(FacetQuartet)==144);
static_assert(sizeof(ExpandedQuartet)==208 && sizeof(RawFacetOctet)==272);
static_assert(offsetof(FacetQuartet, Revision)==128);

int main()
{
    std::printf("FACET_LAYOUT descriptor=%zu expanded_sample=%zu quartet=%zu expanded_quartet=%zu raw_octet=%zu revision_offset=%zu\n",
                sizeof(FacetReservoirDescriptor),sizeof(ExpandedFacetSample),sizeof(FacetQuartet),
                sizeof(ExpandedQuartet),sizeof(RawFacetOctet),offsetof(FacetQuartet,Revision));
}
