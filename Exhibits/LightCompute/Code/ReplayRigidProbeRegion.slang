/*==========================================================================================================================================
                                               REPLAYRIGIDPROBEREGION.SLANG
==========================================================================================================================================*/
// 🧩 Retain one primary receiver and the four spatial donors after the shared full primary pass.
#include "LightInfluence.slang"

[shader("compute")]
[numthreads(64, 1, 1)]
void RestrictReplayRigidReceivers(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x;
    if (Pixel >= Extent.Dimensions.x) return;
    int2 Coordinate = int2(Pixel % Extent.History.x, Pixel / Extent.History.x);
    int2 Offset = abs(Coordinate - int2(28, 38));
    if (Offset.x + Offset.y > 1)
    {
        SurfacePoint Receiver = Receivers[Pixel];
        Receiver.PositionHit.w = 0;
        Receivers[Pixel] = Receiver;
    }
}
