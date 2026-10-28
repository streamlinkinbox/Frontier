/*==========================================================================================================================================
                                               REPLAYRIGIDPROBEPRIMARY.SLANG
==========================================================================================================================================*/
// 🧩 Invoke the shared primary-ray shader only for one audited receiver and its spatial donors.
#include "LightInfluence.slang"

[shader("compute")]
[numthreads(64, 1, 1)]
void TraceReplayRigidProbePrimary(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x;
    if (Pixel >= Extent.Dimensions.x) return;
    int2 Coordinate = int2(Pixel % Extent.History.x, Pixel / Extent.History.x);
    int2 Offset = abs(Coordinate - int2(28, 38));
    if (Offset.x + Offset.y > 1) return;
    TracePrimarySurfaces(Dispatch);
}
