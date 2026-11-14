/*============================================================================================================================================
                                                     PARTICLELENSRASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Pure Slang gravitational Einstein ring & thermal heat-shimmer optical distortion overlay

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

struct LensVertexOut
{
    float4 ClipPosition : SV_Position;
    float2 QuadUv       : TEXCOORD0;
};

[shader("vertex")]
LensVertexOut VertexEntry(uint VertexId : SV_VulkanVertexID)
{
    LensVertexOut Out;
    static const float2 QuadCorners[6] = {
        float2(-1.0, -1.0), float2(1.0, -1.0), float2(1.0,  1.0),
        float2(-1.0, -1.0), float2(1.0,  1.0), float2(-1.0, 1.0)
    };
    float2 Corner = QuadCorners[VertexId % 6u];
    float Radius = max(0.8, RigCfg.ReactionDamping.z);
    float3 WorldPos = RigCfg.OriginRadius.xyz
                    + GlobalCfg.CameraRight.xyz * (Corner.x * Radius)
                    + GlobalCfg.CameraUp.xyz    * (Corner.y * Radius);
    Out.ClipPosition = mul(GlobalCfg.ViewProjection, float4(WorldPos, 1.0));
    Out.QuadUv       = Corner;
    return Out;
}

[shader("fragment")]
float4 FragmentEntry(LensVertexOut In) : SV_Target
{
    float R = length(In.QuadUv);
    if (R > 1.0)
    {
        discard;
    }

    float EventHorizon = smoothstep(0.32, 0.28, R);
    float EinsteinRing = exp(-pow((R - 0.42) * 14.0, 2.0));
    float Shimmer      = 0.5 + 0.5 * sin(R * 22.0 - GlobalCfg.TimingMetrics.y * 5.0);

    float3 RingCol = lerp(RigCfg.PrimaryColour.rgb, RigCfg.SecondaryColour.rgb, Shimmer) * (1.8 * EinsteinRing);
    float3 FinalCol = lerp(RingCol, float3(0.0, 0.0, 0.0), EventHorizon);
    float  Alpha    = clamp(max(EventHorizon, EinsteinRing * 0.9), 0.0, 1.0);
    return float4(FinalCol, Alpha);
}
