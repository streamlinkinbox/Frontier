/*============================================================================================================================================
                                                        PARTICLELAWINTERFACES.H
============================================================================================================================================*/
// 🧩 Modular C++20 interfaces for release rules, motion laws, colliders, integrators, deterministic PRNG, and 32 B readback telemetry

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/MathConstants.h"
#include "Engine/DistanceFieldSpace/SignedDistanceVolume.h"
#include "Engine/SimulationSpace/ForceFields/FieldLatticeCascade.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldAccumulator.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldDescriptors.h"
#include "Engine/SimulationSpace/Particles/ParticleChannelBank.h"
#include "Engine/SimulationSpace/Particles/ParticleStandardChannels.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace Frontier::SimulationSpace::Particles
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    DETERMINISTIC PRNG
//------------------------------------------------------------------------------------------------------------------------

struct ParticleRng
{
    uint32_t Cursor = 0x9E3779B9u; // [-] - Deterministic 32-bit xorshift/splitmix cursor

    explicit constexpr ParticleRng(uint32_t Seed = 1337u) noexcept
        : Cursor(Seed != 0u ? Seed : 0x9E3779B9u)
    {
    }

    [[nodiscard]] constexpr uint32_t NextUint32() noexcept
    {
        uint32_t Z = (Cursor += 0x9E3779B9u);
        Z          = (Z ^ (Z >> 16u)) * 0x85EBCA6Bu;
        Z          = (Z ^ (Z >> 13u)) * 0xC2B2AE35u;
        return Z ^ (Z >> 16u);
    }

    [[nodiscard]] constexpr float NextUnitFloat() noexcept
    {
        return static_cast<float>(NextUint32() & 0x00FFFFFFu) * (1.0f / 16777216.0f);
    }

    [[nodiscard]] constexpr float NextRange(float MinBound, float MaxBound) noexcept
    {
        return MinBound + (MaxBound - MinBound) * NextUnitFloat();
    }

    [[nodiscard]] ForceFields::SpatialVector3 NextUnitSphere() noexcept
    {
        const float Azimuth = Constants::TauFloat * NextUnitFloat();
        const float CosPol  = NextRange(-1.0f, 1.0f);
        const float SinPol  = std::sqrt(std::max(0.0f, 1.0f - CosPol * CosPol));
        return { SinPol * std::cos(Azimuth), SinPol * std::sin(Azimuth), CosPol };
    }

    [[nodiscard]] ForceFields::SpatialVector3 NextConeDirection(
        const ForceFields::SpatialVector3& Axis,
        float                              HalfAngleRad) noexcept
    {
        const float AxisLen = std::sqrt(Axis.X * Axis.X + Axis.Y * Axis.Y + Axis.Z * Axis.Z);
        ForceFields::SpatialVector3 W{ 0.0f, 0.0f, 1.0f };
        if (AxisLen > 1.0e-5f)
        {
            W = { Axis.X / AxisLen, Axis.Y / AxisLen, Axis.Z / AxisLen };
        }

        const ForceFields::SpatialVector3 Ref = (std::abs(W.Z) < 0.92f)
            ? ForceFields::SpatialVector3{ 0.0f, 0.0f, 1.0f }
            : ForceFields::SpatialVector3{ 1.0f, 0.0f, 0.0f };

        ForceFields::SpatialVector3 U{
            Ref.Y * W.Z - Ref.Z * W.Y,
            Ref.Z * W.X - Ref.X * W.Z,
            Ref.X * W.Y - Ref.Y * W.X,
        };
        const float ULen = std::sqrt(U.X * U.X + U.Y * U.Y + U.Z * U.Z);
        if (ULen > 1.0e-6f)
        {
            U = { U.X / ULen, U.Y / ULen, U.Z / ULen };
        }
        const ForceFields::SpatialVector3 V{
            W.Y * U.Z - W.Z * U.Y,
            W.Z * U.X - W.X * U.Z,
            W.X * U.Y - W.Y * U.X,
        };

        const float ClampedHalf = std::clamp(HalfAngleRad, 0.0f, Constants::PiFloat);
        const float CosMax      = std::cos(ClampedHalf);
        const float CosTheta    = CosMax + (1.0f - CosMax) * NextUnitFloat();
        const float SinTheta    = std::sqrt(std::max(0.0f, 1.0f - CosTheta * CosTheta));
        const float Phi         = Constants::TauFloat * NextUnitFloat();
        const float CosPhi      = std::cos(Phi);
        const float SinPhi      = std::sin(Phi);

        return {
            U.X * (SinTheta * CosPhi) + V.X * (SinTheta * SinPhi) + W.X * CosTheta,
            U.Y * (SinTheta * CosPhi) + V.Y * (SinTheta * SinPhi) + W.Y * CosTheta,
            U.Z * (SinTheta * CosPhi) + V.Z * (SinTheta * SinPhi) + W.Z * CosTheta,
        };
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                              INTERACTIVE COLLIDERS & TELEMETRY
//------------------------------------------------------------------------------------------------------------------------

struct DynamicSphereCollider
{
    ForceFields::SpatialVector3 CentreMeters  { 0.0f, 0.0f, 1.0f }; // [m]   - Sphere center (+Z Up)
    ForceFields::SpatialVector3 VelocityMs    { 0.0f, 0.0f, 0.0f }; // [m/s] - Instantaneous sphere velocity
    float                       RadiusMeters  = 0.35f;              // [m]   - Collision radius
    float                       Restitution   = 0.65f;              // [0-1] - Rebound elasticity
    bool                        Active        = false;              // [-]   - Collider enabled switch
};

// Exact 32-byte GPU-parity reduction readback summary feeding Inspector Hero & 32 B READ telemetry
struct alignas(16) ParticleReadbackSummary
{
    uint32_t AliveCount         = 0u;   // [count] - Active particle slots after step
    uint32_t TotalSpawnedCount  = 0u;   // [count] - Cumulative particles spawned since reset
    float    PeakSpeedMs        = 0.0f; // [m/s]   - Maximum linear velocity magnitude
    float    BoundsRadiusMeters = 0.0f; // [m]     - Bounding sphere radius from BoundsCenter

    float    BoundsCenterX      = 0.0f; // [m]     - Active cloud centroid X
    float    BoundsCenterY      = 0.0f; // [m]     - Active cloud centroid Y
    float    BoundsCenterZ      = 0.0f; // [m]     - Active cloud centroid Z
    float    StepDurationUs     = 0.0f; // [us]    - Measured CPU/GPU step duration in microseconds
};

static_assert(sizeof(ParticleReadbackSummary) == 32u, "ParticleReadbackSummary must remain 32 bytes for GPU readback parity.");

struct SimulationStepInputs
{
    double                                          TimeSeconds        = 0.0;           // [s]     - Simulation clock time
    float                                           DeltaTimeSeconds   = 1.0f / 60.0f;  // [s]     - Fixed step duration
    const ForceFields::ForceFieldAccumulator*       ForceAccumulator   = nullptr;       // [-]     - Optional direct C++ ForceFieldAccumulator
    const ForceFields::FieldLatticeCascade*         ForceLattice       = nullptr;       // [-]     - Optional 4-tier trilinear velocity lattice
    const DistanceFieldSpace::SignedDistanceVolume* WorldDistanceField = nullptr;       // [-]     - Optional Frontier world SDF obstacle volume
    std::span<const DynamicSphereCollider>          SphereColliders    {};              // [-]     - Optional dynamic throw/obstacle spheres
    uint64_t                                        TickIndex          = 0u;            // [count] - Monotonic fixed tick counter
    uint32_t                                        DeterministicSeed  = 1337u;         // [-]     - Master reproducible PRNG seed
};

//------------------------------------------------------------------------------------------------------------------------
//                                              MODULAR RULE INTERFACES
//------------------------------------------------------------------------------------------------------------------------

class IParticleReleaseRule
{
public:
    virtual ~IParticleReleaseRule() = default;

    virtual void DeclareRequiredChannels(ParticleChannelBank& Bank) const = 0;

    virtual uint32_t ExecuteRelease(
        ParticleChannelBank&        Bank,
        const SimulationStepInputs& Inputs,
        ParticleRng&                Rng) = 0;

    virtual void ResetReleaseClock() noexcept = 0;
    virtual void TriggerImmediateBurst(uint32_t OverrideCount = 0u) noexcept = 0;

    [[nodiscard]] virtual bool SupportsGpuCompute() const noexcept { return false; }
};

class IParticleMotionLaw
{
public:
    virtual ~IParticleMotionLaw() = default;

    virtual void DeclareRequiredChannels(ParticleChannelBank& Bank) const = 0;

    // Optional single-threaded pre-step before range dispatch (e.g. building spatial hash for Boids or updating orbiting pole).
    virtual void PrepareTick(
        ParticleChannelBank&        Bank,
        const SimulationStepInputs& Inputs)
    {
        (void)Bank;
        (void)Inputs;
    }

    // Disjoint-range parallelizable evaluation over slots [BeginSlot .. EndSlot).
    virtual void ApplyToRange(
        ParticleChannelBank&        Bank,
        uint32_t                    BeginSlot,
        uint32_t                    EndSlot,
        const SimulationStepInputs& Inputs) const = 0;

    // Optional post-range callback for single-threaded structural mutations (e.g. sub-burst spawns).
    virtual void FinalizeTick(
        ParticleChannelBank&        Bank,
        const SimulationStepInputs& Inputs,
        ParticleRng&                Rng)
    {
        (void)Bank;
        (void)Inputs;
        (void)Rng;
    }

    [[nodiscard]] virtual bool SupportsGpuCompute() const noexcept { return false; }
};

class IParticleCollisionRule
{
public:
    virtual ~IParticleCollisionRule() = default;

    virtual void DeclareRequiredChannels(ParticleChannelBank& Bank) const = 0;

    virtual void ResolveRange(
        ParticleChannelBank&        Bank,
        uint32_t                    BeginSlot,
        uint32_t                    EndSlot,
        const SimulationStepInputs& Inputs) const = 0;

    [[nodiscard]] virtual bool SupportsGpuCompute() const noexcept { return false; }
};

class IParticleIntegrator
{
public:
    virtual ~IParticleIntegrator() = default;

    virtual void DeclareRequiredChannels(ParticleChannelBank& Bank) const = 0;

    virtual void IntegrateRange(
        ParticleChannelBank&        Bank,
        uint32_t                    BeginSlot,
        uint32_t                    EndSlot,
        const SimulationStepInputs& Inputs) const = 0;

    [[nodiscard]] virtual bool SupportsGpuCompute() const noexcept { return true; }
};

//------------------------------------------------------------------------------------------------------------------------
//                                           INLINE C++ CUSTOM LAMBDA ADAPTERS
//------------------------------------------------------------------------------------------------------------------------

class CustomLambdaReleaseRule final : public IParticleReleaseRule
{
public:
    using DeclareFn = std::function<void(ParticleChannelBank&)>;
    using ExecuteFn = std::function<uint32_t(ParticleChannelBank&, const SimulationStepInputs&, ParticleRng&)>;

    CustomLambdaReleaseRule(std::string RuleLabel, DeclareFn InDeclare, ExecuteFn InExecute)
        : Label(std::move(RuleLabel))
        , DeclareCallback(std::move(InDeclare))
        , ExecuteCallback(std::move(InExecute))
    {
    }

    void DeclareRequiredChannels(ParticleChannelBank& Bank) const override
    {
        if (DeclareCallback)
        {
            DeclareCallback(Bank);
        }
    }

    uint32_t ExecuteRelease(
        ParticleChannelBank&        Bank,
        const SimulationStepInputs& Inputs,
        ParticleRng&                Rng) override
    {
        if (!ExecuteCallback)
        {
            return 0u;
        }
        return ExecuteCallback(Bank, Inputs, Rng);
    }

    void ResetReleaseClock() noexcept override {}
    void TriggerImmediateBurst(uint32_t /*OverrideCount*/) noexcept override {}

    [[nodiscard]] const std::string& QueryLabel() const noexcept { return Label; }

private:
    std::string Label           {};
    DeclareFn   DeclareCallback {};
    ExecuteFn   ExecuteCallback {};
};

class CustomLambdaMotionLaw final : public IParticleMotionLaw
{
public:
    using DeclareFn = std::function<void(ParticleChannelBank&)>;
    using ApplyFn   = std::function<void(ParticleChannelBank&, uint32_t, uint32_t, const SimulationStepInputs&)>;

    CustomLambdaMotionLaw(std::string LawLabel, DeclareFn InDeclare, ApplyFn InApply)
        : Label(std::move(LawLabel))
        , DeclareCallback(std::move(InDeclare))
        , ApplyCallback(std::move(InApply))
    {
    }

    void DeclareRequiredChannels(ParticleChannelBank& Bank) const override
    {
        if (DeclareCallback)
        {
            DeclareCallback(Bank);
        }
    }

    void ApplyToRange(
        ParticleChannelBank&        Bank,
        uint32_t                    BeginSlot,
        uint32_t                    EndSlot,
        const SimulationStepInputs& Inputs) const override
    {
        if (ApplyCallback)
        {
            ApplyCallback(Bank, BeginSlot, EndSlot, Inputs);
        }
    }

    [[nodiscard]] const std::string& QueryLabel() const noexcept { return Label; }

private:
    std::string Label           {};
    DeclareFn   DeclareCallback {};
    ApplyFn     ApplyCallback   {};
};

} // namespace Frontier::SimulationSpace::Particles
