/*============================================================================================================================================
                                                    PARTICLEOVERLAYRASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Pure Slang reference floor grid (-10..+10 m, +Z Up) + effector / force gizmo indicator overlay

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

struct OverlayVertexOut
{
    float4 ClipPosition : SV_Position;
    float4 LineColor    : COLOR0;
};

[shader("vertex")]
OverlayVertexOut VertexEntry(uint VertexId : SV_VulkanVertexID)
{
    OverlayVertexOut Out;
    // 42 grid lines (-10..+10 along X and Y on Z = 0 floor) -> 84 vertices (LineList)
    uint LineIdx  = VertexId / 2u;
    uint EndIdx   = VertexId % 2u;
    bool AlongX   = (LineIdx < 21u);
    int  GridStep = int(AlongX ? LineIdx : (LineIdx - 21u)) - 10;
    float Coord   = float(GridStep);
    float Span    = (EndIdx == 0u) ? -10.0 : 10.0;

    float3 WorldPos = AlongX ? float3(Span, Coord, 0.0) : float3(Coord, Span, 0.0);
    Out.ClipPosition = mul(GlobalCfg.ViewProjection, float4(WorldPos, 1.0));

    bool IsAxis = (GridStep == 0);
    if (IsAxis)
    {
        Out.LineColor = AlongX ? float4(0.55, 0.22, 0.24, 0.65) : float4(0.22, 0.55, 0.28, 0.65);
    }
    else
    {
        Out.LineColor = float4(0.22, 0.26, 0.34, 0.35);
    }
    return Out;
}

[shader("fragment")]
float4 FragmentEntry(OverlayVertexOut In) : SV_Target
{
    return float4(In.LineColor.rgb * In.LineColor.a, In.LineColor.a);
}
