//============================================================================================================================================
//                                                            CARDDIRECT.SLANG
//============================================================================================================================================
// 🧩 Direct sun and clustered light irradiance on every scheduled card texel, feeding the surface store and indirect GI radiosity.

#pragma once
#include "IntrinsicParity.slang"
#include "HostConstants.slang"

#include "RefreshDescriptor.slang"
#include "DistanceMarch.slang"
#include "CardProjection.slang"
#include "CardIrradiance.slang"
#include "FireIllumination.slang"
#include "ClusteredLights.slang"

  [[vk::binding(1, 0)]] StructuredBuffer<CardProfile>        Cards;
  [[vk::binding(2, 0)]] StructuredBuffer<OccupantProfile>    Occupants;
  [[vk::binding(3, 0)]] StructuredBuffer<uint2>        Tiles;
  [[vk::binding(4, 0)]] ConstantBuffer<CardTileJob>               Work;
  [[vk::binding(5, 0)]] Texture2D<float4>                  CardNormal;
  [[vk::binding(6, 0)]] Texture2D<float4>                   CardDepth;
  [[vk::binding(7, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4>            CardDirectTarget;
  [[vk::binding(8, 0)]] StructuredBuffer<ClusteredLightRegister>  SceneLights;   // single-element span: read as SceneLights[0]

 
[shader("compute")]
[numthreads(8, 8, 1)]
void IntegrateCardDirect(uint3 Group : SV_GroupID, uint3 Local : SV_GroupThreadID)
{
    let WorkIndex = ResolveWorkIndex(Group);
    if (WorkIndex >= Work.TileCount)
    {
        return;
    }
    let Assignment = Tiles[WorkIndex];
    let Card = Cards[Assignment.x];
    let TileTexel = ResolveTileTexel(Assignment, Local.xy);
    if (any(TileTexel >= Card.TexelExtent))
    {
        return;
    }
    let Texel = Card.TexelOrigin + TileTexel;
    let Depth = LoadTexel(CardDepth, int2(Texel), 0).x;
    if (Depth > 1.5)
    {
        CardDirectTarget[Texel] = float4(0.0);
        return;
    }

    let Occupant = Occupants[Card.Occupant];
    let Coordinates = float3((float2(TileTexel) + 0.5) / float2(Card.TexelExtent), Depth);
    let Position = (mul(float4(ReconstructCardLocal(Card, Coordinates), 1.0), Occupant.LocalToWorld)).xyz;
    let LocalNormal = LoadTexel(CardNormal, int2(Texel), 0).xyz * 2.0 - 1.0;
    let Normal = normalize((mul(float4(LocalNormal, 0.0), Occupant.LocalToWorld)).xyz);

    let Cosine = dot(Normal, Refresh.SunDirection);
    var Irradiance = float3(0.0);
    if (Cosine > 0.0)
    {
        let Bias = ResolveSurfaceBias(Position);
        var Visibility = 1.0;
        if (FeatureEnabled(Refresh, FeatureDirectShadow))
        {
            Visibility = TraverseSceneShadow(Position + Normal * Bias, Refresh.SunDirection, 0.5 * Bias, 400.0, 6.0);
        }
        Irradiance = Refresh.SunIlluminance * Cosine * Visibility;
    }
    let Fire = ResolveFireArrival(Position, Normal);
    Irradiance += Fire.Illuminance * max(dot(Normal, Fire.Direction), 0.0);

    // 3D Clustered light irradiance feeding surface cards (iterating only active lights in this texel's 3D cluster cell)
    let CellIdx = ResolveClusterCellIndex(SceneLights[0].GridMinimum, SceneLights[0].GridScale, Position);
    if (CellIdx != -1)
    {
        let Pair = SceneLights[0].CellMasks[ToUnsigned(max(CellIdx, 0)) >> 1u];
        let Mask2 = ResolveClusterMaskFromPair(CellIdx, Pair, SceneLights[0].GridMinimum.w);
        for (var Word = 0u; Word < 2u; Word += 1u)
        {
            var Mask = SelectBranch(Mask2.x, Mask2.y, Word == 1u);
            let Base = Word * 32u;
            while (Mask != 0u)
            {
                let Bit = firstbitlow(Mask);
                Mask     &= Mask - 1u;
                let Light = SceneLights[0].Lights[Base + Bit];
                Irradiance += EvaluateLightArrival(Light, Position, Normal);
            }
        }
    }

    CardDirectTarget[Texel] = float4(Irradiance, 1.0);
}
