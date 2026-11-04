//============================================================================================================================================
//                                                        VISIBILITYRASTER.SLANG
//============================================================================================================================================
// 🧩 Visibility raster: cluster-triangle identity and barycentrics only; opaque draws write depth, transmissive draws depth-test without
//    writing, and a second entry depth-peels the glass back layer against the front layer's depth (VisibilityRaster.wgsl).

#pragma once
#include "IntrinsicParity.slang"
#include "HostConstants.slang"
#include "RefreshDescriptor.slang"

  [[vk::binding(1, 0)]] StructuredBuffer<OccupantProfile> Occupants;

struct VisibilityVarying
{
    float4 Position : SV_Position;
    [[vk::location(0)]] nointerpolation uint TriangleFirst : LOCATION0;   // global expanded-vertex index of the triangle's first corner
    [[vk::location(1)]] float3 Barycentric : LOCATION1;
    [[vk::location(2)]] nointerpolation uint Occupant : LOCATION2;
};

[shader("vertex")]
VisibilityVarying VisibilityVertex(uint VertexIndex : SV_VulkanVertexID,
                                   [[vk::location(0)]] float3 LocalPosition : LOCATION0, [[vk::location(1)]] uint Family : LOCATION1,
                                   [[vk::location(2)]] float3 Normal : LOCATION2, [[vk::location(3)]] uint OccupantOffset : LOCATION3,
                                   [[vk::location(4)]] uint OccupantIndex : LOCATION4)
{
    let Index = OccupantIndex + OccupantOffset;
    var Varying : VisibilityVarying = {};
    Varying.Position = mul(float4(LocalPosition, 1.0), mul(Occupants[Index].LocalToWorld, Refresh.ViewProjection));
    Varying.TriangleFirst = VertexIndex - (VertexIndex % 3u);
    let Corner = VertexIndex % 3u;
    Varying.Barycentric = float3(SelectBranch(0.0, 1.0, Corner == 0u), SelectBranch(0.0, 1.0, Corner == 1u), SelectBranch(0.0, 1.0, Corner == 2u));
    Varying.Occupant = Index;
    return Varying;
}

struct VisibilityTargets
{
    [[vk::location(0)]] uint2  OpaqueVis : LOCATION0;
    [[vk::location(1)]] float2 OpaqueBary : LOCATION1;
    [[vk::location(2)]] uint2  GlassVis : LOCATION2;
    [[vk::location(3)]] float2 GlassBary : LOCATION3;
};

// 📝 Two entries, two programs, one shared raster sequence: each entry writes only its own layer's targets and zeroes the other
//    layer's, so an opaque fragment never masquerades as a glass front fragment in the resolve. The opaque program writes depth,
//    the glass program does not, and the host records opaque commands first so glass depth-tests against opaque geometry.
[shader("fragment")]
VisibilityTargets VisibilityFragment(VisibilityVarying Varying)
{
    var Targets : VisibilityTargets = {};
    Targets.OpaqueVis = uint2(Varying.TriangleFirst + 1u, Varying.Occupant);
    Targets.OpaqueBary = Varying.Barycentric.yz;
    Targets.GlassVis = uint2(0u);
    Targets.GlassBary = float2(0.0);
    return Targets;
}

[shader("fragment")]
VisibilityTargets VisibilityGlassFragment(VisibilityVarying Varying)
{
    var Targets : VisibilityTargets = {};
    Targets.OpaqueVis = uint2(0u);
    Targets.OpaqueBary = float2(0.0);
    Targets.GlassVis = uint2(Varying.TriangleFirst + 1u, Varying.Occupant);
    Targets.GlassBary = Varying.Barycentric.yz;
    return Targets;
}

  [[vk::binding(0, 1)]] Texture2D<float> FrontGlassDepth;

struct GlassBackTargets
{
    [[vk::location(0)]] uint2  GlassBackVis : LOCATION0;
    [[vk::location(1)]] float2 GlassBackBary : LOCATION1;
};

// 📝 Depth peel: keep only glass fragments farther than the front layer at this pixel (reversed-Z: farther is smaller depth); the
//    sequence's own depth target then keeps the nearest of those.
[shader("fragment")]
GlassBackTargets VisibilityGlassBackFragment(VisibilityVarying Varying)
{
    let Coord = int2(ToSigned(Varying.Position.x), ToSigned(Varying.Position.y));
    let FrontDepth = LoadTexel(FrontGlassDepth, Coord, 0);
    if (Varying.Position.z >= FrontDepth - 1.0e-5) { discard; }
    var Targets : GlassBackTargets = {};
    Targets.GlassBackVis = uint2(Varying.TriangleFirst + 1u, Varying.Occupant);
    Targets.GlassBackBary = Varying.Barycentric.yz;
    return Targets;
}
