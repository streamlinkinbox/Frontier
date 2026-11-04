//============================================================================================================================================
//                                                           HIZPYRAMID.SLANG
//============================================================================================================================================
// 🧩 Conservative reversed-Z depth pyramid (1 near, 0 far or clear): the seed copies the surface depth into mip 0; each mip stores the
//    minimum depth over its footprint and HiZDownsample.slang builds the rest of the chain (HiZPyramid.wgsl).

#pragma once
#include "IntrinsicParity.slang"

  [[vk::binding(0, 0)]] Texture2D<float>                                    SourceDepth;
  [[vk::binding(1, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float>     DestMip0;

[shader("compute")]
[numthreads(8, 8, 1)]
void SeedHiZFromDepth(uint3 ID : SV_DispatchThreadID)
{
    let Extent = TextureExtent(SourceDepth);
    if (ID.x >= Extent.x || ID.y >= Extent.y) { return; }
    let Depth = LoadTexel(SourceDepth, int2(ID.xy), 0);
    DestMip0[ID.xy] = Depth;
}
