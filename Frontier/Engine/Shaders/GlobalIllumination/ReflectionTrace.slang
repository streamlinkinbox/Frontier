//============================================================================================================================================
//                                                         REFLECTIONTRACE.SLANG
//============================================================================================================================================
// 🧩 Glossy reflection rays: a GGX visible-normal sample per resampling pixel, traced through the scene distance field.

#pragma once
#include "IntrinsicParity.slang"
#include "HostConstants.slang"

// 📝 The specular counterpart of the indirect candidate: the reflected ray reads card radiance where it lands and the
//    sky where it escapes. Surfaces rougher than RoughnessCutoff are left to the resolve, which substitutes the
//    diffuse irradiance there exactly as Lumen reuses its diffuse gather for rough reflections.

#include "CardSampling.slang"
#include "SurfaceReflectance.slang"
#include "PackingCodec.slang"
#include "DiscrepancySequence.slang"
#include "RayIntersection.slang"

  [[vk::binding(1, 0)]] StructuredBuffer<MaterialProfile> Materials;
  [[vk::binding(2, 0)]] Texture2D<float4>        SurfaceAttributes;
  [[vk::binding(3, 0)]] Texture2D<float>             SurfaceDepth;
  [[vk::binding(4, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4>         ReflectionTarget;

static const float RoughnessCutoff = 0.5;

 
[shader("compute")]
[numthreads(8, 8, 1)]
void TraverseReflection(uint3 Invocation : SV_DispatchThreadID)
{
    let TracePixel = Invocation.xy;
    if (any(float2(TracePixel) >= Refresh.TraceExtent))
    {
        return;
    }
    let ScreenPixel = min(int2(TracePixel * Refresh.TraceScale), int2(Refresh.ScreenExtent) - int2(1));
    let Depth = LoadTexel(SurfaceDepth, ScreenPixel, 0);
    if (Depth <= 0.0)
    {
        ReflectionTarget[int2(TracePixel)] = float4(0.0);
        return;
    }
    let Attributes = LoadTexel(SurfaceAttributes, ScreenPixel, 0);
    let Surface = ProjectSurfaceSample(Materials[ToUnsigned(Attributes.z + 0.5)]);
    let Roughness = SelectBranch(Surface.Roughness, Surface.CoatRoughness, Surface.CoatWeight > 0.0);
    let UseRtReflections = FeatureEnabled(Refresh, FeatureRayTracing) && FeatureEnabled(Refresh, FeatureRayTracedReflections);
    let Cutoff = SelectBranch(RoughnessCutoff, ResolveReflectionRoughness(Refresh), UseRtReflections);
    if (Roughness > Cutoff)
    {
        ReflectionTarget[int2(TracePixel)] = float4(0.0);
        return;
    }
    let Position = ReconstructWorldPosition(float2(ScreenPixel) + 0.5, Depth);
    let Normal = DecodeOctahedral(Attributes.xy);
    let View = normalize(Refresh.CameraPosition - Position);
    let Basis3 = ConstructTangentBasis(Normal);
    let TangentView = mul(View, transpose(Basis3));
    let Pair = ProjectTemporalPair(TracePixel, Refresh.RefreshIndex, 29u);
    let Half = mul(ProjectVisibleNormal(TangentView, Roughness * Roughness, Pair), Basis3);
    var Direction = reflect(-View, Half);
    if (dot(Direction, Normal) <= 0.0)
    {
        Direction = reflect(-View, Normal);
    }

    var Radiance = float3(0.0);
    if (UseRtReflections)
    {
        var Seed = ConstructPixelSeed(TracePixel, Refresh.RefreshIndex, 41u);
        Radiance = TraceMultiBounceReflection(Position + Normal * RayEpsilon, Direction, Roughness, Seed);
    }
    else
    {
        let Bias = ResolveSurfaceBias(Position);
        let Hit = TraverseSceneDistance(Position + Normal * Bias, Direction, 0.5 * Bias, 300.0, Roughness * 0.05);
        Radiance = ResolveMarchRadiance(Hit, Direction);
        if (!Hit.Hit && Roughness<0.25) { Radiance += ResolveCelestialRadiance(Direction,MoonAtlas,SkySampler,ResolveReflectedDiscWeight())*(1.0-smoothstep(0.05,0.25,Roughness)); }
    }
    ReflectionTarget[int2(TracePixel)] = float4(min(Radiance, float3(200.0)), 1.0);
}

// 📝 RefinementMirrorTrace: surfaces smoother than MirrorTraceRoughness traced once per screen pixel into a screen-resolution target
//    (alpha 1 where traced) that the recombination takes unfiltered; the half-resolution trace and its variance filter still serve
//    every rougher surface. Other pixels leave alpha 0.
static const float MirrorTraceRoughness = 0.20;

[shader("compute")]
[numthreads(8, 8, 1)]
void TraverseMirrorReflection(uint3 Invocation : SV_DispatchThreadID)
{
    let ScreenPixel = int2(Invocation.xy);
    if (any(float2(ScreenPixel) >= Refresh.ScreenExtent))
    {
        return;
    }
    let Depth = LoadTexel(SurfaceDepth, ScreenPixel, 0);
    if (Depth <= 0.0)
    {
        ReflectionTarget[ScreenPixel] = float4(0.0);
        return;
    }
    let Attributes = LoadTexel(SurfaceAttributes, ScreenPixel, 0);
    let Surface = ProjectSurfaceSample(Materials[ToUnsigned(Attributes.z + 0.5)]);
    let Roughness = SelectBranch(Surface.Roughness, Surface.CoatRoughness, Surface.CoatWeight > 0.0);
    let Reference = RefinementEnabled(Refresh, RefinementMirrorReference);
    let Limit = SelectBranch(MirrorTraceRoughness, ResolveReflectionRoughness(Refresh), Reference);
    if (Roughness >= Limit || !FeatureEnabled(Refresh, FeatureRayTracing) || !FeatureEnabled(Refresh, FeatureRayTracedReflections))
    {
        ReflectionTarget[ScreenPixel] = float4(0.0);
        return;
    }
    let Position = ReconstructWorldPosition(float2(ScreenPixel) + 0.5, Depth);
    let Normal = DecodeOctahedral(Attributes.xy);
    let View = normalize(Refresh.CameraPosition - Position);
    let Basis3 = ConstructTangentBasis(Normal);
    let TangentView = mul(View, transpose(Basis3));
    let Pair = ProjectTemporalPair(uint2(ScreenPixel), Refresh.RefreshIndex, 31u);
    var Seed = ConstructPixelSeed(uint2(ScreenPixel), Refresh.RefreshIndex, 47u);
    // 📝 Measurement reference: 16 R2 points of the lobe per pixel instead of one.
    let Samples = SelectBranch(1u, 16u, Reference);
    var Radiance = float3(0.0);
    for (var Sample = 0u; Sample < Samples; Sample++)
    {
        let Point = SelectBranch(Pair, frac(Pair + float(Sample) * float2(0.7548776662466927, 0.5698402909980532)), Reference);
        let Half = mul(ProjectVisibleNormal(TangentView, Roughness * Roughness, Point), Basis3);
        var Direction = reflect(-View, Half);
        if (dot(Direction, Normal) <= 0.0)
        {
            Direction = reflect(-View, Normal);
        }
        Radiance += min(TraceMultiBounceReflection(Position + Normal * RayEpsilon, Direction, Roughness, Seed), float3(200.0));
    }
    ReflectionTarget[ScreenPixel] = float4(Radiance / float(Samples), 1.0);
}

