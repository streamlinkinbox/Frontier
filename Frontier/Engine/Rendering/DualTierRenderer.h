/*==========================================================================================================================================
                                                        DUAL TIER RENDERER
                                     ------------------------------------------------------------------
             Dual-tier Proving Grounds renderer: RTX hardware RayQuery + ReSTIR GI + DielectricBoundary + SVGF,
               paired with GTX 4-cascade toroidal 3D SDF + SurfaceCards + OrderedSoftwareBvh triangle reflections.
==========================================================================================================================================*/

// 🧩 Dual-tier illumination engine executing both the RTX RayQuery + ReSTIR path and the GTX 4-cascade SDF + BVH fallback.

#pragma once

#include "AtmosphereModel.h"
#include "CelestialModel.h"
#include "CloudField.h"
#include "DistanceField.h"
#include "FogMedium.h"
#include "SurfaceCards.h"
#include "TriangleVisibilitySpace.h"
#include "WeatherEffects.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Frontier
{

struct PathReplayRayQueryDeviceCapabilities;

//------------------------------------------------------------------------------------------------------------------------
//                                             EXECUTION TIER & INSPECTION MODES
//------------------------------------------------------------------------------------------------------------------------

enum class DualTierExecutionMode : uint8_t
{
    HardwareRayQueryRtx = 0,   // [-] - VK_KHR_acceleration_structure + VK_KHR_ray_query + ReSTIR GI + Dielectric glass
    ToroidalSdfBvhGtx   = 1    // [-] - 4-cascade SceneClip0..3 SDF cone GI + SurfaceCards + OrderedSoftwareBvh reflections
};

enum class ViewportInspectionMode : uint8_t
{
    FinalLit            = 0,   // [-] - Full tone-mapped linear HDR composite
    IndirectIrradiance  = 1,   // [-] - ReSTIR GI (RTX) vs 4-cascade SDF + SurfaceCards (GTX)
    SpecularReflections = 2,   // [-] - RayQuery reflections (RTX) vs OrderedSoftwareBvh reflections (GTX)
    DistanceCascades    = 3,   // [-] - Toroidal 3D SDF cascades SceneClip0..3 + signed-distance isolines
    DielectricGlazing   = 4,   // [-] - Exact Fresnel + Beer-Lambert cabin glass transmittance
    SurfaceNormals      = 5    // [-] - World-space XYZ surface normals
};

struct DualTierTierDecision
{
    DualTierExecutionMode Mode                = DualTierExecutionMode::ToroidalSdfBvhGtx;
    bool                  HardwareRtxCapable  = false;
    const char*           DiagnosticSummary   = "";
};

// Negotiates RTX vs GTX fallback from Vulkan physical-device capabilities + user preference.
[[nodiscard]] DualTierTierDecision NegotiateDualTierExecution(
    const PathReplayRayQueryDeviceCapabilities& Capabilities,
    bool                                        PreferHardwareRtx = true) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                              PROVING GROUNDS FRAME INPUTS
//------------------------------------------------------------------------------------------------------------------------

struct EverestViewportInputs
{
    DualTierExecutionMode  ExecutionMode           = DualTierExecutionMode::HardwareRayQueryRtx;
    ViewportInspectionMode InspectionMode          = ViewportInspectionMode::FinalLit;

    // Orbit camera around ControlVehicle centre (0, 0, 0.65 m)
    double CameraYawDegrees                        = -36.0;    // [deg]
    double CameraPitchDegrees                      = 16.5;     // [deg]
    double CameraDistanceMetres                    = 5.85;     // [m]

    // Solar & sky coupling
    SpatialVector        SunDirection{0.48, -0.62, 0.62};      // [-] - Unit vector toward sun
    double               SunElevationDegrees       = 38.0;     // [deg]
    double               SunTemperatureKelvin      = 5778.0;   // [K]
    double               SolarIlluminance          = 12.0;     // [x]
    bool                 SunEnabled                = true;
    bool                 SunDiscEnabled            = true;
    AtmosphereParameters Atmosphere{};
    bool                 AtmosphereEnabled         = true;
    double               SkyBrightnessScale        = 1.0;

    // Volumetric clouds & height/aerial fog
    bool            CloudsEnabled                  = true;
    double          CloudCoverage                  = 0.55;
    FogParameters   Fog{};
    bool            FogEnabled                     = false;
    double          AerialStartMetres              = 18.0;     // [m]
    double          AerialDensity                  = 0.008;    // [1/m]

    // Night sky (Stars & Moon)
    SpatialVector MoonDirection{-0.42, 0.58, 0.70};
    double        MoonIlluminationFraction         = 0.72;     // [0..1]
    double        MoonlightStrength                = 0.25;     // [x]
    bool          MoonEnabled                      = true;
    bool          StarsEnabled                     = true;
    double        StarLimitingMagnitude            = 6.0;      // [mag]
    double        StarRotationDegrees              = 0.0;      // [deg]

    // Storm, lightning & surface deposit
    bool   LightningActive                         = false;
    double LightningFlashIntensity                 = 0.0;      // [0..1]
    double PrecipitationRate                       = 0.0;      // [0..1]
    bool   SnowMode                                = false;
    double SurfaceWetnessOrSnow                    = 0.0;      // [0..1]

    // Dual-tier GI / reflection budgets & ControlVehicle telemetry
    double      BvhReflectionRoughnessCutoff       = 0.55;     // [-]
    double      SdfOuterSpanMetres                 = 160.0;    // [m]
    double      ExposureBiasStops                  = 0.0;      // [EV]
    bool        DielectricGlazingEnabled           = true;
    bool        SurfaceCardsEnabled                = true;
    SpectrumRgb VehicleBodyAlbedo{0.11, 0.34, 0.52};           // [-] - Everest metallic glacier-blue
    double      VehicleBodyRoughness               = 0.14;     // [-]
    bool        HeadlampsEnabled                   = true;
    bool        BrakeLampsEnabled                  = true;
    bool        HazardLampsEnabled                 = false;
    bool        CollisionOverlayEnabled            = false;
};

struct DualTierFrameTelemetry
{
    uint32_t TriangleCount          = 0;
    uint32_t BvhEnclosureCount      = 0;
    uint32_t ActiveSdfCascades      = 4;
    uint32_t SurfaceCardTexels      = 0;
    double   CardRadiosityResidual  = 0.0;
    double   MeanLuminance          = 0.0;
    double   MeanIndirectLuminance  = 0.0;
    double   ReflectionHitFraction  = 0.0;
};

//------------------------------------------------------------------------------------------------------------------------
//                                           DUAL TIER PROVING GROUNDS RENDERER
//------------------------------------------------------------------------------------------------------------------------

class DualTierProvingGroundsRenderer
{
  public:
    DualTierProvingGroundsRenderer();

    // Evaluates a linear HDR RGB image (Width * Height pixels, row-major top-to-bottom) and populates Telemetry.
    void RenderFrame(uint32_t Width, uint32_t Height, const EverestViewportInputs& Inputs,
                     std::vector<SpectrumRgb>& OutLinearRgb, DualTierFrameTelemetry& OutTelemetry) const;

    // Projects a 3D world-space point into normalized [0..1] viewport UV for overlays (Collision X-Ray, Sun flare, etc.).
    [[nodiscard]] bool ProjectWorldPoint(SpatialVector WorldPoint, const EverestViewportInputs& Inputs,
                                         double AspectRatio, double& OutU, double& OutV) const noexcept;

    [[nodiscard]] uint32_t QueryTriangleCount() const noexcept { return SceneBvh.FacetCount(); }
    [[nodiscard]] uint32_t QueryCardTexelCount() const noexcept { return SolvedCards.TexelCount(); }

  private:
    enum class SurfaceCategory : uint8_t
    {
        AsphaltApron     = 0,
        RadiosityWall    = 1,
        GantryStructure  = 2,
        VehiclePaint     = 3,
        DielectricCanopy = 4,
        RubberTyre       = 5,
        AlloyRim         = 6,
        EmissiveLamp     = 7,
        CabinInterior    = 8
    };

    struct FacetMaterialRecord
    {
        SpatialVector   Normal{};
        SpectrumRgb     Albedo{0.5, 0.5, 0.5};
        SpectrumRgb     Emissive{0.0, 0.0, 0.0};
        double          Roughness   = 0.5;
        double          Metallic    = 0.0;
        SurfaceCategory Category    = SurfaceCategory::AsphaltApron;
    };

    struct CardColourRecord
    {
        SpatialVector Centre{};
        SpatialVector Normal{};
        SpectrumRgb   Tint{1.0, 1.0, 1.0};
        double        AreaMetresSq = 1.0;
        uint32_t      FirstTexel   = 0;
        uint32_t      TexelCount   = 0;
    };

    TriangleVisibilitySpace                  SceneBvh;
    std::vector<FacetMaterialRecord>         FacetMaterials;
    DistanceFieldScene                       AnalyticSdf;
    std::array<BakedDistanceField, 4>        ToroidalCascades;
    std::array<double, 4>                    CascadeHalfExtentsMetres{{8.0, 24.0, 72.0, 180.0}};
    CardRadiosity                            SolvedCards;
    std::vector<CardColourRecord>            CardClusters;
    double                                   InitialCardResidual = 0.0;
    uint32_t                                 PackedEnclosureCount = 0;
    std::vector<StarRecord>                  NightStarCatalogue;
    LightningBolt                            CachedStormBolt;

    [[nodiscard]] SpectrumRgb EvaluateSkyAndCelestial(SpatialVector RayDir, const EverestViewportInputs& Inputs,
                                                      const SpectrumRgb& SolarIrradiance) const noexcept;
    [[nodiscard]] uint32_t    SelectCascadeIndex(SpatialVector WorldPoint, double OuterSpanScale) const noexcept;
    [[nodiscard]] double      SampleToroidalSignedDistance(SpatialVector WorldPoint, uint32_t CascadeIndex) const noexcept;
    [[nodiscard]] SpectrumRgb GatherCardMultiBounceIrradiance(SpatialVector Position, SpatialVector Normal,
                                                              const EverestViewportInputs& Inputs) const noexcept;
    [[nodiscard]] SpectrumRgb EvaluateRtxIndirectIrradiance(SpatialVector Position, SpatialVector Normal,
                                                            uint32_t PixelHash, const EverestViewportInputs& Inputs,
                                                            const SpectrumRgb& SolarIrradiance) const noexcept;
    [[nodiscard]] SpectrumRgb EvaluateGtxConeIndirectIrradiance(SpatialVector Position, SpatialVector Normal,
                                                                const EverestViewportInputs& Inputs,
                                                                const SpectrumRgb& SolarIrradiance) const noexcept;
};

} // namespace Frontier
