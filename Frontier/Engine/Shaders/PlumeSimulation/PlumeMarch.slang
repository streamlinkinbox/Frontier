//============================================================================================================================================
//                                                          PLUMEMARCH.SLANG
//============================================================================================================================================
// 🧩 Scattering quadrature of the smoke-and-fire lattice at trace resolution: sun, sky, fire light, flame emission and transmittance. A port of
//    PlumeMarch.wgsl.

#pragma once
#include "IntrinsicParity.slang"
#include "RefreshDescriptor.slang"
#include "PlumeLayouts.slang"
#include "SkyProjection.slang"

// 📝 Per resampling pixel, the camera ray is clipped to the coverage box the transport wrote and to the opaque depth, then marched at about one
//    sample per cell from a per-pixel offset that rotates every refresh, so the temporal antialiasing integrates the quadrature noise away. What
//    each sample receives:
//
//    | Term             | How                                                                                       |
//    |------------------|-------------------------------------------------------------------------------------------|
//    | sun              | cascaded shadow depth × the light lattice's transmittance, in three scattering octaves     |
//    |                  | (Wrenninge): each octave sees a thinner medium and a rounder phase, so dense smoke glows   |
//    |                  | through instead of turning grey                                                           |
//    | sky              | the sky view averaged, occluded by the zenith transmittance — a thick column has a dark   |
//    |                  | belly — plus a sun-lit ground bounce from below                                           |
//    | fire light       | the burning tyres and exhaust pops as point lights inside the volume: smoke above a fire  |
//    |                  | is lit orange from beneath                                                                |
//    | flame            | blackbody emission at a temperature between tip and core set by the cell's heat,          |
//    |                  | flickering with the detail noise                                                          |
//
//    Optics mix per sample: rubber smoke scatters (albedo ≈ 0.9), soot absorbs (≈ 0.1). Sub-cell detail erodes the trilinear blobs of the lattice
//    with a tiling noise volume drifting with the wind and the rise of the plume. The result is premultiplied radiance and transmittance,
//    composited before antialiasing by the recombination.

struct PlumeCascadeReceiver
{
    float4x4 LightViewProjection[3];
    float4 Splits;      // [m] xyz far distance of each cascade, w cascade count
    float4 TexelWorld;  // [m] world size of one shadow texel per cascade
};

  [[vk::binding(1, 0)]]  ConstantBuffer<PlumeConstants>         Plume;
  [[vk::binding(2, 0)]]  Texture3D<float4>                      ThermoState;
  [[vk::binding(3, 0)]]  SamplerState                           LatticeSampler;
  [[vk::binding(4, 0)]]  Texture2D<float>                       SurfaceDepth;
  [[vk::binding(5, 0)]]  StructuredBuffer<int>                  Coverage;
  [[vk::binding(6, 0)]]  Texture2DArray<float>                  ShadowDepth;
  [[vk::binding(7, 0)]]  SamplerComparisonState                 ShadowSampler;
  [[vk::binding(8, 0)]]  ConstantBuffer<PlumeCascadeReceiver>   Receiver;
  [[vk::binding(9, 0)]]  Texture2D<float4>                      SkyView;
  [[vk::binding(10, 0)]] SamplerState                           SkySampler;
  [[vk::binding(11, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> PlumeTarget;
  [[vk::binding(12, 0)]] Texture3D<float4>                      LightState;
  [[vk::binding(13, 0)]] ConstantBuffer<FireLightConstants>     FireLights;
  [[vk::binding(14, 0)]] Texture3D<float4>                      DetailState;
  [[vk::binding(15, 0)]] SamplerState                           DetailSampler;

float4 SampleThermo(float3 Position)
{
    return max(ThermoState.SampleLevel(LatticeSampler, ProjectThermoCoordinates(Plume, Position), 0.0), float4(0.0));
}

// 📐 Henyey–Greenstein, normalised over the sphere.
float ResolveHenyeyGreenstein(float Cosine, float Anisotropy)
{
    let Squared = Anisotropy * Anisotropy;
    return (1.0 - Squared) / (12.56637061436 * pow(max(1.0 + Squared - 2.0 * Anisotropy * Cosine, 1.0e-4), 1.5));
}

// 📐 A forward lobe (g) mixed 72 % with a backward one (−0.65 g), which is what gives a back-lit plume its silver lining.
float ResolvePhase(float Cosine, float Anisotropy)
{
    return lerp(ResolveHenyeyGreenstein(Cosine, -0.65 * Anisotropy), ResolveHenyeyGreenstein(Cosine, Anisotropy), 0.72);
}

// 📝 One hardware-filtered comparison in the cascade that holds the sample; beyond the last cascade the sun is seen.
float ResolveSunVisibility(float3 Position)
{
    let ViewDistance = ResolveViewDepth(Position);
    let Count = ToUnsigned(Receiver.Splits.w);
    if (Count == 0u || ViewDistance > Receiver.Splits[Count - 1u])
    {
        return 1.0;
    }
    var Cascade = 0u;
    while (Cascade + 1u < Count && ViewDistance > Receiver.Splits[Cascade])
    {
        Cascade++;
    }
    let Clip = mul(float4(Position, 1.0), Receiver.LightViewProjection[Cascade]);
    let Ndc = Clip.xyz / Clip.w;
    let Coordinates = float2(Ndc.x * 0.5 + 0.5, 0.5 - Ndc.y * 0.5);
    if (any(Coordinates < float2(0.0)) || any(Coordinates > float2(1.0)) || Ndc.z > 1.0)
    {
        return 1.0;
    }
    return ShadowDepth.SampleCmpLevelZero(ShadowSampler, float3(Coordinates, float(Cascade)), Ndc.z);
}

// 📝 Transmittance toward the sun (x) and the zenith (y) through the plume, resolved once per coarse cell by the transport
//    (PlumeAdvection.slang ResolveSunTransmittance) and read here with one filtered sample.
float2 ResolveSelfShadow(float3 Position)
{
    return clamp(LightState.SampleLevel(LatticeSampler, ProjectVelocityCoordinates(Plume, Position), 0.0).xy, float2(0.0), float2(1.0));
}

// 📝 Isotropic sky term: the zenith and four horizon directions of the sky view.
float3 ResolveSkyAmbient()
{
    var Sky = SkyView.SampleLevel(SkySampler, ProjectSkyCoordinates(float3(0.0, 0.0, 1.0)), 0.0).rgb * 2.0;
    for (var Side = 0u; Side < 4u; Side++)
    {
        let θ = float(Side) * 1.5707963;
        Sky += SkyView.SampleLevel(SkySampler, ProjectSkyCoordinates(normalize(float3(cos(θ), sin(θ), 0.15))), 0.0).rgb;
    }
    return (Sky / 6.0) * Refresh.SkyScale + Refresh.NightSkyRadiance;
}

// 📝 The burning tyres and exhaust pops as softened point lights: irradiance over 4π, the phase taken as isotropic.
float3 ResolveFireLight(float3 Position)
{
    var Light = float3(0.0);
    for (var Index = 0u; Index < min(FireLights.Count.x, 4u); Index++)
    {
        let Offset = FireLights.PositionRadius[Index].xyz - Position;
        let Radius = FireLights.PositionRadius[Index].w;
        Light += FireLights.Radiance[Index].rgb / (dot(Offset, Offset) + Radius * Radius);
    }
    return Light * (0.0795774715 * Plume.Scattering.w);
}

// 📐 Sub-cell detail: three noise octaves (one per channel) folded into a value around 0.5, drifting downwind and upward with the plume so the
//    detail reads as motion below the lattice. Erosion takes density away where the noise is low, more of it where the cell is thin — edges turn
//    to wisps while a dense core keeps its mass.
float4 ResolveDetail(float3 Position)
{
    let Time = Plume.Wind.w;
    let Drift = float3(-Plume.Wind.x * 0.35, -Plume.Wind.y * 0.35, -0.55) * Time;
    let Coordinates = (Position + Drift) * Plume.Detail.y;
    return DetailState.SampleLevel(DetailSampler, Coordinates, 0.0);
}

float ErodeDensity(float Density, float Noise)
{
    let Strength = Plume.Detail.x;
    let Thin = 1.0 - clamp(Density * 0.8, 0.0, 1.0);
    return max(0.0, Density * (1.0 + Strength * (Noise - 0.5) * 1.6) - Strength * Thin * (1.0 - Noise) * 0.35 * min(Density, 1.0));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void MarchPlume(uint3 Invocation : SV_DispatchThreadID)
{
    let TracePixel = int2(Invocation.xy);
    if (any(float2(TracePixel) >= Refresh.TraceExtent))
    {
        return;
    }
    let Low  = int3(Coverage[0], Coverage[1], Coverage[2]);
    let High = int3(Coverage[4], Coverage[5], Coverage[6]);
    if (any(Low > High))
    {
        PlumeTarget[uint2(TracePixel)] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // ① the ray, clipped to the coverage box (grown by a cell) and to the opaque surface
    let Edge = Plume.Lattice.x;
    let Origin = float3(float2(2 * Plume.VelocityOrigin.xy) * Edge, Plume.Lattice.z);
    let BoxMinimum = Origin + (float3(Low) - 1.0) * Edge;
    let BoxMaximum = Origin + (float3(High) + 2.0) * Edge;
    let ScreenExtent = int2(ToSigned(Refresh.ScreenExtent.x), ToSigned(Refresh.ScreenExtent.y));
    let ScreenPixel = min(TracePixel * ToSigned(Refresh.TraceScale), ScreenExtent - int2(1));
    let Depth = LoadTexel(SurfaceDepth, ScreenPixel, 0);
    let Far = ReconstructWorldPosition(float2(ScreenPixel) + 0.5, max(Depth, 1.0e-6));
    let Toward = Far - Refresh.CameraPosition;
    let Reach = length(Toward);
    let Direction = Toward / Reach;
    let Opaque = SelectBranch(Reach, 1.0e5, Depth <= 0.0);
    let Inverse = 1.0 / SelectBranch(Direction, float3(1.0e-9), abs(Direction) < float3(1.0e-9));
    let Near = (BoxMinimum - Refresh.CameraPosition) * Inverse;
    let Distant = (BoxMaximum - Refresh.CameraPosition) * Inverse;
    let Enter = max(max(min(Near.x, Distant.x), min(Near.y, Distant.y)), max(min(Near.z, Distant.z), 0.0));
    let Exit  = min(min(max(Near.x, Distant.x), max(Near.y, Distant.y)), min(max(Near.z, Distant.z), Opaque));
    if (Exit <= Enter)
    {
        PlumeTarget[uint2(TracePixel)] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // ② about one sample per cell, from an interleaved-gradient offset rotated by the golden ratio each refresh
    let Count = ToUnsigned(clamp(ceil((Exit - Enter) / (0.9 * Edge)), 4.0, max(Plume.Optical.w, 4.0)));
    let Step = (Exit - Enter) / float(Count);
    let Offset = frac(52.9829189 * frac(0.06711056 * float(TracePixel.x) + 0.00583715 * float(TracePixel.y))
                      + 0.61803398875 * float(Refresh.RefreshIndex % 256u));
    let Cosine = dot(Direction, Refresh.SunDirection);
    let Anisotropy = Plume.Scattering.y;
    let PhaseOctaves = float3(ResolvePhase(Cosine, Anisotropy), ResolvePhase(Cosine, 0.5 * Anisotropy), ResolvePhase(Cosine, 0.25 * Anisotropy));
    let SkyAmbient = ResolveSkyAmbient() * Plume.Scattering.z;
    let GroundBounce = Refresh.SunIlluminance * (max(Refresh.SunDirection.z, 0.0) * 0.12 * InversePi * Plume.Scattering.z);
    let Cells = Plume.ThermoExtent.w == 1u;
    let Strength = Plume.Scattering.x;
    let Flicker = 0.5 + 0.5 * sin(Plume.Wind.w * 23.0);

    var Radiance = float3(0.0);
    var Transmittance = 1.0;
    for (var Index = 0u; Index < Count; Index++)
    {
        if (Transmittance < 0.01)
        {
            break;
        }
        let Position = Refresh.CameraPosition + Direction * (Enter + (float(Index) + Offset) * Step);
        var Thermo = SampleThermo(Position);
        if (Thermo.r < 1.0e-3 && Thermo.a < 1.0e-3 && Thermo.b < 1.0e-3)
        {
            continue;
        }
        if (Cells)
        {
            // 🔍 the lattice itself: smoke cyan, soot violet, heat red, flame yellow, unlit
            let Absorbed = 1.0 - exp(-(Thermo.r * 1.5 + Thermo.a * 1.5 + Thermo.b) * Step);
            let Colour = float3(0.35, 0.75, 1.0) * min(Thermo.r, 2.0) + float3(0.55, 0.3, 0.9) * min(Thermo.a, 2.0)
                       + float3(1.0, 0.25, 0.05) * min(Thermo.g * 0.02, 2.0) + float3(1.0, 0.85, 0.2) * min(Thermo.b, 2.0);
            Radiance += Transmittance * Colour * Absorbed * 4.0;
            Transmittance *= 1.0 - Absorbed;
            continue;
        }

        // ③ sub-cell detail on both particulates; the flame flickers with the noise's fourth channel
        if (Plume.Detail.x > 0.0)
        {
            let Noise = ResolveDetail(Position);
            let Folded = dot(Noise.rgb, float3(0.5, 0.3, 0.2));
            Thermo.r = ErodeDensity(Thermo.r, Folded);
            Thermo.a = ErodeDensity(Thermo.a, Folded);
            Thermo.b *= lerp(1.0, 0.45 + 1.1 * lerp(Noise.a, Flicker, 0.25), min(Plume.Detail.x, 1.0));
        }

        // ④ optics of the mixture
        let SmokeExtinction = Thermo.r * Plume.Optical.x;
        let SootExtinction = Thermo.a * Plume.Optical.z;
        let Particulate = SmokeExtinction + SootExtinction;
        let Extinction = Particulate + Thermo.b * 0.8;
        if (Extinction < 1.0e-4)
        {
            continue;
        }
        let Absorbed = 1.0 - exp(-Extinction * Step);
        let Albedo = (SmokeExtinction * Plume.Optical.y + SootExtinction * Plume.Sun.w) / max(Particulate, 1.0e-5);
        let Share = Particulate / Extinction;

        // ⑤ sun in three octaves, sky occluded from above, fire light from within
        let Shadow = ResolveSelfShadow(Position);
        let Visibility = ResolveSunVisibility(Position);
        // 📝 Each further octave is light that scattered again inside the medium, so it is weighted by the albedo once more: white rubber smoke
        //    glows through, soot — which absorbs at every event — keeps only its single bounce.
        let Octaves = PhaseOctaves.x * Shadow.x + Strength * Albedo * (0.6 * PhaseOctaves.y * pow(Shadow.x, 0.25)
                                                                       + 0.36 * PhaseOctaves.z * pow(Shadow.x, 0.0625));
        let Sunlight = Refresh.SunIlluminance * (Visibility * Octaves);
        let Sky = SkyAmbient * (0.18 + 0.82 * pow(Shadow.y, 0.6)) + GroundBounce;
        let Scattered = (Sunlight + Sky + ResolveFireLight(Position)) * (Albedo * Share * Absorbed);

        // ⑥ flame: blackbody between tip and core, brighter where the gas is hotter
        let Heat = Thermo.g / (Thermo.g + Plume.Flame.z);
        let Temperature = lerp(Plume.Flame.y, Plume.Flame.x, Heat);
        let Emission = ResolveBlackbodyRadiance(Temperature) * (Plume.SourceMaximum.w * min(Thermo.b, 1.5) * Step);

        Radiance += Transmittance * (Scattered + Emission);
        Transmittance *= 1.0 - Absorbed;
    }
    PlumeTarget[uint2(TracePixel)] = float4(Radiance, Transmittance);
}
