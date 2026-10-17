// Real triangle geometry and exact instance transforms. No SDF, screen trace,
// surface card, irradiance cache, or ambient lighting term is used by this path.
//! include "PathRecords.wgsl"
//! include "PackingCodec.wgsl"
//! include "SurfaceReflectance.wgsl"
//! include "SkyProjection.wgsl"
//! include "EffectRecords.wgsl"
//! include "VehicleVisual.wgsl"
//! include "ClusteredLights.wgsl"
@group(1) @binding(0) var<storage,read> Geometry: array<u32>;
@group(1) @binding(1) var<storage,read> Occupants: array<OccupantRecord>;
@group(1) @binding(2) var<storage,read> Materials: array<MaterialRecord>;
@group(1) @binding(3) var<storage,read> SceneLights: array<LightRecord>;
@group(1) @binding(4) var SkyView: texture_2d<f32>;
@group(1) @binding(5) var SkySampler: sampler;
@group(1) @binding(6) var TyreMarks: texture_2d<f32>;
@group(1) @binding(7) var<uniform> Effect: EffectRecord;
@group(1) @binding(8) var<storage,read> VehicleState: VehicleRecord;
@group(1) @binding(9) var<storage,read> PreviousVehicleTriangles: array<u32>;
//! include "RoadMarks.wgsl"
struct PathTriangle { A: vec3f, Family: u32, B: vec3f, C: vec3f, N0: vec3f, N1: vec3f, N2: vec3f, }
struct PathHit {
    Distance: f32, Instance: u32, Triangle: u32, Family: u32,
    Barycentric: vec2f, Position: vec3f, LocalPosition: vec3f, Normal: vec3f, GeometricNormal: vec3f,
}
fn PathVector(Word: u32) -> vec3f { return bitcast<vec3f>(vec3u(Geometry[Word],Geometry[Word+1u],Geometry[Word+2u])); }
fn PathTriangleAt(Index: u32) -> PathTriangle {
    let W = Path.Geometry.x+Index*24u;
    return PathTriangle(PathVector(W),Geometry[W+3u],PathVector(W+4u),PathVector(W+8u),
                        PathVector(W+12u),PathVector(W+16u),PathVector(W+20u));
}
fn PathPreviousLocalPosition(Hit: PathHit) -> vec3f {
    if (Hit.Instance>=5u) { return Hit.LocalPosition; }
    let Word=Hit.Triangle*24u;
    let A=bitcast<vec3f>(vec3u(PreviousVehicleTriangles[Word],PreviousVehicleTriangles[Word+1u],PreviousVehicleTriangles[Word+2u]));
    let B=bitcast<vec3f>(vec3u(PreviousVehicleTriangles[Word+4u],PreviousVehicleTriangles[Word+5u],PreviousVehicleTriangles[Word+6u]));
    let C=bitcast<vec3f>(vec3u(PreviousVehicleTriangles[Word+8u],PreviousVehicleTriangles[Word+9u],PreviousVehicleTriangles[Word+10u]));
    return A*(1.0-Hit.Barycentric.x-Hit.Barycentric.y)+B*Hit.Barycentric.x+C*Hit.Barycentric.y;
}
fn PathInstanceWord(Index: u32) -> u32 { return Path.Geometry.y+Index*8u; }
fn PathOffsetOrigin(P: vec3f, N: vec3f) -> vec3f {
    // Ray Tracing Gems, scale-aware ULP offset (no centimetre-sized global bias).
    let Offset = vec3i(N*64.0);
    let Bits = bitcast<vec3i>(P)+select(Offset,-Offset,P<vec3f(0.0));
    return select(bitcast<vec3f>(Bits),P+N*(1.0/65536.0),abs(P)<vec3f(1.0/32.0));
}
fn PathBox(Origin: vec3f, Direction: vec3f, Low: vec3f, High: vec3f, Limit: f32) -> bool {
    var Near = 0.0; var Far = Limit;
    for (var A = 0u; A < 3u; A++) {
        if (abs(Direction[A]) < 1.0e-30) {
            if (Origin[A] < Low[A] || Origin[A] > High[A]) { return false; }
        } else {
            let T0 = (Low[A]-Origin[A])/Direction[A]; let T1 = (High[A]-Origin[A])/Direction[A];
            Near = max(Near,min(T0,T1)); Far = min(Far,max(T0,T1));
        }
    }
    return Near <= Far;
}
// Watertight shear-space triangle test. No fixed determinant epsilon that drops
// small/distant triangles. Geometric and interpolated normals remain distinct.
fn PathIntersectTriangle(O: vec3f, D: vec3f, T: PathTriangle, Limit: f32) -> vec3f {
    let Abs = abs(D); var Kz = 0u;
    if (Abs.y > Abs.x) { Kz = 1u; } if (Abs.z > Abs[Kz]) { Kz = 2u; }
    var Kx = (Kz+1u)%3u; var Ky = (Kx+1u)%3u;
    if (D[Kz] < 0.0) { let Swap = Kx; Kx = Ky; Ky = Swap; }
    let Sx = D[Kx]/D[Kz]; let Sy = D[Ky]/D[Kz]; let Sz = 1.0/D[Kz];
    let A = T.A-O; let B = T.B-O; let C = T.C-O;
    let Ax = A[Kx]-Sx*A[Kz]; let Ay = A[Ky]-Sy*A[Kz];
    let Bx = B[Kx]-Sx*B[Kz]; let By = B[Ky]-Sy*B[Kz];
    let Cx = C[Kx]-Sx*C[Kz]; let Cy = C[Ky]-Sy*C[Kz];
    let U = Cx*By-Cy*Bx; let V = Ax*Cy-Ay*Cx; let W = Bx*Ay-By*Ax;
    if ((min(U,min(V,W)) < 0.0 && max(U,max(V,W)) > 0.0)) { return vec3f(-1.0,0.0,0.0); }
    let Det = U+V+W;
    if (abs(Det) < 1.0e-30) { return vec3f(-1.0,0.0,0.0); }
    let Az=A[Kz]*Sz; let Bz=B[Kz]*Sz; let Cz=C[Kz]*Sz;
    let Distance = (U*Az+V*Bz+W*Cz)/Det;
    // Forward-error bound from the watertight PBRT triangle test. In particular,
    // giant planar facets near the origin must not self-intersect at a tiny
    // positive t produced by catastrophic cancellation in shear space.
    let MaxZ=max(abs(Az),max(abs(Bz),abs(Cz)));
    let MaxX=max(abs(Ax),max(abs(Bx),abs(Cx))); let MaxY=max(abs(Ay),max(abs(By),abs(Cy)));
    let DeltaZ=1.78814e-7*MaxZ;
    let DeltaX=2.98024e-7*(MaxX+MaxZ); let DeltaY=2.98024e-7*(MaxY+MaxZ);
    let DeltaE=2.0*(1.19210e-7*MaxX*MaxY+DeltaY*MaxX+DeltaX*MaxY);
    let MaxE=max(abs(U),max(abs(V),abs(W)));
    let DeltaT=3.0*(1.78814e-7*MaxE*MaxZ+DeltaE*MaxZ+DeltaZ*MaxE)/abs(Det);
    if (Distance <= DeltaT || Distance >= Limit) { return vec3f(-1.0,0.0,0.0); }
    return vec3f(Distance,V/Det,W/Det);
}
fn PathMasked(Instance: u32, Position: vec3f) -> bool {
    if (Instance >= 5u || Effect.MaskMode == 0u) { return false; }
    if (Effect.MaskMode == 3u) { return true; }
    let P = (Occupants[0].WorldToLocal*vec4f(Position,1.0)).xyz;
    let Unit = (P-Effect.BodyMinimum.xyz)/max(Effect.BodyMaximum.xyz-Effect.BodyMinimum.xyz,vec3f(0.001));
    let Elapsed = Effect.Clock-Effect.MaskStart; let Seed = u32(Effect.Emitters[0].Centre.w);
    if (Effect.MaskMode == 1u) { return Elapsed >= ResolveDerezRelease(Unit,Seed,Effect.Style,1.0); }
    return Elapsed < ResolveRezArrival(Unit,Seed,Effect.Style,1.0);
}
fn PathMakeHit(Instance: u32, Triangle: u32, Bary: vec2f, View: vec3f) -> PathHit {
    let T = PathTriangleAt(Triangle); let O = Occupants[Instance];
    let Local = T.A*(1.0-Bary.x-Bary.y)+T.B*Bary.x+T.C*Bary.y;
    let P = (O.LocalToWorld*vec4f(Local,1.0)).xyz;
    let InverseTranspose = transpose(O.WorldToLocal);
    var Ng = normalize((InverseTranspose*vec4f(cross(T.B-T.A,T.C-T.A),0.0)).xyz);
    let Interpolated = T.N0*(1.0-Bary.x-Bary.y)+T.N1*Bary.x+T.N2*Bary.y;
    let WorldNormal=(InverseTranspose*vec4f(Interpolated,0.0)).xyz;
    var N=Ng;
    if (dot(WorldNormal,WorldNormal)>1.0e-20) { N=normalize(WorldNormal); }
    Ng = select(-Ng,Ng,dot(Ng,View)>=0.0); N = select(-N,N,dot(N,Ng)>=0.0);
    if (dot(N,View) <= 0.001 || dot(N,Ng) < 0.17) { N = Ng; }
    let Family = select(T.Family,O.MaterialOverride,O.MaterialOverride!=NoMaterialOverride);
    return PathHit(0.0,Instance,Triangle,Family,Bary,P,Local,N,Ng);
}
fn PathTraceRay(Origin: vec3f, Direction: vec3f, Limit: f32, SkipInstance: u32, SkipTriangle: u32) -> PathHit {
    var Hit: PathHit; Hit.Distance = Limit; Hit.Instance = PathInvalid; Hit.Triangle = PathInvalid;
    var Node = Path.Scene.x;
    // Threaded traversal visits each node at most once. End sentinels, not an
    // arbitrary iteration/stack budget, terminate BOTH levels of the hierarchy.
    while (Node < Path.Scene.y) {
        let W = Node*8u; let Link = Geometry[W+3u]; let Count = Geometry[W+7u];
        if (!PathBox(Origin,Direction,PathVector(W),PathVector(W+4u),Hit.Distance)) {
            Node = select(Link,Node+1u,Count>0u); continue;
        }
        if (Count == 0u) { Node++; continue; }
        for (var I = 0u; I < Count; I++) {
            let Instance = Geometry[Path.Geometry.z+(Link+I)*2u]; let IW = PathInstanceWord(Instance);
            let Transform = Occupants[Instance].WorldToLocal;
            let O = (Transform*vec4f(Origin,1.0)).xyz; let D = (Transform*vec4f(Direction,0.0)).xyz;
            // D is NOT normalized: t remains in world-ray units under scaling.
            var B = Geometry[IW]; let End = Geometry[IW+1u];
            while (B < End) {
                let BW = B*8u; let BL = Geometry[BW+3u]; let BC = Geometry[BW+7u];
                if (!PathBox(O,D,PathVector(BW),PathVector(BW+4u),Hit.Distance)) { B = select(BL,B+1u,BC>0u); continue; }
                if (BC == 0u) { B++; continue; }
                for (var J = 0u; J < BC; J++) {
                    if (Instance==SkipInstance && BL+J==SkipTriangle) { continue; }
                    let Test = PathIntersectTriangle(O,D,PathTriangleAt(BL+J),Hit.Distance);
                    if (Test.x <= 0.0) { continue; }
                    let Position = Origin+Direction*Test.x;
                    if (PathMasked(Instance,Position)) { continue; }
                    Hit.Distance = Test.x; Hit.Instance = Instance; Hit.Triangle = BL+J; Hit.Barycentric = Test.yz;
                }
                B++;
            }
        }
        Node++;
    }
    if (Hit.Instance != PathInvalid) {
        let Distance = Hit.Distance; Hit = PathMakeHit(Hit.Instance,Hit.Triangle,Hit.Barycentric,-Direction); Hit.Distance = Distance;
    }
    return Hit;
}
fn PathTrace(Origin: vec3f, Direction: vec3f, Limit: f32) -> PathHit {
    return PathTraceRay(Origin,Direction,Limit,PathInvalid,PathInvalid);
}
fn PathArea(Triangle: u32, Instance: u32) -> f32 {
    let T = PathTriangleAt(Triangle); let O = Occupants[Instance].LocalToWorld;
    let U = (O*vec4f(T.B-T.A,0.0)).xyz; let V = (O*vec4f(T.C-T.A,0.0)).xyz;
    return 0.5*length(cross(U,V));
}
fn PathMaterial(Hit: PathHit) -> SurfaceSample {
    var S = ProjectSurfaceSample(Materials[Hit.Family]);
    if (Hit.Family == VehicleGlassFamily) {
        S.Transmission = select(0.0,Path.Display.z,VehicleState.Cabin.y > 0.5);
    }
    let Marked = (Hit.Family>=MarkedFamilyFirst && Hit.Family<=MarkedFamilyLast)
        || (Hit.Family>=MarkedFamilyExtraFirst && Hit.Family<=MarkedFamilyExtraLast);
    if (Marked) {
        let Rubber = ProjectSurfaceSample(Materials[TyreMarkFamily]); let Metal = ProjectSurfaceSample(Materials[RimMarkFamily]);
        let Deposit = SampleRoadMark(Hit.Position.xy,0.005,false); let Scrape = SampleRoadMark(Hit.Position.xy,0.005,true);
        S.Albedo = mix(mix(S.Albedo,Rubber.Albedo,Deposit),Metal.Albedo,Scrape);
        S.Roughness = mix(mix(S.Roughness,Rubber.Roughness,Deposit),Metal.Roughness,Scrape);
        S.Metalness = mix(S.Metalness,Metal.Metalness,Scrape);
    }
    return S;
}
fn PathSky(Direction: vec3f) -> vec3f {
    return max(vec3f(0.0),textureSampleLevel(SkyView,SkySampler,ProjectSkyCoordinates(Direction),0.0).rgb*Frame.SkyScale+Frame.NightSkyRadiance);
}
const PathSunCos: f32 = 0.9999888967; // 0.27-degree angular radius
const PathSunSolidAngle: f32 = 0.000069762;
fn PathSun(Direction: vec3f) -> vec3f {
    return select(vec3f(0.0),Frame.SunIlluminance/PathSunSolidAngle,dot(Direction,Frame.SunDirection)>=PathSunCos);
}
// Thin-sheet coloured transmittance (there is no volume/thickness/IOR record in
// the authored MaterialRecord). Reflections are sampled by the GGX dielectric.
// A disabled specular lobe is genuinely disabled, including at grazing angles.
// Use the same convention for reflection, lobe probabilities and transmission.
fn PathFresnel(F0: vec3f, Cosine: f32) -> vec3f {
    return select(vec3f(0.0),ResolveFresnel(F0,Cosine),any(F0>vec3f(0.0)));
}
fn PathTransmission(S: SurfaceSample, NdotV: f32) -> vec3f {
    return mix(vec3f(1.0),S.Albedo,0.12)*S.Transmission*(1.0-PathFresnel(vec3f(S.SpecularLevel),NdotV));
}
struct PathTransmittance { Value: vec3f, Probability: f32, }
fn PathVisibility(Origin: vec3f, Direction: vec3f, Distance: f32, Instance: u32, Triangle: u32) -> PathTransmittance {
    var O = Origin; var Remaining = Distance; var Visibility = vec3f(1.0); var Probability = 1.0;
    var SkipInstance=Instance; var SkipTriangle=Triangle;
    // Transmittance follows every intervening thin sheet. A finite segment loses
    // positive distance each iteration; opaque blockers terminate immediately.
    loop {
        let Hit = PathTraceRay(O,Direction,Remaining,SkipInstance,SkipTriangle);
        if (Hit.Instance == PathInvalid) { return PathTransmittance(Visibility,Probability); }
        let S = PathMaterial(Hit);
        if (S.Transmission <= 0.0) { return PathTransmittance(vec3f(0.0),0.0); }
        Visibility *= PathTransmission(S,abs(dot(Hit.Normal,-Direction)));
        Probability *= PathLobeProbabilities(S,abs(dot(Hit.Normal,-Direction)),true).w;
        if (!any(Visibility > vec3f(0.0))) { return PathTransmittance(vec3f(0.0),0.0); }
        let Next = PathOffsetOrigin(Hit.Position,-Hit.GeometricNormal);
        let Advance = dot(Next-O,Direction);
        if (Advance <= 0.0) { return PathTransmittance(vec3f(0.0),0.0); }
        Remaining -= Advance;
        if (Remaining <= 0.0) { return PathTransmittance(Visibility,Probability); }
        O = Next; SkipInstance=Hit.Instance; SkipTriangle=Hit.Triangle;
    }
}
