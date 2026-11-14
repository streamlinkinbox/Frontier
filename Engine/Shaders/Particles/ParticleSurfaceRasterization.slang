/*============================================================================================================================================
                                                  PARTICLESURFACERASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Vulkan 1.3 GPU studio-lit 3D triangle surface rasterizer for particle props, cloth, shields, domes, slabs & MoGraph solids

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Shaders/Particles/ParticleChannelLayouts.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                                       RESOURCES
//------------------------------------------------------------------------------------------------------------------------

struct GpuSurfaceVertex
{
    float4 PosMetersEmissive; // xyz = WorldPosMeters [m], w = EmissiveGain [-]
    float4 NormalMetallic;    // xyz = WorldNormal [-],    w = MetallicFactor [0..1]
    float4 ColorRgba;         // rgba = Base linear albedo/opacity
};

[[vk::binding(0, 0)]]
ConstantBuffer<GpuParticleViewportUniforms> Viewport;

[[vk::binding(1, 0)]]
StructuredBuffer<GpuSurfaceVertex> SurfaceVertexBuffer;

struct SurfaceVaryings
{
    float4 ClipPosition   : SV_Position;
    float3 WorldPos       : TEXCOORD0;
    float3 WorldNormal    : TEXCOORD1;
    float4 ColorRgba      : COLOR0;
    float2 EmissiveMetal  : TEXCOORD2;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENTRY POINTS
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
SurfaceVaryings ParticleSurfaceVertex(uint VertexId : SV_VulkanVertexID)
{
    SurfaceVaryings Out;
    Out.ClipPosition  = float4(0.0, 0.0, 2.0, 1.0);
    Out.WorldPos      = float3(0.0, 0.0, 0.0);
    Out.WorldNormal   = float3(0.0, 0.0, 1.0);
    Out.ColorRgba     = float4(0.0, 0.0, 0.0, 0.0);
    Out.EmissiveMetal = float2(0.0, 0.0);

    if (VertexId >= Viewport.ViewportControl.x)
    {
        return Out;
    }

    const GpuSurfaceVertex Vert = SurfaceVertexBuffer[VertexId];
    const float3 Pos            = Vert.PosMetersEmissive.xyz;

    Out.ClipPosition = float4(
        dot(Viewport.ViewProjRow0, float4(Pos, 1.0)),
        dot(Viewport.ViewProjRow1, float4(Pos, 1.0)),
        dot(Viewport.ViewProjRow2, float4(Pos, 1.0)),
        dot(Viewport.ViewProjRow3, float4(Pos, 1.0)));
    Out.WorldPos      = Pos;
    Out.WorldNormal   = Vert.NormalMetallic.xyz;
    Out.ColorRgba     = Vert.ColorRgba;
    Out.EmissiveMetal = float2(Vert.PosMetersEmissive.w, Vert.NormalMetallic.w);
    return Out;
}

[shader("fragment")]
float4 ParticleSurfaceFragment(SurfaceVaryings In) : SV_Target0
{
    const float Alpha = saturate(In.ColorRgba.a);
    if (Alpha <= 0.004)
    {
        discard;
    }

    const float3 ViewDir = normalize(Viewport.CameraPosTime.xyz - In.WorldPos);
    float3 N             = In.WorldNormal;
    const float NLen     = length(N);
    N = (NLen > 1e-5) ? (N / NLen) : float3(0.0, 0.0, 1.0);
    if (dot(N, ViewDir) < 0.0)
    {
        N = -N;
    }

    // Studio 3-point lighting (+Z Up)
    const float3 KeyDir  = normalize(float3(0.56, -0.44, 0.70));
    const float3 FillDir = normalize(float3(-0.62, 0.50, 0.42));
    const float3 RimDir  = normalize(float3(-0.35, -0.78, 0.52));

    const float KeyDiff  = saturate(dot(N, KeyDir));
    const float FillDiff = saturate(dot(N, FillDir));
    const float HemiSky  = 0.5 + 0.5 * N.z;
    const float Fresnel  = pow(1.0 - saturate(dot(N, ViewDir)), 3.0);

    const float3 HalfKey = normalize(KeyDir + ViewDir);
    const float3 HalfRim = normalize(RimDir + ViewDir);
    const float Metal    = saturate(In.EmissiveMetal.y);
    const float Shininess = lerp(24.0, 96.0, Metal);
    const float SpecKey  = pow(saturate(dot(N, HalfKey)), Shininess);
    const float SpecRim  = pow(saturate(dot(N, HalfRim)), 32.0);

    const float3 BaseRgb = In.ColorRgba.rgb;
    const float3 Diffuse = BaseRgb * (0.28 + 0.24 * HemiSky + 0.88 * KeyDiff + 0.28 * FillDiff);
    const float3 SpecCol = lerp(float3(0.95, 0.97, 1.0), BaseRgb * 1.35 + float3(0.22, 0.24, 0.28), Metal);
    const float3 Specular = SpecCol * (SpecKey * lerp(0.28, 1.25, Metal) + SpecRim * lerp(0.18, 0.65, Metal));
    const float3 RimGlow  = lerp(float3(0.16, 0.22, 0.30), BaseRgb + float3(0.25, 0.30, 0.38), Metal) * (Fresnel * lerp(0.24, 0.72, Metal));

    const float Emissive = max(In.EmissiveMetal.x, 0.0);
    // The exposure control only drives the live particle sprites; reference props keep their authored
    // albedo so the studio-lit solids are not washed out or crushed by per-preset sprite exposure fits.
    const float Boost = 1.0;
    // If purely emissive (e.g. shield / dome / aurora / Einstein ring), emphasize Fresnel & radiance cleanly
    float3 LitRgb = Diffuse * lerp(1.0, 0.35, Metal * 0.65) + Specular + RimGlow + BaseRgb * Emissive;
    (void)Boost;

    if (Viewport.ViewportControl.z == 1u)
    {
        const float EdgeBoost = 0.45 + 0.95 * Fresnel;
        const float3 AddRgb   = (BaseRgb * (0.45 + Emissive) * EdgeBoost + Specular * 0.45) * Alpha;
        return float4(AddRgb, Alpha);
    }

    return float4(LitRgb, Alpha);
}
