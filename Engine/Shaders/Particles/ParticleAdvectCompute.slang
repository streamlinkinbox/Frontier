/*============================================================================================================================================
                                                      PARTICLEADVECTCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Vulkan 1.3 Compute shader advancing modular particles with live ForceFieldEvaluationShared.slang coupling, curl, colliders & curves

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Shaders/Particles/ParticleChannelShared.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                                       RESOURCES
//------------------------------------------------------------------------------------------------------------------------

[[vk::binding(0, 0)]]
ConstantBuffer<GpuParticleUniforms> Uniforms;

[[vk::binding(1, 0)]]
RWStructuredBuffer<GpuKinematicsSlot> KinematicsBuffer;

[[vk::binding(2, 0)]]
RWStructuredBuffer<GpuLifespanSlot> LifespanBuffer;

[[vk::binding(3, 0)]]
RWStructuredBuffer<GpuAppearanceSlot> AppearanceBuffer;

[[vk::binding(4, 0)]]
StructuredBuffer<ForceSourceShaderEntry> SourceBuffer;

[[vk::binding(5, 0)]]
StructuredBuffer<float4> WaypointBuffer;

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENTRY POINTS
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeParticleAdvect(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint Slot = DispatchThreadID.x;
    if (Slot >= Uniforms.ActiveCount || Slot >= Uniforms.Capacity)
    {
        return;
    }

    GpuLifespanSlot Life = LifespanBuffer[Slot];
    if ((Life.LivenessMask & 1u) == 0u)
    {
        return;
    }

    GpuKinematicsSlot Kin    = KinematicsBuffer[Slot];
    GpuAppearanceSlot Appear = AppearanceBuffer[Slot];

    float  Dt      = Uniforms.DeltaTimeSec;
    float3 Pos     = Kin.PositionInvMass.xyz;
    float  InvMass = Kin.PositionInvMass.w;
    float3 Vel     = Kin.VelocityDrag.xyz;
    float  DragMul = Kin.VelocityDrag.w;

    if (InvMass > 0.0)
    {
        // 1. Gravity, buoyancy & linear aerodynamic drag (matches GravityBuoyancySolver)
        float NetAccelZ = -ParticleGravityMs2 * Uniforms.GravityDragCurl.x + Uniforms.GravityDragCurl.y;
        Vel.z += NetAccelZ * Dt;

        float EffectiveDrag = max(0.0, Uniforms.GravityDragCurl.z * DragMul);
        if (EffectiveDrag > 0.0)
        {
            Vel *= exp(-EffectiveDrag * Dt);
        }

        // 2. Direct ForceFields coupling via shared ForceFieldEvaluationShared.slang (matches ForceFieldCouplingSolver)
        float CouplingGain = Uniforms.ForceCoupling.x;
        float AccelScale   = Uniforms.ForceCoupling.y;
        float FieldDragMul = Uniforms.ForceCoupling.z;
        if (Uniforms.SourceCount > 0u && (CouplingGain > 1.0e-5 || AccelScale > 1.0e-5))
        {
            float3 SumAdvection = float3(0.0, 0.0, 0.0);
            float3 SumAccel     = float3(0.0, 0.0, 0.0);
            float  SumDrag      = 0.0;

            for (uint SrcIdx = 0u; SrcIdx < Uniforms.SourceCount; ++SrcIdx)
            {
                ForceSampleOutput Sample = VfEvaluateSingleSource(SourceBuffer[SrcIdx], WaypointBuffer, Pos, Vel);
                SumAdvection += Sample.AdvectionVelocity;
                SumAccel     += Sample.LinearAcceleration;
                SumDrag      += Sample.DragDecayRate;
            }

            float Blend = 1.0 - exp(-max(0.0, CouplingGain) * Dt);
            Vel += (SumAdvection - Vel) * Blend;
            Vel += SumAccel * (AccelScale * InvMass * Dt);

            float FieldDrag = max(0.0, SumDrag * FieldDragMul);
            if (FieldDrag > 1.0e-5)
            {
                Vel *= exp(-FieldDrag * Dt);
            }
        }

        // 3. Divergence-free analytic curl turbulence (matches CurlTurbulenceSolver)
        float CurlAmp = Uniforms.GravityDragCurl.w;
        if (CurlAmp > 1.0e-5)
        {
            float TimePhase = Uniforms.TimeSeconds * Uniforms.CurlTimingBounds.y;
            float3 CurlVec  = ParticleEvaluateAnalyticCurl(Pos, Uniforms.CurlTimingBounds.x, TimePhase);
            Vel += CurlVec * (CurlAmp * Dt);
        }

        // 4. Semi-Implicit Euler position integration (matches SemiImplicitEulerIntegrator)
        Pos += Vel * Dt;

        // 5. Ground plane collider (matches GroundPlaneCollider)
        if (Uniforms.ColliderFlags.x != 0u)
        {
            float FloorZ = Uniforms.CurlTimingBounds.z;
            if (Pos.z < FloorZ)
            {
                Pos.z = FloorZ;
                if (Vel.z < 0.0)
                {
                    Vel.z = -Vel.z * Uniforms.CurlTimingBounds.w;
                    if (abs(Vel.z) < 0.08)
                    {
                        Vel.z = 0.0;
                    }
                }
                float Keep = saturate(1.0 - Uniforms.ForceCoupling.w);
                Vel.x *= Keep;
                Vel.y *= Keep;
            }
        }

        // 6. Dynamic sphere contact collider (matches SphereContactCollider bit-for-bit)
        if (Uniforms.ColliderFlags.y != 0u)
        {
            float3 Center = Uniforms.SphereCenterRadius.xyz;
            float  Radius = max(1.0e-4, Uniforms.SphereCenterRadius.w);
            float3 Diff   = Pos - Center;
            float  DistSq = dot(Diff, Diff);
            if (DistSq < Radius * Radius)
            {
                float  Dist    = sqrt(max(DistSq, 1.0e-10));
                float3 Normal  = (DistSq > 1.0e-10) ? (Diff / Dist) : float3(0.0, 0.0, 1.0);
                Pos            = Center + Normal * Radius;
                float3 RelVel  = Vel - Uniforms.SphereVelBounce.xyz;
                float  NormalV = dot(RelVel, Normal);
                if (NormalV < 0.0)
                {
                    float Impulse = -(1.0 + Uniforms.SphereVelBounce.w) * NormalV * Uniforms.ReleaseSpeedLife.w;
                    Vel += Normal * Impulse;
                }
            }
        }
    }

    // 7. Lifespan progression & appearance curve evaluation (matches AppearanceCurveSolver)
    float LifeRatio = 0.0;
    if (Life.MaxLifeSeconds > 1.0e-5)
    {
        LifeRatio = saturate(Life.AgeSeconds / Life.MaxLifeSeconds);
    }

    float RawCoord = LifeRatio;
    uint  Driver   = Uniforms.ColorDriverMode.x;
    if (Driver == ColorDriverSpeedMagnitude)
    {
        RawCoord = length(Vel) / max(0.1, Uniforms.ColorDriverNorm.x);
    }
    else if (Driver == ColorDriverHeightAltitude)
    {
        RawCoord = Pos.z / max(0.1, Uniforms.ColorDriverNorm.y);
    }
    else if (Driver == ColorDriverSlotHash)
    {
        RawCoord = float(Life.SeedUint32 & 0xFFFFu) * (1.0 / 65536.0);
    }

    float RampCoord = ParticleEvaluateRampWrap(RawCoord + Uniforms.ColorDriverNorm.z, Uniforms.ColorDriverMode.y);
    Appear.ColorRgba       = ParticleSampleColorRamp(Uniforms.ColorStopA, Uniforms.ColorStopC, Uniforms.ColorStopB, RampCoord);
    Appear.VisualMetrics.x = lerp(Uniforms.SizeSpinMetrics.x, Uniforms.SizeSpinMetrics.y, LifeRatio) * Uniforms.SizeSpinMetrics.z;
    Appear.VisualMetrics.y += Appear.VisualMetrics.z * Dt;
    Appear.VisualMetrics.w = RampCoord;

    Life.AgeSeconds += Dt;
    if (Life.MaxLifeSeconds > 0.0 && Life.AgeSeconds >= Life.MaxLifeSeconds)
    {
        Life.LivenessMask &= ~1u;
    }

    Kin.PositionInvMass = float4(Pos, InvMass);
    Kin.VelocityDrag    = float4(Vel, DragMul);

    KinematicsBuffer[Slot] = Kin;
    LifespanBuffer[Slot]   = Life;
    AppearanceBuffer[Slot] = Appear;
}
