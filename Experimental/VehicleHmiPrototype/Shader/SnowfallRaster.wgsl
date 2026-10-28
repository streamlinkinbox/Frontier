//! include "FrameDescriptor.wgsl"
struct SnowVarying {@builtin(position) Position:vec4f,@location(0) UV:vec2f,@location(1) Angle:f32}
@vertex
fn SnowVertex(@builtin(vertex_index) I:u32,@location(0) P:vec4f,@location(1) Shape:vec4f)->SnowVarying{
 var V:SnowVarying;let Clip=Frame.ViewProjection*vec4f(P.xyz,1);if(Clip.w<.05){V.Position=vec4f(0,0,-2,1);return V;}
 var Corners=array<vec2f,6>(vec2f(-1,-1),vec2f(1,-1),vec2f(1,1),vec2f(-1,-1),vec2f(1,1),vec2f(-1,1));let C=Corners[I%6u];
 let Up=vec3f(Frame.View[0].y,Frame.View[1].y,Frame.View[2].y);let W=Frame.ViewProjection*vec4f(P.xyz+Up*P.w,1);
 let Radius=clamp(length((W.xy/max(.05,W.w)-Clip.xy/Clip.w)*.5*Frame.ScreenExtent),.65,4.0);
 V.Position=vec4f(Clip.xy+C*Radius*2.0*Frame.InverseScreenExtent*Clip.w,Clip.z,Clip.w);V.UV=C;V.Angle=Shape.x;return V;
}
@fragment
fn SnowFragment(V:SnowVarying)->@location(0) vec4f{
 let R=length(V.UV);if(R>=1.0){discard;}
 let A=atan2(V.UV.y,V.UV.x)+V.Angle;let Spokes=exp(-abs(sin(A*3.0))*9.0);let Profile=(1.0-smoothstep(.45,1.0,R))*max(.25,Spokes);
 if(Profile<.025){discard;}
 let Day=smoothstep(-.12,.12,Frame.SolarDisc.z);let Light=vec3f(.04,.05,.07)+vec3f(.65,.73,.83)*Day+Frame.NightSkyRadiance+Frame.SolarRadiance.rgb*.05*Day;
 return vec4f(Light*Profile,V.Position.z);
}
