//============================================================================================================================================
//                                                         DISPLAYANTIALIAS.SLANG
//============================================================================================================================================
// 🧩 Temporal antialiasing: motion-reprojected prior colour, clamped to the surrounding 3×3 YCoCg extent.

#pragma once
#include "IntrinsicParity.slang"
#include "HostConstants.slang"

#include "RefreshDescriptor.slang"

  [[vk::binding(1, 0)]] Texture2D<float4> RecombinedRadiance;
  [[vk::binding(2, 0)]] Texture2D<float4>     PriorRadiance;
  [[vk::binding(3, 0)]] Texture2D<float4>     SurfaceMotion;
  [[vk::binding(4, 0)]] SamplerState       LinearClamp;
  [[vk::binding(5, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4>   AntialiasTarget;

float3 ProjectCoCg(float3 Colour)
{
    return float3(0.25 * Colour.r + 0.5 * Colour.g + 0.25 * Colour.b, 0.5 * Colour.r - 0.5 * Colour.b,
                 -0.25 * Colour.r + 0.5 * Colour.g - 0.25 * Colour.b);
}

float3 ReconstructRgb(float3 CoCg)
{
    return float3(CoCg.x + CoCg.y - CoCg.z, CoCg.x + CoCg.z, CoCg.x - CoCg.y - CoCg.z);
}

// 📝 Range compression keeps a single bright texel from dominating the clamp box (Karis 2014).
float3 CompressRange(float3 Colour)
{
    return Colour / (1.0 + ResolveLuminance(Colour));
}

float3 ExpandRange(float3 Colour)
{
    return Colour / max(1.0 - ResolveLuminance(Colour), 1.0e-4);
}

 
[shader("compute")]
[numthreads(8, 8, 1)]
void IntegrateAntialias(uint3 Invocation : SV_DispatchThreadID)
{
    let Pixel = int2(Invocation.xy);
    if (any(float2(Pixel) >= Refresh.ScreenExtent))
    {
        return;
    }
    let Current = CompressRange(LoadTexel(RecombinedRadiance, Pixel, 0).rgb);
    if (!FeatureEnabled(Refresh, FeatureTemporalAntialias) || Refresh.InspectionMode == 6u || Refresh.InspectionMode == 7u)
    {
        AntialiasTarget[Pixel] = float4(ExpandRange(Current), 1.0);
        return;
    }
    var Minimum = float3(1.0e9);
    var Maximum = float3(-1.0e9);
    var Mean = float3(0.0);
    var Square = float3(0.0);
    let Limit = int2(Refresh.ScreenExtent) - int2(1);
    for (var Y = -1; Y <= 1; Y++)
    {
        for (var X = -1; X <= 1; X++)
        {
            let Tap = ProjectCoCg(CompressRange(LoadTexel(RecombinedRadiance, clamp(Pixel + int2(X, Y), int2(0), Limit), 0).rgb));
            Minimum = min(Minimum, Tap);
            Maximum = max(Maximum, Tap);
            Mean += Tap;
            Square += Tap * Tap;
        }
    }
    Mean /= 9.0;
    let Deviation = sqrt(max(Square / 9.0 - Mean * Mean, float3(0.0)));
    Minimum = max(Minimum, Mean - 1.25 * Deviation);
    Maximum = min(Maximum, Mean + 1.25 * Deviation);

    let Motion = LoadTexel(SurfaceMotion, Pixel, 0).xy;
    let PriorCoordinates = (float2(Pixel) + 0.5 - Motion) * Refresh.InverseScreenExtent;
    var Fraction = 0.1;
    var Prior = Current;
    if (any(PriorCoordinates < float2(0.0)) || any(PriorCoordinates > float2(1.0)) || Refresh.RefreshIndex < 2u
        || FeatureEnabled(Refresh, FeatureTemporalRestart))
    {
        Fraction = 1.0;
    }
    else
    {
        Prior = CompressRange(PriorRadiance.SampleLevel(LinearClamp, PriorCoordinates, 0.0).rgb);
        Prior = ReconstructRgb(clamp(ProjectCoCg(Prior), Minimum, Maximum));
    }
    let Resolved = lerp(Prior, Current, Fraction);
    AntialiasTarget[Pixel] = float4(ExpandRange(Resolved), 1.0);
}
