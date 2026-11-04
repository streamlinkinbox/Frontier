/*==========================================================================================================================================
                                                       FORCE FIELD
                                     ------------------------------------------------------------------
             Unified 3D atmospheric & environmental force field (evolved from WindField):
             - Ekman altitude-sheared wind + multi-frequency gust waves (zero-noise SampleStep fast path)
             - Divergence-free curl eddies and a mesoscale drift (SampleSwirl, evaluated once per ray or particle)
             - Triggerable 3D TornadoColumn (tornado / waterspout / dust devil with curved spine,
               Burgers-Rott swirl, radial inflow, helical updraft, cyclostrophic Δp condensation funnel,
               and surface saltation debris skirt)
             - Triggerable GustFront (haboob dust/sand wall) and Microburst
             - Authored ForceSource records (ForceSourceCatalogue): flows, accelerations and damping
             - Multi-species aerodynamic Lagrangian solver for Rain, Hail, Snow, Dust, Sand, and Debris.
==========================================================================================================================================*/
// 🧩 General-purpose triggerable 3D ForceField coupling clouds, precipitation, gusts, dust, sand, and debris.

#pragma once

#include "Engine/PhysicalDynamics/ForceSourceCatalogue.h"
#include "Engine/PlanetaryAtmosphere/AtmosphereScattering.h"
#include "Engine/Scene/EngineConfigurations.h"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace Frontier
{

using ForceSourceHandle = uint64_t;

//------------------------------------------------------------------------------------------------------------------------
//                      1. PLANETARY EKMAN WIND PROFILE & BEAUFORT CLASSIFICATION
//------------------------------------------------------------------------------------------------------------------------

struct PlanetaryWindProfile
{
    double   BearingRadians          = 0.58;  // [rad] horizontal flow azimuth in XZ (0 = +X, π/2 = +Z)
    double   SpeedMetersPerSecond    = 9.5;   // [m/s] 10 m reference wind speed
    double   GustAmplitude           = 0.28;  // [0..1] multi-frequency coherent gust modulation
    double   GustFrequencyHz         = 0.085; // [Hz] primary gust wave frequency
    double   ShearPerKilometer       = 0.35;  // [1/km] vertical speed shear ∂|u|/∂z
    double   VeerRadiansPerKilometer = 0.21;  // [rad/km] Ekman spiral clockwise turning with altitude
    double   SwirlScaleMeters        = 95.0;  // [m] eddy correlation length for SampleSwirl curl noise
    double   SwirlStrength           = 0.32;  // [0..2] fraction of local wind speed in divergence-free eddies
    uint32_t Seed                    = 7331u;
    double   MesoscaleScaleMeters    = 0.0;   // [m] slow speed/direction variation wavelength (0 = none)
    double   MesoscaleStrength       = 0.0;   // [0..1] its share of the speed; 55° of turn at full strength
};

struct BeaufortWindClassification
{
    uint32_t         ForceNumber = 0u; // 0..12
    std::string_view Description = "Calm";
};

[[nodiscard]] BeaufortWindClassification ClassifyBeaufortWind(double SpeedMetersPerSecond) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                      2. TRIGGERABLE COHERENT 3D FORCE SOURCES (TORNADO, GUST FRONT, MICROBURST)
//------------------------------------------------------------------------------------------------------------------------

// 📝 The categories (ForceSourceCategory, EngineConfigurations.h) are shared with the authored ForceSource records; the
//    triggered sources below are the flow categories 1 – 8 and keep their GPU record numbers.

struct TornadoColumnParameters
{
    SpatialVector GroundAnchorMeters      = {0.0, 0.0, 220.0}; // Touchdown point {X, GroundY, Z}
    SpatialVector TranslationVelocityMps  = {3.5, 0.0, 1.2};   // Horizontal storm track velocity [m/s]
    double        SpineLeanXMeters        = 38.0;              // Quadratic curvature lean into parent wall cloud [m]
    double        SpineLeanZMeters        = -24.0;             // Quadratic curvature lean along storm track [m]
    double        ColumnHeightMeters      = 950.0;             // Ground-to-wall-cloud funnel span [m]
    double        GroundCoreRadiusMeters  = 20.0;              // Tight touch-down Burgers-Rott core radius [m]
    double        CollarCoreRadiusMeters  = 96.0;              // Wide mesocyclone collar radius at cloud base [m]
    double        TangentialPeakSpeedMps  = 68.0;              // Peak v_θ at r = R_core (EF3–EF4 tornado) [m/s]
    double        RadialInflowSpeedMps    = 26.0;              // Surface boundary-layer radial inflow toward core [m/s]
    double        AxialUpdraftSpeedMps    = 42.0;              // Helical core updraft v_y [m/s]
    double        CondensationStrength    = 0.92;              // [0..1] cyclostrophic Δp funnel cloud density
    double        DebrisSkirtHeightMeters = 88.0;              // Lofted dust/sand/debris sheath height above ground [m]
    double        DebrisExtinctionPerM    = 0.085;             // Peak optical extinction [1/m] of ground debris cloud
    SpectrumRgb   DebrisAlbedoRgb         = {0.46, 0.38, 0.29}; // Warm umbral/ochre soil & debris scattering albedo
};

struct GustFrontParameters
{
    SpatialVector OriginMeters           = {-180.0, 0.0, 60.0};
    SpatialVector PropagationDirectionXZ = {0.92, 0.0, 0.39}; // Unit horizontal propagation direction
    double        FrontSpeedMps          = 18.0;              // Propagation celerity [m/s]
    double        PeakGustSpeedMps       = 29.0;              // Surge velocity behind nose [m/s]
    double        CrestDepthMeters       = 85.0;              // Longitudinal thickness of the gust head [m]
    double        VerticalExtentMeters   = 140.0;             // Haboob / gust head vertical height [m]
    double        LoftedDustExtinctionM  = 0.045;             // Optical extinction [1/m] of lofted sand/dust wall
    SpectrumRgb   DustAlbedoRgb          = {0.68, 0.54, 0.36}; // Quartz/saharan sand scattering tint
};

struct MicroburstParameters
{
    SpatialVector CoreCenterMeters        = {0.0, 260.0, 180.0};
    double        GroundPlaneYMeters      = 0.0;
    double        ColumnRadiusMeters      = 65.0;
    double        DowndraftSpeedMps       = 24.0;
    double        OutflowRingRadiusMeters = 150.0;
    double        OutflowPeakSpeedMps     = 31.0;
    double        LoftedDustExtinctionM   = 0.028;
};

struct GustPuffParameters
{
    SpatialVector CenterMeters            = {0.0, 12.0, 40.0};
    SpatialVector SurgeVelocityMps        = {11.0, 0.8, 4.5}; // Additive local gust surge [m/s]
    double        RadiusXZMeters          = 28.0;             // Horizontal Gaussian core radius [m]
    double        RadiusYMeters           = 14.0;             // Vertical Gaussian core radius [m]
    double        GlobalWindDriftCoupling = 0.92;             // [0..1.5] Fraction of local global wind advecting the puff
    double        WispDustExtinctionM     = 0.008;            // Optional surface dust/mist wisp extinction [1/m]
    SpectrumRgb   DustAlbedoRgb           = {0.68, 0.58, 0.44};
};

struct AerodynamicWakeParameters
{
    SpatialVector BodyOriginMeters     = {0.0, 0.65, 0.0};  // Rear axle / trailing edge world anchor [m]
    SpatialVector BodyVelocityMps      = {0.0, 0.0, 22.0};  // Moving body velocity (wake trails behind -hat(V)) [m/s]
    double        HalfWidthMeters         = 1.15;              // Half-track / wingtip half-span [m]
    double        WakeLengthMeters        = 36.0;              // Downstream wake decay length [m]
    double        WakeHeightMeters        = 3.4;               // Vertical rooster-tail plume height [m]
    double        SlipstreamSuctionMps    = 13.5;              // Forward slipstream tow velocity behind body [m/s]
    double        TrailingCirculationMps  = 7.5;               // Counter-rotating wingtip/pillar swirl + lift [m/s]
    double        RoosterTailExtinctionM  = 0.035;             // Lofted road dust / rain spray extinction [1/m]
    SpectrumRgb   SprayDustAlbedoRgb      = {0.66, 0.56, 0.42};
};

struct ThermalUpdraftParameters
{
    SpatialVector BaseAnchorMeters        = {0.0, 0.0, 120.0};
    double        ColumnHeightMeters      = 620.0;             // Thermal chimney vertical span [m]
    double        CoreRadiusMeters        = 48.0;              // Warm core radius [m]
    double        PeakUpdraftSpeedMps     = 14.5;              // Buoyant vertical velocity [m/s]
    double        EntrainmentInflowMps    = 4.8;               // Lower-layer inward entrainment speed [m/s]
    double        CumulusBoostStrength    = 0.45;              // [0..1] Localized cloud condensation boost at top
    double        PlumeDustExtinctionM    = 0.010;             // Optional thermal dust/ash haze [1/m]
    SpectrumRgb   PlumeAlbedoRgb          = {0.62, 0.54, 0.44};
};

struct DirectionalJetParameters
{
    SpatialVector OriginMeters            = {0.0, 4.0, 0.0};
    SpatialVector AxisDirectionUnit       = {1.0, 0.0, 0.0};   // Arbitrary 3D jet / canyon Venturi direction
    double        CorridorLengthMeters    = 95.0;              // Axial reach along AxisDirectionUnit [m]
    double        CorridorRadiusMeters    = 12.0;              // Cross-sectional core radius [m]
    double        CoreJetSpeedMps         = 24.0;              // Peak axial flow speed [m/s]
    double        JetParticulateExtM      = 0.012;             // Optional channel dust/steam extinction [1/m]
    SpectrumRgb   JetAlbedoRgb            = {0.70, 0.66, 0.60};
};

struct SphericalImpulseParameters
{
    SpatialVector EpicenterMeters         = {0.0, 2.0, 60.0};
    double        InitialRadiusMeters     = 3.0;               // Starting shell radius r_0 [m]
    double        ExpansionSpeedMps       = 110.0;             // Wavefront celerity c_s [m/s]
    double        ShellThicknessMeters    = 14.0;              // Gaussian shock shell half-thickness [m]
    double        PeakRadialSpeedMps      = 42.0;              // Peak outward air impulse at r_0 [m/s]
    double        ShockDustExtinctionM    = 0.032;             // Transient dust/condensation shell extinction [1/m]
    SpectrumRgb   ShockAlbedoRgb          = {0.66, 0.56, 0.44};
};

struct TriggerEnvelopeTiming
{
    double RampInSeconds  = 1.25;
    double HoldSeconds    = 45.0; // <= 0 means indefinite until ReleaseTrigger(Handle) is called
    double RampOutSeconds = 2.00;
};

struct ActiveForceSource
{
    ForceSourceHandle          Handle             = 0u;
    ForceSourceCategory        Category           = ForceSourceCategory::None;
    double                     AgeSeconds         = 0.0;
    bool                       ReleaseRequested   = false;
    double                     ReleaseStartAgeS   = 0.0;
    double                     ActivationEnvelope = 0.0; // Smooth [0..1] envelope
    double                     SwirlPhaseRadians  = 0.0; // Accumulated angular twist [rad]
    TriggerEnvelopeTiming      Timing{};
    TornadoColumnParameters    Column{};
    GustFrontParameters        GustFront{};
    MicroburstParameters       Microburst{};
    GustPuffParameters         GustPuff{};
    AerodynamicWakeParameters  Wake{};
    ThermalUpdraftParameters   Thermal{};
    DirectionalJetParameters   Jet{};
    SphericalImpulseParameters Impulse{};
};

//------------------------------------------------------------------------------------------------------------------------
//                      3. POINT EVALUATION & GPU FLOAT4 RECORD ENCODING
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t kForceSourceFloat4Stride = 4u;
inline constexpr uint32_t kMaxGpuForceSourcesRealm = 16u;

struct ForceFieldSample
{
    SpatialVector AirVelocityMps{};                   // [m/s] Total 3D air velocity (wind + vortices + gusts)
    SpatialVector RadialPressureGradientPaPerM{};     // [Pa/m] ∇p for centrifuging debris / drawing mist
    double        PressureDeficitPascals       = 0.0; // [Pa] Cyclostrophic pressure drop Δp >= 0
    double        FunnelCondensationDensity    = 0.0; // [0..1] Adiabatic tornado funnel cloud density
    double        HelicalTwistRadians          = 0.0; // [rad] Azimuthal domain twist around vortex spine
    double        ParticulateExtinctionPerM    = 0.0; // [1/m] Lofted dust/sand/debris extinction near ground
    SpectrumRgb   ParticulateScatteringAlbedo  = {0.58, 0.48, 0.36};
};

//------------------------------------------------------------------------------------------------------------------------
//                      4. MULTI-SPECIES AERODYNAMIC PARTICLES & PRECIPITATION
//------------------------------------------------------------------------------------------------------------------------

enum class AerodynamicParticleSpecies : uint8_t
{
    RainDrop     = 0, // ρ ≈ 1000 kg/m³, D ≈ 2.4 mm, v_term ≈ 7.2 m/s
    HailStone    = 1, // ρ ≈ 890 kg/m³,  D ≈ 12 mm,  v_term ≈ 16.5 m/s
    SnowFlake    = 2, // ρ ≈ 120 kg/m³,  D ≈ 4.0 mm, v_term ≈ 1.3 m/s, strong eddy flutter
    FineDust     = 3, // ρ ≈ 2600 kg/m³, D ≈ 35 μm,  suspended aerosol / haboob plume
    QuartzSand   = 4, // ρ ≈ 2650 kg/m³, D ≈ 0.28 mm, Bagnold saltation hops along ground
    LoftedDebris = 5, // ρ ≈ 650 kg/m³,  D ≈ 35 mm,  centrifuged tornado debris sheath
};

struct AerodynamicParticle
{
    SpatialVector              PositionMeters{};
    SpatialVector              VelocityMps{};
    AerodynamicParticleSpecies Species          = AerodynamicParticleSpecies::RainDrop;
    float                      DiameterMeters   = 0.0024f;
    float                      AgeSeconds       = 0.0f;
    uint32_t                   Seed             = 1u;
    bool                       RestingOnGround  = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                      5. FORCE FIELD SPACE COORDINATOR
//------------------------------------------------------------------------------------------------------------------------

class ForceFieldSpace
{
  public:
    void ConfigureEnabled(bool Enabled) noexcept { Enabled_ = Enabled; }
    [[nodiscard]] bool QueryEnabled() const noexcept { return Enabled_; }
    void SetWindProfile(const PlanetaryWindProfile& Profile) noexcept;
    [[nodiscard]] const PlanetaryWindProfile& WindProfile() const noexcept { return Wind_; }

    void SetGroundPlaneYMeters(double GroundYMeters) noexcept;
    [[nodiscard]] double GroundPlaneYMeters() const noexcept { return GroundPlaneYMeters_; }

    [[nodiscard]] double ElapsedSeconds() const noexcept { return ElapsedSeconds_; }
    void                 SetElapsedSeconds(double Seconds) noexcept;

    // 📝 The authored ForceSource records on the Force Field's timeline clock (TimelineSeconds: windows and triggers
    //    follow it, and Step advances it). Their flows join EvaluateStepSample; accelerations and damping are read apart
    //    through EvaluateContribution.
    void ConfigureAuthoredSources(std::span<const ForceSourceConfiguration> Sources, double TimelineSeconds);
    [[nodiscard]] const std::vector<PreparedForceSource>& AuthoredSources() const noexcept { return Authored_; }
    [[nodiscard]] double           AuthoredSeconds() const noexcept { return AuthoredSeconds_; }
    [[nodiscard]] ForceWindReading QueryAuthoredWind() const noexcept;

    // Explicit event triggers (all 8 coherent 3D force sources superposed onto global Ekman wind)
    ForceSourceHandle TriggerTornado(const TornadoColumnParameters& Parameters,
                                     TriggerEnvelopeTiming         Timing = {1.25, 45.0, 2.00});
    ForceSourceHandle TriggerDustDevil(SpatialVector GroundAnchorMeters,
                                       double        HeightMeters   = 160.0,
                                       double        PeakSwirlMps   = 22.0,
                                       double        HoldSeconds    = 20.0);
    ForceSourceHandle TriggerGustFront(const GustFrontParameters& Parameters,
                                       TriggerEnvelopeTiming      Timing = {0.60, 30.0, 1.50});
    ForceSourceHandle TriggerMicroburst(const MicroburstParameters& Parameters,
                                        TriggerEnvelopeTiming       Timing = {0.80, 18.0, 1.80});
    ForceSourceHandle TriggerGustPuff(const GustPuffParameters& Parameters,
                                      TriggerEnvelopeTiming     Timing = {0.45, 12.0, 1.20});
    ForceSourceHandle TriggerAerodynamicWake(const AerodynamicWakeParameters& Parameters,
                                             TriggerEnvelopeTiming            Timing = {0.20, 20.0, 0.80});
    ForceSourceHandle TriggerThermalUpdraft(const ThermalUpdraftParameters& Parameters,
                                            TriggerEnvelopeTiming           Timing = {1.50, 60.0, 2.50});
    ForceSourceHandle TriggerDirectionalJet(const DirectionalJetParameters& Parameters,
                                            TriggerEnvelopeTiming           Timing = {0.35, 30.0, 1.00});
    ForceSourceHandle TriggerSphericalImpulse(const SphericalImpulseParameters& Parameters,
                                              TriggerEnvelopeTiming             Timing = {0.04, 2.50, 0.80});

    bool ReleaseTrigger(ForceSourceHandle Handle) noexcept;
    void ClearSources() noexcept { Sources_.clear(); }

    void Step(double DeltaSeconds) noexcept;

    [[nodiscard]] const std::vector<ActiveForceSource>& ActiveSources() const noexcept { return Sources_; }

    // Fast noise-free analytic evaluation safe inside per-step volumetric raymarching
    [[nodiscard]] SpatialVector SampleStep(SpatialVector WorldPointMeters) const noexcept;

    // Divergence-free curl eddy velocity: the curl of a 2D gradient-noise stream function drifting at 0.35 × the wind
    // (evaluate once per ray or particle, never per march step). CellSpanMeters fades eddies a coarser cell cannot hold.
    [[nodiscard]] SpatialVector SampleSwirl(SpatialVector WorldPointMeters, double CellSpanMeters = 0.0) const noexcept;

    // Full coupled evaluation: wind + triggered columns/gusts + authored flows + cyclostrophic Δp funnel + lofted
    // dust/sand/debris. CellSpanMeters > 0 is the level of detail: gust bands, eddies and mesoscale shorter than two
    // cells fade out, as a coarse cascade would average them away.
    [[nodiscard]] ForceFieldSample EvaluateStepSample(SpatialVector WorldPointMeters,
                                                      bool          IncludeCurlSwirl = false,
                                                      double        CellSpanMeters   = 0.0) const noexcept;

    // 📝 One contribution at a point: Flow [m/s] as EvaluateStepSample, Accelerate [m/s²], Damp [1/s] in X. Solo limits
    //    the sum to one authored source (its index) or to the wind (kSoloWind).
    static constexpr int32_t kSoloEvery = -1;
    static constexpr int32_t kSoloWind  = -2;
    [[nodiscard]] SpatialVector EvaluateContribution(ForceContribution Contribution,
                                                     SpatialVector     WorldPointMeters,
                                                     double            CellSpanMeters   = 0.0,
                                                     bool              IncludeCurlSwirl = true,
                                                     int32_t           Solo             = kSoloEvery) const noexcept;

    // Advances Rain, Hail, Snow, FineDust, QuartzSand, and LoftedDebris under aerodynamic drag, gravity,
    // cyclostrophic pressure gradients, and Bagnold ground saltation lofting.
    void StepParticles(std::span<AerodynamicParticle> Particles, double DeltaSeconds) const noexcept;

    // Packs up to kMaxGpuForceSourcesRealm active sources into float4 slots for PlanetaryEnsemble GPU upload.
    uint32_t AppendGpuForceRecords(std::vector<std::array<float, 4>>& OutFloat4Table) const;

  private:
    [[nodiscard]] SpatialVector EvaluateWindFlow(SpatialVector WorldPointMeters, bool IncludeCurlSwirl, double CellSpanMeters) const noexcept;
    void PrepareAuthoredSources() noexcept;

    bool Enabled_ = true;
    PlanetaryWindProfile                  Wind_{};
    double                                GroundPlaneYMeters_ = 0.0;
    double                                ElapsedSeconds_     = 0.0;
    double                                AuthoredSeconds_    = 0.0;
    ForceSourceHandle                     NextHandle_         = 1u;
    std::vector<ActiveForceSource>        Sources_{};
    std::vector<ForceSourceConfiguration> AuthoredConfigurations_{};
    std::vector<PreparedForceSource>      Authored_{};
};

} // namespace Frontier
