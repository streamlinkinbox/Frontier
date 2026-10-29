/*============================================================================================================================================
                                                    FACETRESERVOIRDESCRIPTOR.H
============================================================================================================================================*/
// 🧩 Unpacked facet barycentrics, revision, RGB suffix and historical resampling normalizer.
#pragma once
#ifdef __cplusplus
#include <array>
#include <cstdint>
#include <cstddef>
#define FACET_UINT std::uint32_t
#define FACET_RGB std::array<float, 3>
#define FACET_ALIGN alignas(16)
#else
#define FACET_UINT uint
#define FACET_RGB float3
#define FACET_ALIGN
#endif

struct FACET_ALIGN FacetReservoirDescriptor
{
    FACET_UINT FacetIndex;
    FACET_UINT Revision;
    float U, V;
    FACET_RGB Outgoing;
    float Weight;
};

#ifdef __cplusplus
static_assert(sizeof(FacetReservoirDescriptor) == 32 && alignof(FacetReservoirDescriptor) == 16);
static_assert(offsetof(FacetReservoirDescriptor, FacetIndex) == 0);
static_assert(offsetof(FacetReservoirDescriptor, Revision) == 4);
static_assert(offsetof(FacetReservoirDescriptor, U) == 8 && offsetof(FacetReservoirDescriptor, V) == 12);
static_assert(offsetof(FacetReservoirDescriptor, Outgoing) == 16);
static_assert(offsetof(FacetReservoirDescriptor, Weight) == 28);
#endif
#undef FACET_UINT
#undef FACET_RGB
#undef FACET_ALIGN
