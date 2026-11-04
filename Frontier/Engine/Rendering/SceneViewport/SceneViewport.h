/*==========================================================================================================================================
                                                            SCENEVIEWPORT.H
==========================================================================================================================================*/
// 🧩 The live scene viewport: the ported VehicleHMI ReSTIR GI refresh of the proving ground (no vehicle), its own sky driven by the
//    level's environment records, and the PlanetaryAtmosphere environment overlaid on it, presented as one image.

#pragma once

#include "EnvironmentOverlay.h"
#include "Rendering/AnalyticIllumination/AnalyticIlluminationSequence.h"
#include "Rendering/GlobalIllumination/OccupantRelocation.h"
#include "Rendering/GlobalIllumination/PickProjection.h"

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Frontier::GlobalIllumination
{

struct SceneViewportConfiguration
{
    std::filesystem::path GlobalIlluminationShaders{};   // [-] EngineContent/ShaderArchives/GlobalIllumination
    std::filesystem::path EnvironmentShaders{};          // [-] EngineContent/ShaderArchives/Environment; empty = the demo's own sky only
    std::filesystem::path SceneArchive{};                // [-] EnvironmentArchives/ProvingGround
    std::filesystem::path CelestialMaps{};               // [-] EnvironmentArchives/CelestialMaps
    std::filesystem::path ShapeArchives{};               // [-] EngineContent/ShapeArchives; empty = no engine shapes (beyond the demo)
    uint32_t              ReserveCapacity = 512;         // [-] stress-pool occupants kept as the editor's reserve (0 = the demo's pool)
    std::string           Quality    = "medium";         // [-] the demo's grades: low | medium | high
    bool                  RayTracing = true;             // [-] RTX ReSTIR path tracer; false = GTX distance-field GI
    uint32_t              TraceScale = 2;                // [-] screen pixels per resampling pixel along one axis
    // 📝 The PlanetaryAtmosphere environment lights the scene (transmitted sun, moonlit night, its sky in every bounce and
    //    reflection, its cloud shadows on the ground, scene-shadowed fog shafts); false keeps the demo's own light inputs and
    //    only draws the environment over the image.
    bool                  EnvironmentLighting = true;
    // 🔍 Bring-up steps and device faults, one line each (the editor's diagnostic journal); unset = silent.
    std::function<void(const std::string&)> Journal{};
};

// 📝 A camera in the illumination world (z up), as an orbit or fly controller resolves it.
struct ViewportCamera
{
    Vector3 Position{-47.0, 0.0, 3.15};
    Vector3 Target{-40.0, 0.0, 1.35};
    Vector3 Up{0.0, 0.0, 1.0};
    double  VerticalFieldOfView = 1.0471975511965976;   // [rad] 60°
    double  Near                = 0.05;                 // [m]
};

// 📝 Render-pipeline record values that change the refresh without rebuilding it.
struct ViewportRendering
{
    bool     GlobalIllumination   = true;
    bool     TemporalAntiAliasing = true;
    double   ExposureCompensation = 0.0;     // [EV]
    uint32_t Inspection           = 0;       // [-] the demo's inspection view 0–14 (RefreshToggles::Inspection)
    bool     VirtualizedGeometry  = false;   // [-] the cluster nesting takeover (the demo's ?hierarchy=1)
    uint32_t VirtualizedStreamSlots = 0;     // [-] arena slots the pages stream through (0: one per page)
    bool     AnalyticIllumination = false;   // [-] the fast closed-form renderer (AnalyticIllumination) instead of the GI
    uint32_t Refinements          = 0;       // [-] RefinementBit mask of options beyond the demo (0: the demo)
    // 📝 The rest of the Render settings record. Every default is the demo's, so a caller that sets only the fields above refreshes
    //    exactly as before; the fields above overwrite Toggles' Indirect, Antialias, ExposureCompensation, Inspection and Refinements,
    //    and Toggles.RayTracing applies only on a viewport built with ray tracing (SceneViewportConfiguration::RayTracing).
    RefreshToggles   Toggles{};
    uint32_t         TraceScale     = 0;       // [-] screen pixels per resampling pixel along one axis (1, 2, 4); 0 = as constructed
    uint32_t         CardTileBudget = 0;       // [-] static surface-card tiles relit per refresh; 0 = the quality grade's
    VarianceSettings DiffuseFilter{0.08f, 24, 3, 4.0f};
    VarianceSettings GlossyFilter{0.12f, 12, 2, 3.0f};
};

// 📝 What the refresh ran with, for the viewport's readout (the demo's PresentMeasurements).
struct ViewportStatistics
{
    std::array<uint32_t, 2> ScreenExtent      = {0, 0};   // [px]
    std::array<uint32_t, 2> TraceExtent       = {0, 0};   // [px] resampling targets
    bool                    RayTracing        = true;     // [-] RTX ReSTIR path tracer, else GTX distance-field GI
    uint32_t                MaxBounces        = 0;        // [-]
    uint32_t                ReflectionBounces = 0;        // [-]
    uint32_t                InitialCandidates = 0;        // [-] M
    uint32_t                TlasInstances     = 0;        // [-] active instances of the software TLAS
    uint32_t                TlasEntries       = 0;        // [-] TLAS entries in use
    uint32_t                SceneTriangles    = 0;
    uint32_t                BlasTriangles     = 0;        // [-] static BLAS triangles
    uint32_t                ClipResolution    = 0;        // [voxels] distance clipmap edge (the grade's ClipResolution)
    uint32_t                SurfaceCards      = 0;        // [-] surface cards in the depot
    uint32_t                GeometryClusters  = 0;        // [-] cluster candidates the surface raster culls
};

class SceneViewport
{
  public:
    SceneViewport();
    ~SceneViewport();

    SceneViewport(const SceneViewport&)            = delete;
    SceneViewport& operator=(const SceneViewport&) = delete;

    // 📝 HostDevice: the editor's presentation device (submissions then share its queue and thread); null opens a device of
    //    its own (offscreen validation). Brings up the proving ground with the vehicle deactivated.
    bool Construct(const VulkanDeviceAccess* HostDevice, const SceneViewportConfiguration& Configuration, std::array<uint32_t, 2> ScreenExtent,
                   std::string* OutDiagnostic = nullptr);

    // 📝 One refresh at ScreenExtent (the screen targets follow a changed extent). The presented image is complete when this
    //    returns; the last submission ends with a full dependency, so later submissions on the queue may sample it.
    bool Refresh(const ViewportCamera& Camera, const SceneEnvironment& Environment, const ViewportRendering& Rendering, double DeltaSeconds,
                 std::array<uint32_t, 2> ScreenExtent, std::string* OutDiagnostic = nullptr);

    // 📝 UNORM view of the sRGB-encoded presented image (VK_IMAGE_LAYOUT_GENERAL): display it without further conversion.
    [[nodiscard]] VkImageView QueryPresentedView() const noexcept;
    [[nodiscard]] std::array<uint32_t, 2> QueryScreenExtent() const noexcept;
    [[nodiscard]] bool QueryEnvironmentActive() const noexcept { return static_cast<bool>(Overlay_); }
    [[nodiscard]] const std::string& QueryAdapter() const noexcept { return Adapter_; }
    [[nodiscard]] ViewportStatistics QueryStatistics() const noexcept;
    [[nodiscard]] const SceneViewportConfiguration& QueryConfiguration() const noexcept { return Configuration_; }
    bool ReadPresented(std::vector<uint8_t>& OutRgba);

    // 📝 The demo's ReconstructScreenTargets sizing: density × render scale, capped by the grade's pixel limit, at least 64 px.
    //    ViewportScale (Render settings › Viewport scale, 0.25–2) multiplies the grade's render scale under the same cap.
    [[nodiscard]] static std::array<uint32_t, 2> ResolveScreenExtent(double PanelWidth, double PanelHeight, double PixelDensity, const std::string& Quality,
                                                                     double ViewportScale = 1.0);

    // 📝 The demo's own environment the refresh encodes (its sky lights the scene; the overlaid environment is drawn over it).
    EnvironmentSettings Sky{};
    // 🔍 Last word on the refresh toggles before each refresh (validation tools switch the demo's options and the refinements).
    std::function<void(RefreshToggles&)> ToggleAdjustment{};
    // 📝 Beyond the demo: the editor's placements of the proving ground's occupants (OccupantRelocation), applied before the next
    //    refresh; returns how many occupants changed. A rebuilt viewport starts from the archive again.
    uint32_t RelocateOccupants(const std::vector<OccupantPlacement>& Placements);
    // 📝 Beyond the demo: GPU picking (PickProjection). The occupants drawn at and around a point of the last refreshed image (a
    //    fraction of its width and height from the top-left), seen through that refresh's camera, unjittered, with the occupants
    //    where that refresh drew them; false while nothing has been refreshed or when the pick program is unavailable.
    bool PickOccupants(std::array<double, 2> Point, OccupantPick& Outcome, std::string* OutDiagnostic = nullptr);
    // 📝 The same for a pixel of an image of ScreenExtent pixels seen through Camera (validation, no refresh needed).
    bool PickOccupants(const ViewportCamera& Camera, std::array<uint32_t, 2> ScreenExtent, std::array<uint32_t, 2> Pixel, OccupantPick& Outcome,
                       std::string* OutDiagnostic = nullptr);
    // 📝 The unjittered view-projection the refresh encodes for Camera at ScreenExtent (RefreshEncoder, AnalyticIlluminationSequence).
    [[nodiscard]] static Transform ResolveViewProjection(const ViewportCamera& Camera, std::array<uint32_t, 2> ScreenExtent) noexcept;
    [[nodiscard]] const SceneStructure* QueryScene() const noexcept { return Host_ ? &Host_->Scene : nullptr; }
    [[nodiscard]] const std::vector<ShapeArchive>& QueryShapes() const noexcept { return Shapes_; }
    [[nodiscard]] IlluminationHost*   AccessHost() noexcept { return Host_.get(); }
    [[nodiscard]] EnvironmentOverlay* AccessOverlay() noexcept { return Overlay_.get(); }

  private:
    void ResolveSky(const SceneEnvironment& Environment);

    SceneViewportConfiguration             Configuration_{};
    std::unique_ptr<DeviceExchange>        Exchange_{};
    std::unique_ptr<VulkanComputeExchange> Environment_{};
    std::unique_ptr<IlluminationHost>      Host_{};
    std::vector<ShapeArchive>              Shapes_{};                        // [-] the engine shapes the scene was built with
    bool                                   VirtualizedConfigured_ = false;   // [-] last levelspace value of the cluster takeover
    RefreshToggles                         LastToggles_{};                   // [-] what the last global-illumination refresh ran with
    std::unique_ptr<AnalyticIllumination::AnalyticIlluminationSequence> Analytic_{};   // [-] built on first selection
    std::unique_ptr<EnvironmentOverlay>    Overlay_{};
    std::unique_ptr<PickProjection>        Pick_{};                          // [-] built on the first pick
    std::optional<ViewportCamera>          PickCamera_{};                    // [-] the camera of the last refreshed image
    std::array<uint32_t, 2>                PickExtent_ = {0, 0};             // [px] and its extent
    std::shared_ptr<DeviceTextureView>     PresentedView_{};
    std::string                            Adapter_{};
    double                                 ElapsedSeconds_ = 0.0;
};

} // namespace Frontier::GlobalIllumination
