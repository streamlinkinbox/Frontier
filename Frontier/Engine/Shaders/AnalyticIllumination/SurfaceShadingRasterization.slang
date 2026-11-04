//============================================================================================================================================
//                                                   SURFACESHADINGRASTERIZATION.SLANG
//============================================================================================================================================
// 🧩 The analytic image: a gradient sky with the sun disc, then every rastered occupant lit in closed form — Lambert diffuse,
//    normalised Blinn-Phong highlights under Schlick Fresnel, a sky/ground hemisphere fill and one shadowed sun — tone mapped (ACES)
//    straight into the presented image.

#pragma once
#include "AnalyticLayouts.slang"

// 📝 ACES filmic fit (Narkowicz 2015) on scene-referred radiance; the sRGB target view encodes the result.
float3 ProjectFilmic(float3 Radiance)
{
    return clamp((Radiance * (2.51 * Radiance + 0.03)) / (Radiance * (2.43 * Radiance + 0.59) + 0.14), float3(0.0), float3(1.0));
}

float3 ResolveSkyRadiance(float3 Direction)
{
    let Height = clamp(Direction.z, -1.0, 1.0);
    if (Height < 0.0) { return lerp(Analytic.SkyHorizon.rgb, Analytic.Ground.rgb * Analytic.SkyHorizon.rgb, saturate(-Height * 4.0)); }
    var Radiance = lerp(Analytic.SkyHorizon.rgb, Analytic.SkyZenith.rgb, pow(Height, 0.45));
    let Alignment = dot(Direction, Analytic.Sun.xyz);
    Radiance += Analytic.SunIrradiance.rgb * 0.08 * pow(saturate(Alignment), 48.0);   // forward-scattering glow around the sun
    if (Alignment >= Analytic.Sun.w) { Radiance += Analytic.SunIrradiance.rgb * 40.0; }   // the disc
    return Radiance;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SKY
//------------------------------------------------------------------------------------------------------------------------

struct SkyVarying
{
    float4 Position : SV_Position;
    [[vk::location(0)]] float2 Ndc : LOCATION0;
};

// 📝 One triangle over the whole target at the far plane (reversed-Z depth 0).
[shader("vertex")]
SkyVarying SkyVertex(uint VertexIndex : SV_VulkanVertexID)
{
    let Corner = float2(float((VertexIndex << 1u) & 2u), float(VertexIndex & 2u));
    var Varying : SkyVarying = {};
    Varying.Ndc = Corner * 2.0 - 1.0;
    Varying.Position = float4(Varying.Ndc, 0.0, 1.0);
    return Varying;
}

[shader("fragment")]
float4 SkyFragment(SkyVarying Varying) : SV_Target0
{
    let Far = mul(float4(Varying.Ndc, 0.0001, 1.0), Analytic.InverseViewProjection);
    let Direction = normalize(Far.xyz / Far.w - Analytic.Camera.xyz);
    return float4(ProjectFilmic(ResolveSkyRadiance(Direction) * Analytic.Camera.w), 1.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SURFACES
//------------------------------------------------------------------------------------------------------------------------

struct ShadingVarying
{
    float4 Position : SV_Position;
    [[vk::location(0)]] float3 World : LOCATION0;
    [[vk::location(1)]] float3 Normal : LOCATION1;
    [[vk::location(2)]] nointerpolation uint Family : LOCATION2;
};

[shader("vertex")]
ShadingVarying ShadingVertex([[vk::location(0)]] float3 Position : LOCATION0, [[vk::location(1)]] uint Family : LOCATION1,
                             [[vk::location(2)]] float3 Normal : LOCATION2, [[vk::location(3)]] uint OccupantOffset : LOCATION3,
                             [[vk::location(4)]] uint OccupantIndex : LOCATION4)
{
    let Occupant = Occupants[OccupantIndex + OccupantOffset];
    let World = mul(float4(Position, 1.0), Occupant.LocalToWorld);
    var Varying : ShadingVarying = {};
    Varying.Position = mul(World, Analytic.ViewProjection);
    Varying.World    = World.xyz;
    Varying.Normal   = mul(float4(Normal, 0.0), Occupant.LocalToWorld).xyz;
    let Override     = Occupant.MaterialOverride;
    Varying.Family   = Override == NoFamilyOverride ? Family : ((Override & FamilyBlockBit) != 0u ? (Override & ~FamilyBlockBit) + Family : Override);
    return Varying;
}

// 📝 Sun visibility: 3x3 comparison taps of the solar depth map (1 lit, 0 shadowed; outside the map counts as lit).
float SampleSolarVisibility(float3 World, float SlopeBias)
{
    let Clip = mul(float4(World, 1.0), Analytic.SolarViewProjection);
    let Ndc  = Clip.xyz / Clip.w;
    let Uv   = float2(Ndc.x * 0.5 + 0.5, 0.5 - Ndc.y * 0.5);
    if (any(Uv < float2(0.0)) || any(Uv > float2(1.0)) || Ndc.z > 1.0) { return 1.0; }
    let Reference = Ndc.z - Analytic.SkyZenith.w - SlopeBias;
    var Visibility = 0.0;
    for (var Y = -1; Y <= 1; Y++)
    {
        for (var X = -1; X <= 1; X++)
        {
            Visibility += SolarDepth.SampleCmpLevelZero(SolarComparison, Uv + float2(float(X), float(Y)) * Analytic.SunIrradiance.w, Reference);
        }
    }
    return Visibility / 9.0;
}

[shader("fragment")]
float4 ShadingFragment(ShadingVarying Varying) : SV_Target0
{
    let Material = Materials[Varying.Family];
    let View = normalize(Analytic.Camera.xyz - Varying.World);
    var Normal = normalize(Varying.Normal);
    if (dot(Normal, View) < 0.0) { Normal = -Normal; }   // two-sided: thin props are lit from the side they are seen from
    let Light   = Analytic.Sun.xyz;
    let Half    = normalize(Light + View);
    let NdotL   = saturate(dot(Normal, Light));
    let NdotH   = saturate(dot(Normal, Half));
    let VdotH   = saturate(dot(View, Half));
    let Alpha     = max(Material.Roughness * Material.Roughness, 0.03);
    let Exponent  = max(2.0 / (Alpha * Alpha) - 2.0, 1.0);
    let Reflectance = lerp(float3(Material.SpecularLevel), Material.Albedo, Material.Metalness);
    let Fresnel   = Reflectance + (1.0 - Reflectance) * pow(1.0 - VdotH, 5.0);
    let Diffuse   = Material.Albedo * (1.0 - Material.Metalness) / Pi;
    let Specular  = Fresnel * ((Exponent + 8.0) / (8.0 * Pi)) * pow(NdotH, Exponent) * Analytic.Ground.w;
    let SlopeBias = Analytic.SunIrradiance.w * 1.5 * sqrt(max(1.0 - NdotL * NdotL, 0.0)) / max(NdotL, 0.05);
    let Visibility = NdotL > 0.0 ? SampleSolarVisibility(Varying.World, SlopeBias) : 0.0;
    let Direct = (Diffuse + Specular) * Analytic.SunIrradiance.rgb * NdotL * Visibility;
    let Sky    = lerp(Analytic.Ground.rgb * Analytic.SkyHorizon.rgb, Analytic.SkyZenith.rgb, 0.5 + 0.5 * Normal.z);
    let Fill   = Material.Albedo * (1.0 - 0.6 * Material.Metalness) * Sky * Analytic.SkyHorizon.w;
    let Radiance = Direct + Fill + Material.Incandescence;
    return float4(ProjectFilmic(Radiance * Analytic.Camera.w), 1.0);
}
