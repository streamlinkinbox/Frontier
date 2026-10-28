//! include "FrameDescriptor.wgsl"
//! include "PrecipitationRecords.wgsl"
@group(0) @binding(1) var<uniform> P:PrecipitationParameters;
struct SnowVarying {@builtin(position) Position:vec4f,@location(0) UV:vec2f,@location(1) @interpolate(flat) Kind:u32,@location(2) Fade:f32,@location(3) @interpolate(flat) Rest:f32}
@vertex fn SnowVertex(@builtin(vertex_index) I:u32,@location(0) X:vec4f,@location(1) Shape:vec4f,@location(2) Motion:vec4f)->SnowVarying{
 var V:SnowVarying;let Clip=Frame.ViewProjection*vec4f(X.xyz,1);let Category=u32(Shape.z)/4u;
 if((Shape.y!=1.0&&Shape.y!=4.0)||Clip.w<.05){V.Position=vec4f(0,0,-2,1);return V;}
 var Corners=array<vec2f,6>(vec2f(-1,-1),vec2f(1,-1),vec2f(1,1),vec2f(-1,-1),vec2f(1,1),vec2f(-1,1));let Q=Corners[I%6u];
 let Up=vec3f(Frame.View[0].y,Frame.View[1].y,Frame.View[2].y);let W=Frame.ViewProjection*vec4f(X.xyz+Up*X.w,1);let Radius=max(select(.5,1.0,Category==3u),length((W.xy/W.w-Clip.xy/Clip.w)*.5*Frame.ScreenExtent));
 var Offset=Q*min(5.0,Radius);let IsRest=Shape.y==4.0;
 if(Category<2u&&!IsRest){let Factor=P.Look.y*select(1.0,.5,Category==1u)*.05*.4;let Tail=Frame.ViewProjection*vec4f(X.xyz-Motion.xyz*Factor,1);let D=(Tail.xy/max(.01,Tail.w)-Clip.xy/Clip.w)*.5*Frame.ScreenExtent;let Length=max(1.5,length(D));let Along=normalize(select(vec2f(0,1),D,length(D)>.001));let Across=vec2f(-Along.y,Along.x);Offset=Across*Q.x*max(.35,Radius*.8)+Along*(Q.y+1.0)*.5*min(80.0,Length);}
 if(IsRest&&Category<2u){Offset=Q*vec2f(1.0,.35)*(1.0+Shape.x/max(.1,P.Look.z)*3.0)*2.0;}
 V.Position=vec4f(Clip.xy+Offset*2.0*Frame.InverseScreenExtent*Clip.w,Clip.z,Clip.w);V.UV=Q;V.Kind=Category;V.Rest=select(0.0,Shape.x/max(.1,P.Look.z),IsRest);V.Fade=min(1.0,Clip.w/2.0)*(1.0-clamp((Clip.w-72.0)/27.0,0.0,1.0));return V;
}
@fragment fn SnowFragment(V:SnowVarying)->@location(0) vec4f{
 let R=length(V.UV);var Profile=1.0;
 if(V.Kind>=2u){if(R>1.0){discard;}Profile=1.0-smoothstep(.7,1.0,R);if(V.Kind!=3u){Profile*=.75+.25*max(0.0,1.0-length(V.UV+vec2f(.3)));}}
 else if(V.Rest>0.0){if(R>.98||R<.65){discard;}Profile=(1.0-V.Rest)*.35;}else{Profile=1.0-smoothstep(.5,1.0,abs(V.UV.x));}
 let Day=smoothstep(-.12,.12,Frame.SolarDisc.z);let Light=vec3f(.04,.05,.07)+vec3f(.65,.73,.83)*Day+Frame.NightSkyRadiance+Frame.SolarRadiance.rgb*.05*Day;return vec4f(Light*Profile*P.Look.x*V.Fade,V.Position.z);
}
