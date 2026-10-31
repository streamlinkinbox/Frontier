/*==========================================================================================================================================
                                                         ENVIRONMENT RECORDS
                                     ------------------------------------------------------------------
             Global reusable Engine records: Folders, Sun, Lens Flare, Atmosphere, Clouds, and Shared Wind Forcefield.
==========================================================================================================================================*/

// 🧩 Global reusable Engine records: Folders, Sun, Lens Flare, Atmosphere, Clouds, and Shared Wind Forcefield.

#pragma once

#include "RecordEntry.h"

#include "Engine/Rendering/AtmosphereModel.h"
#include "Engine/Rendering/CelestialModel.h"
#include "Engine/Rendering/CloudField.h"
#include "Engine/Rendering/LensFlareModel.h"

#include <array>
#include <cstdint>
#include <string>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    FOLDER RECORD
//------------------------------------------------------------------------------------------------------------------------

// 📝 Reusable Outliner folder enclosure shared by every Frontier project. Organises scene records into named,
//    colour-tagged groups with one-click cascade visibility and Inspector enclosure controls.
class FolderRecord final : public RecordEntry
{
  public:
    explicit FolderRecord(std::string Title = "New Folder", std::string Tag = "WORKSPACE", uint32_t PackedRgba = 0xFF7AD9A3u);

    std::string Title;                // [text] - Folder display name
    std::string Tag;                  // [text] - Uppercase group eyebrow ("ENVIRONMENT", "VEHICLE", ...)
    std::string Notes;                // [text] - Optional authoring note shown in Folder Observatory
    uint32_t    AccentRgba = 0xFF7AD9A3u;
    bool        Enabled    = true;    // [-] - Cascade visibility switch
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     SUN RECORD
//------------------------------------------------------------------------------------------------------------------------

enum class SolarLightingPreset : uint8_t
{
    Noon   = 0,
    Golden = 1,
    Dusk   = 2,
    Night  = 3
};

struct SolarDayTrajectory
{
    std::array<double, 97> ElevationDegrees{};   // [deg] - Sampled every 15 minutes across [0..24] h
    double                 RiseHour       = -1.0; // [h] - < 0 when sun never rises
    double                 SetHour        = -1.0; // [h] - < 0 when sun never sets
    double                 DaylightHours  = 0.0;  // [h] - Integrated duration above 0 deg
};

class SunRecord final : public RecordEntry
{
  public:
    SunRecord();

    double  ElevationDegrees     = 38.0;     // [deg] - [-90..90], positive above limb
    double  AzimuthDegrees       = 145.0;    // [deg] - [-180..180], +X east / +Y north
    double  SolarIlluminance     = 12.0;     // [x] - [0..60] exhibit intensity multiplier
    double  TemperatureKelvin    = 5778.0;   // [K] - [2000..10000] Planck source temperature
    double  AngularRadiusDegrees = 0.27;     // [deg] - [0.05..2.0] half-angle (diameter = 2 * radius)
    double  OpticalGlow          = 1.0;      // [x] - [0..3.0] aureole multiplier
    double  SolarTimeHours       = 12.0;     // [h] - [0..24] local solar control clock
    double  DayDurationHours     = 6.0;      // [h] - [0.01..24] real hours per animated cycle
    double  LatitudeDegrees      = -26.18;   // [deg] - [-90..90]
    double  LongitudeDegrees     = 28.32;    // [deg] - [-180..180]
    double  UtcOffsetHours       = 2.0;      // [h] - [-12..14]
    int32_t CalendarYear         = 2026;     // [year]
    int32_t CalendarMonth        = 10;       // [month]
    int32_t CalendarDay          = 31;       // [day]
    int32_t ColourSourceIndex    = 0;        // [-] - 0 = Temperature, 1 = RGB
    int32_t PresetIndex          = 0;        // [-] - SolarLightingPreset ordinal
    double  TintRed              = 1.0;      // [-] - Linear RGB [0..1]
    double  TintGreen            = 0.96;     // [-]
    double  TintBlue             = 0.90;     // [-]
    bool    Enabled              = true;     // [-] - Sunlight toggle
    bool    DiscEnabled          = true;     // [-] - Finite solar disc in sky
    bool    DayCycleActive       = false;    // [-] - Continuous day-cycle animation
    bool    TimeLinked           = false;    // [-] - Driven by ephemeris clock + coordinates

    void                             ResetToDefaults() noexcept;
    void                             ApplyPreset(SolarLightingPreset Preset) noexcept;
    void                             SynchronizeFromEphemeris() noexcept;
    [[nodiscard]] SpatialVector      QuerySunDirection() const noexcept;
    [[nodiscard]] SolarDayTrajectory EvaluateDayTrajectory() const noexcept;
    [[nodiscard]] static double      EvaluateNormalizedPlanck(double WavelengthNanometres, double Kelvin) noexcept;
    [[nodiscard]] static SpectrumRgb EvaluateBlackbodyRgb(double Kelvin) noexcept;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  LENS FLARE RECORD
//------------------------------------------------------------------------------------------------------------------------

enum class LensFlarePresetCategory : uint8_t
{
    Cinematic    = 0,
    Clean        = 1,
    Anamorphic   = 2,
    Subtle       = 3,
    Halo         = 4,
    CustomLayers = 5
};

class LensFlareRecord final : public RecordEntry
{
  public:
    LensFlareRecord();

    int32_t PresetIndex         = 0;       // [-] - LensFlarePresetCategory ordinal
    double  PreviewSourceU      = 0.28;    // [UV] - [0.06..0.94] interactive composite light X
    double  PreviewSourceV      = 0.43;    // [UV] - [0.08..0.92] interactive composite light Y
    double  Intensity           = 0.30;    // [x] - [0..5]
    double  Spread              = 1.00;    // [x] - [0.25..2.0]
    double  ChromaticSeparation = 0.20;    // [-] - [0..1]
    int32_t GhostShapeIndex     = 1;       // [-] - 0 = Round (0), 1 = Hexagon (6), 2 = Octagon (8)
    double  GhostCount          = 6.0;     // [count] - [0..24]
    double  GhostBrightness     = 0.30;    // [x] - [0..2]
    double  GhostSpacing        = 1.00;    // [x] - [0..3]
    double  HaloRadius          = 0.22;    // [UV] - [0.01..0.6]
    double  HaloWidth           = 0.025;   // [UV] - [0.005..0.2]
    double  HaloBrightness      = 0.20;    // [x] - [0..2]
    double  RotationDegrees     = 0.0;     // [deg] - [0..360]
    double  RayPairs            = 6.0;     // [pairs] - [2..12]
    double  StreakGain          = 0.30;    // [x] - [0..3]
    double  ApertureBlades      = 6.0;     // [blades] - [2..12]
    bool    Enabled             = true;    // [-] - Scene lens-flare toggle
    bool    AnamorphicEnabled   = true;    // [-] - Cool horizontal anamorphic streak
    bool    WarmStreakEnabled   = false;   // [-] - Rotatable warm streak
    bool    StarburstEnabled    = true;    // [-] - Polygonal diffraction burst

    void                        ApplyPreset(LensFlarePresetCategory Preset) noexcept;
    [[nodiscard]] int32_t       QueryGhostSides() const noexcept;
    [[nodiscard]] int32_t       QueryActiveRayPairs() const noexcept;
    [[nodiscard]] SpectrumRgb   EvaluateOpticalSample(double SampleU, double SampleV, double AspectRatio) const noexcept;
    [[nodiscard]] SpectrumRgb   EvaluateOpticalSampleAtSource(double SampleU, double SampleV, double SourceU,
                                                              double SourceV, double AspectRatio) const noexcept;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  ATMOSPHERE RECORD
//------------------------------------------------------------------------------------------------------------------------

class AtmosphereRecord final : public RecordEntry
{
  public:
    AtmosphereRecord();

    AtmosphereParameters Parameters;                     // [-] - Underlying Bruneton/Hillaire physical medium
    TransmittanceLut     LookupTable;                    // [-] - Baked 128x32 optical-depth LUT
    uint32_t             LookupRevision          = 1;    // [-] - Incremented whenever LUT re-bakes
    double               ProbeAltitudeMetres     = 8000.0; // [m] - [0..30000] column scan probe
    double               RayleighMultiplier      = 1.0;  // [x] - [0..3]
    double               MieMultiplier           = 1.0;  // [x] - [0..6]
    double               OzoneMultiplier         = 1.0;  // [x] - [0..4]
    double               RayleighScaleMetres     = 8000.0; // [m] - [1000..16000]
    double               MieScaleMetres          = 1200.0; // [m] - [500..8000]
    double               MieAnisotropyG          = 0.80; // [-] - [-0.95..0.95]
    double               SkyBrightnessScale      = 1.0;  // [x] - [0..3]
    double               LimbGlow                = 1.0;  // [x] - [0..3] ("Horizon glow" in UI)
    double               BoundaryLine            = 1.0;  // [x] - [0..3] ("White line" in UI)
    double               LookupSamples           = 40.0; // [samples] - [8..64]
    double               SkyTintRed              = 1.0;  // [-]
    double               SkyTintGreen            = 1.0;  // [-]
    double               SkyTintBlue             = 1.0;  // [-]
    double               GroundAlbedoRed         = 0.30; // [-]
    double               GroundAlbedoGreen       = 0.30; // [-]
    double               GroundAlbedoBlue        = 0.30; // [-]
    bool                 Enabled                 = true;
    bool                 AirMassAttenuation      = true;
    bool                 MultipleScattering      = true;
    bool                 CivilTwilightOnly       = true;
    bool                 UseBakedLookup          = true;

    void                        SynchronizeParameters() noexcept;
    void                        RebakeLookupTable();
    [[nodiscard]] double        QueryRelativeRayleighDensity(double AltitudeMetres) const noexcept;
    [[nodiscard]] double        QueryRelativeMieDensity(double AltitudeMetres) const noexcept;
    [[nodiscard]] SpectrumRgb   EvaluateSolarTransmission(double SunElevationDegrees) const noexcept;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    CLOUD RECORD
//------------------------------------------------------------------------------------------------------------------------

class CloudRecord final : public RecordEntry
{
  public:
    CloudRecord();

    CloudParameters Parameters;                           // [-] - Underlying volumetric slab + FBM parameters
    int32_t         FormationIndex        = 2;            // [-] - 0=Stratus, 1=Stratocumulus, 2=Cumulus, 3=Cumulonimbus, 4=Altostratus, 5=Cirrus
    double          Coverage              = 0.55;         // [-] - [0..1]
    double          DensityMultiplier     = 1.00;         // [x] - [0..4]
    double          FloorAltitudeMetres   = 1500.0;       // [m] - [100..20000] ("Base altitude")
    double          ThicknessMetres       = 1200.0;       // [m] - [100..6000]
    double          CeilingLimitMetres    = 14000.0;      // [m] - [4000..20000]
    double          FeatureScale          = 1.00;         // [x] - [0.2..3.0]
    double          AnvilShaping          = 0.50;         // [-] - [0..1]
    double          DetailErosion         = 0.30;         // [-] - [0..1]
    double          ForwardAnisotropyG    = 0.35;         // [-] - [-0.8..0.8]
    double          LightAbsorption       = 0.15;         // [-] - [0..1]
    double          PowderResponse        = 0.45;         // [-] - [0..1]
    double          SunShadowTaps         = 2.0;          // [taps] - [0..4]
    double          ResolutionPercent     = 35.0;         // [% / axis] - [25..100]
    double          SkyReferenceSamples   = 12.0;         // [samples] - [8..64]
    double          LocalReferenceSamples = 16.0;         // [samples] - [8..64]
    double          ShadowStrength        = 0.65;         // [-] - [0..1]
    double          ShadowSoftnessMetres  = 35.0;         // [m] - [0..200]
    double          ShadowSpanMetres      = 6000.0;       // [m] - [1000..16000]
    double          ShadowSamples         = 6.0;          // [samples] - [4..12]
    bool            Enabled               = true;
    bool            CastShadows           = true;
    bool            FollowSharedWind      = true;

    void                         SynchronizeParameters(double WindSpeedMps = 7.0, double WindBearingDegrees = 90.0) noexcept;
    [[nodiscard]] double         QueryEffectiveTopMetres() const noexcept;
    [[nodiscard]] double         QueryRenderedDepthMetres() const noexcept;
    [[nodiscard]] bool           QueryCeilingClipped() const noexcept;
    [[nodiscard]] uint32_t       QueryCompleteLayerRaySamples() const noexcept;
    [[nodiscard]] const char*    QueryFormationName() const noexcept;
    [[nodiscard]] static double  EvaluateFormationProfile(double NormalizedHeight, int32_t Formation, double Anvil) noexcept;
    [[nodiscard]] double         SampleTopDownDensity(double NormalizedU, double NormalizedV) const noexcept;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     WIND RECORD
//------------------------------------------------------------------------------------------------------------------------

class WindRecord final : public RecordEntry
{
  public:
    WindRecord();

    double ReferenceSpeedMps        = 7.0;    // [m/s] - [0..40]
    double TravelBearingDegrees     = 90.0;   // [deg] - [0..360] clockwise from north (where air travels TO)
    double GustStrength             = 0.25;   // [-] - [0..1]
    double Turbulence               = 0.15;   // [-] - [0..1]
    double ShearPerKilometre        = 0.15;   // [x/km] - [0..2]
    double VeerDegreesPerKilometre  = 8.0;    // [deg/km] - [-60..60]
    double PrecipitationDrift       = 0.65;   // [-] - [0..1]
    bool   Enabled                  = true;
    bool   RouteClouds              = true;
    bool   RouteLocalCloud          = true;
    bool   RouteLocalFog            = true;
    bool   RoutePrecipitation       = true;

    [[nodiscard]] double        QueryFromBearingDegrees() const noexcept;
    void                        ConfigureFromBearingDegrees(double FromDegrees) noexcept;
    [[nodiscard]] static double EvaluateGustFactor(double Gust, double PhaseSeconds) noexcept;
    [[nodiscard]] SpatialVector EvaluateSteadyVelocityAtAltitude(double AltitudeMetres) const noexcept;
};

} // namespace Frontier
