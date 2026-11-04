//============================================================================================================================================
//                                                      CLUSTERDEBUGFALLBACK.SLANG
//============================================================================================================================================
// 🧩 Checkers written into the display inspection target when a cluster inspection mode is selected but the virtualized geometry
//    cannot show real data: magenta unavailable, red failed, orange no eligible facet set (ClusterDebugFallback.wgsl).

#pragma once
#include "IntrinsicParity.slang"

  [[vk::binding(0, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4>     Target;

void PaintChecker(uint3 ID, float3 Dark, float3 Bright)
{
    let Extent = TextureExtent(Target);
    if (ID.x >= Extent.x || ID.y >= Extent.y) { return; }
    let Checker = ((ID.x / 24u) + (ID.y / 24u)) % 2u == 0u;
    Target[ID.xy] = float4(SelectBranch(Dark, Bright, Checker), 1.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void DebugUnavailable(uint3 ID : SV_DispatchThreadID) { PaintChecker(ID, float3(0.10, 0.02, 0.14), float3(0.62, 0.08, 0.52)); }

[shader("compute")]
[numthreads(8, 8, 1)]
void DebugFailed(uint3 ID : SV_DispatchThreadID) { PaintChecker(ID, float3(0.16, 0.02, 0.02), float3(0.66, 0.10, 0.08)); }

[shader("compute")]
[numthreads(8, 8, 1)]
void DebugNoGeometry(uint3 ID : SV_DispatchThreadID) { PaintChecker(ID, float3(0.16, 0.07, 0.01), float3(0.68, 0.38, 0.06)); }
