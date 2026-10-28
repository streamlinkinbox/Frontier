//! include "FrameDescriptor.wgsl"
struct DepositVarying {@builtin(position) Position:vec4f,@location(0) Normal:vec3f,@location(1) Thickness:f32}
@vertex
fn DepositVertex(@location(0) P:vec3f,@location(1) N:vec3f,@location(2) Thickness:f32)->DepositVarying{
 var V:DepositVarying;V.Position=Frame.ViewProjection*vec4f(P,1);V.Normal=N;V.Thickness=Thickness;return V;
}
@fragment
fn DepositFragment(V:DepositVarying)->@location(0) vec4f{
 let Coverage=smoothstep(0.0,.004,V.Thickness);if(Coverage<.015){discard;}
 let N=normalize(V.Normal);let Day=smoothstep(-.12,.12,Frame.SolarDisc.z);
 let Diffuse=max(0.0,dot(N,Frame.SolarDisc.xyz));let Light=Frame.NightSkyRadiance+vec3f(.12,.16,.22)*Day*Frame.SkyScale+Frame.SolarRadiance.rgb*.2*Day*Diffuse;
 return vec4f(vec3f(.82,.89,.96)*Light,Coverage);
}
