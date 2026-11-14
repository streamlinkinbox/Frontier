/*============================================================================================================================================
                                                        PARTICLEDOMAINSOLVERS.CPP
============================================================================================================================================*/
// 🧩 Implementations of all modular domain release rules and solvers (XPBD Strands, Rosensweig Ferrofluid, Fireworks, Boids, Fracture, MoGraph, Shield, Space)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Particles/ParticleDomainSolvers.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier::SimulationSpace::Particles
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     INTERNAL ROUTINES
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// Minimal 5x7 uppercase vector-stroke points for Sky-Writing Fireworks text (0..9, A..Z)
void AppendSkyCharacterPoints(
    char                                      Ch,
    float                                     OffsetX,
    float                                     ScaleMeters,
    std::vector<ForceFields::SpatialVector3>& OutPoints)
{
    const char Upper = (Ch >= 'a' && Ch <= 'z') ? static_cast<char>(Ch - 'a' + 'A') : Ch;
    auto PushStroke = [&](float X0, float Z0, float X1, float Z1, int32_t Samples)
    {
        for (int32_t I = 0; I <= Samples; ++I)
        {
            const float T = static_cast<float>(I) / static_cast<float>(std::max(1, Samples));
            OutPoints.push_back({
                OffsetX + (X0 + (X1 - X0) * T) * ScaleMeters,
                0.0f,
                (Z0 + (Z1 - Z0) * T) * ScaleMeters,
            });
        }
    };

    switch (Upper)
    {
        case '0':
        case 'O':
            PushStroke(0.0f, 0.0f, 0.7f, 0.0f, 2);
            PushStroke(0.7f, 0.0f, 0.7f, 1.0f, 3);
            PushStroke(0.7f, 1.0f, 0.0f, 1.0f, 2);
            PushStroke(0.0f, 1.0f, 0.0f, 0.0f, 3);
            break;
        case '2':
            PushStroke(0.0f, 1.0f, 0.7f, 1.0f, 2);
            PushStroke(0.7f, 1.0f, 0.7f, 0.5f, 2);
            PushStroke(0.7f, 0.5f, 0.0f, 0.5f, 2);
            PushStroke(0.0f, 0.5f, 0.0f, 0.0f, 2);
            PushStroke(0.0f, 0.0f, 0.7f, 0.0f, 2);
            break;
        case '6':
            PushStroke(0.7f, 1.0f, 0.0f, 1.0f, 2);
            PushStroke(0.0f, 1.0f, 0.0f, 0.0f, 3);
            PushStroke(0.0f, 0.0f, 0.7f, 0.0f, 2);
            PushStroke(0.7f, 0.0f, 0.7f, 0.5f, 2);
            PushStroke(0.7f, 0.5f, 0.0f, 0.5f, 2);
            break;
        case 'F':
            PushStroke(0.0f, 0.0f, 0.0f, 1.0f, 3);
            PushStroke(0.0f, 1.0f, 0.7f, 1.0f, 2);
            PushStroke(0.0f, 0.5f, 0.55f, 0.5f, 2);
            break;
        case 'R':
            PushStroke(0.0f, 0.0f, 0.0f, 1.0f, 3);
            PushStroke(0.0f, 1.0f, 0.65f, 1.0f, 2);
            PushStroke(0.65f, 1.0f, 0.65f, 0.55f, 2);
            PushStroke(0.65f, 0.55f, 0.0f, 0.55f, 2);
            PushStroke(0.25f, 0.55f, 0.7f, 0.0f, 2);
            break;
        case 'N':
            PushStroke(0.0f, 0.0f, 0.0f, 1.0f, 3);
            PushStroke(0.0f, 1.0f, 0.7f, 0.0f, 3);
            PushStroke(0.7f, 0.0f, 0.7f, 1.0f, 3);
            break;
        case 'T':
            PushStroke(0.0f, 1.0f, 0.7f, 1.0f, 2);
            PushStroke(0.35f, 1.0f, 0.35f, 0.0f, 3);
            break;
        case 'I':
        case '1':
            PushStroke(0.35f, 0.0f, 0.35f, 1.0f, 3);
            break;
        case 'E':
            PushStroke(0.0f, 0.0f, 0.0f, 1.0f, 3);
            PushStroke(0.0f, 1.0f, 0.7f, 1.0f, 2);
            PushStroke(0.0f, 0.5f, 0.55f, 0.5f, 2);
            PushStroke(0.0f, 0.0f, 0.7f, 0.0f, 2);
            break;
        default:
            // Compact star-diamond fallback for any other symbol
            PushStroke(0.35f, 0.0f, 0.7f, 0.5f, 2);
            PushStroke(0.7f, 0.5f, 0.35f, 1.0f, 2);
            PushStroke(0.35f, 1.0f, 0.0f, 0.5f, 2);
            PushStroke(0.0f, 0.5f, 0.35f, 0.0f, 2);
            break;
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                      1. XPBD STRANDS, CLOTH, VINES, FLORA & TREES
//------------------------------------------------------------------------------------------------------------------------

void StrandGridSource::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(ChannelLayouts::Kinematics);
    (void)Bank.RequireChannel(ChannelLayouts::Appearance);
    (void)Bank.RequireChannel(ChannelLayouts::StrandAnchor);
    (void)Bank.RequireChannel(ChannelLayouts::StrandConnectivity);
}

uint32_t StrandGridSource::ReleaseParticles(
    ParticleChannelBank&        Bank,
    const SimulationTickInputs& /*Inputs*/,
    ParticleRandomGenerator&                Generator)
{
    if (Initialized)
    {
        return 0u;
    }
    Initialized = true;

    const uint32_t Strands   = std::max(1u, Settings.StrandCount);
    const uint32_t Segments  = std::max(2u, Settings.SegmentsPerStrand);
    const uint32_t TotalNeed = std::min(Bank.QueryCapacity(), Strands * Segments);
    if (TotalNeed == 0u)
    {
        return 0u;
    }

    Bank.ClearActiveSlots();
    uint32_t BeginSlot     = 0u;
    const uint32_t AcquiredCount = Bank.AcquireSlots(TotalNeed, BeginSlot);

    auto Kin    = Bank.AccessChannel(ChannelLayouts::Kinematics);
    auto Appear = Bank.AccessChannel(ChannelLayouts::Appearance);
    auto Anchor = Bank.AccessChannel(ChannelLayouts::StrandAnchor);
    auto Conn   = Bank.AccessChannel(ChannelLayouts::StrandConnectivity);

    const float SegLen = Settings.StrandLengthMeters / static_cast<float>(std::max(1u, Segments - 1u));

    for (uint32_t Idx = 0u; Idx < AcquiredCount; ++Idx)
    {
        const uint32_t Slot   = BeginSlot + Idx;
        const uint32_t Strand = Idx / Segments;
        const uint32_t Seg    = Idx % Segments;
        const float    U      = (Strands > 1u) ? (static_cast<float>(Strand) / static_cast<float>(Strands - 1u)) : 0.5f;
        const float    V      = (Segments > 1u) ? (static_cast<float>(Seg) / static_cast<float>(Segments - 1u)) : 0.0f;

        float Px = Settings.OriginMeters.X;
        float Py = Settings.OriginMeters.Y;
        float Pz = Settings.OriginMeters.Z;
        bool  Pinned = (Seg == 0u);
        uint32_t CrossLink = 0xFFFFFFFFu;

        switch (Settings.Pattern)
        {
            case StrandLayoutPattern::ClothBannerGrid:
                Px += V * Settings.StrandLengthMeters;
                Py += (U - 0.5f) * 0.05f;
                Pz += (U - 0.5f) * Settings.WidthMeters - std::sin(V * Constants::PiFloat) * Settings.CatenarySagMeters;
                if (Strand > 0u)
                {
                    CrossLink = BeginSlot + (Strand - 1u) * Segments + Seg;
                }
                break;

            case StrandLayoutPattern::HangingCurtain:
                Px += (U - 0.5f) * Settings.WidthMeters;
                Py += Generator.NextRange(-0.08f, 0.08f);
                Pz -= V * Settings.StrandLengthMeters;
                break;

            case StrandLayoutPattern::UpwardFloraField:
            {
                const float RingAngle = U * Constants::TauFloat * 3.1f + Generator.NextRange(-0.2f, 0.2f);
                const float RingRad   = std::sqrt(U) * Settings.WidthMeters * 0.5f;
                Px += std::cos(RingAngle) * RingRad + V * Settings.CatenarySagMeters * 0.45f;
                Py += std::sin(RingAngle) * RingRad;
                Pz += V * Settings.StrandLengthMeters;
                break;
            }

            case StrandLayoutPattern::RadialWeb:
            {
                const float SpokeAng = U * Constants::TauFloat;
                const float Rad      = 0.15f + V * Settings.StrandLengthMeters;
                Px += std::cos(SpokeAng) * Rad;
                Py += std::sin(SpokeAng) * Rad;
                Pz -= std::sin(V * Constants::PiFloat) * Settings.CatenarySagMeters;
                Pinned = (Seg == 0u || Seg == Segments - 1u);
                if (Strand > 0u)
                {
                    CrossLink = BeginSlot + (Strand - 1u) * Segments + Seg;
                }
                break;
            }

            case StrandLayoutPattern::CanopyTree:
            {
                const float LimbAng = U * Constants::TauFloat;
                const float Spread  = std::pow(V, 1.35f) * Settings.WidthMeters * 0.65f;
                Px += std::cos(LimbAng) * Spread;
                Py += std::sin(LimbAng) * Spread;
                Pz += V * Settings.StrandLengthMeters;
                Pinned = (Seg <= 1u);
                break;
            }
        }

        Kin[Slot].PosX        = Px;
        Kin[Slot].PosY        = Py;
        Kin[Slot].PosZ        = Pz;
        Kin[Slot].InverseMass = Pinned ? 0.0f : 1.0f;
        Kin[Slot].VelX        = 0.0f;
        Kin[Slot].VelY        = 0.0f;
        Kin[Slot].VelZ        = 0.0f;

        Anchor[Slot].PrevPosX         = Px;
        Anchor[Slot].PrevPosY         = Py;
        Anchor[Slot].PrevPosZ         = Pz;
        Anchor[Slot].RestLengthMeters = std::max(0.02f, SegLen);
        Anchor[Slot].AnchorX          = Px;
        Anchor[Slot].AnchorY          = Py;
        Anchor[Slot].AnchorZ          = Pz;
        Anchor[Slot].PinWeight        = Pinned ? 1.0f : 0.0f;

        Conn[Slot].StrandIndex   = Strand;
        Conn[Slot].SegmentIndex  = Seg;
        Conn[Slot].LinkPrevSlot  = (Seg > 0u) ? (Slot - 1u) : 0xFFFFFFFFu;
        Conn[Slot].LinkCrossSlot = CrossLink;

        Appear[Slot].RadiusMeters = Settings.RadiusMeters * (1.0f - 0.45f * V);
        Appear[Slot].ColorR       = 0.72f - 0.18f * V;
        Appear[Slot].ColorG       = 0.84f - 0.12f * V;
        Appear[Slot].ColorB       = 0.56f;
        Appear[Slot].ColorA       = 1.0f;
    }

    return AcquiredCount;
}

void XpbdStrandSolver::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(ChannelLayouts::Kinematics);
    (void)Bank.RequireChannel(ChannelLayouts::StrandAnchor);
    (void)Bank.RequireChannel(ChannelLayouts::StrandConnectivity);
}

void XpbdStrandSolver::EvaluateRange(
    ParticleChannelBank&        Bank,
    uint32_t                    BeginSlot,
    uint32_t                    EndSlot,
    const SimulationTickInputs& Inputs) const
{
    auto Kin    = Bank.AccessChannel(ChannelLayouts::Kinematics);
    auto Anchor = Bank.AccessChannel(ChannelLayouts::StrandAnchor);
    auto Conn   = Bank.QueryChannel(ChannelLayouts::StrandConnectivity);
    if (Kin.empty() || Anchor.empty() || Conn.empty())
    {
        return;
    }

    const float Dt        = Inputs.DeltaTimeSeconds;
    const float Omega     = Constants::TauFloat * Settings.WaveSpeedHz;
    const float StiffPull = std::clamp(Settings.ElasticStiffness * 1.4f, 0.1f, 6.0f);
    const float Damp      = std::exp(-std::max(0.0f, Settings.Damping) * Dt);

    for (uint32_t Slot = BeginSlot; Slot < EndSlot; ++Slot)
    {
        KinematicsChannel& P = Kin[Slot];
        if (P.InverseMass <= 0.0f)
        {
            continue;
        }

        // Travelling aeroelastic wave along strand
        const float Phase = static_cast<float>(Inputs.TimeSeconds) * Omega
                          - static_cast<float>(Conn[Slot].SegmentIndex) * 0.55f
                          - static_cast<float>(Conn[Slot].StrandIndex) * 0.35f;
        P.VelY += std::sin(Phase) * Settings.WaveAmplitudeMs2 * Dt;

        // Gentle spring restore toward rest posture for upright flora & trees
        const StrandAnchorChannel& A = Anchor[Slot];
        if (A.PinWeight > 0.01f && A.PinWeight < 0.999f)
        {
            P.VelX += (A.AnchorX - P.PosX) * StiffPull * A.PinWeight * Dt;
            P.VelY += (A.AnchorY - P.PosY) * StiffPull * A.PinWeight * Dt;
            P.VelZ += (A.AnchorZ - P.PosZ) * StiffPull * A.PinWeight * Dt;
        }

        P.VelX *= Damp;
        P.VelY *= Damp;
        P.VelZ *= Damp;
    }
}

void XpbdStrandSolver::FinalizeTick(
    ParticleChannelBank&        Bank,
    const SimulationTickInputs& Inputs,
    ParticleRandomGenerator&                /*Generator*/)
{
    auto Kin    = Bank.AccessChannel(ChannelLayouts::Kinematics);
    auto Anchor = Bank.AccessChannel(ChannelLayouts::StrandAnchor);
    auto Conn   = Bank.AccessChannel(ChannelLayouts::StrandConnectivity);
    if (Kin.empty() || Anchor.empty() || Conn.empty())
    {
        return;
    }

    const uint32_t Active   = Bank.QueryActiveCount();
    const float    Dt       = std::max(1.0e-4f, Inputs.DeltaTimeSeconds);
    const float    InvDt    = 1.0f / Dt;
    const float    Stiff01  = std::clamp(Settings.ElasticStiffness * 0.45f, 0.15f, 0.96f);
    const uint32_t Iters    = std::clamp(Settings.SubIterations, 1u, 12u);

    auto SolveDistancePair = [&](uint32_t SlotA, uint32_t SlotB, float RestLen, float Alpha, uint32_t& LinkToTear)
    {
        if (SlotB >= Active)
        {
            return;
        }
        KinematicsChannel& A = Kin[SlotA];
        KinematicsChannel& B = Kin[SlotB];
        const float Wa       = A.InverseMass;
        const float Wb       = B.InverseMass;
        const float WSum     = Wa + Wb;
        if (WSum <= 1.0e-6f)
        {
            return;
        }

        const float Dx   = A.PosX - B.PosX;
        const float Dy   = A.PosY - B.PosY;
        const float Dz   = A.PosZ - B.PosZ;
        const float Dist = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
        if (Dist <= 1.0e-6f)
        {
            return;
        }

        const float Stretch = Dist - RestLen;
        if (Settings.TearSpeedMs > 0.1f && Stretch > RestLen * (0.45f + Settings.TearSpeedMs * 0.08f))
        {
            LinkToTear = 0xFFFFFFFFu;
            return;
        }

        const float Corr = (Stretch / Dist) * Alpha / WSum;
        A.PosX -= Dx * Corr * Wa;
        A.PosY -= Dy * Corr * Wa;
        A.PosZ -= Dz * Corr * Wa;
        B.PosX += Dx * Corr * Wb;
        B.PosY += Dy * Corr * Wb;
        B.PosZ += Dz * Corr * Wb;
    };

    for (uint32_t Iter = 0u; Iter < Iters; ++Iter)
    {
        for (uint32_t Slot = 0u; Slot < Active; ++Slot)
        {
            if (Conn[Slot].LinkPrevSlot != 0xFFFFFFFFu)
            {
                SolveDistancePair(Slot, Conn[Slot].LinkPrevSlot, Anchor[Slot].RestLengthMeters, Stiff01, Conn[Slot].LinkPrevSlot);
            }
            if (Conn[Slot].LinkCrossSlot != 0xFFFFFFFFu)
            {
                SolveDistancePair(Slot, Conn[Slot].LinkCrossSlot, Anchor[Slot].RestLengthMeters * 1.15f, Stiff01 * 0.65f, Conn[Slot].LinkCrossSlot);
            }
        }
    }

    // Derive XPBD velocity cleanly from constrained displacement
    for (uint32_t Slot = 0u; Slot < Active; ++Slot)
    {
        if (Kin[Slot].InverseMass <= 0.0f)
        {
            Kin[Slot].PosX = Anchor[Slot].AnchorX;
            Kin[Slot].PosY = Anchor[Slot].AnchorY;
            Kin[Slot].PosZ = Anchor[Slot].AnchorZ;
            Kin[Slot].VelX = 0.0f;
            Kin[Slot].VelY = 0.0f;
            Kin[Slot].VelZ = 0.0f;
            continue;
        }
        Kin[Slot].VelX = (Kin[Slot].PosX - Anchor[Slot].PrevPosX) * InvDt;
        Kin[Slot].VelY = (Kin[Slot].PosY - Anchor[Slot].PrevPosY) * InvDt;
        Kin[Slot].VelZ = (Kin[Slot].PosZ - Anchor[Slot].PrevPosZ) * InvDt;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                           2. ROSENSWEIG MAGNETIC FERROFLUID
//------------------------------------------------------------------------------------------------------------------------

void FerrofluidSurfaceSource::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(ChannelLayouts::Kinematics);
    (void)Bank.RequireChannel(ChannelLayouts::Appearance);
    (void)Bank.RequireChannel(ChannelLayouts::MagneticCusp);
}

uint32_t FerrofluidSurfaceSource::ReleaseParticles(
    ParticleChannelBank&        Bank,
    const SimulationTickInputs& /*Inputs*/,
    ParticleRandomGenerator&                /*Generator*/)
{
    if (Initialized)
    {
        return 0u;
    }
    Initialized = true;

    const uint32_t TotalNeed = std::min(Bank.QueryCapacity(), std::max(16u, Settings.SpikeCount));
    Bank.ClearActiveSlots();
    uint32_t BeginSlot     = 0u;
    const uint32_t AcquiredCount = Bank.AcquireSlots(TotalNeed, BeginSlot);

    auto Kin    = Bank.AccessChannel(ChannelLayouts::Kinematics);
    auto Appear = Bank.AccessChannel(ChannelLayouts::Appearance);
    auto Cusp   = Bank.AccessChannel(ChannelLayouts::MagneticCusp);

    const float GoldenAngle = 2.39996323f; // [rad] - Phyllotaxis spacing for uniform Rosensweig hexagonal lattice
    for (uint32_t I = 0u; I < AcquiredCount; ++I)
    {
        const uint32_t Slot = BeginSlot + I;
        const float    Frac = (static_cast<float>(I) + 0.5f) / static_cast<float>(AcquiredCount);
        const float    Ang  = static_cast<float>(I) * GoldenAngle;

        float Ax = Settings.OriginMeters.X;
        float Ay = Settings.OriginMeters.Y;
        float Az = Settings.OriginMeters.Z;
        float Nx = 0.0f;
        float Ny = 0.0f;
        float Nz = 1.0f;

        if (Settings.VesselForm == 2u)
        {
            // Levitated spherical ferrofluid orb
            const float CosPol = 1.0f - 2.0f * Frac;
            const float SinPol = std::sqrt(std::max(0.0f, 1.0f - CosPol * CosPol));
            Nx = SinPol * std::cos(Ang);
            Ny = SinPol * std::sin(Ang);
            Nz = CosPol;
            Ax += Nx * Settings.VesselRadiusMeters * 0.72f;
            Ay += Ny * Settings.VesselRadiusMeters * 0.72f;
            Az += Nz * Settings.VesselRadiusMeters * 0.72f;
        }
        else
        {
            // Shallow circular petri dish
            const float R = std::sqrt(Frac) * Settings.VesselRadiusMeters;
            Ax += std::cos(Ang) * R;
            Ay += std::sin(Ang) * R;
        }

        Kin[Slot].PosX        = Ax;
        Kin[Slot].PosY        = Ay;
        Kin[Slot].PosZ        = Az;
        Kin[Slot].InverseMass = 1.0f;

        Cusp[Slot].DishAnchorX      = Ax;
        Cusp[Slot].DishAnchorY      = Ay;
        Cusp[Slot].DishAnchorZ      = Az;
        Cusp[Slot].CuspHeightMeters = 0.04f;
        Cusp[Slot].NormalX          = Nx;
        Cusp[Slot].NormalY          = Ny;
        Cusp[Slot].NormalZ          = Nz;
        Cusp[Slot].Susceptibility   = 0.85f + 0.30f * std::sin(Ang * 3.0f);

        Appear[Slot].RadiusMeters = 0.055f;
        Appear[Slot].ColorR       = 0.06f;
        Appear[Slot].ColorG       = 0.08f;
        Appear[Slot].ColorB       = 0.12f;
        Appear[Slot].ColorA       = 1.0f;
    }

    return AcquiredCount;
}

void RosensweigFerrofluidSolver::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(ChannelLayouts::Kinematics);
    (void)Bank.RequireChannel(ChannelLayouts::Appearance);
    (void)Bank.RequireChannel(ChannelLayouts::MagneticCusp);
}

void RosensweigFerrofluidSolver::InitializeTick(
    ParticleChannelBank&        /*Bank*/,
    const SimulationTickInputs& Inputs)
{
    if (Settings.ManualMagnetPole)
    {
        EvaluatedPoleMeters = Settings.ManualPoleMeters;
        return;
    }

    const float Angle  = static_cast<float>(Inputs.TimeSeconds) * Settings.OrbitSpeedRadSec;
    const float OrbitR = Settings.VesselRadiusMeters * 0.62f;
    EvaluatedPoleMeters = {
        Settings.OriginMeters.X + std::cos(Angle) * OrbitR,
        Settings.OriginMeters.Y + std::sin(Angle) * OrbitR,
        Settings.OriginMeters.Z + Settings.MaxCuspHeightMeters * 1.15f + 0.25f * std::sin(Angle * 2.0f),
    };
}

void RosensweigFerrofluidSolver::EvaluateRange(
    ParticleChannelBank&        Bank,
    uint32_t                    BeginSlot,
    uint32_t                    EndSlot,
    const SimulationTickInputs& Inputs) const
{
    auto Kin    = Bank.AccessChannel(ChannelLayouts::Kinematics);
    auto Appear = Bank.AccessChannel(ChannelLayouts::Appearance);
    auto Cusp   = Bank.AccessChannel(ChannelLayouts::MagneticCusp);
    if (Kin.empty() || Cusp.empty())
    {
        return;
    }

    const float Dt         = Inputs.DeltaTimeSeconds;
    const float SmoothRate = 1.0f - std::exp(-std::max(0.5f, Settings.CapillaryDamping) * Dt);

    for (uint32_t Slot = BeginSlot; Slot < EndSlot; ++Slot)
    {
        MagneticCuspChannel& M = Cusp[Slot];
        KinematicsChannel&   P = Kin[Slot];

        const float Dx     = EvaluatedPoleMeters.X - M.DishAnchorX;
        const float Dy     = EvaluatedPoleMeters.Y - M.DishAnchorY;
        const float Dz     = EvaluatedPoleMeters.Z - M.DishAnchorZ;
        const float DistSq = Dx * Dx + Dy * Dy + Dz * Dz + 0.18f;
        const float Dist   = std::sqrt(DistSq);

        // Rosensweig instability peak: sharp Gaussian cluster beneath the orbiting dipole
        const float LatDx    = EvaluatedPoleMeters.X - M.DishAnchorX;
        const float LatDy    = EvaluatedPoleMeters.Y - M.DishAnchorY;
        const float LatSq    = LatDx * LatDx + LatDy * LatDy;
        const float FluxNorm = std::max(0.0f, (Dx * M.NormalX + Dy * M.NormalY + Dz * M.NormalZ) / Dist);
        const float Cluster  = std::exp(-LatSq / 0.58f);
        float TargetHeight   = Settings.MaxCuspHeightMeters * M.Susceptibility
                             * (0.08f + 0.92f * Cluster * FluxNorm * FluxNorm);

        // Coupled ForceFields magnetic/wind amplification
        if (Inputs.ForceAccumulator != nullptr)
        {
            const ForceFields::SpatialForceSample Sample =
                Inputs.ForceAccumulator->QueryForceFieldSample({ M.DishAnchorX, M.DishAnchorY, M.DishAnchorZ }, {}, Inputs.TimeSeconds);
            const float FieldMag = std::sqrt(
                Sample.LinearAcceleration.X * Sample.LinearAcceleration.X
                + Sample.LinearAcceleration.Y * Sample.LinearAcceleration.Y
                + Sample.LinearAcceleration.Z * Sample.LinearAcceleration.Z);
            TargetHeight += std::min(0.22f, FieldMag * 0.018f);
        }

        TargetHeight         = std::clamp(TargetHeight, 0.03f, Settings.MaxCuspHeightMeters);
        M.CuspHeightMeters  += (TargetHeight - M.CuspHeightMeters) * SmoothRate;

        // Tilt cusp gently toward orbiting dipole pole and drive via critically damped capillary spring
        const float Lean   = 0.16f * (M.CuspHeightMeters / std::max(0.1f, Settings.MaxCuspHeightMeters));
        const float TargetX = M.DishAnchorX + M.NormalX * M.CuspHeightMeters + (Dx / Dist) * Lean;
        const float TargetY = M.DishAnchorY + M.NormalY * M.CuspHeightMeters + (Dy / Dist) * Lean;
        const float TargetZ = M.DishAnchorZ + M.NormalZ * M.CuspHeightMeters + (Dz / Dist) * Lean;

        const float SpringK = 28.0f;
        const float DampC   = std::exp(-11.5f * Dt);
        P.VelX = (P.VelX + (TargetX - P.PosX) * SpringK * Dt) * DampC;
        P.VelY = (P.VelY + (TargetY - P.PosY) * SpringK * Dt) * DampC;
        P.VelZ = (P.VelZ + (TargetZ - P.PosZ) * SpringK * Dt) * DampC;

        if (!Appear.empty())
        {
            const float Spec = std::clamp(M.CuspHeightMeters / std::max(0.1f, Settings.MaxCuspHeightMeters), 0.0f, 1.0f);
            Appear[Slot].ColorR     = 0.08f + 0.56f * Spec * Spec;
            Appear[Slot].ColorG     = 0.11f + 0.68f * Spec * Spec;
            Appear[Slot].ColorB     = 0.18f + 0.80f * Spec * Spec;
            Appear[Slot].ColorRampT = Spec;
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                       3. MULTI-TIER HANABI FIREWORKS & SKY TEXT
//------------------------------------------------------------------------------------------------------------------------

void PyroVolleySource::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(ChannelLayouts::Kinematics);
    (void)Bank.RequireChannel(ChannelLayouts::Lifespan);
    (void)Bank.RequireChannel(ChannelLayouts::Appearance);
    (void)Bank.RequireChannel(ChannelLayouts::PyroVolley);
}

uint32_t PyroVolleySource::ReleaseParticles(
    ParticleChannelBank&        Bank,
    const SimulationTickInputs& Inputs,
    ParticleRandomGenerator&                Generator)
{
    IntervalAccumSec += Inputs.DeltaTimeSeconds;
    if (Settings.VolleyIntervalSec > 0.1f && IntervalAccumSec >= Settings.VolleyIntervalSec)
    {
        IntervalAccumSec -= Settings.VolleyIntervalSec;
        ++PendingRockets;
    }

    if (PendingRockets == 0u)
    {
        return 0u;
    }

    uint32_t BeginSlot     = 0u;
    const uint32_t AcquiredCount = Bank.AcquireSlots(PendingRockets, BeginSlot);
    PendingRockets         = 0u;
    if (AcquiredCount == 0u)
    {
        return 0u;
    }

    auto Kin    = Bank.AccessChannel(ChannelLayouts::Kinematics);
    auto Life   = Bank.AccessChannel(ChannelLayouts::Lifespan);
    auto Appear = Bank.AccessChannel(ChannelLayouts::Appearance);
    auto Pyro   = Bank.AccessChannel(ChannelLayouts::PyroVolley);

    for (uint32_t Offset = 0u; Offset < AcquiredCount; ++Offset)
    {
        const uint32_t Slot = BeginSlot + Offset;
        const float SpreadX = Generator.NextRange(-1.4f, 1.4f);
        const float SpreadY = Generator.NextRange(-0.9f, 0.9f);

        Kin[Slot].PosX        = Settings.LaunchOriginMeters.X + SpreadX * 0.25f;
        Kin[Slot].PosY        = Settings.LaunchOriginMeters.Y + SpreadY * 0.25f;
        Kin[Slot].PosZ        = Settings.LaunchOriginMeters.Z;
        Kin[Slot].InverseMass = 1.0f;
        Kin[Slot].VelX        = SpreadX * 1.15f;
        Kin[Slot].VelY        = SpreadY * 1.15f;
        Kin[Slot].VelZ        = Settings.AscentSpeedMs * Generator.NextRange(0.90f, 1.15f);

        const float DetSec        = Generator.NextRange(0.65f, 0.95f);
        Life[Slot].AgeSeconds     = 0.0f;
        Life[Slot].MaxLifeSeconds = DetSec + 0.05f;
        Life[Slot].SeedUint32     = Generator.NextUint32();
        Life[Slot].LivenessMask   = 1u;

        Pyro[Slot].TierIndex         = 0u;
        Pyro[Slot].DetonationTimeSec = DetSec;
        Pyro[Slot].SubBurstCount     = Settings.StarsPerBurst;
        Pyro[Slot].PatternCode       = (Settings.PatternCode == 6u) ? (VolleyCounter % 6u) : Settings.PatternCode;
        Pyro[Slot].TrailGain         = 1.0f;
        ++VolleyCounter;

        Appear[Slot].RadiusMeters = 0.09f;
        Appear[Slot].ColorR       = 1.0f;
        Appear[Slot].ColorG       = 0.88f;
        Appear[Slot].ColorB       = 0.54f;
        Appear[Slot].ColorA       = 1.0f;
    }

    return AcquiredCount;
}

void PyroVolleySource::ResetSource() noexcept
{
    IntervalAccumSec = 0.0f;
    PendingRockets   = 2u;
    VolleyCounter    = 0u;
}

void PyroVolleySource::TriggerImmediateBurst(uint32_t OverrideCount) noexcept
{
    PendingRockets += (OverrideCount > 0u) ? std::min(OverrideCount, 6u) : 1u;
}

void PyroVolleyBurstSolver::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(ChannelLayouts::Kinematics);
    (void)Bank.RequireChannel(ChannelLayouts::Lifespan);
    (void)Bank.RequireChannel(ChannelLayouts::Appearance);
    (void)Bank.RequireChannel(ChannelLayouts::PyroVolley);
}

void PyroVolleyBurstSolver::EvaluateRange(
    ParticleChannelBank&        Bank,
    uint32_t                    BeginSlot,
    uint32_t                    EndSlot,
    const SimulationTickInputs& Inputs) const
{
    auto Kin    = Bank.AccessChannel(ChannelLayouts::Kinematics);
    auto Life   = Bank.QueryChannel(ChannelLayouts::Lifespan);
    auto Appear = Bank.AccessChannel(ChannelLayouts::Appearance);
    auto Pyro   = Bank.QueryChannel(ChannelLayouts::PyroVolley);
    if (Kin.empty() || Life.empty() || Pyro.empty())
    {
        return;
    }

    const float Dt = Inputs.DeltaTimeSeconds;
    for (uint32_t Slot = BeginSlot; Slot < EndSlot; ++Slot)
    {
        if (Pyro[Slot].TierIndex == 1u)
        {
            // Sky-text and decorative Hanabi stars have heightened air drag so shapes hold cleanly in the sky
            const float StarDrag = std::exp(-1.65f * Dt);
            Kin[Slot].VelX *= StarDrag;
            Kin[Slot].VelY *= StarDrag;
            Kin[Slot].VelZ *= StarDrag;
            if (!Appear.empty() && Life[Slot].MaxLifeSeconds > 1.0e-4f)
            {
                const float Fade = 1.0f - std::clamp(Life[Slot].AgeSeconds / Life[Slot].MaxLifeSeconds, 0.0f, 1.0f);
                Appear[Slot].ColorA = Fade * Fade;
            }
        }
    }
}

void PyroVolleyBurstSolver::FinalizeTick(
    ParticleChannelBank&        Bank,
    const SimulationTickInputs& /*Inputs*/,
    ParticleRandomGenerator&                Generator)
{
    auto Kin    = Bank.AccessChannel(ChannelLayouts::Kinematics);
    auto Life   = Bank.AccessChannel(ChannelLayouts::Lifespan);
    auto Appear = Bank.AccessChannel(ChannelLayouts::Appearance);
    auto Pyro   = Bank.AccessChannel(ChannelLayouts::PyroVolley);
    if (Kin.empty() || Life.empty() || Pyro.empty())
    {
        return;
    }

    struct PendingDetonation
    {
        ForceFields::SpatialVector3 Pos{};
        uint32_t                    StarCount   = 0u;
        uint32_t                    PatternCode = 0u;
    };

    std::vector<PendingDetonation> Detonations{};
    const uint32_t InitialActive = Bank.QueryActiveCount();
    for (uint32_t Slot = 0u; Slot < InitialActive; ++Slot)
    {
        if (Pyro[Slot].TierIndex == 0u && Life[Slot].AgeSeconds >= Pyro[Slot].DetonationTimeSec)
        {
            Detonations.push_back({
                { Kin[Slot].PosX, Kin[Slot].PosY, Kin[Slot].PosZ },
                Pyro[Slot].SubBurstCount,
                Pyro[Slot].PatternCode,
            });
            Pyro[Slot].TierIndex     = 2u;
            Life[Slot].AgeSeconds    = Life[Slot].MaxLifeSeconds + 1.0f;
        }
    }

    static constexpr std::array<std::array<float, 3u>, 6u> BurstHues = {{
        {{ 1.00f, 0.84f, 0.34f }}, // Gold Chrysanthemum
        {{ 0.32f, 0.88f, 1.00f }}, // Electric Cyan
        {{ 1.00f, 0.34f, 0.62f }}, // Magenta Heart
        {{ 0.42f, 1.00f, 0.56f }}, // Emerald Ring
        {{ 1.00f, 0.60f, 0.24f }}, // Amber Willow
        {{ 0.96f, 0.92f, 1.00f }}, // Silver Sky Text
    }};

    for (const PendingDetonation& Burst : Detonations)
    {
        std::vector<ForceFields::SpatialVector3> SkyPts{};
        if (Burst.PatternCode == 5u && !Settings.SkyText.empty())
        {
            const float CharSpan = 0.85f;
            const float TotalW   = static_cast<float>(Settings.SkyText.size()) * CharSpan;
            for (std::size_t C = 0u; C < Settings.SkyText.size(); ++C)
            {
                AppendSkyCharacterPoints(Settings.SkyText[C], static_cast<float>(C) * CharSpan - TotalW * 0.5f, 1.1f, SkyPts);
            }
        }

        const uint32_t Request = !SkyPts.empty() ? static_cast<uint32_t>(SkyPts.size()) : std::max(12u, Burst.StarCount);
        uint32_t BeginSlot     = 0u;
        const uint32_t AcquiredCount = Bank.AcquireSlots(Request, BeginSlot);
        const auto& Hue        = BurstHues[Burst.PatternCode % BurstHues.size()];

        for (uint32_t I = 0u; I < AcquiredCount; ++I)
        {
            const uint32_t Slot = BeginSlot + I;
            ForceFields::SpatialVector3 Vel{};

            if (!SkyPts.empty())
            {
                const ForceFields::SpatialVector3& Target = SkyPts[I % SkyPts.size()];
                Vel = { Target.X * 2.1f, Generator.NextRange(-0.25f, 0.25f), Target.Z * 2.1f };
            }
            else if (Burst.PatternCode == 2u)
            {
                // Ring burst
                const float Ang = (static_cast<float>(I) / static_cast<float>(AcquiredCount)) * Constants::TauFloat;
                Vel = { std::cos(Ang) * Settings.BurstSpeedMs, std::sin(Ang) * Settings.BurstSpeedMs * 0.25f, std::sin(Ang) * Settings.BurstSpeedMs };
            }
            else if (Burst.PatternCode == 3u)
            {
                // Parametric Heart
                const float T  = (static_cast<float>(I) / static_cast<float>(AcquiredCount)) * Constants::TauFloat;
                const float Hx = 16.0f * std::pow(std::sin(T), 3.0f);
                const float Hz = 13.0f * std::cos(T) - 5.0f * std::cos(2.0f * T) - 2.0f * std::cos(3.0f * T) - std::cos(4.0f * T);
                Vel            = { Hx * 0.28f, Generator.NextRange(-0.3f, 0.3f), Hz * 0.28f };
            }
            else
            {
                const ForceFields::SpatialVector3 Dir = Generator.NextUnitSphere();
                const float Spd = Settings.BurstSpeedMs * Generator.NextRange(0.75f, 1.15f);
                Vel = { Dir.X * Spd, Dir.Y * Spd, Dir.Z * Spd };
            }

            Kin[Slot].PosX        = Burst.Pos.X;
            Kin[Slot].PosY        = Burst.Pos.Y;
            Kin[Slot].PosZ        = Burst.Pos.Z;
            Kin[Slot].InverseMass = 1.0f;
            Kin[Slot].VelX        = Vel.X;
            Kin[Slot].VelY        = Vel.Y;
            Kin[Slot].VelZ        = Vel.Z;

            Life[Slot].AgeSeconds     = 0.0f;
            Life[Slot].MaxLifeSeconds = Generator.NextRange(1.4f, 2.3f);
            Life[Slot].SeedUint32     = Generator.NextUint32();
            Life[Slot].LivenessMask   = 1u;

            Pyro[Slot].TierIndex         = 1u;
            Pyro[Slot].DetonationTimeSec = 100.0f;
            Pyro[Slot].PatternCode       = Burst.PatternCode;

            Appear[Slot].RadiusMeters = 0.065f;
            Appear[Slot].ColorR       = Hue[0];
            Appear[Slot].ColorG       = Hue[1];
            Appear[Slot].ColorB       = Hue[2];
            Appear[Slot].ColorA       = 1.0f;
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                           4. BOIDS MURMURATION & FLOCKING
//------------------------------------------------------------------------------------------------------------------------

void BoidsFlockSource::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(ChannelLayouts::Kinematics);
    (void)Bank.RequireChannel(ChannelLayouts::Appearance);
    (void)Bank.RequireChannel(ChannelLayouts::FlockSteering);
}

uint32_t BoidsFlockSource::ReleaseParticles(
    ParticleChannelBank&        Bank,
    const SimulationTickInputs& /*Inputs*/,
    ParticleRandomGenerator&                Generator)
{
    if (Initialized)
    {
        return 0u;
    }
    Initialized = true;

    const uint32_t TotalNeed = std::min(Bank.QueryCapacity(), std::max(8u, Settings.FlockCount));
    Bank.ClearActiveSlots();
    uint32_t BeginSlot     = 0u;
    const uint32_t AcquiredCount = Bank.AcquireSlots(TotalNeed, BeginSlot);

    auto Kin    = Bank.AccessChannel(ChannelLayouts::Kinematics);
    auto Appear = Bank.AccessChannel(ChannelLayouts::Appearance);
    auto Flock  = Bank.AccessChannel(ChannelLayouts::FlockSteering);

    for (uint32_t I = 0u; I < AcquiredCount; ++I)
    {
        const uint32_t Slot                 = BeginSlot + I;
        const ForceFields::SpatialVector3 P = Generator.NextUnitSphere();
        const float R                       = Settings.HomeRadiusMeters * 0.65f * std::cbrt(Generator.NextUnitFloat());
        const ForceFields::SpatialVector3 H = Generator.NextUnitSphere();

        Kin[Slot].PosX        = Settings.HomeOriginMeters.X + P.X * R;
        Kin[Slot].PosY        = Settings.HomeOriginMeters.Y + P.Y * R;
        Kin[Slot].PosZ        = Settings.HomeOriginMeters.Z + P.Z * R * 0.6f;
        Kin[Slot].InverseMass = 1.0f;
        Kin[Slot].VelX        = H.X * Settings.CruiseSpeedMs;
        Kin[Slot].VelY        = H.Y * Settings.CruiseSpeedMs;
        Kin[Slot].VelZ        = H.Z * Settings.CruiseSpeedMs * 0.4f;

        Flock[Slot].HeadingX       = H.X;
        Flock[Slot].HeadingY       = H.Y;
        Flock[Slot].HeadingZ       = H.Z;
        Flock[Slot].WingPhaseRad   = Generator.NextRange(0.0f, Constants::TauFloat);
        Flock[Slot].SpeciesVariant = Settings.CreatureVariant;

        Appear[Slot].RadiusMeters = 0.08f;
        Appear[Slot].ColorR       = 0.78f;
        Appear[Slot].ColorG       = 0.88f;
        Appear[Slot].ColorB       = 0.80f;
        Appear[Slot].ColorA       = 1.0f;
    }

    return AcquiredCount;
}

void BoidsFlockSolver::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(ChannelLayouts::Kinematics);
    (void)Bank.RequireChannel(ChannelLayouts::FlockSteering);
}

void BoidsFlockSolver::InitializeTick(ParticleChannelBank& Bank, const SimulationTickInputs& /*Inputs*/)
{
    const auto Kin     = Bank.QueryChannel(ChannelLayouts::Kinematics);
    const uint32_t Cnt = Bank.QueryActiveCount();
    Snapshot.resize(Cnt);
    for (uint32_t I = 0u; I < Cnt; ++I)
    {
        Snapshot[I] = { Kin[I].PosX, Kin[I].PosY, Kin[I].PosZ, Kin[I].VelX, Kin[I].VelY, Kin[I].VelZ };
    }
}

void BoidsFlockSolver::EvaluateRange(
    ParticleChannelBank&        Bank,
    uint32_t                    BeginSlot,
    uint32_t                    EndSlot,
    const SimulationTickInputs& Inputs) const
{
    auto Kin   = Bank.AccessChannel(ChannelLayouts::Kinematics);
    auto Flock = Bank.AccessChannel(ChannelLayouts::FlockSteering);
    if (Kin.empty() || Flock.empty() || Snapshot.empty())
    {
        return;
    }

    const float Dt       = Inputs.DeltaTimeSeconds;
    const float PercSq   = Settings.PerceptionMeters * Settings.PerceptionMeters;
    const float SepSq    = Settings.SeparationMeters * Settings.SeparationMeters;
    const uint32_t Total = static_cast<uint32_t>(Snapshot.size());
    const uint32_t Stride = std::max(1u, Total / 96u); // Bounded O(1) neighbor sample budget for large flocks

    for (uint32_t Slot = BeginSlot; Slot < EndSlot; ++Slot)
    {
        const BoidKinematicSnapshot& Self = Snapshot[Slot];
        float SepX = 0.0f, SepY = 0.0f, SepZ = 0.0f;
        float AliX = 0.0f, AliY = 0.0f, AliZ = 0.0f;
        float CohX = 0.0f, CohY = 0.0f, CohZ = 0.0f;
        uint32_t Neighbors = 0u;

        for (uint32_t Other = (Slot % Stride); Other < Total; Other += Stride)
        {
            if (Other == Slot)
            {
                continue;
            }
            const float Dx     = Self.PosX - Snapshot[Other].PosX;
            const float Dy     = Self.PosY - Snapshot[Other].PosY;
            const float Dz     = Self.PosZ - Snapshot[Other].PosZ;
            const float DistSq = Dx * Dx + Dy * Dy + Dz * Dz;
            if (DistSq > 1.0e-6f && DistSq < PercSq)
            {
                ++Neighbors;
                AliX += Snapshot[Other].VelX;
                AliY += Snapshot[Other].VelY;
                AliZ += Snapshot[Other].VelZ;
                CohX += Snapshot[Other].PosX;
                CohY += Snapshot[Other].PosY;
                CohZ += Snapshot[Other].PosZ;
                if (DistSq < SepSq)
                {
                    const float Inv = 1.0f / std::max(0.08f, DistSq);
                    SepX += Dx * Inv;
                    SepY += Dy * Inv;
                    SepZ += Dz * Inv;
                }
            }
        }

        float Ax = SepX * 1.4f;
        float Ay = SepY * 1.4f;
        float Az = SepZ * 1.4f;

        if (Neighbors > 0u)
        {
            const float InvN = 1.0f / static_cast<float>(Neighbors);
            Ax += (AliX * InvN - Self.VelX) * 1.15f + (CohX * InvN - Self.PosX) * 0.85f;
            Ay += (AliY * InvN - Self.VelY) * 1.15f + (CohY * InvN - Self.PosY) * 0.85f;
            Az += (AliZ * InvN - Self.VelZ) * 1.15f + (CohZ * InvN - Self.PosZ) * 0.85f;
        }

        // Soft containment return toward roost home
        const float Hx    = Settings.HomeOriginMeters.X - Self.PosX;
        const float Hy    = Settings.HomeOriginMeters.Y - Self.PosY;
        const float Hz    = Settings.HomeOriginMeters.Z - Self.PosZ;
        const float HDist = std::sqrt(Hx * Hx + Hy * Hy + Hz * Hz);
        if (HDist > Settings.HomeRadiusMeters)
        {
            const float Pull = (HDist - Settings.HomeRadiusMeters) * 2.6f / std::max(0.1f, HDist);
            Ax += Hx * Pull;
            Ay += Hy * Pull;
            Az += Hz * Pull;
        }

        // Dynamic predator / thrown sphere scatter evasion
        for (const DynamicSphereCollider& Sphere : Inputs.SphereColliders)
        {
            if (!Sphere.Active)
            {
                continue;
            }
            const float Ex     = Self.PosX - Sphere.CentreMeters.X;
            const float Ey     = Self.PosY - Sphere.CentreMeters.Y;
            const float Ez     = Self.PosZ - Sphere.CentreMeters.Z;
            const float EvadeR = Sphere.RadiusMeters + Settings.PerceptionMeters;
            const float DistSq = Ex * Ex + Ey * Ey + Ez * Ez;
            if (DistSq > 1.0e-5f && DistSq < EvadeR * EvadeR)
            {
                const float InvD = 12.0f / std::sqrt(DistSq);
                Ax += Ex * InvD;
                Ay += Ey * InvD;
                Az += Ez * InvD;
            }
        }

        const float AccMag = std::sqrt(Ax * Ax + Ay * Ay + Az * Az);
        if (AccMag > Settings.MaxSteerAccelMs2)
        {
            const float Scale = Settings.MaxSteerAccelMs2 / AccMag;
            Ax *= Scale;
            Ay *= Scale;
            Az *= Scale;
        }

        Kin[Slot].VelX += Ax * Dt;
        Kin[Slot].VelY += Ay * Dt;
        Kin[Slot].VelZ += Az * Dt;

        const float Spd = std::sqrt(Kin[Slot].VelX * Kin[Slot].VelX + Kin[Slot].VelY * Kin[Slot].VelY + Kin[Slot].VelZ * Kin[Slot].VelZ);
        if (Spd > 1.0e-4f)
        {
            const float Desired = std::clamp(Spd, Settings.CruiseSpeedMs * 0.6f, Settings.CruiseSpeedMs * 1.45f);
            const float Blend   = (Desired / Spd);
            Kin[Slot].VelX *= Blend;
            Kin[Slot].VelY *= Blend;
            Kin[Slot].VelZ *= Blend;

            Flock[Slot].HeadingX = Kin[Slot].VelX / Desired;
            Flock[Slot].HeadingY = Kin[Slot].VelY / Desired;
            Flock[Slot].HeadingZ = Kin[Slot].VelZ / Desired;
        }

        Flock[Slot].SteerX        = Ax;
        Flock[Slot].SteerY        = Ay;
        Flock[Slot].SteerZ        = Az;
        Flock[Slot].WingPhaseRad += (9.5f + Spd * 1.4f) * Dt;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                  5. GLACIAL & SEISMIC FRACTURE PROPAGATION
//------------------------------------------------------------------------------------------------------------------------

void FractureSurfaceSource::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(ChannelLayouts::Kinematics);
    (void)Bank.RequireChannel(ChannelLayouts::Appearance);
    (void)Bank.RequireChannel(ChannelLayouts::FractureStress);
}

uint32_t FractureSurfaceSource::ReleaseParticles(
    ParticleChannelBank&        Bank,
    const SimulationTickInputs& /*Inputs*/,
    ParticleRandomGenerator&                Generator)
{
    if (Initialized)
    {
        return 0u;
    }
    Initialized = true;

    const uint32_t TotalNeed = std::min(Bank.QueryCapacity(), std::max(16u, Settings.CellCount));
    Bank.ClearActiveSlots();
    uint32_t BeginSlot     = 0u;
    const uint32_t AcquiredCount = Bank.AcquireSlots(TotalNeed, BeginSlot);

    auto Kin    = Bank.AccessChannel(ChannelLayouts::Kinematics);
    auto Appear = Bank.AccessChannel(ChannelLayouts::Appearance);
    auto Frac   = Bank.AccessChannel(ChannelLayouts::FractureStress);

    for (uint32_t I = 0u; I < AcquiredCount; ++I)
    {
        const uint32_t Slot = BeginSlot + I;
        const float Ang     = Generator.NextRange(0.0f, Constants::TauFloat);
        const float R       = std::sqrt(Generator.NextUnitFloat()) * Settings.SheetRadiusMeters;
        const float Ox      = Settings.OriginMeters.X + std::cos(Ang) * R;
        const float Oy      = Settings.OriginMeters.Y + std::sin(Ang) * R;
        const float Oz      = Settings.OriginMeters.Z;

        Kin[Slot].PosX        = Ox;
        Kin[Slot].PosY        = Oy;
        Kin[Slot].PosZ        = Oz;
        Kin[Slot].InverseMass = 0.0f; // Intact until fracture front reaches cell

        Frac[Slot].CellOriginX       = Ox;
        Frac[Slot].CellOriginY       = Oy;
        Frac[Slot].CellOriginZ       = Oz;
        Frac[Slot].CrackDepthMeters  = 0.0f;
        Frac[Slot].AccumulatedStress = 0.0f;
        Frac[Slot].BranchId          = I % 8u;
        Frac[Slot].DetachedBit       = 0u;
        Frac[Slot].ShardScale        = Generator.NextRange(0.75f, 1.35f);

        Appear[Slot].RadiusMeters = 0.09f * Frac[Slot].ShardScale;
        Appear[Slot].ColorR       = (Settings.Pattern == 2u) ? 0.34f : 0.68f;
        Appear[Slot].ColorG       = (Settings.Pattern == 2u) ? 0.24f : 0.88f;
        Appear[Slot].ColorB       = (Settings.Pattern == 2u) ? 0.20f : 0.98f;
        Appear[Slot].ColorA       = 1.0f;
    }

    return AcquiredCount;
}

void FractureCrackSolver::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(ChannelLayouts::Kinematics);
    (void)Bank.RequireChannel(ChannelLayouts::Appearance);
    (void)Bank.RequireChannel(ChannelLayouts::FractureStress);
}

void FractureCrackSolver::EvaluateRange(
    ParticleChannelBank&        Bank,
    uint32_t                    BeginSlot,
    uint32_t                    EndSlot,
    const SimulationTickInputs& Inputs) const
{
    auto Kin    = Bank.AccessChannel(ChannelLayouts::Kinematics);
    auto Appear = Bank.AccessChannel(ChannelLayouts::Appearance);
    auto Frac   = Bank.AccessChannel(ChannelLayouts::FractureStress);
    if (Kin.empty() || Frac.empty())
    {
        return;
    }

    const float WaveFrontR = static_cast<float>(Inputs.TimeSeconds) * (1.15f * Settings.BranchDensity);
    for (uint32_t Slot = BeginSlot; Slot < EndSlot; ++Slot)
    {
        FractureStressChannel& F = Frac[Slot];
        if (F.DetachedBit != 0u)
        {
            continue;
        }

        const float Dx = F.CellOriginX - Settings.OriginMeters.X;
        const float Dy = F.CellOriginY - Settings.OriginMeters.Y;
        const float R  = std::sqrt(Dx * Dx + Dy * Dy);

        if (R <= WaveFrontR)
        {
            F.AccumulatedStress = std::min(1.0f, F.AccumulatedStress + Inputs.DeltaTimeSeconds * (1.2f * Settings.BranchDensity));
            F.CrackDepthMeters  = F.AccumulatedStress * Settings.IncisionDepthMeters;
            Kin[Slot].PosZ      = F.CellOriginZ - F.CrackDepthMeters * 0.25f;

            if (F.AccumulatedStress >= 0.92f)
            {
                F.DetachedBit         = 1u;
                Kin[Slot].InverseMass = 1.0f;
                const float InvR      = 1.0f / std::max(0.15f, R);
                Kin[Slot].VelX        = Dx * InvR * 1.35f;
                Kin[Slot].VelY        = Dy * InvR * 1.35f;
                Kin[Slot].VelZ        = 1.6f + 0.8f * Settings.IncisionDepthMeters;
            }
            if (!Appear.empty())
            {
                Appear[Slot].ColorRampT = F.AccumulatedStress;
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                    6. MOGRAPH EFFECTOR LATTICE & DEREZ WAVES
//------------------------------------------------------------------------------------------------------------------------

void LatticeGridSource::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(ChannelLayouts::Kinematics);
    (void)Bank.RequireChannel(ChannelLayouts::Appearance);
    (void)Bank.RequireChannel(ChannelLayouts::LatticeGrid);
}

uint32_t LatticeGridSource::ReleaseParticles(
    ParticleChannelBank&        Bank,
    const SimulationTickInputs& /*Inputs*/,
    ParticleRandomGenerator&                /*Generator*/)
{
    if (Initialized)
    {
        return 0u;
    }
    Initialized = true;

    const uint32_t Gx        = std::max(2u, Settings.GridX);
    const uint32_t Gy        = std::max(2u, Settings.GridY);
    const uint32_t TotalNeed = std::min(Bank.QueryCapacity(), Gx * Gy);
    Bank.ClearActiveSlots();
    uint32_t BeginSlot     = 0u;
    const uint32_t AcquiredCount = Bank.AcquireSlots(TotalNeed, BeginSlot);

    auto Kin    = Bank.AccessChannel(ChannelLayouts::Kinematics);
    auto Appear = Bank.AccessChannel(ChannelLayouts::Appearance);
    auto Grid   = Bank.AccessChannel(ChannelLayouts::LatticeGrid);

    const float HalfW = 0.5f * static_cast<float>(Gx - 1u) * Settings.SpacingMeters;
    const float HalfH = 0.5f * static_cast<float>(Gy - 1u) * Settings.SpacingMeters;

    for (uint32_t I = 0u; I < AcquiredCount; ++I)
    {
        const uint32_t Slot = BeginSlot + I;
        const uint32_t Cx   = I % Gx;
        const uint32_t Cy   = I / Gx;
        const float Rx      = Settings.OriginMeters.X + static_cast<float>(Cx) * Settings.SpacingMeters - HalfW;
        const float Ry      = Settings.OriginMeters.Y + static_cast<float>(Cy) * Settings.SpacingMeters - HalfH;
        const float Rz      = Settings.OriginMeters.Z;

        Kin[Slot].PosX        = Rx;
        Kin[Slot].PosY        = Ry;
        Kin[Slot].PosZ        = Rz;
        Kin[Slot].InverseMass = Settings.EnableDerezLaunch ? 0.0f : 1.0f;

        Grid[Slot].RestGridX       = Rx;
        Grid[Slot].RestGridY       = Ry;
        Grid[Slot].RestGridZ       = Rz;
        Grid[Slot].ExtrusionMeters = 0.0f;
        Grid[Slot].GridCoordX      = Cx;
        Grid[Slot].GridCoordY      = Cy;
        Grid[Slot].GridCoordZ      = 0u;
        Grid[Slot].WavePhase       = static_cast<float>(Cx + Cy) * 0.25f;

        Appear[Slot].RadiusMeters = Settings.SpacingMeters * 0.42f;
        Appear[Slot].ColorR       = 0.94f;
        Appear[Slot].ColorG       = 0.46f;
        Appear[Slot].ColorB       = 0.40f;
        Appear[Slot].ColorA       = 1.0f;
    }

    return AcquiredCount;
}

void MoGraphEffectorSolver::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(ChannelLayouts::Kinematics);
    (void)Bank.RequireChannel(ChannelLayouts::Appearance);
    (void)Bank.RequireChannel(ChannelLayouts::LatticeGrid);
}

void MoGraphEffectorSolver::EvaluateRange(
    ParticleChannelBank&        Bank,
    uint32_t                    BeginSlot,
    uint32_t                    EndSlot,
    const SimulationTickInputs& Inputs) const
{
    auto Kin    = Bank.AccessChannel(ChannelLayouts::Kinematics);
    auto Appear = Bank.AccessChannel(ChannelLayouts::Appearance);
    auto Grid   = Bank.AccessChannel(ChannelLayouts::LatticeGrid);
    if (Kin.empty() || Grid.empty())
    {
        return;
    }

    const float Dt      = Inputs.DeltaTimeSeconds;
    const float Angle   = static_cast<float>(Inputs.TimeSeconds) * Constants::TauFloat * Settings.WaveFrequencyHz;
    const float EffX    = Settings.OriginMeters.X + std::cos(Angle * 0.5f) * (Settings.FalloffMeters * 0.75f);
    const float EffY    = Settings.OriginMeters.Y + std::sin(Angle * 0.5f) * (Settings.FalloffMeters * 0.75f);
    const float InvFall = 1.0f / std::max(0.25f, Settings.FalloffMeters);
    const float Smooth  = 1.0f - std::exp(-8.5f * Dt);

    for (uint32_t Slot = BeginSlot; Slot < EndSlot; ++Slot)
    {
        LatticeGridChannel& G = Grid[Slot];
        const float Dx        = G.RestGridX - EffX;
        const float Dy        = G.RestGridY - EffY;
        const float NormDist  = std::sqrt(Dx * Dx + Dy * Dy) * InvFall;
        const float Falloff   = std::max(0.0f, 1.0f - NormDist);
        const float SmoothF   = Falloff * Falloff * (3.0f - 2.0f * Falloff);

        const float TargetH = Settings.ExtrusionMeters * SmoothF * (0.65f + 0.35f * std::sin(Angle + G.WavePhase));

        if (Settings.EnableDerezLaunch && Kin[Slot].InverseMass <= 0.0f && SmoothF > 0.65f)
        {
            Kin[Slot].InverseMass = 1.0f;
            Kin[Slot].VelZ        = 2.2f + TargetH * 1.4f;
        }

        if (!Settings.EnableDerezLaunch || Kin[Slot].InverseMass <= 0.0f)
        {
            const float PrevH   = G.ExtrusionMeters;
            G.ExtrusionMeters  += (TargetH - G.ExtrusionMeters) * Smooth;
            const float NextZ   = G.RestGridZ + G.ExtrusionMeters;
            Kin[Slot].VelX      = 0.0f;
            Kin[Slot].VelY      = 0.0f;
            Kin[Slot].VelZ      = (G.ExtrusionMeters - PrevH) / std::max(1.0e-4f, Dt);
            Kin[Slot].PosX      = G.RestGridX;
            Kin[Slot].PosY      = G.RestGridY;
            Kin[Slot].PosZ      = NextZ;
        }

        if (!Appear.empty())
        {
            const float Gain        = std::clamp(G.ExtrusionMeters / std::max(0.1f, Settings.ExtrusionMeters), 0.0f, 1.0f);
            Appear[Slot].ColorRampT = Gain;
            Appear[Slot].RadiusMeters = Settings.SpacingMeters * (0.35f + 0.25f * Gain);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                    7. ENERGY SHIELD BARRIER & SPACE REFRACTION
//------------------------------------------------------------------------------------------------------------------------

void ShieldBarrierSource::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(ChannelLayouts::Kinematics);
    (void)Bank.RequireChannel(ChannelLayouts::Appearance);
}

uint32_t ShieldBarrierSource::ReleaseParticles(
    ParticleChannelBank&        Bank,
    const SimulationTickInputs& /*Inputs*/,
    ParticleRandomGenerator&                /*Generator*/)
{
    if (Initialized)
    {
        return 0u;
    }
    Initialized = true;

    const uint32_t TotalNeed = std::min(Bank.QueryCapacity(), std::max(24u, Settings.CellCount));
    Bank.ClearActiveSlots();
    uint32_t BeginSlot     = 0u;
    const uint32_t AcquiredCount = Bank.AcquireSlots(TotalNeed, BeginSlot);

    auto Kin    = Bank.AccessChannel(ChannelLayouts::Kinematics);
    auto Appear = Bank.AccessChannel(ChannelLayouts::Appearance);

    const float GoldenAngle = 2.39996323f;
    for (uint32_t I = 0u; I < AcquiredCount; ++I)
    {
        const uint32_t Slot = BeginSlot + I;
        const float    Frac = (static_cast<float>(I) + 0.5f) / static_cast<float>(AcquiredCount);
        const float    Ang  = static_cast<float>(I) * GoldenAngle;

        float Nx = 0.0f;
        float Ny = 0.0f;
        float Nz = 1.0f;
        if (Settings.GeometryVariant == 3u)
        {
            Nx = std::cos(Ang);
            Ny = std::sin(Ang);
            Nz = (Frac - 0.5f) * 1.4f;
        }
        else
        {
            const float CosPol = (Settings.GeometryVariant == 1u) ? (1.0f - Frac) : (1.0f - 2.0f * Frac);
            const float SinPol = std::sqrt(std::max(0.0f, 1.0f - CosPol * CosPol));
            Nx = SinPol * std::cos(Ang);
            Ny = SinPol * std::sin(Ang);
            Nz = CosPol;
        }

        Kin[Slot].PosX        = Settings.CentreMeters.X + Nx * Settings.RadiusMeters;
        Kin[Slot].PosY        = Settings.CentreMeters.Y + Ny * Settings.RadiusMeters;
        Kin[Slot].PosZ        = Settings.CentreMeters.Z + Nz * Settings.RadiusMeters;
        Kin[Slot].InverseMass = 0.0f;

        Appear[Slot].RadiusMeters = 0.11f;
        Appear[Slot].ColorR       = 0.35f;
        Appear[Slot].ColorG       = 0.92f;
        Appear[Slot].ColorB       = 0.78f;
        Appear[Slot].ColorA       = 0.65f;
    }

    return AcquiredCount;
}

void EnergyShieldRippleSolver::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(ChannelLayouts::Kinematics);
    (void)Bank.RequireChannel(ChannelLayouts::Appearance);
}

void EnergyShieldRippleSolver::InitializeTick(
    ParticleChannelBank&        /*Bank*/,
    const SimulationTickInputs& Inputs)
{
    for (const DynamicSphereCollider& Sphere : Inputs.SphereColliders)
    {
        if (!Sphere.Active)
        {
            continue;
        }
        const float Dx   = Sphere.CentreMeters.X - Settings.CentreMeters.X;
        const float Dy   = Sphere.CentreMeters.Y - Settings.CentreMeters.Y;
        const float Dz   = Sphere.CentreMeters.Z - Settings.CentreMeters.Z;
        const float Dist = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
        if (Dist <= Settings.RadiusMeters + Sphere.RadiusMeters + 0.2f && Dist > 1.0e-4f)
        {
            LastImpactPoint = {
                Settings.CentreMeters.X + (Dx / Dist) * Settings.RadiusMeters,
                Settings.CentreMeters.Y + (Dy / Dist) * Settings.RadiusMeters,
                Settings.CentreMeters.Z + (Dz / Dist) * Settings.RadiusMeters,
            };
            LastImpactTimeSec = Inputs.TimeSeconds;
        }
    }
}

void EnergyShieldRippleSolver::EvaluateRange(
    ParticleChannelBank&        Bank,
    uint32_t                    BeginSlot,
    uint32_t                    EndSlot,
    const SimulationTickInputs& Inputs) const
{
    const auto Kin = Bank.QueryChannel(ChannelLayouts::Kinematics);
    auto Appear    = Bank.AccessChannel(ChannelLayouts::Appearance);
    if (Kin.empty() || Appear.empty())
    {
        return;
    }

    const float Elapsed = static_cast<float>(Inputs.TimeSeconds - LastImpactTimeSec);
    const float RingR   = Elapsed * Settings.RippleSpeedHz * 1.4f;
    const float Decay   = std::exp(-std::max(0.0f, Elapsed) * 1.8f);

    for (uint32_t Slot = BeginSlot; Slot < EndSlot; ++Slot)
    {
        const float Dx   = Kin[Slot].PosX - LastImpactPoint.X;
        const float Dy   = Kin[Slot].PosY - LastImpactPoint.Y;
        const float Dz   = Kin[Slot].PosZ - LastImpactPoint.Z;
        const float Dist = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);

        const float Delta = Dist - RingR;
        const float Pulse = std::exp(-(Delta * Delta) * 5.0f) * Decay;
        Appear[Slot].ColorRampT = std::clamp(Pulse, 0.0f, 1.0f);
        Appear[Slot].ColorA     = 0.35f + 0.65f * Pulse;
    }
}

void SpaceRefractionSolver::DeclareRequiredChannels(ParticleChannelBank& Bank) const
{
    (void)Bank.RequireChannel(ChannelLayouts::Kinematics);
}

void SpaceRefractionSolver::EvaluateRange(
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
        KinematicsChannel& P = Kin[Slot];
        if (P.InverseMass <= 0.0f)
        {
            continue;
        }

        if (Settings.OrbitalStrength > 1.0e-4f)
        {
            const float Dx     = Settings.SingularityMeters.X - P.PosX;
            const float Dy     = Settings.SingularityMeters.Y - P.PosY;
            const float Dz     = Settings.SingularityMeters.Z - P.PosZ;
            const float R2     = Dx * Dx + Dy * Dy + Dz * Dz + Settings.InnerRadiusMeters * Settings.InnerRadiusMeters;
            const float InvR   = 1.0f / std::sqrt(R2);
            const float Pull   = Settings.OrbitalStrength / R2;

            // Centripetal gravity + tangential accretion swirl in XY
            P.VelX += (Dx * InvR * Pull - Dy * InvR * Pull * 0.45f) * Dt;
            P.VelY += (Dy * InvR * Pull + Dx * InvR * Pull * 0.45f) * Dt;
            P.VelZ += (Dz * InvR * Pull * 1.4f) * Dt;
        }
        if (Settings.ThermalRiseMs > 1.0e-4f)
        {
            P.VelZ += (Settings.ThermalRiseMs - P.VelZ) * std::min(1.0f, 2.5f * Dt);
        }
    }
}

} // namespace Frontier::SimulationSpace::Particles
