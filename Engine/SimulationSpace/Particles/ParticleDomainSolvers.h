/*============================================================================================================================================
                                                        PARTICLEDOMAINSOLVERS.H
============================================================================================================================================*/
// 🧩 Modular specialized domain release rules and motion laws (XPBD Strands/Cloth/Flora, Rosensweig Ferrofluid, Fireworks, Boids, Fracture, MoGraph, Shield, Space)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Particles/ParticleLawInterfaces.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier::SimulationSpace::Particles
{

//------------------------------------------------------------------------------------------------------------------------
//                                      1. XPBD STRANDS, CLOTH, VINES, FLORA & TREES
//------------------------------------------------------------------------------------------------------------------------

enum class StrandLayoutPattern : uint8_t
{
    ClothFlagGrid    = 0u, // [-] - Left-mast pinned rectangular cloth grid with lateral weft links
    HangingCurtain   = 1u, // [-] - Top-bar pinned curtains, cables, chains, vines & chimes
    UpwardFloraField = 2u, // [-] - Bottom-rooted grass, wheat, reeds, ferns & kelp blades
    RadialWeb        = 3u, // [-] - Concentric orb-weave spiderweb spokes & rings
    CanopyTree       = 4u, // [-] - Anchored trunk -> limb -> canopy branch hierarchy
};

struct StrandGridParameters
{
    StrandLayoutPattern         Pattern           = StrandLayoutPattern::ClothFlagGrid;
    ForceFields::SpatialVector3 OriginMeters      { 0.0f, 0.0f, 1.5f }; // [m]   - Root / anchor origin (+Z Up)
    uint32_t                    StrandCount       = 16u;                // [cnt] - Number of strands / rows
    uint32_t                    SegmentsPerStrand = 10u;                // [cnt] - Vertices per strand
    float                       WidthMeters       = 2.4f;               // [m]   - Lateral span across strands
    float                       StrandLengthMeters = 1.8f;              // [m]   - Length of each strand
    float                       CatenarySagMeters = 0.25f;              // [m]   - Initial gravity drape sag
    float                       RadiusMeters      = 0.035f;             // [m]   - Visual strand thickness
};

class StrandGridReleaseRule final : public IParticleReleaseRule
{
public:
    explicit StrandGridReleaseRule(const StrandGridParameters& InParams = {}) noexcept
        : Params(InParams)
    {
    }

    void     DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    uint32_t ExecuteRelease(ParticleChannelBank& Bank, const SimulationStepInputs& Inputs, ParticleRng& Rng) override;
    void     ResetReleaseClock() noexcept override { Initialized = false; }
    void     TriggerImmediateBurst(uint32_t /*OverrideCount*/) noexcept override { Initialized = false; }
    [[nodiscard]] bool SupportsGpuCompute() const noexcept override { return true; }

private:
    StrandGridParameters Params      {};
    bool                 Initialized = false;
};

struct XpbdStrandSolverParameters
{
    float    ElasticStiffness = 1.15f; // [x]   - Constraint stiffness [0.1..3.0]
    float    WaveSpeedHz      = 1.6f;  // [Hz]  - Travelling wind / current flutter rate
    float    WaveAmplitudeMs2 = 1.2f;  // [m/s2]- Harmonic wind ripple excitation
    float    TearSpeedMs      = 8.5f;  // [m/s] - Impact speed above which strand links snap (0 = untearable)
    float    Damping          = 0.18f; // [1/s] - Internal fiber damping
    uint32_t SubIterations    = 5u;    // [cnt] - XPBD projection iterations per tick
};

class XpbdStrandSolverLaw final : public IParticleMotionLaw
{
public:
    explicit XpbdStrandSolverLaw(const XpbdStrandSolverParameters& InParams = {}) noexcept
        : Params(InParams)
    {
    }

    void DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    void ApplyToRange(ParticleChannelBank& Bank, uint32_t BeginSlot, uint32_t EndSlot, const SimulationStepInputs& Inputs) const override;
    void FinalizeTick(ParticleChannelBank& Bank, const SimulationStepInputs& Inputs, ParticleRng& Rng) override;
    [[nodiscard]] bool SupportsGpuCompute() const noexcept override { return true; }

    XpbdStrandSolverParameters& AccessParameters() noexcept { return Params; }

private:
    XpbdStrandSolverParameters Params{};
};

//------------------------------------------------------------------------------------------------------------------------
//                                           2. ROSENSWEIG MAGNETIC FERROFLUID
//------------------------------------------------------------------------------------------------------------------------

struct FerrofluidParameters
{
    ForceFields::SpatialVector3 OriginMeters       { 0.0f, 0.0f, 0.15f }; // [m]     - Dish or levitated orb center
    uint8_t                     VesselForm         = 1u;                  // [1|2]   - 1=Petri Dish, 2=Levitated Orb
    uint32_t                    SpikeCount         = 192u;                // [cnt]   - Active magnetic surface cusps
    float                       VesselRadiusMeters = 1.15f;               // [m]     - Dish / orb radius
    float                       OrbitSpeedRadSec   = 1.15f;               // [rad/s] - Neodymium pole orbit speed
    float                       MaxCuspHeightMeters = 0.92f;              // [m]     - Peak Rosensweig cusp elevation
    float                       CapillaryDamping   = 5.2f;                // [1/s]   - Surface tension damping
    bool                        ManualMagnetPole   = false;               // [-]     - Manual pole override switch
    ForceFields::SpatialVector3 ManualPoleMeters   { 0.45f, 0.0f, 0.95f };// [m]     - Manual pole position
};

class FerrofluidSurfaceReleaseRule final : public IParticleReleaseRule
{
public:
    explicit FerrofluidSurfaceReleaseRule(const FerrofluidParameters& InParams = {}) noexcept
        : Params(InParams)
    {
    }

    void     DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    uint32_t ExecuteRelease(ParticleChannelBank& Bank, const SimulationStepInputs& Inputs, ParticleRng& Rng) override;
    void     ResetReleaseClock() noexcept override { Initialized = false; }
    void     TriggerImmediateBurst(uint32_t /*OverrideCount*/) noexcept override { Initialized = false; }
    [[nodiscard]] bool SupportsGpuCompute() const noexcept override { return true; }

private:
    FerrofluidParameters Params      {};
    bool                 Initialized = false;
};

class RosensweigFerrofluidLaw final : public IParticleMotionLaw
{
public:
    explicit RosensweigFerrofluidLaw(const FerrofluidParameters& InParams = {}) noexcept
        : Params(InParams)
    {
    }

    void DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    void PrepareTick(ParticleChannelBank& Bank, const SimulationStepInputs& Inputs) override;
    void ApplyToRange(ParticleChannelBank& Bank, uint32_t BeginSlot, uint32_t EndSlot, const SimulationStepInputs& Inputs) const override;
    [[nodiscard]] bool SupportsGpuCompute() const noexcept override { return true; }

    FerrofluidParameters& AccessParameters() noexcept { return Params; }
    [[nodiscard]] ForceFields::SpatialVector3 QueryEvaluatedPoleMeters() const noexcept { return EvaluatedPoleMeters; }

private:
    FerrofluidParameters        Params              {};
    ForceFields::SpatialVector3 EvaluatedPoleMeters { 0.45f, 0.0f, 0.95f };
};

//------------------------------------------------------------------------------------------------------------------------
//                                       3. MULTI-TIER HANABI FIREWORKS & SKY TEXT
//------------------------------------------------------------------------------------------------------------------------

struct PyroVolleyParameters
{
    ForceFields::SpatialVector3 LaunchOriginMeters  { 0.0f, 0.0f, 0.2f }; // [m]   - Mortar launch origin
    float                       VolleyIntervalSec   = 1.15f;              // [s]   - Interval between rocket launches
    float                       AscentSpeedMs       = 11.5f;              // [m/s] - Initial mortar ascent speed
    float                       BurstSpeedMs        = 5.8f;               // [m/s] - Radial star burst speed
    uint32_t                    StarsPerBurst       = 72u;                // [cnt] - Secondary stars per detonation
    uint32_t                    PatternCode         = 6u;                 // [idx] - 0..6 (GrandFinale=6, SkyText=5)
    std::string                 SkyText             { "2026" };           // [-]   - Custom sky-writing message
};

class PyroVolleyReleaseRule final : public IParticleReleaseRule
{
public:
    explicit PyroVolleyReleaseRule(const PyroVolleyParameters& InParams = {}) noexcept
        : Params(InParams)
    {
    }

    void     DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    uint32_t ExecuteRelease(ParticleChannelBank& Bank, const SimulationStepInputs& Inputs, ParticleRng& Rng) override;
    void     ResetReleaseClock() noexcept override;
    void     TriggerImmediateBurst(uint32_t OverrideCount = 0u) noexcept override;

private:
    PyroVolleyParameters Params             {};
    float                IntervalAccumSec   = 0.0f;
    uint32_t             PendingRockets     = 2u;
    uint32_t             VolleyCounter      = 0u;
};

class PyroVolleyBurstLaw final : public IParticleMotionLaw
{
public:
    explicit PyroVolleyBurstLaw(const PyroVolleyParameters& InParams = {}) noexcept
        : Params(InParams)
    {
    }

    void DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    void ApplyToRange(ParticleChannelBank& Bank, uint32_t BeginSlot, uint32_t EndSlot, const SimulationStepInputs& Inputs) const override;
    void FinalizeTick(ParticleChannelBank& Bank, const SimulationStepInputs& Inputs, ParticleRng& Rng) override;

    PyroVolleyParameters& AccessParameters() noexcept { return Params; }

private:
    PyroVolleyParameters Params{};
};

//------------------------------------------------------------------------------------------------------------------------
//                                           4. BOIDS MURMURATION & FLOCKING
//------------------------------------------------------------------------------------------------------------------------

struct BoidsFlockParameters
{
    ForceFields::SpatialVector3 HomeOriginMeters  { 0.0f, 0.0f, 2.2f }; // [m]   - Roost / flock home center
    float                       HomeRadiusMeters  = 4.2f;               // [m]   - Soft home containment envelope
    float                       PerceptionMeters  = 1.6f;               // [m]   - Neighbor perception radius
    float                       SeparationMeters  = 0.55f;              // [m]   - Personal space separation radius
    float                       CruiseSpeedMs     = 3.8f;               // [m/s] - Target cruise flight speed
    float                       MaxSteerAccelMs2  = 9.5f;               // [m/s2]- Steering maneuver authority
    uint8_t                     CreatureVariant   = 0u;                 // [0..2]- 0=Birds, 1=Butterflies, 2=Fish
    uint32_t                    FlockCount        = 180u;               // [cnt] - Initial flock size
};

class BoidsFlockReleaseRule final : public IParticleReleaseRule
{
public:
    explicit BoidsFlockReleaseRule(const BoidsFlockParameters& InParams = {}) noexcept
        : Params(InParams)
    {
    }

    void     DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    uint32_t ExecuteRelease(ParticleChannelBank& Bank, const SimulationStepInputs& Inputs, ParticleRng& Rng) override;
    void     ResetReleaseClock() noexcept override { Initialized = false; }
    void     TriggerImmediateBurst(uint32_t /*OverrideCount*/) noexcept override { Initialized = false; }

private:
    BoidsFlockParameters Params      {};
    bool                 Initialized = false;
};

class BoidsFlockLaw final : public IParticleMotionLaw
{
public:
    explicit BoidsFlockLaw(const BoidsFlockParameters& InParams = {}) noexcept
        : Params(InParams)
    {
    }

    void DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    void PrepareTick(ParticleChannelBank& Bank, const SimulationStepInputs& Inputs) override;
    void ApplyToRange(ParticleChannelBank& Bank, uint32_t BeginSlot, uint32_t EndSlot, const SimulationStepInputs& Inputs) const override;

    BoidsFlockParameters& AccessParameters() noexcept { return Params; }

private:
    struct CachedBoidPosition
    {
        float PosX = 0.0f;
        float PosY = 0.0f;
        float PosZ = 0.0f;
        float VelX = 0.0f;
        float VelY = 0.0f;
        float VelZ = 0.0f;
    };

    BoidsFlockParameters            Params      {};
    std::vector<CachedBoidPosition> Snapshot    {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                  5. GLACIAL & SEISMIC FRACTURE PROPAGATION
//------------------------------------------------------------------------------------------------------------------------

struct FractureCrackParameters
{
    ForceFields::SpatialVector3 OriginMeters     { 0.0f, 0.0f, 0.1f }; // [m] - Fracture sheet origin
    float                       SheetRadiusMeters = 2.6f;              // [m] - Slab radius
    float                       IncisionDepthMeters = 0.45f;           // [m] - Maximum crack depth
    float                       BranchDensity    = 1.25f;              // [x] - Crack branching speed multiplier
    uint8_t                     Pattern          = 1u;                 // [1|2]- 1=Glacial Ice, 2=Seismic Quake
    uint32_t                    CellCount        = 256u;               // [cnt]- Fracture cell shards
};

class FractureSurfaceReleaseRule final : public IParticleReleaseRule
{
public:
    explicit FractureSurfaceReleaseRule(const FractureCrackParameters& InParams = {}) noexcept
        : Params(InParams)
    {
    }

    void     DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    uint32_t ExecuteRelease(ParticleChannelBank& Bank, const SimulationStepInputs& Inputs, ParticleRng& Rng) override;
    void     ResetReleaseClock() noexcept override { Initialized = false; }
    void     TriggerImmediateBurst(uint32_t /*OverrideCount*/) noexcept override { Initialized = false; }

private:
    FractureCrackParameters Params      {};
    bool                    Initialized = false;
};

class FractureCrackLaw final : public IParticleMotionLaw
{
public:
    explicit FractureCrackLaw(const FractureCrackParameters& InParams = {}) noexcept
        : Params(InParams)
    {
    }

    void DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    void ApplyToRange(ParticleChannelBank& Bank, uint32_t BeginSlot, uint32_t EndSlot, const SimulationStepInputs& Inputs) const override;

    FractureCrackParameters& AccessParameters() noexcept { return Params; }

private:
    FractureCrackParameters Params{};
};

//------------------------------------------------------------------------------------------------------------------------
//                                    6. MOGRAPH EFFECTOR LATTICE & DEREZ WAVES
//------------------------------------------------------------------------------------------------------------------------

struct MoGraphEffectorParameters
{
    ForceFields::SpatialVector3 OriginMeters     { 0.0f, 0.0f, 0.4f }; // [m] - Lattice base origin
    uint32_t                    GridX            = 18u;                // [cnt]- Columns along X
    uint32_t                    GridY            = 18u;                // [cnt]- Rows along Y
    float                       SpacingMeters    = 0.22f;              // [m] - Inter-specimen grid spacing
    float                       FalloffMeters    = 2.1f;               // [m] - Travelling effector falloff radius
    float                       ExtrusionMeters  = 1.4f;               // [m] - Peak +Z height modulation
    float                       WaveFrequencyHz  = 0.85f;              // [Hz]- Effector sweep speed
    bool                        EnableDerezLaunch = false;             // [-] - Launch specimens ballistically when wave crests
};

class LatticeGridReleaseRule final : public IParticleReleaseRule
{
public:
    explicit LatticeGridReleaseRule(const MoGraphEffectorParameters& InParams = {}) noexcept
        : Params(InParams)
    {
    }

    void     DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    uint32_t ExecuteRelease(ParticleChannelBank& Bank, const SimulationStepInputs& Inputs, ParticleRng& Rng) override;
    void     ResetReleaseClock() noexcept override { Initialized = false; }
    void     TriggerImmediateBurst(uint32_t /*OverrideCount*/) noexcept override { Initialized = false; }
    [[nodiscard]] bool SupportsGpuCompute() const noexcept override { return true; }

private:
    MoGraphEffectorParameters Params      {};
    bool                      Initialized = false;
};

class MoGraphEffectorLaw final : public IParticleMotionLaw
{
public:
    explicit MoGraphEffectorLaw(const MoGraphEffectorParameters& InParams = {}) noexcept
        : Params(InParams)
    {
    }

    void DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    void ApplyToRange(ParticleChannelBank& Bank, uint32_t BeginSlot, uint32_t EndSlot, const SimulationStepInputs& Inputs) const override;
    [[nodiscard]] bool SupportsGpuCompute() const noexcept override { return true; }

    MoGraphEffectorParameters& AccessParameters() noexcept { return Params; }

private:
    MoGraphEffectorParameters Params{};
};

//------------------------------------------------------------------------------------------------------------------------
//                                    7. ENERGY SHIELD BARRIER & SPACE REFRACTION
//------------------------------------------------------------------------------------------------------------------------

struct EnergyShieldParameters
{
    ForceFields::SpatialVector3 CentreMeters    { 0.0f, 0.0f, 1.4f }; // [m]   - Barrier center
    float                       RadiusMeters    = 1.65f;              // [m]   - Barrier envelope radius
    float                       RippleSpeedHz   = 2.4f;               // [Hz]  - Geodesic impact ripple wave speed
    uint8_t                     GeometryVariant = 1u;                 // [0..3]- 0=Sphere, 1=HexDome, 2=Prism, 3=Cylinder
    uint32_t                    CellCount       = 320u;               // [cnt] - Tessellated barrier cells
};

class ShieldBarrierReleaseRule final : public IParticleReleaseRule
{
public:
    explicit ShieldBarrierReleaseRule(const EnergyShieldParameters& InParams = {}) noexcept
        : Params(InParams)
    {
    }

    void     DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    uint32_t ExecuteRelease(ParticleChannelBank& Bank, const SimulationStepInputs& Inputs, ParticleRng& Rng) override;
    void     ResetReleaseClock() noexcept override { Initialized = false; }
    void     TriggerImmediateBurst(uint32_t /*OverrideCount*/) noexcept override { Initialized = false; }

private:
    EnergyShieldParameters Params      {};
    bool                   Initialized = false;
};

class EnergyShieldRippleLaw final : public IParticleMotionLaw
{
public:
    explicit EnergyShieldRippleLaw(const EnergyShieldParameters& InParams = {}) noexcept
        : Params(InParams)
    {
    }

    void DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    void PrepareTick(ParticleChannelBank& Bank, const SimulationStepInputs& Inputs) override;
    void ApplyToRange(ParticleChannelBank& Bank, uint32_t BeginSlot, uint32_t EndSlot, const SimulationStepInputs& Inputs) const override;

    EnergyShieldParameters& AccessParameters() noexcept { return Params; }

private:
    EnergyShieldParameters      Params            {};
    ForceFields::SpatialVector3 LastImpactPoint   { 1.65f, 0.0f, 1.4f };
    double                      LastImpactTimeSec = -10.0;
};

struct SpaceRefractionParameters
{
    ForceFields::SpatialVector3 SingularityMeters { 0.0f, 0.0f, 1.6f }; // [m]   - Orbital core / thermal column center
    float                       OrbitalStrength   = 7.5f;               // [m3/s2]- Keplerian swirl gain (0 = thermal rise)
    float                       InnerRadiusMeters = 0.45f;              // [m]   - Innermost stable orbit regularization
    float                       ThermalRiseMs     = 0.0f;               // [m/s] - Rising thermal plume velocity
};

class SpaceRefractionLaw final : public IParticleMotionLaw
{
public:
    explicit SpaceRefractionLaw(const SpaceRefractionParameters& InParams = {}) noexcept
        : Params(InParams)
    {
    }

    void DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    void ApplyToRange(ParticleChannelBank& Bank, uint32_t BeginSlot, uint32_t EndSlot, const SimulationStepInputs& Inputs) const override;
    [[nodiscard]] bool SupportsGpuCompute() const noexcept override { return true; }

private:
    SpaceRefractionParameters Params{};
};

} // namespace Frontier::SimulationSpace::Particles
