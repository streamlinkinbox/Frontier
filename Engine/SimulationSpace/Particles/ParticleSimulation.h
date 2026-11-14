/*============================================================================================================================================
                                                          PARTICLESIMULATION.H
============================================================================================================================================*/
// 🧩 Composable ParticleSimulation, fluent ParticleSimulationBuilder (built-in + custom C++ types & lambdas), and ParticleSimulationDirector

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Particles/ParticleBuiltInLaws.h"
#include "Engine/SimulationSpace/Particles/ParticleChannelBank.h"
#include "Engine/SimulationSpace/Particles/ParticleLawInterfaces.h"

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
    VulkanCompute = 2u, // [-] - Request GPU compute execution when all channels & laws support it
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      PARTICLE SIMULATION
//------------------------------------------------------------------------------------------------------------------------

class ParticleSimulation
{
public:
    explicit ParticleSimulation(std::string InLabel = "ParticleSimulation");

    ParticleSimulation(const ParticleSimulation&)            = delete;
    ParticleSimulation& operator=(const ParticleSimulation&) = delete;
    ParticleSimulation(ParticleSimulation&&) noexcept        = default;
    ParticleSimulation& operator=(ParticleSimulation&&) noexcept = default;

    void ConfigureCapacity(uint32_t MaxCapacity, ChannelStoragePolicy Policy = ChannelStoragePolicy::DenseSwapRemove);
    void AssignDeterministicSeed(uint32_t Seed) noexcept;
    void AssignExecutionTarget(ExecutionTarget Target) noexcept { RequestedTarget = Target; }

    void AttachReleaseRule(std::unique_ptr<IParticleReleaseRule> Rule);
    void AttachMotionLaw(std::unique_ptr<IParticleMotionLaw> Law);
    void AttachCollisionRule(std::unique_ptr<IParticleCollisionRule> Collider);
    void AssignIntegrator(std::unique_ptr<IParticleIntegrator> InIntegrator);

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

    // Executes one fixed movement tick: Release -> Prepare -> Motion Laws -> Integrate -> Colliders -> Finalize -> Age/Retire -> 32 B Telemetry
    void Step(const SimulationStepInputs& Inputs);
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
    [[nodiscard]] bool                           QueryEligibleForGpuCompute() const noexcept;

private:
    void AdvanceLifespansAndRetire(float DeltaTimeSeconds) noexcept;
    void ComputeReadbackSummary(double StepStartMicros) noexcept;

    std::string                                           Label           {};
    ParticleChannelBank                                   Channels        {};
    std::vector<std::unique_ptr<IParticleReleaseRule>>    ReleaseRules    {};
    std::vector<std::unique_ptr<IParticleMotionLaw>>      MotionLaws      {};
    std::vector<std::unique_ptr<IParticleCollisionRule>>  CollisionRules  {};
    std::unique_ptr<IParticleIntegrator>                  Integrator      {};
    ParticleRng                                           Rng             {};
    uint32_t                                              MasterSeed      = 1337u;
    ExecutionTarget                                       RequestedTarget = ExecutionTarget::AutoSelect;
    ParticleReadbackSummary                               ReadbackSummary {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                               FLUENT C++20 SIMULATION BUILDER
//------------------------------------------------------------------------------------------------------------------------

class ParticleSimulationBuilder
{
public:
    explicit ParticleSimulationBuilder(std::string InLabel = "ParticleSimulation");

    ParticleSimulationBuilder& ConfigureCapacity(
        uint32_t             MaxCapacity,
        ChannelStoragePolicy Policy = ChannelStoragePolicy::DenseSwapRemove);

    ParticleSimulationBuilder& WithDeterministicSeed(uint32_t Seed) noexcept;
    ParticleSimulationBuilder& WithExecutionTarget(ExecutionTarget Target) noexcept;

    template <typename AttributeT>
        requires std::is_trivially_copyable_v<AttributeT> && std::is_default_constructible_v<AttributeT>
    ParticleSimulationBuilder& RequireCustomChannel(const ChannelTag<AttributeT>& Tag)
    {
        (void)BuiltSimulation.RequireChannel(Tag);
        return *this;
    }

    template <typename RuleT, typename... ArgsT>
        requires std::derived_from<RuleT, IParticleReleaseRule>
    ParticleSimulationBuilder& WithReleaseRule(ArgsT&&... Args)
    {
        BuiltSimulation.AttachReleaseRule(std::make_unique<RuleT>(std::forward<ArgsT>(Args)...));
        return *this;
    }

    template <typename LawT, typename... ArgsT>
        requires std::derived_from<LawT, IParticleMotionLaw>
    ParticleSimulationBuilder& WithMotionLaw(ArgsT&&... Args)
    {
        BuiltSimulation.AttachMotionLaw(std::make_unique<LawT>(std::forward<ArgsT>(Args)...));
        return *this;
    }

    template <typename ColliderT, typename... ArgsT>
        requires std::derived_from<ColliderT, IParticleCollisionRule>
    ParticleSimulationBuilder& WithCollider(ArgsT&&... Args)
    {
        BuiltSimulation.AttachCollisionRule(std::make_unique<ColliderT>(std::forward<ArgsT>(Args)...));
        return *this;
    }

    template <typename IntegratorT, typename... ArgsT>
        requires std::derived_from<IntegratorT, IParticleIntegrator>
    ParticleSimulationBuilder& WithIntegrator(ArgsT&&... Args)
    {
        BuiltSimulation.AssignIntegrator(std::make_unique<IntegratorT>(std::forward<ArgsT>(Args)...));
        return *this;
    }

    ParticleSimulationBuilder& WithCustomRelease(
        std::string                        RuleLabel,
        CustomLambdaReleaseRule::DeclareFn DeclareCallback,
        CustomLambdaReleaseRule::ExecuteFn ExecuteCallback);

    ParticleSimulationBuilder& WithCustomLaw(
        std::string                      LawLabel,
        CustomLambdaMotionLaw::DeclareFn DeclareCallback,
        CustomLambdaMotionLaw::ApplyFn   ApplyCallback);

    [[nodiscard]] ParticleSimulation Construct();

private:
    ParticleSimulation BuiltSimulation;
};

//------------------------------------------------------------------------------------------------------------------------
//                                              PARTICLE SIMULATION DIRECTOR
//------------------------------------------------------------------------------------------------------------------------

class ParticleSimulationDirector
{
public:
    ParticleSimulationDirector() = default;

    void     RegisterSimulation(uint32_t UniqueToken, ParticleSimulation Simulation);
    bool     RemoveSimulation(uint32_t UniqueToken) noexcept;
    void     ClearAllSimulations() noexcept;

    void     StepAll(const SimulationStepInputs& Inputs);
    void     ResetAll() noexcept;
    void     TriggerBurst(uint32_t UniqueToken, uint32_t OverrideCount = 0u) noexcept;

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
