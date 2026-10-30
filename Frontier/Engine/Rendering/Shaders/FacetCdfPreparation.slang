/*============================================================================================================================================
                                                         FACETCDFPREPARATION.SLANG
============================================================================================================================================*/
// 🧩 Prepare a retained facet area distribution in one invocation before dependent sampling.

#include "LightInfluence.slang"
#include "FacetCdfProfile.slang"

[shader("compute")][numthreads(1, 1, 1)] void PrepareFacetCdf(uint3 Dispatch : SV_DispatchThreadID)
{
    if (Dispatch.x != 0) return;
    uint Runs = Cells[2], Samples = Cells[9], Areas = Cells[10];
    for (uint Index = 0; Index < (Extent.Sampling.z + 4) * 4; ++Index) Cells[Index] = 0;
    Cells[1] = FacetCdfSourceCount();
    Cells[2] = Runs + 1;
    Cells[6] = Extent.Sampling.z;
    Cells[8] = 1;
    Cells[9] = Samples;
    Cells[10] = Areas;
    Cells[0] = ValidateFacetProfile();
}
