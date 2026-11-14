/*============================================================================================================================================
                                                      PARTICLERELEASECOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Vulkan 1.3 Compute shader spawning modular particle volumes (Point/Sphere/Disc/Box/Lattice/Arc) directly into SoA SSBOs

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

//------------------------------------------------------------------------------------------------------------------------
//                                                  DIRECTIONAL CONE SAMPLER
//------------------------------------------------------------------------------------------------------------------------

float3 SampleConeDirection(float3 Axis, float HalfAngleRad, inout uint RngWord)
{
    float3 Forward = VfNormalizeSafe(Axis, float3(0.0, 0.0, 1.0));
    float  Clamped = clamp(HalfAngleRad, 0.0, VfPi);
    if (Clamped <= 1.0e-5)
    {
        return Forward;
    }

    float MinCos   = cos(Clamped);
    float CosTheta = MinCos + (1.0 - MinCos) * ParticleNextUnit(RngWord);
    float SinTheta = sqrt(max(0.0, 1.0 - CosTheta * CosTheta));
    float Phi      = VfTau * ParticleNextUnit(RngWord);

    float3 RefAxis   = (abs(Forward.z) < 0.92) ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 RightAxis = VfNormalizeSafe(cross(RefAxis, Forward), float3(1.0, 0.0, 0.0));
    float3 Bitangent = cross(Forward, RightAxis);

    return Forward * CosTheta + (RightAxis * cos(Phi) + Bitangent * sin(Phi)) * SinTheta;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENTRY POINTS
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeParticleRelease(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint Offset = DispatchThreadID.x;
    if (Offset >= Uniforms.ReleaseCount)
    {
        return;
    }

    uint Slot = Uniforms.ReleaseStartSlot + Offset;
    if (Slot >= Uniforms.Capacity)
    {
        return;
    }

    uint RngWord = (Uniforms.MasterSeed ^ ((Slot + 1u) * 747796405u) ^ ((Uniforms.ColliderFlags.w + 1u) * 2891336453u)) | 1u;
    ParticleXorshift32(RngWord);

    float3 ReleasePos = Uniforms.ReleaseOriginRadius.xyz;
    float  RadiusM    = max(0.0, Uniforms.ReleaseOriginRadius.w);
    float3 BoxHalf    = Uniforms.ReleaseBoxExtents.xyz;
    uint   Shape      = Uniforms.ColorDriverMode.z;

    if (Shape == VolumeShapeSphereVolume)
    {
        float3 UnitDir = SampleConeDirection(float3(0.0, 0.0, 1.0), VfPi, RngWord);
        float  Radial  = pow(ParticleNextUnit(RngWord), 1.0 / 3.0) * RadiusM;
        ReleasePos += UnitDir * Radial;
    }
    else if (Shape == VolumeShapeDiscVolume)
    {
        float Angle  = ParticleNextUnit(RngWord) * VfTau;
        float Radial = sqrt(ParticleNextUnit(RngWord)) * RadiusM;
        ReleasePos.x += cos(Angle) * Radial;
        ReleasePos.y += sin(Angle) * Radial;
    }
    else if (Shape == VolumeShapeBoxVolume)
    {
        ReleasePos.x += (ParticleNextUnit(RngWord) * 2.0 - 1.0) * BoxHalf.x;
        ReleasePos.y += (ParticleNextUnit(RngWord) * 2.0 - 1.0) * BoxHalf.y;
        ReleasePos.z += (ParticleNextUnit(RngWord) * 2.0 - 1.0) * BoxHalf.z;
    }
    else if (Shape == VolumeShapeBoxSurfaceLattice)
    {
        uint  FaceIdx = ParticleXorshift32(RngWord) % 6u;
        float Sx      = (ParticleNextUnit(RngWord) * 2.0 - 1.0) * BoxHalf.x;
        float Sy      = (ParticleNextUnit(RngWord) * 2.0 - 1.0) * BoxHalf.y;
        float Sz      = (ParticleNextUnit(RngWord) * 2.0 - 1.0) * BoxHalf.z;
        if      (FaceIdx == 0u) { Sx =  BoxHalf.x; }
        else if (FaceIdx == 1u) { Sx = -BoxHalf.x; }
        else if (FaceIdx == 2u) { Sy =  BoxHalf.y; }
        else if (FaceIdx == 3u) { Sy = -BoxHalf.y; }
        else if (FaceIdx == 4u) { Sz =  BoxHalf.z; }
        else                    { Sz = -BoxHalf.z; }
        ReleasePos += float3(Sx, Sy, Sz);
    }
    else if (Shape == VolumeShapeDualElectrodeArc)
    {
        float Ratio   = (Uniforms.ReleaseCount > 1u) ? (float(Offset) / float(Uniforms.ReleaseCount - 1u)) : ParticleNextUnit(RngWord);
        float ArcSpan = max(0.4, RadiusM * 2.0);
        ReleasePos.x += (Ratio - 0.5) * ArcSpan;
        ReleasePos.z += sin(Ratio * VfPi) * 0.25;
    }

    float3 LaunchDir = SampleConeDirection(Uniforms.ReleaseDirSpread.xyz, Uniforms.ReleaseDirSpread.w, RngWord);
    float  SpeedMin  = Uniforms.ReleaseBoxExtents.w;
    float  SpeedMax  = max(SpeedMin, Uniforms.ReleaseSpeedLife.x);
    float  SpeedMs   = ParticleNextRange(RngWord, SpeedMin, SpeedMax);
    float  LifeMin   = Uniforms.ReleaseSpeedLife.y;
    float  LifeMax   = max(LifeMin, Uniforms.ReleaseSpeedLife.z);

    GpuKinematicsSlot Kin;
    Kin.PositionInvMass = float4(ReleasePos, 1.0);
    Kin.VelocityDrag    = float4(LaunchDir * SpeedMs, 1.0);
    KinematicsBuffer[Slot] = Kin;

    GpuLifespanSlot Life;
    Life.AgeSeconds     = 0.0;
    Life.MaxLifeSeconds = ParticleNextRange(RngWord, LifeMin, LifeMax);
    Life.SeedUint32     = ParticleXorshift32(RngWord);
    Life.LivenessMask   = 1u;
    LifespanBuffer[Slot] = Life;

    float RollRad = ParticleNextRange(RngWord, 0.0, VfTau);
    float SpinMax = Uniforms.SizeSpinMetrics.w;
    float SpinRad = ParticleNextRange(RngWord, -SpinMax, SpinMax);

    GpuAppearanceSlot Appear;
    Appear.ColorRgba     = Uniforms.ColorStopA;
    Appear.VisualMetrics = float4(Uniforms.SizeSpinMetrics.x * Uniforms.SizeSpinMetrics.z, RollRad, SpinRad, 0.0);
    AppearanceBuffer[Slot] = Appear;
}
