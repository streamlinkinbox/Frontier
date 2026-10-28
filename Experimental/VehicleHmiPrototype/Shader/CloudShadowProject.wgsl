// 128² projected optical-depth map. No 3D light cache or full-screen raymarch.
//! include "FrameDescriptor.wgsl"
//! include "CloudRecords.wgsl"
//! include "CloudShadowRecords.wgsl"
@group(0) @binding(1) var<uniform> C:CloudParameters;
@group(0) @binding(2) var ShapeField:texture_3d<f32>;
@group(0) @binding(3) var FieldSampler:sampler;
@group(0) @binding(4) var<uniform> Shadow:CloudShadowParameters;
@group(0) @binding(5) var ShadowTarget:texture_storage_2d<rgba16float,write>;
//! include "CloudField.wgsl"
fn OpticalDepth(O:vec3f,D:vec3f,Hit:vec2f,Local:bool)->f32 {
 if(Hit.y<=Hit.x){return 0.0;}let N=u32(Shadow.Quality.y);let Step=(Hit.y-Hit.x)/f32(N);var Tau=0.0;
 for(var I=0u;I<N;I++){
  let P=O+D*(Hit.x+(f32(I)+.5)*Step);var Density=0.0;
  if(Local){Density=PuffDensity(P);}else{Density=LayerDensity(P);}
  Tau+=Density*Step*.012;if(Tau>=8.0){break;}
 }return min(8.0,Tau);
}
@compute @workgroup_size(8,8)
fn ProjectCloudShadow(@builtin(global_invocation_id) ID:vec3u){
 let N=textureDimensions(ShadowTarget);if(any(ID.xy>=N)){return;}
 let Ground=Shadow.Map.xy+(vec2f(ID.xy)+.5)/vec2f(N)*Shadow.Map.z;
 let D=Shadow.Sun.xyz;
 // Start below every supported cloud bound, including negative-altitude local volumes.
 let O=vec3f(Ground-32768.0*D.xy/D.z,-32768.0);
 let G=LayerHit(O,D,2000000.0);let L=PuffHit(O,D,2000000.0);
 let GlobalTau=OpticalDepth(O,D,G,false);let LocalTau=OpticalDepth(O,D,L,true);
 var Ends=vec2f(0);if(L.y>L.x){Ends=vec2f(O.z+D.z*L.x,O.z+D.z*L.y);}
 textureStore(ShadowTarget,vec2i(ID.xy),vec4f(GlobalTau,LocalTau,Ends));
}
