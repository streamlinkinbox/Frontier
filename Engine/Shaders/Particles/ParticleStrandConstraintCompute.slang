/*============================================================================================================================================
                                                  PARTICLESTRANDCONSTRAINTCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Vulkan 1.3 Compute shader enforcing parallel XPBD/Verlet distance, cloth shear, pin anchor & sphere push-out constraints

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
RWStructuredBuffer<GpuStrandAnchorSlot> StrandAnchorBuffer;

[[vk::binding(3, 0)]]
StructuredBuffer<GpuStrandLinkSlot> StrandLinkBuffer;

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENTRY POINTS
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeStrandConstraint(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint Slot = DispatchThreadID.x;
    if (Slot >= Uniforms.ActiveCount || Slot >= Uniforms.Capacity)
    {
        return;
    }

    uint SubMode = Uniforms.ColliderFlags.z;
    GpuKinematicsSlot   Kin    = KinematicsBuffer[Slot];
    GpuStrandAnchorSlot Anchor = StrandAnchorBuffer[Slot];
    GpuStrandLinkSlot   Link   = StrandLinkBuffer[Slot];

    float3 Pos     = Kin.PositionInvMass.xyz;
    float  InvMass = Kin.PositionInvMass.w;

    if (SubMode == StrandModeVerletSetup)
    {
        // Snapshot position prior to advection/relaxation so Verlet velocity is exact
        Anchor.PrevPosRestLen.xyz = Pos;
        StrandAnchorBuffer[Slot]  = Anchor;
        return;
    }

    if (SubMode == StrandModeRelax)
    {
        float PinWeight = saturate(Anchor.AnchorPinWeight.w);
        if (PinWeight >= 0.999 || InvMass <= 0.0)
        {
            Kin.PositionInvMass.xyz = Anchor.AnchorPinWeight.xyz;
            Kin.VelocityDrag.xyz    = float3(0.0, 0.0, 0.0);
            KinematicsBuffer[Slot]  = Kin;
            return;
        }

        float Stiffness      = saturate(Uniforms.ColorDriverNorm.w);
        float CrossStiffness = saturate(asfloat(Uniforms.ColorDriverMode.w));
        float RestLength     = max(1.0e-3, Anchor.PrevPosRestLen.w);
        float3 Correction    = float3(0.0, 0.0, 0.0);

        // Upstream longitudinal strand distance constraint
        if (Link.LinkPrevSlot < Uniforms.ActiveCount)
        {
            float3 PrevPos = KinematicsBuffer[Link.LinkPrevSlot].PositionInvMass.xyz;
            float  PrevInv = KinematicsBuffer[Link.LinkPrevSlot].PositionInvMass.w;
            float3 Delta   = Pos - PrevPos;
            float  Dist    = length(Delta);
            if (Dist > 1.0e-5)
            {
                float Share = (PrevInv <= 0.0) ? 1.0 : 0.5;
                float Error = (Dist - RestLength) / Dist;
                Correction -= Delta * (Error * Share * Stiffness);
            }
        }

        // Lateral cloth / web structural cross-link constraint
        if (Link.LinkCrossSlot < Uniforms.ActiveCount && CrossStiffness > 1.0e-4)
        {
            float3 CrossPos = KinematicsBuffer[Link.LinkCrossSlot].PositionInvMass.xyz;
            float  CrossInv = KinematicsBuffer[Link.LinkCrossSlot].PositionInvMass.w;
            float3 Delta    = Pos - CrossPos;
            float  Dist     = length(Delta);
            if (Dist > 1.0e-5)
            {
                float Share = (CrossInv <= 0.0) ? 1.0 : 0.5;
                float Error = (Dist - RestLength) / Dist;
                Correction -= Delta * (Error * Share * CrossStiffness);
            }
        }

        Pos += Correction;

        // Soft elastic anchor pull for partially pinned foliage/kelp nodes
        if (PinWeight > 1.0e-4)
        {
            Pos = lerp(Pos, Anchor.AnchorPinWeight.xyz, PinWeight);
        }

        // Dynamic sphere push-out inside XPBD relaxation
        if (Uniforms.ColliderFlags.y != 0u)
        {
            float3 Center = Uniforms.SphereCenterRadius.xyz;
            float  Radius = max(0.05, Uniforms.SphereCenterRadius.w);
            float3 Diff   = Pos - Center;
            float  DistSq = dot(Diff, Diff);
            if (DistSq < Radius * Radius)
            {
                float  Dist   = sqrt(max(1.0e-8, DistSq));
                float3 Normal = (Dist > 1.0e-4) ? (Diff / Dist) : float3(0.0, 0.0, 1.0);
                Pos           = Center + Normal * (Radius + 1.0e-3);
            }
        }

        Kin.PositionInvMass.xyz = Pos;
        KinematicsBuffer[Slot]  = Kin;
        return;
    }

    if (SubMode == StrandModeDeriveVel)
    {
        float PinWeight = saturate(Anchor.AnchorPinWeight.w);
        if (PinWeight >= 0.999 || InvMass <= 0.0)
        {
            Kin.PositionInvMass.xyz = Anchor.AnchorPinWeight.xyz;
            Kin.VelocityDrag.xyz    = float3(0.0, 0.0, 0.0);
        }
        else
        {
            float SafeDt = max(1.0e-4, Uniforms.DeltaTimeSec);
            Kin.VelocityDrag.xyz = (Pos - Anchor.PrevPosRestLen.xyz) / SafeDt;
        }
        KinematicsBuffer[Slot] = Kin;
    }
}
