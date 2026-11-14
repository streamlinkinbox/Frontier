/*============================================================================================================================================
                                                         PARTICLEBUILTINLAWS.CPP
============================================================================================================================================*/
// 🧩 Implementations of built-in modular release rules, ForceFields coupling, gravity, curl turbulence, colliders (Ground/Sphere/SDF) & integrators

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Particles/ParticleBuiltInLaws.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace::Particles
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

constexpr float StandardGravityMs2 = 9.80665f; // [m/s^2] - Standard Earth gravity along -Z

[[nodiscard]] float ApplyRampWrap(float T, RampWrapRule Wrap) noexcept
{
    if (Wrap == RampWrapRule::Repeat)
    {
        return T - std::floor(T);
    }
    if (Wrap == RampWrapRule::PingPong)
    {
        const float Mod = T - 2.0f * std::floor(T * 0.5f);
        return (Mod <= 1.0f) ? Mod : (2.0f - Mod);
    }
    return std::clamp(T, 0.0f, 1.0f);
}

[[nodiscard]] ForceFields::SpatialVector3 EvaluateAnalyticCurl(
    float X,
    float Y,
    float Z,
    float Frequency,
    float TimePhase) noexcept
{
    // Exact divergence-free curl of 3D trigonometric vector potential A = (sin(y+t)cos(z), sin(z+t)cos(x), sin(x+t)cos(y))
    const float Px = X * Frequency;
    const float Py = Y * Frequency;
    const float Pz = Z * Frequency;

    const float dAz_dy = -std::sin(Px + TimePhase) * std::sin(Py);
    const float dAy_dz =  std::cos(Pz + TimePhase) * std::cos(Px);
    const float dAx_dz = -std::sin(Py + TimePhase) * std::sin(Pz);
    const float dAz_dx =  std::cos(Px + TimePhase) * std::cos(Py);
    const float dAy_dx = -std::sin(Pz + TimePhase) * std::sin(Px);
    const float dAx_dy =  std::cos(Py + TimePhase) * std::cos(Pz);

    return {
        dAz_dy - dAy_dz,
        dAx_dz - dAz_dx,
        dAy_dx - dAx_dy,
    };
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                1. VOLUME RELEASE RULE
//------------------------------------------------------------------------------------------------------------------------

void VolumeReleaseRule::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(StandardChannels::Kinematics);
    (void)Bank.RequireChannel(StandardChannels::Lifespan);
    (void)Bank.RequireChannel(StandardChannels::Appearance);
    if (Params.AllocateOrientation)
    {
        (void)Bank.RequireChannel(StandardChannels::Orientation);
    }
}

uint32_t VolumeReleaseRule::ExecuteRelease(
    ParticleChannelBank&        Bank,
    const SimulationStepInputs& Inputs,
    ParticleRng&                Rng)
{
    uint32_t RequestedCount = 0u;
    if (!InitialBurstDispatched && Params.InitialBurstCount > 0u)
    {
        RequestedCount        += Params.InitialBurstCount;
        InitialBurstDispatched = true;
    }

    if (PendingManualBurst > 0u)
    {
        RequestedCount    += PendingManualBurst;
        PendingManualBurst = 0u;
    }

    if (Params.BirthRatePerSec > 0.0f && Inputs.DeltaTimeSeconds > 0.0f)
    {
        FractionalAccumulator += Params.BirthRatePerSec * Inputs.DeltaTimeSeconds;
        const uint32_t ContinuousCount = static_cast<uint32_t>(std::floor(FractionalAccumulator));
        FractionalAccumulator         -= static_cast<float>(ContinuousCount);
        RequestedCount                += ContinuousCount;
    }

    if (Params.BurstIntervalSec > 1.0e-3f && Params.BurstCount > 0u)
    {
        BurstTimerSeconds += Inputs.DeltaTimeSeconds;
        if (BurstTimerSeconds >= Params.BurstIntervalSec)
        {
            BurstTimerSeconds -= Params.BurstIntervalSec;
            RequestedCount    += Params.BurstCount;
        }
    }

    if (RequestedCount == 0u)
    {
        return 0u;
    }

    uint32_t BeginSlot     = 0u;
    const uint32_t Spawned = Bank.SpawnSlots(RequestedCount, BeginSlot);
    if (Spawned == 0u)
    {
        return 0u;
    }

    auto Kin    = Bank.AccessChannel(StandardChannels::Kinematics);
    auto Life   = Bank.AccessChannel(StandardChannels::Lifespan);
    auto Appear = Bank.AccessChannel(StandardChannels::Appearance);
    auto Orient = Bank.AccessChannel(StandardChannels::Orientation);

    for (uint32_t Offset = 0u; Offset < Spawned; ++Offset)
    {
        const uint32_t Slot = BeginSlot + Offset;
        ForceFields::SpatialVector3 SpawnPos = Params.OriginMeters;

        switch (Params.Shape)
        {
            case VolumeShapeCategory::PointOrigin:
                break;

            case VolumeShapeCategory::SphereVolume:
            {
                const ForceFields::SpatialVector3 Dir = Rng.NextUnitSphere();
                const float R = Params.RadiusMeters * std::cbrt(Rng.NextUnitFloat());
                SpawnPos.X += Dir.X * R;
                SpawnPos.Y += Dir.Y * R;
                SpawnPos.Z += Dir.Z * R;
                break;
            }

            case VolumeShapeCategory::DiscVolume:
            {
                const float Angle = Constants::TauFloat * Rng.NextUnitFloat();
                const float R     = Params.RadiusMeters * std::sqrt(Rng.NextUnitFloat());
                SpawnPos.X += std::cos(Angle) * R;
                SpawnPos.Y += std::sin(Angle) * R;
                break;
            }

            case VolumeShapeCategory::BoxVolume:
            {
                SpawnPos.X += Rng.NextRange(-Params.BoxHalfExtents.X, Params.BoxHalfExtents.X);
                SpawnPos.Y += Rng.NextRange(-Params.BoxHalfExtents.Y, Params.BoxHalfExtents.Y);
                SpawnPos.Z += Rng.NextRange(-Params.BoxHalfExtents.Z, Params.BoxHalfExtents.Z);
                break;
            }

            case VolumeShapeCategory::BoxSurfaceLattice:
            {
                const uint32_t Face = Rng.NextUint32() % 6u;
                float Rx = Rng.NextRange(-Params.BoxHalfExtents.X, Params.BoxHalfExtents.X);
                float Ry = Rng.NextRange(-Params.BoxHalfExtents.Y, Params.BoxHalfExtents.Y);
                float Rz = Rng.NextRange(-Params.BoxHalfExtents.Z, Params.BoxHalfExtents.Z);
                if (Face == 0u)      { Rx =  Params.BoxHalfExtents.X; }
                else if (Face == 1u) { Rx = -Params.BoxHalfExtents.X; }
                else if (Face == 2u) { Ry =  Params.BoxHalfExtents.Y; }
                else if (Face == 3u) { Ry = -Params.BoxHalfExtents.Y; }
                else if (Face == 4u) { Rz =  Params.BoxHalfExtents.Z; }
                else                 { Rz = -Params.BoxHalfExtents.Z; }
                SpawnPos.X += Rx;
                SpawnPos.Y += Ry;
                SpawnPos.Z += Rz;
                break;
            }

            case VolumeShapeCategory::DualElectrodeArc:
            {
                const float U = (Spawned > 1u) ? (static_cast<float>(Offset) / static_cast<float>(Spawned - 1u)) : Rng.NextUnitFloat();
                const float ArcSpan = std::max(0.4f, Params.RadiusMeters * 2.0f);
                SpawnPos.X += (U - 0.5f) * ArcSpan;
                SpawnPos.Z += std::sin(U * Constants::PiFloat) * 0.25f;
                break;
            }
        }

        const ForceFields::SpatialVector3 Dir = Rng.NextConeDirection(Params.LaunchDirection, Params.ConeSpreadRad);
        const float Speed                     = Rng.NextRange(Params.SpeedMinMs, std::max(Params.SpeedMinMs, Params.SpeedMaxMs));

        Kin[Slot].PosX        = SpawnPos.X;
        Kin[Slot].PosY        = SpawnPos.Y;
        Kin[Slot].PosZ        = SpawnPos.Z;
        Kin[Slot].InverseMass = 1.0f;
        Kin[Slot].VelX        = Dir.X * Speed;
        Kin[Slot].VelY        = Dir.Y * Speed;
        Kin[Slot].VelZ        = Dir.Z * Speed;
        Kin[Slot].DragScale   = 1.0f;

        Life[Slot].AgeSeconds     = 0.0f;
        Life[Slot].MaxLifeSeconds = Rng.NextRange(Params.LifeMinSec, std::max(Params.LifeMinSec, Params.LifeMaxSec));
        Life[Slot].SeedUint32     = Rng.NextUint32();
        Life[Slot].LivenessMask   = 1u;

        Appear[Slot].RadiusMeters  = Params.SizeStartMeters;
        Appear[Slot].RollRadians   = Rng.NextRange(0.0f, Constants::TauFloat);
        Appear[Slot].SpinRadPerSec = Rng.NextRange(-Params.SpinRateRadSec, Params.SpinRateRadSec);
        Appear[Slot].ColorRampT    = 0.0f;

        if (!Orient.empty())
        {
            const ForceFields::SpatialVector3 SpinAxis = Rng.NextUnitSphere();
            const float SpinMag                        = Rng.NextRange(0.2f, std::max(0.2f, Params.SpinRateRadSec * 1.5f));
            Orient[Slot].QuatX                         = 0.0f;
            Orient[Slot].QuatY                         = 0.0f;
            Orient[Slot].QuatZ                         = 0.0f;
            Orient[Slot].QuatW                         = 1.0f;
            Orient[Slot].AngVelX                       = SpinAxis.X * SpinMag;
            Orient[Slot].AngVelY                       = SpinAxis.Y * SpinMag;
            Orient[Slot].AngVelZ                       = SpinAxis.Z * SpinMag;
            Orient[Slot].InertiaScalar                 = 1.0f;
        }
    }

    return Spawned;
}

void VolumeReleaseRule::ResetReleaseClock() noexcept
{
    FractionalAccumulator  = 0.0f;
    BurstTimerSeconds      = 0.0f;
    PendingManualBurst     = 0u;
    InitialBurstDispatched = false;
}

void VolumeReleaseRule::TriggerImmediateBurst(uint32_t OverrideCount) noexcept
{
    PendingManualBurst += (OverrideCount > 0u) ? OverrideCount : std::max(1u, Params.BurstCount);
}

//------------------------------------------------------------------------------------------------------------------------
//                                           2. GRAVITY & BUOYANCY LAW
//------------------------------------------------------------------------------------------------------------------------

void GravityAndBuoyancyLaw::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(StandardChannels::Kinematics);
}

void GravityAndBuoyancyLaw::ApplyToRange(
    ParticleChannelBank&        Bank,
    uint32_t                    BeginSlot,
    uint32_t                    EndSlot,
    const SimulationStepInputs& Inputs) const
{
    auto Kin = Bank.AccessChannel(StandardChannels::Kinematics);
    if (Kin.empty())
    {
        return;
    }

    const float Dt        = Inputs.DeltaTimeSeconds;
    const float NetAccelZ = -StandardGravityMs2 * Params.GravityScale + Params.BuoyancyAccelMs2;

    for (uint32_t Slot = BeginSlot; Slot < EndSlot; ++Slot)
    {
        KinematicsChannel& P = Kin[Slot];
        if (P.InverseMass <= 0.0f)
        {
            continue;
        }

        P.VelZ += NetAccelZ * Dt;

        const float EffectiveDrag = std::max(0.0f, Params.DragCoefficient * P.DragScale);
        if (EffectiveDrag > 0.0f)
        {
            const float Damp = std::exp(-EffectiveDrag * Dt);
            P.VelX *= Damp;
            P.VelY *= Damp;
            P.VelZ *= Damp;
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                           3. FORCEFIELDS COUPLING LAW
//------------------------------------------------------------------------------------------------------------------------

void ForceFieldCouplingLaw::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(StandardChannels::Kinematics);
}

void ForceFieldCouplingLaw::ApplyToRange(
    ParticleChannelBank&        Bank,
    uint32_t                    BeginSlot,
    uint32_t                    EndSlot,
    const SimulationStepInputs& Inputs) const
{
    if (Params.CouplingGain <= 1.0e-5f && Params.AccelScale <= 1.0e-5f)
    {
        return;
    }
    if (Inputs.ForceAccumulator == nullptr && Inputs.ForceLattice == nullptr)
    {
        return;
    }

    auto Kin    = Bank.AccessChannel(StandardChannels::Kinematics);
    auto Orient = Bank.AccessChannel(StandardChannels::Orientation);
    if (Kin.empty())
    {
        return;
    }

    const float Dt             = Inputs.DeltaTimeSeconds;
    const bool  UseLattice     = Params.PreferLatticeLookup && (Inputs.ForceLattice != nullptr);
    const bool  HasOrientation = !Orient.empty();

    for (uint32_t Slot = BeginSlot; Slot < EndSlot; ++Slot)
    {
        KinematicsChannel& P = Kin[Slot];
        if (P.InverseMass <= 0.0f)
        {
            continue;
        }

        const ForceFields::SpatialVector3 Pos{ P.PosX, P.PosY, P.PosZ };
        const ForceFields::SpatialVector3 Vel{ P.VelX, P.VelY, P.VelZ };

        if (UseLattice)
        {
            const ForceFields::FieldLatticeVoxelSample Voxel =
                Inputs.ForceLattice->SampleTierTrilinear(Params.LatticeTierIndex, Pos);
            const float Blend = 1.0f - std::exp(-std::max(0.0f, Params.CouplingGain) * Dt);
            P.VelX += (Voxel.VelocityX - P.VelX) * Blend;
            P.VelY += (Voxel.VelocityY - P.VelY) * Blend;
            P.VelZ += (Voxel.VelocityZ - P.VelZ) * Blend;
        }
        else if (Inputs.ForceAccumulator != nullptr)
        {
            const ForceFields::SpatialForceSample Sample =
                Inputs.ForceAccumulator->QueryForceFieldSample(Pos, Vel, Inputs.TimeSeconds);

            const float Blend = 1.0f - std::exp(-std::max(0.0f, Params.CouplingGain) * Dt);
            P.VelX += (Sample.AdvectionVelocity.X - P.VelX) * Blend;
            P.VelY += (Sample.AdvectionVelocity.Y - P.VelY) * Blend;
            P.VelZ += (Sample.AdvectionVelocity.Z - P.VelZ) * Blend;

            P.VelX += Sample.LinearAcceleration.X * Params.AccelScale * P.InverseMass * Dt;
            P.VelY += Sample.LinearAcceleration.Y * Params.AccelScale * P.InverseMass * Dt;
            P.VelZ += Sample.LinearAcceleration.Z * Params.AccelScale * P.InverseMass * Dt;

            const float FieldDrag = std::max(0.0f, Sample.DragDecayRate * Params.DragScale);
            if (FieldDrag > 1.0e-5f)
            {
                const float Damp = std::exp(-FieldDrag * Dt);
                P.VelX *= Damp;
                P.VelY *= Damp;
                P.VelZ *= Damp;
            }

            if (HasOrientation && Params.TorqueScale > 1.0e-5f)
            {
                OrientationChannel& Q = Orient[Slot];
                Q.AngVelX += Sample.AngularTorque.X * Params.TorqueScale * Dt;
                Q.AngVelY += Sample.AngularTorque.Y * Params.TorqueScale * Dt;
                Q.AngVelZ += Sample.AngularTorque.Z * Params.TorqueScale * Dt;
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                           4. LOCAL CURL TURBULENCE LAW
//------------------------------------------------------------------------------------------------------------------------

void LocalCurlTurbulenceLaw::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(StandardChannels::Kinematics);
}

void LocalCurlTurbulenceLaw::ApplyToRange(
    ParticleChannelBank&        Bank,
    uint32_t                    BeginSlot,
    uint32_t                    EndSlot,
    const SimulationStepInputs& Inputs) const
{
    if (Params.AmplitudeMs <= 1.0e-5f)
    {
        return;
    }

    auto Kin = Bank.AccessChannel(StandardChannels::Kinematics);
    if (Kin.empty())
    {
        return;
    }

    const float Dt        = Inputs.DeltaTimeSeconds;
    const float TimePhase = static_cast<float>(Inputs.TimeSeconds) * Params.TimeScrollSpeed;

    for (uint32_t Slot = BeginSlot; Slot < EndSlot; ++Slot)
    {
        KinematicsChannel& P = Kin[Slot];
        if (P.InverseMass <= 0.0f)
        {
            continue;
        }
        const ForceFields::SpatialVector3 Curl =
            EvaluateAnalyticCurl(P.PosX, P.PosY, P.PosZ, Params.FrequencyPerM, TimePhase);

        P.VelX += Curl.X * Params.AmplitudeMs * Dt;
        P.VelY += Curl.Y * Params.AmplitudeMs * Dt;
        P.VelZ += Curl.Z * Params.AmplitudeMs * Dt;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                            5. FLUTTER & TUMBLE LAW
//------------------------------------------------------------------------------------------------------------------------

void FlutterAndTumbleLaw::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(StandardChannels::Kinematics);
    (void)Bank.RequireChannel(StandardChannels::Appearance);
}

void FlutterAndTumbleLaw::ApplyToRange(
    ParticleChannelBank&        Bank,
    uint32_t                    BeginSlot,
    uint32_t                    EndSlot,
    const SimulationStepInputs& Inputs) const
{
    auto Kin    = Bank.AccessChannel(StandardChannels::Kinematics);
    auto Appear = Bank.AccessChannel(StandardChannels::Appearance);
    auto Life   = Bank.QueryChannel(StandardChannels::Lifespan);
    auto Orient = Bank.AccessChannel(StandardChannels::Orientation);
    if (Kin.empty() || Appear.empty())
    {
        return;
    }

    const float Dt       = Inputs.DeltaTimeSeconds;
    const float AngDamp  = std::exp(-std::max(0.0f, Params.AngularDamping) * Dt);
    const float OmegaRad = Constants::TauFloat * Params.FlutterFrequencyHz;

    for (uint32_t Slot = BeginSlot; Slot < EndSlot; ++Slot)
    {
        const uint32_t Seed = !Life.empty() ? Life[Slot].SeedUint32 : (Slot * 2654435761u + 17u);
        const float Phase   = static_cast<float>(Seed & 0xFFFFu) * (Constants::TauFloat / 65536.0f)
                            + static_cast<float>(Inputs.TimeSeconds) * OmegaRad;

        if (Kin[Slot].InverseMass > 0.0f && Params.FlutterAmplitudeMs2 > 1.0e-5f)
        {
            Kin[Slot].VelX += std::sin(Phase) * Params.FlutterAmplitudeMs2 * Dt;
            Kin[Slot].VelY += std::cos(Phase * 0.77f) * Params.FlutterAmplitudeMs2 * Dt;
        }

        Appear[Slot].RollRadians   += Appear[Slot].SpinRadPerSec * Dt;
        Appear[Slot].SpinRadPerSec *= AngDamp;

        if (!Orient.empty())
        {
            OrientationChannel& Q = Orient[Slot];
            Q.AngVelX *= AngDamp;
            Q.AngVelY *= AngDamp;
            Q.AngVelZ *= AngDamp;

            // First-order quaternion kinematic derivative: dq/dt = 0.5 * (0, w) * q
            const float HalfDt = 0.5f * Dt;
            const float Dx     = HalfDt * ( Q.AngVelX * Q.QuatW + Q.AngVelY * Q.QuatZ - Q.AngVelZ * Q.QuatY);
            const float Dy     = HalfDt * (-Q.AngVelX * Q.QuatZ + Q.AngVelY * Q.QuatW + Q.AngVelZ * Q.QuatX);
            const float Dz     = HalfDt * ( Q.AngVelX * Q.QuatY - Q.AngVelY * Q.QuatX + Q.AngVelZ * Q.QuatW);
            const float Dw     = HalfDt * (-Q.AngVelX * Q.QuatX - Q.AngVelY * Q.QuatY - Q.AngVelZ * Q.QuatZ);

            Q.QuatX += Dx;
            Q.QuatY += Dy;
            Q.QuatZ += Dz;
            Q.QuatW += Dw;

            const float Len = std::sqrt(Q.QuatX * Q.QuatX + Q.QuatY * Q.QuatY + Q.QuatZ * Q.QuatZ + Q.QuatW * Q.QuatW);
            if (Len > 1.0e-6f)
            {
                const float Inv = 1.0f / Len;
                Q.QuatX *= Inv;
                Q.QuatY *= Inv;
                Q.QuatZ *= Inv;
                Q.QuatW *= Inv;
            }
            else
            {
                Q.QuatX = 0.0f;
                Q.QuatY = 0.0f;
                Q.QuatZ = 0.0f;
                Q.QuatW = 1.0f;
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                           6. COLOR & SIZE CURVE LAW
//------------------------------------------------------------------------------------------------------------------------

void ColorAndSizeCurveLaw::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(StandardChannels::Appearance);
}

void ColorAndSizeCurveLaw::ApplyToRange(
    ParticleChannelBank&        Bank,
    uint32_t                    BeginSlot,
    uint32_t                    EndSlot,
    const SimulationStepInputs& /*Inputs*/) const
{
    auto Appear = Bank.AccessChannel(StandardChannels::Appearance);
    auto Life   = Bank.QueryChannel(StandardChannels::Lifespan);
    auto Kin    = Bank.QueryChannel(StandardChannels::Kinematics);
    if (Appear.empty())
    {
        return;
    }

    for (uint32_t Slot = BeginSlot; Slot < EndSlot; ++Slot)
    {
        float LifeT = 0.0f;
        if (!Life.empty() && Life[Slot].MaxLifeSeconds > 1.0e-5f)
        {
            LifeT = std::clamp(Life[Slot].AgeSeconds / Life[Slot].MaxLifeSeconds, 0.0f, 1.0f);
        }

        float RawCoord = LifeT;
        switch (Params.Driver)
        {
            case ColorSampleDriver::LifetimeRatio:
                RawCoord = LifeT;
                break;

            case ColorSampleDriver::SpeedMagnitude:
                if (!Kin.empty())
                {
                    const float Spd = std::sqrt(
                        Kin[Slot].VelX * Kin[Slot].VelX
                        + Kin[Slot].VelY * Kin[Slot].VelY
                        + Kin[Slot].VelZ * Kin[Slot].VelZ);
                    RawCoord = Spd / std::max(0.1f, Params.SpeedNormMaxMs);
                }
                break;

            case ColorSampleDriver::HeightAltitude:
                if (!Kin.empty())
                {
                    RawCoord = Kin[Slot].PosZ / std::max(0.1f, Params.HeightNormMaxM);
                }
                break;

            case ColorSampleDriver::RandomPerSlot:
                if (!Life.empty())
                {
                    RawCoord = static_cast<float>(Life[Slot].SeedUint32 & 0xFFFFu) * (1.0f / 65536.0f);
                }
                break;
        }

        const float T = ApplyRampWrap(RawCoord + Params.PhaseOffset, Params.WrapRule);
        Appear[Slot].ColorRampT = T;

        if (T <= 0.5f)
        {
            const float U = T * 2.0f;
            Appear[Slot].ColorR = Params.ColorA[0] + (Params.ColorC[0] - Params.ColorA[0]) * U;
            Appear[Slot].ColorG = Params.ColorA[1] + (Params.ColorC[1] - Params.ColorA[1]) * U;
            Appear[Slot].ColorB = Params.ColorA[2] + (Params.ColorC[2] - Params.ColorA[2]) * U;
            Appear[Slot].ColorA = Params.ColorA[3] + (Params.ColorC[3] - Params.ColorA[3]) * U;
        }
        else
        {
            const float U = (T - 0.5f) * 2.0f;
            Appear[Slot].ColorR = Params.ColorC[0] + (Params.ColorB[0] - Params.ColorC[0]) * U;
            Appear[Slot].ColorG = Params.ColorC[1] + (Params.ColorB[1] - Params.ColorC[1]) * U;
            Appear[Slot].ColorB = Params.ColorC[2] + (Params.ColorB[2] - Params.ColorC[2]) * U;
            Appear[Slot].ColorA = Params.ColorC[3] + (Params.ColorB[3] - Params.ColorC[3]) * U;
        }

        Appear[Slot].RadiusMeters =
            (Params.SizeStartMeters + (Params.SizeEndMeters - Params.SizeStartMeters) * LifeT) * Params.SizeMultiplier;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                              7. MODULAR COLLIDERS
//------------------------------------------------------------------------------------------------------------------------

void GroundPlaneCollider::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(StandardChannels::Kinematics);
}

void GroundPlaneCollider::ResolveRange(
    ParticleChannelBank&        Bank,
    uint32_t                    BeginSlot,
    uint32_t                    EndSlot,
    const SimulationStepInputs& /*Inputs*/) const
{
    auto Kin = Bank.AccessChannel(StandardChannels::Kinematics);
    if (Kin.empty())
    {
        return;
    }

    const float TangentialKeep = std::clamp(1.0f - Params.SlidingFriction, 0.0f, 1.0f);
    for (uint32_t Slot = BeginSlot; Slot < EndSlot; ++Slot)
    {
        KinematicsChannel& P = Kin[Slot];
        if (P.InverseMass <= 0.0f)
        {
            continue;
        }
        if (P.PosZ < Params.FloorZMeters)
        {
            P.PosZ = Params.FloorZMeters;
            if (P.VelZ < 0.0f)
            {
                P.VelZ = -P.VelZ * Params.Restitution;
                if (std::abs(P.VelZ) < 0.08f)
                {
                    P.VelZ = 0.0f;
                }
            }
            P.VelX *= TangentialKeep;
            P.VelY *= TangentialKeep;
        }
    }
}

void DynamicSphereColliderRule::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(StandardChannels::Kinematics);
}

void DynamicSphereColliderRule::ResolveRange(
    ParticleChannelBank&        Bank,
    uint32_t                    BeginSlot,
    uint32_t                    EndSlot,
    const SimulationStepInputs& Inputs) const
{
    if (Inputs.SphereColliders.empty())
    {
        return;
    }
    auto Kin = Bank.AccessChannel(StandardChannels::Kinematics);
    if (Kin.empty())
    {
        return;
    }

    for (const DynamicSphereCollider& Sphere : Inputs.SphereColliders)
    {
        if (!Sphere.Active || Sphere.RadiusMeters <= 1.0e-4f)
        {
            continue;
        }
        const float RadSq = Sphere.RadiusMeters * Sphere.RadiusMeters;
        for (uint32_t Slot = BeginSlot; Slot < EndSlot; ++Slot)
        {
            KinematicsChannel& P = Kin[Slot];
            if (P.InverseMass <= 0.0f)
            {
                continue;
            }
            const float Dx     = P.PosX - Sphere.CentreMeters.X;
            const float Dy     = P.PosY - Sphere.CentreMeters.Y;
            const float Dz     = P.PosZ - Sphere.CentreMeters.Z;
            const float DistSq = Dx * Dx + Dy * Dy + Dz * Dz;
            if (DistSq < RadSq)
            {
                const float Dist = std::sqrt(std::max(DistSq, 1.0e-10f));
                const float Nx   = (DistSq > 1.0e-10f) ? (Dx / Dist) : 0.0f;
                const float Ny   = (DistSq > 1.0e-10f) ? (Dy / Dist) : 0.0f;
                const float Nz   = (DistSq > 1.0e-10f) ? (Dz / Dist) : 1.0f;

                P.PosX = Sphere.CentreMeters.X + Nx * Sphere.RadiusMeters;
                P.PosY = Sphere.CentreMeters.Y + Ny * Sphere.RadiusMeters;
                P.PosZ = Sphere.CentreMeters.Z + Nz * Sphere.RadiusMeters;

                const float RelVx = P.VelX - Sphere.VelocityMs.X;
                const float RelVy = P.VelY - Sphere.VelocityMs.Y;
                const float RelVz = P.VelZ - Sphere.VelocityMs.Z;
                const float RelVn = RelVx * Nx + RelVy * Ny + RelVz * Nz;
                if (RelVn < 0.0f)
                {
                    const float J = -(1.0f + Sphere.Restitution) * RelVn * ImpulseGain;
                    P.VelX += Nx * J;
                    P.VelY += Ny * J;
                    P.VelZ += Nz * J;
                }
            }
        }
    }
}

void SignedDistanceVolumeCollider::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(StandardChannels::Kinematics);
}

void SignedDistanceVolumeCollider::ResolveRange(
    ParticleChannelBank&        Bank,
    uint32_t                    BeginSlot,
    uint32_t                    EndSlot,
    const SimulationStepInputs& Inputs) const
{
    if (Inputs.WorldDistanceField == nullptr)
    {
        return;
    }

    auto Kin = Bank.AccessChannel(StandardChannels::Kinematics);
    if (Kin.empty())
    {
        return;
    }

    const float TangentialKeep = std::clamp(1.0f - Params.SurfaceFriction, 0.0f, 1.0f);
    for (uint32_t Slot = BeginSlot; Slot < EndSlot; ++Slot)
    {
        KinematicsChannel& P = Kin[Slot];
        if (P.InverseMass <= 0.0f)
        {
            continue;
        }

        const Mathematics::Axis3D WorldPos{ P.PosX, P.PosY, P.PosZ };
        const float SignedDist = Inputs.WorldDistanceField->SampleSignedDistance(WorldPos);
        if (SignedDist < Params.ContactMarginMeters)
        {
            const Mathematics::Axis3D Normal = Inputs.WorldDistanceField->EvaluateSurfaceGradient(WorldPos);
            const float PushOut              = Params.ContactMarginMeters - SignedDist;
            P.PosX += Normal.X * PushOut;
            P.PosY += Normal.Y * PushOut;
            P.PosZ += Normal.Z * PushOut;

            const float Vn = P.VelX * Normal.X + P.VelY * Normal.Y + P.VelZ * Normal.Z;
            const float VtX = P.VelX - Normal.X * Vn;
            const float VtY = P.VelY - Normal.Y * Vn;
            const float VtZ = P.VelZ - Normal.Z * Vn;

            const float OutVn = (Vn < 0.0f) ? (-Vn * Params.Restitution) : Vn;
            P.VelX = VtX * TangentialKeep + Normal.X * OutVn;
            P.VelY = VtY * TangentialKeep + Normal.Y * OutVn;
            P.VelZ = VtZ * TangentialKeep + Normal.Z * OutVn;
        }
        else if (Params.AdhesionStrength > 1.0e-5f && SignedDist < Params.ContactMarginMeters * 6.0f)
        {
            const Mathematics::Axis3D Normal = Inputs.WorldDistanceField->EvaluateSurfaceGradient(WorldPos);
            const float Pull                 = Params.AdhesionStrength * Inputs.DeltaTimeSeconds;
            P.VelX -= Normal.X * Pull;
            P.VelY -= Normal.Y * Pull;
            P.VelZ -= Normal.Z * Pull;
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                             8. MODULAR TIME INTEGRATORS
//------------------------------------------------------------------------------------------------------------------------

void SemiImplicitEulerIntegrator::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(StandardChannels::Kinematics);
}

void SemiImplicitEulerIntegrator::IntegrateRange(
    ParticleChannelBank&        Bank,
    uint32_t                    BeginSlot,
    uint32_t                    EndSlot,
    const SimulationStepInputs& Inputs) const
{
    auto Kin = Bank.AccessChannel(StandardChannels::Kinematics);
    if (Kin.empty())
    {
        return;
    }

    const float Dt = Inputs.DeltaTimeSeconds;
    for (uint32_t Slot = BeginSlot; Slot < EndSlot; ++Slot)
    {
        KinematicsChannel& P = Kin[Slot];
        if (P.InverseMass <= 0.0f)
        {
            continue;
        }
        P.PosX += P.VelX * Dt;
        P.PosY += P.VelY * Dt;
        P.PosZ += P.VelZ * Dt;
    }
}

void XpbdVerletIntegrator::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(StandardChannels::Kinematics);
    (void)Bank.RequireChannel(StandardChannels::StrandAnchor);
}

void XpbdVerletIntegrator::IntegrateRange(
    ParticleChannelBank&        Bank,
    uint32_t                    BeginSlot,
    uint32_t                    EndSlot,
    const SimulationStepInputs& Inputs) const
{
    auto Kin    = Bank.AccessChannel(StandardChannels::Kinematics);
    auto Anchor = Bank.AccessChannel(StandardChannels::StrandAnchor);
    if (Kin.empty() || Anchor.empty())
    {
        return;
    }

    const float Dt = Inputs.DeltaTimeSeconds;
    for (uint32_t Slot = BeginSlot; Slot < EndSlot; ++Slot)
    {
        KinematicsChannel&   P = Kin[Slot];
        StrandAnchorChannel& A = Anchor[Slot];
        A.PrevPosX             = P.PosX;
        A.PrevPosY             = P.PosY;
        A.PrevPosZ             = P.PosZ;
        if (P.InverseMass <= 0.0f || A.PinWeight >= 0.999f)
        {
            P.PosX = A.AnchorX;
            P.PosY = A.AnchorY;
            P.PosZ = A.AnchorZ;
            P.VelX = 0.0f;
            P.VelY = 0.0f;
            P.VelZ = 0.0f;
            continue;
        }
        P.PosX += P.VelX * Dt;
        P.PosY += P.VelY * Dt;
        P.PosZ += P.VelZ * Dt;
    }
}

void MidpointRk2Integrator::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(StandardChannels::Kinematics);
}

void MidpointRk2Integrator::IntegrateRange(
    ParticleChannelBank&        Bank,
    uint32_t                    BeginSlot,
    uint32_t                    EndSlot,
    const SimulationStepInputs& Inputs) const
{
    auto Kin = Bank.AccessChannel(StandardChannels::Kinematics);
    if (Kin.empty())
    {
        return;
    }

    const float Dt     = Inputs.DeltaTimeSeconds;
    const float HalfDt = 0.5f * Dt;
    for (uint32_t Slot = BeginSlot; Slot < EndSlot; ++Slot)
    {
        KinematicsChannel& P = Kin[Slot];
        if (P.InverseMass <= 0.0f)
        {
            continue;
        }

        if (Inputs.ForceAccumulator != nullptr)
        {
            const ForceFields::SpatialVector3 MidPos{
                P.PosX + P.VelX * HalfDt,
                P.PosY + P.VelY * HalfDt,
                P.PosZ + P.VelZ * HalfDt,
            };
            const ForceFields::SpatialForceSample MidSample =
                Inputs.ForceAccumulator->QueryForceFieldSample(MidPos, { P.VelX, P.VelY, P.VelZ }, Inputs.TimeSeconds + HalfDt);
            const float MidVx = P.VelX + MidSample.LinearAcceleration.X * HalfDt * P.InverseMass;
            const float MidVy = P.VelY + MidSample.LinearAcceleration.Y * HalfDt * P.InverseMass;
            const float MidVz = P.VelZ + MidSample.LinearAcceleration.Z * HalfDt * P.InverseMass;
            P.PosX += MidVx * Dt;
            P.PosY += MidVy * Dt;
            P.PosZ += MidVz * Dt;
        }
        else
        {
            P.PosX += P.VelX * Dt;
            P.PosY += P.VelY * Dt;
            P.PosZ += P.VelZ * Dt;
        }
    }
}

} // namespace Frontier::SimulationSpace::Particles
