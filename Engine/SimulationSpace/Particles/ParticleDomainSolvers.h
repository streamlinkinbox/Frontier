/*============================================================================================================================================
                                                        PARTICLEDOMAINSOLVERS.H
============================================================================================================================================*/
// 🧩 Modular specialized domain sources and solvers (XPBD Strands/Cloth/Flora, Rosensweig Ferrofluid, Fireworks, Boids, Fracture, MoGraph, Shield, Space)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Particles/ParticleDynamics.h"

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
    ClothBannerGrid  = 0u, // [-] - Left-mast pinned rectangular cloth banner with lateral weft links
    HangingCurtain   = 1u, // [-] - Top-bar pinned curtains, cables, chains, vines & chimes
    UpwardFloraField = 2u, // [-] - Bottom-rooted grass, wheat, reeds, ferns & kelp blades
    RadialWeb        = 3u, // [-] - Concentric orb-weave spiderweb spokes & rings
    CanopyTree       = 4u, // [-] - Anchored trunk -> limb -> canopy branch layout
};

struct StrandGridParameters
{
    StrandLayoutPattern         Pattern            = StrandLayoutPattern::ClothBannerGrid;
    ForceFields::SpatialVector3 OriginMeters       { 0.0f, 0.0f, 1.5f }; // [m]   - Root / anchor origin (+Z Up)
    uint32_t                    StrandCount        = 16u;                // [cnt] - Number of strands / rows
    uint32_t                    SegmentsPerStrand  = 10u;                // [cnt] - Vertices per strand
    float                       WidthMeters        = 2.4f;               // [m]   - Lateral span across strands
    float                       StrandLengthMeters = 1.8f;               // [m]   - Length of each strand
    float                       CatenarySagMeters  = 0.25f;              // [m]   - Initial gravity drape sag
    float                       RadiusMeters       = 0.035f;             // [m]   - Visual strand thickness
};

class StrandGridSource final : public ParticleSource
{
public:
    explicit StrandGridSource(const StrandGridParameters& SourceSettings = {}) noexcept
        : Settings(SourceSettings)
    {
    }

    [[nodiscard]] const char* QueryDebugName() const noexcept override { return "StrandGridSource"; }
    [[nodiscard]] bool        QueryGpuComputeSupported() const noexcept override { return true; }

    void     DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    uint32_t ReleaseParticles(ParticleChannelBank& Bank, const SimulationTickInputs& Inputs, ParticleRandomGenerator& Generator) override;
    void     ResetSource() noexcept override { Initialized = false; }
    void     TriggerImmediateBurst(uint32_t /*OverrideCount*/) noexcept override { Initialized = false; }

private:
    StrandGridParameters Settings    {};
    bool                 Initialized = false;
};

struct XpbdStrandSolverParameters
{
    float    ElasticStiffness = 1.15f; // [x]    - Constraint stiffness [0.1..3.0]
    float    WaveSpeedHz      = 1.6f;  // [Hz]   - Travelling wind / current flutter rate
    float    WaveAmplitudeMs2 = 1.2f;  // [m/s2] - Harmonic wind ripple excitation
    float    TearSpeedMs      = 8.5f;  // [m/s]  - Impact speed above which strand links snap (0 = untearable)
    float    Damping          = 0.18f; // [1/s]  - Internal fiber damping
    uint32_t SubIterations    = 5u;    // [cnt]  - XPBD projection iterations per tick
};

class XpbdStrandSolver final : public ParticleMotionSolver
{
public:
    explicit XpbdStrandSolver(const XpbdStrandSolverParameters& SolverSettings = {}) noexcept
        : Settings(SolverSettings)
    {
    }

    [[nodiscard]] const char* QueryDebugName() const noexcept override { return "XpbdStrandSolver"; }
    [[nodiscard]] bool        QueryGpuComputeSupported() const noexcept override { return true; }

    void DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    void EvaluateRange(ParticleChannelBank& Bank, uint32_t BeginSlot, uint32_t EndSlot, const SimulationTickInputs& Inputs) const override;
    void FinalizeTick(ParticleChannelBank& Bank, const SimulationTickInputs& Inputs, ParticleRandomGenerator& Generator) override;

    XpbdStrandSolverParameters& AccessParameters() noexcept { return Settings; }

private:
    XpbdStrandSolverParameters Settings{};
};

//------------------------------------------------------------------------------------------------------------------------
//                                           2. ROSENSWEIG MAGNETIC FERROFLUID
//------------------------------------------------------------------------------------------------------------------------

struct FerrofluidParameters
{
    ForceFields::SpatialVector3 OriginMeters        { 0.0f, 0.0f, 0.15f }; // [m]     - Dish or levitated orb center
    uint8_t                     VesselForm          = 1u;                  // [1|2]   - 1=Petri Dish, 2=Levitated Orb
    uint32_t                    SpikeCount          = 192u;                // [cnt]   - Active magnetic surface cusps
    float                       VesselRadiusMeters  = 1.15f;               // [m]     - Dish / orb radius
    float                       OrbitSpeedRadSec    = 1.15f;               // [rad/s] - Neodymium pole orbit speed
    float                       MaxCuspHeightMeters = 0.92f;               // [m]     - Peak Rosensweig cusp elevation
    float                       CapillaryDamping    = 5.2f;                // [1/s]   - Surface tension damping
    bool                        ManualMagnetPole    = false;               // [-]     - Manual pole override switch
    ForceFields::SpatialVector3 ManualPoleMeters    { 0.45f, 0.0f, 0.95f };// [m]     - Manual pole position
};

class FerrofluidSurfaceSource final : public ParticleSource
{
public:
    explicit FerrofluidSurfaceSource(const FerrofluidParameters& SourceSettings = {}) noexcept
        : Settings(SourceSettings)
    {
    }

    [[nodiscard]] const char* QueryDebugName() const noexcept override { return "FerrofluidSurfaceSource"; }
    [[nodiscard]] bool        QueryGpuComputeSupported() const noexcept override { return true; }

    void     DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    uint32_t ReleaseParticles(ParticleChannelBank& Bank, const SimulationTickInputs& Inputs, ParticleRandomGenerator& Generator) override;
    void     ResetSource() noexcept override { Initialized = false; }
    void     TriggerImmediateBurst(uint32_t /*OverrideCount*/) noexcept override { Initialized = false; }

private:
    FerrofluidParameters Settings    {};
    bool                 Initialized = false;
};

class RosensweigFerrofluidSolver final : public ParticleMotionSolver
{
public:
    explicit RosensweigFerrofluidSolver(const FerrofluidParameters& SolverSettings = {}) noexcept
        : Settings(SolverSettings)
    {
    }

    [[nodiscard]] const char* QueryDebugName() const noexcept override { return "RosensweigFerrofluidSolver"; }
    [[nodiscard]] bool        QueryGpuComputeSupported() const noexcept override { return true; }

    void DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    void InitializeTick(ParticleChannelBank& Bank, const SimulationTickInputs& Inputs) override;
    void EvaluateRange(ParticleChannelBank& Bank, uint32_t BeginSlot, uint32_t EndSlot, const SimulationTickInputs& Inputs) const override;

    FerrofluidParameters& AccessParameters() noexcept { return Settings; }
    [[nodiscard]] ForceFields::SpatialVector3 QueryEvaluatedPoleMeters() const noexcept { return EvaluatedPoleMeters; }

private:
    FerrofluidParameters        Settings            {};
    ForceFields::SpatialVector3 EvaluatedPoleMeters { 0.45f, 0.0f, 0.95f };
};

//------------------------------------------------------------------------------------------------------------------------
//                                       3. MULTI-TIER HANABI FIREWORKS & SKY TEXT
//------------------------------------------------------------------------------------------------------------------------

struct PyroVolleyParameters
{
    ForceFields::SpatialVector3 LaunchOriginMeters { 0.0f, 0.0f, 0.2f }; // [m]   - Mortar launch origin
    float                       VolleyIntervalSec  = 1.15f;              // [s]   - Interval between rocket launches
    float                       AscentSpeedMs      = 11.5f;              // [m/s] - Initial mortar ascent speed
    float                       BurstSpeedMs       = 5.8f;               // [m/s] - Radial star burst speed
    uint32_t                    StarsPerBurst      = 72u;                // [cnt] - Secondary stars per detonation
    uint32_t                    PatternCode        = 6u;                 // [idx] - 0..6 (GrandFinale=6, SkyText=5)
    std::string                 SkyText            { "2026" };           // [-]   - Custom sky-writing message
};

class PyroVolleySource final : public ParticleSource
{
public:
    explicit PyroVolleySource(const PyroVolleyParameters& SourceSettings = {}) noexcept
        : Settings(SourceSettings)
    {
    }

    [[nodiscard]] const char* QueryDebugName() const noexcept override { return "PyroVolleySource"; }

    void     DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    uint32_t ReleaseParticles(ParticleChannelBank& Bank, const SimulationTickInputs& Inputs, ParticleRandomGenerator& Generator) override;
    void     ResetSource() noexcept override;
    void     TriggerImmediateBurst(uint32_t OverrideCount = 0u) noexcept override;

private:
    PyroVolleyParameters Settings         {};
    float                IntervalAccumSec = 0.0f;
    uint32_t             PendingRockets   = 2u;
    uint32_t             VolleyCounter    = 0u;
};

class PyroVolleyBurstSolver final : public ParticleMotionSolver
{
public:
    explicit PyroVolleyBurstSolver(const PyroVolleyParameters& SolverSettings = {}) noexcept
        : Settings(SolverSettings)
    {
    }

    [[nodiscard]] const char* QueryDebugName() const noexcept override { return "PyroVolleyBurstSolver"; }

    void DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    void EvaluateRange(ParticleChannelBank& Bank, uint32_t BeginSlot, uint32_t EndSlot, const SimulationTickInputs& Inputs) const override;
    void FinalizeTick(ParticleChannelBank& Bank, const SimulationTickInputs& Inputs, ParticleRandomGenerator& Generator) override;

    PyroVolleyParameters& AccessParameters() noexcept { return Settings; }

private:
    PyroVolleyParameters Settings{};
};

//------------------------------------------------------------------------------------------------------------------------
//                                           4. BOIDS MURMURATION & FLOCKING
//------------------------------------------------------------------------------------------------------------------------

struct BoidsFlockParameters
{
    ForceFields::SpatialVector3 HomeOriginMeters { 0.0f, 0.0f, 2.2f }; // [m]    - Roost / flock home center
    float                       HomeRadiusMeters = 4.2f;               // [m]    - Soft home containment envelope
    float                       PerceptionMeters = 1.6f;               // [m]    - Neighbor perception radius
    float                       SeparationMeters = 0.55f;              // [m]    - Personal space separation radius
    float                       CruiseSpeedMs    = 3.8f;               // [m/s]  - Target cruise flight speed
    float                       MaxSteerAccelMs2 = 9.5f;               // [m/s2] - Steering maneuver authority
    uint8_t                     CreatureVariant  = 0u;                 // [0..2] - 0=Birds, 1=Butterflies, 2=Fish
    uint32_t                    FlockCount       = 180u;               // [cnt]  - Initial flock size
};

class BoidsFlockSource final : public ParticleSource
{
public:
    explicit BoidsFlockSource(const BoidsFlockParameters& SourceSettings = {}) noexcept
        : Settings(SourceSettings)
    {
    }

    [[nodiscard]] const char* QueryDebugName() const noexcept override { return "BoidsFlockSource"; }

    void     DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    uint32_t ReleaseParticles(ParticleChannelBank& Bank, const SimulationTickInputs& Inputs, ParticleRandomGenerator& Generator) override;
    void     ResetSource() noexcept override { Initialized = false; }
    void     TriggerImmediateBurst(uint32_t /*OverrideCount*/) noexcept override { Initialized = false; }

private:
    BoidsFlockParameters Settings    {};
    bool                 Initialized = false;
};

class BoidsFlockSolver final : public ParticleMotionSolver
{
public:
    explicit BoidsFlockSolver(const BoidsFlockParameters& SolverSettings = {}) noexcept
        : Settings(SolverSettings)
    {
    }

    [[nodiscard]] const char* QueryDebugName() const noexcept override { return "BoidsFlockSolver"; }

    void DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    void InitializeTick(ParticleChannelBank& Bank, const SimulationTickInputs& Inputs) override;
    void EvaluateRange(ParticleChannelBank& Bank, uint32_t BeginSlot, uint32_t EndSlot, const SimulationTickInputs& Inputs) const override;

    BoidsFlockParameters& AccessParameters() noexcept { return Settings; }

private:
    struct BoidKinematicSnapshot
    {
        float PosX = 0.0f;
        float PosY = 0.0f;
        float PosZ = 0.0f;
        float VelX = 0.0f;
        float VelY = 0.0f;
        float VelZ = 0.0f;
    };

    BoidsFlockParameters               Settings {};
    std::vector<BoidKinematicSnapshot> Snapshot {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                  5. GLACIAL & SEISMIC FRACTURE PROPAGATION
//------------------------------------------------------------------------------------------------------------------------

struct FractureCrackParameters
{
    ForceFields::SpatialVector3 OriginMeters        { 0.0f, 0.0f, 0.1f }; // [m]   - Fracture sheet origin
    float                       SheetRadiusMeters   = 2.6f;               // [m]   - Slab radius
    float                       IncisionDepthMeters = 0.45f;              // [m]   - Maximum crack depth
    float                       BranchDensity       = 1.25f;              // [x]   - Crack branching speed multiplier
    uint8_t                     Pattern             = 1u;                 // [1|2] - 1=Glacial Ice, 2=Seismic Quake
    uint32_t                    CellCount           = 256u;               // [cnt] - Fracture cell shards
};

class FractureSurfaceSource final : public ParticleSource
{
public:
    explicit FractureSurfaceSource(const FractureCrackParameters& SourceSettings = {}) noexcept
        : Settings(SourceSettings)
    {
    }

    [[nodiscard]] const char* QueryDebugName() const noexcept override { return "FractureSurfaceSource"; }

    void     DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    uint32_t ReleaseParticles(ParticleChannelBank& Bank, const SimulationTickInputs& Inputs, ParticleRandomGenerator& Generator) override;
    void     ResetSource() noexcept override { Initialized = false; }
    void     TriggerImmediateBurst(uint32_t /*OverrideCount*/) noexcept override { Initialized = false; }

private:
    FractureCrackParameters Settings    {};
    bool                    Initialized = false;
};

class FractureCrackSolver final : public ParticleMotionSolver
{
public:
    explicit FractureCrackSolver(const FractureCrackParameters& SolverSettings = {}) noexcept
        : Settings(SolverSettings)
    {
    }

    [[nodiscard]] const char* QueryDebugName() const noexcept override { return "FractureCrackSolver"; }

    void DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    void EvaluateRange(ParticleChannelBank& Bank, uint32_t BeginSlot, uint32_t EndSlot, const SimulationTickInputs& Inputs) const override;

    FractureCrackParameters& AccessParameters() noexcept { return Settings; }

private:
    FractureCrackParameters Settings{};
};

//------------------------------------------------------------------------------------------------------------------------
//                                    6. MOGRAPH EFFECTOR LATTICE & DEREZ WAVES
//------------------------------------------------------------------------------------------------------------------------

struct MoGraphEffectorParameters
{
    ForceFields::SpatialVector3 OriginMeters      { 0.0f, 0.0f, 0.4f }; // [m]   - Lattice base origin
    uint32_t                    GridX             = 18u;                // [cnt] - Columns along X
    uint32_t                    GridY             = 18u;                // [cnt] - Rows along Y
    float                       SpacingMeters     = 0.22f;              // [m]   - Inter-specimen grid spacing
    float                       FalloffMeters     = 2.1f;               // [m]   - Travelling effector falloff radius
    float                       ExtrusionMeters   = 1.4f;               // [m]   - Peak +Z height modulation
    float                       WaveFrequencyHz   = 0.85f;              // [Hz]  - Effector sweep speed
    bool                        EnableDerezLaunch = false;              // [-]   - Launch specimens ballistically when wave crests
};

class LatticeGridSource final : public ParticleSource
{
public:
    explicit LatticeGridSource(const MoGraphEffectorParameters& SourceSettings = {}) noexcept
        : Settings(SourceSettings)
    {
    }

    [[nodiscard]] const char* QueryDebugName() const noexcept override { return "LatticeGridSource"; }
    [[nodiscard]] bool        QueryGpuComputeSupported() const noexcept override { return true; }

    void     DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    uint32_t ReleaseParticles(ParticleChannelBank& Bank, const SimulationTickInputs& Inputs, ParticleRandomGenerator& Generator) override;
    void     ResetSource() noexcept override { Initialized = false; }
    void     TriggerImmediateBurst(uint32_t /*OverrideCount*/) noexcept override { Initialized = false; }

private:
    MoGraphEffectorParameters Settings    {};
    bool                      Initialized = false;
};

class MoGraphEffectorSolver final : public ParticleMotionSolver
{
public:
    explicit MoGraphEffectorSolver(const MoGraphEffectorParameters& SolverSettings = {}) noexcept
        : Settings(SolverSettings)
    {
    }

    [[nodiscard]] const char* QueryDebugName() const noexcept override { return "MoGraphEffectorSolver"; }
    [[nodiscard]] bool        QueryGpuComputeSupported() const noexcept override { return true; }

    void DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    void EvaluateRange(ParticleChannelBank& Bank, uint32_t BeginSlot, uint32_t EndSlot, const SimulationTickInputs& Inputs) const override;

    MoGraphEffectorParameters& AccessParameters() noexcept { return Settings; }

private:
    MoGraphEffectorParameters Settings{};
};

//------------------------------------------------------------------------------------------------------------------------
//                                    7. ENERGY SHIELD BARRIER & SPACE REFRACTION
//------------------------------------------------------------------------------------------------------------------------

struct EnergyShieldParameters
{
    ForceFields::SpatialVector3 CentreMeters    { 0.0f, 0.0f, 1.4f }; // [m]    - Barrier center
    float                       RadiusMeters    = 1.65f;              // [m]    - Barrier envelope radius
    float                       RippleSpeedHz   = 2.4f;               // [Hz]   - Geodesic impact ripple wave speed
    uint8_t                     GeometryVariant = 1u;                 // [0..3] - 0=Sphere, 1=HexDome, 2=Prism, 3=Cylinder
    uint32_t                    CellCount       = 320u;               // [cnt]  - Tessellated barrier cells
};

class ShieldBarrierSource final : public ParticleSource
{
public:
    explicit ShieldBarrierSource(const EnergyShieldParameters& SourceSettings = {}) noexcept
        : Settings(SourceSettings)
    {
    }

    [[nodiscard]] const char* QueryDebugName() const noexcept override { return "ShieldBarrierSource"; }

    void     DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    uint32_t ReleaseParticles(ParticleChannelBank& Bank, const SimulationTickInputs& Inputs, ParticleRandomGenerator& Generator) override;
    void     ResetSource() noexcept override { Initialized = false; }
    void     TriggerImmediateBurst(uint32_t /*OverrideCount*/) noexcept override { Initialized = false; }

private:
    EnergyShieldParameters Settings    {};
    bool                   Initialized = false;
};

class EnergyShieldRippleSolver final : public ParticleMotionSolver
{
public:
    explicit EnergyShieldRippleSolver(const EnergyShieldParameters& SolverSettings = {}) noexcept
        : Settings(SolverSettings)
    {
    }

    [[nodiscard]] const char* QueryDebugName() const noexcept override { return "EnergyShieldRippleSolver"; }

    void DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    void InitializeTick(ParticleChannelBank& Bank, const SimulationTickInputs& Inputs) override;
    void EvaluateRange(ParticleChannelBank& Bank, uint32_t BeginSlot, uint32_t EndSlot, const SimulationTickInputs& Inputs) const override;

    EnergyShieldParameters& AccessParameters() noexcept { return Settings; }

private:
    EnergyShieldParameters      Settings          {};
    ForceFields::SpatialVector3 LastImpactPoint   { 1.65f, 0.0f, 1.4f };
    double                      LastImpactTimeSec = -10.0;
};

struct SpaceRefractionParameters
{
    ForceFields::SpatialVector3 SingularityMeters { 0.0f, 0.0f, 1.6f }; // [m]     - Orbital focus / thermal column center
    float                       OrbitalStrength   = 7.5f;               // [m3/s2] - Keplerian swirl gain (0 = thermal rise)
    float                       InnerRadiusMeters = 0.45f;              // [m]     - Innermost stable orbit regularization
    float                       ThermalRiseMs     = 0.0f;               // [m/s]   - Rising thermal plume velocity
};

class SpaceRefractionSolver final : public ParticleMotionSolver
{
public:
    explicit SpaceRefractionSolver(const SpaceRefractionParameters& SolverSettings = {}) noexcept
        : Settings(SolverSettings)
    {
    }

    [[nodiscard]] const char* QueryDebugName() const noexcept override { return "SpaceRefractionSolver"; }
    [[nodiscard]] bool        QueryGpuComputeSupported() const noexcept override { return true; }

    void DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    void EvaluateRange(ParticleChannelBank& Bank, uint32_t BeginSlot, uint32_t EndSlot, const SimulationTickInputs& Inputs) const override;

private:
    SpaceRefractionParameters Settings{};
};

} // namespace Frontier::SimulationSpace::Particles
