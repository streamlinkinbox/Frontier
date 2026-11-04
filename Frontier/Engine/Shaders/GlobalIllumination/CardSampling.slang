//============================================================================================================================================
//                                                           CARDSAMPLING.SLANG
//============================================================================================================================================
// 🧩 Surface-card lookup — the radiance a distance-field hit point carries, read from its occupant's cards (the surface store).

#pragma once
#include "IntrinsicParity.slang"

#include "RefreshDescriptor.slang"
#include "DistanceMarch.slang"
#include "SkyProjection.slang"
#include "CardProjection.slang"
#include "CelestialSky.slang"

// 📝 Group 2 is the card depot as the tracers see it: occupants, cards, card depth (2.0+ marks a vacant texel) and the
//    card final radiance, plus the sky-view radiance a ray that escapes reads instead.
  [[vk::binding(0, 2)]] StructuredBuffer<OccupantProfile> Occupants;
  [[vk::binding(1, 2)]] StructuredBuffer<CardProfile>     Cards;
  [[vk::binding(2, 2)]] Texture2D<float4>                CardDepth;
  [[vk::binding(3, 2)]] Texture2D<float4>                CardFinal;
  [[vk::binding(4, 2)]] Texture2D<float4>                  SkyView;
  [[vk::binding(5, 2)]] SamplerState               SkySampler;
  [[vk::binding(6, 2)]] Texture2DArray<float4> MoonAtlas;

//------------------------------------------------------------------------------------------------------------------------
//                                                     GROUND CARDS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Ground tiles own one +Z card each, allocated consecutively, so a ground hit resolves its card from x, y alone.
uint ResolveGroundCard(float2 Planar)
{
    let PadMinimum = float2(GroundPadMinimumX, GroundPadMinimumY);
    let TileCount = uint2(GroundTileCountX, GroundTileCountY);
    let PadMaximum = PadMinimum + GroundTileExtent * float2(TileCount);
    if (all(Planar >= PadMinimum) && all(Planar < PadMaximum))
    {
        let Tile = min(uint2((Planar - PadMinimum) / GroundTileExtent), TileCount - uint2(1u));
        return GroundFirstCard + Tile.x * TileCount.y + Tile.y;
    }
    if (Planar.x < PadMinimum.x)
    {
        return GroundStripCard;
    }
    if (Planar.x >= PadMaximum.x)
    {
        return GroundStripCard + 1u;
    }
    if (Planar.y < PadMinimum.y)
    {
        return GroundStripCard + 2u;
    }
    return GroundStripCard + 3u;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CARD RADIANCE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Four bilinear taps, each validated on its own captured depth. Returns (Σw·L, Σw) for depth-validated taps in the
//    first vector and the same for every occupied tap in the second, which is the fallback when validation rejects all.
struct CardTaps
{
    float4 Validated; 
    float4 Occupied;  
};

CardTaps SampleCardTaps(CardProfile Card, float3 Coordinates, float Tolerance)
{
    var Taps : CardTaps = {};
    Taps.Validated = float4(0.0);
    Taps.Occupied  = float4(0.0);
    let Lowest = int2(Card.TexelOrigin);
    let Highest = int2(Card.TexelOrigin + Card.TexelExtent) - int2(1);
    let Position = float2(Card.TexelOrigin) + clamp(Coordinates.xy, float2(0.0), float2(1.0)) * float2(Card.TexelExtent) - 0.5;
    let Corner = floor(Position);
    let Fraction = Position - Corner;
    for (var Tap = 0u; Tap < 4u; Tap++)
    {
        let Offset = uint2(Tap & 1u, Tap >> 1u);
        let Texel = clamp(int2(Corner) + int2(Offset), Lowest, Highest);
        let Bilinear = SelectBranch(1.0 - Fraction.x, Fraction.x, Offset.x == 1u) * SelectBranch(1.0 - Fraction.y, Fraction.y, Offset.y == 1u);
        let Depth = LoadTexel(CardDepth, Texel, 0).x;
        if (Depth > 1.5)
        {
            continue;
        }
        let Radiance = LoadTexel(CardFinal, Texel, 0).rgb;
        let Weight = max(Bilinear, 1.0e-3);
        Taps.Occupied += float4(Radiance * Weight, Weight);
        if (abs(Depth - Coordinates.z) <= Tolerance)
        {
            Taps.Validated += float4(Radiance * Weight, Weight);
        }
    }
    return Taps;
}

// 📝 Radiance leaving a scene point toward the tracer, from the cards of the occupant the distance field names there.
//    Each card is weighted by how squarely it faces the hit normal; a card that cannot see the point fails its depth
//    test and drops out, which is how a concave occupant's six cards stay correct. Tolerance [m] scales with the
//    cascade that produced the hit, since the hit point is only as precise as that cascade's voxel.
//    Returns (radiance, coverage); coverage 0 means no card covers the point at all.
float4 SampleCardRadiance(float3 Position, float3 Normal, uint Identity, float Tolerance)
{
    if (Identity == GroundIdentity)
    {
        let Card = Cards[ResolveGroundCard(Position.xy)];
        let Coordinates = ProjectCardCoordinates(Card, float3(Position.xy, Distance.GroundHeight));
        let Taps = SampleCardTaps(Card, Coordinates, 2.0);
        if (Taps.Occupied.w > 0.0)
        {
            return float4(Taps.Occupied.rgb / Taps.Occupied.w, 1.0);
        }
        return float4(0.0);
    }
    if (Identity >= ArrayCount(Occupants))
    {
        return float4(0.0);
    }
    let Occupant = Occupants[Identity];
    if ((Occupant.Participation & ParticipationFallback) != 0u)
    {
        return float4(ResolveFallbackRadiance(Occupant, Position, Normal), 1.0);
    }
    let Local = (mul(float4(Position, 1.0), Occupant.WorldToLocal)).xyz;
    let LocalNormal = normalize((mul(float4(Normal, 0.0), Occupant.WorldToLocal)).xyz);
    var Validated = float4(0.0);
    var Occupied = float4(0.0);
    for (var Index = 0u; Index < Occupant.CardCount; Index++)
    {
        let Card = Cards[Occupant.FirstCard + Index];
        let Facing = dot(LocalNormal, ResolveCardAxis(Card.Axis));
        if (Facing <= 0.0)
        {
            continue;
        }
        let Coordinates = ProjectCardCoordinates(Card, Local);
        if (any(Coordinates.xy < float2(-0.02)) || any(Coordinates.xy > float2(1.02)))
        {
            continue;
        }
        let Taps = SampleCardTaps(Card, Coordinates, Tolerance / ResolveCardDepthExtent(Card));
        let AxisWeight = Facing * Facing;
        Validated += float4(Taps.Validated.rgb, Taps.Validated.w) * AxisWeight;
        Occupied  += float4(Taps.Occupied.rgb, Taps.Occupied.w) * AxisWeight;
    }
    if (Validated.w > 1.0e-4)
    {
        return float4(Validated.rgb / Validated.w, 1.0);
    }
    if (Occupied.w > 1.0e-4)
    {
        return float4(Occupied.rgb / Occupied.w, 0.5);
    }
    return float4(0.0);
}

// 📝 A card-less occupant (a fragment: born mid-refresh, gone into the field the same refresh, never captured) shades its
//    hit from its packed albedo, under the sun with a short traced shadow and the sky's hemisphere toward the normal:
//    L = ρ/π · (E_sun · cosθ · V + π · L_sky(n') · (½ + ½ n_z)). Rough, and enough for a second bounce.
float3 ResolveFallbackRadiance(OccupantProfile Occupant, float3 Position, float3 Normal)
{
    let Albedo = UnpackUnormQuad(Occupant.FirstCard).rgb;
    var Irradiance = float3(0.0);
    let Cosine = dot(Normal, Refresh.SunDirection);
    if (Cosine > 0.0)
    {
        let Bias = ResolveSurfaceBias(Position);
        Irradiance += Refresh.SunIlluminance * Cosine * TraverseSceneShadow(Position + Normal * Bias, Refresh.SunDirection, 0.5 * Bias, 60.0, 6.0);
    }
    let Upward = normalize(Normal + float3(0.0, 0.0, 1.0));
    Irradiance += 3.14159265 * SampleSkyRadiance(Upward) * (0.5 + 0.5 * Normal.z);
    return Albedo * InversePi * Irradiance;
}

// 📝 Tolerance for a hit answered by cascade Level: one and a half of its voxels, never below 6 cm.
float ResolveCardTolerance(uint Level)
{
    let Clamped = min(Level, Distance.ClipCount - 1u);
    return max(1.5 * Distance.Clips[Clamped].VoxelExtent, 0.06);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SKY RADIANCE
//------------------------------------------------------------------------------------------------------------------------

float3 SampleSkyRadiance(float3 Direction)
{
    let Coordinates = ProjectSkyCoordinates(normalize(Direction));
    return SkyView.SampleLevel(SkySampler, Coordinates, 0.0).rgb * Refresh.SkyScale + Refresh.NightSkyRadiance;
}

// 📝 What a traced ray brings back: card radiance at a surface, sky radiance on an escape. A surface no card covers
//    returns nothing — an uncovered point is treated as an absorber rather than guessed, which can only darken.
float3 ResolveMarchRadiance(MarchHit Hit, float3 Direction)
{
    if (!Hit.Hit)
    {
        return SampleSkyRadiance(Direction);
    }
    let Card = SampleCardRadiance(Hit.Position, Hit.Normal, Hit.Identity, ResolveCardTolerance(Hit.Level));
    if (Card.w > 0.0)
    {
        return Card.rgb;
    }
    return float3(0.0);
}
