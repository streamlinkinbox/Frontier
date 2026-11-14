/*============================================================================================================================================
                                                        PARTICLEADVECTCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Pure Slang compute simulation pass: ballistics, gravity (-Z), curl noise, 19 ForceModels, ball/floor colliders & ColourRampSampling

import ColourRampSampling;
import ParticleBuffers;
#include "ParticleForceCoupling.slang"

struct DispatchPushParams
{
    uint SlotOffset;
    uint SlotCount;
    uint ForceSourceCount;
    uint SpawnCursor;
};

[[vk::push_constant]]
DispatchPushParams PushParams;

[[vk::binding(0, 0)]] ConstantBuffer<GlobalParticleUniform>    GlobalCfg;
[[vk::binding(1, 0)]] ConstantBuffer<RigParticleUniform>       RigCfg;
[[vk::binding(2, 0)]] ConstantBuffer<ColourRampLut64Buffer>    ColourLut;
[[vk::binding(3, 0)]] RWStructuredBuffer<ParticleSlot>         ParticleBuffer;
[[vk::binding(5, 0)]] StructuredBuffer<ForceSourceShaderEntry> ForceSources;
[[vk::binding(6, 0)]] StructuredBuffer<float4>                 ForceWaypoints;

float3 SampleCurlNoise3D(float3 P, float Freq, float TimeSec)
{
    float3 Q = P * max(0.05, Freq) + float3(0.0, 0.0, TimeSec);
    float E = 0.04;
    float Inv2E = 1.0 / (2.0 * E);

    float dZ_dY = (SampleLatticeNoise3D(Q + float3(0.0, E, 0.0) + float3(17.3, 0.0, 0.0)) -
                   SampleLatticeNoise3D(Q - float3(0.0, E, 0.0) + float3(17.3, 0.0, 0.0))) * Inv2E;
    float dY_dZ = (SampleLatticeNoise3D(Q + float3(0.0, 0.0, E) + float3(31.7, 0.0, 0.0)) -
                   SampleLatticeNoise3D(Q - float3(0.0, 0.0, E) + float3(31.7, 0.0, 0.0))) * Inv2E;
    float dX_dZ = (SampleLatticeNoise3D(Q + float3(0.0, 0.0, E) + float3(5.1, 0.0, 0.0)) -
                   SampleLatticeNoise3D(Q - float3(0.0, 0.0, E) + float3(5.1, 0.0, 0.0))) * Inv2E;
    float dZ_dX = (SampleLatticeNoise3D(Q + float3(E, 0.0, 0.0) + float3(17.3, 0.0, 0.0)) -
                   SampleLatticeNoise3D(Q - float3(E, 0.0, 0.0) + float3(17.3, 0.0, 0.0))) * Inv2E;
    float dY_dX = (SampleLatticeNoise3D(Q + float3(E, 0.0, 0.0) + float3(31.7, 0.0, 0.0)) -
                   SampleLatticeNoise3D(Q - float3(E, 0.0, 0.0) + float3(31.7, 0.0, 0.0))) * Inv2E;
    float dX_dY = (SampleLatticeNoise3D(Q + float3(0.0, E, 0.0) + float3(5.1, 0.0, 0.0)) -
                   SampleLatticeNoise3D(Q - float3(0.0, E, 0.0) + float3(5.1, 0.0, 0.0))) * Inv2E;

    return float3(dZ_dY - dY_dZ, dX_dZ - dZ_dX, dY_dX - dX_dY);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeEntry(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    if (DispatchThreadId.x >= PushParams.SlotCount)
    {
        return;
    }

    uint GlobalIdx = PushParams.SlotOffset + DispatchThreadId.x;
    ParticleSlot Slot = ParticleBuffer[GlobalIdx];
    float Dt = clamp(GlobalCfg.TimingMetrics.x * GlobalCfg.TimingMetrics.z, 1.0e-4, 0.05);
    float Elapsed = GlobalCfg.TimingMetrics.y;

    if (Slot.PositionAge.w >= Slot.VelocityLifetime.w)
    {
        return;
    }

    Slot.PositionAge.w += Dt;
    if (Slot.PositionAge.w >= Slot.VelocityLifetime.w)
    {
        ParticleBuffer[GlobalIdx] = Slot;
        return;
    }

    // Gravity (-Z Up) + buoyancy (+Z Up)
    Slot.VelocityLifetime.z += (RigCfg.FluidResponse.z - RigCfg.SpeedDragGravity.w * 9.81) * Dt;

    // Curl manifold advection when CurlMode > 0
    uint CurlMode = uint(RigCfg.ScaleAndModes.y + 0.5);
    if (CurlMode > 0u)
    {
        float3 CurlVel = SampleCurlNoise3D(Slot.PositionAge.xyz, max(0.2, RigCfg.MolecularTerms.y), Elapsed * max(0.05, RigCfg.MolecularTerms.w))
                       * max(0.5, RigCfg.MolecularTerms.z);
        Slot.VelocityLifetime.xyz = lerp(Slot.VelocityLifetime.xyz, CurlVel, clamp(Dt * 6.0, 0.0, 1.0));
    }

    // Couple SimulationSpace::ForceFields (all 19 ForceModel definitions)
    if (PushParams.ForceSourceCount > 0u)
    {
        ForceSampleOutput Force = EvaluateParticleForceContribution(
            ForceSources,
            ForceWaypoints,
            PushParams.ForceSourceCount,
            Slot.PositionAge.xyz,
            Slot.VelocityLifetime.xyz,
            RigCfg.OriginRadius.xyz,
            0.0,
            0.0);

        float Gain = clamp(max(RigCfg.FluidResponse.x, 0.8) * Dt, 0.0, 1.0);
        Slot.VelocityLifetime.xyz += Force.AdvectionVelocity * Gain + Force.LinearAcceleration * Dt;
        if (Force.DragDecayRate > 0.0)
        {
            Slot.VelocityLifetime.xyz *= exp(-Force.DragDecayRate * Dt);
        }
    }

    // Exponential aerodynamic drag
    if (RigCfg.SpeedDragGravity.z > 0.0)
    {
        Slot.VelocityLifetime.xyz *= exp(-RigCfg.SpeedDragGravity.z * Dt);
    }

    Slot.PositionAge.xyz += Slot.VelocityLifetime.xyz * Dt;

    // Floor bounce (+Z Up)
    float LifeNorm = clamp(Slot.PositionAge.w / max(1.0e-4, Slot.VelocityLifetime.w), 0.0, 1.0);
    float CurSize  = lerp(RigCfg.LifetimeSize.z, RigCfg.LifetimeSize.w, LifeNorm);
    Slot.Attributes.x = CurSize;

    float FloorZ = 0.5 * CurSize;
    if (Slot.PositionAge.z < FloorZ)
    {
        Slot.PositionAge.z = FloorZ;
        if (Slot.VelocityLifetime.z < 0.0)
        {
            Slot.VelocityLifetime.z = -Slot.VelocityLifetime.z * RigCfg.FluidResponse.y;
            Slot.VelocityLifetime.xy *= 0.86;
        }
    }

    // Interactive chrome collider ball
    if (GlobalCfg.ColliderBallPose.w > 0.01)
    {
        float3 Diff   = Slot.PositionAge.xyz - GlobalCfg.ColliderBallPose.xyz;
        float  MinSep = GlobalCfg.ColliderBallPose.w + 0.5 * CurSize;
        float  D2     = dot(Diff, Diff);
        if (D2 < MinSep * MinSep)
        {
            float  D   = sqrt(max(1.0e-6, D2));
            float3 Nrm = Diff / D;
            Slot.PositionAge.xyz = GlobalCfg.ColliderBallPose.xyz + Nrm * MinSep;
            float3 RelV = Slot.VelocityLifetime.xyz - GlobalCfg.ColliderBallVelocity.xyz;
            float  Vn   = dot(RelV, Nrm);
            if (Vn < 0.0)
            {
                float Imp = (1.0 + max(0.25, RigCfg.FluidResponse.y)) * (-Vn) * max(0.5, GlobalCfg.ColliderBallVelocity.w);
                Slot.VelocityLifetime.xyz += Nrm * Imp + GlobalCfg.ColliderBallVelocity.xyz * 0.45;
            }
        }
    }

    // Scalar channel extraction & ColourRampSampling LUT lookup
    uint Channel = uint(RigCfg.ColourRangeMeta.y + 0.5);
    float Scalar = LifeNorm;
    if (Channel == 3u)
    {
        Scalar = length(Slot.VelocityLifetime.xyz);
    }
    else if (Channel == 4u)
    {
        Scalar = Slot.VelocityLifetime.z;
    }
    else if (Channel == 7u)
    {
        Scalar = length(Slot.PositionAge.xyz - RigCfg.OriginRadius.xyz);
    }
    else if (Channel == 8u)
    {
        Scalar = Slot.PositionAge.z;
    }
    else if (Channel == 9u)
    {
        Scalar = SampleLatticeNoise3D(Slot.PositionAge.xyz * 0.85);
    }
    else if (Channel == 10u)
    {
        Scalar = Slot.Attributes.w;
    }

    Slot.ColourRgba = EvaluateParticleRampColour(Scalar, RigCfg, ColourLut);
    ParticleBuffer[GlobalIdx] = Slot;
}
