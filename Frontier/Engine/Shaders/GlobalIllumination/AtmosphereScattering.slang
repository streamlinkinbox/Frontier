
#pragma once
#include "IntrinsicParity.slang"
#include "HostConstants.slang"
// Hillaire-style isotropic multiple-scattering closure + quadratic sky integration.
#include "SkyProjection.slang"
#include "NativeTwilight.slang"
#include "AtmosphereMedium.slang"
  [[vk::binding(1, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> SkyViewTarget;
  [[vk::binding(2, 0)]] Texture2D<float4> TransmittanceLut;
  [[vk::binding(3, 0)]] Texture2D<float4> MultipleLut;
  [[vk::binding(4, 0)]] SamplerState LutSampler;
  [[vk::binding(5, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> SkyTransmissionTarget;
 
[shader("compute")]
[numthreads(8, 8, 1)]
void IntegrateSkyView(uint3 Invocation : SV_DispatchThreadID)
{
    let Extent = TextureExtent(SkyViewTarget);
    if (any(Invocation.xy >= Extent))
    {
        return;
    }
    if (Atmosphere.Heights.w<0.5){SkyViewTarget[int2(Invocation.xy)] = float4(0.0);SkyTransmissionTarget[int2(Invocation.xy)] = float4(1.0);return;}
    let Samples = ToUnsigned(clamp(Atmosphere.Ground.w,8.0,64.0));
    let Coordinates = (float2(Invocation.xy) + 0.5) / float2(Extent);
    var Direction = ReconstructSkyDirection(Coordinates);
    // 📝 Below the horizon the view is lifted onto it: the scene's own ground answers those rays first.
    Direction.z = max(Direction.z, 0.0);
    Direction = normalize(Direction + float3(0.0, 0.0, 1.0e-4));
    let Origin = float3(0.0, 0.0, GroundRadius + Atmosphere.ObserverAltitude);

    let Ground = IntersectSphere(Origin, Direction, GroundRadius);
    var Travel = IntersectSphere(Origin, Direction, AtmosphereRadius).y;
    if (Ground.x > 0.0)
    {
        Travel = Ground.x;
    }

    let Cosine = dot(Direction, Atmosphere.SunDirection);
    let RayleighPhase = 3.0 / (16.0 * 3.14159265358979) * (1.0 + Cosine * Cosine);
    let g = Atmosphere.Medium.w;
    let g2 = g * g;
    let MiePhase = 3.0 / (8.0 * 3.14159265358979) * ((1.0 - g2) * (1.0 + Cosine * Cosine))
                 / ((2.0 + g2) * pow(1.0 + g2 - 2.0 * g * Cosine, 1.5));

    var Transmittance = float3(1.0);
    var Radiance = float3(0.0);
    for (var Sample = 0u; Sample < Samples; Sample++)
    {
        let A = float(Sample) / float(Samples);
        let B = float(Sample + 1u) / float(Samples);
        let Step = Travel * (B * B - A * A);
        let Point = Origin + Direction * (Travel * 0.5 * (A * A + B * B));
        let Densities = ResolveDensities(length(Point) - GroundRadius);
        let Extinction = ResolveExtinction(Densities);
        let Scattering = RayleighScatter * Atmosphere.Medium.x * Densities.x * RayleighPhase + float3(MieScatter * Atmosphere.Medium.y * Densities.y * MiePhase);
        let SegmentTransmittance = exp(-Extinction * Step);
        // 📝 Energy-conserving segment integral: ∫ T(s)·S ds = S·(1 − T_seg)/σ_t, times the transmittance so far.
        let Integral = (float3(1.0) - SegmentTransmittance) / max(Extinction, float3(1.0e-12));
        let Direct = Scattering * LookupTransmittance(TransmittanceLut, LutSampler, Point, Atmosphere.SunDirection);
        let SigmaS = RayleighScatter * Atmosphere.Medium.x * Densities.x + float3(MieScatter * Atmosphere.Medium.y * Densities.y);
        let Multiple = SigmaS * LookupMultiple(MultipleLut,LutSampler,Point,Atmosphere.SunDirection) * Atmosphere.Heights.z;
        Radiance += Transmittance * (Direct + Multiple) * Integral;
        Transmittance *= SegmentTransmittance;
    }
    SkyTransmissionTarget[int2(Invocation.xy)] = float4(Transmittance, 1.0);
    SkyViewTarget[int2(Invocation.xy)] = float4(Radiance * Atmosphere.SolarIlluminance * Atmosphere.SolarTint.rgb + NativeTwilight(Direction,Atmosphere.SunDirection,Atmosphere.Pad0), 1.0);
}
