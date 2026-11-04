//============================================================================================================================================
//                                                           CARDCAPTURE.SLANG
//============================================================================================================================================
// 🧩 Orthographic capture of an occupant's facets into one card tile: diffuse albedo, local normal, incandescence, card depth.

#pragma once
#include "IntrinsicParity.slang"
#include "HostConstants.slang"

// 📝 Capture runs once per card at bring-up, in the occupant's own refresh, so a moving occupant never recaptures —
//    only its lighting is refreshed. The card index arrives as the instance index of the draw.

#include "CardProjection.slang"

  [[vk::binding(0, 0)]] StructuredBuffer<CardProfile>     Cards;
  [[vk::binding(1, 0)]] StructuredBuffer<OccupantProfile> Occupants;
  [[vk::binding(2, 0)]] StructuredBuffer<MaterialProfile> Materials;

struct CaptureVarying
{
    float4 Position : SV_Position;             
    [[vk::location(0)]] float3 Normal : LOCATION0;                     
    [[vk::location(1)]] nointerpolation uint Family : LOCATION1;  
    [[vk::location(2)]] nointerpolation uint Card : LOCATION2;    
};

struct CaptureTargets
{
    [[vk::location(0)]] float4 Albedo : LOCATION0;   
    [[vk::location(1)]] float4 Normal : LOCATION1;   
    [[vk::location(2)]] float4 Incandescence : LOCATION2; 
    [[vk::location(3)]] float4 Depth : LOCATION3;    
};


[shader("vertex")]
CaptureVarying CaptureVertex([[vk::location(0)]] float3 Position : LOCATION0, [[vk::location(1)]] uint Family : LOCATION1, [[vk::location(2)]] float3 Normal : LOCATION2,
                 uint CardIndex : SV_VulkanInstanceID)
{
    let Card = Cards[CardIndex];
    let Occupant = Occupants[Card.Occupant];
    let Coordinates = ProjectCardCoordinates(Card, Position);
    var Varying : CaptureVarying = {};
    Varying.Position = float4(Coordinates.x * 2.0 - 1.0, 1.0 - Coordinates.y * 2.0, Coordinates.z, 1.0);
    Varying.Normal   = Normal;
    Varying.Family   = ResolveSurfaceFamily(Family, Occupant.MaterialOverride);
    Varying.Card     = CardIndex;
    return Varying;
}


[shader("fragment")]
CaptureTargets CaptureFragment(CaptureVarying Varying)
{
    let Card = Cards[Varying.Card];
    let Material = Materials[Varying.Family];
    var Normal = normalize(Varying.Normal);
    // 📝 An open surface seen from behind still faces the card that saw it; the captured normal is turned toward it.
    if (dot(Normal, ResolveCardAxis(Card.Axis)) < 0.0)
    {
        Normal = -Normal;
    }
    var Targets : CaptureTargets = {};
    Targets.Albedo   = float4(Material.Albedo * (1.0 - Material.Metalness), 1.0);
    Targets.Normal   = float4(Normal * 0.5 + 0.5, 1.0);
    Targets.Incandescence = float4(Material.Incandescence, 1.0);
    Targets.Depth    = float4(Varying.Position.z, 0.0, 0.0, 1.0);
    return Targets;
}
