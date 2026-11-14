/*============================================================================================================================================
                                                    PARTICLEFRACTURECOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Vulkan 1.3 Compute solver for radial glacial/seismic crack front propagation, shard detachment & tumbling 3D quaternion spin

#include "Engine/Shaders/Particles/ParticleChannelLayouts.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                                    DESCRIPTOR BINDINGS
//------------------------------------------------------------------------------------------------------------------------

[[vk::binding(0, 0)]] ConstantBuffer<GpuDomainUniforms>         DomainParams;
[[vk::binding(1, 0)]] RWStructuredBuffer<GpuKinematicsSlot>     KinematicsBuffer;
[[vk::binding(3, 0)]] RWStructuredBuffer<GpuAppearanceSlot>     AppearanceBuffer;
[[vk::binding(4, 0)]] RWStructuredBuffer<GpuOrientationSlot>    OrientationBuffer;
[[vk::binding(8, 0)]] RWStructuredBuffer<GpuFractureStressSlot> FractureStressBuffer;

//------------------------------------------------------------------------------------------------------------------------
//                                                  COMPUTE ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(64, 1, 1)]
void EvaluateFractureMain(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint Slot        = DispatchThreadId.x;
    uint ActiveCount = DomainParams.DomainControl.x;
    if (Slot >= ActiveCount)
    {
        return;
    }

    float  DeltaTimeSec     = DomainParams.TimingCounts.x;
    float  TimeSeconds      = DomainParams.TimingCounts.y;
    float3 SheetOrigin      = DomainParams.OriginScalarA.xyz;
    float  BranchDensity    = max(0.2, DomainParams.ScalarPackC.x);
    float  IncisionDepthM   = max(0.05, DomainParams.ScalarPackC.y);

    GpuFractureStressSlot Frac   = FractureStressBuffer[Slot];
    GpuKinematicsSlot     Kin    = KinematicsBuffer[Slot];
    GpuOrientationSlot    Orient = OrientationBuffer[Slot];

    // Detached shards integrate their 3D tumbling quaternion continuously while falling ballistically
    if (Frac.DetachedBit != 0u)
    {
        Orient.QuaternionXyzw = ParticleIntegrateQuat(Orient.QuaternionXyzw, Orient.AngVelInertia.xyz, DeltaTimeSec);
        OrientationBuffer[Slot] = Orient;
        return;
    }

    float WaveFrontR = TimeSeconds * (1.15 * BranchDensity);
    float2 RadialXy  = Frac.CellOriginCrackDepth.xy - SheetOrigin.xy;
    float  RadialDist = length(RadialXy);

    if (RadialDist <= WaveFrontR)
    {
        Frac.AccumulatedStress = min(1.0, Frac.AccumulatedStress + DeltaTimeSec * (1.2 * BranchDensity));
        Frac.CellOriginCrackDepth.w = Frac.AccumulatedStress * IncisionDepthM;
        Kin.PositionInvMass.z = Frac.CellOriginCrackDepth.z - Frac.CellOriginCrackDepth.w * 0.25;

        if (Frac.AccumulatedStress >= 0.92)
        {
            Frac.DetachedBit      = 1u;
            Kin.PositionInvMass.w = 1.0; // Unpin into ballistic flight

            float InvR = 1.0 / max(0.15, RadialDist);
            Kin.VelocityDrag.x = RadialXy.x * InvR * 1.35;
            Kin.VelocityDrag.y = RadialXy.y * InvR * 1.35;
            Kin.VelocityDrag.z = 1.6 + 0.8 * IncisionDepthM;

            // Deterministic per-branch rotational tumble impulse
            float Phase = float(Slot) * 1.6180339 + float(Frac.BranchId);
            Orient.AngVelInertia.xyz = float3(
                sin(Phase) * 4.2,
                cos(Phase * 1.37) * 4.2,
                sin(Phase * 0.73) * 3.1);
            OrientationBuffer[Slot] = Orient;
        }

        KinematicsBuffer[Slot]     = Kin;
        FractureStressBuffer[Slot] = Frac;

        GpuAppearanceSlot Appear = AppearanceBuffer[Slot];
        Appear.VisualMetrics.w = Frac.AccumulatedStress;
        AppearanceBuffer[Slot] = Appear;
    }
}
