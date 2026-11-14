/*============================================================================================================================================
                                              PARTICLEORIENTEDSOLIDRASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Vulkan 1.3 instanced 3D oriented solid GPU rasterizer (Quaternion-rotated Cubes, Shards, Ferrofluid Spikes, Winged Boids & Coins)
// Uses SV_VulkanVertexID / SV_VulkanInstanceID to stay 100% free of SpvCapabilityDrawParameters on all Vulkan 1.3 devices.

#include "Engine/Shaders/Particles/ParticleChannelLayouts.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                                    DESCRIPTOR BINDINGS
//------------------------------------------------------------------------------------------------------------------------

[[vk::binding(0, 0)]] ConstantBuffer<GpuParticleViewportUniforms> ViewportParams;
[[vk::binding(1, 0)]] StructuredBuffer<GpuKinematicsSlot>         KinematicsBuffer;
[[vk::binding(2, 0)]] StructuredBuffer<GpuLifespanSlot>           LifespanBuffer;
[[vk::binding(3, 0)]] StructuredBuffer<GpuAppearanceSlot>         AppearanceBuffer;
[[vk::binding(4, 0)]] StructuredBuffer<GpuOrientationSlot>        OrientationBuffer;

//------------------------------------------------------------------------------------------------------------------------
//                                                    VERTEX INTERPOLANTS
//------------------------------------------------------------------------------------------------------------------------

struct SolidRasterVertexOutput
{
    float4 ClipPosition : SV_Position;
    float3 WorldNormal  : TEXCOORD0;
    float3 WorldPos     : TEXCOORD1;
    float4 LinearColor  : TEXCOORD2;
    float  MetallicHint : TEXCOORD3;
};

//------------------------------------------------------------------------------------------------------------------------
//                                               PROCEDURAL 3D SOLID PROFILES
//------------------------------------------------------------------------------------------------------------------------

// Returns 36 vertices (12 triangles) for a closed 3D unit solid in local space
void FetchUnitCubeVertex(uint VertIdx, out float3 LocalPos, out float3 LocalNormal)
{
    static const float3 Corners[8] = {
        float3(-0.5, -0.5, -0.5), float3( 0.5, -0.5, -0.5),
        float3( 0.5,  0.5, -0.5), float3(-0.5,  0.5, -0.5),
        float3(-0.5, -0.5,  0.5), float3( 0.5, -0.5,  0.5),
        float3( 0.5,  0.5,  0.5), float3(-0.5,  0.5,  0.5)
    };
    static const uint Indices[36] = {
        4u, 5u, 6u,  4u, 6u, 7u, // +Z Top
        0u, 2u, 1u,  0u, 3u, 2u, // -Z Bottom
        1u, 2u, 6u,  1u, 6u, 5u, // +X Front
        0u, 4u, 7u,  0u, 7u, 3u, // -X Back
        2u, 3u, 7u,  2u, 7u, 6u, // +Y Left
        0u, 1u, 5u,  0u, 5u, 4u  // -Y Right
    };
    static const float3 FaceNormals[6] = {
        float3( 0.0,  0.0,  1.0), float3( 0.0,  0.0, -1.0),
        float3( 1.0,  0.0,  0.0), float3(-1.0,  0.0,  0.0),
        float3( 0.0,  1.0,  0.0), float3( 0.0, -1.0,  0.0)
    };

    uint FaceIdx = VertIdx / 6u;
    LocalPos    = Corners[Indices[VertIdx]];
    LocalNormal = FaceNormals[FaceIdx];
}

// 3D Octagonal Rosensweig Ferrofluid Cone/Spike (36 vertices: 8 lateral facets + 4 cap tris)
void FetchSpikeVertex(uint VertIdx, out float3 LocalPos, out float3 LocalNormal)
{
    uint TriIdx    = VertIdx / 3u;
    uint CornerIdx = VertIdx % 3u;
    if (TriIdx < 8u)
    {
        float A0 = (float(TriIdx)     / 8.0) * 6.28318530718;
        float A1 = (float(TriIdx + 1) / 8.0) * 6.28318530718;
        float3 P0 = float3(cos(A0) * 0.58, sin(A0) * 0.58, -0.35);
        float3 P1 = float3(cos(A1) * 0.58, sin(A1) * 0.58, -0.35);
        float3 Apex = float3(0.0, 0.0, 1.35);
        LocalPos    = (CornerIdx == 0u) ? P0 : ((CornerIdx == 1u) ? P1 : Apex);
        LocalNormal = normalize(cross(P1 - P0, Apex - P0));
    }
    else
    {
        // Base cap triangles (TriIdx 8..11)
        uint Seg = (TriIdx - 8u) * 2u;
        float A0 = (float(Seg)     / 8.0) * 6.28318530718;
        float A1 = (float(Seg + 2) / 8.0) * 6.28318530718;
        float3 Center = float3(0.0, 0.0, -0.35);
        float3 P0     = float3(cos(A0) * 0.58, sin(A0) * 0.58, -0.35);
        float3 P1     = float3(cos(A1) * 0.58, sin(A1) * 0.58, -0.35);
        LocalPos    = (CornerIdx == 0u) ? Center : ((CornerIdx == 1u) ? P1 : P0);
        LocalNormal = float3(0.0, 0.0, -1.0);
    }
}

// 3D Articulated Winged Boid / Creature silhouette (36 vertices: fuselage + flapping swept wings + tail)
void FetchWingedBoidVertex(uint VertIdx, float FlapRad, out float3 LocalPos, out float3 LocalNormal)
{
    float WingLift = sin(FlapRad) * 0.52;
    float3 Nose      = float3( 0.00,  0.95,  0.00);
    float3 Tail      = float3( 0.00, -0.85,  0.06);
    float3 SpineTop  = float3( 0.00,  0.05,  0.22);
    float3 SpineBot  = float3( 0.00,  0.05, -0.14);
    float3 LeftTip   = float3(-1.35, -0.12,  WingLift);
    float3 RightTip  = float3( 1.35, -0.12,  WingLift);
    float3 LeftMid   = float3(-0.42,  0.22,  0.08 + WingLift * 0.35);
    float3 RightMid  = float3( 0.42,  0.22,  0.08 + WingLift * 0.35);
    float3 LeftTail  = float3(-0.34, -0.95,  0.12);
    float3 RightTail = float3( 0.34, -0.95,  0.12);

    uint TriIdx    = VertIdx / 3u;
    uint CornerIdx = VertIdx % 3u;
    float3 A = Nose;
    float3 B = LeftMid;
    float3 C = SpineTop;

    if      (TriIdx == 0u) { A = Nose; B = SpineTop; C = LeftMid; }
    else if (TriIdx == 1u) { A = Nose; B = RightMid; C = SpineTop; }
    else if (TriIdx == 2u) { A = SpineTop; B = Tail; C = LeftMid; }
    else if (TriIdx == 3u) { A = SpineTop; B = RightMid; C = Tail; }
    else if (TriIdx == 4u) { A = LeftMid; B = LeftTip; C = Tail; }
    else if (TriIdx == 5u) { A = RightMid; B = Tail; C = RightTip; }
    else if (TriIdx == 6u) { A = Nose; B = LeftMid; C = SpineBot; }
    else if (TriIdx == 7u) { A = Nose; B = SpineBot; C = RightMid; }
    else if (TriIdx == 8u) { A = LeftMid; B = Tail; C = LeftTip; }
    else if (TriIdx == 9u) { A = RightMid; B = RightTip; C = Tail; }
    else if (TriIdx == 10u){ A = Tail; B = LeftTail; C = RightTail; }
    else                   { A = Tail; B = RightTail; C = LeftTail; }

    LocalPos    = (CornerIdx == 0u) ? A : ((CornerIdx == 1u) ? B : C);
    LocalNormal = normalize(cross(B - A, C - A) + float3(0.0, 0.0, 1.0e-5));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  VERTEX ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
SolidRasterVertexOutput VertexMain(uint VertexId : SV_VulkanVertexID, uint InstanceId : SV_VulkanInstanceID)
{
    SolidRasterVertexOutput Output;
    Output.ClipPosition = float4(2.0, 2.0, 2.0, 1.0);
    Output.WorldNormal  = float3(0.0, 0.0, 1.0);
    Output.WorldPos     = float3(0.0, 0.0, 0.0);
    Output.LinearColor  = float4(0.0, 0.0, 0.0, 0.0);
    Output.MetallicHint = 0.0;

    uint ActiveCount = ViewportParams.ViewportControl.x;
    if (InstanceId >= ActiveCount || VertexId >= 36u)
    {
        return Output;
    }

    GpuLifespanSlot Life = LifespanBuffer[InstanceId];
    if ((Life.LivenessMask & 1u) == 0u)
    {
        return Output;
    }

    GpuKinematicsSlot  Kin    = KinematicsBuffer[InstanceId];
    GpuAppearanceSlot  Appear = AppearanceBuffer[InstanceId];
    GpuOrientationSlot Orient = OrientationBuffer[InstanceId];

    float RadiusM = max(0.015, Appear.VisualMetrics.x * ViewportParams.CameraRightScale.w);
    uint  Profile = ViewportParams.ViewportControl.y;

    float3 LocalPos    = float3(0.0, 0.0, 0.0);
    float3 LocalNormal = float3(0.0, 0.0, 1.0);
    float  MetalHint   = 0.15;

    if (Profile == SolidProfileSpike)
    {
        FetchSpikeVertex(VertexId, LocalPos, LocalNormal);
        LocalPos.z *= (1.1 + 1.4 * Appear.VisualMetrics.w);
        MetalHint = 0.88;
    }
    else if (Profile == SolidProfileWingedBoid)
    {
        float FlapRad = ViewportParams.CameraPosTime.w * 11.5 + float(InstanceId) * 0.73;
        FetchWingedBoidVertex(VertexId, FlapRad, LocalPos, LocalNormal);
        MetalHint = 0.12;
    }
    else if (Profile == SolidProfileShard)
    {
        FetchUnitCubeVertex(VertexId, LocalPos, LocalNormal);
        // Faceted crystalline shard proportions (elongated + tapered prism)
        LocalPos.x *= 1.35;
        LocalPos.y *= 0.85;
        LocalPos.z *= 0.42;
        if (LocalPos.x > 0.0) { LocalPos.y *= 0.55; }
        MetalHint = 0.45;
    }
    else if (Profile == SolidProfileCoinDisc)
    {
        FetchUnitCubeVertex(VertexId, LocalPos, LocalNormal);
        LocalPos.x *= 1.15;
        LocalPos.y *= 1.15;
        LocalPos.z *= 0.18;
        MetalHint = 0.92;
    }
    else
    {
        // SolidProfileCube (MoGraph Lattice / Derez / Voxel Cube)
        FetchUnitCubeVertex(VertexId, LocalPos, LocalNormal);
        LocalPos.z *= (0.85 + 1.65 * Appear.VisualMetrics.w);
        MetalHint = 0.35;
    }

    float4 Quat = ParticleNormalizeQuat(Orient.QuaternionXyzw);
    // Apply per-slot in-plane roll when OrientationChannel is still identity
    if (abs(Quat.w - 1.0) < 1.0e-4 && abs(Appear.VisualMetrics.y) > 1.0e-4)
    {
        float HalfR = Appear.VisualMetrics.y * 0.5;
        Quat = float4(0.0, 0.0, sin(HalfR), cos(HalfR));
    }

    float3 RotatedPos = ParticleRotateByQuat(Quat, LocalPos * (RadiusM * 2.2));
    float3 WorldPos   = Kin.PositionInvMass.xyz + RotatedPos;
    float3 WorldNorm  = normalize(ParticleRotateByQuat(Quat, LocalNormal));

    Output.ClipPosition = ParticleProjectWorldClip(ViewportParams, WorldPos);
    Output.WorldNormal  = WorldNorm;
    Output.WorldPos     = WorldPos;
    Output.LinearColor  = Appear.ColorRgba;
    Output.MetallicHint = MetalHint;
    return Output;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 FRAGMENT ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

[shader("fragment")]
float4 FragmentMain(SolidRasterVertexOutput Input) : SV_Target0
{
    float3 Normal  = normalize(Input.WorldNormal);
    float3 ViewDir = normalize(ViewportParams.CameraPosTime.xyz - Input.WorldPos);
    float3 KeyDir  = normalize(float3(0.55, -0.45, 0.70));
    float3 FillDir = normalize(float3(-0.62, 0.52, 0.35));

    float KeyDiff  = saturate(dot(Normal, KeyDir));
    float FillDiff = saturate(dot(Normal, FillDir)) * 0.32;
    float HemiSky  = 0.24 + 0.22 * (Normal.z * 0.5 + 0.5);

    float3 HalfVec = normalize(KeyDir + ViewDir);
    float  SpecPow = lerp(18.0, 64.0, Input.MetallicHint);
    float  Spec    = pow(saturate(dot(Normal, HalfVec)), SpecPow) * lerp(0.28, 1.15, Input.MetallicHint);
    float  Fresnel = pow(1.0 - saturate(dot(Normal, ViewDir)), 3.0) * 0.42;

    float3 Albedo  = Input.LinearColor.rgb * ViewportParams.CameraUpBoost.w;
    float3 LitRgb  = Albedo * (HemiSky + KeyDiff * 0.92 + FillDiff)
                   + lerp(float3(1.0, 0.96, 0.90), Albedo + float3(0.35, 0.42, 0.52), Input.MetallicHint) * (Spec + Fresnel);

    return float4(LitRgb, saturate(Input.LinearColor.a));
}
