//============================================================================================================================================
//                                                             HIZPYRAMID.WGSL
//============================================================================================================================================
// 🧩 Conservative Reversed-Z depth pyramid (1.0 = near, 0.0 = far/clear): each mip stores the MINIMUM depth over its
//    footprint. The seed pass copies the surface depth into mip 0; HiZDownsample.wgsl builds the rest of the chain.
//    (Kept as separate modules per entry point: sharing @binding slots between entries with different resource
//    types in one module is rejected by some WGSL validators, so seed and downsample never share a module.)

@group(0) @binding(0) var SourceDepth : texture_depth_2d;
@group(0) @binding(1) var DestMip0    : texture_storage_2d<r32float, write>;

@compute @workgroup_size(8, 8)
fn SeedHiZFromDepth(@builtin(global_invocation_id) ID : vec3u)
{
    let Extent = textureDimensions(SourceDepth);
    if (ID.x >= Extent.x || ID.y >= Extent.y) { return; }
    let Depth = textureLoad(SourceDepth, vec2i(ID.xy), 0);
    textureStore(DestMip0, vec2i(ID.xy), vec4f(Depth, 0.0, 0.0, 1.0));
}
