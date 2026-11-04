//============================================================================================================================================
//                                                            PICKRASTER.SLANG
//============================================================================================================================================
// 🧩 Beyond the demo: GPU picking. Every rastered occupant projected from a refresh's unjittered camera into a small target centred
//    on one screen pixel; each texel keeps the nearest occupant's index + 1 (0 where nothing is drawn).

#pragma once
#include "SceneLayouts.slang"

struct PickProfile
{
    float4x4 ViewProjection;   // [-]   the refresh's unjittered view-projection
    float2   Centre;           // [ndc] centre of the picked screen pixel
    float2   Scale;            // [-]   screen extent over the target extent, per axis
};

[[vk::binding(0, 0)]] ConstantBuffer<PickProfile>       Pick;
[[vk::binding(1, 0)]] StructuredBuffer<OccupantProfile> Occupants;

struct PickVarying
{
    float4 Position : SV_Position;
    [[vk::location(0)]] nointerpolation uint Occupant : LOCATION0;
};

struct PickTargets
{
    [[vk::location(0)]] uint Occupant : LOCATION0;   // [-] occupant index + 1
};

// 📝 The surface raster's vertex step without its shading outputs. The picked pixel's neighbourhood is stretched over the target:
//    clip xy about the pixel's centre, times the extent ratio, so each target texel is one screen pixel around it and covers what
//    the screen pixel's centre covers.
[shader("vertex")]
PickVarying PickVertex([[vk::location(0)]] float3 Position : LOCATION0, [[vk::location(3)]] uint OccupantOffset : LOCATION3,
                       [[vk::location(4)]] uint OccupantIndex : LOCATION4)
{
    let Index = OccupantIndex + OccupantOffset;
    let World = mul(float4(Position, 1.0), Occupants[Index].LocalToWorld);
    var Clip = mul(World, Pick.ViewProjection);
    Clip.xy = (Clip.xy - Pick.Centre * Clip.w) * Pick.Scale;
    var Varying : PickVarying = {};
    Varying.Position = Clip;
    Varying.Occupant = Index;
    return Varying;
}

[shader("fragment")]
PickTargets PickFragment(PickVarying Varying)
{
    var Targets : PickTargets = {};
    Targets.Occupant = Varying.Occupant + 1u;
    return Targets;
}
