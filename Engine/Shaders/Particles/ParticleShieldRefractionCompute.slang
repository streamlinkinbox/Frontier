/*============================================================================================================================================
                                               PARTICLESHIELDREFRACTIONCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Vulkan 1.3 Compute dual-mode solver: Mode 0 = Energy Shield impact ripple wave, Mode 1 = Space Refraction orbital singularity & thermal rise

#include "Engine/Shaders/Particles/ParticleChannelLayouts.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                                    DESCRIPTOR BINDINGS
//------------------------------------------------------------------------------------------------------------------------

[[vk::binding(0, 0)]] ConstantBuffer<GpuDomainUniforms>      DomainParams;
[[vk::binding(1, 0)]] RWStructuredBuffer<GpuKinematicsSlot>  KinematicsBuffer;
[[vk::binding(3, 0)]] RWStructuredBuffer<GpuAppearanceSlot>  AppearanceBuffer;

//------------------------------------------------------------------------------------------------------------------------
//                                                  COMPUTE ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(64, 1, 1)]
void EvaluateShieldRefractionMain(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint Slot        = DispatchThreadId.x;
    uint ActiveCount = DomainParams.DomainControl.x;
    if (Slot >= ActiveCount)
    {
        return;
    }

    uint  ModeCode     = DomainParams.DomainControl.z; // 0 = Energy Shield Ripple, 1 = Space Refraction Orbital/Thermal
    float DeltaTimeSec = DomainParams.TimingCounts.x;
    float TimeSeconds  = DomainParams.TimingCounts.y;

    GpuKinematicsSlot Kin = KinematicsBuffer[Slot];

    if (ModeCode == 0u)
    {
        // Energy Shield impact ripple propagation over spherical/domed/cylindrical hex cells
        float3 ImpactPoint    = DomainParams.AuxVectorScalarB.xyz;
        float  RippleSpeedHz  = max(0.2, DomainParams.ScalarPackC.x);
        float  ImpactTimeSec  = DomainParams.TimingCounts.z;

        float Elapsed = max(0.0, TimeSeconds - ImpactTimeSec);
        float RingR   = Elapsed * RippleSpeedHz * 1.4;
        float Decay   = exp(-Elapsed * 1.8);

        float  Dist  = length(Kin.PositionInvMass.xyz - ImpactPoint);
        float  Delta = Dist - RingR;
        float  Pulse = exp(-(Delta * Delta) * 5.0) * Decay;

        // Add subtle ambient shimmer scanline so shield cells stay visually alive between impacts
        float Shimmer = 0.14 + 0.12 * sin(Kin.PositionInvMass.z * 5.0 - TimeSeconds * 3.2);
        float Energy  = saturate(max(Pulse, Shimmer));

        GpuAppearanceSlot Appear = AppearanceBuffer[Slot];
        Appear.VisualMetrics.w = Energy;
        Appear.ColorRgba.a     = 0.35 + 0.65 * Energy;
        AppearanceBuffer[Slot] = Appear;
        return;
    }

    // Mode 1: Space Refraction orbital gravity well + tangential accretion swirl + thermal updraft
    if (Kin.PositionInvMass.w <= 0.0)
    {
        return;
    }

    float3 Singularity     = DomainParams.OriginScalarA.xyz;
    float  InnerRadiusM    = max(0.08, DomainParams.OriginScalarA.w);
    float  OrbitalStrength = DomainParams.ScalarPackC.x;
    float  ThermalRiseMs   = DomainParams.ScalarPackC.y;

    float3 Vel = Kin.VelocityDrag.xyz;
    if (OrbitalStrength > 1.0e-4)
    {
        float3 ToCenter = Singularity - Kin.PositionInvMass.xyz;
        float  R2       = dot(ToCenter, ToCenter) + InnerRadiusM * InnerRadiusM;
        float  InvR     = rsqrt(R2);
        float  Pull     = OrbitalStrength / R2;

        Vel.x += (ToCenter.x * InvR * Pull - ToCenter.y * InvR * Pull * 0.45) * DeltaTimeSec;
        Vel.y += (ToCenter.y * InvR * Pull + ToCenter.x * InvR * Pull * 0.45) * DeltaTimeSec;
        Vel.z += (ToCenter.z * InvR * Pull * 1.4) * DeltaTimeSec;
    }

    if (ThermalRiseMs > 1.0e-4)
    {
        Vel.z += (ThermalRiseMs - Vel.z) * min(1.0, 2.5 * DeltaTimeSec);
    }

    Kin.VelocityDrag.xyz   = Vel;
    KinematicsBuffer[Slot] = Kin;
}
