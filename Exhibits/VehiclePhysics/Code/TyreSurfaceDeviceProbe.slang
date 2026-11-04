/*==========================================================================================================================================
                                                   TYRESURFACEDEVICEPROBE.SLANG
==========================================================================================================================================*/
// 🧩 Test the reusable Engine vertex functions against particles left resident by the simulation dispatch.
#include "TyreSurfaceDevice.slang"

struct SurfaceProbe
{
    float4 Position;
    float4 Normal;
    float4 Deformation;
    uint4 Controls;
};
struct SurfaceResult
{
    float4 Position;
    float4 Normal;
};

[[vk::binding(0, 0)]] StructuredBuffer<TyreProjectionParticle> Particles;
[[vk::binding(1, 0)]] StructuredBuffer<SurfaceProbe> Probes;
[[vk::binding(2, 0)]] RWStructuredBuffer<SurfaceResult> Results;

[shader("compute")]
[numthreads(64, 1, 1)]
void CompareTyreSurface(uint3 Thread : SV_DispatchThreadID)
{
    uint ProbeCount, ProbeStride, ResultCount, ResultStride;
    Probes.GetDimensions(ProbeCount, ProbeStride);
    Results.GetDimensions(ResultCount, ResultStride);
    if (Thread.x >= ProbeCount || Thread.x >= ResultCount) return;
    SurfaceProbe Probe = Probes[Thread.x];
    bool TyreSurface = (Probe.Controls.z & 1) != 0;
    bool Shredded = (Probe.Controls.z & 2) != 0;
    TyreSurfaceVertex Vertex;
    if (Probe.Controls.y == 0)
        Vertex = ResolveSpringTyreVertex(TyreSurface, Shredded, Probe.Deformation, Probe.Position.xyz, Probe.Normal.xyz);
    else
        Vertex = ResolveContinuumTyreVertex(Particles, Probe.Controls.x, TyreSurface, Shredded, Probe.Deformation.y,
                                           Probe.Position.xyz, Probe.Normal.xyz);
    SurfaceResult Result;
    Result.Position = float4(Vertex.Position, 0.0);
    Result.Normal = float4(Vertex.Normal, 0.0);
    Results[Thread.x] = Result;
}
