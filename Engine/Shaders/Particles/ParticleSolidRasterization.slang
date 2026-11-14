/*============================================================================================================================================
                                                     PARTICLESOLIDRASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Pure Slang instanced 3D oriented cube/shard/coin/MoGraph-column rasterizer with Blinn-Phong + rim shading

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

struct SolidVertexOut
{
    float4 ClipPosition : SV_Position;
    float3 WorldNormal  : TEXCOORD0;
    float3 WorldPos     : TEXCOORD1;
    float4 TintRgba     : COLOR0;
};

[shader("vertex")]
SolidVertexOut VertexEntry(uint VertexId : SV_VulkanVertexID, uint InstanceId : SV_VulkanInstanceID)
{
    SolidVertexOut Out;
    uint GlobalIdx = PushParams.SlotOffset + InstanceId;
    ParticleSlot Slot = ParticleBuffer[GlobalIdx];
    if (InstanceId >= PushParams.SlotCount || Slot.PositionAge.w >= Slot.VelocityLifetime.w)
    {
        Out.ClipPosition = float4(2.0, 2.0, 2.0, 1.0);
        Out.WorldNormal  = float3(0.0, 0.0, 1.0);
        Out.WorldPos     = float3(0.0, 0.0, 0.0);
        Out.TintRgba     = float4(0.0, 0.0, 0.0, 0.0);
        return Out;
    }

    static const float3 CubeCorners[36] = {
        // +Z top
        float3(-1,-1, 1), float3( 1,-1, 1), float3( 1, 1, 1), float3(-1,-1, 1), float3( 1, 1, 1), float3(-1, 1, 1),
        // -Z bottom
        float3(-1, 1,-1), float3( 1, 1,-1), float3( 1,-1,-1), float3(-1, 1,-1), float3( 1,-1,-1), float3(-1,-1,-1),
        // +X
        float3( 1,-1,-1), float3( 1, 1,-1), float3( 1, 1, 1), float3( 1,-1,-1), float3( 1, 1, 1), float3( 1,-1, 1),
        // -X
        float3(-1, 1,-1), float3(-1,-1,-1), float3(-1,-1, 1), float3(-1, 1,-1), float3(-1,-1, 1), float3(-1, 1, 1),
        // +Y
        float3( 1, 1,-1), float3(-1, 1,-1), float3(-1, 1, 1), float3( 1, 1,-1), float3(-1, 1, 1), float3( 1, 1, 1),
        // -Y
        float3(-1,-1,-1), float3( 1,-1,-1), float3( 1,-1, 1), float3(-1,-1,-1), float3( 1,-1, 1), float3(-1,-1, 1)
    };
    static const float3 FaceNormals[6] = {
        float3(0,0,1), float3(0,0,-1), float3(1,0,0), float3(-1,0,0), float3(0,1,0), float3(0,-1,0)
    };

    uint VIdx = VertexId % 36u;
    float3 LocalPos = CubeCorners[VIdx];
    float3 LocalNrm = FaceNormals[VIdx / 6u];

    float Spin = Slot.Attributes.y;
    float C = cos(Spin);
    float S = sin(Spin);
    float3 RotPos = float3(LocalPos.x * C - LocalPos.y * S, LocalPos.x * S + LocalPos.y * C, LocalPos.z);
    float3 RotNrm = float3(LocalNrm.x * C - LocalNrm.y * S, LocalNrm.x * S + LocalNrm.y * C, LocalNrm.z);

    float HalfExt = max(0.005, Slot.Attributes.x * max(0.25, RigCfg.ScaleAndModes.x));
    float3 WorldPos = Slot.PositionAge.xyz + RotPos * HalfExt;

    Out.ClipPosition = mul(GlobalCfg.ViewProjection, float4(WorldPos, 1.0));
    Out.WorldNormal  = normalize(RotNrm);
    Out.WorldPos     = WorldPos;
    Out.TintRgba     = Slot.ColourRgba;
    return Out;
}

[shader("fragment")]
float4 FragmentEntry(SolidVertexOut In) : SV_Target
{
    float3 N = normalize(In.WorldNormal);
    float3 L = normalize(float3(0.45, -0.35, 0.82));
    float3 V = normalize(GlobalCfg.CameraPosition.xyz - In.WorldPos);
    float3 H = normalize(L + V);

    float Diff = 0.25 + 0.75 * max(0.0, dot(N, L));
    float Spec = pow(max(0.0, dot(N, H)), 24.0) * 0.45;
    float Rim  = pow(1.0 - max(0.0, dot(N, V)), 3.0) * 0.35;

    float3 Lit = In.TintRgba.rgb * Diff + float3(Spec + Rim);
    float  A   = clamp(In.TintRgba.a, 0.0, 1.0);
    return float4(Lit * A, A);
}
