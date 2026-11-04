//============================================================================================================================================
//                                                     VIRTUALIZEDGEOMETRYCULL.SLANG
//============================================================================================================================================
// 🧩 Parallel GPU cut (own error ≤ threshold < coarser error, refraction deviation for glass), frustum and normal-cone culling and
//    two-phase reversed-Z Hi-Z occlusion culling into indirect commands (VirtualizedGeometryCull.wgsl, statement for statement).

#pragma once
#include "IntrinsicParity.slang"
#include "HostConstants.slang"
#include "RefreshDescriptor.slang"

// 📝 Flags: bit 0 opaque, bit 1 transmissive, bit 2 normal-cone cull, bit 3 resident, bits 8..15 tier (NestingProjection.cpp).
struct NestedClusterProfile
{
    float3 TightMinimum;     // [m]
    uint   Occupant;         // [-]
    float3 TightMaximum;     // [m]
    uint   FirstIndex;       // [-]
    float4 LodSphere;        // [m] xyz centre, w radius
    float4 CoarserSphere;    // [m] xyz centre, w radius
    float4 Cone;             // [-] xyz axis, w cutoff cosine
    float  Error;            // [m]
    float  CoarserError;     // [m]
    uint   IndexCount;       // [-]
    uint   Flags;            // [-]
};

struct ClusterCommand
{
    uint IndexCount;
    uint InstanceCount;
    uint FirstIndex;
    uint BaseVertex;
    uint FirstInstance;
};

  [[vk::binding(1, 0)]] StructuredBuffer<OccupantProfile>        ClusterOccupants;
  [[vk::binding(2, 0)]] StructuredBuffer<NestedClusterProfile>   Clusters;
  [[vk::binding(3, 0)]] RWStructuredBuffer<ClusterCommand>       Phase1Commands;
  [[vk::binding(4, 0)]] RWStructuredBuffer<ClusterCommand>       Phase2Commands;
  [[vk::binding(5, 0)]] RWStructuredBuffer<uint>                 ClusterVerdicts;   // 0 not cut or culled, 1 phase-1 visible, 2 retry,
                                                                                     // 3 phase-2 disoccluded, 4 occluded
  [[vk::binding(6, 0)]] Texture2D<float>                         HiZPyramid;
// 📝 I1: four command lists per refresh, [phase-1 opaque | phase-1 glass | phase-2 opaque | phase-2 glass], each ArrayCount(Clusters)
//    long, the occupant in FirstInstance: compacted (appended, counts zeroed by the host before phase 1) for one indirect-count draw
//    per list, and by record (culled records keep zero instances) for one multi-draw per list where drawIndirectCount is absent.
  [[vk::binding(7, 0)]] RWStructuredBuffer<ClusterCommand>       CompactedCommands;
  [[vk::binding(8, 0)]] RWStructuredBuffer<uint>                 CompactedCounts;
  [[vk::binding(9, 0)]] RWStructuredBuffer<ClusterCommand>       ClassCommands;

static const float PixelErrorThreshold = 1.0;    // [px]
static const float NearPlaneClamp      = 0.1;    // [m]
static const float GlassRefractionEta  = 1.52;   // [-]

uint CountTextureLevels(Texture2D<float> Texture) { uint Width; uint Height; uint Levels; Texture.GetDimensions(0, Width, Height, Levels); return Levels; }

float ExtractWorldScale(float4x4 M)
{
    let SX = length(M[0].xyz);
    let SY = length(M[1].xyz);
    let SZ = length(M[2].xyz);
    return max(max(SX, SY), max(SZ, 1.0e-6));
}

float EvaluateSphereErrorPx(float4 Sphere, float ObjectError, float4x4 LocalToWorld, uint Flags, float4 Cone)
{
    if (ObjectError <= 0.0) { return 0.0; }
    if (ObjectError > 1.0e20) { return 1.0e30; }
    let Scale = ExtractWorldScale(LocalToWorld);
    let WorldCenter = (mul(float4(Sphere.xyz, 1.0), LocalToWorld)).xyz;
    let WorldRadius = Scale * Sphere.w;
    let Distance = max(length(WorldCenter - Refresh.CameraPosition) - WorldRadius, NearPlaneClamp);
    var Effective = Scale * ObjectError;
    if ((Flags & 2u) != 0u)
    {
        let Cutoff = clamp(Cone.w, -1.0, 1.0);
        let Spread = sqrt(max(0.0, 1.0 - Cutoff * Cutoff));
        Effective = Effective * (1.0 + (GlassRefractionEta - 1.0) * Spread);
    }
    let ProjectionScale = max(abs(Refresh.UnjitteredViewProjection[1][1]), 1.0) * Refresh.ScreenExtent.y * 0.5;
    return (Effective / Distance) * ProjectionScale;
}

bool TestFrustum(NestedClusterProfile C, float4x4 M)
{
    var Outside : uint[6] = { 0u, 0u, 0u, 0u, 0u, 0u };
    for (var K = 0u; K < 8u; K = K + 1u)
    {
        let P = float3(SelectBranch(C.TightMinimum.x, C.TightMaximum.x, (K & 1u) != 0u),
                       SelectBranch(C.TightMinimum.y, C.TightMaximum.y, (K & 2u) != 0u),
                       SelectBranch(C.TightMinimum.z, C.TightMaximum.z, (K & 4u) != 0u));
        let Q = mul(float4(P, 1.0), M);
        Outside[0] += SelectBranch(0u, 1u, Q.x < -Q.w);
        Outside[1] += SelectBranch(0u, 1u, Q.x >  Q.w);
        Outside[2] += SelectBranch(0u, 1u, Q.y < -Q.w);
        Outside[3] += SelectBranch(0u, 1u, Q.y >  Q.w);
        Outside[4] += SelectBranch(0u, 1u, Q.z <  0.0);
        Outside[5] += SelectBranch(0u, 1u, Q.z >  Q.w);
    }
    for (var Plane = 0u; Plane < 6u; Plane = Plane + 1u)
    {
        if (Outside[Plane] == 8u) { return false; }
    }
    return true;
}

bool TestNormalCone(NestedClusterProfile C, float4x4 LocalToWorld)
{
    if ((C.Flags & 4u) == 0u || (C.Flags & 2u) != 0u) { return true; }
    let Cutoff = C.Cone.w;
    if (Cutoff <= 0.0) { return true; }
    let Scale = ExtractWorldScale(LocalToWorld);
    let Center = (mul(float4(C.LodSphere.xyz, 1.0), LocalToWorld)).xyz;
    let Radius = Scale * C.LodSphere.w;
    let Axis = normalize((mul(float4(C.Cone.xyz, 0.0), LocalToWorld)).xyz);
    let ToCenter = Center - Refresh.CameraPosition;
    let Distance = length(ToCenter);
    if (Distance <= Radius + 1.0e-5) { return true; }
    let SinAlpha = sqrt(max(0.0, 1.0 - Cutoff * Cutoff));
    let SinBeta  = min(1.0, Radius / Distance);
    let CosBeta  = sqrt(max(0.0, 1.0 - SinBeta * SinBeta));
    let ViewDotAxis = dot(ToCenter / Distance, Axis);
    return ViewDotAxis <= (SinAlpha * CosBeta + Cutoff * SinBeta);
}

bool TestHiZOcclusion(NestedClusterProfile C, float4x4 M)
{
    let BaseExtent = float2(TextureExtent(HiZPyramid, 0));
    if (BaseExtent.x <= 1.0 && BaseExtent.y <= 1.0) { return true; }
    var MinScreen = float2(1.0e20, 1.0e20);
    var MaxScreen = float2(-1.0e20, -1.0e20);
    var ClosestDepth = -1.0e20;
    for (var K = 0u; K < 8u; K = K + 1u)
    {
        let P = float3(SelectBranch(C.TightMinimum.x, C.TightMaximum.x, (K & 1u) != 0u),
                       SelectBranch(C.TightMinimum.y, C.TightMaximum.y, (K & 2u) != 0u),
                       SelectBranch(C.TightMinimum.z, C.TightMaximum.z, (K & 4u) != 0u));
        let Q = mul(float4(P, 1.0), M);
        if (Q.w <= 1.0e-4) { return true; }   // crosses the near plane: conservatively visible
        let Ndc = Q.xyz / Q.w;
        let Screen = float2(Ndc.x * 0.5 + 0.5, 0.5 - Ndc.y * 0.5) * BaseExtent;
        MinScreen = min(MinScreen, Screen);
        MaxScreen = max(MaxScreen, Screen);
        ClosestDepth = max(ClosestDepth, Ndc.z);
    }
    if (MaxScreen.x < 0.0 || MinScreen.x >= BaseExtent.x || MaxScreen.y < 0.0 || MinScreen.y >= BaseExtent.y)
    {
        return false;
    }
    let ClampedMin = clamp(MinScreen, float2(0.0), BaseExtent - float2(1.0));
    let ClampedMax = clamp(MaxScreen, float2(0.0), BaseExtent - float2(1.0));
    let Footprint = max(max(ClampedMax.x - ClampedMin.x, ClampedMax.y - ClampedMin.y), 1.0);
    let MipCount = CountTextureLevels(HiZPyramid);
    let Mip = min(MipCount - 1u, ToUnsigned(ceil(log2(Footprint))));
    let MipExtent = float2(TextureExtent(HiZPyramid, Mip));
    let Scale = MipExtent / BaseExtent;
    let X0 = ToSigned(clamp(floor(ClampedMin.x * Scale.x), 0.0, MipExtent.x - 1.0));
    let X1 = ToSigned(clamp(floor(ClampedMax.x * Scale.x), 0.0, MipExtent.x - 1.0));
    let Y0 = ToSigned(clamp(floor(ClampedMin.y * Scale.y), 0.0, MipExtent.y - 1.0));
    let Y1 = ToSigned(clamp(floor(ClampedMax.y * Scale.y), 0.0, MipExtent.y - 1.0));
    let D00 = LoadTexel(HiZPyramid, int2(X0, Y0), ToSigned(Mip));
    let D10 = LoadTexel(HiZPyramid, int2(X1, Y0), ToSigned(Mip));
    let D01 = LoadTexel(HiZPyramid, int2(X0, Y1), ToSigned(Mip));
    let D11 = LoadTexel(HiZPyramid, int2(X1, Y1), ToSigned(Mip));
    let OccluderDepth = min(min(D00, D10), min(D01, D11));
    return clamp(ClosestDepth, 0.0, 1.0) + 1.0e-5 >= OccluderDepth;
}

void SelectAndCullPhase1Body(uint3 ID)
{
    let I = ID.x;
    if (I >= ArrayCount(Clusters)) { return; }
    let C = Clusters[I];
    // 📝 Page streaming: a cluster whose page is not resident in its arena slot draws nothing; the cut falls back to its resident
    //    coarser clusters because their profiles still evaluate normally.
    if ((C.Flags & 8u) == 0u)
    {
        ClusterVerdicts[I] = 0u;
        Phase1Commands[I] = ClusterCommand(C.IndexCount, 0u, C.FirstIndex, 0u, 0u);
        Phase2Commands[I] = ClusterCommand(C.IndexCount, 0u, C.FirstIndex, 0u, 0u);
        return;
    }
    let Occupant = ClusterOccupants[C.Occupant];

    let OwnErrorPx     = EvaluateSphereErrorPx(C.LodSphere, C.Error, Occupant.LocalToWorld, C.Flags, C.Cone);
    let CoarserErrorPx = EvaluateSphereErrorPx(C.CoarserSphere, C.CoarserError, Occupant.LocalToWorld, C.Flags, C.Cone);
    let OnCut = OwnErrorPx <= PixelErrorThreshold && CoarserErrorPx > PixelErrorThreshold;
    if (!OnCut)
    {
        ClusterVerdicts[I] = 0u;
        Phase1Commands[I] = ClusterCommand(C.IndexCount, 0u, C.FirstIndex, 0u, 0u);
        Phase2Commands[I] = ClusterCommand(C.IndexCount, 0u, C.FirstIndex, 0u, 0u);
        return;
    }

    let CurrentM = mul(Occupant.LocalToWorld, Refresh.ViewProjection);
    if (!TestFrustum(C, CurrentM) || !TestNormalCone(C, Occupant.LocalToWorld))
    {
        ClusterVerdicts[I] = 0u;
        Phase1Commands[I] = ClusterCommand(C.IndexCount, 0u, C.FirstIndex, 0u, 0u);
        Phase2Commands[I] = ClusterCommand(C.IndexCount, 0u, C.FirstIndex, 0u, 0u);
        return;
    }

    let PreviousM = mul(Occupant.PreviousLocalToWorld, Refresh.PreviousUnjitteredViewProjection);
    let VisibleInPhase1 = Refresh.RefreshIndex == 0u || TestHiZOcclusion(C, PreviousM);
    ClusterVerdicts[I] = SelectBranch(2u, 1u, VisibleInPhase1);
    Phase1Commands[I]  = ClusterCommand(C.IndexCount, SelectBranch(0u, 1u, VisibleInPhase1), C.FirstIndex, 0u, 0u);
    Phase2Commands[I]  = ClusterCommand(C.IndexCount, 0u, C.FirstIndex, 0u, 0u);
}

void CullPhase2DisoccludedBody(uint3 ID)
{
    let I = ID.x;
    if (I >= ArrayCount(Clusters)) { return; }
    if (ClusterVerdicts[I] != 2u) { return; }
    let C = Clusters[I];
    let Occupant = ClusterOccupants[C.Occupant];
    let CurrentM = mul(Occupant.LocalToWorld, Refresh.ViewProjection);
    let Disoccluded = TestHiZOcclusion(C, CurrentM);
    ClusterVerdicts[I] = SelectBranch(4u, 3u, Disoccluded);
    Phase2Commands[I]  = ClusterCommand(C.IndexCount, SelectBranch(0u, 1u, Disoccluded), C.FirstIndex, 0u, 0u);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void SelectAndCullPhase1(uint3 ID : SV_DispatchThreadID) { SelectAndCullPhase1Body(ID); }

[shader("compute")]
[numthreads(64, 1, 1)]
void CullPhase2Disoccluded(uint3 ID : SV_DispatchThreadID) { CullPhase2DisoccludedBody(ID); }

void PlaceCommand(uint List, uint I, NestedClusterProfile C, bool Visible)
{
    ClassCommands[List * ArrayCount(Clusters) + I] = ClusterCommand(C.IndexCount, SelectBranch(0u, 1u, Visible), C.FirstIndex, 0u, C.Occupant);
    if (!Visible) { return; }
    var Slot = 0u;
    InterlockedAdd(CompactedCounts[List], 1u, Slot);
    CompactedCommands[List * ArrayCount(Clusters) + Slot] = ClusterCommand(C.IndexCount, 1u, C.FirstIndex, 0u, C.Occupant);
}

// 📝 I1: the demo's phase-1 cut and cull, then the record's place in the four lists (both phase-2 lists start without it).
[shader("compute")]
[numthreads(64, 1, 1)]
void SelectAndCullPhase1Compacted(uint3 ID : SV_DispatchThreadID)
{
    SelectAndCullPhase1Body(ID);
    let I = ID.x;
    if (I >= ArrayCount(Clusters)) { return; }
    let C = Clusters[I];
    let Glass = (C.Flags & 2u) != 0u;
    PlaceCommand(SelectBranch(0u, 1u, Glass), I, C, ClusterVerdicts[I] == 1u);
    PlaceCommand(SelectBranch(1u, 0u, Glass), I, C, false);
    PlaceCommand(2u, I, C, false);
    PlaceCommand(3u, I, C, false);
}

// 📝 I1: the demo's phase-2 disocclusion test, then every disoccluded record joins its class's phase-2 list.
[shader("compute")]
[numthreads(64, 1, 1)]
void CullPhase2DisoccludedCompacted(uint3 ID : SV_DispatchThreadID)
{
    CullPhase2DisoccludedBody(ID);
    let I = ID.x;
    if (I >= ArrayCount(Clusters)) { return; }
    if (ClusterVerdicts[I] == 3u) { PlaceCommand(SelectBranch(2u, 3u, (Clusters[I].Flags & 2u) != 0u), I, Clusters[I], true); }
}
