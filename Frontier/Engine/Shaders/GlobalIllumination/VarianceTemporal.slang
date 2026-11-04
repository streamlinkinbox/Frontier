//============================================================================================================================================
//                                                         VARIANCETEMPORAL.SLANG
//============================================================================================================================================
// 🧩 Temporal integration of a resolved signal with luminance moments, reprojected through motion and validated per tap.

#pragma once
#include "IntrinsicParity.slang"
#include "HostConstants.slang"

// 📝 SVGF's temporal step (Schied et al. 2017): the prior signal is gathered with four bilinear taps, each refused
//    unless the prior guide agrees in normal and depth, then mixed with α = max(1/N, MinimumWeight). Luminance
//    moments μ₁, μ₂ are integrated alongside; variance μ₂ − μ₁² is inflated while the tally is short.

#include "RefreshDescriptor.slang"
#include "PackingCodec.slang"

struct VarianceConstants
{
    uint Step;              // [px]  wavelet step (unused here)
    float MinimumWeight;     // [-]   floor of the temporal weight
    float ColourSigma;       // [-]   luminance edge-stopping scale (unused here)
    float TallyLimit;        // [-]   longest tally N
};

  [[vk::binding(1, 0)]] ConstantBuffer<VarianceConstants>  Variance;
  [[vk::binding(2, 0)]] Texture2D<float4>      CurrentSignal;
  [[vk::binding(3, 0)]] Texture2D<float4>      SurfaceMotion;
  [[vk::binding(4, 0)]] Texture2D<float4>       CurrentGuide;
  [[vk::binding(5, 0)]] Texture2D<float4>      PreviousGuide;
  [[vk::binding(6, 0)]] Texture2D<float4>        PriorSignal;
  [[vk::binding(7, 0)]] Texture2D<float4>       PriorMoments;
  [[vk::binding(8, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4>   IntegratedTarget;
  [[vk::binding(9, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4>      MomentsTarget;

 
[shader("compute")]
[numthreads(8, 8, 1)]
void IntegrateTemporal(uint3 Invocation : SV_DispatchThreadID)
{
    let TracePixel = int2(Invocation.xy);
    let Extent = int2(TextureExtent(CurrentSignal));
    if (any(TracePixel >= Extent))
    {
        return;
    }
    let Guide = LoadTexel(CurrentGuide, TracePixel, 0);
    let Signal = LoadTexel(CurrentSignal, TracePixel, 0).rgb;
    let Luminance = ResolveLuminance(Signal);
    if (Guide.w < 0.5)
    {
        IntegratedTarget[TracePixel] = float4(0.0);
        MomentsTarget[TracePixel] = float4(0.0);
        return;
    }
    let Normal = DecodeOctahedral(Guide.xy);
    let ScreenPixel = min(TracePixel * ToSigned(Refresh.TraceScale), int2(Refresh.ScreenExtent) - int2(1));
    let Motion = LoadTexel(SurfaceMotion, ScreenPixel, 0).xy;
    let PriorPosition = (float2(ScreenPixel) + 0.5 - Motion) / float(Refresh.TraceScale) - 0.5;
    let Corner = floor(PriorPosition);
    let Fraction = PriorPosition - Corner;

    var PriorColour = float3(0.0);
    var PriorMoment = float3(0.0);
    var TotalWeight = 0.0;
    for (var Tap = 0u; Tap < 4u; Tap++)
    {
        let Offset = int2(ToSigned(Tap & 1u), ToSigned(Tap >> 1u));
        let Texel = int2(Corner) + Offset;
        if (any(Texel < int2(0)) || any(Texel >= Extent))
        {
            continue;
        }
        let PriorGuide = LoadTexel(PreviousGuide, Texel, 0);
        if (PriorGuide.w < 0.5 || dot(DecodeOctahedral(PriorGuide.xy), Normal) < 0.85
            || !ClassifyPriorDepth(PriorGuide.z, Guide.w - 1.0, Guide.z))
        {
            continue;
        }
        let Weight = SelectBranch(1.0 - Fraction.x, Fraction.x, Offset.x == 1) * SelectBranch(1.0 - Fraction.y, Fraction.y, Offset.y == 1);
        PriorColour += LoadTexel(PriorSignal, Texel, 0).rgb * Weight;
        PriorMoment += LoadTexel(PriorMoments, Texel, 0).xyz * Weight;
        TotalWeight += Weight;
    }

    var Tally = 1.0;
    var Colour = Signal;
    var Moments = float2(Luminance, Luminance * Luminance);
    if (TotalWeight > 0.01 && !FeatureEnabled(Refresh, FeatureTemporalRestart))
    {
        PriorColour /= TotalWeight;
        PriorMoment /= TotalWeight;
        Tally = min(PriorMoment.z + 1.0, Variance.TallyLimit);
        let Fraction = max(1.0 / Tally, Variance.MinimumWeight);
        Colour  = lerp(PriorColour, Signal, Fraction);
        Moments = lerp(PriorMoment.xy, Moments, max(Fraction, 0.2));
    }
    var Spread = max(Moments.y - Moments.x * Moments.x, 0.0);
    Spread *= 4.0 / min(Tally, 4.0);
    IntegratedTarget[TracePixel] = float4(Colour, Spread);
    MomentsTarget[TracePixel] = float4(Moments, Tally, 0.0);
}
