//! include "FrameDescriptor.wgsl"
//! include "PrecipitationRecords.wgsl"
@group(0) @binding(1) var<uniform> P:PrecipitationParameters;
@group(0) @binding(2) var<storage,read> Grains:array<SettledGrain>;
struct Varying {@builtin(position) Position:vec4f,@location(0) Normal:vec3f,@location(1) Kind:f32}
@vertex fn DepositVertex(@builtin(vertex_index) V:u32,@builtin(instance_index) I:u32)->Varying{
 let G=Grains[I];let Face=V/3u;let K=Face%4u;let Upper=Face<4u;
 var Ring=array<vec3f,4>(vec3f(1,0,0),vec3f(0,1,0),vec3f(-1,0,0),vec3f(0,-1,0));
 let A=vec3f(0,0,select(-1.0,1.0,Upper));let B=Ring[select((K+1u)%4u,K,Upper)];let C0=Ring[select(K,(K+1u)%4u,Upper)];
 var Q=A;if(V%3u==1u){Q=B;}if(V%3u==2u){Q=C0;}var Normal=normalize(cross(B-A,C0-A));
 var Position=G.PositionRadius.xyz+Q*G.PositionRadius.w;
 if(G.Meta.y>.5){Position=(P.Body*vec4f(Position,1)).xyz;Normal=normalize((P.Body*vec4f(Normal,0)).xyz);}
 var R:Varying;R.Position=Frame.ViewProjection*vec4f(Position,1);R.Normal=Normal;R.Kind=G.Meta.x;return R;
}
// Opaque actual triangles, no coverage mask, no texture and no connected ground/car coating.
@fragment fn DepositFragment(V:Varying)->@location(0) vec4f{
 let Day=smoothstep(-.12,.12,Frame.SolarDisc.z);let N=normalize(V.Normal);let Light=Frame.NightSkyRadiance+vec3f(.15,.18,.23)*Day*Frame.SkyScale+Frame.SolarRadiance.rgb*.16*Day*max(0.0,dot(N,Frame.SolarDisc.xyz));let Tint=select(vec3f(.78,.86,.96),vec3f(.94,.97,1),V.Kind==3.0);return vec4f(Tint*Light,1);
}
