//! include "FrameDescriptor.wgsl"
struct BoltVarying {@builtin(position) Position:vec4f,@location(0) Radiance:vec3f,@location(1) Across:f32}
@vertex
fn SparkVertex(@builtin(vertex_index) I:u32,@location(0) A:vec4f,@location(1) B:vec4f,@location(2) Energy:vec4f)->BoltVarying{
 var V:BoltVarying;var H=Frame.UnjitteredViewProjection*vec4f(A.xyz,1);var T=Frame.UnjitteredViewProjection*vec4f(B.xyz,1);
 // Clip the world segment against the camera near half-space rather than stretching it across the screen.
 if(H.w<.05&&T.w<.05){V.Position=vec4f(0,0,-2,1);return V;}
 if(H.w<.05){H=mix(H,T,(.05-H.w)/max(.0001,T.w-H.w));}
 if(T.w<.05){T=mix(T,H,(.05-T.w)/max(.0001,H.w-T.w));}
 let HP=H.xy/H.w*.5*Frame.ScreenExtent;let TP=T.xy/T.w*.5*Frame.ScreenExtent;
 let Along=HP-TP;let Len=length(Along);let Across=vec2f(-Along.y,Along.x)/max(.001,Len);
 let Up=vec3f(Frame.View[0].y,Frame.View[1].y,Frame.View[2].y);
 let WClip=Frame.UnjitteredViewProjection*vec4f(A.xyz+Up*A.w,1);
 let Width=clamp(length(WClip.xy/max(.05,WClip.w)*.5*Frame.ScreenExtent-HP),1.3,18.0);
 var Corners=array<vec2f,6>(vec2f(0,-1),vec2f(1,-1),vec2f(1,1),vec2f(0,-1),vec2f(1,1),vec2f(0,1));let C=Corners[I%6u];
 let Clip=mix(T,H,C.x);let Pixel=mix(TP,HP,C.x)+Across*C.y*Width*.5;
 V.Position=vec4f(Pixel/(.5*Frame.ScreenExtent)*Clip.w,Clip.z,Clip.w);V.Radiance=Energy.rgb;V.Across=C.y;return V;
}
@fragment
fn SparkFragment(V:BoltVarying)->@location(0) vec4f{
 let Core=exp(-V.Across*V.Across*45.0);let Halo=exp(-V.Across*V.Across*3.5)*.25;
 return vec4f(V.Radiance*(Core+Halo),V.Position.z);
}
