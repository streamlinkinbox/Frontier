/*==========================================================================================================================================
                                              TYRESOLIDCONSTITUTIVEPROBE.SLANG
==========================================================================================================================================*/
// 🧩 Execute the solid's actual stress evaluator on independently prescribed deformation gradients.
#include "TyreSolidConstitutive.slang"

struct TyreStressProbe
{
    float4 F0, F1, F2, Moduli;
};
struct TyreStressSample
{
    float4 P0, P1, P2;
};
[[vk::binding(0, 0)]] StructuredBuffer<TyreStressProbe> Probes;
[[vk::binding(1, 0)]] RWStructuredBuffer<TyreStressSample> Samples;

[shader("compute")]
[numthreads(64, 1, 1)]
void EvaluateSolidStressProbes(uint3 Thread : SV_DispatchThreadID)
{
    uint ProbeCount, ProbeStride, SampleCount, SampleStride;
    Probes.GetDimensions(ProbeCount, ProbeStride);
    Samples.GetDimensions(SampleCount, SampleStride);
    if (Thread.x >= ProbeCount || Thread.x >= SampleCount) return;
    TyreStressProbe Probe = Probes[Thread.x];
    TyreSolidStress Stress = EvaluateTyreSolidStress(Probe.F0.xyz, Probe.F1.xyz, Probe.F2.xyz, Probe.Moduli.x, Probe.Moduli.y);
    TyreStressSample Sample;
    Sample.P0 = float4(Stress.Row0, Stress.Jacobian);
    Sample.P1 = float4(Stress.Row1, 0.0);
    Sample.P2 = float4(Stress.Row2, 0.0);
    Samples[Thread.x] = Sample;
}
