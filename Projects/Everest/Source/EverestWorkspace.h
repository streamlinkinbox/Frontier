/*==========================================================================================================================================
                                                         EVERESTWORKSPACE.H
==========================================================================================================================================*/
// 🧩 Project Everest workspace: wraps EverestApplication runtime + #if FRONTIER_EDITOR docked authoring interface.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "EverestApplication.h"
#include "EverestSimulationRecords.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)
struct ImDrawList;
struct ImVec2;
#include "Engine/Interface/ConstructPanel.h"
#include "Engine/Interface/EnvironmentEditAccess.h"
#include "Engine/Interface/Controls/PlacementGizmo.h"
#include "Engine/Interface/LogStreamPanel.h"
#include "Engine/Interface/PanelDockspace.h"
#include "Engine/Interface/SceneDirectoryPanel.h"
#include "Engine/Interface/SceneInspectorPanel.h"
#include "UI/Editor/ControlCentrePanel.h"
namespace Frontier
{
class ForceFieldVisualization;
}
#endif

#include "Engine/Scene/EngineConfigurations.h"
#include "Engine/Scene/EnvironmentDiagnostics.h"
#include "Engine/Scene/GeometryLayout.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace Everest
{

// 📝 What the Scene Viewport asks the host application to render and present each interface tick; the host owns the device
//    and the renderer, the workspace owns the camera (orbit, pan, dolly, fly) and the level records the host reads.
struct SceneViewportRequest
{
    float                 PanelWidth          = 0.0f;                 // [px] interface pixels
    float                 PanelHeight         = 0.0f;                 // [px]
    float                 PixelDensity        = 1.0f;                 // [-] framebuffer pixels per interface pixel
    std::array<double, 3> CameraPosition      = {0.0, 0.0, 0.0};      // [m] z-up world of the scene
    std::array<double, 3> CameraTarget        = {1.0, 0.0, 0.0};      // [m]
    double                VerticalFieldOfView = 1.0471975511965976;   // [rad]
    double                DeltaSeconds        = 0.0;                  // [s]
    std::array<double,3> CameraUp = {0,0,1};
    // 📝 GPU picking: a click on the stage asks for the record drawn under it in the image the click was made on; the readout's
    //    Pick answers the serial one interface tick later.
    uint64_t              PickSerial          = 0;                    // [-] advances with every click; 0 = nothing asked
    std::array<double, 2> PickPoint           = {0.0, 0.0};           // [-] fraction of the stage's width and height, from the top-left
};

// 📝 The answer to the stage's last click (GPU picking, PickProjection): the Geometry record drawn at the clicked pixel, or the
//    nearest one drawn within the pick's reach around it.
struct SceneViewportPick
{
    uint64_t                Serial       = 0;              // [-] the request's PickSerial this answers; 0 = none yet
    std::optional<uint64_t> Record{};                      // [-] record token value; empty: the sky or nothing a record owns
    uint32_t                Occupant     = 0xFFFFFFFFu;    // [-] scene occupant at the clicked pixel (0xFFFFFFFF: none)
    double                  Milliseconds = 0.0;            // [ms] the pick's round trip on the device
    std::string             Failure{};                     // [-] why nothing could be picked
};

// 📝 What the host's renderer reports for the viewport bar, readout and status strip (the demo's PresentMeasurements).
struct SceneViewportReadout
{
    std::shared_ptr<const Frontier::EnvironmentRenderReadout> Environment;
    bool                    Presenting          = false;     // [-] an image is live
    std::string             Adapter{};                       // [-] GPU name
    std::string             Quality{};                       // [-] low / medium / high
    std::string             RendererTag{};                   // [-] "RTX ReSTIR PT (3B GI · 2B Refl · M=2)" or "GTX SDF GI"
    std::string             AccelerationLine{};              // [-] "BVH hierarchy   TLAS …" (RTX) or empty
    std::array<uint32_t, 2> ScreenExtent        = {0, 0};    // [px]
    std::array<uint32_t, 2> TraceExtent         = {0, 0};    // [px]
    double                  RefreshMilliseconds = 0.0;       // [ms] smoothed refresh duration
    double                  LatestMilliseconds  = 0.0;       // [ms] the last refresh alone
    bool                    Held                = false;     // [-] live refresh is off: the image is the last one, not refreshed
    std::string             Failure{};                       // [-] why there is no image, if it failed
    bool                    RayTracing          = false;     // [-] RTX ReSTIR path tracer, else GTX distance-field GI
    uint32_t                TlasInstances       = 0;         // [-] active software TLAS instances
    uint32_t                ClipResolution      = 0;         // [voxels] distance clipmap edge
    uint32_t                SurfaceCards        = 0;         // [-] surface cards
    uint32_t                SceneTriangles      = 0;
    uint32_t                GeometryClusters    = 0;         // [-] geometry cluster candidates
    // 📝 What each Geometry record resolved to against the loaded scene archive (record token value → resolution) and a
    //    counter that advances whenever any of them changed; the workspace copies them into the records for their inspector.
    std::shared_ptr<const std::unordered_map<uint64_t, Frontier::GeometryConfiguration::Resolution>> Geometry{};
    uint64_t                GeometryGeneration  = 0;
    SceneViewportPick       Pick{};                          // [-] the answer to the last click on the stage
};

struct SceneViewportImage
{
    uint64_t             Texture = 0;   // [-] ImTextureID of the presented image; 0 while unavailable
    std::string          Status{};      // [-] renderer, extent and refresh time, or the bring-up / failure line
    SceneViewportReadout Readout{};     // [-]
};

// 📝 The demo's overlay switches (OverlayDock.js): readout (H), distance slice (I) and the clean view (V), which hides both.
struct SceneViewportOverlays
{
    bool ReadoutVisible       = false;   // [-] HudVisible, off by default as in the demo
    bool Fullscreen = false;
};

// 📝 Free camera of the viewport, navigated as Unreal Engine's perspective viewport (z up, metres): a position, the view's
//    heading and elevation, the flight velocity W/A/S/D/Q/E/R/F accelerate, Unreal's eight camera speed settings, the Z/C
//    field-of-view zoom that recoils once the right button is released, and the pivot that Alt orbits and dollies around.
//    The default is the former orbit view: focus (−10, 6, 3), 65.88 m away at yaw −142.43°, pitch 5.23°.
struct SceneViewportFlight
{
    std::array<double, 3> Position            = {-61.9997, -34.0018, 9.0052};   // [m] z-up world of the scene
    double                Heading             = 37.57;                          // [°] view direction, counter-clockwise from +X
    double                Elevation           = -5.23;                          // [°] view direction above the horizon
    double                PivotDistance       = 65.88;                          // [m] Alt orbit and dolly pivot ahead of the view
    double                VerticalFieldOfView = 1.0471975511965976;             // [rad] at rest
    double                FieldOfViewOffset   = 0.0;                            // [°] Z/C zoom while flying
    double                FieldOfViewRate     = 0.0;                            // [°/s]
    std::array<double, 3> Velocity            = {0.0, 0.0, 0.0};                // [m/s] flight velocity
    int                   SpeedSetting        = 4;                              // [1-8] Unreal camera speed setting
    double                WheelRemainder      = 0.0;                            // [-] partial wheel notches (touchpads)

    [[nodiscard]] std::array<double, 3> ResolveForward() const noexcept;
    [[nodiscard]] std::array<double, 3> ResolveLateral() const noexcept;
    [[nodiscard]] std::array<double, 3> ResolveTarget() const noexcept;
    [[nodiscard]] double                ResolveFieldOfView() const noexcept;
    // 📝 Unreal's GetCameraSpeed table for settings 1–8 (0.03125 … 32; 1 at the default setting 4).
    [[nodiscard]] static double         ResolveSpeed(int Setting) noexcept;
};

// 📝 One interface tick of pointer and keyboard input over the viewport, as the camera reads it (ImGui conventions: y down).
struct SceneViewportPointer
{
    bool                  Hovered       = false;        // [-] the pointer is over the viewport image
    bool                  Active        = false;        // [-] a drag that began on the viewport is still held
    bool                  LeftDown      = false;        // [-]
    bool                  RightDown     = false;        // [-]
    bool                  MiddleDown    = false;        // [-]
    bool                  AltDown       = false;        // [-]
    bool                  ControlDown   = false;
    bool                  ShiftDown     = false;        // [-]
    std::array<double, 2> MotionPixels  = {0.0, 0.0};   // [px] pointer motion, +x right, +y down
    double                WheelNotches  = 0.0;          // [-] +away from the user
    std::array<bool, 10>  FlightKeys    = {};           // [-] forward, back, left, right, up, down, view up, view down, widen, narrow
    bool                  RecentrePress = false;        // [-] F pressed (without the right button)
};

// 📝 Advances the viewport camera by one tick of input: Unreal Engine's perspective viewport navigation.
void IntegrateSceneViewportFlight(SceneViewportFlight& Flight, const SceneViewportPointer& Pointer, double DeltaSeconds);

enum class SceneCameraMode : uint32_t { Orbit, Chase, Free, Top, Bottom, Front, Back, Left, Right, Space };
inline constexpr std::array<const char*,10> SceneCameraNames = {"Orbit selected","Chase selected","Free / WASD","Top","Bottom","Front","Back","Left","Right","Space navigation"};
struct SceneCameraSubject
{
    bool Available = false;
    bool Celestial = false;
    std::array<double,3> Position{};
    double Radius = 5;
    double Azimuth = 0, Elevation = 0, Diameter = 1;
};
struct SceneViewportNavigation
{
    SceneCameraMode Mode = SceneCameraMode::Free;
    std::array<double,3> Anchor{};
    std::array<double,3> ChaseOffset{};
    std::array<double,3> Up = {0,0,1};
    SceneViewportFlight ScenePose;
    bool ScenePoseReady = false;
    uint64_t Selected = 0;
};
void ConfigureSceneCamera(SceneViewportFlight& Flight, SceneViewportNavigation& Navigation,
                          SceneCameraMode Mode, const SceneCameraSubject& Subject, bool FrameSelection = false);
void IntegrateSceneCamera(SceneViewportFlight& Flight, SceneViewportNavigation& Navigation,
                          const SceneViewportPointer& Pointer, const SceneCameraSubject& Subject, double DeltaSeconds);

class EverestWorkspace
{
  public:
#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)
    explicit EverestWorkspace(Frontier::ControlCentrePanel& ControlCentre,
                              std::filesystem::path         ProjectRootPath = "Projects/Everest");
#else
    explicit EverestWorkspace(std::filesystem::path ProjectRootPath = "Projects/Everest");
#endif
    ~EverestWorkspace();

    EverestWorkspace(const EverestWorkspace&)            = delete;
    EverestWorkspace& operator=(const EverestWorkspace&) = delete;

    void IntegrateSimulation(double DeltaSeconds);

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)
    bool ConfigurePlatform() noexcept;
    void ConstructInterface(double DeltaSeconds);

    std::function<void()> EvaluateReturnToBrowser;
    // 📝 Set by the host application when it can render the scene; the viewport falls back to its schematic without it.
    std::function<SceneViewportImage(const SceneViewportRequest&)> PresentSceneViewport;
    // 📝 Set by the host application: hides the pointer and locks it to the platform window (an ImGuiViewport's
    //    PlatformHandle) while a camera drag lasts, so the view turns without the pointer meeting the screen edge; the
    //    pointer returns where the drag began. Without it the pointer stays visible and free.
    std::function<void(void* PlatformWindow, bool ConstraintEnabled)> ConstrainPointer;
    [[nodiscard]] SceneViewportNavigation& AccessViewportNavigation() noexcept { return Navigation; }
    [[nodiscard]] SceneViewportFlight&      AccessViewportFlight() noexcept { return Flight; }
    [[nodiscard]] SceneViewportOverlays&    AccessViewportOverlays() noexcept { return Overlays; }

    // 📝 The Force Field's view in the viewport: Shown swaps the scene for the slice (100 m to 100 km); Overlay is the slice
    //    over the rendered scene; Hovering and HoverPoint are the pointer's last reading; Scrubbing holds the timeline.
    struct FieldViewport
    {
        bool                                               Shown = false;
        std::unique_ptr<Frontier::ForceFieldVisualization> Slice;
        std::unique_ptr<Frontier::ForceFieldVisualization> Overlay;
        bool                                               Hovering = false;
        std::array<double, 2>                              HoverPoint{};
        bool                                               Scrubbing = false;
        Frontier::ForceFieldReadings                       Readings{};          // [-] what the Force Field page's hero reads
        double                                             ReadingSeconds = 0.0; // [s] since the last peak joined the history
        int                                                LoggedContribution = -1; // [-] the contribution the Log Stream last named
    };
    [[nodiscard]] FieldViewport& AccessFieldViewport() noexcept { return Field; }

    [[nodiscard]] Frontier::SceneDirectoryPanel& QueryOutliner() noexcept { return OutlinerPanel; }
    [[nodiscard]] Frontier::SceneInspectorPanel& QueryInspector() noexcept { return InspectorPanel; }
    [[nodiscard]] Frontier::LogStreamPanel&      QueryLogStream() noexcept { return LogPanel; }
    [[nodiscard]] Frontier::PanelDockspace&      QueryDockspace() noexcept { return Dockspace; }
    [[nodiscard]] Frontier::ConstructPanel&      QueryConstructDialog() noexcept { return ConstructDialog; }

    // 📝 Adds what the Construct dialog ordered: an engine shape as a Geometry record on the ground where the view looks, or a
    //    record type; and duplicates a scene object (its copies from the reserve). The dialog and the keys call these too.
    void                  ServiceConstructOrder(const Frontier::ConstructOrder& Order);
    Frontier::RecordToken DuplicateGeometryRecord(Frontier::RecordToken Token);
#endif

    [[nodiscard]] EverestApplication&          AccessApplication() noexcept { return Application; }
    [[nodiscard]] const EverestApplication&    QueryApplication() const noexcept { return Application; }
    [[nodiscard]] Frontier::SceneWorld&        QuerySceneWorld() noexcept { return Application.AccessWorld(); }
    [[nodiscard]] const Frontier::SceneWorld&  QuerySceneWorld() const noexcept { return Application.QueryWorld(); }
    [[nodiscard]] const std::filesystem::path& QueryProjectRoot() const noexcept { return ProjectRoot; }
    [[nodiscard]] const std::filesystem::path& QueryActiveLevelPath() const noexcept { return ActiveLevelPath; }
    [[nodiscard]] const std::string&           QueryDiagnostic() const noexcept { return Diagnostic; }
    [[nodiscard]] double                       QueryElapsedSeconds() const noexcept { return ElapsedSeconds; }

  private:
    void InitializeWorld();

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)
    void ServiceDynamicSceneRequests();
    void SaveActiveLevel();
    // 📝 Scene objects (the Project Zero editor's tools): the Construct dialog's catalogue and orders, duplicates, renames, the
    //    keys, picking in the viewport and the transform handles.
    void ConfigureConstructCatalogue();
    void ServiceSceneObjectKeys();
    void ApplyPendingSurfaces();
    [[nodiscard]] std::string ResolveUniqueName(const std::string& Stem) const;
    [[nodiscard]] std::array<double, 3> ResolveGroundTarget() const;
    [[nodiscard]] Frontier::GeometryConfiguration* ResolveEditedGeometry();
    [[nodiscard]] Frontier::PlacementView ResolvePlacementView(const ImVec2& StageMin, const ImVec2& StageExtent) const;
    void ConstructViewportSurface();
    void ConstructViewportSchematic(ImDrawList* DrawList, const ImVec2& Min, const ImVec2& Max);
    void IntegrateViewportCamera(bool Hovered, bool Active, double DeltaSeconds, bool LeftClaimed = false);
    void ReleasePointerConstraint();
    void ConstructViewportBar(const ImVec2& Min, const ImVec2& Max, float Scale);
    void ConstructViewportOverlays(const ImVec2& StageMin, const ImVec2& StageMax, const SceneViewportImage& Image, float Scale);
    void ConstructViewportFooter(float Scale);
    SceneCameraSubject ResolveCameraSubject() const;
    void ConstructFullscreenViewport(float Scale);
    void ToggleViewportOverlay(int Overlay);
    void AnnounceViewportNotice(std::string Text);
    // 📝 The Force Field in the viewport (EverestFieldViewport.cpp): the timeline clock, one slice from the level's records,
    //    the Field view with its timeline lanes, its bar, and the slice over the scene.
    void AdvanceFieldTimeline(double DeltaSeconds);
    bool ConfigureFieldSlice(Frontier::ForceFieldVisualization& Slice, bool Camera);
    void ConstructFieldViewport(const ImVec2& StageMin, const ImVec2& StageMax, float Scale);
    void ConstructFieldTimeline(const ImVec2& Min, const ImVec2& Max, float Scale);
    void ConstructFieldBar(const ImVec2& Min, const ImVec2& Max, float Scale);
    void ConstructFieldOverlay(const ImVec2& StageMin, const ImVec2& StageMax, float Scale);
    // 📝 The Force Field page's list of forces and the Field view's readings, and the page's workspace actions.
    void PublishFieldReadings(Frontier::EnvironmentEditAccess& Access, double DeltaSeconds);
    void ServiceFieldActions();
    Frontier::RecordToken ConstructForceRecord(const std::string& Identifier, const std::string& Name, bool Visible);

    Frontier::ControlCentrePanel& Centre;
    std::shared_ptr<bool>         Liveness = std::make_shared<bool>(true);   // the Control Centre actions' weak token
    Frontier::PanelDockspace      Dockspace;
    Frontier::SceneDirectoryPanel OutlinerPanel;
    Frontier::SceneInspectorPanel InspectorPanel;
    Frontier::LogStreamPanel      LogPanel;
    uint64_t                      ObservedLayoutRevision = 0;
    SceneViewportFlight           Flight{};
    SceneViewportNavigation Navigation{};
    double                        ViewportDeltaSeconds   = 0.0;
    void*                         ConstrainedWindow      = nullptr;   // [-] platform window holding the pointer, or null
    int                           ConstraintSettleTicks  = 0;         // [-] ticks whose motion is dropped after a lock
    SceneViewportOverlays         Overlays{};
    SceneViewportReadout          LastReadout{};
    uint64_t                      PublishedGeometryGeneration = 0;   // [-] the geometry resolutions last copied into the records
    std::array<float, 240>        FrameDurations{};                   // [ms] the last 240 interface ticks (1 % low)
    size_t                        FrameDurationCursor    = 0;
    double                        FrameSeconds           = 0.0;       // [s] smoothed tick (0.9 / 0.1)
    std::string                   NoticeText{};                       // [-] #overlay-notice
    double                        NoticeSeconds          = 0.0;       // [s] left on screen
    int                           AnnouncedSpeedSetting  = 4;         // [-]
    Frontier::ConstructPanel      ConstructDialog{};
    Frontier::PlacementGizmo      Gizmo{};
    Frontier::PlacementGizmoMode  GizmoMode              = Frontier::PlacementGizmoMode::Move;
    ImVec2                        PickPress{};                        // [px] where a left press on the stage began
    bool                          PickPending            = false;     // [-] that press may still be a click (a pick)
    uint64_t                      PickSerial             = 0;         // [-] clicks sent to the viewport for GPU picking
    uint64_t                      PickApplied            = 0;         // [-] the last of them whose answer was applied
    std::array<double, 2>         PickPoint{};                        // [-] the last click, as a fraction of the stage
    bool                          GizmoPress             = false;     // [-] that press began on a handle
    std::unordered_map<uint64_t, std::array<float, 3>> PendingAlbedo{};   // [-] constructed shapes waiting for their family
    FieldViewport                 Field{};
#endif

    std::filesystem::path ProjectRoot;
    std::filesystem::path ActiveLevelPath;
    EverestApplication    Application;
    std::string           Diagnostic;
    double                ElapsedSeconds = 0.0;
};

} // namespace Everest
