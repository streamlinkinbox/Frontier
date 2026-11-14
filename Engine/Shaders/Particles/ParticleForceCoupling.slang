/*============================================================================================================================================
                                                      PARTICLEFORCECOUPLING.SLANG
============================================================================================================================================*/
// 🧩 Pure Slang particle force-coupling bridge importing ForceFieldEvaluationShared.slang (19 ForceModel laws + ambient wind + dipole)

#pragma once

import "../Compute/ForceFieldEvaluationShared";

//------------------------------------------------------------------------------------------------------------------------
//                                                  PARTICLE FORCE COUPLING
//------------------------------------------------------------------------------------------------------------------------

ForceSampleOutput EvaluateParticleForceContribution(
    StructuredBuffer<ForceSourceShaderEntry> SourceBuffer,
    StructuredBuffer<float4>                 WaypointBuffer,
    uint                                     SourceCount,
    float3                                   WorldPoint,
    float3                                   ParticleVelocity,
    float3                                   RigOrigin,
    float                                    LocalMagneticStrength,
    float                                    LocalMagneticRadius)
{
    ForceSampleOutput Combined;
    Combined.AdvectionVelocity  = float3(0.0, 0.0, 0.0);
    Combined.LinearAcceleration = float3(0.0, 0.0, 0.0);
    Combined.AngularTorque      = float3(0.0, 0.0, 0.0);
    Combined.DragDecayRate      = 0.0;

    for (uint Idx = 0u; Idx < SourceCount; ++Idx)
    {
        ForceSampleOutput Sample = VfEvaluateSingleSource(SourceBuffer[Idx], WaypointBuffer, WorldPoint, ParticleVelocity);
        Combined.AdvectionVelocity  += Sample.AdvectionVelocity;
        Combined.LinearAcceleration += Sample.LinearAcceleration;
        Combined.AngularTorque      += Sample.AngularTorque;
        Combined.DragDecayRate      += Sample.DragDecayRate;
    }

    if (LocalMagneticStrength > 0.001)
    {
        float3 R  = WorldPoint - RigOrigin;
        float  R2 = dot(R, R);
        float  Dist = sqrt(max(0.09, R2));
        if (Dist < LocalMagneticRadius)
        {
            float3 U = R / Dist;
            float3 B = float3(3.0 * U.z * U.x, 3.0 * U.z * U.y, 3.0 * U.z * U.z - 1.0);
            float  BScale = LocalMagneticStrength / (1.0 + R2 * 0.25);
            Combined.AdvectionVelocity += normalize(B) * BScale;
        }
    }

    return Combined;
}
