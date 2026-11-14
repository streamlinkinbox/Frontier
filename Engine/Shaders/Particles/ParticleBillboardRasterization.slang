/*============================================================================================================================================
                                                  PARTICLEBILLBOARDRASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Vulkan 1.3 GPU billboard / velocity-stretched spark / glowing orb / shard rasterizer reading live SoA SSBOs directly

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Shaders/Particles/ParticleChannelLayouts.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                                       RESOURCES
//------------------------------------------------------------------------------------------------------------------------

[[vk::binding(0, 0)]]
ConstantBuffer<GpuParticleViewportUniforms> Viewport;

[[vk::binding(1, 0)]]
StructuredBuffer<GpuKinematicsSlot> KinematicsBuffer;

[[vk::binding(2, 0)]]
StructuredBuffer<GpuLifespanSlot> LifespanBuffer;

[[vk::binding(3, 0)]]
StructuredBuffer<GpuAppearanceSlot> AppearanceBuffer;

struct BillboardVaryings
{
    float4 ClipPosition : SV_Position;
    float2 LocalQuadUv  : TEXCOORD0;
    float4 TintRgba     : COLOR0;
    nointerpolation uint Silhouette : TEXCOORD1;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENTRY POINTS
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
BillboardVaryings ParticleBillboardVertex(
    uint VertexId   : SV_VulkanVertexID,
    uint InstanceId : SV_VulkanInstanceID)
{
    static const float2 QuadCorners[6] = {
        float2(-1.0, -1.0), float2( 1.0, -1.0), float2( 1.0,  1.0),
        float2(-1.0, -1.0), float2( 1.0,  1.0), float2(-1.0,  1.0)
    };

    BillboardVaryings Out;
    Out.LocalQuadUv = QuadCorners[VertexId % 6u];
    Out.Silhouette  = Viewport.ViewportControl.y;

    if (InstanceId >= Viewport.ViewportControl.x)
    {
        Out.ClipPosition = float4(0.0, 0.0, -10.0, 1.0);
        Out.TintRgba     = float4(0.0, 0.0, 0.0, 0.0);
        return Out;
    }

    GpuLifespanSlot Life = LifespanBuffer[InstanceId];
    if ((Life.LivenessMask & 1u) == 0u)
    {
        Out.ClipPosition = float4(0.0, 0.0, -10.0, 1.0);
        Out.TintRgba     = float4(0.0, 0.0, 0.0, 0.0);
        return Out;
    }

    GpuKinematicsSlot Kin    = KinematicsBuffer[InstanceId];
    GpuAppearanceSlot Appear = AppearanceBuffer[InstanceId];

    float3 WorldCenter = Kin.PositionInvMass.xyz;
    float3 Velocity    = Kin.VelocityDrag.xyz;
    float  RadiusM     = max(0.012, Appear.VisualMetrics.x * Viewport.CameraRightScale.w);
    float  RollRad     = Appear.VisualMetrics.y;

    float3 CamRight = Viewport.CameraRightScale.xyz;
    float3 CamUp    = Viewport.CameraUpBoost.xyz;
    float3 CamFwd   = VfNormalizeSafe(WorldCenter - Viewport.CameraPosTime.xyz, float3(1.0, 0.0, 0.0));

    float3 AxisX = CamRight;
    float3 AxisY = CamUp;
    float  ExtentX = RadiusM;
    float  ExtentY = RadiusM;

    if (Out.Silhouette == ParticleSilhouetteSparkStreak)
    {
        float Speed = length(Velocity);
        if (Speed > 0.25)
        {
            float3 VelDir  = Velocity / Speed;
            float3 SideDir = VfNormalizeSafe(cross(CamFwd, VelDir), CamRight);
            float3 Aligned = VfNormalizeSafe(cross(SideDir, CamFwd), VelDir);
            AxisX   = SideDir;
            AxisY   = Aligned;
            ExtentX = RadiusM * 0.55;
            ExtentY = RadiusM * (1.2 + min(5.5, Speed * 0.28));
        }
    }
    else
    {
        float CosR = cos(RollRad);
        float SinR = sin(RollRad);
        AxisX = CamRight * CosR + CamUp * SinR;
        AxisY = -CamRight * SinR + CamUp * CosR;
    }

    float3 WorldPos = WorldCenter + AxisX * (Out.LocalQuadUv.x * ExtentX) + AxisY * (Out.LocalQuadUv.y * ExtentY);
    Out.ClipPosition = ParticleProjectWorldClip(Viewport, WorldPos);

    float Boost = max(0.5, Viewport.CameraUpBoost.w);
    Out.TintRgba = float4(Appear.ColorRgba.rgb * Boost, saturate(Appear.ColorRgba.a));
    return Out;
}

[shader("fragment")]
float4 ParticleBillboardFragment(BillboardVaryings In) : SV_Target0
{
    if (In.TintRgba.a <= 0.005)
    {
        discard;
    }

    float2 Uv   = In.LocalQuadUv;
    float  Dist = length(Uv);
    float  Alpha = 0.0;
    float3 Color = In.TintRgba.rgb;

    if (In.Silhouette == ParticleSilhouetteGlowOrb)
    {
        if (Dist >= 1.0)
        {
            discard;
        }
        float Halo   = pow(saturate(1.0 - Dist), 2.1);
        float HotCtr = exp(-Dist * Dist * 9.5);
        Color = lerp(Color, float3(1.0, 0.98, 0.94), HotCtr * 0.55);
        Alpha = In.TintRgba.a * saturate(Halo + HotCtr * 0.65);
    }
    else if (In.Silhouette == ParticleSilhouetteSparkStreak)
    {
        float Radial = length(float2(Uv.x * 1.15, Uv.y));
        if (Radial >= 1.0)
        {
            discard;
        }
        float StreakCenter = exp(-Uv.x * Uv.x * 7.5 - Uv.y * Uv.y * 2.2);
        Color = lerp(Color, float3(1.0, 0.97, 0.88), StreakCenter * 0.6);
        Alpha = In.TintRgba.a * saturate(pow(saturate(1.0 - Radial), 1.35) + StreakCenter * 0.5);
    }
    else if (In.Silhouette == ParticleSilhouetteShardQuad)
    {
        float Edge = max(abs(Uv.x), abs(Uv.y));
        if (Edge > 0.92)
        {
            discard;
        }
        float Bevel = 0.72 + 0.28 * (1.0 - Uv.y * 0.5 + Uv.x * 0.35);
        Color *= Bevel;
        Alpha = In.TintRgba.a * (1.0 - smoothstep(0.78, 0.92, Edge));
    }
    else
    {
        if (Dist >= 1.0)
        {
            discard;
        }
        float Soft = 1.0 - smoothstep(0.18, 1.0, Dist);
        float Rim  = 0.82 + 0.28 * saturate(1.0 - Dist * 1.15);
        Color *= Rim;
        Alpha = In.TintRgba.a * Soft;
    }

    // Premultiply for additive mode; straight alpha for alpha blend mode
    if (Viewport.ViewportControl.z == ParticleBlendAdditive)
    {
        return float4(Color * Alpha, Alpha);
    }
    return float4(Color, Alpha);
}
