/*==========================================================================================================================================
                                                   REPLAYFRESHBUDGET.SLANG
==========================================================================================================================================*/
// 🧩 Eight current-only paths using the production path evaluator; validation control, not a reservoir candidate.

#include "PathReplayReservoir.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                               CURRENT-ONLY PATH MEAN
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")][numthreads(64,1,1)] void GenerateFreshBudget(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x;
    if (Pixel >= Extent.Dimensions.x) return;
    SurfacePoint Receiver = Receivers[Pixel];
    float4 LocalMeanDescriptor = 0;
    float4 MeanRadiance        = 0;
    if (Receiver.PositionHit.w != 0 && Receiver.MotionPositionToken.w > 0 && Receiver.ViewMetallic.w == 0)
    {
        uint SeedSequence  = Extent.Sampling.x ^ (Pixel * 747796405u + 2891336453u);
        uint FirstSeed     = NextRandom(SeedSequence);
        float3 RadianceSum = EvaluateReplayPath(Receiver, FirstSeed);
        for (uint Index = 1; Index < 8; ++Index)
            RadianceSum += EvaluateReplayPath(Receiver, NextRandom(SeedSequence));
        float3 Mean = RadianceSum / 8.f;
        // 📝 Resolve consumes this mean exactly once with unit weight. No spatial/temporal reuse is allowed.
        LocalMeanDescriptor = float4(EncodeReplaySeed(FirstSeed), 1, 1);
        MeanRadiance        = float4(Mean, ReplayTarget(Mean));
    }
    OutputColour[ReplaySlot(2, Pixel)] = LocalMeanDescriptor;
    OutputColour[ReplaySlot(3, Pixel)] = MeanRadiance;
}
