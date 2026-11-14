/*============================================================================================================================================
                                                PARTICLELATTICEEFFECTORCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Vulkan 1.3 Compute solver for MoGraph travelling radial effector falloff, lattice extrusion, derez ballistic launch & 3D twist quaternion

#include "Engine/Shaders/Particles/ParticleChannelLayouts.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                                    DESCRIPTOR BINDINGS
//------------------------------------------------------------------------------------------------------------------------

[[vk::binding(0, 0)]] ConstantBuffer<GpuDomainUniforms>       DomainParams;
[[vk::binding(1, 0)]] RWStructuredBuffer<GpuKinematicsSlot>   KinematicsBuffer;
[[vk::binding(3, 0)]] RWStructuredBuffer<GpuAppearanceSlot>   AppearanceBuffer;
[[vk::binding(4, 0)]] RWStructuredBuffer<GpuOrientationSlot>  OrientationBuffer;
[[vk::binding(9, 0)]] RWStructuredBuffer<GpuLatticeGridSlot>  LatticeGridBuffer;

//------------------------------------------------------------------------------------------------------------------------
//                                                  COMPUTE ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(64, 1, 1)]
void EvaluateLatticeEffectorMain(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint Slot        = DispatchThreadId.x;
    uint ActiveCount = DomainParams.DomainControl.x;
    if (Slot >= ActiveCount)
    {
        return;
    }

    float  DeltaTimeSec     = max(1.0e-4, DomainParams.TimingCounts.x);
    float  TimeSeconds      = DomainParams.TimingCounts.y;
    float3 OriginMeters     = DomainParams.OriginScalarA.xyz;
    float  ExtrusionMeters  = max(0.1, DomainParams.OriginScalarA.w);
    float  FalloffMeters    = max(0.25, DomainParams.ScalarPackC.x);
    float  WaveFreqHz       = DomainParams.ScalarPackC.y;
    float  SpacingMeters    = max(0.05, DomainParams.ScalarPackC.z);
    uint   DerezLaunch      = DomainParams.DomainControl.z;

    float Angle   = TimeSeconds * 6.28318530718 * WaveFreqHz;
    float EffX    = OriginMeters.x + cos(Angle * 0.5) * (FalloffMeters * 0.75);
    float EffY    = OriginMeters.y + sin(Angle * 0.5) * (FalloffMeters * 0.75);
    float InvFall = 1.0 / FalloffMeters;
    float Smooth  = 1.0 - exp(-8.5 * DeltaTimeSec);

    GpuLatticeGridSlot Grid   = LatticeGridBuffer[Slot];
    GpuKinematicsSlot  Kin    = KinematicsBuffer[Slot];
    GpuOrientationSlot Orient = OrientationBuffer[Slot];

    float Dx       = Grid.RestGridExtrusion.x - EffX;
    float Dy       = Grid.RestGridExtrusion.y - EffY;
    float NormDist = sqrt(Dx * Dx + Dy * Dy) * InvFall;
    float Falloff  = max(0.0, 1.0 - NormDist);
    float SmoothF  = Falloff * Falloff * (3.0 - 2.0 * Falloff);

    float TargetH = ExtrusionMeters * SmoothF * (0.65 + 0.35 * sin(Angle + Grid.WavePhase));

    if (DerezLaunch != 0u && Kin.PositionInvMass.w <= 0.0 && SmoothF > 0.65)
    {
        Kin.PositionInvMass.w = 1.0;
        Kin.VelocityDrag.xyz  = float3(0.0, 0.0, 2.2 + TargetH * 1.4);
        Orient.AngVelInertia.xyz = float3(
            sin(Grid.WavePhase) * 3.0,
            cos(Grid.WavePhase) * 3.0,
            2.4);
    }

    if (DerezLaunch == 0u || Kin.PositionInvMass.w <= 0.0)
    {
        float PrevH = Grid.RestGridExtrusion.w;
        Grid.RestGridExtrusion.w += (TargetH - PrevH) * Smooth;
        float NextZ = Grid.RestGridExtrusion.z + Grid.RestGridExtrusion.w;

        Kin.VelocityDrag.xyz = float3(0.0, 0.0, (Grid.RestGridExtrusion.w - PrevH) / DeltaTimeSec);
        Kin.PositionInvMass.xyz = float3(Grid.RestGridExtrusion.x, Grid.RestGridExtrusion.y, NextZ);

        // Smooth Z-axis twist quaternion proportional to effector activation
        float HalfTwist = SmoothF * 1.15;
        Orient.QuaternionXyzw = float4(0.0, 0.0, sin(HalfTwist), cos(HalfTwist));
    }
    else
    {
        Orient.QuaternionXyzw = ParticleIntegrateQuat(Orient.QuaternionXyzw, Orient.AngVelInertia.xyz, DeltaTimeSec);
    }

    LatticeGridBuffer[Slot] = Grid;
    KinematicsBuffer[Slot]  = Kin;
    OrientationBuffer[Slot] = Orient;

    float Gain = saturate(Grid.RestGridExtrusion.w / ExtrusionMeters);
    GpuAppearanceSlot Appear = AppearanceBuffer[Slot];
    Appear.VisualMetrics.x = SpacingMeters * (0.35 + 0.25 * Gain);
    Appear.VisualMetrics.w = Gain;
    AppearanceBuffer[Slot] = Appear;
}
