/*============================================================================================================================================
                                                PARTICLESURFACEBACKINGRASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Pure Slang backing geometry, obsidian/chrome/wireframe finish & interactive chrome ball collider rasterizer

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

struct SurfaceVertexOut
{
    float4 ClipPosition : SV_Position;
    float3 WorldNormal  : TEXCOORD0;
    float3 WorldPos     : TEXCOORD1;
    float2 QuadUv       : TEXCOORD2;
};

[shader("vertex")]
SurfaceVertexOut VertexEntry(uint VertexId : SV_VulkanVertexID)
{
    SurfaceVertexOut Out;
    float BallR = GlobalCfg.ColliderBallPose.w;
    if (BallR <= 0.01)
    {
        Out.ClipPosition = float4(2.0, 2.0, 2.0, 1.0);
        Out.WorldNormal  = float3(0.0, 0.0, 1.0);
        Out.WorldPos     = float3(0.0, 0.0, 0.0);
        Out.QuadUv       = float2(0.0, 0.0);
        return Out;
    }

    static const float2 QuadCorners[6] = {
        float2(-1.0, -1.0), float2(1.0, -1.0), float2(1.0,  1.0),
        float2(-1.0, -1.0), float2(1.0,  1.0), float2(-1.0, 1.0)
    };
    float2 Corner = QuadCorners[VertexId % 6u];
    float3 WorldPos = GlobalCfg.ColliderBallPose.xyz
                    + GlobalCfg.CameraRight.xyz * (Corner.x * BallR)
                    + GlobalCfg.CameraUp.xyz    * (Corner.y * BallR);

    Out.ClipPosition = mul(GlobalCfg.ViewProjection, float4(WorldPos, 1.0));
    Out.WorldNormal  = normalize(GlobalCfg.CameraPosition.xyz - GlobalCfg.ColliderBallPose.xyz);
    Out.WorldPos     = WorldPos;
    Out.QuadUv       = Corner;
    return Out;
}

[shader("fragment")]
float4 FragmentEntry(SurfaceVertexOut In) : SV_Target
{
    float R2 = dot(In.QuadUv, In.QuadUv);
    if (R2 > 1.0)
    {
        discard;
    }

    float Z = sqrt(max(0.0, 1.0 - R2));
    float3 N = normalize(GlobalCfg.CameraRight.xyz * In.QuadUv.x + GlobalCfg.CameraUp.xyz * In.QuadUv.y + In.WorldNormal * Z);
    float3 V = normalize(GlobalCfg.CameraPosition.xyz - In.WorldPos);
    float3 L = normalize(float3(0.45, -0.35, 0.85));
    float3 R = reflect(-V, N);

    // Chrome environment reflection + rim specular
    float SkyMix = 0.5 + 0.5 * R.z;
    float3 EnvCol = lerp(float3(0.14, 0.16, 0.22), float3(0.75, 0.86, 0.98), pow(SkyMix, 1.6));
    float Spec = pow(max(0.0, dot(N, normalize(L + V))), 48.0);
    float3 Chrome = EnvCol * (0.45 + 0.55 * max(0.0, dot(N, L))) + float3(Spec);

    return float4(Chrome, 1.0);
}
