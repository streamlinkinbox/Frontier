//! include "AtmosphereMedium.wgsl"
@group(0) @binding(1) var Target : texture_storage_2d<rgba16float,write>;
@compute @workgroup_size(8,8)
fn IntegrateTransmittance(@builtin(global_invocation_id) ID: vec3u) {
 let Size=textureDimensions(Target); if(any(ID.xy>=Size)){return;}
 let UV=(vec2f(ID.xy)+0.5)/vec2f(Size);
 let H=UV.y*UV.y*(AtmosphereRadius-GroundRadius);
 let Mu=UV.x*2.0-1.0;
 let P=vec3f(0.0,0.0,GroundRadius+max(1.0,H));
 let D=vec3f(sqrt(max(0.0,1.0-Mu*Mu)),0.0,Mu);
 textureStore(Target,vec2i(ID.xy),vec4f(MarchTransmittance(P,D),1.0));
}
