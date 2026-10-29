/*==========================================================================================================================================
                                           REPLAYMIXEDMATERIALLINKS.SLANG
==========================================================================================================================================*/
// 🧩 Audit the exact Engine disk offsets, compatibility and pre-spatial reservoir validity after the measured pass.
#include "PathReplayReservoir.slang"

[shader("compute")][numthreads(64, 1, 1)] void InspectReplayMixedMaterialLinks(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x;
    if (Pixel >= Extent.Dimensions.x) return;
    SurfacePoint Receiver = Receivers[Pixel];
    float Same = 0, FromDim = 0, FromMetal = 0, Rejected = 0;
    if (ReplayReservoirHistoryValid(OutputColour[ReplaySlot(2, Pixel)]))
    {
        int2 Centre = int2(Pixel % Extent.History.x, Pixel / Extent.History.x);
        for (uint Tap = 0; Tap < 4u; ++Tap)
        {
            int2 P = Centre + ProjectSpatialDiskOffset(Pixel, Extent.Sampling.x, Tap);
            if (any(P < 0) || any(P >= int2(Extent.History.xy))) continue;
            uint Donor = uint(P.y) * Extent.History.x + uint(P.x);
            SurfacePoint Neighbor = Receivers[Donor];
            if (!EvaluateSpatialSurfaceCompatibility(Receiver, Neighbor) ||
                !ReplayReservoirHistoryValid(OutputColour[ReplaySlot(2, Donor)]))
            {
                Rejected += 1;
                continue;
            }
            uint Address = MaterialAddress(Receiver), DonorAddress = MaterialAddress(Neighbor);
            if (Address == DonorAddress) Same += 1;
            else if (Address == 1u && DonorAddress == 269u) FromDim += 1;
            else if (Address == 269u && DonorAddress == 1u) FromMetal += 1;
            else Rejected += 1;
        }
    }
    OutputColour[Pixel] = float4(Same, FromDim, FromMetal, Rejected);
}
