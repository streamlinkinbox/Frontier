//============================================================================================================================================
//                                                           HIZDOWNSAMPLE.WGSL
//============================================================================================================================================
// 🧩 One Hi-Z pyramid mip from its parent: minimum Reversed-Z depth over the 2x2 footprint, extended to
//    3 taps along an axis when the parent dimension is odd so non-power-of-two viewports never leak
//    (an unsampled parent column/row would make clusters behind it look visible). Separate module from
//    the seed pass (HiZPyramid.wgsl) so no @binding slot is ever shared between entry points.

@group(0) @binding(0) var ParentMip : texture_2d<f32>;
@group(0) @binding(1) var ChildMip  : texture_storage_2d<r32float, write>;

@compute @workgroup_size(8, 8)
fn DownsampleHiZMip(@builtin(global_invocation_id) ID : vec3u)
{
    let ParentExtent = textureDimensions(ParentMip);
    let ChildExtent  = textureDimensions(ChildMip);
    if (ID.x >= ChildExtent.x || ID.y >= ChildExtent.y) { return; }

    let BaseX = ID.x * 2u;
    let BaseY = ID.y * 2u;
    let ExtraX = (ParentExtent.x & 1u) != 0u && ParentExtent.x > 1u && (ID.x + 1u == ChildExtent.x);
    let ExtraY = (ParentExtent.y & 1u) != 0u && ParentExtent.y > 1u && (ID.y + 1u == ChildExtent.y);
    let TapsX = select(min(2u, ParentExtent.x - BaseX), 3u, ExtraX);
    let TapsY = select(min(2u, ParentExtent.y - BaseY), 3u, ExtraY);

    var MinDepth = 1.0;
    for (var DY = 0u; DY < TapsY; DY = DY + 1u)
    {
        for (var DX = 0u; DX < TapsX; DX = DX + 1u)
        {
            let Coord = vec2i(i32(min(BaseX + DX, ParentExtent.x - 1u)), i32(min(BaseY + DY, ParentExtent.y - 1u)));
            MinDepth = min(MinDepth, textureLoad(ParentMip, Coord, 0).x);
        }
    }
    textureStore(ChildMip, vec2i(ID.xy), vec4f(MinDepth, 0.0, 0.0, 1.0));
}
