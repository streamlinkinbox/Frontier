/*============================================================================================================================================
                                                     PARTICLEKINEMATICSSOLVERS.CPP
============================================================================================================================================*/
// 🧩 Implementations of native particle sources, ForceFields coupling, gravity, curl turbulence, colliders (Ground/Sphere/SDF) & integrators

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Particles/ParticleKinematicsSolvers.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace::Particles
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     INTERNAL ROUTINES
//------------------------------------------------------------------------------------------------------------------------

namespace
{

constexpr float StandardGravityMs2 = 9.80665f; // [m/s^2] - Standard Earth gravity along -Z

[[nodiscard]] float EvaluateRampWrap(float SampleCoord, RampWrapMode Mode) noexcept
{
    if (Mode == RampWrapMode::Repeat)
    {
        return SampleCoord - std::floor(SampleCoord);
    }
    if (Mode == RampWrapMode::PingPong)
    {
        const float Mod = SampleCoord - 2.0f * std::floor(SampleCoord * 0.5f);
        return (Mod <= 1.0f) ? Mod : (2.0f - Mod);
    }
    return std::clamp(SampleCoord, 0.0f, 1.0f);
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

    const float DerivAzDy = -std::sin(Px + TimePhase) * std::sin(Py);
    const float DerivAyDz =  std::cos(Pz + TimePhase) * std::cos(Px);
    const float DerivAxDz = -std::sin(Py + TimePhase) * std::sin(Pz);
    const float DerivAzDx =  std::cos(Px + TimePhase) * std::cos(Py);
    const float DerivAyDx = -std::sin(Pz + TimePhase) * std::sin(Px);
    const float DerivAxDy =  std::cos(Py + TimePhase) * std::cos(Pz);

    return {
        DerivAzDy - DerivAyDz,
        DerivAxDz - DerivAzDx,
        DerivAyDx - DerivAxDy,
    };
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                1. VOLUME PARTICLE SOURCE
//------------------------------------------------------------------------------------------------------------------------

void VolumeParticleSource::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(ChannelLayouts::Kinematics);
    (void)Bank.RequireChannel(ChannelLayouts::Lifespan);
    (void)Bank.RequireChannel(ChannelLayouts::Appearance);
    if (Settings.AllocateOrientation)
    {
        (void)Bank.RequireChannel(ChannelLayouts::Orientation);
    }
}

uint32_t VolumeParticleSource::ReleaseParticles(
    ParticleChannelBank&        Bank,
    const SimulationTickInputs& Inputs,
    ParticleRandomGenerator&    Generator)
{
    uint32_t RequestedCount = 0u;
    if (!InitialBurstDispatched && Settings.InitialBurstCount > 0u)
    {
        RequestedCount        += Settings.InitialBurstCount;
        InitialBurstDispatched = true;
    }

    if (PendingManualBurst > 0u)
    {
        RequestedCount    += PendingManualBurst;
        PendingManualBurst = 0u;
    }

    if (Settings.BirthRatePerSec > 0.0f && Inputs.DeltaTimeSeconds > 0.0f)
    {
        FractionalAccumulator += Settings.BirthRatePerSec * Inputs.DeltaTimeSeconds;
        const uint32_t ContinuousCount = static_cast<uint32_t>(std::floor(FractionalAccumulator));
        FractionalAccumulator         -= static_cast<float>(ContinuousCount);
        RequestedCount                += ContinuousCount;
    }

    if (Settings.BurstIntervalSec > 1.0e-3f && Settings.BurstCount > 0u)
    {
        BurstTimerSeconds += Inputs.DeltaTimeSeconds;
        if (BurstTimerSeconds >= Settings.BurstIntervalSec)
        {
            BurstTimerSeconds -= Settings.BurstIntervalSec;
            RequestedCount    += Settings.BurstCount;
        }
    }

    if (RequestedCount == 0u)
    {
        return 0u;
    }

    uint32_t BeginSlot           = 0u;
    const uint32_t AcquiredCount = Bank.AcquireSlots(RequestedCount, BeginSlot);
    if (AcquiredCount == 0u)
    {
        return 0u;
    }

    auto Kin    = Bank.AccessChannel(ChannelLayouts::Kinematics);
    auto Life   = Bank.AccessChannel(ChannelLayouts::Lifespan);
    auto Appear = Bank.AccessChannel(ChannelLayouts::Appearance);
    auto Orient = Bank.AccessChannel(ChannelLayouts::Orientation);

    for (uint32_t Offset = 0u; Offset < AcquiredCount; ++Offset)
    {
        const uint32_t Slot = BeginSlot + Offset;
        ForceFields::SpatialVector3 ReleasePosition = Settings.OriginMeters;

        switch (Settings.Shape)
        {
            case VolumeShapeCategory::PointOrigin:
                break;

            case VolumeShapeCategory::SphereVolume:
            {
                const ForceFields::SpatialVector3 Dir = Generator.NextUnitSphere();
                const float RadialDist = Settings.RadiusMeters * std::cbrt(Generator.NextUnitFloat());
                ReleasePosition.X += Dir.X * RadialDist;
                ReleasePosition.Y += Dir.Y * RadialDist;
                ReleasePosition.Z += Dir.Z * RadialDist;
                break;
            }

            case VolumeShapeCategory::DiscVolume:
            {
                const float Angle      = Constants::TauFloat * Generator.NextUnitFloat();
                const float RadialDist = Settings.RadiusMeters * std::sqrt(Generator.NextUnitFloat());
                ReleasePosition.X += std::cos(Angle) * RadialDist;
                ReleasePosition.Y += std::sin(Angle) * RadialDist;
                break;
            }

            case VolumeShapeCategory::BoxVolume:
            {
                ReleasePosition.X += Generator.NextRange(-Settings.BoxHalfExtents.X, Settings.BoxHalfExtents.X);
                ReleasePosition.Y += Generator.NextRange(-Settings.BoxHalfExtents.Y, Settings.BoxHalfExtents.Y);
                ReleasePosition.Z += Generator.NextRange(-Settings.BoxHalfExtents.Z, Settings.BoxHalfExtents.Z);
                break;
            }

            case VolumeShapeCategory::BoxSurfaceLattice:
            {
                const uint32_t FaceIndex = Generator.NextUint32() % 6u;
                float SampleX = Generator.NextRange(-Settings.BoxHalfExtents.X, Settings.BoxHalfExtents.X);
                float SampleY = Generator.NextRange(-Settings.BoxHalfExtents.Y, Settings.BoxHalfExtents.Y);
                float SampleZ = Generator.NextRange(-Settings.BoxHalfExtents.Z, Settings.BoxHalfExtents.Z);
                if (FaceIndex == 0u)      { SampleX =  Settings.BoxHalfExtents.X; }
                else if (FaceIndex == 1u) { SampleX = -Settings.BoxHalfExtents.X; }
                else if (FaceIndex == 2u) { SampleY =  Settings.BoxHalfExtents.Y; }
                else if (FaceIndex == 3u) { SampleY = -Settings.BoxHalfExtents.Y; }
                else if (FaceIndex == 4u) { SampleZ =  Settings.BoxHalfExtents.Z; }
                else                      { SampleZ = -Settings.BoxHalfExtents.Z; }
                ReleasePosition.X += SampleX;
                ReleasePosition.Y += SampleY;
                ReleasePosition.Z += SampleZ;
                break;
            }

            case VolumeShapeCategory::DualElectrodeArc:
            {
                const float Ratio   = (AcquiredCount > 1u) ? (static_cast<float>(Offset) / static_cast<float>(AcquiredCount - 1u)) : Generator.NextUnitFloat();
                const float ArcSpan = std::max(0.4f, Settings.RadiusMeters * 2.0f);
                ReleasePosition.X += (Ratio - 0.5f) * ArcSpan;
                ReleasePosition.Z += std::sin(Ratio * Constants::PiFloat) * 0.25f;
                break;
            }
        }

        const ForceFields::SpatialVector3 LaunchDir = Generator.NextConeDirection(Settings.LaunchDirection, Settings.ConeSpreadRad);
        const float Speed                           = Generator.NextRange(Settings.SpeedMinMs, std::max(Settings.SpeedMinMs, Settings.SpeedMaxMs));

        Kin[Slot].PosX        = ReleasePosition.X;
        Kin[Slot].PosY        = ReleasePosition.Y;
        Kin[Slot].PosZ        = ReleasePosition.Z;
        Kin[Slot].InverseMass = 1.0f;
        Kin[Slot].VelX        = LaunchDir.X * Speed;
        Kin[Slot].VelY        = LaunchDir.Y * Speed;
        Kin[Slot].VelZ        = LaunchDir.Z * Speed;
        Kin[Slot].DragScale   = 1.0f;

        Life[Slot].AgeSeconds     = 0.0f;
        Life[Slot].MaxLifeSeconds = Generator.NextRange(Settings.LifeMinSec, std::max(Settings.LifeMinSec, Settings.LifeMaxSec));
        Life[Slot].SeedUint32     = Generator.NextUint32();
        Life[Slot].LivenessMask   = 1u;

        Appear[Slot].RadiusMeters  = Settings.SizeStartMeters;
        Appear[Slot].RollRadians   = Generator.NextRange(0.0f, Constants::TauFloat);
        Appear[Slot].SpinRadPerSec = Generator.NextRange(-Settings.SpinRateRadSec, Settings.SpinRateRadSec);
        Appear[Slot].ColorRampT    = 0.0f;

        if (!Orient.empty())
        {
            const ForceFields::SpatialVector3 SpinAxis = Generator.NextUnitSphere();
            const float SpinMag                        = Generator.NextRange(0.2f, std::max(0.2f, Settings.SpinRateRadSec * 1.5f));
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

    return AcquiredCount;
}

void VolumeParticleSource::ResetSource() noexcept
{
    FractionalAccumulator  = 0.0f;
    BurstTimerSeconds      = 0.0f;
    PendingManualBurst     = 0u;
    InitialBurstDispatched = false;
}

void VolumeParticleSource::TriggerImmediateBurst(uint32_t OverrideCount) noexcept
{
    PendingManualBurst += (OverrideCount > 0u) ? OverrideCount : std::max(1u, Settings.BurstCount);
}

//------------------------------------------------------------------------------------------------------------------------
//                                           2. GRAVITY & BUOYANCY SOLVER
//------------------------------------------------------------------------------------------------------------------------

void GravityBuoyancySolver::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(ChannelLayouts::Kinematics);
}

void GravityBuoyancySolver::EvaluateRange(
    ParticleChannelBank&        Bank,
    uint32_t                    BeginSlot,
    uint32_t                    EndSlot,
    const SimulationTickInputs& Inputs) const
{
    auto Kin = Bank.AccessChannel(ChannelLayouts::Kinematics);
    if (Kin.empty())
    {
        return;
    }

    const float Dt        = Inputs.DeltaTimeSeconds;
    const float NetAccelZ = -StandardGravityMs2 * Settings.GravityScale + Settings.BuoyancyAccelMs2;

    for (uint32_t Slot = BeginSlot; Slot < EndSlot; ++Slot)
    {
        KinematicsChannel& Body = Kin[Slot];
        if (Body.InverseMass <= 0.0f)
        {
            continue;
        }

        Body.VelZ += NetAccelZ * Dt;

        const float EffectiveDrag = std::max(0.0f, Settings.DragCoefficient * Body.DragScale);
        if (EffectiveDrag > 0.0f)
        {
            const float Damp = std::exp(-EffectiveDrag * Dt);
            Body.VelX *= Damp;
            Body.VelY *= Damp;
            Body.VelZ *= Damp;
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                           3. FORCEFIELDS COUPLING SOLVER
//------------------------------------------------------------------------------------------------------------------------

void ForceFieldCouplingSolver::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(ChannelLayouts::Kinematics);
}

void ForceFieldCouplingSolver::EvaluateRange(
    ParticleChannelBank&        Bank,
    uint32_t                    BeginSlot,
    uint32_t                    EndSlot,
    const SimulationTickInputs& Inputs) const
{
    if (Settings.CouplingGain <= 1.0e-5f && Settings.AccelScale <= 1.0e-5f)
    {
        return;
    }
    if (Inputs.ForceAccumulator == nullptr && Inputs.ForceLattice == nullptr)
    {
        return;
    }

    auto Kin    = Bank.AccessChannel(ChannelLayouts::Kinematics);
    auto Orient = Bank.AccessChannel(ChannelLayouts::Orientation);
    if (Kin.empty())
    {
        return;
    }

    const float Dt                 = Inputs.DeltaTimeSeconds;
    const bool  UseLattice         = Settings.PreferLatticeLookup && (Inputs.ForceLattice != nullptr);
    const bool  OrientationPresent = !Orient.empty();

    for (uint32_t Slot = BeginSlot; Slot < EndSlot; ++Slot)
    {
        KinematicsChannel& Body = Kin[Slot];
        if (Body.InverseMass <= 0.0f)
        {
            continue;
        }

        const ForceFields::SpatialVector3 Pos{ Body.PosX, Body.PosY, Body.PosZ };
        const ForceFields::SpatialVector3 Vel{ Body.VelX, Body.VelY, Body.VelZ };

        if (UseLattice)
        {
            const ForceFields::FieldLatticeVoxelSample Voxel =
                Inputs.ForceLattice->SampleTierTrilinear(Settings.LatticeTierIndex, Pos);
            const float Blend = 1.0f - std::exp(-std::max(0.0f, Settings.CouplingGain) * Dt);
            Body.VelX += (Voxel.VelocityX - Body.VelX) * Blend;
            Body.VelY += (Voxel.VelocityY - Body.VelY) * Blend;
            Body.VelZ += (Voxel.VelocityZ - Body.VelZ) * Blend;
        }
        else if (Inputs.ForceAccumulator != nullptr)
        {
            const ForceFields::SpatialForceSample Sample =
                Inputs.ForceAccumulator->QueryForceFieldSample(Pos, Vel, Inputs.TimeSeconds);

            const float Blend = 1.0f - std::exp(-std::max(0.0f, Settings.CouplingGain) * Dt);
            Body.VelX += (Sample.AdvectionVelocity.X - Body.VelX) * Blend;
            Body.VelY += (Sample.AdvectionVelocity.Y - Body.VelY) * Blend;
            Body.VelZ += (Sample.AdvectionVelocity.Z - Body.VelZ) * Blend;

            Body.VelX += Sample.LinearAcceleration.X * Settings.AccelScale * Body.InverseMass * Dt;
            Body.VelY += Sample.LinearAcceleration.Y * Settings.AccelScale * Body.InverseMass * Dt;
            Body.VelZ += Sample.LinearAcceleration.Z * Settings.AccelScale * Body.InverseMass * Dt;

            const float FieldDrag = std::max(0.0f, Sample.DragDecayRate * Settings.DragScale);
            if (FieldDrag > 1.0e-5f)
            {
                const float Damp = std::exp(-FieldDrag * Dt);
                Body.VelX *= Damp;
                Body.VelY *= Damp;
                Body.VelZ *= Damp;
            }

            if (OrientationPresent && Settings.TorqueScale > 1.0e-5f)
            {
                OrientationChannel& Rotation = Orient[Slot];
                Rotation.AngVelX += Sample.AngularTorque.X * Settings.TorqueScale * Dt;
                Rotation.AngVelY += Sample.AngularTorque.Y * Settings.TorqueScale * Dt;
                Rotation.AngVelZ += Sample.AngularTorque.Z * Settings.TorqueScale * Dt;
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                           4. CURL TURBULENCE SOLVER
//------------------------------------------------------------------------------------------------------------------------

void CurlTurbulenceSolver::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(ChannelLayouts::Kinematics);
}

void CurlTurbulenceSolver::EvaluateRange(
    ParticleChannelBank&        Bank,
    uint32_t                    BeginSlot,
    uint32_t                    EndSlot,
    const SimulationTickInputs& Inputs) const
{
    if (Settings.AmplitudeMs <= 1.0e-5f)
    {
        return;
    }

    auto Kin = Bank.AccessChannel(ChannelLayouts::Kinematics);
    if (Kin.empty())
    {
        return;
    }

    const float Dt        = Inputs.DeltaTimeSeconds;
    const float TimePhase = static_cast<float>(Inputs.TimeSeconds) * Settings.TimeScrollSpeed;

    for (uint32_t Slot = BeginSlot; Slot < EndSlot; ++Slot)
    {
        KinematicsChannel& Body = Kin[Slot];
        if (Body.InverseMass <= 0.0f)
        {
            continue;
        }
        const ForceFields::SpatialVector3 Curl =
            EvaluateAnalyticCurl(Body.PosX, Body.PosY, Body.PosZ, Settings.FrequencyPerM, TimePhase);

        Body.VelX += Curl.X * Settings.AmplitudeMs * Dt;
        Body.VelY += Curl.Y * Settings.AmplitudeMs * Dt;
        Body.VelZ += Curl.Z * Settings.AmplitudeMs * Dt;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                            5. AERO FLUTTER SOLVER
//------------------------------------------------------------------------------------------------------------------------

void AeroFlutterSolver::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(ChannelLayouts::Kinematics);
    (void)Bank.RequireChannel(ChannelLayouts::Appearance);
}

void AeroFlutterSolver::EvaluateRange(
    ParticleChannelBank&        Bank,
    uint32_t                    BeginSlot,
    uint32_t                    EndSlot,
    const SimulationTickInputs& Inputs) const
{
    auto Kin    = Bank.AccessChannel(ChannelLayouts::Kinematics);
    auto Appear = Bank.AccessChannel(ChannelLayouts::Appearance);
    auto Life   = Bank.QueryChannel(ChannelLayouts::Lifespan);
    auto Orient = Bank.AccessChannel(ChannelLayouts::Orientation);
    if (Kin.empty() || Appear.empty())
    {
        return;
    }

    const float Dt       = Inputs.DeltaTimeSeconds;
    const float AngDamp  = std::exp(-std::max(0.0f, Settings.AngularDamping) * Dt);
    const float OmegaRad = Constants::TauFloat * Settings.FlutterFrequencyHz;

    for (uint32_t Slot = BeginSlot; Slot < EndSlot; ++Slot)
    {
        const uint32_t Seed = !Life.empty() ? Life[Slot].SeedUint32 : (Slot * 2654435761u + 17u);
        const float Phase   = static_cast<float>(Seed & 0xFFFFu) * (Constants::TauFloat / 65536.0f)
                            + static_cast<float>(Inputs.TimeSeconds) * OmegaRad;

        if (Kin[Slot].InverseMass > 0.0f && Settings.FlutterAmplitudeMs2 > 1.0e-5f)
        {
            Kin[Slot].VelX += std::sin(Phase) * Settings.FlutterAmplitudeMs2 * Dt;
            Kin[Slot].VelY += std::cos(Phase * 0.77f) * Settings.FlutterAmplitudeMs2 * Dt;
        }

        Appear[Slot].RollRadians   += Appear[Slot].SpinRadPerSec * Dt;
        Appear[Slot].SpinRadPerSec *= AngDamp;

        if (!Orient.empty())
        {
            OrientationChannel& Rotation = Orient[Slot];
            Rotation.AngVelX *= AngDamp;
            Rotation.AngVelY *= AngDamp;
            Rotation.AngVelZ *= AngDamp;

            // First-order quaternion kinematic derivative: dq/dt = 0.5 * (0, w) * q
            const float HalfDt = 0.5f * Dt;
            const float Dx     = HalfDt * ( Rotation.AngVelX * Rotation.QuatW + Rotation.AngVelY * Rotation.QuatZ - Rotation.AngVelZ * Rotation.QuatY);
            const float Dy     = HalfDt * (-Rotation.AngVelX * Rotation.QuatZ + Rotation.AngVelY * Rotation.QuatW + Rotation.AngVelZ * Rotation.QuatX);
            const float Dz     = HalfDt * ( Rotation.AngVelX * Rotation.QuatY - Rotation.AngVelY * Rotation.QuatX + Rotation.AngVelZ * Rotation.QuatW);
            const float Dw     = HalfDt * (-Rotation.AngVelX * Rotation.QuatX - Rotation.AngVelY * Rotation.QuatY - Rotation.AngVelZ * Rotation.QuatZ);

            Rotation.QuatX += Dx;
            Rotation.QuatY += Dy;
            Rotation.QuatZ += Dz;
            Rotation.QuatW += Dw;

            const float Length = std::sqrt(
                Rotation.QuatX * Rotation.QuatX + Rotation.QuatY * Rotation.QuatY
                + Rotation.QuatZ * Rotation.QuatZ + Rotation.QuatW * Rotation.QuatW);
            if (Length > 1.0e-6f)
            {
                const float InvLength = 1.0f / Length;
                Rotation.QuatX *= InvLength;
                Rotation.QuatY *= InvLength;
                Rotation.QuatZ *= InvLength;
                Rotation.QuatW *= InvLength;
            }
            else
            {
                Rotation.QuatX = 0.0f;
                Rotation.QuatY = 0.0f;
                Rotation.QuatZ = 0.0f;
                Rotation.QuatW = 1.0f;
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                           6. APPEARANCE CURVE SOLVER
//------------------------------------------------------------------------------------------------------------------------

void AppearanceCurveSolver::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(ChannelLayouts::Appearance);
}

void AppearanceCurveSolver::EvaluateRange(
    ParticleChannelBank&        Bank,
    uint32_t                    BeginSlot,
    uint32_t                    EndSlot,
    const SimulationTickInputs& /*Inputs*/) const
{
    auto Appear = Bank.AccessChannel(ChannelLayouts::Appearance);
    auto Life   = Bank.QueryChannel(ChannelLayouts::Lifespan);
    auto Kin    = Bank.QueryChannel(ChannelLayouts::Kinematics);
    if (Appear.empty())
    {
        return;
    }

    for (uint32_t Slot = BeginSlot; Slot < EndSlot; ++Slot)
    {
        float LifeRatio = 0.0f;
        if (!Life.empty() && Life[Slot].MaxLifeSeconds > 1.0e-5f)
        {
            LifeRatio = std::clamp(Life[Slot].AgeSeconds / Life[Slot].MaxLifeSeconds, 0.0f, 1.0f);
        }

        float RawCoord = LifeRatio;
        switch (Settings.Driver)
        {
            case ColorSampleDriver::LifetimeRatio:
                RawCoord = LifeRatio;
                break;

            case ColorSampleDriver::SpeedMagnitude:
                if (!Kin.empty())
                {
                    const float Speed = std::sqrt(
                        Kin[Slot].VelX * Kin[Slot].VelX
                        + Kin[Slot].VelY * Kin[Slot].VelY
                        + Kin[Slot].VelZ * Kin[Slot].VelZ);
                    RawCoord = Speed / std::max(0.1f, Settings.SpeedNormMaxMs);
                }
                break;

            case ColorSampleDriver::HeightAltitude:
                if (!Kin.empty())
                {
                    RawCoord = Kin[Slot].PosZ / std::max(0.1f, Settings.HeightNormMaxM);
                }
                break;

            case ColorSampleDriver::RandomPerSlot:
                if (!Life.empty())
                {
                    RawCoord = static_cast<float>(Life[Slot].SeedUint32 & 0xFFFFu) * (1.0f / 65536.0f);
                }
                break;
        }

        const float RampCoord = EvaluateRampWrap(RawCoord + Settings.PhaseOffset, Settings.WrapMode);
        Appear[Slot].ColorRampT = RampCoord;

        if (RampCoord <= 0.5f)
        {
            const float Blend = RampCoord * 2.0f;
            Appear[Slot].ColorR = Settings.ColorA[0] + (Settings.ColorC[0] - Settings.ColorA[0]) * Blend;
            Appear[Slot].ColorG = Settings.ColorA[1] + (Settings.ColorC[1] - Settings.ColorA[1]) * Blend;
            Appear[Slot].ColorB = Settings.ColorA[2] + (Settings.ColorC[2] - Settings.ColorA[2]) * Blend;
            Appear[Slot].ColorA = Settings.ColorA[3] + (Settings.ColorC[3] - Settings.ColorA[3]) * Blend;
        }
        else
        {
            const float Blend = (RampCoord - 0.5f) * 2.0f;
            Appear[Slot].ColorR = Settings.ColorC[0] + (Settings.ColorB[0] - Settings.ColorC[0]) * Blend;
            Appear[Slot].ColorG = Settings.ColorC[1] + (Settings.ColorB[1] - Settings.ColorC[1]) * Blend;
            Appear[Slot].ColorB = Settings.ColorC[2] + (Settings.ColorB[2] - Settings.ColorC[2]) * Blend;
            Appear[Slot].ColorA = Settings.ColorC[3] + (Settings.ColorB[3] - Settings.ColorC[3]) * Blend;
        }

        Appear[Slot].RadiusMeters =
            (Settings.SizeStartMeters + (Settings.SizeEndMeters - Settings.SizeStartMeters) * LifeRatio) * Settings.SizeMultiplier;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                              7. MODULAR COLLIDERS
//------------------------------------------------------------------------------------------------------------------------

void GroundPlaneCollider::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(ChannelLayouts::Kinematics);
}

void GroundPlaneCollider::EnforceRange(
    ParticleChannelBank&        Bank,
    uint32_t                    BeginSlot,
    uint32_t                    EndSlot,
    const SimulationTickInputs& /*Inputs*/) const
{
    auto Kin = Bank.AccessChannel(ChannelLayouts::Kinematics);
    if (Kin.empty())
    {
        return;
    }

    const float TangentialKeep = std::clamp(1.0f - Settings.SlidingFriction, 0.0f, 1.0f);
    for (uint32_t Slot = BeginSlot; Slot < EndSlot; ++Slot)
    {
        KinematicsChannel& Body = Kin[Slot];
        if (Body.InverseMass <= 0.0f)
        {
            continue;
        }
        if (Body.PosZ < Settings.FloorZMeters)
        {
            Body.PosZ = Settings.FloorZMeters;
            if (Body.VelZ < 0.0f)
            {
                Body.VelZ = -Body.VelZ * Settings.Restitution;
                if (std::abs(Body.VelZ) < 0.08f)
                {
                    Body.VelZ = 0.0f;
                }
            }
            Body.VelX *= TangentialKeep;
            Body.VelY *= TangentialKeep;
        }
    }
}

void SphereContactCollider::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(ChannelLayouts::Kinematics);
}

void SphereContactCollider::EnforceRange(
    ParticleChannelBank&        Bank,
    uint32_t                    BeginSlot,
    uint32_t                    EndSlot,
    const SimulationTickInputs& Inputs) const
{
    if (Inputs.SphereColliders.empty())
    {
        return;
    }
    auto Kin = Bank.AccessChannel(ChannelLayouts::Kinematics);
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
            KinematicsChannel& Body = Kin[Slot];
            if (Body.InverseMass <= 0.0f)
            {
                continue;
            }
            const float Dx     = Body.PosX - Sphere.CentreMeters.X;
            const float Dy     = Body.PosY - Sphere.CentreMeters.Y;
            const float Dz     = Body.PosZ - Sphere.CentreMeters.Z;
            const float DistSq = Dx * Dx + Dy * Dy + Dz * Dz;
            if (DistSq < RadSq)
            {
                const float Dist = std::sqrt(std::max(DistSq, 1.0e-10f));
                const float Nx   = (DistSq > 1.0e-10f) ? (Dx / Dist) : 0.0f;
                const float Ny   = (DistSq > 1.0e-10f) ? (Dy / Dist) : 0.0f;
                const float Nz   = (DistSq > 1.0e-10f) ? (Dz / Dist) : 1.0f;

                Body.PosX = Sphere.CentreMeters.X + Nx * Sphere.RadiusMeters;
                Body.PosY = Sphere.CentreMeters.Y + Ny * Sphere.RadiusMeters;
                Body.PosZ = Sphere.CentreMeters.Z + Nz * Sphere.RadiusMeters;

                const float RelVx = Body.VelX - Sphere.VelocityMs.X;
                const float RelVy = Body.VelY - Sphere.VelocityMs.Y;
                const float RelVz = Body.VelZ - Sphere.VelocityMs.Z;
                const float RelVn = RelVx * Nx + RelVy * Ny + RelVz * Nz;
                if (RelVn < 0.0f)
                {
                    const float Impulse = -(1.0f + Sphere.Restitution) * RelVn * ImpulseGain;
                    Body.VelX += Nx * Impulse;
                    Body.VelY += Ny * Impulse;
                    Body.VelZ += Nz * Impulse;
                }
            }
        }
    }
}

void SignedDistanceVolumeCollider::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(ChannelLayouts::Kinematics);
}

void SignedDistanceVolumeCollider::EnforceRange(
    ParticleChannelBank&        Bank,
    uint32_t                    BeginSlot,
    uint32_t                    EndSlot,
    const SimulationTickInputs& Inputs) const
{
    if (Inputs.WorldDistanceField == nullptr)
    {
        return;
    }

    auto Kin = Bank.AccessChannel(ChannelLayouts::Kinematics);
    if (Kin.empty())
    {
        return;
    }

    const float TangentialKeep = std::clamp(1.0f - Settings.SurfaceFriction, 0.0f, 1.0f);
    for (uint32_t Slot = BeginSlot; Slot < EndSlot; ++Slot)
    {
        KinematicsChannel& Body = Kin[Slot];
        if (Body.InverseMass <= 0.0f)
        {
            continue;
        }

        const Mathematics::Axis3D WorldPos{ Body.PosX, Body.PosY, Body.PosZ };
        const float SignedDist = Inputs.WorldDistanceField->SampleSignedDistance(WorldPos);
        if (SignedDist < Settings.ContactMarginMeters)
        {
            const Mathematics::Axis3D Normal = Inputs.WorldDistanceField->EvaluateSurfaceGradient(WorldPos);
            const float PushOut              = Settings.ContactMarginMeters - SignedDist;
            Body.PosX += Normal.X * PushOut;
            Body.PosY += Normal.Y * PushOut;
            Body.PosZ += Normal.Z * PushOut;

            const float NormalVel = Body.VelX * Normal.X + Body.VelY * Normal.Y + Body.VelZ * Normal.Z;
            const float TanVelX   = Body.VelX - Normal.X * NormalVel;
            const float TanVelY   = Body.VelY - Normal.Y * NormalVel;
            const float TanVelZ   = Body.VelZ - Normal.Z * NormalVel;

            const float OutNormalVel = (NormalVel < 0.0f) ? (-NormalVel * Settings.Restitution) : NormalVel;
            Body.VelX = TanVelX * TangentialKeep + Normal.X * OutNormalVel;
            Body.VelY = TanVelY * TangentialKeep + Normal.Y * OutNormalVel;
            Body.VelZ = TanVelZ * TangentialKeep + Normal.Z * OutNormalVel;
        }
        else if (Settings.AdhesionStrength > 1.0e-5f && SignedDist < Settings.ContactMarginMeters * 6.0f)
        {
            const Mathematics::Axis3D Normal = Inputs.WorldDistanceField->EvaluateSurfaceGradient(WorldPos);
            const float Pull                 = Settings.AdhesionStrength * Inputs.DeltaTimeSeconds;
            Body.VelX -= Normal.X * Pull;
            Body.VelY -= Normal.Y * Pull;
            Body.VelZ -= Normal.Z * Pull;
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                             8. MODULAR TIME INTEGRATORS
//------------------------------------------------------------------------------------------------------------------------

void SemiImplicitEulerIntegrator::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(ChannelLayouts::Kinematics);
}

void SemiImplicitEulerIntegrator::AdvanceRange(
    ParticleChannelBank&        Bank,
    uint32_t                    BeginSlot,
    uint32_t                    EndSlot,
    const SimulationTickInputs& Inputs) const
{
    auto Kin = Bank.AccessChannel(ChannelLayouts::Kinematics);
    if (Kin.empty())
    {
        return;
    }

    const float Dt = Inputs.DeltaTimeSeconds;
    for (uint32_t Slot = BeginSlot; Slot < EndSlot; ++Slot)
    {
        KinematicsChannel& Body = Kin[Slot];
        if (Body.InverseMass <= 0.0f)
        {
            continue;
        }
        Body.PosX += Body.VelX * Dt;
        Body.PosY += Body.VelY * Dt;
        Body.PosZ += Body.VelZ * Dt;
    }
}

void XpbdVerletIntegrator::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(ChannelLayouts::Kinematics);
    (void)Bank.RequireChannel(ChannelLayouts::StrandAnchor);
}

void XpbdVerletIntegrator::AdvanceRange(
    ParticleChannelBank&        Bank,
    uint32_t                    BeginSlot,
    uint32_t                    EndSlot,
    const SimulationTickInputs& Inputs) const
{
    auto Kin    = Bank.AccessChannel(ChannelLayouts::Kinematics);
    auto Anchor = Bank.AccessChannel(ChannelLayouts::StrandAnchor);
    if (Kin.empty() || Anchor.empty())
    {
        return;
    }

    const float Dt = Inputs.DeltaTimeSeconds;
    for (uint32_t Slot = BeginSlot; Slot < EndSlot; ++Slot)
    {
        KinematicsChannel&   Body = Kin[Slot];
        StrandAnchorChannel& Pin  = Anchor[Slot];
        Pin.PrevPosX              = Body.PosX;
        Pin.PrevPosY              = Body.PosY;
        Pin.PrevPosZ              = Body.PosZ;
        if (Body.InverseMass <= 0.0f || Pin.PinWeight >= 0.999f)
        {
            Body.PosX = Pin.AnchorX;
            Body.PosY = Pin.AnchorY;
            Body.PosZ = Pin.AnchorZ;
            Body.VelX = 0.0f;
            Body.VelY = 0.0f;
            Body.VelZ = 0.0f;
            continue;
        }
        Body.PosX += Body.VelX * Dt;
        Body.PosY += Body.VelY * Dt;
        Body.PosZ += Body.VelZ * Dt;
    }
}

void MidpointRk2Integrator::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(ChannelLayouts::Kinematics);
}

void MidpointRk2Integrator::AdvanceRange(
    ParticleChannelBank&        Bank,
    uint32_t                    BeginSlot,
    uint32_t                    EndSlot,
    const SimulationTickInputs& Inputs) const
{
    auto Kin = Bank.AccessChannel(ChannelLayouts::Kinematics);
    if (Kin.empty())
    {
        return;
    }

    const float Dt     = Inputs.DeltaTimeSeconds;
    const float HalfDt = 0.5f * Dt;
    for (uint32_t Slot = BeginSlot; Slot < EndSlot; ++Slot)
    {
        KinematicsChannel& Body = Kin[Slot];
        if (Body.InverseMass <= 0.0f)
        {
            continue;
        }

        if (Inputs.ForceAccumulator != nullptr)
        {
            const ForceFields::SpatialVector3 MidPos{
                Body.PosX + Body.VelX * HalfDt,
                Body.PosY + Body.VelY * HalfDt,
                Body.PosZ + Body.VelZ * HalfDt,
            };
            const ForceFields::SpatialForceSample MidSample =
                Inputs.ForceAccumulator->QueryForceFieldSample(MidPos, { Body.VelX, Body.VelY, Body.VelZ }, Inputs.TimeSeconds + HalfDt);
            const float MidVx = Body.VelX + MidSample.LinearAcceleration.X * HalfDt * Body.InverseMass;
            const float MidVy = Body.VelY + MidSample.LinearAcceleration.Y * HalfDt * Body.InverseMass;
            const float MidVz = Body.VelZ + MidSample.LinearAcceleration.Z * HalfDt * Body.InverseMass;
            Body.PosX += MidVx * Dt;
            Body.PosY += MidVy * Dt;
            Body.PosZ += MidVz * Dt;
        }
        else
        {
            Body.PosX += Body.VelX * Dt;
            Body.PosY += Body.VelY * Dt;
            Body.PosZ += Body.VelZ * Dt;
        }
    }
}

} // namespace Frontier::SimulationSpace::Particles
