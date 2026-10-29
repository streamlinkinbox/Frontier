/*==========================================================================================================================================
                                                   TYRESURFACEVERTEXPROBE.SLANG
==========================================================================================================================================*/
// 🧩 Compile-only stage check: production mesh inputs, transforms and graphics pipeline are not supplied here.
#include "TyreSurfaceDevice.slang"

[[vk::binding(0, 0)]] StructuredBuffer<TyreProjectionParticle> Particles;
[[vk::binding(1, 0)]] StructuredBuffer<float4> Vertices;

struct VertexOutput
{
    float4 Position : SV_Position;
    float3 Normal : TEXCOORD0;
};

[shader("vertex")]
VertexOutput CheckTyreVertex(uint Index : SV_VertexID)
{
    float3 Position = Vertices[Index * 2].xyz;
    float3 Normal = Vertices[Index * 2 + 1].xyz;
    TyreSurfaceVertex Deformed = ResolveContinuumTyreVertex(Particles, 0, true, false, 0.04, Position, Normal);
    VertexOutput Result;
    Result.Position = float4(Deformed.Position, 1.0);
    Result.Normal = Deformed.Normal;
    return Result;
}
