/*==========================================================================================================================================
                                              REPLAYRIGIDTAILAUDIT.SLANG
==========================================================================================================================================*/
// 🧩 Replay the five already-generated donor seeds at one receiver for a finite tail diagnostic.
#include "PathReplayReservoir.slang"
#include "ReplayRigidTailBreakdown.slang"

[shader("compute")]
[numthreads(64, 1, 1)]
void AuditReplayRigidTail(uint3 Dispatch : SV_DispatchThreadID)
{
    if (Dispatch.x != 0) return;
    uint Pixel = 38u * Extent.History.x + 28u;
    SurfacePoint Receiver = Receivers[Pixel];
    static const int2 Offsets[5] = {int2(0, 0), int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1)};
    int2 Centre = int2(28, 38);
    uint SelectionBits = (Extent.Sampling.x * 2891336453u + 1013904223u) ^ (Pixel * 277803737u + 13u);
    uint Chosen = 0, ChosenSeed = 0;
    float Sum = 0;
    for (uint Index = 0; Index < 5; ++Index)
    {
        int2 Position = Centre + Offsets[Index];
        uint Donor = uint(Position.y) * Extent.History.x + uint(Position.x);
        float4 Source = OutputColour[ReplaySlot(2, Donor)];
        float3 Radiance = Index == 0 ? OutputColour[ReplaySlot(3, Pixel)].xyz :
                                      EvaluateReplayPath(Receiver, DecodeReplaySeed(Source.xy));
        float Target = ReplayTarget(Radiance);
        float Weight = Target * Source.z * Source.w;
        Sum += Weight;
        if (RandomUnit(SelectionBits) * Sum < Weight)
        {
            Chosen = Index;
            ChosenSeed = DecodeReplaySeed(Source.xy);
        }
        OutputColour[ReplaySlot(5, Pixel + Index)] = float4(Radiance, Target);
        OutputColour[ReplaySlot(6, Pixel + Index)] = float4(Weight, Source.z, Source.x, Source.y);
    }
    float3 Breakdown = TraceRigidTailBreakdown(Receiver, ChosenSeed, Pixel);
    OutputColour[ReplaySlot(5, Pixel + 20)] = float4(Breakdown, float(Chosen));
}
