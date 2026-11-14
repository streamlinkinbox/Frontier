/*============================================================================================================================================
                                                         PARTICLESIMULATION.CPP
============================================================================================================================================*/
// 🧩 Composable ParticleSimulation tick execution, 32-byte telemetry summary, fluent specification & multi-simulation registry

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Particles/ParticleSimulation.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace Frontier::SimulationSpace::Particles
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     INTERNAL ROUTINES
//------------------------------------------------------------------------------------------------------------------------

namespace
{

constexpr uint32_t RangeTileSlots = 256u; // [slots] - Contiguous 64B-aligned SoA tile granularity

[[nodiscard]] double QueryClockMicros() noexcept
{
    const auto Now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration<double, std::micro>(Now).count();
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      PARTICLE SIMULATION
//------------------------------------------------------------------------------------------------------------------------

ParticleSimulation::ParticleSimulation(std::string SimulationLabel)
    : Label(std::move(SimulationLabel))
    , Integrator(std::make_unique<SemiImplicitEulerIntegrator>())
    , RandomGenerator(1337u)
{
    Channels.ConfigureCapacity(256u, ChannelStoragePolicy::DenseSwapRemove);
}

void ParticleSimulation::ConfigureCapacity(uint32_t MaxCapacity, ChannelStoragePolicy Policy)
{
    Channels.ConfigureCapacity(std::max(1u, MaxCapacity), Policy);
}

void ParticleSimulation::AssignDeterministicSeed(uint32_t Seed) noexcept
{
    MasterSeed = (Seed != 0u) ? Seed : 1337u;
    RandomGenerator.AssignSeed(MasterSeed);
}

void ParticleSimulation::AttachSource(std::unique_ptr<ParticleSource> Source)
{
    if (!Source)
    {
        return;
    }
    Source->DeclareRequiredChannels(Channels);
    Sources.push_back(std::move(Source));
}

void ParticleSimulation::AttachMotionSolver(std::unique_ptr<ParticleMotionSolver> Solver)
{
    if (!Solver)
    {
        return;
    }
    Solver->DeclareRequiredChannels(Channels);
    MotionSolvers.push_back(std::move(Solver));
}

void ParticleSimulation::AttachCollider(std::unique_ptr<ParticleCollider> Collider)
{
    if (!Collider)
    {
        return;
    }
    Collider->DeclareRequiredChannels(Channels);
    Colliders.push_back(std::move(Collider));
}

void ParticleSimulation::AssignIntegrator(std::unique_ptr<ParticleIntegrator> StepIntegrator)
{
    if (!StepIntegrator)
    {
        return;
    }
    StepIntegrator->DeclareRequiredChannels(Channels);
    Integrator = std::move(StepIntegrator);
}

void ParticleSimulation::Tick(const SimulationTickInputs& Inputs)
{
    const double StartMicros = QueryClockMicros();
    const float  Dt          = std::clamp(Inputs.DeltaTimeSeconds, 0.0f, 0.1f);
    if (Dt <= 0.0f)
    {
        ComputeReadbackSummary(StartMicros);
        return;
    }

    SimulationTickInputs SafeInputs = Inputs;
    SafeInputs.DeltaTimeSeconds     = Dt;

    // 1. Release new particle slots via attached sources
    for (std::unique_ptr<ParticleSource>& Source : Sources)
    {
        (void)Source->ReleaseParticles(Channels, SafeInputs, RandomGenerator);
    }

    const uint32_t ActiveCount = Channels.QueryActiveCount();
    if (ActiveCount > 0u)
    {
        // 2. Per-tick initialization (e.g. orbiting magnet pole, boids snapshot)
        for (std::unique_ptr<ParticleMotionSolver>& Solver : MotionSolvers)
        {
            Solver->InitializeTick(Channels, SafeInputs);
        }

        // 3. Evaluate modular motion solvers in contiguous SoA tiles
        for (uint32_t TileBegin = 0u; TileBegin < ActiveCount; TileBegin += RangeTileSlots)
        {
            const uint32_t TileEnd = std::min(ActiveCount, TileBegin + RangeTileSlots);
            for (const std::unique_ptr<ParticleMotionSolver>& Solver : MotionSolvers)
            {
                Solver->EvaluateRange(Channels, TileBegin, TileEnd, SafeInputs);
            }
        }

        // 4. Advance positions via active integrator
        if (Integrator)
        {
            for (uint32_t TileBegin = 0u; TileBegin < ActiveCount; TileBegin += RangeTileSlots)
            {
                const uint32_t TileEnd = std::min(ActiveCount, TileBegin + RangeTileSlots);
                Integrator->AdvanceRange(Channels, TileBegin, TileEnd, SafeInputs);
            }
        }

        // 5. Enforce modular colliders (GroundPlane, SphereContact, SignedDistanceVolume)
        for (uint32_t TileBegin = 0u; TileBegin < ActiveCount; TileBegin += RangeTileSlots)
        {
            const uint32_t TileEnd = std::min(ActiveCount, TileBegin + RangeTileSlots);
            for (const std::unique_ptr<ParticleCollider>& Collider : Colliders)
            {
                Collider->EnforceRange(Channels, TileBegin, TileEnd, SafeInputs);
            }
        }

        // 6. Post-range structural finalization (XPBD constraints, pyro sub-bursts)
        for (std::unique_ptr<ParticleMotionSolver>& Solver : MotionSolvers)
        {
            Solver->FinalizeTick(Channels, SafeInputs, RandomGenerator);
        }

        // 7. Advance lifespans & reclaim expired slots synchronously across ALL channels (including custom user types)
        ReclaimExpiredLifespans(Dt);
    }

    ReadbackSummary.RevisionCounter += 1u;
    ComputeReadbackSummary(StartMicros);
}

void ParticleSimulation::TriggerBurst(uint32_t OverrideCount) noexcept
{
    for (std::unique_ptr<ParticleSource>& Source : Sources)
    {
        Source->TriggerImmediateBurst(OverrideCount);
    }
}

void ParticleSimulation::Reset(uint32_t OverrideSeed) noexcept
{
    if (OverrideSeed != 0u)
    {
        MasterSeed = OverrideSeed;
    }
    RandomGenerator.AssignSeed(MasterSeed);
    Channels.ResetAllChannels();

    for (std::unique_ptr<ParticleSource>& Source : Sources)
    {
        Source->ResetSource();
    }
    for (std::unique_ptr<ParticleMotionSolver>& Solver : MotionSolvers)
    {
        Solver->ResetSolver();
    }

    ReadbackSummary = ParticleReadbackSummary{};
}

bool ParticleSimulation::QueryGpuComputeEligible() const noexcept
{
    if (!Channels.QueryChannelsGpuResident())
    {
        return false;
    }
    for (const std::unique_ptr<ParticleSource>& Source : Sources)
    {
        if (!Source->QueryGpuComputeSupported())
        {
            return false;
        }
    }
    for (const std::unique_ptr<ParticleMotionSolver>& Solver : MotionSolvers)
    {
        if (!Solver->QueryGpuComputeSupported())
        {
            return false;
        }
    }
    for (const std::unique_ptr<ParticleCollider>& Collider : Colliders)
    {
        if (!Collider->QueryGpuComputeSupported())
        {
            return false;
        }
    }
    if (Integrator && !Integrator->QueryGpuComputeSupported())
    {
        return false;
    }
    return true;
}

void ParticleSimulation::ReclaimExpiredLifespans(float DeltaTimeSeconds) noexcept
{
    auto Lifespans = Channels.AccessChannel(ChannelLayouts::Lifespan);
    if (Lifespans.empty())
    {
        return;
    }

    uint32_t Slot = 0u;
    while (Slot < Channels.QueryActiveCount())
    {
        LifespanChannel& Life = Lifespans[Slot];
        Life.AgeSeconds += DeltaTimeSeconds;
        if (Life.MaxLifeSeconds > 0.0f && Life.AgeSeconds >= Life.MaxLifeSeconds)
        {
            if (Channels.QueryStoragePolicy() == ChannelStoragePolicy::DenseSwapRemove)
            {
                Channels.ReclaimSlot(Slot);
                // Re-inspect same Slot because last active slot was swapped here
                continue;
            }
            Life.LivenessMask = 0u;
        }
        ++Slot;
    }
}

void ParticleSimulation::ComputeReadbackSummary(double TickStartMicros) noexcept
{
    const uint32_t Active = Channels.QueryActiveCount();
    auto Kin              = Channels.QueryChannel(ChannelLayouts::Kinematics);

    float PeakSpeedSq = 0.0f;
    float SumX        = 0.0f;
    float SumY        = 0.0f;
    float SumZ        = 0.0f;
    if (!Kin.empty() && Active > 0u)
    {
        for (uint32_t Slot = 0u; Slot < Active; ++Slot)
        {
            const KinematicsChannel& Body = Kin[Slot];
            const float SpeedSq           = Body.VelX * Body.VelX + Body.VelY * Body.VelY + Body.VelZ * Body.VelZ;
            if (std::isfinite(SpeedSq) && SpeedSq > PeakSpeedSq)
            {
                PeakSpeedSq = SpeedSq;
            }
            SumX += Body.PosX;
            SumY += Body.PosY;
            SumZ += Body.PosZ;
        }
    }

    const float InvActive = (Active > 0u) ? (1.0f / static_cast<float>(Active)) : 0.0f;
    const float CenterX   = SumX * InvActive;
    const float CenterY   = SumY * InvActive;
    const float CenterZ   = SumZ * InvActive;

    float MaxRadiusSq = 0.0f;
    if (!Kin.empty() && Active > 0u)
    {
        for (uint32_t Slot = 0u; Slot < Active; ++Slot)
        {
            const float Dx  = Kin[Slot].PosX - CenterX;
            const float Dy  = Kin[Slot].PosY - CenterY;
            const float Dz  = Kin[Slot].PosZ - CenterZ;
            const float RSq = Dx * Dx + Dy * Dy + Dz * Dz;
            if (std::isfinite(RSq) && RSq > MaxRadiusSq)
            {
                MaxRadiusSq = RSq;
            }
        }
    }

    ReadbackSummary.AliveCount         = Active;
    ReadbackSummary.PeakSpeedMs        = std::sqrt(PeakSpeedSq);
    ReadbackSummary.BoundsRadiusMeters = std::sqrt(MaxRadiusSq);
    ReadbackSummary.TickDurationUs     = static_cast<float>(std::max(0.0, QueryClockMicros() - TickStartMicros));
    ReadbackSummary.BoundsCenterX      = CenterX;
    ReadbackSummary.BoundsCenterY      = CenterY;
    ReadbackSummary.BoundsCenterZ      = CenterZ;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           FLUENT C++20 SIMULATION SPECIFICATION
//------------------------------------------------------------------------------------------------------------------------

ParticleSimulationSpecification::ParticleSimulationSpecification(std::string SimulationLabel)
    : AssembledSimulation(std::move(SimulationLabel))
{
}

ParticleSimulationSpecification& ParticleSimulationSpecification::ConfigureCapacity(
    uint32_t             MaxCapacity,
    ChannelStoragePolicy Policy)
{
    AssembledSimulation.ConfigureCapacity(MaxCapacity, Policy);
    return *this;
}

ParticleSimulationSpecification& ParticleSimulationSpecification::AssignDeterministicSeed(uint32_t Seed) noexcept
{
    AssembledSimulation.AssignDeterministicSeed(Seed);
    return *this;
}

ParticleSimulationSpecification& ParticleSimulationSpecification::AssignExecutionTarget(ExecutionTarget Target) noexcept
{
    AssembledSimulation.AssignExecutionTarget(Target);
    return *this;
}

ParticleSimulationSpecification& ParticleSimulationSpecification::AttachCustomSource(
    std::string                           SourceLabel,
    CustomParticleSource::ChannelCallback DeclareCallback,
    CustomParticleSource::ReleaseCallback ReleaseCallback)
{
    AssembledSimulation.AttachSource(
        std::make_unique<CustomParticleSource>(
            std::move(SourceLabel),
            std::move(DeclareCallback),
            std::move(ReleaseCallback)));
    return *this;
}

ParticleSimulationSpecification& ParticleSimulationSpecification::AttachCustomMotionSolver(
    std::string                            SolverLabel,
    CustomParticleSolver::ChannelCallback  DeclareCallback,
    CustomParticleSolver::EvaluateCallback EvaluateCallback)
{
    AssembledSimulation.AttachMotionSolver(
        std::make_unique<CustomParticleSolver>(
            std::move(SolverLabel),
            std::move(DeclareCallback),
            std::move(EvaluateCallback)));
    return *this;
}

ParticleSimulation ParticleSimulationSpecification::Construct()
{
    return std::move(AssembledSimulation);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              PARTICLE SIMULATION REGISTRY
//------------------------------------------------------------------------------------------------------------------------

void ParticleSimulationRegistry::RegisterSimulation(uint32_t UniqueToken, ParticleSimulation Simulation)
{
    for (TokenizedEntry& Entry : Entries)
    {
        if (Entry.UniqueToken == UniqueToken)
        {
            Entry.Simulation = std::move(Simulation);
            return;
        }
    }
    Entries.push_back(TokenizedEntry{ UniqueToken, std::move(Simulation) });
}

bool ParticleSimulationRegistry::RemoveSimulation(uint32_t UniqueToken) noexcept
{
    for (std::size_t Index = 0u; Index < Entries.size(); ++Index)
    {
        if (Entries[Index].UniqueToken == UniqueToken)
        {
            Entries.erase(Entries.begin() + static_cast<std::ptrdiff_t>(Index));
            return true;
        }
    }
    return false;
}

void ParticleSimulationRegistry::ClearAllSimulations() noexcept
{
    Entries.clear();
}

void ParticleSimulationRegistry::TickAll(const SimulationTickInputs& Inputs)
{
    for (TokenizedEntry& Entry : Entries)
    {
        Entry.Simulation.Tick(Inputs);
    }
}

void ParticleSimulationRegistry::ResetAll() noexcept
{
    for (TokenizedEntry& Entry : Entries)
    {
        Entry.Simulation.Reset();
    }
}

void ParticleSimulationRegistry::TriggerBurst(uint32_t UniqueToken, uint32_t OverrideCount) noexcept
{
    if (ParticleSimulation* Sim = FindSimulation(UniqueToken))
    {
        Sim->TriggerBurst(OverrideCount);
    }
}

ParticleSimulation* ParticleSimulationRegistry::FindSimulation(uint32_t UniqueToken) noexcept
{
    for (TokenizedEntry& Entry : Entries)
    {
        if (Entry.UniqueToken == UniqueToken)
        {
            return &Entry.Simulation;
        }
    }
    return nullptr;
}

const ParticleSimulation* ParticleSimulationRegistry::FindSimulation(uint32_t UniqueToken) const noexcept
{
    for (const TokenizedEntry& Entry : Entries)
    {
        if (Entry.UniqueToken == UniqueToken)
        {
            return &Entry.Simulation;
        }
    }
    return nullptr;
}

uint32_t ParticleSimulationRegistry::QueryTotalAliveParticles() const noexcept
{
    uint32_t TotalAlive = 0u;
    for (const TokenizedEntry& Entry : Entries)
    {
        TotalAlive += Entry.Simulation.QueryActiveCount();
    }
    return TotalAlive;
}

} // namespace Frontier::SimulationSpace::Particles
