/*============================================================================================================================================
                                                     PARTICLERIBBONRASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Pure Slang 20-vertex continuous ribbon strip rasterizer for cables, cloth flags, grass/reeds, ferns, kelp, anemones & trails

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
[[vk::binding(4, 0)]] StructuredBuffer<StrandVertex>        StrandBuffer;

struct RibbonVertexOut
{
    float4 ClipPosition : SV_Position;
    float2 RibbonUv     : TEXCOORD0;
    float4 TintRgba     : COLOR0;
};

[shader("vertex")]
RibbonVertexOut VertexEntry(uint VertexId : SV_VulkanVertexID, uint InstanceId : SV_VulkanInstanceID)
{
    RibbonVertexOut Out;
    uint SegCount = max(2u, uint(RigCfg.StrandAndBlend.x + 0.5));
    uint StrandVerts = SegCount;
    if (InstanceId >= PushParams.SlotCount || StrandVerts < 2u)
    {
        Out.ClipPosition = float4(2.0, 2.0, 2.0, 1.0);
        Out.RibbonUv     = float2(0.0, 0.0);
        Out.TintRgba     = float4(0.0, 0.0, 0.0, 0.0);
        return Out;
    }

    uint SegIdx  = min(StrandVerts - 2u, VertexId / 6u);
    uint TriVert = VertexId % 6u;
    static const uint2 CornerMap[6] = {
        uint2(0u, 0u), uint2(1u, 0u), uint2(1u, 1u),
        uint2(0u, 0u), uint2(1u, 1u), uint2(0u, 1u)
    };
    uint2 C = CornerMap[TriVert];
    uint VIdx = InstanceId * StrandVerts + SegIdx + C.x;
    uint NextIdx = min(InstanceId * StrandVerts + StrandVerts - 1u, VIdx + 1u);
    uint PrevIdx = max(InstanceId * StrandVerts, (VIdx > 0u) ? (VIdx - 1u) : 0u);

    StrandVertex V    = StrandBuffer[VIdx];
    StrandVertex Vn   = StrandBuffer[NextIdx];
    StrandVertex Vp   = StrandBuffer[PrevIdx];
    float3 Tangent    = normalize(length(Vn.PositionRadius.xyz - Vp.PositionRadius.xyz) > 1.0e-5
                                      ? (Vn.PositionRadius.xyz - Vp.PositionRadius.xyz)
                                      : float3(0.0, 0.0, 1.0));
    float3 ViewDir    = normalize(GlobalCfg.CameraPosition.xyz - V.PositionRadius.xyz);
    float3 Side       = normalize(cross(ViewDir, Tangent));
    float  HalfW      = max(0.004, V.PositionRadius.w);
    float  SignSide   = (C.y == 0u) ? -1.0 : 1.0;

    float3 WorldPos   = V.PositionRadius.xyz + Side * (HalfW * SignSide);
    float  AlongT     = float(SegIdx + C.x) / float(StrandVerts - 1u);

    Out.ClipPosition = mul(GlobalCfg.ViewProjection, float4(WorldPos, 1.0));
    Out.RibbonUv     = float2(AlongT, float(C.y));
    Out.TintRgba     = lerp(RigCfg.PrimaryColour, RigCfg.SecondaryColour, AlongT);
    return Out;
}

[shader("fragment")]
float4 FragmentEntry(RibbonVertexOut In) : SV_Target
{
    float EdgeFade = smoothstep(0.0, 0.15, In.RibbonUv.y) * smoothstep(1.0, 0.85, In.RibbonUv.y);
    float Alpha    = clamp(In.TintRgba.a * EdgeFade, 0.0, 1.0);
    return float4(In.TintRgba.rgb * Alpha, Alpha);
}
