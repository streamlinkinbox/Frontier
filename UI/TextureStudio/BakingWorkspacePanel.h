/*============================================================================================================================================
                                                          BAKINGWORKSPACEPANEL.H
============================================================================================================================================*/
// 🧩 Native 1:1 port of Experimental/TextureStudio/src/BakingStudio.jsx's "Mesh maps" workflow: built-in study
//    meshes (Teapot/Cube/Sphere), the 27-tile mesh-map grid, a CPU orbit-camera 3D preview, a 2D map viewer and
//    the resolution/projection inspector. "Material patch" and "Volume SDF" modes are present as the same
//    mode-switch buttons the web app has, but their content panels are not yet native — see ConstructInspector.

#pragma once
#include "Engine/ContentInterchange/BakeField.h"
#include "Engine/ContentInterchange/BakeMesh.h"
#include "Engine/ContentInterchange/VolumePreview.h"
#include "Engine/Interface/PanelDockspace.h"
#include "Engine/Interface/PixelSpace.h"
#include "Engine/Interface/VectorAuthoringControls.h"
#include "Engine/Lifecycle/ActivationSequence.h"
#include "Engine/Render/MaterialPreviewConfiguration.h"
#include "Engine/Render/WorkspaceViewportRendering.h"
#include <atomic>
#include <filesystem>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Frontier
{
class BakingWorkspacePanel final : public ActivationSequence
{
  public:
    BakingWorkspacePanel();
    void ConstructInterface();
    void ConfigureInterfaceScale(float Scale) noexcept { Scale = Scale > 0.1f ? Scale : 1.f; InterfaceScale = Scale; Controls.Scale = Scale; }
    [[nodiscard]] float QueryInterfaceScale() const noexcept { return InterfaceScale; }
    // 📝 Same contract as Pattern/Stamp's ConfigureTypeface: the app hands this the shared "DM Sans" regular/light
    // handles once at startup so the "Baking" pane heading and card titles read with the same type family as
    // every other native workspace instead of ImGui's bundled default font.
    void ConfigureTypeface(void *Regular, void *Light = nullptr) noexcept { RegularTypeface = Regular; LightTypeface = Light ? Light : Regular; }

    // 📝 Cross-workspace navigation, wired exactly like Stamp/Pattern's ActivateMaterial/ActivatePattern/ActivateStamp.
    std::function<std::filesystem::path(bool)> ChooseBakeMesh;
    void ImportBakeMesh(const std::filesystem::path &Path, bool LowTarget);
    bool ImportDroppedBakeMesh(const std::filesystem::path &Path, float X, float Y);
    std::function<void()> ActivateMaterial;
    std::function<void()> ActivatePattern;
    std::function<void()> ActivateStamp;
    std::function<void()> ActivateTexture;

    // 📝 GPU bake hooks bound by the host application, exactly like Pattern/Stamp's QueryCoverage: this panel
    // never touches Vulkan itself. QueryGpuBakeDevice returns the compute device's name ("" = unavailable, the
    // inspector's device choice stays CPU), SubmitGpuBake hands over one flattened BakeFieldRequest, and
    // PollGpuBake reports band progress until BakeGpuOutcome::Complete. Any GPU refusal falls back to the CPU
    // route for that bake (see PollBake), never to a silently skipped bake.
    std::function<std::string()> QueryGpuBakeDevice;
    std::function<bool(std::shared_ptr<const BakeFieldRequest>)> SubmitGpuBake;
    std::function<BakeGpuOutcome()> PollGpuBake;

    // Unified 3D PBR viewport hooks shared with Material and Texture Paint via MaterialLibraryRendering.
    std::function<void(const SurfaceGeometry &)> ConfigureViewportGeometry;
    std::function<void(const CompiledSurfaceMaterial &)> ConfigureViewportMaterial;
    std::function<void(const WorkspaceViewportOrbitCamera &)> ConfigureViewportCamera;
    std::function<void(uint32_t, uint32_t)> ConfigureViewportExtent;
    std::function<WorkspaceViewportImage()> QueryViewport;

    // 📝 Where ConstructInterface last wrote exported PNG/manifest files (QueryExportDirectory to configure it);
    // empty until a bake has been exported at least once this session.
    [[nodiscard]] const std::string &QueryLastExportDirectory() const noexcept { return LastExportDirectory; }
    void ConfigureExportDirectory(std::filesystem::path Directory) { ExportDirectory = std::move(Directory); }

    // 📝 Screen-space rect of a tracked interactive control, last frame it was drawn — mirrors Pattern/Stamp's
    // QueryControlExtent convention so headless evidence/validation can drive real clicks instead of poking
    // private state. Only the handful of controls automated evidence actually needs are tracked (see .cpp).
    [[nodiscard]] PlaneExtent QueryControlExtent(const std::string &Caption) const;
    // 📝 Read-only access to the live recipe (headless evidence/validation needs this to assert, e.g., that
    // the "All" preset really did select every implemented mesh map, not just open a dialog that looked right).
    [[nodiscard]] const BakeSettings &QuerySettings() const noexcept { return Settings; }
    [[nodiscard]] const VolumeDistanceField &QueryVolume() const noexcept { return Volume; }
    [[nodiscard]] size_t QuerySelectedSourceCount() const noexcept { return SelectedSources.size(); }

    // 📝 Same host contract Pattern/StampWorkspacePanel expose (TextureStudioApplication.cpp calls these on
    // every registered workspace's Panels instance): layout persistence, detached-window platform windows and
    // recovery all go through this one shared dockspace instead of Baking inventing its own.
    [[nodiscard]] PanelDockspace &QueryPanels() noexcept { return Panels; }
    void ConfigurePlatform() { (void)Panels.ConfigurePlatform(); }

  private:
    friend struct BakingWorkspaceValidation;
    bool InitializeContent() noexcept override;
    void FinalizeContent() noexcept override;
    void IntegrateContent(double DeltaSeconds) noexcept override;

    //--------------------------------------------------------------------------------------------------------------
    //                                                 DOCUMENT STATE
    //--------------------------------------------------------------------------------------------------------------
    enum class StudioMode { Mesh, Material, Volume };
    StudioMode Mode = StudioMode::Mesh;
    bool OutlinerTab = false;
    bool SingleMapBake = false;
    bool CustomResolution = false;
    std::array<uint32_t, 3> VolumeDimensions{48, 48, 48};
    float VolumePadding = .14f;
    float VolumeBand = 0.f;
    bool VolumeSigned = true;
    int VolumeAxis = 2;
    float VolumeSlice = .5f;
    int VolumePreviewStyle = 0, VolumePalette = 0;
    VolumePreviewOptions CachedVolumePreviewOptions;
    std::vector<uint8_t> CachedVolumePreview;
    VolumeDistanceField Volume;
    std::future<VolumeDistanceField> VolumeFuture;
    bool VolumeRunning = false;
    bool VolumeQueued = false;
    bool BatchMapsAfterVolume = false;
    void StartVolumeBake();
    void PollVolumeBake();
    void ConstructVolumePreview(PlaneExtent Region, bool Spatial);
    enum class ViewMode { Preview3D, Maps2D };
    ViewMode View = ViewMode::Preview3D;

    std::string DemoShape = "Teapot";
    DemoBakeAssets Assets;
    BakeSettings Settings;
    std::vector<std::string> SelectedTargets; // low part names included in the next bake
    std::vector<std::string> SelectedSources; // high part names included in the next bake
    [[nodiscard]] BakeMeshSet QuerySelectedSources() const;
    std::string InspectMapKey = "normal";
    std::string PreviewSetName, PreviewMapKey = "normal";
    // 📝 3D-preview texture choice: empty ("Shaded") keeps the plain Lambert-lit surface; any other value is a
    // baked map key, and ConstructViewport3D paints the mesh with that map's actual baked texels (sampled at
    // each triangle's real UVs, not a GPU texture - same "genuine CPU representation of the real data" approach
    // as ConstructMapPreview's 2D block raster) tinted by the same per-face Lambert shade. Native-only addition
    // requested directly by the user; the web reference always hardwires the bevel/tangent normal in 3D with no
    // picker, so there is no studioTheme.css/BakingStudio.jsx counterpart to port for this dropdown.
    std::string Preview3DMapKey;

    void SelectDemo(const std::string &Shape);
    void ClearResults() noexcept;

    //--------------------------------------------------------------------------------------------------------------
    //                                                 BAKE EXECUTION
    //--------------------------------------------------------------------------------------------------------------
    std::future<std::vector<BakePartResult>> BakeFuture;
    std::atomic<bool> BakeRunning{false};
    std::atomic<uint64_t> BakeDone{0}, BakeTotal{1};
    std::string BakeProgressLabel;
    std::vector<BakePartResult> Results;
    std::string Info, Error;
    // 📝 Device choice + the in-flight step PollBake walks: the CPU route is one worker future, the GPU route
    // is prepare-on-worker (flatten + rasterize) -> compute-via-host-hooks (banded, across frames) ->
    // encode-on-worker. Snapshots are retained for the whole bake so a GPU refusal at any step can restart
    // the identical bake on the CPU (FallbackToCpuBake) against the settings the user actually pressed Bake with.
    bool PreferGpuBake = false;
    enum class BakeStep { Idle, CpuBaking, GpuPreparing, GpuComputing, GpuEncoding };
    BakeStep ActiveBakeStep = BakeStep::Idle;
    std::future<std::shared_ptr<BakeFieldRequest>> GpuRequestFuture;
    std::shared_ptr<const BakeFieldRequest> GpuRequest;
    size_t GpuTargetIndex = 0;
    std::vector<BakePartResult> GpuMatchedResults;
    void PrepareGpuBake();
    BakeMeshSet BakeHighSnapshot;
    std::vector<BakePart> BakeTargetSnapshots;
    BakeSettings BakeSettingsSnapshot;
    std::string BakeDeviceUsed = "CPU"; // completion-line device attribution (GPU name, or the refusal that fell back)
    void StartCpuBake();
    void StartTiledBake();
    void FallbackToCpuBake(const std::string &Reason);
    void AcceptBakeResults();
    // 📝 1:1 with BakingStudio.jsx's `stale` flag: true once the inspector's settings have changed since the
    // snapshot StartBake() captured for the bake that produced Results - drives the "Previous bake · settings
    // changed" subtitle and the 2D map caption's "previous recipe" vs "current bake" wording.
    BakeSettings SettingsAtLastBake;
    [[nodiscard]] bool SettingsStale() const noexcept { return !Results.empty() && !SettingsFieldsEqual(Settings, SettingsAtLastBake); }
    void StartBake(bool IncludeVolume = true);
    void StartSelectedMapBake();
    void PollBake();
    // 📝 1:1 with BakingStudio.jsx's <FolderOpen>/saveRecipe() header buttons - SaveRecipe() writes a genuine
    // JSON settings snapshot (no vendored JSON library in this tree, so it is hand-serialized; every field is
    // real, not a stub). OpenRecipe reports that a native file-picker isn't wired into this build yet rather
    // than silently doing nothing, since there is no dialog library in this tree to back a real file pick.
    void SaveRecipe();
    void OpenRecipe();

    //--------------------------------------------------------------------------------------------------------------
    //                                                 SETTINGS UNDO/REDO (BakingStudio.jsx's travel())
    //--------------------------------------------------------------------------------------------------------------
    std::vector<BakeSettings> SettingsUndoStack, SettingsRedoStack;
    BakeSettings SettingsAtFrameStart;
    double SettingsCoalesceClockSeconds = 0.0, LastSettingsPushSeconds = -1000.0;
    static bool SettingsFieldsEqual(const BakeSettings &A, const BakeSettings &B) noexcept;
    void CoalesceSettingsUndo();
    void UndoSettings();
    void RedoSettings();


    //--------------------------------------------------------------------------------------------------------------
    //                                                 2D MAP PREVIEW (DRAW-LIST BLOCK RASTER) + PNG EXPORT
    //--------------------------------------------------------------------------------------------------------------
    bool ConstructMapPreview(PlaneExtent Region);
    std::filesystem::path ExportDirectory;
    std::string LastExportDirectory;
    std::future<std::string> ExportFuture;
    std::string PendingExportDirectory;
    void PollExport();
    void ExportResults();

    //--------------------------------------------------------------------------------------------------------------
    //                                                 3D ORBIT VIEWPORT (ImGui draw-list rasterization)
    //--------------------------------------------------------------------------------------------------------------
    // 📝 Reuses the real Material-workspace orbit camera (Engine/Render/MaterialPreviewConfiguration.h) -
    // the same Yaw/Pitch/Zoom/Pan fields and the same ConstructOrbitProjection() math
    // MaterialLibraryPanel's inspector preview drags - instead of a separately hand-rolled camera struct.
    // Baking has no fixed per-shape bounding table like Material's five catalogue shapes, so it supplies its
    // own live-measured demo-mesh radius/pivot to ConstructOrbitProjection() rather than reusing
    // ConstructMaterialPreviewProjection()'s Shape-indexed lookup, which is specific to Material's fixed
    // ShaderBall/Sphere/Plane/Cube/Cloth catalogue (and its thumbnail-atlas 16-unit shape spacing).
    // 📝 Pitch is 0.16 rather than the pre-port 0.35 because ConstructOrbitProjection already adds its own
    // .1908 rad "look slightly down" base elevation (the same Alloy capture-rig default Material's preview
    // starts at) - added together the two land at roughly the same starting elevation as before.
    MaterialPreviewConfiguration Camera{.Yaw = .7f, .Pitch = .16f};
    // 📝 Same orbit lag as MaterialLibraryPanel's inspector preview (see its SynchronizePreviewCamera): Camera
    // is the raw drag/wheel target, SmoothedCamera eases toward it every frame so the viewport glides instead
    // of snapping, using the identical exponential blend. Reset view therefore also glides, not snaps - same
    // feel as Material, since ResetCamera() only resets the target (see below).
    MaterialPreviewConfiguration SmoothedCamera;
    bool SmoothedCameraReady = false;
    void SynchronizeCamera();
    bool Overlay = false, Wireframe = false;
    std::string CachedViewportMeshKey;
    SurfaceGeometry CachedViewportGeometry;
    std::array<float, 3> CachedViewportPivot{0.f, 0.f, 0.f};
    float CachedViewportRadius = .85f;
    uint64_t BakeResultsRevision = 1;
    std::string CachedViewportMaterialKey;
    CompiledSurfaceMaterial CachedViewportMaterial;
    WorkspaceViewportOrbitCamera SentViewportCamera{};
    bool SentViewportCameraReady = false;
    uint32_t SentViewportWidth = 0, SentViewportHeight = 0;
    WorkspaceViewportOrbitCamera CachedCpuOrbit{};
    std::string CachedCpuStateKey;
    uint32_t CachedCpuWidth = 0, CachedCpuHeight = 0;
    std::vector<uint8_t> CachedCpuRgba;
    void ConstructViewport3D(PlaneExtent Region);
    void ResetCamera() noexcept { ResetMaterialPreviewView(Camera); }

    //--------------------------------------------------------------------------------------------------------------
    //                                                 LAYOUT SECTIONS
    //--------------------------------------------------------------------------------------------------------------
    float InterfaceScale = 1.f;
    void ConstructHeader();
    void ConstructAssetsDock();
    void ConstructPreviewDock();
    void ConstructInspector();
    void ConstructMeshMapGrid(float Width);
    void ApplyPreset(const std::string &Preset);
    // 📝 The reusable dockable-panel host every other TextureWorkspace panel (Material/Pattern/Stamp)
    // registers its own docks with - registered once in the constructor (see the .cpp), replacing Baking's
    // previous bespoke fixed-position SetNextWindowPos/Size layout for the Assets/Preview/Inspector docks.
    PanelDockspace Panels;

    std::map<std::string, PlaneExtent> ControlExtents;
    void Track(const std::string &Caption);

    // 📝 Same shared rounded-control surface Pattern/Stamp draw their cards, pill buttons and field rows
    // with (Engine/Interface/VectorAuthoringControls.h) - reused here instead of raw ImGui widgets so the
    // Baking workspace actually matches the rest of the app's "Alloy" design language, not a bespoke look.
    VectorAuthoringControlSurface Controls;
    void *RegularTypeface = nullptr, *LightTypeface = nullptr;
};
} // namespace Frontier
