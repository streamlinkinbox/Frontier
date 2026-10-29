/*============================================================================================================================================
                                                    FACETRESERVOIRSTRIDE.SLANG
============================================================================================================================================*/
// 🧩 Expose typed descriptor SSBO member offsets and array stride for independent SPIR-V inspection.
#include "FacetReservoirDescriptor.h"
[[vk::binding(0, 0)]] RWStructuredBuffer<FacetReservoirDescriptor> Descriptors;
[shader("compute")]
[numthreads(1, 1, 1)]
void InspectDescriptorStride(uint3 Dispatch : SV_DispatchThreadID)
{
    FacetReservoirDescriptor D = Descriptors[Dispatch.x];
    Descriptors[Dispatch.x + 1] = D;
}
