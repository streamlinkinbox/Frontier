//! include "FrameDescriptor.wgsl"
struct LocalArrival { Direction:vec3f, Illuminance:vec3f, }
fn LocalLightMask(Position:vec3f)->u32 {
    let Cell=vec3i(floor((Position-Frame.LightClusterOrigin.xyz)/vec3f(16.0,16.0,8.0)));
    // Cards/mirrors outside the moving grid remain correct via bounded all-light fallback.
    if(any(Cell<vec3i(0))||any(Cell>=vec3i(8,8,4))){return 0xffffffffu;}
    return Frame.LightClusters[u32(Cell.x+8*(Cell.y+8*Cell.z))].x;
}
fn IESAt(H:u32,V:u32)->f32 {
    let I=H*16u+V;return Frame.IESProfile[I/4u][I%4u];
}
fn LocalPhotometry(L:LocalLight,Ray:vec3f)->f32 {
    let Cosine=dot(Ray,L.DirectionType.xyz);
    let Type=u32(L.DirectionType.w+0.5);
    if(Type==0u){return 1.0;}
    let Right=normalize(cross(L.DirectionType.xyz,L.UpInner.xyz));
    let H=atan2(dot(Ray,Right),max(Cosine,0.0001));
    let V=atan2(dot(Ray,L.UpInner.xyz),max(Cosine,0.0001));
    if(Type==6u){
        let Polar=acos(clamp(Cosine,-1.0,1.0))*15.0/3.14159265;
        let Azimuth=fract(atan2(dot(Ray,Right),dot(Ray,L.UpInner.xyz))/6.2831853+1.0)*15.0;
        let A=vec2u(min(floor(vec2f(Azimuth,Polar)),vec2f(14.0)));
        let F=vec2f(Azimuth,Polar)-vec2f(A);
        return mix(mix(IESAt(A.x,A.y),IESAt(A.x,A.y+1u),F.y),mix(IESAt(A.x+1u,A.y),IESAt(A.x+1u,A.y+1u),F.y),F.x);
    }
    if(Cosine<=0.0){return 0.0;}
    if(Type==1u){return smoothstep(L.ColourOuter.w,L.UpInner.w,Cosine);}
    if(Type==5u){
        // Shielded roadway distribution: broad across the road, less spill along its support.
        return pow(max(Cosine,0.0),0.65)*exp(-pow(H/1.1,4.0)-pow(V/0.85,4.0));
    }
    let W=acos(clamp(L.ColourOuter.w,-1.0,1.0))/select(0.610865,0.314159,Type==3u);
    if(Type==3u){return exp(-pow(H/(0.14*W),2.0)-pow(V/0.10,2.0));}
    if(Type==4u){return exp(-pow(H/(0.65*W),4.0)-pow(V/0.06,4.0))*(1.0-smoothstep(0.0,0.018,V));}
    // Authored low-beam approximation, not road-certified photometry. Hard upper cutoff,
    // softened edge, broad foreground and a slightly biased hotspot below the horizon.
    let Cutoff=1.0-smoothstep(-0.005,0.014,V);
    return Cutoff*(0.25*exp(-pow(H/(0.40*W),4.0)-pow((V+0.08)/0.16,2.0))+
        0.75*exp(-pow((H-0.025)/(0.15*W),2.0)-pow((V+0.025)/0.065,2.0)));
}
fn ResolveLocalArrival(Position:vec3f,Normal:vec3f,Index:u32)->LocalArrival {
    var A:LocalArrival;A.Direction=vec3f(0,0,1);A.Illuminance=vec3f(0);
    let L=Frame.LocalLights[Index];let Delta=L.PositionReach.xyz-Position;
    let D2=dot(Delta,Delta);let Reach=L.PositionReach.w;
    if(Reach<=0.0||D2>=Reach*Reach){return A;}
    A.Direction=Delta/max(sqrt(D2),0.0001);
    if(dot(Normal,A.Direction)<=0.0){return A;}
    let Window=pow(1.0-pow(D2/(Reach*Reach),2.0),2.0);
    A.Illuminance=L.ColourOuter.rgb*Window*LocalPhotometry(L,-A.Direction)/max(D2,0.04);
    return A;
}
