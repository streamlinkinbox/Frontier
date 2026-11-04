//============================================================================================================================================
//                                                        ENVIRONMENTLIGHTING.SLANG
//============================================================================================================================================
// 🧩 The PlanetaryAtmosphere environment as the refresh's light inputs: its sky into the refresh's sky view, its cloud shadow map
//    into the refresh's projected cloud shadows — the two inputs the ported units already sample.

#pragma once
#include "IntrinsicParity.slang"
#include "SkyProjection.slang"

// 📝 Not part of the VehicleHMI demo: with the environment lighting the scene these units overwrite the demo's own sky view and its
//    idle cloud shadow map each refresh; every ported unit keeps sampling them exactly as before (SampleSkyRadiance,
//    CloudGroundTransmission). Without the environment neither runs and the demo's inputs stay in place.

struct EnvironmentLightingProfile
{
    float4 SunDirection;   // xyz the environment's primary sun on its y-up axes, w 1 / RadianceScale (environment → refresh units)
    float4 Planet;         // x ground radius [km], y observer radius [km], z realm layer, w sky weight (0: no environment sky)
    float4 CloudSpan;      // x half span of the environment's cloud shadow map [m], y realm layer, z shadow weight, w unused
};
  [[vk::binding(0, 0)]] ConstantBuffer<EnvironmentLightingProfile> Lighting;
  [[vk::binding(1, 0)]] Texture2DArray<float4>                     EnvironmentSkyView;       // radiance, environment units
  [[vk::binding(2, 0)]] Texture2DArray<float4>                     EnvironmentCloudShadow;   // optical depth, transmittance, height
  [[vk::binding(3, 0)]] SamplerState                               EnvironmentSampler;
  [[vk::binding(4, 0)]] [format("rgba16f")] RWTexture2D<float4>    SkyTarget;                // the refresh's sky view
  [[vk::binding(5, 0)]] [format("rgba16f")] RWTexture2D<float4>    CloudShadowTarget;        // the refresh's cloud shadow map

static const float EnvironmentPi = 3.14159265358979;

// 📝 The illumination world (x east, y north, z up) on the environment's axes (x east, y up, z south).
float3 ProjectLightingAxes(float3 Vector)
{
    return float3(Vector.x, Vector.z, -Vector.y);
}

// 📝 The environment's own sky-view lookup (AtmosphereScattering.slang DirectionToSkyViewUv), term for term: azimuth from the sun on
//    U, limb-relative square-root elevation on V — so the refresh sees exactly the sky the environment's dome draws.
float2 ProjectEnvironmentSkyCoordinates(float GroundRadius, float ObserverRadius, float3 Direction, float3 Sun)
{
    let Clamped = max(ObserverRadius, GroundRadius + 1.0e-4);
    let Limb    = -asin(clamp(GroundRadius / Clamped, 0.0, 1.0));
    let View    = asin(clamp(Direction.y, -1.0, 1.0));
    var V = 0.5;
    if (View < Limb)
    {
        V = 0.5 - 0.5 * sqrt(saturate((View - Limb) / max(-0.5 * EnvironmentPi - Limb, -1.0e-4)));
    }
    else
    {
        V = 0.5 + 0.5 * sqrt(saturate((View - Limb) / max(0.5 * EnvironmentPi - Limb, 1.0e-4)));
    }
    let ViewAcross = float2(Direction.x, Direction.z);
    let SunAcross  = float2(Sun.x, Sun.z);
    var Azimuth = 0.0;
    if (length(ViewAcross) > 1.0e-5 && length(SunAcross) > 1.0e-5)
    {
        Azimuth = acos(clamp(dot(ViewAcross / length(ViewAcross), SunAcross / length(SunAcross)), -1.0, 1.0));
    }
    return saturate(float2(Azimuth / EnvironmentPi, V));
}

// 📝 Every texel of the refresh's sky view (SkyProjection.slang parametrisation) takes the environment's sky radiance in that
//    direction, converted to the refresh's radiometric scale.
[shader("compute")]
[numthreads(8, 8, 1)]
void ProjectEnvironmentSky(uint3 Thread : SV_DispatchThreadID)
{
    uint Width;
    uint Height;
    SkyTarget.GetDimensions(Width, Height);
    if (Thread.x >= Width || Thread.y >= Height)
    {
        return;
    }
    let Coordinates = (float2(Thread.xy) + 0.5) / float2(float(Width), float(Height));
    let Direction   = ProjectLightingAxes(ReconstructSkyDirection(Coordinates));
    let Uv          = ProjectEnvironmentSkyCoordinates(Lighting.Planet.x, Lighting.Planet.y, Direction, Lighting.SunDirection.xyz);
    let Radiance    = EnvironmentSkyView.SampleLevel(EnvironmentSampler, float3(Uv, Lighting.Planet.z), 0.0).rgb;
    SkyTarget[int2(Thread.xy)] = float4(max(Radiance, float3(0.0)) * (Lighting.SunDirection.w * Lighting.Planet.w), 1.0);
}

// 📝 The refresh's cloud shadow map covers the environment map's square: world (−H, −H) + uv·2H on z = 0, whose rows run north
//    where the environment's run south. x holds the optical depth whose e^−τ is the environment's own (multiple-scattering)
//    transmittance; no local medium (y = 0), so CloudGroundTransmission attenuates every receiver below the slab by it.
[shader("compute")]
[numthreads(8, 8, 1)]
void ProjectCloudShadow(uint3 Thread : SV_DispatchThreadID)
{
    uint Width;
    uint Height;
    CloudShadowTarget.GetDimensions(Width, Height);
    if (Thread.x >= Width || Thread.y >= Height)
    {
        return;
    }
    let Coordinates = (float2(Thread.xy) + 0.5) / float2(float(Width), float(Height));
    let Environment = float2(Coordinates.x, 1.0 - Coordinates.y);
    let Sample      = EnvironmentCloudShadow.SampleLevel(EnvironmentSampler, float3(Environment, Lighting.CloudSpan.y), 0.0);
    let Depth       = -log(clamp(Sample.y, 1.0e-4, 1.0)) * Lighting.CloudSpan.z;
    CloudShadowTarget[int2(Thread.xy)] = float4(Depth, 0.0, 0.0, 0.0);
}
