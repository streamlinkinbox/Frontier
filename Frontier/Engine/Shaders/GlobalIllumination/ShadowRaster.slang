//============================================================================================================================================
//                                                           SHADOWRASTER.SLANG
//============================================================================================================================================
// 🧩 Depth-only projection of every rastered occupant into one cascade of the sun's shadow depth.

#pragma once
#include "IntrinsicParity.slang"
#include "HostConstants.slang"

#include "SceneLayouts.slang"
#include "EffectLayouts.slang"
#include "VehicleVisual.slang"

struct CascadeProfile
{
    float4x4 LightViewProjection; 
};

  [[vk::binding(0, 0)]] ConstantBuffer<CascadeProfile>             Cascade;
  [[vk::binding(1, 0)]] StructuredBuffer<OccupantProfile>     Occupants;
  [[vk::binding(2, 0)]] ConstantBuffer<EffectProfile>              Effect;   // the vehicle program only
  [[vk::binding(3, 0)]] StructuredBuffer<VehicleProfile>  VehicleState;   // single-element span: read as VehicleState[0]  // the vehicle program only

// 📝 OccupantOffset: as in SurfaceRaster.slang, a fragmented set's vertices name their own occupant.

[shader("vertex")]
float4 ShadowVertex([[vk::location(0)]] float3 Position : LOCATION0, [[vk::location(1)]] uint OccupantOffset : LOCATION1,
                uint OccupantIndex : SV_VulkanInstanceID) : SV_Position
{
    let World = mul(float4(Position, 1.0), Occupants[OccupantIndex + OccupantOffset].LocalToWorld);
    return mul(World, Cascade.LightViewProjection);
}

// 📝 The vehicle program, for the car's runs only: rigid wheel damage visibility and the steering wheel's turn as in
//    the surface raster; see-through glazing casts no shadow (the sun reaches the cabin through it); and while the
//    car derezzes or rezzes its shadow comes apart with it — the fragment step drops exactly the cells the surface
//    raster drops.
struct MaskedShadowVarying
{
    float4 Position : SV_Position;         
    [[vk::location(0)]] float3 BodyLocal : LOCATION0;              
    [[vk::location(1)]] nointerpolation uint Glass : LOCATION1; 
};


[shader("vertex")]
MaskedShadowVarying MaskedShadowVertex([[vk::location(0)]] float3 Position : LOCATION0, [[vk::location(1)]] uint OccupantOffset : LOCATION1, [[vk::location(2)]] uint Family : LOCATION2,
                      uint OccupantIndex : SV_VulkanInstanceID)
{
    let Index = OccupantIndex + OccupantOffset;
    var Local = Position;
    if (Index >= 1u && Index < 5u)
    {
        Local = ResolveWheelVertex(Index - 1u, Family, Position, float3(0.0, 0.0, 1.0)).Position;
    }
    else if (Index == 0u)
    {
        Local = DeformBodyVertex(Family, Position, float3(0.0, 0.0, 1.0)).Position;
    }
    let World = mul(float4(Local, 1.0), Occupants[Index].LocalToWorld);
    var Varying : MaskedShadowVarying = {};
    Varying.Position = mul(World, Cascade.LightViewProjection);
    Varying.BodyLocal = (mul(World, Occupants[0].WorldToLocal)).xyz;
    Varying.Glass = SelectBranch(0u, 1u, Index == 0u && Family == VehicleGlassFamily);
    return Varying;
}


[shader("fragment")]
void MaskedShadowFragment(MaskedShadowVarying Varying)
{
    if (Varying.Glass == 1u && VehicleState[0].Cabin.y > 0.5)
    {
        discard;
    }
    if (Effect.MaskMode == 0u)
    {
        return;
    }
    if (Effect.MaskMode == 3u)
    {
        discard;
    }
    let Unit = (Varying.BodyLocal - Effect.BodyMinimum.xyz) / max(Effect.BodyMaximum.xyz - Effect.BodyMinimum.xyz, float3(1.0e-3));
    let Elapsed = Effect.Clock - Effect.MaskStart;
    let Seed = ToUnsigned(Effect.Luminaires[0].Centre.w);
    if (Effect.MaskMode == 1u && Elapsed >= ResolveDerezRelease(Unit, Seed, Effect.Style, 1.0))
    {
        discard;
    }
    if (Effect.MaskMode == 2u && Elapsed < ResolveRezArrival(Unit, Seed, Effect.Style, 1.0))
    {
        discard;
    }
}
