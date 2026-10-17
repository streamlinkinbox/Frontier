// Multi-bounce Monte Carlo transport: energy-layered diffuse + GGX/VNDF + coat,
// thin dielectric transmission, one next-event sample per vertex, power MIS and
// Russian roulette. The suffix is replayed on reconnection, not treated as a
// view-independent cached colour at glossy secondary vertices.
//! include "PathScene.wgsl"
struct PathBsdfSample { Direction: vec3f, Weight: vec3f, Pdf: f32, Delta: u32, }
fn PathBsdfCos(S: SurfaceSample, N: vec3f, V: vec3f, L: vec3f) -> vec3f {
    let Nl = dot(N,L); let Nv = dot(N,V);
    if (Nl <= 0.0 || Nv <= 0.0) { return vec3f(0.0); }
    let H = normalize(V+L); let Nh = max(dot(N,H),0.0); let Vh = max(dot(V,H),0.0);
    let Fresnel = PathFresnel(ResolveSpecularColour(S),Vh);
    let Diffuse = ResolveDiffuseAlbedo(S)*(1.0-S.Transmission)*InversePi*(vec3f(1.0)-Fresnel);
    let Specular = Fresnel*ResolveGgxDistribution(Nh,S.Roughness)*ResolveSmithVisibility(Nl,Nv,S.Roughness);
    let Fc = S.CoatWeight*ResolveFresnel(vec3f(0.04),Vh).x;
    let Coat = Fc*ResolveGgxDistribution(Nh,S.CoatRoughness)*ResolveSmithVisibility(Nl,Nv,S.CoatRoughness);
    return max(vec3f(0.0),((Diffuse+Specular)*(1.0-Fc)+Coat)*Nl);
}
fn PathLobeProbabilities(S: SurfaceSample, NdotV: f32, Transmission: bool) -> vec4f {
    let Base = PathFresnel(ResolveSpecularColour(S),NdotV);
    let D = max(0.0,ResolveLuminance(ResolveDiffuseAlbedo(S))*(1.0-S.Transmission));
    let G = max(0.0,ResolveLuminance(Base));
    let C = max(0.0,S.CoatWeight*ResolveFresnel(vec3f(0.04),NdotV).x);
    let T = select(0.0,ResolveLuminance(PathTransmission(S,NdotV)),Transmission);
    let Sum = D+G+C+T;
    return vec4f(D,G,C,T)/max(Sum,1.0e-12);
}
fn PathGgxG1(Nv: f32, Roughness: f32) -> f32 {
    let A = Roughness*Roughness; return 2.0*Nv/max(Nv+sqrt(A*A+(1.0-A*A)*Nv*Nv),1.0e-20);
}
fn PathGgxPdf(N: vec3f, V: vec3f, L: vec3f, Roughness: f32) -> f32 {
    if (dot(N,L) <= 0.0 || dot(N,V) <= 0.0) { return 0.0; }
    let H = normalize(V+L);
    if (dot(V,H)<=0.0) { return 0.0; }
    return ResolveGgxDistribution(max(0.0,dot(N,H)),Roughness)*PathGgxG1(dot(N,V),Roughness)/(4.0*dot(N,V));
}
fn PathBsdfPdf(S: SurfaceSample, N: vec3f, V: vec3f, L: vec3f, Transmission: bool) -> f32 {
    let P = PathLobeProbabilities(S,max(0.0,dot(N,V)),Transmission);
    return P.x*max(0.0,dot(N,L))*InversePi+P.y*PathGgxPdf(N,V,L,S.Roughness)+P.z*PathGgxPdf(N,V,L,S.CoatRoughness);
}
// Heitz 2018, sampling the visible GGX normal distribution (not the NDF).
fn PathSampleVndf(N: vec3f, V: vec3f, Roughness: f32, U: vec2f) -> vec3f {
    let Basis = ConstructTangentFrame(N); let Local = transpose(Basis)*V;
    let Alpha = Roughness*Roughness;
    let Vh = normalize(vec3f(Alpha*Local.xy,max(Local.z,1.0e-6)));
    let Len = dot(Vh.xy,Vh.xy);
    let T1 = select(vec3f(1.0,0.0,0.0),vec3f(-Vh.y,Vh.x,0.0)/sqrt(max(Len,1.0e-20)),Len>0.0);
    let T2 = cross(Vh,T1);
    let R = sqrt(U.x); let Phi = 2.0*Pi*U.y;
    let T = R*cos(Phi); var B = R*sin(Phi);
    let S = 0.5*(1.0+Vh.z); B = (1.0-S)*sqrt(max(0.0,1.0-T*T))+S*B;
    let Nh = T*T1+B*T2+sqrt(max(0.0,1.0-T*T-B*B))*Vh;
    let H = normalize(vec3f(Alpha*Nh.xy,max(0.0,Nh.z)));
    return Basis*H;
}
fn PathSampleBsdf(S: SurfaceSample, N: vec3f, V: vec3f, Transmission: bool, Rng: ptr<function,u32>) -> PathBsdfSample {
    var Result: PathBsdfSample;
    let P = PathLobeProbabilities(S,max(0.0,dot(N,V)),Transmission);
    let Choice = PathRandom(Rng); let U = PathRandom2(Rng);
    if (Choice >= P.x+P.y+P.z && P.w > 0.0) {
        Result.Direction = -V; Result.Weight = PathTransmission(S,max(0.0,dot(N,V)))/P.w;
        Result.Pdf = P.w; Result.Delta = 1u; return Result;
    }
    if (Choice < P.x) {
        let R = sqrt(U.x); let Phi = 2.0*Pi*U.y;
        Result.Direction = ConstructTangentFrame(N)*vec3f(R*cos(Phi),R*sin(Phi),sqrt(1.0-U.x));
    } else {
        let Roughness = select(S.CoatRoughness,S.Roughness,Choice<P.x+P.y);
        Result.Direction = reflect(-V,PathSampleVndf(N,V,Roughness,U));
    }
    Result.Pdf = PathBsdfPdf(S,N,V,Result.Direction,Transmission);
    if (Result.Pdf > 0.0) { Result.Weight = PathBsdfCos(S,N,V,Result.Direction)/Result.Pdf; }
    return Result;
}
fn PathMis(A: f32, B: f32) -> f32 {
    if (!all(vec2f(A,B)>=vec2f(0.0)) || !all(vec2f(A,B)<vec2f(3.0e37))) { return 0.0; }
    let Scale = max(max(A,B),1.0e-30); let X = A/Scale; let Y = B/Scale;
    return X*X/max(X*X+Y*Y,1.0e-30);
}
fn PathLightKinds() -> vec4u {
    return vec4u(1u,select(0u,1u,any(Frame.SunIlluminance>vec3f(0.0))),select(0u,1u,Path.Scene.w>0u),
                 select(0u,1u,Path.Counts.z>0u || Frame.FireLightIntensity>0.0));
}
fn PathLightKindCount() -> f32 { let K = PathLightKinds(); return f32(K.x+K.y+K.z+K.w); }
fn PathEnvironmentPdf(Direction: vec3f) -> f32 {
    let K = PathLightKinds(); let Sun = select(0.0,1.0/PathSunSolidAngle,K.y>0u && dot(Direction,Frame.SunDirection)>=PathSunCos);
    return (1.0/(4.0*Pi)+Sun)/PathLightKindCount();
}
fn PathEmitterProbability(Instance: u32, Triangle: u32) -> f32 {
    var Low = 0u; var High = Path.Scene.w;
    while (Low < High) {
        let Mid = (Low+High)/2u; let W = Path.Geometry.w+Mid*4u;
        let I = Geometry[W+1u]; let T = Geometry[W];
        if (I<Instance || (I==Instance && T<Triangle)) { Low=Mid+1u; } else { High=Mid; }
    }
    if (Low>=Path.Scene.w) { return 0.0; }
    let W=Path.Geometry.w+Low*4u;
    if (Geometry[W]!=Triangle || Geometry[W+1u]!=Instance) { return 0.0; }
    return bitcast<f32>(Geometry[W+2u]);
}
fn PathEmitterPdf(Hit: PathHit, From: vec3f) -> f32 {
    let Delta = From-Hit.Position; let D2 = dot(Delta,Delta);
    if (!(D2>1.0e-12) || !PathFinite(D2)) { return 0.0; }
    let Cosine = abs(dot(Hit.GeometricNormal,Delta))*inverseSqrt(D2);
    let Area = PathArea(Hit.Triangle,Hit.Instance);
    if (!(Area*Cosine>1.0e-20)) { return 0.0; }
    let Pdf=PathEmitterProbability(Hit.Instance,Hit.Triangle)*D2/(Area*Cosine*PathLightKindCount());
    return select(0.0,Pdf,PathFinite(Pdf));
}
fn PathEmission(Hit: PathHit) -> vec3f {
    var E = Materials[Hit.Family].Emission*Frame.EmissionScale;
    if (Hit.Instance < 5u && Effect.MaskMode != 0u && Effect.MaskMode != 3u) {
        let Local = (Occupants[0].WorldToLocal*vec4f(Hit.Position,1.0)).xyz;
        let Unit=(Local-Effect.BodyMinimum.xyz)/max(Effect.BodyMaximum.xyz-Effect.BodyMinimum.xyz,vec3f(0.001));
        let Seed=u32(Effect.Emitters[0].Centre.w); let Elapsed=Effect.Clock-Effect.MaskStart;
        var Clock=ResolveDerezRelease(Unit,Seed,Effect.Style,1.0)-Elapsed;
        if (Effect.MaskMode==2u) { Clock=Elapsed-ResolveRezArrival(Unit,Seed,Effect.Style,1.0); }
        if (Clock<EffectGlowSeconds) {
            let Glow=Effect.Colour.y*select(1.0,3.0,Clock<EffectHotSeconds);
            E+=Materials[Hit.Family].Albedo*Glow*Frame.EmissionScale;
        }
    }
    return E;
}
fn PathSampleEmitter(Rng: ptr<function,u32>) -> vec2u {
    let U=PathRandom(Rng); var Low=0u; var High=Path.Scene.w-1u;
    while (Low<High) {
        let Mid=(Low+High)/2u;
        if (bitcast<f32>(Geometry[Path.Geometry.w+Mid*4u+3u])<U) { Low=Mid+1u; } else { High=Mid; }
    }
    let W=Path.Geometry.w+Low*4u; return vec2u(Geometry[W],Geometry[W+1u]);
}
fn PathBarycentric(Rng: ptr<function,u32>) -> vec2f {
    var B=PathRandom2(Rng); if (B.x+B.y>1.0) { B=vec2f(1.0)-B; } return B;
}
fn PathNextEvent(Hit: PathHit, V: vec3f, S: SurfaceSample, Transmission: bool, Rng: ptr<function,u32>) -> vec3f {
    let K=PathLightKinds(); let Count=PathLightKindCount(); var Choice=u32(PathRandom(Rng)*Count);
    var Direction=vec3f(0.0); var Distance=1.0e30; var Incoming=vec3f(0.0); var Pdf=0.0;
    var DeltaLight=false;
    if (Choice==0u) {
        Direction=PathUniformSphere(PathRandom2(Rng)); Incoming=PathSky(Direction)+PathSun(Direction); Pdf=PathEnvironmentPdf(Direction);
    } else {
        Choice--;
        if (K.y>0u && Choice==0u) {
            let U=PathRandom2(Rng); let Cosine=mix(1.0,PathSunCos,U.x); let Radius=sqrt(max(0.0,1.0-Cosine*Cosine));
            let Phi=2.0*Pi*U.y;
            Direction=ConstructTangentFrame(Frame.SunDirection)*vec3f(Radius*cos(Phi),Radius*sin(Phi),Cosine);
            Incoming=PathSky(Direction)+PathSun(Direction); Pdf=PathEnvironmentPdf(Direction);
        } else {
            Choice-=K.y;
            if (K.z>0u && Choice==0u) {
                let E=PathSampleEmitter(Rng); let B=PathBarycentric(Rng);
                let Light=PathMakeHit(E.y,E.x,B,Hit.Position-(Occupants[E.y].LocalToWorld*vec4f(PathTriangleAt(E.x).A,1.0)).xyz);
                let Toward=Light.Position-Hit.Position; Distance=length(Toward);
                if (Distance<1.0e-6 || PathMasked(Light.Instance,Light.Position)) { return vec3f(0.0); }
                Direction=Toward/Distance; Incoming=PathEmission(Light); Pdf=PathEmitterPdf(Light,Hit.Position);
                let End=PathOffsetOrigin(Light.Position,Light.GeometricNormal);
                Distance=max(0.0,dot(End-PathOffsetOrigin(Hit.Position,Hit.GeometricNormal),Direction));
            } else {
                let Number=Path.Counts.z+select(0u,1u,Frame.FireLightIntensity>0.0);
                let Index=min(u32(PathRandom(Rng)*f32(Number)),Number-1u);
                var Light: LightRecord;
                if (Index<Path.Counts.z) { Light=SceneLights[Index]; }
                else { Light=LightRecord(vec4f(Frame.FireLightPosition,0.0),vec4f(0.0,0.0,-1.0,Frame.FireLightReach),
                                        vec4f(Frame.FireLightColour,Frame.FireLightIntensity),vec4f(0.0,0.0,0.0,1.0)); }
                let To=Light.PositionType.xyz-Hit.Position; let D2=dot(To,To); Distance=sqrt(D2);
                if (Distance<1.0e-6 || Distance>=Light.DirectionReach.w || Light.Params.w<0.5) { return vec3f(0.0); }
                Direction=To/Distance;
                let Ratio=D2/(Light.DirectionReach.w*Light.DirectionReach.w); let Window=(1.0-Ratio*Ratio)*(1.0-Ratio*Ratio);
                Incoming=Light.ColourIntensity.rgb*(Light.ColourIntensity.w*Window*EvaluateLightIES(Light,-To,Distance)/max(D2,0.36));
                Pdf=1.0/(Count*f32(Number)); DeltaLight=true;
                Distance=max(0.0,Distance-1.0e-4);
            }
        }
    }
    if (Pdf<=0.0 || dot(Hit.GeometricNormal,Direction)<=0.0 || !any(Incoming>vec3f(0.0))) { return vec3f(0.0); }
    var Visibility=PathTransmittance(vec3f(1.0),1.0);
    if (FeatureEnabled(Frame,FeatureDirectShadow)) { Visibility=PathVisibility(PathOffsetOrigin(Hit.Position,Hit.GeometricNormal),Direction,Distance,Hit.Instance,Hit.Triangle); }
    let Bsdf=PathBsdfCos(S,Hit.Normal,V,Direction);
    let Weight=select(PathMis(Pdf,PathBsdfPdf(S,Hit.Normal,V,Direction,Transmission)*Visibility.Probability),1.0,DeltaLight);
    return Bsdf*Incoming*Visibility.Value*(Weight/Pdf);
}
// Radiance at a reconnected second vertex, including its emission with MIS for
// the primary proposal. Each following vertex is traced afresh at the NEW view.
fn PathTraceSuffix(First: PathHit, FirstView: vec3f, PrimaryPdf: f32, Depth: u32, Seed: u32) -> vec3f {
    var Rng=Seed; var Hit=First; var View=FirstView; var Throughput=vec3f(1.0); var Radiance=vec3f(0.0);
    var LinkOrigin=First.Position+FirstView*First.Distance; var LinkPdf=PrimaryPdf;
    var EmissionWeight=PathMis(LinkPdf,PathEmitterPdf(First,LinkOrigin));
    for (var Bounce=Depth; Bounce<Path.Counts.x; Bounce++) {
        let S=PathMaterial(Hit);
        Radiance+=Throughput*PathEmission(Hit)*EmissionWeight;
        if (Bounce+1u>=Path.Counts.x || (!FeatureEnabled(Frame,FeatureIndirect) && Bounce>0u)) { break; }
        Radiance+=Throughput*PathNextEvent(Hit,View,S,true,&Rng);
        let B=PathSampleBsdf(S,Hit.Normal,View,true,&Rng);
        if (!any(B.Weight>vec3f(0.0)) || (B.Delta==0u && dot(Hit.GeometricNormal,B.Direction)<=0.0)) { break; }
        Throughput*=B.Weight;
        if (f32(Bounce)>=Path.Options.x) {
            let Survival=clamp(max(Throughput.x,max(Throughput.y,Throughput.z)),0.05,0.95);
            if (PathRandom(&Rng)>=Survival) { break; } Throughput/=Survival;
        }
        let Side=select(Hit.GeometricNormal,-Hit.GeometricNormal,B.Delta!=0u);
        if (B.Delta!=0u) { LinkPdf*=B.Pdf; } else { LinkOrigin=Hit.Position; LinkPdf=B.Pdf; }
        let Next=PathTraceRay(PathOffsetOrigin(Hit.Position,Side),B.Direction,1.0e30,Hit.Instance,Hit.Triangle);
        if (Next.Instance==PathInvalid) {
            let Weight=PathMis(LinkPdf,PathEnvironmentPdf(B.Direction));
            Radiance+=Throughput*(PathSky(B.Direction)+PathSun(B.Direction))*Weight; break;
        }
        EmissionWeight=PathMis(LinkPdf,PathEmitterPdf(Next,LinkOrigin));
        Hit=Next; View=-B.Direction;
    }
    return PathSafeColour(Radiance);
}
// An independent delta transmission branch at the PRIMARY vertex. It has no
// reconnection PDF, so it must NOT be merged into a continuous GI reservoir.
fn PathPrimaryTransmission(Hit: PathHit, View: vec3f, S: SurfaceSample, Seed: u32) -> vec3f {
    let T=PathTransmission(S,max(0.0,dot(Hit.Normal,View)));
    if (!any(T>vec3f(0.0))) { return vec3f(0.0); }
    let Next=PathTraceRay(PathOffsetOrigin(Hit.Position,-Hit.GeometricNormal),-View,1.0e30,Hit.Instance,Hit.Triangle);
    if (Next.Instance==PathInvalid) { return T*(PathSky(-View)+PathSun(-View)); }
    // A delta predecessor gives unit MIS weight at this next vertex.
    return T*PathTraceSuffix(Next,View,1.0e30,1u,Seed);
}
