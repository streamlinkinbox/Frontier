/*============================================================================================================================================
                                                    PARTICLEFERROFLUIDCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Vulkan 1.3 Compute solver for Rosensweig magnetic ferrofluid cusp instability, dipole tilt & coupled ForceFields amplification

#include "Engine/Shaders/Particles/ParticleChannelLayouts.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                                    DESCRIPTOR BINDINGS
//------------------------------------------------------------------------------------------------------------------------

[[vk::binding(0, 0)]]  ConstantBuffer<GpuDomainUniforms>          DomainParams;
[[vk::binding(1, 0)]]  RWStructuredBuffer<GpuKinematicsSlot>      KinematicsBuffer;
[[vk::binding(3, 0)]]  RWStructuredBuffer<GpuAppearanceSlot>      AppearanceBuffer;
[[vk::binding(4, 0)]]  RWStructuredBuffer<GpuOrientationSlot>     OrientationBuffer;
[[vk::binding(5, 0)]]  RWStructuredBuffer<GpuMagneticCuspSlot>    MagneticCuspBuffer;
[[vk::binding(10, 0)]] StructuredBuffer<ForceSourceShaderEntry>   ForceSourcesBuffer;
[[vk::binding(11, 0)]] StructuredBuffer<float4>                   ForceWaypointsBuffer;

//------------------------------------------------------------------------------------------------------------------------
//                                                  COMPUTE ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(64, 1, 1)]
void EvaluateFerrofluidMain(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint Slot = DispatchThreadId.x;
    uint ActiveCount = DomainParams.DomainControl.x;
    if (Slot >= ActiveCount)
    {
        return;
    }

    float DeltaTimeSec      = DomainParams.TimingCounts.x;
    float TimeSeconds       = DomainParams.TimingCounts.y;
    float MaxCuspHeightM    = max(0.1, DomainParams.OriginScalarA.w);
    float CapillaryDamping  = max(0.5, DomainParams.ScalarPackC.x);
    uint  SourceCount       = DomainParams.DomainControl.z;
    float3 PoleMeters       = DomainParams.AuxVectorScalarB.xyz;

    GpuMagneticCuspSlot Cusp = MagneticCuspBuffer[Slot];
    GpuKinematicsSlot   Kin  = KinematicsBuffer[Slot];

    float3 AnchorPos  = Cusp.DishAnchorHeight.xyz;
    float  CuspHeight = Cusp.DishAnchorHeight.w;
    float3 SurfNormal = normalize(Cusp.NormalSuscept.xyz + float3(0.0, 0.0, 1.0e-5));
    float  Suscept    = Cusp.NormalSuscept.w;

    float3 ToPole = PoleMeters - AnchorPos;
    float  DistSq = dot(ToPole, ToPole) + 0.18;
    float  Dist   = sqrt(DistSq);

    float  LatSq     = ToPole.x * ToPole.x + ToPole.y * ToPole.y;
    float  FluxNorm  = max(0.0, dot(ToPole, SurfNormal) / Dist);
    float  Cluster   = exp(-LatSq / 0.58);
    float  TargetH   = MaxCuspHeightM * Suscept * (0.08 + 0.92 * Cluster * FluxNorm * FluxNorm);

    // Couple live Frontier ForceFields acceleration into ferrofluid cusp elevation
    if (SourceCount > 0u)
    {
        float3 TotalAccel = float3(0.0, 0.0, 0.0);
        for (uint SrcIdx = 0u; SrcIdx < SourceCount; ++SrcIdx)
        {
            ForceSampleOutput Sample = VfEvaluateSingleSource(ForceSourcesBuffer[SrcIdx], ForceWaypointsBuffer, AnchorPos, float3(0.0, 0.0, 0.0));
            TotalAccel += Sample.LinearAcceleration;
        }
        TargetH += min(0.22, length(TotalAccel) * 0.018);
    }

    TargetH = clamp(TargetH, 0.03, MaxCuspHeightM);
    float SmoothRate = 1.0 - exp(-CapillaryDamping * DeltaTimeSec);
    CuspHeight += (TargetH - CuspHeight) * SmoothRate;
    Cusp.DishAnchorHeight.w = CuspHeight;
    MagneticCuspBuffer[Slot] = Cusp;

    float  Lean      = 0.16 * (CuspHeight / MaxCuspHeightM);
    float3 PoleDir   = ToPole / max(1.0e-4, Dist);
    float3 TargetPos = AnchorPos + SurfNormal * CuspHeight + PoleDir * Lean;

    // Critically damped capillary spring drives velocity smoothly before Advect integration
    float  SpringK = 28.0;
    float  DampC   = exp(-11.5 * DeltaTimeSec);
    float3 Pos     = Kin.PositionInvMass.xyz;
    float3 Vel     = Kin.VelocityDrag.xyz;
    Vel = (Vel + (TargetPos - Pos) * (SpringK * DeltaTimeSec)) * DampC;
    Kin.VelocityDrag.xyz = Vel;
    KinematicsBuffer[Slot] = Kin;

    // Orient 3D spike tip along tilted cusp normal
    float3 TipUp    = normalize(SurfNormal + PoleDir * (0.35 * Lean));
    float3 RefAxis  = (abs(TipUp.z) < 0.92) ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 TipRight = normalize(cross(RefAxis, TipUp));
    float3 TipFwd   = normalize(cross(TipUp, TipRight));

    GpuOrientationSlot Orient = OrientationBuffer[Slot];
    Orient.QuaternionXyzw = ParticleQuatFromBasis(TipRight, TipFwd, TipUp);
    OrientationBuffer[Slot] = Orient;

    // Iridescent specular height tint
    float Spec = saturate(CuspHeight / MaxCuspHeightM);
    GpuAppearanceSlot Appear = AppearanceBuffer[Slot];
    Appear.ColorRgba.r     = 0.08 + 0.56 * Spec * Spec;
    Appear.ColorRgba.g     = 0.11 + 0.68 * Spec * Spec;
    Appear.ColorRgba.b     = 0.18 + 0.80 * Spec * Spec;
    Appear.ColorRgba.a     = 1.0;
    Appear.VisualMetrics.w = Spec;
    AppearanceBuffer[Slot] = Appear;
}
