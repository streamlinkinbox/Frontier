//============================================================================================================================================
//                                                          DIRECTRADIANCE.SLANG
//============================================================================================================================================
// 🧩 Direct sun radiance and clustered lights at every primary surface, cascaded shadow depth, and sky background.

#pragma once
#include "IntrinsicParity.slang"
#include "HostConstants.slang"

#include "RefreshDescriptor.slang"
#include "CelestialSky.slang"
#include "SurfaceReflectance.slang"
#include "PackingCodec.slang"
#include "SkyProjection.slang"
#include "FireIllumination.slang"
#include "ClusteredLights.slang"
#include "RayIntersection.slang"

struct CascadeReceiver
{
    float4x4 LightViewProjection[3]; 
    float4 Splits;                 // [m]   xyz far distance of each cascade, w cascade count
    float4 TexelWorld;             // [m]   world size of one shadow texel per cascade
};

  [[vk::binding(1, 0)]] StructuredBuffer<MaterialProfile>     Materials;
  [[vk::binding(2, 0)]] Texture2D<float4>            SurfaceAttributes;
  [[vk::binding(3, 0)]] Texture2D<float>                 SurfaceDepth;
  [[vk::binding(4, 0)]] Texture2DArray<float>                  ShadowDepth;
  [[vk::binding(5, 0)]] SamplerComparisonState                ShadowSampler;
  [[vk::binding(6, 0)]] ConstantBuffer<CascadeReceiver>            Receiver;
  [[vk::binding(7, 0)]] Texture2D<float4>                DirectSkyView;
  [[vk::binding(8, 0)]] SamplerState             DirectSkySampler;
   [[vk::binding(9, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4>       DirectTarget;
  [[vk::binding(10, 0)]] StructuredBuffer<ClusteredLightRegister> SceneLights;   // single-element span: read as SceneLights[0]

  [[vk::binding(11, 0)]] ConstantBuffer<CloudShadowParameters> Shadow;
  [[vk::binding(12, 0)]] Texture2D<float4> CloudShadowMap;
  [[vk::binding(13, 0)]] SamplerState CloudShadowSampler;
  // 📝 RefinementPenumbraFilter: the unshadowed sun term and the sun ray's blocker travel (-1 when unoccluded), for ResolvePenumbra.
  [[vk::binding(14, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> PenumbraTarget;
#include "CloudShadowSample.slang"

// 📝 3×3 percentage-closer taps, each a hardware bilinear comparison, after a normal offset of 1.5 texels.
float ResolveCascadeVisibility(float3 Position, float3 Normal)
{
    let ViewDistance = ResolveViewDepth(Position);
    let Count = ToUnsigned(Receiver.Splits.w);
    var Cascade = 0u;
    while (Cascade + 1u < Count && ViewDistance > Receiver.Splits[Cascade])
    {
        Cascade++;
    }
    if (ViewDistance > Receiver.Splits[Count - 1u])
    {
        return 1.0;
    }
    let Offset = Position + Normal * (1.5 * Receiver.TexelWorld[Cascade]);
    let Clip = mul(float4(Offset, 1.0), Receiver.LightViewProjection[Cascade]);
    let Ndc = Clip.xyz / Clip.w;
    let Coordinates = float2(Ndc.x * 0.5 + 0.5, 0.5 - Ndc.y * 0.5);
    if (any(Coordinates < float2(0.0)) || any(Coordinates > float2(1.0)) || Ndc.z > 1.0)
    {
        return 1.0;
    }
    let Texel = 1.0 / float2(TextureExtent(ShadowDepth).xy);
    var Lit = 0.0;
    for (var Y = -1; Y <= 1; Y++)
    {
        for (var X = -1; X <= 1; X++)
        {
            Lit += ShadowDepth.SampleCmpLevelZero(ShadowSampler, float3(Coordinates + float2(float(X), float(Y)) * Texel, float(Cascade)), Ndc.z);
        }
    }
    return Lit / 9.0;
}

float3 ResolveBackgroundRadiance(float3 Direction)
{
    var Radiance = DirectSkyView.SampleLevel(DirectSkySampler, ProjectSkyCoordinates(Direction), 0.0).rgb * Refresh.SkyScale
                 + Refresh.NightSkyRadiance;
    Radiance += CelestialSky(Direction, MoonAtlas, SkySampler);
    return Radiance;
}

 
[shader("compute")]
[numthreads(8, 8, 1)]
void IntegrateDirectRadiance(uint3 Invocation : SV_DispatchThreadID)
{
    let Pixel = Invocation.xy;
    if (any(float2(Pixel) >= Refresh.ScreenExtent))
    {
        return;
    }
    let Depth = LoadTexel(SurfaceDepth, int2(Pixel), 0);
    let Centre = float2(Pixel) + 0.5;
    if (Depth <= 0.0)
    {
        let Far = ReconstructWorldPosition(Centre, 1.0e-6);
        DirectTarget[int2(Pixel)] = float4(ResolveBackgroundRadiance(normalize(Far - Refresh.CameraPosition)), 0.0);
        return;
    }
    let Position = ReconstructWorldPosition(Centre, Depth);
    let Attributes = LoadTexel(SurfaceAttributes, int2(Pixel), 0);
    let Normal = DecodeOctahedral(Attributes.xy);
    let Surface = ProjectSurfaceSample(Materials[ToUnsigned(Attributes.z + 0.5)]);
    let View = normalize(Refresh.CameraPosition - Position);
    let UseRtShadows = FeatureEnabled(Refresh, FeatureRayTracing) && FeatureEnabled(Refresh, FeatureRayTracedShadows);
    let RayOrigin = Position + Normal * RayEpsilon;

    var Visibility = 1.0;
    var SunDir = Refresh.SunDirection;
    let PenumbraFiltered = UseRtShadows && RefinementEnabled(Refresh, RefinementPenumbraFilter);
    var SunOcclusion = -1.0;
    if (FeatureEnabled(Refresh, FeatureDirectShadow))
    {
        if (UseRtShadows)
        {
            let SunPair = SelectBranch(ProjectTemporalPair(Pixel, Refresh.RefreshIndex, 17u), ProjectStratifiedPair(Pixel, Refresh.RefreshIndex),
                                       RefinementEnabled(Refresh, RefinementStratifiedSun));
            SunDir     = SampleSunConeDirection(Refresh.SunDirection, SunPair);
            if (RefinementEnabled(Refresh, RefinementShadowReference))
            {
                // 📝 Measurement reference: the visible fraction of 64 R2 points of the disc, rotated per pixel and refresh.
                var Visible = 0.0;
                for (var Sample = 0u; Sample < 64u; Sample++)
                {
                    let Point = frac(float2(0.5, 0.5) + float(Sample) * float2(0.7548776662466927, 0.5698402909980532) + SunPair);
                    Visible += TraceSceneVisibility(RayOrigin, SampleSunConeDirection(Refresh.SunDirection, Point), RayEpsilon, 400.0);
                }
                Visibility = Visible / 64.0;
            }
            else if (PenumbraFiltered)
            {
                SunOcclusion = TraceSceneOcclusion(RayOrigin, SunDir, RayEpsilon, 400.0);
                Visibility   = SelectBranch(1.0, 0.0, SunOcclusion >= 0.0);
            }
            else
            {
                Visibility = TraceSceneVisibility(RayOrigin, SunDir, RayEpsilon, 400.0);
            }
        }
        else
        {
            Visibility = ResolveCascadeVisibility(Position, Normal);
        }
    }
    let CloudTransmission = CloudGroundTransmission(Position);
    Visibility *= CloudTransmission;
    var Radiance = Refresh.SunIlluminance * Visibility * ResolveSunReflectance(Surface, Normal, View, SunDir);
    if (PenumbraFiltered)
    {
        PenumbraTarget[int2(Pixel)] = float4(Refresh.SunIlluminance * CloudTransmission * ResolveSunReflectance(Surface, Normal, View, SunDir), SunOcclusion);
    }
    let Fire = ResolveFireArrival(Position, Normal);
    if (any(Fire.Illuminance > float3(0.0)))
    {
        Radiance += Fire.Illuminance * ResolveDirectReflectance(Surface, Normal, View, Fire.Direction);
    }

    // Live HMI Display Rectangular Area-Light NEE (Tablet + Instrument Cluster) with BVH shadow visibility
    let HmiPair = ProjectTemporalPair(Pixel, Refresh.RefreshIndex, 43u);
    Radiance += EvaluateHmiAreaLightNee(Position, Normal, View, Surface, HmiPair, UseRtShadows) * InversePi;

    // 3D Clustered light evaluations (bitmask-culled point lights, roadway IES street lamps, and vehicle beams)
    let CellIdx = ResolveClusterCellIndex(SceneLights[0].GridMinimum, SceneLights[0].GridScale, Position);
    if (CellIdx != -1)
    {
        let Pair = SceneLights[0].CellMasks[ToUnsigned(max(CellIdx, 0)) >> 1u];
        let Mask2 = ResolveClusterMaskFromPair(CellIdx, Pair, SceneLights[0].GridMinimum.w);
        if ((Mask2.x | Mask2.y) != 0u)
        {
            var ClusterRad = float3(0.0);
            var BestContrib = float3(0.0);
            var BestDir = float3(0.0, 0.0, 1.0);
            var BestDist = 0.0;
            var BestLum = 0.0;

            for (var Word = 0u; Word < 2u; Word += 1u)
            {
                var Mask = SelectBranch(Mask2.x, Mask2.y, Word == 1u);
                let Base = Word * 32u;
                while (Mask != 0u)
                {
                    let Bit = firstbitlow(Mask);
                    Mask     &= Mask - 1u;
                    let Light = SceneLights[0].Lights[Base + Bit];
                    let LightIllum = EvaluateLightIlluminance(Light, Position, Normal);
                    if (any(LightIllum > float3(0.0)))
                    {
                        let Offset = Light.PositionType.xyz - Position;
                        let LightDist = length(Offset);
                        let TowardLight = Offset / max(LightDist, 1.0e-4);
                        let Contrib = LightIllum * ResolveDirectReflectance(Surface, Normal, View, TowardLight);
                        ClusterRad     += Contrib;
                        let Lum = dot(Contrib, float3(0.2126, 0.7152, 0.0722));
                        if (Lum > BestLum)
                        {
                            BestLum     = Lum;
                            BestContrib = Contrib;
                            BestDir     = TowardLight;
                            BestDist    = LightDist;
                        }
                    }
                }
            }

            let SunLum = dot(Refresh.SunIlluminance, float3(0.2126, 0.7152, 0.0722));
            if (UseRtShadows && BestLum > 0.04 + 0.015 * SunLum)
            {
                let LightVis = TraceSceneVisibility(RayOrigin, BestDir, RayEpsilon, max(BestDist - 0.65, RayEpsilon));
                ClusterRad  += BestContrib * (LightVis - 1.0);
            }
            Radiance += ClusterRad;
        }
    }

    DirectTarget[int2(Pixel)] = float4(Radiance, 1.0);
}
