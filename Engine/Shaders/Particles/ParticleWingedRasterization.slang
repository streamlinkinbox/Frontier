/*============================================================================================================================================
                                                    PARTICLEWINGEDRASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Pure Slang articulated dual-wing creature rasterizer for butterflies, birds, starling murmurations & bioluminescent fish

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

struct WingedVertexOut
{
    float4 ClipPosition : SV_Position;
    float3 WorldNormal  : TEXCOORD0;
    float4 TintRgba     : COLOR0;
};

[shader("vertex")]
WingedVertexOut VertexEntry(uint VertexId : SV_VulkanVertexID, uint InstanceId : SV_VulkanInstanceID)
{
    WingedVertexOut Out;
    uint GlobalIdx = PushParams.SlotOffset + InstanceId;
    ParticleSlot Slot = ParticleBuffer[GlobalIdx];
    if (InstanceId >= PushParams.SlotCount || Slot.PositionAge.w >= Slot.VelocityLifetime.w)
    {
        Out.ClipPosition = float4(2.0, 2.0, 2.0, 1.0);
        Out.WorldNormal  = float3(0.0, 0.0, 1.0);
        Out.TintRgba     = float4(0.0, 0.0, 0.0, 0.0);
        return Out;
    }

    float Flap = sin(GlobalCfg.TimingMetrics.y * 14.0 + Slot.Attributes.w * 19.0) * 0.72;
    float WingSpan = max(0.02, Slot.Attributes.x * 1.45);
    float SideSign = ((VertexId % 6u) < 3u) ? -1.0 : 1.0;
    uint  SubVert  = VertexId % 3u;

    float3 LocalPt = float3(0.0, 0.0, 0.0);
    if (SubVert == 0u)
    {
        LocalPt = float3(0.55 * WingSpan, 0.0, 0.0);
    }
    else if (SubVert == 1u)
    {
        LocalPt = float3(-0.35 * WingSpan, 0.0, 0.0);
    }
    else
    {
        LocalPt = float3(0.10 * WingSpan, SideSign * cos(Flap) * WingSpan, sin(Flap) * WingSpan);
    }

    float3 Fwd = normalize(length(Slot.VelocityLifetime.xyz) > 1.0e-3 ? Slot.VelocityLifetime.xyz : float3(1.0, 0.0, 0.0));
    float3 Up  = (abs(Fwd.z) > 0.94) ? float3(1.0, 0.0, 0.0) : float3(0.0, 0.0, 1.0);
    float3 Side = normalize(cross(Up, Fwd));
    float3 RealUp = cross(Fwd, Side);

    float3 WorldPos = Slot.PositionAge.xyz + Fwd * LocalPt.x + Side * LocalPt.y + RealUp * LocalPt.z;
    Out.ClipPosition = mul(GlobalCfg.ViewProjection, float4(WorldPos, 1.0));
    Out.WorldNormal  = normalize(RealUp + Side * (SideSign * -sin(Flap)));
    Out.TintRgba     = Slot.ColourRgba;
    return Out;
}

[shader("fragment")]
float4 FragmentEntry(WingedVertexOut In) : SV_Target
{
    float Light = 0.45 + 0.55 * abs(dot(normalize(In.WorldNormal), normalize(float3(0.4, 0.3, 0.85))));
    float A = clamp(In.TintRgba.a, 0.0, 1.0);
    return float4(In.TintRgba.rgb * Light * A, A);
}
