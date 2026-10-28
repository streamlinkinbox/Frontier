// Solar-disc occlusion evaluated ONCE per frame, not nine taps per screen pixel.
//! include "FrameDescriptor.wgsl"
@group(0) @binding(1) var OpaqueDepth:texture_depth_2d;
@group(0) @binding(2) var OpticalTransmission:texture_2d<f32>;
@group(0) @binding(3) var VisibilityTarget:texture_storage_2d<rgba16float,write>;
@compute @workgroup_size(1)
fn IntegrateFlareVisibility(){
 var Visibility=0.0;
 let Clip=Frame.UnjitteredViewProjection*vec4f(Frame.SolarDisc.xyz,0.0);
 if(Frame.Flare.x>0.0 && Frame.SolarDisc.z>0.0 && Clip.w>0.0){
  let UV=vec2f(Clip.x/Clip.w*0.5+0.5,0.5-Clip.y/Clip.w*0.5);
  if(all(UV>=vec2f(0.0))&&all(UV<=vec2f(1.0))){
   let Radius=max(1.0,Frame.SolarDisc.w/max(1e-6,Frame.StarControl.x));
   for(var I=0u;I<9u;I++){
    let A=f32(I)*0.78539816;let R=select(Radius*0.7,0.0,I==0u);
    let Tap=clamp(vec2i(UV*Frame.ScreenExtent+vec2f(cos(A),sin(A))*R),vec2i(0),vec2i(Frame.ScreenExtent)-1);
    Visibility+=select(0.0,textureLoad(OpticalTransmission,Tap,0).r,textureLoad(OpaqueDepth,Tap,0)<=0.0)/9.0;
   }
  }
 }
 textureStore(VisibilityTarget,vec2i(0),vec4f(Visibility));
}
