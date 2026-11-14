/*============================================================================================================================================
                                                    PARTICLERIBBONRASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Vulkan 1.3 GPU strand / cloth / ribbon rasterizer connecting live StrandConnectivity SSBO links directly on GPU

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Shaders/Particles/ParticleChannelShared.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                                       RESOURCES
//------------------------------------------------------------------------------------------------------------------------

[[vk::binding(0, 0)]]
ConstantBuffer<GpuParticleViewportUniforms> Viewport;

[[vk::binding(1, 0)]]
StructuredBuffer<GpuKinematicsSlot> KinematicsBuffer;

[[vk::binding(2, 0)]]
StructuredBuffer<GpuAppearanceSlot> AppearanceBuffer;

[[vk::binding(3, 0)]]
StructuredBuffer<GpuStrandLinkSlot> StrandLinkBuffer;

struct RibbonVaryings
{
    float4 ClipPosition : SV_Position;
    float2 RibbonUv     : TEXCOORD0;
    float4 TintRgba     : COLOR0;
    float3 ShadingNorm  : TEXCOORD1;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENTRY POINTS
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
RibbonVaryings ParticleRibbonVertex(
    uint VertexId   : SV_VulkanVertexID,
    uint InstanceId : SV_VulkanInstanceID)
{
    // Each instance renders 2 segments (12 verts): [0..5] longitudinal LinkPrevSlot, [6..11] lateral cloth LinkCrossSlot
    static const float2 StripUv[6] = {
        float2(0.0, -1.0), float2(1.0, -1.0), float2(1.0,  1.0),
        float2(0.0, -1.0), float2(1.0,  1.0), float2(0.0,  1.0)
    };

    RibbonVaryings Out;
    Out.RibbonUv    = StripUv[VertexId % 6u];
    Out.ShadingNorm = float3(0.0, 0.0, 1.0);

    uint ActiveCount = Viewport.ViewportControl.x;
    if (InstanceId >= ActiveCount)
    {
        Out.ClipPosition = float4(0.0, 0.0, -10.0, 1.0);
        Out.TintRgba     = float4(0.0, 0.0, 0.0, 0.0);
        return Out;
    }

    GpuStrandLinkSlot Link = StrandLinkBuffer[InstanceId];
    bool IsCrossLink       = (VertexId >= 6u);
    uint NeighborSlot      = IsCrossLink ? Link.LinkCrossSlot : Link.LinkPrevSlot;

    if (NeighborSlot >= ActiveCount)
    {
        Out.ClipPosition = float4(0.0, 0.0, -10.0, 1.0);
        Out.TintRgba     = float4(0.0, 0.0, 0.0, 0.0);
        return Out;
    }

    float3 PosA = KinematicsBuffer[InstanceId].PositionInvMass.xyz;
    float3 PosB = KinematicsBuffer[NeighborSlot].PositionInvMass.xyz;
    float3 Seg  = PosB - PosA;
    float  SegL = length(Seg);
    if (SegL <= 1.0e-5)
    {
        Out.ClipPosition = float4(0.0, 0.0, -10.0, 1.0);
        Out.TintRgba     = float4(0.0, 0.0, 0.0, 0.0);
        return Out;
    }

    float3 MidPos    = (PosA + PosB) * 0.5;
    float3 ViewDir   = VfNormalizeSafe(Viewport.CameraPosTime.xyz - MidPos, float3(1.0, 0.0, 0.0));
    float3 SegDir    = Seg / SegL;
    float3 SideVec   = VfNormalizeSafe(cross(ViewDir, SegDir), Viewport.CameraRightScale.xyz);
    float3 NormalVec = VfNormalizeSafe(cross(SegDir, SideVec), float3(0.0, 0.0, 1.0));

    GpuAppearanceSlot AppA = AppearanceBuffer[InstanceId];
    GpuAppearanceSlot AppB = AppearanceBuffer[NeighborSlot];

    float BaseWidth = max(0.008, asfloat(Viewport.ViewportControl.w));
    float WidthA    = max(BaseWidth, AppA.VisualMetrics.x * 0.65);
    float WidthB    = max(BaseWidth, AppB.VisualMetrics.x * 0.65);

    float  AlongT    = Out.RibbonUv.x;
    float  HalfW     = lerp(WidthA, WidthB, AlongT) * (IsCrossLink ? 0.85 : 1.0);
    float3 AnchorPos = lerp(PosA, PosB, AlongT);
    float3 WorldPos  = AnchorPos + SideVec * (Out.RibbonUv.y * HalfW);

    Out.ClipPosition = ParticleProjectWorldClip(Viewport, WorldPos);
    Out.ShadingNorm  = NormalVec;
    Out.TintRgba     = lerp(AppA.ColorRgba, AppB.ColorRgba, AlongT);
    Out.TintRgba.rgb *= max(0.5, Viewport.CameraUpBoost.w);
    return Out;
}

[shader("fragment")]
float4 ParticleRibbonFragment(RibbonVaryings In) : SV_Target0
{
    if (In.TintRgba.a <= 0.005)
    {
        discard;
    }

    float3 Normal  = normalize(In.ShadingNorm);
    float3 SunDir  = normalize(float3(0.42, -0.36, 0.83));
    float  NdotL   = abs(dot(Normal, SunDir));
    float  Profile = sqrt(saturate(1.0 - In.RibbonUv.y * In.RibbonUv.y));
    float  Shade   = 0.45 + 0.55 * NdotL * (0.75 + 0.25 * Profile);

    float3 LitRgb = In.TintRgba.rgb * Shade;
    float  Edge   = 1.0 - smoothstep(0.68, 1.0, abs(In.RibbonUv.y));
    float  Alpha  = In.TintRgba.a * Edge;
    return float4(LitRgb, Alpha);
}
