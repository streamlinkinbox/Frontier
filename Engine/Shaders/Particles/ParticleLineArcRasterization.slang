/*============================================================================================================================================
                                                  PARTICLELINEARCRASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Vulkan 1.3 GPU camera-facing 3D segment, silk strand, dielectric arc & laser beam rasterizer

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Shaders/Particles/ParticleChannelLayouts.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                                       RESOURCES
//------------------------------------------------------------------------------------------------------------------------

struct GpuSegmentPrimitive
{
    float4 StartPosWidth;  // xyz = StartPosMeters [m], w = HalfWidthMeters [m]
    float4 EndPosEmissive; // xyz = EndPosMeters [m],   w = EmissiveGain [-]
    float4 ColorStartRgba; // rgba = Linear color at segment start
    float4 ColorEndRgba;   // rgba = Linear color at segment end
};

[[vk::binding(0, 0)]]
ConstantBuffer<GpuParticleViewportUniforms> Viewport;

[[vk::binding(1, 0)]]
StructuredBuffer<GpuSegmentPrimitive> SegmentBuffer;

struct SegmentVaryings
{
    float4 ClipPosition : SV_Position;
    float2 RibbonUv     : TEXCOORD0;
    float4 TintRgba     : COLOR0;
    float  EmissiveGain : TEXCOORD1;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENTRY POINTS
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
SegmentVaryings ParticleLineArcVertex(
    uint VertexId   : SV_VulkanVertexID,
    uint InstanceId : SV_VulkanInstanceID)
{
    static const float2 StripCorners[6] = {
        float2(0.0, -1.0), float2(1.0, -1.0), float2(1.0,  1.0),
        float2(0.0, -1.0), float2(1.0,  1.0), float2(0.0,  1.0)
    };

    SegmentVaryings Out;
    Out.ClipPosition = float4(0.0, 0.0, 2.0, 1.0);
    Out.RibbonUv     = float2(0.0, 0.0);
    Out.TintRgba     = float4(0.0, 0.0, 0.0, 0.0);
    Out.EmissiveGain = 0.0;

    if (InstanceId >= Viewport.ViewportControl.x)
    {
        return Out;
    }

    const GpuSegmentPrimitive Seg = SegmentBuffer[InstanceId];
    const float3 PosA  = Seg.StartPosWidth.xyz;
    const float3 PosB  = Seg.EndPosEmissive.xyz;
    const float  HalfW = max(Seg.StartPosWidth.w, 0.0025);

    const float3 MidPos  = 0.5 * (PosA + PosB);
    const float3 Delta   = PosB - PosA;
    const float  SegLen  = length(Delta);
    const float3 Tangent = (SegLen > 1e-5) ? (Delta / SegLen) : float3(1.0, 0.0, 0.0);
    const float3 ViewDir = normalize(Viewport.CameraPosTime.xyz - MidPos);
    float3 SideDir       = cross(ViewDir, Tangent);
    const float SideLen  = length(SideDir);
    if (SideLen > 1e-5)
    {
        SideDir /= SideLen;
    }
    else
    {
        SideDir = Viewport.CameraRightScale.xyz;
    }

    // Clamp minimum screen-readable thickness by distance so fine webs/arcs never vanish to 0 pixels
    const float DistCam   = max(length(Viewport.CameraPosTime.xyz - MidPos), 0.5);
    const float MinWidth  = DistCam * 0.0026;
    const float WidthFinal = max(HalfW, MinWidth);

    const float2 Corner  = StripCorners[VertexId % 6u];
    const float3 BasePos = lerp(PosA, PosB, Corner.x);
    const float3 WorldPos = BasePos + SideDir * (Corner.y * WidthFinal);

    Out.ClipPosition = float4(
        dot(Viewport.ViewProjRow0, float4(WorldPos, 1.0)),
        dot(Viewport.ViewProjRow1, float4(WorldPos, 1.0)),
        dot(Viewport.ViewProjRow2, float4(WorldPos, 1.0)),
        dot(Viewport.ViewProjRow3, float4(WorldPos, 1.0)));
    Out.RibbonUv     = Corner;
    Out.TintRgba     = lerp(Seg.ColorStartRgba, Seg.ColorEndRgba, Corner.x);
    Out.EmissiveGain = Seg.EndPosEmissive.w;
    return Out;
}

[shader("fragment")]
float4 ParticleLineArcFragment(SegmentVaryings In) : SV_Target0
{
    const float CrossDist = abs(In.RibbonUv.y);
    if (CrossDist > 1.0)
    {
        discard;
    }

    const float CoreMask = smoothstep(1.0, 0.18, CrossDist);
    const float HotCore  = exp(-CrossDist * CrossDist * 5.2);
    const float Alpha    = saturate(In.TintRgba.a * CoreMask);
    if (Alpha <= 0.003)
    {
        discard;
    }

    const float Glow = max(In.EmissiveGain, 0.0);
    float3 Rgb = In.TintRgba.rgb * (0.85 + 0.55 * CoreMask);
    if (Glow > 0.01)
    {
        Rgb = Rgb * (1.0 + Glow * (0.45 + 1.15 * HotCore)) + float3(0.95, 0.98, 1.0) * (HotCore * Glow * 0.55);
    }

    // Additive pass encodes BlendMode == 1u -> premultiply intensity by alpha
    if (Viewport.ViewportControl.z == 1u)
    {
        return float4(Rgb * Alpha, Alpha);
    }
    return float4(Rgb, Alpha);
}
