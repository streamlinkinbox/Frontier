/*============================================================================================================================================
                                                          PARTICLEBUILTINLAWS.H
============================================================================================================================================*/
// 🧩 Built-in modular release rules, core motion laws (ForceFields coupling, gravity, curl, flutter, curves), colliders (Ground/Sphere/SDF) & integrators

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Particles/ParticleLawInterfaces.h"

#include <array>
#include <cstdint>

namespace Frontier::SimulationSpace::Particles
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class VolumeShapeCategory : uint8_t
{
    PointOrigin       = 0u, // [-] - Concentrated point source
    SphereVolume      = 1u, // [-] - Volumetric sphere interior
    DiscVolume        = 2u, // [-] - Horizontal XY circular disc
    BoxVolume         = 3u, // [-] - Volumetric 3D axis-aligned box
    BoxSurfaceLattice = 4u, // [-] - Regular 3D surface lattice
    DualElectrodeArc  = 5u, // [-] - Linear span between paired electrodes
};

enum class ColorSampleDriver : uint8_t
{
    LifetimeRatio  = 0u, // [-] - Age / MaxLife [0..1]
    SpeedMagnitude = 1u, // [-] - Normalized linear velocity magnitude
    HeightAltitude = 2u, // [-] - World +Z elevation
    RandomPerSlot  = 3u, // [-] - Deterministic per-slot hash
};

enum class RampWrapRule : uint8_t
{
    Clamp    = 0u, // [-] - Clamp coordinate to [0, 1]
    Repeat   = 1u, // [-] - Sawtooth repeat
    PingPong = 2u, // [-] - Mirrored triangle wave
};

//------------------------------------------------------------------------------------------------------------------------
//                                                1. VOLUME RELEASE RULE
//------------------------------------------------------------------------------------------------------------------------

struct VolumeReleaseParameters
{
    VolumeShapeCategory         Shape            = VolumeShapeCategory::SphereVolume; // [-]   - Spatial spawn volume
    ForceFields::SpatialVector3 OriginMeters     { 0.0f, 0.0f, 1.2f };                // [m]   - Release center (+Z Up)
    ForceFields::SpatialVector3 LaunchDirection  { 0.0f, 0.0f, 1.0f };                // [-]   - Mean release direction
    ForceFields::SpatialVector3 BoxHalfExtents   { 1.0f, 1.0f, 1.0f };                // [m]   - Half extents for box shapes
    float                       RadiusMeters     = 0.35f;                             // [m]   - Sphere/disc radius
    float                       ConeSpreadRad    = 0.52f;                             // [rad] - Dispersion half-angle
    float                       BirthRatePerSec  = 120.0f;                            // [1/s] - Continuous spawn rate
    uint32_t                    InitialBurstCount = 0u;                               // [cnt] - Immediate burst on tick 0
    uint32_t                    BurstCount       = 64u;                               // [cnt] - Particles per burst trigger
    float                       BurstIntervalSec = 0.0f;                              // [s]   - Periodic burst interval (0 = off)
    float                       SpeedMinMs       = 1.2f;                              // [m/s] - Minimum initial speed
    float                       SpeedMaxMs       = 4.2f;                              // [m/s] - Maximum initial speed
    float                       LifeMinSec       = 1.2f;                              // [s]   - Minimum lifespan
    float                       LifeMaxSec       = 2.6f;                              // [s]   - Maximum lifespan
    float                       SizeStartMeters  = 0.08f;                             // [m]   - Initial visual size
    float                       SpinRateRadSec   = 1.2f;                              // [rad/s] - Angular tumble scale
    bool                        AllocateOrientation = false;                          // [-]   - Opt into OrientationChannel
};

class VolumeReleaseRule final : public IParticleReleaseRule
{
public:
    explicit VolumeReleaseRule(const VolumeReleaseParameters& InParams = {}) noexcept
        : Params(InParams)
    {
    }

    void     DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    uint32_t ExecuteRelease(ParticleChannelBank& Bank, const SimulationStepInputs& Inputs, ParticleRng& Rng) override;
    void     ResetReleaseClock() noexcept override;
    void     TriggerImmediateBurst(uint32_t OverrideCount = 0u) noexcept override;
    [[nodiscard]] bool SupportsGpuCompute() const noexcept override { return true; }

    VolumeReleaseParameters& AccessParameters() noexcept { return Params; }
    [[nodiscard]] const VolumeReleaseParameters& QueryParameters() const noexcept { return Params; }

private:
    VolumeReleaseParameters Params                 {};
    float                   FractionalAccumulator  = 0.0f;
    float                   BurstTimerSeconds      = 0.0f;
    uint32_t                PendingManualBurst     = 0u;
    bool                    InitialBurstDispatched = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                              2. CORE MOTION LAWS
//------------------------------------------------------------------------------------------------------------------------

struct GravityAndBuoyancyParameters
{
    float GravityScale    = 1.0f;  // [x]   - Multiplier on -9.81 m/s^2 (+Z Up)
    float BuoyancyAccelMs2 = 0.0f; // [m/s2]- Upward +Z convective acceleration
    float DragCoefficient = 0.35f; // [1/s] - Exponential linear aerodynamic damping
};

class GravityAndBuoyancyLaw final : public IParticleMotionLaw
{
public:
    explicit GravityAndBuoyancyLaw(const GravityAndBuoyancyParameters& InParams = {}) noexcept
        : Params(InParams)
    {
    }

    void DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    void ApplyToRange(ParticleChannelBank& Bank, uint32_t BeginSlot, uint32_t EndSlot, const SimulationStepInputs& Inputs) const override;
    [[nodiscard]] bool SupportsGpuCompute() const noexcept override { return true; }

    GravityAndBuoyancyParameters& AccessParameters() noexcept { return Params; }

private:
    GravityAndBuoyancyParameters Params{};
};

struct ForceFieldCouplingParameters
{
    float    CouplingGain         = 1.0f;  // [1/s] - Aerodynamic relaxation toward ForceFields AdvectionVelocity
    float    AccelScale           = 1.0f;  // [x]   - Multiplier on ForceFields LinearAcceleration
    float    TorqueScale          = 0.35f; // [x]   - Multiplier on ForceFields AngularTorque (into OrientationChannel)
    float    DragScale            = 1.0f;  // [x]   - Multiplier on ForceFields DragDecayRate
    bool     PreferLatticeLookup  = false; // [-]   - Sample FieldLatticeCascade trilinearly instead of analytical accumulator
    uint32_t LatticeTierIndex     = 1u;    // [idx] - Cascade tier [0..3] when PreferLatticeLookup is active
};

class ForceFieldCouplingLaw final : public IParticleMotionLaw
{
public:
    explicit ForceFieldCouplingLaw(const ForceFieldCouplingParameters& InParams = {}) noexcept
        : Params(InParams)
    {
    }

    void DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    void ApplyToRange(ParticleChannelBank& Bank, uint32_t BeginSlot, uint32_t EndSlot, const SimulationStepInputs& Inputs) const override;
    [[nodiscard]] bool SupportsGpuCompute() const noexcept override { return true; }

    ForceFieldCouplingParameters& AccessParameters() noexcept { return Params; }

private:
    ForceFieldCouplingParameters Params{};
};

struct LocalCurlTurbulenceParameters
{
    float AmplitudeMs     = 0.45f; // [m/s] - Divergence-free curl velocity amplitude
    float FrequencyPerM   = 0.85f; // [1/m] - Spatial curl wavenumber
    float TimeScrollSpeed = 0.65f; // [1/s] - Temporal phase advection rate
};

class LocalCurlTurbulenceLaw final : public IParticleMotionLaw
{
public:
    explicit LocalCurlTurbulenceLaw(const LocalCurlTurbulenceParameters& InParams = {}) noexcept
        : Params(InParams)
    {
    }

    void DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    void ApplyToRange(ParticleChannelBank& Bank, uint32_t BeginSlot, uint32_t EndSlot, const SimulationStepInputs& Inputs) const override;
    [[nodiscard]] bool SupportsGpuCompute() const noexcept override { return true; }

    LocalCurlTurbulenceParameters& AccessParameters() noexcept { return Params; }

private:
    LocalCurlTurbulenceParameters Params{};
};

struct FlutterAndTumbleParameters
{
    float FlutterAmplitudeMs2 = 0.65f; // [m/s2] - Lateral aerodynamic leaf/petal flutter
    float FlutterFrequencyHz  = 2.2f;  // [Hz]   - Oscillation frequency
    float AngularDamping      = 0.25f; // [1/s]  - Spin damping rate
};

class FlutterAndTumbleLaw final : public IParticleMotionLaw
{
public:
    explicit FlutterAndTumbleLaw(const FlutterAndTumbleParameters& InParams = {}) noexcept
        : Params(InParams)
    {
    }

    void DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    void ApplyToRange(ParticleChannelBank& Bank, uint32_t BeginSlot, uint32_t EndSlot, const SimulationStepInputs& Inputs) const override;
    [[nodiscard]] bool SupportsGpuCompute() const noexcept override { return true; }

    FlutterAndTumbleParameters& AccessParameters() noexcept { return Params; }

private:
    FlutterAndTumbleParameters Params{};
};

struct ColorAndSizeCurveParameters
{
    std::array<float, 4u> ColorA          {{ 1.0f, 0.92f, 0.55f, 1.0f }}; // [linear] - Start ramp stop
    std::array<float, 4u> ColorC          {{ 1.0f, 0.78f, 0.32f, 1.0f }}; // [linear] - Midpoint ramp stop
    std::array<float, 4u> ColorB          {{ 1.0f, 0.35f, 0.05f, 0.0f }}; // [linear] - End ramp stop
    float                 SizeStartMeters = 0.08f;                        // [m]      - Birth size
    float                 SizeEndMeters   = 0.03f;                        // [m]      - Expiry size
    float                 SizeMultiplier  = 1.0f;                         // [x]      - Visual scale multiplier
    ColorSampleDriver     Driver          = ColorSampleDriver::LifetimeRatio;
    RampWrapRule          WrapRule        = RampWrapRule::Clamp;
    float                 PhaseOffset     = 0.0f;                         // [-1..1]  - Ramp phase offset
    float                 SpeedNormMaxMs  = 10.0f;                        // [m/s]    - Normalization speed for SpeedMagnitude
    float                 HeightNormMaxM  = 6.0f;                         // [m]      - Normalization height for HeightAltitude
};

class ColorAndSizeCurveLaw final : public IParticleMotionLaw
{
public:
    explicit ColorAndSizeCurveLaw(const ColorAndSizeCurveParameters& InParams = {}) noexcept
        : Params(InParams)
    {
    }

    void DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    void ApplyToRange(ParticleChannelBank& Bank, uint32_t BeginSlot, uint32_t EndSlot, const SimulationStepInputs& Inputs) const override;
    [[nodiscard]] bool SupportsGpuCompute() const noexcept override { return true; }

    ColorAndSizeCurveParameters& AccessParameters() noexcept { return Params; }

private:
    ColorAndSizeCurveParameters Params{};
};

//------------------------------------------------------------------------------------------------------------------------
//                                              3. MODULAR COLLIDERS
//------------------------------------------------------------------------------------------------------------------------

struct GroundPlaneParameters
{
    float FloorZMeters    = 0.0f;  // [m]   - Ground altitude (+Z Up)
    float Restitution     = 0.35f; // [0-1] - Bounce elasticity
    float SlidingFriction = 0.25f; // [0-1] - Tangential velocity loss on contact
};

class GroundPlaneCollider final : public IParticleCollisionRule
{
public:
    explicit GroundPlaneCollider(const GroundPlaneParameters& InParams = {}) noexcept
        : Params(InParams)
    {
    }

    void DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    void ResolveRange(ParticleChannelBank& Bank, uint32_t BeginSlot, uint32_t EndSlot, const SimulationStepInputs& Inputs) const override;
    [[nodiscard]] bool SupportsGpuCompute() const noexcept override { return true; }

    GroundPlaneParameters& AccessParameters() noexcept { return Params; }

private:
    GroundPlaneParameters Params{};
};

class DynamicSphereColliderRule final : public IParticleCollisionRule
{
public:
    explicit DynamicSphereColliderRule(float InImpulseGain = 1.0f) noexcept
        : ImpulseGain(InImpulseGain)
    {
    }

    void DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    void ResolveRange(ParticleChannelBank& Bank, uint32_t BeginSlot, uint32_t EndSlot, const SimulationStepInputs& Inputs) const override;
    [[nodiscard]] bool SupportsGpuCompute() const noexcept override { return true; }

private:
    float ImpulseGain = 1.0f;
};

struct SignedDistanceColliderParameters
{
    float ContactMarginMeters = 0.02f; // [m]   - Surface standoff margin
    float Restitution         = 0.45f; // [0-1] - Bounce elasticity off SDF surface
    float SurfaceFriction     = 0.22f; // [0-1] - Tangential friction on SDF surface
    float AdhesionStrength    = 0.0f;  // [m/s] - Optional inward surface stick traction
};

class SignedDistanceVolumeCollider final : public IParticleCollisionRule
{
public:
    explicit SignedDistanceVolumeCollider(const SignedDistanceColliderParameters& InParams = {}) noexcept
        : Params(InParams)
    {
    }

    void DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    void ResolveRange(ParticleChannelBank& Bank, uint32_t BeginSlot, uint32_t EndSlot, const SimulationStepInputs& Inputs) const override;
    [[nodiscard]] bool SupportsGpuCompute() const noexcept override { return true; }

private:
    SignedDistanceColliderParameters Params{};
};

//------------------------------------------------------------------------------------------------------------------------
//                                             4. MODULAR TIME INTEGRATORS
//------------------------------------------------------------------------------------------------------------------------

class SemiImplicitEulerIntegrator final : public IParticleIntegrator
{
public:
    void DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    void IntegrateRange(ParticleChannelBank& Bank, uint32_t BeginSlot, uint32_t EndSlot, const SimulationStepInputs& Inputs) const override;
};

class XpbdVerletIntegrator final : public IParticleIntegrator
{
public:
    void DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    void IntegrateRange(ParticleChannelBank& Bank, uint32_t BeginSlot, uint32_t EndSlot, const SimulationStepInputs& Inputs) const override;
};

class MidpointRk2Integrator final : public IParticleIntegrator
{
public:
    void DeclareRequiredChannels(ParticleChannelBank& Bank) const override;
    void IntegrateRange(ParticleChannelBank& Bank, uint32_t BeginSlot, uint32_t EndSlot, const SimulationStepInputs& Inputs) const override;
};

} // namespace Frontier::SimulationSpace::Particles
