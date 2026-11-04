/*==========================================================================================================================================
                                                      ENGINECONFIGURATIONS.H
==========================================================================================================================================*/
// 🧩 Modular runtime configurations for Render Pipeline, Environment, Spatial, Physics/Mass, Thermal, Chemical, and Surface domains.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    REVISION TRACKER
//------------------------------------------------------------------------------------------------------------------------

struct RevisionCounter
{
    uint64_t Revision = 0;

    void AdvanceRevision() noexcept
    {
        ++Revision;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    RENDER ENGINE CONFIGURATIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The one Render settings record (levelspace type "RenderPipeline"): every setting of the ported renderer (the VehicleHMI demo's
//    RefreshToggles, IlluminationSettings and the two VarianceSettings), the options measured beyond it, the debug views and the
//    refresh timing. The defaults are the demo's. It absorbed the Diagnostics, DistanceField, SurfaceCards and PolygonClusters
//    records, none of whose values reached the renderer; a level that still lists them loads without them.
struct RenderPipelineConfiguration : RevisionCounter
{
    bool  Enabled                = true;
    bool  HardwareRayQueryTier   = true;    // [-] RTX ReSTIR path tracer (VK_KHR_ray_query BVH) | GTX distance-field GI; rebuilds the viewport
    int   QualityTier            = 2;       // [-] 0 Low · 1 Medium · 2 High (IlluminationConstants grades); reloads the scene

    // 📝 Resolution & sampling
    float RenderScale            = 1.0f;    // [-] viewport scale, multiplies the grade's render scale under its pixel ceiling (0.25–2)
    int   TraceScale             = 2;       // [-] 1 Full · 2 Half · 4 Quarter: screen pixels per GI and reflection pixel along one axis
    int   InitialCandidates      = 2;       // [-] paths per pixel (M), 1–4

    // 📝 Global illumination
    bool  GlobalIllumination     = true;
    int   MaxBounces             = 3;       // [-] path bounces, 1–4
    bool  MultiBounceNee         = true;
    bool  RussianRoulette        = true;
    bool  TemporalReuse          = true;
    bool  SpatialReuse           = true;
    bool  VisibilityReuse        = true;
    bool  RadianceRefresh        = true;
    int   SpatialCount           = 5;       // [-] spatial candidates, 0–8
    float SpatialRadius          = 20.0f;   // [px] 2–48
    float TemporalTallyLimit     = 20.0f;   // [-] temporal M limit, 1–40
    float IndirectScale          = 1.0f;    // [-]
    float SkyScale               = 1.0f;    // [-]
    float IncandescenceScale     = 1.0f;    // [-] emissive surfaces
    bool  CardRadiosity          = true;
    int   CardTileBudget         = 0;       // [-] static card tiles relit per refresh; 0 = the grade's

    // 📝 Reflections
    bool  Reflection             = true;
    bool  RayTracedReflections   = true;
    int   ReflectionBounces      = 2;       // [-] 1–4
    float ReflectionScale        = 1.0f;    // [-]
    float ReflectionRoughness    = 0.80f;   // [-] roughness cutoff of the RTX reflection rays, 0.15–1
    bool  MirrorFilter           = false;   // [-] measured option: smooth surfaces take the unfiltered reflection
    bool  MirrorTrace            = false;   // [-] measured option, with MirrorFilter: smooth surfaces traced at screen resolution

    // 📝 Sun & shadows
    bool  DirectShadow           = true;
    bool  RayTracedShadows       = true;
    bool  FireflyFilter          = true;
    bool  SolarDisc              = false;   // [-] measured option: the sun shaded as a disc; no solar disc in reflection rays
    bool  StratifiedSun          = false;   // [-] measured option: sun-disc samples stratified over 4×4 pixel blocks
    bool  PenumbraFilter         = false;   // [-] measured option: ray-traced sun verdicts filtered across their penumbra

    // 📝 Denoising & antialiasing (wavelet levels = VarianceSettings::WaveletIterations + 1)
    bool  VarianceFilter         = true;
    bool  TemporalAntiAliasing   = true;
    int   DiffuseWaveletLevels   = 4;       // [-] 1–4
    int   DiffuseTallyLimit      = 24;      // [-] history limit, 1–64
    float DiffuseMinimumWeight   = 0.08f;   // [-] 0.01–0.5
    float DiffuseColourSigma     = 4.0f;    // [σ] 0.5–16
    int   GlossyWaveletLevels    = 3;
    int   GlossyTallyLimit       = 12;
    float GlossyMinimumWeight    = 0.12f;
    float GlossyColourSigma      = 3.0f;

    // 📝 Exposure
    bool  AutoExposure           = true;
    float ExposureCompensationEv = 0.15f;   // [EV] −4–4

    // 📝 Geometry: the demo's ?hierarchy=1 and ?streampages (the cluster nesting takeover of eligible static sets)
    bool  VirtualizedGeometry    = false;
    int   VirtualizedStreamSlots = 0;       // [-] 0: one arena slot per page

    // 📝 Debug views: the demo's inspection views 0–14 and, as a view of its own, the editor's fast closed-form renderer
    int   InspectionModeIndex    = 0;       // [-] 0–14 (RefreshToggles::Inspection)
    int   DistanceInspection     = 1;       // [-] view 6 reads: 1 normals · 2 traversal steps · 3 next-event light / cards
    bool  AnalyticIllumination   = false;   // [-] the analytic illumination instead of the global illumination

    // 📝 Refresh timing
    bool  LiveRefresh            = true;    // [-] false holds the last image (compare a still against a change)
    int   TargetRate             = 60;      // [Hz] budget line of the timing chart: 30 · 60 · 120

    // 📝 Measured by the viewport and published by the workspace after each refresh; not saved.
    struct Measurements
    {
        bool                    Presenting       = false;
        bool                    RayTracing       = false;
        double                  RefreshMs        = 0.0;   // [ms] smoothed refresh (submission to completion)
        double                  LatestMs         = 0.0;   // [ms] the last refresh alone
        std::array<float, 96>   HistoryMs{};              // [ms] ring of recent refreshes
        uint32_t                HistoryCount     = 0;     // [-] refreshes written into the ring
        uint64_t                RefreshCount     = 0;     // [-] refreshes since the viewport came up
        std::array<uint32_t, 2> ScreenExtent     = {0, 0};
        std::array<uint32_t, 2> TraceExtent      = {0, 0};
        uint32_t                TlasInstances    = 0;
        uint32_t                ClipResolution   = 0;     // [voxels]
        uint32_t                SurfaceCards     = 0;
        uint32_t                GeometryClusters = 0;
        uint32_t                SceneTriangles   = 0;
        std::string             Adapter{};
    } Measured{};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENVIRONMENT CONFIGURATIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The level's sun. Below the original fields sit the demo Sun page's remaining settings (EnvironmentDefaults): the
//    disc and its aureole, the colour source, and the day clock that places the sun from a date, a place and a time.
struct SolarConfiguration : RevisionCounter
{
    bool                 Enabled              = true;
    float                ElevationDegrees     = 38.0f;
    float                AzimuthDegrees       = 132.0f;
    float                IlluminanceKilolux   = 98.0f;
    float                AngularDiameterDeg   = 0.53f;
    float                KelvinTemperature    = 5778.0f;
    bool                 CastRaytracedShadow  = true;
    bool                 VolumetricLightShaft = true;
    bool                 DiscVisible          = true;                 // SunDiscEnabled
    float                AureoleGain          = 1.0f;                 // SunGlow [×]
    uint32_t             ColourSource         = 0;                    // SunColourSource: 0 Temperature, 1 RGB
    std::array<float, 3> TintLinear           = {1.0f, 1.0f, 1.0f};   // SunTint, linear
    bool                 TimeLinked           = false;                // SunAutoPosition: the day clock places the sun
    bool                 DayCycle             = false;                // DayAnimate: the clock runs
    float                LocalSolarHours      = 12.0f;                // SunTime [h]
    float                DayDurationHours     = 6.0f;                 // DayDuration [real h per simulated day]
    std::string          CalendarDate         = "2026-10-20";         // SkyDate
    float                LatitudeDegrees      = -26.18f;              // [°]
    float                LongitudeDegrees     = 28.32f;               // [°]
    float                UtcOffsetHours       = 2.0f;                 // TimeZone [h]
};

struct OpticalFlareConfiguration : RevisionCounter
{
    bool     Enabled              = true;
    float    Intensity            = 0.65f;
    float    LuminanceThreshold   = 1.80f;
    uint32_t ApertureBladeCount   = 7;
    float    HaloRadius           = 0.42f;
    float    ChromaticDispersion  = 0.18f;
    bool     AnamorphicStreak     = true;
    bool     DiffractionStarburst = true;
    uint32_t Preset              = 0;       // [-] Cinematic, Anamorphic, Starburst, Halo, Custom layers
    float Spread                 = 1.0f;
    uint32_t GhostCount          = 6;
    float GhostBrightness        = 0.3f;
    uint32_t GhostShape          = 6;
    float GhostSpacing           = 1.0f;
    float HaloWidth              = 0.025f;
    float HaloBrightness         = 0.2f;
    float StreakStrength         = 0.3f;
    float RotationDegrees        = 0.0f;
    uint32_t RayCount            = 6;
    bool WarmStreak              = false;
    float PreviewPosition[2]     = {0.28f, 0.43f};
};

struct AtmosphereOpticalConfiguration
{
    friend bool operator==(const AtmosphereOpticalConfiguration&, const AtmosphereOpticalConfiguration&) = default;
    float RayleighScattering = 1.0f;
    float MieTurbidity = 2.4f;
    float MieAnisotropyG = 0.80f;
    float OzoneAbsorptionScale = 1.0f;
    float PlanetRadiusKm = 6360.0f;
    float AtmosphereThicknessKm = 80.0f;
    float RayleighHeightMetres = 8000.0f;
    float MieHeightMetres = 1200.0f;
    float GroundAlbedo[3] = {0.3f,0.3f,0.3f};
    float HorizonGlow = 1.0f;
    bool MultiScatteringLut = true;
    uint32_t TierSamples = 40;
};

struct AtmosphereConfiguration : RevisionCounter, AtmosphereOpticalConfiguration
{
    bool Enabled = true;
    bool AerialPerspective = true;
    float SkyTint[3] = {1.0f,1.0f,1.0f};
    float SkyScale = 1.0f;
    float WhiteLine = 1.0f;
    bool LineAtCivilOnly = true;
    bool AirMass = true;
    bool UseBaked = false;
    bool BakedAvailable = false;
    AtmosphereOpticalConfiguration Baked{};
    uint64_t BakeAcknowledged = 0;
    uint64_t BakeRequested = 0; // [-] transient command serial; acknowledged only after a successful GPU readback
};

struct CloudConfiguration : RevisionCounter
{
    bool  Enabled              = true;
    bool  LocalVolume          = false;
    float Coverage             = 0.44f;
    float ExtinctionDensity    = 0.85f;
    float BaseAltitudeMetres   = 1650.0f;
    float LayerThicknessMetres = 1400.0f;
    float WindDriftSpeed       = 12.0f;
    float SilverLiningPower    = 0.72f;
    bool  TemporalReprojection = true;
    bool  CastGroundShadows    = true;
    // 📝 VehicleHMI formation, sampling and bounded-volume controls. Units match the demo.
    uint32_t Formation           = 2;
    float CeilingMetres          = 14000.0f;
    float FeatureScale           = 1.0f;
    float AnvilShaping           = 0.5f;
    float EdgeErosion            = 0.3f;
    float PhaseAnisotropy        = 0.35f;
    float LightAbsorption        = 0.15f;
    float PowderResponse         = 0.45f;
    uint32_t LightSamples        = 2;
    uint32_t ResolutionPercent   = 35;
    uint32_t SkySamples          = 12;
    uint32_t LocalSamples        = 16;
    float ShadowStrength         = 0.65f;
    float ShadowSoftnessMetres   = 35.0f;
    float ShadowSpanMetres       = 6000.0f;
    uint32_t ShadowSamples       = 6;
    bool FollowWind              = true;
    uint32_t BoundsShape         = 0;
    uint32_t LocalFormation      = 1;
    float EdgeSoftness           = 0.5f;
    float LocalFeatureMetres     = 30.0f;
    float CentreMetres[3]        = {0.0f, 0.0f, 400.0f};
    float HalfExtentsMetres[3]   = {300.0f, 300.0f, 150.0f};
};

struct StarConfiguration : RevisionCounter
{
    bool     Enabled                = true;
    uint32_t HipparcosCount         = 5000;
    float    MagnitudeGain          = 1.15f;
    float    MilkyWayLuminance      = 0.55f;
    float    SiderealSpeed          = 1.0f;
    float    AtmosphericScintillate = 0.35f;
    float LimitingMagnitude      = 6.0f;
    float PointSize              = 1.0f;
    float HaloGain               = 1.0f;
    float RotationDegrees        = 0.0f;
    bool ScintillationEnabled    = true;
    float ScintillationRateHz     = 1.0f;
};

struct MoonInstanceConfiguration
{
    bool Visible = false;
    bool FollowSky = false;
    uint32_t Body = 0;
    float PhaseDays = 14.765f;
    float AzimuthDegrees = -8.0f;
    float ElevationDegrees = 38.0f;
    float RollDegrees = 0.0f;
    float PitchDegrees = 0.0f;
    float SpinDegrees = 0.0f;
    float AngularDiameterDegrees = 25.0f;
    float Brightness = 1.0f;
    float Glow = 0.2f;
};

struct MoonConfiguration : RevisionCounter
{
    bool  Enabled               = true;
    float PrimaryPhase          = 0.68f;
    float PrimaryOrbitDegrees   = 214.0f;
    float PrimaryAlbedo         = 0.12f;
    bool  SecondaryMoonEnabled  = true;
    float SecondaryOrbitDegrees = 64.0f;
    float EarthshineLuminance   = 0.22f;
    bool IlluminateScene = true;
    float LightPower = 1.0f;
    std::array<MoonInstanceConfiguration, 4> Instances = []
    {
        std::array<MoonInstanceConfiguration, 4> Bodies{};
        Bodies[0].Visible = true;
        Bodies[0].PhaseDays = 0.68f * 29.53f;
        Bodies[1].Visible = true;
        Bodies[1].Body = 1;
        Bodies[1].AngularDiameterDegrees = 1.6f;
        Bodies[1].AzimuthDegrees = 30; Bodies[1].ElevationDegrees = 25;
        Bodies[2].AzimuthDegrees = -65; Bodies[2].ElevationDegrees = 45;
        Bodies[3].AzimuthDegrees = 70; Bodies[3].ElevationDegrees = 20;
        return Bodies;
    }();
};

// 📝 One planet of the sky, each its own Scene Directory entry. Body picks the demo's orbital solution it follows (0 Venus,
//    1 Jupiter, 2 Mars, 3 Saturn); the drawn diameter is exaggerated so the disc reads at viewport resolution.
struct PlanetConfiguration : RevisionCounter
{
    bool     Enabled                 = true;
    uint32_t Body                    = 0;
    float    ApparentDiameterDegrees = 0.80f;
    float    Brightness              = 1.0f;
    bool     RingSystem              = false;
    bool     AtmosphereShell         = false;
    bool FollowSky = true;
    float AzimuthDegrees = -8.0f;
    float ElevationDegrees = 35.0f;
    float SpinDegrees = 0.0f;
    float TiltDegrees = 20.0f;
    float RollDegrees = 0.0f;
    float SurfaceTint[3] = {1,1,1};
    float BandContrast = 1.0f;
    float SurfaceDetail = 1.0f;
    float CloudVeil = 0.8f;
    float AtmosphereHeight = 0.065f;
    float LimbGlow = 0.85f;
    float RingInnerRadius = 1.32f;
    float RingOuterRadius = 2.38f;
    float RingOpticalDepth = 1.45f;

};

inline constexpr std::array<const char*, 4> PlanetBodyNames = {"Venus", "Jupiter", "Mars", "Saturn"};

// 📝 The four planets as a level without planet entries draws them (the environment's earlier defaults).
[[nodiscard]] inline std::array<PlanetConfiguration, 4> ResolveDefaultPlanets() noexcept
{
    std::array<PlanetConfiguration, 4> Planets{};
    const std::array<float, 4> Diameters = {0.80f, 1.26f, 0.69f, 1.03f};
    for (uint32_t Body = 0; Body < 4; ++Body)
    {
        Planets[Body].Body                    = Body;
        Planets[Body].ApparentDiameterDegrees = Diameters[Body];
    }
    Planets[0].AtmosphereShell = true;
    Planets[3].RingSystem      = true;
    return Planets;
}

struct FogConfiguration : RevisionCounter
{
    bool  AerialOnly = false;
    bool  Enabled             = true;
    bool  LocalVolume         = false;
    float BaseDensity         = 0.014f;
    float HeightFalloff       = 0.12f;
    float BaseAltitudeMetres  = 0.0f;
    float PhaseAnisotropy     = 0.55f;
    float StartDistanceMetres = 8.0f;
    bool  VolumetricFroxelGrid = true;
    bool  ReceiveRayShadow    = true;
    float TintLinear[3]          = {0.52f, 0.62f, 0.69f};
    float SunScatter             = 0.5f;
    bool AerialEnabled           = true;
    float AerialDensity          = 0.00004f;
    float AerialStartMetres       = 100.0f;
    float AerialMieBlend          = 0.5f;
    uint32_t BoundsShape         = 0;
    float CentreMetres[3]        = {30.0f, 0.0f, 2.5f};
    float HalfExtentsMetres[3]   = {18.0f, 18.0f, 4.0f};
    bool DensityFalloff          = true;
    float EdgeSoftness           = 0.45f;
    float LocalDensity           = 0.35f;
    float Heterogeneity          = 0.75f;
    float NoiseScaleMetres       = 5.0f;
    float IndependentWindMps     = 0.8f;
    bool FollowWind              = true;
    float Albedo[3]              = {0.72f, 0.8f, 0.9f};
    float Absorption             = 0.08f;
    bool SelfShadow              = true;
    uint32_t LocalSamples        = 28;
};

struct LightningConfiguration : RevisionCounter
{
    bool     Enabled              = true;
    float    StrikesPerMinute     = 4.5f;
    float    BranchingFactor      = 0.62f;
    float    PeakCurrentKiloamps  = 32.0f;
    float    CloudSheetLuminance  = 2.8f;
    bool     AutoStormDischarge   = false;
    bool     AcousticShockwave    = true;
    uint32_t TriggeredStrikeCount = 0;
    uint32_t Discharge           = 0;
    uint32_t CloudSource         = 0;
    float DistanceMetres         = 1800.0f;
    float MinimumIntervalSeconds = 6.0f;
    float MaximumIntervalSeconds = 15.0f;
    float ChannelSpanMetres      = 2400.0f;
    float BoltBrightness         = 1.0f;
    float CloudFlashStrength     = 1.0f;
    float FlashRadiusMetres      = 1800.0f;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    FORCE FIELD TAXONOMY
//------------------------------------------------------------------------------------------------------------------------

// 📝 The eos force-field taxonomy: a flow is a velocity the air carries [m/s], an acceleration is added to a receiver's
//    velocity whatever its coupling [m/s²], and damping scales velocity down [1/s]. The three never share a bucket.
enum class ForceContribution : uint8_t
{
    Flow       = 0,
    Accelerate = 1,
    Damp       = 2,
};

// 📝 The localized force types. 1 – 8 keep the numbers the GPU force records carry (ForceField.slang); 9 – 16 are the
//    eos Particle editor's fields (forcefields.js).
enum class ForceSourceCategory : uint8_t
{
    None           = 0,
    TornadoColumn  = 1,    // Burgers–Rott column: tornado, waterspout, dust devil
    GustFront      = 2,    // travelling surge: squall line, haboob wall
    Microburst     = 3,    // downdraft column and its ground outflow ring
    GustPuff       = 4,    // Gaussian packet drifting with the wind
    VehicleWake    = 5,    // slipstream tow and counter-rotating pair behind a moving body
    ThermalUpdraft = 6,    // buoyant chimney with entrainment and divergence
    JetCorridor    = 7,    // Venturi corridor: canyon, tunnel draft
    BlastWave      = 8,    // expanding spherical front r(t) = r₀ + c·t
    Current        = 9,    // steady flow inside a box
    Attractor      = 10,   // pull toward the centre, with swirl and a swallow radius
    Repulsor       = 11,   // push away from the centre
    Orbit          = 12,   // tangential pull around world up
    Lift           = 13,   // straight up, fading toward the edge
    MagneticDipole = 14,   // along the field lines of a dipole on world Z
    Gravity        = 15,   // unbounded downward acceleration
    DragVolume     = 16,   // velocity bled away inside a box
};

enum class ForceReach : uint8_t
{
    Everywhere = 0,
    Sphere     = 1,
    Column     = 2,
    Box        = 3,
};

enum class ForceFalloff : uint8_t
{
    None          = 0,
    Linear        = 1,
    Smooth        = 2,
    InverseSquare = 3,
};

enum class ForceActivation : uint8_t
{
    Always  = 0,
    Window  = 1,
    Trigger = 2,
};

// 📝 Receivers (the Acts on list). A force with no receivers named acts on every receiver; only such flows can share
//    the baked lattice, the rest are evaluated per receiver.
enum ForceReceiver : uint32_t
{
    ForceReceiverParticles     = 1u << 0u,
    ForceReceiverDebris        = 1u << 1u,
    ForceReceiverPrecipitation = 1u << 2u,
    ForceReceiverClouds        = 1u << 3u,
    ForceReceiverPlumes        = 1u << 4u,
    ForceReceiverFoliage       = 1u << 5u,
    ForceReceiverVehicles      = 1u << 6u,
    ForceReceiverEvery         = (1u << 7u) - 1u,
};

// 📝 What the field visualization colours: horizontal speed, one axis, vorticity ω or divergence ∇·v.
enum class ForceFieldQuantity : uint8_t
{
    Speed      = 0,
    AlongX     = 1,   // east
    AlongZ     = 2,   // south
    Vertical   = 3,
    Vorticity  = 4,
    Divergence = 5,
};

enum class ForceFieldProjection : uint8_t
{
    Map     = 0,   // top-down
    Oblique = 1,   // the slice as a tilted plate
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    FORCE FIELD AND WIND
//------------------------------------------------------------------------------------------------------------------------

// 📝 The master component: the whole field's switches, its visualization and the proposed cascades. The Wind is its guest
//    record and the localized forces (ForceSource records) sit under it.
struct ForceFieldConfiguration : RevisionCounter
{
    bool Enabled = true;
    bool Visualize = false;
    float CentreMetres[3] = {-20.0f,0.0f,8.0f};
    float HalfExtentsMetres[3] = {20.0f,20.0f,8.0f};
    uint32_t GridResolution = 5;
    float ArrowSeconds = 0.5f; // [s] velocity multiplied by this time gives the world-space arrow length
    bool IncludeTurbulence = true;

    // 📝 Range and detail (proposed): cascade c holds CellsPerSide² × VerticalCells cells of FinestCellMetres × 8^c.
    uint32_t Cascades         = 4;
    float    FinestCellMetres = 2.0f;
    uint32_t CellsPerSide     = 128;
    uint32_t VerticalCells    = 32;
    float    ExtentMetres     = 100000.0f;   // [m] the authored extent the minimap shows
    uint32_t Receivers        = ForceReceiverEvery;

    // 📝 The field visualization: what the viewport's Field view and the scene overlay show.
    ForceContribution    ViewContribution   = ForceContribution::Flow;
    ForceFieldQuantity   Quantity           = ForceFieldQuantity::Speed;
    ForceFieldProjection Projection         = ForceFieldProjection::Map;
    bool                 AutomaticScale     = true;
    float                FixedScale         = 30.0f;   // [unit of the contribution] full scale of the colours
    bool                 Heatmap            = true;
    bool                 SmoothHeatmap      = true;
    bool                 Arrows             = true;
    bool                 Tracers            = true;
    bool                 ShowCascades       = false;
    bool                 Labels             = true;
    bool                 SceneOverlay       = false;   // the slice drawn over the scene viewport
    float                SliceHeightMetres  = 10.0f;   // [m] above the ground
    float                HeatmapOpacity     = 1.0f;
    float                ArrowSpacingPixels = 40.0f;
    uint32_t             TracerCount        = 360;
    float                CellPixels         = 6.0f;    // [px] the sample cell on screen

    // 📝 The timeline windowed and triggered forces follow; TimelineSeconds is not saved.
    float    TimelineDurationSeconds = 120.0f;
    float    TimelineRate            = 1.0f;
    bool     TimelinePlaying         = true;
    double   TimelineSeconds         = 0.0;
};

struct WindConfiguration : RevisionCounter
{
    bool  Enabled              = true;
    float DirectionDegrees     = 45.0f;
    float SpeedMetresPerSecond = 6.8f;
    float GustAmplitude        = 3.4f;
    float CurlNoiseTurbulence  = 0.48f;
    float CurlFrequencyHz      = 0.25f;
    bool  AdvectClouds         = true;
    bool  AdvectPlumes         = true;
    bool  AdvectParticles      = true;
    // 📝 Relative gust strength is separate from the earlier absolute GustAmplitude [m/s].
    float GustFrequencyHz = 0.085f;
    float TurbulenceScaleMetres = 95.0f;
    float GustStrength           = 0.25f;
    float ShearPerKilometre      = 0.15f;
    float VeerDegreesPerKilometre = 8.0f;
    // 📝 A slow speed and direction variation over tens of kilometres, so a 100 km view is not one uniform arrow.
    float    MesoscaleScaleMetres = 18000.0f;
    float    MesoscaleStrength    = 0.22f;
    float    GroundHeightMetres   = 0.0f;
    uint32_t Seed                 = 7331;
};

// 📝 One localized force (a ForceSource record under the Force Field). The position is in scene axes (x east, y north,
//    z up); bearings are compass degrees of travel (0° north, 90° east). The type's own parameters share one record:
//    each type reads the ones its descriptor lists (ForceSourceCatalogue) and the rest keep their defaults.
struct ForceSourceConfiguration : RevisionCounter
{
    bool                Enabled              = true;
    ForceSourceCategory Category             = ForceSourceCategory::GustPuff;
    double              CentreMetres[3]      = {0.0, 0.0, 0.0};
    ForceReach          Reach                = ForceReach::Sphere;
    double              ReachMetres          = 100.0;          // [m] sphere or column radius
    double              HeightMetres         = 200.0;          // [m] column or box height, type height
    double              HalfExtentsMetres[2] = {50.0, 50.0};   // [m] box half extents across and along the bearing
    ForceFalloff        Falloff              = ForceFalloff::Smooth;
    ForceActivation     Activation           = ForceActivation::Always;
    double              BeginsSeconds        = 0.0;
    double              LastsSeconds         = 0.0;            // [s] 0: forever
    double              RepeatsSeconds       = 0.0;            // [s] 0: once
    double              RampInSeconds        = 1.25;
    double              RampOutSeconds       = 2.0;
    uint32_t            ActsOn               = 0;              // ForceReceiver bits; 0: every receiver

    double Strength                   = 12.0;   // [m/s, m/s² or 1/s by contribution]
    double InnerRadiusMetres          = 20.0;
    double CollarRadiusMetres         = 96.0;
    double InflowMetresPerSecond      = 26.0;
    double UpdraftMetresPerSecond     = 42.0;
    double Spin                       = 1.0;    // [-] +1 cyclonic (counter-clockwise from above), −1 anticyclonic
    double TrackBearingDegrees        = 109.0;
    double TrackSpeedMetresPerSecond  = 3.7;
    double FrontSpeedMetresPerSecond  = 18.0;
    double CrestDepthMetres           = 85.0;
    double HalfWidthMetres            = 900.0;
    double BearingDegrees             = 0.0;
    double DowndraftMetresPerSecond   = 24.0;
    double RingRadiusMetres           = 150.0;
    double LengthMetres               = 95.0;
    double LiftMetresPerSecond        = 0.8;
    double VerticalRadiusMetres       = 14.0;
    double Drift                      = 0.92;   // [-] share of the wind at its height that carries a gust puff
    double ExpansionMetresPerSecond   = 110.0;
    double ThicknessMetres            = 14.0;
    double InitialRadiusMetres        = 3.0;
    double CirculationMetresPerSecond = 7.5;
    double BodySpeedMetresPerSecond   = 22.0;
    double Swirl                      = 2.0;    // [m/s²]
    double SwallowRadiusMetres        = 0.0;

    double TriggeredAtSeconds = -1.0;   // [s] timeline time of the last Fire; < 0 armed. Not saved.
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONTINUUM & PARTICLE SOLVERS
//------------------------------------------------------------------------------------------------------------------------

struct FluidPlumeConfiguration : RevisionCounter
{
    bool     Enabled              = true;
    uint32_t LatticeResolutionX   = 128;
    uint32_t LatticeResolutionY   = 64;
    uint32_t LatticeResolutionZ   = 128;
    float    BuoyancyLift         = 2.15f;
    float    VorticityConfinement = 1.85f;
    float    DissipationRate      = 0.22f;
    float    FuelInjectionRate    = 1.40f;
    float    FlameIntensity       = 1.65f;
    float    SootExtinction       = 1.10f;
    float    BlackbodyCoreKelvin  = 1850.0f;
    bool     CombustionEnabled    = true;
    bool     SelfShadowing        = true;
};

struct FractureConfiguration : RevisionCounter
{
    bool     Enabled                 = true;
    uint32_t VoronoiCellCount        = 32;
    float    ImpulseThresholdNs      = 42.0f;
    float    ImpactRadialBias        = 0.65f;
    float    DebrisLifetimeSeconds   = 18.0f;
    float    SleepLinearVelocity     = 0.15f;
    bool     FractureArmed           = true;
    bool     ImpactDustBurst         = true;
    uint32_t PrefractureTriggerCount = 0;
};

struct ParticleConfiguration : RevisionCounter
{
    bool  Enabled                 = true;
    float SparkRatePerSecond      = 280.0f;
    float RainDropDensity         = 0.0f;
    float SnowFlakeDensity        = 0.0f;
    float GravityMultiplier       = 1.0f;
    float AerodynamicDrag         = 0.32f;
    float BounceRestitution       = 0.45f;
    bool  GpuWeatherPrecipitation = true;
    bool  DepthBufferCollisions   = true;
};

//------------------------------------------------------------------------------------------------------------------------
//                                        COMPOSABLE OBJECT PROPERTY CONFIGURATIONS
//------------------------------------------------------------------------------------------------------------------------
// Any scene RecordEntry can attach zero, one, or multiple of these independent property configurations:
//   - SpatialConfiguration       : 3D position, orientation, and non-uniform scale
//   - PhysicsConfiguration       : MassConfiguration (mass, density, volume + helpers), velocity, friction, restitution
//   - ThermalConfiguration       : Temperature (K / °C), specific heat, conductivity, emissivity, radiative flux
//   - ChemicalConfiguration      : Molar mass, moles, concentration, activation energy, Arrhenius rate, enthalpy
//   - SurfaceOpticsConfiguration : Albedo, roughness, metalness, index of refraction (only for rendered surfaces)

struct SpatialConfiguration : RevisionCounter
{
    float PositionMetres[3]     = {0.0f, 1.0f, 0.0f};
    float OrientationDegrees[3] = {0.0f, 0.0f, 0.0f};
    float Scale[3]              = {1.0f, 1.0f, 1.0f};

    [[nodiscard]] float ComputeEuclideanDistance(const SpatialConfiguration& Other) const noexcept
    {
        const float Dx = PositionMetres[0] - Other.PositionMetres[0];
        const float Dy = PositionMetres[1] - Other.PositionMetres[1];
        const float Dz = PositionMetres[2] - Other.PositionMetres[2];
        return std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
    }
};

struct MassConfiguration
{
    float MassKilograms          = 12.5f;     // [kg]
    float DensityKgPerCubicMetre = 2700.0f;   // [kg/m^3]
    float VolumeCubicMetres      = 0.00463f;  // [m^3]

    void SyncMassFromDensityAndVolume() noexcept
    {
        MassKilograms = std::max(1.0e-6f, DensityKgPerCubicMetre * VolumeCubicMetres);
    }

    void SyncVolumeFromMassAndDensity() noexcept
    {
        VolumeCubicMetres = MassKilograms / std::max(1.0e-4f, DensityKgPerCubicMetre);
    }

    [[nodiscard]] float ComputeKineticEnergyJoules(float LinearSpeedMps) const noexcept
    {
        return 0.5f * MassKilograms * LinearSpeedMps * LinearSpeedMps;
    }

    [[nodiscard]] float ComputeLinearMomentumNs(float LinearSpeedMps) const noexcept
    {
        return MassKilograms * LinearSpeedMps;
    }

    [[nodiscard]] float ComputeSolidSphereInertia(float RadiusMetres) const noexcept
    {
        return 0.4f * MassKilograms * RadiusMetres * RadiusMetres;
    }
};

struct PhysicsConfiguration : RevisionCounter
{
    bool              Enabled                  = true;
    MassConfiguration Mass{};
    float             LinearVelocityMps[3]     = {0.0f, 0.0f, 0.0f};
    float             AngularVelocityRadSec[3] = {0.0f, 0.0f, 0.0f};
    float             Restitution              = 0.42f;
    float             StaticFriction           = 0.58f;
    float             DynamicFriction          = 0.44f;
    float             LinearDamping            = 0.04f;
    bool              DynamicBody              = true;
    bool              GravityCoupled           = true;

    [[nodiscard]] float ComputeLinearSpeedMps() const noexcept
    {
        return std::sqrt(LinearVelocityMps[0] * LinearVelocityMps[0] +
                         LinearVelocityMps[1] * LinearVelocityMps[1] +
                         LinearVelocityMps[2] * LinearVelocityMps[2]);
    }
};

struct ThermalConfiguration : RevisionCounter
{
    static constexpr float StefanBoltzmannConstant = 5.670374419e-8f; // [W / (m^2 * K^4)]

    bool  Enabled                      = true;
    float TemperatureKelvin            = 293.15f; // [K]
    float SpecificHeatJPerKgK          = 897.0f;  // [J / (kg * K)]
    float ThermalConductivityWPerMK    = 205.0f;  // [W / (m * K)]
    float SurfaceEmissivity            = 0.82f;   // [0..1]
    float ConvectionCoefficientWPerM2K = 14.0f;   // [W / (m^2 * K)]
    bool  ConductiveExchange           = true;
    bool  RadiativeExchange            = true;

    [[nodiscard]] float QueryCelsius() const noexcept
    {
        return TemperatureKelvin - 273.15f;
    }

    void ConfigureCelsius(float Celsius) noexcept
    {
        TemperatureKelvin = std::max(0.01f, Celsius + 273.15f);
    }

    [[nodiscard]] float ComputeSensibleHeatJoules(float MassKilograms, float ReferenceKelvin = 293.15f) const noexcept
    {
        return MassKilograms * SpecificHeatJPerKgK * (TemperatureKelvin - ReferenceKelvin);
    }

    [[nodiscard]] float ComputeRadiativeFluxWPerM2() const noexcept
    {
        const float T2 = TemperatureKelvin * TemperatureKelvin;
        return std::clamp(SurfaceEmissivity, 0.0f, 1.0f) * StefanBoltzmannConstant * T2 * T2;
    }
};

struct ChemicalConfiguration : RevisionCounter
{
    static constexpr float UniversalGasConstant = 8.314462618f; // [J / (mol * K)]

    bool  Enabled                    = true;
    float MolarMassGramsPerMole      = 18.015f; // [g/mol]
    float AmountMoles                = 2.0f;    // [mol]
    float ConcentrationMolPerLitre   = 1.0f;    // [mol/L]
    float ActivationEnergyKjPerMol   = 48.5f;   // [kJ/mol]
    float PreExponentialFactorPerSec = 1.0e5f;  // [1/s]
    float EnthalpyDeltaKjPerMol      = -57.1f;  // [kJ/mol] (negative = exothermic)
    int   MatterPhaseIndex           = 1;       // 0 Solid · 1 Liquid · 2 Gas · 3 Plasma
    bool  ReversibleEquilibrium      = true;
    bool  CatalyzedPathway           = false;

    [[nodiscard]] float ComputeSubstanceMassKilograms() const noexcept
    {
        return std::max(0.0f, AmountMoles * MolarMassGramsPerMole * 1.0e-3f);
    }

    [[nodiscard]] float ComputeArrheniusRateConstant(float TemperatureKelvin) const noexcept
    {
        const float SafeKelvin  = std::max(1.0f, TemperatureKelvin);
        const float EffectiveEa = (CatalyzedPathway ? ActivationEnergyKjPerMol * 0.65f : ActivationEnergyKjPerMol) * 1000.0f;
        return PreExponentialFactorPerSec * std::exp(-EffectiveEa / (UniversalGasConstant * SafeKelvin));
    }

    [[nodiscard]] float ComputeReactionHeatKilojoules() const noexcept
    {
        return AmountMoles * EnthalpyDeltaKjPerMol;
    }
};

struct SurfaceOpticsConfiguration : RevisionCounter
{
    bool  Enabled                   = true;
    float AlbedoRgb[3]              = {0.74f, 0.77f, 0.81f};
    float Roughness                 = 0.38f;
    float Metallic                  = 0.08f;
    float IndexOfRefraction         = 1.50f;
    bool  CastShadows               = true;
    bool  ReceiveGlobalIllumination = true;
};

//------------------------------------------------------------------------------------------------------------------------
//                                              SCENE GEOMETRY CONFIGURATIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One Array modifier, after Blender's: Count copies of everything above it in the modifier stack. Copy n moves by n × (the
//    constant offset in metres + the relative offset in extents of one copy) along the object's own axes and turns by n × the
//    rotation step about the object's Z axis, in place (about the copy's own origin) or about the object's origin (radial).
struct ArrayModifierConfiguration
{
    std::string Label               = "Array";
    bool        Enabled             = true;                   // [-] the viewport switch: off, the stack skips the modifier
    bool        Expanded            = true;                   // [-] the panel is unfolded in the stack
    uint32_t    Count               = 2;                      // [-] copies, the first one included
    bool        RelativeOffset      = true;                   // [-]
    float       RelativeFactor[3]   = {1.0f, 0.0f, 0.0f};     // [-] multiples of one copy's extent
    bool        ConstantOffset      = false;                  // [-]
    float       ConstantMetres[3]   = {1.0f, 0.0f, 0.0f};     // [m]
    float       RotationStepDegrees = 0.0f;                   // [°] about Z, per copy
    bool        RadialPivot         = false;                  // [-] turn about the object's origin instead of each copy's own
};

// 📝 One surface slot an object owns instead of the material family it shares: the family it replaces and its twelve
//    MaterialSpecification channels (albedo rgb, roughness, incandescence rgb, metalness, coat, coat roughness, specular, transmission).
struct SurfaceSlotConfiguration
{
    uint32_t              Family = 0;      // [-]
    std::array<float, 12> Channels{};      // [-]
};

// 📝 One object of the level's scene (levelspace type "Geometry"): the scene archive's placements it stands for (or an engine
//    shape), the quick controls, the transform, the modifier stack, its own surfaces and the fracture, edited by the geometry
//    inspector (the Project Zero editor's).
struct GeometryConfiguration : RevisionCounter
{
    // 📝 The archive occupants: one name stem per part, each matching the stem alone or the stem and one trailing token
    //    ("Boundary North -58"), in archive order; OccupantFirst / OccupantCount take a range of every part's matches.
    std::vector<std::string> OccupantStems{};
    uint32_t                 OccupantFirst = 0;       // [-]
    uint32_t                 OccupantCount = 0;       // [-] 0: every match from OccupantFirst
    uint32_t                 BaseCopies    = 0;       // [-] archive placements the modifier stack starts from; 0: all of them
    // 📝 An object that does not claim the archive's occupants (a duplicate) lays out reserve copies of the same shapes; an object
    //    with a ShapeName is an engine shape (EngineContent/ShapeArchives/<ShapeName>.toml) and names no occupants.
    bool                     ClaimsArchive = true;
    std::string              ShapeName{};
    // 📝 Where the object's origin sits in the archive (position and turn about Z): the transform at which the object shows
    //    the archive's own placements. Unauthored, it is the first placement's.
    bool                     ArchiveOriginAuthored = false;
    float                    ArchiveOriginMetres[3] = {0.0f, 0.0f, 0.0f};   // [m]
    float                    ArchiveYawDegrees      = 0.0f;                 // [°]
    std::string              GroupName{};             // [-] Outliner group
    std::string              GlyphName{};             // [-] Outliner glyph (GlyphSymbol name)
    std::string              Notes{};

    bool Visible            = true;    // [-] drawn, traced and unioned into the distance field
    bool Dynamic            = false;   // [-] its cards relight every refresh (moves at run time once the physics runs)
    bool CastShadows        = true;    // [-] in the shadow runs, the shadow rays and the analytic solar depth
    bool GlobalIllumination = true;    // [-] in the distance union and the traced rays
    bool Physics            = false;   // [-] authored: a rigid body once the physics runs
    bool Locked             = false;   // [-] the transform and the modifier stack refuse edits

    SpatialConfiguration                    Spatial{};     // [-] the object's transform in the scene (z up)
    std::vector<ArrayModifierConfiguration> Modifiers{};   // [-] top to bottom
    std::vector<SurfaceSlotConfiguration>   SurfaceSlots{};   // [-] families this object no longer shares
    FractureConfiguration                   Fracture = [] { FractureConfiguration Disabled; Disabled.Enabled = false; return Disabled; }();

    // 📝 Resolved by the viewport against the loaded scene archive and published by the workspace; not saved.
    struct Resolution
    {
        bool                               Bound            = false;   // [-] every part matched the same number of occupants
        uint32_t                           Authored         = 0;       // [-] placements in the archive: the most copies shown
        uint32_t                           Copies           = 0;       // [-] copies the base and the modifier stack produce
        uint32_t                           Shown            = 0;       // [-] copies drawn
        uint32_t                           Layout           = 0;       // [-] base placements it lays out (the archive's; 1 for a shape)
        uint32_t                           Reserved         = 0;       // [-] copies laid out from the reserve (beyond the archive's)
        uint32_t                           ReserveFree      = 0;       // [-] reserve occupants nobody holds
        bool                               Shape            = false;   // [-] an engine shape
        bool                               OwnSurfaces      = false;   // [-] holds a material block of its own
        uint32_t                           Parts            = 0;       // [-] shapes per copy
        uint32_t                           Facets           = 0;       // [-] triangles per copy
        float                              Extent[3]        = {};      // [m] one copy's local bounds
        float                              BoundsMinimum[3] = {};      // [m] world bounds of the shown copies
        float                              BoundsMaximum[3] = {};      // [m]
        std::vector<std::string>           Shapes{};                   // [-] the parts' facet sets
        std::vector<uint32_t>              Families{};                 // [-] material families the copies use, ascending
        std::vector<std::string>           FamilyNames{};
        std::vector<std::array<float, 12>> FamilyChannels{};           // [-] albedo, roughness, emission, metalness, coat, specular, transmission
        std::vector<uint32_t>              FamilyUsers{};              // [-] geometry objects sharing each family
        bool                               Breakable        = false;   // [-] a smash-alley prop with a fragment pool
        uint32_t                           FragmentCapacity = 0;       // [-]
        bool                               Luminous         = false;   // [-] carries clustered lamps (street lamps, lanterns)
        std::string                        Diagnostic{};               // [-] why it did not bind
    } Resolved{};
};

} // namespace Frontier
