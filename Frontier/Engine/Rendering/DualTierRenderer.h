/*==========================================================================================================================================
                                                        DUAL TIER RENDERER
                                     ------------------------------------------------------------------
             Dual-tier Proving Grounds renderer: RTX hardware RayQuery + Two-Level SAH BVH + ReSTIR GI + DielectricBoundary + SVGF,
               paired with GTX 4-cascade toroidal 3D SDF + SurfaceCards + OrderedSoftwareBvh triangle reflections.
==========================================================================================================================================*/

// 🧩 Dual-tier 1:1 Proving Grounds & ControlVehicle renderer executing both RTX RayQuery + ReSTIR GI and GTX SDF + BVH fallback.

#pragma once

#include "AtmosphereModel.h"
#include "CelestialModel.h"
#include "CloudField.h"
#include "DistanceField.h"
#include "FogMedium.h"
#include "ProvingGroundsCourse.h"
#include "SurfaceCards.h"
#include "TriangleVisibilitySpace.h"
#include "WeatherEffects.h"

#include <array>
#include <cstdint>
#include <filesystem>
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

// Matches InspectionModes in VehicleHMI InspectorPages.js (0..9 + DielectricGlazing).
enum class ViewportInspectionMode : uint8_t
{
    FinalLit            = 0,   // [-] - Full ACES filmic tone-mapped linear HDR composite
    IndirectIrradiance  = 1,   // [-] - ReSTIR GI (RTX) vs 4-cascade SDF + SurfaceCards (GTX)
    SpecularReflections = 2,   // [-] - Multi-bounce BVH reflections (RTX) vs OrderedSoftwareBvh reflections (GTX)
    DistanceCascades    = 3,   // [-] - Toroidal 3D SDF cascades SceneClip0..3 + traversal isolines
    DielectricGlazing   = 4,   // [-] - Exact Fresnel + Beer-Lambert ControlVehicle glass transmittance
    SurfaceNormals      = 5,   // [-] - World-space XYZ surface normals
    DirectSunOnly       = 6,   // [-] - Direct solar + clustered 3D lamp radiance
    SurfaceAlbedo       = 7,   // [-] - Demodulated base reflectance
    SurfaceCardsView    = 8    // [-] - Surface-card radiosity atlas projection
};

struct DualTierTierDecision
{
    DualTierExecutionMode Mode               = DualTierExecutionMode::ToroidalSdfBvhGtx;
    bool                  HardwareRtxCapable = false;
    const char*           DiagnosticSummary  = "";
};

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
    EverestCameraMode      CameraMode              = EverestCameraMode::Chase;

    // ControlVehicle world pose (SpawnPoint [-40, 0, 0.53], +X forward, +Y left, +Z up)
    std::array<float, 3> VehiclePosition           = { -40.0f, 0.0f, 0.53f };
    float                VehicleYawRadians         = 0.0f;
    float                VehiclePitchRadians       = 0.0f;
    float                VehicleRollRadians        = 0.0f;
    float                FrontSteerRadians         = 0.0f;
    float                WheelSpinRadians          = 0.0f;
    bool                 UsePerCornerWheelPose     = false;
    std::array<float, 4> WheelSteerPerCorner       = { 0.0f, 0.0f, 0.0f, 0.0f };
    std::array<float, 4> WheelSpinPerCorner        = { 0.0f, 0.0f, 0.0f, 0.0f };
    std::array<float, 4> WheelVerticalOffsetM      = { 0.0f, 0.0f, 0.0f, 0.0f };
    double               VehicleEngineRpm          = 700.0;
    int32_t              VehicleGearNumber         = 0;

    // Camera orbit / chase offsets (degrees/metres kept compatible with existing callers)
    double CameraYawDegrees                        = 22.0;     // [deg] - Slight 3/4 offset so side silhouette & course read together
    double CameraPitchDegrees                      = 12.0;     // [deg]
    double CameraDistanceMetres                    = 7.0;      // [m]

    // Solar & sky coupling (IlluminationHost.js defaults: Elevation=38 deg, Azimuth=145 deg, Illuminance=12)
    SpatialVector        SunDirection{ -0.646, 0.452, 0.616 };
    double               SunElevationDegrees       = 38.0;     // [deg]
    double               SunAzimuthDegrees         = 145.0;    // [deg]
    double               SunTemperatureKelvin      = 5778.0;   // [K]
    double               SolarIlluminance          = 12.0;     // [x]
    bool                 SunEnabled                = true;
    bool                 SunDiscEnabled            = true;
    AtmosphereParameters Atmosphere{};
    bool                 AtmosphereEnabled         = true;
    double               SkyBrightnessScale        = 1.0;

    // Volumetric clouds & height/aerial fog
    bool            CloudsEnabled                  = true;
    double          CloudCoverage                  = 0.48;
    FogParameters   Fog{};
    bool            FogEnabled                     = false;
    double          AerialStartMetres              = 28.0;     // [m]
    double          AerialDensity                  = 0.0025;   // [1/m]

    // Night sky (Stars & Moon)
    SpatialVector MoonDirection{ -0.42, 0.58, 0.70 };
    double        MoonAngularDiameterDegrees       = 0.52;     // [deg]
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

    // Dual-tier GI / reflection budgets & ControlVehicle appearance
    double      BvhReflectionRoughnessCutoff       = 0.80;     // [-]
    double      ReflectionScale                    = 1.0;      // [x]
    double      IndirectScale                      = 1.0;      // [x]
    double      SdfOuterSpanMetres                 = 160.0;    // [m]
    double      ExposureBiasStops                  = 0.0;      // [EV]
    bool                      DielectricGlazingEnabled     = true;
    bool                      SurfaceCardsEnabled          = true;
    bool                      ClusteredLightsEnabled       = true;
    bool                      LensFlareEnabled             = true;
    double                    LensFlareIntensity           = 0.30;
    bool                      DistanceSliceInsetEnabled    = false;
    float       DistanceSliceHeightMetres          = 0.60f;    // [m]

    // Default ControlVehicle BodyCoat: linear Rec.709 [0.680005*0.78, 0.800458*0.78, 0.0] (MaterialSpecification.js)
    SpectrumRgb VehicleBodyAlbedo{ 0.530404, 0.624357, 0.0 };
    double      VehicleBodyRoughness               = 0.0775;   // [-]
    double      VehicleSpeedKmh                    = 0.0;      // [km/h] - Live OLED cluster speed & gear telemetry
    uint8_t     AutomotiveBeamMode                 = 1u;       // 0=Off, 1=LowBeam, 2=HighBeam, 3=Fog
    bool        HeadlampsEnabled                   = true;
    bool        BrakeLampsEnabled                  = true;
    bool        HazardLampsEnabled                 = false;
    bool        RoofLightEnabled                   = false;
    bool        RoofRallyPodMode                   = false;
    bool        BreakablesEnabled                  = true;
    bool        CollisionOverlayEnabled            = false;
    bool        TyreSmokeActive                    = false;
};

struct DualTierFrameTelemetry
{
    uint32_t TriangleCount          = 0u;
    uint32_t BvhEnclosureCount      = 0u;
    uint32_t ActiveTlasInstances    = 0u;
    uint32_t ActiveClusterLights    = 0u;
    uint32_t ActiveSdfCascades      = 4u;
    uint32_t SurfaceCardTexels      = 0u;
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
    explicit DualTierProvingGroundsRenderer(const std::filesystem::path& VehicleArchiveDirectory = {});

    // Evaluates a linear HDR RGB image (Width * Height pixels, row-major top-to-bottom) and populates Telemetry.
    void RenderFrame(uint32_t Width, uint32_t Height, const EverestViewportInputs& Inputs,
                     std::vector<SpectrumRgb>& OutLinearRgb, DualTierFrameTelemetry& OutTelemetry) const;

    // Evaluates a 1:1 native sRGB RGBA8 viewport image ready for direct upload into ImTextureData / Vulkan swapchain.
    void RenderRgba8Viewport(uint32_t Width, uint32_t Height, const EverestViewportInputs& Inputs,
                             uint8_t* OutRgbaPixels, DualTierFrameTelemetry& OutTelemetry) const;

    // Projects a 3D world-space point into normalized [0..1] viewport UV for gizmos & collision overlays.
    [[nodiscard]] bool ProjectWorldPoint(SpatialVector WorldPoint, const EverestViewportInputs& Inputs,
                                         double AspectRatio, double& OutU, double& OutV) const noexcept;

    [[nodiscard]] bool     QueryLoadedVehicleArchives() const noexcept { return VehicleGeometry.LoadedFromArchives; }
    [[nodiscard]] uint32_t QueryAuthoredBodyFacetCount() const noexcept { return VehicleGeometry.AuthoredBodyFacets; }
    [[nodiscard]] uint32_t QueryTriangleCount() const noexcept { return TotalAuthoredTriangles; }
    [[nodiscard]] uint32_t QueryCardTexelCount() const noexcept { return SolvedCards.TexelCount(); }

    [[nodiscard]] const ControlVehicleAssembly& QueryVehicleAssembly() const noexcept { return VehicleGeometry; }
    [[nodiscard]] const ProvingGroundsAssembly& QueryCourseAssembly() const noexcept { return CourseGeometry; }

  private:
    struct MeshBlasNode
    {
        std::array<float, 3> Minimum = { 0.0f, 0.0f, 0.0f };
        uint32_t             First   = 0u;
        std::array<float, 3> Maximum = { 0.0f, 0.0f, 0.0f };
        uint32_t             Count   = 0u;   // >0 leaf, 0 internal (First = left child, First + 1 = sibling or Upper)
        uint32_t             Upper   = 0u;
    };

    struct MeshBlas
    {
        std::vector<CourseTriangle> Triangles;
        std::vector<uint32_t>       Order;
        std::vector<MeshBlasNode>   Nodes;

        void Build(std::vector<CourseTriangle> SourceTriangles);
    };

    ControlVehicleAssembly VehicleGeometry{};
    ProvingGroundsAssembly CourseGeometry{};
    MeshBlas               StaticWorldBlas{};
    MeshBlas               BreakablesBlas{};
    MeshBlas               VehicleBodyBlas{};
    MeshBlas               VehicleGlazingBlas{};
    MeshBlas               RoofSlimLedBlas{};
    MeshBlas               RoofRallyPodBlas{};
    MeshBlas               WheelLeftBlas{};
    MeshBlas               WheelRightBlas{};

    uint32_t               TotalAuthoredTriangles = 0u;
    uint32_t               TotalBvhNodes          = 0u;
    TransmittanceLut       BakedSkyLut{};
    CardRadiosity          SolvedCards{};
    double                 SolvedCardResidual     = 0.0;

    [[nodiscard]] double      SampleProvingGroundsSignedDistance(SpatialVector WorldPoint,
                                                                 const EverestViewportInputs& Inputs) const noexcept;
    [[nodiscard]] SpectrumRgb EvaluateSkyAndCelestial(SpatialVector RayDir, const EverestViewportInputs& Inputs,
                                                      bool IncludeSolarDisc) const noexcept;
};

} // namespace Frontier
