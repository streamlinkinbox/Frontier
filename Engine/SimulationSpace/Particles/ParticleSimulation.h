/*============================================================================================================================================
                                                          PARTICLESIMULATION.H
============================================================================================================================================*/
// 🧩 Composable ParticleSimulation, fluent ParticleSimulationSpecification (native + custom C++ types & lambdas), and ParticleSimulationRegistry

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Particles/ParticleChannelBank.h"
#include "Engine/SimulationSpace/Particles/ParticleDynamics.h"
#include "Engine/SimulationSpace/Particles/ParticleKinematicsSolvers.h"
#include "Engine/SimulationSpace/Particles/ParticleVulkanExecutor.h"

#include <concepts>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace Frontier::SimulationSpace::Particles
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ExecutionTarget : uint8_t
{
    AutoSelect    = 0u, // [-] - Select Vulkan compute when eligible; seamlessly run CPU otherwise
    CpuParallel   = 1u, // [-] - Explicit multi-threaded / serial C++ CPU execution
    VulkanCompute = 2u, // [-] - Request GPU compute execution when all channels & solvers support it
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      PARTICLE SIMULATION
//------------------------------------------------------------------------------------------------------------------------

class ParticleSimulation
{
public:
    explicit ParticleSimulation(std::string SimulationLabel = "ParticleSimulation");

    ParticleSimulation(const ParticleSimulation&)                = delete;
    ParticleSimulation& operator=(const ParticleSimulation&)     = delete;
    ParticleSimulation(ParticleSimulation&&) noexcept            = default;
    ParticleSimulation& operator=(ParticleSimulation&&) noexcept = default;

    void ConfigureCapacity(uint32_t MaxCapacity, ChannelStoragePolicy Policy = ChannelStoragePolicy::DenseSwapRemove);
    void AssignDeterministicSeed(uint32_t Seed) noexcept;
    void AssignExecutionTarget(ExecutionTarget Target) noexcept { RequestedTarget = Target; }
    void AttachVulkanExecutor(ParticleVulkanExecutor* Executor, bool SyncChannelsFromGpu = true) noexcept;
    void DetachVulkanExecutor() noexcept;

    void AttachSource(std::unique_ptr<ParticleSource> Source);
    void AttachMotionSolver(std::unique_ptr<ParticleMotionSolver> Solver);
    void AttachCollider(std::unique_ptr<ParticleCollider> Collider);
    void AssignIntegrator(std::unique_ptr<ParticleIntegrator> StepIntegrator);

    template <typename AttributeT>
        requires std::is_trivially_copyable_v<AttributeT> && std::is_default_constructible_v<AttributeT>
    std::span<AttributeT> RequireChannel(const ChannelTag<AttributeT>& Tag)
    {
        return Channels.RequireChannel(Tag);
    }

    template <typename AttributeT>
        requires std::is_trivially_copyable_v<AttributeT> && std::is_default_constructible_v<AttributeT>
    [[nodiscard]] std::span<AttributeT> AccessChannel(const ChannelTag<AttributeT>& Tag) noexcept
    {
        return Channels.AccessChannel(Tag);
    }

    template <typename AttributeT>
        requires std::is_trivially_copyable_v<AttributeT> && std::is_default_constructible_v<AttributeT>
    [[nodiscard]] std::span<const AttributeT> QueryChannel(const ChannelTag<AttributeT>& Tag) const noexcept
    {
        return Channels.QueryChannel(Tag);
    }

    // Executes one fixed movement tick: Release -> Initialize -> Motion Solvers -> Integrate -> Colliders -> Finalize -> Reclaim -> 32 B Telemetry
    void Tick(const SimulationTickInputs& Inputs);
    void TriggerBurst(uint32_t OverrideCount = 0u) noexcept;
    void Reset(uint32_t OverrideSeed = 0u) noexcept;

    [[nodiscard]] const std::string&             QueryLabel() const noexcept { return Label; }
    [[nodiscard]] ParticleChannelBank&           AccessChannelBank() noexcept { return Channels; }
    [[nodiscard]] const ParticleChannelBank&     QueryChannelBank() const noexcept { return Channels; }
    [[nodiscard]] const ParticleReadbackSummary& QueryReadbackSummary() const noexcept { return ReadbackSummary; }
    [[nodiscard]] uint32_t                       QueryActiveCount() const noexcept { return Channels.QueryActiveCount(); }
    [[nodiscard]] uint32_t                       QueryCapacity() const noexcept { return Channels.QueryCapacity(); }
    [[nodiscard]] uint32_t                       QueryDeterministicSeed() const noexcept { return MasterSeed; }
    [[nodiscard]] ExecutionTarget                QueryExecutionTarget() const noexcept { return RequestedTarget; }
    [[nodiscard]] bool                           QueryGpuComputeEligible() const noexcept;
    [[nodiscard]] bool                           QueryLastTickExecutedOnGpu() const noexcept { return LastTickExecutedOnGpu; }
    [[nodiscard]] ParticleVulkanExecutor*        QueryAttachedVulkanExecutor() const noexcept { return AttachedExecutor; }
    [[nodiscard]] const std::string&             QueryLastGpuDiagnostic() const noexcept { return LastGpuDiagnostic; }

private:
    bool TryExecuteTickOnVulkanGpu(const SimulationTickInputs& SafeInputs, uint32_t ReleasedSlotsThisTick);
    void ReclaimExpiredLifespans(float DeltaTimeSeconds) noexcept;
    void ComputeReadbackSummary(double TickStartMicros) noexcept;

    std::string                                        Label                 {};
    ParticleChannelBank                                Channels              {};
    std::vector<std::unique_ptr<ParticleSource>>       Sources               {};
    std::vector<std::unique_ptr<ParticleMotionSolver>> MotionSolvers         {};
    std::vector<std::unique_ptr<ParticleCollider>>     Colliders             {};
    std::unique_ptr<ParticleIntegrator>                Integrator            {};
    ParticleRandomGenerator                            RandomGenerator       {};
    uint32_t                                           MasterSeed            = 1337u;
    ExecutionTarget                                    RequestedTarget       = ExecutionTarget::AutoSelect;
    ParticleReadbackSummary                            ReadbackSummary       {};
    ParticleVulkanExecutor*                            AttachedExecutor      = nullptr;
    bool                                               SyncChannelsFromGpu   = true;
    bool                                               GpuStateSynchronized  = false;
    bool                                               LastTickExecutedOnGpu = false;
    const DistanceFieldSpace::SignedDistanceVolume*    LastUploadedSdf       = nullptr;
    std::string                                        LastGpuDiagnostic     {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                           FLUENT C++20 SIMULATION SPECIFICATION
//------------------------------------------------------------------------------------------------------------------------

class ParticleSimulationSpecification
{
public:
    explicit ParticleSimulationSpecification(std::string SimulationLabel = "ParticleSimulation");

    ParticleSimulationSpecification& ConfigureCapacity(
        uint32_t             MaxCapacity,
        ChannelStoragePolicy Policy = ChannelStoragePolicy::DenseSwapRemove);

    ParticleSimulationSpecification& AssignDeterministicSeed(uint32_t Seed) noexcept;
    ParticleSimulationSpecification& AssignExecutionTarget(ExecutionTarget Target) noexcept;

    template <typename AttributeT>
        requires std::is_trivially_copyable_v<AttributeT> && std::is_default_constructible_v<AttributeT>
    ParticleSimulationSpecification& RequireCustomChannel(const ChannelTag<AttributeT>& Tag)
    {
        (void)AssembledSimulation.RequireChannel(Tag);
        return *this;
    }

    template <typename SourceT, typename... ArgsT>
        requires std::derived_from<SourceT, ParticleSource>
    ParticleSimulationSpecification& AttachSource(ArgsT&&... Args)
    {
        AssembledSimulation.AttachSource(std::make_unique<SourceT>(std::forward<ArgsT>(Args)...));
        return *this;
    }

    template <typename SolverT, typename... ArgsT>
        requires std::derived_from<SolverT, ParticleMotionSolver>
    ParticleSimulationSpecification& AttachMotionSolver(ArgsT&&... Args)
    {
        AssembledSimulation.AttachMotionSolver(std::make_unique<SolverT>(std::forward<ArgsT>(Args)...));
        return *this;
    }

    template <typename ColliderT, typename... ArgsT>
        requires std::derived_from<ColliderT, ParticleCollider>
    ParticleSimulationSpecification& AttachCollider(ArgsT&&... Args)
    {
        AssembledSimulation.AttachCollider(std::make_unique<ColliderT>(std::forward<ArgsT>(Args)...));
        return *this;
    }

    template <typename IntegratorT, typename... ArgsT>
        requires std::derived_from<IntegratorT, ParticleIntegrator>
    ParticleSimulationSpecification& AssignIntegrator(ArgsT&&... Args)
    {
        AssembledSimulation.AssignIntegrator(std::make_unique<IntegratorT>(std::forward<ArgsT>(Args)...));
        return *this;
    }

    ParticleSimulationSpecification& AttachCustomSource(
        std::string                           SourceLabel,
        CustomParticleSource::ChannelCallback DeclareCallback,
        CustomParticleSource::ReleaseCallback ReleaseCallback);

    ParticleSimulationSpecification& AttachCustomMotionSolver(
        std::string                            SolverLabel,
        CustomParticleSolver::ChannelCallback  DeclareCallback,
        CustomParticleSolver::EvaluateCallback EvaluateCallback);

    [[nodiscard]] ParticleSimulation Construct();

private:
    ParticleSimulation AssembledSimulation;
};

//------------------------------------------------------------------------------------------------------------------------
//                                              PARTICLE SIMULATION REGISTRY
//------------------------------------------------------------------------------------------------------------------------

class ParticleSimulationRegistry
{
public:
    ParticleSimulationRegistry() = default;

    void RegisterSimulation(uint32_t UniqueToken, ParticleSimulation Simulation);
    bool RemoveSimulation(uint32_t UniqueToken) noexcept;
    void ClearAllSimulations() noexcept;

    void TickAll(const SimulationTickInputs& Inputs);
    void ResetAll() noexcept;
    void TriggerBurst(uint32_t UniqueToken, uint32_t OverrideCount = 0u) noexcept;

    [[nodiscard]] ParticleSimulation*       FindSimulation(uint32_t UniqueToken) noexcept;
    [[nodiscard]] const ParticleSimulation* FindSimulation(uint32_t UniqueToken) const noexcept;
    [[nodiscard]] uint32_t                  QuerySimulationCount() const noexcept { return static_cast<uint32_t>(Entries.size()); }
    [[nodiscard]] uint32_t                  QueryTotalAliveParticles() const noexcept;

private:
    struct TokenizedEntry
    {
        uint32_t           UniqueToken = 0u;
        ParticleSimulation Simulation  {};
    };

    std::vector<TokenizedEntry> Entries{};
};

} // namespace Frontier::SimulationSpace::Particles
