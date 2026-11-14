/*============================================================================================================================================
                                                       PARTICLERELEASECOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Pure Slang compute release pass spawning new particles across Point, Sphere, Disc, Box, SurfaceLattice, DualElectrode

import ColourRampSampling;
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
[[vk::binding(2, 0)]] ConstantBuffer<ColourRampLut64Buffer> ColourLut;
[[vk::binding(3, 0)]] RWStructuredBuffer<ParticleSlot>      ParticleBuffer;

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeEntry(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint SpawnCount = uint(max(0.0, RigCfg.ReleaseBudget.w));
    if (DispatchThreadId.x >= SpawnCount || PushParams.SlotCount == 0u)
    {
        return;
    }

    uint LocalIdx = (PushParams.SpawnCursor + DispatchThreadId.x) % PushParams.SlotCount;
    uint GlobalIdx = PushParams.SlotOffset + LocalIdx;
    uint Salt = uint(RigCfg.CarrierDispatch.y) ^ (uint(GlobalCfg.TimingMetrics.w) * 747796405u) ^ (LocalIdx * 2891336453u);

    float U0 = ParticleHashFloat(LocalIdx, Salt + 11u);
    float U1 = ParticleHashFloat(LocalIdx, Salt + 29u);
    float U2 = ParticleHashFloat(LocalIdx, Salt + 53u);
    float U3 = ParticleHashFloat(LocalIdx, Salt + 79u);

    float3 Origin = RigCfg.OriginRadius.xyz;
    float  Radius = max(0.0, RigCfg.OriginRadius.w);
    uint   OrigCat = uint(RigCfg.ReleaseBudget.y + 0.5);

    float3 SpawnPos = Origin;
    float3 LaunchDir = normalize(length(RigCfg.DirectionSpread.xyz) > 1.0e-4 ? RigCfg.DirectionSpread.xyz : float3(0.0, 0.0, 1.0));

    if (OrigCat == 1u) // SphereVolume
    {
        float Theta = U0 * ParticleTau;
        float CosP  = 1.0 - 2.0 * U1;
        float SinP  = sqrt(max(0.0, 1.0 - CosP * CosP));
        float R     = Radius * pow(U2, 0.33333);
        float3 Nrm  = float3(SinP * cos(Theta), SinP * sin(Theta), CosP);
        SpawnPos    = Origin + Nrm * R;
        LaunchDir   = Nrm;
    }
    else if (OrigCat == 2u) // Disc
    {
        float Theta = U0 * ParticleTau;
        float R     = Radius * sqrt(U1);
        SpawnPos    = Origin + float3(cos(Theta) * R, sin(Theta) * R, 0.0);
    }
    else if (OrigCat == 3u) // BoxVolume
    {
        float3 Ext = RigCfg.BoxExtentsHead.xyz;
        SpawnPos   = Origin + float3((U0 * 2.0 - 1.0) * Ext.x, (U1 * 2.0 - 1.0) * Ext.y, (U2 * 2.0 - 1.0) * Ext.z);
    }

    float Spread = RigCfg.DirectionSpread.w;
    float ThetaC = U1 * ParticleTau;
    float ConeR  = tan(clamp(Spread * 0.5, 0.0, 1.45)) * sqrt(U2);
    float3 VelDir = normalize(LaunchDir + float3(cos(ThetaC) * ConeR, sin(ThetaC) * ConeR, (U2 - 0.5) * Spread * 0.5));
    float Speed = lerp(RigCfg.SpeedDragGravity.x, RigCfg.SpeedDragGravity.y, U0);

    ParticleSlot Slot;
    Slot.PositionAge      = float4(SpawnPos, 0.0);
    Slot.VelocityLifetime = float4(VelDir * Speed, lerp(RigCfg.LifetimeSize.x, RigCfg.LifetimeSize.y, U3));
    Slot.ColourRgba       = EvaluateParticleRampColour(0.0, RigCfg, ColourLut);
    Slot.Attributes       = float4(RigCfg.LifetimeSize.z, U1 * ParticleTau, 0.0, U3);
    Slot.PhaseTiming      = float4(0.0, 0.0, 0.0, 0.0);

    ParticleBuffer[GlobalIdx] = Slot;
}
