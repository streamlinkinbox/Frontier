/*============================================================================================================================================
                                                     PARTICLESPATIALGRIDCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Pure Slang compute pass for Boids flocking (cohesion/alignment/separation/predator evasion) & Lennard-Jones molecular kinetics

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
[[vk::binding(3, 0)]] RWStructuredBuffer<ParticleSlot>      ParticleBuffer;

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeEntry(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    if (DispatchThreadId.x >= PushParams.SlotCount)
    {
        return;
    }

    uint SimFamily = uint(RigCfg.SimulationStep.w + 0.5);
    if (SimFamily != 3u && SimFamily != 4u && SimFamily != 6u)
    {
        return;
    }

    uint GlobalIdx = PushParams.SlotOffset + DispatchThreadId.x;
    ParticleSlot Self = ParticleBuffer[GlobalIdx];
    if (Self.PositionAge.w >= Self.VelocityLifetime.w)
    {
        return;
    }

    float Dt = clamp(GlobalCfg.TimingMetrics.x * GlobalCfg.TimingMetrics.z, 1.0e-4, 0.05);
    uint SampleStep = max(1u, PushParams.SlotCount / 48u);

    if (SimFamily == 6u) // BoidsFlock
    {
        float3 Coh = float3(0.0, 0.0, 0.0);
        float3 Ali = float3(0.0, 0.0, 0.0);
        float3 Sep = float3(0.0, 0.0, 0.0);
        uint Neigh = 0u;
        float PercR = max(0.6, RigCfg.ReactionDamping.x);
        float SepR  = PercR * 0.45;

        for (uint J = DispatchThreadId.x % SampleStep; J < PushParams.SlotCount; J += SampleStep)
        {
            if (J == DispatchThreadId.x)
            {
                continue;
            }
            ParticleSlot Other = ParticleBuffer[PushParams.SlotOffset + J];
            if (Other.PositionAge.w >= Other.VelocityLifetime.w)
            {
                continue;
            }
            float3 D  = Other.PositionAge.xyz - Self.PositionAge.xyz;
            float  D2 = dot(D, D);
            if (D2 > PercR * PercR || D2 < 1.0e-7)
            {
                continue;
            }
            Coh += Other.PositionAge.xyz;
            Ali += Other.VelocityLifetime.xyz;
            ++Neigh;
            if (D2 < SepR * SepR)
            {
                Sep -= D / sqrt(D2);
            }
        }

        float3 Accel = (RigCfg.OriginRadius.xyz - Self.PositionAge.xyz) * 0.65;
        if (Neigh > 0u)
        {
            float InvN = 1.0 / float(Neigh);
            Accel += (Coh * InvN - Self.PositionAge.xyz) * 1.15
                   + (Ali * InvN - Self.VelocityLifetime.xyz) * 1.65
                   + Sep * 2.85;
        }
        float BallR = GlobalCfg.ColliderBallPose.w;
        if (BallR > 0.01)
        {
            float3 Avoid = Self.PositionAge.xyz - GlobalCfg.ColliderBallPose.xyz;
            float  Dist  = length(Avoid);
            float  EvadeR = BallR + 0.85;
            if (Dist < EvadeR && Dist > 1.0e-4)
            {
                Accel += (Avoid / Dist) * ((EvadeR - Dist) * 14.0);
            }
        }
        Self.VelocityLifetime.xyz += Accel * Dt;
        float MaxSpd = max(1.0, RigCfg.SpeedDragGravity.y);
        float Spd = length(Self.VelocityLifetime.xyz);
        if (Spd > MaxSpd)
        {
            Self.VelocityLifetime.xyz *= MaxSpd / Spd;
        }
    }
    else // MolecularAtoms / ChemicalReaction (Lennard-Jones 12-6)
    {
        float Sigma = max(0.06, RigCfg.MolecularTerms.z);
        float Eps   = max(0.02, RigCfg.MolecularTerms.y);
        float CutSq = (Sigma * 3.0) * (Sigma * 3.0);
        float3 Force = float3(0.0, 0.0, 0.0);

        for (uint J = DispatchThreadId.x % SampleStep; J < PushParams.SlotCount; J += SampleStep)
        {
            if (J == DispatchThreadId.x)
            {
                continue;
            }
            ParticleSlot Other = ParticleBuffer[PushParams.SlotOffset + J];
            if (Other.PositionAge.w >= Other.VelocityLifetime.w)
            {
                continue;
            }
            float3 D  = Self.PositionAge.xyz - Other.PositionAge.xyz;
            float  R2 = max(0.02 * Sigma * Sigma, dot(D, D));
            if (R2 <= CutSq)
            {
                float InvR2 = (Sigma * Sigma) / R2;
                float InvR6 = InvR2 * InvR2 * InvR2;
                float Scal  = clamp(24.0 * Eps * InvR2 * (2.0 * InvR6 * InvR6 - InvR6), -25.0, 25.0);
                Force += D * Scal;
            }
        }
        Self.VelocityLifetime.xyz += Force * Dt;
    }

    Self.PositionAge.xyz += Self.VelocityLifetime.xyz * Dt;
    Self.PositionAge.w   += Dt;
    ParticleBuffer[GlobalIdx] = Self;
}
