/*============================================================================================================================================
                                                       PARTICLEWORKSPACESESSION.H
============================================================================================================================================*/
// 🧩 Authoring session for Particle Studio Outliner + Inspector directly reusing C++ ForceFieldAccumulator, FieldLatticeCascade & ForceInspectorView

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/PlaybackScrubber.h"
#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/SimulationSpace/ForceFields/FieldLatticeCascade.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldAccumulator.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldDescriptors.h"
#include "Engine/Workspaces/Particles/ParticlePresetRegistry.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ParticleSelectionDomain : uint8_t
{
    EnvironmentField = 0u, // [-] - Master coupled ForceFields environment selected
    AmbientWind      = 1u, // [-] - Primary ambient LinearWind force entry selected
    ForceSource      = 2u, // [-] - Individual C++ ForceSourceRecordEntry selected
    ParticleEffect   = 3u, // [-] - Individual non-quarantined ParticleEffectRecordEntry selected
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ParticleEffectRecordEntry
{
    uint32_t                   UniqueToken           = 0u;                                          // [id]    - Monotonic effect token
    std::string                PresetIdentifier      {};                                            // [-]     - Source non-quarantined preset slug
    std::string                Title                 {};                                            // [-]     - Editable Outliner & Inspector heading
    std::string                Sublabel              {};                                            // [-]     - Secondary row summary text
    ParticleEffectCategory     Category              = ParticleEffectCategory::BallisticsAndSparks; // [-]     - Active domain category
    ParticleAuthoringDomain    AuthoringDomain       = ParticleAuthoringDomain::GeneralParticle;    // [-]     - Specialized Inspector section
    bool                       Visible               = true;                                        // [-]     - Outliner visibility / enabled switch

    // Release Volume & Birth Kinematics
    ParticleReleaseVolume      ReleaseVolume         = ParticleReleaseVolume::PointOrigin;          // [-]     - Spatial release geometry
    SimulationSpace::ForceFields::SpatialVector3 OriginMeters      { 0.0f, 0.0f, 1.2f };            // [m]     - World anchor (+Z Up)
    SimulationSpace::ForceFields::SpatialVector3 LaunchDirection   { 0.0f, 0.0f, 1.0f };            // [-]     - Nominal release axis
    float                      RadiusMeters          = 0.5f;                                        // [m]     - Source volume radius
    float                      ConeSpreadRad         = 0.52f;                                       // [rad]   - Cone dispersion half-angle
    uint32_t                   Capacity              = 1024u;                                       // [count] - Allocated particle slots
    float                      BirthRatePerSec       = 120.0f;                                      // [1/s]   - Continuous release rate
    float                      SpeedMinMs            = 1.2f;                                        // [m/s]   - Minimum launch speed
    float                      SpeedMaxMs            = 4.2f;                                        // [m/s]   - Maximum launch speed
    float                      LifeMinSec            = 1.2f;                                        // [s]     - Minimum particle lifespan
    float                      LifeMaxSec            = 2.6f;                                        // [s]     - Maximum particle lifespan
    float                      BurstIntervalSec      = 0.0f;                                        // [s]     - Periodic burst cadence (0 = off)
    uint32_t                   BurstCount            = 96u;                                         // [count] - Particles per burst pulse
    bool                       BurstAtOrigin         = true;                                        // [-]     - Release bursts at local origin

    // Shading, Silhouette & Color Ramp
    ParticleVisualSilhouette   Silhouette            = ParticleVisualSilhouette::Streak;            // [-]     - Rendered particle silhouette
    ParticleBlendCategory      BlendCategory         = ParticleBlendCategory::AdditiveGlow;         // [-]     - Optical compositing law
    float                      SizeStartMeters       = 0.08f;                                       // [m]     - Initial diameter
    float                      SizeEndMeters         = 0.03f;                                       // [m]     - Final diameter
    float                      SizeMultiplier        = 1.0f;                                        // [x]     - Global visual scale gain
    ColorRgba                  ColorA                { 255u, 235u, 140u, 255u };                    // [-]     - Color ramp start stop
    ColorRgba                  ColorB                { 255u, 89u,  13u,  255u };                    // [-]     - Color ramp end stop
    ColorRgba                  ColorC                { 255u, 255u, 180u, 255u };                    // [-]     - Color ramp highlight stop
    ParticleColorByCategory    ColorBy               = ParticleColorByCategory::LifetimeRatio;      // [-]     - Color ramp sampling driver
    ParticleRampWrapCategory   RampWrap              = ParticleRampWrapCategory::Clamp;             // [-]     - Ramp coordinate wrap mode
    ParticleRampInterpCategory RampInterp            = ParticleRampInterpCategory::Smooth;          // [-]     - Stop interpolation curve
    float                      RampPhaseOffset       = 0.0f;                                        // [-1..1] - Ramp coordinate phase shift

    // Physics & Reused C++ ForceFields Coupling
    float                      GravityMultiplier     = 1.0f;                                        // [x]     - Gravitational acceleration scale
    float                      DragCoefficient       = 0.35f;                                       // [1/s]   - Aerodynamic linear damping
    float                      ForceCouplingGain     = 1.0f;                                        // [x]     - Gain applied to ForceFieldAccumulator
    float                      CurlAmplitude         = 0.45f;                                       // [m/s]   - Local curl turbulence strength
    float                      CurlFrequency         = 0.85f;                                       // [1/m]   - Local curl spatial frequency
    float                      FloorRestitution      = 0.35f;                                       // [0-1]   - Ground bounce elasticity
    float                      SpinRateRadSec        = 1.4f;                                        // [rad/s] - Angular tumble velocity
    bool                       BallInteractionEnabled = true;                                       // [-]     - Throw-ball collision interaction
    bool                       RibbonTrailsEnabled   = false;                                       // [-]     - Tapered 3D ribbon trail switch

    // Specialized Domain Parameters (Zero Quarantine Fields)
    uint8_t                    RiggingArchitecture   = 0u;                                          // [0..9]  - Rigging & flora curve architecture
    uint32_t                   StrandRows            = 18u;                                         // [count] - Strands, strips, or stipes
    uint32_t                   CurveSegments         = 12u;                                         // [count] - Resolution segments per curve
    float                      StrandHeightMeters    = 2.2f;                                        // [m]     - Strand or frond length
    float                      CatenarySagMeters     = 0.32f;                                       // [m]     - Gravity sag / frond arch
    float                      ElasticStiffness      = 1.15f;                                       // [x]     - Restoring spring stiffness
    float                      WaveSpeedHz           = 1.6f;                                        // [Hz]    - Wind wave or current speed
    float                      TearSpeedMs           = 8.5f;                                        // [m/s]   - Impact tear threshold (0 = none)
    bool                       CurveDebugEnabled     = false;                                       // [-]     - Curve segment inspection overlay

    uint8_t                    FerrofluidVessel      = 0u;                                          // [1..2]  - 1=Petri Dish, 2=Levitated Orb
    uint32_t                   FerroSpikeCount       = 192u;                                        // [count] - Magnetic Rosensweig cusps
    float                      MagnetOrbitSpeedRadS  = 1.15f;                                       // [rad/s] - Neodymium pole orbit speed
    float                      MaxSpikeHeightMeters  = 0.92f;                                       // [m]     - Peak cusp elevation
    bool                       ManualMagnetPole      = false;                                       // [-]     - Manual pole positioning switch

    FireworkPatternCategory    FireworkPattern       = FireworkPatternCategory::GrandFinale;        // [-]     - Pyrotechnic shell pattern
    std::string                FireworkSkyText       { "2026" };                                    // [-]     - Custom sky-writing firework text

    uint8_t                    FracturePattern       = 0u;                                          // [1..2]  - 1=Glacial Ice, 2=Ground Quake
    float                      FractureDepthMeters   = 0.45f;                                       // [m]     - Fissure incision depth
    float                      CrackBranchDensity    = 1.25f;                                       // [x]     - Secondary fracture branching

    uint8_t                    EffectorArchitecture  = 0u;                                          // [1..5]  - MoGraph grid layout
    float                      EffectorFalloffMeters = 2.1f;                                        // [m]     - Spatial falloff envelope radius
    float                      EffectorExtrusionMeters = 1.4f;                                      // [m]     - Height modulation amplitude

    uint8_t                    ShieldGeometry        = 0u;                                          // [0..3]  - 0=Sphere, 1=Hex, 2=Prism, 3=Cyl
    float                      ShieldRippleSpeed     = 2.4f;                                        // [Hz]    - Impact ripple propagation rate
    float                      ShieldCellDensity     = 14.0f;                                       // [1/m]   - Tessellated lattice density

    uint8_t                    BoidsCreatureVariant  = 0u;                                          // [0..2]  - 0=Birds, 1=Butterflies, 2=Fish
    float                      BoidsPerceptionMeters = 1.6f;                                        // [m]     - Neighbour perception radius
    float                      BoidsCruiseSpeedMs    = 4.2f;                                        // [m/s]   - Target cruising velocity

    float                      TransitionHoldSec     = 0.85f;                                       // [s]     - Pre-derez solid hold duration
    float                      DistortionStrength    = 1.35f;                                       // [x]     - Gravitational/thermal lens gain

    uint32_t                   DeterministicSeed     = 2026u;                                       // [-]     - Reproducible PRNG seed
    uint32_t                   BurstTriggerCounter   = 0u;                                          // [count] - Manual burst pulse counter
};

//------------------------------------------------------------------------------------------------------------------------
//                                             PARTICLEWORKSPACESESSION
//------------------------------------------------------------------------------------------------------------------------

class ParticleWorkspaceSession
{
public:
    static constexpr std::size_t AnemometerSampleCapacity = 64u; // [count] - Rolling Beaufort wind trace samples

    ParticleWorkspaceSession();

    void PopulateDefaultStudioScene();
    void ClearAllEntries();

    // Non-quarantined Particle Effect lifecycle
    uint32_t AddEffectFromPreset(std::string_view PresetIdentifier);
    uint32_t DuplicateEffect(uint32_t EffectToken);
    bool     RemoveEffect(uint32_t EffectToken) noexcept;
    bool     ResetEffectToPreset(uint32_t EffectToken);
    void     TriggerEffectBurst(uint32_t EffectToken) noexcept;

    // Directly reused C++ ForceFields lifecycle (SimulationSpace::ForceFields)
    uint32_t AddForceSource(SimulationSpace::ForceFields::ForceModel Law);
    bool     RemoveForceSource(uint32_t ForceToken) noexcept;
    void     SynchronizeForceFieldsAccumulator();
    void     AdvanceForceFieldsClock(double DeltaSeconds);

    // Animation Play Slider & PlaybackScrubber synchronization
    void     ApplyPlaybackInteraction(const PlaybackScrubber::PlaybackScrubberInteraction& Interaction);
    void     SeekPlaybackSeconds(float TargetSeconds);
    void     ResetPlaybackClock();
    [[nodiscard]] const PlaybackScrubber::PlaybackClockParameters& QueryPlaybackClock() const noexcept;
    [[nodiscard]] PlaybackScrubber::PlaybackClockParameters&       AccessPlaybackClock() noexcept;
    [[nodiscard]] bool                                             QueryScrubbingPlayhead() const noexcept;

    // Selection routing
    void SelectEnvironmentField() noexcept;
    void SelectAmbientWind() noexcept;
    void SelectForceSource(uint32_t ForceToken) noexcept;
    void SelectParticleEffect(uint32_t EffectToken) noexcept;
    void IncrementRevision() noexcept;

    // Queries & mutable accessors
    [[nodiscard]] ParticleSelectionDomain                                         QuerySelectionDomain() const noexcept;
    [[nodiscard]] uint32_t                                                        QuerySelectedToken() const noexcept;
    [[nodiscard]] uint64_t                                                        QueryRevision() const noexcept;
    [[nodiscard]] double                                                          QuerySimulationTimeSec() const noexcept;
    [[nodiscard]] std::span<const ParticleEffectRecordEntry>                      QueryEffects() const noexcept;
    [[nodiscard]] std::span<ParticleEffectRecordEntry>                            AccessEffects() noexcept;
    [[nodiscard]] const ParticleEffectRecordEntry*                                FindEffect(uint32_t EffectToken) const noexcept;
    [[nodiscard]] ParticleEffectRecordEntry*                                      AccessEffect(uint32_t EffectToken) noexcept;
    [[nodiscard]] ParticleEffectRecordEntry*                                      AccessSelectedEffect() noexcept;
    [[nodiscard]] const ParticleEffectRecordEntry*                                QuerySelectedEffect() const noexcept;

    [[nodiscard]] std::span<const SimulationSpace::ForceFields::ForceSourceRecordEntry> QueryForceSources() const noexcept;
    [[nodiscard]] std::span<SimulationSpace::ForceFields::ForceSourceRecordEntry>       AccessForceSources() noexcept;
    [[nodiscard]] SimulationSpace::ForceFields::ForceSourceRecordEntry*                 FindForceSource(uint32_t ForceToken) noexcept;
    [[nodiscard]] const SimulationSpace::ForceFields::ForceSourceRecordEntry*           FindForceSource(uint32_t ForceToken) const noexcept;
    [[nodiscard]] SimulationSpace::ForceFields::ForceSourceRecordEntry*                 AccessActiveForceForInspector() noexcept;
    [[nodiscard]] SimulationSpace::ForceFields::ForceFieldAccumulator&                  AccessForceAccumulator() noexcept;
    [[nodiscard]] const SimulationSpace::ForceFields::ForceFieldAccumulator&            QueryForceAccumulator() const noexcept;
    [[nodiscard]] SimulationSpace::ForceFields::FieldLatticeCascade&                    AccessLatticeCascade() noexcept;
    [[nodiscard]] SimulationSpace::ForceFields::ForceOverlaySettings&                   AccessForceOverlay() noexcept;
    [[nodiscard]] const SimulationSpace::ForceFields::ForceOverlaySettings&             QueryForceOverlay() const noexcept;
    [[nodiscard]] OutlinerPanel&                                                        AccessForceOutlinerMirror() noexcept;
    [[nodiscard]] OutlinerRecordEntry*                                                  AccessActiveForceOutlinerEntry() noexcept;
    [[nodiscard]] std::span<const float>                                                QueryAnemometerSamples() const noexcept;
    [[nodiscard]] int32_t*                                                              AccessSelectedWaypointIndex() noexcept;

    [[nodiscard]] uint32_t                                                        QueryVisibleRecordCount() const noexcept;
    [[nodiscard]] uint32_t                                                        QueryHiddenRecordCount() const noexcept;
    [[nodiscard]] uint32_t                                                        QueryTotalRecordCount() const noexcept;
    [[nodiscard]] SimulationSpace::ForceFields::ForceDiagnosticsReading           QueryAmbientWindDiagnostics() const noexcept;
    [[nodiscard]] SimulationSpace::ForceFields::SpatialForceSample                SampleCoupledForceAt(
        const SimulationSpace::ForceFields::SpatialVector3& WorldPoint) const noexcept;

    // Live .ParticleSpace TOML descriptor preview for Spec & Export tab
    [[nodiscard]] std::string SerializeSelectedEffectTomlPreview() const;

private:
    void SyncForceOutlinerMirror();
    void RecordAnemometerSample() noexcept;
    [[nodiscard]] ParticleEffectRecordEntry ConstructEffectFromDescriptor(
        const ParticlePresetDescriptor& Descriptor,
        uint32_t                        AssignedToken) const;

    std::vector<ParticleEffectRecordEntry>                              Effects               {};
    std::vector<SimulationSpace::ForceFields::ForceSourceRecordEntry>   ForceSources          {};
    SimulationSpace::ForceFields::ForceFieldAccumulator                 ForceAccumulator      {};
    SimulationSpace::ForceFields::FieldLatticeCascade                   LatticeCascade        { 16u };
    SimulationSpace::ForceFields::ForceOverlaySettings                  ForceOverlay          {};
    OutlinerPanel                                                       ForceOutlinerMirror   {};
    std::array<float, AnemometerSampleCapacity>                         AnemometerRing        {};
    uint32_t                                                            AnemometerSampleCount = 0u;
    int32_t                                                             SelectedWaypoint      = -1;

    ParticleSelectionDomain                                             SelectionDomain       = ParticleSelectionDomain::ParticleEffect;
    uint32_t                                                            SelectedToken         = 0u;
    uint32_t                                                            NextEffectToken       = 1u;
    uint32_t                                                            NextForceToken        = 1u;
    uint64_t                                                            AuthoringRevision     = 1u;
    double                                                              SimulationTimeSec     = 0.0;
    PlaybackScrubber::PlaybackClockParameters                           PlaybackClock         {};
    bool                                                                ScrubbingPlayhead     = false;
};

} // namespace Frontier::Workspaces
