/*============================================================================================================================================
                                                    PARTICLEPYROVOLLEYCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Vulkan 1.3 Compute solver for multi-tier Hanabi fireworks rocket ascent, atomic burst detonation, Ring/Heart/Willow stars & wake drag

#include "Engine/Shaders/Particles/ParticleChannelLayouts.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                                    DESCRIPTOR BINDINGS
//------------------------------------------------------------------------------------------------------------------------

[[vk::binding(0, 0)]]  ConstantBuffer<GpuDomainUniforms>      DomainParams;
[[vk::binding(1, 0)]]  RWStructuredBuffer<GpuKinematicsSlot>  KinematicsBuffer;
[[vk::binding(2, 0)]]  RWStructuredBuffer<GpuLifespanSlot>    LifespanBuffer;
[[vk::binding(3, 0)]]  RWStructuredBuffer<GpuAppearanceSlot>  AppearanceBuffer;
[[vk::binding(6, 0)]]  RWStructuredBuffer<GpuPyroVolleySlot>  PyroVolleyBuffer;
[[vk::binding(15, 0)]] RWStructuredBuffer<uint>               PyroAllocCounterBuffer;

//------------------------------------------------------------------------------------------------------------------------
//                                                      PALETTE LOOKUP
//------------------------------------------------------------------------------------------------------------------------

float3 SamplePyroBurstHue(uint PatternCode)
{
    uint Code = PatternCode % 6u;
    if (Code == 0u) { return float3(1.00, 0.84, 0.34); }
    if (Code == 1u) { return float3(0.32, 0.88, 1.00); }
    if (Code == 2u) { return float3(1.00, 0.34, 0.62); }
    if (Code == 3u) { return float3(0.42, 1.00, 0.56); }
    if (Code == 4u) { return float3(1.00, 0.60, 0.24); }
    return float3(0.96, 0.92, 1.00);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  COMPUTE ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(64, 1, 1)]
void EvaluatePyroVolleyMain(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint Slot        = DispatchThreadId.x;
    uint ActiveCount = DomainParams.DomainControl.x;
    uint Capacity    = DomainParams.DomainControl.y;
    if (Slot >= ActiveCount)
    {
        return;
    }

    float DeltaTimeSec = DomainParams.TimingCounts.x;
    float BurstSpeedMs = max(1.0, DomainParams.AuxVectorScalarB.w);

    GpuLifespanSlot   Life = LifespanBuffer[Slot];
    if ((Life.LivenessMask & 1u) == 0u)
    {
        return;
    }

    GpuPyroVolleySlot Pyro = PyroVolleyBuffer[Slot];
    GpuKinematicsSlot Kin  = KinematicsBuffer[Slot];

    // Tier 1: Primary Hanabi stars experience heightened atmospheric drag and smooth squared opacity fade
    if (Pyro.TierIndex == 1u)
    {
        float StarDrag = exp(-1.65 * DeltaTimeSec);
        Kin.VelocityDrag.xyz *= StarDrag;
        KinematicsBuffer[Slot] = Kin;

        GpuAppearanceSlot Appear = AppearanceBuffer[Slot];
        if (Life.MaxLifeSeconds > 1.0e-4)
        {
            float Fade = 1.0 - saturate(Life.AgeSeconds / Life.MaxLifeSeconds);
            Appear.ColorRgba.a     = Fade * Fade;
            Appear.VisualMetrics.w = saturate(Life.AgeSeconds / Life.MaxLifeSeconds);
        }
        AppearanceBuffer[Slot] = Appear;
        return;
    }

    // Tier 0: Ascent rocket reaches scheduled apex detonation -> atomically allocate star burst slots
    if (Pyro.TierIndex == 0u && Life.AgeSeconds >= Pyro.DetonationTimeSec)
    {
        Pyro.TierIndex     = 2u;
        Life.AgeSeconds    = Life.MaxLifeSeconds + 1.0;
        Life.LivenessMask  = 0u;
        PyroVolleyBuffer[Slot] = Pyro;
        LifespanBuffer[Slot]   = Life;

        uint RequestStars = max(12u, min(128u, Pyro.SubBurstCount));
        uint AllocBegin   = 0u;
        InterlockedAdd(PyroAllocCounterBuffer[0], RequestStars, AllocBegin);

        float3 BurstPos = Kin.PositionInvMass.xyz;
        float3 Hue      = SamplePyroBurstHue(Pyro.PatternCode);
        uint   Seed     = Life.SeedUint32 ^ (Slot * 747796405u + 2891336453u);

        for (uint StarIdx = 0u; StarIdx < RequestStars; ++StarIdx)
        {
            uint TargetSlot = AllocBegin + StarIdx;
            if (TargetSlot >= Capacity)
            {
                break;
            }

            float Frac = float(StarIdx) / float(RequestStars);
            float Ang  = Frac * 6.28318530718;
            float3 StarVel = float3(0.0, 0.0, 0.0);

            if (Pyro.PatternCode == 2u)
            {
                // Ring burst
                StarVel = float3(cos(Ang) * BurstSpeedMs, sin(Ang) * BurstSpeedMs * 0.25, sin(Ang) * BurstSpeedMs);
            }
            else if (Pyro.PatternCode == 3u)
            {
                // Parametric Heart
                float SinT = sin(Ang);
                float Hx   = 16.0 * SinT * SinT * SinT;
                float Hz   = 13.0 * cos(Ang) - 5.0 * cos(2.0 * Ang) - 2.0 * cos(3.0 * Ang) - cos(4.0 * Ang);
                StarVel    = float3(Hx * 0.28, ParticleNextRange(Seed, -0.3, 0.3), Hz * 0.28);
            }
            else
            {
                // Spherical chrysanthemum / willow shell
                float CosPol = ParticleNextRange(Seed, -1.0, 1.0);
                float SinPol = sqrt(max(0.0, 1.0 - CosPol * CosPol));
                float Phi    = ParticleNextRange(Seed, 0.0, 6.28318530718);
                float Spd    = BurstSpeedMs * ParticleNextRange(Seed, 0.75, 1.15);
                StarVel      = float3(SinPol * cos(Phi), SinPol * sin(Phi), CosPol) * Spd;
            }

            GpuKinematicsSlot StarKin;
            StarKin.PositionInvMass = float4(BurstPos, 1.0);
            StarKin.VelocityDrag    = float4(StarVel, 1.0);
            KinematicsBuffer[TargetSlot] = StarKin;

            GpuLifespanSlot StarLife;
            StarLife.AgeSeconds     = 0.0;
            StarLife.MaxLifeSeconds = ParticleNextRange(Seed, 1.4, 2.3);
            StarLife.SeedUint32     = ParticleXorshift32(Seed);
            StarLife.LivenessMask   = 1u;
            LifespanBuffer[TargetSlot] = StarLife;

            GpuPyroVolleySlot StarPyro;
            StarPyro.TierIndex         = 1u;
            StarPyro.DetonationTimeSec = 100.0;
            StarPyro.SubBurstCount     = 0u;
            StarPyro.PatternCode       = Pyro.PatternCode;
            StarPyro.TargetTrailGain   = float4(0.0, 0.0, 0.0, 1.0);
            PyroVolleyBuffer[TargetSlot] = StarPyro;

            GpuAppearanceSlot StarAppear;
            StarAppear.ColorRgba     = float4(Hue, 1.0);
            StarAppear.VisualMetrics = float4(0.065, 0.0, 0.0, 0.0);
            AppearanceBuffer[TargetSlot] = StarAppear;
        }
    }
}
