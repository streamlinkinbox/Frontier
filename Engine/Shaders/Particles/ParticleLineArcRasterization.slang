/*============================================================================================================================================
                                                    PARTICLELINEARCRASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Pure Slang dielectric arc, laser/plasma beam, spiderweb bond & molecular link rasterizer

import ParticleBuffers;

struct DispatchPushParams
{
    uint SlotOffset;
    uint SlotCount;
    uint ForceSourceCount;
    uint SpawnCursor;
};

[[vk::push_constant]]
DispatchPushParams PushParams;

[[vk::binding(0, 0)]] ConstantBuffer<GlobalParticleUniform> GlobalCfg;
[[vk::binding(1, 0)]] ConstantBuffer<RigParticleUniform>    RigCfg;
[[vk::binding(3, 0)]] StructuredBuffer<ParticleSlot>        ParticleBuffer;

struct LineArcVertexOut
{
    float4 ClipPosition : SV_Position;
    float2 EdgeUv       : TEXCOORD0;
    float4 TintRgba     : COLOR0;
};

[shader("vertex")]
LineArcVertexOut VertexEntry(uint VertexId : SV_VulkanVertexID, uint InstanceId : SV_VulkanInstanceID)
{
    LineArcVertexOut Out;
    uint SegIdx = (InstanceId > 0u) ? InstanceId : (VertexId / 6u);
    if (SegIdx + 1u >= PushParams.SlotCount)
    {
        Out.ClipPosition = float4(2.0, 2.0, 2.0, 1.0);
        Out.EdgeUv       = float2(0.0, 0.0);
        Out.TintRgba     = float4(0.0, 0.0, 0.0, 0.0);
        return Out;
    }

    ParticleSlot A = ParticleBuffer[PushParams.SlotOffset + SegIdx];
    ParticleSlot B = ParticleBuffer[PushParams.SlotOffset + SegIdx + 1u];
    if (A.PositionAge.w >= A.VelocityLifetime.w || B.PositionAge.w >= B.VelocityLifetime.w)
    {
        Out.ClipPosition = float4(2.0, 2.0, 2.0, 1.0);
        Out.EdgeUv       = float2(0.0, 0.0);
        Out.TintRgba     = float4(0.0, 0.0, 0.0, 0.0);
        return Out;
    }

    static const float2 BeamQuadCorners[6] = {
        float2(0.0, 0.0), float2(1.0, 0.0), float2(1.0, 1.0),
        float2(0.0, 0.0), float2(1.0, 1.0), float2(0.0, 1.0)
    };
    float2 C = BeamQuadCorners[VertexId % 6u];
    float3 P0 = A.PositionAge.xyz;
    float3 P1 = B.PositionAge.xyz;
    float3 Pos = (C.x < 0.5) ? P0 : P1;
    float3 Dir = normalize(length(P1 - P0) > 1.0e-5 ? (P1 - P0) : float3(1.0, 0.0, 0.0));
    float3 ViewDir = normalize(GlobalCfg.CameraPosition.xyz - Pos);
    float3 Side = normalize(cross(ViewDir, Dir));
    float HalfW = max(0.042, A.Attributes.x * 1.5);
    float3 WorldPos = Pos + Side * (HalfW * ((C.y < 0.5) ? -1.0 : 1.0));

    Out.ClipPosition = mul(GlobalCfg.ViewProjection, float4(WorldPos, 1.0));
    Out.EdgeUv       = C;
    Out.TintRgba     = lerp(A.ColourRgba, B.ColourRgba, C.x);
    return Out;
}

[shader("fragment")]
float4 FragmentEntry(LineArcVertexOut In) : SV_Target
{
    float Core = exp(-pow((In.EdgeUv.y - 0.5) * 2.3, 2.0));
    float Hot  = exp(-pow((In.EdgeUv.y - 0.5) * 6.5, 2.0));
    float A = clamp(In.TintRgba.a * Core, 0.0, 1.0);
    float3 Col = In.TintRgba.rgb * (1.5 * Core) + float3(0.85, 0.96, 1.0) * (1.25 * Hot);
    return float4(Col * A, A);
}
