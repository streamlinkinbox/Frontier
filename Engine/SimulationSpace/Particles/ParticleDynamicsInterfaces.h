/*============================================================================================================================================
                                                      PARTICLEDYNAMICSINTERFACES.H
============================================================================================================================================*/
// 🧩 Modular particle dynamics interfaces: sources, motion solvers, colliders, integrators, custom lambdas, and ForceFields/SDF tick inputs

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/MathConstants.h"
#include "Engine/DistanceFieldSpace/SignedDistanceVolume.h"
#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/SimulationSpace/ForceFields/FieldLatticeCascade.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldAccumulator.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldDescriptors.h"
#include "Engine/SimulationSpace/Particles/ParticleChannelBank.h"
#include "Engine/SimulationSpace/Particles/ParticleChannelLayouts.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <utility>

namespace Frontier::SimulationSpace::Particles
{

//------------------------------------------------------------------------------------------------------------------------
//                                               DETERMINISTIC PSEUDO-RANDOM GENERATOR
//------------------------------------------------------------------------------------------------------------------------

class ParticleRandomGenerator
{
public:
    explicit constexpr ParticleRandomGenerator(uint32_t Seed = 0x5EED1234u) noexcept
        : GeneratorSeed(Seed != 0u ? Seed : 0x5EED1234u)
    {
    }

    constexpr void AssignSeed(uint32_t Seed) noexcept
    {
        GeneratorSeed = (Seed != 0u) ? Seed : 0x5EED1234u;
    }

    [[nodiscard]] constexpr uint32_t NextUint32() noexcept
    {
        uint32_t Sample = GeneratorSeed;
        Sample ^= Sample << 13u;
        Sample ^= Sample >> 17u;
        Sample ^= Sample << 5u;
        GeneratorSeed = Sample;
        return Sample;
    }

    [[nodiscard]] constexpr float NextUnitFloat() noexcept
    {
        return static_cast<float>(NextUint32() >> 8u) * (1.0f / 16777216.0f);
    }

    [[nodiscard]] constexpr float NextRange(float MinimumBound, float MaximumBound) noexcept
    {
        return MinimumBound + (MaximumBound - MinimumBound) * NextUnitFloat();
    }

    [[nodiscard]] constexpr float NextSignedFloat() noexcept
    {
        return NextUnitFloat() * 2.0f - 1.0f;
    }

    [[nodiscard]] ForceFields::SpatialVector3 NextUnitSphere() noexcept
    {
        const float CosinePolar = NextSignedFloat();
        const float Azimuth     = NextUnitFloat() * Constants::TauFloat;
        const float SinePolar   = std::sqrt(std::max(0.0f, 1.0f - CosinePolar * CosinePolar));
        return { SinePolar * std::cos(Azimuth), SinePolar * std::sin(Azimuth), CosinePolar };
    }

    [[nodiscard]] ForceFields::SpatialVector3 NextConeDirection(
        const ForceFields::SpatialVector3& Axis,
        float                              HalfAngleRad) noexcept
    {
        const float AxisLength = std::sqrt(Axis.X * Axis.X + Axis.Y * Axis.Y + Axis.Z * Axis.Z);
        ForceFields::SpatialVector3 Forward{ 0.0f, 0.0f, 1.0f };
        if (AxisLength > 1.0e-6f)
        {
            const float InverseLength = 1.0f / AxisLength;
            Forward = { Axis.X * InverseLength, Axis.Y * InverseLength, Axis.Z * InverseLength };
        }

        const float ClampedAngle = std::clamp(HalfAngleRad, 0.0f, Constants::PiFloat);
        if (ClampedAngle <= 1.0e-5f)
        {
            return Forward;
        }

        const float MinimumCos = std::cos(ClampedAngle);
        const float CosTheta   = MinimumCos + (1.0f - MinimumCos) * NextUnitFloat();
        const float SinTheta   = std::sqrt(std::max(0.0f, 1.0f - CosTheta * CosTheta));
        const float Phi        = Constants::TauFloat * NextUnitFloat();

        const ForceFields::SpatialVector3 ReferenceAxis =
            (std::abs(Forward.Z) < 0.92f) ? ForceFields::SpatialVector3{ 0.0f, 0.0f, 1.0f } : ForceFields::SpatialVector3{ 1.0f, 0.0f, 0.0f };

        // Right = normalize(cross(ReferenceAxis, Forward))
        float RightX = ReferenceAxis.Y * Forward.Z - ReferenceAxis.Z * Forward.Y;
        float RightY = ReferenceAxis.Z * Forward.X - ReferenceAxis.X * Forward.Z;
        float RightZ = ReferenceAxis.X * Forward.Y - ReferenceAxis.Y * Forward.X;
        const float RightLen = std::sqrt(RightX * RightX + RightY * RightY + RightZ * RightZ);
        if (RightLen > 1.0e-6f)
        {
            const float InvRight = 1.0f / RightLen;
            RightX *= InvRight;
            RightY *= InvRight;
            RightZ *= InvRight;
        }

        // Bitangent = cross(Forward, Right)
        const float BitangentX = Forward.Y * RightZ - Forward.Z * RightY;
        const float BitangentY = Forward.Z * RightX - Forward.X * RightZ;
        const float BitangentZ = Forward.X * RightY - Forward.Y * RightX;

        const float CosPhi = std::cos(Phi);
        const float SinPhi = std::sin(Phi);
        return {
            Forward.X * CosTheta + (RightX * CosPhi + BitangentX * SinPhi) * SinTheta,
            Forward.Y * CosTheta + (RightY * CosPhi + BitangentY * SinPhi) * SinTheta,
            Forward.Z * CosTheta + (RightZ * CosPhi + BitangentZ * SinPhi) * SinTheta,
        };
    }

private:
    uint32_t GeneratorSeed = 0x5EED1234u; // [-] - Xorshift32 seed register
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 INTERACTIVE SPHERE COLLIDER
//------------------------------------------------------------------------------------------------------------------------

struct alignas(16) DynamicSphereCollider
{
    ForceFields::SpatialVector3 CentreMeters { 0.0f, 0.0f, 1.0f }; // [m]   - Sphere world center (+Z Up)
    float                       RadiusMeters = 0.45f;              // [m]   - Collision sphere radius
    ForceFields::SpatialVector3 VelocityMs   { 0.0f, 0.0f, 0.0f }; // [m/s] - Thrown / animated sphere velocity
    float                       Restitution  = 0.45f;              // [0-1] - Elastic bounce coefficient
    float                       ImpulseScale = 1.0f;               // [x]   - Velocity transfer gain
    bool                        Active       = false;              // [-]   - Whether collider participates this tick
};

//------------------------------------------------------------------------------------------------------------------------
//                                              32-BYTE ASYNC READBACK TELEMETRY
//------------------------------------------------------------------------------------------------------------------------

struct alignas(16) ParticleReadbackSummary
{
    uint32_t AliveCount         = 0u;   // [count] - Active particle count after tick
    float    PeakSpeedMs        = 0.0f; // [m/s]   - Maximum linear speed across active slots
    float    BoundsRadiusMeters = 0.0f; // [m]     - Radial bounding extent around centroid
    float    TickDurationUs     = 0.0f; // [us]    - Measured tick wall duration in microseconds
    float    BoundsCenterX      = 0.0f; // [m]     - Active cloud centroid X
    float    BoundsCenterY      = 0.0f; // [m]     - Active cloud centroid Y
    float    BoundsCenterZ      = 0.0f; // [m]     - Active cloud centroid Z
    uint32_t RevisionCounter    = 0u;   // [tick]  - Monotonic fixed-step tick counter
};

static_assert(sizeof(ParticleReadbackSummary) == 32u, "ParticleReadbackSummary must remain exactly 32 bytes.");

//------------------------------------------------------------------------------------------------------------------------
//                                                  PER-TICK SIMULATION INPUTS
//------------------------------------------------------------------------------------------------------------------------

struct SimulationTickInputs
{
    double                                          TimeSeconds        = 0.0;        // [s]    - Accumulated simulation time
    float                                           DeltaTimeSeconds   = 1.0f / 60.0f; // [s]  - Fixed simulation substep
    const ForceFields::ForceFieldAccumulator*       ForceAccumulator   = nullptr;    // [-]    - Optional live C++ ForceFields accumulator
    const ForceFields::FieldLatticeCascade*         ForceLattice       = nullptr;    // [-]    - Optional pre-baked 3D clipmap cascade
    const DistanceFieldSpace::SignedDistanceVolume* WorldDistanceField = nullptr;    // [-]    - Optional sparse 3D SDF volume
    std::span<const DynamicSphereCollider>          SphereColliders    {};           // [-]    - Optional dynamic interactive spheres
    uint64_t                                        TickIndex          = 0u;         // [tick] - Current simulation tick number
};

//------------------------------------------------------------------------------------------------------------------------
//                                            COMPOSABLE PARTICLE DYNAMICS INTERFACES
//------------------------------------------------------------------------------------------------------------------------

class ParticleSource
{
public:
    virtual ~ParticleSource() = default;

    [[nodiscard]] virtual const char* QueryDebugName() const noexcept           = 0;
    [[nodiscard]] virtual bool        QueryGpuComputeSupported() const noexcept { return true; }

    virtual void     DeclareRequiredChannels(ParticleChannelBank& Bank) const = 0;
    virtual uint32_t ReleaseParticles(
        ParticleChannelBank&        Bank,
        const SimulationTickInputs& Inputs,
        ParticleRandomGenerator&    Generator) = 0;

    virtual void TriggerImmediateBurst(uint32_t /*OverrideCount*/ = 0u) noexcept {}
    virtual void ResetSource() noexcept {}
};

class ParticleMotionSolver
{
public:
    virtual ~ParticleMotionSolver() = default;

    [[nodiscard]] virtual const char* QueryDebugName() const noexcept           = 0;
    [[nodiscard]] virtual bool        QueryGpuComputeSupported() const noexcept { return true; }

    virtual void DeclareRequiredChannels(ParticleChannelBank& Bank) const = 0;

    // Optional per-tick initialization before range evaluation (e.g. updating an orbiting magnetic pole).
    virtual void InitializeTick(ParticleChannelBank& /*Bank*/, const SimulationTickInputs& /*Inputs*/) {}

    // Evaluates this motion solver over contiguous particle slots [BeginSlot .. EndSlot).
    virtual void EvaluateRange(
        ParticleChannelBank&        Bank,
        uint32_t                    BeginSlot,
        uint32_t                    EndSlot,
        const SimulationTickInputs& Inputs) const = 0;

    // Optional post-range finalization for single-threaded structural mutations (e.g. sub-bursts or XPBD constraints).
    virtual void FinalizeTick(
        ParticleChannelBank&        /*Bank*/,
        const SimulationTickInputs& /*Inputs*/,
        ParticleRandomGenerator&    /*Generator*/)
    {
    }

    virtual void ResetSolver() noexcept {}
};

class ParticleCollider
{
public:
    virtual ~ParticleCollider() = default;

    [[nodiscard]] virtual const char* QueryDebugName() const noexcept           = 0;
    [[nodiscard]] virtual bool        QueryGpuComputeSupported() const noexcept { return true; }

    virtual void DeclareRequiredChannels(ParticleChannelBank& Bank) const = 0;
    virtual void EnforceRange(
        ParticleChannelBank&        Bank,
        uint32_t                    BeginSlot,
        uint32_t                    EndSlot,
        const SimulationTickInputs& Inputs) const = 0;
};

class ParticleIntegrator
{
public:
    virtual ~ParticleIntegrator() = default;

    [[nodiscard]] virtual const char* QueryDebugName() const noexcept           = 0;
    [[nodiscard]] virtual bool        QueryGpuComputeSupported() const noexcept { return true; }

    virtual void DeclareRequiredChannels(ParticleChannelBank& Bank) const = 0;
    virtual void AdvanceRange(
        ParticleChannelBank&        Bank,
        uint32_t                    BeginSlot,
        uint32_t                    EndSlot,
        const SimulationTickInputs& Inputs) const = 0;
};

//------------------------------------------------------------------------------------------------------------------------
//                                           INLINE C++ CUSTOM LAMBDA ADAPTERS
//------------------------------------------------------------------------------------------------------------------------

class CustomParticleSource final : public ParticleSource
{
public:
    using ChannelCallback = std::function<void(ParticleChannelBank&)>;
    using ReleaseCallback = std::function<uint32_t(ParticleChannelBank&, const SimulationTickInputs&, ParticleRandomGenerator&)>;

    CustomParticleSource(std::string Name, ChannelCallback DeclareChannels, ReleaseCallback ReleaseParticlesFunc)
        : Label(std::move(Name))
        , ChannelDeclaration(std::move(DeclareChannels))
        , ParticleRelease(std::move(ReleaseParticlesFunc))
    {
    }

    [[nodiscard]] const char* QueryDebugName() const noexcept override { return Label.c_str(); }
    [[nodiscard]] bool        QueryGpuComputeSupported() const noexcept override { return false; }

    void DeclareRequiredChannels(ParticleChannelBank& Bank) const override
    {
        if (ChannelDeclaration)
        {
            ChannelDeclaration(Bank);
        }
    }

    uint32_t ReleaseParticles(
        ParticleChannelBank&        Bank,
        const SimulationTickInputs& Inputs,
        ParticleRandomGenerator&    Generator) override
    {
        return ParticleRelease ? ParticleRelease(Bank, Inputs, Generator) : 0u;
    }

private:
    std::string     Label              {};
    ChannelCallback ChannelDeclaration {};
    ReleaseCallback ParticleRelease    {};
};

class CustomParticleSolver final : public ParticleMotionSolver
{
public:
    using ChannelCallback  = std::function<void(ParticleChannelBank&)>;
    using EvaluateCallback = std::function<void(ParticleChannelBank&, uint32_t, uint32_t, const SimulationTickInputs&)>;

    CustomParticleSolver(std::string Name, ChannelCallback DeclareChannels, EvaluateCallback RangeEvalFunc)
        : Label(std::move(Name))
        , ChannelDeclaration(std::move(DeclareChannels))
        , RangeEvaluation(std::move(RangeEvalFunc))
    {
    }

    [[nodiscard]] const char* QueryDebugName() const noexcept override { return Label.c_str(); }
    [[nodiscard]] bool        QueryGpuComputeSupported() const noexcept override { return false; }

    void DeclareRequiredChannels(ParticleChannelBank& Bank) const override
    {
        if (ChannelDeclaration)
        {
            ChannelDeclaration(Bank);
        }
    }

    void EvaluateRange(
        ParticleChannelBank&        Bank,
        uint32_t                    BeginSlot,
        uint32_t                    EndSlot,
        const SimulationTickInputs& Inputs) const override
    {
        if (RangeEvaluation)
        {
            RangeEvaluation(Bank, BeginSlot, EndSlot, Inputs);
        }
    }

private:
    std::string      Label              {};
    ChannelCallback  ChannelDeclaration {};
    EvaluateCallback RangeEvaluation    {};
};

} // namespace Frontier::SimulationSpace::Particles
