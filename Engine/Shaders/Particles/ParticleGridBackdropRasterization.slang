/*============================================================================================================================================
                                                PARTICLEGRIDBACKDROPRASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Vulkan 1.3 GPU studio OLED radial backdrop + 3D perspective +Z Up ground grid & axis rasterizer (zero CPU pixel loops)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Shaders/Particles/ParticleChannelLayouts.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                                       RESOURCES
//------------------------------------------------------------------------------------------------------------------------

[[vk::binding(0, 0)]]
ConstantBuffer<GpuParticleViewportUniforms> Viewport;

struct GridBackdropVaryings
{
    float4 ClipPosition    : SV_Position;
    float2 ScreenNdc       : TEXCOORD0;
    float3 WorldPoint      : TEXCOORD1;
    uint   SurfaceCategory : TEXCOORD2;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   FULLSCREEN BACKDROP ENTRY
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
GridBackdropVaryings ParticleBackdropVertex(uint VertexId : SV_VulkanVertexID)
{
    static const float2 TriPos[3] = {
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
        float2(-1.0,  3.0)
    };
    GridBackdropVaryings Out;
    Out.ScreenNdc       = TriPos[VertexId % 3u];
    Out.ClipPosition    = float4(Out.ScreenNdc, 0.0, 1.0); // Reverse-Z far plane (0.0)
    Out.WorldPoint      = float3(0.0, 0.0, 0.0);
    Out.SurfaceCategory = 0u;
    return Out;
}

[shader("fragment")]
float4 ParticleBackdropFragment(GridBackdropVaryings In) : SV_Target0
{
    float  Radial = length(In.ScreenNdc * float2(0.72, 0.92));
    float3 Center = float3(0.045, 0.052, 0.066);
    float3 Edge   = float3(0.014, 0.015, 0.019);
    float3 Color  = lerp(Center, Edge, saturate(Radial * 0.92));
    return float4(Color, 1.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   3D FLOOR GRID PLANE ENTRY
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
GridBackdropVaryings ParticleFloorGridVertex(uint VertexId : SV_VulkanVertexID)
{
    static const float2 PlaneQuad[6] = {
        float2(-1.0, -1.0), float2( 1.0, -1.0), float2( 1.0,  1.0),
        float2(-1.0, -1.0), float2( 1.0,  1.0), float2(-1.0,  1.0)
    };
    float  HalfExtent = 18.0;
    float2 LocalXy    = PlaneQuad[VertexId % 6u] * HalfExtent;
    float3 WorldPos   = float3(LocalXy.x, LocalXy.y, 0.0);

    GridBackdropVaryings Out;
    Out.WorldPoint      = WorldPos;
    Out.ScreenNdc       = PlaneQuad[VertexId % 6u];
    Out.ClipPosition    = ParticleProjectWorldClip(Viewport, WorldPos);
    Out.SurfaceCategory = 1u;
    return Out;
}

[shader("fragment")]
float4 ParticleFloorGridFragment(GridBackdropVaryings In) : SV_Target0
{
    float2 Coord  = In.WorldPoint.xy;
    float  DistM  = length(Coord);
    float  Fade   = 1.0 - smoothstep(4.0, 17.5, DistM);
    if (Fade <= 0.004)
    {
        discard;
    }

    // Anti-aliased 1 m minor grid & 5 m major grid
    float2 MinorDeriv = max(fwidth(Coord), float2(1.0e-4, 1.0e-4));
    float2 MinorCell  = abs(frac(Coord - 0.5) - 0.5) / MinorDeriv;
    float  MinorLine  = 1.0 - min(min(MinorCell.x, MinorCell.y), 1.0);

    float2 MajorCoord = Coord * 0.2;
    float2 MajorDeriv = max(fwidth(MajorCoord), float2(1.0e-4, 1.0e-4));
    float2 MajorCell  = abs(frac(MajorCoord - 0.5) - 0.5) / MajorDeriv;
    float  MajorLine  = 1.0 - min(min(MajorCell.x, MajorCell.y), 1.0);

    // Axis highlight lines (+X coral red, +Y emerald green)
    float AxisXLine = 1.0 - saturate(abs(Coord.y) / max(MinorDeriv.y * 1.4, 0.015));
    float AxisYLine = 1.0 - saturate(abs(Coord.x) / max(MinorDeriv.x * 1.4, 0.015));

    float3 LineColor = float3(0.18, 0.20, 0.24);
    float  Alpha     = MinorLine * 0.24 + MajorLine * 0.34;
    if (MajorLine > MinorLine * 0.5)
    {
        LineColor = float3(0.28, 0.31, 0.36);
    }
    if (AxisXLine > 0.2)
    {
        LineColor = lerp(LineColor, float3(0.84, 0.34, 0.32), AxisXLine);
        Alpha     = max(Alpha, AxisXLine * 0.72);
    }
    if (AxisYLine > 0.2)
    {
        LineColor = lerp(LineColor, float3(0.32, 0.78, 0.48), AxisYLine);
        Alpha     = max(Alpha, AxisYLine * 0.72);
    }

    Alpha *= Fade;
    if (Alpha <= 0.005)
    {
        discard;
    }
    return float4(LineColor, Alpha);
}
