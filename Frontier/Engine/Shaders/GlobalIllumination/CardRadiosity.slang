//============================================================================================================================================
//                                                          CARDRADIOSITY.SLANG
//============================================================================================================================================
// 🧩 Hemispherical gather for card radiosity sites: 16 stratified cosine rays per site, hits read last refresh's card radiance.

#pragma once
#include "IntrinsicParity.slang"
#include "HostConstants.slang"

// 📝 A site stands for a 4×4 block of card texels. Its 16 lanes first elect the occupied texel nearest the block centre,
//    then each traces one stratum of a 4×4 cosine-weighted hemisphere from it. E = π·mean(L) is folded into the site's
//    running estimate with an exponential weight that never falls below a floor, so the depot keeps responding to
//    moving light and moving occupants instead of freezing once converged.

#include "CardSampling.slang"
#include "CardIrradiance.slang"
#include "DiscrepancySequence.slang"

  [[vk::binding(1, 0)]] StructuredBuffer<uint2>      Tiles;
  [[vk::binding(2, 0)]] ConstantBuffer<CardTileJob>             Work;
  [[vk::binding(3, 0)]] Texture2D<float4>                CardNormal;
  [[vk::binding(4, 0)]] RWStructuredBuffer<float4> Sites;

groupshared uint SiteKeys[4];
groupshared float3 LaneRadiance[64];

 
[shader("compute")]
[numthreads(64, 1, 1)]
void IntegrateCardRadiosity(uint3 Group : SV_GroupID, uint Lane : SV_GroupIndex)
{
    let WorkIndex = ResolveWorkIndex(Group);
    if (WorkIndex >= Work.TileCount)
    {
        return;
    }
    let Site = Lane >> 4u;
    let Ray = Lane & 15u;
    if (Lane < 4u)
    {
        InterlockedExchange(SiteKeys[Lane], 0xFFFFFFFFu, AtomicDiscard);
    }
    GroupMemoryBarrierWithGroupSync();

    let Assignment = Tiles[WorkIndex];
    let Card = Cards[Assignment.x];
    let SiteOrigin = ResolveTileTexel(Assignment, uint2(Site & 1u, Site >> 1u) * 4u);
    let CandidateTile = SiteOrigin + uint2(Ray & 3u, Ray >> 2u);

    // ① elect the occupied texel nearest the block centre
    if (all(CandidateTile < Card.TexelExtent))
    {
        let Depth = LoadTexel(CardDepth, int2(Card.TexelOrigin + CandidateTile), 0).x;
        if (Depth <= 1.5)
        {
            let Offset = float2(float(Ray & 3u), float(Ray >> 2u)) - float2(1.5);
            let Key = (ToUnsigned(dot(Offset, Offset) * 4.0) << 4u) | Ray;
            InterlockedMin(SiteKeys[Site], Key);
        }
    }
    GroupMemoryBarrierWithGroupSync();
    let Elected = SiteKeys[Site];

    var Radiance = float3(0.0);
    if (Elected != 0xFFFFFFFFu)
    {
        let ElectedRay = Elected & 15u;
        let TileTexel = SiteOrigin + uint2(ElectedRay & 3u, ElectedRay >> 2u);
        let Texel = int2(Card.TexelOrigin + TileTexel);
        let Depth = LoadTexel(CardDepth, Texel, 0).x;
        let Occupant = Occupants[Card.Occupant];
        let Coordinates = float3((float2(TileTexel) + 0.5) / float2(Card.TexelExtent), Depth);
        let Position = (mul(float4(ReconstructCardLocal(Card, Coordinates), 1.0), Occupant.LocalToWorld)).xyz;
        let LocalNormal = LoadTexel(CardNormal, Texel, 0).xyz * 2.0 - 1.0;
        let Normal = normalize((mul(float4(LocalNormal, 0.0), Occupant.LocalToWorld)).xyz);

        // ② one stratum of a jittered 4×4 cosine hemisphere per lane
        var Seed = ConstructPixelSeed(uint2(Texel), Refresh.RefreshIndex, Ray);
        let Pair = (float2(float(Ray & 3u), float(Ray >> 2u)) + float2(AdvanceUniform(Seed), AdvanceUniform(Seed))) * 0.25;
        let Direction = mul(ProjectCosineHemisphere(Pair), ConstructTangentBasis(Normal));
        let Bias = ResolveSurfaceBias(Position);
        let Hit = TraverseSceneDistance(Position + Normal * Bias, Direction, 0.5 * Bias, 120.0, 0.02);
        Radiance = ResolveMarchRadiance(Hit, Direction);
    }
    LaneRadiance[Lane] = Radiance;
    GroupMemoryBarrierWithGroupSync();

    // ③ fold E = π·mean(L) into the site estimate
    if (Ray == 0u)
    {
        let SiteTexel = Card.TexelOrigin + SiteOrigin;
        let SiteIndex = (SiteTexel.y / 4u) * Work.SiteStride + SiteTexel.x / 4u;
        if (Elected == 0xFFFFFFFFu)
        {
            Sites[SiteIndex] = float4(0.0);
            return;
        }
        var Sum = float3(0.0);
        for (var Index = 0u; Index < 16u; Index++)
        {
            Sum += LaneRadiance[Site * 16u + Index];
        }
        let Estimate = Pi * Sum / 16.0;
        let Prior = Sites[SiteIndex];
        let Dynamic = (Occupants[Card.Occupant].Participation & ParticipationDynamic) != 0u;
        let Floor = SelectBranch(Work.StaticWeight, Work.DynamicWeight, Dynamic);
        let Fraction = max(1.0 / (Prior.w + 1.0), Floor);
        Sites[SiteIndex] = float4(lerp(Prior.rgb, Estimate, Fraction), min(Prior.w + 1.0, 64.0));
    }
}
