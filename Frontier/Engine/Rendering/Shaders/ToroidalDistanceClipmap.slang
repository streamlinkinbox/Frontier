/*==========================================================================================================================================
                                                 TOROIDALDISTANCECLIPMAP.SLANG
==========================================================================================================================================*/
// 🧩 Four nested toroidal rgba16f 3D signed-distance cascades (SceneClip0..3) and cone-traced indirect irradiance for GTX / non-RT GPUs.

struct ToroidalClipmapConstants
{
    float4 CascadeOriginAndVoxelMetres[4];   // xyz = snapped world origin [m], w = voxel size [m]
    int4   ToroidalScrollTexels[4];          // xyz = toroidal ring offset [texels], w = resolution per axis
    uint4  ViewportDimensions;               // xy = viewport [px], z = cone count, w = max march steps
    float4 SkyAndGroundIrradiance;           // xyz = hemisphere sky radiance, w = ground bounce scale
};

[[vk::binding(0, 0)]] Texture3D<float4>       SceneClip0;
[[vk::binding(1, 0)]] Texture3D<float4>       SceneClip1;
[[vk::binding(2, 0)]] Texture3D<float4>       SceneClip2;
[[vk::binding(3, 0)]] Texture3D<float4>       SceneClip3;
[[vk::binding(4, 0)]] SamplerState            LinearWrapSampler;
[[vk::binding(5, 0)]] StructuredBuffer<float4> SurfacePositions;
[[vk::binding(6, 0)]] StructuredBuffer<float4> SurfaceNormals;
[[vk::binding(7, 0)]] StructuredBuffer<float4> CardAtlasIrradiance;
[[vk::binding(8, 0)]] RWTexture2D<float4>      OutConeIrradiance;
[[vk::push_constant]] ConstantBuffer<ToroidalClipmapConstants> Clipmap;

float SampleCascadeSignedDistance(uint CascadeIndex, float3 WorldMetres, out float3 EmissiveAndCardHint)
{
    float4 OriginVoxel = Clipmap.CascadeOriginAndVoxelMetres[CascadeIndex];
    int4   ScrollRes   = Clipmap.ToroidalScrollTexels[CascadeIndex];
    float  Resolution  = max(1.0, float(ScrollRes.w));
    float  HalfExtent  = 0.5 * Resolution * OriginVoxel.w;
    float3 Local       = (WorldMetres - OriginVoxel.xyz + HalfExtent) / max(1e-4, 2.0 * HalfExtent);
    float3 ToroidalUv  = frac(Local + float3(ScrollRes.xyz) / Resolution);

    float4 Sampled = float4(HalfExtent, 0.0, 0.0, 0.0);
    if (CascadeIndex == 0u)      Sampled = SceneClip0.SampleLevel(LinearWrapSampler, ToroidalUv, 0.0);
    else if (CascadeIndex == 1u) Sampled = SceneClip1.SampleLevel(LinearWrapSampler, ToroidalUv, 0.0);
    else if (CascadeIndex == 2u) Sampled = SceneClip2.SampleLevel(LinearWrapSampler, ToroidalUv, 0.0);
    else                         Sampled = SceneClip3.SampleLevel(LinearWrapSampler, ToroidalUv, 0.0);

    EmissiveAndCardHint = Sampled.yzw;
    return Sampled.x;
}

uint SelectToroidalCascade(float3 WorldMetres)
{
    [unroll]
    for (uint CascadeIndex = 0u; CascadeIndex < 3u; ++CascadeIndex)
    {
        float4 OriginVoxel = Clipmap.CascadeOriginAndVoxelMetres[CascadeIndex];
        float  HalfExtent  = 0.44 * float(Clipmap.ToroidalScrollTexels[CascadeIndex].w) * OriginVoxel.w;
        float3 Delta       = abs(WorldMetres - OriginVoxel.xyz);
        if (max(Delta.x, max(Delta.y, Delta.z)) <= HalfExtent)
            return CascadeIndex;
    }
    return 3u;
}

float3 EvaluateConeDirection(float3 Normal, uint ConeIndex, uint ConeCount)
{
    float GoldenAngle = 2.39996323;
    float Zeta        = (float(ConeIndex) + 0.5) / max(1.0, float(ConeCount));
    float Radial      = sqrt(Zeta);
    float Azimuth     = float(ConeIndex) * GoldenAngle;
    float3 Reference  = abs(Normal.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 Tangent    = normalize(cross(Reference, Normal));
    float3 Bitangent  = cross(Normal, Tangent);
    return normalize(Tangent * (Radial * cos(Azimuth)) +
                     Bitangent * (Radial * sin(Azimuth)) +
                     Normal * sqrt(max(0.0, 1.0 - Zeta)));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void TraceToroidalConeIrradiance(uint3 Dispatch : SV_DispatchThreadID)
{
    if (Dispatch.x >= Clipmap.ViewportDimensions.x || Dispatch.y >= Clipmap.ViewportDimensions.y)
        return;

    uint   LinearPixel = Dispatch.y * Clipmap.ViewportDimensions.x + Dispatch.x;
    float4 PositionHit = SurfacePositions[LinearPixel];
    if (PositionHit.w <= 0.0)
    {
        OutConeIrradiance[Dispatch.xy] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float3 Normal      = normalize(SurfaceNormals[LinearPixel].xyz);
    uint   ConeCount   = clamp(Clipmap.ViewportDimensions.z, 4u, 16u);
    uint   MaxSteps    = clamp(Clipmap.ViewportDimensions.w, 8u, 48u);
    float  TanHalfCone = 0.32;
    float3 Accumulated = float3(0.0, 0.0, 0.0);
    float  AmbientVis  = 0.0;

    for (uint ConeIndex = 0u; ConeIndex < ConeCount; ++ConeIndex)
    {
        float3 Direction     = EvaluateConeDirection(Normal, ConeIndex, ConeCount);
        float  TravelMetres  = Clipmap.CascadeOriginAndVoxelMetres[0].w * 1.25;
        float  Transmittance = 1.0;
        float3 ConeRadiance  = float3(0.0, 0.0, 0.0);

        for (uint Step = 0u; Step < MaxSteps && Transmittance > 0.02; ++Step)
        {
            float3 SamplePoint = PositionHit.xyz + Normal * 0.06 + Direction * TravelMetres;
            uint   Cascade     = SelectToroidalCascade(SamplePoint);
            float3 CardIrr     = float3(0.0, 0.0, 0.0);
            float  SignedDist  = SampleCascadeSignedDistance(Cascade, SamplePoint, CardIrr);
            float  Radius      = max(Clipmap.CascadeOriginAndVoxelMetres[Cascade].w, TravelMetres * TanHalfCone);
            float  Occlusion   = saturate(0.5 - SignedDist / (2.0 * Radius));

            ConeRadiance  += Transmittance * Occlusion * CardIrr;
            Transmittance *= (1.0 - Occlusion * 0.85);
            TravelMetres  += max(Radius * 0.75, abs(SignedDist));
        }

        float3 SkyFallback = Clipmap.SkyAndGroundIrradiance.xyz *
                             lerp(Clipmap.SkyAndGroundIrradiance.w, 1.0, saturate(Direction.z * 0.5 + 0.5));
        Accumulated += ConeRadiance + Transmittance * SkyFallback;
        AmbientVis  += Transmittance;
    }

    float InverseCones = 1.0 / float(ConeCount);
    OutConeIrradiance[Dispatch.xy] = float4(Accumulated * InverseCones, AmbientVis * InverseCones);
}
