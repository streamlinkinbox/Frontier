/*==========================================================================================================================================
                                              REPLAYSEGMENTVALIDATION.SLANG
==========================================================================================================================================*/
// 🧩 Inspect the production pane-segment calculation and MIS limit weights through Vulkan readback.

#include "PathReplayReservoir.slang"

[shader("compute")][numthreads(64, 1, 1)] void InspectReplaySegment(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x;
    if (Pixel >= Extent.Dimensions.x) return;
    ReplayThreadBoxTests = 0;
    ReplayThreadTriTests = 0;
    float Probability;
    uint PaneCount;
    float3 Transmittance = ReplaySegmentTransmittance(Receivers[Pixel].PositionHit.xyz + float3(0, 0, 0.001),
                                                      float3(0, 0, 1), 1.4, 0xffffffffu,
                                                      Probability, PaneCount);
    OutputColour[Pixel] = float4(Transmittance.x, Probability, float(PaneCount), 1.0);
}

[shader("compute")][numthreads(64, 1, 1)] void InspectReplayMisLimits(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x;
    if (Pixel >= Extent.Dimensions.x) return;
    float Ratio = Pixel % 4u == 0u ? 0.0 : Pixel % 4u == 1u ? 1.0 :
                  Pixel % 4u == 2u ? asfloat(0x7f800000u) : asfloat(0x7fc00000u);
    OutputColour[Pixel] = float4(ReplayMisWeight(Ratio, false), ReplayMisWeight(Ratio, true),
                                 ReplaySourceMisWeight(Ratio, 0.0), 1.0);
}
