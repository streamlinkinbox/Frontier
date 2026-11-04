//============================================================================================================================================
//                                                         DISPLAYEXPOSURE.SLANG
//============================================================================================================================================
// 🧩 Eye adaptation: log-average luminance of a 64×64 lattice of the refresh, approached exponentially by the exposure.

#pragma once
#include "IntrinsicParity.slang"
#include "HostConstants.slang"

#include "RefreshDescriptor.slang"

  [[vk::binding(1, 0)]] Texture2D<float4>                 ResolvedRadiance;
  [[vk::binding(2, 0)]] RWStructuredBuffer<float>  Adaptation;   // fixed [4] span

groupshared float LaneSums[256];

 
[shader("compute")]
[numthreads(256, 1, 1)]
void IntegrateExposure(uint Lane : SV_GroupIndex)
{
    var Sum = 0.0;
    for (var Sample = 0u; Sample < 16u; Sample++)
    {
        let Index = Lane * 16u + Sample;
        let Lattice = float2(float(Index % 64u) + 0.5, float(Index / 64u) + 0.5) / 64.0;
        let Pixel = int2(Lattice * Refresh.ScreenExtent);
        let Luminance = ResolveLuminance(LoadTexel(ResolvedRadiance, Pixel, 0).rgb);
        Sum += log2(max(Luminance, 0.045));
    }
    LaneSums[Lane] = Sum;
    GroupMemoryBarrierWithGroupSync();
    for (var Stride = 128u; Stride > 0u; Stride >>= 1u)
    {
        if (Lane < Stride)
        {
            LaneSums[Lane] += LaneSums[Lane + Stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (Lane == 0u)
    {
        let Logarithm = LaneSums[0] / 4096.0;
        let Average = exp2(Logarithm);
        // 📝 Physically bounded eye adaptation: dark automotive cabin materials (3–5 % charcoal leather/trim) must stay
        //    dark charcoal rather than being boosted to 18 % middle grey. Below the daylight knee (0.24), adaptation
        //    lifts gently (22 % per stop) and clamps to [0.18, 0.82] before user ExposureCompensation.
        let Floor = log2(0.24);
        let Adapted = SelectBranch(Logarithm, Floor + (Logarithm - Floor) * 0.22, Logarithm < Floor);
        let Target = clamp(0.18 / exp2(Adapted), 0.18, 0.82) * exp2(Refresh.ExposureCompensation);
        let Prior = Adaptation[0];
        var Exposure = Target;
        if (Prior > 0.0 && Refresh.RefreshIndex > 2u)
        {
            Exposure = exp2(lerp(log2(Prior), log2(Target), 0.06));
        }
        if (!FeatureEnabled(Refresh, FeatureAutoExposure))
        {
            Exposure = 0.55 * exp2(Refresh.ExposureCompensation);
        }
        Adaptation[0] = Exposure;
        Adaptation[1] = Average;
    }
}
