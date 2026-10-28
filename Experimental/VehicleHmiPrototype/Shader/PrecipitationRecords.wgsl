struct PrecipitationParameters {
 Body:mat4x4f,InverseBody:mat4x4f,
 Region:vec4f, // world minimum xy, ground cell edge, ground dimension
 Car:vec4f, // minimum xy, cell edge, dimension
 Step:vec4f, // dt, enabled, particle budget, emission probability
 Snow:vec4f, // downward speed, representative volume units, maximum depth, tick
 Wind:vec4f, // shared horizontal velocity, altitude shear, veer degrees/km
 Weather:vec4f, // shared wind clock, turbulence, prewarm, cloud selection 0 both / 1 global / 2 local
 Extra:vec4f, // size, wind drift, follow wind, spawn plane Z (0 = actual clouds)
 PreviousInverseBody:mat4x4f,
 Physics:vec4f, // terminal speed, drag, user restitution, selected category
 Emitter:vec4f, // camera xyz, radius
 Direction:vec4f, // horizontal camera forward xy, cloud-mask enabled, spawn ceiling Z
 Look:vec4f, // opacity, streak length, rest time, accumulation enabled/weight
};
struct SettledGrain {PositionRadius:vec4f,Meta:vec4f}

struct SnowParticle {Position:vec4f,Shape:vec4f,VelocityVolume:vec4f,OriginDensity:vec4f}
fn TerrainHeight(P:vec2f)->f32{
 if(P.x>=-52.0&&P.x<=20.0&&abs(P.y)>=6.0&&abs(P.y)<=6.3){return .12;}
 if(P.x>=22.0&&P.x<=28.0&&abs(P.y)<=3.2){return 1.35*(P.x-22.0)/6.0;}return 0.0;
}
fn ReceiverIndex(P:vec2f,Map:vec4f)->i32{let XY=vec2i(floor((P-Map.xy)/Map.z));let N=i32(Map.w);if(any(XY<vec2i(0))||any(XY>=vec2i(N))){return -1;}return XY.y*N+XY.x;}

fn PrecipType(Category:u32)->vec4f{switch Category {case 0u:{return vec4f(8.5,.25,0,2.2);}case 1u:{return vec4f(3.2,.6,0,.6);}case 2u:{return vec4f(22,.08,.6,9);}case 4u:{return vec4f(5.5,.4,.15,3);}default:{return vec4f(1.4,1.2,0,5);}}}
