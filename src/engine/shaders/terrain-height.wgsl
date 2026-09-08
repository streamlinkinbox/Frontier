// Read back only a 2D upper envelope (not the multi-megabyte volume) for D8
// contributing-area analysis. This pass is coalesced after edits, never per pixel.
@group(0) @binding(0) var terrain: texture_3d<f32>;
@group(0) @binding(1) var heights: texture_storage_2d<rgba32float,write>;
@compute @workgroup_size(8,8)
fn extractHeight(@builtin(global_invocation_id) id:vec3u) {
  let dims=textureDimensions(terrain);
  if(id.x>=dims.x||id.y>=dims.z){return;}
  var height=-10.;var present=0.;
  for(var y=i32(dims.y)-1;y>=0;y--){
    let d=textureLoad(terrain,vec3i(i32(id.x),y,i32(id.y)),0).x;
    if(d>=0.){continue;}
    present=1.;
    if(y==i32(dims.y)-1){height=38.;}
    else {
      let air=textureLoad(terrain,vec3i(i32(id.x),y+1,i32(id.y)),0).x;
      let t=clamp(-d/max(air-d,.000001),0.,1.);
      height=-10.+(f32(y)+.5+t)*48./f32(dims.y);
    }
    break;
  }
  textureStore(heights,vec2i(id.xy),vec4f(height,present,0.,0.));
}
