/*==========================================================================================================================================
                                           REPLAYMATERIALGUARDLINKS.SLANG
==========================================================================================================================================*/
// 🧩 Audit each candidate's actual spatial eligibility without retracing paths.
#include "PathReplayReservoir.slang"

[shader("compute")][numthreads(64, 1, 1)] void InspectReplayMaterialGuardLinks(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x;
    if (Pixel >= Extent.Dimensions.x) return;
    SurfacePoint Receiver = Receivers[Pixel];
    float Eligible = 0, CrossMaterial = 0, CrossPosition = 0, Rejected = 0;
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
            Eligible += 1;
            if (MaterialAddress(Receiver) != MaterialAddress(Neighbor)) CrossMaterial += 1;
            if (Receiver.PositionHit.x != Neighbor.PositionHit.x) CrossPosition += 1;
        }
    }
    OutputColour[Pixel] = float4(Eligible, CrossMaterial, CrossPosition, Rejected);
}
