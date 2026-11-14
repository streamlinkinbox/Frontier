/*============================================================================================================================================
                                                    PARTICLEREDUCTIONCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Vulkan 1.3 Compute shader reducing active particles into the exact 32-byte host-visible ParticleReadbackSummary staging buffer

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
StructuredBuffer<GpuKinematicsSlot> KinematicsBuffer;

[[vk::binding(2, 0)]]
StructuredBuffer<GpuLifespanSlot> LifespanBuffer;

[[vk::binding(3, 0)]]
RWStructuredBuffer<GpuReductionScratch> ScratchBuffer;

[[vk::binding(4, 0)]]
RWStructuredBuffer<GpuReadbackSummary> ReadbackSummaryBuffer;

//------------------------------------------------------------------------------------------------------------------------
//                                                   WORKGROUP SHARED MEMORY
//------------------------------------------------------------------------------------------------------------------------

groupshared uint  SharedAlive[64];
groupshared float SharedPeakSpeed[64];
groupshared float SharedSumX[64];
groupshared float SharedSumY[64];
groupshared float SharedSumZ[64];

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENTRY POINTS
//------------------------------------------------------------------------------------------------------------------------

// SubMode 0: Parallel workgroup reduction into ScratchBuffer
[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeParticleReductionAccumulate(
    uint3 DispatchThreadID : SV_DispatchThreadID,
    uint3 GroupThreadID    : SV_GroupThreadID)
{
    uint Lane = GroupThreadID.x;
    uint Slot = DispatchThreadID.x;

    uint  LocalAlive = 0u;
    float LocalSpeed = 0.0;
    float LocalPosX  = 0.0;
    float LocalPosY  = 0.0;
    float LocalPosZ  = 0.0;

    if (Slot < Uniforms.ActiveCount && Slot < Uniforms.Capacity)
    {
        GpuLifespanSlot Life = LifespanBuffer[Slot];
        if ((Life.LivenessMask & 1u) != 0u)
        {
            GpuKinematicsSlot Kin = KinematicsBuffer[Slot];
            LocalAlive = 1u;
            LocalSpeed = length(Kin.VelocityDrag.xyz);
            LocalPosX  = Kin.PositionInvMass.x;
            LocalPosY  = Kin.PositionInvMass.y;
            LocalPosZ  = Kin.PositionInvMass.z;
        }
    }

    SharedAlive[Lane]     = LocalAlive;
    SharedPeakSpeed[Lane] = LocalSpeed;
    SharedSumX[Lane]      = LocalPosX;
    SharedSumY[Lane]      = LocalPosY;
    SharedSumZ[Lane]      = LocalPosZ;
    GroupMemoryBarrierWithGroupSync();

    for (uint Stride = 32u; Stride > 0u; Stride >>= 1u)
    {
        if (Lane < Stride)
        {
            SharedAlive[Lane]     += SharedAlive[Lane + Stride];
            SharedPeakSpeed[Lane]  = max(SharedPeakSpeed[Lane], SharedPeakSpeed[Lane + Stride]);
            SharedSumX[Lane]      += SharedSumX[Lane + Stride];
            SharedSumY[Lane]      += SharedSumY[Lane + Stride];
            SharedSumZ[Lane]      += SharedSumZ[Lane + Stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (Lane == 0u && SharedAlive[0] > 0u)
    {
        uint PrevVal;
        InterlockedAdd(ScratchBuffer[0].AliveSum, SharedAlive[0], PrevVal);
        InterlockedMax(ScratchBuffer[0].PeakSpeedOrdered, ParticleFloatToOrderedUint(SharedPeakSpeed[0]), PrevVal);

        int FixedX = int(clamp(SharedSumX[0] * 1000.0, -2.0e9, 2.0e9));
        int FixedY = int(clamp(SharedSumY[0] * 1000.0, -2.0e9, 2.0e9));
        int FixedZ = int(clamp(SharedSumZ[0] * 1000.0, -2.0e9, 2.0e9));
        int PrevInt;
        InterlockedAdd(ScratchBuffer[0].SumPosXFixed, FixedX, PrevInt);
        InterlockedAdd(ScratchBuffer[0].SumPosYFixed, FixedY, PrevInt);
        InterlockedAdd(ScratchBuffer[0].SumPosZFixed, FixedZ, PrevInt);
    }
}

// SubMode 1: Compute bounding radius around reduced centroid & finalize exact 32-byte ParticleReadbackSummary
[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeParticleReductionBounds(
    uint3 DispatchThreadID : SV_DispatchThreadID,
    uint3 GroupThreadID    : SV_GroupThreadID)
{
    uint Lane  = GroupThreadID.x;
    uint Slot  = DispatchThreadID.x;
    uint Alive = ScratchBuffer[0].AliveSum;

    float InvAlive = (Alive > 0u) ? (1.0 / float(Alive)) : 0.0;
    float3 Center  = float3(
        float(ScratchBuffer[0].SumPosXFixed) * 0.001 * InvAlive,
        float(ScratchBuffer[0].SumPosYFixed) * 0.001 * InvAlive,
        float(ScratchBuffer[0].SumPosZFixed) * 0.001 * InvAlive);

    float LocalDistSq = 0.0;
    if (Alive > 0u && Slot < Uniforms.ActiveCount && Slot < Uniforms.Capacity)
    {
        if ((LifespanBuffer[Slot].LivenessMask & 1u) != 0u)
        {
            float3 Diff = KinematicsBuffer[Slot].PositionInvMass.xyz - Center;
            LocalDistSq = dot(Diff, Diff);
        }
    }

    SharedPeakSpeed[Lane] = LocalDistSq;
    GroupMemoryBarrierWithGroupSync();

    for (uint Stride = 32u; Stride > 0u; Stride >>= 1u)
    {
        if (Lane < Stride)
        {
            SharedPeakSpeed[Lane] = max(SharedPeakSpeed[Lane], SharedPeakSpeed[Lane + Stride]);
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (Lane == 0u && SharedPeakSpeed[0] > 0.0)
    {
        uint PrevVal;
        InterlockedMax(ScratchBuffer[0].MaxDistSqOrdered, ParticleFloatToOrderedUint(SharedPeakSpeed[0]), PrevVal);
    }
}

[shader("compute")]
[numthreads(1, 1, 1)]
void ComputeParticleReductionFinalize(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    if (DispatchThreadID.x > 0u)
    {
        return;
    }

    uint Alive = ScratchBuffer[0].AliveSum;
    GpuReadbackSummary Out;
    Out.AliveCount      = Alive;
    Out.PeakSpeedMs     = (Alive > 0u) ? asfloat(ScratchBuffer[0].PeakSpeedOrdered) : 0.0;
    Out.TickDurationUs  = 0.0;
    Out.RevisionCounter = Uniforms.ColliderFlags.w;

    if (Alive > 0u)
    {
        float InvAlive = 1.0 / float(Alive);
        Out.BoundsCenterX      = float(ScratchBuffer[0].SumPosXFixed) * 0.001 * InvAlive;
        Out.BoundsCenterY      = float(ScratchBuffer[0].SumPosYFixed) * 0.001 * InvAlive;
        Out.BoundsCenterZ      = float(ScratchBuffer[0].SumPosZFixed) * 0.001 * InvAlive;
        Out.BoundsRadiusMeters = sqrt(max(0.0, asfloat(ScratchBuffer[0].MaxDistSqOrdered)));
    }
    else
    {
        Out.BoundsCenterX      = 0.0;
        Out.BoundsCenterY      = 0.0;
        Out.BoundsCenterZ      = 0.0;
        Out.BoundsRadiusMeters = 0.0;
    }

    ReadbackSummaryBuffer[0] = Out;
}
