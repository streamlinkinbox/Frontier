//============================================================================================================================================
//                                                         VARIANCEWAVELET.SLANG
//============================================================================================================================================
// 🧩 One à-trous wavelet iteration: a 5×5 B3-spline filter at step 2^i, stopped at depth, normal and luminance edges.

#pragma once
#include "IntrinsicParity.slang"
#include "HostConstants.slang"

// 📝 w(p, q) = h(q) · exp(−|z_p − z_q| / (σ_z·step·z_p)) · max(0, n_p·n_q)^σ_n · exp(−|l_p − l_q| / (σ_l·√Var_p + ε)),
//    h the separable B3 spline (1, 4, 6, 4, 1)/16. Variance is propagated as Σw²·Var / (Σw)² (Dammertz et al. 2010,
//    Schied et al. 2017). Signal alpha carries the variance between iterations.

#include "RefreshDescriptor.slang"
#include "PackingCodec.slang"

struct VarianceConstants
{
    uint Step;            // [px]
    float MinimumWeight;    // [-]  (unused here)
    float ColourSigma;     // [-]
    float TallyLimit;    // [-]  (unused here)
};

  [[vk::binding(1, 0)]] ConstantBuffer<VarianceConstants> Variance;
  [[vk::binding(2, 0)]] Texture2D<float4>      SourceSignal;
  [[vk::binding(3, 0)]] Texture2D<float4>      CurrentGuide;
  [[vk::binding(4, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4>     WaveletTarget;

static const float Spline[3] = {0.375, 0.25, 0.0625};

 
[shader("compute")]
[numthreads(8, 8, 1)]
void ConvolveWavelet(uint3 Invocation : SV_DispatchThreadID)
{
    let Pixel = int2(Invocation.xy);
    let Extent = int2(TextureExtent(SourceSignal));
    if (any(Pixel >= Extent))
    {
        return;
    }
    let Guide = LoadTexel(CurrentGuide, Pixel, 0);
    let Centre = LoadTexel(SourceSignal, Pixel, 0);
    if (Guide.w < 0.5)
    {
        WaveletTarget[Pixel] = Centre;
        return;
    }
    let Normal = DecodeOctahedral(Guide.xy);
    let Luminance = ResolveLuminance(Centre.rgb);
    let Deviation = sqrt(max(Centre.a, 0.0));
    let Step = ToSigned(Variance.Step);
    var Sum = float4(0.0);
    var WeightSum = 0.0;
    var SpreadSum = 0.0;
    for (var Y = -2; Y <= 2; Y++)
    {
        for (var X = -2; X <= 2; X++)
        {
            let Texel = Pixel + int2(X, Y) * Step;
            if (any(Texel < int2(0)) || any(Texel >= Extent))
            {
                continue;
            }
            let TapGuide = LoadTexel(CurrentGuide, Texel, 0);
            if (TapGuide.w < 0.5)
            {
                continue;
            }
            let Tap = LoadTexel(SourceSignal, Texel, 0);
            let Kernel = Spline[abs(X)] * Spline[abs(Y)];
            let DepthWeight = exp(-abs(TapGuide.z - Guide.z) / (0.015 * float(Step) * Guide.z + 1.0e-3));
            let NormalWeight = pow(max(dot(DecodeOctahedral(TapGuide.xy), Normal), 0.0), 64.0);
            let LuminanceWeight = exp(-abs(ResolveLuminance(Tap.rgb) - Luminance) / (Variance.ColourSigma * Deviation + 1.0e-3));
            let Weight = Kernel * DepthWeight * NormalWeight * LuminanceWeight;
            Sum += float4(Tap.rgb * Weight, 0.0);
            SpreadSum += Weight * Weight * Tap.a;
            WeightSum += Weight;
        }
    }
    if (WeightSum <= 1.0e-6)
    {
        WaveletTarget[Pixel] = Centre;
        return;
    }
    WaveletTarget[Pixel] = float4(Sum.rgb / WeightSum, SpreadSum / (WeightSum * WeightSum));
}
