/*============================================================================================================================================
                                                    PARTICLEBOIDSFLOCKCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Vulkan 1.3 Compute Reynolds Boids flocking (separation, alignment, cohesion, roost containment, predator evade, wingbeat & 3D banking)

#include "Engine/Shaders/Particles/ParticleChannelLayouts.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                                    DESCRIPTOR BINDINGS
//------------------------------------------------------------------------------------------------------------------------

[[vk::binding(0, 0)]] ConstantBuffer<GpuDomainUniforms>        DomainParams;
[[vk::binding(1, 0)]] RWStructuredBuffer<GpuKinematicsSlot>    KinematicsBuffer;
[[vk::binding(4, 0)]] RWStructuredBuffer<GpuOrientationSlot>   OrientationBuffer;
[[vk::binding(7, 0)]] RWStructuredBuffer<GpuFlockSteeringSlot> FlockSteeringBuffer;

//------------------------------------------------------------------------------------------------------------------------
//                                                  COMPUTE ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(64, 1, 1)]
void EvaluateBoidsFlockMain(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint Slot        = DispatchThreadId.x;
    uint ActiveCount = DomainParams.DomainControl.x;
    if (Slot >= ActiveCount)
    {
        return;
    }

    float  DeltaTimeSec     = DomainParams.TimingCounts.x;
    float3 HomeOrigin       = DomainParams.OriginScalarA.xyz;
    float  HomeRadiusM      = max(0.5, DomainParams.OriginScalarA.w);
    float3 PredatorCenter   = DomainParams.AuxVectorScalarB.xyz;
    float  PredatorRadiusM  = DomainParams.AuxVectorScalarB.w;
    float  PerceptionM      = max(0.25, DomainParams.ScalarPackC.x);
    float  SeparationM      = max(0.10, DomainParams.ScalarPackC.y);
    float  CruiseSpeedMs    = max(0.50, DomainParams.ScalarPackC.z);
    float  MaxSteerAccelMs2 = max(1.00, DomainParams.ScalarPackC.w);
    uint   PredatorActive   = DomainParams.DomainControl.z;

    GpuKinematicsSlot    SelfKin   = KinematicsBuffer[Slot];
    GpuFlockSteeringSlot SelfFlock = FlockSteeringBuffer[Slot];

    float3 SelfPos = SelfKin.PositionInvMass.xyz;
    float3 SelfVel = SelfKin.VelocityDrag.xyz;

    float PercSq = PerceptionM * PerceptionM;
    float SepSq  = SeparationM * SeparationM;
    uint  Stride = max(1u, ActiveCount / 96u);

    float3 SepAccum  = float3(0.0, 0.0, 0.0);
    float3 AliAccum  = float3(0.0, 0.0, 0.0);
    float3 CohAccum  = float3(0.0, 0.0, 0.0);
    uint   Neighbors = 0u;

    for (uint Other = (Slot % Stride); Other < ActiveCount; Other += Stride)
    {
        if (Other == Slot)
        {
            continue;
        }
        GpuKinematicsSlot OtherKin = KinematicsBuffer[Other];
        float3 Delta  = SelfPos - OtherKin.PositionInvMass.xyz;
        float  DistSq = dot(Delta, Delta);
        if (DistSq > 1.0e-6 && DistSq < PercSq)
        {
            Neighbors += 1u;
            AliAccum  += OtherKin.VelocityDrag.xyz;
            CohAccum  += OtherKin.PositionInvMass.xyz;
            if (DistSq < SepSq)
            {
                SepAccum += Delta / max(0.08, DistSq);
            }
        }
    }

    float3 Accel = SepAccum * 1.4;
    if (Neighbors > 0u)
    {
        float InvN = 1.0 / float(Neighbors);
        Accel += (AliAccum * InvN - SelfVel) * 1.15 + (CohAccum * InvN - SelfPos) * 0.85;
    }

    // Soft containment pull toward roost center
    float3 ToHome = HomeOrigin - SelfPos;
    float  HDist  = length(ToHome);
    if (HDist > HomeRadiusM)
    {
        Accel += ToHome * ((HDist - HomeRadiusM) * 2.6 / max(0.1, HDist));
    }

    // Dynamic predator sphere evasion
    if (PredatorActive != 0u)
    {
        float3 EvadeDelta = SelfPos - PredatorCenter;
        float  EvadeRad   = PredatorRadiusM + PerceptionM;
        float  DistSq     = dot(EvadeDelta, EvadeDelta);
        if (DistSq > 1.0e-5 && DistSq < EvadeRad * EvadeRad)
        {
            Accel += EvadeDelta * (12.0 / sqrt(DistSq));
        }
    }

    float AccMag = length(Accel);
    if (AccMag > MaxSteerAccelMs2)
    {
        Accel *= (MaxSteerAccelMs2 / AccMag);
    }

    SelfVel += Accel * DeltaTimeSec;
    float Speed = length(SelfVel);
    float3 Heading = SelfFlock.HeadingWingPhase.xyz;
    if (Speed > 1.0e-4)
    {
        float Desired = clamp(Speed, CruiseSpeedMs * 0.6, CruiseSpeedMs * 1.45);
        SelfVel *= (Desired / Speed);
        Heading  = SelfVel / Desired;
        Speed    = Desired;
    }

    SelfKin.VelocityDrag.xyz = SelfVel;
    KinematicsBuffer[Slot]   = SelfKin;

    SelfFlock.HeadingWingPhase.xyz = Heading;
    SelfFlock.HeadingWingPhase.w  += (9.5 + Speed * 1.4) * DeltaTimeSec;
    SelfFlock.SteerSpecies.xyz     = Accel;
    FlockSteeringBuffer[Slot]      = SelfFlock;

    // Derive banked 3D flight quaternion from forward heading + lateral acceleration
    float3 WorldUp  = float3(0.0, 0.0, 1.0);
    float3 BankUp   = normalize(WorldUp + Accel * 0.045);
    float3 RightDir = cross(Heading, BankUp);
    if (dot(RightDir, RightDir) < 1.0e-5)
    {
        RightDir = float3(1.0, 0.0, 0.0);
    }
    RightDir = normalize(RightDir);
    float3 OrthoUp = normalize(cross(RightDir, Heading));

    GpuOrientationSlot Orient = OrientationBuffer[Slot];
    Orient.QuaternionXyzw = ParticleQuatFromBasis(RightDir, Heading, OrthoUp);
    OrientationBuffer[Slot] = Orient;
}
