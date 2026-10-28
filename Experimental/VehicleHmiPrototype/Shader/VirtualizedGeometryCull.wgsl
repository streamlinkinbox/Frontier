//============================================================================================================================================
//                                                      VIRTUALIZEDGEOMETRYCULL.WGSL
//============================================================================================================================================
// 🧩 Parallel GPU DAG cut (SelfErrorPx <= Threshold < ParentErrorPx with refraction-deviation error for glass),
//    frustum + normal-cone backface culling, and two-phase Reversed-Z Hi-Z occlusion culling into indirect draws.

//! include "FrameDescriptor.wgsl"

struct HierarchyClusterRecord
{
    TightMinimum : vec3f,
    Occupant     : u32,
    TightMaximum : vec3f,
    FirstIndex   : u32,
    LodSphere    : vec4f,   // xyz center, w radius
    ParentSphere : vec4f,   // xyz center, w radius
    Cone         : vec4f,   // xyz axis, w cutoff cosine
    Error        : f32,
    ParentError  : f32,
    IndexCount   : u32,
    Flags        : u32,     // bit0 opaque, bit1 transmissive, bit2 backfaceConeCull, bits 8..15 level
}

struct IndirectDrawCommand
{
    IndexCount    : u32,
    InstanceCount : u32,
    FirstIndex    : u32,
    BaseVertex    : u32,
    FirstInstance : u32,
}

@group(0) @binding(1) var<storage, read>       ClusterOccupants : array<OccupantRecord>;
@group(0) @binding(2) var<storage, read>       Clusters         : array<HierarchyClusterRecord>;
@group(0) @binding(3) var<storage, read_write> Phase1Draws      : array<IndirectDrawCommand>;
@group(0) @binding(4) var<storage, read_write> Phase2Draws      : array<IndirectDrawCommand>;
@group(0) @binding(5) var<storage, read_write> ClusterStates    : array<u32>;   // 0=not-cut/culled, 1=phase1-vis, 2=phase2-retry, 3=phase2-disoccluded, 4=occluded
@group(0) @binding(6) var                      HiZPyramid       : texture_2d<f32>;

const PixelErrorThreshold : f32 = 1.0;
const NearPlaneClamp      : f32 = 0.1;
const GlassRefractionEta  : f32 = 1.52;

fn ExtractWorldScale(M : mat4x4f) -> f32
{
    let SX = length(M[0].xyz);
    let SY = length(M[1].xyz);
    let SZ = length(M[2].xyz);
    return max(max(SX, SY), max(SZ, 1.0e-6));
}

fn EvaluateSphereErrorPx(Sphere : vec4f, ObjectError : f32, LocalToWorld : mat4x4f, Flags : u32, Cone : vec4f) -> f32
{
    if (ObjectError <= 0.0) { return 0.0; }
    if (ObjectError > 1.0e20) { return 1.0e30; }
    let Scale = ExtractWorldScale(LocalToWorld);
    let WorldCenter = (LocalToWorld * vec4f(Sphere.xyz, 1.0)).xyz;
    let WorldRadius = Scale * Sphere.w;
    let Distance = max(length(WorldCenter - Frame.CameraPosition) - WorldRadius, NearPlaneClamp);
    var Effective = Scale * ObjectError;
    if ((Flags & 2u) != 0u)
    {
        let Cutoff = clamp(Cone.w, -1.0, 1.0);
        let Spread = sqrt(max(0.0, 1.0 - Cutoff * Cutoff));
        Effective = Effective * (1.0 + (GlassRefractionEta - 1.0) * Spread);
    }
    let ProjectionScale = max(abs(Frame.UnjitteredViewProjection[1][1]), 1.0) * Frame.ScreenExtent.y * 0.5;
    return (Effective / Distance) * ProjectionScale;
}

fn TestFrustum(C : HierarchyClusterRecord, M : mat4x4f) -> bool
{
    var Outside = array<u32, 6>(0u, 0u, 0u, 0u, 0u, 0u);
    for (var K = 0u; K < 8u; K = K + 1u)
    {
        let P = vec3f(
            select(C.TightMinimum.x, C.TightMaximum.x, (K & 1u) != 0u),
            select(C.TightMinimum.y, C.TightMaximum.y, (K & 2u) != 0u),
            select(C.TightMinimum.z, C.TightMaximum.z, (K & 4u) != 0u)
        );
        let Q = M * vec4f(P, 1.0);
        Outside[0] += select(0u, 1u, Q.x < -Q.w);
        Outside[1] += select(0u, 1u, Q.x >  Q.w);
        Outside[2] += select(0u, 1u, Q.y < -Q.w);
        Outside[3] += select(0u, 1u, Q.y >  Q.w);
        Outside[4] += select(0u, 1u, Q.z <  0.0);
        Outside[5] += select(0u, 1u, Q.z >  Q.w);
    }
    for (var Plane = 0u; Plane < 6u; Plane = Plane + 1u)
    {
        if (Outside[Plane] == 8u) { return false; }
    }
    return true;
}

fn TestNormalCone(C : HierarchyClusterRecord, LocalToWorld : mat4x4f) -> bool
{
    if ((C.Flags & 4u) == 0u || (C.Flags & 2u) != 0u) { return true; }
    let Cutoff = C.Cone.w;
    if (Cutoff <= 0.0) { return true; }
    let Scale = ExtractWorldScale(LocalToWorld);
    let Center = (LocalToWorld * vec4f(C.LodSphere.xyz, 1.0)).xyz;
    let Radius = Scale * C.LodSphere.w;
    let Axis = normalize((LocalToWorld * vec4f(C.Cone.xyz, 0.0)).xyz);
    let ToCenter = Center - Frame.CameraPosition;
    let Distance = length(ToCenter);
    if (Distance <= Radius + 1.0e-5) { return true; }
    let SinAlpha = sqrt(max(0.0, 1.0 - Cutoff * Cutoff));
    let SinBeta  = min(1.0, Radius / Distance);
    let CosBeta  = sqrt(max(0.0, 1.0 - SinBeta * SinBeta));
    let ViewDotAxis = dot(ToCenter / Distance, Axis);
    return ViewDotAxis <= (SinAlpha * CosBeta + Cutoff * SinBeta);
}

fn TestHiZOcclusion(C : HierarchyClusterRecord, M : mat4x4f) -> bool
{
    let BaseExtent = vec2f(textureDimensions(HiZPyramid, 0));
    if (BaseExtent.x <= 1.0 && BaseExtent.y <= 1.0) { return true; }
    var MinScreen = vec2f(1.0e20, 1.0e20);
    var MaxScreen = vec2f(-1.0e20, -1.0e20);
    var ClosestDepth = -1.0e20;
    for (var K = 0u; K < 8u; K = K + 1u)
    {
        let P = vec3f(
            select(C.TightMinimum.x, C.TightMaximum.x, (K & 1u) != 0u),
            select(C.TightMinimum.y, C.TightMaximum.y, (K & 2u) != 0u),
            select(C.TightMinimum.z, C.TightMaximum.z, (K & 4u) != 0u)
        );
        let Q = M * vec4f(P, 1.0);
        if (Q.w <= 1.0e-4) { return true; }   // crosses near plane: conservatively visible
        let Ndc = Q.xyz / Q.w;
        let Screen = vec2f(Ndc.x * 0.5 + 0.5, 0.5 - Ndc.y * 0.5) * BaseExtent;
        MinScreen = min(MinScreen, Screen);
        MaxScreen = max(MaxScreen, Screen);
        ClosestDepth = max(ClosestDepth, Ndc.z);
    }
    if (MaxScreen.x < 0.0 || MinScreen.x >= BaseExtent.x || MaxScreen.y < 0.0 || MinScreen.y >= BaseExtent.y)
    {
        return false;
    }
    let ClampedMin = clamp(MinScreen, vec2f(0.0), BaseExtent - vec2f(1.0));
    let ClampedMax = clamp(MaxScreen, vec2f(0.0), BaseExtent - vec2f(1.0));
    let Footprint = max(max(ClampedMax.x - ClampedMin.x, ClampedMax.y - ClampedMin.y), 1.0);
    let MipCount = textureNumLevels(HiZPyramid);
    let Mip = min(MipCount - 1u, u32(ceil(log2(Footprint))));
    let MipExtent = vec2f(textureDimensions(HiZPyramid, Mip));
    let Scale = MipExtent / BaseExtent;
    let X0 = i32(clamp(floor(ClampedMin.x * Scale.x), 0.0, MipExtent.x - 1.0));
    let X1 = i32(clamp(floor(ClampedMax.x * Scale.x), 0.0, MipExtent.x - 1.0));
    let Y0 = i32(clamp(floor(ClampedMin.y * Scale.y), 0.0, MipExtent.y - 1.0));
    let Y1 = i32(clamp(floor(ClampedMax.y * Scale.y), 0.0, MipExtent.y - 1.0));
    let D00 = textureLoad(HiZPyramid, vec2i(X0, Y0), i32(Mip)).x;
    let D10 = textureLoad(HiZPyramid, vec2i(X1, Y0), i32(Mip)).x;
    let D01 = textureLoad(HiZPyramid, vec2i(X0, Y1), i32(Mip)).x;
    let D11 = textureLoad(HiZPyramid, vec2i(X1, Y1), i32(Mip)).x;
    let OccluderDepth = min(min(D00, D10), min(D01, D11));
    return clamp(ClosestDepth, 0.0, 1.0) + 1.0e-5 >= OccluderDepth;
}

@compute @workgroup_size(64)
fn SelectAndCullPhase1(@builtin(global_invocation_id) ID : vec3u)
{
    let I = ID.x;
    if (I >= arrayLength(&Clusters)) { return; }
    let C = Clusters[I];
    // 📝 Page streaming: a cluster whose page is not resident in its arena slot draws nothing; the cut falls
    //    back to its resident ancestors because their records still evaluate normally.
    if ((C.Flags & 8u) == 0u)
    {
        ClusterStates[I] = 0u;
        Phase1Draws[I] = IndirectDrawCommand(C.IndexCount, 0u, C.FirstIndex, 0u, 0u);
        Phase2Draws[I] = IndirectDrawCommand(C.IndexCount, 0u, C.FirstIndex, 0u, 0u);
        return;
    }
    let Occupant = ClusterOccupants[C.Occupant];

    let SelfErrorPx   = EvaluateSphereErrorPx(C.LodSphere, C.Error, Occupant.LocalToWorld, C.Flags, C.Cone);
    let ParentErrorPx = EvaluateSphereErrorPx(C.ParentSphere, C.ParentError, Occupant.LocalToWorld, C.Flags, C.Cone);
    let OnCut = SelfErrorPx <= PixelErrorThreshold && ParentErrorPx > PixelErrorThreshold;
    if (!OnCut)
    {
        ClusterStates[I] = 0u;
        Phase1Draws[I] = IndirectDrawCommand(C.IndexCount, 0u, C.FirstIndex, 0u, 0u);
        Phase2Draws[I] = IndirectDrawCommand(C.IndexCount, 0u, C.FirstIndex, 0u, 0u);
        return;
    }

    let CurrentM = Frame.ViewProjection * Occupant.LocalToWorld;
    if (!TestFrustum(C, CurrentM) || !TestNormalCone(C, Occupant.LocalToWorld))
    {
        ClusterStates[I] = 0u;
        Phase1Draws[I] = IndirectDrawCommand(C.IndexCount, 0u, C.FirstIndex, 0u, 0u);
        Phase2Draws[I] = IndirectDrawCommand(C.IndexCount, 0u, C.FirstIndex, 0u, 0u);
        return;
    }

    let PreviousM = Frame.PreviousUnjitteredViewProjection * Occupant.PreviousLocalToWorld;
    let VisibleInPhase1 = Frame.FrameIndex == 0u || TestHiZOcclusion(C, PreviousM);
    ClusterStates[I] = select(2u, 1u, VisibleInPhase1);
    Phase1Draws[I]   = IndirectDrawCommand(C.IndexCount, select(0u, 1u, VisibleInPhase1), C.FirstIndex, 0u, 0u);
    Phase2Draws[I]   = IndirectDrawCommand(C.IndexCount, 0u, C.FirstIndex, 0u, 0u);
}

@compute @workgroup_size(64)
fn CullPhase2Disoccluded(@builtin(global_invocation_id) ID : vec3u)
{
    let I = ID.x;
    if (I >= arrayLength(&Clusters)) { return; }
    if (ClusterStates[I] != 2u) { return; }
    let C = Clusters[I];
    let Occupant = ClusterOccupants[C.Occupant];
    let CurrentM = Frame.ViewProjection * Occupant.LocalToWorld;
    let Disoccluded = TestHiZOcclusion(C, CurrentM);
    ClusterStates[I] = select(4u, 3u, Disoccluded);
    Phase2Draws[I]   = IndirectDrawCommand(C.IndexCount, select(0u, 1u, Disoccluded), C.FirstIndex, 0u, 0u);
}
