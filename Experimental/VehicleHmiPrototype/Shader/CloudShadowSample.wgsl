//! include "CloudShadowRecords.wgsl"
// Bind Shadow, CloudShadowMap and CloudShadowSampler in the receiver shader.
fn SampleProjectedCloudShadow(UV:vec2f,Z:f32)->f32{
 let Data=textureSampleLevel(CloudShadowMap,CloudShadowSampler,UV,0.0);
 // Full optical depth below each medium; approximate remaining depth inside it.
 let GlobalFraction=clamp((Shadow.Quality.w-Z)/max(1.0,Shadow.Quality.w-Shadow.Quality.z),0.0,1.0);
 let LocalFraction=clamp((Data.w-Z)/max(.01,Data.w-Data.z),0.0,1.0);
 return exp(-Data.x*GlobalFraction-Data.y*LocalFraction);
}
fn CloudGroundTransmission(Position:vec3f)->f32{
 if(Shadow.Map.w<.5){return 1.0;}
 let Ground=Position.xy-Position.z*Shadow.Sun.xy/max(.03,Shadow.Sun.z);
 let UV=(Ground-Shadow.Map.xy)/Shadow.Map.z;
 // Smoothly hand back to unshadowed sunlight at the finite field boundary.
 let Edge=min(min(UV.x,UV.y),min(1.0-UV.x,1.0-UV.y));if(Edge<=0){return 1.0;}
 let Fade=smoothstep(0.0,.08,Edge)*smoothstep(.03,.12,Shadow.Sun.z)*Shadow.Sun.w;
 let Offset=Shadow.Quality.x/Shadow.Map.z;var T=0.0;
 if(Offset<.00001){T=SampleProjectedCloudShadow(UV,Position.z);}else{
  T=(SampleProjectedCloudShadow(UV+vec2f(-Offset,-Offset),Position.z)+SampleProjectedCloudShadow(UV+vec2f(Offset,-Offset),Position.z)+SampleProjectedCloudShadow(UV+vec2f(-Offset,Offset),Position.z)+SampleProjectedCloudShadow(UV+vec2f(Offset,Offset),Position.z))*.25;
 }
 return mix(1.0,T,Fade);
}
