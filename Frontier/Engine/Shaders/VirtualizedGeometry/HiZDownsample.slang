//============================================================================================================================================
//                                                          HIZDOWNSAMPLE.SLANG
//============================================================================================================================================
// 🧩 One Hi-Z mip from the finer one: minimum reversed-Z depth over the 2x2 footprint, three taps along an axis whose finer
//    dimension is odd so non-power-of-two viewports never leak (HiZDownsample.wgsl).

#pragma once
#include "IntrinsicParity.slang"

  [[vk::binding(0, 0)]] Texture2D<float>                                    ParentMip;
  [[vk::binding(1, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float>     ChildMip;

uint2 StorageExtent(RWTexture2D<float> Texture) { uint Width; uint Height; Texture.GetDimensions(Width, Height); return uint2(Width, Height); }

[shader("compute")]
[numthreads(8, 8, 1)]
void DownsampleHiZMip(uint3 ID : SV_DispatchThreadID)
{
    let ParentExtent = TextureExtent(ParentMip);
    let ChildExtent  = StorageExtent(ChildMip);
    if (ID.x >= ChildExtent.x || ID.y >= ChildExtent.y) { return; }

    let BaseX = ID.x * 2u;
    let BaseY = ID.y * 2u;
    let ExtraX = (ParentExtent.x & 1u) != 0u && ParentExtent.x > 1u && (ID.x + 1u == ChildExtent.x);
    let ExtraY = (ParentExtent.y & 1u) != 0u && ParentExtent.y > 1u && (ID.y + 1u == ChildExtent.y);
    let TapsX = SelectBranch(min(2u, ParentExtent.x - BaseX), 3u, ExtraX);
    let TapsY = SelectBranch(min(2u, ParentExtent.y - BaseY), 3u, ExtraY);

    var MinDepth = 1.0;
    for (var DY = 0u; DY < TapsY; DY = DY + 1u)
    {
        for (var DX = 0u; DX < TapsX; DX = DX + 1u)
        {
            let Coord = int2(ToSigned(min(BaseX + DX, ParentExtent.x - 1u)), ToSigned(min(BaseY + DY, ParentExtent.y - 1u)));
            MinDepth = min(MinDepth, LoadTexel(ParentMip, Coord, 0));
        }
    }
    ChildMip[ID.xy] = MinDepth;
}
