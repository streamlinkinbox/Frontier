/*==========================================================================================================================================
                                               REPLAYMOTIONPROBEREGION.SLANG
==========================================================================================================================================*/
// 🧩 Retain two primary-ray neighbourhoods for the moving replay convergence audit.
#include "LightInfluence.slang"

[shader("compute")]
[numthreads(64, 1, 1)]
void RestrictReplayMotionReceivers(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x;
    if (Pixel >= Extent.Dimensions.x) return;
    int2 Coordinate = int2(Pixel % Extent.History.x, Pixel / Extent.History.x);
    bool RigidRegion = all(abs(Coordinate - int2(28, 38)) <= int2(6, 6));
    bool FloorRegion = all(abs(Coordinate - int2(46, 50)) <= int2(6, 6));
    if (!RigidRegion && !FloorRegion)
    {
        SurfacePoint Receiver = Receivers[Pixel];
        Receiver.PositionHit.w = 0;
        Receivers[Pixel] = Receiver;
    }
}
