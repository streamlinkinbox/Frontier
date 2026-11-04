//============================================================================================================================================
//                                                            CARDFINAL.SLANG
//============================================================================================================================================
// 🧩 Final card radiance: diffuse albedo over π times direct plus interpolated radiosity irradiance, plus authored incandescence.

#pragma once
#include "IntrinsicParity.slang"
#include "HostConstants.slang"

#include "RefreshDescriptor.slang"
#include "CardIrradiance.slang"

  [[vk::binding(1, 0)]] StructuredBuffer<CardProfile> Cards;
  [[vk::binding(2, 0)]] StructuredBuffer<uint2> Tiles;
  [[vk::binding(3, 0)]] ConstantBuffer<CardTileJob>        Work;
  [[vk::binding(4, 0)]] Texture2D<float4>           CardAlbedo;
  [[vk::binding(5, 0)]] Texture2D<float4>         CardIncandescence;
  [[vk::binding(6, 0)]] Texture2D<float4>           CardDirect;
  [[vk::binding(7, 0)]] Texture2D<float4>            CardDepth;
  [[vk::binding(8, 0)]] StructuredBuffer<float4> Sites;
  [[vk::binding(9, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4>      CardFinalTarget;

// 📝 Radiosity sites are spatially filtered as they are interpolated: a Gaussian (σ = 0.9 site) over the 4×4 sites
//    around the texel, restricted to the texel's own card and to sites that have gathered at all. Indirect irradiance
//    is low-frequency, and 16 rays per site per gather is otherwise visible as a 4×4-texel speckle.
float3 InterpolateSiteIrradiance(CardProfile Card, uint2 Texel)
{
    let Continuous = (float2(Texel) + 0.5) / 4.0 - 0.5;
    let Corner = int2(floor(Continuous)) - int2(1);
    let Lowest = int2(Card.TexelOrigin / 4u);
    let Highest = int2((Card.TexelOrigin + Card.TexelExtent) / 4u) - int2(1);
    var Sum = float4(0.0);
    for (var Y = 0; Y < 4; Y++)
    {
        for (var X = 0; X < 4; X++)
        {
            let Site = Corner + int2(X, Y);
            if (any(Site < Lowest) || any(Site > Highest))
            {
                continue;
            }
            let Record = Sites[ToUnsigned(Site.y) * Work.SiteStride + ToUnsigned(Site.x)];
            if (Record.w <= 0.0)
            {
                continue;
            }
            let Offset = float2(Site) - Continuous;
            let Weight = exp(-dot(Offset, Offset) / (2.0 * 0.9 * 0.9));
            Sum += float4(Record.rgb * Weight, Weight);
        }
    }
    return SelectBranch(float3(0.0), Sum.rgb / Sum.w, Sum.w > 1.0e-4);
}

 
[shader("compute")]
[numthreads(8, 8, 1)]
void IntegrateCardFinal(uint3 Group : SV_GroupID, uint3 Local : SV_GroupThreadID)
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
    if (LoadTexel(CardDepth, int2(Texel), 0).x > 1.5)
    {
        CardFinalTarget[int2(Texel)] = float4(0.0);
        return;
    }
    let Albedo = LoadTexel(CardAlbedo, int2(Texel), 0).rgb;
    let Incandescence = LoadTexel(CardIncandescence, int2(Texel), 0).rgb * Refresh.IncandescenceScale;
    let Direct = LoadTexel(CardDirect, int2(Texel), 0).rgb;
    var Indirect = float3(0.0);
    if (FeatureEnabled(Refresh, FeatureCardRadiosity))
    {
        Indirect = InterpolateSiteIrradiance(Card, Texel);
    }
    let Radiance = Albedo * InversePi * (Direct + Indirect) + Incandescence;
    CardFinalTarget[int2(Texel)] = float4(Radiance, 1.0);
}
