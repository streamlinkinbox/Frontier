/*============================================================================================================================================
                                                         PARTICLESIMULATION.CPP
============================================================================================================================================*/
// 🧩 Implementation of ParticleSimulation fixed-step runner, fluent ParticleSimulationBuilder, 32 B telemetry reduction, and ParticleSimulationDirector

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
//                                                      PARTICLE SIMULATION
//------------------------------------------------------------------------------------------------------------------------

ParticleSimulation::ParticleSimulation(std::string InLabel)
    : Label(std::move(InLabel))
    , Integrator(std::make_unique<SemiImplicitEulerIntegrator>())
    , Rng(MasterSeed)
{
}

void ParticleSimulation::ConfigureCapacity(uint32_t MaxCapacity, ChannelStoragePolicy Policy)
{
    Channels.ConfigureCapacity(MaxCapacity, Policy);
}

void ParticleSimulation::AssignDeterministicSeed(uint32_t Seed) noexcept
{
    MasterSeed = (Seed != 0u) ? Seed : 1337u;
    Rng        = ParticleRng(MasterSeed);
}

void ParticleSimulation::AttachReleaseRule(std::unique_ptr<IParticleReleaseRule> Rule)
{
    if (!Rule)
    {
        return;
    }
    Rule->DeclareRequiredChannels(Channels);
    ReleaseRules.push_back(std::move(Rule));
}

void ParticleSimulation::AttachMotionLaw(std::unique_ptr<IParticleMotionLaw> Law)
{
    if (!Law)
    {
        return;
    }
    Law->DeclareRequiredChannels(Channels);
    MotionLaws.push_back(std::move(Law));
}

void ParticleSimulation::AttachCollisionRule(std::unique_ptr<IParticleCollisionRule> Collider)
{
    if (!Collider)
    {
        return;
    }
    Collider->DeclareRequiredChannels(Channels);
    CollisionRules.push_back(std::move(Collider));
}

void ParticleSimulation::AssignIntegrator(std::unique_ptr<IParticleIntegrator> InIntegrator)
{
    if (!InIntegrator)
    {
        return;
    }
    InIntegrator->DeclareRequiredChannels(Channels);
    Integrator = std::move(InIntegrator);
}

void ParticleSimulation::Step(const SimulationStepInputs& Inputs)
{
    const auto StartClock = std::chrono::steady_clock::now();

    SimulationStepInputs EffectiveInputs = Inputs;
    EffectiveInputs.DeltaTimeSeconds     = std::clamp(Inputs.DeltaTimeSeconds, 1.0e-5f, 0.1f);

    // 1. Release / Spawn phase
    for (std::unique_ptr<IParticleReleaseRule>& Rule : ReleaseRules)
    {
        const uint32_t Spawned = Rule->ExecuteRelease(Channels, EffectiveInputs, Rng);
        ReadbackSummary.TotalSpawnedCount += Spawned;
    }

    const uint32_t ActiveBefore = Channels.QueryActiveCount();
    if (ActiveBefore > 0u)
    {
        // 2. Single-threaded PrepareTick phase (e.g. spatial snapshots, orbiting dipole update, impact detection)
        for (std::unique_ptr<IParticleMotionLaw>& Law : MotionLaws)
        {
            Law->PrepareTick(Channels, EffectiveInputs);
        }

        // 3. Disjoint-range Motion Law evaluation (partitioned in 256-slot cache-friendly tiles)
        constexpr uint32_t TileSlots = 256u;
        for (std::unique_ptr<IParticleMotionLaw>& Law : MotionLaws)
        {
            for (uint32_t Begin = 0u; Begin < ActiveBefore; Begin += TileSlots)
            {
                const uint32_t End = std::min(ActiveBefore, Begin + TileSlots);
                Law->ApplyToRange(Channels, Begin, End, EffectiveInputs);
            }
        }

        // 4. Time Integration phase
        if (Integrator)
        {
            for (uint32_t Begin = 0u; Begin < ActiveBefore; Begin += TileSlots)
            {
                const uint32_t End = std::min(ActiveBefore, Begin + TileSlots);
                Integrator->IntegrateRange(Channels, Begin, End, EffectiveInputs);
            }
        }

        // 5. Post-integration structural/constraint FinalizeTick phase (XPBD projections, sub-burst spawns)
        for (std::unique_ptr<IParticleMotionLaw>& Law : MotionLaws)
        {
            Law->FinalizeTick(Channels, EffectiveInputs, Rng);
        }

        // 6. Collision resolution over current active range (including any newly spawned sub-bursts)
        const uint32_t ActiveAfterFinalize = Channels.QueryActiveCount();
        for (std::unique_ptr<IParticleCollisionRule>& Collider : CollisionRules)
        {
            for (uint32_t Begin = 0u; Begin < ActiveAfterFinalize; Begin += TileSlots)
            {
                const uint32_t End = std::min(ActiveAfterFinalize, Begin + TileSlots);
                Collider->ResolveRange(Channels, Begin, End, EffectiveInputs);
            }
        }

        // 7. Lifespan aging & synchronized channel retirement
        AdvanceLifespansAndRetire(EffectiveInputs.DeltaTimeSeconds);
    }

    const auto EndClock      = std::chrono::steady_clock::now();
    const double ElapsedUs   = std::chrono::duration<double, std::micro>(EndClock - StartClock).count();
    ComputeReadbackSummary(ElapsedUs);
}

void ParticleSimulation::TriggerBurst(uint32_t OverrideCount) noexcept
{
    for (std::unique_ptr<IParticleReleaseRule>& Rule : ReleaseRules)
    {
        Rule->TriggerImmediateBurst(OverrideCount);
    }
}

void ParticleSimulation::Reset(uint32_t OverrideSeed) noexcept
{
    if (OverrideSeed != 0u)
    {
        MasterSeed = OverrideSeed;
    }
    Rng = ParticleRng(MasterSeed);
    Channels.ResetAllChannels();
    for (std::unique_ptr<IParticleReleaseRule>& Rule : ReleaseRules)
    {
        Rule->ResetReleaseClock();
    }
    ReadbackSummary = ParticleReadbackSummary{};
}

bool ParticleSimulation::QueryEligibleForGpuCompute() const noexcept
{
    if (!Channels.QueryAllChannelsGpuResident())
    {
        return false;
    }
    for (const std::unique_ptr<IParticleReleaseRule>& Rule : ReleaseRules)
    {
        if (!Rule->SupportsGpuCompute())
        {
            return false;
        }
    }
    for (const std::unique_ptr<IParticleMotionLaw>& Law : MotionLaws)
    {
        if (!Law->SupportsGpuCompute())
        {
            return false;
        }
    }
    for (const std::unique_ptr<IParticleCollisionRule>& Collider : CollisionRules)
    {
        if (!Collider->SupportsGpuCompute())
        {
            return false;
        }
    }
    return Integrator ? Integrator->SupportsGpuCompute() : true;
}

void ParticleSimulation::AdvanceLifespansAndRetire(float DeltaTimeSeconds) noexcept
{
    auto Life = Channels.AccessChannel(StandardChannels::Lifespan);
    if (Life.empty())
    {
        return;
    }

    uint32_t Slot = 0u;
    while (Slot < Channels.QueryActiveCount())
    {
        LifespanChannel& Entry = Life[Slot];
        Entry.AgeSeconds      += DeltaTimeSeconds;
        if (Entry.MaxLifeSeconds > 0.0f && Entry.AgeSeconds >= Entry.MaxLifeSeconds)
        {
            Channels.RetireSlot(Slot);
            if (Channels.QueryStoragePolicy() == ChannelStoragePolicy::FixedSlotOrder)
            {
                ++Slot;
            }
            continue;
        }
        ++Slot;
    }
}

void ParticleSimulation::ComputeReadbackSummary(double ElapsedMicros) noexcept
{
    const uint32_t Active = Channels.QueryActiveCount();
    ReadbackSummary.AliveCount     = Active;
    ReadbackSummary.StepDurationUs = static_cast<float>(ElapsedMicros);

    const auto Kin = Channels.QueryChannel(StandardChannels::Kinematics);
    if (Active == 0u || Kin.empty())
    {
        ReadbackSummary.PeakSpeedMs        = 0.0f;
        ReadbackSummary.BoundsRadiusMeters = 0.0f;
        ReadbackSummary.BoundsCenterX      = 0.0f;
        ReadbackSummary.BoundsCenterY      = 0.0f;
        ReadbackSummary.BoundsCenterZ      = 0.0f;
        return;
    }

    double SumX   = 0.0;
    double SumY   = 0.0;
    double SumZ   = 0.0;
    float  PeakSq = 0.0f;
    for (uint32_t Slot = 0u; Slot < Active; ++Slot)
    {
        SumX += static_cast<double>(Kin[Slot].PosX);
        SumY += static_cast<double>(Kin[Slot].PosY);
        SumZ += static_cast<double>(Kin[Slot].PosZ);

        const float SpdSq = Kin[Slot].VelX * Kin[Slot].VelX
                          + Kin[Slot].VelY * Kin[Slot].VelY
                          + Kin[Slot].VelZ * Kin[Slot].VelZ;
        PeakSq = std::max(PeakSq, SpdSq);
    }

    const float InvN = 1.0f / static_cast<float>(Active);
    const float Cx   = static_cast<float>(SumX) * InvN;
    const float Cy   = static_cast<float>(SumY) * InvN;
    const float Cz   = static_cast<float>(SumZ) * InvN;

    float MaxDistSq = 0.0f;
    for (uint32_t Slot = 0u; Slot < Active; ++Slot)
    {
        const float Dx = Kin[Slot].PosX - Cx;
        const float Dy = Kin[Slot].PosY - Cy;
        const float Dz = Kin[Slot].PosZ - Cz;
        MaxDistSq      = std::max(MaxDistSq, Dx * Dx + Dy * Dy + Dz * Dz);
    }

    ReadbackSummary.PeakSpeedMs        = std::sqrt(PeakSq);
    ReadbackSummary.BoundsCenterX      = Cx;
    ReadbackSummary.BoundsCenterY      = Cy;
    ReadbackSummary.BoundsCenterZ      = Cz;
    ReadbackSummary.BoundsRadiusMeters = std::sqrt(MaxDistSq);
}

//------------------------------------------------------------------------------------------------------------------------
//                                               FLUENT C++20 SIMULATION BUILDER
//------------------------------------------------------------------------------------------------------------------------

ParticleSimulationBuilder::ParticleSimulationBuilder(std::string InLabel)
    : BuiltSimulation(std::move(InLabel))
{
}

ParticleSimulationBuilder& ParticleSimulationBuilder::ConfigureCapacity(uint32_t MaxCapacity, ChannelStoragePolicy Policy)
{
    BuiltSimulation.ConfigureCapacity(MaxCapacity, Policy);
    return *this;
}

ParticleSimulationBuilder& ParticleSimulationBuilder::WithDeterministicSeed(uint32_t Seed) noexcept
{
    BuiltSimulation.AssignDeterministicSeed(Seed);
    return *this;
}

ParticleSimulationBuilder& ParticleSimulationBuilder::WithExecutionTarget(ExecutionTarget Target) noexcept
{
    BuiltSimulation.AssignExecutionTarget(Target);
    return *this;
}

ParticleSimulationBuilder& ParticleSimulationBuilder::WithCustomRelease(
    std::string                        RuleLabel,
    CustomLambdaReleaseRule::DeclareFn DeclareCallback,
    CustomLambdaReleaseRule::ExecuteFn ExecuteCallback)
{
    BuiltSimulation.AttachReleaseRule(std::make_unique<CustomLambdaReleaseRule>(
        std::move(RuleLabel),
        std::move(DeclareCallback),
        std::move(ExecuteCallback)));
    return *this;
}

ParticleSimulationBuilder& ParticleSimulationBuilder::WithCustomLaw(
    std::string                      LawLabel,
    CustomLambdaMotionLaw::DeclareFn DeclareCallback,
    CustomLambdaMotionLaw::ApplyFn   ApplyCallback)
{
    BuiltSimulation.AttachMotionLaw(std::make_unique<CustomLambdaMotionLaw>(
        std::move(LawLabel),
        std::move(DeclareCallback),
        std::move(ApplyCallback)));
    return *this;
}

ParticleSimulation ParticleSimulationBuilder::Construct()
{
    return std::move(BuiltSimulation);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              PARTICLE SIMULATION DIRECTOR
//------------------------------------------------------------------------------------------------------------------------

void ParticleSimulationDirector::RegisterSimulation(uint32_t UniqueToken, ParticleSimulation Simulation)
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

bool ParticleSimulationDirector::RemoveSimulation(uint32_t UniqueToken) noexcept
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

void ParticleSimulationDirector::ClearAllSimulations() noexcept
{
    Entries.clear();
}

void ParticleSimulationDirector::StepAll(const SimulationStepInputs& Inputs)
{
    for (TokenizedEntry& Entry : Entries)
    {
        Entry.Simulation.Step(Inputs);
    }
}

void ParticleSimulationDirector::ResetAll() noexcept
{
    for (TokenizedEntry& Entry : Entries)
    {
        Entry.Simulation.Reset();
    }
}

void ParticleSimulationDirector::TriggerBurst(uint32_t UniqueToken, uint32_t OverrideCount) noexcept
{
    if (ParticleSimulation* Sim = FindSimulation(UniqueToken))
    {
        Sim->TriggerBurst(OverrideCount);
    }
}

ParticleSimulation* ParticleSimulationDirector::FindSimulation(uint32_t UniqueToken) noexcept
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

const ParticleSimulation* ParticleSimulationDirector::FindSimulation(uint32_t UniqueToken) const noexcept
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

uint32_t ParticleSimulationDirector::QueryTotalAliveParticles() const noexcept
{
    uint32_t Total = 0u;
    for (const TokenizedEntry& Entry : Entries)
    {
        Total += Entry.Simulation.QueryActiveCount();
    }
    return Total;
}

} // namespace Frontier::SimulationSpace::Particles
